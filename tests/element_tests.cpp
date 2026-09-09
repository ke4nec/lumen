#include <catch2/catch_test_macros.hpp>

#include <type_traits>

#include "lumen/core/element.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/platform/platform_window.h"
#include "lumen/render/renderer.h"

using namespace lumen::core;

TEST_CASE("element_inflates_children", "[element]") {
    Widget root = makeColumn({makeText("a"), makeText("b")});
    Element element(root);
    REQUIRE(element.children().size() == 2);
    CHECK(element.children()[0]->widget().text == "a");
    CHECK(element.children()[1]->widget().text == "b");
    CHECK(element.children()[0]->parent() == &element);
    CHECK(element.isDirty());
}

TEST_CASE("element_reuses_same_type_and_key", "[element]") {
    Widget first = makeRow({makeText("a", {}, {}, 0.0F, "k1"),
                            makeText("b", {}, {}, 0.0F, "k2")});
    Element element(first);
    const Element* firstChild = element.children()[0].get();
    const Element* secondChild = element.children()[1].get();

    Widget second = makeRow({makeText("a2", {}, {}, 0.0F, "k1"),
                             makeText("b2", {}, {}, 0.0F, "k2")});
    element.update(second);

    REQUIRE(element.children().size() == 2);
    CHECK(element.children()[0].get() == firstChild);
    CHECK(element.children()[1].get() == secondChild);
    CHECK(element.children()[0]->widget().text == "a2");
    CHECK(element.isDirty());
}

TEST_CASE("element_rebuilds_on_type_change", "[element]") {
    Element element(makeText("hello", {}, {}, 0.0F, "same"));
    element.update(makeButton("hello", {}, {}, 0.0F, "same"));
    CHECK(element.widget().type == WidgetType::Button);
    CHECK(element.children().empty());
}

TEST_CASE("element_rebuilds_on_key_change", "[element]") {
    Widget first = makeColumn({makeText("a", {}, {}, 0.0F, "k1")});
    Element element(first);
    const Element* childBefore = element.children()[0].get();

    Widget second = makeColumn({makeText("a", {}, {}, 0.0F, "k2")});
    element.update(second);

    REQUIRE(element.children().size() == 1);
    CHECK(element.children()[0].get() != childBefore);
    CHECK(element.children()[0]->widget().key == "k2");
}

TEST_CASE("element_keyed_reorder_reuses_nodes", "[element]") {
    Widget first = makeRow({makeText("a", {}, {}, 0.0F, "k1"),
                            makeText("b", {}, {}, 0.0F, "k2"),
                            makeText("c", {}, {}, 0.0F, "k3")});
    Element element(first);
    const Element* a = element.children()[0].get();
    const Element* b = element.children()[1].get();
    const Element* c = element.children()[2].get();

    Widget reordered = makeRow({makeText("c", {}, {}, 0.0F, "k3"),
                                makeText("a", {}, {}, 0.0F, "k1"),
                                makeText("b", {}, {}, 0.0F, "k2")});
    element.update(reordered);

    REQUIRE(element.children().size() == 3);
    CHECK(element.children()[0].get() == c);
    CHECK(element.children()[1].get() == a);
    CHECK(element.children()[2].get() == b);
}

TEST_CASE("element_keyless_reorder_keeps_positional_identity", "[element]") {
    Element element(makeRow({makeText("old text"), makeButton("old button")}));
    const Element* oldButton = element.children()[1].get();

    element.update(makeRow({makeButton("new button"), makeText("new text")}));

    REQUIRE(element.children().size() == 2);
    CHECK(element.children()[0].get() != oldButton);
    CHECK(element.children()[1].get() != oldButton);
    CHECK(element.children()[0]->widget().type == WidgetType::Button);
    CHECK(element.children()[1]->widget().type == WidgetType::Text);
}

TEST_CASE("element_is_not_movable", "[element]") {
    CHECK_FALSE(std::is_move_constructible_v<Element>);
    CHECK_FALSE(std::is_move_assignable_v<Element>);
}

TEST_CASE("element_shrinks_children_on_update", "[element]") {
    Widget first = makeRow({makeText("a", {}, {}, 0.0F, "k1"),
                            makeText("b", {}, {}, 0.0F, "k2"),
                            makeText("c", {}, {}, 0.0F, "k3")});
    Element element(first);
    const Element* secondChild = element.children()[1].get();

    Widget second = makeRow({makeText("b2", {}, {}, 0.0F, "k2")});
    element.update(second);

    REQUIRE(element.children().size() == 1);
    // The keyed child is reused; removed siblings are destroyed.
    CHECK(element.children()[0].get() == secondChild);
    CHECK(element.children()[0]->widget().text == "b2");
    CHECK(element.isDirty());
}

TEST_CASE("element_dirty_flag_lifecycle", "[element]") {
    Element element(makeText("x"));
    CHECK(element.isDirty());
    element.clearDirty();
    CHECK_FALSE(element.isDirty());
    element.markDirty();
    CHECK(element.isDirty());
    element.update(makeText("y"));
    CHECK(element.isDirty());
}

TEST_CASE("render_node_find_by_key", "[element]") {
    RenderNode root;
    root.key = "root";
    RenderNode child;
    child.key = "target";
    child.size = Size{10.0F, 10.0F};
    root.children.push_back(child);

    CHECK(findNodeByKey(root, "target") != nullptr);
    CHECK(findNodeByKey(root, "target")->size == Size{10.0F, 10.0F});
    CHECK(findNodeByKey(root, "missing") == nullptr);
}

TEST_CASE("widget_can_reuse_rule", "[element]") {
    CHECK(Element::canReuse(makeText("a", {}, {}, 0.0F, "k"),
                            makeText("b", {}, {}, 0.0F, "k")));
    CHECK_FALSE(Element::canReuse(makeText("a", {}, {}, 0.0F, "k1"),
                                  makeText("a", {}, {}, 0.0F, "k2")));
    CHECK_FALSE(Element::canReuse(makeText("a"), makeButton("a")));
}

TEST_CASE("widget_type_helpers", "[element]") {
    CHECK(isLeafWidget(WidgetType::Text));
    CHECK(isLeafWidget(WidgetType::Button));
    CHECK(isLeafWidget(WidgetType::TextField));
    CHECK_FALSE(isLeafWidget(WidgetType::Container));
    CHECK_FALSE(isLeafWidget(WidgetType::Row));
    CHECK(isFlexContainer(WidgetType::Row));
    CHECK(isFlexContainer(WidgetType::Column));
    CHECK_FALSE(isFlexContainer(WidgetType::Stack));
}

TEST_CASE("module_stage_names", "[element]") {
    // The renderer label must reflect what is actually compiled in: the
    // stage-5 Skia adapter only exists in LUMEN_ENABLE_SKIA builds.
#ifdef LUMEN_HAS_SKIA_BACKEND
    CHECK(std::string(lumen::render::rendererStageName()) == "stage5");
#else
    CHECK(std::string(lumen::render::rendererStageName()) == "stage2");
#endif
    CHECK(std::string(lumen::platform::platformStageName()) == "stage2");
    CHECK(std::string(lumen::dsl::dslStageName()) == "stage4");
}

TEST_CASE("platform_event_defaults", "[element]") {
    lumen::platform::Event event{};
    CHECK(event.type == lumen::platform::EventType::None);
    CHECK(event.position == Offset{0.0F, 0.0F});
    CHECK(event.keyCode == 0);
    CHECK(event.text.empty());
    CHECK(event.editCursor == 0);
    CHECK(event.editLength == 0);
}
