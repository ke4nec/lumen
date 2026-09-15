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

FrameScheduler::FrameScheduler() : FrameScheduler(Config{}) {}

FrameScheduler::FrameScheduler(Config config, FrameClock* clock)
    : config_(config), clock_(clock) {
    lastSubmitMs_ = now();
}

void FrameScheduler::requestFrame(FrameReason reason, core::WindowId window) {
    pending_ |= kReasonBit(reason);
    lastRequestedWindow_ = window;
    if (reason == FrameReason::Resize) {
        lastResizeRequestMs_ = now();
        hasResizeRequest_ = true;
    }
}

void FrameScheduler::setAnimationsActive(bool active) {
    animationsActive_ = active;
}

void FrameScheduler::setAnimationDeadline(std::optional<std::uint64_t> deadlineMs) {
    animationDeadlineMs_ = deadlineMs;
}

void FrameScheduler::setWindowVisible(bool visible) {
    windowVisible_ = visible;
}

void FrameScheduler::setVSyncEnabled(bool vsync) {
    vsync_ = vsync;
}

void FrameScheduler::setReduceAnimation(bool reduceAnimation) {
    // 可访问性设置（plan 8C：减少动画）：动画不再驱动连续帧提交；光标
    // 闪烁等视觉效果由应用侧按设置静止。
    reduceAnimation_ = reduceAnimation;
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
    // 减少动画时动画不驱动提交（plan 8C 可访问性设置）。
    const bool animationsEffective = animationsActive_ && !reduceAnimation_;

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
            // 帧预算未到：有原因或动画在跑就等到 deadline；离散唤醒
            //（tooltip 延迟）等到 min(预算, 时刻)；否则空闲等待。
            if (hasPending || animationsEffective) {
                decision.reasons = pending_;
                decision.waitMs = remaining;
            } else if (animationDeadlineMs_.has_value() &&
                       nowMs < *animationDeadlineMs_) {
                decision.waitMs = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(remaining,
                                            *animationDeadlineMs_ - nowMs));
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

    if (animationsEffective) {
        // 动画 deadline：上一帧提交后经过一个帧间隔即到期（上面的节流
        // 已经把未到期的情形挡掉）。
        decision.submit = true;
        decision.reasons = kReasonBit(FrameReason::Animation);
        decision.waitMs = 0;
        return decision;
    }

    // 离散唤醒：到达即按 Animation 提交一帧；未到期空闲等待到时刻。
    if (animationDeadlineMs_.has_value()) {
        if (nowMs >= *animationDeadlineMs_) {
            decision.submit = true;
            decision.reasons = kReasonBit(FrameReason::Animation);
            decision.waitMs = 0;
            return decision;
        }
        decision.waitMs =
            static_cast<std::uint32_t>(*animationDeadlineMs_ - nowMs);
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
    // 离散唤醒一次性消费（应用每轮按最新状态重设）。
    animationDeadlineMs_.reset();
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
