#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "lumen/accessibility/bridge.h"
#include "lumen/core/geometry.h"
#include "lumen/style/tokens.h"

namespace lumen::style {

// 视觉系统 Theme（docs/lumen-visual-system-design.md §4）。
//
// 可复制、按不可变方式使用的值对象，由应用或窗口根节点持有；没有全局
// 可变单例。Theme 不保存控件实例状态——状态由交互快照在解析时提供。
// 组件 token 引用 semantic token；应用只在品牌定制时提供局部
// StyleOverrides。

enum class ControlDensity : std::uint8_t {
    Compact,      // Small/Compact 列（§3.2 尺度表）
    Comfortable,  // Medium/Comfortable 列（桌面默认）
    Touch,        // Large/Touch 列（触摸平台默认）
};

// M11（v0.4 视觉方向，design/gallery.html）：方向 = palette 槽位 + 方向级
// Metrics 覆盖，经既有 primitive→semantic→component 单向链派生；每方向
// 都可与 dark/light、high contrast、font scale、reduceAnimation 正交组合。
// AuroraSignal 为扁平近似（玻璃/渐变/光晕不做，见 roadmap M11 已知限制）。
enum class ThemeDirection : std::uint8_t {
    CoreDark,        // 默认基线：neutral/blue（与 v0.3 token 等值）
    InkLinen,        // 暖光纸张 + 紫 accent 的编辑感方向
    AuroraSignal,    // 深蓝 + 青 accent 的渲染展示方向（扁平近似）
    UtilityContrast  // 高对比工具台：2px 边框、紧凑圆角
};

// 方向色板（槽位法）：把方向色值填进 PrimitivePalette 的 neutral/blue
// 槽位，darkColorScheme/lightColorScheme 映射零改动。dark/light 各一份。
[[nodiscard]] PrimitivePalette primitivePaletteFor(ThemeDirection direction,
                                                   bool darkMode);

// Semantic color token（§3.1）：用户可理解的角色。light/dark/high
// contrast 只改这里与派生规则，控件代码不遍历。
struct ColorScheme {
    core::Color pageBackground{24, 24, 27, 255};
    core::Color surface{39, 39, 46, 255};
    core::Color surfaceElevated{52, 52, 62, 255};
    core::Color contentPrimary{228, 228, 231, 255};
    core::Color contentSecondary{140, 140, 152, 255};
    core::Color accent{86, 140, 240, 255};
    core::Color onAccent{240, 244, 255, 255};
    // Tonal 变体的低饱和容器。
    core::Color accentContainer{46, 60, 96, 255};
    core::Color onAccentContainer{198, 214, 255, 255};
    core::Color borderDefault{82, 82, 91, 255};
    core::Color borderStrong{161, 161, 170, 255};
    core::Color focusRing{150, 185, 250, 255};
    core::Color selectionBackground{86, 140, 240, 130};
    core::Color statusError{224, 90, 96, 255};
    core::Color statusSuccess{74, 160, 106, 255};
    core::Color statusWarning{204, 152, 64, 255};
    core::Color disabledBackground{46, 46, 54, 255};
    core::Color disabledContent{140, 140, 152, 170};
    core::Color scrim{0, 0, 0, 132};
    // 状态叠加层（hover/pressed 用 blendOver 派生，保持状态可辨识）。
    core::Color hoverOverlay{255, 255, 255, 26};
    core::Color pressedOverlay{0, 0, 0, 72};

    bool operator==(const ColorScheme&) const = default;
};

// 排版 token：控件文本一律取自这里的角色样式（Button 用 label）。
struct Typography {
    core::TextStyle title{};
    core::TextStyle label{};
    core::TextStyle body{};
    core::TextStyle caption{};

    bool operator==(const Typography&) const = default;
};

// 尺度 token（§3.2 初始尺度）。每项保存 [Small, Medium, Large] 三档，
// density 决定基准档位；font scale 派生时同步放大。
struct Metrics {
    ControlDensity density{ControlDensity::Comfortable};
    // 基准档索引：Compact→0，Comfortable→1，Touch→2；ControlSize 在此
    // 基础上偏移（Small 偏小、Large 偏大，不越界）。
    std::uint8_t baseIndex{1};

    float minHeight[3]{32.0F, 40.0F, 48.0F};
    float buttonMinWidth[3]{64.0F, 64.0F, 72.0F};
    float textFieldMinWidth[3]{96.0F, 120.0F, 144.0F};
    float controlPaddingX[3]{8.0F, 12.0F, 16.0F};
    float controlPaddingY[3]{4.0F, 6.0F, 8.0F};
    float controlGap[3]{4.0F, 8.0F, 8.0F};
    float controlRadius[3]{4.0F, 6.0F, 8.0F};

    float cardRadius{8.0F};
    float dialogRadius{12.0F};
    float focusRingWidth{2.0F};
    float controlBorderWidth{1.0F};

    // 等比放大（font scale 派生）：最小高度、内边距、间距与最小宽度。
    void scaleBy(float factor);

    bool operator==(const Metrics&) const = default;
};

// 阴影/层级 token（§8 冻结扩展）：当前 Renderer 不支持阴影时组件用边框
// 与表面层级表达，不得在控件中散落阴影常量。
struct ElevationTokens {
    core::Color shadowColor{0, 0, 0, 96};
    core::Offset shadowOffset{0.0F, 4.0F};
    float shadowBlur{12.0F};
    float dialogLevel{3.0F};
    float cardLevel{1.0F};

    bool operator==(const ElevationTokens&) const = default;
};

// 动效 token（§8 冻结扩展）：reduceAnimation 时全部时长归零，并继续遵
// 守 FrameScheduler 的可访问性规则。M11：tooltipDelayMs 为 hover 显隐
// 去抖（reduceAnimation 归零 = 立即显示），tooltipFadeMs 为淡入淡出。
struct MotionTokens {
    std::uint32_t stateTransitionMs{kDurationFastMs};
    std::uint32_t dialogTransitionMs{kDurationNormalMs};
    std::uint32_t navigatorTransitionMs{kDurationSlowMs};
    std::uint32_t caretBlinkHalfPeriodMs{530};
    std::uint32_t tooltipDelayMs{400};
    std::uint32_t tooltipFadeMs{120};

    void reduceAnimation();

    bool operator==(const MotionTokens&) const = default;
};

// --- Component token（§3.1）：控件部件与变体的基础值；hover/pressed
// 等状态值由 StyleResolver 从这些基础值派生。 ---

struct ButtonVariantTokens {
    core::Color background{core::Color::transparent()};
    core::Color content{core::Color::transparent()};
    core::Color border{core::Color::transparent()};

    bool operator==(const ButtonVariantTokens&) const = default;
};

struct ButtonTokens {
    ButtonVariantTokens filled{};
    ButtonVariantTokens tonal{};
    ButtonVariantTokens outline{};
    ButtonVariantTokens ghost{};
    ButtonVariantTokens danger{};

    bool operator==(const ButtonTokens&) const = default;
};

struct TextFieldTokens {
    core::Color background{46, 46, 54, 255};
    core::Color border{82, 82, 91, 255};
    core::Color borderFocused{150, 185, 250, 255};
    core::Color borderInvalid{224, 90, 96, 255};
    core::Color placeholder{140, 140, 152, 255};
    core::Color caret{228, 228, 231, 255};
    core::Color preeditUnderline{160, 190, 250, 255};

    bool operator==(const TextFieldTokens&) const = default;
};

struct CheckboxTokens {
    core::Color indicator{60, 60, 70, 255};
    core::Color indicatorChecked{86, 140, 240, 255};
    core::Color mark{240, 244, 255, 255};
    // [Small, Medium, Large]；随 density/font scale 派生。
    float indicatorSize[3]{16.0F, 18.0F, 22.0F};
    float markInset[3]{4.0F, 4.0F, 5.0F};
    float labelGap{8.0F};

    bool operator==(const CheckboxTokens&) const = default;
};

struct SwitchTokens {
    core::Color trackOff{60, 60, 70, 255};
    core::Color trackOn{86, 140, 240, 255};
    core::Color knob{228, 228, 231, 255};
    // [Small, Medium, Large]：宽×高 / 滑块边长。
    float trackWidth[3]{32.0F, 36.0F, 44.0F};
    float trackHeight[3]{18.0F, 20.0F, 24.0F};
    float knobSize[3]{12.0F, 14.0F, 16.0F};
    float knobInset{3.0F};
    float labelGap{8.0F};

    bool operator==(const SwitchTokens&) const = default;
};

struct DialogTokens {
    core::Color scrim{0, 0, 0, 132};
    core::Color surface{52, 52, 62, 255};
    float radius{12.0F};
    core::EdgeInsets padding{24.0F, 24.0F, 24.0F, 24.0F};
    float minWidth{240.0F};
    float maxWidth{420.0F};
    float widthRatio{0.6F};
    float heightRatio{0.5F};
    float actionGap{8.0F};
    float elevation{3.0F};

    bool operator==(const DialogTokens&) const = default;
};

struct ScrollbarTokens {
    core::Color rest{140, 140, 152, 120};
    core::Color hovered{161, 161, 170, 180};
    core::Color dragged{161, 161, 170, 230};
    float thickness{8.0F};
    float minLength{24.0F};

    bool operator==(const ScrollbarTokens&) const = default;
};

// 主题值对象（§4 建议结构的实现）。light/dark/high contrast/font
// scale/density/reduced motion 都由 fromSettings 与工厂派生。
struct Theme {
    ColorScheme colors{};
    Typography typography{};
    Metrics metrics{};
    ElevationTokens elevation{};
    MotionTokens motion{};
    IconTheme icons{};
    ButtonTokens button{};
    TextFieldTokens textField{};
    CheckboxTokens checkbox{};
    SwitchTokens switchControl{};
    DialogTokens dialog{};
    ScrollbarTokens scrollbar{};
    // M11：派生元数据（值语义，参与 ==）。应用直接读取，替代按色值
    // 反推（gallery/settings 旧的 pageBackground 比较启发式）。
    ThemeDirection direction{ThemeDirection::CoreDark};
    bool darkMode{true};

    [[nodiscard]] static Theme dark(
        ControlDensity density = ControlDensity::Comfortable,
        ThemeDirection direction = ThemeDirection::CoreDark);
    [[nodiscard]] static Theme light(
        ControlDensity density = ControlDensity::Comfortable,
        ThemeDirection direction = ThemeDirection::CoreDark);
    [[nodiscard]] static Theme fromSettings(
        const accessibility::AccessibilitySettings& settings,
        bool darkMode = true,
        ControlDensity density = ControlDensity::Comfortable,
        ThemeDirection direction = ThemeDirection::CoreDark);

    bool operator==(const Theme&) const = default;
};

// M6：ThemeScope 构建（shared_ptr<void> 携带 Theme 拷贝；核心不接触
// 样式类型，布局层经 style::ScopedThemeOverride 还原）。
[[nodiscard]] std::shared_ptr<void> makeThemeScopeData(Theme theme);

// M6：平台主题适配器——只转换系统主题输入（dark mode/accent/字体缩放）
// 到 Theme 派生，不返回平台控件对象。base 为应用当前主题。
// M12 修复：settings 携带应用当前可访问性输入（高对比/减少动画/字体
// 缩放），darkMode/accent 只切换对应维度——不再丢弃 base 的派生；方向
// 继承 base.direction。accentColor 为空时保持 base 的强调色。accent
// 覆盖后经组件 token 重建（button/checkbox/switch 链一致派生）。
[[nodiscard]] Theme adaptPlatformTheme(
    const Theme& base, const accessibility::AccessibilitySettings& settings,
    bool darkMode, std::optional<core::Color> accentColor = std::nullopt);

// Primitive → semantic 映射（测试契约 §10.1）。
[[nodiscard]] ColorScheme darkColorScheme(const PrimitivePalette& palette);
[[nodiscard]] ColorScheme lightColorScheme(const PrimitivePalette& palette);

// Semantic → component 映射（测试契约 §10.1）。
[[nodiscard]] ButtonTokens buttonTokensFrom(const ColorScheme& colors);
[[nodiscard]] TextFieldTokens textFieldTokensFrom(const ColorScheme& colors);
[[nodiscard]] CheckboxTokens checkboxTokensFrom(const ColorScheme& colors);
[[nodiscard]] SwitchTokens switchTokensFrom(const ColorScheme& colors);
[[nodiscard]] DialogTokens dialogTokensFrom(const ColorScheme& colors,
                                            const Metrics& metrics);
[[nodiscard]] ScrollbarTokens scrollbarTokensFrom(const ColorScheme& colors);

// density → 基准档索引（0/1/2）。
[[nodiscard]] std::uint8_t densityBaseIndex(ControlDensity density);

}  // namespace lumen::style
