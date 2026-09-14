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
    tooltipDelayMs = 0;
    tooltipFadeMs = 0;
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

// M11：方向色板（design/gallery.html 四方向，槽位法填入 PrimitivePalette；
// 映射函数 dark/lightColorScheme 零改动）。CoreDark 与默认调色板等值。
// AuroraSignal 为扁平近似：rgba 表面取实底（合成到页面色），玻璃/渐变/
// 光晕不做；InkLinen 的 serif display 排版不做（跨平台字体确定性优先）。
PrimitivePalette inkLinenPalette(bool darkMode) {
    PrimitivePalette palette;
    if (darkMode) {
        palette.neutral050 = {248, 246, 241, 255};
        palette.neutral100 = {240, 237, 231, 255};
        palette.neutral200 = {233, 229, 222, 255};
        palette.neutral300 = {196, 190, 180, 255};
        palette.neutral400 = {168, 161, 150, 255};
        palette.neutral500 = {122, 116, 108, 255};
        palette.neutral600 = {75, 70, 63, 255};
        palette.neutral700 = {56, 52, 47, 255};
        palette.neutral800 = {42, 39, 35, 255};
        palette.neutral900 = {33, 31, 28, 255};
        palette.neutral950 = {26, 24, 21, 255};
        palette.blue100 = {233, 228, 255, 255};
        palette.blue300 = {173, 163, 248, 255};
        palette.blue500 = {139, 127, 240, 255};
        palette.blue700 = {99, 87, 200, 255};
        palette.green500 = {98, 178, 134, 255};
        return palette;
    }
    palette.neutral050 = {255, 253, 249, 255};
    palette.neutral100 = {245, 242, 236, 255};
    palette.neutral200 = {240, 236, 228, 255};
    palette.neutral300 = {223, 217, 206, 255};
    palette.neutral400 = {190, 182, 167, 255};
    palette.neutral500 = {119, 115, 109, 255};
    palette.neutral600 = {101, 97, 90, 255};
    palette.neutral700 = {80, 76, 70, 255};
    palette.neutral800 = {58, 55, 50, 255};
    palette.neutral900 = {46, 43, 39, 255};
    palette.neutral950 = {37, 37, 43, 255};
    palette.blue100 = {238, 235, 255, 255};
    palette.blue300 = {166, 152, 235, 255};
    palette.blue500 = {99, 87, 200, 255};
    palette.blue700 = {74, 61, 158, 255};
    palette.green500 = {61, 137, 100, 255};
    return palette;
}

PrimitivePalette auroraPalette(bool darkMode) {
    PrimitivePalette palette;
    if (darkMode) {
        palette.neutral050 = {244, 248, 255, 255};
        palette.neutral100 = {228, 235, 250, 255};
        palette.neutral200 = {240, 245, 255, 255};
        palette.neutral300 = {198, 210, 232, 255};
        palette.neutral400 = {75, 93, 126, 255};   // 边框 .42 实底
        palette.neutral500 = {141, 161, 196, 255};
        palette.neutral600 = {40, 51, 75, 255};    // 边框 .18 实底
        palette.neutral700 = {34, 44, 66, 255};
        palette.neutral800 = {31, 48, 77, 255};    // 表面 rgba 基色
        palette.neutral900 = {24, 35, 58, 255};    // 表面 .88 实底
        palette.neutral950 = {13, 20, 36, 255};
        palette.blue100 = {212, 247, 255, 255};
        palette.blue300 = {140, 224, 248, 255};
        palette.blue500 = {114, 216, 247, 255};
        palette.blue700 = {58, 175, 210, 255};
        palette.green500 = {119, 217, 163, 255};
        return palette;
    }
    palette.neutral050 = {244, 247, 252, 255};
    palette.neutral100 = {233, 238, 247, 255};
    palette.neutral200 = {219, 227, 240, 255};
    palette.neutral300 = {183, 195, 215, 255};
    palette.neutral400 = {96, 116, 150, 255};
    palette.neutral500 = {56, 74, 106, 255};
    palette.neutral600 = {74, 92, 124, 255};
    palette.neutral700 = {62, 78, 108, 255};
    palette.neutral800 = {44, 58, 86, 255};
    palette.neutral900 = {33, 45, 68, 255};
    palette.neutral950 = {16, 27, 48, 255};
    palette.blue100 = {214, 240, 250, 255};
    palette.blue300 = {124, 224, 248, 255};
    palette.blue500 = {26, 148, 192, 255};  // 浅底加深青
    palette.blue700 = {18, 110, 148, 255};
    palette.green500 = {26, 142, 96, 255};
    return palette;
}

PrimitivePalette utilityPalette(bool darkMode) {
    PrimitivePalette palette;
    if (darkMode) {
        palette.neutral050 = {248, 250, 252, 255};
        palette.neutral100 = {242, 245, 249, 255};
        palette.neutral200 = {233, 238, 244, 255};
        palette.neutral300 = {200, 210, 224, 255};
        palette.neutral400 = {170, 183, 202, 255};
        palette.neutral500 = {130, 144, 166, 255};
        palette.neutral600 = {60, 74, 100, 255};
        palette.neutral700 = {42, 54, 76, 255};
        palette.neutral800 = {28, 37, 54, 255};
        palette.neutral900 = {20, 27, 40, 255};
        palette.neutral950 = {13, 18, 28, 255};
        palette.blue100 = {215, 230, 250, 255};
        palette.blue300 = {150, 190, 240, 255};
        palette.blue500 = {105, 155, 225, 255};
        palette.blue700 = {29, 85, 165, 255};
        palette.green500 = {52, 150, 95, 255};
        return palette;
    }
    palette.neutral050 = {250, 252, 254, 255};
    palette.neutral100 = {246, 248, 251, 255};
    palette.neutral200 = {255, 255, 255, 255};  // 表面纯白（亮于页面）
    palette.neutral300 = {195, 204, 216, 255};
    palette.neutral400 = {101, 115, 135, 255};
    palette.neutral500 = {66, 81, 105, 255};
    palette.neutral600 = {84, 97, 118, 255};
    palette.neutral700 = {70, 82, 102, 255};
    palette.neutral800 = {52, 62, 80, 255};
    palette.neutral900 = {41, 50, 66, 255};
    palette.neutral950 = {17, 24, 39, 255};
    palette.blue100 = {220, 234, 255, 255};
    palette.blue300 = {120, 165, 220, 255};
    palette.blue500 = {29, 85, 165, 255};
    palette.blue700 = {23, 68, 133, 255};
    palette.green500 = {20, 119, 68, 255};
    return palette;
}

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

// 方向级 Metrics 覆盖（design/gallery.html 半径/边框差异；CoreDark 为
// 冻结默认值零覆盖）。
void applyDirectionMetrics(Metrics& metrics, ThemeDirection direction) {
    switch (direction) {
        case ThemeDirection::CoreDark:
            break;
        case ThemeDirection::InkLinen:
            metrics.controlRadius[0] = 6.0F;
            metrics.controlRadius[1] = 8.0F;
            metrics.controlRadius[2] = 10.0F;
            metrics.cardRadius = 12.0F;
            metrics.dialogRadius = 14.0F;
            break;
        case ThemeDirection::AuroraSignal:
            metrics.controlRadius[0] = 7.0F;
            metrics.controlRadius[1] = 9.0F;
            metrics.controlRadius[2] = 11.0F;
            metrics.cardRadius = 14.0F;
            metrics.dialogRadius = 16.0F;
            break;
        case ThemeDirection::UtilityContrast:
            metrics.controlRadius[0] = 3.0F;
            metrics.controlRadius[1] = 5.0F;
            metrics.controlRadius[2] = 7.0F;
            metrics.cardRadius = 7.0F;
            metrics.dialogRadius = 10.0F;
            metrics.controlBorderWidth = 2.0F;
            break;
    }
}

Theme baseTheme(bool darkMode, ControlDensity density,
                ThemeDirection direction) {
    const PrimitivePalette palette =
        primitivePaletteFor(direction, darkMode);
    Theme theme;
    theme.colors = darkMode ? darkColorScheme(palette)
                            : lightColorScheme(palette);
    // Aurora 近似修正：浅青 accent 上压深字（映射函数的 onAccent 派生
    // 不适用于高明度 accent）。
    if (direction == ThemeDirection::AuroraSignal && darkMode) {
        theme.colors.onAccent = core::Color{8, 19, 33, 255};
    }
    theme.typography = defaultTypography();
    theme.metrics.density = density;
    theme.metrics.baseIndex = densityBaseIndex(density);
    applyDirectionMetrics(theme.metrics, direction);
    theme.button = buttonTokensFrom(theme.colors);
    theme.textField = textFieldTokensFrom(theme.colors);
    theme.checkbox = checkboxTokensFrom(theme.colors);
    theme.switchControl = switchTokensFrom(theme.colors);
    theme.dialog = dialogTokensFrom(theme.colors, theme.metrics);
    theme.scrollbar = scrollbarTokensFrom(theme.colors);
    theme.direction = direction;
    theme.darkMode = darkMode;
    return theme;
}

// 高对比度：纯色正文、更强边框/焦点环，同时保留状态可辨识性（§4）。
// M11：accent 按方向色板取（blue300/700 槽位），非蓝方向不再错色。
void applyHighContrast(Theme& theme, bool darkMode, ThemeDirection direction) {
    const PrimitivePalette palette =
        primitivePaletteFor(direction, darkMode);
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

PrimitivePalette primitivePaletteFor(ThemeDirection direction,
                                     bool darkMode) {
    switch (direction) {
        case ThemeDirection::CoreDark:
            return PrimitivePalette{};
        case ThemeDirection::InkLinen:
            return inkLinenPalette(darkMode);
        case ThemeDirection::AuroraSignal:
            return auroraPalette(darkMode);
        case ThemeDirection::UtilityContrast:
            return utilityPalette(darkMode);
    }
    return PrimitivePalette{};
}

Theme Theme::dark(ControlDensity density, ThemeDirection direction) {
    return baseTheme(/*darkMode=*/true, density, direction);
}

Theme Theme::light(ControlDensity density, ThemeDirection direction) {
    return baseTheme(/*darkMode=*/false, density, direction);
}

Theme Theme::fromSettings(
    const accessibility::AccessibilitySettings& settings, bool darkMode,
    ControlDensity density, ThemeDirection direction) {
    Theme theme = baseTheme(darkMode, density, direction);
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
        applyHighContrast(theme, darkMode, direction);
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
                                        base.metrics.density,
                                        base.direction);
    // 强调色：filled 按钮与焦点环随平台 accent（token 链派生）。
    adapted.button.filled.background = accentColor;
    adapted.colors.accent = accentColor;
    // 字体缩放：经 accessibility 派生（typography 由 fromSettings 派生，
    // 这里保留 metrics 供后续 fromSettings 重派生）。
    (void)fontScale;
    return adapted;
}

}  // namespace lumen::style
