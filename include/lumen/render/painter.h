#pragma once

#include <cstddef>
#include <string>

#include "lumen/core/render_node.h"
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
    // Caret position (code points) inside the focused TextField.
    std::size_t caretCodePoints{0};
    // Caret opacity 0..1 for the blink animation (plan 阶段6); 1 renders a
    // solid caret and keeps headless frame hashes deterministic.
    float caretAlpha{1.0F};
};

// Walks the render tree, accumulates absolute offsets and programs the
// renderer (plan §5.2 step 4). Button/TextField chrome uses built-in
// defaults so the CPU backend alone yields a presentable UI.
void paintScene(Renderer& renderer, const core::RenderNode& root,
                const PaintOptions& options = {});

}  // namespace lumen::render
