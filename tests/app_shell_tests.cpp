// M2（自用路线图）应用壳测试：runApp 主循环与 AppShell 帧管线的
// headless 覆盖——事件顺序、dirty 合并、DPI/IME 状态同步、renderer 替换
// 与关闭请求策略，全部经 FakeApplicationHost 驱动（不依赖真实窗口）。

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/dsl/dsl.h"
#include "lumen/platform/fake_host.h"
#include "lumen/render/renderer.h"

using lumen::app::AppShell;
using lumen::app::RendererSetup;
using lumen::app::RunOptions;
using lumen::app::ShellConfig;
using lumen::core::Key;
using lumen::core::Offset;
using lumen::core::RenderNode;
using lumen::core::Size;
using lumen::core::StateStore;
using lumen::core::Widget;
using lumen::core::WidgetType;
using lumen::platform::FakeApplicationHost;

namespace {

// 与 counter 同形的最小应用（build + 状态 + 业务 handler）。
ShellConfig counterConfig() {
    ShellConfig config;
    config.initialView = Size{800.0F, 600.0F};
    config.build = [] {
        using namespace lumen::dsl;
        namespace core = lumen::core;
        Widget page = container(
            column({core::withKey(text("Count: ", bind("counter")),
                                  "count-text"),
                    core::withKey(button("Go", onClick("go")), "go-button"),
                    core::withKey(text_field(bind("name"), placeholder("Name")),
                                  "name-field")}),
            lumen::core::Color::fromRGBA(24, 24, 27));
        page.key = "root";
        return page;
    };
    return config;
}

void wireCounter(AppShell& shell) {
    shell.state().set("counter", "0");
    shell.state().set("name", "");
    shell.handlers()["go"] = [&shell] {
        shell.state().set("counter", "hit");
    };
}

Offset centerOf(const AppShell& shell, const char* key) {
    const RenderNode* node = lumen::core::findNodeByKey(shell.root(), key);
    REQUIRE(node != nullptr);
    return lumen::core::absoluteOffset(shell.root(), key) +
           Offset{node->size.width * 0.5F, node->size.height * 0.5F};
}

}  // namespace

// --- 事件顺序与状态流转（runApp 驱动 Fake host 事件队列） ---

TEST_CASE("run_app_pumps_host_events_until_quit", "[app]") {
    FakeApplicationHost host;
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});
    (void)shell.renderFrame();  // 布局落地，事件命中当前树。

    REQUIRE(host.initialize());
    host.createWindow({});
    const auto id = host.windowIds().front();
    host.pushPointerDown(id, centerOf(shell, "go-button"));
    host.pushPointerUp(id, centerOf(shell, "go-button"));
    host.pushTextInput(id, "Lumen");
    host.pushQuit();

    RunOptions options;
    options.maxFrames = 0;
    CHECK(lumen::app::runApp(shell, host, options) == 0);
    // 点击触发 handler；文本进入焦点字段（点击按钮不建编辑焦点，
    // textInput 无焦点被丢弃）。
    CHECK(shell.state().get("counter") == "hit");
}

TEST_CASE("run_app_dispatches_pointer_and_text_into_focused_field",
          "[app]") {
    FakeApplicationHost host;
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});
    (void)shell.renderFrame();

    REQUIRE(host.initialize());
    host.createWindow({});
    const auto id = host.windowIds().front();
    const Offset field = centerOf(shell, "name-field");
    host.pushPointerDown(id, field);
    host.pushPointerUp(id, field);
    host.pushTextInput(id, "hi");
    host.pushQuit();

    RunOptions options;
    CHECK(lumen::app::runApp(shell, host, options) == 0);
    CHECK(shell.state().get("name") == "hi");
    CHECK(shell.wantsTextInput());
}

// --- 关闭请求策略 ---

TEST_CASE("run_app_close_request_policy_consumes_or_exits", "[app]") {
    SECTION("no policy exits on close request") {
        FakeApplicationHost host;
        AppShell shell{counterConfig()};
        wireCounter(shell);
        shell.setView(Size{800.0F, 600.0F});
        (void)shell.renderFrame();
        REQUIRE(host.initialize());
    host.createWindow({});
        host.pushCloseRequest(host.windowIds().front());

        RunOptions options;
        CHECK(lumen::app::runApp(shell, host, options) == 0);
    }
    SECTION("policy consuming the request keeps the loop alive") {
        int closeRequests = 0;
        ShellConfig config = counterConfig();
        config.onCloseRequested = [&closeRequests](AppShell&) {
            ++closeRequests;
            return true;  // 消费（modal/路由返回语义）。
        };
        FakeApplicationHost host;
        AppShell shell{std::move(config)};
        wireCounter(shell);
        shell.setView(Size{800.0F, 600.0F});
        (void)shell.renderFrame();
        REQUIRE(host.initialize());
    host.createWindow({});
        host.pushCloseRequest(host.windowIds().front());
        host.pushQuit();

        RunOptions options;
        CHECK(lumen::app::runApp(shell, host, options) == 0);
        CHECK(closeRequests == 1);
    }
}

// --- IME 会话同步（Fake host 的 TextInputSession 记录可断言） ---

TEST_CASE("run_app_syncs_ime_session_with_edit_state", "[app]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});
    (void)shell.renderFrame();

    // 事件窗口 id 任意（shell 分发不按窗口过滤；runApp 会创建自己的
    // 窗口，断言针对那个窗口的会话）。
    const lumen::core::WindowId anyWindow{};
    host.pushPointerDown(anyWindow, centerOf(shell, "name-field"));
    host.pushPointerUp(anyWindow, centerOf(shell, "name-field"));
    host.pushTextInput(anyWindow, "ab");
    host.pushQuit();

    RunOptions options;
    CHECK(lumen::app::runApp(shell, host, options) == 0);

    REQUIRE(host.windowIds().size() == 1);
    auto* session = host.fakeTextInputSession(host.windowIds().front());
    REQUIRE(session != nullptr);
    CHECK(session->startCount == 1);
    CHECK(session->active());
    // 编辑状态同步：候选框锚点跟随光标（caretRect 出自同一布局）。
    CHECK(session->stateCount >= 1);
    CHECK(session->lastState.caretRect.size.width > 0.0F);
    CHECK(session->lastState.caretRect.origin.x > 0.0F);
    CHECK(session->lastState.caretRect == shell.focusedTextRect());
}

TEST_CASE("run_app_consumed_close_request_repaints_an_idle_shell", "[app]") {
    FakeApplicationHost host;
    ShellConfig config = counterConfig();
    config.onCloseRequested = [](AppShell& shell) {
        shell.state().set("counter", "closed");
        return true;
    };
    AppShell shell{std::move(config)};
    wireCounter(shell);
    int polls = 0;
    RunOptions options;
    options.idleWaitMs = 25;
    options.poll = [&](AppShell&, std::uint64_t) {
        if (++polls == 1) {
            host.pushCloseRequest(host.windowIds().front());
        } else {
            host.pushQuit();
        }
        return false;
    };
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    const auto* label = lumen::core::findNodeByKey(shell.root(), "count-text");
    REQUIRE(label != nullptr);
    CHECK(label->text == "Count: closed");
}

TEST_CASE("run_app_rejects_a_factory_that_loses_its_window", "[app]") {
    FakeApplicationHost host;
    AppShell shell{counterConfig()};
    RunOptions options;
    options.maxFrames = 1;
    options.rendererFactory = [](lumen::platform::ApplicationHost& host,
                                  lumen::core::WindowId& id) {
        host.destroyWindow(id);
        id = {};
        return RendererSetup{};
    };
    CHECK(lumen::app::runApp(shell, host, options) == 1);
}

// --- dirty 合并与绘制缓存（shell 直驱，不进事件循环） ---

TEST_CASE("app_shell_merges_multiple_state_changes_into_one_paint", "[app]") {
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});

    const std::uint64_t first = shell.renderFrame();
    REQUIRE(first != 0);
    // 无变化帧：绘制缓存命中，返回上一哈希且不产生新的局部重绘。
    const std::uint32_t partialBefore = shell.partialRepaintCount();
    CHECK(shell.renderFrame() == first);
    CHECK(shell.partialRepaintCount() == partialBefore);

    // 同一帧前的多次状态变更合并为一次重建/绘制。
    shell.state().set("counter", "1");
    shell.state().set("counter", "2");
    const std::uint64_t second = shell.renderFrame();
    CHECK(second != 0);
    CHECK(second != first);
}

TEST_CASE("app_shell_paints_focus_assigned_by_rebuild_hook_in_the_same_frame", "[app]") {
    ShellConfig config = counterConfig();
    config.onRebuilt = [](AppShell& shell) {
        if (shell.focus().focusedIdentity().empty()) {
            const auto* button = lumen::core::findNodeByKey(shell.root(), "go-button");
            REQUIRE(button != nullptr);
            shell.controller().focusNode(*button);
        }
    };
    AppShell shell{std::move(config)};
    wireCounter(shell);
    const auto first = shell.renderFrame();
    CHECK(shell.renderFrame() == first);
    const auto* button = lumen::core::findNodeByKey(shell.root(), "go-button");
    REQUIRE(button != nullptr);
    CHECK(button->commonStyle().focusWidth > 0.0F);
}

TEST_CASE("app_shell_rebuilds_on_view_and_scale_changes", "[app]") {
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});
    const std::uint64_t first = shell.renderFrame();
    REQUIRE(first != 0);

    shell.setView(Size{1024.0F, 768.0F});
    const std::uint64_t resized = shell.renderFrame();
    CHECK(resized != first);

    // DPI 变化：像素尺寸改变，全量重绘后哈希稳定可复。
    shell.setDeviceScale(2.0F);
    const std::uint64_t scaled = shell.renderFrame();
    CHECK(scaled != 0);
    CHECK(shell.renderFrame() == scaled);
}

// --- renderer 替换（外部测试 renderer 注入 + 回退 CPU） ---

namespace {

// 可断言的空渲染器（外部测试 renderer 契约：submit 一定被调用）。
class RecordingRenderer final : public lumen::render::Renderer {
  public:
    void beginFrame(Size viewport) override {
        frames += 1;
        lastViewport = viewport;
    }
    void save() override {}
    void restore() override {}
    void clipRect(lumen::core::Rect) override {}
    void drawRect(lumen::core::Rect, lumen::core::Color,
                  lumen::core::CornerRadius) override {}
    void drawText(lumen::render::TextRun, lumen::core::TextStyle) override {}
    void drawImage(lumen::render::ImageId, lumen::core::Rect) override {}
    void endFrame() override {}

    std::uint64_t frames{0};
    Size lastViewport{};
};

}  // namespace

TEST_CASE("app_shell_swaps_renderers_and_invalidates_paint_cache", "[app]") {
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});
    CHECK(shell.renderFrame() != 0);

    RecordingRenderer external;
    shell.setRenderer(&external);
    // 切换后端必产生一帧（绘制缓存失效）。
    CHECK(shell.renderFrame() == 0);  // 外部后端不回填内部哈希。
    CHECK(external.frames == 1);
    CHECK(external.lastViewport.width == 800.0F);

    // 无变化帧不再提交外部后端（缓存）。
    CHECK(shell.renderFrame() == 0);
    CHECK(external.frames == 1);

    // 回到内部 CPU：恢复哈希输出。
    shell.setRenderer(nullptr);
    const std::uint64_t back = shell.renderFrame();
    CHECK(back != 0);
    CHECK(external.frames == 1);
}

TEST_CASE("run_app_replaces_renderer_via_failure_hook", "[app]") {
    FakeApplicationHost host;
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});
    (void)shell.renderFrame();

    auto external = std::make_unique<RecordingRenderer>();
    RecordingRenderer* externalRaw = external.get();
    bool failedOnce = false;
    int replacementCount = 0;

    RunOptions options;
    options.onRendererFailure =
        [&](lumen::platform::ApplicationHost&, lumen::core::WindowId&)
        -> std::optional<RendererSetup> {
        ++replacementCount;
        external.reset();  // 失效后端销毁。
        RendererSetup fallback;
        fallback.renderer = nullptr;  // 应用壳内部 CPU。
        return fallback;
    };
    // 第一帧后注入一次失效；替换后不再失败。
    options.rendererFactory = [&](lumen::platform::ApplicationHost& h,
                                  lumen::core::WindowId& id) -> RendererSetup {
        external = std::make_unique<RecordingRenderer>();
        externalRaw = external.get();
        RendererSetup setup;
        setup.renderer = externalRaw;
        setup.failed = [&failedOnce]() {
            if (!failedOnce) {
                failedOnce = true;
                return true;
            }
            return false;
        };
        (void)h;
        (void)id;
        return setup;
    };
    options.maxFrames = 2;

    REQUIRE(host.initialize());
    host.createWindow({});
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    CHECK(failedOnce);
    CHECK(replacementCount == 1);
    // 回退后仍有帧产出（内部 CPU 渲染器接管）。
    CHECK(shell.renderFrame() != 0);
}
