// SplitterController（docs/lumen-splitter-design.md §5.2/§5.3）：位置钳制
// 与状态持有；布局/交互经 core::SplitterSource 驱动（见头注释）。

#include "lumen/widgets/splitter.h"

#include <algorithm>
#include <limits>

namespace lumen::widgets {

void SplitterController::noteLayout(float clampedOffset,
                                    float extent) const {
    const float newExtent = std::max(0.0F, extent);
    // M17（splitter-design §5.3）：KeepRatio——extent 变化时 offset 随
    // 比例缩放（两窗格按比例分摊增量）；钳制按**新** extent（先缩放、
    // 更新 lastExtent_、再 clamp——clampOffset 的 max = extent - min
    // 依赖新值）。传入的 clampedOffset 是 KeepOffset 口径，本分支不用。
    if (resizeBehavior_ == ResizeBehavior::KeepRatio && seeded_ &&
        lastExtent_ > 0.0F && newExtent > 0.0F) {
        const float scaled = offset_ * newExtent / lastExtent_;
        lastExtent_ = newExtent;
        offset_ = clampOffset(scaled);
    } else {
        offset_ = clampedOffset;
        lastExtent_ = newExtent;
    }
    seeded_ = true;
    // 塌缩一致性：布局钳制把 offset 推离 min 时解除塌缩（塌缩标记必须
    // 符合视觉事实；KeepRatio 缩放同理）。
    if (collapsed_ && offset_ > minLeading_) {
        collapsed_ = false;
    }
}

bool SplitterController::setCollapsed(bool collapsed) const {
    if (!collapsible_ || collapsed == collapsed_) {
        return false;
    }
    if (collapsed) {
        restore_ = offset_;
        collapsed_ = true;
        applyOffset(minLeading_);
        return true;
    }
    collapsed_ = false;
    applyOffset(restore_);
    return true;
}

void SplitterController::applyOffset(float px) const {
    const float next = clampOffset(px);
    const bool changed = next != offset_;
    offset_ = next;
    // M17：塌缩中移离 min 的任何输入（拖动/步进/到边/程序设值）自动
    // 解除塌缩。
    if (collapsed_ && offset_ > minLeading_) {
        collapsed_ = false;
    }
    if (changed && onOffsetChanged) {
        onOffsetChanged(offset_);
    }
}

}  // namespace lumen::widgets
