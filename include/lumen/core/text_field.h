#pragma once

#include "lumen/core/render_node.h"
#include "lumen/text/text_layout.h"

namespace lumen::core {

// Shared display geometry for painting, pointer selection and IME placement.
struct TextFieldDisplay {
    std::string text{};
    bool showingPlaceholder{false};
    std::size_t compositionStart{0};
    std::size_t compositionLength{0};
};

[[nodiscard]] TextFieldDisplay textFieldDisplay(
    const RenderNode& node, const std::string& composition = {},
    std::size_t selectionStart = 0);
[[nodiscard]] TextStyle textFieldLayoutStyle(const RenderNode& node);
[[nodiscard]] float textFieldWrapWidth(const RenderNode& node);
[[nodiscard]] Offset textFieldTextOrigin(
    const RenderNode& node, const text::TextLayoutResult& layout,
    std::size_t caret, bool focused);
[[nodiscard]] Rect textFieldCaretRect(
    const RenderNode& node, const text::TextLayoutResult& layout,
    std::size_t caret, bool focused);

}  // namespace lumen::core
