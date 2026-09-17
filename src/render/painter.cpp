#include "lumen/render/painter.h"

#include "lumen/core/text_field.h"

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

float lineHeightOf(const TextStyle& style) {
    return style.fontSize > 0.0F
               ? style.fontSize * (style.lineHeight > 0 ? style.lineHeight : 1.2F)
               : 16.8F;
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
        // §6.1 隔离带：不透明填充与环色接近时，环内侧 1 px 表面色环带。
        if (common.focusIsolation.a > 0) {
            const Rect bandRect{
                Offset{rect.origin.x + w, rect.origin.y + w},
                Size{std::max(0.0F, rect.size.width - 2.0F * w),
                     std::max(0.0F, rect.size.height - 2.0F * w)}};
            sink.drawRectStroke(bandRect, common.focusIsolation,
                                insetCorners(common.radius, w), 1.0F);
        }
        const float isolation =
            common.focusIsolation.a > 0 ? w + 1.0F : w;
        contentRect =
            Rect{Offset{rect.origin.x + isolation, rect.origin.y + isolation},
                 Size{std::max(0.0F, rect.size.width - 2.0F * isolation),
                      std::max(0.0F, rect.size.height - 2.0F * isolation)}};
        radiusShrink = isolation;
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
                    const PaintOptions& options) {
    const Rect rect{origin, node.size};
    const CommonResolvedStyle& common = field.common;
    const TextStyle& style = common.text;
    paintControlSurface(sink, rect, field.common);
    const ScopedClip<Sink> clip{sink, rect};
    const auto display = core::textFieldDisplay(
        node, field.focused ? options.composition : std::string{},
        options.selectionStart);
    const bool showingPlaceholder = display.showingPlaceholder;
    const auto compositionGraphemes = display.compositionLength;
    const auto insertAt = display.compositionStart;
    const auto layout = layoutText(display.text, core::textFieldLayoutStyle(node),
                                   core::textFieldWrapWidth(node));
    const Offset textOrigin = origin + core::textFieldTextOrigin(
        node, layout, options.caretGraphemes, field.focused);

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
                     Size{width, layout.lineBoxHeightPx}},
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
                        textOrigin.y + static_cast<float>(lineIndex) *
                                           layout.lineHeightPx +
                               layout.lineBoxHeightPx -
                               1.5F},
                 Size{width, 1.5F}},
            field.preeditUnderline);
    }

    if (field.focused) {
        // 光标：显示文本中的 grapheme 位置（preedit 已计入）。宽度 1
        // logical px（§6.3）。实心矩形使用像素边界；半像素偏移仅适用于
        // 居中描边，放在这里会使 CPU 后端的整数位置光标消失。
        Rect caretRect = core::textFieldCaretRect(
            node, layout, options.caretGraphemes, field.focused);
        caretRect.origin = origin + caretRect.origin;
        const float alpha = std::clamp(options.caretAlpha, 0.0F, 1.0F);
        if (alpha <= 0.0F) {
            return;
        }
        Color caretColor = field.caret;
        caretColor.a =
            static_cast<std::uint8_t>(std::lround(caretColor.a * alpha));
        sink.drawRect(caretRect, caretColor);
    }
}

template <typename Sink>
void paintNode(Sink& sink, const RenderNode& node, Offset absolute,
               const PaintOptions& options, float parentAlpha = 1.0F) {
    if (std::find(options.suppressedIdentities.begin(),
                  options.suppressedIdentities.end(), node.identity) !=
        options.suppressedIdentities.end()) {
        return;
    }
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
        case WidgetType::ThemeScope:
        case WidgetType::List:
        case WidgetType::Tree:
        case WidgetType::TreeList:
            // 集合行（collection-controls-design §10.3）：Row/Container 行
            // 走控件表面（焦点环/隔离带/内缩填充）；普通容器保持卡片表面。
            if (node.collectionRow) {
                paintControlSurface(sink, rect, common);
            } else {
                paintSurface(sink, rect, common);
            }
            break;
        case WidgetType::Tabs:
            break; // Decorations follow child surfaces/focus rings below.
        case WidgetType::Grid:
            paintSurface(sink, rect, common);
            break;
        case WidgetType::Image: {
            // S4（§6.10）：已就绪 → 位图拉满盒子（默认拉伸契约不变）；
            // 未就绪 → surfaceSunken + 1px borderDefault + 居中图片图标
            //（contentSecondary，最大 24px；chrome 全部来自 resolved
            // style，不再有 painter 局部常量）。
            if (node.imageId != 0) {
                sink.drawImage(node.imageId, rect);
            } else {
                paintSurface(sink, rect, common);
                const auto& polylines =
                    core::iconPolylines(core::IconId::Image);
                if (!polylines.empty()) {
                    const float iconBox = std::min(
                        24.0F, std::min(node.size.width,
                                        node.size.height) *
                                       0.6F);
                    if (iconBox > 1.0F) {
                        sink.drawIcon(
                            polylines,
                            Rect{Offset{origin.x +
                                            (node.size.width - iconBox) *
                                                0.5F,
                                        origin.y +
                                            (node.size.height - iconBox) *
                                                0.5F},
                                 Size{iconBox, iconBox}},
                            common.foreground, node.iconStrokeWidth);
                    }
                }
            }
            break;
        }
        case WidgetType::Slider: {
            // S3（§6.5）：细轨道（4px）+ 独立 Thumb（surfaceElevated +
            // accent 轮廓）；端点恒定预留 r+f，值 0/100 时 Thumb 与焦点
            // 环都在节点内；不足以容纳预留时轨道长度夹取为 0 并居中。
            const auto* slider =
                std::get_if<core::SliderResolvedStyle>(
                    &styleSource.component);
            if (slider == nullptr) {
                break;
            }
            const std::string& raw = node.text;
            const float position = std::clamp(
                static_cast<float>(std::atoi(raw.c_str())), 0.0F, 100.0F);
            const float r = slider->thumbDiameter * 0.5F;
            const float trackHeight = slider->trackHeight;
            const float trackY =
                origin.y + (node.size.height - trackHeight) * 0.5F;
            const float usable =
                node.size.width - 2.0F * slider->trackInset;
            const float trackStart = origin.x + slider->trackInset;
            const float trackLength = std::max(0.0F, usable);
            // 不足以容纳两侧预留时轨道长度为 0，Thumb 居中（§6.5）。
            const float centerX =
                usable > 0.0F
                    ? trackStart + trackLength * position / 100.0F
                    : origin.x + node.size.width * 0.5F;
            // 未完成/完成轨道（可用宽为 0 时只画 Thumb，居中不越界）。
            if (trackLength > 0.0F) {
                sink.drawRect(
                    Rect{Offset{trackStart, trackY},
                         Size{trackLength, trackHeight}},
                    slider->trackRemaining,
                    CornerRadius::all(trackHeight * 0.5F));
                const float fillWidth = std::max(
                    0.0F, centerX - r - trackStart);
                if (fillWidth > 0.0F) {
                    sink.drawRect(
                        Rect{Offset{trackStart, trackY},
                             Size{fillWidth, trackHeight}},
                        slider->trackActive,
                        CornerRadius::all(trackHeight * 0.5F));
                }
            }
            // Thumb：焦点环（预留区内）→ 轮廓 → 内部表面。
            const float thumbTop =
                origin.y + (node.size.height - slider->thumbDiameter) * 0.5F;
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                const float ringBox =
                    slider->thumbDiameter + 2.0F * common.focusWidth;
                const float ringTop =
                    origin.y + (node.size.height - ringBox) * 0.5F;
                sink.drawRectStroke(
                    Rect{Offset{centerX - ringBox * 0.5F, ringTop},
                         Size{ringBox, ringBox}},
                    common.focusRing, CornerRadius::all(ringBox * 0.5F),
                    common.focusWidth);
            }
            if (slider->thumbOutline.a > 0) {
                sink.drawRectStroke(
                    Rect{Offset{centerX - r, thumbTop},
                         Size{slider->thumbDiameter, slider->thumbDiameter}},
                    slider->thumbOutline, CornerRadius::all(r),
                    slider->thumbBorderWidth);
            }
            const float fillInset = slider->thumbBorderWidth;
            const float fillBox =
                std::max(0.0F, slider->thumbDiameter - 2.0F * fillInset);
            if (fillBox > 0.0F) {
                sink.drawRect(
                    Rect{
                        Offset{centerX - fillBox * 0.5F, thumbTop + fillInset},
                        Size{fillBox, fillBox}},
                    slider->thumbFill,
                    CornerRadius::all(fillBox * 0.5F));
            }
            break;
        }
        case WidgetType::ProgressBar: {
            // S3（§6.6）：高度分档（4/6/8），圆角 = 高度一半；填充不超过
            // 轨道（小于圆角直径时圆角同步夹取）。无交互状态。
            const auto* bar = std::get_if<core::ProgressBarResolvedStyle>(
                &styleSource.component);
            if (bar == nullptr) {
                break;
            }
            const std::string& raw = node.text;
            const float position = std::clamp(
                static_cast<float>(std::atoi(raw.c_str())), 0.0F, 100.0F);
            const float trackHeight = bar->trackHeight;
            const float trackY =
                origin.y + (node.size.height - trackHeight) * 0.5F;
            sink.drawRect(
                Rect{Offset{origin.x, trackY},
                     Size{node.size.width, trackHeight}},
                bar->track, CornerRadius::all(trackHeight * 0.5F));
            const float fillWidth =
                node.size.width * position / 100.0F;
            if (fillWidth > 0.5F) {
                // 小于圆角直径的填充也画不到轨道外。
                const float fillRadius =
                    std::min(trackHeight * 0.5F, fillWidth * 0.5F);
                sink.drawRect(
                    Rect{Offset{origin.x, trackY},
                         Size{std::min(fillWidth, node.size.width),
                              trackHeight}},
                    bar->fill, CornerRadius::all(fillRadius));
            }
            break;
        }
        case WidgetType::Radio: {
            // S2（§6.4）：空心外环（描边）+ 独立内点；点与环之间保留表面
            // 空隙，不用两次同色填充冒充空心环。专用 RadioResolvedStyle
            //（resolver 不再回落通用容器）。
            const auto* radio =
                std::get_if<core::RadioResolvedStyle>(&styleSource.component);
            if (radio == nullptr) {
                break;
            }

            const ScopedClip<Sink> clip{sink, rect};
            const auto label = layoutText(node.text, common.text,
                std::max(0.01F, node.size.width - radio->slotSize - radio->labelGap));
            const float firstHeight = std::max(radio->slotSize, label.lineBoxHeightPx);
            const float blockHeight = std::max(firstHeight,
                label.size.height + firstHeight - label.lineBoxHeightPx);
            const float blockTop = origin.y + (node.size.height - blockHeight) * 0.5F;
            const float indicatorCenterY = blockTop + firstHeight * 0.5F;
            const float indicator = radio->indicatorSize;
            const float indicatorOrigin =
                origin.x + (radio->slotSize - indicator) * 0.5F;
            const float cy =
                indicatorCenterY - indicator * 0.5F;
            Rect ring{Offset{indicatorOrigin, cy},
                      Size{indicator, indicator}};
            const float ringRadius = indicator * 0.5F;
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                const float outset = common.focusWidth + 1.0F;
                sink.drawRectStroke(
                    Rect{Offset{ring.origin.x - outset, ring.origin.y - outset},
                         Size{indicator + 2.0F * outset, indicator + 2.0F * outset}},
                    common.focusRing, CornerRadius::all(ringRadius + outset),
                    common.focusWidth);
            }
            // 环内表面 + 空心描边（Off=borderStrong，On=accent）。
            sink.drawRect(ring, radio->indicator,
                          CornerRadius::all(ring.size.width * 0.5F));
            sink.drawRectStroke(
                ring, radio->checked ? radio->indicatorChecked
                                     : radio->indicatorOutline,
                CornerRadius::all(ring.size.width * 0.5F), common.borderWidth);
            if (radio->checked) {
                // 独立内点（直径 = 外径 × dotRatio），与环之间保留
                // 表面空隙；相对固定的指示器盒居中。
                const float dot = indicator * radio->dotRatio;
                const float dotInset =
                    (ring.size.width - dot) * 0.5F;
                sink.drawRect(
                    Rect{Offset{ring.origin.x + dotInset,
                                ring.origin.y + dotInset},
                         Size{dot, dot}},
                    radio->dot, CornerRadius::all(dot * 0.5F));
            }
            paintLines(sink, label, common.text,
                Offset{origin.x + radio->slotSize + radio->labelGap,
                       blockTop + (firstHeight - label.lineBoxHeightPx) * 0.5F});
            break;
        }
        case WidgetType::Tooltip: {
            // S4（§6.9）：caption + surfaceElevated + borderDefault 轮廓；
            // padding 来自 resolved style，文本按节点宽换行。
            paintSurface(sink, rect, common);
            {
                const ScopedClip<Sink> clip{sink, rect};
                const bool wrap =
                    node.multiline || common.text.maxLines != 1;
                const auto layout =
                    layoutText(node.text, common.text,
                               wrap ? std::max(
                                          0.0F, node.size.width -
                                                    common.padding.horizontal())
                                    : 0.0F);
                paintLines(sink, layout, common.text,
                           origin + Offset{common.padding.left,
                                           common.padding.top});
            }
            break;
        }
        case WidgetType::Dropdown: {
            // S3（§6.7）：值行与字段同源 chrome（surfaceSunken +
            // borderStrong 轮廓、同高/圆角/padding）；尾随 Chevron 使用
            // inlineIconSize 档位并单独预留空间（不与文字重叠）；展开
            // 方向由应用经 icon 声明（默认 ChevronDown，可设 ChevronUp）。
            const auto* dropdown =
                std::get_if<core::ButtonResolvedStyle>(
                    &styleSource.component);
            const Rect valueRow{origin, node.size};
            paintControlSurface(sink, valueRow, common);
            {
                const ScopedClip<Sink> clip{sink, valueRow};
                const float chevron =
                    dropdown != nullptr ? dropdown->iconSize : 16.0F;
                const float stroke =
                    dropdown != nullptr ? dropdown->iconStroke
                                        : node.iconStrokeWidth;
                const float chevronGap =
                    dropdown != nullptr ? dropdown->iconGap : 6.0F;
                // 文本可用区 = 值行 - padding×2 - Chevron 槽位。
                const float padX = common.padding.left;
                const float textMax = std::max(
                    0.0F, node.size.width - 2.0F * padX - chevron -
                              chevronGap);
                TextStyle valueStyle = common.text;
                valueStyle.maxLines = 1;
                valueStyle.overflow = core::TextOverflow::Ellipsis;
                const auto layout =
                    node.text.empty()
                        ? text::TextLayoutResult{}
                        : layoutText(node.text, valueStyle, textMax);
                // 单行按完整绘制框居中，紧行高只控制多行步进。
                const float lineHeight = layout.lines.empty()
                                             ? lineHeightOf(common.text)
                                             : layout.lineBoxHeightPx;
                paintLines(sink, layout, common.text,
                           Offset{origin.x + padX,
                                  origin.y + (node.size.height - lineHeight) *
                                                 0.5F});
                const auto chevronId =
                    static_cast<core::IconId>(node.icon) ==
                            core::IconId::None
                        ? core::IconId::ChevronDown
                        : static_cast<core::IconId>(node.icon);
                const auto& polylines = core::iconPolylines(chevronId);
                if (!polylines.empty()) {
                    sink.drawIcon(
                        polylines,
                        Rect{Offset{origin.x + node.size.width - padX -
                                        chevron,
                                    origin.y +
                                        (node.size.height - chevron) * 0.5F},
                             Size{chevron, chevron}},
                        common.foreground, stroke);
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
            const auto buttonStyle =
                std::get_if<core::ButtonResolvedStyle>(
                    &styleSource.component);
            const float iconSize =
                buttonStyle != nullptr ? buttonStyle->iconSize : 16.0F;
            const float iconGap =
                buttonStyle != nullptr ? buttonStyle->iconGap : 6.0F;
            const float iconStroke =
                buttonStyle != nullptr ? buttonStyle->iconStroke : 1.5F;
            const auto buttonIcon =
                static_cast<core::IconId>(node.icon);
            const bool hasIcon =
                buttonIcon != core::IconId::None &&
                !core::iconPolylines(buttonIcon).empty();
            // 单行省略（§6.1）：可用宽度不足时省略，图标不盖文字。
            TextStyle labelStyle = common.text;
            labelStyle.maxLines = 1;
            labelStyle.overflow = core::TextOverflow::Ellipsis;
            const bool startAligned = buttonStyle != nullptr && buttonStyle->alignContentStart;
            const bool reserveIcon = hasIcon || (buttonStyle != nullptr && buttonStyle->reserveIconSpace);
            const float iconExtent =
                reserveIcon ? iconSize + (node.text.empty() ? 0.0F : iconGap)
                        : 0.0F;
            const float availableWidth = std::max(
                0.0F, node.size.width - iconExtent -
                          2.0F * std::max(common.padding.left,
                                          common.padding.right));
            const auto textLayout =
                node.text.empty()
                    ? text::TextLayoutResult{}
                    : layoutText(node.text, labelStyle, availableWidth);
            const float textWidth = textLayout.size.width;
            // 同 Dropdown：按完整绘制框居中。
            const float lineHeight = textLayout.lines.empty()
                                         ? lineHeightOf(common.text)
                                         : textLayout.lineBoxHeightPx;
            const float totalWidth = textWidth + iconExtent;
            const float contentTop =
                origin.y + (node.size.height - lineHeight) * 0.5F;
            const ScopedClip<Sink> clip{sink, rect};
            if (!node.text.empty()) {
                paintLines(sink, textLayout, common.text,
                           Offset{origin.x + (startAligned ? common.padding.left :
                                              (node.size.width - totalWidth) * 0.5F),
                                  contentTop});
            }
            if (hasIcon) {
                const auto& polylines = core::iconPolylines(buttonIcon);
                sink.drawIcon(
                    polylines,
                    Rect{Offset{origin.x +
                                    (startAligned ? node.size.width - common.padding.right - iconSize :
                                     (node.size.width - totalWidth) * 0.5F + textWidth +
                                         (node.text.empty() ? 0.0F : iconGap)),
                                origin.y +
                                    (node.size.height - iconSize) * 0.5F},
                         Size{iconSize, iconSize}},
                    common.foreground, iconStroke);
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
            // §6.4：Off = surfaceSunken 内部 + borderStrong 描边轮廓；
            // On = accent 填充 + onAccent 勾号（IconId::Check 折线，替代
            // 旧内方块）。指示器画在槽位内的焦点环预留区之内。
            const auto* checkbox =
                std::get_if<core::CheckboxResolvedStyle>(&styleSource.component);
            if (checkbox == nullptr) {
                break;
            }

            const ScopedClip<Sink> clip{sink, rect};
            const auto label = layoutText(node.text, common.text,
                std::max(0.01F, node.size.width - checkbox->slotSize - checkbox->labelGap));
            const float firstHeight = std::max(checkbox->slotSize, label.lineBoxHeightPx);
            const float blockHeight = std::max(firstHeight,
                label.size.height + firstHeight - label.lineBoxHeightPx);
            const float blockTop = origin.y + (node.size.height - blockHeight) * 0.5F;
            const float indicatorCenterY = blockTop + firstHeight * 0.5F;
            const float indicator = checkbox->indicatorSize;
            // 槽位 = indicator + 2 × (focusRingWidth + 1px 隔离带)；指示器在槽
            // 位内居中（聚焦时环围绕指示器，不改变槽位/标签起点）。
            const float indicatorOrigin =
                origin.x + (checkbox->slotSize - indicator) * 0.5F;
            Rect indicatorRect{
                Offset{indicatorOrigin,
                       indicatorCenterY - indicator * 0.5F},
                Size{indicator, indicator}};
            float indicatorRadius = checkbox->indicatorRadius;
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                // 焦点环围绕指示器（描边环带；damage 不变量：绘制不越出
                // 节点）。
                const float outset = common.focusWidth + 1.0F;
                sink.drawRectStroke(
                    Rect{Offset{indicatorRect.origin.x - outset, indicatorRect.origin.y - outset},
                         Size{indicator + 2.0F * outset, indicator + 2.0F * outset}},
                    common.focusRing, CornerRadius::all(indicatorRadius + outset),
                    common.focusWidth);
            }
            sink.drawRect(indicatorRect,
                          checkbox->checked ? checkbox->indicatorChecked
                                            : checkbox->indicator,
                          CornerRadius::all(indicatorRadius));
            sink.drawRectStroke(indicatorRect, checkbox->indicatorOutline,
                                CornerRadius::all(indicatorRadius), common.borderWidth);
            if (checkbox->checked) {
                // On：accent 填充 + 勾号（目录折线按 markInset 内缩）。
                const auto& polylines =
                    core::iconPolylines(core::IconId::Check);
                if (!polylines.empty()) {
                    const float inset = checkbox->markInset;
                    const Rect markBox{
                        Offset{indicatorRect.origin.x + inset,
                               indicatorRect.origin.y + inset},
                        Size{std::max(0.0F, indicatorRect.size.width -
                                                2.0F * inset),
                             std::max(0.0F, indicatorRect.size.height -
                                               2.0F * inset)}};
                    if (markBox.size.width > 1.0F) {
                        sink.drawIcon(polylines, markBox, checkbox->mark,
                                      node.iconStrokeWidth);
                    }
                }
            }
            paintLines(sink, label, common.text,
                Offset{origin.x + checkbox->slotSize + checkbox->labelGap,
                       blockTop + (firstHeight - label.lineBoxHeightPx) * 0.5F});
            break;
        }
        case WidgetType::Switch: {
            // §6.4：轨道（pill 圆角 = 高度一半）+ 描边轮廓 + 两态滑块
            //（knobOff/knobOn 独立对比）；左右内距由 resolver 按
            //(trackHeight - knobSize)/2 推导。轨道画在槽位内。
            const auto* control =
                std::get_if<core::SwitchResolvedStyle>(&styleSource.component);
            if (control == nullptr) {
                break;
            }

            const ScopedClip<Sink> clip{sink, rect};
            const auto label = layoutText(node.text, common.text,
                std::max(0.01F, node.size.width - control->slotSize - control->labelGap));
            const float firstHeight = std::max((control->trackHeight + control->slotSize - control->trackWidth), label.lineBoxHeightPx);
            const float blockHeight = std::max(firstHeight,
                label.size.height + firstHeight - label.lineBoxHeightPx);
            const float blockTop = origin.y + (node.size.height - blockHeight) * 0.5F;
            const float indicatorCenterY = blockTop + firstHeight * 0.5F;
            const float trackOrigin =
                origin.x + (control->slotSize - control->trackWidth) * 0.5F;
            const float trackTop =
                indicatorCenterY - control->trackHeight * 0.5F;
            Rect trackRect{Offset{trackOrigin, trackTop},
                           Size{control->trackWidth, control->trackHeight}};
            float trackRadius = control->trackHeight * 0.5F;
            // 焦点环围绕轨道（描边环带）。
            if (common.focusWidth > 0.0F && common.focusRing.a > 0) {
                const float outset = common.focusWidth + 1.0F;
                sink.drawRectStroke(
                    Rect{Offset{trackRect.origin.x - outset, trackRect.origin.y - outset},
                         Size{trackRect.size.width + 2.0F * outset, trackRect.size.height + 2.0F * outset}},
                    common.focusRing, CornerRadius::all(trackRadius + outset),
                    common.focusWidth);
            }
            sink.drawRect(trackRect,
                          control->checked ? control->trackOn
                                           : control->trackOff,
                          CornerRadius::all(trackRadius));
            // 轨道轮廓（Off 必要轮廓；On 保持轮廓一致性）。
            sink.drawRectStroke(trackRect, control->trackOutline,
                                CornerRadius::all(trackRadius), common.borderWidth);
            const float knobX =
                trackRect.origin.x + control->knobInset +
                control->knobPosition * (trackRect.size.width -
                    2.0F * control->knobInset - control->knobSize);
            sink.drawRect(
                Rect{Offset{knobX,
                            trackRect.origin.y +
                                (trackRect.size.height - control->knobSize) *
                                    0.5F},
                     Size{control->knobSize, control->knobSize}},
                control->checked ? control->knobOn : control->knobOff,
                CornerRadius::all(control->knobSize * 0.5F));
            paintLines(sink, label, common.text,
                Offset{origin.x + control->slotSize + control->labelGap,
                       blockTop + (firstHeight - label.lineBoxHeightPx) * 0.5F});
            break;
        }
    }

    // 滚动视口裁剪：子内容不得溢出 viewport（overflow clip，plan §3.4）。
    if (node.clipContent) {
        const ScopedClip<Sink> clip{sink, rect};
        for (const auto& child : node.children) {
            paintNode(sink, child, origin, options, nodeAlpha);
        }
        // S3（§7.2）：滚动条——Thumb 实色经 RenderNode 传递（专用 token，
        // 不再对前景乘 alpha）；可视宽 4、最小长 24、上下 inset 4、圆角
        // = 可视宽一半；长度按 viewport/content 比例并在
        // [minLength, trackLength] 夹取（短视口不越界）。
        if (node.scrollbarThickness > 0.0F &&
            node.scrollExtent > 0.0F && node.scrollbarColor.a > 0) {
            const float inset = node.scrollbarThickness -
                                        node.scrollbarThumbWidth;
            const float trackTop = origin.y + inset;
            const float trackLength = std::max(
                0.0F, node.size.height - 2.0F * inset);
            const float fraction =
                node.size.height / (node.size.height + node.scrollExtent);
            // [minLength, trackLength] 夹取：短视口时最小值不越界。
            const float thumbHeight = std::min(
                std::max(trackLength * fraction, node.scrollbarMinLength),
                trackLength);
            if (thumbHeight > 0.0F && trackLength > 0.0F) {
                const float scrollable = trackLength - thumbHeight;
                const float progress = node.scrollExtent > 0.0F
                                           ? node.scrollOffset /
                                                 node.scrollExtent
                                           : 0.0F;
                const float thumbY = trackTop + progress * scrollable;
                sink.drawRect(
                    Rect{Offset{origin.x + node.size.width -
                                    node.scrollbarThickness +
                                    (node.scrollbarThickness -
                                     node.scrollbarThumbWidth) * 0.5F,
                                thumbY},
                         Size{node.scrollbarThumbWidth, thumbHeight}},
                    core::scaleColorAlpha(node.scrollbarColor, nodeAlpha),
                    CornerRadius::all(node.scrollbarThumbWidth * 0.5F));
            }
        }
    } else {
        for (const auto& child : node.children) {
            paintNode(sink, child, origin, options, nodeAlpha);
        }
    }
    if (node.type == WidgetType::Tabs) {
        const auto* tabs = std::get_if<core::TabsResolvedStyle>(&styleSource.component);
        if (tabs) {
            const ScopedClip<Sink> clip{sink, rect};
            for (const auto& child : node.children) {
                const float bottom = origin.y + child.offset.y + child.size.height + 2.0F;
                sink.drawRect(Rect{Offset{origin.x + child.offset.x, bottom},
                    Size{child.size.width, tabs->separatorHeight}}, tabs->separator);
                if (child.selected) {
                    sink.drawRect(Rect{Offset{origin.x + child.offset.x, bottom},
                        Size{child.size.width, tabs->indicatorHeight}}, tabs->indicator);
                }
            }
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
