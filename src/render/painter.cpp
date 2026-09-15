#include "lumen/render/painter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "lumen/core/icon_id.h"
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
// 标/选区/绘制宽度不会漂移）。M1：经作用域字体源读取，默认占位。
thread_local const text::FontManager* t_paintFonts = nullptr;

const text::FontManager& paintFonts() {
    return t_paintFonts != nullptr
               ? *t_paintFonts
               : text::PlaceholderFontManager::shared();
}

struct ScopedPaintFonts {
    const text::FontManager* previous{nullptr};
    explicit ScopedPaintFonts(const text::FontManager& fonts) {
        previous = t_paintFonts;
        t_paintFonts = &fonts;
    }
    ~ScopedPaintFonts() { t_paintFonts = previous; }
    ScopedPaintFonts(const ScopedPaintFonts&) = delete;
    ScopedPaintFonts& operator=(const ScopedPaintFonts&) = delete;
};

text::TextLayoutResult layoutText(const std::string& text,
                                  const TextStyle& style, float maxWidth) {
    static thread_local text::TextLayoutCache cache;
    return cache.compute(text, style, maxWidth, paintFonts());
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
    void drawRectStroke(Rect rect, Color color, CornerRadius radius,
                        float width) {
        list_.drawRectStroke(rect, color, radius, width);
    }
    void drawText(TextRun run, TextStyle style) {
        list_.drawText(std::move(run), style, currentClip_);
    }
    void drawImage(ImageId id, Rect destination) {
        list_.drawImage(id, destination);
    }
    void drawIcon(std::vector<std::vector<Offset>> polylines, Rect box,
                  Color color, float strokeWidth) {
        list_.drawIcon(std::move(polylines), box, color, strokeWidth,
                       currentClip_);
    }
    void drawShadow(Rect elevatedBox, Color color, Offset offset,
                    float blur) {
        list_.drawShadow(elevatedBox, color, offset, blur, currentClip_);
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
void paintLines(Sink& sink, const text::TextLayoutResult& layout,
                const TextStyle& style, Offset origin);

template <typename Sink>
void paintTextAt(Sink& sink, const std::string& text, const TextStyle& style,
                 Offset origin) {
    if (text.empty()) {
        return;
    }
    // M1：单行标签也携带布局 shaped 数据（与正文同一份字体事实；
    // 无 shaping 能力的后端忽略并回退自身路径）。
    const auto layout = layoutText(text, style, 0.0F);
    paintLines(sink, layout, style, origin);
}

// 多行文本绘制：逐行发 TextRun（视觉序文本 + shaped runs，RTL/混合
// 已按双向重排；baseline/字形 xOffset 均出自同一份 TextLayout）。
template <typename Sink>
void paintLines(Sink& sink, const text::TextLayoutResult& layout,
                const TextStyle& style, Offset origin) {
    for (std::size_t i = 0; i < layout.lines.size(); ++i) {
        const auto& line = layout.lines[i];
        if (line.visual.empty()) {
            continue;
        }
        TextRun run;
        run.text = line.visual;
        run.origin = Offset{origin.x,
                            origin.y + static_cast<float>(i) *
                                           layout.lineHeightPx};
        run.baselinePx = layout.baseline;
        run.shapedRuns.reserve(line.runs.size());
        for (const auto& shaped : line.runs) {
            TextGlyphRun glyphRun;
            glyphRun.family = shaped.family;
            glyphRun.placeholder = shaped.placeholder;
            glyphRun.glyphs = shaped.glyphs;
            run.shapedRuns.push_back(std::move(glyphRun));
        }
        sink.drawText(std::move(run), style);
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

// 控件表面 + 焦点环 + 边框（visual-system §7/§11；S1 §9.2 描边命令）。
//
// damage 不变量：控件的所有绘制都落在节点矩形内。焦点环与边框使用描
// 边命令绘制（环带贴外缘，内部不再被边框/焦点色填充），透明背景的
// Outline/Ghost/Tooltip 因此保持真透明；背景按剩余内缩区域填充。
template <typename Sink>
void paintControlSurface(Sink& sink, const Rect& rect,
                         const CommonResolvedStyle& common) {
    if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
        sink.drawRectStroke(rect, common.focusRing, common.radius,
                            common.focusWidth);
    }
    Rect contentRect = rect;
    float radiusShrink = 0.0F;
    if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
        const float w = common.focusWidth;
        contentRect =
            Rect{Offset{rect.origin.x + w, rect.origin.y + w},
                 Size{std::max(0.0F, rect.size.width - 2.0F * w),
                      std::max(0.0F, rect.size.height - 2.0F * w)}};
        radiusShrink = w;
    }
    if (common.borderWidth > 0.0F && common.border.a > 0) {
        sink.drawRectStroke(contentRect, common.border,
                            insetCorners(common.radius, radiusShrink),
                            common.borderWidth);
        const float inset = common.borderWidth;
        const Size innerSize{
            std::max(0.0F, contentRect.size.width - 2.0F * inset),
            std::max(0.0F, contentRect.size.height - 2.0F * inset)};
        if (innerSize.width > 0.0F && innerSize.height > 0.0F &&
            common.background.a > 0) {
            sink.drawRect(
                Rect{Offset{contentRect.origin.x + inset,
                            contentRect.origin.y + inset},
                     innerSize},
                common.background,
                insetCorners(common.radius, radiusShrink + inset));
        }
        return;
    }
    if (common.background.a > 0) {
        sink.drawRect(contentRect, common.background,
                      insetCorners(common.radius, radiusShrink));
    }
}

// 容器表面（无焦点环；卡片/页面背景）。边框同样走描边命令，透明背景
// 不被边框色填充（§9.2）。
template <typename Sink>
void paintSurface(Sink& sink, const Rect& rect,
                  const CommonResolvedStyle& common) {
    if (common.borderWidth > 0.0F && common.border.a > 0) {
        sink.drawRectStroke(rect, common.border, common.radius,
                            common.borderWidth);
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
                    const PaintOptions& options) {    const Rect rect{origin, node.size};
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
               const PaintOptions& options, float parentAlpha = 1.0F) {
    // M10：整节点透明度（transitionAlpha 转场通道；子树继承父 alpha）。
    // 全透明子树不产生命令；alpha<1 时对整份 resolved style 颜色缩放，
    // CPU/Skia/GPU 消费同一份命令数据。已知限制：DrawImage 无颜色通道，
    // 位图不参与透明度。
    const float nodeAlpha =
        std::clamp(parentAlpha * node.transitionAlpha, 0.0F, 1.0F);
    if (nodeAlpha <= 0.0F) {
        return;
    }
    const Offset origin = absolute + node.offset;
    const Rect rect{origin, node.size};
    std::optional<core::ResolvedStyle> fadedStyle;
    if (nodeAlpha < 1.0F) {
        fadedStyle = node.style;
        core::scaleStyleColors(*fadedStyle, nodeAlpha);
    }
    const core::ResolvedStyle& styleSource =
        fadedStyle.has_value() ? *fadedStyle : node.style;
    const CommonResolvedStyle& common = core::commonStyle(styleSource);

    // M6：层级阴影（布局期折算参数；命令一致地发往所有后端——CPU 的
    // 降级由后端决定）。
    if (node.elevation > 0.0F && node.shadowColor.a > 0) {
        sink.drawShadow(rect, core::scaleColorAlpha(node.shadowColor, nodeAlpha),
                        node.shadowOffset, node.shadowBlur);
    }

    switch (node.type) {
        case WidgetType::Container:
        case WidgetType::Row:
        case WidgetType::Column:
        case WidgetType::Stack:
        case WidgetType::FocusScope:
        case WidgetType::ScrollView:
        case WidgetType::ListView:
        case WidgetType::VirtualList:  // M3：滚动视口同源绘制（裁剪/表面）
        case WidgetType::Tabs:
        case WidgetType::ThemeScope:
            paintSurface(sink, rect, common);
            break;
        case WidgetType::Grid:
            paintSurface(sink, rect, common);
            break;
        case WidgetType::Image: {
            // M3：已就绪 → 位图拉满盒子（与 DrawImage 语义一致）；
            // 未就绪 → 固定占位（表面 + 边框 + 中心叉），语义名称保留。
            paintSurface(sink, rect, common);
            if (node.imageId != 0) {
                sink.drawImage(node.imageId, rect);
            } else {
                const Color placeholderFill =
                    common.background.a > 0 ? common.background
                                            : Color::fromRGBA(39, 39, 42);
                sink.drawRect(rect, placeholderFill);
                const Color border = Color::fromRGBA(82, 82, 91);
                const float thickness = std::max(1.0F, node.size.height * 0.04F);
                // 边框（四条薄矩形）+ 中心叉（占位语义，确定性几何）。
                sink.drawRect(Rect{origin, Size{node.size.width, thickness}},
                              border);
                sink.drawRect(Rect{Offset{origin.x,
                                          origin.y + node.size.height -
                                              thickness},
                                   Size{node.size.width, thickness}},
                              border);
                sink.drawRect(Rect{origin, Size{thickness, node.size.height}},
                              border);
                sink.drawRect(Rect{Offset{origin.x + node.size.width -
                                              thickness,
                                          origin.y},
                                   Size{thickness, node.size.height}},
                              border);
                const float arm = std::min(node.size.width, node.size.height) *
                                  0.25F;
                if (arm > 1.0F) {
                    const float cx = origin.x + node.size.width * 0.5F;
                    const float cy = origin.y + node.size.height * 0.5F;
                    const float ct = std::max(1.0F, thickness);
                    sink.drawRect(
                        Rect{Offset{cx - arm * 0.5F, cy - ct * 0.5F},
                             Size{arm, ct}},
                        border);
                    sink.drawRect(
                        Rect{Offset{cx - ct * 0.5F, cy - arm * 0.5F},
                             Size{ct, arm}},
                        border);
                }
            }
            break;
        }
        case WidgetType::Slider:
        case WidgetType::ProgressBar: {
            // M6：轨道 + 填充/滑块（0..100；bind 控件经 text，未绑定用
            // value；夹取 [0,100]）。色由前景派生（token 链）。
            const std::string& raw = node.text;
            const float position = std::clamp(
                static_cast<float>(std::atoi(raw.c_str())), 0.0F, 100.0F);
            const float trackHeight =
                std::max(8.0F, node.size.height * 0.35F);
            const float trackY =
                origin.y + (node.size.height - trackHeight) * 0.5F;
            Color track = common.foreground;
            track.a = static_cast<std::uint8_t>(
                std::lround(static_cast<float>(track.a) * 0.25F));
            Color fill = common.foreground;
            fill.a = static_cast<std::uint8_t>(
                std::lround(static_cast<float>(fill.a) * 0.75F));
            const float fillWidth =
                node.size.width * position / 100.0F;
            sink.drawRect(Rect{Offset{origin.x, trackY},
                               Size{node.size.width, trackHeight}},
                          track, CornerRadius::all(trackHeight * 0.5F));
            if (node.type == WidgetType::ProgressBar) {
                if (fillWidth > 0.5F) {
                    sink.drawRect(
                        Rect{Offset{origin.x, trackY},
                             Size{fillWidth, trackHeight}}, fill,
                        CornerRadius::all(trackHeight * 0.5F));
                }
            } else {
                // Slider：填充到 thumb + 圆形 thumb。
                const float thumb = trackHeight * 1.6F;
                const float thumbX =
                    origin.x + fillWidth - thumb * 0.5F;
                if (fillWidth > 0.5F) {
                    sink.drawRect(
                        Rect{Offset{origin.x, trackY},
                             Size{std::max(0.0F, thumbX - origin.x),
                                  trackHeight}},
                        fill, CornerRadius::all(trackHeight * 0.5F));
                }
                sink.drawRect(
                    Rect{Offset{std::max(origin.x, thumbX),
                                origin.y +
                                    (node.size.height - thumb) * 0.5F},
                         Size{thumb, thumb}},
                    common.foreground, CornerRadius::all(thumb * 0.5F));
            }
            break;
        }
        case WidgetType::Radio: {
            // M6：圆形指示 + 标签（选中画内点）。
            const auto* checkbox =
                std::get_if<core::CheckboxResolvedStyle>(
                    &styleSource.component);
            const float indicator =
                checkbox != nullptr ? checkbox->indicatorSize : 16.0F;
            const float cy = origin.y + (node.size.height - indicator) *
                                            0.5F;
            Rect ring{Offset{origin.x, cy}, Size{indicator, indicator}};
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                sink.drawRectStroke(ring, common.focusRing,
                                    CornerRadius::all(indicator),
                                    common.focusWidth);
                const float w = common.focusWidth;
                ring = Rect{Offset{ring.origin.x + w, ring.origin.y + w},
                            Size{indicator - 2.0F * w,
                                 indicator - 2.0F * w}};
            }
            sink.drawRect(ring, common.foreground,
                          CornerRadius::all(indicator * 0.5F));
            if (node.checked || node.selected) {
                const float inset = indicator * 0.28F;
                sink.drawRect(
                    Rect{Offset{ring.origin.x + inset,
                                ring.origin.y + inset},
                         Size{indicator - 2.0F * inset,
                              indicator - 2.0F * inset}},
                    common.foreground,
                    CornerRadius::all((indicator - 2.0F * inset) * 0.5F));
            }
            if (!node.text.empty()) {
                const float lineHeight = lineHeightOf(common.text);
                paintTextAt(
                    sink, node.text, common.text,
                    Offset{origin.x + indicator +
                               (checkbox != nullptr ? checkbox->labelGap
                                                    : 8.0F),
                           origin.y + (node.size.height - lineHeight) *
                                          0.5F});
            }
            break;
        }
        case WidgetType::Tooltip: {
            // M6：提示气泡（表面 + 边框 + 文本；token 链派生色）。
            paintSurface(sink, rect, common);
            {
                const ScopedClip<Sink> clip{sink, rect};
                const float lineHeight = lineHeightOf(common.text);
                paintTextAt(sink, node.text, common.text,
                            Offset{origin.x + 6.0F,
                                   origin.y +
                                       (node.size.height - lineHeight) *
                                           0.5F});
            }
            break;
        }
        case WidgetType::Dropdown: {
            // M11：值行（按钮表面 + 文本 + ChevronDown 图标）；收起叶子，
            // 选项经框架级 overlay 浮动菜单呈现（不再内嵌展开）。
            const float lineHeight = lineHeightOf(common.text);
            const Rect valueRow{origin, node.size};
            paintControlSurface(sink, valueRow, common);
            {
                const ScopedClip<Sink> clip{sink, valueRow};
                paintTextAt(sink, node.text, common.text,
                            Offset{origin.x + 8.0F, origin.y});
                const float iconSize = lineHeight;
                const auto& polylines =
                    core::iconPolylines(core::IconId::ChevronDown);
                if (!polylines.empty()) {
                    sink.drawIcon(
                        polylines,
                        Rect{Offset{origin.x + node.size.width -
                                        iconSize - 6.0F,
                                    origin.y},
                             Size{iconSize, iconSize}},
                        common.foreground, node.iconStrokeWidth);
                }
            }
            break;
        }
        case WidgetType::Icon: {
            // M6：矢量图标（语义 ID → 折线目录；颜色继承前景、线宽默认
            // token，风格可用 textStyle 覆盖）。装饰性：无语义标签不进
            // 语义树（semantics 跳过空 label 的 Icon）。
            const auto iconId = static_cast<core::IconId>(node.icon);
            if (iconId != core::IconId::None) {
                const std::vector<std::vector<Offset>>& polylines =
                    core::iconPolylines(iconId);
                if (!polylines.empty()) {
                    sink.drawIcon(polylines, rect, common.foreground,
                                  node.iconStrokeWidth);
                }
            }
            break;
        }
        case WidgetType::Button: {
            paintControlSurface(sink, rect, common);
            const float width = textWidth(node.text, common.text);
            const float lineHeight = lineHeightOf(common.text);
            const auto buttonIcon =
                static_cast<core::IconId>(node.icon);
            const float iconSize = common.text.fontSize > 0.0F
                                       ? common.text.fontSize
                                       : 16.0F;
            const float iconGap =
                buttonIcon != core::IconId::None && !node.text.empty()
                    ? iconSize * 0.35F
                    : 0.0F;
            const float totalWidth =
                width + (buttonIcon != core::IconId::None
                             ? iconSize + iconGap
                             : 0.0F);
            const ScopedClip<Sink> clip{sink, rect};
            paintTextAt(sink, node.text, common.text,
                        Offset{origin.x + (node.size.width - totalWidth) *
                                               0.5F,
                               origin.y + (node.size.height - lineHeight) *
                                              0.5F});
            if (buttonIcon != core::IconId::None) {
                const auto& polylines = core::iconPolylines(buttonIcon);
                if (!polylines.empty()) {
                    const float top = origin.y +
                                      (node.size.height - iconSize) * 0.5F;
                    sink.drawIcon(
                        polylines,
                        Rect{Offset{origin.x +
                                        (node.size.width - totalWidth) *
                                            0.5F +
                                        width + iconGap,
                                    top},
                             Size{iconSize, iconSize}},
                        common.foreground, iconSize * 0.1F);
                }
            }
            break;
        }
        case WidgetType::TextField: {
            const auto* field =
                std::get_if<core::TextFieldResolvedStyle>(
                    &styleSource.component);
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
                std::get_if<core::CheckboxResolvedStyle>(&styleSource.component);
            if (checkbox == nullptr) {
                break;
            }
            const float indicator = checkbox->indicatorSize;
            Rect indicatorRect{
                Offset{origin.x,
                       origin.y + (node.size.height - indicator) * 0.5F},
                Size{indicator, indicator}};
            float indicatorRadius = checkbox->indicatorRadius;
            // 焦点环：描边命令（环带贴指示框外缘；damage 不变量：绘制
            // 不越出节点）。
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                sink.drawRectStroke(indicatorRect, common.focusRing,
                                    CornerRadius::all(indicatorRadius),
                                    common.focusWidth);
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
                std::get_if<core::SwitchResolvedStyle>(&styleSource.component);
            if (control == nullptr) {
                break;
            }
            const float trackTop =
                origin.y + (node.size.height - control->trackHeight) * 0.5F;
            Rect trackRect{Offset{origin.x, trackTop},
                           Size{control->trackWidth, control->trackHeight}};
            float trackRadius = control->trackHeight * 0.5F;
            // 焦点环：描边命令（环带贴轨道外缘）。
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                sink.drawRectStroke(trackRect, common.focusRing,
                                    CornerRadius::all(trackRadius),
                                    common.focusWidth);
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
            paintNode(sink, child, origin, options, nodeAlpha);
        }
        // M6：滚动条（ScrollbarTokens 厚度经布局折算；thumb 几何出自
        // scrollOffset/scrollExtent，色为前景半透明派生）。
        if (node.scrollbarThickness > 0.0F &&
            node.scrollExtent > 0.0F) {
            const float trackHeight =
                node.size.height - node.scrollbarThickness;
            const float fraction =
                node.size.height / (node.size.height + node.scrollExtent);
            const float thumbHeight = std::max(
                node.scrollbarThickness * 3.0F, trackHeight * fraction);
            const float scrollable = std::max(
                0.0F, trackHeight - thumbHeight);
            const float progress = node.scrollExtent > 0.0F
                                       ? node.scrollOffset /
                                             node.scrollExtent
                                       : 0.0F;
            const float thumbY =
                node.scrollbarThickness * 0.5F + progress * scrollable;
            Color thumb = common.foreground;
            thumb.a = static_cast<std::uint8_t>(std::lround(
                static_cast<float>(thumb.a) * 0.45F));
            sink.drawRect(Rect{Offset{origin.x + node.size.width -
                                          node.scrollbarThickness,
                                      origin.y + thumbY},
                               Size{node.scrollbarThickness * 0.5F,
                                    thumbHeight}},
                          thumb,
                          CornerRadius::all(node.scrollbarThickness * 0.25F));
        }
    } else {
        for (const auto& child : node.children) {
            paintNode(sink, child, origin, options, nodeAlpha);
        }
    }
}

}  // namespace

void paintScene(Renderer& renderer, const core::RenderNode& root,
                const PaintOptions& options) {
    paintNode(renderer, root, Offset{}, options);
}

void paintScene(Renderer& renderer, const core::RenderNode& root,
                const PaintOptions& options,
                const text::FontManager& fonts) {
    const ScopedPaintFonts guard{fonts};
    paintNode(renderer, root, Offset{}, options);
}

RenderCommandList recordScene(const core::RenderNode& root,
                              const PaintOptions& options) {
    RenderCommandList list;
    CommandRecorder recorder{list};
    paintNode(recorder, root, Offset{}, options);
    return list;
}

RenderCommandList recordScene(const core::RenderNode& root,
                              const PaintOptions& options,
                              const text::FontManager& fonts) {
    const ScopedPaintFonts guard{fonts};
    RenderCommandList list;
    CommandRecorder recorder{list};
    paintNode(recorder, root, Offset{}, options);
    return list;
}

}  // namespace lumen::render
