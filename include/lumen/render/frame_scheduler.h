#pragma once

#include <cstdint>
#include <optional>

#include "lumen/core/windowing.h"

namespace lumen::render {

// v0.2 阶段7D (plan §3.2): 帧调度器，由应用主循环拥有。
//
// 每个 UI turn：
//   1. 平台事件入队并合并 invalidate 原因（requestFrame）。
//   2. UI 线程完成状态回调、build/reconcile、layout 和命令录制。
//   3. Scheduler 按 VSync/目标帧率决定是否提交；无 dirty、无动画、无
//      资源完成时不提交（shouldSubmitFrame 返回 false）。
//   4. markFrameSubmitted 清除已消费的原因；turn 中途的新请求只能进入
//      下一帧，不能重入当前帧。
//
// 输入优先（Input 不被 resize 防抖拖延）、动画 deadline、resize 防抖、
// 最小化暂停和显式 requestFrame 都在这里实现；时间源可注入以便
// headless 测试确定性运行。

enum class FrameReason : std::uint8_t {
    Input = 0,
    Animation,
    Resize,
    Resource,
    HotReload,
    Explicit,
    Count,
};

// 可注入时间源（headless 测试用 ManualClock 之类驱动）。
class FrameClock {
  public:
    virtual ~FrameClock() = default;
    [[nodiscard]] virtual std::uint64_t nowMs() const = 0;
};

// steady_clock 实现；生产路径默认。
class RealtimeClock final : public FrameClock {
  public:
    [[nodiscard]] std::uint64_t nowMs() const override;
};

class FrameScheduler {
  public:
    struct Config {
        // 目标帧率上限；0 表示不限。VSync 关闭时仍按此值节流。
        std::uint32_t targetFps{60};
        // resize 静默期：最后一次 Resize 请求后等待该时长才提交，连续
        // resize 不会逐事件提交。
        std::uint32_t resizeDebounceMs{16};
        // 窗口最小化/隐藏时暂停提交（原因保留，恢复后补交）。
        bool pauseWhenHidden{true};
    };

    // clock 为空时使用 RealtimeClock。
    FrameScheduler();
    explicit FrameScheduler(Config config, FrameClock* clock = nullptr);

    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    // 平台事件/资源完成/显式请求 → 合并进 pending。同一 turn 内多次
    // 请求只合并为一次提交。window 随请求关联（阶段8A 诊断：哪个窗口
    // 触发了本次帧请求；lastRequestedWindow 可查）。
    void requestFrame(FrameReason reason, core::WindowId window = {});

    // 持续动画是否活跃（tween/blink 等）。活跃时按 deadline 循环提交。
    void setAnimationsActive(bool active);
    // 窗口可见性（最小化/还原）。
    void setWindowVisible(bool visible);
    // VSync 开关：开 = 按 targetFps 节流；关 = 有原因立即提交。
    void setVSyncEnabled(bool vsync);
    // 减少动画（可访问性设置，阶段8C）：动画不再驱动连续帧提交。
    void setReduceAnimation(bool reduceAnimation);

    struct FrameDecision {
        // 本 turn 是否应提交帧。
        bool submit{false};
        // 距下一次需要提交的毫秒数；0 = 立即。nullopt = 可无限等待
        // （空闲且无动画）。
        std::optional<std::uint32_t> waitMs{};
        // 触发本决策的原因位集（调试/诊断）。
        std::uint32_t reasons{0};
        // 因隐藏而暂停。
        bool pausedByHidden{false};
        // 因 resize 防抖而延迟。
        bool deferredByResizeDebounce{false};
    };

    // 纯计算决策，无副作用。
    [[nodiscard]] FrameDecision evaluateFrame() const;

    // 决策 + 消费：返回 true 时 pending 原因移入当前帧（active），应用
    // 提交后必须调用 markFrameSubmitted()。turn 中途的 requestFrame 进入
    // 下一帧的 pending，不重入当前帧。
    [[nodiscard]] bool shouldSubmitFrame();

    // 提交完成：清除已消费原因并推进 lastSubmit 时间戳。
    void markFrameSubmitted();

    // 事件等待超时建议（SDL_WaitEventTimeout 等）。
    [[nodiscard]] std::optional<std::uint32_t> msUntilNextFrame() const;

    [[nodiscard]] bool hasPendingReasons() const { return pending_ != 0; }
    [[nodiscard]] bool isWindowVisible() const { return windowVisible_; }
    [[nodiscard]] bool animationsActive() const { return animationsActive_; }
    [[nodiscard]] std::uint64_t submittedFrames() const { return submittedFrames_; }
    // 最近一次 requestFrame 关联的窗口（阶段8A 诊断；空 = 未指定）。
    [[nodiscard]] core::WindowId lastRequestedWindow() const {
        return lastRequestedWindow_;
    }

  private:
    [[nodiscard]] std::uint64_t now() const;
    [[nodiscard]] std::uint32_t frameIntervalMs() const;

    Config config_{};
    FrameClock* clock_{nullptr};
    std::uint32_t pending_{0};
    std::uint32_t active_{0};
    bool animationsActive_{false};
    bool reduceAnimation_{false};
    bool windowVisible_{true};
    bool vsync_{true};
    bool hasSubmittedOnce_{false};
    std::uint64_t lastSubmitMs_{0};
    std::uint64_t lastResizeRequestMs_{0};
    bool hasResizeRequest_{false};
    std::uint64_t submittedFrames_{0};
    core::WindowId lastRequestedWindow_{};
};

}  // namespace lumen::render
