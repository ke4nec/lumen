#include "lumen/layout/layout.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <numeric>

#include "lumen/accessibility/semantics.h"
#include "lumen/core/splitter.h"
#include "lumen/core/utf8.h"
#include "lumen/text/font_manager.h"
#include "lumen/text/text_layout.h"

namespace lumen::layout {
namespace {

using core::Constraints;
using core::EdgeInsets;
using core::Offset;
using core::RenderNode;
using core::ResolvedStyle;
using core::Size;
using core::SplitterSource;
using core::TextStyle;
using core::VirtualListSource;
using core::Widget;
using core::WidgetType;

float clampFloat(float value, float low, float high) {
    return std::clamp(value, low, high);
}

// M1：布局期字体源（UI 线程独占；公开入口显式传入，内部经作用域 guard
// 读取，避免为每个递归层新增参数；默认占位，CPU 不依赖 Skia）。
thread_local const text::FontManager* t_activeFonts = nullptr;


thread_local const LayoutEngine::PrepareItem* t_prepareItem = nullptr;

struct ScopedItemPreparation {
    const LayoutEngine::PrepareItem* previous{t_prepareItem};
    explicit ScopedItemPreparation(const LayoutEngine::PrepareItem& prepare) {
        t_prepareItem = &prepare;
    }
    ~ScopedItemPreparation() { t_prepareItem = previous; }
    ScopedItemPreparation(const ScopedItemPreparation&) = delete;
    ScopedItemPreparation& operator=(const ScopedItemPreparation&) = delete;
};

const text::FontManager& activeFonts() {
    return t_activeFonts != nullptr ? *t_activeFonts
                                    : text::PlaceholderFontManager::shared();
}

struct ScopedFonts {
    const text::FontManager* previous{nullptr};
    explicit ScopedFonts(const text::FontManager& fonts) {
        previous = t_activeFonts;
        t_activeFonts = &fonts;
    }
    ~ScopedFonts() { t_activeFonts = previous; }
    ScopedFonts(const ScopedFonts&) = delete;
    ScopedFonts& operator=(const ScopedFonts&) = delete;
};

// identity 与旧 assignIdentities 相同的拼接规则：keyless = 索引路径，
// keyed = key 路径（重建间稳定，交互快照/damage 依赖）。
std::string childIdentity(const std::string& parentPath, const Widget& child,
                          std::size_t index) {
    const std::string segment =
        child.key.empty() ? "i:" + std::to_string(index) : "k:" + child.key;
    return parentPath + "/" + segment;
}

// 视觉系统（visual-system-design §5）：每个节点在布局前解析一次样式；
// 布局度量与 painter 使用同一份 resolved style。resolver 不修改 Widget。
RenderNode makeNode(const Widget& widget, Offset offset, Size size,
                    const style::StyleContext& styleContext,
                    const std::string& identity) {
    RenderNode node;
    node.type = widget.type;
    node.key = widget.key;
    node.identity = identity;
    node.offset = offset;
    node.size = size;
    node.style = style::resolveStyle(widget, styleContext, identity);
    node.padding = core::commonStyle(node.style).padding;
    node.text = widget.text;
    node.placeholder = widget.placeholder;
    node.bind = widget.bind;
    node.onClick = widget.onClick;
    node.obscure = widget.obscure;
    node.readOnly = widget.readOnly;
    node.multiline = widget.multiline;
    node.semanticsLabel = widget.semanticsLabel;
    node.semanticsValue = widget.semanticsValue;
    node.semanticsRole = widget.semanticsRole;
    node.semanticsActions = widget.semanticsActions;
    node.checked = widget.checked;
    node.scrollOffset = widget.scrollOffset;
    node.imageId = widget.imageId;
    node.imageSource = widget.imageSource;
    // 值控件复用 RenderNode::text：绑定值由 applyBinds 写入，未绑定值
    // 由构建器直接放在 Widget::text 中。
    if (widget.type == WidgetType::Slider ||
        widget.type == WidgetType::ProgressBar ||
        widget.type == WidgetType::Dropdown) {
        node.text = widget.text;
    }
    // §4.5：按实际图标盒缩放描边，不能使用 Image/Checkbox 宿主宽度。
    float iconBoxSize = std::min(size.width, size.height);
    if (widget.type == WidgetType::Image) {
        iconBoxSize = std::min(24.0F, iconBoxSize * 0.6F);
    } else if (const auto* checkbox =
                   std::get_if<core::CheckboxResolvedStyle>(&node.style.component)) {
        iconBoxSize = std::max(
            0.0F, checkbox->indicatorSize - 2.0F * checkbox->markInset);
    }
    node.iconStrokeWidth = styleContext.theme.icons.strokeWidth * iconBoxSize /
                           std::max(0.01F, styleContext.theme.icons.defaultSize);
    node.icon = static_cast<std::uint8_t>(widget.icon);
    node.transitionAlpha = widget.transitionAlpha;
    node.showScrollbar = widget.showScrollbar;
    node.elevation = widget.elevation > 0.0F ? widget.elevation : core::commonStyle(node.style).elevation;
    if (node.elevation > 0.0F) {
        // §4.5 阴影分级：层级数（1..3）查表得到 offset/blur/alpha；
        // 高对比模式各级 alpha 为 0（不产生阴影命令）。
        const style::ElevationTokens& elevation = styleContext.theme.elevation;
        const style::ElevationShadowParams& params =
            elevation.paramsFor(node.elevation);
        node.shadowColor = core::Color{elevation.shadowColor.r,
                                       elevation.shadowColor.g,
                                       elevation.shadowColor.b,
                                       params.alpha};
        node.shadowOffset = params.offset;
        node.shadowBlur = params.blur;
    }
    if (widget.showScrollbar &&
        (widget.type == WidgetType::ScrollView ||
         widget.type == WidgetType::ListView ||
         widget.type == WidgetType::VirtualList ||
         // 集合控件：同一滚动条 token 路径（TreeList 含表头区）。
         widget.type == WidgetType::List ||
         widget.type == WidgetType::Tree ||
         widget.type == WidgetType::TreeList)) {
        node.scrollbarThickness = styleContext.theme.scrollbar.thickness;
        node.scrollbarThumbWidth = styleContext.theme.scrollbar.thumbWidth;
        node.scrollbarMinLength = styleContext.theme.scrollbar.minLength;
        node.scrollbarColor = styleContext.theme.scrollbar.rest;
    }
    node.enabled = widget.enabled;
    node.invalid = widget.invalid;
    node.selected = widget.selected;
    node.collectionRow = widget.collectionRow;
    node.windowDrag = widget.windowDrag;
    node.virtualSource = widget.virtualSource;
    return node;
}

// v0.3 阶段8B: 文本度量统一走 text::TextLayout（布局与绘制共用同一份
// 布局结果）。maxWidth <= 0 表示不换行；TextField 单行不换行（横向滚动
// 属于 8D 视口），Text 按约束换行并支持 maxLines/ellipsis。
text::TextLayoutResult layoutTextContent(const std::string& content,
                                        const TextStyle& style,
                                        float maxWidth, bool wrap) {
    TextStyle effective = style;
    if (!wrap) {
        effective.maxLines = 1;
        maxWidth = 0.0F;
    }
    // M7：经布局缓存（同文本/样式/宽度/字体后端的重复度量直接命中；
    // plan 阶段2 布局缓存契约，此前未接线——静态文本场景每帧全量
    // reshape）。UI 线程独占；缓存实例按线程隔离（字体源亦线程局部）。
    static thread_local text::TextLayoutCache cache;
    return cache.compute(content, effective, maxWidth, activeFonts());
}

Size measureTextContent(const std::string& content, const TextStyle& style,
                         float maxWidth, bool wrap) {
    return layoutTextContent(content, style, maxWidth, wrap).size;
}

Size measureLeafIntrinsic(const Widget& widget, const ResolvedStyle& resolved,
                          const style::StyleContext& styleContext,
                          float maxWidth) {
    const TextStyle& textStyle = core::commonStyle(resolved).text;
    const std::string& content =
        widget.text.empty() && !widget.placeholder.empty() ? widget.placeholder
                                                           : widget.text;
    switch (widget.type) {
        case WidgetType::Text:
            return measureTextContent(
                content, textStyle, maxWidth,
                /*wrap=*/widget.multiline || textStyle.maxLines != 1);
        case WidgetType::Button: {
            // chrome（padding/最小尺寸）来自 resolved style——布局与
            // painter 同源（visual-system §7.1）。图标计入内容组宽度
            //（§6.1：文本+尾随图标作为居中内容组）。
            const EdgeInsets& chrome = core::commonStyle(resolved).padding;
            const Size textSize = measureTextContent(content, textStyle, 0.0F,
                                                     false);
            float contentWidth = textSize.width;
            const auto* buttonStyle =
                std::get_if<core::ButtonResolvedStyle>(&resolved.component);
            if (buttonStyle != nullptr &&
                (widget.icon != core::IconId::None || buttonStyle->reserveIconSpace)) {
                contentWidth +=
                    buttonStyle->iconSize +
                    (content.empty() ? 0.0F : buttonStyle->iconGap);
            }
            const float width =
                std::max(contentWidth + chrome.horizontal(),
                         resolved.minWidth);
            const float height = std::max(textSize.height + chrome.vertical(),
                                          resolved.minHeight);
            return Size{width, height};
        }
        case WidgetType::TextField: {
            const EdgeInsets& chrome = core::commonStyle(resolved).padding;
            const Size textSize = measureTextContent(
                content, textStyle,
                widget.multiline && maxWidth > 0.0F ? maxWidth : 0.0F,
                widget.multiline);
            const float width =
                std::max(textSize.width + chrome.horizontal(),
                         resolved.minWidth);
            const float height = std::max(textSize.height + chrome.vertical(),
                                          resolved.minHeight);
            return Size{width, height};
        }
        case WidgetType::Icon: {
            // M6：默认尺寸来自 IconTheme（token），width/height 覆盖。
            return Size{styleContext.theme.icons.defaultSize,
                        styleContext.theme.icons.defaultSize};
        }
        case WidgetType::Slider: {
            // S3（§6.5）：可操作整行高度 = 档位 minHeight（≥ thumb + 焦点
            // 预留）；宽度默认填充（无 intrinsic 宽）。
            const auto* slider =
                std::get_if<core::SliderResolvedStyle>(&resolved.component);
            const float minHeight =
                slider != nullptr
                    ? std::max(resolved.minHeight,
                               slider->thumbDiameter +
                                   2.0F * (slider->trackInset -
                                           slider->thumbDiameter * 0.5F))
                    : 24.0F;
            return Size{0.0F, minHeight};
        }
        case WidgetType::ProgressBar: {
            // S3（§6.6）：可视高度 = 档位 trackHeight（4/6/8）。
            const auto* bar = std::get_if<core::ProgressBarResolvedStyle>(
                &resolved.component);
            return Size{0.0F,
                        bar != nullptr ? bar->trackHeight : 6.0F};
        }
        case WidgetType::Radio: {
            // S2：Radio 专用解析（§6.4）——槽位（指示器 + 焦点环 + 1px
            // 隔离带）+ 标签。
            const auto* radio =
                std::get_if<core::RadioResolvedStyle>(&resolved.component);
            if (radio == nullptr) {
                return measureTextContent(content, textStyle, 0.0F, false);
            }
            const float available = std::min(widget.width.value_or(maxWidth), maxWidth);
            const float labelWidth = available > 0.0F
                ? std::max(0.01F, available - radio->slotSize - radio->labelGap -
                    core::commonStyle(resolved).padding.horizontal()) : 0.0F;
            const auto label = layoutTextContent(content, textStyle, labelWidth, true);
            return Size{radio->slotSize + radio->labelGap + label.size.width,
                        std::max({radio->slotSize, label.size.height + std::max(0.0F,
                                      radio->slotSize - label.lineBoxHeightPx),
                                  resolved.minHeight})};
        }
        case WidgetType::Dropdown: {
            // S3（§6.7）：值行与字段同源——chrome/最小尺寸来自 resolved
            // style（Button 路径同构）+ 尾随 Chevron 槽位。
            const EdgeInsets& chrome = core::commonStyle(resolved).padding;
            const Size textSize = measureTextContent(content, textStyle, 0.0F,
                                                     false);
            const auto* dropdown =
                std::get_if<core::ButtonResolvedStyle>(&resolved.component);
            const float chevron =
                dropdown != nullptr
                    ? dropdown->iconSize +
                          (content.empty() ? 0.0F : dropdown->iconGap)
                    : 0.0F;
            const float width = std::max(
                textSize.width + chevron + chrome.horizontal(),
                resolved.minWidth);
            const float height = std::max(textSize.height + chrome.vertical(),
                                          resolved.minHeight);
            return Size{width, height};
        }
        case WidgetType::Tooltip: {
            // S4（§6.9）：文本换行（最大宽 280）；表面 padding 由
            // layoutLeaf 统一追加（与其他叶子一致，不重复叠加）。
            float outerWidth = maxWidth > 0.0F
                                   ? maxWidth : styleContext.theme.tooltip.maxWidth;
            if (widget.width.has_value()) {
                outerWidth = std::min(outerWidth, *widget.width);
            }
            const float wrapWidth = std::max(
                0.01F, std::min(outerWidth, styleContext.theme.tooltip.maxWidth) -
                           core::commonStyle(resolved).padding.horizontal());
            return measureTextContent(content, textStyle, wrapWidth, true);
        }
        case WidgetType::Checkbox: {
            const auto* checkbox =
                std::get_if<core::CheckboxResolvedStyle>(&resolved.component);
            if (checkbox == nullptr) {
                return measureTextContent(content, textStyle, 0.0F, false);
            }
            const float available = std::min(widget.width.value_or(maxWidth), maxWidth);
            const float labelWidth = available > 0.0F
                ? std::max(0.01F, available - checkbox->slotSize - checkbox->labelGap -
                    core::commonStyle(resolved).padding.horizontal()) : 0.0F;
            const auto label = layoutTextContent(content, textStyle, labelWidth, true);
            // 槽位含焦点环预留（§4.4）：是否聚焦不改变标签起点。
            return Size{checkbox->slotSize + checkbox->labelGap + label.size.width,
                        std::max({checkbox->slotSize, label.size.height + std::max(0.0F,
                                      checkbox->slotSize - label.lineBoxHeightPx),
                                  resolved.minHeight})};
        }
        case WidgetType::Switch: {
            const auto* control =
                std::get_if<core::SwitchResolvedStyle>(&resolved.component);
            if (control == nullptr) {
                return measureTextContent(content, textStyle, 0.0F, false);
            }
            const float available = std::min(widget.width.value_or(maxWidth), maxWidth);
            const float labelWidth = available > 0.0F
                ? std::max(0.01F, available - control->slotSize - control->labelGap -
                    core::commonStyle(resolved).padding.horizontal()) : 0.0F;
            const auto label = layoutTextContent(content, textStyle, labelWidth, true);
            return Size{control->slotSize + control->labelGap + label.size.width,
                        std::max({control->trackHeight + control->slotSize -
                                      control->trackWidth,
                                  label.size.height + std::max(0.0F, control->trackHeight +
                                      control->slotSize - control->trackWidth -
                                      label.lineBoxHeightPx),
                                  resolved.minHeight})};
        }
        default:
            return measureTextContent(content, textStyle, 0.0F, false);
    }
}

RenderNode layoutLeaf(const Widget& widget, const Constraints& constraints,
                      const style::StyleContext& styleContext,
                      const std::string& identity) {
    const Constraints outer = constraints.deflate(widget.margin);
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    Size intrinsic = measureLeafIntrinsic(
        widget, resolved, styleContext,
        outer.isBoundedWidth() ? outer.maxWidth : 0.0F);
    // Button/TextField/Dropdown intrinsic measurement already includes their
    // chrome padding; text and generic leaves use the resolved box padding here.
    if (widget.type != WidgetType::Button &&
        widget.type != WidgetType::TextField &&
        widget.type != WidgetType::Dropdown) {
        intrinsic.width += core::commonStyle(resolved).padding.horizontal();
        intrinsic.height += core::commonStyle(resolved).padding.vertical();
    }
    Size size = outer.constrain(intrinsic);
    // M6：Slider/ProgressBar 填充可用宽（轨道语义；显式 width 优先）。
    if ((widget.type == WidgetType::Slider ||
         widget.type == WidgetType::ProgressBar) &&
        !widget.width.has_value() && outer.isBoundedWidth()) {
        size.width = outer.maxWidth;
    }
    // Border-box overrides: fixed size wins over intrinsic measurement.
    if (widget.width.has_value()) {
        size.width =
            clampFloat(*widget.width, outer.minWidth, outer.maxWidth);
    }
    if (widget.height.has_value()) {
        size.height =
            clampFloat(*widget.height, outer.minHeight, outer.maxHeight);
    }
    return makeNode(widget, Offset{0.0F, 0.0F}, size, styleContext, identity);
}

RenderNode layoutContainer(const Widget& widget, const Constraints& constraints,
                           const style::StyleContext& styleContext,
                           const std::string& identity);

RenderNode layoutFlex(const Widget& widget, const Constraints& constraints,
                      const style::StyleContext& styleContext,
                      const std::string& identity, bool isRow);

// S3（§6.8）：Tabs 上下文解析——子按钮改写为 Ghost + 两态前景覆盖
//（选中 accentContent / 未选 contentSecondary）+ 页签水平 padding 与
// 行间距；页签行 chrome（分隔线/指示条）由 painter 从 TabsResolvedStyle
// 绘制。应用侧不需要给每个按钮手写颜色。
RenderNode layoutTabs(const Widget& widget, const Constraints& constraints,
                      const style::StyleContext& styleContext,
                      const std::string& identity);

RenderNode layoutStack(const Widget& widget, const Constraints& constraints,
                       const style::StyleContext& styleContext,
                       const std::string& identity);

RenderNode layoutScrollView(const Widget& widget, const Constraints& constraints,
                            const style::StyleContext& styleContext,
                            const std::string& identity);

// M3：Grid 与 VirtualList（定义见后；layoutSingle 引用）。
RenderNode layoutGrid(const Widget& widget, const Constraints& constraints,
                      const style::StyleContext& styleContext,
                      const std::string& identity);
RenderNode layoutVirtualList(const Widget& widget, const Constraints& constraints,
                             const style::StyleContext& styleContext,
                             const std::string& identity);
// 集合控件：TreeList（树 + 列）——VirtualList 引擎 + 粘性表头。
RenderNode layoutTreeList(const Widget& widget, const Constraints& constraints,
                          const style::StyleContext& styleContext,
                          const std::string& identity);
// Splitter（splitter-design §6）：两窗格 + 框架物化分隔条。
RenderNode layoutSplitter(const Widget& widget, const Constraints& constraints,
                          const style::StyleContext& styleContext,
                          const std::string& identity);

RenderNode layoutSingle(const Widget& widget, const Constraints& constraints,
                        const style::StyleContext& styleContext,
                        const std::string& identity) {
    if (core::isLeafWidget(widget.type)) {
        return layoutLeaf(widget, constraints, styleContext, identity);
    }
    switch (widget.type) {
        case WidgetType::Container:
        case WidgetType::FocusScope:
            // FocusScope 布局同 Container：单子，边界用于焦点遍历。
            return layoutContainer(widget, constraints, styleContext,
                                   identity);
        case WidgetType::Row:
            return layoutFlex(widget, constraints, styleContext, identity,
                              true);
        case WidgetType::Column:
            return layoutFlex(widget, constraints, styleContext, identity,
                              false);
        case WidgetType::Stack:
            return layoutStack(widget, constraints, styleContext, identity);
        case WidgetType::ScrollView:
        case WidgetType::ListView:
            return layoutScrollView(widget, constraints, styleContext,
                                    identity);
        case WidgetType::Dropdown:
            // M11：值行叶子（收起态；选项经框架级 overlay 浮动菜单）。
            return layoutLeaf(widget, constraints, styleContext, identity);
        case WidgetType::ThemeScope: {
            // M6：局部主题域——子树解析切换到覆盖主题。
            const auto* override =
                static_cast<const style::Theme*>(widget.themeOverride);
            if (override != nullptr) {
                const style::StyleContext scopedContext{
                    *override, styleContext.interaction,
                    styleContext.accessibility, styleContext.deviceScale,
                    styleContext.previewStates};
                const style::ScopedThemeOverride guard{*override};
                return layoutContainer(widget, constraints, scopedContext,
                                       identity);
            }
            return layoutContainer(widget, constraints, styleContext,
                                   identity);
        }
        case WidgetType::Tabs:
            return layoutTabs(widget, constraints, styleContext, identity);
        case WidgetType::Grid:
            return layoutGrid(widget, constraints, styleContext, identity);
        case WidgetType::VirtualList:
        // 集合控件：List/Tree 与 VirtualList 同一虚拟化引擎（差异全部
        // 在 widgets 层控制器的 buildItem/source 实现）。
        case WidgetType::List:
        case WidgetType::Tree:
            return layoutVirtualList(widget, constraints, styleContext,
                                     identity);
        case WidgetType::TreeList:
            return layoutTreeList(widget, constraints, styleContext, identity);
        case WidgetType::Splitter:
            return layoutSplitter(widget, constraints, styleContext, identity);
        case WidgetType::Image:
        case WidgetType::Text:
        case WidgetType::Button:
        case WidgetType::TextField:
        case WidgetType::Checkbox:
        case WidgetType::Switch:
        case WidgetType::Icon:
        case WidgetType::Slider:
        case WidgetType::ProgressBar:
        case WidgetType::Radio:
        case WidgetType::Tooltip:
            return layoutLeaf(widget, constraints, styleContext, identity);
    }
    return layoutLeaf(widget, constraints, styleContext, identity);
}

RenderNode layoutContainer(const Widget& widget, const Constraints& constraints,
                           const style::StyleContext& styleContext,
                           const std::string& identity) {
    // Container is single-child by contract (see widget.h); extra children
    // indicate a programming error.
    assert(widget.children.size() <= 1);

    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const EdgeInsets& padding = core::commonStyle(resolved).padding;
    const Constraints outer = constraints.deflate(widget.margin);
    const Constraints inner = outer.deflate(padding);

    float borderWidth = padding.horizontal();
    float borderHeight = padding.vertical();
    RenderNode childNode{};
    bool hasChild = !widget.children.empty();

    if (widget.width.has_value()) {
        borderWidth =
            clampFloat(*widget.width, outer.minWidth, outer.maxWidth);
    }
    if (widget.height.has_value()) {
        borderHeight =
            clampFloat(*widget.height, outer.minHeight, outer.maxHeight);
    }

    if (hasChild) {
        const Widget& child = widget.children.front();
        Constraints childConstraints;
        if (widget.width.has_value()) {
            const float contentWidth =
                std::max(0.0F, borderWidth - padding.horizontal());
            childConstraints.minWidth = 0.0F;
            childConstraints.maxWidth = contentWidth;
        } else {
            childConstraints.minWidth = 0.0F;
            childConstraints.maxWidth = inner.maxWidth;
        }
        if (widget.height.has_value()) {
            const float contentHeight =
                std::max(0.0F, borderHeight - padding.vertical());
            childConstraints.minHeight = 0.0F;
            childConstraints.maxHeight = contentHeight;
        } else {
            childConstraints.minHeight = 0.0F;
            childConstraints.maxHeight = inner.maxHeight;
        }
        childNode = layoutSingle(child, childConstraints, styleContext,
                                 childIdentity(identity, child, 0));
        // The parent consumes the child margin, same rule as Row/Column/
        // Stack: it offsets the child inside the padded content box and
        // participates in the border size.
        childNode.offset = Offset{padding.left + child.margin.left,
                                  padding.top + child.margin.top};
        if (!widget.width.has_value()) {
            borderWidth = clampFloat(
                childNode.size.width + padding.horizontal() +
                    child.margin.horizontal(),
                outer.minWidth, outer.maxWidth);
        }
        if (!widget.height.has_value()) {
            borderHeight = clampFloat(
                childNode.size.height + padding.vertical() +
                    child.margin.vertical(),
                outer.minHeight, outer.maxHeight);
        }
    } else {
        if (!widget.width.has_value()) {
            borderWidth = clampFloat(padding.horizontal(),
                                     outer.minWidth, outer.maxWidth);
        }
        if (!widget.height.has_value()) {
            borderHeight = clampFloat(padding.vertical(),
                                      outer.minHeight, outer.maxHeight);
        }
    }

    // Clamp once more so tight parents always win (resize correctness).
    borderWidth = clampFloat(borderWidth, outer.minWidth, outer.maxWidth);
    borderHeight = clampFloat(borderHeight, outer.minHeight, outer.maxHeight);

    RenderNode node = makeNode(widget, Offset{0.0F, 0.0F},
                               Size{borderWidth, borderHeight}, styleContext,
                               identity);
    if (hasChild) {
        node.children.push_back(std::move(childNode));
    }
    return node;
}

float mainAxisStart(core::MainAxisAlignment align, float freeSpace,
                    std::size_t count) {
    using core::MainAxisAlignment;
    switch (align) {
        case MainAxisAlignment::Start:
        case MainAxisAlignment::SpaceBetween:
            return 0.0F;
        case MainAxisAlignment::Center:
            return freeSpace * 0.5F;
        case MainAxisAlignment::End:
            return freeSpace;
        case MainAxisAlignment::SpaceAround:
            return count == 0
                       ? 0.0F
                       : freeSpace / (static_cast<float>(count) * 2.0F);
        case MainAxisAlignment::SpaceEvenly:
            return count == 0
                       ? 0.0F
                       : freeSpace / (static_cast<float>(count) + 1.0F);
    }
    return 0.0F;
}

float mainAxisGap(core::MainAxisAlignment align, float freeSpace,
                  std::size_t count) {
    using core::MainAxisAlignment;
    switch (align) {
        case MainAxisAlignment::SpaceBetween:
            return count > 1 ? freeSpace / (static_cast<float>(count) - 1.0F)
                             : 0.0F;
        case MainAxisAlignment::SpaceAround:
            return count == 0 ? 0.0F : freeSpace / static_cast<float>(count);
        case MainAxisAlignment::SpaceEvenly:
            return count == 0
                       ? 0.0F
                       : freeSpace / (static_cast<float>(count) + 1.0F);
        default:
            return 0.0F;
    }
}

float crossAxisOffset(core::CrossAxisAlignment align, float freeSpace) {
    using core::CrossAxisAlignment;
    switch (align) {
        case CrossAxisAlignment::Start:
        case CrossAxisAlignment::Stretch:
            return 0.0F;
        case CrossAxisAlignment::Center:
            return freeSpace * 0.5F;
        case CrossAxisAlignment::End:
            return freeSpace;
    }
    return 0.0F;
}

// Row/Column share one implementation; isRow selects the main axis.
RenderNode layoutFlex(const Widget& widget, const Constraints& constraints,
                      const style::StyleContext& styleContext,
                      const std::string& identity, bool isRow) {
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const EdgeInsets& padding = core::commonStyle(resolved).padding;
    const Constraints outer = constraints.deflate(widget.margin);
    const float paddingMain = isRow ? padding.horizontal() : padding.vertical();
    const float paddingCross = isRow ? padding.vertical() : padding.horizontal();
    const float outerMinMain =
        isRow ? outer.minWidth : outer.minHeight;
    const float outerMaxMain =
        isRow ? outer.maxWidth : outer.maxHeight;
    const float outerMinCross =
        isRow ? outer.minHeight : outer.minWidth;
    const float outerMaxCross =
        isRow ? outer.maxHeight : outer.maxWidth;
    // Resolve explicit border-box sizes before measuring children so child
    // constraints and the flex budget target the box this widget will really
    // occupy (same rule as layoutContainer). Measuring against the incoming
    // maximum instead let flex children overflow a fixed-size Row/Column.
    const std::optional<float> mainOverride =
        isRow ? widget.width : widget.height;
    const std::optional<float> crossOverride =
        isRow ? widget.height : widget.width;
    const float resolvedMain =
        mainOverride.has_value()
            ? clampFloat(*mainOverride, outerMinMain, outerMaxMain)
            : 0.0F;
    const float resolvedCross =
        crossOverride.has_value()
            ? clampFloat(*crossOverride, outerMinCross, outerMaxCross)
            : 0.0F;
    const float contentMaxMain = std::max(
        0.0F, (mainOverride.has_value() ? resolvedMain : outerMaxMain) -
                  paddingMain);
    const float contentMaxCross = std::max(
        0.0F,
        (crossOverride.has_value() ? resolvedCross : outerMaxCross) -
            paddingCross);
    const bool boundedMain =
        mainOverride.has_value() ||
        (isRow ? outer.isBoundedWidth() : outer.isBoundedHeight());

    const std::size_t count = widget.children.size();
    const float baseSpacing =
        count > 0 ? widget.spacing * static_cast<float>(count - 1) : 0.0F;

    std::vector<RenderNode> measured(count);
    std::vector<float> mainWithMargin(count, 0.0F);
    std::vector<float> crossWithMargin(count, 0.0F);

    float totalFlex = 0.0F;
    for (const auto& child : widget.children) {
        if (child.flex > 0.0F && boundedMain) {
            totalFlex += child.flex;
        }
    }
    const bool distributeFlex = totalFlex > 0.0F && boundedMain;

    // Pass 1: inflexible children get loose content-box constraints. The
    // child accounts for its own margin inside layoutSingle().
    float fixedMain = baseSpacing;
    for (std::size_t i = 0; i < count; ++i) {
        const Widget& child = widget.children[i];
        const bool isFlexChild = child.flex > 0.0F && distributeFlex;
        if (isFlexChild) {
            continue;
        }
        Constraints childConstraints{};
        if (isRow) {
            childConstraints = Constraints{0.0F, contentMaxMain, 0.0F,
                                           contentMaxCross};
        } else {
            childConstraints = Constraints{0.0F, contentMaxCross, 0.0F,
                                           contentMaxMain};
        }
        measured[i] = layoutSingle(child, childConstraints, styleContext,
                                   childIdentity(identity, child, i));
        const float childMain =
            (isRow ? measured[i].size.width : measured[i].size.height) +
            (isRow ? child.margin.horizontal() : child.margin.vertical());
        const float childCross =
            (isRow ? measured[i].size.height : measured[i].size.width) +
            (isRow ? child.margin.vertical() : child.margin.horizontal());
        mainWithMargin[i] = childMain;
        crossWithMargin[i] = childCross;
        fixedMain += childMain;
    }

    // Pass 2: flex children split remaining main-axis space. Budgets are
    // pre-margin (include the child margin); layoutSingle deflates the margin
    // to produce the border box.
    float remaining =
        distributeFlex ? std::max(0.0F, contentMaxMain - fixedMain) : 0.0F;
    if (distributeFlex) {
        for (std::size_t i = 0; i < count; ++i) {
            const Widget& child = widget.children[i];
            if (child.flex <= 0.0F) {
                continue;
            }
            const float allocated =
                totalFlex > 0.0F ? remaining * (child.flex / totalFlex) : 0.0F;
            Constraints childConstraints;
            if (isRow) {
                const float budget = std::max(0.0F, allocated);
                childConstraints =
                    Constraints{child.shrinkWrap ? 0.0F : budget, budget, 0.0F, contentMaxCross};
            } else {
                const float budget = std::max(0.0F, allocated);
                childConstraints =
                    Constraints{0.0F, contentMaxCross, child.shrinkWrap ? 0.0F : budget, budget};
            }
            measured[i] = layoutSingle(child, childConstraints, styleContext,
                                       childIdentity(identity, child, i));
            const float childMain =
                (isRow ? measured[i].size.width : measured[i].size.height) +
                (isRow ? child.margin.horizontal() : child.margin.vertical());
            const float childCross =
                (isRow ? measured[i].size.height : measured[i].size.width) +
                (isRow ? child.margin.vertical() : child.margin.horizontal());
            mainWithMargin[i] = childMain;
            crossWithMargin[i] = childCross;
        }
    }

    float contentMain = 0.0F;
    float contentCross = 0.0F;
    for (std::size_t i = 0; i < count; ++i) {
        contentMain += mainWithMargin[i];
        contentCross = std::max(contentCross, crossWithMargin[i]);
    }
    if (count > 0) {
        contentMain += baseSpacing;
    }

    float borderMain = clampFloat(contentMain + paddingMain, outerMinMain,
                                  outerMaxMain);
    float borderCross = clampFloat(contentCross + paddingCross, outerMinCross,
                                   outerMaxCross);
    if (mainOverride.has_value()) {
        borderMain = resolvedMain;
    }
    if (crossOverride.has_value()) {
        borderCross = resolvedCross;
    }
    // 集合行（collection-controls-design §10.1）：行最小高度走视觉系统
    // §3.2 尺度表（resolveContainer 注入 metrics.minHeight[baseIndex]）；
    // 变高项（多行内容）按内容增高不受影响。
    if (widget.collectionRow && !crossOverride.has_value() &&
        resolved.minHeight > borderCross) {
        borderCross =
            clampFloat(resolved.minHeight, outerMinCross, outerMaxCross);
    }
    // contentBoxMain is derived after the stretch pass because stretching can
    // change child main sizes. The cross box must exist beforehand: it is the
    // stretch budget itself.
    const float contentBoxCross = std::max(0.0F, borderCross - paddingCross);

    // Stretch pass: constraints below are pre-margin budgets. layoutSingle()
    // deflates the child margin, so the resulting border box lands exactly on
    // contentBoxCross minus the margin (no double subtraction).
    const bool stretch =
        widget.crossAxis == core::CrossAxisAlignment::Stretch;
    if (stretch) {
        for (std::size_t i = 0; i < count; ++i) {
            const Widget& child = widget.children[i];
            const float marginMain = isRow ? child.margin.horizontal()
                                           : child.margin.vertical();
            const bool isFlexChild = child.flex > 0.0F && distributeFlex;
            Constraints stretchConstraints;
            if (isRow) {
                const float mainBudget =
                    measured[i].size.width + marginMain;
                if (isFlexChild) {
                    stretchConstraints = Constraints{mainBudget, mainBudget,
                                                     contentBoxCross,
                                                     contentBoxCross};
                } else {
                    stretchConstraints = Constraints{0.0F, contentMaxMain,
                                                     contentBoxCross,
                                                     contentBoxCross};
                }
            } else {
                const float mainBudget =
                    measured[i].size.height + marginMain;
                if (isFlexChild) {
                    stretchConstraints = Constraints{contentBoxCross,
                                                     contentBoxCross,
                                                     mainBudget, mainBudget};
                } else {
                    stretchConstraints = Constraints{contentBoxCross,
                                                     contentBoxCross, 0.0F,
                                                     contentMaxMain};
                }
            }
            measured[i] = layoutSingle(child, stretchConstraints, styleContext,
                                       childIdentity(identity, child, i));
            crossWithMargin[i] =
                (isRow ? measured[i].size.height : measured[i].size.width) +
                (isRow ? child.margin.vertical() : child.margin.horizontal());
            mainWithMargin[i] =
                (isRow ? measured[i].size.width : measured[i].size.height) +
                marginMain;
        }
        // The stretch pass re-measured children under tight cross
        // constraints; a child whose main size depends on its cross size can
        // change, so re-derive the main border size. The cross border size
        // stays fixed: the stretch budget was derived from it.
        float stretchedMain = baseSpacing;
        for (float value : mainWithMargin) {
            stretchedMain += value;
        }
        borderMain = clampFloat(stretchedMain + paddingMain, outerMinMain,
                                outerMaxMain);
        if (mainOverride.has_value()) {
            borderMain = resolvedMain;
        }
    }
    const float contentBoxMain = std::max(0.0F, borderMain - paddingMain);

    const float totalChildrenMain =
        std::accumulate(mainWithMargin.begin(), mainWithMargin.end(), 0.0F) +
        baseSpacing;
    const float freeSpace = std::max(0.0F, contentBoxMain - totalChildrenMain);
    const float start = mainAxisStart(widget.mainAxis, freeSpace, count);
    const float extraGap = mainAxisGap(widget.mainAxis, freeSpace, count);
    // `spacing` is the minimum gap; Space* alignments add on top.
    float cursor = start;

    RenderNode node = makeNode(
        widget, Offset{0.0F, 0.0F},
        isRow ? Size{borderMain, borderCross} : Size{borderCross, borderMain},
        styleContext, identity);

    for (std::size_t i = 0; i < count; ++i) {
        const Widget& child = widget.children[i];
        const float childMainNoMargin =
            isRow ? measured[i].size.width : measured[i].size.height;
        const float childCrossNoMargin =
            isRow ? measured[i].size.height : measured[i].size.width;
        const float childMarginMain = isRow ? child.margin.horizontal()
                                            : child.margin.vertical();
        const float childMarginCross = isRow ? child.margin.vertical()
                                             : child.margin.horizontal();
        const float childCrossTotal = childCrossNoMargin + childMarginCross;
        const float freeCross =
            std::max(0.0F, contentBoxCross - childCrossTotal);
        const float crossOffset = crossAxisOffset(widget.crossAxis, freeCross);

        Offset childOffset{};
        if (isRow) {
            childOffset = Offset{
                padding.left + cursor + child.margin.left,
                padding.top + crossOffset + child.margin.top,
            };
            cursor +=
                childMainNoMargin + childMarginMain + widget.spacing + extraGap;
        } else {
            childOffset = Offset{
                padding.left + crossOffset + child.margin.left,
                padding.top + cursor + child.margin.top,
            };
            cursor +=
                childMainNoMargin + childMarginMain + widget.spacing + extraGap;
        }
        measured[i].offset = childOffset;
        node.children.push_back(std::move(measured[i]));
    }

    return node;
}

// v0.3 阶段8D (plan §3.4): 滚动视口。子内容主轴（纵向）不受限，横向
// 受视口约束；scrollOffset 应用到子 offset（夹取到 [0, scrollExtent]），
// painter 按视口裁剪（RenderNode.clipContent）。
RenderNode layoutScrollView(const Widget& widget,
                            const Constraints& constraints,
                            const style::StyleContext& styleContext,
                            const std::string& identity) {
    assert(widget.children.size() <= 1);
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const EdgeInsets& padding = core::commonStyle(resolved).padding;
    const Constraints outer = constraints.deflate(widget.margin);

    float viewportWidth =
        widget.width.has_value()
            ? clampFloat(*widget.width, outer.minWidth, outer.maxWidth)
            : outer.maxWidth;
    float viewportHeight =
        widget.height.has_value()
            ? clampFloat(*widget.height, outer.minHeight, outer.maxHeight)
            : outer.maxHeight;

    RenderNode node = makeNode(widget, Offset{0.0F, 0.0F},
                               Size{viewportWidth, viewportHeight},
                               styleContext, identity);
    node.clipContent = true;

    if (widget.children.empty()) {
        node.scrollExtent = 0.0F;
        node.scrollOffset = 0.0F;
        return node;
    }

    const Widget& child = widget.children.front();
    // 内容约束：宽 ≤ 视口 - padding，高不限（滚动视口语义）。
    const float contentMaxWidth =
        std::max(0.0F, viewportWidth - padding.horizontal());
    Constraints childConstraints{0.0F, contentMaxWidth, 0.0F,
                                 Constraints::unbounded().maxHeight};
    RenderNode childNode = layoutSingle(child, childConstraints, styleContext,
                                        childIdentity(identity, child, 0));
    const float contentHeight = childNode.size.height +
                                child.margin.vertical() +
                                padding.vertical();
    if (widget.shrinkWrap && !widget.height.has_value()) {
        viewportHeight = clampFloat(contentHeight, outer.minHeight, outer.maxHeight);
        node.size.height = viewportHeight;
    }
    const float scrollExtent =
        std::max(0.0F, contentHeight - viewportHeight);
    const float offset = std::clamp(widget.scrollOffset, 0.0F, scrollExtent);

    childNode.offset = Offset{
        padding.left + child.margin.left,
        padding.top + child.margin.top - offset};
    node.children.push_back(std::move(childNode));
    node.scrollExtent = scrollExtent;
    node.scrollOffset = offset;
    return node;
}

// M3（自用路线图）：Grid。子项按阅读顺序填入；固定列数
//（columnCount>0）或按最小列宽自适应（列数 =
// floor((可用宽+列间距)/(最小列宽+列间距))，至少 1 列）。单元宽度均分
//（(可用宽-(列-1)*列间距)/列）；行高 = 行内子项外部高度最大值；行间用
// gridRowGap。子项约束：宽度 ≤ 单元宽（子项自身决定填充），高度不限。
// 窗口变化重排由约束传播自然发生（几何确定性：同约束同结果）。
RenderNode layoutGrid(const Widget& widget, const Constraints& constraints,
                      const style::StyleContext& styleContext,
                      const std::string& identity) {
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const EdgeInsets& padding = core::commonStyle(resolved).padding;
    const Constraints outer = constraints.deflate(widget.margin);

    const float availableWidth =
        widget.width.has_value()
            ? clampFloat(*widget.width, outer.minWidth, outer.maxWidth)
            : outer.maxWidth;
    const float contentWidth =
        std::max(0.0F, availableWidth - padding.horizontal());
    const float columnGap = std::max(0.0F, widget.gridColumnGap);
    int columns = widget.gridColumnCount;
    if (columns <= 0) {
        const float minWidth = widget.gridMinColumnWidth;
        columns = minWidth > 0.0F
                      ? static_cast<int>(std::floor(
                            (contentWidth + columnGap) /
                            (minWidth + columnGap)))
                      : 1;
        columns = std::max(1, columns);
    }
    const int safeColumns = std::max(1, columns);
    const float cellWidth =
        std::max(0.0F, (contentWidth - static_cast<float>(safeColumns - 1) *
                                            columnGap) /
                           static_cast<float>(safeColumns));
    const float rowGap = std::max(0.0F, widget.gridRowGap);

    RenderNode node = makeNode(widget, Offset{0.0F, 0.0F},
                               Size{availableWidth, 0.0F}, styleContext,
                               identity);
    // 先分单元约束布局全部子项，再按行聚合定位（行高 = 行内最大）。
    struct Cell {
        RenderNode node{};
        float outerHeight{0.0F};
    };
    std::vector<Cell> cells;
    cells.reserve(widget.children.size());
    for (std::size_t i = 0; i < widget.children.size(); ++i) {
        const Widget& child = widget.children[i];
        // 单元宽度紧约束（网格语义：单元格均匀填满列宽；子项高度
        // 自适应，行高 = 行内最大外部高度）。
        const Constraints childConstraints{
            cellWidth, cellWidth, 0.0F, Constraints::unbounded().maxHeight};
        RenderNode childNode =
            layoutSingle(child, childConstraints, styleContext,
                         childIdentity(identity, child, i));
        cells.push_back(Cell{std::move(childNode),
                             childNode.size.height + child.margin.vertical()});
    }

    const std::size_t rows =
        (widget.children.size() + static_cast<std::size_t>(safeColumns) - 1) /
        static_cast<std::size_t>(safeColumns);
    float contentHeight = 0.0F;
    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t begin =
            row * static_cast<std::size_t>(safeColumns);
        const std::size_t end =
            std::min(widget.children.size(),
                     begin + static_cast<std::size_t>(safeColumns));
        float rowHeight = 0.0F;
        for (std::size_t i = begin; i < end; ++i) {
            rowHeight = std::max(rowHeight, cells[i].outerHeight);
        }
        for (std::size_t i = begin; i < end; ++i) {
            Cell& cell = cells[i];
            const std::size_t column = i - begin;
            cell.node.offset = Offset{
                padding.left + static_cast<float>(column) *
                                   (cellWidth + columnGap) +
                                   widget.children[i].margin.left,
                padding.top + contentHeight +
                    widget.children[i].margin.top};
            node.children.push_back(std::move(cell.node));
        }
        contentHeight += rowHeight;
        if (row + 1 < rows) {
            contentHeight += rowGap;
        }
    }
    contentHeight += padding.vertical();

    node.size = Size{availableWidth,
                     widget.height.has_value()
                         ? clampFloat(*widget.height, outer.minHeight,
                                      outer.maxHeight)
                         : outer.constrain(Size{availableWidth, contentHeight})
                               .height};
    return node;
}

// M3：VirtualList。数据源（itemCount/estimatedExtent/extentOf/
// scrollOffset/buildItem/noteExtent）驱动：可见区（含缓存）经
// buildItem 物化，子项绝对定位在内容坐标（offsetOfIndex），滚动偏移
// 应用到子 offset；实测修正后继续物化到视口填满。每项在本次布局中
// 仅构建/测量一次，避免估值偏大时留下空白或反复构建同一项目。
// 虚拟化行区物化（VirtualList 与 TreeList 共用的 M3 引擎 §3.4）：可见区
//（含 cacheExtent 缓存）行构建/测量/绝对定位，extent 修正后继续物化到
// 稳定（每项本轮仅构建一次，至多物化 itemCount 项）。两种视口的差异显
// 式参数化：rowsViewport 为行区视口高；rangeShift 把 scrollOffset 换算
// 为行内容坐标（VirtualList 内容含顶部 padding）；childBaseY 为行区内
// 容原点（padding.top[ + 表头高]）；contentExtentPad 为滚动内容高在
// totalExtent 之外的附加（VirtualList 的上下 padding）。
void materializeVirtualRows(RenderNode& node,
                            const VirtualListSource* source,
                            const style::StyleContext& styleContext,
                            const std::string& identity,
                            const EdgeInsets& padding, float contentMaxWidth,
                            float cacheExtent, float rowsViewport,
                            float rangeShift, float childBaseY,
                            float contentExtentPad) {
    struct MaterializedItem {
        RenderNode node;
        EdgeInsets margin;
    };
    std::map<std::size_t, MaterializedItem> materialized;
    for (;;) {
        node.scrollExtent = std::max(
            0.0F, source->totalExtent() + contentExtentPad - rowsViewport);
        node.scrollOffset =
            std::clamp(source->scrollOffset(), 0.0F, node.scrollExtent);
        const auto [first, last] = source->visibleRangeAt(
            node.scrollOffset - rangeShift, rowsViewport, cacheExtent);
        bool extentsChanged = false;
        for (std::size_t i = first; i < last; ++i) {
            if (materialized.contains(i)) {
                continue;
            }
            Widget item = source->buildItem(i);
            if (t_prepareItem != nullptr && *t_prepareItem) {
                (*t_prepareItem)(item);
            }
            const Constraints childConstraints{
                0.0F, contentMaxWidth, 0.0F,
                Constraints::unbounded().maxHeight};
            RenderNode childNode = layoutSingle(
                item, childConstraints, styleContext,
                childIdentity(identity, item, i));
            const float assumed = source->extentOf(i);
            source->noteExtent(i, childNode.size.height + item.margin.vertical());
            extentsChanged = extentsChanged || source->extentOf(i) != assumed;
            materialized.emplace(i, MaterializedItem{std::move(childNode), item.margin});
        }
        if (extentsChanged) {
            // 每次修正都来自首次物化的新项，因此至多物化 itemCount 项。
            // 下一轮沿用已测节点，只补齐修正后进入可见区的项目。
            continue;
        }
        for (std::size_t i = first; i < last; ++i) {
            auto& item = materialized.at(i);
            item.node.offset = Offset{
                padding.left + item.margin.left,
                childBaseY + item.margin.top + source->offsetOfIndex(i) -
                    node.scrollOffset};
            node.children.push_back(std::move(item.node));
        }
        break;
    }
}

RenderNode layoutVirtualList(const Widget& widget,
                             const Constraints& constraints,
                             const style::StyleContext& styleContext,
                             const std::string& identity) {
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const EdgeInsets& padding = core::commonStyle(resolved).padding;
    const Constraints outer = constraints.deflate(widget.margin);

    const float viewportWidth =
        widget.width.has_value()
            ? clampFloat(*widget.width, outer.minWidth, outer.maxWidth)
            : outer.maxWidth;
    const float viewportHeight =
        widget.height.has_value()
            ? clampFloat(*widget.height, outer.minHeight, outer.maxHeight)
            : outer.maxHeight;

    RenderNode node = makeNode(widget, Offset{0.0F, 0.0F},
                               Size{viewportWidth, viewportHeight},
                               styleContext, identity);
    node.clipContent = true;

    const VirtualListSource* source = widget.virtualSource;
    if (source != nullptr) {
        source->updateViewport(viewportHeight, padding.vertical());
    }
    if (source == nullptr || source->itemCount() == 0) {
        node.scrollExtent = 0.0F;
        node.scrollOffset = 0.0F;
        return node;
    }

    materializeVirtualRows(
        node, source, styleContext, identity, padding,
        std::max(0.0F, viewportWidth - padding.horizontal()),
        std::max(0.0F, widget.virtualCacheExtent),
        /*rowsViewport=*/viewportHeight,
        /*rangeShift=*/padding.top,
        /*childBaseY=*/padding.top,
        /*contentExtentPad=*/padding.vertical());
    return node;
}

// 集合控件（collection-controls-design §8.3）：TreeList = VirtualList 引擎
// + 粘性表头。表头是非滚动 chrome：noteContentWidth 回填列宽 → 布局实测
// 表头高度 → 行区视口 = 视口高 - 表头高；行滚动偏移只作用于行区，表头
// 固定在顶部（QHeaderView/wx report 模式语义）。
RenderNode layoutTreeList(const Widget& widget,
                          const Constraints& constraints,
                          const style::StyleContext& styleContext,
                          const std::string& identity) {
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const EdgeInsets& padding = core::commonStyle(resolved).padding;
    const Constraints outer = constraints.deflate(widget.margin);

    const float viewportWidth =
        widget.width.has_value()
            ? clampFloat(*widget.width, outer.minWidth, outer.maxWidth)
            : outer.maxWidth;
    const float viewportHeight =
        widget.height.has_value()
            ? clampFloat(*widget.height, outer.minHeight, outer.maxHeight)
            : outer.maxHeight;

    RenderNode node = makeNode(widget, Offset{0.0F, 0.0F},
                               Size{viewportWidth, viewportHeight},
                               styleContext, identity);
    node.clipContent = true;

    const VirtualListSource* source = widget.virtualSource;
    const float contentMaxWidth =
        std::max(0.0F, viewportWidth - padding.horizontal());
    if (source != nullptr) {
        // 列宽按内容宽分配（控制器幂等缓存；视口变化时自动重排）。
        source->noteContentWidth(contentMaxWidth);
    }

    // 粘性表头：布局一次、置于顶部、不随滚动平移。不透明 surface 底
    //（collection-design §8.3/§10.3：treelist.header.background；行滚
    // 入表头区时被盖住，不与表头文字叠加）。
    RenderNode headerNode{};
    float headerHeight = 0.0F;
    if (widget.collectionShowHeader && source != nullptr) {
        Widget header = source->buildHeader();
        if (header.type != WidgetType::Container || !header.children.empty() ||
            !header.key.empty()) {
            header.color = styleContext.theme.colors.surface;
            headerNode = layoutSingle(
                header,
                Constraints{0.0F, contentMaxWidth, 0.0F,
                            Constraints::unbounded().maxHeight},
                styleContext, childIdentity(identity, header, 0));
            headerNode.offset = Offset{padding.left + header.margin.left,
                                       padding.top + header.margin.top};
            headerHeight = headerNode.size.height + header.margin.vertical();
        }
    }

    const float rowsViewport =
        std::max(0.0F, viewportHeight - headerHeight - padding.vertical());
    if (source != nullptr) {
        // 行区视口（表头下方）同步给滚动控制器。
        source->updateViewport(rowsViewport, 0.0F);
    }
    if (source == nullptr || source->itemCount() == 0) {
        node.scrollExtent = 0.0F;
        node.scrollOffset = 0.0F;
        if (headerHeight > 0.0F) {
            node.children.push_back(std::move(headerNode));
        }
        return node;
    }

    // 行区先物化，表头后入 children（非滚动 chrome 盖在行区之上：
    // 绘制/命中都是后子节点在上，滚入表头区的行被不透明表头盖住）。
    materializeVirtualRows(
        node, source, styleContext, identity, padding, contentMaxWidth,
        std::max(0.0F, widget.virtualCacheExtent),
        /*rowsViewport=*/rowsViewport,
        /*rangeShift=*/0.0F,
        /*childBaseY=*/padding.top + headerHeight,
        /*contentExtentPad=*/0.0F);
    if (headerHeight > 0.0F) {
        node.children.push_back(std::move(headerNode));
    }
    return node;
}

// Splitter（splitter-design §6）：两窗格主轴精确分配 + 框架物化分隔条。
// 位置 = 源 offset（首次布局播种 initial），钳制 [minLeading, 主轴可用 −
// minTrailing]；极端窄窗按最小值比例压缩（不重叠不崩）。分隔条为 Button
// 节点（key 前缀 "split:div:"，collectionRow 可聚焦；onClick 不注册
// handler——单击无操作，拖动/键盘/双击复位由交互层经 splitterSource 接
// 管）；painter 按 splitterSource 特判绘制居中轨道线。
RenderNode layoutSplitter(const Widget& widget, const Constraints& constraints,
                          const style::StyleContext& styleContext,
                          const std::string& identity) {
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const EdgeInsets& padding = core::commonStyle(resolved).padding;
    const Constraints outer = constraints.deflate(widget.margin);

    const float width =
        widget.width.has_value()
            ? clampFloat(*widget.width, outer.minWidth, outer.maxWidth)
            : outer.maxWidth;
    const float height =
        widget.height.has_value()
            ? clampFloat(*widget.height, outer.minHeight, outer.maxHeight)
            : outer.maxHeight;
    RenderNode node = makeNode(widget, Offset{0.0F, 0.0F},
                               Size{width, height}, styleContext, identity);

    const SplitterSource* source = widget.splitterSource;
    if (widget.children.size() < 2 || source == nullptr) {
        // 声明不完整（children/source 缺失）：保持空容器，不崩溃。
        return node;
    }

    const bool horizontal = widget.splitterHorizontal;
    const float crossMax =
        horizontal ? std::max(0.0F, height - padding.vertical())
                   : std::max(0.0F, width - padding.horizontal());
    const float dividerExtent =
        core::splitterHitExtent(styleContext.theme.metrics.baseIndex);
    const float trackThickness = core::kSplitterTrackThickness;
    const float mainMax =
        std::max(0.0F,
                 (horizontal ? width - padding.horizontal()
                             : height - padding.vertical()) -
                     trackThickness);

    float offset = source->seeded() ? source->offsetPx()
                                    : source->initialOffset();
    const float minLead = std::max(0.0F, source->minLeading());
    const float minTrail = std::max(0.0F, source->minTrailing());
    if (mainMax < minLead + minTrail) {
        offset = minLead + minTrail > 0.0F
                     ? mainMax * minLead / (minLead + minTrail)
                     : 0.0F;
    } else {
        offset = std::clamp(offset, minLead, mainMax - minTrail);
    }
    source->noteLayout(offset, mainMax);

    const float leadingMain = offset;
    const float trailingMain = std::max(0.0F, mainMax - leadingMain);
    const auto paneConstraints = [horizontal, crossMax](float main) {
        return horizontal ? Constraints{main, main, crossMax, crossMax}
                          : Constraints{crossMax, crossMax, main, main};
    };

    Widget divider;
    divider.type = WidgetType::Button;
    divider.key =
        "split:div:" + (widget.key.empty() ? identity : widget.key);
    divider.onClick = divider.key;  // 无注册 handler：单击无操作
    divider.collectionRow = true;   // Tab 可聚焦（键盘步进）
    divider.semanticsRole = "splitter";
    divider.semanticsLabel = "分栏调节";
    const int percent = mainMax > 0.0F
                            ? static_cast<int>(std::lround(
                                  offset / mainMax * 100.0F))
                            : 0;
    divider.semanticsValue = std::to_string(percent) + "%";
    divider.semanticsActions = accessibility::kActionFocus |
                               accessibility::kActionSetValue;
    divider.enabled = widget.enabled;
    divider.buttonVariant = core::ButtonVariant::Ghost;

    const std::string leadId =
        childIdentity(identity, widget.children[0], 0);
    const std::string dividerId = childIdentity(identity, divider, 1);
    const std::string trailId =
        childIdentity(identity, widget.children[1], 2);

    RenderNode leading =
        layoutSingle(widget.children[0], paneConstraints(leadingMain),
                     styleContext, leadId);
    RenderNode dividerNode = layoutSingle(
        divider,
        horizontal
            ? Constraints{dividerExtent, dividerExtent, crossMax, crossMax}
            : Constraints{crossMax, crossMax, dividerExtent, dividerExtent},
        styleContext, dividerId);
    dividerNode.splitterSource = source;
    RenderNode trailing =
        layoutSingle(widget.children[1], paneConstraints(trailingMain),
                     styleContext, trailId);

    if (horizontal) {
        leading.offset = Offset{padding.left, padding.top};
        dividerNode.offset = Offset{
            padding.left + leadingMain -
                (dividerExtent - trackThickness) * 0.5F,
            padding.top};
        trailing.offset =
            Offset{padding.left + leadingMain + trackThickness,
                   padding.top};
    } else {
        leading.offset = Offset{padding.left, padding.top};
        dividerNode.offset = Offset{
            padding.left,
            padding.top + leadingMain -
                (dividerExtent - trackThickness) * 0.5F};
        trailing.offset =
            Offset{padding.left,
                   padding.top + leadingMain + trackThickness};
    }
    node.children.push_back(std::move(leading));
    node.children.push_back(std::move(dividerNode));
    node.children.push_back(std::move(trailing));
    return node;
}

void stackAlignmentFactors(core::StackAlignment alignment, float& xFactor,
                           float& yFactor) {
    using core::StackAlignment;
    switch (alignment) {
        case StackAlignment::TopLeft:
            xFactor = 0.0F;
            yFactor = 0.0F;
            break;
        case StackAlignment::TopCenter:
            xFactor = 0.5F;
            yFactor = 0.0F;
            break;
        case StackAlignment::TopRight:
            xFactor = 1.0F;
            yFactor = 0.0F;
            break;
        case StackAlignment::CenterLeft:
            xFactor = 0.0F;
            yFactor = 0.5F;
            break;
        case StackAlignment::Center:
            xFactor = 0.5F;
            yFactor = 0.5F;
            break;
        case StackAlignment::CenterRight:
            xFactor = 1.0F;
            yFactor = 0.5F;
            break;
        case StackAlignment::BottomLeft:
            xFactor = 0.0F;
            yFactor = 1.0F;
            break;
        case StackAlignment::BottomCenter:
            xFactor = 0.5F;
            yFactor = 1.0F;
            break;
        case StackAlignment::BottomRight:
            xFactor = 1.0F;
            yFactor = 1.0F;
            break;
    }
}

// S3（§6.8）：Tabs 布局——先解析行样式，再改写子按钮（Ghost + 前景
/// padding 覆盖），最后按 Row 布局。子节点 selected 声明原样保留。
RenderNode layoutTabs(const Widget& widget, const Constraints& constraints,
                      const style::StyleContext& styleContext,
                      const std::string& identity) {
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const auto* tabs =
        std::get_if<core::TabsResolvedStyle>(&resolved.component);
    Widget row = widget;
    if (tabs != nullptr) {
        row.spacing = styleContext.theme.tabs.tabGap;
        for (Widget& child : row.children) {
            if (child.type != WidgetType::Button) {
                continue;
            }
            child.buttonVariant = core::ButtonVariant::Ghost;
            bool disabled = !child.enabled;
            if (styleContext.previewStates) {
                const auto preview = styleContext.previewStates->find(child.key);
                if (preview != styleContext.previewStates->end()) disabled = preview->second.disabled;
            }
            if (!child.styleOverrides.foreground.has_value()) {
                child.styleOverrides.foreground =
                    disabled ? styleContext.theme.colors.disabledContent
                    : child.selected ? tabs->selectedContent
                                     : tabs->unselectedContent;
            }
            if (!child.styleOverrides.padding.has_value()) {
                child.styleOverrides.padding = core::EdgeInsets::symmetric(
                    styleContext.theme.tabs.tabPaddingX, 0.0F);
            }
        }
    }
    const auto outer = constraints.deflate(widget.margin);
    const float width = widget.width.has_value()
        ? clampFloat(*widget.width, outer.minWidth, outer.maxWidth) : outer.maxWidth;
    RenderNode node = makeNode(widget, {}, {}, styleContext, identity);
    const float contentWidth = std::max(0.0F, width - node.padding.horizontal());
    const float contentHeight = std::max(0.0F, outer.maxHeight - node.padding.vertical());
    float x = 0.0F, y = 0.0F, lineHeight = 0.0F, usedWidth = 0.0F;
    const float indicatorSpace = tabs ? tabs->indicatorHeight + 2.0F : 0.0F;
    for (std::size_t i = 0; i < row.children.size(); ++i) {
        const auto& child = row.children[i];
        auto item = layoutSingle(child, Constraints{0.0F, contentWidth, 0.0F, contentHeight},
                                 styleContext, childIdentity(identity, child, i));
        const float itemWidth = item.size.width + child.margin.horizontal();
        if (x > 0.0F && x + itemWidth > contentWidth) {
            y += lineHeight + row.spacing;
            x = 0.0F;
            lineHeight = 0.0F;
        }
        item.offset = Offset{node.padding.left + x + child.margin.left,
                             node.padding.top + y + child.margin.top};
        lineHeight = std::max(lineHeight, item.size.height + child.margin.vertical() + indicatorSpace);
        usedWidth = std::max(usedWidth, x + itemWidth);
        x += itemWidth + row.spacing;
        node.children.push_back(std::move(item));
    }
    node.size = outer.constrain(Size{widget.width.has_value() ? width : usedWidth + node.padding.horizontal(),
        widget.height.value_or(y + lineHeight + node.padding.vertical())});
    node.clipContent = true;
    return node;
}

RenderNode layoutStack(const Widget& widget, const Constraints& constraints,
                       const style::StyleContext& styleContext,
                       const std::string& identity) {
    const ResolvedStyle resolved =
        style::resolveStyle(widget, styleContext, identity);
    const EdgeInsets& padding = core::commonStyle(resolved).padding;
    const Constraints outer = constraints.deflate(widget.margin);
    // Resolve explicit sizes before measuring so children are constrained by
    // the border box the stack will actually occupy (same rule as Container).
    const float resolvedWidth =
        widget.width.has_value()
            ? clampFloat(*widget.width, outer.minWidth, outer.maxWidth)
            : 0.0F;
    const float resolvedHeight =
        widget.height.has_value()
            ? clampFloat(*widget.height, outer.minHeight, outer.maxHeight)
            : 0.0F;
    const float contentMaxWidth = std::max(
        0.0F,
        (widget.width.has_value() ? resolvedWidth : outer.maxWidth) -
            padding.horizontal());
    const float contentMaxHeight = std::max(
        0.0F,
        (widget.height.has_value() ? resolvedHeight : outer.maxHeight) -
        padding.vertical());

    std::vector<RenderNode> measured;
    measured.reserve(widget.children.size());
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;
    for (std::size_t i = 0; i < widget.children.size(); ++i) {
        const Widget& child = widget.children[i];
        Constraints childConstraints{0.0F, contentMaxWidth, 0.0F,
                                     contentMaxHeight};
        RenderNode childNode = layoutSingle(child, childConstraints,
                                            styleContext,
                                            childIdentity(identity, child, i));
        contentWidth = std::max(
            contentWidth, childNode.size.width + child.margin.horizontal());
        contentHeight = std::max(
            contentHeight, childNode.size.height + child.margin.vertical());
        measured.push_back(std::move(childNode));
    }

    float borderWidth =
        clampFloat(contentWidth + padding.horizontal(), outer.minWidth,
                   outer.maxWidth);
    float borderHeight = clampFloat(contentHeight + padding.vertical(),
                                    outer.minHeight, outer.maxHeight);
    if (widget.width.has_value()) {
        borderWidth = resolvedWidth;
    }
    if (widget.height.has_value()) {
        borderHeight = resolvedHeight;
    }
    const float contentBoxWidth =
        std::max(0.0F, borderWidth - padding.horizontal());
    const float contentBoxHeight =
        std::max(0.0F, borderHeight - padding.vertical());

    float xFactor = 0.0F;
    float yFactor = 0.0F;
    stackAlignmentFactors(widget.stackAlignment, xFactor, yFactor);

    RenderNode node =
        makeNode(widget, Offset{0.0F, 0.0F}, Size{borderWidth, borderHeight},
                 styleContext, identity);
    for (std::size_t i = 0; i < measured.size(); ++i) {
        const Widget& child = widget.children[i];
        Offset childOffset{};
        if (child.stackPosition.has_value()) {
            childOffset = Offset{
                padding.left + child.stackPosition->x +
                    child.margin.left,
                padding.top + child.stackPosition->y +
                    child.margin.top,
            };
        } else {
            const float freeWidth =
                std::max(0.0F, contentBoxWidth - measured[i].size.width -
                                   child.margin.horizontal());
            const float freeHeight = std::max(
                0.0F, contentBoxHeight - measured[i].size.height -
                         child.margin.vertical());
            childOffset = Offset{
                padding.left + child.margin.left + freeWidth * xFactor,
                padding.top + child.margin.top + freeHeight * yFactor,
            };
        }
        measured[i].offset = childOffset;
        node.children.push_back(std::move(measured[i]));
    }
    return node;
}

}  // namespace

core::RenderNode LayoutEngine::layout(const core::Widget& widget,
                                       const core::Constraints& constraints,
                                       const style::StyleContext& styleContext) {
    return layoutSingle(widget, constraints, styleContext,
                        childIdentity({}, widget, 0));
}

core::RenderNode LayoutEngine::layout(const core::Widget& widget,
                                       const core::Constraints& constraints,
                                       const style::StyleContext& styleContext,
                                       const text::FontManager& fonts,
                                       const PrepareItem& prepareItem) {
    const ScopedFonts guard{fonts};
    const ScopedItemPreparation preparation{prepareItem};
    return layoutSingle(widget, constraints, styleContext,
                        childIdentity({}, widget, 0));
}

core::RenderNode LayoutEngine::layout(const core::Widget& widget,
                                       const core::Constraints& constraints,
                                       const text::FontManager& fonts) {
    const style::Theme theme = style::Theme::dark();
    const style::InteractionStateSnapshot interaction;
    const accessibility::AccessibilitySettings settings;
    return layout(widget, constraints,
                  style::StyleContext{theme, interaction, settings}, fonts);
}

core::RenderNode LayoutEngine::layout(const core::Widget& widget,
                                       const core::Constraints& constraints) {
    // 便捷入口（DSL/headless/测试）：默认暗色主题、空交互状态。
    const style::Theme theme = style::Theme::dark();
    const style::InteractionStateSnapshot interaction;
    const accessibility::AccessibilitySettings settings;
    return layout(widget, constraints,
                  style::StyleContext{theme, interaction, settings});
}

core::Size LayoutEngine::intrinsicSize(const core::Widget& widget,
                                       const core::Constraints& constraints,
                                       const style::StyleContext& styleContext) {
    return layoutSingle(widget, constraints, styleContext,
                        childIdentity({}, widget, 0))
        .size;
}

core::Size LayoutEngine::intrinsicSize(const core::Widget& widget,
                                       const core::Constraints& constraints,
                                       const style::StyleContext& styleContext,
                                       const text::FontManager& fonts) {
    const ScopedFonts guard{fonts};
    return layoutSingle(widget, constraints, styleContext,
                        childIdentity({}, widget, 0))
        .size;
}

core::Size LayoutEngine::intrinsicSize(const core::Widget& widget,
                                       const core::Constraints& constraints,
                                       const text::FontManager& fonts) {
    const style::Theme theme = style::Theme::dark();
    const style::InteractionStateSnapshot interaction;
    const accessibility::AccessibilitySettings settings;
    return intrinsicSize(widget, constraints,
                         style::StyleContext{theme, interaction, settings},
                         fonts);
}

core::Size LayoutEngine::intrinsicSize(const core::Widget& widget,
                                       const core::Constraints& constraints) {
    const style::Theme theme = style::Theme::dark();
    const style::InteractionStateSnapshot interaction;
    const accessibility::AccessibilitySettings settings;
    return intrinsicSize(widget, constraints,
                         style::StyleContext{theme, interaction, settings});
}

namespace {

// 深度优先寻找第一个文本叶子（Text/TextField/Button）的 baseline。
bool findBaseline(const core::RenderNode& node, core::Offset absolute,
                  float& out) {
    const core::Offset origin = absolute + node.offset;
    switch (node.type) {
        case core::WidgetType::Text:
        case core::WidgetType::TextField:
        case core::WidgetType::Button: {
            const core::TextStyle& style = node.textStyle();
            const float fontSize =
                style.fontSize > 0.0F ? style.fontSize : 14.0F;
            const float lineCenterOffset =
                (node.size.height - fontSize * 1.2F) * 0.5F;
            out = origin.y + node.padding.top + lineCenterOffset +
                  fontSize * 0.8F;
            return true;
        }
        default:
            break;
    }
    for (const auto& child : node.children) {
        if (findBaseline(child, origin, out)) {
            return true;
        }
    }
    return false;
}

}  // namespace

float LayoutEngine::baseline(const core::RenderNode& node) {
    float result = -1.0F;
    if (findBaseline(node, core::Offset{}, result)) {
        return result;
    }
    return -1.0F;
}

}  // namespace lumen::layout
