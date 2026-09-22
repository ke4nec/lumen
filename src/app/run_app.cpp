// M2（自用路线图）：runApp —— UI 线程主循环。
//
// 职责：宿主初始化/窗口创建 → 渲染器与字体装配 → 事件泵（归一化
// HostEvent → AppShell 分发）→ FrameScheduler 决策 → renderFrame → 呈现
// → IME 会话同步（候选框锚点）→ 渲染器失效回退 → 空闲等待。
// 平台差异全部留在 ApplicationHost 实现内；公共头无 SDL/Skia 类型。

#include "lumen/app/app_shell.h"

#include <algorithm>
#include <cstdio>

#include "lumen/core/windowing.h"
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
        "[diag] backend=%s gpu=%s partial=%s dpi=%.2f pixels=%.0fx%.0f\n",
        caps.backendName, caps.gpu ? "yes" : "no",
        caps.partialSubmit ? "yes" : "no", metrics.deviceScale,
        metrics.drawableSize.width, metrics.drawableSize.height);
}

void printFrameDiagnostics(std::uint64_t frames, std::uint32_t partial,
                           const render::RenderStats& stats,
                           std::uint64_t skippedIdleTurns) {
    std::printf(
        "[diag] frames=%llu partial=%u idleTurns=%llu cmds=%llu "
        "culled=%llu submitMs=%.2f gpuWaitMs=%.2f buildMs=%.2f "
        "uploads=%llu%s%s\n",
        static_cast<unsigned long long>(frames), partial,
        static_cast<unsigned long long>(skippedIdleTurns),
        static_cast<unsigned long long>(stats.commandCount),
        static_cast<unsigned long long>(stats.culledCommands), stats.submitMs,
        stats.gpuWaitMs, stats.cpuBuildMs,
        static_cast<unsigned long long>(stats.uploads),
        stats.fullFrameFallback ? " fallback=yes" : "",
        stats.fallbackReason.empty()
            ? ""
            : (" reason=" + stats.fallbackReason).c_str());
}

}  // namespace

int runApp(AppShell& shell, platform::ApplicationHost& host,
           RunOptions options) {
    if (!host.initialize()) {
        return 1;
    }

    auto windowId = host.createWindow(options.windowDesc);
    if (!windowId.has_value()) {
        return 1;
    }

    // --- 渲染器/字体装配（依赖注入；全部可空） ---
    RendererSetup setup;
    if (options.rendererFactory) {
        setup = options.rendererFactory(host, *windowId);
    }
    // 工厂可能重建窗口；重建失败不能被当作 headless CPU 成功。
    if (!host.windowMetrics(*windowId).has_value()) {
        return 1;
    }
    shell.setRenderer(setup.renderer);
    if (options.fontFactory) {
        if (auto fonts = options.fontFactory()) {
            shell.setFontManager(std::move(fonts));
        }
    }
    // 剪贴板接线（Ctrl+C/V 经交互层消费；宿主不可用时保持未注入，
    // 与 headless Fake 一致的行文：空指针 = 无剪贴板）。
    if (auto* clipboard = host.clipboard()) {
        shell.controller().setClipboard(clipboard);
    }

    // 窗口图标（RunOptions.windowIcon）：渲染器装配后设置——工厂可能
    // 重建窗口（GPU 回退），图标必须落在最终窗口上。先查能力位再调
    // provider（宿主不支持时不做无谓光栅化）；失败只诊断降级（exe/
    // 桌面图标资源由打包层提供），不阻塞启动。
    if (options.windowIcon && host.capabilities().windowIcon) {
        const platform::WindowIcon icon = options.windowIcon();
        if (icon.width > 0) {
            const auto result = host.setWindowIcon(*windowId, icon);
            if (options.diagnostics) {
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

    // 自定义标题栏（lumen-titlebar-design §4.1）：注册拖拽区谓词——命中
    // 判定在 AppShell（事件树/交互排除），宿主 hit-test 另判 resize 边。
    if (options.windowDesc.customTitleBar) {
        host.setWindowDragRegion(*windowId, [&shell](core::Offset position) {
            return shell.isWindowDragPoint(position);
        });
    }

    // 透明窗口（WindowDesc.transparent）：清屏色转全透明（CPU 路径；
    // 应用内容自绘圆角，圆角外像素交桌面合成）。外部渲染器的透明
    // clear 由其装配负责。
    if (options.windowDesc.transparent) {
        shell.setClearColor(core::Color::transparent());
    }

    // 指标同步：视口/DPI 跟随宿主（后续 Resize/DpiChanged 事件刷新）。
    const auto applyMetrics = [&]() {
        const auto metrics = host.windowMetrics(*windowId);
        if (!metrics.has_value() || metrics->logicalSize.width <= 0.0F) {
            return;
        }
        shell.setDeviceScale(metrics->deviceScale);
        shell.setView(metrics->logicalSize);
        if (setup.syncDeviceScale) {
            setup.syncDeviceScale(metrics->deviceScale);
        }
    };
    applyMetrics();

    // --- M13：原生无障碍桥（LUMEN_ENABLE_ACCESSIBILITY_BRIDGE 编入时） ---
    // AT 的语义 action 经 dispatch 回灌 AppShell::performAccessibilityAction
    //（与键盘/指针同路径）；桥为局部变量——函数返回时先于宿主窗口销毁
    // 析构（断开 WM_GETOBJECT 应答与 UIA provider）。
    std::unique_ptr<accessibility::AccessibilityBridge> nativeA11y;
    if (options.nativeAccessibility) {
        accessibility::PlatformAccessibilityHost a11yHost;
        a11yHost.nativeWindow = host.nativeWindowHandle(*windowId);
        a11yHost.deviceScale =
            host.windowMetrics(*windowId).value_or(WindowMetrics{}).deviceScale;
        a11yHost.applicationName = options.windowDesc.title;
        a11yHost.dispatch = [&shell](const std::string& nodeId,
                                     std::uint32_t action,
                                     const std::string& value,
                                     float scrollDeltaY) {
            return shell.performAccessibilityAction(nodeId, action, value,
                                                    scrollDeltaY);
        };
        std::string a11yDiagnostics;
        nativeA11y = accessibility::createPlatformAccessibilityBridge(
            a11yHost, &a11yDiagnostics);
        if (nativeA11y != nullptr) {
            shell.setAccessibilityBridge(nativeA11y.get());
            host.noteAccessibilityBridgeActive(true);
            if (options.diagnostics) {
                std::printf("[diag] a11y=%s\n",
                            nativeA11y->bridgeName().c_str());
            }
        } else if (options.diagnostics && !a11yDiagnostics.empty()) {
            std::printf("[diag] a11y=off (%s)\n", a11yDiagnostics.c_str());
        }
    }

    render::RealtimeClock clock;
    render::FrameScheduler::Config schedulerConfig;
    schedulerConfig.targetFps = 60;
    schedulerConfig.resizeDebounceMs = 16;
    schedulerConfig.pauseWhenHidden = true;
    render::FrameScheduler scheduler{schedulerConfig, &clock};
    scheduler.requestFrame(render::FrameReason::Explicit, *windowId);
    const auto requestPendingFrame = [&]() {
        // 一次性 tick/业务回调和 onRebuilt 的后续重建也必须提交；动画
        // 是否继续与内容是否待绘制是两个独立状态。已有原因继续沿用，
        // 避免 resize 引起的 dirty 绕过原有防抖。
        if (shell.hasPendingFrame() && !scheduler.hasPendingReasons()) {
            scheduler.requestFrame(render::FrameReason::Explicit, *windowId);
        }
    };

    if (options.diagnostics) {
        printStartupDiagnostics(
            shell.capabilities(),
            host.windowMetrics(*windowId).value_or(WindowMetrics{}));
    }

    // 默认呈现：宿主窗口呈现内部 CPU 帧缓冲；Fake host（无真实窗口）跳过。
    platform::PlatformWindow* presentWindow =
        host.platformWindow(*windowId);
    const auto presentFrame = [&]() -> bool {
        if (setup.present) {
            return setup.present();
        }
        if (setup.renderer != nullptr) {
            return true;  // 后端自行交换（GPU）。
        }
        if (presentWindow == nullptr) {
            return true;  // Fake host：headless 冒烟无呈现目标。
        }
        return presentWindow->present(shell.pixels()) ==
               platform::PresentResult::Ok;
    };

    // IME 会话同步：焦点进入/离开编辑字段时启停，编辑期间同步候选框
    // 锚点（caretRect；Linux IBus/Fcitx/Wayland 依赖）。
    const auto syncTextInput = [&]() {
        auto* session = host.textInputSession(*windowId);
        if (session == nullptr) {
            return;
        }
        const bool want = shell.wantsTextInput();
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

    std::uint64_t idleTurns = 0;
    std::uint64_t lastPollMs = clock.nowMs();
    std::uint64_t lastDiagPrintMs = clock.nowMs();
    bool running = true;

    // 悬停光标同步（splitter-design §7）：交互层从命中链推导期望形状
    //（core 语义，无平台类型），此处映射 SystemCursor 落到宿主；仅在变
    // 化时调用（SDL 为进程级创建）。
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
    core::PointerCursor lastCursor = shell.pointerCursor();
    const auto syncPointerCursor = [&]() {
        const core::PointerCursor desired = shell.pointerCursor();
        if (desired == lastCursor) {
            return;
        }
        lastCursor = desired;
        host.setCursor(*windowId, mapCursor(desired));
    };
    while (running) {
        bool eventsPumped = false;
        HostEvent event;
        while (host.pollEvent(event)) {
            eventsPumped = true;
            switch (event.type) {
                case HostEventType::Quit:
                    running = false;
                    break;
                case HostEventType::WindowCloseRequested:
                    // 关闭请求策略：应用消费（modal/路由返回）则继续，
                    // 否则退出（plan §3.4 统一关闭规则）。
                    if (!shell.requestClose()) {
                        running = false;
                    } else {
                        scheduler.requestFrame(render::FrameReason::Input,
                                               *windowId);
                    }
                    break;
                case HostEventType::Resize:
                case HostEventType::DpiChanged:
                    // 约束/DPI 跟随新指标；DPI 变化在下一帧 surface
                    // 重建前处理（plan §3.2）。
                    applyMetrics();
                    scheduler.requestFrame(render::FrameReason::Resize,
                                           event.window);
                    break;
                case HostEventType::WindowMinimized:
                    scheduler.setWindowVisible(false);
                    break;
                case HostEventType::WindowRestored:
                    scheduler.setWindowVisible(true);
                    scheduler.requestFrame(render::FrameReason::Resize,
                                           event.window);
                    // 自定义标题栏：还原事件转发应用（最大化态复位为
                    // false；最小化恢复同路径）。
                    if (options.onEvent) {
                        options.onEvent(shell, event);
                    }
                    break;
                case HostEventType::WindowMaximized:
                    // 自定义标题栏：最大化完成（按钮图标/布局自适应经
                    // onEvent 消费；无钩子则仅请求重绘）。
                    if (options.onEvent) {
                        options.onEvent(shell, event);
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerDown:
                    shell.pointerDown(event.position, event.modifiers,
                                      event.button);
                    if (event.device != core::PointerDevice::Touch) {
                        syncPointerCursor();
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerMove:
                    shell.pointerMove(event.position);
                    if (event.device != core::PointerDevice::Touch) {
                        syncPointerCursor();
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerUp:
                    shell.pointerUp(event.position, event.button);
                    if (event.device != core::PointerDevice::Touch) {
                        syncPointerCursor();
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerCancel:
                    shell.pointerCancel();
                    if (event.device != core::PointerDevice::Touch) {
                        syncPointerCursor();
                    }
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::Wheel:
                    // 消费状态可用于诊断/冒泡（M5 收口）；runApp 自身不
                    // 区分（输入帧照常请求）。modifiers 供 Shift+纵轮的
                    // 水平视口投影（lumen-scroll-design §4）。
                    (void)shell.wheel(event.position, event.scrollDelta,
                                      event.modifiers);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::TextInput:
                    shell.textInput(event.text);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::TextEditing:
                    shell.textEditing(event.text);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::KeyDown:
                    shell.keyDown(event.keyCode, event.modifiers,
                                  event.keyChar);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::FileDialogCompleted:
                    // M4：平台服务异步完成（文件选择等）——转给应用钩子
                    // 处理；无钩子则忽略。
                    if (options.onEvent) {
                        options.onEvent(shell, event);
                        scheduler.requestFrame(render::FrameReason::Input,
                                               event.window);
                    }
                    break;
                case HostEventType::SystemThemeChanged:
                    // M12：系统主题切换——转给应用钩子重派生主题
                    //（adaptPlatformTheme / setTheme）；无钩子则忽略。
                    if (options.onEvent) {
                        options.onEvent(shell, event);
                        scheduler.requestFrame(render::FrameReason::Input,
                                               *windowId);
                    }
                    break;
                default:
                    break;
            }
        }
        if (!eventsPumped) {
            idleTurns += 1;
        }

        // 扩展轮询（热重载等）：空闲粒度节流。
        const std::uint64_t nowMs = clock.nowMs();
        if (options.poll && nowMs - lastPollMs >= options.idleWaitMs) {
            lastPollMs = nowMs;
            if (options.poll(shell, nowMs)) {
                scheduler.requestFrame(render::FrameReason::HotReload,
                                       *windowId);
            }
        }

        // 时间驱动状态（caret 闪烁/转场/应用动画 onAnimate）与 IME 会话
        // 状态。
        shell.tick(nowMs);
        syncTextInput();

        // M10：caret 闪烁、转场与状态过渡共用动画帧调度（FrameReason::
        // Animation deadline 循环；reduceAnimation 时 MotionTokens 归零，
        // 无连续动画源）。
        scheduler.setAnimationsActive(shell.animationsActive());
        // M11 review：离散定时唤醒（tooltip 延迟到期）——空闲等待到
        // 时刻，不占用连续动画帧。
        scheduler.setAnimationDeadline(shell.animationWakeMs());
        requestPendingFrame();

        if (scheduler.shouldSubmitFrame()) {
            // maxFrames 测量/冒烟模式强制全量重绘：damage 统计归零但像素
            // 与局部路径一致（局部/全量像素等价由测试断言），保证帧间可比。
            shell.paintFrame(options.maxFrames != 0);
            syncPointerCursor();
            // Rebuild may start a checked/hover blend on this very frame.
            scheduler.setAnimationsActive(shell.animationsActive());
            // 渲染器失效（GPU 上下文丢失等）：回退策略替换渲染器（可
            // 重建窗口）；诊断文本由回退钩子（应用层）负责——后端原因
            // 属于装配层知识。失败则退出。
            if (setup.failed && setup.failed()) {
                auto replacement = options.onRendererFailure
                                       ? options.onRendererFailure(
                                           host, *windowId)
                                       : std::nullopt;
                if (!replacement.has_value() ||
                    !host.windowMetrics(*windowId).has_value()) {
                    return 1;
                }
                setup = std::move(*replacement);
                presentWindow = host.platformWindow(*windowId);
                shell.cancelComposition();
                shell.setRenderer(setup.renderer);
                applyMetrics();
                const auto metrics = host.windowMetrics(*windowId);
                scheduler.setWindowVisible(
                    metrics.has_value() ? metrics->visible : true);
                // 失效提交没有产生帧：先经回退后端补一帧再继续计数。
                shell.paintFrame(true);
                if (options.diagnostics) {
                    printStartupDiagnostics(
                        shell.capabilities(),
                        host.windowMetrics(*windowId).value_or(
                            WindowMetrics{}));
                }
            }
            if (!presentFrame()) {
                std::fprintf(stderr, "CPU present failed\n");
                return 1;
            }
            // renderFrame 可重建字段几何或由 onRebuilt 移动焦点；输入法
            // 必须看到刚呈现的树，回退新建窗口也在这里重启会话。
            syncTextInput();
            scheduler.markFrameSubmitted();
            // 绘制中的 onRebuilt/呈现回调可能再次标脏：先挂下一帧再等待。
            requestPendingFrame();
            if (options.maxFrames != 0) {
                if (scheduler.submittedFrames() >= options.maxFrames) {
                    running = false;
                } else {
                    scheduler.requestFrame(render::FrameReason::Explicit,
                                           *windowId);
                }
            }
            if (options.diagnostics &&
                clock.nowMs() - lastDiagPrintMs >= 2000) {
                lastDiagPrintMs = clock.nowMs();
                printFrameDiagnostics(scheduler.submittedFrames(),
                                      shell.partialRepaintCount(),
                                      shell.stats(), idleTurns);
            }
        }

        if (!running) {
            break;
        }
        // 空闲等待：有 deadline 等到 deadline，否则最多等一个轮询周期；
        // 宿主事件唤醒（SDL_WaitEventTimeout 语义由宿主事件泵承担）。
        const auto waitMs = scheduler.msUntilNextFrame();
        const std::uint32_t capped = std::min<std::uint32_t>(
            waitMs.value_or(options.idleWaitMs), options.idleWaitMs);
        if (capped > 0) {
            host.waitForEvents(capped);
        }
    }
    if (options.diagnostics) {
        printFrameDiagnostics(scheduler.submittedFrames(),
                              shell.partialRepaintCount(), shell.stats(),
                              idleTurns);
    }
    return 0;
}

}  // namespace lumen::app
