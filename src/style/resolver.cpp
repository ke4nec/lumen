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
    // 图标部件（§6.1/§4.5）：槽位尺寸与 gap 按档位，线宽按 16px→1.5
    // 基准比例缩放。
    button.iconSize = theme.metrics.inlineIconSize[index];
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
    // §6.1：不透明填充与焦点环颜色接近时，环内侧预留 1 px 表面隔离带
    //（几何不受聚焦影响——文字按节点居中，不随环带位移）。
    if (common.focusWidth > 0.0F && common.background.a == 255) {
        common.focusIsolation = theme.colors.surface;
    }
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

// 指示器/轨道类控件的槽位：指示器边长 + 焦点环宽度 + 1 px 隔离带
//（§4.4；是否聚焦不改变槽位与标签起点）。
float indicatorSlot(const Theme& theme, float indicatorSize) {
    return indicatorSize + theme.metrics.focusRingWidth + 1.0F;
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
    } else if (state.invalid && !checkbox.checked) {
        checkbox.indicatorOutline = theme.colors.statusError;
    } else if (state.pressed) {
        // pressed 对当前指示器表面叠加（§6.4）。
        checkbox.indicator = blendOver(checkbox.indicator,
                                       theme.colors.pressedOverlay);
        checkbox.indicatorChecked = blendOver(checkbox.indicatorChecked,
                                              theme.colors.pressedOverlay);
    } else if (state.hovered) {
        // hover 轮廓取 focusRing（§6.4）。
        checkbox.indicatorOutline = theme.colors.focusRing;
    }
    common.focusWidth = focusWidthFor(state, theme);
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

    if (state.disabled) {
        control.trackOff = theme.colors.disabledBackground;
        control.trackOutline = theme.colors.disabledContent;
        control.trackOn = theme.colors.disabledBackground;
        control.knobOff = theme.colors.disabledContent;
        control.knobOn = theme.colors.disabledContent;
        common.foreground = theme.colors.disabledContent;
    } else if (state.invalid && !control.checked) {
        control.trackOutline = theme.colors.statusError;
    } else if (state.pressed) {
        control.trackOff = blendOver(control.trackOff,
                                     theme.colors.pressedOverlay);
        control.trackOn = blendOver(control.trackOn,
                                    theme.colors.pressedOverlay);
    } else if (state.hovered) {
        control.trackOutline = theme.colors.focusRing;
    }
    common.focusWidth = focusWidthFor(state, theme);
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
        radio.indicatorChecked = theme.colors.disabledBackground;
        radio.dot = theme.colors.disabledContent;
        common.foreground = theme.colors.disabledContent;
    } else if (state.invalid && !radio.checked) {
        radio.indicatorOutline = theme.colors.statusError;
    } else if (state.pressed) {
        radio.indicator = blendOver(radio.indicator,
                                    theme.colors.pressedOverlay);
    } else if (state.hovered) {
        radio.indicatorOutline = theme.colors.focusRing;
    }
    common.focusWidth = focusWidthFor(state, theme);
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
        case WidgetType::Text:
            return resolveText(widget, context.theme);
        default:
            return resolveContainer(widget, context.theme);
    }
}

}  // namespace

ResolvedStyle resolveStyle(const Widget& widget, const StyleContext& context,
                           const std::string& identity) {
    if (t_themeOverride != nullptr) {
        // 局部主题域：覆盖主题 + 原交互快照（状态解析优先级不变）。
        const StyleContext scoped{*t_themeOverride, context.interaction,
                                  context.accessibility,
                                  context.deviceScale};
        return resolveStyleImpl(widget, scoped, identity);
    }
    return resolveStyleImpl(widget, context, identity);
}

}  // namespace lumen::style
