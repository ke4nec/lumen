#include "lumen/core/text_field.h"

#include <algorithm>

#include "lumen/text/grapheme.h"

namespace lumen::core {
namespace {

std::string obscureText(const std::string& value) {
    std::string display;
    const auto count = text::graphemeCount(value);
    display.reserve(count * 3);
    for (std::size_t i = 0; i < count; ++i) {
        display += "\xE2\x80\xA2";
    }
    return display;
}

}  // namespace

TextFieldDisplay textFieldDisplay(const RenderNode& node,
                                  const std::string& composition,
                                  std::size_t selectionStart) {
    TextFieldDisplay result;
    result.showingPlaceholder = node.text.empty() && composition.empty() &&
                                !node.placeholder.empty();
    result.text = result.showingPlaceholder ? node.placeholder
                  : node.obscure ? obscureText(node.text)
                                 : node.text;
    result.compositionLength = text::graphemeCount(composition);
    result.compositionStart = selectionStart >= result.compositionLength
                                  ? selectionStart - result.compositionLength
                                  : 0;
    if (result.compositionLength > 0) {
        const auto& display = result.text;
        result.text = text::graphemeSubstring(display, 0, result.compositionStart) +
                      (node.obscure ? obscureText(composition) : composition) +
                      text::graphemeSubstring(display, result.compositionStart,
                                              text::graphemeCount(display));
    }
    return result;
}

TextStyle textFieldLayoutStyle(const RenderNode& node) {
    TextStyle style = node.textStyle();
    style.maxLines = node.multiline ? 0 : 1;
    return style;
}

float textFieldWrapWidth(const RenderNode& node) {
    return node.multiline
               ? std::max(0.01F, node.size.width - node.padding.horizontal())
               : 0.0F;
}

Offset textFieldTextOrigin(const RenderNode& node,
                           const text::TextLayoutResult& layout,
                           std::size_t caret, bool focused) {
    float x = node.padding.left;
    if (!node.multiline && focused) {
        x += std::min(0.0F, node.size.width - node.padding.horizontal() -
                               1.0F - layout.graphemeToX(caret, nullptr));
    }
    return Offset{x, (node.size.height - layout.size.height) * 0.5F};
}

Rect textFieldCaretRect(const RenderNode& node,
                        const text::TextLayoutResult& layout,
                        std::size_t caret, bool focused) {
    const Offset origin = textFieldTextOrigin(node, layout, caret, focused);
    std::size_t line = 0;
    const float x = layout.graphemeToX(caret, &line);
    return Rect{Offset{origin.x + x,
                       origin.y + static_cast<float>(line) * layout.lineHeightPx},
                Size{1.0F, layout.lineBoxHeightPx}};
}

}  // namespace lumen::core
