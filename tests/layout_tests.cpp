#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"

using namespace lumen::core;
using namespace lumen::layout;
using Catch::Matchers::WithinAbs;

namespace {

Constraints tight(float width, float height) {
    return Constraints::tight(Size{width, height});
}

Constraints loose(float width, float height) {
    return Constraints::loose(Size{width, height});
}

}  // namespace

TEST_CASE("container_applies_fixed_size", "[layout]") {
    const Widget widget = makeContainerLeaf(100.0F, 50.0F);
    const RenderNode node = LayoutEngine::layout(widget, loose(800.0F, 600.0F));
    CHECK_THAT(node.size.width, WithinAbs(100.0F, 0.001F));
    CHECK_THAT(node.size.height, WithinAbs(50.0F, 0.001F));
}

TEST_CASE("container_padding_expands_around_child", "[layout]") {
    TextStyle style;
    style.fontSize = 20.0F;
    Widget child = makeText("Hi", style);
    Widget widget = makeContainer(std::move(child), std::nullopt, std::nullopt,
                                  EdgeInsets::all(10.0F));
    const RenderNode node = LayoutEngine::layout(widget, loose(800.0F, 600.0F));
    // "Hi" measures 24x24; +20 padding => 44x44.
    CHECK_THAT(node.size.width, WithinAbs(44.0F, 0.01F));
    CHECK_THAT(node.size.height, WithinAbs(44.0F, 0.01F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.children[0].offset.x, WithinAbs(10.0F, 0.001F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(10.0F, 0.001F));
}

TEST_CASE("container_margin_shrinks_within_parent", "[layout]") {
    const Widget withoutMargin = makeContainerLeaf();
    const Widget withMargin = makeContainerLeaf(
        std::nullopt, std::nullopt, {}, EdgeInsets::all(10.0F));
    const RenderNode plain =
        LayoutEngine::layout(withoutMargin, tight(100.0F, 100.0F));
    const RenderNode margined =
        LayoutEngine::layout(withMargin, tight(100.0F, 100.0F));
    CHECK_THAT(plain.size.width, WithinAbs(100.0F, 0.001F));
    CHECK_THAT(margined.size.width, WithinAbs(80.0F, 0.001F));
    CHECK_THAT(margined.size.height, WithinAbs(80.0F, 0.001F));
}

TEST_CASE("row_distributes_flex_space", "[layout]") {
    Widget fixed = makeContainerLeaf(100.0F, 50.0F);
    Widget flexOne = withFlex(makeContainerLeaf(std::nullopt, 50.0F), 1.0F);
    Widget flexTwo = withFlex(makeContainerLeaf(std::nullopt, 50.0F), 2.0F);
    Widget row = makeRow({fixed, flexOne, flexTwo});
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));

    REQUIRE(node.children.size() == 3);
    CHECK_THAT(node.size.width, WithinAbs(600.0F, 0.001F));
    CHECK_THAT(node.children[0].size.width, WithinAbs(100.0F, 0.01F));
    // Remaining 500 splits 1:2 => ~166.67 and ~333.33.
    CHECK_THAT(node.children[1].size.width, WithinAbs(166.67F, 0.5F));
    CHECK_THAT(node.children[2].size.width, WithinAbs(333.33F, 0.5F));
    CHECK_THAT(node.children[0].offset.x, WithinAbs(0.0F, 0.01F));
    CHECK_THAT(node.children[1].offset.x, WithinAbs(100.0F, 0.5F));
    CHECK_THAT(node.children[2].offset.x, WithinAbs(266.67F, 0.5F));
}

TEST_CASE("column_distributes_flex_space", "[layout]") {
    Widget fixed = makeContainerLeaf(50.0F, 100.0F);
    Widget flexOne = withFlex(makeContainerLeaf(50.0F, std::nullopt), 1.0F);
    Widget flexTwo = withFlex(makeContainerLeaf(50.0F, std::nullopt), 3.0F);
    Widget column = makeColumn({fixed, flexOne, flexTwo});
    const RenderNode node = LayoutEngine::layout(column, tight(100.0F, 600.0F));

    REQUIRE(node.children.size() == 3);
    CHECK_THAT(node.children[0].size.height, WithinAbs(100.0F, 0.01F));
    // Remaining 500 splits 1:3 => 125 and 375.
    CHECK_THAT(node.children[1].size.height, WithinAbs(125.0F, 0.5F));
    CHECK_THAT(node.children[2].size.height, WithinAbs(375.0F, 0.5F));
}

TEST_CASE("row_main_axis_center_alignment", "[layout]") {
    Widget child = makeContainerLeaf(100.0F, 50.0F);
    Widget row = makeRow({child}, MainAxisAlignment::Center);
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.children[0].offset.x, WithinAbs(250.0F, 0.01F));
}

TEST_CASE("row_cross_axis_center_alignment", "[layout]") {
    Widget child = makeContainerLeaf(100.0F, 50.0F);
    Widget row = makeRow({child}, MainAxisAlignment::Start,
                         CrossAxisAlignment::Center);
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.children[0].offset.y, WithinAbs(25.0F, 0.01F));
}

TEST_CASE("row_stretch_cross_axis", "[layout]") {
    Widget child = makeContainerLeaf(100.0F, std::nullopt);
    Widget row = makeRow({child}, MainAxisAlignment::Start,
                         CrossAxisAlignment::Stretch);
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.children[0].size.height, WithinAbs(100.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(0.0F, 0.001F));
}

TEST_CASE("row_stretch_preserves_margins", "[layout]") {
    Widget child =
        makeContainerLeaf(100.0F, std::nullopt, {}, EdgeInsets::all(10.0F));
    Widget row = makeRow({child}, MainAxisAlignment::Start,
                         CrossAxisAlignment::Stretch);
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));
    REQUIRE(node.children.size() == 1);
    // Content cross box is 100; minus 20 margin => 80 border height at y=10.
    CHECK_THAT(node.children[0].size.height, WithinAbs(80.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(10.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.x, WithinAbs(10.0F, 0.01F));
}

TEST_CASE("row_stretch_flex_child_preserves_margins", "[layout]") {
    Widget flexChild = withFlex(
        makeContainerLeaf(std::nullopt, std::nullopt, {},
                          EdgeInsets::symmetric(10.0F, 5.0F)),
        1.0F);
    Widget row = makeRow({flexChild}, MainAxisAlignment::Start,
                         CrossAxisAlignment::Stretch);
    const RenderNode node = LayoutEngine::layout(row, tight(400.0F, 100.0F));
    REQUIRE(node.children.size() == 1);
    // Main budget 400 minus 20 margin => 380 width; cross 100 minus 10 => 90.
    CHECK_THAT(node.children[0].size.width, WithinAbs(380.0F, 0.5F));
    CHECK_THAT(node.children[0].size.height, WithinAbs(90.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.x, WithinAbs(10.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(5.0F, 0.01F));
}

TEST_CASE("stack_aligns_center", "[layout]") {
    Widget small = makeContainerLeaf(50.0F, 50.0F);
    Widget large = makeContainerLeaf(100.0F, 100.0F);
    Widget stack = makeStack({small, large}, StackAlignment::Center);
    const RenderNode node = LayoutEngine::layout(stack, tight(200.0F, 200.0F));
    REQUIRE(node.children.size() == 2);
    CHECK_THAT(node.size.width, WithinAbs(200.0F, 0.001F));
    // Content box is 200x200; 50px child centers at 75, 100px child at 50.
    CHECK_THAT(node.children[0].offset.x, WithinAbs(75.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(75.0F, 0.01F));
    CHECK_THAT(node.children[1].offset.x, WithinAbs(50.0F, 0.01F));
    CHECK_THAT(node.children[1].offset.y, WithinAbs(50.0F, 0.01F));
}

TEST_CASE("stack_explicit_position_overrides_alignment", "[layout]") {
    Widget child = withStackPosition(makeContainerLeaf(40.0F, 30.0F),
                                     Offset{10.0F, 20.0F});
    Widget stack = makeStack({child}, StackAlignment::Center);
    const RenderNode node = LayoutEngine::layout(stack, loose(300.0F, 300.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.children[0].offset.x, WithinAbs(10.0F, 0.001F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(20.0F, 0.001F));
}

TEST_CASE("text_measures_intrinsic_size", "[layout]") {
    TextStyle style;
    style.fontSize = 20.0F;
    const Widget widget = makeText("Hi", style);
    const RenderNode node =
        LayoutEngine::layout(widget, Constraints::unbounded());
    CHECK_THAT(node.size.width, WithinAbs(24.0F, 0.01F));
    CHECK_THAT(node.size.height, WithinAbs(24.0F, 0.01F));
}

TEST_CASE("leaf_widgets_have_minimum_sizes", "[layout]") {
    const RenderNode button =
        LayoutEngine::layout(makeButton("OK"), Constraints::unbounded());
    CHECK(button.size.width >= 64.0F);
    CHECK(button.size.height >= 32.0F);

    const RenderNode field = LayoutEngine::layout(makeTextField("", "Name"),
                                                  Constraints::unbounded());
    CHECK(field.size.width >= 96.0F);
    CHECK(field.size.height >= 30.0F);
}

TEST_CASE("fixed_size_applies_to_all_widget_types", "[layout]") {
    const auto box = loose(800.0F, 600.0F);
    const RenderNode text =
        LayoutEngine::layout(makeText("Hi", {}, {}, 0.0F, "", 120.0F, 40.0F),
                             box);
    CHECK_THAT(text.size.width, WithinAbs(120.0F, 0.001F));
    CHECK_THAT(text.size.height, WithinAbs(40.0F, 0.001F));

    const RenderNode row = LayoutEngine::layout(
        makeRow({makeContainerLeaf(10.0F, 10.0F)}, MainAxisAlignment::Start,
                CrossAxisAlignment::Start, 0.0F, {}, {}, "", 200.0F, 100.0F),
        box);
    CHECK_THAT(row.size.width, WithinAbs(200.0F, 0.001F));
    CHECK_THAT(row.size.height, WithinAbs(100.0F, 0.001F));

    const RenderNode stack = LayoutEngine::layout(
        makeStack({makeContainerLeaf(10.0F, 10.0F)}, StackAlignment::TopLeft,
                  {}, {}, "", 150.0F, 90.0F),
        box);
    CHECK_THAT(stack.size.width, WithinAbs(150.0F, 0.001F));
    CHECK_THAT(stack.size.height, WithinAbs(90.0F, 0.001F));
}

TEST_CASE("resize_updates_root_layout", "[layout]") {
    Widget row =
        makeRow({withFlex(makeContainerLeaf(std::nullopt, 50.0F), 1.0F),
                 withFlex(makeContainerLeaf(std::nullopt, 50.0F), 1.0F)});
    const RenderNode small = LayoutEngine::layout(row, tight(800.0F, 600.0F));
    const RenderNode large = LayoutEngine::layout(row, tight(1024.0F, 768.0F));
    CHECK_THAT(small.size.width, WithinAbs(800.0F, 0.001F));
    CHECK_THAT(large.size.width, WithinAbs(1024.0F, 0.001F));
    CHECK_THAT(small.children[0].size.width, WithinAbs(400.0F, 0.5F));
    CHECK_THAT(large.children[0].size.width, WithinAbs(512.0F, 0.5F));
}

TEST_CASE("nested_constraint_propagation", "[layout]") {
    TextStyle style;
    style.fontSize = 20.0F;
    Widget inner = makeText("Hi", style);
    Widget column = makeColumn({inner}, MainAxisAlignment::Start,
                               CrossAxisAlignment::Start, 0.0F,
                               EdgeInsets::all(16.0F));
    const RenderNode node = LayoutEngine::layout(column, tight(800.0F, 600.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.children[0].offset.x, WithinAbs(16.0F, 0.001F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(16.0F, 0.001F));
    CHECK_THAT(node.size.width, WithinAbs(800.0F, 0.001F));
    CHECK_THAT(node.size.height, WithinAbs(600.0F, 0.001F));
}

// --- Regression: explicit sizes must drive child constraints, not the
// incoming constraint maximum (bug found in the stage 0/1 review). ---

TEST_CASE("row_fixed_width_constrains_flex_children", "[layout]") {
    Widget flexChild = withFlex(makeContainerLeaf(std::nullopt, 50.0F), 1.0F);
    Widget row = makeRow({flexChild}, MainAxisAlignment::Start,
                         CrossAxisAlignment::Start, 0.0F, {}, {}, "", 200.0F,
                         std::nullopt);
    const RenderNode node =
        LayoutEngine::layout(row, loose(800.0F, 600.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.size.width, WithinAbs(200.0F, 0.01F));
    // Flex budget comes from the fixed border box (200), not loose max (800).
    CHECK_THAT(node.children[0].size.width, WithinAbs(200.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.x, WithinAbs(0.0F, 0.01F));
}

TEST_CASE("row_fixed_width_clamps_oversized_child", "[layout]") {
    Widget row = makeRow({makeContainerLeaf(300.0F, 30.0F)},
                         MainAxisAlignment::Start, CrossAxisAlignment::Start,
                         0.0F, {}, {}, "", 200.0F, std::nullopt);
    const RenderNode node =
        LayoutEngine::layout(row, loose(800.0F, 600.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.children[0].size.width, WithinAbs(200.0F, 0.01F));
    CHECK_THAT(node.size.width, WithinAbs(200.0F, 0.01F));
}

TEST_CASE("column_fixed_height_constrains_flex_children", "[layout]") {
    Widget flexChild = withFlex(makeContainerLeaf(50.0F, std::nullopt), 1.0F);
    Widget column =
        makeColumn({flexChild}, MainAxisAlignment::Start,
                   CrossAxisAlignment::Start, 0.0F, {}, {}, "", std::nullopt,
                   100.0F);
    const RenderNode node =
        LayoutEngine::layout(column, loose(800.0F, 600.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.size.height, WithinAbs(100.0F, 0.01F));
    CHECK_THAT(node.children[0].size.height, WithinAbs(100.0F, 0.01F));
}

TEST_CASE("stack_fixed_size_constrains_children", "[layout]") {
    Widget stack = makeStack({makeContainerLeaf(300.0F, 30.0F)},
                             StackAlignment::TopLeft, {}, {}, "", 100.0F,
                             std::nullopt);
    const RenderNode node =
        LayoutEngine::layout(stack, loose(800.0F, 600.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.size.width, WithinAbs(100.0F, 0.01F));
    CHECK_THAT(node.children[0].size.width, WithinAbs(100.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.x, WithinAbs(0.0F, 0.01F));
}

// --- Regression: the parent consumes the child margin (Container used to
// ignore it when positioning and sizing). ---

TEST_CASE("container_consumes_child_margin", "[layout]") {
    Widget child =
        makeContainerLeaf(100.0F, 100.0F, {}, EdgeInsets::all(10.0F));
    Widget widget =
        makeContainer(std::move(child), std::nullopt, std::nullopt,
                      EdgeInsets::all(10.0F));
    const RenderNode node =
        LayoutEngine::layout(widget, loose(800.0F, 600.0F));
    REQUIRE(node.children.size() == 1);
    // Child sits at padding + margin on each axis...
    CHECK_THAT(node.children[0].offset.x, WithinAbs(20.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(20.0F, 0.01F));
    CHECK_THAT(node.children[0].size.width, WithinAbs(100.0F, 0.01F));
    CHECK_THAT(node.children[0].size.height, WithinAbs(100.0F, 0.01F));
    // ...and the border box grows by child margin + padding on both axes.
    CHECK_THAT(node.size.width, WithinAbs(140.0F, 0.01F));
    CHECK_THAT(node.size.height, WithinAbs(140.0F, 0.01F));
}

// --- Coverage for Space* alignments, End alignment and spacing. ---

TEST_CASE("row_space_between_respects_minimum_spacing", "[layout]") {
    Widget row = makeRow({makeContainerLeaf(100.0F, 10.0F),
                          makeContainerLeaf(100.0F, 10.0F)},
                         MainAxisAlignment::SpaceBetween,
                         CrossAxisAlignment::Start, 10.0F);
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));
    REQUIRE(node.children.size() == 2);
    CHECK_THAT(node.children[0].offset.x, WithinAbs(0.0F, 0.01F));
    // 600 - 200 content - 10 spacing = 390 flexible; second child at
    // 100 + 10 + 390 = 500 (spacing is the minimum gap, the rest is added).
    CHECK_THAT(node.children[1].offset.x, WithinAbs(500.0F, 0.01F));
}

TEST_CASE("row_space_around_distributes_half_gaps", "[layout]") {
    Widget row = makeRow({makeContainerLeaf(100.0F, 10.0F),
                          makeContainerLeaf(100.0F, 10.0F),
                          makeContainerLeaf(100.0F, 10.0F)},
                         MainAxisAlignment::SpaceAround);
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));
    REQUIRE(node.children.size() == 3);
    // 300 free space => 100 per child slot, half a slot on each outer edge.
    CHECK_THAT(node.children[0].offset.x, WithinAbs(50.0F, 0.01F));
    CHECK_THAT(node.children[1].offset.x, WithinAbs(250.0F, 0.01F));
    CHECK_THAT(node.children[2].offset.x, WithinAbs(450.0F, 0.01F));
}

TEST_CASE("row_space_evenly_distributes_gaps", "[layout]") {
    Widget row = makeRow({makeContainerLeaf(100.0F, 10.0F),
                          makeContainerLeaf(100.0F, 10.0F),
                          makeContainerLeaf(100.0F, 10.0F)},
                         MainAxisAlignment::SpaceEvenly);
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));
    REQUIRE(node.children.size() == 3);
    // 300 free space over 4 gaps => 75 per gap.
    CHECK_THAT(node.children[0].offset.x, WithinAbs(75.0F, 0.01F));
    CHECK_THAT(node.children[1].offset.x, WithinAbs(250.0F, 0.01F));
    CHECK_THAT(node.children[2].offset.x, WithinAbs(425.0F, 0.01F));
}

TEST_CASE("row_main_axis_end_alignment", "[layout]") {
    Widget row = makeRow({makeContainerLeaf(100.0F, 50.0F)},
                         MainAxisAlignment::End);
    const RenderNode node = LayoutEngine::layout(row, tight(600.0F, 100.0F));
    REQUIRE(node.children.size() == 1);
    CHECK_THAT(node.children[0].offset.x, WithinAbs(500.0F, 0.01F));
    CHECK_THAT(node.children[0].offset.y, WithinAbs(0.0F, 0.01F));
}

TEST_CASE("text_measures_cjk_by_code_points", "[layout]") {
    // "你好世界" is 4 glyphs / 12 UTF-8 bytes; measurement counts glyphs.
    const RenderNode node = LayoutEngine::layout(
        makeText("\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c"),
        Constraints::unbounded());
    CHECK_THAT(node.size.width, WithinAbs(33.6F, 0.01F));
    CHECK_THAT(node.size.height, WithinAbs(16.8F, 0.01F));
}
