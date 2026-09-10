// Damage tracking tests (plan 阶段6: 脏矩形).

#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "lumen/core/damage.h"
#include "lumen/layout/layout.h"
#include "lumen/core/render_node.h"

using namespace lumen::core;
using namespace lumen::layout;
using Catch::Matchers::WithinAbs;

namespace {

RenderNode layoutOf(const Widget& widget, float width = 300.0F,
                    float height = 400.0F) {
    return LayoutEngine::layout(
        widget, Constraints::tight(Size{width, height}));
}

}  // namespace

TEST_CASE("damage_none_when_trees_identical", "[damage]") {
    const RenderNode a = layoutOf(
        makeColumn({withKey(makeText("a"), "one"), withKey(makeText("b"), "two")}));
    const RenderNode b = layoutOf(
        makeColumn({withKey(makeText("a"), "one"), withKey(makeText("b"), "two")}));
    std::vector<Rect> damage;
    REQUIRE(collectDamage(a, b, damage));
    CHECK(damage.empty());
}

TEST_CASE("damage_localizes_text_change_to_its_rect", "[damage]") {
    const RenderNode before = layoutOf(
        makeColumn({withKey(makeText("a"), "one"), withKey(makeText("b"), "two")}));
    const RenderNode after = layoutOf(makeColumn(
        {withKey(makeText("a"), "one"), withKey(makeText("z"), "two")}));
    std::vector<Rect> damage;
    REQUIRE(collectDamage(before, after, damage));
    const Rect expected = Rect{absoluteOffset(after, "two"),
                               findNodeByKey(after, "two")->size};
    REQUIRE(damage.size() == 1);
    CHECK(damage[0] == expected);
}

TEST_CASE("damage_covers_old_and_new_positions_on_move", "[damage]") {
    const RenderNode before =
        layoutOf(makeColumn({withKey(makeText("a"), "one")}, MainAxisAlignment::Start));
    const RenderNode after = layoutOf(
        makeColumn({withKey(makeText("a"), "one")}, MainAxisAlignment::End));
    std::vector<Rect> damage;
    REQUIRE(collectDamage(before, after, damage));
    CHECK(damage.size() == 2);
    CHECK(damage[0] == Rect{Offset{}, findNodeByKey(before, "one")->size});
    // MainAxis End moves the row down, not sideways (cross axis Start).
    CHECK(damage[1].origin.y > 0.0F);
}

TEST_CASE("damage_covers_added_and_removed_subtrees", "[damage]") {
    const Widget pair = makeColumn(
        {withKey(makeText("a"), "one"), withKey(makeText("b"), "two")});
    const RenderNode before = layoutOf(pair);
    const RenderNode after = layoutOf(
        makeColumn({withKey(makeText("a"), "one"),
                    withKey(makeText("b"), "two"),
                    withKey(makeButton("New"), "added")}));
    std::vector<Rect> damage;
    REQUIRE(collectDamage(before, after, damage));
    // The added button subtree is the only damaged area.
    REQUIRE(damage.size() == 1);
    CHECK(damage[0] == Rect{absoluteOffset(after, "added"),
                            findNodeByKey(after, "added")->size});

    damage.clear();
    REQUIRE(collectDamage(after, before, damage));
    REQUIRE(damage.size() == 1);
    CHECK(damage[0] == Rect{absoluteOffset(after, "added"),
                            findNodeByKey(after, "added")->size});
}

TEST_CASE("damage_deep_change_spares_sibling", "[damage]") {
    const auto build = [](const char* second) {
        return layoutOf(makeColumn({
            withKey(makeText("first"), "first"),
            withKey(makeColumn({makeText("x"), withKey(makeText(second), "deep")}),
                   "inner"),
        }));
    };
    const RenderNode before = build("old");
    const RenderNode after = build("new");
    std::vector<Rect> damage;
    REQUIRE(collectDamage(before, after, damage));
    REQUIRE(damage.size() == 1);
    // Damage lies strictly inside the inner column, below the first row.
    CHECK(damage[0].origin.y > findNodeByKey(before, "first")->rect().bottom());
}

TEST_CASE("damage_returns_false_for_mismatched_roots", "[damage]") {
    const RenderNode a =
        layoutOf(withKey(makeText("a"), "one"));
    RenderNode b = a;
    b.identity = "/different";
    std::vector<Rect> damage;
    CHECK_FALSE(collectDamage(a, b, damage));
}

TEST_CASE("damage_bounds_unions_and_clamps_to_viewport", "[damage]") {
    const std::vector<Rect> rects{Rect::fromXYWH(-10.0F, -10.0F, 30.0F, 30.0F),
                                  Rect::fromXYWH(100.0F, 50.0F, 2000.0F, 20.0F)};
    const auto bounds = damageBounds(rects, Size{300.0F, 200.0F});
    REQUIRE(bounds.has_value());
    CHECK_THAT(bounds->left(), WithinAbs(0.0F, 0.001F));
    CHECK_THAT(bounds->top(), WithinAbs(0.0F, 0.001F));
    CHECK_THAT(bounds->right(), WithinAbs(300.0F, 0.001F));
    CHECK_THAT(bounds->bottom(), WithinAbs(70.0F, 0.001F));

    const std::vector<Rect> offscreen{
        Rect::fromXYWH(500.0F, 500.0F, 10.0F, 10.0F)};
    CHECK_FALSE(damageBounds(offscreen, Size{300.0F, 200.0F}).has_value());
    CHECK_FALSE(damageBounds({}, Size{300.0F, 200.0F}).has_value());
}

TEST_CASE("same_node_ignores_children", "[damage]") {
    const RenderNode parent =
        layoutOf(makeContainer(withKey(makeText("child"), "child")));
    RenderNode copy = parent;
    REQUIRE(copy.children.size() == 1);
    copy.children.front().text = "mutated";
    CHECK(sameNode(parent, copy));
    CHECK_FALSE(parent == copy);
}

TEST_CASE("damage_padding_change_covers_moved_children", "[damage]") {
    // A padding change keeps the container rect but shifts children; the
    // damage must cover the whole subtree, not just the container.
    const RenderNode before = layoutOf(
        makeColumn({withKey(makeText("child"), "child")}, MainAxisAlignment::Start,
                   CrossAxisAlignment::Start, 0.0F, EdgeInsets::all(0.0F)));
    const RenderNode after = layoutOf(
        makeColumn({withKey(makeText("child"), "child")}, MainAxisAlignment::Start,
                   CrossAxisAlignment::Start, 0.0F, EdgeInsets::all(10.0F)));
    std::vector<Rect> damage;
    REQUIRE(collectDamage(before, after, damage));
    REQUIRE_FALSE(damage.empty());
    float maxRight = 0.0F;
    float maxBottom = 0.0F;
    for (const Rect& rect : damage) {
        maxRight = std::max(maxRight, rect.right());
        maxBottom = std::max(maxBottom, rect.bottom());
    }
    // The child's new position (10, 10) + text size lies inside the union.
    const auto* moved = findNodeByKey(after, "child");
    CHECK(maxRight >= moved->rect().right() + 10.0F);
    CHECK(maxBottom >= moved->rect().bottom() + 10.0F);
}

TEST_CASE("damage_does_not_pair_different_keyed_nodes_positionally", "[damage]") {
    RenderNode before;
    before.identity = "/root";
    before.size = Size{100.0F, 100.0F};
    RenderNode oldChild;
    oldChild.key = "old";
    oldChild.identity = "/root/old";
    oldChild.size = Size{10.0F, 10.0F};
    RenderNode overflow;
    overflow.identity = "/root/old/overflow";
    overflow.offset = Offset{20.0F, 0.0F};
    overflow.size = Size{5.0F, 5.0F};
    oldChild.children.push_back(overflow);
    before.children.push_back(oldChild);

    RenderNode after = before;
    after.children.clear();
    RenderNode newChild = oldChild;
    newChild.key = "new";
    newChild.identity = "/root/new";
    newChild.children.clear();
    after.children.push_back(newChild);

    std::vector<Rect> damage;
    REQUIRE(collectDamage(before, after, damage));
    // The old overflow area must be repainted when the keyed node is removed;
    // positional fallback would incorrectly lose this rectangle.
    CHECK(std::any_of(damage.begin(), damage.end(), [](const Rect& rect) {
        return rect.origin.x == 20.0F && rect.origin.y == 0.0F &&
               rect.size == Size{5.0F, 5.0F};
    }));
}
