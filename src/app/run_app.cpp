// M2/M14：runApp —— UI 线程主循环。
//
// 每个 AppWindow 拥有独立的窗口、renderer、IME、无障碍桥和
// FrameScheduler；ApplicationHost 只提供归一化事件和平台服务。

#include "lumen/app/app_shell.h"
#include "lumen/app/font_loader.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

#include "lumen/core/windowing.h"
#include "lumen/diagnostics/runtime_diagnostics.h"
#include "lumen/platform/application_host.h"
#include "lumen/render/frame_scheduler.h"

namespace lumen::app {
namespace {

using core::HostEvent;
using core::HostEventType;
using core::WindowMetrics;

void printStartupDiagnostics(const render::RendererCapabilities& caps,
                            const WindowMetrics& metrics) {
    std::printf(
        "[diag] os=%s backend=%s gpu=%s partial=%s dpi=%.2f pixels=%.0fx%.0f\n",
#if defined(_WIN32)
        "windows",
#elif defined(__APPLE__)
        "macos",
#elif defined(__linux__)
        "linux",
#else
        "other",
#endif
        caps.backendName, caps.gpu ? "yes" : "no",
        caps.partialSubmit ? "yes" : "no", metrics.deviceScale,
        metrics.drawableSize.width, metrics.drawableSize.height);
}

void printFrameDiagnostics(std::uint64_t frames, std::uint32_t partial,
                           const render::RenderStats& stats,
                           std::uint64_t skippedIdleTurns,
                           core::WindowId window,
                           const render::ResourceManager::Diagnostics* resources) {
    // M14-C：诊断行必须可定位窗口与资源层（失败/缓存对齐“结构化、
    // 可诊断”出口；无资源层时省略该维度）。
    std::string resourceText;
    if (resources != nullptr) {
        resourceText = " requests=" +
                       std::to_string(resources->requested) +
                       " failures=" + std::to_string(resources->failures) +
                       " cancels=" + std::to_string(resources->cancels) +
                       " reuploads=" + std::to_string(resources->reuploads);
    }
    std::printf(
        "[diag] window=%u frames=%llu partial=%u idleTurns=%llu cmds=%llu "
        "culled=%llu submitMs=%.2f gpuWaitMs=%.2f buildMs=%.2f "
        "uploads=%llu%s%s%s\n",
        window.value, static_cast<unsigned long long>(frames), partial,
        static_cast<unsigned long long>(skippedIdleTurns),
        static_cast<unsigned long long>(stats.commandCount),
        static_cast<unsigned long long>(stats.culledCommands), stats.submitMs,
        stats.gpuWaitMs, stats.cpuBuildMs,
        static_cast<unsigned long long>(stats.uploads),
        resourceText.c_str(),
        stats.fullFrameFallback ? " fallback=yes" : "",
        stats.fallbackReason.empty()
            ? ""
            : (" reason=" + stats.fallbackReason).c_str());
}

struct WindowRuntime {
    AppWindow app{};
    core::WindowId id{};
    RendererSetup setup{};
    std::unique_ptr<accessibility::AccessibilityBridge> nativeA11y{};
    std::unique_ptr<render::FrameScheduler> scheduler{};
    // M14-C：asyncFonts 时的后台字体加载（完成 pump 见主循环）。
    std::unique_ptr<FontLoader> fontLoader{};
    platform::PlatformWindow* presentWindow{nullptr};
    core::PointerCursor lastCursor{core::PointerCursor::Arrow};
    std::uint64_t idleTurns{0};
    std::uint64_t lastPollMs{0};
    std::uint64_t lastDiagPrintMs{0};
    bool focused{true};
    bool active{true};
};

}  // namespace

int runApp(std::vector<AppWindow> windows, platform::ApplicationHost& host) {
    if (windows.empty()) {
        return 1;
    }
    for (const auto& window : windows) {
        if (window.shell == nullptr) {
            return 1;
        }
    }
    if (!host.initialize()) {
        return 1;
    }

    // G-2：崩溃兜底与持久日志（进程级；首个配置了目录的窗口生效）。
    // start() 检测上次脏标记；正常退出在 cleanup 后 cleanShutdown（清
    // 标记）。崩溃路径不经此（处理器直接终止）。上次运行摘要经
    // onDiagnosticsStarted 回调（RunOptions 按值传入，字段回传无效）。
    std::optional<diagnostics::RuntimeDiagnostics> runtimeDiagnostics;
    for (auto& window : windows) {
        if (window.options.diagnosticsDirectory.empty()) {
            continue;
        }
        diagnostics::RuntimeDiagnosticsOptions diagOptions;
        diagOptions.directory = window.options.diagnosticsDirectory;
        diagOptions.appName = window.options.diagnosticsAppName;
        runtimeDiagnostics.emplace(std::move(diagOptions));
        if (!runtimeDiagnostics->start()) {
            runtimeDiagnostics.reset();
            continue;
        }
        if (window.options.onDiagnosticsStarted) {
            window.options.onDiagnosticsStarted(
                runtimeDiagnostics->lastRunCrash());
        }
        if (window.options.diagnostics) {
            const auto& crash = runtimeDiagnostics->lastRunCrash();
            std::fprintf(stdout,
                         "[diag] last-run-crashed=%d report=%s log=%s\n",
                         crash.crashed ? 1 : 0,
                         crash.reportPath.c_str(), crash.logPath.c_str());
        }
        break;
    }

    render::RealtimeClock clock;
    std::vector<WindowRuntime> runtimes;
    runtimes.reserve(windows.size());
    int exitCode = 0;

    const auto resetSurface = [&](WindowRuntime& runtime) {
        if (runtime.setup.renderer == nullptr) {
            return;
        }
        const auto metrics = host.windowMetrics(runtime.id);
        if (!metrics.has_value()) {
            return;
        }
        render::RenderSurfaceDesc surface;
        surface.window = runtime.id;
        surface.widthPixels = static_cast<int>(metrics->drawableSize.width);
        surface.heightPixels = static_cast<int>(metrics->drawableSize.height);
        surface.deviceScale = metrics->deviceScale;
        if (auto* window = host.platformWindow(runtime.id);
            window != nullptr) {
            const auto native = window->nativeSurface();
            surface.nativeWindow = native.nativeWindow;
            surface.windowSystem = native.windowSystem;
        }
        runtime.setup.renderer->resetSurface(surface);
    };

    const auto applyMetrics = [&](WindowRuntime& runtime) {
        const auto metrics = host.windowMetrics(runtime.id);
        if (!metrics.has_value() || metrics->logicalSize.width <= 0.0F) {
            return;
        }
        runtime.app.shell->setDeviceScale(metrics->deviceScale);
        runtime.app.shell->setView(metrics->logicalSize);
        if (runtime.setup.syncDeviceScale) {
            runtime.setup.syncDeviceScale(metrics->deviceScale);
        }
        resetSurface(runtime);
    };

    const auto metricsAllowSubmission = [&](const WindowRuntime& runtime) {
        const auto metrics = host.windowMetrics(runtime.id);
        return metrics.has_value() && metrics->visible &&
               !metrics->minimized;
    };

    const auto syncWindowVisibility = [&](WindowRuntime& runtime) {
        runtime.scheduler->setWindowVisible(metricsAllowSubmission(runtime));
    };

    const auto stopTextInput = [&](WindowRuntime& runtime) {
        if (auto* session = host.textInputSession(runtime.id);
            session != nullptr && session->active()) {
            session->stop();
        }
    };

    const auto applyAccessibilityPreferences = [&](WindowRuntime& runtime) {
        const auto preferences = host.capabilities();
        if (runtime.app.options.followSystemAccessibility &&
            preferences.systemAccessibilityPreferences) {
            runtime.app.shell->setSystemAccessibilitySettings(
                {preferences.highContrast, preferences.reduceAnimation, preferences.fontScale});
        }
    };

    const auto cleanup = [&]() {
        // Native accessibility providers may still be referenced by the host
        // window, so tear them down while the host-owned windows still exist.
        for (auto& runtime : runtimes) {
            runtime.nativeA11y.reset();
            runtime.app.shell->setAccessibilityBridge(nullptr);
        }
        host.noteAccessibilityBridgeActive(false);
    };

    bool setupFailed = false;
    for (auto& appWindow : windows) {
        WindowRuntime runtime;
        runtime.app = std::move(appWindow);
        const auto windowId = host.createWindow(runtime.app.options.windowDesc);
        if (!windowId.has_value()) {
            setupFailed = true;
            break;
        }
        runtime.id = *windowId;
        if (runtime.app.options.rendererFactory) {
            runtime.setup =
                runtime.app.options.rendererFactory(host, runtime.id);
        }
        // A renderer factory may replace the window while setting up a GPU
        // surface. A missing final window is a fatal setup error.
        if (!runtime.id.valid() || !host.windowMetrics(runtime.id).has_value()) {
            setupFailed = true;
            break;
        }

        AppShell& shell = *runtime.app.shell;
        applyAccessibilityPreferences(runtime);
        shell.setRenderer(runtime.setup.renderer);
        shell.setFrameAllocationSource(
            runtime.app.options.frameAllocationSource);
        if (runtime.app.options.fontFactory) {
            // M14-C：asyncFonts 时工厂移到后台线程（首帧占位度量），
            // 完成后在主循环热替换；同步路径保持原语义。
            if (runtime.app.options.asyncFonts) {
                // RunOptions 工厂无诊断出参：成功取 manager->diagnostic()，
                // 失败由工厂闭包自行输出细节，此处给框架级摘要。
                runtime.fontLoader = std::make_unique<FontLoader>(
                    [factory = runtime.app.options.fontFactory](
                        std::string* diagnostics)
                        -> std::shared_ptr<text::FontManager> {
                        auto fonts = factory();
                        if (diagnostics != nullptr) {
                            *diagnostics =
                                fonts != nullptr
                                    ? fonts->diagnostic()
                                    : "async font factory kept placeholder "
                                      "metrics";
                        }
                        return fonts;
                    });
            } else if (auto fonts = runtime.app.options.fontFactory()) {
                shell.setFontManager(std::move(fonts));
            }
        }
        // M14-C：资源层注入（upload 命令前置；完成 pump 在主循环）。
        if (runtime.app.options.resourceManager) {
            shell.setResourceManager(runtime.app.options.resourceManager);
        }
        if (auto* clipboard = host.clipboard()) {
            shell.controller().setClipboard(clipboard);
        }
        if (runtime.app.options.windowIcon && host.capabilities().windowIcon) {
            const platform::WindowIcon icon = runtime.app.options.windowIcon();
            if (icon.width > 0) {
                const auto result = host.setWindowIcon(runtime.id, icon);
                if (runtime.app.options.diagnostics) {
                    if (result.ok) {
                        std::printf("[diag] window icon: %dx%d\n", icon.width,
                                    icon.height);
                    } else {
                        std::fprintf(stderr, "[diag] window icon: %s\n",
                                     result.message.c_str());
                    }
                }
            }
        }
        if (runtime.app.options.windowDesc.customTitleBar) {
            host.setWindowDragRegion(runtime.id, [&shell](core::Offset position) {
                return shell.isWindowDragPoint(position);
            });
        }
        if (runtime.app.options.windowDesc.transparent) {
            shell.setClearColor(core::Color::transparent());
        }
        applyMetrics(runtime);

        if (runtime.app.options.nativeAccessibility) {
            accessibility::PlatformAccessibilityHost a11yHost;
            a11yHost.nativeWindow = host.nativeWindowHandle(runtime.id);
            a11yHost.deviceScale = host.windowMetrics(runtime.id)
                                        .value_or(WindowMetrics{})
                                        .deviceScale;
            a11yHost.applicationName = runtime.app.options.windowDesc.title;
            a11yHost.dispatch = [&shell](const std::string& nodeId,
                                         std::uint32_t action,
                                         const std::string& value,
                                         float scrollDeltaY) {
                return shell.performAccessibilityAction(nodeId, action, value,
                                                        scrollDeltaY);
            };
            // M13：AT 抓焦点 → 抬升窗口（GrabFocus 平台惯例；窗口焦点
            // 到达后经 WindowFocusGained 发真值 window:activate）。
            a11yHost.activateWindow = [&host, id = runtime.id] {
                host.raiseWindow(id);
                return true;
            };
            std::string diagnostics;
            runtime.nativeA11y = accessibility::createPlatformAccessibilityBridge(
                a11yHost, &diagnostics);
            if (runtime.nativeA11y != nullptr) {
                shell.setAccessibilityBridge(runtime.nativeA11y.get());
                host.noteAccessibilityBridgeActive(
                    runtime.nativeA11y->available());
                if (runtime.app.options.diagnostics) {
                    std::printf("[diag] a11y=%s%s\n",
                                runtime.nativeA11y->bridgeName().c_str(),
                                runtime.nativeA11y->available() ? "" : " (unavailable)");
                }
            } else if (runtime.app.options.diagnostics && !diagnostics.empty()) {
                std::printf("[diag] a11y=off (%s)\n", diagnostics.c_str());
            }
        }

        render::FrameScheduler::Config schedulerConfig;
        schedulerConfig.targetFps = 60;
        schedulerConfig.resizeDebounceMs = 16;
        schedulerConfig.pauseWhenHidden = true;
        runtime.scheduler = std::make_unique<render::FrameScheduler>(
            schedulerConfig, &clock);
        syncWindowVisibility(runtime);
        runtime.scheduler->requestFrame(render::FrameReason::Explicit,
                                        runtime.id);
        runtime.presentWindow = host.platformWindow(runtime.id);
        runtime.lastCursor = shell.pointerCursor();
        runtime.lastPollMs = clock.nowMs();
        runtime.lastDiagPrintMs = runtime.lastPollMs;
        // R6：帧读数 HUD（显式开启才装配）——启用采样 + 纯绘制图层
        //（makeFrameStatsLayer；2026-09-30 修复：不再经视觉 overlay 槽
        // 位——该槽位会随 eventTree 切换吞掉应用输入）；主循环每调度帧
        // 标脏刷新读数（见 requestPendingFrame 前）。bounds/damage 调试
        // 图层同构，无需帧循环介入。
        if (runtime.app.options.frameDebugOverlay) {
            shell.setFrameStatsCapture(true);
            shell.setDebugFrameStats(true);
        }
        shell.setDebugBoundsOverlay(runtime.app.options.debugBoundsOverlay);
        shell.setDebugDamageOverlay(runtime.app.options.debugDamageOverlay);
        shell.setDebugInspector(runtime.app.options.debugInspector);
        if (runtime.app.options.diagnostics) {
            printStartupDiagnostics(
                shell.capabilities(),
                host.windowMetrics(runtime.id).value_or(WindowMetrics{}));
        }
        runtimes.push_back(std::move(runtime));
    }
    if (setupFailed) {
        cleanup();
        return 1;
    }

    const auto findRuntime = [&](core::WindowId id) -> WindowRuntime* {
        for (auto& runtime : runtimes) {
            if (runtime.active && runtime.id == id) {
                return &runtime;
            }
        }
        // Preserve the old single-window convenience where tests queue events
        // against a host-owned window created before runApp. Do this only when
        // there is no active multi-window ambiguity.
        WindowRuntime* only = nullptr;
        for (auto& runtime : runtimes) {
            if (!runtime.active) {
                continue;
            }
            if (only != nullptr) {
                return nullptr;
            }
            only = &runtime;
        }
        return only;
    };

    const auto requestPendingFrame = [](WindowRuntime& runtime) {
        AppShell& shell = *runtime.app.shell;
        if (shell.hasPendingFrame() && !runtime.scheduler->hasPendingReasons()) {
            runtime.scheduler->requestFrame(render::FrameReason::Explicit,
                                            runtime.id);
        }
    };

    const auto mapCursor = [](core::PointerCursor cursor) {
        switch (cursor) {
            case core::PointerCursor::ResizeEW:
                return platform::SystemCursor::ResizeEW;
            case core::PointerCursor::ResizeNS:
                return platform::SystemCursor::ResizeNS;
            case core::PointerCursor::PointingHand:
                return platform::SystemCursor::PointingHand;
            case core::PointerCursor::Arrow:
                break;
        }
        return platform::SystemCursor::Arrow;
    };

    const auto syncPointerCursor = [&](WindowRuntime& runtime) {
        AppShell& shell = *runtime.app.shell;
        const core::PointerCursor desired = shell.pointerCursor();
        if (desired == runtime.lastCursor) {
            return;
        }
        runtime.lastCursor = desired;
        host.setCursor(runtime.id, mapCursor(desired));
    };

    const auto syncTextInput = [&](WindowRuntime& runtime) {
        auto* session = host.textInputSession(runtime.id);
        if (session == nullptr) {
            return;
        }
        AppShell& shell = *runtime.app.shell;
        const bool want = runtime.focused &&
                          runtime.scheduler->isWindowVisible() &&
                          shell.wantsTextInput();
        if (want && !session->active()) {
            session->start();
        } else if (!want && session->active()) {
            session->stop();
        }
        if (want) {
            platform::TextInputEditingState editing;
            editing.caretRect = shell.focusedTextRect();
            session->setEditingState(editing);
        }
    };

    const auto notify = [&](WindowRuntime& runtime, const HostEvent& event,
                            render::FrameReason reason) {
        if (runtime.app.options.onEvent) {
            runtime.app.options.onEvent(*runtime.app.shell, event);
            runtime.scheduler->requestFrame(reason, runtime.id);
        }
    };

    bool running = true;
    while (running) {
        bool eventsPumped = false;
        for (auto& runtime : runtimes) {
            if (runtime.active && runtime.nativeA11y != nullptr) {
                runtime.nativeA11y->pump();
                // AT 派发（焦点/值/激活）在 UI 线程同步改变应用状态并标
                // 脏，但不产生宿主事件——这里合并请求一帧，语义推送随帧
                // 末完成（否则空闲应用永不上报 AT 驱动的状态变化）。
                if (runtime.app.shell->hasPendingFrame()) {
                    runtime.scheduler->requestFrame(
                        render::FrameReason::Input, runtime.id);
                }
            }
        }
        // M14-C：异步字体 pump——后台工厂完成（成功/失败）后在 UI 线程
        // 交付一次：成功热替换 AppShell 字体（占位→正式度量全帧重绘）
        // 并请求资源帧；失败保持占位，诊断经 options.diagnostics 输出。
        // 未完成时零成本跳过；交付后 loader 即可析构（join 已完成）。
        for (auto& runtime : runtimes) {
            if (!runtime.active || runtime.fontLoader == nullptr ||
                !runtime.fontLoader->finished()) {
                continue;
            }
            std::string fontDiagnostics;
            auto fonts = runtime.fontLoader->pump(&fontDiagnostics);
            runtime.fontLoader.reset();
            if (fonts != nullptr) {
                runtime.app.shell->setFontManager(std::move(fonts));
                runtime.scheduler->requestFrame(
                    render::FrameReason::Resource, runtime.id);
            }
            if (runtime.app.options.diagnostics) {
                std::printf("[diag] fonts: %s\n", fontDiagnostics.c_str());
            }
        }
        // M14-C：资源完成 pump（每管理器一次；多窗口共享管理器时按完成
        // 事件携带的 window 路由）。完成 → 标脏 + 请求资源帧：应用 build
        // 从 manager 取 imageId/占位，占位→就绪的翻页由此自动发生，应
        // 用层无需拼接不可观测的轮询状态。
        for (auto& runtime : runtimes) {
            if (!runtime.active || !runtime.app.options.resourceManager) {
                continue;
            }
            const auto completions =
                runtime.app.options.resourceManager->pumpCompletions();
            for (const auto& completion : completions) {
                WindowRuntime* target = &runtime;
                if (completion.window.valid()) {
                    const auto match = std::find_if(
                        runtimes.begin(), runtimes.end(),
                        [&completion](const WindowRuntime& item) {
                            return item.active && item.id == completion.window;
                        });
                    if (match != runtimes.end()) {
                        target = &*match;
                    }
                }
                target->app.shell->markDirty();
                target->scheduler->requestFrame(render::FrameReason::Resource,
                                                target->id);
            }
            break;  // pump 消费的是管理器全局队列，一次即可。
        }
        HostEvent event;
        while (host.pollEvent(event)) {
            eventsPumped = true;
            if (event.type == HostEventType::Quit) {
                running = false;
                break;
            }
            if (event.type == HostEventType::SystemThemeChanged ||
                event.type == HostEventType::SystemAccessibilityChanged ||
                event.type == HostEventType::ClipboardChanged ||
                (event.type == HostEventType::LifecycleChanged &&
                 !event.window.valid())) {
                for (auto& runtime : runtimes) {
                    if (!runtime.active) {
                        continue;
                    }
                    if (event.type == HostEventType::SystemAccessibilityChanged) {
                        applyAccessibilityPreferences(runtime);
                    }
                    if (event.type == HostEventType::LifecycleChanged) {
                        if (event.lifecycle == core::AppLifecycle::Active) {
                            syncWindowVisibility(runtime);
                        } else {
                            runtime.scheduler->setWindowVisible(false);
                            stopTextInput(runtime);
                        }
                    }
                    notify(runtime, event, render::FrameReason::Input);
                }
                continue;
            }
            WindowRuntime* runtime = findRuntime(event.window);
            if (runtime == nullptr) {
                continue;
            }
            AppShell& shell = *runtime->app.shell;
            auto& scheduler = *runtime->scheduler;
            switch (event.type) {
                case HostEventType::WindowCloseRequested:
                    if (shell.requestClose()) {
                        scheduler.requestFrame(render::FrameReason::Input,
                                               runtime->id);
                    } else {
                        scheduler.setWindowVisible(false);
                        stopTextInput(*runtime);
                        runtime->active = false;
                        const bool anyActive = std::any_of(
                            runtimes.begin(), runtimes.end(),
                            [](const WindowRuntime& item) { return item.active; });
                        if (!anyActive) {
                            running = false;
                        }
                    }
                    break;
                case HostEventType::Resize:
                case HostEventType::DpiChanged:
                    applyMetrics(*runtime);
                    scheduler.requestFrame(render::FrameReason::Resize,
                                           runtime->id);
                    break;
                case HostEventType::WindowMinimized:
                    scheduler.setWindowVisible(false);
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                case HostEventType::WindowRestored:
                    applyMetrics(*runtime);
                    syncWindowVisibility(*runtime);
                    scheduler.requestFrame(render::FrameReason::Resize,
                                           runtime->id);
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                case HostEventType::WindowMaximized:
                    notify(*runtime, event, render::FrameReason::Input);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                // M16：全屏切换（metrics 查询即时反映；尺寸变化另经
                // Resize 事件到达）。
                case HostEventType::WindowFullscreenEntered:
                case HostEventType::WindowFullscreenExited:
                    applyMetrics(*runtime);
                    notify(*runtime, event, render::FrameReason::Input);
                    scheduler.requestFrame(render::FrameReason::Resize,
                                           runtime->id);
                    break;
                case HostEventType::WindowFocusGained:
                    runtime->focused = true;
                    // M13：窗口激活转发（provider 发 window:activate；
                    // 屏幕阅读器以窗口激活切换“当前应用”）。
                    shell.noteWindowActive(true);
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                case HostEventType::WindowFocusLost:
                    runtime->focused = false;
                    shell.noteWindowActive(false);
                    stopTextInput(*runtime);
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                case HostEventType::KeyUp:
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                case HostEventType::SurfaceDetached:
                    scheduler.setWindowVisible(false);
                    shell.cancelComposition();
                    if (runtime->setup.renderer != nullptr) {
                        render::RenderSurfaceDesc detached;
                        detached.window = runtime->id;
                        runtime->setup.renderer->resetSurface(detached);
                    }
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                case HostEventType::SurfaceReattached:
                    applyMetrics(*runtime);
                    // M14-C：surface 分离期间渲染目标可能随窗口一起销毁，
                    // 与 renderer 替换路径同语义重排队 Ready 资源（幂等，
                    // ImageId 不变；上传命令随下一帧前置输出）。
                    if (runtime->app.options.resourceManager) {
                        runtime->app.options.resourceManager
                            ->handleDeviceRebuilt();
                    }
                    syncWindowVisibility(*runtime);
                    scheduler.requestFrame(render::FrameReason::Resize,
                                           runtime->id);
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                case HostEventType::LifecycleChanged:
                    if (event.lifecycle == core::AppLifecycle::Active) {
                        syncWindowVisibility(*runtime);
                    } else {
                        scheduler.setWindowVisible(false);
                        stopTextInput(*runtime);
                    }
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                case HostEventType::PointerDown:
                    shell.pointerDown(event.position, event.modifiers,
                                      event.button, event.device,
                                      event.pointerId);
                    if (event.device != core::PointerDevice::Touch) {
                        syncPointerCursor(*runtime);
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                case HostEventType::PointerMove:
                    shell.pointerMove(event.position, event.pointerId);
                    if (event.device != core::PointerDevice::Touch) {
                        syncPointerCursor(*runtime);
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                case HostEventType::PointerUp:
                    shell.pointerUp(event.position, event.button,
                                    event.pointerId);
                    if (event.device != core::PointerDevice::Touch) {
                        syncPointerCursor(*runtime);
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                case HostEventType::PointerCancel:
                    shell.pointerCancel();
                    if (event.device != core::PointerDevice::Touch) {
                        syncPointerCursor(*runtime);
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                case HostEventType::Wheel:
                    (void)shell.wheel(event.position, event.scrollDelta,
                                      event.modifiers);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                case HostEventType::TextInput:
                    shell.textInput(event.text);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                case HostEventType::TextEditing:
                    shell.textEditing(event.text);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                case HostEventType::KeyDown:
                    shell.keyDown(event.keyCode, event.modifiers,
                                  event.keyChar);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           runtime->id);
                    break;
                case HostEventType::FileDialogCompleted:
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                // M16：托盘菜单激活/全局快捷键转发应用（onEvent 消费）。
                case HostEventType::TrayActivated:
                case HostEventType::GlobalHotkey:
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                // M15：OS 拖入会话转发应用（onEvent 消费落点/负载；应用
                // 变更状态后自行请求帧）。框架内重排拖拽不经此路径。
                case HostEventType::DragEnter:
                case HostEventType::DragMove:
                case HostEventType::DragDrop:
                case HostEventType::DragLeave:
                    notify(*runtime, event, render::FrameReason::Input);
                    break;
                default:
                    break;
            }
        }

        if (!eventsPumped) {
            for (auto& runtime : runtimes) {
                if (runtime.active) {
                    ++runtime.idleTurns;
                }
            }
        }

        const std::uint64_t nowMs = clock.nowMs();
        for (auto& runtime : runtimes) {
            if (!runtime.active) {
                continue;
            }
            AppShell& shell = *runtime.app.shell;
            auto& scheduler = *runtime.scheduler;
            if (runtime.app.options.poll &&
                nowMs - runtime.lastPollMs >= runtime.app.options.idleWaitMs) {
                runtime.lastPollMs = nowMs;
                if (runtime.app.options.poll(shell, nowMs)) {
                    scheduler.requestFrame(render::FrameReason::HotReload,
                                           runtime.id);
                }
            }
            shell.tick(nowMs);
            syncTextInput(runtime);
            scheduler.setAnimationsActive(shell.animationsActive());
            scheduler.setAnimationDeadline(shell.animationWakeMs());
            if (runtime.app.options.frameDebugOverlay) {
                // R6：HUD 读数随每调度帧刷新（显式开启才有的额外帧）。
                shell.markDirty();
            }
            requestPendingFrame(runtime);
        }

        for (auto& runtime : runtimes) {
            if (!runtime.active || !runtime.scheduler->shouldSubmitFrame()) {
                continue;
            }
            AppShell& shell = *runtime.app.shell;
            auto& scheduler = *runtime.scheduler;
            const auto& options = runtime.app.options;
            shell.paintFrame(options.maxFrames != 0);
            syncPointerCursor(runtime);
            scheduler.setAnimationsActive(shell.animationsActive());
            if (runtime.setup.failed && runtime.setup.failed()) {
                auto replacement = options.onRendererFailure
                                       ? options.onRendererFailure(host,
                                                                   runtime.id)
                                       : std::nullopt;
                if (!replacement.has_value() ||
                    !host.windowMetrics(runtime.id).has_value()) {
                    exitCode = 1;
                    running = false;
                    break;
                }
                runtime.setup = std::move(*replacement);
                runtime.presentWindow = host.platformWindow(runtime.id);
                shell.cancelComposition();
                shell.setRenderer(runtime.setup.renderer);
                // M14-C：renderer/GPU 设备重建后重排队全部 Ready 资源
                //（ImageId 不变；上传命令随下一帧前置输出）。
                if (options.resourceManager) {
                    options.resourceManager->handleDeviceRebuilt();
                }
                applyMetrics(runtime);
                const auto metrics = host.windowMetrics(runtime.id);
                scheduler.setWindowVisible(
                    metrics.has_value() ? metrics->visible : true);
                shell.paintFrame(true);
                if (options.diagnostics) {
                    printStartupDiagnostics(
                        shell.capabilities(),
                        host.windowMetrics(runtime.id).value_or(WindowMetrics{}));
                }
            }
            bool presentOk = true;
            if (runtime.setup.present) {
                presentOk = runtime.setup.present();
            } else if (runtime.setup.renderer == nullptr &&
                       runtime.presentWindow != nullptr) {
                presentOk = runtime.presentWindow->present(shell.pixels()) ==
                            platform::PresentResult::Ok;
            }
            if (!presentOk) {
                std::fprintf(stderr, "CPU present failed\n");
                exitCode = 1;
                running = false;
                break;
            }
            syncTextInput(runtime);
            scheduler.markFrameSubmitted();
            requestPendingFrame(runtime);
            if (options.maxFrames != 0) {
                if (scheduler.submittedFrames() >= options.maxFrames) {
                    runtime.active = false;
                    const bool anyActive = std::any_of(
                        runtimes.begin(), runtimes.end(),
                        [](const WindowRuntime& item) { return item.active; });
                    if (!anyActive) {
                        running = false;
                    }
                } else {
                    scheduler.requestFrame(render::FrameReason::Explicit,
                                           runtime.id);
                }
            }
            if (options.diagnostics &&
                nowMs - runtime.lastDiagPrintMs >= 2000) {
                runtime.lastDiagPrintMs = nowMs;
                const auto* resources =
                    runtime.app.options.resourceManager
                        ? &runtime.app.options.resourceManager->diagnostics()
                        : nullptr;
                printFrameDiagnostics(scheduler.submittedFrames(),
                                      shell.partialRepaintCount(), shell.stats(),
                                      runtime.idleTurns, runtime.id, resources);
            }
        }

        if (!running) {
            break;
        }
        std::optional<std::uint32_t> waitMs;
        for (const auto& runtime : runtimes) {
            if (!runtime.active) {
                continue;
            }
            const std::uint32_t candidate =
                runtime.scheduler->msUntilNextFrame().value_or(
                    runtime.app.options.idleWaitMs);
            if (!waitMs.has_value() || candidate < *waitMs) {
                waitMs = candidate;
            }
        }
        if (waitMs.has_value() && *waitMs > 0) {
            host.waitForEvents(*waitMs);
        }
    }

    for (const auto& runtime : runtimes) {
        if (runtime.app.options.diagnostics) {
            const auto* resources =
                runtime.app.options.resourceManager
                    ? &runtime.app.options.resourceManager->diagnostics()
                    : nullptr;
            printFrameDiagnostics(runtime.scheduler->submittedFrames(),
                                  runtime.app.shell->partialRepaintCount(),
                                  runtime.app.shell->stats(), runtime.idleTurns,
                                  runtime.id, resources);
        }
    }
    cleanup();
    // G-2：干净退出清脏标记 + 冲刷日志 + 恢复处理器（崩溃路径不经此）。
    if (runtimeDiagnostics.has_value()) {
        runtimeDiagnostics->cleanShutdown();
    }
    return exitCode;
}

int runApp(AppShell& shell, platform::ApplicationHost& host,
           RunOptions options) {
    return runApp(std::vector<AppWindow>{{&shell, std::move(options)}}, host);
}

}  // namespace lumen::app
