#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "lumen/core/geometry.h"

using namespace lumen::core;
using Catch::Matchers::WithinAbs;

TEST_CASE("constraints_tight_is_single_size", "[geometry]") {
    const auto constraints = Constraints::tight(Size{800.0F, 600.0F});
    CHECK(constraints.isTight());
    CHECK(constraints.biggest() == Size{800.0F, 600.0F});
    CHECK(constraints.smallest() == Size{800.0F, 600.0F});
}

TEST_CASE("constraints_loose_allows_shrink", "[geometry]") {
    const auto constraints = Constraints::loose(Size{800.0F, 600.0F});
    CHECK_FALSE(constraints.isTight());
    CHECK(constraints.constrain(Size{100.0F, 100.0F}) ==
          Size{100.0F, 100.0F});
    CHECK(constraints.constrain(Size{900.0F, 700.0F}) ==
          Size{800.0F, 600.0F});
    CHECK(constraints.constrain(Size{-10.0F, -5.0F}) == Size{0.0F, 0.0F});
}

TEST_CASE("constraints_clamp_to_min_max", "[geometry]") {
    const Constraints constraints{100.0F, 400.0F, 50.0F, 300.0F};
    CHECK(constraints.constrain(Size{10.0F, 10.0F}) == Size{100.0F, 50.0F});
    CHECK(constraints.constrain(Size{500.0F, 500.0F}) ==
          Size{400.0F, 300.0F});
    CHECK(constraints.constrain(Size{200.0F, 150.0F}) ==
          Size{200.0F, 150.0F});
}

TEST_CASE("constraints_deflate_by_insets", "[geometry]") {
    const auto constraints = Constraints::tight(Size{800.0F, 600.0F});
    const auto deflated = constraints.deflate(EdgeInsets::all(16.0F));
    CHECK(deflated == Constraints{768.0F, 768.0F, 568.0F, 568.0F});

    const auto loose = Constraints::loose(Size{800.0F, 600.0F});
    const auto looseDeflated =
        loose.deflate(EdgeInsets::symmetric(10.0F, 20.0F));
    CHECK(looseDeflated.minWidth == 0.0F);
    CHECK(looseDeflated.maxWidth == 780.0F);
    CHECK(looseDeflated.minHeight == 0.0F);
    CHECK(looseDeflated.maxHeight == 560.0F);
}

TEST_CASE("edge_insets_measure", "[geometry]") {
    CHECK(EdgeInsets::all(8.0F).horizontal() == 16.0F);
    CHECK(EdgeInsets::all(8.0F).vertical() == 16.0F);
    CHECK(EdgeInsets::symmetric(10.0F, 20.0F).horizontal() == 20.0F);
    CHECK(EdgeInsets::symmetric(10.0F, 20.0F).vertical() == 40.0F);
    const auto only = EdgeInsets::only(1.0F, 2.0F, 3.0F, 4.0F);
    CHECK(only.horizontal() == 4.0F);
    CHECK(only.vertical() == 6.0F);
}

TEST_CASE("rect_contains_point", "[geometry]") {
    const auto rect = Rect::fromXYWH(10.0F, 20.0F, 100.0F, 50.0F);
    CHECK(rect.left() == 10.0F);
    CHECK(rect.top() == 20.0F);
    CHECK(rect.right() == 110.0F);
    CHECK(rect.bottom() == 70.0F);
    CHECK(rect.contains(Offset{50.0F, 40.0F}));
    CHECK_FALSE(rect.contains(Offset{5.0F, 40.0F}));
    // Right/bottom edges are exclusive so adjacent nodes do not overlap.
    CHECK_FALSE(rect.contains(Offset{110.0F, 40.0F}));
    CHECK_FALSE(rect.contains(Offset{50.0F, 70.0F}));
}

TEST_CASE("color_and_style_defaults", "[geometry]") {
    CHECK(Color::transparent() == Color{0, 0, 0, 0});
    CHECK(CornerRadius::zero() == CornerRadius{0.0F, 0.0F, 0.0F, 0.0F});
    CHECK(CornerRadius::all(8.0F).topLeft == 8.0F);
    CHECK(TextStyle{}.fontSize == 14.0F);
}
