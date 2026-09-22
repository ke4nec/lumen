#include "lumen/style/resolver.h"
#include "lumen/core/scrollbar.h"

#include <algorithm>

namespace lumen::style {
namespace {

using core::Color;
using core::CommonResolvedStyle;
using core::EdgeInsets;
using core::ResolvedStyle;
using core::StyleOverrides;
using core::TextStyle;
using core::Widget;
using core::WidgetType;

// 控件尺寸档位：density 决定基准，ControlSize 相对偏移，夹取到 [0, 2]。
std::uint8_t sizeIndexFor(const Theme& theme, core::ControlSize size) {
    int index = static_cast<int>(theme.metrics.baseIndex);
    switch (size) {
        case core::ControlSize::Small:
            --index;
            break;
        case core::ControlSize::Medium:
            break;
        case core::ControlSize::Large:
            ++index;
            break;
    }
    return static_cast<std::uint8_t>(std::clamp(index, 0, 2));
}

const ButtonVariantTokens& variantTokens(const ButtonTokens& tokens,
                                         core::ButtonVariant variant) {
    switch (variant) {
        case core::ButtonVariant::Filled:
            return tokens.filled;
        case core::ButtonVariant::Tonal:
            return tokens.tonal;
        case core::ButtonVariant::Outline:
            return tokens.outline;
        case core::ButtonVariant::Ghost:
            return tokens.ghost;
        case core::ButtonVariant::Danger:
            return tokens.danger;
        // 窗口关闭钮：base 取 Ghost（rest 同幽灵；hover/pressed 在
        // resolveButton 内按 statusError/onError 覆写）。
        case core::ButtonVariant::WindowClose:
            return tokens.ghost;
        // chrome 命令钮（Spin/ToolBar/StatusBar）：base 取 Ghost（rest 同
        // 幽灵；hover/pressed/checked 在 resolveButton 内按稿件语言覆写）。
        case core::ButtonVariant::Chrome:
            return tokens.ghost;
    }
    return tokens.filled;
}

// 声明文本合并（§6.1）：Widget.textStyle 是旧声明通道——非默认即视为显
// 式样式，未着色（不透明黑）回落到角色前景色，显式颜色透传；
// StyleOverrides.text 是全显式通道，透明/黑按字面生效。
TextStyle resolveTextStyle(const Widget& widget, const TextStyle& role,
                           const Color& foreground) {
    TextStyle style = role;
    if (!(widget.textStyle == TextStyle{})) {
        style = widget.textStyle;
        if (style.color == Color{0, 0, 0, 255}) {
            style.color = foreground;
        }
        return style;
    }
    style.color = foreground;
    return style;
}

// 字段级覆盖在状态折算之后应用（应用对最终值负责，§10.1 状态优先级与
// 覆盖分层）。text 先应用、foreground 后应用：foreground 是颜色权威，
// 可修正 overrides.text 的未着色样式；单独提供 overrides.text 时其字面
// 颜色（含透明/黑）按值生效。
void applyOverrides(const Widget& widget, CommonResolvedStyle& common) {
    const StyleOverrides& overrides = widget.styleOverrides;
    if (overrides.text.has_value()) {
        common.text = *overrides.text;
    }
    if (overrides.background.has_value()) {
        common.background = *overrides.background;
    }
    if (overrides.foreground.has_value()) {
        common.foreground = *overrides.foreground;
        common.text.color = *overrides.foreground;
    }
    if (overrides.border.has_value()) {
        common.border = *overrides.border;
    }
    if (overrides.borderWidth.has_value()) {
        common.borderWidth = *overrides.borderWidth;
    }
    if (overrides.radius.has_value()) {
        common.radius = *overrides.radius;
    }
    if (overrides.padding.has_value()) {
        common.padding = *overrides.padding;
    }
}

WidgetState stateFor(const Widget& widget, const StyleContext& context,
                     const std::string& identity) {
    if (context.previewStates != nullptr) {
        const auto found = context.previewStates->find(widget.key);
        if (found != context.previewStates->end()) return found->second;
    }
    const bool checked = widget.type == WidgetType::Checkbox ||
                                 widget.type == WidgetType::Switch
                             ? (widget.checked || widget.selected)
                             : widget.checked;
    return context.interaction.stateFor(identity, !widget.enabled, checked,
                                        widget.invalid, widget.selected);
}

float focusWidthFor(const Widget& widget, const WidgetState& state, const Theme& theme) {
    if (!widget.showFocusRing || state.disabled || !state.focused) {
        return 0.0F;
    }
    return theme.metrics.focusRingWidth;
}

CommonResolvedStyle containerCommon(const Widget& widget, const Theme& theme) {
    CommonResolvedStyle common;
    common.background = widget.color;
    common.radius = widget.radius;
    common.foreground = theme.colors.contentPrimary;
    common.padding = widget.padding;
    common.text = theme.typography.body;
    common.text.color = common.foreground;
    return common;
}

ResolvedStyle resolveContainer(const Widget& widget, const Theme& theme,
                               const WidgetState& state) {
    ResolvedStyle resolved;
    commonStyle(resolved.component) = containerCommon(widget, theme);
    if (widget.showFocusRing && core::isScrollableWidget(widget.type)) {
        auto& common = commonStyle(resolved.component);
        common.focusRing = theme.colors.focusRing;
        common.focusWidth = focusWidthFor(widget, state, theme);
    }
    if (widget.type == WidgetType::List || widget.type == WidgetType::Tree) {
        auto& common = commonStyle(resolved.component);
        const auto& tokens = widget.type == WidgetType::Tree ? theme.tree.row : theme.list;
        common.background = tokens.background;
        common.border = tokens.separator;
        common.borderWidth = tokens.separatorWidth;
        common.radius = core::CornerRadius::all(theme.metrics.cardRadius);
        common.padding = EdgeInsets{widget.padding.left + common.borderWidth,
                                    widget.padding.top + common.borderWidth,
                                    widget.padding.right + common.borderWidth,
                                    widget.padding.bottom + common.borderWidth};
    }
    // 集合行（collection-controls-design §10.2）：Row/Container 行由集合
    // 控制器构建——选中/hover/pressed 折算与焦点环（current 行）走与
    // 控件一致的状态规则（§5：pressed 覆盖 hover；focused 必须可见）。
    // 普通容器不受影响（无焦点环，卡片/页面背景）。
    if (widget.collectionRow) {
        CommonResolvedStyle& common = commonStyle(resolved.component);
        common.focusRing = theme.colors.focusRing;
        common.selection = theme.colors.selectionBackground;
        if (state.selected) {
            common.background = blendOver(common.background,
                                           theme.colors.selectionBackground);
        }
        if (state.disabled) {
            common.foreground = theme.colors.disabledContent;
            common.text.color = common.foreground;
        } else if (state.pressed) {
            common.background = blendOver(common.background,
                                            theme.colors.pressedOverlay);
        } else if (state.hovered) {
            common.background = blendOver(common.background,
                                           theme.colors.hoverOverlay);
        }
        // 焦点环对菜单行抑制（semanticsRole="menuItem"）：current 指示
        // 由动能矩形承载，环叠在选中底色上双指示冗余且过亮；集合行保持
        // 底+环视觉——聚焦可见性由背景色块满足（§5 规则 4）。零新增
        // Widget 字段（体积预算）。
        common.focusWidth =
            widget.collectionRow && widget.semanticsRole == "menuItem"
                ? 0.0F
                : focusWidthFor(widget, state, theme);
        // 行最小高度（视觉系统 §3.2 尺度表）：布局把行钳到该下限。
        resolved.minHeight = theme.metrics.minHeight[theme.metrics.baseIndex];
    }
    applyOverrides(widget, commonStyle(resolved.component));

    return resolved;
}

// List row geometry/state comes from Theme, not from its application content.
ResolvedStyle resolveListPart(const Widget& widget, const StyleContext& context,
                              const WidgetState& state, core::ListPart part,
                              const ListTokens& tokens) {
    const Theme& theme = context.theme;
    const auto index = sizeIndexFor(theme, widget.controlSize);
    ResolvedStyle resolved;
    CommonResolvedStyle common = containerCommon(widget, theme);
    common.foreground = tokens.emptyContent;
    common.text = theme.typography.body;
    common.text.color = common.foreground;
    if (part == core::ListPart::Empty) {
        common.padding = EdgeInsets::symmetric(theme.metrics.controlPaddingX[index],
                                               theme.metrics.controlPaddingY[index]);
        resolved.minHeight = theme.metrics.minHeight[index] * 2.0F;
        resolved.controlGap = theme.metrics.controlGap[index];
    } else if (part == core::ListPart::EmptyIcon) {
        resolved.minWidth = resolved.minHeight = tokens.emptyIconSize;
    } else if (part == core::ListPart::EmptyText) {
        common.text = resolveTextStyle(widget, theme.typography.body, common.foreground);
    } else {
        core::ListRowResolvedStyle row;
        common.foreground = state.disabled ? tokens.disabledContent : tokens.content;
        common.text.color = common.foreground;
        common.background = tokens.background;
        if (!state.disabled) {
            common.background = state.pressed ? tokens.pressed
                : state.hovered ? tokens.hovered
                : state.selected ? tokens.selected : tokens.background;
            if (state.selected && !state.pressed) common.background = tokens.selected;
        }
        common.focusRing = theme.colors.focusRing;
        common.focusWidth = focusWidthFor(widget, state, theme);
        common.radius = core::CornerRadius::all(theme.metrics.controlRadius[index]);
        common.padding = EdgeInsets::symmetric(theme.metrics.controlPaddingX[index],
                                               theme.metrics.controlPaddingY[index]);
        if (widget.treePart == core::TreePart::Row || widget.treePart == core::TreePart::LastRow) {
            common.padding.left += static_cast<float>(widget.treeDepth) * theme.tree.indentStep;
            resolved.controlGap = theme.metrics.controlGap[index];
        }
        row.separator = tokens.separator;
        row.separatorWidth = part == core::ListPart::LastRow
            ? 0.0F : tokens.separatorWidth;
        if (state.selected) {
            row.selectionMarker = state.disabled ? tokens.disabledContent : tokens.selectionMarker;
            row.markerWidth = tokens.markerWidth;
            row.markerInset = tokens.markerInset;
        }
        applyOverrides(widget, common);
        row.common = common;
        resolved.component = row;
        resolved.minHeight = theme.metrics.minHeight[index];
        return resolved;
    }
    applyOverrides(widget, common);
    resolved.component = common;
    return resolved;
}

ResolvedStyle resolveTreePart(const Widget& widget, const StyleContext& context,
                              const WidgetState& state) {
    using core::TreePart;
    using core::ListPart;
    const auto& theme = context.theme;
    if (widget.treePart == TreePart::Chevron || widget.treePart == TreePart::Spacer) {
        ResolvedStyle resolved;
        core::ButtonResolvedStyle button;
        auto& common = button.common;
        common.background = !state.disabled && state.pressed ? theme.tree.row.pressed
            : !state.disabled && state.hovered ? theme.tree.row.hovered : Color::transparent();
        common.foreground = state.disabled ? theme.tree.row.disabledContent
            : state.hovered || state.pressed ? theme.tree.row.content : theme.tree.chevronContent;
        common.focusRing = theme.colors.focusRing;
        common.focusWidth = focusWidthFor(widget, state, theme);
        common.radius = core::CornerRadius::all(theme.metrics.controlRadius[sizeIndexFor(theme, widget.controlSize)]);
        common.text = theme.typography.body;
        common.text.color = common.foreground;
        button.iconSize = theme.tree.chevronIconSize;
        button.iconStroke = theme.icons.strokeWidth * button.iconSize / theme.icons.defaultSize;
        applyOverrides(widget, common);
        resolved.component = button;
        resolved.minWidth = resolved.minHeight = theme.tree.chevronHitExtent;
        return resolved;
    }
    const auto part = widget.treePart == TreePart::Row ? ListPart::Row
        : widget.treePart == TreePart::LastRow ? ListPart::LastRow
        : widget.treePart == TreePart::Empty ? ListPart::Empty
        : widget.treePart == TreePart::EmptyIcon ? ListPart::EmptyIcon : ListPart::EmptyText;
    return resolveListPart(widget, context, state, part, theme.tree.row);
}

ResolvedStyle resolveText(const Widget& widget, const Theme& theme) {
    ResolvedStyle resolved;
    CommonResolvedStyle& common = commonStyle(resolved.component);
    common.background = Color::transparent();
    common.foreground = theme.colors.contentPrimary;
    common.padding = widget.padding;
    common.text =
        resolveTextStyle(widget, theme.typography.body, common.foreground);
    applyOverrides(widget, common);
    return resolved;
}

ResolvedStyle resolveButton(const Widget& widget, const StyleContext& context,
                            const WidgetState& state) {
    const Theme& theme = context.theme;
    const std::uint8_t index = sizeIndexFor(theme, widget.controlSize);
    const ButtonVariantTokens& base =
        variantTokens(theme.button, widget.buttonVariant);

    core::ButtonResolvedStyle button;
    button.alignContentStart = widget.alignContentStart;
    button.reserveIconSpace = widget.reserveIconSpace;
    CommonResolvedStyle& common = button.common;
    // 图标部件（§6.1/§4.5）：槽位尺寸与 gap 按档位，线宽按 16px→1.5
    // 基准比例缩放；styleOverrides.iconSize 覆盖盒宽（chrome 件对齐
    // 设计稿 14px 等），描边同源折算保持相对权重。
    button.iconSize =
        widget.styleOverrides.iconSize > 0.0F
            ? widget.styleOverrides.iconSize
            : theme.metrics.inlineIconSize[index];
    button.iconGap = theme.metrics.controlGap[index];
    button.iconStroke =
        theme.icons.strokeWidth * button.iconSize / theme.icons.defaultSize;
    common.background = base.background;
    common.foreground = base.content;
    common.border = base.border;
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.radius = core::CornerRadius::all(theme.metrics.controlRadius[index]);
    common.padding = EdgeInsets::symmetric(theme.metrics.controlPaddingX[index],
                                           theme.metrics.controlPaddingY[index]);

    const bool outlined = widget.buttonVariant == core::ButtonVariant::Outline;
    common.borderWidth = outlined ? theme.metrics.controlBorderWidth : 0.0F;

    // 集合行（collection-controls-design §10.2）：selected 折算为
    // color.selection.background 组件 token——只对集合控制器构建的行
    // 生效，Tabs/Dropdown 等既有 selected 语义不受影响。
    if (state.selected && widget.collectionRow) {
        common.background = blendOver(common.background,
                                      theme.colors.selectionBackground);
    }

    // 窗口关闭钮（Windows 惯例）：rest 幽灵（透明底 + 次要前景）；
    // hover 实心 statusError + onError 反色；pressed 在警示红上叠加
    // 压暗。min/max 窗口钮保持 Ghost（hover 为常规表面派生）。
    const bool windowClose =
        widget.buttonVariant == core::ButtonVariant::WindowClose;
    if (windowClose) {
        common.background = Color::transparent();
        common.foreground = theme.colors.contentSecondary;
    }
    // chrome 命令钮（Spin/ToolBar/StatusBar，design/{spin,toolbar,
    // statusbar}.html）：rest 幽灵；hover 表面派生 + 前景提亮 primary；
    // pressed = List pressed（surface/accent 0.32 混合）；checked（toggle
    // 项）= accentContainer 持久底 + primary，悬停不改变 checked 底。
    const bool chrome = widget.buttonVariant == core::ButtonVariant::Chrome;
    if (chrome) {
        common.background = Color::transparent();
        common.foreground = theme.colors.contentSecondary;
    }

    if (state.disabled) {
        common.background =
            (outlined || widget.buttonVariant == core::ButtonVariant::Ghost ||
             windowClose || chrome)
                ? Color::transparent()
                : theme.colors.disabledBackground;
        common.foreground = theme.colors.disabledContent;
        common.border = outlined ? theme.colors.borderDefault
                                 : Color::transparent();
    } else if (state.pressed) {
        // pressed 覆盖 hover（§5 规则 3）。
        common.background = blendOver(
            windowClose ? theme.button.windowClose.background
                        : common.background,
            theme.colors.pressedOverlay);
        if (windowClose) {
            common.foreground = theme.button.windowClose.content;
        }
        if (chrome) {
            common.background = theme.list.pressed;
            common.foreground = theme.colors.contentPrimary;
        }
    } else if (chrome && widget.checked) {
        // toggle 持久底（design/toolbar.html .is-checked：accentContainer
        // + ink；悬停不改变 checked 底——形状/底色即状态）。
        common.background = theme.colors.accentContainer;
        common.foreground = theme.colors.contentPrimary;
    } else if (state.hovered) {
        if (windowClose) {
            common.background = theme.button.windowClose.background;
            common.foreground = theme.button.windowClose.content;
        } else {
            common.background =
                blendOver(common.background, theme.colors.hoverOverlay);
            if (chrome) {
                common.foreground = theme.colors.contentPrimary;
            }
        }
    }
    common.focusWidth = focusWidthFor(widget, state, theme);
    // §6.1：不透明填充与焦点环颜色接近时，环内侧预留 1 px 表面隔离带
    //（几何不受聚焦影响——文字按节点居中，不随环带位移）。
    if (common.focusWidth > 0.0F && common.background.a == 255) {
        common.focusIsolation = theme.colors.surface;
    }
    common.text = resolveTextStyle(widget, theme.typography.label,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = button;
    resolved.minWidth = widget.text.empty() && widget.icon != core::IconId::None
                            ? theme.metrics.minHeight[index]
                            : theme.metrics.buttonMinWidth[index];
    resolved.minHeight = theme.metrics.minHeight[index];
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget,
                   commonStyle(resolved.component));
    return resolved;
}

ResolvedStyle resolveTextField(const Widget& widget,
                               const StyleContext& context,
                               const WidgetState& state) {
    const Theme& theme = context.theme;
    const std::uint8_t index = sizeIndexFor(theme, widget.controlSize);
    const TextFieldTokens& tokens = theme.textField;

    core::TextFieldResolvedStyle field;
    CommonResolvedStyle& common = field.common;
    common.background = tokens.background;
    common.foreground = theme.colors.contentPrimary;
    common.border = tokens.border;
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.radius = core::CornerRadius::all(theme.metrics.controlRadius[index]);
    common.padding = EdgeInsets::symmetric(theme.metrics.controlPaddingX[index],
                                           theme.metrics.controlPaddingY[index]);
    common.borderWidth = theme.metrics.controlBorderWidth;
    field.placeholder = tokens.placeholder;
    field.caret = tokens.caret;
    field.preeditUnderline = tokens.preeditUnderline;
    field.readOnly = widget.readOnly;
    field.multiline = widget.multiline;
    field.obscure = widget.obscure;

    if (state.disabled) {
        common.background = theme.colors.disabledBackground;
        common.foreground = theme.colors.disabledContent;
        common.border = theme.colors.borderDefault;
        field.placeholder = theme.colors.disabledContent;
        field.caret = theme.colors.disabledContent;
    } else {
        // invalid 影响边框但不覆盖可用性（§5 规则 2）；hover 增强轮廓到
        // focusRing、表面不变（§6.3）；focused 提供焦点环与输入光标。
        if (state.invalid) {
            common.border = tokens.borderInvalid;
        } else if (state.focused) {
            common.border = tokens.borderFocused;
        } else if (state.hovered) {
            common.border = tokens.borderFocused;
        }
        // ReadOnly 保留正常文字与选择/复制，底色用 surface（§6.3）。
        if (widget.readOnly) {
            common.background = theme.colors.surface;
        }
    }
    field.focused = state.focused && !state.disabled;
    common.focusWidth = focusWidthFor(widget, state, theme);
    common.text = resolveTextStyle(widget, theme.typography.body,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = field;
    resolved.minWidth = theme.metrics.textFieldMinWidth[index];
    resolved.minHeight = theme.metrics.minHeight[index];
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget,
                   commonStyle(resolved.component));
    return resolved;
}

// 指示器/轨道类控件的槽位：指示器边长 + 两侧的焦点环宽度和 1 px 隔离带
//（§4.4；是否聚焦不改变槽位与标签起点）。
float indicatorSlot(const Theme& theme, float indicatorSize) {
    return indicatorSize + 2.0F * (theme.metrics.focusRingWidth + 1.0F);
}

ResolvedStyle resolveCheckbox(const Widget& widget,
                              const StyleContext& context,
                              const WidgetState& state) {
    const Theme& theme = context.theme;
    const std::uint8_t index = sizeIndexFor(theme, widget.controlSize);
    const CheckboxTokens& tokens = theme.checkbox;

    core::CheckboxResolvedStyle checkbox;
    CommonResolvedStyle& common = checkbox.common;
    common.background = Color::transparent();
    common.foreground = theme.colors.contentPrimary;
    common.border = Color::transparent();
    common.borderWidth = theme.metrics.controlBorderWidth;
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.radius = core::CornerRadius::zero();
    checkbox.indicator = tokens.indicator;
    checkbox.indicatorOutline = tokens.indicatorOutline;
    checkbox.indicatorChecked = tokens.indicatorChecked;
    checkbox.mark = tokens.mark;
    checkbox.indicatorSize = tokens.indicatorSize[index];
    checkbox.indicatorRadius = theme.metrics.controlRadius[0];
    checkbox.markInset = tokens.markInset[index];
    checkbox.labelGap = tokens.labelGap;
    checkbox.slotSize = indicatorSlot(theme, checkbox.indicatorSize);
    checkbox.checked = state.checked || state.selected;

    if (state.disabled) {
        // 禁用仍可辨认 checked：填充降级为禁用面，勾号/轮廓/文字取
        // disabledContent（§5.2、§6.4）。
        checkbox.indicator = theme.colors.disabledBackground;
        checkbox.indicatorOutline = theme.colors.disabledContent;
        checkbox.indicatorChecked = theme.colors.disabledBackground;
        checkbox.mark = theme.colors.disabledContent;
        common.foreground = theme.colors.disabledContent;
    } else {
        checkbox.indicatorOutline = checkbox.checked ? checkbox.indicatorChecked : checkbox.indicatorOutline;
        if (state.pressed) {
            checkbox.indicator = blendOver(checkbox.indicator, theme.colors.pressedOverlay);
            checkbox.indicatorChecked = blendOver(checkbox.indicatorChecked, theme.colors.pressedOverlay);
        }
        if (state.hovered) checkbox.indicatorOutline = theme.colors.focusRing;
        if (state.invalid) checkbox.indicatorOutline = theme.colors.statusError;
    }
    common.focusWidth = focusWidthFor(widget, state, theme);
    common.text = resolveTextStyle(widget, theme.typography.label,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = checkbox;
    resolved.minHeight = theme.metrics.minHeight[index];
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget,
                   commonStyle(resolved.component));
    return resolved;
}

ResolvedStyle resolveSwitch(const Widget& widget, const StyleContext& context,
                            const WidgetState& state) {
    const Theme& theme = context.theme;
    const std::uint8_t index = sizeIndexFor(theme, widget.controlSize);
    const SwitchTokens& tokens = theme.switchControl;

    core::SwitchResolvedStyle control;
    CommonResolvedStyle& common = control.common;
    common.background = Color::transparent();
    common.foreground = theme.colors.contentPrimary;
    common.border = Color::transparent();
    common.borderWidth = theme.metrics.controlBorderWidth;
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.radius = core::CornerRadius::zero();
    control.trackOff = tokens.trackOff;
    control.trackOutline = tokens.trackOutline;
    control.trackOn = tokens.trackOn;
    control.knobOff = tokens.knobOff;
    control.knobOn = tokens.knobOn;
    control.trackWidth = tokens.trackWidth[index];
    control.trackHeight = tokens.trackHeight[index];
    control.knobSize = tokens.knobSize[index];
    // 左右内距由 (trackHeight - knobSize) / 2 推导（§6.4），不套固定值。
    control.knobInset = std::max(
        0.0F, (control.trackHeight - control.knobSize) * 0.5F);
    control.labelGap = tokens.labelGap;
    control.slotSize = indicatorSlot(theme, control.trackWidth);
    control.checked = state.checked || state.selected;
    control.knobPosition = control.checked ? 1.0F : 0.0F;

    if (state.disabled) {
        control.trackOff = theme.colors.disabledBackground;
        control.trackOutline = theme.colors.disabledContent;
        control.trackOn = theme.colors.disabledBackground;
        control.knobOff = theme.colors.disabledContent;
        control.knobOn = theme.colors.disabledContent;
        common.foreground = theme.colors.disabledContent;
    } else {
        if (state.pressed) {
            control.trackOff = blendOver(control.trackOff, theme.colors.pressedOverlay);
            control.trackOn = blendOver(control.trackOn, theme.colors.pressedOverlay);
        }
        if (state.hovered) control.trackOutline = theme.colors.focusRing;
        if (state.invalid) control.trackOutline = theme.colors.statusError;
    }
    common.focusWidth = focusWidthFor(widget, state, theme);
    common.text = resolveTextStyle(widget, theme.typography.label,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = control;
    resolved.minHeight = theme.metrics.minHeight[index];
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget,
                   commonStyle(resolved.component));
    return resolved;
}

ResolvedStyle resolveSlider(const Widget& widget, const StyleContext& context,
                            const WidgetState& state) {
    const Theme& theme = context.theme;
    const std::uint8_t index = sizeIndexFor(theme, widget.controlSize);
    const SliderTokens& tokens = theme.slider;

    core::SliderResolvedStyle slider;
    CommonResolvedStyle& common = slider.common;
    common.background = Color::transparent();
    common.foreground = theme.colors.contentPrimary;
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.text = theme.typography.label;
    common.text.color = common.foreground;
    slider.trackRemaining = tokens.trackRemaining;
    slider.trackActive = tokens.trackActive;
    slider.thumbFill = tokens.thumbFill;
    slider.thumbOutline = tokens.thumbOutline;
    slider.trackHeight = tokens.trackHeight;
    slider.thumbDiameter = tokens.thumbDiameter[index];
    slider.thumbBorderWidth = tokens.thumbBorderWidth;
    // 端点预留（§6.5）：thumb 半径 + 焦点保护宽，是否聚焦不重定位。
    slider.trackInset =
        slider.thumbDiameter * 0.5F + theme.metrics.focusRingWidth + 1.0F;

    if (state.disabled) {
        slider.trackRemaining = theme.colors.disabledContent;
        slider.trackActive = theme.colors.disabledContent;
        slider.thumbFill = theme.colors.disabledBackground;
        slider.thumbOutline = theme.colors.disabledContent;
        common.foreground = theme.colors.disabledContent;
        common.text.color = common.foreground;
    } else {
        if (state.pressed) {
            slider.thumbFill =
                blendOver(slider.thumbFill, theme.colors.pressedOverlay);
        } else if (state.hovered || state.focused) {
            // hover/拖动时 Thumb 轮廓取 focusRing（§6.5）。
            slider.thumbOutline = theme.colors.focusRing;
        }
    }
    common.focusWidth = focusWidthFor(widget, state, theme);

    ResolvedStyle resolved;
    resolved.component = slider;
    resolved.minHeight = theme.metrics.minHeight[index];
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget, commonStyle(resolved.component));
    return resolved;
}

ResolvedStyle resolveProgressBar(const Widget& widget,
                                 const StyleContext& context,
                                 const WidgetState& state) {
    (void)state;  // ProgressBar 无交互状态（§6.6）。
    const Theme& theme = context.theme;
    const std::uint8_t index = sizeIndexFor(theme, widget.controlSize);
    const ProgressBarTokens& tokens = theme.progressBar;

    core::ProgressBarResolvedStyle bar;
    CommonResolvedStyle& common = bar.common;
    common.background = Color::transparent();
    common.foreground = theme.colors.contentPrimary;
    bar.track = tokens.track;
    bar.fill = tokens.fill;
    bar.trackHeight = tokens.trackHeight[index];
    common.text = theme.typography.caption;
    common.text.color = common.foreground;

    ResolvedStyle resolved;
    resolved.component = bar;
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget, commonStyle(resolved.component));
    return resolved;
}

ResolvedStyle resolveTabs(const Widget& widget, const StyleContext& context,
                          const WidgetState& state) {
    (void)state;  // 页签行为无自身状态；选中态在子节点。
    const Theme& theme = context.theme;
    const TabsTokens& tokens = theme.tabs;

    core::TabsResolvedStyle tabs;
    CommonResolvedStyle& common = tabs.common;
    common.background = Color::transparent();
    common.foreground = theme.colors.contentPrimary;
    common.text = theme.typography.label;
    common.text.color = common.foreground;
    tabs.indicator = tokens.indicator;
    tabs.separator = tokens.separator;
    tabs.selectedContent = tokens.selectedContent;
    tabs.unselectedContent = tokens.unselectedContent;
    tabs.indicatorHeight = tokens.indicatorHeight;
    tabs.separatorHeight = tokens.separatorHeight;

    ResolvedStyle resolved;
    resolved.component = tabs;
    applyOverrides(widget, commonStyle(resolved.component));
    return resolved;
}

// Dropdown 值行（§6.7）：与字段同源 chrome（surfaceSunken 底 +
// borderStrong 轮廓、同高/圆角/padding/最小宽度）；复用
// ButtonResolvedStyle 的图标部件字段承载尾随 Chevron。
ResolvedStyle resolveDropdown(const Widget& widget,
                              const StyleContext& context,
                              const WidgetState& state) {
    const Theme& theme = context.theme;
    const std::uint8_t index = sizeIndexFor(theme, widget.controlSize);

    core::ButtonResolvedStyle dropdown;
    CommonResolvedStyle& common = dropdown.common;
    dropdown.iconSize = theme.metrics.inlineIconSize[index];
    dropdown.iconGap = theme.metrics.controlGap[index];
    dropdown.iconStroke =
        theme.icons.strokeWidth * dropdown.iconSize / theme.icons.defaultSize;
    common.background = theme.colors.surfaceSunken;
    common.foreground = theme.colors.contentPrimary;
    common.border = theme.colors.borderStrong;
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.radius =
        core::CornerRadius::all(theme.metrics.controlRadius[index]);
    common.padding = EdgeInsets::symmetric(theme.metrics.controlPaddingX[index],
                                           theme.metrics.controlPaddingY[index]);
    common.borderWidth = theme.metrics.controlBorderWidth;

    if (state.disabled) {
        common.background = theme.colors.disabledBackground;
        common.foreground = theme.colors.disabledContent;
        common.border = theme.colors.borderDefault;
    } else if (state.pressed) {
        common.background =
            blendOver(common.background, theme.colors.pressedOverlay);
    } else if (state.hovered) {
        common.background =
            blendOver(common.background, theme.colors.hoverOverlay);
    }
    common.focusWidth = focusWidthFor(widget, state, theme);
    common.text = resolveTextStyle(widget, theme.typography.label,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = dropdown;
    resolved.minWidth = theme.metrics.textFieldMinWidth[index];
    resolved.minHeight = theme.metrics.minHeight[index];
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget, commonStyle(resolved.component));
    return resolved;
}

// Tooltip（§6.9）：caption + surfaceElevated + borderDefault 轮廓的紧凑
// 提示表面；不可聚焦、无交互状态。
ResolvedStyle resolveTooltip(const Widget& widget,
                             const StyleContext& context) {
    const Theme& theme = context.theme;
    const TooltipTokens& tokens = theme.tooltip;

    ResolvedStyle resolved;
    CommonResolvedStyle& common = commonStyle(resolved.component);
    common.background = tokens.surface;
    common.foreground = tokens.content;
    common.border = tokens.border;
    common.borderWidth = theme.metrics.controlBorderWidth;
    common.radius =
        core::CornerRadius::all(theme.metrics.controlRadius[1]);
    common.padding = EdgeInsets::symmetric(tokens.paddingX, tokens.paddingY);
    common.text = theme.typography.caption;
    common.text.color = common.foreground;
    common.elevation = tokens.elevation;
    applyOverrides(widget, common);
    return resolved;
}

// Image（§6.10）：未就绪占位 = surfaceSunken + 1px borderDefault 轮廓 +
// 居中图片图标（contentSecondary，最大 24px）；就绪位图覆盖整个盒子。
ResolvedStyle resolveImage(const Widget& widget,
                           const StyleContext& context) {
    const Theme& theme = context.theme;

    ResolvedStyle resolved;
    CommonResolvedStyle& common = commonStyle(resolved.component);
    common.background = theme.colors.surfaceSunken;
    common.foreground = theme.colors.contentSecondary;
    common.border = theme.colors.borderDefault;
    common.borderWidth = theme.metrics.controlBorderWidth;
    common.radius = widget.radius;
    common.text = theme.typography.caption;
    common.text.color = common.foreground;
    applyOverrides(widget, common);
    return resolved;
}

ResolvedStyle resolveRadio(const Widget& widget, const StyleContext& context,
                           const WidgetState& state) {
    const Theme& theme = context.theme;
    const std::uint8_t index = sizeIndexFor(theme, widget.controlSize);
    const RadioTokens& tokens = theme.radio;

    core::RadioResolvedStyle radio;
    CommonResolvedStyle& common = radio.common;
    common.background = Color::transparent();
    common.foreground = theme.colors.contentPrimary;
    common.border = Color::transparent();
    common.borderWidth = theme.metrics.controlBorderWidth;
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.radius = core::CornerRadius::zero();
    radio.indicator = tokens.indicator;
    radio.indicatorOutline = tokens.indicatorOutline;
    radio.indicatorChecked = tokens.indicatorChecked;
    radio.dot = tokens.dot;
    radio.indicatorSize = tokens.indicatorSize[index];
    radio.dotRatio = tokens.dotRatio;
    radio.labelGap = tokens.labelGap;
    radio.slotSize = indicatorSlot(theme, radio.indicatorSize);
    radio.checked = state.checked || state.selected;

    if (state.disabled) {
        radio.indicator = theme.colors.disabledBackground;
        radio.indicatorOutline = theme.colors.disabledContent;
        radio.indicatorChecked = theme.colors.disabledContent;
        radio.dot = theme.colors.disabledContent;
        common.foreground = theme.colors.disabledContent;
    } else {
        if (state.pressed) radio.indicator = blendOver(radio.indicator, theme.colors.pressedOverlay);
        if (state.hovered) {
            radio.indicatorOutline = theme.colors.focusRing;
            radio.indicatorChecked = theme.colors.focusRing;
        }
        if (state.invalid) {
            radio.indicatorOutline = theme.colors.statusError;
            radio.indicatorChecked = theme.colors.statusError;
        }
    }
    common.focusWidth = focusWidthFor(widget, state, theme);
    common.text = resolveTextStyle(widget, theme.typography.label,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = radio;
    resolved.minHeight = theme.metrics.minHeight[index];
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget,
                   commonStyle(resolved.component));
    return resolved;
}

}  // namespace

namespace {

// M6：ThemeScope 覆盖（UI 线程；空 = 使用 StyleContext.theme）。
thread_local const Theme* t_themeOverride = nullptr;

}  // namespace

ScopedThemeOverride::ScopedThemeOverride(const Theme& theme)
    : previous_(t_themeOverride) {
    t_themeOverride = &theme;
}

ScopedThemeOverride::~ScopedThemeOverride() { t_themeOverride = previous_; }

namespace {

ResolvedStyle resolveStyleImpl(const Widget& widget,
                               const StyleContext& context,
                               const std::string& identity) {
    const WidgetState state = stateFor(widget, context, identity);
    if (widget.treePart != core::TreePart::None) {
        return resolveTreePart(widget, context, state);
    }
    if (widget.listPart != core::ListPart::None) {
        return resolveListPart(widget, context, state, widget.listPart, context.theme.list);
    }
    switch (widget.type) {
        case WidgetType::Button:
            return resolveButton(widget, context, state);
        case WidgetType::TextField:
            return resolveTextField(widget, context, state);
        case WidgetType::Checkbox:
            return resolveCheckbox(widget, context, state);
        case WidgetType::Switch:
            return resolveSwitch(widget, context, state);
        case WidgetType::Radio:
            return resolveRadio(widget, context, state);
        case WidgetType::Slider:
            return resolveSlider(widget, context, state);
        case WidgetType::ProgressBar:
            return resolveProgressBar(widget, context, state);
        case WidgetType::Tabs:
            return resolveTabs(widget, context, state);
        case WidgetType::Dropdown:
            return resolveDropdown(widget, context, state);
        case WidgetType::Tooltip:
            return resolveTooltip(widget, context);
        case WidgetType::Image:
            return resolveImage(widget, context);
        case WidgetType::Text:
            return resolveText(widget, context.theme);
        default:
            return resolveContainer(widget, context.theme, state);
    }
}

}  // namespace

ResolvedStyle resolveStyle(const Widget& widget, const StyleContext& context,
                           const std::string& identity) {
    if (t_themeOverride != nullptr) {
        // 局部主题域：覆盖主题 + 原交互快照（状态解析优先级不变）。
        const StyleContext scoped{*t_themeOverride, context.interaction,
                                  context.accessibility,
                                  context.deviceScale, context.previewStates};
        return resolveStyleImpl(widget, scoped, identity);
    }
    return resolveStyleImpl(widget, context, identity);
}

}  // namespace lumen::style
