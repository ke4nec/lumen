// SplitterController（docs/lumen-splitter-design.md §5.2）：位置钳制与
// 状态持有；布局/交互经 core::SplitterSource 驱动（见头注释）。

#include "lumen/widgets/splitter.h"

#include <algorithm>
#include <limits>

namespace lumen::widgets {

void SplitterController::noteLayout(float clampedOffset,
                                    float extent) const {
    lastExtent_ = std::max(0.0F, extent);
    offset_ = clampedOffset;
    seeded_ = true;
}

void SplitterController::applyOffset(float px) const {
    const float next = clampOffset(px);
    const bool changed = next != offset_;
    offset_ = next;
    if (changed && onOffsetChanged) {
        onOffsetChanged(offset_);
    }
}

}  // namespace lumen::widgets
