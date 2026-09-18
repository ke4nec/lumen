// 自定义标题栏框架测试（docs/lumen-titlebar-design.md §6）：
// Widget.windowDrag 物化 → Fake host 平台契约（窗口操作/无效 id 挂靠/
// 默认 no-op 安全降级/拖拽区谓词）。Gallery 结构/窗口命令/runApp 注册
// 集成见同文件 gallery 段（随示例提交合入）。

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <optional>
#include <string>

#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/platform/application_host.h"
#include "lumen/platform/fake_host.h"

using namespace lumen;
using namespace lumen::core;

namespace {

bool drainOne(platform::FakeApplicationHost& host, HostEvent& out) {
    return host.pollEvent(out);
}

}  // namespace

// --- core：windowDrag 物化 ---

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
