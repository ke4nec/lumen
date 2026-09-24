// M2（自用路线图）应用壳测试：runApp 主循环与 AppShell 帧管线的
// headless 覆盖——事件顺序、dirty 合并、DPI/IME 状态同步、renderer 替换
// 与关闭请求策略，全部经 FakeApplicationHost 驱动（不依赖真实窗口）。

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <optional>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/dsl/dsl.h"
#include "lumen/platform/fake_host.h"
#include "lumen/render/renderer.h"

using lumen::app::AppShell;
using lumen::app::AppWindow;
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
                    core::withKey(core::withFocusRing(button("Go", onClick("go")), true),
                                  "go-button"),
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

class VisibilityHost final : public lumen::platform::ApplicationHost {
  public:
    bool initialize() override {
        initialized_ = true;
        lifecycle_ = lumen::core::AppLifecycle::Active;
        return true;
    }

    void shutdown() override { initialized_ = false; }

    [[nodiscard]] lumen::core::AppLifecycle lifecycle() const override {
        return lifecycle_;
    }

    bool pollEvent(lumen::core::HostEvent& out) override {
        if (events_.empty()) {
            out = {};
            return false;
        }
        out = std::move(events_.front());
        events_.pop_front();
        return true;
    }

    std::optional<lumen::core::WindowId> createWindow(
        const lumen::platform::WindowDesc& desc) override {
        if (!initialized_) {
            return std::nullopt;
        }
        metrics_.logicalSize =
            Size{static_cast<float>(desc.width), static_cast<float>(desc.height)};
        metrics_.drawableSize = metrics_.logicalSize;
        metrics_.deviceScale = 1.0F;
        metrics_.visible = false;
        metrics_.minimized = true;
        return window_;
    }

    void destroyWindow(lumen::core::WindowId id) override {
        if (id == window_) {
            initialized_ = false;
        }
    }

    [[nodiscard]] std::optional<lumen::core::WindowMetrics> windowMetrics(
        lumen::core::WindowId id) const override {
        return id == window_ ? std::optional{metrics_} : std::nullopt;
    }

    [[nodiscard]] std::vector<lumen::core::WindowId> windowIds()
        const override {
        return initialized_ ? std::vector<lumen::core::WindowId>{window_}
                            : std::vector<lumen::core::WindowId>{};
    }

    [[nodiscard]] lumen::platform::PlatformWindow* platformWindow(
        lumen::core::WindowId) const override {
        return nullptr;
    }

    [[nodiscard]] lumen::platform::Clipboard* clipboard() override {
        return nullptr;
    }

    [[nodiscard]] lumen::platform::TextInputSession* textInputSession(
        lumen::core::WindowId) override {
        return nullptr;
    }

    [[nodiscard]] lumen::platform::PlatformCapabilities capabilities()
        const override {
        return {};
    }

    void setVisible(bool visible) {
        metrics_.visible = visible;
        metrics_.minimized = !visible;
    }

    void pushWindowRestored() {
        lumen::core::HostEvent event;
        event.type = lumen::core::HostEventType::WindowRestored;
        event.window = window_;
        events_.push_back(std::move(event));
    }

    void pushQuit() {
        lumen::core::HostEvent event;
        event.type = lumen::core::HostEventType::Quit;
        events_.push_back(std::move(event));
    }

  private:
    const lumen::core::WindowId window_{1};
    lumen::core::WindowMetrics metrics_{};
    std::deque<lumen::core::HostEvent> events_{};
    lumen::core::AppLifecycle lifecycle_{lumen::core::AppLifecycle::Launching};
    bool initialized_{false};
};

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

// plan-v0.2 §3.2：脏树/待绘制内容必须独立于连续动画状态请求提交。
// 不用 maxFrames（它会人为注入 Explicit，掩盖漏帧）。检查实际呈现像素。
TEST_CASE("run_app_presents_one_shot_changes_without_active_animation",
          "[app][scheduler]") {
    bool rebuildBeforeDecision = false;
    bool repaintOnly = false;
    SECTION("markDirty from an idle tick") {}
    SECTION("tree rebuilt before scheduler decision") {
        rebuildBeforeDecision = true;
    }
    SECTION("explicit repaint without a dirty tree") {
        repaintOnly = true;
    }

    FakeApplicationHost host;
    int presents = 0;
    bool changed = false;
    std::optional<std::uint64_t> changedAt;
    const auto red = lumen::core::Color::fromRGBA(255, 0, 0);
    const auto blue = lumen::core::Color::fromRGBA(0, 0, 255);
    ShellConfig config;
    config.caretBlink = false;
    config.build = [&] {
        return lumen::dsl::container(
            lumen::dsl::text(""),
            repaintOnly ? lumen::core::Color::transparent()
                        : (changed ? blue : red));
    };
    config.onAnimate = [&](AppShell& app, std::uint64_t nowMs) {
        if (presents == 1 && !changed) {
            changed = true;
            changedAt = nowMs;
            if (repaintOnly) {
                app.setClearColor(blue);
                app.requestFullRepaint();
            } else {
                app.markDirty();
                if (rebuildBeforeDecision) app.rebuildIfDirty();
            }
        }
        if (changedAt && nowMs - *changedAt >= 64) host.pushQuit();
        return false;  // 整个过程从未进入连续动画态，也没有 retire 边沿。
    };
    AppShell shell{std::move(config)};
    shell.setClearColor(red);
    RunOptions options;
    options.windowDesc.width = 16;
    options.windowDesc.height = 16;
    options.idleWaitMs = 1;
    options.nativeAccessibility = false;
    options.rendererFactory = [&](auto&, auto&) {
        RendererSetup setup;
        setup.present = [&] {
            ++presents;
            const auto expected = changed ? blue : red;
            REQUIRE(shell.pixels().rgba.size() >= 4);
            CHECK(shell.pixels().rgba[0] == expected.r);
            CHECK(shell.pixels().rgba[1] == expected.g);
            CHECK(shell.pixels().rgba[2] == expected.b);
            CHECK(shell.pixels().rgba[3] == expected.a);
            return true;
        };
        return setup;
    };
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    CHECK(changed);
    CHECK(presents == 2);
    CHECK_FALSE(shell.animationsActive());
}

TEST_CASE("run_app_routes_events_to_the_matching_window_shell", "[app][multi-window]") {
    FakeApplicationHost host;
    AppShell first{counterConfig()};
    AppShell second{counterConfig()};
    wireCounter(first);
    wireCounter(second);
    first.setView(Size{800.0F, 600.0F});
    second.setView(Size{800.0F, 600.0F});
    (void)first.renderFrame();
    (void)second.renderFrame();

    // FakeApplicationHost allocates ids from 1 in a fresh host. Queueing by id
    // before runApp exercises the same normalized event path as SDL.
    const Offset button = centerOf(first, "go-button");
    host.pushPointerDown(lumen::core::WindowId{1}, button);
    host.pushPointerUp(lumen::core::WindowId{1}, button);

    RunOptions firstOptions;
    firstOptions.maxFrames = 1;
    firstOptions.nativeAccessibility = false;
    RunOptions secondOptions;
    secondOptions.maxFrames = 1;
    secondOptions.nativeAccessibility = false;

    CHECK(lumen::app::runApp(
              std::vector<AppWindow>{{&first, std::move(firstOptions)},
                                     {&second, std::move(secondOptions)}},
              host) == 0);
    CHECK(first.state().get("counter") == "hit");
    CHECK(second.state().get("counter") == "0");
    CHECK(host.lifecycle() == lumen::core::AppLifecycle::Active);
    CHECK(host.windowIds().size() == 2);
}

TEST_CASE("run_app_honors_host_visibility_when_restoring", "[app][lifecycle]") {
    VisibilityHost host;
    AppShell shell{counterConfig()};
    int pollCount = 0;
    int presents = 0;
    int hiddenPresents = 0;

    RunOptions options;
    options.idleWaitMs = 0;
    options.nativeAccessibility = false;
    options.poll = [&](AppShell&, std::uint64_t) {
        if (pollCount++ == 0) {
            host.pushWindowRestored();
        } else if (pollCount == 2) {
            host.setVisible(true);
            host.pushWindowRestored();
        }
        return false;
    };
    options.rendererFactory = [&](lumen::platform::ApplicationHost&,
                                  lumen::core::WindowId& id) {
        RendererSetup setup;
        setup.present = [&host, id, &presents, &hiddenPresents] {
            ++presents;
            const auto metrics = host.windowMetrics(id);
            if (metrics.has_value() && !metrics->visible) {
                ++hiddenPresents;
            }
            if (presents == 1) {
                host.pushQuit();
            }
            return true;
        };
        return setup;
    };

    CHECK(lumen::app::runApp(shell, host, options) == 0);
    CHECK(presents == 1);
    CHECK(hiddenPresents == 0);
}

TEST_CASE("run_app_presents_followup_dirty_from_on_rebuilt", "[app][scheduler]") {
    FakeApplicationHost host;
    int builds = 0;
    int presents = 0;
    std::optional<std::uint64_t> startedAt;
    ShellConfig config;
    config.caretBlink = false;
    config.build = [&] {
        ++builds;
        return lumen::dsl::text(builds == 1 ? "initial" : "settled");
    };
    config.onRebuilt = [&](AppShell& app) {
        if (builds == 1) app.markDirty();
    };
    config.onAnimate = [&](AppShell&, std::uint64_t nowMs) {
        if (!startedAt) startedAt = nowMs;
        if (nowMs - *startedAt >= 64) host.pushQuit();
        return false;
    };
    AppShell shell{std::move(config)};
    RunOptions options;
    options.windowDesc.width = 16;
    options.windowDesc.height = 16;
    options.idleWaitMs = 1;
    options.nativeAccessibility = false;
    options.rendererFactory = [&](auto&, auto&) {
        RendererSetup setup;
        setup.present = [&] {
            ++presents;
            CHECK(shell.root().text == (presents == 1 ? "initial" : "settled"));
            return true;
        };
        return setup;
    };
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    CHECK(builds == 2);
    CHECK(presents == 2);
}

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

TEST_CASE("run_app_stops_ime_when_window_focus_is_lost", "[app][ime]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});
    (void)shell.renderFrame();

    const Offset field = centerOf(shell, "name-field");
    host.pushPointerDown({}, field);
    host.pushPointerUp({}, field);

    RunOptions options;
    options.nativeAccessibility = false;
    options.idleWaitMs = 0;
    options.poll = [&host](AppShell&, std::uint64_t) {
        lumen::core::HostEvent focusLost;
        focusLost.type = lumen::core::HostEventType::WindowFocusLost;
        host.pushRaw(std::move(focusLost));
        host.pushQuit();
        return false;
    };
    CHECK(lumen::app::runApp(shell, host, options) == 0);

    REQUIRE(host.windowIds().size() == 1);
    auto* session = host.fakeTextInputSession(host.windowIds().front());
    REQUIRE(session != nullptr);
    CHECK(session->startCount == 1);
    CHECK(session->stopCount == 1);
    CHECK_FALSE(session->active());
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

TEST_CASE("app_shell_hash_tracks_frames_painted_without_hashing", "[app]") {
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.paintFrame();
    const auto first = lumen::render::frameHash(shell.pixels());
    CHECK(shell.renderFrame() == first);
    shell.state().set("counter", "changed");
    shell.paintFrame();
    const auto changed = lumen::render::frameHash(shell.pixels());
    REQUIRE(changed != first);
    CHECK(shell.renderFrame() == changed);
    shell.paintFrame(true);
    CHECK(shell.renderFrame() == changed);
    shell.setDeviceScale(1.25F);
    shell.paintFrame();
    CHECK(shell.renderFrame() == lumen::render::frameHash(shell.pixels()));
    CHECK(shell.renderFrame() != changed);
}

TEST_CASE("app_shell_accessibility_settings_preserve_theme_direction", "[app]") {
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setTheme(lumen::style::Theme::light(
        lumen::style::ControlDensity::Comfortable,
        lumen::style::ThemeDirection::InkLinen));

    lumen::accessibility::AccessibilitySettings settings;
    settings.fontScale = 1.15F;
    shell.setAccessibilitySettings(settings, false);

    CHECK(shell.theme().direction == lumen::style::ThemeDirection::InkLinen);
    CHECK(shell.theme().darkMode == false);
    CHECK(shell.theme().typography.body.fontSize >
          lumen::style::Theme::light(
              lumen::style::ControlDensity::Comfortable,
              lumen::style::ThemeDirection::InkLinen)
              .typography.body.fontSize);
}

TEST_CASE("app_shell_system_preferences_merge_overrides_and_preserve_editing", "[app][a11y]") {
    AppShell shell{counterConfig()};
    wireCounter(shell);
    auto base = lumen::style::Theme::light(lumen::style::ControlDensity::Compact,
                                          lumen::style::ThemeDirection::InkLinen);
    base = lumen::style::adaptPlatformTheme(base, {}, false,
                                            lumen::core::Color::fromRGBA(34, 110, 60));
    shell.setTheme(base);
    (void)shell.renderFrame();
    const auto* field = lumen::core::findNodeByKey(shell.root(), "name-field");
    REQUIRE(field);
    shell.controller().focusNode(*field);
    shell.controller().setEditingValue(lumen::text::TextEditingValue("retained", {1, 4}));
    const auto editing = shell.controller().editingValue();
    const auto focus = shell.focus().focusedIdentity();

    shell.setAccessibilityOverrides({.highContrast = false});
    shell.setSystemAccessibilitySettings({true, true, 1.5F});
    CHECK(shell.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{false, true, 1.5F});
    CHECK(shell.theme() == lumen::style::adaptPlatformTheme(base, {false, true, 1.5F}, false, base.colors.accent));
    CHECK(shell.hasPendingFrame());
    (void)shell.renderFrame();
    CHECK(shell.controller().editingValue() == editing);
    CHECK(shell.focus().focusedIdentity() == focus);
    shell.setSystemAccessibilitySettings({true, true, 1.5F});
    CHECK_FALSE(shell.hasPendingFrame()); // identical sampling never repaints

    shell.setAccessibilityOverrides({}); // restore the latest OS value immediately
    CHECK(shell.accessibilitySettings().highContrast);
    shell.setSystemAccessibilitySettings({false, false, 1.0F});
    CHECK(shell.theme() == base); // OS toggles must not accumulate scaling/tone changes
    shell.setAccessibilitySettings({false, false, 1.0F}); // legacy setter fixes all fields
    shell.setSystemAccessibilitySettings({true, true, 2.0F});
    CHECK(shell.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{});
    shell.setAccessibilityOverrides({.fontScale = 1.25F});
    CHECK(shell.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{true, true, 1.25F});
    shell.setSystemAccessibilitySettings({true, true, std::numeric_limits<float>::quiet_NaN()});
    shell.setAccessibilityOverrides({});
    CHECK(shell.accessibilitySettings().fontScale == 1.0F);
}

TEST_CASE("run_app_follows_preferences_before_first_frame_and_broadcasts_updates", "[app][a11y][multi-window]") {
    FakeApplicationHost host;
    auto caps = host.capabilities();
    caps.systemAccessibilityPreferences = true;
    caps.highContrast = true;
    caps.fontScale = 1.25F;
    host.setCapabilities(caps);
    AppShell first{counterConfig()}, second{counterConfig()}, optedOut{counterConfig()};
    wireCounter(first);
    wireCounter(second);
    wireCounter(optedOut);
    second.setAccessibilityOverrides({.fontScale = 1.0F});
    std::vector<int> received(3, 0);
    bool initialChecked = false;
    bool updatedChecked = false;
    RunOptions one, two, three;
    int polls = 0;
    one.idleWaitMs = 1;
    one.poll = [&](AppShell&, std::uint64_t) {
        if (++polls > 200) host.pushQuit(); // bound failures without hanging CTest
        return false;
    };
    one.nativeAccessibility = two.nativeAccessibility = three.nativeAccessibility = false;
    three.followSystemAccessibility = false;
    one.rendererFactory = [&](auto&, auto&) {
        RendererSetup setup;
        setup.present = [&] {
            if (!initialChecked) {
                CHECK(first.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{true, false, 1.25F});
                CHECK(second.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{true, false, 1.0F});
                CHECK(optedOut.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{});
                initialChecked = true;
                host.pushSystemAccessibilityChanged(false, true, 1.5F);
            } else {
                CHECK(first.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{false, true, 1.5F});
                CHECK(second.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{false, true, 1.0F});
                CHECK(optedOut.accessibilitySettings() == lumen::accessibility::AccessibilitySettings{});
                updatedChecked = true;
                host.pushQuit();
            }
            return true;
        };
        return setup;
    };
    int index = 0;
    for (auto* options : {&one, &two, &three}) {
        options->onEvent = [&, i = index++](AppShell&, const auto& event) {
            if (event.type == lumen::core::HostEventType::SystemAccessibilityChanged) ++received[i];
        };
    }
    CHECK(lumen::app::runApp({{&first, one}, {&second, two}, {&optedOut, three}}, host) == 0);
    CHECK(initialChecked);
    CHECK(updatedChecked);
    CHECK(received == std::vector<int>{1, 1, 1});
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

    // Release acceptance: document, selection and focus survive window replacement.
    shell.state().set("counter", "kept");
    const auto* field = lumen::core::findNodeByKey(shell.root(), "name-field");
    REQUIRE(field);
    shell.controller().focusNode(*field);
    shell.controller().setEditingValue(lumen::text::TextEditingValue("document", {1, 4}));
    const auto editing = shell.controller().editingValue();
    const auto focus = shell.focus().focusedIdentity();

    auto external = std::make_unique<RecordingRenderer>();
    RecordingRenderer* externalRaw = external.get();
    bool failedOnce = false;
    int replacementCount = 0;

    RunOptions options;
    options.onRendererFailure =
        [&](lumen::platform::ApplicationHost& input, lumen::core::WindowId& id)
        -> std::optional<RendererSetup> {
        ++replacementCount;
        external.reset();  // 失效后端销毁。
        input.destroyWindow(id);
        const auto replacement = input.createWindow({});
        REQUIRE(replacement);
        id = *replacement;
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
    CHECK(shell.state().get("counter") == "kept");
    CHECK(shell.controller().editingValue() == editing);
    CHECK(shell.focus().focusedIdentity() == focus);
    // 回退后仍有帧产出（内部 CPU 渲染器接管）。
    CHECK(shell.renderFrame() != 0);
}

// --- M4：平台服务事件转发 ---

TEST_CASE("run_app_forwards_file_dialog_events_to_on_event", "[app][m4]") {
    using lumen::platform::FileDialogResult;
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    AppShell shell{counterConfig()};
    wireCounter(shell);
    shell.setView(Size{800.0F, 600.0F});
    (void)shell.renderFrame();

    FileDialogResult queued;
    queued.status = lumen::platform::ServiceResult::success();
    queued.paths = {"/tmp/report.txt"};
    host.queueFileDialogResult(std::move(queued));
    // 请求 + quit（完成事件在两者之间被泵出）。
    host.createWindow({});
    const auto window = host.windowIds().front();
    CHECK(host.requestFileDialog(
              window, lumen::platform::FileDialogRequest{})
              .ok);
    host.pushQuit();

    lumen::core::HostEvent received{};
    RunOptions options;
    options.onEvent = [&received](AppShell&, const lumen::core::HostEvent& e) {
        received = e;
    };
    CHECK(lumen::app::runApp(shell, host, options) == 0);
    CHECK(received.type ==
          lumen::core::HostEventType::FileDialogCompleted);
    REQUIRE(received.filePaths.size() == 1);
    CHECK(received.filePaths[0] == "/tmp/report.txt");
}

// --- M14-C：生产生命周期（资源层接线与状态保持契约） ---

namespace {

// .lumenrgba 原始格式（ASCII 头 + straight RGBA），测试无需图像编码器。
std::string writeRawRgba(const char* name, int width, int height,
                         std::uint8_t value) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "LUMENRGBA\n" << width << " " << height << "\n";
    for (int i = 0; i < width * height; ++i) {
        out.put(value).put(value).put(value).put(255);
    }
    return path.string();
}

}  // namespace

TEST_CASE("run_app_resource_completions_drive_frames", "[app]") {
    // M14-C：资源完成由 runApp 主循环 pump——完成即标脏并请求资源帧，
    // 占位→就绪的翻页自动发生；应用 build 只读 manager 状态，不拼接
    // 不可观测的轮询。上传命令经应用壳前置进命令表。
    FakeApplicationHost host;
    auto manager = std::make_shared<lumen::render::ResourceManager>();
    const std::string image =
        writeRawRgba("lumen-m14c-completion.lumenrgba", 4, 4, 200);
    const auto handle = manager->requestImage(image, lumen::core::WindowId{1});
    REQUIRE(handle.valid());

    ShellConfig config;
    config.initialView = Size{200, 100};
    config.build = [&] {
        return lumen::core::withKey(
            lumen::core::makeImage(manager->ready(handle)
                                       ? manager->imageId(handle)
                                       : 0,
                                   image),
            "m14c-image");
    };
    AppShell shell{config};
    RunOptions options;
    options.resourceManager = manager;
    options.maxFrames = 2;
    options.idleWaitMs = 50;
    REQUIRE(host.initialize());
    host.createWindow({});
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    CHECK(manager->state(handle) ==
          lumen::render::ResourceState::Ready);
    // 第二帧即资源帧：上传命令已被消费（内部 CPU renderer 统计）。
    CHECK(shell.stats().uploads >= 1);
}

TEST_CASE("run_app_requeues_resources_after_renderer_replacement", "[app]") {
    // M14-C：renderer/GPU 设备重建（onRendererFailure 回退路径）后，全部
    // Ready 资源重排队上传（ImageId 不变），异步资源不因后端降级丢失。
    FakeApplicationHost host;
    auto manager = std::make_shared<lumen::render::ResourceManager>();
    lumen::render::PixelBuffer pixels;
    pixels.width = 2;
    pixels.height = 2;
    pixels.rgba.assign(16, 120);
    pixels.rgba[3] = pixels.rgba[7] = pixels.rgba[11] = pixels.rgba[15] = 255;
    const auto handle = manager->registerImage(std::move(pixels));
    REQUIRE(handle.valid());

    ShellConfig config;
    config.initialView = Size{200, 100};
    config.build = [] {
        return lumen::core::withKey(lumen::core::makeText("stable"), "text");
    };
    AppShell shell{config};

    auto external = std::make_unique<RecordingRenderer>();
    bool failedOnce = false;
    RunOptions options;
    options.resourceManager = manager;
    options.maxFrames = 2;
    options.idleWaitMs = 20;
    // 第一帧后注入一次失效；替换后不再失败。
    options.rendererFactory =
        [&](lumen::platform::ApplicationHost&, lumen::core::WindowId&)
        -> RendererSetup {
        RendererSetup setup;
        setup.renderer = external.get();
        setup.failed = [&failedOnce]() {
            const bool fail = !failedOnce;
            failedOnce = true;
            return fail;
        };
        return setup;
    };
    options.onRendererFailure =
        [&](lumen::platform::ApplicationHost& input,
            lumen::core::WindowId& id)
        -> std::optional<RendererSetup> {
        input.destroyWindow(id);
        const auto replacement = input.createWindow({});
        REQUIRE(replacement);
        id = *replacement;
        RendererSetup fallback;
        fallback.renderer = nullptr;  // 应用壳内部 CPU。
        return fallback;
    };
    REQUIRE(host.initialize());
    host.createWindow({});
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    CHECK(failedOnce);
    CHECK(manager->state(handle) == lumen::render::ResourceState::Ready);
    CHECK(manager->diagnostics().reuploads == 1);
}

TEST_CASE("run_app_preserves_scroll_and_focus_across_minimize_restore",
          "[app]") {
    // M14-C：最小化（停帧）/恢复（重建帧）期间状态保持契约——滚动偏移
    // 由应用侧 ScrollController 持有、焦点由 FocusManager 持有，恢复后
    // 的重建帧必须原样带出（onWheel/withScrollOffset 为 settings 同款
    // 应用接线）。
    using namespace lumen::dsl;
    FakeApplicationHost host;
    lumen::core::ScrollController scroll;
    ShellConfig config;
    config.initialView = Size{300, 200};
    config.build = [&] {
        std::vector<lumen::core::Widget> rows;
        rows.push_back(lumen::core::withKey(
            text_field(bind("m14c-doc")), "m14c-field"));
        for (int i = 0; i < 40; ++i) {
            rows.push_back(lumen::core::withKey(
                text("row " + std::to_string(i)), "row-" + std::to_string(i)));
        }
        auto view = lumen::core::makeScrollView(column(rows), "m14c-scroll",
                                                280.0F, 160.0F);
        return lumen::core::withScrollOffset(std::move(view), scroll.offset());
    };
    config.onWheel =
        [&](const lumen::core::RenderNode& root,
            const lumen::core::RenderNode* hit, lumen::core::Offset position,
            lumen::core::Offset delta) {
            (void)hit;
            (void)position;
            const auto* viewport =
                lumen::core::findNodeByKey(root, "m14c-scroll");
            if (viewport == nullptr) {
                return false;
            }
            scroll.updateExtents(viewport->size.height,
                                 viewport->size.height +
                                     viewport->scrollExtent);
            return scroll.applyWheel(delta.y);
        };
    AppShell shell{config};
    (void)shell.renderFrame();  // 首帧布局,取按钮中心。

    REQUIRE(host.initialize());
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());
    // 点击聚焦 → 滚动 → 最小化（停帧）→ 恢复（重建帧）→ 退出。
    const auto* fieldNode =
        lumen::core::findNodeByKey(shell.root(), "m14c-field");
    REQUIRE(fieldNode != nullptr);
    const auto fieldCenter =
        lumen::core::absoluteOffset(shell.root(), "m14c-field") +
        lumen::core::Offset{fieldNode->size.width * 0.5F,
                            fieldNode->size.height * 0.5F};
    host.pushPointerDown(*id, fieldCenter);
    host.pushPointerUp(*id, fieldCenter);
    host.pushTextInput(*id, "document");
    host.pushWheel(*id, lumen::core::Offset{140.0F, 80.0F},
                   lumen::core::Offset{0.0F, 600.0F});
    host.minimizeWindow(*id);
    host.restoreWindow(*id);
    host.pushQuit();

    RunOptions options;
    options.idleWaitMs = 10;
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    CHECK(scroll.offset() > 0.0F);
    CHECK_FALSE(shell.focus().focusedIdentity().empty());
    CHECK(shell.controller().editingValue().text() == "document");
}


