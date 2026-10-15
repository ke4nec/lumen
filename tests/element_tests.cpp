#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>
#include <vector>

#include "lumen/core/element.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/platform/platform_window.h"
#include "lumen/render/renderer.h"

using namespace lumen::core;

namespace {

// 超过 keyed 索引阈值（32）的大同级列表构造器：keyed 文本"k<i>"。
std::vector<Widget> keyedTexts(std::size_t count, std::string prefix = "k") {
    std::vector<Widget> children;
    children.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        children.push_back(
            makeText("v" + std::to_string(i), {}, {}, 0.0F,
                     prefix + std::to_string(i)));
    }
    return children;
}

}  // namespace

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

TEST_CASE("element_large_keyed_list_reuses_in_order", "[element]") {
    // 超阈值 keyed 列表走索引路径：稳态更新全部复用、顺序不变、值更新。
    constexpr std::size_t kCount = 1500;
    Element element(makeRow(keyedTexts(kCount)));
    std::vector<const Element*> before;
    before.reserve(kCount);
    for (const auto& child : element.children()) before.push_back(child.get());

    element.update(makeRow(keyedTexts(kCount)));

    REQUIRE(element.children().size() == kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        CHECK(element.children()[i].get() == before[i]);
        CHECK(element.children()[i]->widget().key ==
              "k" + std::to_string(i));
    }
}

TEST_CASE("element_stable_children_keep_storage_and_update_descendants",
          "[element][stable-reconcile]") {
    for (const std::size_t count : {4U, 1500U}) {
        const auto makeTree = [count](const std::string& value) {
            std::vector<Widget> children;
            children.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                auto child = makeColumn({makeText(value)});
                child.key = i % 3 == 0 ? "" : i % 3 == 1 ? "duplicate"
                                                         : std::to_string(i);
                children.push_back(std::move(child));
            }
            return makeRow(std::move(children));
        };
        Element element(makeTree("before"));
        const auto* storage = element.children().data();
        std::vector<const Element*> parents;
        std::vector<const Element*> leaves;
        for (const auto& child : element.children()) {
            parents.push_back(child.get());
            leaves.push_back(child->children().front().get());
        }
        element.clearDirtyTree();
        element.update(makeTree("after"));

        REQUIRE(element.children().size() == count);
        CHECK(element.children().data() == storage);
        CHECK(element.isDirty());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& child = element.children()[i];
            REQUIRE(child.get() == parents[i]);
            REQUIRE(child->children().front().get() == leaves[i]);
            CHECK(child->parent() == &element);
            CHECK(child->children().front()->parent() == child.get());
            CHECK(child->isDirty());
            CHECK(child->children().front()->isDirty());
            CHECK(child->children().front()->widget().text == "after");
        }
    }
}

TEST_CASE("element_late_reorder_preserves_duplicate_key_ownership",
          "[element][stable-reconcile]") {
    Element element(makeRow({makeText("first", {}, {}, 0.0F, "duplicate"),
                             makeText("middle", {}, {}, 0.0F, "middle"),
                             makeText("last", {}, {}, 0.0F, "duplicate")}));
    const auto* first = element.children()[0].get();
    const auto* middle = element.children()[1].get();
    const auto* last = element.children()[2].get();
    element.update(makeRow({makeText("first updated", {}, {}, 0.0F, "duplicate"),
                            makeText("last updated", {}, {}, 0.0F, "duplicate"),
                            makeText("middle updated", {}, {}, 0.0F, "middle")}));
    REQUIRE(element.children().size() == 3);
    CHECK(element.children()[0].get() == first);
    CHECK(element.children()[1].get() == last);
    CHECK(element.children()[2].get() == middle);
    CHECK(element.children()[0]->widget().text == "first updated");
    CHECK(element.children()[1]->widget().text == "last updated");
    CHECK(element.children()[2]->widget().text == "middle updated");
}

TEST_CASE("element_large_keyed_list_reorder_insert_remove", "[element]") {
    // 逆序 + 头插 + 尾删：索引路径的复用映射与线性扫描一致
    //（取最低未用下标，状态跟随旧节点）。
    constexpr std::size_t kCount = 100;
    Element element(makeRow(keyedTexts(kCount)));
    std::vector<const Element*> before;
    for (const auto& child : element.children()) before.push_back(child.get());

    std::vector<Widget> reordered;
    reordered.reserve(kCount);
    reordered.push_back(makeText("new", {}, {}, 0.0F, "k-new"));
    for (std::size_t i = kCount; i-- > 1;) {
        reordered.push_back(makeText("v" + std::to_string(i), {}, {}, 0.0F,
                                     "k" + std::to_string(i)));
    }
    element.update(makeRow(std::move(reordered)));

    // k0 被删（新节点顶替头部），k-new 为新节点，其余逆序复用。
    REQUIRE(element.children().size() == kCount);
    CHECK(element.children()[0]->widget().key == "k-new");
    for (std::size_t i = 1; i < kCount; ++i) {
        const std::size_t oldIndex = kCount - i;
        CHECK(element.children()[i].get() == before[oldIndex]);
        CHECK(element.children()[i]->widget().key ==
              "k" + std::to_string(oldIndex));
    }
}

TEST_CASE("element_large_keyed_duplicates_keep_lowest_unused_order",
          "[element]") {
    // 重复 key：两次更新消费最低未用下标，顺序与扫描口径一致。
    constexpr std::size_t kGroups = 40;
    std::vector<Widget> first;
    for (std::size_t i = 0; i < kGroups; ++i) {
        first.push_back(makeText("a" + std::to_string(i), {}, {}, 0.0F, "dup"));
        first.push_back(makeText("b" + std::to_string(i), {}, {}, 0.0F,
                                 "u" + std::to_string(i)));
    }
    Element element(makeRow(std::move(first)));
    std::vector<const Element*> dupBefore;
    for (const auto& child : element.children()) {
        if (child->widget().key == "dup") dupBefore.push_back(child.get());
    }
    REQUIRE(dupBefore.size() == kGroups);

    std::vector<Widget> second;
    for (std::size_t i = 0; i < kGroups; ++i) {
        second.push_back(makeText("c" + std::to_string(i), {}, {}, 0.0F, "dup"));
    }
    element.update(makeRow(std::move(second)));

    // 40 个 dup 按旧下标升序复用，u* 旧节点全部销毁。
    REQUIRE(element.children().size() == kGroups);
    for (std::size_t i = 0; i < kGroups; ++i) {
        CHECK(element.children()[i].get() == dupBefore[i]);
        CHECK(element.children()[i]->widget().text ==
              "c" + std::to_string(i));
    }
}

TEST_CASE("element_large_mixed_keyless_and_keyed", "[element]") {
    // 混合列表：无 key 走位置口径（不跨位迁移状态），有 key 走索引迁移。
    constexpr std::size_t kCount = 60;
    std::vector<Widget> first;
    for (std::size_t i = 0; i < kCount; ++i) {
        if (i % 2 == 0) {
            first.push_back(makeText("plain" + std::to_string(i)));
        } else {
            first.push_back(makeText("v" + std::to_string(i), {}, {}, 0.0F,
                                     "k" + std::to_string(i)));
        }
    }
    Element element(makeRow(std::move(first)));
    const Element* plainBefore = element.children()[0].get();
    const Element* keyedBefore = element.children()[1].get();

    // 交换前两位：keyless 按位置复用（文本被覆写），keyed 按 key 迁移。
    std::vector<Widget> second;
    second.push_back(makeText("v1", {}, {}, 0.0F, "k1"));
    second.push_back(makeText("moved-plain"));
    for (std::size_t i = 2; i < kCount; ++i) {
        if (i % 2 == 0) {
            second.push_back(makeText("plain" + std::to_string(i)));
        } else {
            second.push_back(makeText("v" + std::to_string(i), {}, {}, 0.0F,
                                      "k" + std::to_string(i)));
        }
    }
    element.update(makeRow(std::move(second)));

    REQUIRE(element.children().size() == kCount);
    // 位置 0：旧 keyless 节点被 keyed k1 顶替 → 新节点（keyless 旧节点
    // 状态不迁移到 keyed 位置）。
    CHECK(element.children()[0].get() == keyedBefore);
    // 位置 1：keyless 按位置复用旧位置 1 的节点？位置 1 旧节点已被移走
    //（k1 迁到位置 0），故此处为新 keyless 节点。
    CHECK(element.children()[1].get() != plainBefore);
    CHECK(element.children()[1].get() != keyedBefore);
    CHECK(element.children()[1]->widget().key.empty());
}

TEST_CASE("element_large_keyed_distinguishes_type", "[element]") {
    // 同 key 不同 type：分属不同 (type,key) 组，不可互复用。
    constexpr std::size_t kCount = 40;
    std::vector<Widget> first;
    for (std::size_t i = 0; i < kCount; ++i) {
        first.push_back(makeText("t" + std::to_string(i), {}, {}, 0.0F, "same"));
    }
    Element element(makeRow(std::move(first)));
    std::vector<const Element*> textBefore;
    for (const auto& child : element.children()) {
        textBefore.push_back(child.get());
    }

    std::vector<Widget> second;
    for (std::size_t i = 0; i < kCount; ++i) {
        second.push_back(
            makeButton("b" + std::to_string(i), {}, {}, 0.0F, "same"));
    }
    element.update(makeRow(std::move(second)));

    // 同 key 但 type 不同 → 全部新建，旧节点无一复用。
    REQUIRE(element.children().size() == kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        CHECK(element.children()[i]->widget().type == WidgetType::Button);
        for (const Element* old : textBefore) {
            CHECK(element.children()[i].get() != old);
        }
    }
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
