#pragma once

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

}  // namespace lumen::core
