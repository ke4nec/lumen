#pragma once

#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"

namespace lumen::layout {

// Box + Flex subset: Row/Column flex distribution, Container padding/margin,
// Stack relative positioning. Intrinsic size, baseline and full Flutter
// constraint semantics are explicitly out of scope for Stage 1.
class LayoutEngine {
  public:
    static core::RenderNode layout(const core::Widget& widget,
                                   const core::Constraints& constraints);
};

}  // namespace lumen::layout
