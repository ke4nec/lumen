#include "lumen/render/painter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

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

// v0.2 阶段7B: 命令录制 sink。与 Renderer 暴露同一组即时调用，另维护
// 当前裁剪栈——文本命令的影响区域就是它的裁剪区（文本无法画出该区域），
// 供 damage 裁剪判断。
class CommandRecorder {
  public:
    explicit CommandRecorder(RenderCommandList& list) : list_(list) {}

    void save() {
        list_.save();
        clipStack_.push_back(currentClip_);
    }
    void restore() {
        list_.restore();
        if (!clipStack_.empty()) {
            currentClip_ = clipStack_.back();
            clipStack_.pop_back();
        }
    }
    void clipRect(Rect rect) {
        list_.clipRect(rect);
        currentClip_ = intersectClip(currentClip_, rect);
    }
    void drawRect(Rect rect, Color color, CornerRadius radius = {}) {
        list_.drawRect(rect, color, radius);
    }
    void drawText(TextRun run, TextStyle style) {
        list_.drawText(std::move(run), style, currentClip_);
    }
    void drawImage(ImageId id, Rect destination) {
        list_.drawImage(id, destination);
    }

  private:
    static std::optional<Rect> intersectClip(const std::optional<Rect>& clip,
                                             Rect rect) {
        if (!clip.has_value()) {
            return rect;
        }
        const float x0 = std::max(clip->left(), rect.left());
        const float y0 = std::max(clip->top(), rect.top());
        const float x1 = std::min(clip->right(), rect.right());
        const float y1 = std::min(clip->bottom(), rect.bottom());
        if (x1 <= x0 || y1 <= y0) {
            // Degenerate clip: the command can never paint; keep an empty
            // bounds so damage culling may drop it anywhere.
            return Rect{Offset{x0, y0}, Size{0.0F, 0.0F}};
        }
        return Rect{Offset{x0, y0}, Size{x1 - x0, y1 - y0}};
    }

    RenderCommandList& list_;
    std::optional<Rect> currentClip_{};
    std::vector<std::optional<Rect>> clipStack_{};
};

template <typename Sink>
void paintTextAt(Sink& sink, const std::string& text, const TextStyle& style,
                 Offset origin) {
    if (text.empty()) {
        return;
    }
    sink.drawText(TextRun{text, origin}, style);
}

// Clips leaf content to the node rect so overflowing text (long field
// content, narrow overrides) cannot bleed over neighbors.
template <typename Sink>
struct ScopedClip {
    Sink& sink;
    ScopedClip(Sink& s, Rect rect) : sink(s) {
        sink.save();
        sink.clipRect(rect);
    }
    ~ScopedClip() { sink.restore(); }
    ScopedClip(const ScopedClip&) = delete;
    ScopedClip& operator=(const ScopedClip&) = delete;
};

template <typename Sink>
void paintNode(Sink& sink, const RenderNode& node, Offset absolute,
               const PaintOptions& options) {
    const Offset origin = absolute + node.offset;
    const Rect rect{origin, node.size};

    switch (node.type) {
        case WidgetType::Container:
        case WidgetType::Row:
        case WidgetType::Column:
        case WidgetType::Stack:
            if (node.color.a > 0) {
                sink.drawRect(rect, node.color, node.radius);
            }
            break;
        case WidgetType::Button: {
            const bool pressed =
                !options.pressedIdentity.empty() &&
                options.pressedIdentity == node.identity;
            sink.drawRect(rect, pressed ? kButtonPressed : kButtonBackground,
                          CornerRadius::all(6.0F));
            const TextStyle style = contentStyle(node.textStyle, kButtonLabel);
            const float fontSize =
                style.fontSize > 0.0F ? style.fontSize : 14.0F;
            const float lineHeight = fontSize * 1.2F;
            const ScopedClip<Sink> clip{sink, rect};
            paintTextAt(sink, node.text, style,
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
            sink.drawRect(rect,
                          focused ? kFieldFocused : kFieldBackground,
                          CornerRadius::all(4.0F));
            const TextStyle style = contentStyle(node.textStyle, kContentText);
            const float fontSize =
                style.fontSize > 0.0F ? style.fontSize : 14.0F;
            const float lineHeight = fontSize * 1.2F;
            const Offset textOrigin{
                origin.x + 8.0F,
                origin.y + (node.size.height - lineHeight) * 0.5F};
            const ScopedClip<Sink> clip{sink, rect};
            if (!node.text.empty()) {
                paintTextAt(sink, node.text, style, textOrigin);
            } else if (!node.placeholder.empty()) {
                TextStyle placeholderStyle = style;
                placeholderStyle.color = kPlaceholderText;
                paintTextAt(sink, node.placeholder, placeholderStyle,
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
                sink.drawRect(Rect{Offset{caretX, textOrigin.y},
                                   Size{caretWidth, lineHeight}},
                              caretColor);
            }
            break;
        }
        case WidgetType::Text: {
            const ScopedClip<Sink> clip{sink, rect};
            paintTextAt(sink, node.text,
                        contentStyle(node.textStyle, kContentText),
                        origin + Offset{node.padding.left, node.padding.top});
            break;
        }
    }

    for (const auto& child : node.children) {
        paintNode(sink, child, origin, options);
    }
}

}  // namespace

void paintScene(Renderer& renderer, const core::RenderNode& root,
                const PaintOptions& options) {
    paintNode(renderer, root, Offset{}, options);
}

RenderCommandList recordScene(const core::RenderNode& root,
                              const PaintOptions& options) {
    RenderCommandList list;
    CommandRecorder recorder{list};
    paintNode(recorder, root, Offset{}, options);
    return list;
}

}  // namespace lumen::render
