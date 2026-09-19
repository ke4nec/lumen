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
    // design/gallery.html 窗口 chrome：caption 48px 一条行；窗口控制
    // 44px 宽、通高、右缘贴合窗口角；透明窗口圆角 16（根四角 + 标题栏
    // 顶角），最大化归零。
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
    // 通高（拉伸到标题栏行高），close 右缘 = 视口右缘。
    CHECK(min->size.height == Catch::Approx(47.0F).margin(0.01F));
    CHECK(close->size.height == Catch::Approx(47.0F).margin(0.01F));
    const Offset closeAbs = absoluteOffset(app.root(), "window-close");
    CHECK(closeAbs.x + close->size.width ==
          Catch::Approx(1280.0F).margin(0.01F));
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
    CHECK(root->commonStyle().radius.topLeft ==
          Catch::Approx(16.0F).margin(0.01F));
    CHECK(bar->commonStyle().radius.topLeft ==
          Catch::Approx(16.0F).margin(0.01F));
    CHECK(bar->commonStyle().radius.bottomLeft == 0.0F);

    // 最大化：圆角归零（.is-maximized）。
    app.noteWindowMaximized(true);
    (void)app.renderFrame();
    root = findNodeByKey(app.root(), "root");
    bar = findNodeByKey(app.root(), "gallery-titlebar");
    REQUIRE(root != nullptr);
    REQUIRE(bar != nullptr);
    CHECK(root->commonStyle().radius.topLeft == 0.0F);
    CHECK(bar->commonStyle().radius.topLeft == 0.0F);
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
