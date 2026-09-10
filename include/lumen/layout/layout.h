#pragma once

#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"

namespace lumen::layout {

// Box + Flex subset: Row/Column flex distribution, Container padding/margin,
// Stack relative positioning. v0.3 阶段8D 补充：滚动视口约束（ScrollView/
// ListView 子内容主轴不限 + clip）、intrinsic 尺寸与 baseline 查询。
class LayoutEngine {
  public:
    static core::RenderNode layout(const core::Widget& widget,
                                   const core::Constraints& constraints);

    // intrinsic（自然）尺寸：在给定约束下测量的内容尺寸（不定位）。
    // flex 近似为“约束下的布局尺寸”，两遍 min/max intrinsic 留待后续。
    [[nodiscard]] static core::Size intrinsicSize(
        const core::Widget& widget, const core::Constraints& constraints);

    // 子树的文本 baseline（根相对 y；无文本节点返回 -1）。
    [[nodiscard]] static float baseline(const core::RenderNode& node);
};

}  // namespace lumen::layout
