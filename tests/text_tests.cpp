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

#include <algorithm>
#include <string>

#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/dsl/dsl.h"
#include "lumen/dsl/text_dsl.h"
#include "lumen/layout/layout.h"
#include "lumen/text/bidi.h"
#include "lumen/text/editing_history.h"
#include "lumen/text/editing_value.h"
#include "lumen/text/font_manager.h"
#include "lumen/text/grapheme.h"
#include "lumen/text/skia_font_manager.h"
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

TEST_CASE("ime_cancel_restores_selection_after_preedit_updates",
          "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("f", "abcd");
    Widget ui = makeContainer(
        withKey(makeTextField("abcd", {}, {}, {}, 0.0F, "field"), "field"));
    ui.children[0].bind = "f";
    const RenderNode root = layoutOf(ui);
    controller.pointerDown(root, centerOf(root, "field"));
    controller.keyDown(Key::End);
    controller.keyDown(Key::Left, kModifierShift);
    controller.keyDown(Key::Left, kModifierShift);
    REQUIRE(controller.selectionStart() == 2);
    REQUIRE(controller.selectionEnd() == 4);
    controller.setComposition("ni");
    controller.setComposition("nihao");
    SECTION("explicit cancellation") { controller.cancelComposition(); }
    SECTION("native cancellation event") { controller.setComposition(""); }
    SECTION("escape") { controller.keyDown(Key::Escape); }
    CHECK_FALSE(controller.composingActive());
    CHECK(controller.composition().empty());
    CHECK(store.get("f") == "abcd");
    CHECK(controller.selectionStart() == 2);
    CHECK(controller.selectionEnd() == 4);
    CHECK(controller.caretGraphemes() == 2);
    controller.cancelComposition();
    CHECK(controller.selectionStart() == 2);
    CHECK(controller.selectionEnd() == 4);
    controller.textInput("!");
    CHECK(store.get("f") == "ab!");
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

// --- M1 双向段落处理（UAX#9 确定性子集） ---

TEST_CASE("bidi_classifies_strong_and_neutral_scripts", "[text][bidi]") {
    // 希伯来（强 R）。
    CHECK(bidiClassFor(0x05D0) == BidiClass::R);
    CHECK(bidiClassFor(0xFB1D) == BidiClass::R);
    // 阿拉伯（AL 视同 R）与阿拉伯指示数字（AN 视同 R）。
    CHECK(bidiClassFor(0x0627) == BidiClass::R);
    CHECK(bidiClassFor(0x0660) == BidiClass::R);
    // ASCII 数字（EN 视同 L）、拉丁/CJK/emoji 强 L。
    CHECK(bidiClassFor('7') == BidiClass::L);
    CHECK(bidiClassFor('a') == BidiClass::L);
    CHECK(bidiClassFor(0x4F60) == BidiClass::L);  // 你
    CHECK(bidiClassFor(0x1F600) == BidiClass::L);  // 😀
    // 空白与标点中性。
    CHECK(bidiClassFor(' ') == BidiClass::Neutral);
    CHECK(bidiClassFor('.') == BidiClass::Neutral);
    CHECK(bidiClassFor(0x3000) == BidiClass::Neutral);  // 全角空格
}

TEST_CASE("bidi_runs_split_by_strong_direction", "[text][bidi]") {
    // LTR 基准段：L R L → 三个 run（level 0/1/0）。
    const std::vector<BidiClass> mixed{
        BidiClass::L, BidiClass::R, BidiClass::R, BidiClass::L};
    const auto runs = resolveBidiRuns(mixed, false);
    REQUIRE(runs.size() == 3);
    CHECK(runs[0] == BidiRun{0, 1, 0});
    CHECK(runs[1] == BidiRun{1, 2, 1});
    CHECK(runs[2] == BidiRun{3, 1, 0});

    // RTL 基准段：R 段 level 1，L 段 level 2（反向嵌入）。
    const auto rtlBase = resolveBidiRuns(mixed, true);
    REQUIRE(rtlBase.size() == 3);
    CHECK(rtlBase[0] == BidiRun{0, 1, 2});
    CHECK(rtlBase[1] == BidiRun{1, 2, 1});
    CHECK(rtlBase[2] == BidiRun{3, 1, 2});

    // 中性消解：两侧强方向一致取该方向，否则取段落方向。
    const std::vector<BidiClass> withNeutral{
        BidiClass::L, BidiClass::Neutral, BidiClass::L,
        BidiClass::Neutral, BidiClass::R};
    const auto resolved = resolveBidiRuns(withNeutral, false);
    // 中性 1 两侧皆 L → L；中性 3 两侧 L/R → 段落方向 L。
    REQUIRE(resolved.size() == 2);
    CHECK(resolved[0] == BidiRun{0, 4, 0});
    CHECK(resolved[1] == BidiRun{4, 1, 1});
}

TEST_CASE("bidi_visual_order_reverses_rtl_segments", "[text][bidi]") {
    // LTR 段落：R 段逆序，L 段保持。
    const std::vector<BidiClass> mixed{
        BidiClass::L, BidiClass::L, BidiClass::L,
        BidiClass::R, BidiClass::R, BidiClass::R,
        BidiClass::L, BidiClass::L, BidiClass::L};
    const auto runs = resolveBidiRuns(mixed, false);
    const auto order = visualOrder(mixed.size(), runs);
    REQUIRE(order.size() == 9);
    CHECK(order == std::vector<std::size_t>{0, 1, 2, 5, 4, 3, 6, 7, 8});

    // 纯 RTL 段落：整行逆序。
    const std::vector<BidiClass> allR{BidiClass::R, BidiClass::R,
                                      BidiClass::R, BidiClass::R};
    const auto rtlRuns = resolveBidiRuns(allR, true);
    CHECK(visualOrder(4, rtlRuns) == std::vector<std::size_t>{3, 2, 1, 0});

    // RTL 段落中的 L 段（level 2）：整体逆序后 L 段内部恢复正序。
    const std::vector<BidiClass> rtlWithL{
        BidiClass::R, BidiClass::L, BidiClass::L, BidiClass::R};
    const auto nested = resolveBidiRuns(rtlWithL, true);
    // level: R=1, L=2, R=1 → 视觉：R(3) L(1,2 正序) R(0)。
    CHECK(visualOrder(4, nested) == std::vector<std::size_t>{3, 1, 2, 0});

    // 全 L：不变。
    const std::vector<BidiClass> allL(3, BidiClass::L);
    const auto lRuns = resolveBidiRuns(allL, false);
    CHECK(visualOrder(3, lRuns) == std::vector<std::size_t>{0, 1, 2});
}

TEST_CASE("text_layout_mixed_direction_keeps_grapheme_indices",
          "[text][text][bidi]") {
    core::TextStyle style;  // LTR 基准
    // "abc" + "שלום" + "123"。
    const std::string mixed =
        "abc\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"
        "123";
    const auto layout = TextLayout::layout(mixed, style, 0.0F,
                                           PlaceholderFontManager::shared());
    REQUIRE(layout.lines.size() == 1);
    const auto& line = layout.lines[0];
    REQUIRE(line.graphemeCount == 10);
    // 视觉序：abc + 逆序希伯来 + 123。
    std::string visual;
    for (std::size_t i = 0; i < line.graphemeCount; ++i) {
        visual += graphemeSubstring(mixed, i, i + 1);
    }
    std::string expected = "abc";
    for (std::size_t i = 0; i < 4; ++i) {
        expected += graphemeSubstring(mixed, 6 - i, 7 - i);  // 逆序 3..6
    }
    expected += "123";
    CHECK(line.visual == expected);

    // Shaped runs 的 cluster 保持逻辑索引（编辑索引不随重排改变）。
    std::vector<std::uint32_t> clusters;
    for (const auto& run : line.runs) {
        for (const auto& glyph : run.glyphs) {
            clusters.push_back(glyph.cluster);
        }
    }
    std::sort(clusters.begin(), clusters.end());
    std::vector<std::uint32_t> expectedClusters;
    for (std::uint32_t i = 0; i < 10; ++i) {
        expectedClusters.push_back(i);
    }
    CHECK(clusters == expectedClusters);

    // 命中测试：最左给逻辑 0，最右给逻辑 10（段落 LTR）。
    CHECK(layout.positionToGrapheme(0.0F, 0.0F) == 0);
    CHECK(layout.positionToGrapheme(line.width, 0.0F) == 10);
    // 希伯来段逻辑 6 的左边缘 = 其视觉位置（abc 之后）。
    const float hebrewStart = line.graphemeX[6];
    CHECK(layout.positionToGrapheme(hebrewStart, 0.0F) == 6);
}

TEST_CASE("text_layout_shaped_runs_describe_placeholder_and_ellipsis",
          "[text][text]") {
    core::TextStyle style;
    style.letterSpacing = 1.0F;
    const auto layout = TextLayout::layout(
        "hi", style, 0.0F, PlaceholderFontManager::shared());
    REQUIRE(layout.lines.size() == 1);
    const auto& line = layout.lines[0];
    REQUIRE(line.runs.size() == 1);
    CHECK(line.runs[0].placeholder);
    CHECK(line.runs[0].family == "lumen-latin");
    REQUIRE(line.runs[0].glyphs.size() == 2);
    // 占位路径 glyphId = 码点；字形 advance 不含 letterSpacing（布局叠加
    // 到 cluster 间距）；cluster = 逻辑位。
    CHECK(line.runs[0].glyphs[0].glyphId == 'h');
    CHECK(line.runs[0].glyphs[0].advancePx ==
          Catch::Approx(14.0F * 0.6F).epsilon(0.001F));
    CHECK(line.runs[0].glyphs[0].cluster == 0);
    CHECK(line.runs[0].glyphs[1].cluster == 1);
    CHECK(line.runs[0].glyphs[0].xOffsetPx == 0.0F);
    // cluster 间距含 letterSpacing（0.6em + 1px）。
    CHECK(line.graphemeX[1] - line.graphemeX[0] ==
          Catch::Approx(14.0F * 0.6F + 1.0F).epsilon(0.001F));
    // baseline/行高仍按占位水平度量（0.8em ascent）。
    CHECK(layout.baseline == Catch::Approx(14.0F * 0.8F).epsilon(0.001F));
    CHECK(layout.fontBackend == FontBackend::Placeholder);
    CHECK(layout.usedPlaceholderFallback);

    // ellipsis：合成省略号的 cluster 指向行尾逻辑位（graphemeCount）。
    core::TextStyle ellipsisStyle;
    ellipsisStyle.overflow = core::TextOverflow::Ellipsis;
    ellipsisStyle.maxLines = 1;
    const auto trimmed = TextLayout::layout(
        "hello world", ellipsisStyle, 14.0F * 0.6F * 4.0F,
        PlaceholderFontManager::shared());
    REQUIRE(trimmed.lines.size() == 1);
    REQUIRE(trimmed.ellipsized);
    const auto& ellipsisRun = trimmed.lines[0].runs.back();
    REQUIRE_FALSE(ellipsisRun.glyphs.empty());
    const std::uint32_t lastCluster =
        ellipsisRun.glyphs.back().cluster;
    // 行内 cluster 位（相对整段）：行起始 + 行内 cluster 数 - 1。
    CHECK(lastCluster ==
          trimmed.lines[0].startGrapheme + trimmed.lines[0].graphemeCount -
              1);
}

// --- M1 undo/redo（EditingHistory + InteractionController） ---

TEST_CASE("editing_history_merges_runs_and_branches", "[text][history]") {
    EditingHistory history;
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());

    history.seed(TextEditingValue{"", TextSelection{0, 0}});
    CHECK_FALSE(history.canUndo());
    // 连续单字输入合并为一个 undo 项。
    for (char c : std::string("abc")) {
        const std::string next(1, c);
        history.push(TextEditingValue{next, TextSelection{1, 1}},
                     EditKind::Insert);
    }
    // 直接构造连续输入序列（值序列模拟 caret 前进）。
    EditingHistory typed;
    typed.seed(TextEditingValue{"", TextSelection{0, 0}});
    typed.push(TextEditingValue{"a", TextSelection{1, 1}},
               EditKind::Insert);
    typed.push(TextEditingValue{"ab", TextSelection{2, 2}},
               EditKind::Insert);
    typed.push(TextEditingValue{"abc", TextSelection{3, 3}},
               EditKind::Insert);
    CHECK(typed.undoSize() == 1);
    CHECK(typed.canUndo());
    const auto undone = typed.undo();
    REQUIRE(undone.has_value());
    CHECK(undone->text().empty());
    CHECK_FALSE(typed.canUndo());
    CHECK(typed.canRedo());
    const auto redone = typed.redo();
    REQUIRE(redone.has_value());
    CHECK(redone->text() == "abc");
    CHECK(redone->selection().collapsed());
    CHECK(redone->caret() == 3);

    // 分支：undo 后新编辑丢弃 redo。
    const auto mid = typed.undo();
    REQUIRE(mid.has_value());
    typed.push(TextEditingValue{"z", TextSelection{1, 1}},
               EditKind::Insert);
    CHECK_FALSE(typed.canRedo());

    // 连续单字删除合并。
    EditingHistory deleted;
    deleted.seed(TextEditingValue{"ab", TextSelection{2, 2}});
    deleted.push(TextEditingValue{"a", TextSelection{1, 1}},
                 EditKind::Delete);
    deleted.push(TextEditingValue{"", TextSelection{0, 0}},
                 EditKind::Delete);
    CHECK(deleted.undoSize() == 1);
    CHECK(deleted.undo()->text() == "ab");

    // 选区移动不进栈，但打断合并。
    EditingHistory moved;
    moved.seed(TextEditingValue{"ab", TextSelection{2, 2}});
    moved.push(TextEditingValue{"a", TextSelection{1, 1}},
               EditKind::Delete);
    moved.push(TextEditingValue{"a", TextSelection{0, 0}},
               EditKind::Selection);
    moved.push(TextEditingValue{"", TextSelection{0, 0}},
               EditKind::Delete);
    CHECK(moved.undoSize() == 2);
}

TEST_CASE("editing_history_transaction_is_single_entry", "[text][history]") {
    EditingHistory history;
    history.seed(TextEditingValue{"", TextSelection{0, 0}});
    history.beginTransaction();
    history.push(TextEditingValue{"a", TextSelection{1, 1}},
                 EditKind::Insert);
    history.push(TextEditingValue{"abc", TextSelection{3, 3}},
                 EditKind::Insert);
    history.endTransaction();
    CHECK(history.undoSize() == 1);
    const auto undone = history.undo();
    REQUIRE(undone.has_value());
    CHECK(undone->text().empty());

    // 嵌套事务：外层 end 才提交。
    history.redo();
    history.beginTransaction();
    history.beginTransaction();
    history.push(TextEditingValue{"abcd", TextSelection{4, 4}},
                 EditKind::Insert);
    history.endTransaction();
    CHECK(history.undoSize() == 1);  // 仍挂起未入栈，深度不变。
    history.endTransaction();
    CHECK(history.undoSize() == 2);
}

TEST_CASE("editing_history_capacity_trims_oldest", "[text][history]") {
    EditingHistory history;
    history.seed(TextEditingValue{"0", TextSelection{1, 1}});
    for (int i = 1; i <= static_cast<int>(EditingHistory::kCapacity) + 5;
         ++i) {
        history.push(TextEditingValue{std::to_string(i * 11),
                                      TextSelection{2, 2}},
                     EditKind::Other);
    }
    // 超容量后最旧条目被淘汰，undo 深度不超过 kCapacity。
    CHECK(history.undoSize() <= EditingHistory::kCapacity);
    CHECK(history.undoSize() >= EditingHistory::kCapacity - 1);
}

TEST_CASE("textfield_undo_redo_through_controller", "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("f", "");

    Widget ui = makeContainer(
        withKey(makeTextField(store.get("f"), {}, {}, {}, 0.0F, "field"),
                "field"));
    ui.children[0].bind = "f";
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "field"));
    REQUIRE(controller.wantsTextInput());
    CHECK_FALSE(controller.canUndo());

    // 单字输入合并：两次 undo 回到空串。
    controller.textInput("a");
    controller.textInput("b");
    controller.textInput("c");
    CHECK(store.get("f") == "abc");
    CHECK(controller.canUndo());
    controller.keyDown(Key::None, kModifierCtrl, 'z');
    CHECK(store.get("f").empty());
    CHECK(controller.caretGraphemes() == 0);
    CHECK_FALSE(controller.canUndo());
    CHECK(controller.canRedo());

    // Ctrl+Shift+Z 与 Ctrl+Y 均为重做。
    controller.keyDown(Key::None, kModifierCtrl | kModifierShift, 'z');
    CHECK(store.get("f") == "abc");
    controller.keyDown(Key::None, kModifierCtrl, 'z');
    CHECK(store.get("f").empty());
    controller.keyDown(Key::None, kModifierCtrl, 'y');
    CHECK(store.get("f") == "abc");
    CHECK_FALSE(controller.canRedo());

    // 分支：undo 后输入新内容，redo 分支被丢弃。
    controller.keyDown(Key::None, kModifierCtrl, 'z');
    controller.textInput("x");
    CHECK_FALSE(controller.canRedo());
    CHECK(store.get("f") == "x");

    // 删除可撤销（Backspace 合并为一个项）。
    controller.keyDown(Key::None, kModifierCtrl, 'z');
    CHECK(store.get("f").empty());
}

TEST_CASE("textfield_ime_commit_is_single_undo_entry",
          "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("f", "");

    Widget ui = makeContainer(
        withKey(makeTextField(store.get("f"), {}, {}, {}, 0.0F, "field"),
                "field"));
    ui.children[0].bind = "f";
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "field"));
    // IME：preedit 更新不进栈；commit 作为单个 Other 项一次撤销。
    controller.setComposition("ni");
    controller.setComposition("你好");
    CHECK(store.get("f").empty());
    CHECK_FALSE(controller.canUndo());
    controller.commitComposition("你好");
    CHECK(store.get("f") == "你好");
    CHECK(controller.canUndo());
    controller.keyDown(Key::None, kModifierCtrl, 'z');
    CHECK(store.get("f").empty());
    // preedit 期间 undo 不响应（返回后正常）。
    controller.textInput("a");
    controller.setComposition("h");
    controller.keyDown(Key::None, kModifierCtrl, 'z');
    CHECK(store.get("f") == "a");
    controller.cancelComposition();
    controller.keyDown(Key::None, kModifierCtrl, 'z');
    CHECK(store.get("f").empty());
}

TEST_CASE("readonly_field_rejects_undo_redo", "[text][interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("f", "locked");

    Widget field = makeTextField(store.get("f"), {}, {}, {}, 0.0F, "field");
    field.readOnly = true;
    Widget ui = makeContainer(withKey(std::move(field), "field"));
    ui.children[0].bind = "f";
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "field"));
    controller.keyDown(Key::None, kModifierCtrl, 'z');
    CHECK(store.get("f") == "locked");
    controller.keyDown(Key::None, kModifierCtrl, 'y');
    CHECK(store.get("f") == "locked");
}

// --- M1 Skia FontManager 工厂（CPU-only 与 Skia 构建都可运行） ---

TEST_CASE("default_font_stack_is_platform_aware", "[text][fonts]") {
    clearDefaultFontStackOverride();
    const auto stack = defaultFontStack();
    CHECK_FALSE(stack.empty());
    CHECK_FALSE(defaultFontFamily().empty());
    CHECK(defaultFontFamily() == stack.front());

    const auto latin = defaultFontStackFor(U'A');
    const auto cjk = defaultFontStackFor(0x4E2D);
    const auto emoji = defaultFontStackFor(0x1F600);
    CHECK_FALSE(latin.empty());
    CHECK_FALSE(cjk.empty());
    CHECK_FALSE(emoji.empty());
    // 非 CJK 系统下拉丁与 CJK 首选不同；CJK 系统下两者统一为 CJK 族。
    const bool preferCjk = systemUiPrefersCjkFont();
    if (preferCjk) {
        CHECK(latin.front() == cjk.front());
    } else {
        CHECK(latin.front() != cjk.front());
    }
#if defined(_WIN32)
    CHECK(cjk.front() == "Microsoft YaHei");
    CHECK(latin.front() ==
          (preferCjk ? "Microsoft YaHei" : "Segoe UI"));
#elif defined(__APPLE__)
    CHECK(cjk.front() == "PingFang SC");
    if (preferCjk) {
        CHECK(latin.front() == "PingFang SC");
    }
#elif defined(__linux__) && !defined(__ANDROID__)
    CHECK(cjk.front() == "Noto Sans CJK SC");
    CHECK(latin.front() ==
          (preferCjk ? "Noto Sans CJK SC" : "Noto Sans"));
#endif
}

TEST_CASE("default_font_stack_override_wins_over_system", "[text][fonts]") {
    clearDefaultFontStackOverride();
    const std::string systemDefault = defaultFontFamily();
    CHECK_FALSE(systemDefault.empty());

    setDefaultFontStackOverride({"MyApp Font", "Fallback Font"});
    CHECK(defaultFontFamily() == "MyApp Font");
    CHECK(defaultFontStack().front() == "MyApp Font");
    // 覆盖后不再按脚本拆分：拉丁/CJK/emoji 统一走应用顺序。
    CHECK(defaultFontStackFor(U'A').front() == "MyApp Font");
    CHECK(defaultFontStackFor(0x4E2D).front() == "MyApp Font");
    CHECK(defaultFontStackFor(0x1F600).front() == "MyApp Font");

    clearDefaultFontStackOverride();
    CHECK(defaultFontFamily() == systemDefault);
    CHECK(defaultFontStackFor(U'A').front() != "MyApp Font");
}

TEST_CASE("skia_font_manager_factory_reports_backend_state",
          "[text][fonts]") {
    std::string diagnostic;
    const auto fonts = createSkiaFontManager(&diagnostic);
    INFO("diagnostic: " << diagnostic);
    CHECK_FALSE(diagnostic.empty());
    if (fonts == nullptr) {
        // CPU-only 构建：工厂明确说明未编译，不静默。
        CHECK(diagnostic.find("not compiled") != std::string::npos);
    } else {
        CHECK(fonts->backend() == FontBackend::Skia);
        CHECK(fonts->supportsShaping());
        CHECK(diagnostic.find("skia") != std::string::npos);
        const auto families = fonts->availableFamilies();
        INFO("familyCount: " << families.size()
                             << " diagnostic: " << fonts->diagnostic());
        for (std::size_t i = 0; i < std::min<std::size_t>(families.size(), 5); ++i) {
            INFO("family[" << i << "]: " << families[i]);
        }
        if (families.empty()) {
            // 极简容器：Skia 已编译但无系统字体，缺字状态必须明确、
            // 编辑索引不受影响（M1 缺字体可启动条款）。
            const FontFallbackStatus status = fonts->resolveWithStatus(
                FontQuery{"", FontWeight::Normal, false, 14.0F}, 'A');
            INFO("missing=" << status.missing
                            << " diag=" << status.diagnostic);
            CHECK(status.missing);
            CHECK_FALSE(status.diagnostic.empty());
            return;
        }
        // 拉丁字母应可解析（桌面系统字体环境）。CI 镜像字体子集差异
        // 时显式 SKIP 而非硬失败（与 GPU 缺硬件时 SKIP 一致）：缺的是
        // 环境字体，不是 shaping 逻辑。
        const std::string family = fonts->resolveFamily(
            FontQuery{"", FontWeight::Normal, false, 14.0F}, 'A');
        INFO("resolved family for 'A': " << family);
        if (family.empty()) {
            SKIP("system fonts lack Latin coverage for 'A'");
        }
        // shaped cluster 返回非空字形且携带 cluster 索引。GDI/CoreText
        // 在缺字时返回空而非崩溃：此时 SKIP（环境字体缺失，非逻辑错）。
        const auto glyphs = fonts->shapeCluster(
            FontQuery{"", FontWeight::Normal, false, 14.0F}, "A", 3);
        INFO("shaped glyphs: " << glyphs.size()
                               << (glyphs.empty() ? 0 : glyphs[0].advancePx));
        if (glyphs.empty()) {
            SKIP("shaping unavailable for 'A' on this font stack");
        }
        REQUIRE(glyphs.size() == 1);
        CHECK(glyphs[0].cluster == 3);
        CHECK(glyphs[0].advancePx > 0.0F);
    }
}
