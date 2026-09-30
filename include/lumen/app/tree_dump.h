#pragma once

// M18（m15-roadmap §6）：headless 树导出工具（开发者诊断的第一块——
// 无窗口环境的对照输出，CI/脚本可 diff）。header-only：不增加链接面，
// settings `--dump-tree` 与单测共用同一实现。后续 inspector/帧统计
// overlay 建立在同源数据上（RenderNode/语义树），dump 输出格式变更需
// 同步调用方 golden。

#include <functional>
#include <sstream>
#include <string>

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

}  // namespace lumen::app
