#pragma once

// G-1（gap-backlog）：窗口内命令分发层。
//
// 框架级命令注册表：命令 id → 回调 + 快捷键绑定 + 生效域。键盘路径、
// 菜单项（MenuItem::command）与工具栏/语义激活共享同一命令模型；菜单
// 显示串从注册表派生（单一数据源，docs/lumen-command-dispatch-design.md）。
//
// 生效域与优先级（同文档 §3）：
//   - Modal：模态打开时唯一可分发的域（对话框内命令）。
//   - FocusDomain：焦点位于指定 FocusScope 域（key 匹配）内时生效，
//     优先于 Window。
//   - Window：无模态时的窗口级兜底。
// 模态（应用声明 setModalActive 或框架模态 overlay）打开时 Window 与
// FocusDomain 命令被屏蔽。同域同绑定冲突：首注册者胜，冲突记录可查询
// （结构化诊断；测试断言确定性）。
//
// 与系统级全局热键（RegisterHotKey/XGrabKey，M16）的关系：本层是同一
// 命令模型的窗口内层；系统级命中后的回灌走 invokeCommand（与键盘同路
// 径），不另建分发模型。
//
// 键匹配输入是归一化事件（core::Key/KeyModifiers/keyChar，windowing.h）；
// 平台无关，公共头不含平台类型（M2 接口约束）。

#include <functional>
#include <string>
#include <vector>

#include "lumen/core/windowing.h"

namespace lumen::app {

class AppShell;

// 快捷键绑定。letter 非 0 表示可打印键和弦/单键（Ctrl+S、纯字母键），
// 匹配 keyChar（大小写不敏感）；key != None 表示功能键（Esc/Enter/方向
// 键等）。两者互斥；都空 = 无绑定（命令只能经 invokeCommand 触发）。
struct KeyBinding {
    core::Key key{core::Key::None};
    core::KeyModifiers modifiers{core::kModifierNone};
    char letter{0};

    // Ctrl/Gui/Alt 和弦（如 Ctrl+S）。和弦在文本字段聚焦时仍分发（除
    // 内建编辑和弦，见 AppShell::keyDown）；纯键让位给字段/树处理。
    [[nodiscard]] static KeyBinding chord(char letter,
                                          core::KeyModifiers modifiers);
    // 功能键绑定（Esc/Enter/方向键等；可带修饰键）。
    [[nodiscard]] static KeyBinding plain(core::Key key,
                                          core::KeyModifiers modifiers =
                                              core::kModifierNone);

    [[nodiscard]] bool isChord() const {
        return (modifiers & (core::kModifierCtrl | core::kModifierAlt |
                             core::kModifierGui)) != 0;
    }
    // 是否匹配归一化按键事件。
    [[nodiscard]] bool matches(core::Key eventKey,
                               core::KeyModifiers eventModifiers,
                               char eventChar) const;
};

// 展示串（"Ctrl+S"/"Shift+Ctrl+Z"/"Esc"/"Alt+F4" 语义）。菜单快捷键列
// 从这里派生，避免显示串与实际绑定分叉。修饰键顺序 Ctrl/Alt/Shift/Cmd
// （Gui 显示为 Cmd）；字母大写。
[[nodiscard]] std::string bindingLabel(const KeyBinding& binding);

// 命令生效域。
enum class CommandScope : std::uint8_t {
    Window,       // 无模态时窗口级兜底
    FocusDomain,  // 焦点位于 domain 指定的 FocusScope 域内（优先于 Window）
    Modal,        // 仅模态打开时（对话框内命令）
};

// 命令模型：id 稳定；invoke 为动作体（UI 线程）；enabled 为动态启用
// 查询（空 = 恒启用）。禁用命令不分发、菜单行派生为禁用。
struct CommandSpec {
    std::string id{};
    std::string label{};  // 人读名（诊断/工具栏提示；可空）
    KeyBinding binding{};
    CommandScope scope{CommandScope::Window};
    std::string domain{};  // FocusDomain 时的 FocusScope key
    std::function<void(AppShell&)> invoke{};
    std::function<bool()> enabled{};  // 空 = 恒启用
};

class CommandRegistry {
  public:
    // 注册：同 id 覆盖旧项。同域同绑定冲突（同 scope+domain 的重复绑
    // 定）：首注册者胜出，冲突进结构化记录（conflicts()）。
    void registerCommand(CommandSpec spec);
    void unregisterCommand(const std::string& id);

    [[nodiscard]] const CommandSpec* find(const std::string& id) const;
    [[nodiscard]] bool commandEnabled(const std::string& id) const;
    // 绑定展示串（菜单派生用）；无绑定/未知 id 返回空串。
    [[nodiscard]] std::string bindingLabelFor(const std::string& id) const;
    // 显式调用（菜单/工具栏/语义/系统热键回灌与键盘同路径）：未知或
    // 禁用返回 false。
    bool invoke(AppShell& shell, const std::string& id);

    // 冲突记录（注册期收集；确定性：按发生顺序）。
    struct Conflict {
        std::string binding{};
        std::string firstId{};
        std::string secondId{};
        CommandScope scope{CommandScope::Window};
        std::string domain{};
    };
    [[nodiscard]] const std::vector<Conflict>& conflicts() const {
        return conflicts_;
    }

    // 键盘分发匹配（AppShell::keyDown 调用）：返回命中的命令，按域优先
    // 级（FocusDomain > Window；模态期仅 Modal）。同域内按注册序取首个
    // 绑定命中（含禁用——禁用命令在本域内屏蔽同绑定的后续注册，且不
    // 跨域回退；冲突已在注册期记录）。chordPhase 选择相位：true 仅和
    // 弦绑定（Ctrl/Alt/Gui），false 仅纯键绑定（分发相位见设计文档 §4）。
    // filter 拒绝（焦点不在域内/字段保护）时继续扫描同域其他命令与更
    // 低优先级域。
    const CommandSpec* match(
        core::Key key, core::KeyModifiers modifiers, char keyChar,
        bool modalActive, bool chordPhase,
        const std::function<bool(const CommandSpec&)>& filter) const;

    [[nodiscard]] bool empty() const { return commands_.empty(); }

  private:
    // 注册序保存（冲突仲裁与匹配顺序依赖注册序）。
    std::vector<CommandSpec> commands_{};
    std::vector<Conflict> conflicts_{};
};

}  // namespace lumen::app
