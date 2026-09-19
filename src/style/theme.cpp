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
    for (float& value : inlineIconSize) {
        scale(value);
    }
}

const ElevationShadowParams& ElevationTokens::paramsFor(float level) const {
    // 层级数夹取到 [1, 3]；0（无阴影）不经此路径（painter 先判断
    // elevation > 0）。
    const auto index = static_cast<std::size_t>(
        std::clamp(level, 1.0F, 3.0F));
    return levels[index];
}

void MotionTokens::reduceAnimation() {
    stateTransitionMs = 0;
    switchTransitionMs = 0;
    dialogTransitionMs = 0;
    navigatorTransitionMs = 0;
    caretBlinkHalfPeriodMs = 0;
    tooltipDelayMs = 0;
    tooltipFadeMs = 0;
    menuOpenFadeMs = 0;
    menuHighlightSlideMs = 0;
    menuSubmenuHoverMs = 0;
}

// --- Primitive → semantic ---
// S1（gui-control-visual-system-task §4.2）：深色 Filled 使用深色文字
//（旧 onAccent ≈#EBF1FF 对 accent 约 2.90:1，不达标）；pressed 叠加减
// 轻；disabledContent 实色化。四方向共用这些可读性修正。

ColorScheme darkColorScheme(const PrimitivePalette& palette) {
    ColorScheme colors;
    colors.pageBackground = palette.neutral950;
    colors.surface = palette.neutral900;
    colors.surfaceElevated = mixColors(palette.neutral800,
                                        palette.neutral200, 0.06F);
    colors.surfaceSunken = palette.neutral800;
    colors.contentPrimary = palette.neutral200;
    colors.contentSecondary = palette.neutral500;
    colors.accent = palette.blue500;
    colors.onAccent = palette.black;
    colors.accentContent = mixColors(palette.blue300, palette.blue100, 0.25F);
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
    colors.onError = palette.black;
    colors.errorContent = palette.red300;
    colors.statusSuccess = palette.green500;
    colors.statusWarning = palette.amber500;
    colors.disabledBackground = palette.neutral800;
    colors.disabledContent = palette.neutral400;
    colors.scrim = core::Color{0, 0, 0, 132};
    colors.hoverOverlay = core::Color{255, 255, 255, 26};
    colors.pressedOverlay = core::Color{0, 0, 0, 26};
    return colors;
}

ColorScheme lightColorScheme(const PrimitivePalette& palette) {
    ColorScheme colors;
    colors.pageBackground = palette.neutral100;
    colors.surface = palette.neutral200;
    colors.surfaceElevated = palette.neutral050;
    colors.surfaceSunken = palette.neutral100;
    colors.contentPrimary = palette.neutral950;
    colors.contentSecondary = palette.neutral500;
    colors.accent = palette.blue500;
    colors.onAccent = palette.white;
    colors.accentContent = palette.blue700;
    colors.accentContainer = palette.blue100;
    colors.onAccentContainer = palette.blue700;
    colors.borderDefault = palette.neutral300;
    colors.borderStrong = palette.neutral500;
    colors.focusRing = palette.blue700;
    colors.selectionBackground =
        core::Color{palette.blue500.r, palette.blue500.g, palette.blue500.b,
                    64};
    colors.statusError = palette.red700;
    colors.onError = palette.white;
    colors.errorContent = palette.red700;
    colors.statusSuccess = palette.green500;
    colors.statusWarning = palette.amber500;
    colors.disabledBackground = palette.neutral200;
    colors.disabledContent = palette.neutral500;
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
    tokens.outline.content = colors.accentContent;
    tokens.outline.border = colors.borderStrong;
    tokens.ghost.background = core::Color::transparent();
    tokens.ghost.content = colors.accentContent;
    // Ghost 边框色派生 borderStrong：Ghost 本体边框宽为 0（外观不变），
    // 供以 Ghost Button 承载的框架 chrome 取色——Splitter 分隔条 rest 线
    //（splitter-design §9.2：rest 1px = color.border.strong）。
    tokens.ghost.border = colors.borderStrong;
    tokens.danger.background = colors.statusError;
    tokens.danger.content = colors.onError;
    return tokens;
}

TextFieldTokens textFieldTokensFrom(const ColorScheme& colors) {
    TextFieldTokens tokens;
    tokens.background = colors.surfaceSunken;
    tokens.border = colors.borderStrong;
    tokens.borderFocused = colors.focusRing;
    tokens.borderInvalid = colors.statusError;
    tokens.placeholder = colors.contentSecondary;
    tokens.caret = colors.contentPrimary;
    tokens.preeditUnderline = colors.focusRing;
    return tokens;
}

CheckboxTokens checkboxTokensFrom(const ColorScheme& colors) {
    CheckboxTokens tokens;
    // §6.4：Off = surfaceSunken 内部 + borderStrong 轮廓；On = accent +
    // onAccent 勾号。
    tokens.indicator = colors.surfaceSunken;
    tokens.indicatorOutline = colors.borderStrong;
    tokens.indicatorChecked = colors.accent;
    tokens.mark = colors.onAccent;
    return tokens;
}

SwitchTokens switchTokensFrom(const ColorScheme& colors) {
    SwitchTokens tokens;
    // §6.4：Off 轨道 surfaceSunken + borderStrong 轮廓；On 轨道 accent；
    // knobOff=contentPrimary、knobOn=onAccent（两态对比独立保证）。
    tokens.trackOff = colors.surfaceSunken;
    tokens.trackOutline = colors.borderStrong;
    tokens.trackOn = colors.accent;
    tokens.knobOff = colors.contentPrimary;
    tokens.knobOn = colors.onAccent;
    return tokens;
}

RadioTokens radioTokensFrom(const ColorScheme& colors) {
    RadioTokens tokens;
    // §6.4：Off 空心环 surfaceSunken + borderStrong；On 外环 accent、内点
    // accent（环内保留表面空隙，不用两次同色填充冒充空心环）。
    tokens.indicator = colors.surfaceSunken;
    tokens.indicatorOutline = colors.borderStrong;
    tokens.indicatorChecked = colors.accent;
    tokens.dot = colors.accent;
    return tokens;
}

SliderTokens sliderTokensFrom(const ColorScheme& colors) {
    SliderTokens tokens;
    // §6.5：未完成轨道 borderStrong、完成轨道 accent；Thumb 表面 +
    // accent 轮廓。
    tokens.trackRemaining = colors.borderStrong;
    tokens.trackActive = colors.accent;
    tokens.thumbFill = colors.surfaceElevated;
    tokens.thumbOutline = colors.accent;
    return tokens;
}

ProgressBarTokens progressBarTokensFrom(const ColorScheme& colors) {
    ProgressBarTokens tokens;
    tokens.track = colors.borderDefault;
    tokens.fill = colors.accent;
    return tokens;
}

TabsTokens tabsTokensFrom(const ColorScheme& colors) {
    TabsTokens tokens;
    tokens.indicator = colors.accent;
    tokens.selectedContent = colors.accentContent;
    tokens.unselectedContent = colors.contentSecondary;
    tokens.separator = colors.borderDefault;
    return tokens;
}

TooltipTokens tooltipTokensFrom(const ColorScheme& colors) {
    TooltipTokens tokens;
    tokens.surface = colors.surfaceElevated;
    tokens.border = colors.borderDefault;
    tokens.content = colors.contentPrimary;
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
    // S3（§7.2）：rest 取 borderStrong 实色。
    ScrollbarTokens tokens;
    tokens.rest = colors.borderStrong;
    tokens.hovered = colors.borderStrong;
    tokens.dragged = colors.borderStrong;
    return tokens;
}

ListTokens listTokensFrom(const ColorScheme& colors) {
    ListTokens tokens;
    tokens.background = colors.surface;
    tokens.hovered = colors.surfaceSunken;
    tokens.pressed = mixColors(colors.surface, colors.accent, 0.32F);
    tokens.selected = colors.accentContainer;
    tokens.separator = colors.borderDefault;
    tokens.content = colors.contentPrimary;
    tokens.disabledContent = colors.disabledContent;
    tokens.emptyContent = colors.contentSecondary;
    tokens.selectionMarker = colors.accent;
    return tokens;
}

// --- Theme 工厂 ---

namespace {

// M11：方向色板（design/gallery.html 四方向，槽位法填入 PrimitivePalette；
// 映射函数 dark/lightColorScheme 零改动）。CoreDark 以 v0.3 默认调色板为
// 基线，dark 侧对齐设计稿 v1 的 ink/muted/line/line-strong 槽位。
// S1（§4.2 目标调色板）：dark 补 borderStrong/success/warning 槽位；
// light 侧新增表面/文字/强调/状态槽位覆盖。
// AuroraSignal 为扁平近似：rgba 表面取实底（合成到页面色），玻璃/渐变/
// 光晕不做；InkLinen 的 serif display 排版不做（跨平台字体确定性优先）。
PrimitivePalette coreDarkPalette(bool darkMode) {
    PrimitivePalette palette;
    if (darkMode) {
        // design/gallery.html v1 Core Dark：--app-ink/#f1f1f4、
        // --app-muted/#a1a1aa、--app-line/#3b3b45；§4.2 line-strong/
        // success/warning 目标值。
        palette.neutral200 = {241, 241, 244, 255};
        palette.neutral400 = {140, 140, 152, 255};
        palette.neutral500 = {161, 161, 170, 255};
        palette.neutral600 = {59, 59, 69, 255};
        palette.green500 = {115, 201, 145, 255};
        palette.amber500 = {226, 181, 102, 255};
    } else {
        // §4.2 light：纯白表面、深 accent #3460BE、深辅助文字与
        // focusRing/onAccentContainer/accentContent #234A91、状态色加深。
        palette.neutral050 = {255, 255, 255, 255};
        palette.neutral200 = {255, 255, 255, 255};
        palette.neutral500 = {82, 82, 91, 255};
        palette.blue500 = {52, 96, 190, 255};
        palette.blue700 = {35, 74, 145, 255};
        palette.green500 = {37, 111, 70, 255};
        palette.amber500 = {138, 87, 0, 255};
    }
    return palette;
}

// §4.2 Core Dark 目标调色板中无法由槽位映射精确表达的语义值（槽位冲
// 突或非线性派生）；在组件 token 重建之前应用。原始色值只存在于主题
// 工厂（§4.1）。
void applyCoreDarkTargets(ColorScheme& colors, bool darkMode) {
    if (darkMode) {
        colors.surfaceElevated = core::Color{52, 52, 63, 255};   // #34343F
        colors.accentContainer = core::Color{46, 60, 96, 255};   // #2E3C60
        colors.accentContent = core::Color{168, 197, 250, 255};  // #A8C5FA
    } else {
        colors.borderStrong = core::Color{113, 113, 122, 255};  // #71717A
        colors.disabledBackground =
            core::Color{228, 228, 231, 255};                    // #E4E4E7
        colors.disabledContent = colors.borderStrong;
    }
}

PrimitivePalette inkLinenPalette(bool darkMode) {
    PrimitivePalette palette;
    if (darkMode) {
        palette.neutral050 = {248, 246, 241, 255};
        palette.neutral100 = {240, 237, 231, 255};
        palette.neutral200 = {233, 229, 222, 255};
        palette.neutral300 = {196, 190, 180, 255};
        palette.neutral400 = {168, 161, 150, 255};
        // S1（§4.3）：辅助文字/浅色 borderStrong 槽位加深，满足 4.5:1/3:1。
        palette.neutral500 = {150, 144, 136, 255};
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
    // S1（§4.3）：辅助文字/浅色 borderStrong 槽位加深，满足 4.5:1/3:1。
    palette.neutral500 = {100, 96, 90, 255};
    palette.neutral600 = {101, 97, 90, 255};
    palette.neutral700 = {80, 76, 70, 255};
    palette.neutral800 = {58, 55, 50, 255};
    palette.neutral900 = {46, 43, 39, 255};
    palette.neutral950 = {37, 37, 43, 255};
    palette.blue100 = {238, 235, 255, 255};
    palette.blue300 = {166, 152, 235, 255};
    palette.blue500 = {99, 87, 200, 255};
    palette.blue700 = {74, 61, 158, 255};
    // S1（§4.3）：状态文字在暖色浅底上加深。
    palette.green500 = {48, 112, 80, 255};
    palette.amber500 = {140, 88, 0, 255};
    return palette;
}

PrimitivePalette auroraPalette(bool darkMode) {
    PrimitivePalette palette;
    if (darkMode) {
        palette.neutral050 = {244, 248, 255, 255};
        palette.neutral100 = {228, 235, 250, 255};
        palette.neutral200 = {240, 245, 255, 255};
        palette.neutral300 = {198, 210, 232, 255};
        // S1（§4.3）：borderStrong/禁用文字槽位提亮（旧 (75,93,126) 对
        // 本方向表面仅约 2.4:1，不满足轮廓/禁用可辨认门槛）。
        palette.neutral400 = {150, 165, 195, 255};
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
    palette.blue500 = {20, 114, 154, 255};  // S1（§4.3）：浅底白字 ≥4.5:1
    palette.blue700 = {10, 80, 112, 255};   // S1（§4.3）：accentContent/Tonal 文字
    palette.green500 = {18, 110, 74, 255};  // S1（§4.3）：状态文字加深
    palette.amber500 = {140, 88, 0, 255};   // S1（§4.3）：状态文字加深
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
    palette.amber500 = {140, 88, 0, 255};  // S1（§4.3）：状态文字加深
    return palette;
}

Typography defaultTypography() {
    Typography typography;
    // §4.5：行高列为逻辑高度（title 28 / body·label 20 / caption 18），
    // TextStyle.lineHeight 是倍数（行高/字号）。
    typography.title.fontSize = 20.0F;
    typography.title.weight = 600;
    typography.title.lineHeight = 28.0F / 20.0F;
    typography.label.fontSize = 14.0F;
    typography.label.weight = 500;
    typography.label.lineHeight = 20.0F / 14.0F;
    typography.body.fontSize = 14.0F;
    typography.body.lineHeight = 20.0F / 14.0F;
    typography.caption.fontSize = 12.0F;
    typography.caption.lineHeight = 18.0F / 12.0F;
    return typography;
}

// §4.5 阴影分级：L1 (0,2)/6/32，L2 (0,4)/12/64，L3 (0,8)/24/80（黑色）。
ElevationTokens defaultElevation() {
    ElevationTokens tokens;
    tokens.levels[1] = ElevationShadowParams{core::Offset{0.0F, 2.0F},
                                             6.0F, 32};
    tokens.levels[2] = ElevationShadowParams{core::Offset{0.0F, 4.0F},
                                             12.0F, 64};
    tokens.levels[3] = ElevationShadowParams{core::Offset{0.0F, 8.0F},
                                             24.0F, 80};
    return tokens;
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
    // CoreDark 的 §4.2 目标值精化（槽位映射无法表达的少数项）。
    if (direction == ThemeDirection::CoreDark) {
        applyCoreDarkTargets(theme.colors, darkMode);
    }
    // AuroraSignal dark 的浅青 accent 作为选区底（alpha 130）会把亮色正文
    // 洗到 4.5 以下；降低选区 alpha 保持文字可读（方向 accent 本身不变）。
    if (direction == ThemeDirection::AuroraSignal && darkMode) {
        theme.colors.selectionBackground =
            core::Color{palette.blue500.r, palette.blue500.g,
                        palette.blue500.b, 96};
    }
    theme.typography = defaultTypography();
    theme.metrics.density = density;
    theme.metrics.baseIndex = densityBaseIndex(density);
    applyDirectionMetrics(theme.metrics, direction);
    theme.elevation = defaultElevation();
    theme.button = buttonTokensFrom(theme.colors);
    theme.textField = textFieldTokensFrom(theme.colors);
    theme.checkbox = checkboxTokensFrom(theme.colors);
    theme.switchControl = switchTokensFrom(theme.colors);
    theme.radio = radioTokensFrom(theme.colors);
    theme.slider = sliderTokensFrom(theme.colors);
    theme.progressBar = progressBarTokensFrom(theme.colors);
    theme.tabs = tabsTokensFrom(theme.colors);
    theme.tooltip = tooltipTokensFrom(theme.colors);
    theme.dialog = dialogTokensFrom(theme.colors, theme.metrics);
    theme.scrollbar = scrollbarTokensFrom(theme.colors);
    theme.list = listTokensFrom(theme.colors);
    theme.direction = direction;
    theme.darkMode = darkMode;
    return theme;
}

// 高对比度：纯色正文、更强边框/焦点环，同时保留状态可辨识性（§4）。
// M11：accent 按方向色板取（blue300/700 槽位），非蓝方向不再错色。
// S1：新增语义角色同步派生；阴影不承担层级信息（§4.3），各级 alpha 置 0。
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
    // accent 直接承担强调文字（HC 下 accent 本身已满足对比）。
    theme.colors.accentContent = theme.colors.accent;
    theme.colors.disabledContent =
        core::Color{theme.colors.contentPrimary.r,
                    theme.colors.contentPrimary.g,
                    theme.colors.contentPrimary.b, 140};
    // 状态层增强：hover/pressed 差异不只靠色相。
    theme.colors.hoverOverlay = darkMode ? core::Color{255, 255, 255, 64}
                                         : core::Color{0, 0, 0, 32};
    theme.colors.pressedOverlay = darkMode ? core::Color{0, 0, 0, 110}
                                           : core::Color{0, 0, 0, 64};
    for (auto& level : theme.elevation.levels) {
        level.alpha = 0;
    }
    // 组件 token 同步重建（semantic → component 单向映射）。
    theme.button = buttonTokensFrom(theme.colors);
    theme.textField = textFieldTokensFrom(theme.colors);
    theme.checkbox = checkboxTokensFrom(theme.colors);
    theme.switchControl = switchTokensFrom(theme.colors);
    theme.radio = radioTokensFrom(theme.colors);
    theme.slider = sliderTokensFrom(theme.colors);
    theme.progressBar = progressBarTokensFrom(theme.colors);
    theme.tabs = tabsTokensFrom(theme.colors);
    theme.tooltip = tooltipTokensFrom(theme.colors);
    theme.scrollbar = scrollbarTokensFrom(theme.colors);
    theme.metrics.focusRingWidth = 3.0F;
    theme.metrics.controlBorderWidth = 2.0F;
    theme.list = listTokensFrom(theme.colors);
    theme.list.hovered = blendOver(theme.list.background, theme.colors.hoverOverlay);
    theme.list.pressed = blendOver(theme.list.background, theme.colors.pressedOverlay);
}

void scaleComponentSizes(Theme& theme, float factor) {
    if (factor <= 0.0F || factor == 1.0F) {
        return;
    }
    theme.list.markerWidth *= factor;
    theme.list.markerInset *= factor;
    theme.list.emptyIconSize *= factor;
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
    for (float& value : theme.radio.indicatorSize) {
        value *= factor;
    }
    for (float& value : theme.slider.thumbDiameter) {
        value *= factor;
    }
    theme.checkbox.labelGap *= factor;
    theme.switchControl.labelGap *= factor;
    theme.radio.labelGap *= factor;
    theme.tabs.tabPaddingX *= factor;
    theme.tabs.tabGap *= factor;
    theme.tooltip.paddingX *= factor;
    theme.tooltip.paddingY *= factor;
    theme.dialog.padding.left *= factor;
    theme.dialog.padding.right *= factor;
    theme.dialog.padding.top *= factor;
    theme.dialog.padding.bottom *= factor;
    theme.dialog.minWidth *= factor;
    theme.dialog.maxWidth *= factor;
    theme.dialog.actionGap *= factor;
    theme.icons.defaultSize *= factor;
}

void scaleTheme(Theme& theme, float factor) {
    if (factor <= 0.0F || factor == 1.0F) {
        return;
    }
    theme.typography.title.fontSize *= factor;
    theme.typography.label.fontSize *= factor;
    theme.typography.body.fontSize *= factor;
    theme.typography.caption.fontSize *= factor;
    theme.metrics.scaleBy(factor);
    scaleComponentSizes(theme, factor);
}

}  // namespace

PrimitivePalette primitivePaletteFor(ThemeDirection direction,
                                     bool darkMode) {
    switch (direction) {
        case ThemeDirection::CoreDark:
            return coreDarkPalette(darkMode);
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
    // Color-derived tokens are rebuilt before dimensions are scaled once.
    if (settings.highContrast) {
        applyHighContrast(theme, darkMode, direction);
    }
    scaleTheme(theme, settings.fontScale);
    if (settings.reduceAnimation) {
        theme.motion.reduceAnimation();
    }
    return theme;
}

std::shared_ptr<void> makeThemeScopeData(Theme theme) {
    return std::make_shared<Theme>(std::move(theme));
}

Theme adaptPlatformTheme(const Theme& base,
                         const accessibility::AccessibilitySettings& settings,
                         bool darkMode, std::optional<core::Color> accentColor) {
    // M12：以应用的完整可访问性输入重派生——高对比/减少动画/字体缩放
    // 与方向全部保留（旧实现只带 fontScale，会丢弃 base 的派生）。
    auto unscaledSettings = settings;
    unscaledSettings.fontScale = 1.0F;
    Theme adapted = Theme::fromSettings(unscaledSettings, darkMode,
                                        base.metrics.density,
                                        base.direction);
    if (accentColor.has_value()) {
        const auto luminance = [](core::Color c) {
            const auto linear = [](double v) {
                v /= 255.0;
                return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
            };
            return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b);
        };
        const auto contrast = [&](core::Color a, core::Color b) {
            const double x = luminance(a), y = luminance(b);
            return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
        };
        // System accents are arbitrary (including nearly white/yellow). Preserve
        // their hue while finding a legal tone for text and control marks.
        auto accent = *accentColor;
        accent.a = 255;
        const core::Color endpoint = darkMode ? core::Color{255, 255, 255, 255}
                                              : core::Color{0, 0, 0, 255};
        adapted.colors.onAccent = darkMode ? core::Color{0, 0, 0, 255}
                                           : core::Color{255, 255, 255, 255};
        const auto safeTone = [&](double surfaceRatio, bool filled) {
            for (int step = 0; step <= 1000; ++step) {
                const auto candidate = mixColors(accent, endpoint, float(step) / 1000.0F);
                bool safe = true;
                for (const auto surface : {adapted.colors.pageBackground,
                                           adapted.colors.surface,
                                           adapted.colors.surfaceSunken,
                                           adapted.colors.surfaceElevated}) {
                    safe = safe && contrast(candidate, surface) >= surfaceRatio;
                }
                if (filled) {
                    for (const auto overlay : {core::Color::transparent(),
                                               adapted.colors.hoverOverlay,
                                               adapted.colors.pressedOverlay}) {
                        safe = safe && contrast(adapted.colors.onAccent,
                            blendOver(candidate, overlay)) >= 4.5;
                    }
                }
                if (safe) return candidate;
            }
            return endpoint;
        };
        adapted.colors.accent = safeTone(3.0, true);
        adapted.colors.accentContent = safeTone(settings.highContrast ? 7.0 : 4.5, false);
        adapted.button = buttonTokensFrom(adapted.colors);
        adapted.checkbox = checkboxTokensFrom(adapted.colors);
        adapted.switchControl = switchTokensFrom(adapted.colors);
        adapted.radio = radioTokensFrom(adapted.colors);
        adapted.slider = sliderTokensFrom(adapted.colors);
        adapted.progressBar = progressBarTokensFrom(adapted.colors);
        adapted.tabs = tabsTokensFrom(adapted.colors);
        adapted.list = listTokensFrom(adapted.colors);
        if (settings.highContrast) {
            adapted.list.hovered = blendOver(adapted.list.background, adapted.colors.hoverOverlay);
            adapted.list.pressed = blendOver(adapted.list.background, adapted.colors.pressedOverlay);
        }
    }
    scaleTheme(adapted, settings.fontScale);
    return adapted;
}

}  // namespace lumen::style
