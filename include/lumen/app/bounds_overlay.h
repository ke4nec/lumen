#pragma once

// R6（completion-plan 阶段D 续项 / m15-roadmap M18 §6.1）：bounds 与
// damage 调试图层。与帧读数 HUD（frame_debug.h）互补：本层不建 Widget
// overlay——直接合成 RenderNode 平铺树（绝对 offset 为数据，无需布局
// 坐标系统支持），在主场景命令后录制，纯绘制、零额外帧（只随重绘帧
// 出现，不驱动帧节奏）。全子树 excludeFromSemantics（诊断图层不进读
// 屏树）。默认关闭时 AppShell 不合成、不录制——frame hash 与性能基线
// 不变。
//
// 语义边界：bounds 层描画「本帧实际布局」的每个节点外框（1px，主题
// token 取色）；damage 层半透明填充「本帧提交的 damage 矩形并集前」的
// 逐矩形清单（AppShell::paintFrame 在提交前留存副本）。两者可同时开启。

#include <functional>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"

namespace lumen::app {

// bounds 描画树：对 scene 每个节点生成一个 1px 描边叶（绝对 offset =
// 累积父偏移；size = border-box）。空输入返回空树（size 0）。
// 深度交替两种描边色（bounds/boundsAlt token 派生），便于区分父子框。
[[nodiscard]] core::RenderNode makeBoundsOverlayTree(
    const core::RenderNode& scene, core::Color bounds, core::Color boundsAlt);

// damage 描画树：每个矩形一个半透明填充 + 1px 描边叶（statusError 派
// 生色；view 坐标即绝对坐标）。空清单返回空树。
[[nodiscard]] core::RenderNode makeDamageOverlayTree(
    const std::vector<core::Rect>& damage, core::Color fill,
    core::Color outline);

namespace bounds_overlay_detail {

// 合成一个描边叶（Container 型；背景透明 + 1px 描边）。
[[nodiscard]] inline core::RenderNode outlineNode(core::Offset offset,
                                                  core::Size size,
                                                  core::Color color) {
    core::RenderNode node;
    node.type = core::WidgetType::Container;
    node.offset = offset;
    node.size = size;
    node.excludeFromSemantics = true;
    node.excludeFromFocus = true;
    node.style.component = core::CommonResolvedStyle{};
    core::CommonResolvedStyle& common =
        core::commonStyle(node.style.component);
    common.border = color;
    common.borderWidth = 1.0F;
    return node;
}

}  // namespace bounds_overlay_detail

inline core::RenderNode makeBoundsOverlayTree(const core::RenderNode& scene,
                                              core::Color bounds,
                                              core::Color boundsAlt) {
    using bounds_overlay_detail::outlineNode;
    core::RenderNode root;
    root.type = core::WidgetType::Container;
    root.size = scene.size;
    root.excludeFromSemantics = true;
    root.excludeFromFocus = true;
    // 先序遍历（与 dumpRenderTree 同序）；绝对 offset = 累积父偏移。
    // 深度交替 bounds/boundsAlt 描边色。
    std::function<void(const core::RenderNode&, core::Offset, int,
                       core::RenderNode&)>
        walk = [&](const core::RenderNode& node, core::Offset absolute,
                   int depth, core::RenderNode& sink) {
        const core::Color color = depth % 2 == 0 ? bounds : boundsAlt;
        if (node.size.width > 0.0F && node.size.height > 0.0F) {
            sink.children.push_back(
                outlineNode(absolute, node.size, color));
        }
        const core::Offset childBase{absolute.x + node.offset.x,
                                     absolute.y + node.offset.y};
        for (const auto& child : node.children) {
            walk(child, childBase, depth + 1, sink);
        }
    };
    // scene 根自身的 offset 通常为 0；沿用累积规则保持一致。
    walk(scene, core::Offset{0.0F, 0.0F}, 0, root);
    return root;
}

inline core::RenderNode makeDamageOverlayTree(
    const std::vector<core::Rect>& damage, core::Color fill,
    core::Color outline) {
    using bounds_overlay_detail::outlineNode;
    core::RenderNode root;
    root.type = core::WidgetType::Container;
    root.excludeFromSemantics = true;
    root.excludeFromFocus = true;
    for (const core::Rect& rect : damage) {
        if (rect.size.width <= 0.0F || rect.size.height <= 0.0F) {
            continue;
        }
        core::RenderNode node =
            outlineNode(rect.origin, rect.size, outline);
        core::CommonResolvedStyle& common =
            core::commonStyle(node.style.component);
        common.background = fill;
        root.children.push_back(std::move(node));
    }
    return root;
}

}  // namespace lumen::app
