// 自定义标题栏专项测试（docs/lumen-titlebar-design.md §6）：
// Widget.windowDrag 物化 → AppShell::isWindowDragPoint 判定 → Gallery
// 标题栏结构/窗口命令/最大化图标 → Fake host 平台契约 → runApp 注册。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <optional>
#include <string>

#include "gallery_app.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/icon_id.h"
#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/render/render_commands.h"
#include "lumen/platform/application_host.h"
#include "lumen/platform/fake_host.h"

using namespace lumen;
using namespace lumen::core;
using namespace lumen::examples;

namespace {

Offset centerOf(const RenderNode& root, const std::string& key) {
    const RenderNode* node = findNodeByKey(root, key);
    REQUIRE(node != nullptr);
    return absoluteOffset(root, key) +
           Offset{node->size.width * 0.5F, node->size.height * 0.5F};
}

void click(GalleryApp& app, const std::string& key) {
    const Offset point = centerOf(app.root(), key);
    app.pointerDown(point);
    app.pointerUp(point);
}

bool drainOne(platform::FakeApplicationHost& host, HostEvent& out) {
    return host.pollEvent(out);
}

}  // namespace

// --- core/app：windowDrag 物化与拖拽判定 ---

TEST_CASE("titlebar_window_drag_flag_materializes_to_render_node",
          "[titlebar]") {
    Widget plain = makeContainerLeaf(100.0F, 20.0F);
    Widget dragged = withWindowDrag(makeContainerLeaf(100.0F, 20.0F));
    const RenderNode plainNode = layout::LayoutEngine::layout(
        plain, Constraints::tight(Size{100.0F, 20.0F}));
    const RenderNode dragNode = layout::LayoutEngine::layout(
        dragged, Constraints::tight(Size{100.0F, 20.0F}));
    CHECK_FALSE(plainNode.windowDrag);
    CHECK(dragNode.windowDrag);
    // withWindowDrag 缺省 true，显式 false 可取消。
    const Widget undragged =
        withWindowDrag(makeContainerLeaf(10.0F, 10.0F), false);
    const RenderNode undragNode = layout::LayoutEngine::layout(
        undragged, Constraints::tight(Size{10.0F, 10.0F}));
    CHECK_FALSE(undragNode.windowDrag);
}

TEST_CASE("titlebar_gallery_structure_marks_only_caption_row",
          "[titlebar][gallery]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();

    // 标题栏三段式：品牌 / 菜单栏 / 拖拽区 / 窗口控制，同时存在。
    REQUIRE(findNodeByKey(app.root(), "gallery-titlebar") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "gallery-titlebar-row") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "gallery-brand") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "gallery-menubar") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "gallery-titlebar-drag") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "gallery-window-actions") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "window-minimize") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "window-maximize") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "window-close") != nullptr);
    // 菜单栏在标题栏行内（chrome 一部分，menu-design 先例）。
    const Offset barAbs = absoluteOffset(app.root(), "gallery-titlebar-row");
    const Offset menuAbs = absoluteOffset(app.root(), "gallery-menubar");
    CHECK(menuAbs.y >= barAbs.y);

    // 物化口径：标题栏行 windowDrag=true，主内容区=false。
    const RenderNode* row =
        findNodeByKey(app.root(), "gallery-titlebar-row");
    REQUIRE(row != nullptr);
    CHECK(row->windowDrag);
    const RenderNode* list = findNodeByKey(app.root(), "gallery-list");
    REQUIRE(list != nullptr);
    CHECK_FALSE(list->windowDrag);
}

TEST_CASE("titlebar_controls_flush_full_height_and_rounded_window",
          "[titlebar][gallery]") {
    // design/gallery.html 窗口 chrome（Terminal 对齐）：caption 48px 一条
    // 行；窗口控制 44px 宽、通高、右缘贴合内容区（1px 外边框内侧）；卡片
    // edge-to-edge：外圆角 8 + 1px 外边框 + 圆角裁剪（padding 内缩），
    // 标题栏/close 取内圆角 7；最大化边框/圆角归零。
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();

    const RenderNode* row = findNodeByKey(app.root(), "gallery-titlebar-row");
    REQUIRE(row != nullptr);
    CHECK(row->size.height == Catch::Approx(47.0F).margin(0.01F));
    // 总高 = 行 47 + 分隔线 1 = 48（border-box 口径）。
    const RenderNode* bar0 = findNodeByKey(app.root(), "gallery-titlebar");
    REQUIRE(bar0 != nullptr);
    CHECK(bar0->size.height == Catch::Approx(48.0F).margin(0.01F));

    const RenderNode* min = findNodeByKey(app.root(), "window-minimize");
    const RenderNode* close = findNodeByKey(app.root(), "window-close");
    REQUIRE(min != nullptr);
    REQUIRE(close != nullptr);
    CHECK(min->size.width == Catch::Approx(44.0F).margin(0.01F));
    CHECK(close->size.width == Catch::Approx(44.0F).margin(0.01F));
    // 通高（拉伸到标题栏行高），close 右缘 = 视口右缘 − 外边框 1
    //（hover 止于边框内侧，外圈边框线完整——Terminal 行为）。
    CHECK(min->size.height == Catch::Approx(47.0F).margin(0.01F));
    CHECK(close->size.height == Catch::Approx(47.0F).margin(0.01F));
    const Offset closeAbs = absoluteOffset(app.root(), "window-close");
    CHECK(closeAbs.x + close->size.width ==
          Catch::Approx(1280.0F - 1.0F).margin(0.01F));
    // 设计稿 caption-button 自身无圆角（design/gallery.html titlebar）：
    // hover 高亮与 close 实心红都是通高矩形，按钮默认 controlRadius 对
    // chrome 件归零——角部钮（close）除外：topRight 跟随窗口内圆角 7
    //（= 外 8 − 边框 1；HTML 稿以 overflow:hidden + 边框裁剪表达；框架另以
    // 卡片 clipRounded 子树门控双防线，直角填充会盖过标题栏内圆角）。
    CHECK(min->commonStyle().radius == CornerRadius::zero());
    CHECK(close->commonStyle().radius.topLeft == 0.0F);
    CHECK(close->commonStyle().radius.topRight ==
          Catch::Approx(7.0F).margin(0.01F));
    CHECK(close->commonStyle().radius.bottomLeft == 0.0F);
    CHECK(close->commonStyle().radius.bottomRight == 0.0F);
    // 图标盒 14px（design/gallery.html caption-button svg 14px），
    // 三钮一致；描边随盒宽折算 ≈1.6。
    {
        const auto commands = render::recordScene(app.root());
        int seenIcons = 0;
        for (const auto& command : commands.commands()) {
            if (command.type != render::CommandType::DrawIcon ||
                command.rect.origin.y > 60.0F ||
                command.rect.origin.x < 1100.0F) {
                continue;
            }
            ++seenIcons;
            CHECK(command.rect.size.width ==
                  Catch::Approx(14.0F).margin(0.01F));
            CHECK(command.rect.size.height ==
                  Catch::Approx(14.0F).margin(0.01F));
            CHECK(command.strokeWidth ==
                  Catch::Approx(1.575F).margin(0.01F));
        }
        CHECK(seenIcons == 3);
    }

    const RenderNode* root = findNodeByKey(app.root(), "root");
    const RenderNode* bar = findNodeByKey(app.root(), "gallery-titlebar");
    REQUIRE(root != nullptr);
    REQUIRE(bar != nullptr);
    // 外层 root 即窗口卡片（edge-to-edge）：8px 外圆角 + 1px 外边框 +
    // 圆角裁剪（padding=边框宽内缩内容）；标题栏取内圆角 7。
    CHECK(root->commonStyle().radius.topLeft ==
          Catch::Approx(8.0F).margin(0.01F));
    CHECK(root->commonStyle().borderWidth ==
          Catch::Approx(1.0F).margin(0.01F));
    CHECK(root->commonStyle().border == app.shell().theme().colors.borderDefault);
    CHECK(root->clipRounded);
    CHECK(root->size.width == Catch::Approx(1280.0F).margin(0.01F));
    CHECK(bar->commonStyle().radius.topLeft ==
          Catch::Approx(7.0F).margin(0.01F));
    CHECK(bar->commonStyle().radius.bottomLeft == 0.0F);

    // 最大化：边框/圆角归零（.is-maximized）——卡片填满视口，close 右缘
    // 回到视口右缘，角部填充跟随归零。
    app.noteWindowMaximized(true);
    (void)app.renderFrame();
    root = findNodeByKey(app.root(), "root");
    bar = findNodeByKey(app.root(), "gallery-titlebar");
    const RenderNode* closeMax = findNodeByKey(app.root(), "window-close");
    REQUIRE(root != nullptr);
    REQUIRE(bar != nullptr);
    REQUIRE(closeMax != nullptr);
    CHECK(root->commonStyle().radius.topLeft == 0.0F);
    CHECK(root->commonStyle().borderWidth == 0.0F);
    CHECK(root->size.width == Catch::Approx(1280.0F).margin(0.01F));
    const Offset closeMaxAbs = absoluteOffset(app.root(), "window-close");
    CHECK(closeMaxAbs.x + closeMax->size.width ==
          Catch::Approx(1280.0F).margin(0.01F));
    CHECK(bar->commonStyle().radius.topLeft == 0.0F);
    CHECK(closeMax->commonStyle().radius.topRight == 0.0F);
}

TEST_CASE("titlebar_drag_points_exclude_interactive_controls",
          "[titlebar][gallery]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();

    // 标题文本 / 拖拽空白 → 可拖（原生 caption 行为）。
    CHECK(app.shell().isWindowDragPoint(
        centerOf(app.root(), "gallery-titlebar-title")));
    CHECK(app.shell().isWindowDragPoint(
        centerOf(app.root(), "gallery-titlebar-drag")));

    // 菜单项 / 窗口按钮（onClick 目标，命中链更深）→ 不可拖。
    CHECK_FALSE(app.shell().isWindowDragPoint(
        centerOf(app.root(), "menu:bar:file")));
    CHECK_FALSE(app.shell().isWindowDragPoint(
        centerOf(app.root(), "window-minimize")));
    CHECK_FALSE(app.shell().isWindowDragPoint(
        centerOf(app.root(), "window-maximize")));
    CHECK_FALSE(app.shell().isWindowDragPoint(
        centerOf(app.root(), "window-close")));

    // 主内容区 → 不可拖。
    CHECK_FALSE(app.shell().isWindowDragPoint(
        centerOf(app.root(), "home-hero")));

    // 紧凑导航下拉（标题栏下自身非拖拽区，≤720 折叠路径）。
    app.setView(Size{700.0F, 800.0F});
    (void)app.renderFrame();
    REQUIRE(findNodeByKey(app.root(), "compact-navigation") != nullptr);
    CHECK_FALSE(app.shell().isWindowDragPoint(
        centerOf(app.root(), "compact-navigation")));
}

TEST_CASE("titlebar_modal_overlay_blocks_drag", "[titlebar][gallery]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();
    const Offset dragPoint = centerOf(app.root(), "gallery-titlebar-drag");
    REQUIRE(app.shell().isWindowDragPoint(dragPoint));

    // ContextMenu 全窗 barrier：打开后同一点命中 overlay → 不可拖。
    const Offset content = centerOf(app.root(), "home-hero");
    app.shell().pointerDown(content, kModifierNone, PointerButton::Secondary);
    app.shell().pointerUp(content, PointerButton::Secondary);
    (void)app.renderFrame();
    REQUIRE(app.shell().overlayRoot() != nullptr);
    CHECK_FALSE(app.shell().isWindowDragPoint(dragPoint));

    // overlay 面板自身命中 → 不可拖。
    const RenderNode* overlay = app.shell().overlayRoot();
    REQUIRE(overlay != nullptr);
    const RenderNode* panel = findNodeByKey(*overlay, "ctx:panel:0");
    REQUIRE(panel != nullptr);
    const Offset panelPoint =
        absoluteOffset(*overlay, "ctx:panel:0") +
        Offset{panel->size.width * 0.5F, panel->size.height * 0.5F};
    CHECK_FALSE(app.shell().isWindowDragPoint(panelPoint));
}

// --- gallery 集成：窗口命令与最大化态 ---

TEST_CASE("titlebar_window_commands_record_and_forward",
          "[titlebar][gallery]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();

    int minimizeCalls = 0;
    int maximizeCalls = 0;
    int closeCalls = 0;
    GalleryApp::WindowCommands commands;
    commands.minimize = [&] { ++minimizeCalls; };
    commands.toggleMaximize = [&] { ++maximizeCalls; };
    commands.requestClose = [&] { ++closeCalls; };
    app.setWindowCommands(std::move(commands));

    click(app, "window-minimize");
    (void)app.renderFrame();
    CHECK(app.lastWindowCommand() == "window-minimize");
    CHECK(minimizeCalls == 1);

    click(app, "window-maximize");
    (void)app.renderFrame();
    CHECK(app.lastWindowCommand() == "window-maximize");
    CHECK(maximizeCalls == 1);

    click(app, "window-close");
    (void)app.renderFrame();
    CHECK(app.lastWindowCommand() == "window-close");
    CHECK(closeCalls == 1);
}

TEST_CASE("titlebar_window_commands_safe_without_host",
          "[titlebar][gallery]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();
    // headless/采样路径不注入平台回调：handler 仍执行并记录，不崩溃。
    click(app, "window-minimize");
    (void)app.renderFrame();
    CHECK(app.lastWindowCommand() == "window-minimize");
    click(app, "window-maximize");
    (void)app.renderFrame();
    CHECK(app.lastWindowCommand() == "window-maximize");
}

TEST_CASE("titlebar_close_falls_back_to_unified_close_policy",
          "[titlebar][gallery]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();
    // 无宿主时 close 回退 shell.requestClose()：弹窗打开 → 消费关闭弹窗。
    // 注：弹窗为模态 overlay，标题栏按钮被遮挡不可点——直接执行 handler
    //（与点击无弹窗时同一 handler 路径），验证回退语义。
    app.shell().handlers().at("show-dialog")();
    (void)app.renderFrame();
    REQUIRE(app.dialogOpen());

    app.shell().handlers().at("window-close")();
    (void)app.renderFrame();
    CHECK(app.lastWindowCommand() == "window-close");
    CHECK_FALSE(app.dialogOpen());
}

TEST_CASE("titlebar_maximized_state_switches_icon",
          "[titlebar][gallery]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();

    const auto maximizeIcon = [&app] {
        const RenderNode* node =
            findNodeByKey(app.root(), "window-maximize");
        REQUIRE(node != nullptr);
        return static_cast<IconId>(node->icon);
    };
    CHECK(maximizeIcon() == IconId::Maximize);
    CHECK_FALSE(app.windowMaximized());

    app.noteWindowMaximized(true);
    (void)app.renderFrame();
    CHECK(app.windowMaximized());
    CHECK(maximizeIcon() == IconId::Restore);

    // 同值重复注入不 markDirty 死循环（行为不变）。
    app.noteWindowMaximized(true);
    (void)app.renderFrame();
    CHECK(maximizeIcon() == IconId::Restore);

    app.noteWindowMaximized(false);
    (void)app.renderFrame();
    CHECK_FALSE(app.windowMaximized());
    CHECK(maximizeIcon() == IconId::Maximize);
}

// --- 平台契约：Fake host + 默认 no-op ---

TEST_CASE("titlebar_fake_host_window_operations", "[titlebar][platform]") {
    platform::FakeApplicationHost host;
    REQUIRE(host.initialize());
    // 初始化广播的 LifecycleChanged 先排空，后续按序断言窗口事件。
    HostEvent ignored{};
    while (host.pollEvent(ignored)) {
    }
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());

    host.minimizeWindow(*id);
    CHECK(host.windowCommandCalls.back() == "minimize");
    auto metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK(metrics->minimized);
    REQUIRE(drainOne(host, ignored));
    CHECK(ignored.type == HostEventType::WindowMinimized);

    host.toggleMaximizeWindow(*id);
    CHECK(host.windowCommandCalls.back() == "maximize");
    metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK(metrics->maximized);
    REQUIRE(drainOne(host, ignored));
    CHECK(ignored.type == HostEventType::WindowMaximized);

    host.toggleMaximizeWindow(*id);
    CHECK(host.windowCommandCalls.back() == "restore");
    metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK_FALSE(metrics->maximized);
    REQUIRE(drainOne(host, ignored));
    CHECK(ignored.type == HostEventType::WindowRestored);

    // requestWindowClose 与系统 X 同路径：合成 WindowCloseRequested。
    host.requestWindowClose(*id);
    CHECK(host.windowCommandCalls.back() == "close");
    REQUIRE(drainOne(host, ignored));
    CHECK(ignored.type == HostEventType::WindowCloseRequested);
    CHECK(ignored.window == *id);
}

TEST_CASE("titlebar_fake_host_invalid_id_falls_back_to_first_window",
          "[titlebar][platform]") {
    platform::FakeApplicationHost host;
    REQUIRE(host.initialize());
    HostEvent ignored{};
    while (host.pollEvent(ignored)) {
    }
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());

    // main.cpp 以 {} 注入窗口命令（SDL host 单窗口便捷路径先例）：
    // Fake 需同语义挂靠首个窗口，而非静默丢弃。
    host.toggleMaximizeWindow({});
    CHECK(host.windowCommandCalls.back() == "maximize");
    CHECK(host.windowMetrics(*id)->maximized);
    REQUIRE(drainOne(host, ignored));
    CHECK(ignored.type == HostEventType::WindowMaximized);
    CHECK(ignored.window == *id);

    host.minimizeWindow({});
    CHECK(host.windowCommandCalls.back() == "minimize");
    CHECK(host.windowMetrics(*id)->minimized);
    REQUIRE(drainOne(host, ignored));
    CHECK(ignored.type == HostEventType::WindowMinimized);
    CHECK(ignored.window == *id);

    host.requestWindowClose({});
    CHECK(host.windowCommandCalls.back() == "close");
    REQUIRE(drainOne(host, ignored));
    CHECK(ignored.type == HostEventType::WindowCloseRequested);
    CHECK(ignored.window == *id);
}

TEST_CASE("titlebar_host_default_window_operations_are_safe_noops",
          "[titlebar][platform]") {
    // 未覆写窗口操作的宿主（默认实现）：无效 id 不崩溃、无事件。
    struct MinimalHost final : public platform::ApplicationHost {
        bool initialize() override { return true; }
        void shutdown() override {}
        [[nodiscard]] core::AppLifecycle lifecycle() const override {
            return core::AppLifecycle::Active;
        }
        bool pollEvent(core::HostEvent&) override { return false; }
        std::optional<core::WindowId> createWindow(
            const platform::WindowDesc&) override {
            return core::WindowId{1};
        }
        void destroyWindow(core::WindowId) override {}
        [[nodiscard]] std::optional<core::WindowMetrics> windowMetrics(
            core::WindowId) const override {
            return std::nullopt;
        }
        [[nodiscard]] std::vector<core::WindowId> windowIds()
            const override {
            return {};
        }
        [[nodiscard]] platform::PlatformWindow* platformWindow(
            core::WindowId) const override {
            return nullptr;
        }
        [[nodiscard]] platform::Clipboard* clipboard() override {
            return nullptr;
        }
        [[nodiscard]] platform::TextInputSession* textInputSession(
            core::WindowId) override {
            return nullptr;
        }
        [[nodiscard]] platform::PlatformCapabilities capabilities()
            const override {
            return {};
        }
    };
    MinimalHost host;
    REQUIRE(host.initialize());
    // 默认 no-op：调用不崩溃（ASan/异常即失败）。
    host.minimizeWindow({});
    host.toggleMaximizeWindow({});
    host.requestWindowClose({});
    host.setWindowDragRegion({}, [](Offset) { return true; });
    core::HostEvent event{};
    CHECK_FALSE(host.pollEvent(event));
}

TEST_CASE("titlebar_fake_host_drag_region_predicate",
          "[titlebar][platform]") {
    platform::FakeApplicationHost host;
    REQUIRE(host.initialize());
    HostEvent ignored{};
    while (host.pollEvent(ignored)) {
    }
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());

    host.setWindowDragRegion(
        *id, [](Offset position) { return position.x < 100.0F; });
    REQUIRE(host.dragRegions.count(*id) == 1);
    CHECK(host.dragRegions[*id](Offset{10.0F, 10.0F}));
    CHECK_FALSE(host.dragRegions[*id](Offset{200.0F, 10.0F}));
}

// --- runApp 接线：customTitleBar 注册拖拽区谓词 ---

TEST_CASE("titlebar_run_app_registers_drag_region_for_custom_title_bar",
          "[titlebar][app]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();

    platform::FakeApplicationHost host;
    REQUIRE(host.initialize());
    HostEvent ignored{};
    while (host.pollEvent(ignored)) {
    }

    app::RunOptions options;
    options.windowDesc.customTitleBar = true;
    options.maxFrames = 1;
    CHECK(app::runApp(app.shell(), host, options) == 0);

    const auto ids = host.windowIds();
    REQUIRE(ids.size() == 1);
    REQUIRE(host.dragRegions.count(ids.front()) == 1);

    // 注册的谓词即 shell.isWindowDragPoint（事件树口径一致）。
    const Offset dragPoint = centerOf(app.root(), "gallery-titlebar-drag");
    const Offset buttonPoint = centerOf(app.root(), "window-close");
    CHECK(host.dragRegions[ids.front()](dragPoint));
    CHECK_FALSE(host.dragRegions[ids.front()](buttonPoint));
}

TEST_CASE("titlebar_run_app_skips_drag_region_without_custom_title_bar",
          "[titlebar][app]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();

    platform::FakeApplicationHost host;
    REQUIRE(host.initialize());
    HostEvent ignored{};
    while (host.pollEvent(ignored)) {
    }

    app::RunOptions options;
    options.windowDesc.customTitleBar = false;
    options.maxFrames = 1;
    CHECK(app::runApp(app.shell(), host, options) == 0);
    CHECK(host.dragRegions.empty());
}

// caption 角部钮 hover 填充跟随窗口内圆角（像素回归）：close hover 实心红
// 只出现在边框内侧——窗口角点保持透明（=清屏色，与对称角一致）；弧下主体
// 是 windowClose 红。Terminal 对齐后卡片几何：24px 阴影边距 + 1px 边框，
// close 止于 x=1255（=1280−24−1），外圈边框完整。
TEST_CASE("titlebar_caption_hover_fill_follows_window_corner_pixels",
          "[titlebar][gallery]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();
    app.shell().setVisualPreviewState(
        "window-close", style::WidgetState{.hovered = true});
    (void)app.renderFrame(/*forceFullRepaint=*/true);

    const auto& pixels = app.pixels();
    const auto at = [&pixels](int x, int y) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * pixels.width + x) * 4;
        return Color::fromRGBA(pixels.rgba[offset], pixels.rgba[offset + 1],
                               pixels.rgba[offset + 2], pixels.rgba[offset + 3]);
    };
    // 窗口角点（阴影边距外）：清屏色，与左上对称角点一致——不是 windowClose
    // 红（修复前直角红填充盖到角点，圆角被破坏）。
    CHECK(at(static_cast<int>(pixels.width) - 1, 1) == at(1, 1));
    CHECK(at(static_cast<int>(pixels.width) - 1, 1) !=
          app.shell().theme().button.windowClose.background);
    // 钮内主体（close 右侧内边距，避开中央 X 字形图标）：实心红通高填充。
    const RenderNode* close = findNodeByKey(app.root(), "window-close");
    REQUIRE(close != nullptr);
    const Offset closeAbs = absoluteOffset(app.root(), "window-close");
    const int hx = static_cast<int>(closeAbs.x + close->size.width - 5.0F);
    const int hy =
        static_cast<int>(closeAbs.y + close->size.height * 0.5F);
    CHECK(at(hx, hy) == app.shell().theme().button.windowClose.background);
}

// 临时诊断（随后移除）：角部 AA 渐变采样。
TEST_CASE("diag_corner_alpha_ramp", "[.][diag]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();
    auto dump = [&](const char* label) {
        const auto& p = app.pixels();
        std::printf("%s top-right corner y=0..3 x=1270..1279 (rgba):\n", label);
        for (int y = 0; y < 4; ++y) {
            for (int x = 1270; x < 1280; ++x) {
                const std::size_t o = (static_cast<std::size_t>(y) * p.width + x) * 4;
                std::printf("(%3d,%3d,%3d,%3d) ", p.rgba[o], p.rgba[o+1], p.rgba[o+2], p.rgba[o+3]);
            }
            std::printf("\n");
        }
    };
    dump("rest");
    app.shell().setVisualPreviewState("window-close", style::WidgetState{.hovered = true});
    (void)app.renderFrame(true);
    dump("close-hover");
}

// 四角不变量（2026-09 框架化修复的系统性验收，2026-09-20 Terminal 对齐
// 升级为外框不变量）：透明清屏下，gallery 卡片四个角点（8px 外圆角之外）
// 在 caption 按钮的 hover/pressed 各状态下恒为全透明——任何贴角 chrome 的
// 填充越出圆角都会在此暴露。角部三重防线：卡片 1px 外边框（hover 止于内侧）
// + close 钮 topRight 内半径 7 跟随 + 卡片/标题栏 clipRounded 子树门控
//（ClipRounded 命令）。另断言外边框在 close hover 下依然完整（右边框中点
// 与顶边框中点为 borderDefault 实色，而非 #c42b1c 高亮红）。
TEST_CASE("titlebar_window_four_corners_stay_transparent",
          "[titlebar][gallery][clip]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    app.shell().setClearColor(core::Color::fromRGBA(0, 0, 0, 0));
    (void)app.renderFrame(/*forceFullRepaint=*/true);
    const auto& p = app.pixels();
    const auto cornerAlpha = [&](int x, int y) {
        return p.rgba[(static_cast<std::size_t>(y) * p.width + x) * 4 + 3];
    };
    const int w = p.width;
    const int h = p.height;
    auto checkCorners = [&]() {
        CHECK(cornerAlpha(1, 1) == 0);
        CHECK(cornerAlpha(w - 2, 1) == 0);
        CHECK(cornerAlpha(1, h - 2) == 0);
        CHECK(cornerAlpha(w - 2, h - 2) == 0);
    };

    checkCorners();  // rest
    for (const char* key : {"window-minimize", "window-maximize",
                            "window-close"}) {
        app.shell().setVisualPreviewState(key, style::WidgetState{.hovered = true});
        (void)app.renderFrame(true);
        checkCorners();
        app.shell().setVisualPreviewState(key, style::WidgetState{.pressed = true});
        (void)app.renderFrame(true);
        checkCorners();
        app.shell().setVisualPreviewState(key, style::WidgetState{});
    }
    (void)app.renderFrame(true);

    // 外边框完整性：close hover 下，卡片右边框中点（1279,40）与顶边框
    // 中点（1257,0）仍为 borderDefault 实色（非高亮红 #c42b1c=196,43,28）。
    // 卡片 edge-to-edge：原点 (0,0)，尺寸 1280×800；边框为外侧 1px 环带；
    // close 右缘止于 x=1279（=1280−1）。
    {
        const auto border = app.shell().theme().colors.borderDefault;
        const auto pixel = [&](int x, int y) {
            const std::size_t o =
                (static_cast<std::size_t>(y) * p.width + x) * 4;
            return core::Color{p.rgba[o], p.rgba[o + 1], p.rgba[o + 2],
                               p.rgba[o + 3]};
        };
        app.shell().setVisualPreviewState("window-close",
                                           style::WidgetState{.hovered = true});
        (void)app.renderFrame(true);
        checkCorners();
        const auto rightBorder = pixel(1279, 40);
        const auto topBorder = pixel(1257, 0);
        CHECK(rightBorder == border);
        CHECK(topBorder == border);
        // 紧贴边框内侧的 hover 像素应为高亮红（证明测试采到了正确的两列）。
        const auto hoverInside = pixel(1278, 40);
        CHECK(hoverInside.r == 196);
        CHECK(hoverInside.g == 43);
        CHECK(hoverInside.b == 28);
        app.shell().setVisualPreviewState("window-close",
                                           style::WidgetState{});
        (void)app.renderFrame(true);
    }

    // 最大化：去边距/边框/阴影/圆角归零（直角窗口），四个角点都是内容。
    app.noteWindowMaximized(true);
    (void)app.renderFrame(true);
    CHECK(cornerAlpha(1, 1) != 0);
    CHECK(cornerAlpha(w - 2, 1) != 0);
    CHECK(cornerAlpha(1, h - 2) != 0);
    CHECK(cornerAlpha(w - 2, h - 2) != 0);
}

// 临时诊断（随后移除）：底角像素来源。
TEST_CASE("diag_bottom_corner", "[.][diag2]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    app.shell().setClearColor(core::Color::fromRGBA(0, 0, 0, 0));
    (void)app.renderFrame(true);
    const auto& p = app.pixels();
    for (int y = p.height - 6; y < p.height; ++y) {
        std::printf("y=%d: ", y);
        for (int x = 0; x < 8; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * p.width + x) * 4;
            std::printf("(%3d,%3d,%3d,%3d) ", p.rgba[o], p.rgba[o+1], p.rgba[o+2], p.rgba[o+3]);
        }
        std::printf("\n");
    }
    const RenderNode* root = findNodeByKey(app.root(), "root");
    const RenderNode* footer = findNodeByKey(app.root(), "gallery-footer");
    std::printf("root radius=%f footer=%p\n",
                root ? root->commonStyle().radius.bottomLeft : -1.0F,
                (const void*)footer);
    if (footer != nullptr) {
        std::printf("footer rect=%f,%f %fx%f color-override=%d\n",
                    footer->rect().origin.x, footer->rect().origin.y,
                    footer->size.width, footer->size.height,
                    footer->commonStyle().background.a);
    }
}
