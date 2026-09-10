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
