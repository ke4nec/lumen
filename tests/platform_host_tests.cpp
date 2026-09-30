// v0.3 阶段8A (plan §4 8A): 平台宿主契约测试。
//
// 覆盖：WindowId/WindowMetrics/AppLifecycle 值语义；fake host 的多窗口
// 事件隔离、生命周期、surface detach/attach、DPI 先行 metrics、剪贴板
// 与 TextInputSession 状态机；两个独立 fake 窗口分别驱动 counter 状态
// 与帧（出口条件）；SDL3 host 在 dummy video driver 下的窗口创建与
// 事件泵冒烟。

#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "lumen/platform/fake_host.h"
#include "lumen/platform/sdl3_host.h"
#include "counter_app.h"

#if defined(__linux__) && defined(LUMEN_HAS_XTEST)
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
// Xlib 的 None/True/False 宏与 core 枚举冲突（同 global_hotkeys_x11.cpp）。
#undef None
#undef True
#undef False
#endif
// 内部接缝（纯映射/probe；不出公共头，测试与实现同仓直连）。
#if defined(__linux__)
#include "global_hotkeys.h"
#endif

using lumen::platform::ApplicationHost;
using lumen::platform::FakeApplicationHost;
using lumen::platform::GlobalHotkeySpec;
using lumen::platform::TrayMenuItem;
using lumen::platform::TraySetup;
using lumen::platform::ManualHostClock;
using lumen::platform::WindowDesc;
using lumen::platform::hostStageName;

using namespace lumen;

TEST_CASE("sdl_wheel_axes_and_flipped_events_preserve_framework_direction", "[platform][scrollbar]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    platform::Sdl3ApplicationHost host;
    REQUIRE(host.initialize());
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());
    core::HostEvent output;
    while (host.pollEvent(output)) {}
    struct RestoreModifiers {
        SDL_Keymod previous{SDL_GetModState()};
        ~RestoreModifiers() { SDL_SetModState(previous); }
    } restoreModifiers;
    SDL_SetModState(SDL_KMOD_SHIFT);
    for (const bool flipped : {false, true}) {
        for (const float direction : {-1.0F, 1.0F}) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_WHEEL;
            event.wheel.windowID = static_cast<SDL_WindowID>(id->value);
            event.wheel.x = direction * (flipped ? -1.0F : 1.0F);
            event.wheel.y = event.wheel.x;
            event.wheel.direction = flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
            REQUIRE(SDL_PushEvent(&event));
            bool found = false;
            while (host.pollEvent(output)) {
                if (output.type != core::HostEventType::Wheel) continue;
                CHECK(output.scrollDelta.x == 40.0F * direction);
                CHECK(output.scrollDelta.y == -40.0F * direction);
                CHECK((output.modifiers & core::kModifierShift) != 0U);
                found = true;
            }
            REQUIRE(found);
        }
    }
}

namespace {

core::HostEvent drainOne(FakeApplicationHost& host) {
    core::HostEvent event;
    const bool got = host.pollEvent(event);
    REQUIRE(got);
    return event;
}

// 泵空队列并断言它确实为空。
void drainEmpty(FakeApplicationHost& host) {
    core::HostEvent event;
    CHECK_FALSE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::None);
}

}  // namespace

TEST_CASE("windowing_value_types_are_platform_agnostic", "[platform]") {
    SECTION("window id validity and ordering") {
        core::WindowId invalid{};
        CHECK_FALSE(invalid.valid());
        core::WindowId first{1};
        core::WindowId second{2};
        CHECK(first.valid());
        CHECK(first == first);
        CHECK(first < second);
    }
    SECTION("lifecycle names cover the v0.3 contract") {
        using core::AppLifecycle;
        CHECK(std::string(core::appLifecycleName(AppLifecycle::Launching)) ==
              "launching");
        CHECK(std::string(core::appLifecycleName(AppLifecycle::Active)) ==
              "active");
        CHECK(std::string(core::appLifecycleName(AppLifecycle::Inactive)) ==
              "inactive");
        CHECK(std::string(core::appLifecycleName(AppLifecycle::Background)) ==
              "background");
        CHECK(std::string(core::appLifecycleName(AppLifecycle::Suspended)) ==
              "suspended");
        CHECK(std::string(core::appLifecycleName(AppLifecycle::Terminating)) ==
              "terminating");
    }
    SECTION("window metrics defaults") {
        core::WindowMetrics metrics;
        CHECK(metrics.deviceScale == 1.0F);
        CHECK(metrics.visible);
        CHECK_FALSE(metrics.minimized);
        CHECK(metrics.safeArea == core::EdgeInsets{});
    }
}

TEST_CASE("fake_host_tracks_lifecycle_and_windows", "[platform]") {
    FakeApplicationHost host;
    CHECK(host.lifecycle() == core::AppLifecycle::Launching);

    CHECK(host.initialize());
    CHECK(host.lifecycle() == core::AppLifecycle::Active);
    // 生命周期事件入队且携带新旧状态。
    core::HostEvent event = drainOne(host);
    CHECK(event.type == core::HostEventType::LifecycleChanged);
    CHECK(event.lifecycle == core::AppLifecycle::Active);
    CHECK(event.previousLifecycle == core::AppLifecycle::Launching);
    drainEmpty(host);

    WindowDesc desc;
    desc.title = "first";
    const auto first = host.createWindow(desc);
    REQUIRE(first.has_value());
    CHECK(first->valid());
    const auto second = host.createWindow(desc);
    REQUIRE(second.has_value());
    CHECK(*first != *second);

    auto metrics = host.windowMetrics(*first);
    REQUIRE(metrics.has_value());
    CHECK(metrics->logicalSize == core::Size{800.0F, 600.0F});
    CHECK(metrics->drawableSize == core::Size{800.0F, 600.0F});
    CHECK(host.windowIds().size() == 2);

    host.destroyWindow(*second);
    CHECK(host.windowIds().size() == 1);
    CHECK_FALSE(host.windowMetrics(*second).has_value());

    host.shutdown();
    CHECK(host.lifecycle() == core::AppLifecycle::Terminating);
    event = drainOne(host);
    CHECK(event.type == core::HostEventType::LifecycleChanged);
    CHECK(event.lifecycle == core::AppLifecycle::Terminating);
}

TEST_CASE("fake_host_routes_events_by_window_with_timestamps", "[platform]") {
    ManualHostClock clock;
    FakeApplicationHost host(&clock);
    REQUIRE(host.initialize());
    drainOne(host);  // LifecycleChanged

    const auto a = host.createWindow({});
    const auto b = host.createWindow({});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    clock.advance(10);
    host.pushPointerDown(*a, core::Offset{5.0F, 6.0F});
    clock.advance(5);
    host.pushKeyDown(*b, core::Key::Enter, core::kModifierCtrl | core::kModifierShift, 'a');
    host.pushQuit();

    core::HostEvent event = drainOne(host);
    CHECK(event.type == core::HostEventType::PointerDown);
    CHECK(event.window == *a);
    CHECK(event.timestampMs == 10);
    CHECK(event.position == core::Offset{5.0F, 6.0F});
    CHECK(event.device == core::PointerDevice::Mouse);

    event = drainOne(host);
    CHECK(event.type == core::HostEventType::KeyDown);
    CHECK(event.window == *b);
    CHECK(event.timestampMs == 15);
    CHECK(event.keyCode == core::Key::Enter);
    CHECK(event.modifiers ==
          (core::kModifierCtrl | core::kModifierShift));
    CHECK(event.keyChar == 'a');

    event = drainOne(host);
    CHECK(event.type == core::HostEventType::Quit);
    CHECK_FALSE(event.window.valid());
    drainEmpty(host);
}

TEST_CASE("fake_host_updates_metrics_before_dpi_and_resize_events",
          "[platform]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    drainOne(host);
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());

    // DPI 变化：事件可见之前 metrics 已经反映新 drawable 尺寸（plan §3.1
    // 的顺序不变量：先更新 WindowMetrics 再请求 FrameScheduler）。
    host.changeDeviceScale(*id, 2.0F);
    auto metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK(metrics->deviceScale == 2.0F);
    CHECK(metrics->drawableSize == core::Size{1600.0F, 1200.0F});
    CHECK(metrics->logicalSize == core::Size{800.0F, 600.0F});
    core::HostEvent event = drainOne(host);
    CHECK(event.type == core::HostEventType::DpiChanged);
    CHECK(event.pixelSize == core::Size{1600.0F, 1200.0F});

    host.resizeWindow(*id, core::Size{400.0F, 300.0F});
    metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK(metrics->logicalSize == core::Size{400.0F, 300.0F});
    CHECK(metrics->drawableSize == core::Size{800.0F, 600.0F});
    event = drainOne(host);
    CHECK(event.type == core::HostEventType::Resize);
    CHECK(event.pixelSize == core::Size{800.0F, 600.0F});
}

TEST_CASE("fake_host_minimize_restore_and_surface_lifecycle", "[platform]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    drainOne(host);
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());

    host.minimizeWindow(*id);
    auto metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK(metrics->minimized);
    CHECK_FALSE(metrics->visible);
    CHECK(drainOne(host).type == core::HostEventType::WindowMinimized);

    // 移动端 surface detach：窗口不可见但状态树保留；reattach 恢复提交。
    host.detachSurface(*id);
    CHECK(drainOne(host).type == core::HostEventType::SurfaceDetached);
    metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK_FALSE(metrics->visible);
    // detach 期间最小化窗口恢复：仍不可见（surface 未重连）。
    host.restoreWindow(*id);
    CHECK(drainOne(host).type == core::HostEventType::WindowRestored);
    metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK_FALSE(metrics->minimized);
    CHECK_FALSE(metrics->visible);

    host.reattachSurface(*id);
    CHECK(drainOne(host).type == core::HostEventType::SurfaceReattached);
    metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK(metrics->visible);
}

TEST_CASE("fake_host_clipboard_and_text_input_session", "[platform]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());

    auto* clipboard = host.fakeClipboard();
    REQUIRE(clipboard != nullptr);
    CHECK_FALSE(clipboard->hasText());
    CHECK(clipboard->setText("hello"));
    CHECK(clipboard->hasText());
    CHECK(clipboard->text() == "hello");
    clipboard->clear();
    CHECK_FALSE(clipboard->hasText());
    // 不可用降级：setText 失败但应用状态不受影响。
    clipboard->setAvailable(false);
    CHECK_FALSE(clipboard->setText("x"));
    clipboard->setAvailable(true);

    auto* session = host.fakeTextInputSession(*id);
    REQUIRE(session != nullptr);
    CHECK_FALSE(session->active());
    session->start();
    CHECK(session->active());
    CHECK(session->startCount == 1);
    lumen::platform::TextInputEditingState state;
    state.text = "你好";
    state.selectionBase = 0;
    state.selectionExtent = 2;
    state.hasComposing = true;
    state.composingBase = 0;
    state.composingExtent = 2;
    state.caretRect = core::Rect::fromXYWH(8.0F, 4.0F, 1.0F, 20.0F);
    session->setEditingState(state);
    CHECK(session->stateCount == 1);
    CHECK(session->lastState.text == "你好");
    CHECK(session->lastState.selectionExtent == 2);
    session->stop();
    CHECK_FALSE(session->active());
    // 未知窗口的服务查询安全返回空。
    CHECK(host.textInputSession(core::WindowId{9999}) == nullptr);
    CHECK(host.platformWindow(core::WindowId{9999}) == nullptr);
}

TEST_CASE("fake_host_capabilities_are_queryable_and_overridable",
          "[platform]") {
    FakeApplicationHost host;
    const auto caps = host.capabilities();
    CHECK(caps.clipboard);
    CHECK(caps.textInput);
    CHECK(caps.ime);
    CHECK(caps.multiWindow);
    CHECK(caps.adapterName == "fake");

    lumen::platform::PlatformCapabilities degraded;
    degraded.adapterName = "fake-degraded";
    host.setCapabilities(degraded);
    CHECK_FALSE(host.capabilities().clipboard);
    CHECK(host.capabilities().adapterName == "fake-degraded");
}

// 8A 出口条件：counter 在两个独立 fake 窗口中分别处理状态和帧。
TEST_CASE("counter_runs_independently_in_two_fake_host_windows",
          "[platform][integration]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    drainOne(host);

    lumen::examples::CounterApp appA;
    lumen::examples::CounterApp appB;
    appA.setView(core::Size{400.0F, 300.0F});
    appB.setView(core::Size{400.0F, 300.0F});
    // 首帧先行：布局树就绪后才能按 key 取中心点。
    appA.renderFrame();
    appB.renderFrame();

    const auto idA = host.createWindow({});
    const auto idB = host.createWindow({});
    REQUIRE(idA.has_value());
    REQUIRE(idB.has_value());

    const auto centerOf = [](lumen::examples::CounterApp& app,
                             const char* key) {
        const core::RenderNode* node = core::findNodeByKey(app.root(), key);
        return core::absoluteOffset(app.root(), key) +
               core::Offset{node->size.width * 0.5F,
                            node->size.height * 0.5F};
    };

    // 窗口 A 点击两次递增；窗口 B 输入名字。事件按 WindowId 路由。
    for (int i = 0; i < 2; ++i) {
        host.pushPointerDown(*idA, centerOf(appA, "increment-button"));
        host.pushPointerUp(*idA, centerOf(appA, "increment-button"));
    }
    host.pushPointerDown(*idB, centerOf(appB, "name-field"));
    host.pushPointerUp(*idB, centerOf(appB, "name-field"));
    host.pushTextInput(*idB, "Lumen");

    core::HostEvent event;
    while (host.pollEvent(event)) {
        REQUIRE(event.window.valid());
        if (event.window == *idA) {
            switch (event.type) {
                case core::HostEventType::PointerDown:
                    appA.pointerDown(event.position);
                    break;
                case core::HostEventType::PointerUp:
                    appA.pointerUp(event.position);
                    break;
                default:
                    break;
            }
        } else {
            switch (event.type) {
                case core::HostEventType::PointerDown:
                    appB.pointerDown(event.position);
                    break;
                case core::HostEventType::PointerUp:
                    appB.pointerUp(event.position);
                    break;
                case core::HostEventType::TextInput:
                    appB.textInput(event.text);
                    break;
                default:
                    break;
            }
        }
    }

    CHECK(appA.counterValue() == 2);
    CHECK(appA.state().get("name").empty());
    CHECK(appB.counterValue() == 0);
    CHECK(appB.state().get("name") == "Lumen");
    // 两窗口各自产生确定性帧哈希且互不相同（视图内容不同）。
    const auto hashA = appA.renderFrame();
    const auto hashB = appB.renderFrame();
    CHECK(hashA != 0);
    CHECK(hashB != 0);
    CHECK(hashA != hashB);
}

TEST_CASE("sdl3_host_smoke_creates_window_and_pumps_events", "[platform]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    lumen::platform::Sdl3ApplicationHost host;
    REQUIRE(host.initialize());
    CHECK(std::string(hostStageName()) != "");

    WindowDesc desc;
    desc.title = "Lumen Host Smoke";
    desc.width = 320;
    desc.height = 240;
    const auto id = host.createWindow(desc);
    REQUIRE(id.has_value());
    CHECK(id->valid());

    const auto metrics = host.windowMetrics(*id);
    REQUIRE(metrics.has_value());
    CHECK(metrics->logicalSize.width == 320.0F);
    CHECK(metrics->logicalSize.height == 240.0F);
    CHECK(metrics->drawableSize.width > 0.0F);
    CHECK(metrics->visible);

    // 过渡适配：宿主窗口可按旧 PlatformWindow 接口使用（present 等）。
    auto* window = host.platformWindow(*id);
    REQUIRE(window != nullptr);
    render::PixelBuffer buffer;
    buffer.width = 64;
    buffer.height = 64;
    buffer.rgba.assign(64 * 64 * 4, 120);
    CHECK(window->present(buffer) == lumen::platform::PresentResult::Ok);

    // TextInputSession 适配：启停与编辑状态回放不崩溃。
    auto* session = host.textInputSession(*id);
    REQUIRE(session != nullptr);
    session->start();
    CHECK(session->active());
    lumen::platform::TextInputEditingState state;
    state.caretRect = core::Rect::fromXYWH(4.0F, 4.0F, 1.0F, 16.0F);
    session->setEditingState(state);
    session->stop();

    // 剪贴板服务可用（dummy driver 下 SDL 剪贴板 API 仍工作）。
    auto* clipboard = host.clipboard();
    if (clipboard != nullptr && clipboard->setText("lumen-smoke")) {
        CHECK(clipboard->hasText());
        CHECK(clipboard->text() == "lumen-smoke");
    }

    // 事件泵：空队列安全返回 false；焦点等初始事件格式良好。
    core::HostEvent event;
    int pumped = 0;
    while (host.pollEvent(event) && pumped < 16) {
        ++pumped;
        CHECK(event.type != core::HostEventType::None);
        if (event.type == core::HostEventType::PointerDown ||
            event.type == core::HostEventType::PointerMove) {
            CHECK(event.window == *id);
        }
    }
    CHECK_FALSE(host.pollEvent(event));

    host.destroyWindow(*id);
    CHECK_FALSE(host.windowMetrics(*id).has_value());
    host.shutdown();
    CHECK(host.lifecycle() == core::AppLifecycle::Terminating);
}

// --- M4：平台服务契约（Fake host 确定性记录 + 失败注入） ---

TEST_CASE("platform_service_results_are_structured", "[platform][m4]") {
    using lumen::platform::ServiceError;
    using lumen::platform::ServiceResult;

    const auto ok = ServiceResult::success();
    CHECK(ok.ok);
    CHECK(ok.error == ServiceError::None);
    CHECK(ok.message.empty());

    const auto unavailable =
        ServiceResult::unavailable("dialogs unsupported");
    CHECK_FALSE(unavailable.ok);
    CHECK(unavailable.error == ServiceError::Unavailable);
    CHECK(unavailable.message == "dialogs unsupported");

    const auto cancelled = ServiceResult::cancelled();
    CHECK_FALSE(cancelled.ok);
    CHECK(cancelled.error == ServiceError::Cancelled);

    const auto failed = ServiceResult::failed("EIO");
    CHECK_FALSE(failed.ok);
    CHECK(failed.error == ServiceError::Failed);
}

TEST_CASE("fake_host_file_dialog_semantics", "[platform][m4]") {
    using lumen::platform::FileDialogRequest;
    using lumen::platform::FileDialogResult;
    using lumen::platform::FakeApplicationHost;
    using lumen::platform::ServiceError;

    FakeApplicationHost host;
    REQUIRE(host.initialize());
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());
    // 清空 createWindow 的积压事件（窗口焦点等），只看对话框语义。
    core::HostEvent drain;
    while (host.pollEvent(drain)) {
    }

    SECTION("no queued result reports unavailable without blocking") {
        FileDialogRequest request;
        request.title = "Open";
        const auto result = host.requestFileDialog(*id, request);
        CHECK_FALSE(result.ok);
        CHECK(result.error == ServiceError::Unavailable);
        CHECK_FALSE(result.message.empty());
        CHECK(host.fileDialogCalls.size() == 1);
    }

    SECTION("injected request failure is synchronous and structured") {
        host.setFileDialogFailure(
            lumen::platform::ServiceResult::unavailable("denied"));
        const auto result =
            host.requestFileDialog(*id, FileDialogRequest{});
        CHECK_FALSE(result.ok);
        CHECK(result.error == ServiceError::Unavailable);
        // 请求失败不发完成事件。
        core::HostEvent event;
        CHECK_FALSE(host.pollEvent(event));
    }

    SECTION("queued result completes as FileDialogCompleted event") {
        FileDialogResult queued;
        queued.status = lumen::platform::ServiceResult::success();
        queued.paths = {"/tmp/a.txt", "/tmp/b.txt"};
        host.queueFileDialogResult(std::move(queued));

        FileDialogRequest request;
        request.title = "Open";
        request.allowMultiple = true;
        CHECK(host.requestFileDialog(*id, request).ok);

        core::HostEvent event;
        REQUIRE(host.pollEvent(event));
        CHECK(event.type == core::HostEventType::FileDialogCompleted);
        CHECK(event.window == *id);
        REQUIRE(event.filePaths.size() == 2);
        CHECK(event.filePaths[0] == "/tmp/a.txt");
        CHECK(event.filePaths[1] == "/tmp/b.txt");
        CHECK(event.text.empty());  // 成功无诊断。
    }

    SECTION("cancellation completes with empty paths and no error") {
        FileDialogResult queued;
        queued.status = lumen::platform::ServiceResult::cancelled();
        host.queueFileDialogResult(std::move(queued));
        CHECK(host.requestFileDialog(*id, FileDialogRequest{}).ok);
        core::HostEvent event;
        REQUIRE(host.pollEvent(event));
        CHECK(event.filePaths.empty());
        CHECK(event.text.empty());
    }
}

TEST_CASE("fake_host_records_cursor_icon_url_and_notifications",
          "[platform][m4]") {
    using lumen::platform::FakeApplicationHost;
    using lumen::platform::NotificationRequest;
    using lumen::platform::SystemCursor;
    using lumen::platform::WindowIcon;

    FakeApplicationHost host;
    REQUIRE(host.initialize());
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());

    // openUrl：默认成功 + 记录；注入失败结构化。
    CHECK(host.openUrl("https://example.com").ok);
    REQUIRE(host.openUrlCalls.size() == 1);
    CHECK(host.openUrlCalls[0].url == "https://example.com");
    host.setOpenUrlFailure(
        lumen::platform::ServiceResult::failed("no browser"));
    CHECK_FALSE(host.openUrl("https://example.com/2").ok);
    CHECK(host.openUrlCalls.back().result.error ==
          lumen::platform::ServiceError::Failed);

    // 通知：默认成功 + 记录。
    NotificationRequest notification;
    notification.title = "T";
    notification.body = "B";
    CHECK(host.postNotification(notification).ok);
    REQUIRE(host.notificationCalls.size() == 1);
    CHECK(host.notificationCalls[0].request.title == "T");

    // 光标：按窗口记录。
    host.setCursor(*id, SystemCursor::IBeam);
    host.setCursor(*id, SystemCursor::PointingHand);
    REQUIRE(host.cursorCalls.size() == 2);
    CHECK(host.cursorCalls[0].second == SystemCursor::IBeam);
    CHECK(host.cursorCalls[1].second == SystemCursor::PointingHand);

    // 图标：记录 + 失败注入。
    WindowIcon icon;
    icon.width = 2;
    icon.height = 2;
    icon.rgba.assign(2 * 2 * 4, 0xFF);
    CHECK(host.setWindowIcon(*id, icon).ok);
    REQUIRE(host.iconCalls.size() == 1);
    CHECK(host.iconCalls[0].icon.width == 2);
    host.setIconFailure(
        lumen::platform::ServiceResult::failed("bad pixels"));
    CHECK_FALSE(host.setWindowIcon(*id, icon).ok);
}

TEST_CASE("platform_capabilities_report_services_and_appearance",
          "[platform][m4]") {
    lumen::platform::FakeApplicationHost host;
    REQUIRE(host.initialize());
    auto caps = host.capabilities();
    // Fake host 默认：服务关闭（测试显式注入能力）。
    CHECK_FALSE(caps.fileDialogs);
    CHECK_FALSE(caps.notifications);

    caps.fileDialogs = true;
    caps.notifications = false;
    caps.openUrl = true;
    caps.cursorShape = true;
    caps.windowIcon = true;
    caps.prefersDarkMode = true;
    caps.accentColor = lumen::core::Color::fromRGBA(10, 20, 30);
    caps.fontScale = 1.25F;
    host.setCapabilities(caps);

    const auto updated = host.capabilities();
    CHECK(updated.fileDialogs);
    CHECK(updated.openUrl);
    CHECK(updated.cursorShape);
    CHECK(updated.windowIcon);
    CHECK(updated.prefersDarkMode);
    CHECK(updated.accentColor == lumen::core::Color::fromRGBA(10, 20, 30));
    CHECK(updated.fontScale == 1.25F);
}

// M12：系统主题切换事件（fake：能力位刷新 + 事件广播同 SDL 语义）。
TEST_CASE("fake_host_system_theme_changed_updates_caps_and_events",
          "[platform]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    CHECK_FALSE(host.capabilities().prefersDarkMode);
    core::HostEvent event;
    host.createWindow({});
    // 清空 createWindow 的窗口广播积压（测试惯例）。
    while (host.pollEvent(event)) {
    }
    host.pushSystemThemeChanged(true);
    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::SystemThemeChanged);
    CHECK(host.capabilities().prefersDarkMode);
    host.pushSystemThemeChanged(false);
    REQUIRE(host.pollEvent(event));
    CHECK_FALSE(host.capabilities().prefersDarkMode);
}

// M12：原生服务 seam（dummy 驱动下能力报告与结构化通知结果；真发送
// 不进 ctest——避免每次测试弹真实系统通知，视觉验收人工执行）。
TEST_CASE("sdl3_host_native_services_report_and_notify_structured",
          "[platform]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    lumen::platform::Sdl3ApplicationHost host;
    REQUIRE(host.initialize());
#if defined(_WIN32)
    // Windows 原生 seam 常开（真发送成败取决于 shell 会话）。
    CHECK(host.capabilities().notifications);
#endif
    // 强调色查询不崩溃；无能力的平台保持安全默认（任何值合法）。
    const auto capabilities = host.capabilities();
    (void)capabilities.accentColor;
    CHECK(capabilities.fontScale >= 0.5F);
    CHECK(capabilities.fontScale <= 3.0F);
    // highContrast/reduceAnimation are host snapshots; their values depend
    // on the desktop session and are intentionally not hard-coded here.
    // 真发送不进 ctest（见上），仅能力位断言。
}

// --- M15：拖放平台契约（OS 拖入事件归一化 + 拖出结构化降级） ---

TEST_CASE("fake_host_drag_drop_events_normalize_payload_and_window",
          "[platform][m15]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    const auto id = host.createWindow(WindowDesc{});
    REQUIRE(id.has_value());

    // 排空 initialize/createWindow 的初始事件（生命周期/焦点），只留
    // 拖放注入序列。
    core::HostEvent event{};
    while (host.pollEvent(event)) {
    }

    // 事件序列：Enter → Move → Drop(text) / Drop(files) → Leave；字段与
    // 宿主翻译后的归一化约定一致（position 逻辑坐标、text/filePaths 负载）。
    host.pushDragEnter(*id, core::Offset{10.0F, 12.0F});
    host.pushDragMove(*id, core::Offset{40.0F, 44.0F});
    host.pushDragDropText(*id, core::Offset{48.0F, 50.0F}, "dropped text");
    host.pushDragDropFiles(*id, core::Offset{60.0F, 70.0F},
                           {"/tmp/a.txt", "/tmp/b.txt"});
    host.pushDragLeave(*id, core::Offset{80.0F, 90.0F});

    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::DragEnter);
    CHECK(event.window == *id);
    CHECK(event.position.x == 10.0F);
    CHECK(event.position.y == 12.0F);

    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::DragMove);
    CHECK(event.position.x == 40.0F);

    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::DragDrop);
    CHECK(event.text == "dropped text");
    CHECK(event.filePaths.empty());
    CHECK(event.position.x == 48.0F);

    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::DragDrop);
    CHECK(event.text.empty());
    REQUIRE(event.filePaths.size() == 2);
    CHECK(event.filePaths[0] == "/tmp/a.txt");
    CHECK(event.filePaths[1] == "/tmp/b.txt");

    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::DragLeave);
    CHECK(event.position.x == 80.0F);

    CHECK_FALSE(host.pollEvent(event));
}

TEST_CASE("fake_host_start_drag_records_and_injects_failure",
          "[platform][m15]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    const auto id = host.createWindow(WindowDesc{});
    REQUIRE(id.has_value());

    // 默认成功（fake 模拟可用服务）；负载完整记录。
    lumen::platform::DragOutPayload payload;
    payload.text = "drag me";
    CHECK(host.startDrag(*id, payload).ok);
    REQUIRE(host.dragStartCalls.size() == 1);
    CHECK(host.dragStartCalls[0].window == *id);
    CHECK(host.dragStartCalls[0].payload.text == "drag me");
    CHECK(host.dragStartCalls[0].result.ok);

    // 失败注入：结构化 Unavailable，调用仍记录。
    host.setDragStartFailure(lumen::platform::ServiceResult::unavailable(
        "fake host: drag start unavailable"));
    const auto result = host.startDrag(*id, payload);
    REQUIRE(host.dragStartCalls.size() == 2);
    CHECK_FALSE(result.ok);
    CHECK(result.error == lumen::platform::ServiceError::Unavailable);
    CHECK(host.dragStartCalls[1].result.error ==
          lumen::platform::ServiceError::Unavailable);

    // 能力位默认 false（fake host 不隐含拖放可用；由测试显式覆写）。
    CHECK_FALSE(host.capabilities().dragDropReceive);
    CHECK_FALSE(host.capabilities().dragDropStart);
}

TEST_CASE("sdl3_host_drag_drop_capabilities_and_start_drag_unavailable",
          "[platform][m15]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    lumen::platform::Sdl3ApplicationHost host;
    REQUIRE(host.initialize());

    // 拖入事件随视频子系统可用；拖出发起 SDL 3.2.10 无 API——能力位
    // 如实 false，调用返回结构化 Unavailable（不阻塞、可诊断）。
    const auto capabilities = host.capabilities();
    CHECK(capabilities.dragDropReceive);
    CHECK_FALSE(capabilities.dragDropStart);

    lumen::platform::DragOutPayload payload;
    payload.text = "out";
    const auto result = host.startDrag(core::WindowId{1}, payload);
    CHECK_FALSE(result.ok);
    CHECK(result.error == lumen::platform::ServiceError::Unavailable);
    CHECK(!result.message.empty());
}

// --- M16：窗口能力（全屏/置顶/OS 模态：记录 + 状态驱动事件） ---

TEST_CASE("fake_host_window_capabilities_record_and_sync",
          "[platform][m16]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    const auto id = host.createWindow(WindowDesc{});
    REQUIRE(id.has_value());
    core::HostEvent event{};
    while (host.pollEvent(event)) {
    }

    // 全屏切换：metrics 同步 + Entered/Exited 事件；再切一次回到窗口态。
    host.toggleFullscreen(*id);
    CHECK_FALSE(host.windowMetrics(*id)->fullscreen == false);
    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::WindowFullscreenEntered);
    CHECK(event.window == *id);
    host.toggleFullscreen(*id);
    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::WindowFullscreenExited);
    CHECK(host.windowMetrics(*id).has_value());
    CHECK_FALSE(host.windowMetrics(*id)->fullscreen);

    // 置顶与模态：记录 + 结构化结果；无效父窗口 Failed。
    host.setAlwaysOnTop(*id, true);
    REQUIRE(host.alwaysOnTopCalls.size() == 1);
    CHECK(host.alwaysOnTopCalls[0].first == *id);
    CHECK(host.alwaysOnTopCalls[0].second);
    CHECK(host.setWindowModal(*id, core::WindowId{999}).error ==
          lumen::platform::ServiceError::Failed);
    REQUIRE(host.modalCalls.size() == 1);
    CHECK(host.setWindowModal(*id, core::WindowId{}).ok);
    CHECK(host.modalCalls.size() == 2);

    // 命令记录含 fullscreen（与 minimize/maximize 同通道）。
    bool sawFullscreen = false;
    for (const auto& call : host.windowCommandCalls) {
        if (call == "fullscreen") sawFullscreen = true;
    }
    CHECK(sawFullscreen);
}

TEST_CASE("sdl3_host_window_capabilities_smoke", "[platform][m16]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    lumen::platform::Sdl3ApplicationHost host;
    REQUIRE(host.initialize());
    const auto caps = host.capabilities();
    CHECK(caps.windowFullscreen);
    CHECK(caps.windowAlwaysOnTop);
    CHECK(caps.windowModal);

    const auto id = host.createWindow(WindowDesc{});
    REQUIRE(id.has_value());
    // dummy 后端调用安全（成败由平台决定；不崩溃即冒烟通过）。
    host.toggleFullscreen(*id);
    // 全屏状态传播（review 修复：windowMetrics 需反映宿主会话标志）——
    // 翻译到 ENTER 事件后 metrics.fullscreen 为 true。
    core::HostEvent fsEvent{};
    while (host.pollEvent(fsEvent)) {
        if (fsEvent.type == core::HostEventType::WindowFullscreenEntered) {
            CHECK(host.windowMetrics(*id)->fullscreen);
            break;
        }
    }
    host.setAlwaysOnTop(*id, true);
    (void)host.setWindowModal(*id, core::WindowId{999});
    // 无效窗口模态：结构化 Failed。
    CHECK(host.setWindowModal(core::WindowId{7}, core::WindowId{}).error ==
          lumen::platform::ServiceError::Failed);
    core::HostEvent event{};
    int pumped = 0;
    while (host.pollEvent(event) && pumped < 16) {
        ++pumped;
    }
    host.destroyWindow(*id);
    host.shutdown();
}

// --- M16：托盘与全局快捷键（记录/失败注入 + 事件回灌 + 能力位） ---

TEST_CASE("fake_host_tray_and_hotkeys_record_and_deliver",
          "[platform][m16]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    const auto id = host.createWindow(WindowDesc{});
    REQUIRE(id.has_value());
    core::HostEvent event{};
    while (host.pollEvent(event)) {
    }

    // 托盘：完整记录（菜单项 command）；失败注入结构化。
    TraySetup tray;
    tray.tooltip = "Lumen";
    tray.menu.push_back(TrayMenuItem{"Open", "open", false});
    CHECK(host.setTray(*id, tray).ok);
    host.removeTray();
    REQUIRE(host.trayCalls.size() == 1);
    CHECK(host.trayCalls[0].window == *id);
    CHECK(host.trayCalls[0].setup.tooltip == "Lumen");
    CHECK(host.removeTrayCalls == 1);
    host.setTrayFailure(lumen::platform::ServiceResult::unavailable(
        "fake host: tray unavailable"));
    CHECK_FALSE(host.setTray(*id, tray).ok);

    // 快捷键：记录 + 注销计数 + 失败注入。
    GlobalHotkeySpec spec;
    spec.id = "capture";
    spec.modifiers = core::kModifierCtrl | core::kModifierAlt;
    spec.key = core::Key::Enter;
    CHECK(host.registerGlobalHotkey(*id, spec).ok);
    CHECK(host.unregisterGlobalHotkey("capture").ok);
    REQUIRE(host.hotkeyCalls.size() == 1);
    CHECK(host.hotkeyCalls[0].spec.id == "capture");
    CHECK(host.hotkeyUnregisterCalls == 1);

    // 事件回灌：TrayActivated.text = command；GlobalHotkey.text = id。
    host.pushTrayActivated(*id, "open");
    host.pushGlobalHotkey(*id, "capture");
    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::TrayActivated);
    CHECK(event.window == *id);
    CHECK(event.text == "open");
    REQUIRE(host.pollEvent(event));
    CHECK(event.type == core::HostEventType::GlobalHotkey);
    CHECK(event.text == "capture");
}

TEST_CASE("sdl3_host_tray_smoke_and_hotkeys_structured", "[platform][m16]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    lumen::platform::Sdl3ApplicationHost host;
    REQUIRE(host.initialize());
    CHECK(host.capabilities().systemTray);
    // R4：快捷键能力随平台接缝如实（X11 可用 = true，其余 false）；
    // 注册结果在两种状态下都必须结构化。
    const bool hotkeysAvailable = host.capabilities().globalHotkeys;
    CAPTURE(hotkeysAvailable);

    // dummy 后端托盘创建可能失败——结果必须结构化（ok 或 Failed 带诊断）。
    TraySetup tray;
    tray.tooltip = "smoke";
    tray.menu.push_back(TrayMenuItem{"Quit", "quit", false});
    const auto trayResult = host.setTray(core::WindowId{1}, tray);
    if (!trayResult.ok) {
        CHECK(trayResult.error == lumen::platform::ServiceError::Failed);
        CHECK(!trayResult.message.empty());
    }
    host.removeTray();

    GlobalHotkeySpec spec;
    spec.id = "x";
    spec.modifiers = core::kModifierCtrl | core::kModifierAlt;
    spec.key = core::Key::Escape;
    const auto hotkeyResult =
        host.registerGlobalHotkey(core::WindowId{1}, spec);
    if (hotkeysAvailable) {
        // X11 后端（Xvfb/真实 X 会话）：合法注册成立；重复 id 与未知
        // 注销都结构化 Failed。
        CHECK(hotkeyResult.ok);
        const auto duplicate =
            host.registerGlobalHotkey(core::WindowId{1}, spec);
        CHECK_FALSE(duplicate.ok);
        CHECK(duplicate.error == lumen::platform::ServiceError::Failed);
        CHECK(!duplicate.message.empty());
        CHECK(host.unregisterGlobalHotkey("x").ok);
        const auto unknown = host.unregisterGlobalHotkey("x");
        CHECK_FALSE(unknown.ok);
        CHECK(unknown.error == lumen::platform::ServiceError::Failed);
    } else {
        // 无平台后端（Wayland/无显示/Win/mac 未实现）——Unavailable
        // + 可读原因。
        CHECK_FALSE(hotkeyResult.ok);
        CHECK(hotkeyResult.error == lumen::platform::ServiceError::Unavailable);
        CHECK(!hotkeyResult.message.empty());
        CHECK_FALSE(host.unregisterGlobalHotkey("x").ok);
    }
    host.shutdown();
}

// --- R4：X11 全局快捷键（映射纯函数 + XTEST 端到端） ---
// 接缝符号仅随 Linux 源文件存在（global_hotkeys.cpp X11 分支），其他
// 平台不编译本组用例。
#if defined(__linux__)

TEST_CASE("x11_hotkey_mapping_translates_core_keys", "[platform][m16]") {
    namespace hotkeys = lumen::platform::hotkeys;
    using lumen::core::Key;
    // keysym 值与 <X11/keysymdef.h> 同值（确定性契约，格式/回放共用）。
    CHECK(hotkeys::keysymForLumenKey(Key::Escape) == 0xff1b);
    CHECK(hotkeys::keysymForLumenKey(Key::Enter) == 0xff0d);
    CHECK(hotkeys::keysymForLumenKey(Key::Backtab) == 0xff89);
    CHECK(hotkeys::keysymForLumenKey(Key::PageUp) == 0xff55);
    CHECK(hotkeys::keysymForLumenKey(Key::Delete) == 0xffff);
    CHECK(hotkeys::keysymForLumenKey(Key::None) == 0);
    // 修饰位显式映射（Shift/Ctrl/Alt=Mod1/Gui=Mod4）。
    CHECK(hotkeys::modifierMask(core::kModifierShift) == (1 << 0));
    CHECK(hotkeys::modifierMask(core::kModifierCtrl) == (1 << 2));
    CHECK(hotkeys::modifierMask(core::kModifierAlt) == (1 << 3));
    CHECK(hotkeys::modifierMask(core::kModifierGui) == (1 << 6));
    CHECK(hotkeys::modifierMask(core::kModifierCtrl | core::kModifierAlt) ==
          ((1 << 2) | (1 << 3)));
    CHECK(hotkeys::modifierMask(0) == 0);
}

TEST_CASE("x11_global_hotkey_end_to_end_with_xtest", "[platform][m16]") {
#if defined(LUMEN_HAS_XTEST)
    const char* enabled = ::getenv("LUMEN_GLOBAL_HOTKEY_E2E");
    if (enabled == nullptr || std::string{enabled} != "1") {
        // 显式开启的现场验证（X11 会话 + XTEST；CI 由 platform-acceptance
        // 或人工执行，普通 ctest 跳过）。
        WARN("LUMEN_GLOBAL_HOTKEY_E2E=1 not set — skipping XTEST e2e");
        return;
    }
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    lumen::platform::Sdl3ApplicationHost host;
    REQUIRE(host.initialize());
    if (!host.capabilities().globalHotkeys) {
        WARN("global hotkeys unavailable in this session — skipping e2e");
        host.shutdown();
        return;
    }
    const auto windowId = host.createWindow(WindowDesc{});
    REQUIRE(windowId.has_value());
    core::HostEvent event{};
    while (host.pollEvent(event)) {
    }

    GlobalHotkeySpec spec;
    spec.id = "e2e";
    // Alt+Escape：Xvfb 极简 keymap 中 Control_L 的键码可能映射到锁定修
    // 饰符（合成 state 为 Lock|Mod1 而非 Ctrl|Alt），真实键盘无此问题；
    // 测试选 Alt 单修饰组合，依赖真实 Mod1 键码。
    spec.modifiers = core::kModifierAlt;
    spec.key = core::Key::Escape;
    REQUIRE(host.registerGlobalHotkey(*windowId, spec).ok);

    // XTEST 合成 Alt+Escape（Alt_L 0xffe9 / Escape 0xff1b）；事件经根
    // 窗口 grab 投递到后端连接。
    Display* xtest = XOpenDisplay(nullptr);
    REQUIRE(xtest != nullptr);
    const auto fakeKey = [&](unsigned long keysym, bool press) {
        const KeyCode code = XKeysymToKeycode(xtest, keysym);
        REQUIRE(code != 0);
        REQUIRE(XTestFakeKeyEvent(xtest, code, press ? 1 : 0, 0));
    };
    fakeKey(0xffe9, true);   // Alt_L down
    fakeKey(0xff1b, true);   // Escape down
    fakeKey(0xff1b, false);  // Escape up
    fakeKey(0xffe9, false);  // Alt_L up
    XSync(xtest, 0);  // Bool=False（Xlib 宏已 undef）。

    const auto isE2E = [](const core::HostEvent& e) {
        return e.type == core::HostEventType::GlobalHotkey &&
               e.text == "e2e";
    };
    bool delivered = false;
    core::HostEvent hotkey{};
    for (int attempt = 0; attempt < 100 && !delivered; ++attempt) {
        while (host.pollEvent(event)) {
            if (isE2E(event)) {
                delivered = true;
                hotkey = event;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(delivered);
    CHECK(hotkey.window == *windowId);

    // 注销后同一合成按键不再回灌。
    REQUIRE(host.unregisterGlobalHotkey("e2e").ok);
    fakeKey(0xffe9, true);
    fakeKey(0xff1b, true);
    fakeKey(0xff1b, false);
    fakeKey(0xffe9, false);
    XSync(xtest, 0);  // Bool=False（Xlib 宏已 undef）。
    bool leaked = false;
    for (int attempt = 0; attempt < 50 && !leaked; ++attempt) {
        while (host.pollEvent(event)) {
            if (isE2E(event)) {
                leaked = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK_FALSE(leaked);
    XCloseDisplay(xtest);
    host.shutdown();
#else
    WARN("XTEST e2e is Linux-only");
#endif
}

#endif  // defined(__linux__)
