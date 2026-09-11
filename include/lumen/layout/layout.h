#pragma once

#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/style/resolver.h"

namespace lumen::layout {

// Box + Flex subset: Row/Column flex distribution, Container padding/margin,
// Stack relative positioning. v0.3 阶段8D 补充：滚动视口约束（ScrollView/
// ListView 子内容主轴不限 + clip）、intrinsic 尺寸与 baseline 查询。
//
// 视觉系统（visual-system-design §5）：layout 接收 StyleContext，对每个
// 节点在布局前解析一次样式并写入 RenderNode；布局度量（最小尺寸、
// chrome 内边距、文本样式、部件尺寸）与 painter 使用同一份 resolved
// style。identity 在遍历中逐层分配（与 assignIdentities 相同规则），供
// 样式解析查询交互快照。
class LayoutEngine {
  public:
    // 应用入口：每帧提供 Theme + 交互快照 + 可访问性设置。
    static core::RenderNode layout(const core::Widget& widget,
                                   const core::Constraints& constraints,
                                   const style::StyleContext& styleContext);

    // 便捷入口：默认暗色 Theme、空交互快照（headless/测试/DSL 场景）。
    static core::RenderNode layout(const core::Widget& widget,
                                   const core::Constraints& constraints);

    // intrinsic（自然）尺寸：在给定约束下测量的内容尺寸（不定位）。
    // flex 近似为“约束下的布局尺寸”，两遍 min/max intrinsic 留待后续。
    [[nodiscard]] static core::Size intrinsicSize(
        const core::Widget& widget, const core::Constraints& constraints,
        const style::StyleContext& styleContext);
    [[nodiscard]] static core::Size intrinsicSize(
        const core::Widget& widget, const core::Constraints& constraints);

    // 子树的文本 baseline（根相对 y；无文本节点返回 -1）。
    [[nodiscard]] static float baseline(const core::RenderNode& node);
};

}  // namespace lumen::layout
