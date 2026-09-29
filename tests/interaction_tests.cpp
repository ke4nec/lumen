#include <catch2/catch_test_macros.hpp>

#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/core/utf8.h"
#include "lumen/dsl/dsl.h"
#include "lumen/layout/layout.h"

using namespace lumen::core;
using namespace lumen::dsl;
using namespace lumen::layout;

namespace {

// Lays a UI out at a fixed size so interaction tests can use real geometry.
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

}  // namespace

TEST_CASE("hit_test_topmost_child_wins", "[interaction]") {
    Widget ui = makeStack(
        {withKey(makeContainerLeaf(300.0F, 200.0F), "bottom"),
         withKey(makeContainerLeaf(300.0F, 200.0F), "top")},
        StackAlignment::TopLeft);
    const RenderNode root = layoutOf(ui);
    std::vector<const RenderNode*> chain;
    const RenderNode* hit = hitTestChain(root, Offset{150.0F, 100.0F}, chain);
    REQUIRE(hit != nullptr);
    CHECK(hit->key == "top");
    CHECK(chain.at(0) == hit);
}

TEST_CASE("hit_test_misses_outside_root", "[interaction]") {
    Widget ui = makeStack({withKey(makeContainerLeaf(300.0F, 200.0F), "a")});
    const RenderNode root = layoutOf(ui);
    std::vector<const RenderNode*> chain;
    CHECK(hitTestChain(root, Offset{350.0F, 100.0F}, chain) == nullptr);
    CHECK(chain.empty());
}

TEST_CASE("hit_test_chain_runs_target_to_root", "[interaction]") {
    Widget inner = withKey(makeButton("OK", {}, {}, 0.0F, "inner"), "inner");
    Widget outer = withKey(makeContainer(std::move(inner)), "outer");
    Widget ui = withKey(makeColumn({std::move(outer)}), "root");
    const RenderNode root = layoutOf(ui);
    std::vector<const RenderNode*> chain;
    const RenderNode* hit = hitTestChain(root, centerOf(root, "inner"), chain);
    REQUIRE(hit != nullptr);
    CHECK(hit->key == "inner");
    REQUIRE(chain.size() == 3);
    CHECK(chain[0]->key == "inner");
    CHECK(chain[1]->key == "outer");
    CHECK(chain[2]->key == "root");
}

TEST_CASE("button_click_fires_handler_on_release", "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    int clicks = 0;
    handlers["doIt"] = [&clicks] { ++clicks; };
    Widget ui = makeContainer(withKey(
        makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt, std::nullopt,
                   "doIt"),
        "btn"));
    const RenderNode root = layoutOf(ui);

    const Offset at = centerOf(root, "btn");
    controller.pointerDown(root, at);
    CHECK(clicks == 0);
    CHECK(controller.pressedKey() == "btn");
    controller.pointerUp(root, at);
    CHECK(clicks == 1);
    CHECK(controller.pressedKey().empty());
}

TEST_CASE("button_requires_down_and_up_inside", "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    int clicks = 0;
    handlers["doIt"] = [&clicks] { ++clicks; };
    Widget ui = withKey(
        makeContainer(makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt,
                                 std::nullopt, "doIt")),
        "root");
    const RenderNode root = layoutOf(ui);
    const Offset inside = centerOf(root, "btn");
    // Bottom-right of the 300x200 root, well past the ~41x33 button.
    const Offset outside{250.0F, 150.0F};

    SECTION("release outside cancels") {
        controller.pointerDown(root, inside);
        controller.pointerUp(root, outside);
        CHECK(clicks == 0);
    }
    SECTION("press outside does not arm") {
        controller.pointerDown(root, outside);
        controller.pointerUp(root, inside);
        CHECK(clicks == 0);
    }
}

TEST_CASE("click_bubbles_to_nearest_handler", "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    std::vector<std::string> fired;
    handlers["child"] = [&fired] { fired.push_back("child"); };
    handlers["parent"] = [&fired] { fired.push_back("parent"); };

    // Plain text over a clickable container: the container's handler fires.
    Widget ui = withKey(
        makeContainer(withOnClick(
            withKey(makeContainer(makeText("label")), "pad"), "parent")),
        "root");
    const RenderNode root = layoutOf(ui);
    controller.pointerDown(root, centerOf(root, "pad"));
    controller.pointerUp(root, centerOf(root, "pad"));
    REQUIRE(fired.size() == 1);
    CHECK(fired[0] == "parent");

    // A clickable button consumes the event before its clickable ancestor.
    fired.clear();
    Widget ui2 = withKey(
        withOnClick(
            makeContainer(withKey(makeButton("OK", {}, {}, 0.0F, "btn",
                                             std::nullopt, std::nullopt,
                                             "child"),
                                  "btn")),
            "parent"),
        "root");
    const RenderNode root2 = layoutOf(ui2);
    controller.pointerDown(root2, centerOf(root2, "btn"));
    controller.pointerUp(root2, centerOf(root2, "btn"));
    REQUIRE(fired.size() == 1);
    CHECK(fired[0] == "child");
}

TEST_CASE("keyless_buttons_with_shared_handler_keep_distinct_targets",
          "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    int clicks = 0;
    handlers["same"] = [&clicks] { ++clicks; };
    const Widget ui = row({button("A", onClick("same")),
                           button("B", onClick("same"))});
    const RenderNode root = layoutOf(ui, 200.0F, 50.0F);
    REQUIRE(root.children.size() == 2);
    const Offset first = root.children[0].offset + Offset{1.0F, 1.0F};
    const Offset second = root.children[1].offset + Offset{1.0F, 1.0F};

    controller.pointerDown(root, first);
    CHECK_FALSE(controller.pressedIdentity().empty());
    controller.pointerUp(root, second);
    CHECK(clicks == 0);
    controller.pointerDown(root, first);
    controller.pointerUp(root, first);
    CHECK(clicks == 1);
}

TEST_CASE("text_field_focus_input_and_editing", "[interaction]") {
    StateStore store;
    store.set("name", "");
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    Widget ui = makeContainer(withKey(
        makeTextField("", "Name", {}, {}, 0.0F, "field", std::nullopt,
                      std::nullopt, "name"),
        "field"));
    const RenderNode root = layoutOf(ui);
    const Offset at = centerOf(root, "field");

    controller.pointerDown(root, at);
    CHECK(focus.focusedKey() == "field");
    CHECK(controller.wantsTextInput());

    controller.textInput("ab");
    CHECK(store.get("name") == "ab");
    controller.textInput("c");
    CHECK(store.get("name") == "abc");
    CHECK(controller.caretCodePoints() == 3);

    controller.keyDown(Key::Backspace);
    CHECK(store.get("name") == "ab");
    controller.keyDown(Key::Left);
    controller.textInput("X");
    CHECK(store.get("name") == "aXb");
    controller.keyDown(Key::End);
    controller.keyDown(Key::Delete);
    CHECK(store.get("name") == "aXb");
    controller.keyDown(Key::Enter);
    CHECK(focus.focusedKey().empty());
    CHECK_FALSE(controller.wantsTextInput());
}

TEST_CASE("click_elsewhere_clears_focus", "[interaction]") {
    StateStore store;
    store.set("name", "");
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    Widget ui = withKey(
        makeContainer(makeTextField("", "Name", {}, {}, 0.0F, "field",
                                    std::nullopt, std::nullopt, "name")),
        "root");
    const RenderNode root = layoutOf(ui);
    controller.pointerDown(root, centerOf(root, "field"));
    REQUIRE(focus.focusedKey() == "field");

    controller.pointerDown(root, Offset{250.0F, 150.0F});
    CHECK(focus.focusedKey().empty());
    // Text input while unfocused is dropped.
    controller.textInput("x");
    CHECK(store.get("name") == "");
}

TEST_CASE("state_store_notifies_only_on_change", "[state]") {
    StateStore store;
    store.set("counter", "1");
    int notifications = 0;
    const auto id = store.subscribe("counter", [&notifications] {
        ++notifications;
    });
    store.set("counter", "1");
    CHECK(notifications == 0);
    store.set("counter", "2");
    CHECK(notifications == 1);
    store.unsubscribe(id);
    store.set("counter", "3");
    CHECK(notifications == 1);
    CHECK(store.observerCount() == 0);
}

TEST_CASE("state_store_multiple_observers_registration_order", "[state]") {
    StateStore store;
    std::vector<std::string> order;
    store.subscribe("k", [&order] { order.push_back("first"); });
    store.subscribe("k", [&order] { order.push_back("second"); });
    store.set("k", "v");
    REQUIRE(order.size() == 2);
    CHECK(order[0] == "first");
    CHECK(order[1] == "second");
}

TEST_CASE("state_store_self_unsubscribe_during_notify", "[state]") {
    StateStore store;
    std::vector<std::string> calls;
    StateStore::ObserverId selfId = 0;
    selfId = store.subscribe("k", [&] {
        calls.push_back("self");
        store.unsubscribe(selfId);
    });
    store.subscribe("k", [&calls] { calls.push_back("other"); });
    store.set("k", "v");
    // The self-unsubscribing observer runs, the following one still fires,
    // and the store ends with the observer removed.
    REQUIRE(calls.size() == 2);
    CHECK(calls[0] == "self");
    CHECK(calls[1] == "other");
    CHECK(store.observerCount() == 1);
    store.set("k", "v2");
    CHECK(calls.size() == 3);
}

TEST_CASE("state_store_skips_subscription_removed_by_prior_observer",
          "[state]") {
    StateStore store;
    int removedCalls = 0;
    StateStore::ObserverId removedId = 0;
    store.subscribe("k", [&] { store.unsubscribe(removedId); });
    removedId = store.subscribe("k", [&] { ++removedCalls; });

    CHECK_NOTHROW(store.set("k", "v"));
    CHECK(removedCalls == 0);
    CHECK(store.observerCount() == 1);
}

TEST_CASE("apply_binds_resolves_text_and_field_values", "[state]") {
    StateStore store;
    store.set("counter", "42");
    store.set("name", "Lumen");
    Widget ui = container(column({
                               withKey(text("Count: ", bind("counter")),
                                       "count-text"),
                               withKey(text_field(bind("name"),
                                                  placeholder("Name")),
                                       "name-field"),
                           }),
                          Color::fromRGBA(24, 24, 27));

    applyBinds(ui, store);
    const RenderNode root = layoutOf(ui);
    const RenderNode* label = findNodeByKey(root, "count-text");
    REQUIRE(label != nullptr);
    CHECK(label->text == "Count: 42");
    const RenderNode* field = findNodeByKey(root, "name-field");
    REQUIRE(field != nullptr);
    CHECK(field->text == "Lumen");
    CHECK(field->placeholder == "Name");

    // Idempotent: re-running never concatenates the prefix twice.
    store.set("counter", "43");
    applyBinds(ui, store);
    const RenderNode relaid = layoutOf(ui);
    CHECK(findNodeByKey(relaid, "count-text")->text == "Count: 43");
}

TEST_CASE("collect_bind_keys_gathers_unique_keys", "[state]") {
    Widget ui = container(column({
                               text("a", bind("counter")),
                               text("b", bind("counter")),
                               text_field(bind("name")),
                               makeText("static"),
                           }),
                          Color::fromRGBA(24, 24, 27));
    const std::set<std::string> keys = collectBindKeys(ui);
    CHECK(keys == std::set<std::string>{"counter", "name"});
}

TEST_CASE("utf8_helpers_count_and_edit_code_points", "[utf8]") {
    // "\xa0" + "b" split to avoid the \xa0b hex-escape ambiguity.
    const std::string mixed = "a\xe4\xbd\xa0" "b";
    CHECK(utf8Length("hello") == 5);
    CHECK(utf8Length("\xe4\xbd\xa0\xe5\xa5\xbd") == 2);
    CHECK(utf8Length("") == 0);
    CHECK(utf8OffsetAt(mixed, 0) == 0);
    CHECK(utf8OffsetAt(mixed, 1) == 1);
    CHECK(utf8OffsetAt(mixed, 2) == 4);
    CHECK(utf8OffsetAt(mixed, 99) == 5);
    CHECK(utf8Insert("ab", 1, "\xe4\xbd\xa0") == mixed);
    CHECK(utf8EraseBefore(mixed, 2) == "ab");
    CHECK(utf8EraseBefore("abc", 0) == "abc");
    CHECK(utf8EraseAfter(mixed, 1) == "ab");
    CHECK(utf8EraseAfter("ab", 2) == "ab");
}

// Truncated multi-byte sequences must degrade instead of throwing or reading
// out of bounds (utf8.h contract).
TEST_CASE("utf8_helpers_survive_truncated_sequences", "[utf8]") {
    const std::string twoByteLead = "\xc3";
    const std::string fourByteLead = "\xf0";
    CHECK(utf8OffsetAt(twoByteLead, 1) == twoByteLead.size());
    CHECK(utf8OffsetAt(fourByteLead, 1) == fourByteLead.size());
    CHECK(utf8Insert(twoByteLead, 1, "x") == "\xc3x");
    CHECK_NOTHROW(utf8Insert(fourByteLead, 1, "y"));
    CHECK_NOTHROW(utf8EraseBefore(fourByteLead, 1));
    CHECK_NOTHROW(utf8EraseAfter(fourByteLead, 1));
    CHECK(utf8Length(twoByteLead) == 1);
    CHECK(utf8EraseAfter(fourByteLead, 0).empty());
}

TEST_CASE("composition_previews_without_committing", "[interaction]") {
    StateStore store;
    store.set("name", "ab");
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    Widget ui = makeContainer(withKey(
        makeTextField("", "Name", {}, {}, 0.0F, "field", std::nullopt,
                      std::nullopt, "name"),
        "field"));
    applyBinds(ui, store);
    const RenderNode root = layoutOf(ui);
    controller.pointerDown(root, centerOf(root, "field"));
    REQUIRE(controller.wantsTextInput());

    // IME preedit (SDL_EVENT_TEXT_EDITING): visible to the controller but
    // never written to the document.
    controller.setComposition("ni");
    CHECK(controller.composition() == "ni");
    CHECK(store.get("name") == "ab");

    // Committing (SDL_EVENT_TEXT_INPUT) clears the preview and inserts.
    controller.textInput("ni");
    CHECK(controller.composition().empty());
    CHECK(store.get("name") == "abni");

    // Moving focus drops any stale composition.
    controller.setComposition("hao");
    controller.pointerDown(root, Offset{250.0F, 150.0F});
    CHECK(controller.composition().empty());
    CHECK_FALSE(controller.wantsTextInput());

    // Preedit while unfocused is ignored, never lingering without a field.
    controller.setComposition("stale");
    CHECK(controller.composition().empty());
    CHECK(store.get("name") == "abni");
}

// --- Basic gestures (plan 阶段6): tap vs drag discrimination. ---

TEST_CASE("gesture_small_jitter_still_clicks", "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    int clicks = 0;
    handlers["doIt"] = [&clicks] { ++clicks; };
    Widget ui = withKey(
        makeContainer(makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt,
                                 std::nullopt, "doIt")),
        "root");
    const RenderNode root = layoutOf(ui);
    const Offset inside = centerOf(root, "btn");

    controller.pointerDown(root, inside);
    // Under the 4px slop: still a tap.
    controller.pointerMove(root, inside + Offset{2.0F, 1.0F});
    CHECK_FALSE(controller.isDragging());
    controller.pointerUp(root, inside + Offset{2.0F, 1.0F});
    CHECK(clicks == 1);
}

TEST_CASE("gesture_drag_cancels_click_and_reports_delta", "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    int clicks = 0;
    handlers["doIt"] = [&clicks] { ++clicks; };
    Widget ui = withKey(
        makeContainer(makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt,
                                 std::nullopt, "doIt")),
        "root");
    const RenderNode root = layoutOf(ui);
    const Offset inside = centerOf(root, "btn");

    controller.pointerDown(root, inside);
    CHECK_FALSE(controller.isDragging());
    controller.pointerMove(root, inside + Offset{12.0F, -3.0F});
    CHECK(controller.isDragging());
    CHECK(controller.dragDelta() == Offset{12.0F, -3.0F});
    // Release over the same button: a drag release is not a click.
    controller.pointerUp(root, inside + Offset{12.0F, -3.0F});
    CHECK(clicks == 0);
    CHECK_FALSE(controller.isDragging());

    // A fresh press without movement clicks again.
    controller.pointerDown(root, inside);
    controller.pointerUp(root, inside);
    CHECK(clicks == 1);
}

TEST_CASE("gesture_move_without_press_is_ignored", "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    const RenderNode root = layoutOf(
        withKey(makeContainer(makeText("x")), "root"));
    controller.pointerMove(root, Offset{50.0F, 50.0F});
    CHECK_FALSE(controller.isDragging());
    CHECK(controller.dragDelta() == Offset{0.0F, 0.0F});
}

// --- 视觉系统：disabled 门控（§10.3 disabled 不响应 pointer/keyboard） ---

TEST_CASE("disabled_button_ignores_pointer_and_keyboard", "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    int clicks = 0;
    handlers["doIt"] = [&clicks] { ++clicks; };
    Widget ui = withKey(
        makeContainer(withEnabled(
            makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt, std::nullopt,
                       "doIt"),
            /*enabled=*/false)),
        "root");
    const RenderNode root = layoutOf(ui);

    const Offset at = centerOf(root, "btn");
    controller.pointerDown(root, at);
    // 不产生按压视觉，也不武装点击。
    CHECK(controller.pressedKey().empty());
    controller.pointerUp(root, at);
    CHECK(clicks == 0);

    // 键盘路径：禁用按钮不进焦点遍历，也无法被激活。
    controller.keyDown(root, Key::Tab);
    CHECK(focus.focusedKey().empty());
}

TEST_CASE("disabled_checkbox_and_switch_do_not_toggle", "[interaction]") {
    StateStore store;
    store.set("check", "false");
    store.set("toggle", "false");
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    Widget ui = withKey(
        makeContainer(makeColumn({
            withEnabled(makeCheckbox("A", "check", "cb"), false),
            withEnabled(makeSwitch("S", "toggle", "sw"), false),
        })),
        "root");
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "cb"));
    controller.pointerUp(root, centerOf(root, "cb"));
    controller.pointerDown(root, centerOf(root, "sw"));
    controller.pointerUp(root, centerOf(root, "sw"));
    CHECK(store.get("check") == "false");
    CHECK(store.get("toggle") == "false");

    // 语义/键盘共用路径 toggleChecked 也拒绝禁用节点。
    const RenderNode* checkbox = findNodeByKey(root, "cb");
    REQUIRE(checkbox != nullptr);
    controller.toggleChecked(*checkbox);
    CHECK(store.get("check") == "false");
}

TEST_CASE("disabled_textfield_takes_no_focus", "[interaction]") {
    StateStore store;
    store.set("name", "");
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    Widget field = withEnabled(
        makeTextField("", "Name", {}, {}, 0.0F, "field"), false);
    field.bind = "name";
    const RenderNode root = layoutOf(withKey(makeContainer(std::move(field)),
                                             "root"));

    controller.pointerDown(root, centerOf(root, "field"));
    controller.pointerUp(root, centerOf(root, "field"));
    CHECK_FALSE(controller.wantsTextInput());
    CHECK(focus.focusedKey().empty());
}

TEST_CASE("hover_tracking_follows_pointer", "[interaction]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    Widget ui = withKey(
        makeContainer(makeButton("OK", {}, {}, 0.0F, "btn")),
        "root");
    const RenderNode root = layoutOf(ui);

    CHECK(controller.hoveredIdentity().empty());
    controller.pointerMove(root, centerOf(root, "btn"));
    CHECK(controller.hoveredKey() == "btn");
    CHECK_FALSE(controller.hoveredIdentity().empty());
    // 离开控件：hover 清空（容器不承载 hover）。
    controller.pointerMove(root, Offset{250.0F, 150.0F});
    CHECK(controller.hoveredIdentity().empty());
    // 按下时 hover 同步到命中目标，释放后保留。
    controller.pointerDown(root, centerOf(root, "btn"));
    CHECK(controller.hoveredKey() == "btn");
    controller.pointerUp(root, centerOf(root, "btn"));
    CHECK(controller.hoveredKey() == "btn");
}

TEST_CASE("hover_tracking_covers_collection_rows", "[interaction]") {
    // 集合行承载 hover（与可聚焦谓词同源）：collectionRow + onClick 的
    // 行获得 hover 高亮；disabled 行与普通行不承载（菜单/列表行 hover
    // 的追踪门——此前只认 Button/TextField/Checkbox/Switch）。
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    Widget row;
    row.type = WidgetType::Row;
    row.key = "row";
    row.onClick = "row-click";
    row.collectionRow = true;
    row.width = 120.0F;
    row.height = 40.0F;
    Widget disabledRow = row;
    disabledRow.key = "row-disabled";
    disabledRow.enabled = false;
    Widget plain;
    plain.type = WidgetType::Row;
    plain.key = "plain";
    plain.onClick = "plain-click";
    plain.width = 120.0F;
    plain.height = 40.0F;
    Widget ui = makeColumn({std::move(row), std::move(disabledRow),
                            std::move(plain)});
    const RenderNode root = layoutOf(ui);

    controller.pointerMove(root, Offset{60.0F, 20.0F});
    CHECK(controller.hoveredKey() == "row");
    // disabled 行不承载（跳过，不落到父容器）。
    controller.pointerMove(root, Offset{60.0F, 60.0F});
    CHECK(controller.hoveredIdentity().empty());
    // 普通行（未标记 collectionRow）不承载 hover。
    controller.pointerMove(root, Offset{60.0F, 100.0F});
    CHECK(controller.hoveredIdentity().empty());
}

// --- M15：框架内拖放会话状态机（m15-roadmap §3） ---

namespace {

// 三行固定尺寸按钮（280x50）；arm sink 按 "row-" key 前缀认领。
Widget dragRowButton(int index) {
    return makeButton("row " + std::to_string(index), {}, {}, 0.0F,
                      "row-" + std::to_string(index), 280.0F, 50.0F,
                      "click" + std::to_string(index));
}

Widget dragRowsUi() {
    return makeColumn({dragRowButton(0), dragRowButton(1), dragRowButton(2)});
}

struct DragSessionRecorder {
    std::vector<DragPhase> phases{};
    std::vector<Offset> positions{};
    std::vector<std::string> sources{};
    std::size_t dropChainSize{0};
    std::string dropDeepestKey{};
    std::size_t startChainSize{0};
};

// 注册按 "row-" 前缀认领的 arm sink；返回 arm sink 是否被咨询过。
bool armRows(InteractionController& controller, bool touchAllowed) {
    bool consulted = false;
    controller.addDragArmSink(
        [&consulted, touchAllowed](
            const std::vector<const RenderNode*>& chain, PointerDevice,
            DragSourceClaim& claim) {
            consulted = true;
            for (const RenderNode* node : chain) {
                if (node->key.rfind("row-", 0) == 0) {
                    claim.key = node->key;
                    claim.identity = node->identity;
                    claim.touchAllowed = touchAllowed;
                    return true;
                }
            }
            return false;
        });
    return consulted;  // 值无意义；sink 内闭包记录咨询状态。
}

DragSessionRecorder recordDragSession(InteractionController& controller) {
    DragSessionRecorder recorder;
    controller.addDragSessionSink(
        [&recorder](DragPhase phase, Offset position,
                    const std::vector<const RenderNode*>& chain,
                    const std::string& sourceKey, const std::string&) {
            recorder.phases.push_back(phase);
            recorder.positions.push_back(position);
            recorder.sources.push_back(sourceKey);
            if (phase == DragPhase::Start) {
                recorder.startChainSize = chain.size();
            }
            if (phase == DragPhase::Drop) {
                recorder.dropChainSize = chain.size();
                recorder.dropDeepestKey =
                    chain.empty() ? std::string{} : chain.front()->key;
            }
        });
    return recorder;
}

}  // namespace

TEST_CASE("drag_session_starts_after_threshold_moves_and_drops",
          "[interaction][m15]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    int clicks = 0;
    handlers["click0"] = [&clicks] { ++clicks; };
    handlers["click2"] = [&clicks] { ++clicks; };

    const RenderNode root = layoutOf(dragRowsUi());
    armRows(controller, /*touchAllowed=*/false);
    DragSessionRecorder recorder = recordDragSession(controller);

    const Offset row0 = centerOf(root, "row-0");
    const Offset row2 = centerOf(root, "row-2");

    // 按下 + 阈值内小位移：不开会话，仍是按压。
    controller.pointerDown(root, row0);
    controller.pointerMove(root, row0 + Offset{0.0F, 4.0F});
    CHECK_FALSE(controller.dragSessionActive());
    CHECK(recorder.phases.empty());

    // 越过启动阈值（默认 8px 曼哈顿）：Start + 同拍 Move。
    controller.pointerMove(root, row0 + Offset{0.0F, 12.0F});
    REQUIRE(controller.dragSessionActive());
    REQUIRE(recorder.phases.size() == 2);
    CHECK(recorder.phases[0] == DragPhase::Start);
    CHECK(recorder.phases[1] == DragPhase::Move);
    CHECK(controller.dragSourceKey() == "row-0");
    CHECK(recorder.sources.front() == "row-0");
    CHECK(recorder.startChainSize >= 1);
    CHECK(controller.isDragging());  // 会话释放不触发点击。

    // 再移动一拍：Move 携带新位置。
    controller.pointerMove(root, row0 + Offset{0.0F, 30.0F});
    REQUIRE(recorder.phases.size() == 3);
    CHECK(recorder.phases[2] == DragPhase::Move);
    CHECK(recorder.positions[2].y > recorder.positions[1].y);

    // 释放在 row-2 上：Drop 携带落点命中链，最深命中是 row-2。
    controller.pointerUp(root, row2);
    CHECK_FALSE(controller.dragSessionActive());
    REQUIRE(recorder.phases.size() == 4);
    CHECK(recorder.phases[3] == DragPhase::Drop);
    CHECK(recorder.dropChainSize >= 1);
    CHECK(recorder.dropDeepestKey == "row-2");
    // 拖放释放绝不触发点击（按压源与释放目标都注册了 handler）。
    CHECK(clicks == 0);
}

TEST_CASE("drag_below_threshold_release_still_clicks",
          "[interaction][m15]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    int clicks = 0;
    handlers["click0"] = [&clicks] { ++clicks; };
    const RenderNode root = layoutOf(dragRowsUi());
    armRows(controller, false);
    DragSessionRecorder recorder = recordDragSession(controller);

    // arm 认领的按压在阈值下释放：普通点击（微抖动不误启会话）。
    const Offset row0 = centerOf(root, "row-0");
    controller.pointerDown(root, row0);
    controller.pointerMove(root, row0 + Offset{0.0F, 5.0F});
    controller.pointerUp(root, row0 + Offset{0.0F, 5.0F});
    CHECK(recorder.phases.empty());
    CHECK(clicks == 1);
}

TEST_CASE("drag_session_cancel_ends_without_drop", "[interaction][m15]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    const RenderNode root = layoutOf(dragRowsUi());
    armRows(controller, false);
    DragSessionRecorder recorder = recordDragSession(controller);

    const Offset row0 = centerOf(root, "row-0");
    controller.pointerDown(root, row0);
    controller.pointerMove(root, row0 + Offset{0.0F, 20.0F});
    REQUIRE(controller.dragSessionActive());
    controller.pointerCancel();
    CHECK_FALSE(controller.dragSessionActive());
    REQUIRE_FALSE(recorder.phases.empty());
    CHECK(recorder.phases.back() == DragPhase::Cancel);
    // Cancel 之后再无 Move/Drop。
    controller.pointerMove(root, row0 + Offset{0.0F, 40.0F});
    controller.pointerUp(root, row0);
    CHECK(recorder.phases.back() == DragPhase::Cancel);
    CHECK(recorder.phases.size() == 3);  // Start + Move + Cancel。
}

TEST_CASE("drag_threshold_is_configurable", "[interaction][m15]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    const RenderNode root = layoutOf(dragRowsUi());
    armRows(controller, false);
    DragSessionRecorder recorder = recordDragSession(controller);
    controller.setDragThresholdPx(30.0F);

    const Offset row0 = centerOf(root, "row-0");
    controller.pointerDown(root, row0);
    controller.pointerMove(root, row0 + Offset{0.0F, 20.0F});
    CHECK_FALSE(controller.dragSessionActive());
    controller.pointerMove(root, row0 + Offset{0.0F, 31.0F});
    CHECK(controller.dragSessionActive());
    controller.pointerUp(root, row0);
}

TEST_CASE("touch_row_drag_defers_to_scroll_until_handle_claims",
          "[interaction][m15]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;

    // 高内容（6 行 x 50px = 300px）放入 200px 视口：纵向可滚。
    const auto scrollableUi = [] {
        std::vector<Widget> rows;
        for (int i = 0; i < 6; ++i) {
            rows.push_back(dragRowButton(i));
        }
        return makeScrollView(makeColumn(std::move(rows)), "viewport", 300.0F,
                              200.0F);
    };

    // 触摸 + 行整体认领（touchAllowed=false）：拖动路由为视口滚动。
    {
        InteractionController controller(store, handlers, focus);
        controller.setScrollDragSink(
            [](const RenderNode*, const RenderNode*, Offset, Offset,
               ScrollDragPhase, std::uint64_t) { return true; });
        armRows(controller, /*touchAllowed=*/false);
        DragSessionRecorder recorder = recordDragSession(controller);
        const RenderNode root = layoutOf(scrollableUi());
        const Offset row0 = centerOf(root, "row-0");
        controller.pointerDown(root, row0, 0, kModifierNone,
                               PointerButton::Primary, PointerDevice::Touch);
        controller.pointerMove(root, row0 + Offset{0.0F, 40.0F});
        CHECK(controller.isScrollDragging());
        CHECK_FALSE(controller.dragSessionActive());
        CHECK(recorder.phases.empty());
        controller.pointerUp(root, row0 + Offset{0.0F, 40.0F});
    }
    // 触摸 + 专用句柄认领（touchAllowed=true）：拖放会话优先于滚动。
    {
        InteractionController controller(store, handlers, focus);
        controller.setScrollDragSink(
            [](const RenderNode*, const RenderNode*, Offset, Offset,
               ScrollDragPhase, std::uint64_t) { return true; });
        armRows(controller, /*touchAllowed=*/true);
        DragSessionRecorder recorder = recordDragSession(controller);
        const RenderNode root = layoutOf(scrollableUi());
        const Offset row0 = centerOf(root, "row-0");
        controller.pointerDown(root, row0, 0, kModifierNone,
                               PointerButton::Primary, PointerDevice::Touch);
        controller.pointerMove(root, row0 + Offset{0.0F, 40.0F});
        CHECK_FALSE(controller.isScrollDragging());
        REQUIRE(controller.dragSessionActive());
        CHECK(recorder.phases.front() == DragPhase::Start);
        controller.pointerUp(root, row0 + Offset{0.0F, 40.0F});
    }
}

TEST_CASE("drag_arm_skipped_for_text_field_press", "[interaction][m15]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);

    // 字段行 + 普通行：字段命中走选区路径（selecting_），arm 不咨询。
    Widget ui = makeColumn({
        makeTextField("edit me", {}, {}, {}, 0.0F, "field", 280.0F, 40.0F,
                      "fieldBind"),
        dragRowButton(0),
    });
    const RenderNode root = layoutOf(std::move(ui));

    bool armConsulted = false;
    controller.addDragArmSink(
        [&armConsulted](const std::vector<const RenderNode*>& chain,
                        PointerDevice, DragSourceClaim& claim) {
            armConsulted = true;
            for (const RenderNode* node : chain) {
                if (node->key.rfind("row-", 0) == 0) {
                    claim.key = node->key;
                    claim.identity = node->identity;
                    return true;
                }
            }
            return false;
        });
    DragSessionRecorder recorder = recordDragSession(controller);

    const Offset field = centerOf(root, "field");
    controller.pointerDown(root, field);
    // 字段命中即锁定选区路径（selecting_）：arm sink 不被咨询——文本
    // 选区拖动优先于拖放（roadmap §3 仲裁表）。
    CHECK_FALSE(armConsulted);
    CHECK(controller.focusedBind() == "fieldBind");
    controller.pointerMove(root, field + Offset{30.0F, 0.0F});
    CHECK_FALSE(controller.dragSessionActive());
    // 字段拖动扩展选区（既有路径），无拖放会话。
    CHECK(recorder.phases.empty());
    controller.pointerUp(root, field + Offset{30.0F, 0.0F});
}
