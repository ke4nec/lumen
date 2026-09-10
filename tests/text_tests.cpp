// v0.3 阶段8B (plan §4 8B / §5.1): 文本契约测试。
//
// 覆盖：UTF-8 校验错误、grapheme 边界（ASCII/CJK/组合字符/emoji
// ZWJ/旗帜/肤色/变体选择符/CRLF/Hangul）、TextEditingValue 状态机
// （选区、词、IME preedit commit/cancel）、字体回退、TextLayout
// （换行/ellipsis/baseline/命中测试/RTL/缓存）、TextField 编辑交互
// （修饰键快捷键、剪贴板、双击选词、pointer cancel、焦点遍历、
// 只读/多行）、DSL 冻结属性解析。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/dsl/dsl.h"
#include "lumen/dsl/text_dsl.h"
#include "lumen/layout/layout.h"
#include "lumen/text/editing_value.h"
#include "lumen/text/font_manager.h"
#include "lumen/text/grapheme.h"
#include "lumen/text/text_layout.h"

using namespace lumen;
using namespace lumen::core;
using namespace lumen::dsl;
using namespace lumen::layout;
using namespace lumen::text;

namespace {

RenderNode layoutOf(const Widget& widget, float width = 300.0F,
                    float height = 200.0F) {
    return LayoutEngine::layout(widget,
                                Constraints::tight(Size{width, height}));
}

Offset centerOf(const RenderNode& root, const std::string& key) {
    const RenderNode* node = findNodeByKey(root, key);
    REQUIRE(node != nullptr);
    return absoluteOffset(root, key) +
           Offset{node->size.width * 0.5F, node->size.height * 0.5F};
}

// 测试用假剪贴板（core::ClipboardProvider）。
class FakeClipboard final : public ClipboardProvider {
  public:
    [[nodiscard]] bool hasText() const override { return !content.empty(); }
    [[nodiscard]] std::string text() const override { return content; }
    bool setText(const std::string& value) override {
        content = value;
        return true;
    }
    void clear() override { content.clear(); }
    std::string content{};
};

}  // namespace

// --- UTF-8 校验（plan §5.1: UTF-8 错误） ---

TEST_CASE("utf8_validation_accepts_valid_and_rejects_broken_input",
          "[text]") {
    CHECK(validateUtf8("hello").valid);
    CHECK(validateUtf8("\xE4\xBD\xA0\xE5\xA5\xBD").valid);  // 你好
    CHECK(validateUtf8("\xF0\x9F\x91\x8D").valid);          // 👍

    const Utf8Validation truncated = validateUtf8("\xE4\xBD");
    CHECK_FALSE(truncated.valid);
    CHECK(truncated.errorByteOffset == 0);

    const Utf8Validation badLead = validateUtf8("ab\xFF");
    CHECK_FALSE(badLead.valid);
    CHECK(badLead.errorByteOffset == 2);

    const Utf8Validation badContinuation = validateUtf8("\xE4\xBDz");
    CHECK_FALSE(badContinuation.valid);

    // 超长编码 'C' (0xC3 0x83 时代的 2 字节 ASCII)。
    CHECK_FALSE(validateUtf8("\xC0\x41").valid);
    // 代理区。
    CHECK_FALSE(validateUtf8("\xED\xA0\x80").valid);
    // 超出 U+10FFFF。
    CHECK_FALSE(validateUtf8("\xF5\x80\x80\x80").valid);
}

// --- grapheme 边界 ---

TEST_CASE("grapheme_boundaries_handle_clusters", "[text]") {
    SECTION("ascii is one cluster per char") {
        CHECK(graphemeCount("abc") == 3);
    }
    SECTION("cjk counts per character") {
        CHECK(graphemeCount("\xE4\xBD\xA0\xE5\xA5\xBD") == 2);  // 你好
    }
    SECTION("combining mark joins the base") {
        // e + U+0301 = é（1 cluster / 2 code points）。
        CHECK(graphemeCount("e\xCC\x81") == 1);
        // 词首 ASCII e 与组合符一起被切掉。
        CHECK(graphemeSubstring("e\xCC\x81x", 0, 1) == "e\xCC\x81");
    }
    SECTION("zwj emoji family is one cluster") {
        // 👨 ZWJ 👩 ZWJ 👧（8 code points / 1 cluster）。
        const std::string family =
            "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F"
            "\x91\xA7";
        CHECK(graphemeCount(family) == 1);
    }
    SECTION("regional indicators pair into flags") {
        // 🇨🇳🇺🇸 = 2 clusters / 4 code points。
        const std::string flags =
            "\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
        CHECK(graphemeCount(flags) == 2);
    }
    SECTION("skin tone modifier joins emoji") {
        // 👍 + 🏻 = 1 cluster。
        CHECK(graphemeCount("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBB") == 1);
    }
    SECTION("variation selector joins base") {
        // ❤ + FE0F = 1 cluster。
        CHECK(graphemeCount("\xE2\x9D\xA4\xEF\xB8\x8F") == 1);
    }
    SECTION("crlf is one cluster") {
        CHECK(graphemeCount("a\r\nb") == 3);
    }
    SECTION("hangul jamo sequence is one cluster") {
        CHECK(graphemeCount("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8") == 1);
    }
}

TEST_CASE("grapheme_offsets_roundtrip", "[text]") {
    const std::string mixed = "a\xF0\x9F\x91\x8D" "b";  // a 👍 b
    CHECK(graphemeCount(mixed) == 3);
    CHECK(graphemeSubstring(mixed, 1, 2) == "\xF0\x9F\x91\x8D");
    CHECK(graphemeByteOffset(mixed, 2) == 5);
    CHECK(graphemeIndexOfByte(mixed, 1) == 1);
    // cluster 内部偏移归入所在 cluster。
    CHECK(graphemeIndexOfByte(mixed, 3) == 1);
    // 越界夹取。
    CHECK(graphemeSubstring(mixed, 0, 99) == mixed);
}

TEST_CASE("word_ranges_split_ascii_and_symbols", "[text]") {
    const std::string phrase = "hello world";
    std::size_t end = 0;
    CHECK(wordRangeContaining(phrase, 1, &end) == 0);  // hello
    CHECK(end == 5);
    CHECK(wordRangeContaining(phrase, 7, &end) == 6);  // world
    CHECK(end == 11);
    // CJK 每 cluster 一个词。
    const std::string cjk = "\xE4\xBD\xA0\xE5\xA5\xBD";  // 你好
    CHECK(wordRangeContaining(cjk, 0, &end) == 0);
    CHECK(end == 1);
    // 标点退化为单 cluster。
    CHECK(wordRangeContaining("a,b", 1, &end) == 1);
    CHECK(end == 2);
}

// --- TextEditingValue 状态机 ---

TEST_CASE("editing_value_inserts_and_moves_by_grapheme", "[text]") {
    TextEditingValue value = TextEditingValue{"hi"}.moveCaretToEnd(false);
    const TextEditingValue after = value.insertText("\xF0\x9F\x91\x8D");
    CHECK(after.text() == "hi\xF0\x9F\x91\x8D");
    CHECK(after.caret() == 3);
    CHECK(after.hasSelection() == false);

    // 按 grapheme 左移：emoji 是一个单位。
    const TextEditingValue left = after.moveCaretLeft(false);
    CHECK(left.caret() == 2);
    // Shift 右移扩选。
    const TextEditingValue extended = left.moveCaretRight(true);
    CHECK(extended.selection().base == 2);
    CHECK(extended.selection().extent == 3);
    CHECK(extended.hasSelection());
    CHECK(extended.selectedText() == "\xF0\x9F\x91\x8D");
}

TEST_CASE("editing_value_deletes_by_grapheme", "[text]") {
    TextEditingValue value{"a\xF0\x9F\x91\x8D" "b"};  // a 👍 b
    // 光标移过 emoji（一个 grapheme 单位）后删除前一个 cluster。
    const TextEditingValue afterEmoji =
        value.moveCaretRight(false).moveCaretRight(false);
    CHECK(afterEmoji.caret() == 2);
    const TextEditingValue deleted = afterEmoji.deleteBackward();
    CHECK(deleted.text() == "ab");
    CHECK(deleted.caret() == 1);
}

TEST_CASE("editing_value_selection_replacement_and_select_all", "[text]") {
    TextEditingValue value{"hello"};
    const TextEditingValue selected = value.moveCaretToEnd(false)
                                          .moveCaretLeft(true)
                                          .moveCaretLeft(true);  // "he[llo]"
    CHECK(selected.selectedText() == "lo");
    const TextEditingValue replaced = selected.insertText("y");
    CHECK(replaced.text() == "hely");

    const TextEditingValue all = value.selectAll();
    CHECK(all.selectedText() == "hello");
    CHECK(all.insertText("x").text() == "x");
}

TEST_CASE("editing_value_word_selection", "[text]") {
    TextEditingValue value{"hello world"};
    const TextEditingValue word = value.selectWord(7);
    CHECK(word.selectedText() == "world");
    // 词级移动。
    const TextEditingValue endCaret = value.moveCaretToEnd(false);
    CHECK(endCaret.moveWordLeft(false).caret() == 6);
    CHECK(endCaret.moveWordLeft(false).moveWordLeft(false).caret() == 0);
    const TextEditingValue start = value.moveCaretToStart(false);
    CHECK(start.moveWordRight(false).caret() == 5);
}

TEST_CASE("editing_value_compose_commit_cancel", "[text]") {
    TextEditingValue value = TextEditingValue{"ni"}.moveCaretToEnd(false);
    // preedit 进入 composing：文档不变。
    const TextEditingValue composing = value.compose("hao");
    CHECK(composing.text() == "ni");
    CHECK(composing.composingActive());
    CHECK(composing.composing().start() == 2);
    CHECK(composing.composing().end() == 5);
    // preedit 更新。
    const TextEditingValue updated = composing.compose("h");
    CHECK(updated.composing().end() == 3);
    // 提交：文档替换 composing 区间。
    const TextEditingValue committed = updated.commitComposition("好");
    CHECK(committed.text() == "ni好");
    CHECK(committed.caret() == 3);
    CHECK_FALSE(committed.composingActive());
    // 取消：恢复进入前选区。
    const TextEditingValue cancelled = composing.cancelComposition();
    CHECK(cancelled.text() == "ni");
    CHECK(cancelled.caret() == 2);
}

// --- 字体回退 ---

TEST_CASE("font_manager_falls_back_across_families", "[text]") {
    const PlaceholderFontManager& fonts = PlaceholderFontManager::shared();
    FontQuery query;  // 请求任意族
    CHECK(fonts.resolveFamily(query, 'a') == "lumen-latin");
    CHECK(fonts.resolveFamily(query, 0x4F60 /*你*/) == "lumen-cjk");
    CHECK(fonts.resolveFamily(query, 0x1F44D /*👍*/) == "lumen-emoji");
    GlyphMetrics metrics{};
    CHECK(fonts.glyphMetrics(query, 'a', &metrics));
    CHECK(metrics.advanceEm == 0.6F);
    const auto families = fonts.availableFamilies();
    REQUIRE(families.size() == 3);
}

// --- TextLayout ---

TEST_CASE("text_layout_measures_and_wraps", "[text]") {
    const core::TextStyle style;  // 14px, 1.2 行高
    // ASCII：0.6em/cluster。
    const auto single = TextLayout::layout("hello", style, 0.0F,
                                           PlaceholderFontManager::shared());
    CHECK(single.lines.size() == 1);
    CHECK(single.size.width == 14.0F * 0.6F * 5.0F);
    CHECK(single.size.height == 14.0F * 1.2F);
    CHECK(single.baseline == 14.0F * 0.8F);

    // "aa bb" 限制到 3 个字符宽：换行在空白处。
    const auto wrapped = TextLayout::layout(
        "aa bb", style, 14.0F * 0.6F * 3.0F,
        PlaceholderFontManager::shared());
    REQUIRE(wrapped.lines.size() == 2);
    CHECK(wrapped.lines[0].visual == "aa ");
    CHECK(wrapped.lines[1].visual == "bb");

    // CJK 可在任意字间断行。
    const std::string cjk = "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C";
    const auto cjkWrapped =
        TextLayout::layout(cjk, style, 14.0F * 2.0F,
                           PlaceholderFontManager::shared());
    CHECK(cjkWrapped.lines.size() == 2);
}

TEST_CASE("text_layout_ellipsizes_overflow", "[text]") {
    core::TextStyle style;
    style.maxLines = 1;
    style.overflow = core::TextOverflow::Ellipsis;
    // 10 字符宽文本限 4 字符宽：保留 3 字符 + …。
    const auto result = TextLayout::layout(
        "0123456789", style, 14.0F * 0.6F * 4.0F,
        PlaceholderFontManager::shared());
    REQUIRE(result.lines.size() == 1);
    CHECK(result.ellipsized);
    // 3 clusters + ellipsis = 4 clusters 宽。
    CHECK(result.lines[0].visual == "012\xE2\x80\xA6");
    CHECK(result.lines[0].width <= 14.0F * 0.6F * 4.0F);

    // maxLines 截断多行。
    core::TextStyle two;
    two.maxLines = 2;
    const auto clipped = TextLayout::layout(
        "aa aa aa", two, 14.0F * 0.6F * 2.5F,
        PlaceholderFontManager::shared());
    CHECK(clipped.lines.size() == 2);
}

TEST_CASE("text_layout_hit_test_and_positions", "[text]") {
    const core::TextStyle style;
    const auto layout = TextLayout::layout("abcd", style, 0.0F,
                                           PlaceholderFontManager::shared());
    const float advance = 14.0F * 0.6F;
    // 命中 cluster 边界。
    CHECK(layout.positionToGrapheme(advance * 0.4F, 0.0F) == 0);
    CHECK(layout.positionToGrapheme(advance * 0.6F, 0.0F) == 1);
    CHECK(layout.positionToGrapheme(advance * 3.5F, 0.0F) == 4);
    // grapheme → x。
    CHECK(layout.graphemeToX(2) == advance * 2.0F);
    CHECK(layout.graphemeToX(4) == advance * 4.0F);
}

TEST_CASE("text_layout_rtl_reorders_visual", "[text]") {
    core::TextStyle style;
    style.direction = core::TextDirection::Rtl;
    // 希伯来语 "שלום"（4 clusters）。
    const std::string hebrew =
        "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
    const auto layout = TextLayout::layout(hebrew, style, 0.0F,
                                           PlaceholderFontManager::shared());
    REQUIRE(layout.lines.size() == 1);
    // 视觉序 = 逻辑逆序。
    CHECK(layout.lines[0].visual ==
          "\xD7\x9D\xD7\x95\xD7\x9C\xD7\xA9");
    // 逻辑 cluster 0 起始在最右。
    const float width = layout.lines[0].width;
    CHECK(layout.lines[0].graphemeX[0] > layout.lines[0].graphemeX[3]);
    CHECK(layout.lines[0].graphemeX[0] + 14.0F * 0.6F ==
          Catch::Approx(width).epsilon(0.001F));
    // 命中测试按视觉 x 给出逻辑边界。
    CHECK(layout.positionToGrapheme(width * 0.9F, 0.0F) == 0);
    CHECK(layout.positionToGrapheme(width * 0.1F, 0.0F) == 4);
}

TEST_CASE("text_layout_cache_counts_hits_and_misses", "[text]") {
    TextLayoutCache cache;
    const core::TextStyle style;
    const auto& fonts = PlaceholderFontManager::shared();
    (void)cache.compute("same", style, 100.0F, fonts);
    (void)cache.compute("same", style, 100.0F, fonts);
    (void)cache.compute("other", style, 100.0F, fonts);
    // 样式或宽度不同即不同键。
    core::TextStyle bigger;
    bigger.fontSize = 20.0F;
    (void)cache.compute("same", bigger, 100.0F, fonts);
    CHECK(cache.stats().misses == 3);
    CHECK(cache.stats().hits == 1);
    cache.clear();
    CHECK(cache.stats().entries == 0);
}

// --- TextField 编辑交互 ---

TEST_CASE("textfield_edits_with_modifiers_and_clipboard", "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    FakeClipboard clipboard;
    controller.setClipboard(&clipboard);
    store.set("f", "hello world");

    Widget ui = makeContainer(
        withKey(makeTextField(store.get("f"), {}, {}, {}, 0.0F, "field"),
                "field"));
    ui.children[0].bind = "f";
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "field"));
    REQUIRE(controller.wantsTextInput());

    // Ctrl+A 全选 → Ctrl+C 复制。
    controller.keyDown(Key::None, kModifierCtrl, 'a');
    CHECK(controller.hasSelection());
    CHECK(controller.selectionStart() == 0);
    CHECK(controller.selectionEnd() == 11);
    controller.keyDown(Key::None, kModifierCtrl, 'c');
    CHECK(clipboard.text() == "hello world");

    // Ctrl+V 覆盖选区。
    clipboard.content = "lumen";
    controller.keyDown(Key::None, kModifierCtrl, 'v');
    CHECK(store.get("f") == "lumen");

    // Shift+Left 扩选 + Ctrl+X 剪切。
    controller.keyDown(Key::End);
    controller.keyDown(Key::Left, kModifierShift);
    CHECK(controller.hasSelection());
    controller.keyDown(Key::None, kModifierCtrl, 'x');
    CHECK(clipboard.text() == "n");
    CHECK(store.get("f") == "lume");

    // Ctrl+Right 词右移动："lume" 整词到词尾（4）。
    controller.keyDown(Key::Home);
    controller.keyDown(Key::Right, kModifierCtrl);
    CHECK(controller.caretGraphemes() == 4);
}

TEST_CASE("textfield_double_click_selects_word_and_drag_extends",
          "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("f", "alpha beta");

    Widget ui = makeContainer(
        withKey(makeTextField(store.get("f"), {}, {}, {}, 0.0F, "field"),
                "field"));
    ui.children[0].bind = "f";
    const RenderNode root = layoutOf(ui);

    // 命中点取字段右段（"beta" 一侧），避开中间的空白。
    const auto* field = findNodeByKey(root, "field");
    REQUIRE(field != nullptr);
    const Offset onBeta =
        absoluteOffset(root, "field") +
        Offset{field->size.width * 0.75F, field->size.height * 0.5F};

    controller.pointerDown(root, onBeta, 100);
    controller.pointerUp(root, onBeta);
    // 第二次点击（双击窗口内，同一字段）选词。
    controller.pointerDown(root, onBeta, 200);
    CHECK(controller.hasSelection());
    CHECK(controller.editingValue().selectedText() == "beta");
}

TEST_CASE("pointer_cancel_releases_press_without_click",
          "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    int clicks = 0;
    handlers["fire"] = [&clicks] { ++clicks; };
    store.set("f", "");

    Widget ui = makeColumn({
        withKey(makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt,
                           std::nullopt, "fire"),
                "btn"),
        withKey(makeTextField("", {}, {}, {}, 0.0F, "field"), "field"),
    });
    ui.children[1].bind = "f";
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "btn"));
    CHECK(controller.pressedKey() == "btn");
    controller.pointerCancel();
    CHECK(controller.pressedKey().empty());
    CHECK_FALSE(controller.isDragging());
    // 取消后的释放不触发点击。
    controller.pointerUp(root, centerOf(root, "btn"));
    CHECK(clicks == 0);
}

TEST_CASE("tab_traverses_focus_between_fields_and_buttons",
          "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    int clicks = 0;
    handlers["fire"] = [&clicks] { ++clicks; };
    store.set("a", "");
    store.set("b", "");

    Widget ui = makeColumn({
        withKey(makeTextField("", {}, {}, {}, 0.0F, "field-a"), "field-a"),
        withKey(makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt,
                           std::nullopt, "fire"),
                "btn"),
        withKey(makeTextField("", {}, {}, {}, 0.0F, "field-b"), "field-b"),
    });
    ui.children[0].bind = "a";
    ui.children[2].bind = "b";
    const RenderNode root = layoutOf(ui);

    // 点击第一个字段聚焦。
    controller.pointerDown(root, centerOf(root, "field-a"));
    CHECK(controller.focusedBind() == "a");
    // Tab → Button。
    controller.keyDown(root, Key::Tab);
    CHECK(focus.focusedKey() == "btn");
    CHECK_FALSE(controller.wantsTextInput());
    // Enter 激活按钮。
    controller.keyDown(root, Key::Enter);
    CHECK(clicks == 1);
    // Tab → 第二个字段。
    controller.keyDown(root, Key::Tab);
    CHECK(controller.focusedBind() == "b");
    // Shift-Tab 回到按钮。
    controller.keyDown(root, Key::Tab, kModifierShift);
    CHECK(focus.focusedKey() == "btn");
    // Backtab（无修饰）同样反向：按钮 → 第一个字段。
    controller.keyDown(root, Key::Backtab);
    CHECK(controller.focusedBind() == "a");
}

TEST_CASE("readonly_field_rejects_edits_but_allows_selection",
          "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("f", "fixed");

    Widget ui = makeContainer(withReadOnly(withKey(
        makeTextField("fixed", {}, {}, {}, 0.0F, "field"), "field")));
    ui.children[0].bind = "f";
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "field"));
    REQUIRE(controller.wantsTextInput());
    CHECK(controller.focusedReadOnly());
    controller.textInput("x");
    controller.keyDown(Key::Backspace);
    CHECK(store.get("f") == "fixed");
    // 选区与全选允许（复制用途）。
    controller.keyDown(Key::None, kModifierCtrl, 'a');
    CHECK(controller.hasSelection());
}

TEST_CASE("multiline_field_enter_inserts_newline_single_line_defocuses",
          "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("m", "");
    store.set("s", "");

    Widget ui = makeColumn({
        withMultiline(withKey(makeTextField("", {}, {}, {}, 0.0F, "multi"),
                              "multi")),
        withKey(makeTextField("", {}, {}, {}, 0.0F, "single"), "single"),
    });
    ui.children[0].bind = "m";
    ui.children[1].bind = "s";
    const RenderNode root = layoutOf(ui);

    // 多行：Enter 换行。
    controller.pointerDown(root, centerOf(root, "multi"));
    controller.textInput("a");
    controller.keyDown(Key::Enter);
    controller.textInput("b");
    CHECK(store.get("m") == "a\nb");

    // 单行：Enter 释放焦点。
    controller.pointerDown(root, centerOf(root, "single"));
    controller.keyDown(Key::Enter);
    CHECK_FALSE(controller.wantsTextInput());
}

TEST_CASE("ime_composition_flow_through_controller", "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("f", "");

    Widget ui = makeContainer(
        withKey(makeTextField("", {}, {}, {}, 0.0F, "field"), "field"));
    ui.children[0].bind = "f";
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "field"));
    // preedit 不落文档。
    controller.setComposition("ni");
    controller.setComposition("nihao");
    CHECK(store.get("f") == "");
    CHECK(controller.composingActive());
    CHECK(controller.composition() == "nihao");
    // 提交：TextInput 到达时先结束 preedit。
    controller.textInput("你好");
    CHECK(store.get("f") == "你好");
    CHECK_FALSE(controller.composingActive());
    CHECK(controller.composition().empty());
    CHECK(controller.caretGraphemes() == 2);
    // Escape 取消未提交的 preedit。
    controller.setComposition("zai");
    controller.keyDown(Key::Escape);
    CHECK(store.get("f") == "你好");
    CHECK(controller.composition().empty());
}

// --- DSL 冻结属性 ---

TEST_CASE("dsl_parses_frozen_text_attributes", "[text][dsl]") {
    const auto parsed = parseLumen(
        "page root {\n"
        "  Column {\n"
        "    Text(\"hello\", family: \"serif\", weight: 700, italic: true, "
        "letterSpacing: 1.5, lineHeight: 1.4, maxLines: 2, "
        "overflow: ellipsis)\n"
        "    TextField(placeholder: \"hint\", obscure: true, "
        "readOnly: true, multiline: true)\n"
        "  }\n"
        "}",
        "attrs.lumen");
    REQUIRE(parsed.ok());
    // 文档根即 Column；Text/TextField 是它的两个子节点。
    REQUIRE(parsed.root.children.size() == 2);
    const auto& styled = parsed.root.children[0];
    CHECK(styled.textStyle.family == "serif");
    CHECK(styled.textStyle.weight == 700);
    CHECK(styled.textStyle.italic);
    CHECK(styled.textStyle.letterSpacing == 1.5F);
    CHECK(styled.textStyle.lineHeight == 1.4F);
    CHECK(styled.textStyle.maxLines == 2);
    CHECK(styled.textStyle.overflow == core::TextOverflow::Ellipsis);
    const auto& field = parsed.root.children[1];
    CHECK(field.obscure);
    CHECK(field.readOnly);
    CHECK(field.multiline);
}

TEST_CASE("dsl_rejects_bad_frozen_attribute_values", "[text][dsl]") {
    const auto weight =
        parseLumen("page root { Text(\"x\", weight: 450) }");
    CHECK_FALSE(weight.ok());
    const auto overflow =
        parseLumen("page root { Text(\"x\", overflow: blink) }");
    CHECK_FALSE(overflow.ok());
    // obscure 只对 TextField 有效。
    const auto misplaced =
        parseLumen("page root { Button(\"x\", obscure: true) }");
    CHECK_FALSE(misplaced.ok());
}
