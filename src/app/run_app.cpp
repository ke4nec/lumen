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

    render::RealtimeClock clock;
    render::FrameScheduler::Config schedulerConfig;
    schedulerConfig.targetFps = 60;
    schedulerConfig.resizeDebounceMs = 16;
    schedulerConfig.pauseWhenHidden = true;
    render::FrameScheduler scheduler{schedulerConfig, &clock};
    scheduler.requestFrame(render::FrameReason::Explicit, *windowId);

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
                    break;
                case HostEventType::PointerDown:
                    shell.pointerDown(event.position);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerMove:
                    shell.pointerMove(event.position);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerUp:
                    shell.pointerUp(event.position);
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerCancel:
                    shell.pointerCancel();
                    scheduler.requestFrame(render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::Wheel:
                    shell.wheel(event.position, event.scrollDelta);
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

        if (scheduler.shouldSubmitFrame()) {
            // maxFrames 测量/冒烟模式强制全量重绘：damage 统计归零但像素
            // 与局部路径一致（局部/全量像素等价由测试断言），保证帧间可比。
            (void)shell.renderFrame(options.maxFrames != 0);
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
                (void)shell.renderFrame(true);
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
