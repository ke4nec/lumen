#include "lumen/platform/mobile_host_seam.h"

#include <utility>

namespace lumen::platform {

MobileHostSeam::MobileHostSeam(Config config) : config_(config) {}

void MobileHostSeam::push(core::HostEvent event) {
    event.timestampMs = nextTimestampMs_++;
    queue_.push_back(std::move(event));
}

void MobileHostSeam::setLifecycle(core::AppLifecycle next) {
    if (next == lifecycle_) {
        return;
    }
    core::HostEvent event;
    event.type = core::HostEventType::LifecycleChanged;
    event.previousLifecycle = lifecycle_;
    event.lifecycle = next;
    lifecycle_ = next;
    push(std::move(event));
}

void MobileHostSeam::updateMetrics(float widthPixels, float heightPixels,
                                   float deviceScale,
                                   core::EdgeInsets safeArea) {
    const float scale = deviceScale > 0.0F ? deviceScale : 1.0F;
    metrics_.drawableSize = core::Size{widthPixels, heightPixels};
    metrics_.logicalSize =
        core::Size{widthPixels / scale, heightPixels / scale};
    metrics_.deviceScale = scale;
    metrics_.safeArea = safeArea;
}

core::Offset MobileHostSeam::toLogical(float x, float y) const {
    if (!config_.touchNormalized) {
        return core::Offset{x / metrics_.deviceScale,
                            y / metrics_.deviceScale};
    }
    return core::Offset{x * metrics_.logicalSize.width,
                        y * metrics_.logicalSize.height};
}

void MobileHostSeam::surfaceCreated(float widthPixels, float heightPixels,
                                    float deviceScale,
                                    core::EdgeInsets safeArea) {
    const bool reattach = surfaceAttached_;
    // metrics 先行（plan §3.1）：事件出队时 drawableSize 已反映新值。
    updateMetrics(widthPixels, heightPixels, deviceScale, safeArea);
    surfaceAttached_ = true;
    metrics_.visible = true;
    metrics_.minimized = false;

    core::HostEvent attach;
    attach.type = reattach ? core::HostEventType::SurfaceReattached
                           : core::HostEventType::SurfaceReattached;
    push(std::move(attach));

    core::HostEvent resize;
    resize.type = core::HostEventType::Resize;
    resize.pixelSize = metrics_.drawableSize;
    push(std::move(resize));

    // 首次 surface 代表初始启动；恢复则必须同时满足 resume 已到达。
    if (lifecycle_ == core::AppLifecycle::Launching) {
        foregroundRequested_ = true;
        setLifecycle(core::AppLifecycle::Active);
    } else if ((lifecycle_ == core::AppLifecycle::Background ||
                lifecycle_ == core::AppLifecycle::Suspended) &&
               foregroundRequested_) {
        setLifecycle(core::AppLifecycle::Active);
    }
    lastNotice_.clear();
}

void MobileHostSeam::surfaceChanged(float widthPixels, float heightPixels,
                                    float deviceScale,
                                    core::EdgeInsets safeArea) {
    if (!surfaceAttached_) {
        lastNotice_ = "surfaceChanged ignored: surface not attached";
        return;
    }
    updateMetrics(widthPixels, heightPixels, deviceScale, safeArea);
    core::HostEvent resize;
    resize.type = core::HostEventType::Resize;
    resize.pixelSize = metrics_.drawableSize;
    push(std::move(resize));
}

void MobileHostSeam::surfaceDestroyed() {
    if (!surfaceAttached_) {
        lastNotice_ = "surfaceDestroyed ignored: already detached";
        return;
    }
    surfaceAttached_ = false;
    metrics_.visible = false;
    core::HostEvent detach;
    detach.type = core::HostEventType::SurfaceDetached;
    push(std::move(detach));
}

void MobileHostSeam::appPaused() {
    foregroundRequested_ = false;
    if (lifecycle_ == core::AppLifecycle::Active ||
        lifecycle_ == core::AppLifecycle::Inactive) {
        setLifecycle(core::AppLifecycle::Background);
        return;
    }
    lastNotice_ = "appPaused ignored in state " +
                  std::string(core::appLifecycleName(lifecycle_));
}

void MobileHostSeam::appResumed() {
    if (lifecycle_ != core::AppLifecycle::Background &&
        lifecycle_ != core::AppLifecycle::Suspended) {
        lastNotice_ = "appResumed ignored in state " +
                      std::string(core::appLifecycleName(lifecycle_));
        return;
    }
    foregroundRequested_ = true;
    if (!surfaceAttached_) {
        // surface 尚未重连：保持当前挂起状态，等待 surfaceCreated。
        lastNotice_ = "appResumed deferred: surface detached";
        return;
    }
    setLifecycle(core::AppLifecycle::Active);
}

void MobileHostSeam::appLowMemory() {
    // 内存告警：标记 Suspended 建议（状态树保留，恢复走统一流程）。
    if (lifecycle_ == core::AppLifecycle::Background) {
        setLifecycle(core::AppLifecycle::Suspended);
    }
}

void MobileHostSeam::touchDown(std::uint32_t pointerId, float x, float y) {
    core::HostEvent event;
    event.type = core::HostEventType::PointerDown;
    event.device = core::PointerDevice::Touch;
    event.pointerId = pointerId;
    event.position = toLogical(x, y);
    event.button = core::PointerButton::Primary;
    push(std::move(event));
}

void MobileHostSeam::touchMove(std::uint32_t pointerId, float x, float y) {
    core::HostEvent event;
    event.type = core::HostEventType::PointerMove;
    event.device = core::PointerDevice::Touch;
    event.pointerId = pointerId;
    event.position = toLogical(x, y);
    push(std::move(event));
}

void MobileHostSeam::touchUp(std::uint32_t pointerId, float x, float y) {
    core::HostEvent event;
    event.type = core::HostEventType::PointerUp;
    event.device = core::PointerDevice::Touch;
    event.pointerId = pointerId;
    event.position = toLogical(x, y);
    event.button = core::PointerButton::Primary;
    push(std::move(event));
}

void MobileHostSeam::touchCancel(std::uint32_t pointerId) {
    core::HostEvent event;
    event.type = core::HostEventType::PointerCancel;
    event.device = core::PointerDevice::Touch;
    event.pointerId = pointerId;
    push(std::move(event));
}

void MobileHostSeam::backRequested() {
    core::HostEvent event;
    event.type = core::HostEventType::WindowCloseRequested;
    push(std::move(event));
}

bool MobileHostSeam::pollEvent(core::HostEvent& out) {
    if (queue_.empty()) {
        out = core::HostEvent{};
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

const char* mobileHostStageName() { return "8E mobile host seam"; }

}  // namespace lumen::platform
