#include "lumen/core/scrollbar.h"

#include <algorithm>

namespace lumen::core {

std::optional<ScrollbarGeometry> scrollbarGeometry(const RenderNode& node) {
    if (!node.showScrollbar || !node.clipContent || !isScrollableWidget(node.type) ||
        node.scrollExtent <= 0.0F || node.scrollbarThickness <= 0.0F ||
        node.scrollbarThumbWidth <= 0.0F || node.scrollbarColor.a == 0 || node.transitionAlpha <= 0.0F) {
        return std::nullopt;
    }
    const bool horizontal = node.scrollAxis == ScrollAxis::Horizontal;
    const float length = horizontal ? node.size.width : node.size.height;
    const float cross = horizontal ? node.size.height : node.size.width;
    if (length <= 0.0F || cross <= 0.0F) return std::nullopt;
    const float inset = std::clamp(node.scrollbarInset, 0.0F, length * 0.5F);
    const float trackLength = length - 2.0F * inset;
    const float thickness = std::min(node.scrollbarThickness, cross);
    if (trackLength <= 0.0F || thickness <= 0.0F) return std::nullopt;
    const float width = std::min(node.scrollbarThumbWidth, thickness);
    const float fraction = length / (length + node.scrollExtent);
    const float thumbLength = std::clamp(trackLength * fraction,
        std::min(std::max(0.0F, node.scrollbarMinLength), trackLength), trackLength);
    const float travel = trackLength - thumbLength;
    const float start = inset + std::clamp(node.scrollOffset / node.scrollExtent, 0.0F, 1.0F) * travel;
    const float edge = cross - thickness;
    const auto rect = [horizontal](float main, float side, float mainSize, float sideSize) {
        return horizontal ? Rect::fromXYWH(main, side, mainSize, sideSize)
                          : Rect::fromXYWH(side, main, sideSize, mainSize);
    };
    return ScrollbarGeometry{rect(inset, edge, trackLength, thickness),
        rect(start, edge + (thickness - width) * 0.5F, thumbLength, width),
        rect(start, edge, thumbLength, thickness), travel};
}

}  // namespace lumen::core
