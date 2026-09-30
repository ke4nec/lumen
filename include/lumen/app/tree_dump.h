#pragma once

// M18（m15-roadmap §6）：headless 树导出工具（开发者诊断的第一块——
// 无窗口环境的对照输出，CI/脚本可 diff）。header-only：不增加链接面，
// settings `--dump-tree` 与单测共用同一实现。后续 inspector/帧统计
// overlay 建立在同源数据上（RenderNode/语义树），dump 输出格式变更需
// 同步调用方 golden。

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>

#include "lumen/accessibility/semantics.h"
#include "lumen/core/render_node.h"

namespace lumen::app {

// 每行一节点（先序遍历）：缩进深度 + key/identity + 类型序号 + 盒子
// （offset 相对父级，size 为 border-box）+ 常用标志。确定性输出（无
// 指针/时间）——同一树两次导出逐字节相等。
inline void dumpRenderTreeNode(std::ostringstream& out,
                               const core::RenderNode& node, int depth) {
    for (int i = 0; i < depth; ++i) {
        out << "  ";
    }
    out << "type=" << static_cast<int>(node.type);
    if (!node.key.empty()) {
        out << " key=" << node.key;
    }
    if (!node.identity.empty()) {
        out << " id=" << node.identity;
    }
    out << " box=(" << node.offset.x << "," << node.offset.y << " "
        << node.size.width << "x" << node.size.height << ")";
    out << " flags=";
    if (!node.enabled) out << "!";
    if (node.selected) out << "s";
    if (node.clipContent) out << "c";
    if (node.excludeFromSemantics) out << "x";
    if (node.scrollExtent > 0.0F) {
        out << " scroll=" << node.scrollOffset << "/" << node.scrollExtent;
    }
    out << "\n";
    for (const auto& child : node.children) {
        dumpRenderTreeNode(out, child, depth + 1);
    }
}

// 整树导出（root 自 depth 0）。空树返回空串。
[[nodiscard]] inline std::string dumpRenderTree(
    const core::RenderNode& root) {
    std::ostringstream out;
    dumpRenderTreeNode(out, root, 0);
    return out.str();
}


// --- M18：语义树导出（AppShell::buildSemanticsSnapshot 同源；确定性） ---

inline void dumpSemanticsNode(std::ostringstream& out,
                              const accessibility::SemanticsNode& node,
                              int depth) {
    for (int i = 0; i < depth; ++i) {
        out << "  ";
    }
    out << "role=" << accessibility::semanticsRoleName(node.role);
    out << " id=" << node.id;
    if (!node.label.empty()) {
        out << " label=\"" << node.label << "\"";
    }
    if (!node.value.empty()) {
        out << " value=\"" << node.value << "\"";
    }
    out << " bounds=(" << node.bounds.origin.x << "," << node.bounds.origin.y
        << " " << node.bounds.size.width << "x" << node.bounds.size.height
        << ")";
    out << " flags=";
    if (!(node.flags & accessibility::kSemanticsEnabled)) out << "!";
    if ((node.flags & accessibility::kSemanticsFocused) != 0) out << "F";
    if ((node.flags & accessibility::kSemanticsSelected) != 0) out << "s";
    if ((node.flags & accessibility::kSemanticsChecked) != 0) out << "c";
    if ((node.flags & accessibility::kSemanticsHidden) != 0) out << "h";
    if ((node.flags & accessibility::kSemanticsInvalid) != 0) out << "i";
    const std::string actions =
        accessibility::semanticsActionsName(node.actions);
    if (!actions.empty()) {
        out << " actions=" << actions;
    }
    out << " children=" << node.children.size();
    out << "\n";
}

// 整树导出（自 rootId 先序递归；children 按 id 引用解析——缺失引用
// 跳过不崩溃）。确定性输出（map 有序；无指针/时间）。
[[nodiscard]] inline std::string dumpSemanticsTree(
    const accessibility::SemanticsTree& tree) {
    std::ostringstream out;
    std::function<void(const std::string&, int)> walk =
        [&](const std::string& id, int depth) {
            const auto* node = tree.find(id);
            if (node == nullptr) {
                return;
            }
            dumpSemanticsNode(out, *node, depth);
            for (const auto& child : node->children) {
                walk(child, depth + 1);
            }
        };
    if (!tree.rootId.empty()) {
        walk(tree.rootId, 0);
    }
    return out.str();
}


// --- R6/M18：ResolvedStyle 导出（--dump-style；与 --dump-tree 同树同序） ---

// 颜色十六进制（#RRGGBBAA；小写）。确定性：uint8 逐位输出。
inline void dumpStyleColor(std::ostringstream& out, core::Color color) {
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "%02x%02x%02x%02x", color.r,
                  color.g, color.b, color.a);
    out << '#' << buffer;
}

// 组件 variant 名称（ComponentResolvedStyle 的 alternative 顺序，
// core/style.h 冻结；新增 variant 必须同步此处与调用方 golden）。
[[nodiscard]] inline const char* componentStyleName(
    const core::ComponentResolvedStyle& component) {
    switch (component.index()) {
        case 0: return "common";
        case 1: return "button";
        case 2: return "textfield";
        case 3: return "checkbox";
        case 4: return "switch";
        case 5: return "radio";
        case 6: return "slider";
        case 7: return "progressbar";
        case 8: return "tabs";
        case 9: return "listrow";
        default: return "unknown";
    }
}

// 组件专有段（common 之外才有）。字段名与 core/style.h 结构体一致。
inline void dumpStyleComponent(std::ostringstream& out,
                               const core::ComponentResolvedStyle& component) {
    std::visit(
        [&out](const auto& part) {
            using Part = std::decay_t<decltype(part)>;
            if constexpr (std::is_same_v<Part, core::CommonResolvedStyle>) {
                (void)part;
            } else if constexpr (std::is_same_v<Part,
                                                core::ButtonResolvedStyle>) {
                out << " icon=" << part.iconSize << "x" << part.iconGap
                    << " stroke=" << part.iconStroke
                    << " alignStart=" << (part.alignContentStart ? "yes" : "no")
                    << " reserveIcon=" << (part.reserveIconSpace ? "yes" : "no");
            } else if constexpr (std::is_same_v<Part,
                                                core::TextFieldResolvedStyle>) {
                out << " placeholder=";
                dumpStyleColor(out, part.placeholder);
                out << " caret=";
                dumpStyleColor(out, part.caret);
                out << " preedit=";
                dumpStyleColor(out, part.preeditUnderline);
                out << " focused=" << (part.focused ? "yes" : "no")
                    << " readOnly=" << (part.readOnly ? "yes" : "no")
                    << " multiline=" << (part.multiline ? "yes" : "no")
                    << " obscure=" << (part.obscure ? "yes" : "no");
            } else if constexpr (std::is_same_v<Part,
                                                core::CheckboxResolvedStyle>) {
                out << " checked=" << (part.checked ? "yes" : "no")
                    << " indeterminate=" << (part.indeterminate ? "yes" : "no")
                    << " indicator=" << part.indicatorSize
                    << " slot=" << part.slotSize << " indicator=";
                dumpStyleColor(out, part.indicator);
                out << " outline=";
                dumpStyleColor(out, part.indicatorOutline);
                out << " on=";
                dumpStyleColor(out, part.indicatorChecked);
                out << " mark=";
                dumpStyleColor(out, part.mark);
            } else if constexpr (std::is_same_v<Part,
                                                core::SwitchResolvedStyle>) {
                out << " checked=" << (part.checked ? "yes" : "no")
                    << " knobPos=" << part.knobPosition
                    << " track=" << part.trackWidth << "x" << part.trackHeight
                    << " knob=" << part.knobSize << " slot=" << part.slotSize
                    << " trackOff=";
                dumpStyleColor(out, part.trackOff);
                out << " trackOn=";
                dumpStyleColor(out, part.trackOn);
                out << " knobOff=";
                dumpStyleColor(out, part.knobOff);
                out << " knobOn=";
                dumpStyleColor(out, part.knobOn);
            } else if constexpr (std::is_same_v<Part,
                                                core::RadioResolvedStyle>) {
                out << " checked=" << (part.checked ? "yes" : "no")
                    << " indicator=" << part.indicatorSize
                    << " dotRatio=" << part.dotRatio
                    << " slot=" << part.slotSize << " indicator=";
                dumpStyleColor(out, part.indicator);
                out << " outline=";
                dumpStyleColor(out, part.indicatorOutline);
                out << " on=";
                dumpStyleColor(out, part.indicatorChecked);
                out << " dot=";
                dumpStyleColor(out, part.dot);
            } else if constexpr (std::is_same_v<Part,
                                                core::SliderResolvedStyle>) {
                out << " trackH=" << part.trackHeight
                    << " thumb=" << part.thumbDiameter
                    << " inset=" << part.trackInset << " remaining=";
                dumpStyleColor(out, part.trackRemaining);
                out << " active=";
                dumpStyleColor(out, part.trackActive);
                out << " thumbFill=";
                dumpStyleColor(out, part.thumbFill);
                out << " thumbOutline=";
                dumpStyleColor(out, part.thumbOutline);
            } else if constexpr (std::is_same_v<Part,
                                                core::ProgressBarResolvedStyle>) {
                out << " trackH=" << part.trackHeight << " track=";
                dumpStyleColor(out, part.track);
                out << " fill=";
                dumpStyleColor(out, part.fill);
            } else if constexpr (std::is_same_v<Part,
                                                core::TabsResolvedStyle>) {
                out << " indicatorH=" << part.indicatorHeight
                    << " separatorH=" << part.separatorHeight
                    << " indicator=";
                dumpStyleColor(out, part.indicator);
                out << " separator=";
                dumpStyleColor(out, part.separator);
            } else if constexpr (std::is_same_v<Part,
                                                core::ListRowResolvedStyle>) {
                out << " separatorW=" << part.separatorWidth
                    << " markerW=" << part.markerWidth
                    << " markerInset=" << part.markerInset
                    << " separator=";
                dumpStyleColor(out, part.separator);
                out << " marker=";
                dumpStyleColor(out, part.selectionMarker);
            }
        },
        component);
}

// 每节点两行（先序；与 dumpRenderTree 同树同序）：
//   1) 深度缩进 + key/identity + 组件名；
//   2) 同级缩进 + "style:" 前缀（区别于子节点行）+ common 段（颜色
//      十六进制/圆角/内边距/文本/度量）+ 组件专有段。
inline void dumpStyleNode(std::ostringstream& out,
                          const core::RenderNode& node, int depth) {
    for (int i = 0; i < depth; ++i) {
        out << "  ";
    }
    out << "type=" << static_cast<int>(node.type);
    if (!node.key.empty()) {
        out << " key=" << node.key;
    }
    if (!node.identity.empty()) {
        out << " id=" << node.identity;
    }
    out << " style=" << componentStyleName(node.style.component) << "\n";
    for (int i = 0; i < depth + 1; ++i) {
        out << "  ";
    }
    out << "style: ";
    const core::CommonResolvedStyle& common = node.commonStyle();
    out << "bg=";
    dumpStyleColor(out, common.background);
    out << " fg=";
    dumpStyleColor(out, common.foreground);
    out << " border=";
    dumpStyleColor(out, common.border);
    out << " focus=";
    dumpStyleColor(out, common.focusRing);
    out << " sel=";
    dumpStyleColor(out, common.selection);
    out << " isolation=";
    dumpStyleColor(out, common.focusIsolation);
    out << " radius=(" << common.radius.topLeft << "," << common.radius.topRight
        << "," << common.radius.bottomLeft << "," << common.radius.bottomRight
        << ")";
    out << " borderWidth=" << common.borderWidth
        << " focusWidth=" << common.focusWidth
        << " elevation=" << common.elevation;
    out << " pad=(" << common.padding.left << "," << common.padding.top << ","
        << common.padding.right << "," << common.padding.bottom << ")";
    out << " text=" << common.text.fontSize << "/" << common.text.weight
        << (common.text.bold ? "bold" : "");
    out << " min=" << node.style.minWidth << "x" << node.style.minHeight
        << " gap=" << node.style.controlGap;
    dumpStyleComponent(out, node.style.component);
    out << "\n";
    for (const auto& child : node.children) {
        dumpStyleNode(out, child, depth + 1);
    }
}

// 整树导出（root 自 depth 0）。空树返回空串。确定性（无指针/时间）。
[[nodiscard]] inline std::string dumpStyleTree(const core::RenderNode& root) {
    std::ostringstream out;
    dumpStyleNode(out, root, 0);
    return out.str();
}

}  // namespace lumen::app
