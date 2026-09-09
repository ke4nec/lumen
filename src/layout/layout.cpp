#include "lumen/layout/layout.h"

#include <algorithm>
#include <cassert>
#include <numeric>

namespace lumen::layout {
namespace {

using core::Constraints;
using core::EdgeInsets;
using core::Offset;
using core::RenderNode;
using core::Size;
using core::Widget;
using core::WidgetType;

float clampFloat(float value, float low, float high) {
    return std::clamp(value, low, high);
}

// Counts Unicode code points so CJK/emoji text does not measure by UTF-8
// bytes.
std::size_t utf8Length(const std::string& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t advance = 1;
        if ((byte & 0x80U) == 0U) {
            advance = 1;
        } else if ((byte & 0xE0U) == 0xC0U) {
            advance = 2;
        } else if ((byte & 0xF0U) == 0xE0U) {
            advance = 3;
        } else if ((byte & 0xF8U) == 0xF0U) {
            advance = 4;
        }
        i += advance;
        ++count;
    }
    return count;
}

Size measureTextIntrinsic(const Widget& widget) {
    const float fontSize =
        widget.textStyle.fontSize > 0.0F ? widget.textStyle.fontSize : 14.0F;
    const float lineHeight = fontSize * 1.2F;
    const std::string& content =
        widget.text.empty() && !widget.placeholder.empty() ? widget.placeholder
                                                           : widget.text;
    const auto glyphs = static_cast<float>(utf8Length(content));
    return Size{glyphs * fontSize * 0.6F, lineHeight};
}

Size measureLeafIntrinsic(const Widget& widget) {
    const Size textSize = measureTextIntrinsic(widget);
    switch (widget.type) {
        case WidgetType::Text:
            return textSize;
        case WidgetType::Button: {
            // Button chrome: 12px horizontal + 8px vertical padding each
            // side, 64x32 minimum for a tappable target.
            const float width = std::max(textSize.width + 24.0F, 64.0F);
            const float height = std::max(textSize.height + 16.0F, 32.0F);
            return Size{width, height};
        }
        case WidgetType::TextField: {
            // Editable field: at least 80px of text width plus chrome.
            const float width = std::max(textSize.width, 80.0F) + 16.0F;
            const float height = textSize.height + 16.0F;
            return Size{width, height};
        }
        default:
            return textSize;
    }
}

RenderNode makeNode(const Widget& widget, Offset offset, Size size) {
    RenderNode node;
    node.type = widget.type;
    node.key = widget.key;
    node.offset = offset;
    node.size = size;
    return node;
}

RenderNode layoutLeaf(const Widget& widget, const Constraints& constraints) {
    const Constraints outer = constraints.deflate(widget.margin);
    Size intrinsic = measureLeafIntrinsic(widget);
    intrinsic.width += widget.padding.horizontal();
    intrinsic.height += widget.padding.vertical();
    Size size = outer.constrain(intrinsic);
    // Border-box overrides: fixed size wins over intrinsic measurement.
    if (widget.width.has_value()) {
        size.width =
            clampFloat(*widget.width, outer.minWidth, outer.maxWidth);
    }
    if (widget.height.has_value()) {
        size.height =
            clampFloat(*widget.height, outer.minHeight, outer.maxHeight);
    }
    return makeNode(widget, Offset{0.0F, 0.0F}, size);
}

RenderNode layoutContainer(const Widget& widget,
                           const Constraints& constraints);

RenderNode layoutFlex(const Widget& widget, const Constraints& constraints,
                      bool isRow);

RenderNode layoutStack(const Widget& widget, const Constraints& constraints);

RenderNode layoutSingle(const Widget& widget,
                        const Constraints& constraints) {
    if (core::isLeafWidget(widget.type)) {
        return layoutLeaf(widget, constraints);
    }
    switch (widget.type) {
        case WidgetType::Container:
            return layoutContainer(widget, constraints);
        case WidgetType::Row:
            return layoutFlex(widget, constraints, true);
        case WidgetType::Column:
            return layoutFlex(widget, constraints, false);
        case WidgetType::Stack:
            return layoutStack(widget, constraints);
        case WidgetType::Text:
        case WidgetType::Button:
        case WidgetType::TextField:
            return layoutLeaf(widget, constraints);
    }
    return layoutLeaf(widget, constraints);
}

RenderNode layoutContainer(const Widget& widget,
                           const Constraints& constraints) {
    // Container is single-child by contract (see widget.h); extra children
    // indicate a programming error.
    assert(widget.children.size() <= 1);

    const Constraints outer = constraints.deflate(widget.margin);
    const Constraints inner = outer.deflate(widget.padding);

    float borderWidth = widget.padding.horizontal();
    float borderHeight = widget.padding.vertical();
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
                std::max(0.0F, borderWidth - widget.padding.horizontal());
            childConstraints.minWidth = 0.0F;
            childConstraints.maxWidth = contentWidth;
        } else {
            childConstraints.minWidth = 0.0F;
            childConstraints.maxWidth = inner.maxWidth;
        }
        if (widget.height.has_value()) {
            const float contentHeight =
                std::max(0.0F, borderHeight - widget.padding.vertical());
            childConstraints.minHeight = 0.0F;
            childConstraints.maxHeight = contentHeight;
        } else {
            childConstraints.minHeight = 0.0F;
            childConstraints.maxHeight = inner.maxHeight;
        }
        childNode = layoutSingle(child, childConstraints);
        // The parent consumes the child margin, same rule as Row/Column/
        // Stack: it offsets the child inside the padded content box and
        // participates in the border size.
        childNode.offset = Offset{widget.padding.left + child.margin.left,
                                  widget.padding.top + child.margin.top};
        if (!widget.width.has_value()) {
            borderWidth = clampFloat(
                childNode.size.width + widget.padding.horizontal() +
                    child.margin.horizontal(),
                outer.minWidth, outer.maxWidth);
        }
        if (!widget.height.has_value()) {
            borderHeight = clampFloat(
                childNode.size.height + widget.padding.vertical() +
                    child.margin.vertical(),
                outer.minHeight, outer.maxHeight);
        }
    } else {
        if (!widget.width.has_value()) {
            borderWidth = clampFloat(widget.padding.horizontal(),
                                     outer.minWidth, outer.maxWidth);
        }
        if (!widget.height.has_value()) {
            borderHeight = clampFloat(widget.padding.vertical(),
                                      outer.minHeight, outer.maxHeight);
        }
    }

    // Clamp once more so tight parents always win (resize correctness).
    borderWidth = clampFloat(borderWidth, outer.minWidth, outer.maxWidth);
    borderHeight = clampFloat(borderHeight, outer.minHeight, outer.maxHeight);

    RenderNode node =
        makeNode(widget, Offset{0.0F, 0.0F}, Size{borderWidth, borderHeight});
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
                      bool isRow) {
    const Constraints outer = constraints.deflate(widget.margin);
    const float paddingMain = isRow ? widget.padding.horizontal()
                                    : widget.padding.vertical();
    const float paddingCross = isRow ? widget.padding.vertical()
                                     : widget.padding.horizontal();
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
        measured[i] = layoutSingle(child, childConstraints);
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
            measured[i] = layoutSingle(child, childConstraints);
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
            measured[i] = layoutSingle(child, stretchConstraints);
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
        isRow ? Size{borderMain, borderCross} : Size{borderCross, borderMain});

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
                widget.padding.left + cursor + child.margin.left,
                widget.padding.top + crossOffset + child.margin.top,
            };
            cursor +=
                childMainNoMargin + childMarginMain + widget.spacing + extraGap;
        } else {
            childOffset = Offset{
                widget.padding.left + crossOffset + child.margin.left,
                widget.padding.top + cursor + child.margin.top,
            };
            cursor +=
                childMainNoMargin + childMarginMain + widget.spacing + extraGap;
        }
        measured[i].offset = childOffset;
        node.children.push_back(std::move(measured[i]));
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

RenderNode layoutStack(const Widget& widget,
                       const Constraints& constraints) {
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
            widget.padding.horizontal());
    const float contentMaxHeight = std::max(
        0.0F,
        (widget.height.has_value() ? resolvedHeight : outer.maxHeight) -
            widget.padding.vertical());

    std::vector<RenderNode> measured;
    measured.reserve(widget.children.size());
    float contentWidth = 0.0F;
    float contentHeight = 0.0F;
    for (const auto& child : widget.children) {
        Constraints childConstraints{0.0F, contentMaxWidth, 0.0F,
                                     contentMaxHeight};
        RenderNode childNode = layoutSingle(child, childConstraints);
        contentWidth = std::max(
            contentWidth, childNode.size.width + child.margin.horizontal());
        contentHeight = std::max(
            contentHeight, childNode.size.height + child.margin.vertical());
        measured.push_back(std::move(childNode));
    }

    float borderWidth =
        clampFloat(contentWidth + widget.padding.horizontal(), outer.minWidth,
                   outer.maxWidth);
    float borderHeight = clampFloat(contentHeight + widget.padding.vertical(),
                                    outer.minHeight, outer.maxHeight);
    if (widget.width.has_value()) {
        borderWidth = resolvedWidth;
    }
    if (widget.height.has_value()) {
        borderHeight = resolvedHeight;
    }
    const float contentBoxWidth =
        std::max(0.0F, borderWidth - widget.padding.horizontal());
    const float contentBoxHeight =
        std::max(0.0F, borderHeight - widget.padding.vertical());

    float xFactor = 0.0F;
    float yFactor = 0.0F;
    stackAlignmentFactors(widget.stackAlignment, xFactor, yFactor);

    RenderNode node =
        makeNode(widget, Offset{0.0F, 0.0F}, Size{borderWidth, borderHeight});
    for (std::size_t i = 0; i < measured.size(); ++i) {
        const Widget& child = widget.children[i];
        Offset childOffset{};
        if (child.stackPosition.has_value()) {
            childOffset = Offset{
                widget.padding.left + child.stackPosition->x +
                    child.margin.left,
                widget.padding.top + child.stackPosition->y +
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
                widget.padding.left + child.margin.left + freeWidth * xFactor,
                widget.padding.top + child.margin.top + freeHeight * yFactor,
            };
        }
        measured[i].offset = childOffset;
        node.children.push_back(std::move(measured[i]));
    }
    return node;
}

}  // namespace

core::RenderNode LayoutEngine::layout(const core::Widget& widget,
                                      const core::Constraints& constraints) {
    return layoutSingle(widget, constraints);
}

}  // namespace lumen::layout
