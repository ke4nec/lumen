#pragma once

// M18（m15-roadmap §6）：headless 树导出工具（开发者诊断的第一块——
// 无窗口环境的对照输出，CI/脚本可 diff）。header-only：不增加链接面，
// settings `--dump-tree` 与单测共用同一实现。后续 inspector/帧统计
// overlay 建立在同源数据上（RenderNode/语义树），dump 输出格式变更需
// 同步调用方 golden。

#include <sstream>
#include <string>

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

}  // namespace lumen::app
