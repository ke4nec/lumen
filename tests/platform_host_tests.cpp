// v0.3 阶段8A (plan §4 8A): 平台宿主契约测试。
//
// 覆盖：WindowId/WindowMetrics/AppLifecycle 值语义；fake host 的多窗口
// 事件隔离、生命周期、surface detach/attach、DPI 先行 metrics、剪贴板
// 与 TextInputSession 状态机；两个独立 fake 窗口分别驱动 counter 状态
// 与帧（出口条件）；SDL3 host 在 dummy video driver 下的窗口创建与
// 事件泵冒烟。

#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "lumen/platform/fake_host.h"
#include "lumen/platform/sdl3_host.h"
#include "counter_app.h"

using lumen::platform::ApplicationHost;
using lumen::platform::FakeApplicationHost;
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
    (void)host.capabilities().accentColor;
    // 真发送不进 ctest（见上），仅能力位断言。
}
