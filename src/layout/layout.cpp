#include "lumen/layout/layout.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <numeric>

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
    node.iconStrokeWidth = styleContext.theme.icons.strokeWidth;
    node.icon = static_cast<std::uint8_t>(widget.icon);
    node.transitionAlpha = widget.transitionAlpha;
    node.showScrollbar = widget.showScrollbar;
    node.elevation = widget.elevation;
    if (widget.elevation > 0.0F) {
        // §4.5 阴影分级：层级数（1..3）查表得到 offset/blur/alpha；
        // 高对比模式各级 alpha 为 0（不产生阴影命令）。
        const style::ElevationTokens& elevation = styleContext.theme.elevation;
        const style::ElevationShadowParams& params =
            elevation.paramsFor(widget.elevation);
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
         widget.type == WidgetType::VirtualList)) {
        node.scrollbarThickness = styleContext.theme.scrollbar.thickness;
    }
    node.enabled = widget.enabled;
    node.invalid = widget.invalid;
    node.selected = widget.selected;
    return node;
}

// v0.3 阶段8B: 文本度量统一走 text::TextLayout（布局与绘制共用同一份
// 布局结果）。maxWidth <= 0 表示不换行；TextField 单行不换行（横向滚动
// 属于 8D 视口），Text 按约束换行并支持 maxLines/ellipsis。
Size measureTextContent(const std::string& content, const TextStyle& style,
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
    const text::TextLayoutResult& layout =
        cache.compute(content, effective, maxWidth, activeFonts());
    return layout.size;
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
            // painter 同源（visual-system §7.1）。
            const EdgeInsets& chrome = core::commonStyle(resolved).padding;
            const Size textSize = measureTextContent(content, textStyle, 0.0F,
                                                     false);
            const float width =
                std::max(textSize.width + chrome.horizontal(),
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
            // M6：轨道高度按行高派生；宽度默认填充（无 intrinsic 宽）。
            const float track = std::max(24.0F, textStyle.fontSize * 1.4F);
            return Size{0.0F, track};
        }
        case WidgetType::ProgressBar: {
            const float track = std::max(16.0F, textStyle.fontSize * 0.9F);
            return Size{0.0F, track};
        }
        case WidgetType::Radio: {
            // 同 Checkbox 度量（圆形指示）。
            const auto* checkbox =
                std::get_if<core::CheckboxResolvedStyle>(&resolved.component);
            if (checkbox == nullptr) {
                return measureTextContent(content, textStyle, 0.0F, false);
            }
            const Size label = measureTextContent(content, textStyle, 0.0F,
                                                  false);
            return Size{checkbox->indicatorSize + checkbox->labelGap +
                            label.width,
                        std::max(checkbox->indicatorSize, label.height)};
        }
        case WidgetType::Tooltip: {
            const Size text = measureTextContent(content, textStyle, maxWidth,
                                                 false);
            const EdgeInsets frame = EdgeInsets::all(6.0F);
            return Size{text.width + frame.horizontal(),
                        text.height + frame.vertical()};
        }
        case WidgetType::Checkbox: {
            const auto* checkbox =
                std::get_if<core::CheckboxResolvedStyle>(&resolved.component);
            if (checkbox == nullptr) {
                return measureTextContent(content, textStyle, 0.0F, false);
            }
            const Size label = measureTextContent(content, textStyle, 0.0F,
                                                  false);
            return Size{checkbox->indicatorSize + checkbox->labelGap +
                            label.width,
                        std::max(checkbox->indicatorSize, label.height)};
        }
        case WidgetType::Switch: {
            const auto* control =
                std::get_if<core::SwitchResolvedStyle>(&resolved.component);
            if (control == nullptr) {
                return measureTextContent(content, textStyle, 0.0F, false);
            }
            const Size label = measureTextContent(content, textStyle, 0.0F,
                                                  false);
            return Size{control->trackWidth + control->labelGap + label.width,
                        std::max(control->trackHeight, label.height)};
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
    // Button/TextField intrinsic measurement already includes their chrome
    // padding; text and generic leaves use the resolved box padding here.
    if (widget.type != WidgetType::Button &&
        widget.type != WidgetType::TextField) {
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
                    styleContext.accessibility, styleContext.deviceScale};
                const style::ScopedThemeOverride guard{*override};
                return layoutContainer(widget, constraints, scopedContext,
                                       identity);
            }
            return layoutContainer(widget, constraints, styleContext,
                                   identity);
        }
        case WidgetType::Tabs:
            return layoutFlex(widget, constraints, styleContext, identity,
                              true);
        case WidgetType::Grid:
            return layoutGrid(widget, constraints, styleContext, identity);
        case WidgetType::VirtualList:
            return layoutVirtualList(widget, constraints, styleContext,
                                     identity);
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
                    Constraints{budget, budget, 0.0F, contentMaxCross};
            } else {
                const float budget = std::max(0.0F, allocated);
                childConstraints =
                    Constraints{0.0F, contentMaxCross, budget, budget};
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

    const float contentMaxWidth =
        std::max(0.0F, viewportWidth - padding.horizontal());
    const float cacheExtent = std::max(0.0F, widget.virtualCacheExtent);

    struct MaterializedItem {
        RenderNode node;
        EdgeInsets margin;
    };
    std::map<std::size_t, MaterializedItem> materialized;
    for (;;) {
        node.scrollExtent = std::max(
            0.0F, source->totalExtent() + padding.vertical() - viewportHeight);
        node.scrollOffset = std::clamp(source->scrollOffset(), 0.0F,
                                        node.scrollExtent);
        const auto [first, last] = source->visibleRangeAt(
            node.scrollOffset - padding.top, viewportHeight, cacheExtent);
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
                padding.top + item.margin.top + source->offsetOfIndex(i) -
                    node.scrollOffset};
            node.children.push_back(std::move(item.node));
        }
        break;
    }
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
