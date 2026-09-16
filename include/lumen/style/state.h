#pragma once

#include <cstdint>
#include <string>

namespace lumen::style {

// 视觉系统样式状态（docs/lumen-visual-system-design.md §5）。
//
// 交互层提供稳定 identity 对应的状态快照；disabled/invalid/checked/
// selected 属于 Widget 声明。状态按通道合成：disabled 抑制交互；
// invalid 决定错误边框，pressed 覆盖 hover 表面，focus 环独立，
// checked/selected 保留真实选择标记。

struct WidgetState {
    bool hovered{false};
    bool pressed{false};
    bool focused{false};
    bool disabled{false};
    bool checked{false};
    bool invalid{false};
    bool selected{false};

    bool operator==(const WidgetState&) const = default;
};

// 每帧由应用从 InteractionController + FocusManager 汇总的指针/焦点状
// 态；identity 在重建间稳定（layout 分配）。disabled 等声明状态由
// resolver 直接读 Widget。
struct InteractionStateSnapshot {
    std::string hoveredIdentity{};
    std::string pressedIdentity{};
    std::string focusedIdentity{};

    [[nodiscard]] WidgetState stateFor(const std::string& identity,
                                       bool disabled, bool checked,
                                       bool invalid, bool selected) const {
        WidgetState state;
        state.hovered = !identity.empty() && hoveredIdentity == identity;
        state.pressed = !identity.empty() && pressedIdentity == identity;
        state.focused = !identity.empty() && focusedIdentity == identity;
        state.disabled = disabled;
        state.checked = checked;
        state.invalid = invalid;
        state.selected = selected;
        return state;
    }

    bool operator==(const InteractionStateSnapshot&) const = default;
};

}  // namespace lumen::style
