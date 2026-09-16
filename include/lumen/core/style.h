#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

#include "lumen/core/geometry.h"

namespace lumen::core {

// 视觉系统（docs/lumen-visual-system-design.md §6.2）：StyleResolver 产出
// 的最终样式值类型。只保存值——不持有 Theme、SDL、Skia 或平台对象指针
//（§11 迁移约束）。lumen-core 保存它以便 RenderNode/layout/painter 共
// 用，而 Theme/token/解析器在 lumen-style。
//
// 布局消费 minWidth/minHeight/padding/text 与组件部件尺寸；painter 消费
// 颜色、圆角、边框、焦点环与部件几何。状态（hover/pressed/focused/
// disabled/checked/invalid）在解析时已折算进这些值；caret、selection、
// composition 等编辑瞬态仍走 PaintOptions。

// 所有控件共享的最终视觉值。
struct CommonResolvedStyle {
    Color background{Color::transparent()};
    Color foreground{Color::transparent()};
    Color border{Color::transparent()};
    Color focusRing{Color::transparent()};
    Color selection{Color::transparent()};
    CornerRadius radius{CornerRadius::zero()};
    // 控件 chrome 内边距（参与 intrinsic 度量；区别于 Widget.padding 的
    // 盒模型内边距）。
    EdgeInsets padding{};
    TextStyle text{};
    float borderWidth{0.0F};
    // 焦点环宽度；>0 表示该节点在本帧携带可见键盘焦点环（不影响布局
    // 尺寸，painter 在节点内描边绘制）。
    float focusWidth{0.0F};
    // 焦点隔离带（§6.1）：不透明填充与焦点环色接近时，环内侧的 1 px 表
    // 面色环带（透明 = 无隔离带）。不影响布局尺寸。
    Color focusIsolation{Color::transparent()};
    float elevation{0.0F};

    bool operator==(const CommonResolvedStyle&) const = default;
};

struct ButtonResolvedStyle {
    CommonResolvedStyle common{};
    // 尾随图标部件（§6.1）：尺寸取 metrics.inlineIconSize 档位、间距取
    // 档位 gap、线宽按 16px→1.5 基准比例缩放（§4.5）。IconId::None 时
    // 不参与测量与绘制。
    float iconSize{16.0F};
    float iconGap{8.0F};
    float iconStroke{1.5F};
    bool operator==(const ButtonResolvedStyle&) const = default;
};

struct TextFieldResolvedStyle {
    CommonResolvedStyle common{};
    Color placeholder{Color::transparent()};
    Color caret{Color::transparent()};
    Color preeditUnderline{Color::transparent()};
    // 焦点状态已折算：focused 控制光标/选区绘制开关。
    bool focused{false};
    bool readOnly{false};
    bool multiline{false};
    bool obscure{false};
    bool operator==(const TextFieldResolvedStyle&) const = default;
};

struct CheckboxResolvedStyle {
    CommonResolvedStyle common{};
    // Off：indicator（surfaceSunken 内部）+ indicatorOutline（轮廓描边）；
    // On：indicatorChecked 填充 + mark 勾号（IconId::Check）。
    Color indicator{Color::transparent()};
    Color indicatorOutline{Color::transparent()};
    Color indicatorChecked{Color::transparent()};
    Color mark{Color::transparent()};
    float indicatorSize{18.0F};
    float indicatorRadius{4.0F};
    float markInset{4.0F};
    float labelGap{8.0F};
    // 指示器槽位（§4.4）：indicatorSize + 2 × (focusRingWidth + 1px 隔离带)；
    // 是否聚焦不改变槽位与标签起点。
    float slotSize{0.0F};
    bool checked{false};
    bool operator==(const CheckboxResolvedStyle&) const = default;
};

struct SwitchResolvedStyle {
    CommonResolvedStyle common{};
    Color trackOff{Color::transparent()};
    Color trackOutline{Color::transparent()};
    Color trackOn{Color::transparent()};
    Color knobOff{Color::transparent()};
    Color knobOn{Color::transparent()};
    float trackWidth{36.0F};
    float trackHeight{20.0F};
    float knobSize{14.0F};
    // 由 (trackHeight - knobSize) / 2 推导（§6.4），随档位变化。
    float knobInset{3.0F};
    float labelGap{8.0F};
    // 轨道槽位（§4.4）：trackWidth + 2 × (focusRingWidth + 1px 隔离带)。
    float slotSize{0.0F};
    bool checked{false};
    bool operator==(const SwitchResolvedStyle&) const = default;
};

// S2（§6.4）：Radio——空心外环 + 独立内点；dotRatio 为内点/外径比。
struct RadioResolvedStyle {
    CommonResolvedStyle common{};
    Color indicator{Color::transparent()};
    Color indicatorOutline{Color::transparent()};
    Color indicatorChecked{Color::transparent()};
    Color dot{Color::transparent()};
    float indicatorSize{18.0F};
    float dotRatio{0.45F};
    float labelGap{8.0F};
    float slotSize{0.0F};
    bool checked{false};
    bool operator==(const RadioResolvedStyle&) const = default;
};

// S3（§6.5）：Slider——细轨道 + 独立 Thumb；端点恒定预留
// trackInset = thumb 半径 + 焦点保护宽（focusRingWidth+1），值/指针/
// 绘制共用同一轨道区间（interaction 从本样式读取）。
struct SliderResolvedStyle {
    CommonResolvedStyle common{};
    Color trackRemaining{Color::transparent()};
    Color trackActive{Color::transparent()};
    Color thumbFill{Color::transparent()};
    Color thumbOutline{Color::transparent()};
    float trackHeight{4.0F};
    float thumbDiameter{18.0F};
    float thumbBorderWidth{2.0F};
    float trackInset{12.0F};
    bool operator==(const SliderResolvedStyle&) const = default;
};

// S3（§6.6）：ProgressBar——无交互状态；高度分档。
struct ProgressBarResolvedStyle {
    CommonResolvedStyle common{};
    Color track{Color::transparent()};
    Color fill{Color::transparent()};
    float trackHeight{6.0F};
    bool operator==(const ProgressBarResolvedStyle&) const = default;
};

// S3（§6.8）：Tabs——页签行 chrome（分隔线 + 选中指示条）与两态文字
// 色；子按钮外观由布局期注入（Ghost + 前景覆盖），本样式只承载行级
// 部件。
struct TabsResolvedStyle {
    CommonResolvedStyle common{};
    Color indicator{Color::transparent()};
    Color separator{Color::transparent()};
    Color selectedContent{Color::transparent()};
    Color unselectedContent{Color::transparent()};
    float indicatorHeight{2.0F};
    float separatorHeight{1.0F};
    bool operator==(const TabsResolvedStyle&) const = default;
};

using ComponentResolvedStyle = std::variant<CommonResolvedStyle,
                                            ButtonResolvedStyle,
                                            TextFieldResolvedStyle,
                                            CheckboxResolvedStyle,
                                            SwitchResolvedStyle,
                                            RadioResolvedStyle,
                                            SliderResolvedStyle,
                                            ProgressBarResolvedStyle,
                                            TabsResolvedStyle>;

struct ResolvedStyle {
    ComponentResolvedStyle component{CommonResolvedStyle{}};
    // 度量：布局的最小尺寸与控件间距（Theme metrics 派生）。
    float minWidth{0.0F};
    float minHeight{0.0F};
    float controlGap{0.0F};

    bool operator==(const ResolvedStyle&) const = default;
};

// variant 的公共段访问器（每个组件样式都内嵌 common；variant 本身也可
// 直接持有 CommonResolvedStyle——容器/文本节点没有组件专有部件）。
[[nodiscard]] inline const CommonResolvedStyle& commonStyle(
    const ComponentResolvedStyle& component) {
    return std::visit(
        [](const auto& part) -> const CommonResolvedStyle& {
            using Part = std::decay_t<decltype(part)>;
            if constexpr (std::is_same_v<Part, CommonResolvedStyle>) {
                return part;
            } else {
                return part.common;
            }
        },
        component);
}

[[nodiscard]] inline CommonResolvedStyle& commonStyle(
    ComponentResolvedStyle& component) {
    return std::visit(
        [](auto& part) -> CommonResolvedStyle& {
            using Part = std::decay_t<decltype(part)>;
            if constexpr (std::is_same_v<Part, CommonResolvedStyle>) {
                return part;
            } else {
                return part.common;
            }
        },
        component);
}

// 便捷访问：节点级（render_node.h 使用）。
[[nodiscard]] inline const CommonResolvedStyle& commonStyle(
    const ResolvedStyle& style) {
    return commonStyle(style.component);
}

// --- M10：转场/状态过渡的颜色工具（只改绘制数据，不动布局几何） ---

// 透明度缩放：alpha 夹取 [0,1] 后乘进颜色 alpha 通道。
[[nodiscard]] inline Color scaleColorAlpha(const Color& color, float alpha) {
    const float scaled =
        static_cast<float>(color.a) * std::clamp(alpha, 0.0F, 1.0F);
    return Color{color.r, color.g, color.b,
                 static_cast<std::uint8_t>(std::lround(scaled))};
}

// 逐通道 RGBA 插值（t 夹取 [0,1]）。
[[nodiscard]] inline Color lerpColor(const Color& from, const Color& to,
                                     float t) {
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    const auto channel = [clamped](std::uint8_t a, std::uint8_t b) {
        const float value = static_cast<float>(a) +
                            (static_cast<float>(b) - static_cast<float>(a)) *
                                clamped;
        return static_cast<std::uint8_t>(std::lround(value));
    };
    return Color{channel(from.r, to.r), channel(from.g, to.g),
                 channel(from.b, to.b), channel(from.a, to.a)};
}

inline void scaleCommonStyleColors(CommonResolvedStyle& common, float alpha) {
    common.background = scaleColorAlpha(common.background, alpha);
    common.foreground = scaleColorAlpha(common.foreground, alpha);
    common.border = scaleColorAlpha(common.border, alpha);
    common.focusRing = scaleColorAlpha(common.focusRing, alpha);
    common.focusIsolation = scaleColorAlpha(common.focusIsolation, alpha);
    common.selection = scaleColorAlpha(common.selection, alpha);
    common.text.color = scaleColorAlpha(common.text.color, alpha);
}

// 整节点透明度：公共段 + 组件专有色（Button 只有公共段）。
inline void scaleStyleColors(ResolvedStyle& style, float alpha) {
    std::visit(
        [alpha](auto& part) {
            using Part = std::decay_t<decltype(part)>;
            if constexpr (std::is_same_v<Part, CommonResolvedStyle>) {
                scaleCommonStyleColors(part, alpha);
            } else {
                scaleCommonStyleColors(part.common, alpha);
                if constexpr (std::is_same_v<Part, TextFieldResolvedStyle>) {
                    part.placeholder =
                        scaleColorAlpha(part.placeholder, alpha);
                    part.caret = scaleColorAlpha(part.caret, alpha);
                    part.preeditUnderline =
                        scaleColorAlpha(part.preeditUnderline, alpha);
                } else if constexpr (std::is_same_v<Part,
                                                  CheckboxResolvedStyle>) {
                    part.indicator = scaleColorAlpha(part.indicator, alpha);
                    part.indicatorOutline =
                        scaleColorAlpha(part.indicatorOutline, alpha);
                    part.indicatorChecked =
                        scaleColorAlpha(part.indicatorChecked, alpha);
                    part.mark = scaleColorAlpha(part.mark, alpha);
                } else if constexpr (std::is_same_v<Part,
                                                   SwitchResolvedStyle>) {
                    part.trackOff = scaleColorAlpha(part.trackOff, alpha);
                    part.trackOutline =
                        scaleColorAlpha(part.trackOutline, alpha);
                    part.trackOn = scaleColorAlpha(part.trackOn, alpha);
                    part.knobOff = scaleColorAlpha(part.knobOff, alpha);
                    part.knobOn = scaleColorAlpha(part.knobOn, alpha);
                } else if constexpr (std::is_same_v<Part,
                                                    RadioResolvedStyle>) {
                    part.indicator = scaleColorAlpha(part.indicator, alpha);
                    part.indicatorOutline =
                        scaleColorAlpha(part.indicatorOutline, alpha);
                    part.indicatorChecked =
                        scaleColorAlpha(part.indicatorChecked, alpha);
                    part.dot = scaleColorAlpha(part.dot, alpha);
                } else if constexpr (std::is_same_v<Part,
                                                    SliderResolvedStyle>) {
                    part.trackRemaining =
                        scaleColorAlpha(part.trackRemaining, alpha);
                    part.trackActive =
                        scaleColorAlpha(part.trackActive, alpha);
                    part.thumbFill = scaleColorAlpha(part.thumbFill, alpha);
                    part.thumbOutline =
                        scaleColorAlpha(part.thumbOutline, alpha);
                } else if constexpr (std::is_same_v<
                                         Part, ProgressBarResolvedStyle>) {
                    part.track = scaleColorAlpha(part.track, alpha);
                    part.fill = scaleColorAlpha(part.fill, alpha);
                } else if constexpr (std::is_same_v<Part,
                                                    TabsResolvedStyle>) {
                    part.indicator = scaleColorAlpha(part.indicator, alpha);
                    part.separator = scaleColorAlpha(part.separator, alpha);
                }
            }
        },
        style.component);
}

inline void lerpCommonStyleColors(CommonResolvedStyle& into,
                                  const CommonResolvedStyle& from,
                                  const CommonResolvedStyle& to, float t) {
    into.background = lerpColor(from.background, to.background, t);
    into.foreground = lerpColor(from.foreground, to.foreground, t);
    into.border = lerpColor(from.border, to.border, t);
    into.focusRing = lerpColor(from.focusRing, to.focusRing, t);
    into.focusIsolation = lerpColor(from.focusIsolation, to.focusIsolation, t);
    into.selection = lerpColor(from.selection, to.selection, t);
    into.text.color = lerpColor(from.text.color, to.text.color, t);
}

// 状态色过渡：以 `to` 为基线（度量/标志取终态），只插值颜色通道。
// 组件类型变化（节点复用切控件）不插值，直接取终态。
[[nodiscard]] inline ResolvedStyle lerpStyleColors(const ResolvedStyle& from,
                                                   const ResolvedStyle& to,
                                                   float t) {
    ResolvedStyle result = to;
    if (from.component.index() != to.component.index()) {
        return result;
    }
    std::visit(
        [t, &result](const auto& fromPart, auto& toPart) {
            using From = std::decay_t<decltype(fromPart)>;
            using To = std::decay_t<decltype(toPart)>;
            if constexpr (!std::is_same_v<From, To>) {
                return;
            } else if constexpr (std::is_same_v<To, CommonResolvedStyle>) {
                lerpCommonStyleColors(toPart, fromPart, toPart, t);
            } else {
                lerpCommonStyleColors(toPart.common, fromPart.common,
                                      toPart.common, t);
                if constexpr (std::is_same_v<To, TextFieldResolvedStyle>) {
                    toPart.placeholder = lerpColor(fromPart.placeholder,
                                                   toPart.placeholder, t);
                    toPart.caret =
                        lerpColor(fromPart.caret, toPart.caret, t);
                    toPart.preeditUnderline =
                        lerpColor(fromPart.preeditUnderline,
                                  toPart.preeditUnderline, t);
                } else if constexpr (std::is_same_v<To,
                                                   CheckboxResolvedStyle>) {
                    toPart.indicator =
                        lerpColor(fromPart.indicator, toPart.indicator, t);
                    toPart.indicatorOutline =
                        lerpColor(fromPart.indicatorOutline,
                                  toPart.indicatorOutline, t);
                    toPart.indicatorChecked =
                        lerpColor(fromPart.indicatorChecked,
                                  toPart.indicatorChecked, t);
                    toPart.mark = lerpColor(fromPart.mark, toPart.mark, t);
                } else if constexpr (std::is_same_v<To,
                                                    SwitchResolvedStyle>) {
                    toPart.trackOff =
                        lerpColor(fromPart.trackOff, toPart.trackOff, t);
                    toPart.trackOutline =
                        lerpColor(fromPart.trackOutline, toPart.trackOutline, t);
                    toPart.trackOn =
                        lerpColor(fromPart.trackOn, toPart.trackOn, t);
                    toPart.knobOff =
                        lerpColor(fromPart.knobOff, toPart.knobOff, t);
                    toPart.knobOn =
                        lerpColor(fromPart.knobOn, toPart.knobOn, t);
                } else if constexpr (std::is_same_v<To,
                                                    RadioResolvedStyle>) {
                    toPart.indicator =
                        lerpColor(fromPart.indicator, toPart.indicator, t);
                    toPart.indicatorOutline =
                        lerpColor(fromPart.indicatorOutline,
                                  toPart.indicatorOutline, t);
                    toPart.indicatorChecked =
                        lerpColor(fromPart.indicatorChecked,
                                  toPart.indicatorChecked, t);
                    toPart.dot = lerpColor(fromPart.dot, toPart.dot, t);
                } else if constexpr (std::is_same_v<To,
                                                    SliderResolvedStyle>) {
                    toPart.trackRemaining =
                        lerpColor(fromPart.trackRemaining,
                                  toPart.trackRemaining, t);
                    toPart.trackActive =
                        lerpColor(fromPart.trackActive, toPart.trackActive, t);
                    toPart.thumbFill =
                        lerpColor(fromPart.thumbFill, toPart.thumbFill, t);
                    toPart.thumbOutline =
                        lerpColor(fromPart.thumbOutline, toPart.thumbOutline, t);
                } else if constexpr (std::is_same_v<
                                         To, ProgressBarResolvedStyle>) {
                    toPart.track = lerpColor(fromPart.track, toPart.track, t);
                    toPart.fill = lerpColor(fromPart.fill, toPart.fill, t);
                } else if constexpr (std::is_same_v<To,
                                                    TabsResolvedStyle>) {
                    toPart.indicator =
                        lerpColor(fromPart.indicator, toPart.indicator, t);
                    toPart.separator =
                        lerpColor(fromPart.separator, toPart.separator, t);
                }
            }
            (void)result;
        },
        from.component, result.component);
    return result;
}

}  // namespace lumen::core
