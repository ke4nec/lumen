#include "lumen/render/painter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "lumen/core/utf8.h"
#include "lumen/text/font_manager.h"
#include "lumen/text/grapheme.h"
#include "lumen/text/text_layout.h"

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
constexpr Color kSelection{70, 118, 214, 130};
constexpr Color kPreeditUnderline{160, 190, 250, 255};
constexpr float kFieldTextPadding = 8.0F;

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

// v0.3 阶段8B: 文本布局统一走 text::TextLayout（与 layout.cpp 同源，光
// 标/选区/绘制宽度不会漂移）。
text::TextLayoutResult layoutText(const std::string& text,
                                  const TextStyle& style, float maxWidth) {
    return text::TextLayout::layout(text, style, maxWidth,
                                    text::PlaceholderFontManager::shared());
}

float textWidth(const std::string& text, const TextStyle& style) {
    return layoutText(text, style, 0.0F).size.width;
}

// 密码模式显示文本：每个 grapheme 一个圆点（U+2022）。
std::string obscuredDisplay(const std::string& text) {
    const std::size_t count = text::graphemeCount(text);
    std::string display;
    display.reserve(count * 3);
    for (std::size_t i = 0; i < count; ++i) {
        display += "\xE2\x80\xA2";
    }
    return display;
}

// v0.2 阶段7B: 命令录制 sink。与 Renderer 暴露的同一组即时调用，另维护
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

// 多行文本绘制：逐行发 TextRun（视觉序文本，RTL 已逆序）。
template <typename Sink>
void paintLines(Sink& sink, const text::TextLayoutResult& layout,
                const TextStyle& style, Offset origin) {
    for (std::size_t i = 0; i < layout.lines.size(); ++i) {
        const auto& line = layout.lines[i];
        if (line.visual.empty()) {
            continue;
        }
        paintTextAt(sink, line.visual, style,
                    Offset{origin.x, origin.y +
                                          static_cast<float>(i) *
                                              layout.lineHeightPx});
    }
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

// v0.3 阶段8B: TextField 绘制。显示文本 = 文档文本（密码模式为圆点），
// preedit 插入在选区起点；选区背景、preedit 下划线与光标都按 TextLayout
// 的 grapheme 位置绘制。
template <typename Sink>
void paintTextField(Sink& sink, const RenderNode& node, Offset origin,
                    const PaintOptions& options) {
    const bool focused = !options.focusedIdentity.empty() &&
                         options.focusedIdentity == node.identity;
    const Rect rect{origin, node.size};
    sink.drawRect(rect, focused ? kFieldFocused : kFieldBackground,
                  CornerRadius::all(4.0F));
    const TextStyle style = contentStyle(node.textStyle, kContentText);
    const float availableWidth =
        std::max(0.0F, node.size.width - 2.0F * kFieldTextPadding);
    const ScopedClip<Sink> clip{sink, rect};

    const bool showingPlaceholder =
        node.text.empty() && !node.placeholder.empty();

    // 显示文本组装：preedit 插入在光标前方（composing 期间 selection 折叠
    // 在 preedit 之后）。
    std::string display = node.obscure
                              ? obscuredDisplay(node.text)
                              : (showingPlaceholder ? node.placeholder
                                                    : node.text);
    const std::size_t compositionGraphemes =
        focused && !options.composition.empty() && !showingPlaceholder
            ? text::graphemeCount(options.composition)
            : 0;
    const std::size_t insertAt = options.selectionStart >= compositionGraphemes
                                     ? options.selectionStart -
                                           compositionGraphemes
                                     : 0;
    if (compositionGraphemes > 0) {
        const std::string preedit = node.obscure
                                        ? obscuredDisplay(options.composition)
                                        : options.composition;
        display = text::graphemeSubstring(display, 0, insertAt) + preedit +
                  text::graphemeSubstring(
                      display, insertAt,
                      text::graphemeCount(display));
    }

    TextStyle layoutStyle = style;
    if (!node.multiline) {
        layoutStyle.maxLines = 1;
    }
    const auto layout = layoutText(
        display, layoutStyle, node.multiline ? availableWidth : 0.0F);
    const Offset textOrigin{
        origin.x + kFieldTextPadding,
        origin.y + (node.size.height - layout.size.height) * 0.5F};

    if (focused && options.hasSelection && !showingPlaceholder) {
        // 选区背景：按行绘制选区覆盖的区间。
        const std::size_t selectionStart = options.selectionStart;
        const std::size_t selectionEnd = options.selectionEnd;
        for (std::size_t i = 0; i < layout.lines.size(); ++i) {
            const auto& line = layout.lines[i];
            const std::size_t lineStart = line.startGrapheme;
            const std::size_t lineEnd = lineStart + line.graphemeCount;
            if (lineStart >= selectionEnd || lineEnd <= selectionStart) {
                continue;
            }
            const std::size_t from =
                std::max(lineStart, selectionStart) - lineStart;
            const std::size_t to =
                std::min(lineEnd, selectionEnd) - lineStart;
            const float x0 = line.graphemeX[from];
            const float x1 = line.graphemeX[to];
            const float left = std::min(x0, x1);
            const float width = std::abs(x1 - x0);
            sink.drawRect(
                Rect{Offset{textOrigin.x + left,
                            textOrigin.y + static_cast<float>(i) *
                                               layout.lineHeightPx},
                     Size{width, layout.lineHeightPx}},
                kSelection);
        }
    }

    TextStyle contentPaintStyle = style;
    if (showingPlaceholder) {
        contentPaintStyle.color = kPlaceholderText;
    }
    paintLines(sink, layout, contentPaintStyle, textOrigin);

    if (compositionGraphemes > 0) {
        // preedit 下划线。
        std::size_t lineIndex = 0;
        const float x0 = layout.graphemeToX(insertAt, &lineIndex);
        const float x1 =
            layout.graphemeToX(insertAt + compositionGraphemes, nullptr);
        const float left = std::min(x0, x1);
        const float width = std::abs(x1 - x0);
        sink.drawRect(
            Rect{Offset{textOrigin.x + left,
                        textOrigin.y + (static_cast<float>(lineIndex) + 1.0F) *
                                           layout.lineHeightPx -
                               1.5F},
                 Size{width, 1.5F}},
            kPreeditUnderline);
    }

    if (focused) {
        // 光标：显示文本中的 grapheme 位置（preedit 已计入）。
        std::size_t lineIndex = 0;
        const float caretOffset =
            layout.graphemeToX(options.caretGraphemes, &lineIndex);
        const float caretX =
            textOrigin.x + caretOffset + 0.5F;
        const float caretWidth = std::max(1.5F, style.fontSize * 0.08F);
        const float alpha = std::clamp(options.caretAlpha, 0.0F, 1.0F);
        if (alpha <= 0.0F) {
            return;
        }
        Color caretColor = kCaret;
        caretColor.a =
            static_cast<std::uint8_t>(std::lround(caretColor.a * alpha));
        sink.drawRect(
            Rect{Offset{caretX, textOrigin.y +
                                    static_cast<float>(lineIndex) *
                                        layout.lineHeightPx},
                 Size{caretWidth, layout.lineHeightPx}},
            caretColor);
    }
}

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
            const float width = textWidth(node.text, style);
            const float lineHeight = style.fontSize > 0.0F
                                         ? style.fontSize * 1.2F
                                         : 16.8F;
            const ScopedClip<Sink> clip{sink, rect};
            paintTextAt(sink, node.text, style,
                        Offset{origin.x + (node.size.width - width) * 0.5F,
                               origin.y + (node.size.height - lineHeight) *
                                              0.5F});
            break;
        }
        case WidgetType::TextField:
            paintTextField(sink, node, origin, options);
            break;
        case WidgetType::Text: {
            const ScopedClip<Sink> clip{sink, rect};
            const TextStyle style =
                contentStyle(node.textStyle, kContentText);
            const bool wrap =
                node.multiline || node.textStyle.maxLines != 1;
            const auto layout =
                layoutText(node.text, style, wrap ? node.size.width : 0.0F);
            paintLines(sink, layout, style,
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
