// v0.2 阶段7D tests (plan §5 调度): invalidate 合并、空闲不绘制、动画
// deadline、resize 防抖、最小化暂停、VSync 开关和时间源注入。

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include "lumen/render/frame_scheduler.h"

using lumen::render::FrameClock;
using lumen::render::FrameReason;
using lumen::render::FrameScheduler;

namespace {

class ManualClock final : public FrameClock {
  public:
    [[nodiscard]] std::uint64_t nowMs() const override { return now_; }
    void advance(std::uint32_t ms) { now_ += ms; }
    void setNow(std::uint64_t ms) { now_ = ms; }

  private:
    std::uint64_t now_{0};
};

}  // namespace

TEST_CASE("scheduler_merges_invalidate_reasons_per_frame", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 0;  // 不节流，聚焦合并语义
    FrameScheduler scheduler{config, &clock};

    scheduler.requestFrame(FrameReason::Input);
    scheduler.requestFrame(FrameReason::Input);
    scheduler.requestFrame(FrameReason::Resource);
    CHECK(scheduler.hasPendingReasons());

    // 同一 turn 的多次请求只产生一次提交机会，且当前帧不重入。
    REQUIRE(scheduler.shouldSubmitFrame());
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK(scheduler.submittedFrames() == 1);
    CHECK_FALSE(scheduler.hasPendingReasons());
}

TEST_CASE("scheduler_does_not_submit_when_idle", "[scheduler]") {
    ManualClock clock;
    FrameScheduler scheduler{FrameScheduler::Config{}, &clock};

    for (int turn = 0; turn < 10; ++turn) {
        clock.advance(20);
        CHECK_FALSE(scheduler.shouldSubmitFrame());
        CHECK(scheduler.msUntilNextFrame() == std::nullopt);
    }
    CHECK(scheduler.submittedFrames() == 0);
}

TEST_CASE("scheduler_applies_animation_deadline", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 60;  // 16ms 帧预算
    FrameScheduler scheduler{config, &clock};

    scheduler.setAnimationsActive(true);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK(scheduler.submittedFrames() == 1);

    // 未到 deadline：不提交，但要等到 deadline。
    clock.advance(8);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    REQUIRE(scheduler.msUntilNextFrame().has_value());
    CHECK(*scheduler.msUntilNextFrame() <= 8);

    // 到达 deadline：动画帧提交。
    clock.advance(8);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK(scheduler.submittedFrames() == 2);

    // 动画停止：帧预算到期后补交一帧终态（终值样本落地），随后空闲。
    scheduler.setAnimationsActive(false);
    clock.advance(16);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK(scheduler.submittedFrames() == 3);
    clock.advance(100);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
}

// 动画活跃→静止的转换补交一帧（"终拍"）：动画态不再驱动提交后，终值
// 样本（tween 终拍 markDirty 的终态树）没有其他提交通道——不补交则屏
// 幕停留在最后一个中间样本（慢帧率下可见：菜单淡入卡在半透明，直到
// 下一次输入帧）。静止→静止不触发；reduceAnimation 下不触发。
TEST_CASE("scheduler_submits_retire_frame_when_animations_end",
          "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 0;  // 不节流，聚焦终拍语义
    FrameScheduler scheduler{config, &clock};

    // 静止→静止：无终拍可补。
    scheduler.setAnimationsActive(false);
    CHECK_FALSE(scheduler.shouldSubmitFrame());

    scheduler.setAnimationsActive(true);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();

    // 活跃→静止：补交一帧，随后回到空闲。
    scheduler.setAnimationsActive(false);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    CHECK(scheduler.submittedFrames() == 2);

    // reduceAnimation：动画从不驱动连续提交，活跃→静止也不补交。
    scheduler.setReduceAnimation(true);
    scheduler.setAnimationsActive(true);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    scheduler.setAnimationsActive(false);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    CHECK(scheduler.submittedFrames() == 2);
}

// 终拍补交同样受 VSync 帧预算节流：预算内等待，预算到即提交。
TEST_CASE("scheduler_retire_frame_respects_vsync_budget", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 60;
    FrameScheduler scheduler{config, &clock};

    scheduler.setAnimationsActive(true);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();

    clock.advance(8);  // 预算内
    scheduler.setAnimationsActive(false);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    REQUIRE(scheduler.msUntilNextFrame().has_value());
    CHECK(*scheduler.msUntilNextFrame() <= 8);

    clock.advance(8);  // 预算到期
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    clock.advance(100);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
}

TEST_CASE("scheduler_debounces_resize", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 0;
    config.resizeDebounceMs = 30;
    FrameScheduler scheduler{config, &clock};

    // 连续 resize：每次请求都刷新静默期，一直不提交。
    for (int burst = 0; burst < 5; ++burst) {
        scheduler.requestFrame(FrameReason::Resize);
        CHECK_FALSE(scheduler.shouldSubmitFrame());
        clock.advance(10);
    }

    // 静默 30ms 后提交。
    clock.advance(31);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK(scheduler.submittedFrames() == 1);
}

TEST_CASE("scheduler_input_bypasses_resize_debounce", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 0;
    config.resizeDebounceMs = 500;
    FrameScheduler scheduler{config, &clock};

    scheduler.requestFrame(FrameReason::Resize);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    // 输入优先：resize 防抖不能拖延输入帧。
    scheduler.requestFrame(FrameReason::Input);
    CHECK(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
}

TEST_CASE("scheduler_pauses_when_minimized", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 0;
    FrameScheduler scheduler{config, &clock};

    scheduler.setAnimationsActive(true);
    scheduler.setWindowVisible(false);
    for (int turn = 0; turn < 10; ++turn) {
        clock.advance(20);
        CHECK_FALSE(scheduler.shouldSubmitFrame());
    }
    // 原因与动画保持挂起，不产生提交。
    CHECK(scheduler.submittedFrames() == 0);

    // 还原后恢复提交。
    scheduler.setWindowVisible(true);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK(scheduler.submittedFrames() == 1);
}

TEST_CASE("scheduler_vsync_toggle_changes_throttling", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 60;
    FrameScheduler scheduler{config, &clock};

    scheduler.setVSyncEnabled(false);
    // VSync 关：有原因立即提交，不受帧预算限制。
    clock.advance(1);
    scheduler.requestFrame(FrameReason::Explicit);
    CHECK(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();

    clock.advance(1);
    scheduler.requestFrame(FrameReason::Explicit);
    CHECK(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();

    // VSync 开：同样 1ms 间隔被节流。
    scheduler.setVSyncEnabled(true);
    scheduler.requestFrame(FrameReason::Explicit);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    clock.advance(16);
    CHECK(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
}

TEST_CASE("scheduler_requests_during_turn_target_next_frame", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 0;
    FrameScheduler scheduler{config, &clock};

    scheduler.requestFrame(FrameReason::Input);
    REQUIRE(scheduler.shouldSubmitFrame());
    // 当前帧录制/提交期间到达的新事件只能请求下一帧。
    scheduler.requestFrame(FrameReason::Input);
    CHECK(scheduler.hasPendingReasons());
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    // 上一帧已提交，pending 属于新帧。
    CHECK(scheduler.submittedFrames() == 1);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK(scheduler.submittedFrames() == 2);
}

TEST_CASE("scheduler_animation_frame_cannot_reenter_before_submission",
          "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 0;
    FrameScheduler scheduler{config, &clock};

    scheduler.setAnimationsActive(true);
    REQUIRE(scheduler.shouldSubmitFrame());
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    scheduler.setAnimationsActive(false);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK_FALSE(scheduler.shouldSubmitFrame());
}

TEST_CASE("scheduler_reducing_running_animation_preserves_terminal_frame",
          "[scheduler]") {
    ManualClock clock;
    FrameScheduler scheduler{{}, &clock};
    scheduler.setAnimationsActive(true);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();

    clock.advance(8);
    scheduler.setReduceAnimation(true);
    scheduler.setAnimationsActive(false);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    REQUIRE(scheduler.msUntilNextFrame().has_value());
    CHECK(*scheduler.msUntilNextFrame() == 8);
    scheduler.setWindowVisible(false);
    clock.advance(100);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    scheduler.setWindowVisible(true);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK_FALSE(scheduler.shouldSubmitFrame());
}

TEST_CASE("scheduler_resource_completion_wakes_the_loop", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 60;
    FrameScheduler scheduler{config, &clock};

    CHECK_FALSE(scheduler.shouldSubmitFrame());
    // 模拟 UI 线程 pump 到异步资源完成。
    scheduler.requestFrame(FrameReason::Resource);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK(scheduler.submittedFrames() == 1);
    // 消费完毕后回到空闲（不重复提交）。
    clock.advance(50);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
}

TEST_CASE("realtime_clock_is_monotonic", "[scheduler]") {
    lumen::render::RealtimeClock clock;
    const std::uint64_t first = clock.nowMs();
    const std::uint64_t second = clock.nowMs();
    CHECK(second >= first);
}

// M11 review：离散动画唤醒（tooltip 延迟到期）——空闲等待到时刻、到达
// 按 Animation 提交一帧、一次性消费；不占用连续动画帧。
TEST_CASE("scheduler_animation_deadline_wakes_idle_loop", "[scheduler]") {
    ManualClock clock;
    FrameScheduler::Config config;
    config.targetFps = 60;
    FrameScheduler scheduler{config, &clock};

    // 空闲：无 deadline 不提交、可无限等待。
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    CHECK_FALSE(scheduler.msUntilNextFrame().has_value());

    // 未来 deadline：等待到时刻，期间不提交。
    scheduler.setAnimationDeadline(500);
    REQUIRE(scheduler.msUntilNextFrame().has_value());
    CHECK(*scheduler.msUntilNextFrame() == 500);
    clock.advance(400);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    REQUIRE(scheduler.msUntilNextFrame().has_value());
    CHECK(*scheduler.msUntilNextFrame() == 100);

    // 到达：按 Animation 提交；一次性消费后回到空闲。
    clock.advance(100);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK_FALSE(scheduler.msUntilNextFrame().has_value());
}

TEST_CASE("scheduler_due_deadline_waits_for_remaining_frame_budget",
          "[scheduler]") {
    ManualClock clock;
    FrameScheduler scheduler{{}, &clock};
    scheduler.requestFrame(FrameReason::Explicit);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();

    scheduler.setAnimationDeadline(8);
    CHECK(scheduler.msUntilNextFrame() == 8U);
    clock.advance(8);
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    // 到期但尚被 VSync 节流，不能误判成空闲而失去下一次唤醒。
    REQUIRE(scheduler.msUntilNextFrame().has_value());
    CHECK(*scheduler.msUntilNextFrame() == 8);
    clock.advance(8);
    REQUIRE(scheduler.shouldSubmitFrame());
    scheduler.markFrameSubmitted();
    CHECK_FALSE(scheduler.shouldSubmitFrame());
    CHECK_FALSE(scheduler.msUntilNextFrame().has_value());
}
