// v0.3 阶段8E (plan §4 8E / §5.2): 移动 host 接缝测试。
//
// 覆盖：surface attach/detach/resize 的 metrics 先行顺序、pause/resume
// 生命周期映射（surface 未重连时 resume 挂起）、内存告警 → Suspended、
// 触摸归一化坐标 → 逻辑坐标（deviceScale 分隔）、pointer cancel、返回
// 请求统一为 WindowCloseRequested。全部 headless，无模拟器依赖。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lumen/platform/mobile_host_seam.h"

using namespace lumen;
using namespace lumen::platform;
using core::AppLifecycle;
using core::HostEventType;

namespace {

core::HostEvent drainOne(MobileHostSeam& seam) {
    core::HostEvent event;
    REQUIRE(seam.pollEvent(event));
    return event;
}

void drainEmpty(MobileHostSeam& seam) {
    core::HostEvent event;
    CHECK_FALSE(seam.pollEvent(event));
}

}  // namespace

TEST_CASE("mobile_seam_surface_lifecycle_metrics_first", "[mobile]") {
    MobileHostSeam seam;
    CHECK(seam.lifecycle() == AppLifecycle::Launching);
    CHECK_FALSE(seam.canRender());

    // surface 创建：SurfaceReattached → Resize → Active，且事件可见时
    // metrics 已经是新值（plan §3.1 顺序不变量）。
    seam.surfaceCreated(1080.0F, 2340.0F, 3.0F,
                        core::EdgeInsets::only(60.0F, 120.0F, 60.0F, 40.0F));
    CHECK(seam.surfaceAttached());
    CHECK(seam.metrics().drawableSize == core::Size{1080.0F, 2340.0F});
    CHECK(seam.metrics().logicalSize == core::Size{360.0F, 780.0F});
    CHECK(seam.metrics().deviceScale == 3.0F);
    CHECK(seam.metrics().safeArea.top == 120.0F);

    CHECK(drainOne(seam).type == HostEventType::SurfaceReattached);
    const core::HostEvent resize = drainOne(seam);
    CHECK(resize.type == HostEventType::Resize);
    CHECK(resize.pixelSize == core::Size{1080.0F, 2340.0F});
    CHECK(drainOne(seam).type == HostEventType::LifecycleChanged);
    CHECK(seam.lifecycle() == AppLifecycle::Active);
    CHECK(seam.canRender());
    drainEmpty(seam);

    // 旋转/resize：metrics 先行。
    seam.surfaceChanged(2340.0F, 1080.0F, 3.0F, core::EdgeInsets{});
    CHECK(seam.metrics().logicalSize == core::Size{780.0F, 360.0F});
    CHECK(drainOne(seam).type == HostEventType::Resize);
    drainEmpty(seam);

    // detach：不销毁状态（metrics 保留），暂停渲染。
    seam.surfaceDestroyed();
    CHECK_FALSE(seam.surfaceAttached());
    CHECK_FALSE(seam.canRender());
    CHECK(drainOne(seam).type == HostEventType::SurfaceDetached);
    CHECK(seam.metrics().logicalSize == core::Size{780.0F, 360.0F});
    drainEmpty(seam);

    // 重连：恢复可见 + 新 Resize（恢复时统一 reset/reupload 流程由
    // Renderer::resetSurface 承担）。
    seam.surfaceCreated(2340.0F, 1080.0F, 3.0F, core::EdgeInsets{});
    CHECK(seam.canRender());
    CHECK(drainOne(seam).type == HostEventType::SurfaceReattached);
    CHECK(drainOne(seam).type == HostEventType::Resize);
    drainEmpty(seam);
}

TEST_CASE("mobile_seam_pause_resume_defers_until_surface", "[mobile]") {
    MobileHostSeam seam;
    seam.surfaceCreated(1000.0F, 2000.0F, 2.0F, core::EdgeInsets{});
    core::HostEvent event;
    while (seam.pollEvent(event)) {
    }
    CHECK(seam.lifecycle() == AppLifecycle::Active);

    // 暂停期间 surface 被销毁（Android 常见顺序）。
    seam.appPaused();
    seam.surfaceDestroyed();
    CHECK(seam.lifecycle() == AppLifecycle::Background);
    CHECK_FALSE(seam.canRender());

    // resume 先于 surface 重建到达：保持 Background 等待重连。
    seam.appResumed();
    CHECK(seam.lifecycle() == AppLifecycle::Background);
    CHECK(seam.lastNotice().find("deferred") != std::string::npos);

    // surface 重连后回 Active。
    seam.surfaceCreated(1000.0F, 2000.0F, 2.0F, core::EdgeInsets{});
    CHECK(seam.lifecycle() == AppLifecycle::Active);
    CHECK(seam.canRender());
}

TEST_CASE("mobile_seam_low_memory_suspends_from_background", "[mobile]") {
    MobileHostSeam seam;
    seam.surfaceCreated(500.0F, 1000.0F, 1.0F, core::EdgeInsets{});
    seam.appPaused();
    CHECK(seam.lifecycle() == AppLifecycle::Background);
    // 前台内存告警不降级；后台告警 → Suspended（状态树保留）。
    seam.appLowMemory();
    CHECK(seam.lifecycle() == AppLifecycle::Suspended);
    seam.appResumed();
    seam.surfaceCreated(500.0F, 1000.0F, 1.0F, core::EdgeInsets{});
    CHECK(seam.lifecycle() == AppLifecycle::Active);
}

TEST_CASE("mobile_seam_touch_normalizes_to_logical_coordinates", "[mobile]") {
    MobileHostSeam seam;
    seam.surfaceCreated(1080.0F, 2340.0F, 3.0F, core::EdgeInsets{});
    core::HostEvent event;
    while (seam.pollEvent(event)) {
    }

    // 归一化 (0.5, 0.25) → 逻辑 (180, 195)。
    seam.touchDown(7, 0.5F, 0.25F);
    seam.touchMove(7, 0.75F, 0.5F);
    seam.touchCancel(7);
    seam.touchUp(7, 0.5F, 0.25F);

    const core::HostEvent down = drainOne(seam);
    CHECK(down.type == HostEventType::PointerDown);
    CHECK(down.device == core::PointerDevice::Touch);
    CHECK(down.pointerId == 7);
    CHECK(down.position.x == Catch::Approx(180.0F).epsilon(0.001F));
    CHECK(down.position.y == Catch::Approx(195.0F).epsilon(0.001F));

    const core::HostEvent move = drainOne(seam);
    CHECK(move.type == HostEventType::PointerMove);
    CHECK(move.position.x == Catch::Approx(270.0F).epsilon(0.001F));

    const core::HostEvent cancel = drainOne(seam);
    CHECK(cancel.type == HostEventType::PointerCancel);
    CHECK(cancel.pointerId == 7);

    const core::HostEvent up = drainOne(seam);
    CHECK(up.type == HostEventType::PointerUp);
    drainEmpty(seam);
}

TEST_CASE("mobile_seam_back_request_maps_to_close_requested", "[mobile]") {
    MobileHostSeam seam;
    seam.backRequested();
    const core::HostEvent back = drainOne(seam);
    CHECK(back.type == HostEventType::WindowCloseRequested);
    drainEmpty(seam);
    // 事件时间戳单调递增（确定性顺序）。
    seam.touchDown(1, 0.0F, 0.0F);
    const core::HostEvent next = drainOne(seam);
    CHECK(next.timestampMs > back.timestampMs);
}
