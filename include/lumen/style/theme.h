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
// S1（gui-control-visual-system-task §4.2）：surfaceSunken/accentContent/
// onError/errorContent 为新增角色；disabledContent 使用实色（避免多次
// 降透明叠加）。
struct ColorScheme {
    core::Color pageBackground{24, 24, 27, 255};
    core::Color surface{39, 39, 46, 255};
    core::Color surfaceElevated{52, 52, 62, 255};
    core::Color surfaceSunken{46, 46, 54, 255};
    core::Color contentPrimary{228, 228, 231, 255};
    core::Color contentSecondary{140, 140, 152, 255};
    core::Color accent{86, 140, 240, 255};
    core::Color onAccent{0, 0, 0, 255};
    core::Color accentContent{168, 197, 250, 255};
    // Tonal 变体的低饱和容器。
    core::Color accentContainer{46, 60, 96, 255};
    core::Color onAccentContainer{198, 214, 255, 255};
    core::Color borderDefault{82, 82, 91, 255};
    core::Color borderStrong{161, 161, 170, 255};
    core::Color focusRing{150, 185, 250, 255};
    core::Color selectionBackground{86, 140, 240, 130};
    core::Color statusError{224, 90, 96, 255};
    core::Color onError{0, 0, 0, 255};
    core::Color errorContent{240, 150, 154, 255};
    core::Color statusSuccess{74, 160, 106, 255};
    core::Color statusWarning{204, 152, 64, 255};
    core::Color disabledBackground{46, 46, 54, 255};
    core::Color disabledContent{140, 140, 152, 255};
    core::Color scrim{0, 0, 0, 132};
    // 状态叠加层（hover/pressed 用 blendOver 派生，保持状态可辨识）。
    core::Color hoverOverlay{255, 255, 255, 26};
    core::Color pressedOverlay{0, 0, 0, 26};

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
    // 行内图标边长档位（§4.4：Button 尾随图标/Dropdown Chevron 的槽位）。
    float inlineIconSize[3]{16.0F, 16.0F, 20.0F};

    // 等比放大（font scale 派生）：最小高度、内边距、间距与最小宽度。
    void scaleBy(float factor);

    bool operator==(const Metrics&) const = default;
};

// 阴影/层级 token（§4.5 分级目标）：三级抬升各自的 offset/blur/alpha；
// 颜色恒为黑色（shadowColor）。CPU 后端维持扁平降级；高对比模式把各级
// alpha 置 0（阴影不承担唯一层级信息）。
struct ElevationShadowParams {
    core::Offset offset{0.0F, 0.0F};
    float blur{0.0F};
    std::uint8_t alpha{0};

    bool operator==(const ElevationShadowParams&) const = default;
};

struct ElevationTokens {
    core::Color shadowColor{0, 0, 0, 255};
    // levels[0] 保留（无阴影）；Widget.elevation 的层级数（1..3）索引。
    // §4.5：L1 (0,2)/6/32，L2 (0,4)/12/64，L3 (0,8)/24/80。
    ElevationShadowParams levels[4]{};
    // 兼容旧 API：单级阴影参数（L2 等值）。
    float dialogLevel{3.0F};
    float cardLevel{1.0F};

    [[nodiscard]] const ElevationShadowParams& paramsFor(
        float level) const;

    bool operator==(const ElevationTokens&) const = default;
};

// 动效 token（§8 冻结扩展）：reduceAnimation 时全部时长归零，并继续遵
// 守 FrameScheduler 的可访问性规则。M11：tooltipDelayMs 为 hover 显隐
// 去抖（reduceAnimation 归零 = 立即显示），tooltipFadeMs 为淡入淡出。
struct MotionTokens {
    std::uint32_t stateTransitionMs{kDurationFastMs};
    std::uint32_t dialogTransitionMs{kDurationNormalMs};
    // S4（§9.1）：Navigator Fade 200ms（旧值 350 按本规格调整）。
    std::uint32_t navigatorTransitionMs{kDurationNormalMs};
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
    // S2（§6.4）：Off = surfaceSunken 内部 + borderStrong 轮廓；On = accent
    // 填充 + onAccent 勾号（IconId::Check）。
    core::Color indicator{46, 46, 54, 255};
    core::Color indicatorOutline{161, 161, 170, 255};
    core::Color indicatorChecked{86, 140, 240, 255};
    core::Color mark{0, 0, 0, 255};
    // [Small, Medium, Large]；随 density/font scale 派生。
    float indicatorSize[3]{16.0F, 18.0F, 22.0F};
    float markInset[3]{3.0F, 4.0F, 5.0F};
    float labelGap{8.0F};

    bool operator==(const CheckboxTokens&) const = default;
};

struct SwitchTokens {
    // S2（§6.4）：轨道 Off = surfaceSunken + borderStrong 轮廓；On = accent；
    // knob 两态独立（knobOff=contentPrimary、knobOn=onAccent）。
    core::Color trackOff{46, 46, 54, 255};
    core::Color trackOutline{161, 161, 170, 255};
    core::Color trackOn{86, 140, 240, 255};
    core::Color knobOff{228, 228, 231, 255};
    core::Color knobOn{0, 0, 0, 255};
    // [Small, Medium, Large]：宽×高 / 滑块边长。左右内距由
    // (trackHeight - knobSize) / 2 推导（§6.4），不再是固定值。
    float trackWidth[3]{32.0F, 36.0F, 44.0F};
    float trackHeight[3]{18.0F, 20.0F, 24.0F};
    float knobSize[3]{12.0F, 14.0F, 16.0F};
    float labelGap{8.0F};

    bool operator==(const SwitchTokens&) const = default;
};

// S2（§6.4）：Radio 专用部件 token——空心外环 + 独立内点（内点直径为
// 外径 0.45，点与环之间保留表面空隙）。
struct RadioTokens {
    core::Color indicator{46, 46, 54, 255};      // 环内表面（surfaceSunken）
    core::Color indicatorOutline{161, 161, 170, 255};  // Off 外环
    core::Color indicatorChecked{86, 140, 240, 255};   // On 外环
    core::Color dot{86, 140, 240, 255};          // On 内点（accent）
    float dotRatio{0.45F};
    float indicatorSize[3]{16.0F, 18.0F, 22.0F};
    float labelGap{8.0F};

    bool operator==(const RadioTokens&) const = default;
};

// S3（§6.5）：Slider 部件 token——细轨道 + 独立 Thumb（表面 + accent
// 轮廓）；端点预留 r+f（thumb 半径 + 焦点保护宽）。
struct SliderTokens {
    core::Color trackRemaining{161, 161, 170, 255};   // borderStrong
    core::Color trackActive{86, 140, 240, 255};       // accent
    core::Color thumbFill{52, 52, 63, 255};           // surfaceElevated
    core::Color thumbOutline{86, 140, 240, 255};      // accent
    float trackHeight{4.0F};
    float thumbDiameter[3]{16.0F, 18.0F, 22.0F};
    float thumbBorderWidth{2.0F};

    bool operator==(const SliderTokens&) const = default;
};

// S3（§6.6）：ProgressBar——高度分档 4/6/8，背景 borderDefault、填充
// accent；无交互状态。
struct ProgressBarTokens {
    core::Color track{82, 82, 91, 255};          // borderDefault
    core::Color fill{86, 140, 240, 255};         // accent
    float trackHeight[3]{4.0F, 6.0F, 8.0F};

    bool operator==(const ProgressBarTokens&) const = default;
};

// S3（§6.8）：Tabs——平面页签行；选中指示条 + 分隔线 + 两态文字色。
// 子按钮外观由布局期 Tabs 上下文注入（Ghost + 前景覆盖），不在 Gallery
// 手写颜色。
struct TabsTokens {
    core::Color indicator{86, 140, 240, 255};         // accent
    core::Color selectedContent{168, 197, 250, 255};  // accentContent
    core::Color unselectedContent{161, 161, 170, 255};
    core::Color separator{82, 82, 91, 255};           // borderDefault
    float indicatorHeight{2.0F};
    float separatorHeight{1.0F};
    float tabPaddingX{12.0F};
    float tabGap{4.0F};

    bool operator==(const TabsTokens&) const = default;
};

// S4（§6.9）：Tooltip——caption 文本的紧凑提示表面（surfaceElevated +
// borderDefault 轮廓，L2 阴影由 elevation 表达）。
struct TooltipTokens {
    core::Color surface{52, 52, 62, 255};       // surfaceElevated
    core::Color border{82, 82, 91, 255};        // borderDefault
    core::Color content{241, 241, 244, 255};    // contentPrimary
    float paddingX{8.0F};
    float paddingY{6.0F};
    float maxWidth{280.0F};
    float elevation{2.0F};  // §4.5 L2（Dropdown/Tooltip）

    bool operator==(const TooltipTokens&) const = default;
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
    // S3（§7.2）：rest 取 borderStrong 实色（专用 token 传递，painter 不
    // 再对前景乘 alpha）；hovered/dragged 为预留交互（未接线前常显 rest）。
    core::Color rest{161, 161, 170, 255};
    core::Color hovered{161, 161, 170, 255};
    core::Color dragged{161, 161, 170, 255};
    float thickness{8.0F};     // 轨道宽（命中预留）
    float thumbWidth{4.0F};    // 可视 Thumb 宽
    float minLength{24.0F};
    float inset{4.0F};         // 上下内距
    // 圆角 = 可视宽度一半（painter 推导）。

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
    RadioTokens radio{};
    SliderTokens slider{};
    ProgressBarTokens progressBar{};
    TabsTokens tabs{};
    TooltipTokens tooltip{};
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
// 覆盖后所有使用 accent 的组件 token 一致派生，再统一应用 fontScale。
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
[[nodiscard]] RadioTokens radioTokensFrom(const ColorScheme& colors);
[[nodiscard]] SliderTokens sliderTokensFrom(const ColorScheme& colors);
[[nodiscard]] ProgressBarTokens progressBarTokensFrom(
    const ColorScheme& colors);
[[nodiscard]] TabsTokens tabsTokensFrom(const ColorScheme& colors);
[[nodiscard]] TooltipTokens tooltipTokensFrom(const ColorScheme& colors);
[[nodiscard]] DialogTokens dialogTokensFrom(const ColorScheme& colors,
                                            const Metrics& metrics);
[[nodiscard]] ScrollbarTokens scrollbarTokensFrom(const ColorScheme& colors);

// density → 基准档索引（0/1/2）。
[[nodiscard]] std::uint8_t densityBaseIndex(ControlDensity density);

}  // namespace lumen::style
