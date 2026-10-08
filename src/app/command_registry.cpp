#include "lumen/app/command_registry.h"

#include <cctype>
#include <utility>

#include "lumen/app/app_shell.h"

namespace lumen::app {

// --- KeyBinding ---

KeyBinding KeyBinding::chord(char letter, core::KeyModifiers modifiers) {
    KeyBinding binding;
    binding.letter = static_cast<char>(
        std::tolower(static_cast<unsigned char>(letter)));
    binding.modifiers = modifiers;
    return binding;
}

KeyBinding KeyBinding::plain(core::Key key, core::KeyModifiers modifiers) {
    KeyBinding binding;
    binding.key = key;
    binding.modifiers = modifiers;
    return binding;
}

bool KeyBinding::matches(core::Key eventKey,
                         core::KeyModifiers eventModifiers,
                         char eventChar) const {
    if (key != core::Key::None) {
        return eventKey == key && eventModifiers == modifiers;
    }
    if (letter == 0 || eventChar == 0) {
        return false;
    }
    const char want =
        static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    const char got = static_cast<char>(
        std::tolower(static_cast<unsigned char>(eventChar)));
    return want == got && eventModifiers == modifiers;
}

namespace {

// 功能键展示名（Key 枚举 → 菜单快捷键列习惯写法）。
std::string keyLabel(core::Key key) {
    switch (key) {
        case core::Key::Backspace:
            return "Backspace";
        case core::Key::Tab:
            return "Tab";
        case core::Key::Enter:
            return "Enter";
        case core::Key::Escape:
            return "Esc";
        case core::Key::Left:
            return "Left";
        case core::Key::Right:
            return "Right";
        case core::Key::Up:
            return "Up";
        case core::Key::Down:
            return "Down";
        case core::Key::Home:
            return "Home";
        case core::Key::End:
            return "End";
        case core::Key::Delete:
            return "Del";
        case core::Key::PageUp:
            return "PageUp";
        case core::Key::PageDown:
            return "PageDown";
        case core::Key::Backtab:
            return "Backtab";
        case core::Key::F10:
            return "F10";
        case core::Key::Alt:
            return "Alt";
        case core::Key::None:
            break;
    }
    return "";
}

}  // namespace

std::string bindingLabel(const KeyBinding& binding) {
    std::string label;
    const auto append = [&label](const char* part) {
        if (!label.empty()) {
            label += '+';
        }
        label += part;
    };
    if ((binding.modifiers & core::kModifierCtrl) != 0) {
        append("Ctrl");
    }
    if ((binding.modifiers & core::kModifierAlt) != 0) {
        append("Alt");
    }
    if ((binding.modifiers & core::kModifierShift) != 0) {
        append("Shift");
    }
    if ((binding.modifiers & core::kModifierGui) != 0) {
        append("Cmd");
    }
    if (binding.key != core::Key::None) {
        append(keyLabel(binding.key).c_str());
        return label;
    }
    if (binding.letter != 0) {
        std::string letter(1, static_cast<char>(
                                  std::toupper(static_cast<unsigned char>(
                                      binding.letter))));
        append(letter.c_str());
    }
    return label;
}

// --- CommandRegistry ---

void CommandRegistry::registerCommand(CommandSpec spec) {
    if (spec.id.empty() || !spec.invoke) {
        return;
    }
    // 同 id 覆盖（先移除旧项，保持注册序语义：新位置即新序）。
    for (auto it = commands_.begin(); it != commands_.end(); ++it) {
        if (it->id == spec.id) {
            commands_.erase(it);
            break;
        }
    }
    // 冲突仲裁：同 scope+domain 内既有绑定与本绑定完全一致 → 首注册者
    // 胜（本项仍登记，match 按注册序取首个命中），冲突入册。
    if (spec.binding.key != core::Key::None || spec.binding.letter != 0) {
        for (const auto& existing : commands_) {
            const bool sameDomain =
                existing.scope == spec.scope &&
                (spec.scope != CommandScope::FocusDomain ||
                 existing.domain == spec.domain);
            if (!sameDomain) {
                continue;
            }
            if (existing.binding.key == spec.binding.key &&
                existing.binding.modifiers == spec.binding.modifiers &&
                existing.binding.letter == spec.binding.letter) {
                conflicts_.push_back(Conflict{
                    bindingLabel(spec.binding), existing.id, spec.id,
                    spec.scope, spec.domain});
                break;  // 只记录首个冲突源（确定性）。
            }
        }
    }
    commands_.push_back(std::move(spec));
}

void CommandRegistry::unregisterCommand(const std::string& id) {
    for (auto it = commands_.begin(); it != commands_.end(); ++it) {
        if (it->id == id) {
            commands_.erase(it);
            return;
        }
    }
}

const CommandSpec* CommandRegistry::find(const std::string& id) const {
    for (const auto& command : commands_) {
        if (command.id == id) {
            return &command;
        }
    }
    return nullptr;
}

bool CommandRegistry::commandEnabled(const std::string& id) const {
    const CommandSpec* spec = find(id);
    return spec != nullptr && (!spec->enabled || spec->enabled());
}

std::string CommandRegistry::bindingLabelFor(const std::string& id) const {
    const CommandSpec* spec = find(id);
    if (spec == nullptr || (spec->binding.key == core::Key::None &&
                            spec->binding.letter == 0)) {
        return {};
    }
    return bindingLabel(spec->binding);
}

bool CommandRegistry::invoke(AppShell& shell, const std::string& id) {
    CommandSpec* spec = nullptr;
    for (auto& command : commands_) {
        if (command.id == id) {
            spec = &command;
            break;
        }
    }
    if (spec == nullptr || (spec->enabled && !spec->enabled())) {
        return false;
    }
    spec->invoke(shell);
    return true;
}

const CommandSpec* CommandRegistry::match(
    core::Key key, core::KeyModifiers modifiers, char keyChar,
    bool modalActive, bool chordPhase,
    const std::function<bool(const CommandSpec&)>& filter) const {
    if (commands_.empty()) {
        return nullptr;
    }
    // 域优先级：模态期仅 Modal；否则 FocusDomain → Window。同域内按注
    // 册序取首个绑定命中（含禁用：域内禁用命中屏蔽同绑定后续注册且不
    // 跨域回退）。filter 拒绝时继续同域扫描（多 FocusDomain 域共用一
    // 个绑定的场景）与更低优先级域。chordPhase 选择相位（仅和弦/仅纯
    // 键——AppShell 两相位分发，设计文档 §4）。
    const CommandScope order[2] = {CommandScope::FocusDomain,
                                   CommandScope::Window};
    const std::size_t scopeCount = modalActive ? 0 : 2;
    for (std::size_t s = 0; s < scopeCount; ++s) {
        for (const auto& command : commands_) {
            if (command.scope != order[s]) {
                continue;
            }
            if (command.binding.isChord() != chordPhase) {
                continue;
            }
            if (!command.binding.matches(key, modifiers, keyChar)) {
                continue;
            }
            if (filter && !filter(command)) {
                continue;
            }
            return &command;
        }
    }
    if (!modalActive) {
        return nullptr;
    }
    for (const auto& command : commands_) {
        if (command.scope != CommandScope::Modal) {
            continue;
        }
        if (command.binding.isChord() != chordPhase) {
            continue;
        }
        if (!command.binding.matches(key, modifiers, keyChar)) {
            continue;
        }
        if (filter && !filter(command)) {
            continue;
        }
        return &command;
    }
    return nullptr;
}

}  // namespace lumen::app
