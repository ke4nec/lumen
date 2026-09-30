// M15 拖放（docs/lumen-drag-drop-design.md）测试：List 行重排会话
//（arm 认领 → 阈值启动 → 插入间隙跟踪 → Drop 提交/Cancel 回退）、
// ghost/插入指示 overlay 契约与 DragDropTokens 派生。
//
// 命名遵循项目测试规范（行为命名，*_tests.cpp）。

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/style/theme.h"
#include "lumen/widgets/list.h"

using namespace lumen;
using namespace lumen::core;
using lumen::widgets::ListController;

namespace {

struct ReorderApp {
    app::AppShell shell{makeConfig()};
    ListController list;
    std::vector<std::pair<std::size_t, std::size_t>> reorders{};

    static app::ShellConfig makeConfig() {
        app::ShellConfig config;
        config.build = [] { return Widget{}; };
        return config;
    }

    ReorderApp() {
        list.setItemCount(5);
        list.setItemBuilder([](std::size_t index) {
            return makeText("Item " + std::to_string(index));
        });
        list.attach(shell, "files");
        list.setReorderable(true);
        list.onReorder = [this](std::size_t from, std::size_t to) {
            reorders.emplace_back(from, to);
        };
    }

    void build() {
        shell.swapRoot(makeList(&list, "files", std::nullopt, 360.0F));
        shell.rebuildIfDirty();
    }

    // 行节点中心（窗口逻辑坐标）。
    Offset rowCenter(const std::string& itemKey) const {
        const std::string nodeKey = "files:item:" + itemKey;
        const RenderNode* node = findNodeByKey(shell.root(), nodeKey);
        REQUIRE(node != nullptr);
        return absoluteOffset(shell.root(), nodeKey) +
               Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    }
};

}  // namespace

TEST_CASE("list_drag_reorder_commits_drop_below_target",
          "[dragdrop][m15]") {
    ReorderApp app;
    app.build();

    // 行 0（默认行高 40，中心 y≈20）拖到行 2 下半（y≈105）：插入间隙
    // 3 → 先移除后插入 → 最终落点行号 2。
    const Offset start = app.rowCenter("i0");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 12.0F});
    REQUIRE(app.list.dragActive());
    CHECK(app.list.dragFromIndex() == 0);

    app.shell.pointerMove(app.rowCenter("i2") + Offset{0.0F, 6.0F});
    CHECK(app.list.dragInsertBefore() == 3);

    app.shell.pointerUp(app.rowCenter("i2") + Offset{0.0F, 6.0F});
    CHECK_FALSE(app.list.dragActive());
    REQUIRE(app.reorders.size() == 1);
    CHECK(app.reorders[0].first == 0);
    CHECK(app.reorders[0].second == 2);
    // 拖放释放不触发行点击选择。
    CHECK(app.list.selection().currentKey().empty());
}

TEST_CASE("list_drag_reorder_commits_drop_above_target",
          "[dragdrop][m15]") {
    ReorderApp app;
    app.build();

    // 行 3 拖到行 1 上半：间隙 1 → 最终落点 1（上移）。
    const Offset start = app.rowCenter("i3");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 12.0F});
    REQUIRE(app.list.dragActive());
    app.shell.pointerMove(app.rowCenter("i1") + Offset{0.0F, -6.0F});
    CHECK(app.list.dragInsertBefore() == 1);
    app.shell.pointerUp(app.rowCenter("i1") + Offset{0.0F, -6.0F});
    REQUIRE(app.reorders.size() == 1);
    CHECK(app.reorders[0].first == 3);
    CHECK(app.reorders[0].second == 1);
}

TEST_CASE("list_drag_below_threshold_release_still_selects",
          "[dragdrop][m15]") {
    ReorderApp app;
    app.build();

    // 阈值内微抖动：释放仍是行点击（选择建立），不提交重排。
    const Offset start = app.rowCenter("i0");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 5.0F});
    CHECK_FALSE(app.list.dragActive());
    app.shell.pointerUp(start + Offset{0.0F, 5.0F});
    CHECK(app.reorders.empty());
    CHECK(app.list.selection().currentKey() == "i0");
}

TEST_CASE("list_drag_cancel_discards_and_clears_overlay",
          "[dragdrop][m15]") {
    ReorderApp app;
    app.build();

    const Offset start = app.rowCenter("i0");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 20.0F});
    REQUIRE(app.list.dragActive());
    app.shell.rebuildIfDirty();  // Start 置脏后的重建物化 ghost overlay。
    REQUIRE(app.shell.hasOverlay());

    app.shell.pointerCancel();
    CHECK_FALSE(app.list.dragActive());
    CHECK_FALSE(app.shell.hasOverlay());
    CHECK(app.reorders.empty());

    // 取消后的移动/释放不再产生会话事件。
    app.shell.pointerMove(start + Offset{0.0F, 60.0F});
    app.shell.pointerUp(start + Offset{0.0F, 60.0F});
    CHECK(app.reorders.empty());
}

TEST_CASE("list_drag_ghost_and_indicator_render_in_overlay",
          "[dragdrop][m15]") {
    ReorderApp app;
    app.build();
    const style::Theme& theme = app.shell.theme();

    const Offset start = app.rowCenter("i1");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 15.0F});
    app.shell.rebuildIfDirty();  // Start 置脏后的重建物化 ghost overlay。
    REQUIRE(app.shell.overlayRoot() != nullptr);

    // ghost：源行内容 + token 表面；指示线：token 色/厚度，位于间隙 1
    //（悬停源行自身）的上边界。
    const RenderNode* ghost =
        findNodeByKey(*app.shell.overlayRoot(), "files:drag-ghost");
    REQUIRE(ghost != nullptr);
    CHECK(ghost->commonStyle().background == theme.dragDrop.ghostSurface);
    const RenderNode* indicator =
        findNodeByKey(*app.shell.overlayRoot(), "files:drag-indicator");
    REQUIRE(indicator != nullptr);
    CHECK(indicator->commonStyle().background ==
          theme.dragDrop.dropIndicator);
    CHECK(indicator->size.height == theme.dragDrop.indicatorThickness);
    // 指示线在源行上边界（列表视口顶部 y=0 + offsetOfIndex(1)=40）。
    CHECK(indicator->offset.y == 40.0F);

    app.shell.pointerUp(start + Offset{0.0F, 15.0F});
    CHECK_FALSE(app.shell.hasOverlay());
}

TEST_CASE("list_drag_disabled_without_reorderable_claim",
          "[dragdrop][m15]") {
    ReorderApp app;
    app.list.setReorderable(false);
    app.build();

    const Offset start = app.rowCenter("i0");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 60.0F});
    CHECK_FALSE(app.list.dragActive());
    app.shell.pointerUp(start + Offset{0.0F, 60.0F});
    CHECK(app.reorders.empty());
}

TEST_CASE("drag_drop_tokens_derive_from_color_scheme",
          "[dragdrop][m15][style]") {
    const style::Theme dark = style::Theme::dark();
    CHECK(dark.dragDrop.dropIndicator == dark.colors.accent);
    CHECK(dark.dragDrop.ghostSurface == dark.colors.surfaceElevated);
    CHECK(dark.dragDrop.ghostBorder == dark.colors.borderStrong);
    CHECK(dark.dragDrop.indicatorThickness > 0.0F);

    const style::Theme light = style::Theme::light();
    CHECK(light.dragDrop.dropIndicator == light.colors.accent);
    CHECK(light.dragDrop.ghostSurface == light.colors.surfaceElevated);
}

// --- M15：DataGrid 行/列拖拽重排 + 键盘/语义等价 ---

#include "lumen/widgets/datagrid.h"

using lumen::widgets::DataColumn;
using lumen::widgets::DataGridController;

namespace {

struct GridApp {
    DataGridController grid;
    app::AppShell shell{makeShellConfig(&grid)};
    std::vector<std::pair<std::size_t, std::size_t>> reorders{};

    static app::ShellConfig makeShellConfig(DataGridController* g) {
        app::ShellConfig config;
        config.build = [g] { return g->build(); };
        return config;
    }

    GridApp() {
        grid.setColumns({DataColumn{"name", "Name", 120.0F},
                         DataColumn{"size", "Size", 100.0F},
                         DataColumn{"date", "Date", 140.0F}});
        grid.setRowCount(5);
        grid.setCellText([](std::size_t row, const std::string& col) {
            return "r" + std::to_string(row) + "/" + col;
        });
        grid.attach(shell, "g");
        grid.setRowReorderable(true);
        grid.setColumnReorderable(true);
        grid.onRowReorder = [this](std::size_t from, std::size_t to) {
            reorders.emplace_back(from, to);
        };
    }

    void build() {
        shell.rebuildIfDirty();
        shell.rebuildIfDirty();  // 列窗口首帧收敛（§19 T6.1 两帧口径）。
    }

    Offset nodeCenter(const std::string& nodeKey) const {
        const RenderNode* node = findNodeByKey(shell.root(), nodeKey);
        REQUIRE(node != nullptr);
        return absoluteOffset(shell.root(), nodeKey) +
               Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    }
};

}  // namespace

TEST_CASE("datagrid_row_drag_reorder_commits", "[dragdrop][m15][grid]") {
    GridApp app;
    app.build();

    // 行 0 拖到行 2 下半：间隙 3 → 最终行号 2（先移除后插入）。
    const Offset start = app.nodeCenter("g:item:r0");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 12.0F});
    REQUIRE(app.grid.dragActive());
    app.shell.pointerMove(app.nodeCenter("g:item:r2") + Offset{0.0F, 6.0F});
    CHECK(app.grid.dragInsertBefore() == 3);
    app.shell.pointerUp(app.nodeCenter("g:item:r2") + Offset{0.0F, 6.0F});
    REQUIRE(app.reorders.size() == 1);
    CHECK(app.reorders[0].first == 0);
    CHECK(app.reorders[0].second == 2);
    CHECK_FALSE(app.grid.dragActive());
}

TEST_CASE("datagrid_alt_arrows_move_current_row", "[dragdrop][m15][grid]") {
    GridApp app;
    app.build();

    app.grid.selection().setCurrent("r1");
    CHECK(app.grid.handleKey(core::Key::Down, core::kModifierAlt));
    REQUIRE(app.reorders.size() == 1);
    CHECK(app.reorders[0].first == 1);
    CHECK(app.reorders[0].second == 2);

    // 边界：首行上移已消费、无提交。
    app.grid.selection().setCurrent("r0");
    CHECK(app.grid.handleKey(core::Key::Up, core::kModifierAlt));
    CHECK(app.reorders.size() == 1);

    // 未启用重排的网格不消费 Alt+方向。
    lumen::widgets::DataGridController plain;
    CHECK_FALSE(plain.handleKey(core::Key::Down, core::kModifierAlt));
}

TEST_CASE("datagrid_rows_expose_move_semantics_and_route_actions",
          "[dragdrop][m15][grid]") {
    GridApp app;
    app.build();

    // 行节点暴露 MoveUp/MoveDown；语义分发路径（expandCollectionRow 同
    // 型）经 row move sink 提交重排。
    const RenderNode* row = findNodeByKey(app.shell.root(), "g:item:r2");
    REQUIRE(row != nullptr);
    CHECK((row->semanticsActions &
           lumen::accessibility::kActionMoveUp) != 0);
    CHECK((row->semanticsActions &
           lumen::accessibility::kActionMoveDown) != 0);
    CHECK(app.shell.controller().moveCollectionRow(*row, /*up=*/true));
    REQUIRE(app.reorders.size() == 1);
    CHECK(app.reorders[0].first == 2);
    CHECK(app.reorders[0].second == 1);

    // 语义 action 名序列包含 moveUp/moveDown（RecordingBridge/回环证据）。
    const std::string names =
        lumen::accessibility::semanticsActionsName(
            lumen::accessibility::kActionMoveUp |
            lumen::accessibility::kActionMoveDown);
    CHECK(names.find("moveUp") != std::string::npos);
    CHECK(names.find("moveDown") != std::string::npos);
}

TEST_CASE("datagrid_column_drag_reorder_commits", "[dragdrop][m15][grid]") {
    GridApp app;
    app.build();
    REQUIRE(app.grid.columns().size() == 3);
    CHECK(app.grid.columns()[0].key == "name");

    // 表头 size 拖到 name 左半：提交 moveColumn(size → 0)。
    const Offset start = app.nodeCenter("g:head:size");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{-12.0F, 0.0F});
    REQUIRE(app.grid.dragActive());
    const Offset target = app.nodeCenter("g:head:name");
    app.shell.pointerMove(target + Offset{-10.0F, 0.0F});
    app.shell.pointerUp(target + Offset{-10.0F, 0.0F});
    CHECK_FALSE(app.grid.dragActive());
    REQUIRE(app.grid.columns().size() == 3);
    CHECK(app.grid.columns()[0].key == "size");
    CHECK(app.grid.columns()[1].key == "name");
    CHECK(app.grid.columns()[2].key == "date");
}

// --- M15 review 回归：会话中源行消失后的清理（ghost 滞留防御） ---

TEST_CASE("list_drag_session_cleans_overlay_when_source_row_vanishes",
          "[dragdrop][m15]") {
    ReorderApp app;
    app.build();

    const Offset start = app.rowCenter("i0");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 20.0F});
    REQUIRE(app.list.dragActive());
    app.shell.rebuildIfDirty();
    REQUIRE(app.shell.hasOverlay());

    // 会话中数据变更使源行消失（keyOf 默认 "i<index>"：缩到 0 行 →
    // i0 不存在）。
    app.list.setItemCount(0);
    app.shell.rebuildIfDirty();

    // Cancel：无论源行是否存在都必须清理会话与 overlay（review 修复：
    // 此前提前 return，ghost 永久滞留）。
    app.shell.pointerCancel();
    CHECK_FALSE(app.list.dragActive());
    CHECK_FALSE(app.shell.hasOverlay());
}

TEST_CASE("list_drag_drop_after_source_vanish_ends_without_commit",
          "[dragdrop][m15]") {
    ReorderApp app;
    app.build();

    const Offset start = app.rowCenter("i0");
    app.shell.pointerDown(start);
    app.shell.pointerMove(start + Offset{0.0F, 20.0F});
    REQUIRE(app.list.dragActive());

    app.list.setItemCount(0);
    app.shell.rebuildIfDirty();
    app.shell.pointerUp(start + Offset{0.0F, 20.0F});
    CHECK_FALSE(app.list.dragActive());
    CHECK_FALSE(app.shell.hasOverlay());
    CHECK(app.reorders.empty());
}
