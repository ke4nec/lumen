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
        case WidgetType::Icon:   // M6：图标叶子（矢量折线）
        case WidgetType::Slider:      // M6：值控件叶子
        case WidgetType::ProgressBar: // M6：展示叶子
        case WidgetType::Radio:       // M6：选择叶子
        case WidgetType::Tooltip:     // M6：提示叶子
        case WidgetType::Dropdown:    // M11：值行叶子（选项走 overlay）
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
        case WidgetType::Tabs:      // M6：标签行容器
        case WidgetType::ThemeScope:  // M6：主题域（单子容器）
        // 集合控件：布局期按可见区物化行（同 VirtualList）。
        case WidgetType::List:
        case WidgetType::Tree:
        case WidgetType::TreeList:
        // Splitter：两窗格容器（分隔条由布局物化）。
        case WidgetType::Splitter:
            return false;
    }
    return false;
}

bool isFlexContainer(WidgetType type) {
    return type == WidgetType::Row || type == WidgetType::Column;
}

bool isScrollableWidget(WidgetType type) {
    return type == WidgetType::ScrollView || type == WidgetType::ListView ||
           type == WidgetType::VirtualList ||  // M3：滚轮/键盘/语义滚动目标
           // 集合控件：同一滚动路径（M3 引擎复用）。
           type == WidgetType::List || type == WidgetType::Tree ||
           type == WidgetType::TreeList;
}

Widget VirtualListSource::buildHeader() const { return Widget{}; }

}  // namespace lumen::core
