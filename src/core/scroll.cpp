#include "lumen/core/scroll.h"

#include <algorithm>
#include <cmath>

namespace lumen::core {

void ScrollController::updateExtents(float viewportExtent,
                                     float contentExtent) {
    viewportExtent_ = std::max(0.0F, viewportExtent);
    contentExtent_ = std::max(0.0F, contentExtent);
    maxOffset_ = std::max(0.0F, contentExtent_ - viewportExtent_);
    offset_ = clampOffset(offset_);
}

float ScrollController::clampOffset(float value) const {
    return std::clamp(value, 0.0F, maxOffset_);
}

bool ScrollController::scrollBy(float delta) {
    const float next = clampOffset(offset_ + delta);
    if (next == offset_) {
        return false;
    }
    offset_ = next;
    return true;
}

void ScrollController::scrollTo(float offset) {
    offset_ = clampOffset(offset);
}

bool ScrollController::applyWheel(float wheelDelta) {
    cancelDrag();
    return scrollBy(wheelDelta);
}

bool ScrollController::applyDrag(float dragDelta) {
    // 内容跟随手指：指针沿轴正向拖动（delta > 0）时内容向轴起点滚回
    //（offset 减小）。拖动接管惯性（抓住滚动中的列表）。
    stopFling();
    return scrollBy(-dragDelta * kDragRatio);
}

bool ScrollController::applyKey(Key key, float viewportExtent) {
    cancelDrag();
    const float page = viewportExtent > 0.0F ? viewportExtent * 0.9F
                                             : 120.0F;
    // 方向键按活动轴取组（水平 Left/Right、纵向 Up/Down）；翻页与
    // Home/End 两轴共用。
    const Key forwardKey =
        axis_ == ScrollAxis::Horizontal ? Key::Right : Key::Down;
    const Key backwardKey =
        axis_ == ScrollAxis::Horizontal ? Key::Left : Key::Up;
    if (key == Key::PageDown || key == forwardKey) {
        return scrollBy(page);
    }
    if (key == Key::PageUp || key == backwardKey) {
        return scrollBy(-page);
    }
    if (key == Key::Home) {
        if (offset_ == 0.0F) {
            return false;
        }
        offset_ = 0.0F;
        return true;
    }
    if (key == Key::End) {
        if (offset_ == maxOffset_) {
            return false;
        }
        offset_ = maxOffset_;
        return true;
    }
    return false;
}

bool ScrollController::semanticScroll(float delta) {
    cancelDrag();
    return scrollBy(-delta);
}

void ScrollController::noteDragSample(float dragDeltaPixels,
                                      std::uint64_t timestampMs) {
    if (!dragSampled_) {
        dragSampled_ = true;
        dragVelocityPxMs_ = 0.0F;
        lastDragSampleMs_ = timestampMs;
        pendingDragDelta_ = dragDeltaPixels;
        return;
    }
    pendingDragDelta_ += dragDeltaPixels;
    const std::uint64_t dt =
        timestampMs >= lastDragSampleMs_ ? timestampMs - lastDragSampleMs_ : 0;
    if (dt > 0) {
        dragVelocityPxMs_ = pendingDragDelta_ / static_cast<float>(dt);
        lastDragSampleMs_ = timestampMs;
        pendingDragDelta_ = 0.0F;
    }
}

bool ScrollController::endDrag(std::uint64_t timestampMs) {
    // 冲洗残余样本：释放时刻的最近位移计入速度。
    if (dragSampled_ && pendingDragDelta_ != 0.0F &&
        timestampMs > lastDragSampleMs_) {
        dragVelocityPxMs_ =
            pendingDragDelta_ /
            static_cast<float>(timestampMs - lastDragSampleMs_);
        pendingDragDelta_ = 0.0F;
    }
    dragSampled_ = false;
    // 惯性方向与手指相反（内容继续沿拖动方向滚）。
    flingVelocityPxMs_ = -dragVelocityPxMs_;
    if (!canScroll() ||
        std::abs(flingVelocityPxMs_) < kFlingStartPxPerMs) {
        flingVelocityPxMs_ = 0.0F;
        return false;
    }
    flingLastMs_ = timestampMs;
    return true;
}

bool ScrollController::stepFling(std::uint64_t nowMs) {
    if (flingVelocityPxMs_ == 0.0F) {
        return false;
    }
    const double dt =
        nowMs >= flingLastMs_ ? static_cast<double>(nowMs - flingLastMs_)
                              : 0.0;
    if (dt <= 0.0) {
        return true;
    }
    flingLastMs_ = nowMs;
    const bool moved = scrollBy(flingVelocityPxMs_ * static_cast<float>(dt));
    flingVelocityPxMs_ = static_cast<float>(
        static_cast<double>(flingVelocityPxMs_) * std::exp(-dt / kFlingTauMs));
    if (!moved || std::abs(flingVelocityPxMs_) < kFlingStopPxPerMs) {
        flingVelocityPxMs_ = 0.0F;
        return false;
    }
    return true;
}

void ScrollController::stopFling() { flingVelocityPxMs_ = 0.0F; }

void ScrollController::cancelDrag() {
    stopFling();
    dragSampled_ = false;
    lastDragSampleMs_ = 0;
    pendingDragDelta_ = 0.0F;
    dragVelocityPxMs_ = 0.0F;
}

float ScrollController::visibleFraction() const {
    if (contentExtent_ <= 0.0F) {
        return 1.0F;
    }
    return std::clamp(viewportExtent_ / contentExtent_, 0.0F, 1.0F);
}

}  // namespace lumen::core
