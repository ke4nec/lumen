// 集合控件（docs/lumen-collection-controls-design.md）测试：
// SelectionModel 四模式语义、ListController 行包装/选择/键盘/滚动对齐、
// TreeController 扁平化与展开状态、TreeListController 列宽/粘性表头、
// 行渲染契约（选中背景/焦点环）与语义角色。
//
// 命名遵循项目测试规范（行为命名，*_tests.cpp）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/virtual_list.h"
#include "lumen/layout/layout.h"
#include "lumen/widgets/list.h"
#include "lumen/widgets/selection.h"
#include "lumen/widgets/tree.h"

using namespace lumen;
using namespace lumen::core;
using lumen::widgets::ListController;
using lumen::widgets::SelectionMode;
using lumen::widgets::SelectionModel;
using lumen::widgets::TreeController;
using lumen::widgets::TreeListColumn;
using lumen::widgets::TreeListController;
using lumen::widgets::TreeModel;
using lumen::layout::LayoutEngine;

namespace {

Constraints tightView(float width, float height) {
    return Constraints::tight(Size{width, height});
}

// --- SelectionModel ---

class SequenceFixture {
  public:
    SelectionModel model;

    SequenceFixture() {
        model.setKeySequence([](const std::string& from,
                                const std::string& to) {
            // 稳定行序 a b c d e。
            const std::vector<std::string> order{"a", "b", "c", "d", "e"};
            std::vector<std::string> keys;
            const auto at = [&order](const std::string& key) {
                for (std::size_t i = 0; i < order.size(); ++i) {
                    if (order[i] == key) {
                        return static_cast<int>(i);
                    }
                }
                return -1;
            };
            int begin = at(from);
            int end = at(to);
            if (begin < 0 || end < 0) {
                return keys;
            }
            if (begin > end) {
                std::swap(begin, end);
            }
            for (int i = begin; i <= end; ++i) {
                keys.push_back(order[static_cast<std::size_t>(i)]);
            }
            return keys;
        });
    }
};

// --- List fixtures ---

struct ListApp {
    app::AppShell shell{makeConfig()};
    ListController list;
    int activated{0};
    std::string lastActivated{};

    static app::ShellConfig makeConfig() {
        app::ShellConfig config;
        config.build = [] { return Widget{}; };
        return config;
    }

    ListApp() {
        list.setItemBuilder([](std::size_t index) {
            return makeText("Item " + std::to_string(index));
        });
        list.attach(shell, "files");
        list.setSelectionMode(SelectionMode::Extended);
        list.onActivated = [this](const std::string& key) {
            ++activated;
            lastActivated = key;
        };
    }

    void build() {
        shell.swapRoot(makeList(&list, "files", std::nullopt, 360.0F));
        shell.rebuildIfDirty();
    }
};

// --- Tree fixtures ---

class FlatTreeModel final : public TreeModel {
  public:
    struct Node {
        std::string key;
        std::vector<std::string> children;
    };
    std::vector<Node> nodes;
    mutable std::size_t buildRowCalls{0};

    static std::vector<Node> sample() {
        return {Node{"root1", {"a", "b"}},
                Node{"root2", {}},
                Node{"root3", {"c"}}};
    }

    [[nodiscard]] const Node* nodeOf(const std::string& key) const {
        for (const auto& node : nodes) {
            if (node.key == key) {
                return &node;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t childCount(
        const std::string& parent) const override {
        if (parent.empty()) {
            return nodes.size();
        }
        const Node* node = nodeOf(parent);
        return node != nullptr ? node->children.size() : 0;
    }

    [[nodiscard]] std::string childAt(const std::string& parent,
                                      std::size_t index) const override {
        if (parent.empty()) {
            return index < nodes.size() ? nodes[index].key : "";
        }
        const Node* node = nodeOf(parent);
        if (node == nullptr || index >= node->children.size()) {
            return "";
        }
        return node->children[index];
    }

    [[nodiscard]] bool hasChildren(const std::string& key) const override {
        const Node* node = nodeOf(key);
        return node != nullptr && !node->children.empty();
    }

    [[nodiscard]] Widget buildRow(const std::string& key,
                                  std::size_t depth) const override {
        ++buildRowCalls;
        Widget text = makeText(key);
        text.margin = EdgeInsets::only(static_cast<float>(depth), 0.0F);
        return text;
    }
};

struct TreeApp {
    app::AppShell shell{ListApp::makeConfig()};
    FlatTreeModel model;
    TreeController tree;
    std::string lastActivated{};

    TreeApp() {
        model.nodes = FlatTreeModel::sample();
        tree.setModel(&model);
        tree.attach(shell, "tree");
        tree.setSelectionMode(SelectionMode::Single);
        tree.onActivated = [this](const std::string& key) {
            lastActivated = key;
        };
    }

    void build() {
        shell.swapRoot(makeTree(&tree, "tree", std::nullopt, 360.0F));
        shell.rebuildIfDirty();
    }
};

}  // namespace

// --- SelectionModel：四模式 ---

TEST_CASE("selection_extended_click_resets_and_modifiers_toggle_range",
          "[collection]") {
    SequenceFixture f;
    f.model.setMode(SelectionMode::Extended);

    f.model.click("b", false, false);
    REQUIRE(f.model.isSelected("b"));
    REQUIRE(f.model.selectedCount() == 1);

    // Ctrl+单击切换（不影响其余选择）。
    f.model.click("d", true, false);
    REQUIRE(f.model.isSelected("b"));
    REQUIRE(f.model.isSelected("d"));
    REQUIRE(f.model.selectedCount() == 2);

    // Shift+单击区间（anchor=d → f 无效 key 序列外由 fixture 返回空，
    // 用真实行 e）。
    f.model.click("e", false, true);
    REQUIRE(f.model.isSelected("d"));
    REQUIRE(f.model.isSelected("e"));
    CHECK_FALSE(f.model.isSelected("a"));

    // 无修饰单击重置。
    f.model.click("a", false, false);
    REQUIRE(f.model.selectedCount() == 1);
    REQUIRE(f.model.isSelected("a"));
}

TEST_CASE("selection_single_click_replaces_selection", "[collection]") {
    SequenceFixture f;
    f.model.setMode(SelectionMode::Single);
    f.model.click("a", false, false);
    f.model.click("c", false, false);
    REQUIRE(f.model.selectedCount() == 1);
    REQUIRE(f.model.isSelected("c"));
    // Ctrl/Shift 在 Single 下同单击。
    f.model.click("d", true, true);
    REQUIRE(f.model.selectedCount() == 1);
    REQUIRE(f.model.isSelected("d"));
}

TEST_CASE("selection_multiple_click_toggles", "[collection]") {
    SequenceFixture f;
    f.model.setMode(SelectionMode::Multiple);
    f.model.click("a", false, false);
    f.model.click("c", false, false);
    REQUIRE(f.model.selectedCount() == 2);
    f.model.click("a", false, false);
    REQUIRE(f.model.selectedCount() == 1);
    REQUIRE_FALSE(f.model.isSelected("a"));
}

TEST_CASE("selection_current_moves_independently_of_selected_set",
          "[collection]") {
    SequenceFixture f;
    f.model.setMode(SelectionMode::Extended);
    f.model.click("a", false, false);
    f.model.moveTo("c", false);
    // current 移动即随动选中（Qt Extended 键盘语义）。
    CHECK(f.model.currentKey() == "c");
    CHECK(f.model.isSelected("c"));
    // None 模式下键盘只移 current。
    f.model.setMode(SelectionMode::None);
    f.model.moveTo("d", false);
    CHECK(f.model.currentKey() == "d");
    CHECK_FALSE(f.model.isSelected("d"));
}

TEST_CASE("selection_mode_none_clears_and_single_shrinks", "[collection]") {
    SequenceFixture f;
    f.model.setMode(SelectionMode::Extended);
    f.model.setSelected({"a", "b", "c"});
    f.model.setMode(SelectionMode::Single);
    // 收紧到 Single：选择集裁剪（无 current 时清空）。
    CHECK(f.model.selectedCount() <= 1);
    f.model.setMode(SelectionMode::None);
    CHECK(f.model.selectedCount() == 0);
}

// --- ListController ---

TEST_CASE("list_builds_focused_collection_rows_with_selected_state",
          "[collection]") {
    ListApp app;
    app.list.setItemCount(20);
    app.build();

    const Widget first = app.list.buildItem(0);
    REQUIRE(first.type == WidgetType::Row);
    CHECK(first.collectionRow);
    CHECK(first.onClick == "list:files:i0");
    CHECK(first.semanticsRole == "listItem");
    CHECK(first.children.size() == 1);

    app.list.selection().setSelected({"i3"});
    app.list.selection().setCurrent("i3");
    const Widget selected = app.list.buildItem(3);
    CHECK(selected.selected);
}

TEST_CASE("list_layout_materializes_visible_window_only", "[collection]") {
    ListApp app;
    app.list.setItemCount(1000);
    app.build();

    const RenderNode& root = app.shell.root();
    const RenderNode* list = findNodeByKey(root, "files");
    REQUIRE(list != nullptr);
    // 360px 视口 + 200px 缓存：物化行数 O(visible)，远小于千。
    CHECK(list->children.size() < 30);
    CHECK(list->children.size() >= 8);
    // 首行为集合行。
    CHECK(list->children.front().collectionRow);
}

TEST_CASE("list_click_selects_and_double_click_activates", "[collection]") {
    ListApp app;
    app.list.setItemCount(10);
    app.build();

    // 定位第 2 行中心（行高实测，取节点矩形）。
    const RenderNode* list = findNodeByKey(app.shell.root(), "files");
    REQUIRE(list != nullptr);
    REQUIRE(list->children.size() > 2);
    const RenderNode& row = list->children[2];
    const float x = row.offset.x + 40.0F;
    const float y = row.offset.y + row.size.height * 0.5F;

    app.shell.pointerDown(Offset{x, y}, kModifierNone);
    app.shell.pointerUp(Offset{x, y});
    app.shell.rebuildIfDirty();
    CHECK(app.list.selection().isSelected("i2"));
    CHECK(app.list.selection().currentKey() == "i2");

    // 双击（400ms 内同一行）激活。
    app.shell.pointerDown(Offset{x, y});
    app.shell.pointerUp(Offset{x, y});
    CHECK(app.activated == 1);
    CHECK(app.lastActivated == "i2");
}

TEST_CASE("list_ctrl_click_toggles_in_extended_mode", "[collection]") {
    ListApp app;
    app.list.setItemCount(10);
    app.build();

    const RenderNode* list = findNodeByKey(app.shell.root(), "files");
    REQUIRE(list != nullptr);
    const float rowHeight = list->children[1].size.height;
    const float x = 40.0F;

    app.shell.pointerDown(Offset{x, rowHeight * 1.5F}, kModifierCtrl);
    app.shell.pointerUp(Offset{x, rowHeight * 1.5F});
    app.shell.pointerDown(Offset{x, rowHeight * 3.5F}, kModifierCtrl);
    app.shell.pointerUp(Offset{x, rowHeight * 3.5F});
    CHECK(app.list.selection().isSelected("i1"));
    CHECK(app.list.selection().isSelected("i3"));
    CHECK(app.list.selection().selectedCount() == 2);
}

TEST_CASE("list_keyboard_moves_current_and_selects", "[collection]") {
    ListApp app;
    app.list.setItemCount(10);
    app.build();

    CHECK(app.list.handleKey(Key::Down, kModifierNone));
    CHECK(app.list.selection().currentKey() == "i0");
    CHECK(app.list.handleKey(Key::Down, kModifierNone));
    CHECK(app.list.selection().currentKey() == "i1");
    CHECK(app.list.selection().isSelected("i1"));
    CHECK(app.list.handleKey(Key::Up, kModifierNone));
    CHECK(app.list.selection().currentKey() == "i0");
    CHECK(app.list.handleKey(Key::End, kModifierNone));
    CHECK(app.list.selection().currentKey() == "i9");
    CHECK(app.list.handleKey(Key::Home, kModifierNone));
    CHECK(app.list.selection().currentKey() == "i0");
    // 非导航键不消费。
    CHECK_FALSE(app.list.handleKey(Key::Left, kModifierNone));
}

TEST_CASE("list_ctrl_a_selects_all_in_extended_mode", "[collection]") {
    ListApp app;
    app.list.setItemCount(5);
    app.build();

    CHECK(app.list.handleKey(Key::None, kModifierCtrl, 'a'));
    CHECK(app.list.selection().selectedCount() == 5);
}

TEST_CASE("list_scroll_alignment_positions_row", "[collection]") {
    ListApp app;
    app.list.setItemCount(100);
    app.build();
    app.list.selection().setCurrent("i50");
    app.list.scrollToKey("i50", ListController::ScrollAlignment::Start);
    app.shell.rebuildIfDirty();

    const RenderNode* list = findNodeByKey(app.shell.root(), "files");
    REQUIRE(list != nullptr);
    // Start 对齐：目标行顶 ≈ 视口顶。cacheExtent 会物化视口上方
    // 缓存行（首行非目标行），故按 key 定位目标行断言。
    const RenderNode* target = findNodeByKey(*list, "files:item:i50");
    REQUIRE(target != nullptr);
    CHECK(target->offset.y == Catch::Approx(0.0F).margin(1.0F));
}

// --- TreeController ---

TEST_CASE("tree_flattens_visible_rows_and_respects_expansion",
          "[collection]") {
    TreeApp app;
    app.build();

    // 全折叠：3 根行。
    CHECK(app.tree.itemCount() == 3);
    CHECK(app.tree.visibleRows()[0].key == "root1");
    CHECK(app.tree.visibleRows()[0].depth == 0);
    CHECK(app.tree.visibleRows()[0].hasChildren);

    app.tree.expand("root1");
    CHECK(app.tree.itemCount() == 5);
    CHECK(app.tree.visibleRows()[1].key == "a");
    CHECK(app.tree.visibleRows()[1].depth == 1);
    CHECK_FALSE(app.tree.visibleRows()[1].hasChildren);

    app.tree.collapse("root1");
    CHECK(app.tree.itemCount() == 3);

    // 展开状态按 key 持久（数据同 key 重排不丢失）。
    // root1（a,b）+ root3（c）：3 根 + 3 子 = 6。
    app.tree.expand("root1");
    app.tree.expand("root3");
    CHECK(app.tree.itemCount() == 6);
    CHECK(app.tree.isExpanded("root1"));
}

TEST_CASE("tree_row_builds_chevron_only_for_branches", "[collection]") {
    TreeApp app;
    app.build();

    const Widget branch = app.tree.buildItem(0);
    REQUIRE(branch.type == WidgetType::Row);
    REQUIRE(branch.children.size() == 2);
    const Widget& chevron = branch.children.front();
    REQUIRE(chevron.type == WidgetType::Button);
    CHECK(chevron.icon == IconId::ChevronRight);
    CHECK(chevron.onClick == "tree:tree:toggle:root1");
    // 叶行：spacer 而非按钮。
    const Widget leaf = app.tree.buildItem(1);
    REQUIRE(leaf.children.size() == 2);
    CHECK(leaf.children.front().type == WidgetType::Container);
    // treeItem 语义 + 展开值。
    CHECK(branch.semanticsRole == "treeItem");
    CHECK(branch.semanticsValue == "false");
}

TEST_CASE("tree_chevron_click_toggles_without_selecting", "[collection]") {
    TreeApp app;
    app.build();

    const RenderNode* tree = findNodeByKey(app.shell.root(), "tree");
    REQUIRE(tree != nullptr);
    REQUIRE(!tree->children.empty());
    const RenderNode& row = tree->children.front();
    // chevron 在行首（24×24）。
    const float x = row.offset.x + 12.0F;
    const float y = row.offset.y + row.size.height * 0.5F;

    app.shell.pointerDown(Offset{x, y});
    app.shell.pointerUp(Offset{x, y});
    CHECK(app.tree.isExpanded("root1"));
    CHECK_FALSE(app.tree.selection().isSelected("root1"));
}

TEST_CASE("tree_keyboard_left_collapse_right_expand", "[collection]") {
    TreeApp app;
    app.build();

    app.tree.selection().setCurrent("root1");
    // Right：展开。
    CHECK(app.tree.handleKey(Key::Right, kModifierNone));
    CHECK(app.tree.isExpanded("root1"));
    // Right：已展开 → current 移到首个子级。
    CHECK(app.tree.handleKey(Key::Right, kModifierNone));
    CHECK(app.tree.selection().currentKey() == "a");
    // Left：叶行（无子级）→ current 移回父级。
    CHECK(app.tree.handleKey(Key::Left, kModifierNone));
    CHECK(app.tree.selection().currentKey() == "root1");
    // Left：已展开 → 折叠。
    CHECK(app.tree.handleKey(Key::Left, kModifierNone));
    CHECK_FALSE(app.tree.isExpanded("root1"));
}

TEST_CASE("tree_extent_cache_survives_fold_unfold", "[collection]") {
    TreeApp app;
    app.build();
    app.tree.expand("root1");
    app.shell.rebuildIfDirty();

    const auto extentBefore = app.tree.extentOf(1);
    // 折叠再展开：行序回到同 key，实测高度保留（key 缓存）。
    app.tree.collapse("root1");
    app.tree.expand("root1");
    CHECK(app.tree.extentOf(1) == Catch::Approx(extentBefore).margin(0.01F));
}

TEST_CASE("tree_expand_all_respects_guard_limit", "[collection]") {
    TreeApp app;
    app.build();
    CHECK(app.tree.expandAll(100));   // 小树：允许。
    CHECK(app.tree.isExpanded("root1"));
    CHECK(app.tree.itemCount() == 6);  // 3 根 + a/b/c 全展开。
    CHECK_FALSE(app.tree.expandAll(0));  // 上限 0：拒绝。
    app.tree.collapseAll();
    CHECK(app.tree.itemCount() == 3);
}

// --- TreeListController ---

struct TreeListApp {
    // 独立视口：根布局用 tight(view_) 夹取 widget 尺寸，列宽断言基于
    // 360px 视口（800 默认窗口会把 name 弹性列放大）。
    static app::ShellConfig makeConfig() {
        app::ShellConfig config;
        config.build = [] { return Widget{}; };
        config.initialView = Size{360.0F, 600.0F};
        return config;
    }

    app::AppShell shell{makeConfig()};
    FlatTreeModel model;
    TreeListController treeList;
    std::string lastSortColumn;
    bool lastSortDescending{false};

    TreeListApp() {
        model.nodes = FlatTreeModel::sample();
        treeList.setModel(&model);
        treeList.attach(shell, "deps");
        treeList.setSelectionMode(SelectionMode::Single);
        treeList.setColumns({{"name", "名称", 0.0F, 1.0F, 120.0F, true, true},
                             {"type", "类型", 96.0F, 0.0F, 48.0F, true, false},
                             {"size", "大小", 72.0F, 0.0F, 48.0F, true, true}});
        treeList.setCellBuilder(
            [](const std::string& rowKey, const std::string& columnId) {
                if (columnId == "name") {
                    return makeText(rowKey);
                }
                return makeText("-");
            });
        treeList.onHeaderClick = [this](const std::string& columnId,
                                        bool descending) {
            lastSortColumn = columnId;
            lastSortDescending = descending;
        };
    }

    void build() {
        shell.swapRoot(makeTreeList(&treeList, &treeList.columns(), true,
                                    "deps", 360.0F, 360.0F));
        shell.rebuildIfDirty();
    }
};

TEST_CASE("treelist_distributes_fixed_and_weighted_column_widths",
          "[collection]") {
    TreeListApp app;
    app.build();

    const auto& widths = app.treeList.columnWidths();
    REQUIRE(widths.size() == 3);
    // 固定列按声明（type 96、size 72）；name 拿剩余宽（视口 - 168）。
    CHECK(widths[1] == Catch::Approx(96.0F).margin(0.01F));
    CHECK(widths[2] == Catch::Approx(72.0F).margin(0.01F));
    CHECK(widths[0] > 100.0F);
    CHECK(widths[0] + widths[1] + widths[2] <= 360.0F + 0.01F);
}

TEST_CASE("treelist_header_is_sticky_and_not_scrolled", "[collection]") {
    TreeListApp app;
    app.model.nodes = FlatTreeModel::sample();
    // 制造滚动：展开全部 + 小视口。
    app.build();
    app.treeList.expandAll(1000);
    app.shell.rebuildIfDirty();

    const RenderNode* tree = findNodeByKey(app.shell.root(), "deps");
    REQUIRE(tree != nullptr);
    // 首子节点 = 表头（粘性）。
    const RenderNode& header = tree->children.front();
    CHECK(header.key == "deps:header");
    const float headerY = header.offset.y;

    app.treeList.scroll().scrollBy(40.0F);
    app.shell.rebuildIfDirty();
    tree = findNodeByKey(app.shell.root(), "deps");
    REQUIRE(tree != nullptr);
    // 滚动后表头 y 不变（行区滚动）。
    CHECK(tree->children.front().key == "deps:header");
    CHECK(tree->children.front().offset.y == Catch::Approx(headerY)
                                              .margin(0.01F));
}

TEST_CASE("treelist_header_click_fires_sort_hook", "[collection]") {
    TreeListApp app;
    app.build();

    const RenderNode* tree = findNodeByKey(app.shell.root(), "deps");
    REQUIRE(tree != nullptr);
    const RenderNode* header = findNodeByKey(app.shell.root(), "deps:header");
    REQUIRE(header != nullptr);
    // 点击 name 表头（sortable）。
    const RenderNode* nameCell =
        findNodeByKey(app.shell.root(), "deps:head:name");
    REQUIRE(nameCell != nullptr);
    const float x = nameCell->offset.x + 20.0F;
    const float y = nameCell->offset.y + nameCell->size.height * 0.5F;

    app.shell.pointerDown(Offset{x, y});
    app.shell.pointerUp(Offset{x, y});
    CHECK(app.lastSortColumn == "name");
    CHECK(app.lastSortDescending == false);
    // 再点一次：方向翻转。
    app.shell.pointerDown(Offset{x, y});
    app.shell.pointerUp(Offset{x, y});
    CHECK(app.lastSortDescending == true);
}

TEST_CASE("collection_semantics_roles_for_list_and_tree", "[collection]") {
    ListApp listApp;
    listApp.list.setItemCount(4);
    listApp.build();
    {
        const auto semantics =
            accessibility::buildSemanticsTree(listApp.shell.root());
        const RenderNode* list = findNodeByKey(listApp.shell.root(), "files");
        REQUIRE(list != nullptr);
        const auto* node = semantics.find(list->identity);
        REQUIRE(node != nullptr);
        CHECK(node->role == accessibility::SemanticsRole::List);
        CHECK((node->actions & accessibility::kActionScroll) != 0);
        // 行角色经 semanticsRole 覆盖为 listItem。
        const auto* row = semantics.find(list->children.front().identity);
        REQUIRE(row != nullptr);
        CHECK(row->role == accessibility::SemanticsRole::ListItem);
    }

    TreeApp treeApp;
    treeApp.build();
    {
        const auto semantics =
            accessibility::buildSemanticsTree(treeApp.shell.root());
        const RenderNode* tree = findNodeByKey(treeApp.shell.root(), "tree");
        REQUIRE(tree != nullptr);
        const auto* node = semantics.find(tree->identity);
        REQUIRE(node != nullptr);
        CHECK(node->role == accessibility::SemanticsRole::Tree);
        const auto* row = semantics.find(tree->children.front().identity);
        REQUIRE(row != nullptr);
        CHECK(row->role == accessibility::SemanticsRole::TreeItem);
    }
}

TEST_CASE("collection_selected_row_resolves_selection_background",
          "[collection]") {
    ListApp app;
    app.list.setItemCount(4);
    app.build();
    app.list.selection().setSelected({"i1"});
    app.shell.rebuildIfDirty();

    const RenderNode* list = findNodeByKey(app.shell.root(), "files");
    REQUIRE(list != nullptr);
    REQUIRE(list->children.size() > 1);
    const RenderNode& row = list->children[1];
    CHECK(row.selected);
    // 集合行背景 = blendOver(transparent, selectionBackground) ≠ 透明。
    CHECK(row.commonStyle().background.a > 0);
}

TEST_CASE("list_widget_stays_within_size_budget", "[collection]") {
    // M7 体积门槛（实测口径）：Release 基线 800B，集合字段 +16B → 816B。
    // Debug 构建的 MSVC STL _ITERATOR_DEBUG_LEVEL=2 令每个容器
    // （std::string/std::vector）膨胀 +8B，属工具链开销而非 Widget
    // 声明增长，故按构建模式分别断言。
#ifdef NDEBUG
    CHECK(sizeof(Widget) <= 816);
#else
    CHECK(sizeof(Widget) <= 920);
#endif
}

// --- 修复回归：批量去重 / 模式收紧 / 行点击 sink / 框架级滚动 ---

TEST_CASE("selection_set_selected_dedupes_bulk_and_keeps_first_seen_order",
          "[collection]") {
    SequenceFixture f;
    // 千级批量含重复（Ctrl+A 重复触发/区间重叠场景）：O(n) 去重。
    std::vector<std::string> bulk;
    bulk.reserve(2000);
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 1000; ++i) {
            bulk.push_back("k" + std::to_string(i));
        }
    }
    f.model.setSelected(std::move(bulk));
    REQUIRE(f.model.selectedCount() == 1000);
    CHECK(f.model.isSelected("k0"));
    CHECK(f.model.isSelected("k999"));
    CHECK(f.model.selectedKeys().front() == "k0");
}

TEST_CASE("selection_single_mode_keeps_current_only_if_selected",
          "[collection]") {
    SequenceFixture f;
    f.model.setMode(SelectionMode::Extended);
    f.model.setSelected({"a", "b", "c"});
    // current 已选中：Single 收紧保留 current。
    f.model.setCurrent("b");
    f.model.setMode(SelectionMode::Single);
    CHECK(f.model.selectedCount() == 1);
    CHECK(f.model.isSelected("b"));

    // current 未选中：收紧后清空（不凭空选中）。
    f.model.setMode(SelectionMode::Extended);
    f.model.setSelected({"a", "b", "c"});
    f.model.setCurrent("d");
    f.model.setMode(SelectionMode::Single);
    CHECK(f.model.selectedCount() == 0);
}

TEST_CASE("list_row_clicks_do_not_grow_handler_registry", "[collection]") {
    ListApp app;
    app.list.setItemCount(200);
    app.build();
    const std::size_t handlersBefore = app.shell.handlers().size();

    // 滚动 + 点击多行：行点击走 sink 分发，常驻注册表不随行累积。
    for (int step = 0; step < 8; ++step) {
        app.list.scroll().scrollBy(600.0F);
        app.shell.rebuildIfDirty();
        const RenderNode* list = findNodeByKey(app.shell.root(), "files");
        REQUIRE(list != nullptr);
        REQUIRE(!list->children.empty());
        const RenderNode& row = list->children.front();
        const Offset point = row.offset + Offset{40.0F, row.size.height * 0.5F};
        app.shell.pointerDown(point);
        app.shell.pointerUp(point);
        app.shell.rebuildIfDirty();
    }
    CHECK(app.list.selection().selectedCount() >= 1);
    CHECK(app.shell.handlers().size() == handlersBefore);
}

TEST_CASE("treelist_column_mode_skips_model_build_row", "[collection]") {
    TreeListApp app;
    app.build();
    app.shell.rebuildIfDirty();
    // 列模式（cellBuilder 已设置）：行内容全部经 cellBuilder，不再调
    // TreeModel::buildRow。
    CHECK(app.model.buildRowCalls == 0);
    // 回退模式：去掉 cellBuilder 后走模型行内容。
    app.treeList.setCellBuilder(nullptr);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    CHECK(app.model.buildRowCalls > 0);
}
