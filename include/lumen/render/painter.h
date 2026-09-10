#pragma once

#include <cstddef>
#include <string>

#include "lumen/core/render_node.h"
#include "lumen/render/render_commands.h"
#include "lumen/render/renderer.h"

namespace lumen::render {

// Interaction-driven visual state the painter cannot derive from the tree.
struct PaintOptions {
    // Widget key of the focused TextField; enables the caret and the focused
    // field background.
    std::string focusedKey{};
    std::string focusedIdentity{};
    // Widget key of the pressed Button; enables the pressed background.
    std::string pressedKey{};
    std::string pressedIdentity{};
    // 光标（grapheme cluster 索引）与闪烁透明度（plan 阶段6）；1 为实心
    // 光标，headless 帧哈希保持确定。
    std::size_t caretGraphemes{0};
    float caretAlpha{1.0F};
    // v0.3 阶段8B: 焦点字段的选区（grapheme 范围）与 preedit 文本。
    std::size_t selectionStart{0};
    std::size_t selectionEnd{0};
    bool hasSelection{false};
    std::string composition{};
};

// Walks the render tree, accumulates absolute offsets and programs the
// renderer (plan §5.2 step 4). Button/TextField chrome uses built-in
// defaults so the CPU backend alone yields a presentable UI.
void paintScene(Renderer& renderer, const core::RenderNode& root,
                const PaintOptions& options = {});

// v0.2 阶段7B: 同一个 walker 只录制命令（plan §3.1: Painter 只负责把
// RenderNode 转换为命令）。命令带 damage 裁剪所需的节点 bounds；文本
// 命令的 bounds 是其所在裁剪区（文本本身被裁剪到该区域）。
[[nodiscard]] RenderCommandList recordScene(const core::RenderNode& root,
                                            const PaintOptions& options = {});

}  // namespace lumen::render
