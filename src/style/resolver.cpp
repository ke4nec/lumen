#include "lumen/style/resolver.h"

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
    }
    return tokens.filled;
}

const TextStyle& roleTypography(const Typography& typography,
                                WidgetType type) {
    switch (type) {
        case WidgetType::Button:
        case WidgetType::Checkbox:
        case WidgetType::Switch:
            return typography.label;
        default:
            return typography.body;
    }
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
    if (overrides.radius.has_value()) {
        common.radius = *overrides.radius;
    }
    if (overrides.padding.has_value()) {
        common.padding = *overrides.padding;
    }
}

WidgetState stateFor(const Widget& widget, const StyleContext& context,
                     const std::string& identity) {
    const bool checked = widget.type == WidgetType::Checkbox ||
                                 widget.type == WidgetType::Switch
                             ? (widget.checked || widget.selected)
                             : widget.checked;
    return context.interaction.stateFor(identity, !widget.enabled, checked,
                                        widget.invalid, widget.selected);
}

float focusWidthFor(const WidgetState& state, const Theme& theme) {
    if (state.disabled || !state.focused) {
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

ResolvedStyle resolveContainer(const Widget& widget, const Theme& theme) {
    ResolvedStyle resolved;
    commonStyle(resolved.component) = containerCommon(widget, theme);
    applyOverrides(widget, commonStyle(resolved.component));

    return resolved;
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
    CommonResolvedStyle& common = button.common;
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

    if (state.disabled) {
        common.background = theme.colors.disabledBackground;
        common.foreground = theme.colors.disabledContent;
        common.border = outlined ? theme.colors.borderDefault
                                 : Color::transparent();
    } else if (state.pressed) {
        // pressed 覆盖 hover（§5 规则 3）。
        common.background = blendOver(common.background,
                                      theme.colors.pressedOverlay);
    } else if (state.hovered) {
        common.background =
            blendOver(common.background, theme.colors.hoverOverlay);
    }
    common.focusWidth = focusWidthFor(state, theme);
    common.text = resolveTextStyle(widget, theme.typography.label,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = button;
    resolved.minWidth = theme.metrics.buttonMinWidth[index];
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
        // invalid 影响边框但不覆盖可用性（§5 规则 2）；focused 提供焦点
        // 环与输入光标，边框色 invalid 优先保留校验反馈。
        if (state.invalid) {
            common.border = tokens.borderInvalid;
        } else if (state.focused) {
            common.border = tokens.borderFocused;
        }
    }
    field.focused = state.focused && !state.disabled;
    common.focusWidth = focusWidthFor(state, theme);
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
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.radius = core::CornerRadius::zero();
    checkbox.indicator = tokens.indicator;
    checkbox.indicatorChecked = tokens.indicatorChecked;
    checkbox.mark = tokens.mark;
    checkbox.indicatorSize = tokens.indicatorSize[index];
    checkbox.markInset = tokens.markInset[index];
    checkbox.markRadius = checkbox.markInset * 0.5F;
    checkbox.labelGap = tokens.labelGap;
    checkbox.checked = state.checked || state.selected;

    if (state.disabled) {
        checkbox.indicator = theme.colors.disabledBackground;
        checkbox.indicatorChecked = theme.colors.disabledBackground;
        checkbox.mark = theme.colors.disabledContent;
        common.foreground = theme.colors.disabledContent;
    } else if (state.invalid && !checkbox.checked) {
        checkbox.indicator = theme.colors.statusError;
    }
    common.focusWidth = focusWidthFor(state, theme);
    common.text = resolveTextStyle(widget, theme.typography.label,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = checkbox;
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
    common.focusRing = theme.colors.focusRing;
    common.selection = theme.colors.selectionBackground;
    common.radius = core::CornerRadius::zero();
    control.trackOff = tokens.trackOff;
    control.trackOn = tokens.trackOn;
    control.knob = tokens.knob;
    control.trackWidth = tokens.trackWidth[index];
    control.trackHeight = tokens.trackHeight[index];
    control.knobSize = tokens.knobSize[index];
    control.knobInset = tokens.knobInset;
    control.labelGap = tokens.labelGap;
    control.checked = state.checked || state.selected;

    if (state.disabled) {
        control.trackOff = theme.colors.disabledBackground;
        control.trackOn = theme.colors.disabledBackground;
        control.knob = theme.colors.disabledContent;
        common.foreground = theme.colors.disabledContent;
    } else if (state.invalid && !control.checked) {
        control.trackOff = theme.colors.statusError;
    }
    common.focusWidth = focusWidthFor(state, theme);
    common.text = resolveTextStyle(widget, theme.typography.label,
                                   common.foreground);

    ResolvedStyle resolved;
    resolved.component = control;
    resolved.controlGap = theme.metrics.controlGap[index];
    applyOverrides(widget,
                   commonStyle(resolved.component));
    return resolved;
}

}  // namespace

ResolvedStyle resolveStyle(const Widget& widget, const StyleContext& context,
                           const std::string& identity) {
    const WidgetState state = stateFor(widget, context, identity);
    switch (widget.type) {
        case WidgetType::Button:
            return resolveButton(widget, context, state);
        case WidgetType::TextField:
            return resolveTextField(widget, context, state);
        case WidgetType::Checkbox:
            return resolveCheckbox(widget, context, state);
        case WidgetType::Switch:
            return resolveSwitch(widget, context, state);
        case WidgetType::Text:
            return resolveText(widget, context.theme);
        default:
            return resolveContainer(widget, context.theme);
    }
}

}  // namespace lumen::style
