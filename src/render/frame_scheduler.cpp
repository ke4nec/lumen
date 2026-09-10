#include "lumen/render/frame_scheduler.h"

#include <algorithm>
#include <chrono>

namespace lumen::render {

namespace {
constexpr std::uint32_t kReasonBit(FrameReason reason) {
    return 1U << static_cast<std::uint32_t>(reason);
}
}  // namespace

std::uint64_t RealtimeClock::nowMs() const {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

FrameScheduler::FrameScheduler(Config config, FrameClock* clock)
    : config_(config), clock_(clock) {
    lastSubmitMs_ = now();
}

void FrameScheduler::requestFrame(FrameReason reason) {
    pending_ |= kReasonBit(reason);
    if (reason == FrameReason::Resize) {
        lastResizeRequestMs_ = now();
        hasResizeRequest_ = true;
    }
}

void FrameScheduler::setAnimationsActive(bool active) {
    animationsActive_ = active;
}

void FrameScheduler::setWindowVisible(bool visible) {
    windowVisible_ = visible;
}

void FrameScheduler::setVSyncEnabled(bool vsync) {
    vsync_ = vsync;
}

std::uint64_t FrameScheduler::now() const {
    static const RealtimeClock kRealtime;
    return clock_ != nullptr ? clock_->nowMs() : kRealtime.nowMs();
}

std::uint32_t FrameScheduler::frameIntervalMs() const {
    if (config_.targetFps == 0) {
        return 0;
    }
    return 1000U / std::max(1U, config_.targetFps);
}

FrameScheduler::FrameDecision FrameScheduler::evaluateFrame() const {
    FrameDecision decision;
    const std::uint64_t nowMs = now();

    // 最小化暂停：原因保留，恢复后按原语义补交。
    if (!windowVisible_ && config_.pauseWhenHidden) {
        decision.pausedByHidden = true;
        decision.waitMs = std::nullopt;
        return decision;
    }

    const bool hasPending = pending_ != 0;
    const bool nonResizePending =
        pending_ & ~kReasonBit(FrameReason::Resize);

    if (hasPending) {
        // 输入优先：非 Resize 原因（含 Input）不被 resize 防抖拖延。
        if (!nonResizePending && hasResizeRequest_) {
            const std::uint64_t sinceResize = nowMs - lastResizeRequestMs_;
            if (sinceResize < config_.resizeDebounceMs) {
                decision.deferredByResizeDebounce = true;
                decision.reasons = pending_;
                decision.waitMs = static_cast<std::uint32_t>(
                    config_.resizeDebounceMs - sinceResize);
                return decision;
            }
        }
    }

    // VSync/目标帧率节流。首帧不受预算限制（应用启动帧立即提交）。
    const std::uint32_t interval = frameIntervalMs();
    if (hasSubmittedOnce_ && vsync_ && interval > 0) {
        const std::uint64_t sinceSubmit = nowMs - lastSubmitMs_;
        if (sinceSubmit < interval) {
            const std::uint32_t remaining =
                static_cast<std::uint32_t>(interval - sinceSubmit);
            // 帧预算未到：有原因或动画在跑就等到 deadline，否则空闲等待。
            if (hasPending || animationsActive_) {
                decision.reasons = pending_;
                decision.waitMs = remaining;
            } else {
                decision.waitMs = std::nullopt;
            }
            return decision;
        }
    }

    if (hasPending) {
        decision.submit = true;
        decision.reasons = pending_;
        decision.waitMs = 0;
        return decision;
    }

    if (animationsActive_) {
        // 动画 deadline：上一帧提交后经过一个帧间隔即到期（上面的节流
        // 已经把未到期的情形挡掉）。
        decision.submit = true;
        decision.reasons = kReasonBit(FrameReason::Animation);
        decision.waitMs = 0;
        return decision;
    }

    // 空闲：无 dirty、无动画、无资源完成 → 不提交。
    decision.waitMs = std::nullopt;
    return decision;
}

bool FrameScheduler::shouldSubmitFrame() {
    const FrameDecision decision = evaluateFrame();
    if (!decision.submit) {
        return false;
    }
    // 消费：pending 移入当前帧；turn 中途的新请求落在 pending（下一帧）。
    active_ = pending_;
    pending_ = 0;
    if (active_ & kReasonBit(FrameReason::Resize)) {
        hasResizeRequest_ = false;
    }
    return true;
}

void FrameScheduler::markFrameSubmitted() {
    active_ = 0;
    lastSubmitMs_ = now();
    hasSubmittedOnce_ = true;
    submittedFrames_ += 1;
}

std::optional<std::uint32_t> FrameScheduler::msUntilNextFrame() const {
    return evaluateFrame().waitMs;
}

}  // namespace lumen::render
