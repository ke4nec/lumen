#pragma once

// R6（completion-plan 阶段D 收口 / m15-roadmap M18 §6.1 首版）：inspector
// 检视图层——纯绘制（与帧读数 HUD/bounds/damage 调试图层同架构），不
// 经 overlay 槽位、不参与命中/焦点/语义，应用输入零影响。悬停检视：
// 最近指针位置（AppShell::pointerMove 记录）解析主树最深命中节点，面
// 板显示 type/key/identity/绝对 bounds/组件样式/滚动/文本摘要，并以
// 2px 描边高亮该节点边界（"选中 → bounds 高亮"的悬停态实现；点击 pin
// 为后续增量——面板固定于右上角，与帧读数 HUD（左上）互让）。
// 行文案与 --dump-tree/--dump-style 同口径（数值类型/组件名），对照
// 工具链一致。

#include <cstdio>
#include <string>
#include <vector>

#include "lumen/app/bounds_overlay.h"
#include "lumen/app/frame_debug.h"
#include "lumen/app/tree_dump.h"
#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"

namespace lumen::app {

// 深度命中：先序走树（子节点逆序 = 绘制序优先），返回包含 point 的
// 最深节点并回写其绝对矩形。无命中返回 nullptr。
[[nodiscard]] inline const core::RenderNode* findNodeAt(
    const core::RenderNode& node, core::Offset absolute, core::Offset point,
    core::Rect& outRect) {
    const core::Rect rect{absolute, node.size};
    if (!rect.contains(point)) {
        return nullptr;
    }
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        const core::Offset childBase{absolute.x + it->offset.x,
                                     absolute.y + it->offset.y};
        if (const core::RenderNode* hit =
                findNodeAt(*it, childBase, point, outRect)) {
            return hit;
        }
    }
    outRect = rect;
    return &node;
}

namespace inspector_layer_detail {

// 信息面板行（Text 节点；绝对 offset 数据——本层不经布局）。
[[nodiscard]] inline core::RenderNode infoRow(core::Offset offset,
                                              float width,
                                              const std::string& content,
                                              core::Color color) {
    core::RenderNode node;
    node.type = core::WidgetType::Text;
    node.offset = offset;
    node.size = core::Size{width, 15.0F};
    node.text = content;
    node.excludeFromSemantics = true;
    node.excludeFromFocus = true;
    core::CommonResolvedStyle& common =
        core::commonStyle(node.style.component);
    common.text.fontSize = 11.0F;
    common.text.color = color;
    return node;
}

}  // namespace inspector_layer_detail

// 检视图层（配色经 FrameOverlayStyle 派生；outline 为命中高亮描边色，
// 2px）。view 尺寸取 scene.size；focusedIdentity 非空且命中节点一致时
// 追加 [focused] 标记。
[[nodiscard]] inline core::RenderNode makeInspectorLayer(
    const core::RenderNode& scene, core::Offset pointer,
    const std::string& focusedIdentity, const FrameOverlayStyle& style,
    core::Color outline) {
    using inspector_layer_detail::infoRow;
    using bounds_overlay_detail::outlineNode;

    constexpr float kLineStep = 15.0F;
    constexpr float kPadX = 10.0F;
    constexpr float kPadY = 7.0F;
    constexpr float kMargin = 8.0F;
    constexpr float kPanelWidth = 300.0F;

    core::Rect hitRect{};
    const core::RenderNode* hit =
        findNodeAt(scene, core::Offset{0.0F, 0.0F}, pointer, hitRect);

    struct Row {
        std::string text{};
        bool accent{false};
    };
    std::vector<Row> rows;
    if (hit == nullptr) {
        rows.push_back(Row{"inspector: no node at pointer", true});
    } else {
        char line[160];
        std::snprintf(line, sizeof(line), "type=%d",
                      static_cast<int>(hit->type));
        std::string first = line;
        if (!hit->key.empty()) {
            first += " key=" + hit->key;
        }
        rows.push_back(Row{first, true});
        if (!hit->identity.empty()) {
            std::string id = hit->identity;
            if (!focusedIdentity.empty() && id == focusedIdentity) {
                id += " [focused]";
            }
            rows.push_back(Row{"id=" + id, false});
        }
        std::snprintf(line, sizeof(line), "at=(%.0f,%.0f) %.0fx%.0f",
                      hitRect.origin.x, hitRect.origin.y, hitRect.size.width,
                      hitRect.size.height);
        rows.push_back(Row{std::string("bounds ") + line, false});
        rows.push_back(
            Row{std::string("style=") + componentStyleName(hit->style.component),
                false});
        if (hit->scrollExtent > 0.0F) {
            std::snprintf(line, sizeof(line), "scroll=%.0f/%.0f",
                          hit->scrollOffset, hit->scrollExtent);
            rows.push_back(Row{line, false});
        }
        if (!hit->text.empty()) {
            std::string snippet = hit->text.substr(0, 24);
            if (hit->text.size() > 24) {
                snippet += "…";
            }
            rows.push_back(Row{"text=\"" + snippet + "\"", false});
        }
        std::string flags;
        if (!hit->enabled) flags += " disabled";
        if (hit->selected) flags += " selected";
        if (!flags.empty()) {
            rows.push_back(Row{std::string("flags:") + flags, false});
        }
    }

    core::RenderNode layer;
    layer.type = core::WidgetType::Container;
    layer.size = scene.size;  // 铺满视口承载（自身无背景）。
    layer.excludeFromSemantics = true;
    layer.excludeFromFocus = true;

    // 命中高亮（2px 描边；先画——面板在其上不遮挡时序一致）。
    if (hit != nullptr) {
        core::RenderNode highlight =
            outlineNode(hitRect.origin, hitRect.size, outline);
        core::commonStyle(highlight.style.component).borderWidth = 2.0F;
        layer.children.push_back(std::move(highlight));
    }

    // 右上角信息面板（半透明表面；行文案同 dump 口径）。
    const float panelHeight =
        kPadY * 2.0F + static_cast<float>(rows.size()) * kLineStep;
    core::RenderNode panel;
    panel.type = core::WidgetType::Container;
    panel.offset = core::Offset{scene.size.width - kPanelWidth - kMargin,
                                kMargin};
    panel.size = core::Size{kPanelWidth, panelHeight};
    panel.excludeFromSemantics = true;
    panel.excludeFromFocus = true;
    core::CommonResolvedStyle& panelStyle =
        core::commonStyle(panel.style.component);
    panelStyle.background = style.panel;
    panelStyle.radius = core::CornerRadius::all(6.0F);
    const float textWidth = kPanelWidth - kPadX * 2.0F;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        panel.children.push_back(infoRow(
            core::Offset{kPadX, kPadY + static_cast<float>(i) * kLineStep},
            textWidth, rows[i].text,
            rows[i].accent ? style.accentText : style.text));
    }
    layer.children.push_back(std::move(panel));
    return layer;
}

}  // namespace lumen::app
