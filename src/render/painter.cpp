#include "lumen/render/painter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "lumen/core/utf8.h"

namespace lumen::render {
namespace {

using core::Color;
using core::CornerRadius;
using core::Offset;
using core::RenderNode;
using core::Rect;
using core::Size;
using core::TextStyle;
using core::WidgetType;

constexpr Color kButtonBackground{212, 212, 216, 255};
constexpr Color kButtonPressed{162, 162, 170, 255};
constexpr Color kFieldBackground{46, 46, 54, 255};
constexpr Color kFieldFocused{64, 64, 76, 255};
constexpr Color kContentText{228, 228, 234, 255};
constexpr Color kButtonLabel{24, 24, 27, 255};
constexpr Color kPlaceholderText{128, 128, 138, 255};
constexpr Color kCaret{238, 238, 242, 255};

// TextStyle defaults to opaque black. On the dark surfaces the painter
// draws (page, fields) that is unreadable, so treat the untouched default as
// "unset" and substitute `fallback`; explicitly styled text passes through.
TextStyle contentStyle(const TextStyle& style, Color fallback) {
    if (style.color == Color{0, 0, 0, 255}) {
        TextStyle adjusted = style;
        adjusted.color = fallback;
        return adjusted;
    }
    return style;
}

// Mirrors layout's placeholder text metric so paint aligns with measurement.
float textAdvance(const std::string& text, float fontSize) {
    return static_cast<float>(core::utf8Length(text)) * fontSize * 0.6F;
}

void paintTextAt(Renderer& renderer, const std::string& text,
                 const TextStyle& style, Offset origin) {
    if (text.empty()) {
        return;
    }
    renderer.drawText(TextRun{text, origin}, style);
}

// Clips leaf content to the node rect so overflowing text (long field
// content, narrow overrides) cannot bleed over neighbors.
struct ScopedClip {
    Renderer& renderer;
    ScopedClip(Renderer& r, Rect rect) : renderer(r) {
        renderer.save();
        renderer.clipRect(rect);
    }
    ~ScopedClip() { renderer.restore(); }
    ScopedClip(const ScopedClip&) = delete;
    ScopedClip& operator=(const ScopedClip&) = delete;
};

void paintNode(Renderer& renderer, const RenderNode& node, Offset absolute,
               const PaintOptions& options) {
    const Offset origin = absolute + node.offset;
    const Rect rect{origin, node.size};

    switch (node.type) {
        case WidgetType::Container:
        case WidgetType::Row:
        case WidgetType::Column:
        case WidgetType::Stack:
            if (node.color.a > 0) {
                renderer.drawRect(rect, node.color, node.radius);
            }
            break;
        case WidgetType::Button: {
            const bool pressed =
                !options.pressedIdentity.empty() &&
                options.pressedIdentity == node.identity;
            renderer.drawRect(rect, pressed ? kButtonPressed : kButtonBackground,
                              CornerRadius::all(6.0F));
            const TextStyle style = contentStyle(node.textStyle, kButtonLabel);
            const float fontSize =
                style.fontSize > 0.0F ? style.fontSize : 14.0F;
            const float lineHeight = fontSize * 1.2F;
            const ScopedClip clip{renderer, rect};
            paintTextAt(renderer, node.text, style,
                        Offset{origin.x + (node.size.width -
                                           textAdvance(node.text, fontSize)) *
                                              0.5F,
                               origin.y + (node.size.height - lineHeight) *
                                              0.5F});
            break;
        }
        case WidgetType::TextField: {
            const bool focused =
                !options.focusedIdentity.empty() &&
                options.focusedIdentity == node.identity;
            renderer.drawRect(rect,
                              focused ? kFieldFocused : kFieldBackground,
                              CornerRadius::all(4.0F));
            const TextStyle style = contentStyle(node.textStyle, kContentText);
            const float fontSize =
                style.fontSize > 0.0F ? style.fontSize : 14.0F;
            const float lineHeight = fontSize * 1.2F;
            const Offset textOrigin{
                origin.x + 8.0F,
                origin.y + (node.size.height - lineHeight) * 0.5F};
            const ScopedClip clip{renderer, rect};
            if (!node.text.empty()) {
                paintTextAt(renderer, node.text, style, textOrigin);
            } else if (!node.placeholder.empty()) {
                TextStyle placeholderStyle = style;
                placeholderStyle.color = kPlaceholderText;
                paintTextAt(renderer, node.placeholder, placeholderStyle,
                            textOrigin);
            }
            if (focused) {
                const float caretX =
                    textOrigin.x +
                    textAdvance(node.text.substr(
                                    0, core::utf8OffsetAt(node.text,
                                                          options.caretCodePoints)),
                                fontSize) +
                    0.5F;
                const float caretWidth = std::max(1.5F, fontSize * 0.08F);
                // Blink fades the caret via alpha (plan 阶段6); the scaled
                // color keeps geometry identical to the solid caret.
                Color caretColor = kCaret;
                const float alpha = std::clamp(options.caretAlpha, 0.0F, 1.0F);
                if (alpha <= 0.0F) {
                    break;
                }
                caretColor.a = static_cast<std::uint8_t>(
                    std::lround(caretColor.a * alpha));
                renderer.drawRect(Rect{Offset{caretX, textOrigin.y},
                                       Size{caretWidth, lineHeight}},
                                  caretColor);
            }
            break;
        }
        case WidgetType::Text: {
            const ScopedClip clip{renderer, rect};
            paintTextAt(renderer, node.text,
                        contentStyle(node.textStyle, kContentText),
                        origin + Offset{node.padding.left, node.padding.top});
            break;
        }
    }

    for (const auto& child : node.children) {
        paintNode(renderer, child, origin, options);
    }
}

}  // namespace

void paintScene(Renderer& renderer, const core::RenderNode& root,
                const PaintOptions& options) {
    paintNode(renderer, root, Offset{}, options);
}

}  // namespace lumen::render
