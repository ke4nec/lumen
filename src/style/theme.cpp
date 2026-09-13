#include "lumen/style/theme.h"

#include <algorithm>
#include <cmath>

namespace lumen::style {

core::Color mixColors(core::Color a, core::Color b, float t) {
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    const auto channel = [clamped](std::uint8_t from, std::uint8_t to) {
        const float mixed =
            static_cast<float>(from) +
            (static_cast<float>(to) - static_cast<float>(from)) * clamped;
        return static_cast<std::uint8_t>(std::lround(mixed));
    };
    return core::Color{channel(a.r, b.r),
                       channel(a.g, b.g),
                       channel(a.b, b.b),
                       channel(a.a, b.a)};
}

core::Color blendOver(core::Color base, core::Color overlay) {
    const float alpha = static_cast<float>(overlay.a) / 255.0F;
    if (alpha <= 0.0F) {
        return base;
    }
    if (alpha >= 1.0F) {
        return overlay;
    }
    const float baseAlpha = static_cast<float>(base.a) / 255.0F;
    const float outAlpha = alpha + baseAlpha * (1.0F - alpha);
    const auto channel = [alpha, baseAlpha, outAlpha](std::uint8_t under,
                                                       std::uint8_t over) {
        const float mixed =
            (static_cast<float>(over) * alpha +
             static_cast<float>(under) * baseAlpha * (1.0F - alpha)) /
            outAlpha;
        return static_cast<std::uint8_t>(std::lround(mixed));
    };
    const auto alphaChannel = static_cast<std::uint8_t>(
        std::lround(outAlpha * 255.0F));
    return core::Color{channel(base.r, overlay.r),
                       channel(base.g, overlay.g),
                       channel(base.b, overlay.b),
                       alphaChannel};
}

std::uint8_t densityBaseIndex(ControlDensity density) {
    switch (density) {
        case ControlDensity::Compact:
            return 0;
        case ControlDensity::Comfortable:
            return 1;
        case ControlDensity::Touch:
            return 2;
    }
    return 1;
}

void Metrics::scaleBy(float factor) {
    if (factor <= 0.0F || factor == 1.0F) {
        return;
    }
    const auto scale = [factor](float& value) { value *= factor; };
    for (float& value : minHeight) {
        scale(value);
    }
    for (float& value : buttonMinWidth) {
        scale(value);
    }
    for (float& value : textFieldMinWidth) {
        scale(value);
    }
    for (float& value : controlPaddingX) {
        scale(value);
    }
    for (float& value : controlPaddingY) {
        scale(value);
    }
    for (float& value : controlGap) {
        scale(value);
    }
}

void MotionTokens::reduceAnimation() {
    stateTransitionMs = 0;
    dialogTransitionMs = 0;
    navigatorTransitionMs = 0;
    caretBlinkHalfPeriodMs = 0;
}

// --- Primitive → semantic ---

ColorScheme darkColorScheme(const PrimitivePalette& palette) {
    ColorScheme colors;
    colors.pageBackground = palette.neutral950;
    colors.surface = palette.neutral900;
    colors.surfaceElevated = mixColors(palette.neutral800,
                                        palette.neutral200, 0.06F);
    colors.contentPrimary = palette.neutral200;
    colors.contentSecondary = palette.neutral500;
    colors.accent = palette.blue500;
    colors.onAccent = mixColors(palette.blue100, palette.white, 0.35F);
    colors.accentContainer = mixColors(palette.neutral900, palette.blue500,
                                       0.24F);
    colors.onAccentContainer = palette.blue100;
    colors.borderDefault = palette.neutral600;
    colors.borderStrong = palette.neutral400;
    colors.focusRing = palette.blue300;
    colors.selectionBackground =
        core::Color{palette.blue500.r, palette.blue500.g, palette.blue500.b,
                    130};
    colors.statusError = palette.red500;
    colors.statusSuccess = palette.green500;
    colors.statusWarning = palette.amber500;
    colors.disabledBackground = palette.neutral800;
    colors.disabledContent =
        core::Color{palette.neutral500.r, palette.neutral500.g,
                    palette.neutral500.b, 170};
    colors.scrim = core::Color{0, 0, 0, 132};
    colors.hoverOverlay = core::Color{255, 255, 255, 26};
    colors.pressedOverlay = core::Color{0, 0, 0, 72};
    return colors;
}

ColorScheme lightColorScheme(const PrimitivePalette& palette) {
    ColorScheme colors;
    colors.pageBackground = palette.neutral100;
    colors.surface = palette.neutral200;
    colors.surfaceElevated = palette.neutral050;
    colors.contentPrimary = palette.neutral950;
    colors.contentSecondary = palette.neutral500;
    colors.accent = palette.blue500;
    colors.onAccent = palette.white;
    colors.accentContainer = mixColors(palette.neutral050, palette.blue500,
                                       0.22F);
    colors.onAccentContainer = palette.blue700;
    colors.borderDefault = palette.neutral300;
    colors.borderStrong = palette.neutral500;
    colors.focusRing = palette.blue700;
    colors.selectionBackground =
        core::Color{palette.blue500.r, palette.blue500.g, palette.blue500.b,
                    130};
    colors.statusError = palette.red700;
    colors.statusSuccess = palette.green500;
    colors.statusWarning = palette.amber500;
    colors.disabledBackground = palette.neutral200;
    colors.disabledContent =
        core::Color{palette.neutral500.r, palette.neutral500.g,
                    palette.neutral500.b, 170};
    colors.scrim = core::Color{0, 0, 0, 96};
    colors.hoverOverlay = core::Color{0, 0, 0, 16};
    colors.pressedOverlay = core::Color{0, 0, 0, 42};
    return colors;
}

// --- Semantic → component ---

ButtonTokens buttonTokensFrom(const ColorScheme& colors) {
    ButtonTokens tokens;
    tokens.filled.background = colors.accent;
    tokens.filled.content = colors.onAccent;
    tokens.tonal.background = colors.accentContainer;
    tokens.tonal.content = colors.onAccentContainer;
    tokens.outline.background = core::Color::transparent();
    tokens.outline.content = colors.accent;
    tokens.outline.border = colors.borderStrong;
    tokens.ghost.background = core::Color::transparent();
    tokens.ghost.content = colors.accent;
    tokens.danger.background = colors.statusError;
    tokens.danger.content = colors.onAccent;
    return tokens;
}

TextFieldTokens textFieldTokensFrom(const ColorScheme& colors) {
    TextFieldTokens tokens;
    tokens.background = colors.surfaceElevated;
    tokens.border = colors.borderDefault;
    tokens.borderFocused = colors.focusRing;
    tokens.borderInvalid = colors.statusError;
    tokens.placeholder = colors.contentSecondary;
    tokens.caret = colors.contentPrimary;
    tokens.preeditUnderline = colors.focusRing;
    return tokens;
}

CheckboxTokens checkboxTokensFrom(const ColorScheme& colors) {
    CheckboxTokens tokens;
    tokens.indicator = colors.borderDefault;
    tokens.indicatorChecked = colors.accent;
    tokens.mark = colors.onAccent;
    return tokens;
}

SwitchTokens switchTokensFrom(const ColorScheme& colors) {
    SwitchTokens tokens;
    tokens.trackOff = colors.borderDefault;
    tokens.trackOn = colors.accent;
    tokens.knob = colors.contentPrimary;
    return tokens;
}

DialogTokens dialogTokensFrom(const ColorScheme& colors,
                              const Metrics& metrics) {
    DialogTokens tokens;
    tokens.scrim = colors.scrim;
    tokens.surface = colors.surfaceElevated;
    tokens.radius = metrics.dialogRadius;
    return tokens;
}

ScrollbarTokens scrollbarTokensFrom(const ColorScheme& colors) {
    ScrollbarTokens tokens;
    tokens.rest = core::Color{colors.contentSecondary.r,
                              colors.contentSecondary.g,
                              colors.contentSecondary.b, 120};
    tokens.hovered = core::Color{colors.borderStrong.r,
                                 colors.borderStrong.g,
                                 colors.borderStrong.b, 180};
    tokens.dragged = core::Color{colors.borderStrong.r,
                                 colors.borderStrong.g,
                                 colors.borderStrong.b, 230};
    return tokens;
}

// --- Theme 工厂 ---

namespace {

Typography defaultTypography() {
    Typography typography;
    typography.title.fontSize = 20.0F;
    typography.title.weight = 600;
    typography.label.fontSize = 14.0F;
    typography.label.weight = 500;
    typography.body.fontSize = 14.0F;
    typography.caption.fontSize = 12.0F;
    return typography;
}

Theme baseTheme(bool darkMode, ControlDensity density) {
    const PrimitivePalette palette;
    Theme theme;
    theme.colors = darkMode ? darkColorScheme(palette)
                            : lightColorScheme(palette);
    theme.typography = defaultTypography();
    theme.metrics.density = density;
    theme.metrics.baseIndex = densityBaseIndex(density);
    theme.button = buttonTokensFrom(theme.colors);
    theme.textField = textFieldTokensFrom(theme.colors);
    theme.checkbox = checkboxTokensFrom(theme.colors);
    theme.switchControl = switchTokensFrom(theme.colors);
    theme.dialog = dialogTokensFrom(theme.colors, theme.metrics);
    theme.scrollbar = scrollbarTokensFrom(theme.colors);
    return theme;
}

// 高对比度：纯色正文、更强边框/焦点环，同时保留状态可辨识性（§4）。
void applyHighContrast(Theme& theme, bool darkMode) {
    const PrimitivePalette palette;
    theme.colors.contentPrimary = darkMode ? palette.white : palette.black;
    theme.colors.contentSecondary = theme.colors.contentPrimary;
    theme.colors.borderDefault = theme.colors.contentPrimary;
    theme.colors.borderStrong = theme.colors.contentPrimary;
    theme.colors.accent = darkMode ? palette.blue300 : palette.blue700;
    theme.colors.focusRing = theme.colors.accent;
    theme.colors.onAccent = darkMode ? palette.black : palette.white;
    theme.colors.disabledContent =
        core::Color{theme.colors.contentPrimary.r,
                    theme.colors.contentPrimary.g,
                    theme.colors.contentPrimary.b, 140};
    // 状态层增强：hover/pressed 差异不只靠色相。
    theme.colors.hoverOverlay = darkMode ? core::Color{255, 255, 255, 64}
                                         : core::Color{0, 0, 0, 32};
    theme.colors.pressedOverlay = darkMode ? core::Color{0, 0, 0, 110}
                                           : core::Color{0, 0, 0, 64};
    // 组件 token 同步重建（semantic → component 单向映射）。
    theme.button = buttonTokensFrom(theme.colors);
    theme.textField = textFieldTokensFrom(theme.colors);
    theme.checkbox = checkboxTokensFrom(theme.colors);
    theme.switchControl = switchTokensFrom(theme.colors);
    theme.metrics.focusRingWidth = 3.0F;
    theme.metrics.controlBorderWidth = 2.0F;
}

void scaleComponentSizes(Theme& theme, float factor) {
    if (factor <= 0.0F || factor == 1.0F) {
        return;
    }
    for (float& value : theme.checkbox.indicatorSize) {
        value *= factor;
    }
    for (float& value : theme.checkbox.markInset) {
        value *= factor;
    }
    for (float& value : theme.switchControl.trackWidth) {
        value *= factor;
    }
    for (float& value : theme.switchControl.trackHeight) {
        value *= factor;
    }
    for (float& value : theme.switchControl.knobSize) {
        value *= factor;
    }
    theme.icons.defaultSize *= factor;
}

}  // namespace

Theme Theme::dark(ControlDensity density) {
    return baseTheme(/*darkMode=*/true, density);
}

Theme Theme::light(ControlDensity density) {
    return baseTheme(/*darkMode=*/false, density);
}

Theme Theme::fromSettings(
    const accessibility::AccessibilitySettings& settings, bool darkMode,
    ControlDensity density) {
    Theme theme = darkMode ? dark(density) : light(density);
    // font scale：同步放大排版、控件最小高度与相关间距（§4）。
    if (settings.fontScale > 0.0F && settings.fontScale != 1.0F) {
        const float scale = settings.fontScale;
        theme.typography.title.fontSize *= scale;
        theme.typography.label.fontSize *= scale;
        theme.typography.body.fontSize *= scale;
        theme.typography.caption.fontSize *= scale;
        theme.metrics.scaleBy(scale);
        scaleComponentSizes(theme, scale);
    }
    if (settings.highContrast) {
        applyHighContrast(theme, darkMode);
    }
    if (settings.reduceAnimation) {
        theme.motion.reduceAnimation();
    }
    return theme;
}

std::shared_ptr<void> makeThemeScopeData(Theme theme) {
    return std::make_shared<Theme>(std::move(theme));
}

Theme adaptPlatformTheme(const Theme& base, bool darkMode,
                         core::Color accentColor, float fontScale) {
    accessibility::AccessibilitySettings settings;
    settings.fontScale = fontScale;
    Theme adapted = Theme::fromSettings(settings, darkMode,
                                         base.metrics.density);
    // 强调色：filled 按钮与焦点环随平台 accent（token 链派生）。
    adapted.button.filled.background = accentColor;
    adapted.colors.accent = accentColor;
    // 字体缩放：经 accessibility 派生（typography 由 fromSettings 派生，
    // 这里保留 metrics 供后续 fromSettings 重派生）。
    (void)fontScale;
    return adapted;
}

}  // namespace lumen::style
