// M3：VirtualList 控制器实现（见头注释）。UI 线程独占。
//
// 累计偏移按已测量的稀疏段求和；可见区起点经累计偏移二分定位，
// 不需要构建视口上方的 Widget。

#include "lumen/core/virtual_list.h"

#include <algorithm>
#include <cmath>

namespace lumen::core {
namespace {

constexpr float kMinExtent = 1.0F;  // 实测值下限（防 0 高死循环）。

}  // namespace

void VirtualListController::setItemCount(std::size_t count) {
    if (count == itemCount_) {
        return;
    }
    itemCount_ = count;
    // 超界项的实测值失效（数据替换语义；stable key 变化 = 项目替换）。
    if (!measured_.empty()) {
        measured_.erase(measured_.lower_bound(count), measured_.end());
    }
    updateViewport(scroll_.viewportExtent(), contentPadding_);
}

void VirtualListController::setEstimatedExtent(float extent) {
    if (std::isfinite(extent) && extent > 0.0F && extent != estimatedExtent_) {
        estimatedExtent_ = extent;
        updateViewport(scroll_.viewportExtent(), contentPadding_);
    }
}

void VirtualListController::setItemBuilder(
    std::function<Widget(std::size_t)> builder) {
    itemBuilder_ = std::move(builder);
}

std::size_t VirtualListController::itemCount() const { return itemCount_; }

float VirtualListController::estimatedExtent() const {
    return estimatedExtent_;
}

float VirtualListController::extentOf(std::size_t index) const {
    const auto it = measured_.find(index);
    return it != measured_.end() ? it->second : estimatedExtent_;
}

float VirtualListController::scrollOffset() const {
    return scroll_.offset();
}

Widget VirtualListController::buildItem(std::size_t index) const {
    if (itemBuilder_ == nullptr) {
        return Widget{};
    }
    return itemBuilder_(index);
}

void VirtualListController::noteExtent(std::size_t index,
                                       float extent) const {
    if (index >= itemCount_ || !std::isfinite(extent)) {
        return;
    }
    const float clamped = std::max(kMinExtent, extent);
    const auto it = measured_.find(index);
    if (it != measured_.end() && it->second == clamped) {
        return;  // 幂等：布局第二遍不再标记变化。
    }
    const float previousExtent = extentOf(index);
    const float previousOffset = scroll_.offset();
    const bool aboveViewport =
        offsetOfIndex(index) + previousExtent <= previousOffset;
    measured_[index] = clamped;
    // 先更新最大偏移，再平移锚点；否则在列表底部正向修正会被旧 max
    // 截断。首次测量同样要把估值与实测值之差计入。
    updateViewport(scroll_.viewportExtent(), contentPadding_);
    scroll_.scrollTo(previousOffset +
                     (aboveViewport ? clamped - previousExtent : 0.0F));
    extentsChanged_ = true;
}

void VirtualListController::updateViewport(float viewportExtent,
                                            float contentPadding) const {
    contentPadding_ = std::max(0.0F, contentPadding);
    scroll_.updateExtents(viewportExtent, totalExtent() + contentPadding_);
}


float VirtualListController::totalExtent() const {
    float total = 0.0F;
    std::size_t cursor = 0;
    for (const auto& [index, extent] : measured_) {
        if (index > cursor) {
            total += static_cast<float>(index - cursor) * estimatedExtent_;
        }
        total += extent;
        cursor = index + 1;
    }
    if (cursor < itemCount_) {
        total += static_cast<float>(itemCount_ - cursor) * estimatedExtent_;
    }
    return total;
}

float VirtualListController::offsetOfIndex(std::size_t index) const {
    const std::size_t clamped = std::min(index, itemCount_);
    float offset = 0.0F;
    std::size_t cursor = 0;
    for (const auto& [i, extent] : measured_) {
        if (i >= clamped) {
            break;
        }
        if (i > cursor) {
            offset +=
                static_cast<float>(std::min(i, clamped) - cursor) *
                estimatedExtent_;
        }
        offset += extent;
        cursor = i + 1;
    }
    if (cursor < clamped) {
        offset += static_cast<float>(clamped - cursor) * estimatedExtent_;
    }
    return offset;
}

std::pair<std::size_t, std::size_t> VirtualListController::visibleRange(
    float viewportExtent, float cacheExtent) const {
    const float offset = std::clamp(
        scroll_.offset(), 0.0F,
        std::max(0.0F, totalExtent() + contentPadding_ - viewportExtent));
    return visibleRangeAt(offset, viewportExtent, cacheExtent);
}

void VirtualListController::scrollToIndex(std::size_t index,
                                          float viewportExtent) {
    if (itemCount_ == 0) {
        return;
    }
    const std::size_t target = std::min(index, itemCount_ - 1);
    const float top = offsetOfIndex(target);
    const float bottom = top + extentOf(target);
    const float offset = scroll_.offset();
    if (top < offset) {
        scroll_.scrollTo(top);  // 在视口上方：滚到可见顶部。
    } else if (bottom > offset + viewportExtent) {
        // 在视口下方：滚到可见底部（保留上方上下文）。
        scroll_.scrollTo(bottom - viewportExtent);
    }
}

bool VirtualListController::consumeExtentsChanged() {
    const bool changed = extentsChanged_;
    extentsChanged_ = false;
    return changed;
}

}  // namespace lumen::core
