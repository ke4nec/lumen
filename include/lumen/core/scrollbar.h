#pragma once

#include <optional>

#include "lumen/core/render_node.h"

namespace lumen::core {

// Scroll design §5: one local geometry for painting, hit testing and dragging.
struct ScrollbarGeometry {
    Rect track;
    Rect thumb;
    Rect thumbHit;
    float travel{0.0F};
};

[[nodiscard]] std::optional<ScrollbarGeometry> scrollbarGeometry(const RenderNode& node);

}  // namespace lumen::core
