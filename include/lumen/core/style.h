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
    // 尺寸，painter 在控件外扩绘制）。
    float focusWidth{0.0F};
    float elevation{0.0F};

    bool operator==(const CommonResolvedStyle&) const = default;
};

struct ButtonResolvedStyle {
    CommonResolvedStyle common{};
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
    Color indicator{Color::transparent()};
    Color indicatorChecked{Color::transparent()};
    Color mark{Color::transparent()};
    float indicatorSize{18.0F};
    float indicatorRadius{4.0F};
    float markInset{4.0F};
    float markRadius{2.0F};
    float labelGap{8.0F};
    bool checked{false};
    bool operator==(const CheckboxResolvedStyle&) const = default;
};

struct SwitchResolvedStyle {
    CommonResolvedStyle common{};
    Color trackOff{Color::transparent()};
    Color trackOn{Color::transparent()};
    Color knob{Color::transparent()};
    float trackWidth{36.0F};
    float trackHeight{20.0F};
    float knobSize{14.0F};
    float knobInset{3.0F};
    float labelGap{8.0F};
    bool checked{false};
    bool operator==(const SwitchResolvedStyle&) const = default;
};

using ComponentResolvedStyle = std::variant<CommonResolvedStyle,
                                            ButtonResolvedStyle,
                                            TextFieldResolvedStyle,
                                            CheckboxResolvedStyle,
                                            SwitchResolvedStyle>;

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
                    part.indicatorChecked =
                        scaleColorAlpha(part.indicatorChecked, alpha);
                    part.mark = scaleColorAlpha(part.mark, alpha);
                } else if constexpr (std::is_same_v<Part,
                                                   SwitchResolvedStyle>) {
                    part.trackOff = scaleColorAlpha(part.trackOff, alpha);
                    part.trackOn = scaleColorAlpha(part.trackOn, alpha);
                    part.knob = scaleColorAlpha(part.knob, alpha);
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
                    toPart.indicatorChecked =
                        lerpColor(fromPart.indicatorChecked,
                                  toPart.indicatorChecked, t);
                    toPart.mark = lerpColor(fromPart.mark, toPart.mark, t);
                } else if constexpr (std::is_same_v<To,
                                                    SwitchResolvedStyle>) {
                    toPart.trackOff =
                        lerpColor(fromPart.trackOff, toPart.trackOff, t);
                    toPart.trackOn =
                        lerpColor(fromPart.trackOn, toPart.trackOn, t);
                    toPart.knob = lerpColor(fromPart.knob, toPart.knob, t);
                }
            }
            (void)result;
        },
        from.component, result.component);
    return result;
}

}  // namespace lumen::core
