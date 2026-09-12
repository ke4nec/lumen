#pragma once

#include <cstddef>
#include <string>

#include "lumen/core/render_node.h"
#include "lumen/render/render_commands.h"
#include "lumen/render/renderer.h"
#include "lumen/text/font_manager.h"

namespace lumen::render {

// 视觉系统（visual-system-design §5）：控件 chrome 状态（hover、pressed、
// focused、disabled、checked、invalid）已在布局期折算进 RenderNode 的
// resolved style；PaintOptions 只保留文本绘制无法从样式树推导的编辑瞬
// 态（caret、selection、IME composition）。
struct PaintOptions {
    // 光标（grapheme cluster 索引）与闪烁透明度（plan 阶段6）；1 为实心
    // 光标，headless 帧哈希保持确定。
    std::size_t caretGraphemes{0};
    float caretAlpha{1.0F};
    // 焦点字段的选区（grapheme 范围）与 preedit 文本。
    std::size_t selectionStart{0};
    std::size_t selectionEnd{0};
    bool hasSelection{false};
    std::string composition{};
};

// Walks the render tree, accumulates absolute offsets and programs the
// renderer (plan §5.2 step 4). 所有颜色、圆角、边框、焦点环与部件几何
// 都来自 RenderNode 的 resolved style——painter 不再持有控件默认值，也
// 不依赖 Theme（CPU/Skia/GPU 命令路径无需知道 Theme）。
void paintScene(Renderer& renderer, const core::RenderNode& root,
                const PaintOptions& options = {});

// M1：显式字体源的绘制入口（与 LayoutEngine 传入同一 FontManager 时，
// 布局与绘制共用同一份 TextLayout；CPU 默认占位）。
void paintScene(Renderer& renderer, const core::RenderNode& root,
                const PaintOptions& options, const text::FontManager& fonts);

// v0.2 阶段7B: 同一个 walker 只录制命令（plan §3.1: Painter 只负责把
// RenderNode 转换为命令）。命令带 damage 裁剪所需的节点 bounds；文本
// 命令的 bounds 是其所在裁剪区（文本本身被裁剪到该区域）。
[[nodiscard]] RenderCommandList recordScene(const core::RenderNode& root,
                                            const PaintOptions& options = {});

// M1：显式字体源的录制入口（同上）。
[[nodiscard]] RenderCommandList recordScene(const core::RenderNode& root,
                                            const PaintOptions& options,
                                            const text::FontManager& fonts);

}  // namespace lumen::render
