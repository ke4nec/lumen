#include "lumen/core/widget.h"

namespace lumen::core {

std::pair<std::size_t, std::size_t> VirtualListSource::visibleRangeAt(
    float offset, float viewportExtent, float cacheExtent) const {
    if (itemCount() == 0 || viewportExtent <= 0.0F) {
        return {0, 0};
    }
    const float cache = std::max(0.0F, cacheExtent);
    const float top = std::max(0.0F, offset - cache);
    const float bottom = offset + viewportExtent + cache;
    // extent 累计单调：二分跳过视口上方项，不构建离屏 Widget。
    std::size_t low = 0;
    std::size_t high = itemCount();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (offsetOfIndex(middle) + extentOf(middle) <= top) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    const std::size_t first = low;
    float y = offsetOfIndex(first);
    while (low < itemCount() && y < bottom) {
        y += extentOf(low++);
    }
    return {first, low};
}

bool isLeafWidget(WidgetType type) {
    switch (type) {
        case WidgetType::Text:
        case WidgetType::Button:
        case WidgetType::TextField:
        case WidgetType::Checkbox:
        case WidgetType::Switch:
        case WidgetType::Image:  // M3：图像叶子（占位/位图绘制）
            return true;
        case WidgetType::Container:
        case WidgetType::Row:
        case WidgetType::Column:
        case WidgetType::Stack:
        case WidgetType::ScrollView:
        case WidgetType::ListView:
        case WidgetType::FocusScope:
        case WidgetType::Grid:
        case WidgetType::VirtualList:
            return false;
    }
    return false;
}

bool isFlexContainer(WidgetType type) {
    return type == WidgetType::Row || type == WidgetType::Column;
}

bool isScrollableWidget(WidgetType type) {
    return type == WidgetType::ScrollView || type == WidgetType::ListView ||
           type == WidgetType::VirtualList;  // M3：滚轮/键盘/语义滚动目标
}

}  // namespace lumen::core
