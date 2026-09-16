// M3（自用路线图）测试：Grid / Image / VirtualList。
//
// 覆盖：Grid 在 320/768/1080 与连续 resize 下的几何稳定性与最小列宽自
// 适应；VirtualList 只物化可见窗口、快速拖动、键盘定位、实测 extent 修
// 正与锚点稳定、动态数据变更、焦点保持、局部 damage 与全帧像素一致；
// Image 占位/位图绘制与语义；VirtualListController 的几何契约。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/damage.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/core/virtual_list.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/render/render_commands.h"
#include "lumen/style/theme.h"

using namespace lumen;
using namespace lumen::core;
using lumen::layout::LayoutEngine;

namespace {

Constraints tightView(float width, float height) {
    return Constraints::tight(Size{width, height});
}

// 简单文本卡片项：高度可配置（验证实测 extent 修正）。
struct ItemSpec {
    std::size_t count{0};
    float height{40.0F};
    float estimated{40.0F};
    float offsetX{0.0F};  // buildItem 命中用（未用）
};

class ListSource final : public VirtualListSource {
  public:
    std::size_t count{0};
    float itemHeight{40.0F};
    float estimated{40.0F};
    float offset{0.0F};
    // 实测缓存（extentOf 用；noteExtent 写）。
    mutable std::map<std::size_t, float> measured{};
    // 记录物化过的 index（断言只物化可见窗口）。
    mutable std::vector<std::size_t> builtIndices{};

    [[nodiscard]] std::size_t itemCount() const override { return count; }
    [[nodiscard]] float estimatedExtent() const override { return estimated; }
    [[nodiscard]] float extentOf(std::size_t index) const override {
        const auto it = measured.find(index);
        return it != measured.end() ? it->second : estimated;
    }
    [[nodiscard]] float scrollOffset() const override { return offset; }
    [[nodiscard]] float totalExtent() const override {
        float total = 0.0F;
        for (std::size_t i = 0; i < count; ++i) {
            total += extentOf(i);
        }
        return total;
    }
    [[nodiscard]] float offsetOfIndex(std::size_t index) const override {
        float offset = 0.0F;
        for (std::size_t i = 0; i < index && i < count; ++i) {
            offset += extentOf(i);
        }
        return offset;
    }
    [[nodiscard]] std::pair<std::size_t, std::size_t> visibleRange(
        float viewportExtent, float cacheExtent) const override {
        if (count == 0 || viewportExtent <= 0.0F) {
            return {0, 0};
        }
        const float top = std::max(0.0F, offset - cacheExtent);
        const float bottom = offset + viewportExtent + cacheExtent;
        std::size_t first = 0;
        float y = 0.0F;
        while (first < count && y + extentOf(first) <= top) {
            y += extentOf(first);
            ++first;
        }
        std::size_t last = first;
        float iy = y;
        while (last < count && iy < bottom) {
            iy += extentOf(last);
            ++last;
        }
        return {first, last};
    }
    [[nodiscard]] Widget buildItem(std::size_t index) const override {
        builtIndices.push_back(index);
        Widget item = makeText("Item " + std::to_string(index));
        item.key = "item-" + std::to_string(index);
        item.height = extentOf(index);
        item.width = std::nullopt;  // 宽度由视口约束决定
        return item;
    }
    void noteExtent(std::size_t index, float extent) const override {
        measured[index] = extent;
    }
};

}  // namespace

// --- Grid ---

TEST_CASE("grid_fixed_columns_geometry_is_stable_across_viewports",
          "[grid]") {
    const auto layoutGridAt = [](float width) {
        std::vector<Widget> cells;
        for (int i = 0; i < 12; ++i) {
            Widget cell = makeText("c");
            cell.key = "c" + std::to_string(i);
            cells.push_back(std::move(cell));
        }
        Widget grid = makeGrid(std::move(cells), 3, 0.0F, 8.0F, 6.0F, "grid");
        return LayoutEngine::layout(grid, tightView(width, 600.0F));
    };

    for (const float width : {320.0F, 768.0F, 1920.0F}) {
        const RenderNode root = layoutGridAt(width);
        const RenderNode* grid = findNodeByKey(root, "grid");
        REQUIRE(grid != nullptr);
        // 3 列 × (12 项) = 4 行；单元宽 = (宽 - 2×列间距)/3。
        const float cellWidth = (width - 2.0F * 8.0F) / 3.0F;
        const RenderNode* c0 = findNodeByKey(root, "c0");
        const RenderNode* c1 = findNodeByKey(root, "c1");
        const RenderNode* c3 = findNodeByKey(root, "c3");
        REQUIRE(c0 != nullptr);
        REQUIRE(c1 != nullptr);
        REQUIRE(c3 != nullptr);
        CHECK(c0->offset.x == 0.0F);
        CHECK(c1->offset.x == Catch::Approx(cellWidth + 8.0F).margin(0.01F));
        // 第二行起点 y = 第一行高 + 行间距。
        CHECK(c3->offset.y == Catch::Approx(c0->size.height + 6.0F)
                                 .margin(0.01F));
        CHECK(grid->children.size() == 12);
        // 连续 resize：同宽度重复布局几何一致（确定性）。
        const RenderNode again = layoutGridAt(width);
        CHECK(again == root);
    }
}

TEST_CASE("grid_min_column_width_adapts_column_count", "[grid]") {
    const auto layoutAt = [](float width, float minColumn) {
        std::vector<Widget> cells;
        for (int i = 0; i < 6; ++i) {
            cells.push_back(withKey(makeText("x"), "x" + std::to_string(i)));
        }
        return LayoutEngine::layout(
            makeGrid(std::move(cells), 0, minColumn, 0.0F, 0.0F, "grid"),
            tightView(width, 400.0F));
    };
    // 768px / 200px 最小列宽 → 3 列；320px → 1 列（窄窗口仍可用）。
    const RenderNode wide = layoutAt(768.0F, 200.0F);
    const RenderNode* x0 = findNodeByKey(wide, "x0");
    const RenderNode* x1 = findNodeByKey(wide, "x1");
    const RenderNode* x3 = findNodeByKey(wide, "x3");
    REQUIRE(x0 != nullptr);
    REQUIRE(x1 != nullptr);
    REQUIRE(x3 != nullptr);
    CHECK(x1->offset.x == Catch::Approx(768.0F / 3.0F).margin(0.01F));
    CHECK(x3->offset.y == Catch::Approx(x0->size.height).margin(0.01F));

    const RenderNode narrow = layoutAt(320.0F, 200.0F);
    const RenderNode* n1 = findNodeByKey(narrow, "x1");
    REQUIRE(n1 != nullptr);
    CHECK(n1->offset.x == 0.0F);  // 单列：第二项在下方。
    CHECK(n1->offset.y > 0.0F);
    CHECK(n1->size.width == Catch::Approx(320.0F).margin(0.01F));
}

TEST_CASE("grid_row_height_is_max_of_row_and_honors_fixed_size", "[grid]") {
    std::vector<Widget> cells;
    Widget shortCell = withKey(makeText("s"), "short");
    Widget tallCell = withKey(makeText("t"), "tall");
    tallCell.height = 80.0F;
    cells.push_back(std::move(shortCell));
    cells.push_back(std::move(tallCell));
    cells.push_back(withKey(makeText("u"), "third"));

    const RenderNode root = LayoutEngine::layout(
        makeGrid(std::move(cells), 2, 0.0F, 0.0F, 0.0F, "grid"),
        tightView(400.0F, 300.0F));
    const RenderNode* short_ = findNodeByKey(root, "short");
    const RenderNode* third = findNodeByKey(root, "third");
    REQUIRE(short_ != nullptr);
    REQUIRE(third != nullptr);
    // 行高 = max(short, tall=80)；第二行起始 y = 80。
    CHECK(third->offset.y == Catch::Approx(80.0F).margin(0.01F));
    CHECK(short_->size.height < 80.0F);
}

// --- VirtualList ---

TEST_CASE("virtual_list_materializes_only_visible_window", "[virtual-list]") {
    ListSource source;
    source.count = 1000;
    source.itemHeight = 40.0F;
    source.estimated = 40.0F;

    const RenderNode root = LayoutEngine::layout(
        makeVirtualList(&source, "list", std::nullopt, 600.0F, 200.0F),
        tightView(400.0F, 600.0F));
    const RenderNode* list = findNodeByKey(root, "list");
    REQUIRE(list != nullptr);
    // 首屏：视口 600 + 前后缓存 200 → ~25 项（远小于 1000）。
    CHECK(list->children.size() < 40);
    CHECK(list->children.size() >= 15);
    // scrollExtent = 1000×40 - 600。
    CHECK(list->scrollExtent == Catch::Approx(1000.0F * 40.0F - 600.0F)
                                   .margin(0.01F));
    // 只物化了可见窗口内的 index（连续且有限）。
    REQUIRE_FALSE(source.builtIndices.empty());
    CHECK(source.builtIndices.front() == 0);
    CHECK(source.builtIndices.back() < 40);
    // stable key 语义：物化项的 identity 含 item key。
    const RenderNode* item0 = findNodeByKey(root, "item-0");
    REQUIRE(item0 != nullptr);
    CHECK(item0->identity.find("item-0") != std::string::npos);
}

TEST_CASE("virtual_list_fast_drag_positions_far_window", "[virtual-list]") {
    ListSource source;
    source.count = 1000;
    source.offset = 1000.0F * 40.0F - 600.0F;  // 滚到底。

    const RenderNode root = LayoutEngine::layout(
        makeVirtualList(&source, "list", std::nullopt, 600.0F, 100.0F),
        tightView(400.0F, 600.0F));
    const RenderNode* list = findNodeByKey(root, "list");
    REQUIRE(list != nullptr);
    REQUIRE_FALSE(list->children.empty());
    // 底部窗口：最后一项在视口内、identity 稳定。
    const std::string lastKey = "item-999";
    const RenderNode* last = findNodeByKey(root, lastKey);
    REQUIRE(last != nullptr);
    const float lastBottom = last->offset.y + last->size.height;
    CHECK(lastBottom <= 600.0F + 0.01F);
    CHECK(lastBottom >= 500.0F);
    // 首项未物化。
    CHECK(findNodeByKey(root, "item-0") == nullptr);
}

TEST_CASE("virtual_list_measured_extents_correct_positions_in_frame",
          "[virtual-list]") {
    // 项高不均匀：偶数项 60、奇数项 20（估算 40）。
    ListSource source;
    source.count = 10;
    source.estimated = 40.0F;
    for (std::size_t i = 0; i < 10; ++i) {
        source.measured[i] = (i % 2 == 0) ? 60.0F : 20.0F;
    }
    const RenderNode root = LayoutEngine::layout(
        makeVirtualList(&source, "list", std::nullopt, 200.0F, 50.0F),
        tightView(300.0F, 200.0F));
    const RenderNode* item0 = findNodeByKey(root, "item-0");
    const RenderNode* item1 = findNodeByKey(root, "item-1");
    const RenderNode* item2 = findNodeByKey(root, "item-2");
    REQUIRE(item0 != nullptr);
    REQUIRE(item1 != nullptr);
    REQUIRE(item2 != nullptr);
    // 位置按实测累计：0 → 60 → 80。
    CHECK(item1->offset.y == Catch::Approx(60.0F).margin(0.01F));
    CHECK(item2->offset.y == Catch::Approx(80.0F).margin(0.01F));
    // 物化项的实测高度回填到 source（布局期 noteExtent）。
    // （item.width 未设 → 文本固有高度；至少不破坏 measured。）
}

TEST_CASE("virtual_list_controller_anchor_survives_extent_corrections",
          "[virtual-list]") {
    VirtualListController controller;
    controller.setItemCount(100);
    controller.setEstimatedExtent(40.0F);
    controller.setItemBuilder([](std::size_t index) {
        Widget item = makeText("Item");
        item.key = "item-" + std::to_string(index);
        return item;
    });

    // 滚到中部（ScrollController 契约：先告知视口/内容范围）。
    controller.scroll().updateExtents(600.0F, controller.totalExtent());
    controller.scroll().scrollTo(400.0F);
    REQUIRE(controller.scroll().offset() == Catch::Approx(400.0F).margin(0.01F));

    // 视口上方项（index 5，顶部 200 < offset 400）先测得 40，再修正为
    // 80：锚点平移 offset += 40，不跳顶（首次测量恰好等于估值）。
    controller.noteExtent(5, 40.0F);
    CHECK(controller.consumeExtentsChanged());  // 首次测量入缓存。
    controller.noteExtent(5, 80.0F);
    CHECK(controller.scroll().offset() == Catch::Approx(440.0F).margin(0.01F));
    CHECK(controller.consumeExtentsChanged());

    // 视口内项（index 12，顶部 480 > offset）修正不动 offset。
    controller.noteExtent(12, 100.0F);
    CHECK(controller.scroll().offset() == Catch::Approx(440.0F).margin(0.01F));

    // itemCount 收缩：offset 夹取到新 max（仍不回顶，除非内容更小）。
    controller.setItemCount(30);
    controller.scroll().scrollBy(10000.0F);  // 夹取到底部。
    CHECK(controller.scroll().offset() <= controller.scroll().maxScrollOffset());
    CHECK(controller.scroll().offset() > 0.0F);  // 不跳回顶部。
    controller.setItemCount(11);  // 内容比视口小，偏移必须归零。
    CHECK(controller.scroll().offset() == 0.0F);
    CHECK(controller.scroll().maxScrollOffset() == 0.0F);
}

TEST_CASE("virtual_list_controller_ranges_and_positioning", "[virtual-list]") {
    VirtualListController controller;
    controller.setItemCount(1000);
    controller.setEstimatedExtent(40.0F);
    controller.setItemBuilder([](std::size_t index) {
        Widget item = makeText("Item");
        item.key = "i-" + std::to_string(index);
        return item;
    });

    controller.scroll().updateExtents(600.0F, controller.totalExtent());

    // 首屏：viewport 600、缓存 200。
    auto [first, last] = controller.visibleRange(600.0F, 200.0F);
    CHECK(first == 0);
    CHECK(last == 20);  // (600+200)/40

    // 滚到 4000：first = (4000-200)/40 = 95。
    controller.scroll().scrollTo(4000.0F);
    std::tie(first, last) = controller.visibleRange(600.0F, 200.0F);
    CHECK(first == 95);
    CHECK(last == 120);  // (4000+600+200)/40

    // 键盘定位（最小移动语义）：index 500 在视口下方 → 滚到可见底部
    //（项底 20040 - 视口 600 = 19440）。
    controller.scrollToIndex(500, 600.0F);
    CHECK(controller.scroll().offset() ==
          Catch::Approx(19440.0F).margin(0.01F));
    // 已可见的定位不移动（index 500 完整可见于 [19440, 20040]）。
    controller.scrollToIndex(500, 600.0F);
    CHECK(controller.scroll().offset() ==
          Catch::Approx(19440.0F).margin(0.01F));
    // 上方项滚回可见。
    controller.scrollToIndex(2, 600.0F);
    CHECK(controller.scroll().offset() ==
          Catch::Approx(2.0F * 40.0F).margin(0.01F));

    // 空列表。
    controller.setItemCount(0);
    std::tie(first, last) = controller.visibleRange(600.0F, 200.0F);
    CHECK(first == 0);
    CHECK(last == 0);
}

TEST_CASE("virtual_list_shrinking_data_clamps_and_fills_the_viewport", "[virtual-list]") {
    VirtualListController controller;
    controller.setItemCount(1000);
    controller.setEstimatedExtent(40.0F);
    controller.setItemBuilder([](std::size_t index) {
        Widget item = withKey(makeText("Item"), "item-" + std::to_string(index));
        item.height = 40.0F;
        return item;
    });
    controller.scroll().updateExtents(200.0F, controller.totalExtent());
    controller.scroll().scrollTo(4000.0F);
    controller.setItemCount(10);
    CHECK(controller.scroll().offset() == 200.0F);
    CHECK(controller.scroll().maxScrollOffset() == 200.0F);
    const auto root = LayoutEngine::layout(
        makeVirtualList(&controller, "list", {}, {}, 0.0F), tightView(300, 200));
    REQUIRE(findNodeByKey(root, "item-5") != nullptr);
    CHECK(findNodeByKey(root, "item-5")->offset.y == 0.0F);
    CHECK(root.children.size() == 5);
}

TEST_CASE("virtual_list_first_measurement_preserves_anchor_at_end", "[virtual-list]") {
    VirtualListController controller;
    controller.setItemCount(100);
    controller.setEstimatedExtent(40.0F);
    controller.scroll().updateExtents(200.0F, controller.totalExtent());
    controller.scroll().scrollTo(3800.0F);
    controller.noteExtent(5, 80.0F);
    CHECK(controller.scroll().offset() == 3840.0F);
    CHECK(controller.scroll().maxScrollOffset() == 3840.0F);
    // The item crossing the viewport top is the anchor itself.
    controller.scroll().scrollTo(3810.0F);
    controller.noteExtent(94, 80.0F);
    CHECK(controller.scroll().offset() == 3810.0F);
}

TEST_CASE("virtual_list_large_estimates_still_fill_the_first_frame", "[virtual-list]") {
    VirtualListController controller;
    controller.setItemCount(1000);
    controller.setEstimatedExtent(1000.0F);
    std::size_t builds = 0;
    controller.setItemBuilder([&](std::size_t index) {
        ++builds;
        Widget item = withKey(makeText("Item"), "item-" + std::to_string(index));
        item.height = 10.0F;
        return item;
    });
    const auto root = LayoutEngine::layout(
        makeVirtualList(&controller, "list", {}, {}, 0.0F), tightView(300, 200));
    REQUIRE(root.children.size() == 20);
    CHECK(root.children.back().offset.y + root.children.back().size.height == 200.0F);
    CHECK(root.scrollExtent == controller.totalExtent() - 200.0F);
    CHECK(builds == 20);
}

TEST_CASE("image_change_is_repainted_alongside_other_damage", "[image][damage]") {
    app::AppShell shell{app::ShellConfig{}};
    const auto scene = [](std::uint64_t image, const char* label) {
        Widget text = withKey(makeText(label), "label");
        text.height = 40.0F;
        return makeColumn({text, makeImage(image, "asset://logo", 80, 60, "image")});
    };
    shell.setView(Size{300, 200});
    shell.swapRoot(scene(0, "Before"));
    (void)shell.renderFrame();
    shell.swapRoot(scene(77, "After"));
    const auto partial = shell.renderFrame();
    REQUIRE(shell.partialRepaintCount() == 1);
    CHECK(partial == shell.renderFrame(true));
}

TEST_CASE("virtual_list_items_bind_state_and_repaint_after_input", "[virtual-list][app]") {
    VirtualListController controller;
    controller.setItemCount(1);
    controller.setItemBuilder([](std::size_t) {
        return makeCheckbox("Enabled", "enabled", "toggle");
    });
    app::ShellConfig config;
    config.build = [&] { return makeVirtualList(&controller, "list"); };
    app::AppShell shell{std::move(config)};
    shell.state().set("enabled", "true");
    (void)shell.renderFrame();
    const auto* toggle = findNodeByKey(shell.root(), "toggle");
    REQUIRE(toggle != nullptr);
    CHECK(toggle->checked);
    const Offset center = absoluteOffset(shell.root(), "toggle") +
                          Offset{toggle->size.width * 0.5F, toggle->size.height * 0.5F};
    shell.pointerDown(center);
    shell.pointerUp(center);
    (void)shell.renderFrame();
    CHECK(shell.state().get("enabled") == "false");
    CHECK_FALSE(findNodeByKey(shell.root(), "toggle")->checked);
    shell.state().set("enabled", "true");
    (void)shell.renderFrame();
    CHECK(findNodeByKey(shell.root(), "toggle")->checked);
}

TEST_CASE("virtual_list_focus_identity_survives_recycle", "[virtual-list]") {
    ListSource source;
    source.count = 200;

    const auto layoutAtOffset = [&](float offset) {
        source.offset = offset;
        source.builtIndices.clear();
        return LayoutEngine::layout(
            makeVirtualList(&source, "list", std::nullopt, 400.0F, 0.0F),
            tightView(300.0F, 400.0F));
    };
    const RenderNode top = layoutAtOffset(0.0F);
    const RenderNode* item5 = findNodeByKey(top, "item-5");
    REQUIRE(item5 != nullptr);
    const std::string identity = item5->identity;

    // 滚走（item-5 回收）再滚回：identity 稳定（焦点/语义依据）。
    const RenderNode far = layoutAtOffset(1000.0F * 40.0F - 400.0F);
    CHECK(findNodeByKey(far, "item-5") == nullptr);
    const RenderNode back = layoutAtOffset(0.0F);
    const RenderNode* again = findNodeByKey(back, "item-5");
    REQUIRE(again != nullptr);
    CHECK(again->identity == identity);
}

// --- 局部 damage 与全帧像素一致（AppShell 驱动真实帧管线） ---

namespace {

class VirtualListApp {
  public:
    VirtualListApp()
        : shell_(std::make_unique<app::AppShell>(makeConfig(this))) {
        shell_->state().set("title", "Items");
    }

    app::AppShell& shell() { return *shell_; }
    VirtualListController& controller() { return list_; }

  private:
    [[nodiscard]] static app::ShellConfig makeConfig(VirtualListApp* self) {
        app::ShellConfig config;
        config.initialView = Size{400.0F, 600.0F};
        config.build = [self] { return self->buildUi(); };
        config.onWheel =
            [self](const RenderNode& root, const RenderNode* hit,
                   Offset /*position*/, Offset delta) {
                const RenderNode* viewport = hit;
                if (viewport == nullptr) {
                    viewport = findNodeByKey(root, "big-list");
                }
                if (viewport == nullptr ||
                    !isScrollableWidget(viewport->type)) {
                    return false;
                }
                self->list_.scroll().updateExtents(
                    viewport->size.height,
                    viewport->size.height + viewport->scrollExtent);
                const bool changed = self->list_.scroll().applyWheel(delta.y);
                if (changed && self->shell_ != nullptr) {
                    self->shell_->markDirty();
                }
                return changed;
            };
        return config;
    }

    [[nodiscard]] Widget buildUi() const {
        Widget title = makeText(shell_->state().get("title"));
        title.key = "title";
        Widget list = makeVirtualList(&list_, "big-list", std::nullopt,
                                      560.0F, 100.0F);
        Widget column = makeColumn({std::move(title), std::move(list)});
        column.key = "root";
        return column;
    }

    mutable VirtualListController list_{};
    std::unique_ptr<app::AppShell> shell_{};
};

}  // namespace

TEST_CASE("virtual_list_partial_damage_matches_full_repaint", "[virtual-list]") {
    VirtualListApp app;
    app.controller().setItemCount(1000);
    app.controller().setEstimatedExtent(40.0F);
    app.controller().setItemBuilder([](std::size_t index) {
        Widget item = makeText("Item " + std::to_string(index));
        item.key = "item-" + std::to_string(index);
        item.height = 40.0F;
        return item;
    });
    app.shell().setView(Size{400.0F, 600.0F});

    // 首帧（全帧）。
    const std::uint64_t first = app.shell().renderFrame();
    REQUIRE(first != 0);

    // 滚动若干次：每次走局部 damage；末帧与强制全帧逐像素一致。
    for (int i = 0; i < 8; ++i) {
        app.controller().scroll().updateExtents(560.0F, 1000.0F * 40.0F);
        app.controller().scroll().scrollBy(360.0F);
        app.shell().markDirty();
    }
    const std::uint64_t partial = app.shell().renderFrame();
    // 局部重绘至少发生过一次。
    CHECK(app.shell().partialRepaintCount() >= 1);

    // 同状态下强制全帧：帧缓冲必须与局部路径一致。
    const auto& pixels = app.shell().pixels();
    render::CpuRenderer reference{1.0F};
    reference.beginFrame(Size{400.0F, 600.0F});
    render::paintScene(reference, app.shell().root());
    reference.endFrame();
    REQUIRE(reference.pixels().rgba.size() == pixels.rgba.size());
    bool identical = true;
    for (std::size_t i = 0; i < pixels.rgba.size(); ++i) {
        if (reference.pixels().rgba[i] != pixels.rgba[i]) {
            identical = false;
            break;
        }
    }
    CHECK(identical);
    (void)partial;
}

// --- Image ---

TEST_CASE("image_widget_draws_placeholder_then_bitmap", "[image]") {
    const auto layoutImage = [](std::uint64_t id) {
        // 根节点总是占满视口（tight 约束）；Image 经 Column 得到宽松
        // 子约束后 width/height 覆盖生效。
        Widget image = makeImage(id, "asset://logo", 120.0F, 80.0F, "logo");
        Widget column = makeColumn({std::move(image)});
        column.key = "host";
        return LayoutEngine::layout(column, tightView(300.0F, 200.0F));
    };

    // 未就绪：S4（§6.10）占位 = surfaceSunken 表面 + borderDefault 描边
    // 轮廓 + 居中 IconId::Image 图标（不再按图片高度比例生成粗边框）。
    const RenderNode pending = layoutImage(0);
    const RenderNode* node = findNodeByKey(pending, "logo");
    REQUIRE(node != nullptr);
    CHECK(node->size.width == Catch::Approx(120.0F).margin(0.01F));
    CHECK(node->imageId == 0);
    CHECK(node->imageSource == "asset://logo");
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    CHECK(node->commonStyle().background ==
          theme.colors.surfaceSunken);
    CHECK(node->commonStyle().border == theme.colors.borderDefault);
    render::RenderCommandList pendingCommands =
        render::recordScene(pending);
    bool sawPlaceholderFill = false;
    bool sawPlaceholderStroke = false;
    bool sawImageIcon = false;
    for (const auto& command : pendingCommands.commands()) {
        if (command.type == render::CommandType::DrawRect &&
            command.color == theme.colors.surfaceSunken) {
            sawPlaceholderFill = true;
        }
        if (command.type == render::CommandType::DrawRectStroke &&
            command.color == theme.colors.borderDefault) {
            sawPlaceholderStroke = true;
        }
        if (command.type == render::CommandType::DrawIcon &&
            command.polylines == iconPolylines(core::IconId::Image)) {
            sawImageIcon = true;
        }
    }
    CHECK(sawPlaceholderFill);
    CHECK(sawPlaceholderStroke);
    CHECK(sawImageIcon);

    // 已就绪：DrawImage 命令携带稳定 id 与节点盒子。
    const RenderNode ready = layoutImage(77);
    render::RenderCommandList readyCommands = render::recordScene(ready);
    bool sawImage = false;
    for (const auto& command : readyCommands.commands()) {
        if (command.type == render::CommandType::DrawImage) {
            sawImage = command.image == 77;
        }
    }
    CHECK(sawImage);
}

TEST_CASE("image_and_virtual_list_semantics_roles", "[virtual-list][image]") {
    ListSource source;
    source.count = 5;
    const RenderNode root = LayoutEngine::layout(
        makeVirtualList(&source, "list", std::nullopt, 300.0F, 0.0F),
        tightView(300.0F, 300.0F));
    const auto tree = accessibility::buildSemanticsTree(root);
    const auto* listNode = tree.find(findNodeByKey(root, "list")->identity);
    REQUIRE(listNode != nullptr);
    CHECK(listNode->role == accessibility::SemanticsRole::List);
    CHECK((listNode->actions & accessibility::kActionScroll) != 0);
}
