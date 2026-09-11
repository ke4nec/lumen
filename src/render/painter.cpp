#include "lumen/render/painter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "lumen/core/utf8.h"
#include "lumen/text/font_manager.h"
#include "lumen/text/grapheme.h"
#include "lumen/text/text_layout.h"

namespace lumen::render {
namespace {

using core::Color;
using core::CommonResolvedStyle;
using core::CornerRadius;
using core::Offset;
using core::RenderNode;
using core::Rect;
using core::Size;
using core::TextStyle;
using core::WidgetType;

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

float lineHeightOf(const TextStyle& style) {
    return style.fontSize > 0.0F ? style.fontSize * 1.2F : 16.8F;
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

CornerRadius insetCorners(const CornerRadius& radius, float inset) {
    const auto clampCorner = [inset](float value) {
        return std::max(0.0F, value - inset);
    };
    return CornerRadius{clampCorner(radius.topLeft),
                        clampCorner(radius.topRight),
                        clampCorner(radius.bottomLeft),
                        clampCorner(radius.bottomRight)};
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

// 控件表面 + 焦点环 + 边框（visual-system §7/§11）。
//
// damage 不变量：控件的所有绘制都落在节点矩形内。焦点环因此内嵌绘制
//（环 = 节点矩形外圈，表面按 focusWidth 内缩），不影响布局尺寸也不产
// 生矩形外的脏像素。只支持填充矩形的渲染契约下边框用双层绘制表达
//（外层边框色，内层背景色按 borderWidth 内缩）。
template <typename Sink>
void paintControlSurface(Sink& sink, const Rect& rect,
                         const CommonResolvedStyle& common) {
    Rect surfaceRect = rect;
    float radiusShrink = 0.0F;
    if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
        sink.drawRect(rect, common.focusRing, common.radius);
        const float w = common.focusWidth;
        surfaceRect =
            Rect{Offset{rect.origin.x + w, rect.origin.y + w},
                 Size{std::max(0.0F, rect.size.width - 2.0F * w),
                      std::max(0.0F, rect.size.height - 2.0F * w)}};
        radiusShrink = w;
    }
    const CornerRadius surfaceRadius =
        insetCorners(common.radius, radiusShrink);
    if (common.borderWidth > 0.0F && common.border.a > 0) {
        sink.drawRect(surfaceRect, common.border, surfaceRadius);
        const float inset = common.borderWidth;
        const Size innerSize{
            std::max(0.0F, surfaceRect.size.width - 2.0F * inset),
            std::max(0.0F, surfaceRect.size.height - 2.0F * inset)};
        if (innerSize.width > 0.0F && innerSize.height > 0.0F &&
            common.background.a > 0) {
            sink.drawRect(
                Rect{Offset{surfaceRect.origin.x + inset,
                            surfaceRect.origin.y + inset},
                     innerSize},
                common.background,
                insetCorners(surfaceRadius, inset));
        }
        return;
    }
    if (common.background.a > 0) {
        sink.drawRect(surfaceRect, common.background, surfaceRadius);
    }
}

// 容器表面（无焦点环；卡片/页面背景）。
template <typename Sink>
void paintSurface(Sink& sink, const Rect& rect,
                  const CommonResolvedStyle& common) {
    if (common.borderWidth > 0.0F && common.border.a > 0) {
        sink.drawRect(rect, common.border, common.radius);
        const float inset = common.borderWidth;
        const Size innerSize{
            std::max(0.0F, rect.size.width - 2.0F * inset),
            std::max(0.0F, rect.size.height - 2.0F * inset)};
        if (innerSize.width > 0.0F && innerSize.height > 0.0F &&
            common.background.a > 0) {
            sink.drawRect(
                Rect{Offset{rect.origin.x + inset, rect.origin.y + inset},
                     innerSize},
                common.background, insetCorners(common.radius, inset));
        }
        return;
    }
    if (common.background.a > 0) {
        sink.drawRect(rect, common.background, common.radius);
    }
}

// v0.3 阶段8B + 视觉系统: TextField 绘制。显示文本 = 文档文本（密码模
// 式为圆点），preedit 插入在选区起点；选区背景、preedit 下划线与光标都
// 按 TextLayout 的 grapheme 位置绘制。chrome（背景/边框/焦点环/padding/
// 颜色）全部来自 resolved style。
template <typename Sink>
void paintTextField(Sink& sink, const RenderNode& node, Offset origin,
                    const core::TextFieldResolvedStyle& field,
                    const PaintOptions& options) {
    const Rect rect{origin, node.size};
    const CommonResolvedStyle& common = field.common;
    const TextStyle& style = common.text;
    paintControlSurface(sink, rect, field.common);
    const float padX = common.padding.left;
    const float availableWidth =
        std::max(0.0F, node.size.width - common.padding.horizontal());
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
        field.focused && !options.composition.empty() && !showingPlaceholder
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
        origin.x + padX,
        origin.y + (node.size.height - layout.size.height) * 0.5F};

    if (field.focused && options.hasSelection && !showingPlaceholder) {
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
                common.selection);
        }
    }

    TextStyle contentPaintStyle = style;
    if (showingPlaceholder) {
        contentPaintStyle.color = field.placeholder;
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
            field.preeditUnderline);
    }

    if (field.focused) {
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
        Color caretColor = field.caret;
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
    const CommonResolvedStyle& common = node.commonStyle();

    switch (node.type) {
        case WidgetType::Container:
        case WidgetType::Row:
        case WidgetType::Column:
        case WidgetType::Stack:
        case WidgetType::FocusScope:
        case WidgetType::ScrollView:
        case WidgetType::ListView:
            paintSurface(sink, rect, common);
            break;
        case WidgetType::Button: {
            paintControlSurface(sink, rect, common);
            const float width = textWidth(node.text, common.text);
            const float lineHeight = lineHeightOf(common.text);
            const ScopedClip<Sink> clip{sink, rect};
            paintTextAt(sink, node.text, common.text,
                        Offset{origin.x + (node.size.width - width) * 0.5F,
                               origin.y + (node.size.height - lineHeight) *
                                              0.5F});
            break;
        }
        case WidgetType::TextField: {
            const auto* field =
                std::get_if<core::TextFieldResolvedStyle>(&node.style.component);
            if (field != nullptr) {
                paintTextField(sink, node, origin, *field, options);
            }
            break;
        }
        case WidgetType::Text: {
            const ScopedClip<Sink> clip{sink, rect};
            const bool wrap =
                node.multiline || common.text.maxLines != 1;
            const auto layout =
                layoutText(node.text, common.text, wrap ? node.size.width : 0.0F);
            paintLines(sink, layout, common.text,
                       origin + Offset{node.padding.left, node.padding.top});
            break;
        }
        case WidgetType::Checkbox: {
            // 指示框 + 选中填充；标签绘制在右侧，垂直居中。几何全部来自
            // resolved token（布局度量同源，visual-system §7.3）。
            const auto* checkbox =
                std::get_if<core::CheckboxResolvedStyle>(&node.style.component);
            if (checkbox == nullptr) {
                break;
            }
            const float indicator = checkbox->indicatorSize;
            Rect indicatorRect{
                Offset{origin.x,
                       origin.y + (node.size.height - indicator) * 0.5F},
                Size{indicator, indicator}};
            float indicatorRadius = checkbox->indicatorRadius;
            // 焦点环内嵌在指示框外圈（damage 不变量：绘制不越出节点）。
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                sink.drawRect(indicatorRect, common.focusRing,
                              CornerRadius::all(indicatorRadius));
                const float w = common.focusWidth;
                indicatorRect =
                    Rect{Offset{indicatorRect.origin.x + w,
                                indicatorRect.origin.y + w},
                         Size{indicator - 2.0F * w, indicator - 2.0F * w}};
                indicatorRadius = std::max(0.0F, indicatorRadius - w);
            }
            sink.drawRect(indicatorRect,
                          checkbox->checked ? checkbox->indicatorChecked
                                            : checkbox->indicator,
                          CornerRadius::all(indicatorRadius));
            if (checkbox->checked) {
                const float inset = checkbox->markInset;
                sink.drawRect(
                    Rect{Offset{indicatorRect.origin.x + inset,
                                indicatorRect.origin.y + inset},
                         Size{indicatorRect.size.width - 2.0F * inset,
                              indicatorRect.size.height - 2.0F * inset}},
                    checkbox->mark, CornerRadius::all(checkbox->markRadius));
            }
            if (!node.text.empty()) {
                const float lineHeight = lineHeightOf(common.text);
                paintTextAt(sink, node.text, common.text,
                            Offset{origin.x + indicator + checkbox->labelGap,
                                   origin.y + (node.size.height - lineHeight) *
                                                  0.5F});
            }
            break;
        }
        case WidgetType::Switch: {
            // 轨道（pill 圆角 = 高度一半）+ 滑块；标签绘制在右侧。
            const auto* control =
                std::get_if<core::SwitchResolvedStyle>(&node.style.component);
            if (control == nullptr) {
                break;
            }
            const float trackTop =
                origin.y + (node.size.height - control->trackHeight) * 0.5F;
            Rect trackRect{Offset{origin.x, trackTop},
                           Size{control->trackWidth, control->trackHeight}};
            float trackRadius = control->trackHeight * 0.5F;
            // 焦点环内嵌在轨道外圈。
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                sink.drawRect(trackRect, common.focusRing,
                              CornerRadius::all(trackRadius));
                const float w = common.focusWidth;
                trackRect = Rect{
                    Offset{trackRect.origin.x + w, trackRect.origin.y + w},
                    Size{control->trackWidth - 2.0F * w,
                         control->trackHeight - 2.0F * w}};
                trackRadius = std::max(0.0F, trackRadius - w);
            }
            sink.drawRect(trackRect,
                          control->checked ? control->trackOn
                                           : control->trackOff,
                          CornerRadius::all(trackRadius));
            const float knobX =
                control->checked
                    ? trackRect.origin.x + trackRect.size.width -
                          control->knobInset - control->knobSize
                    : trackRect.origin.x + control->knobInset;
            sink.drawRect(
                Rect{Offset{knobX,
                            trackRect.origin.y +
                                (trackRect.size.height - control->knobSize) *
                                    0.5F},
                     Size{control->knobSize, control->knobSize}},
                control->knob, CornerRadius::all(control->knobSize * 0.5F));
            if (!node.text.empty()) {
                const float lineHeight = lineHeightOf(common.text);
                paintTextAt(sink, node.text, common.text,
                            Offset{origin.x + control->trackWidth +
                                       control->labelGap,
                                   origin.y + (node.size.height - lineHeight) *
                                                  0.5F});
            }
            break;
        }
    }

    // 滚动视口裁剪：子内容不得溢出 viewport（overflow clip，plan §3.4）。
    if (node.clipContent) {
        const ScopedClip<Sink> clip{sink, rect};
        for (const auto& child : node.children) {
            paintNode(sink, child, origin, options);
        }
    } else {
        for (const auto& child : node.children) {
            paintNode(sink, child, origin, options);
        }
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
