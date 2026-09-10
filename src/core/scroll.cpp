#include "lumen/core/scroll.h"

#include <algorithm>

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

bool ScrollController::applyWheel(float deltaY) {
    return scrollBy(deltaY);
}

bool ScrollController::applyDrag(float deltaY) {
    // 内容跟随手指：手指下移（deltaY > 0）时内容向上滚回（offset 减小）。
    return scrollBy(-deltaY * kDragRatio);
}

bool ScrollController::applyKey(Key key, float viewportExtent) {
    const float page = viewportExtent > 0.0F ? viewportExtent * 0.9F
                                             : 120.0F;
    switch (key) {
        case Key::PageDown:
        case Key::Down:
            return scrollBy(page);
        case Key::PageUp:
        case Key::Up:
            return scrollBy(-page);
        case Key::Home:
            if (offset_ == 0.0F) {
                return false;
            }
            offset_ = 0.0F;
            return true;
        case Key::End:
            if (offset_ == maxOffset_) {
                return false;
            }
            offset_ = maxOffset_;
            return true;
        default:
            return false;
    }
}

bool ScrollController::semanticScroll(float deltaY) {
    return scrollBy(-deltaY);
}

float ScrollController::visibleFraction() const {
    if (contentExtent_ <= 0.0F) {
        return 1.0F;
    }
    return std::clamp(viewportExtent_ / contentExtent_, 0.0F, 1.0F);
}

}  // namespace lumen::core
