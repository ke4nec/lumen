// G-4：可编辑 ComboBox 实现（契约见 combo_box.h 与
// docs/lumen-combo-box-design.md）。overlay 全复用 ContextMenuController
//（barrier/键盘/语义/滚动/焦点恢复）；零新增 WidgetType/RenderCommand。

#include "lumen/widgets/combo_box.h"

#include <cctype>

#include "lumen/core/render_node.h"
#include "lumen/core/state.h"

namespace lumen::widgets {

namespace {

bool containsIgnoreCase(const std::string& haystack,
                        const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.size() < needle.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

}  // namespace

ComboBoxController::ComboBoxController(std::vector<Option> options,
                                       std::string bind, std::string key)
    : options_(std::move(options)),
      bind_(std::move(bind)),
      key_(std::move(key)) {}

std::string ComboBoxController::text(const app::AppShell& shell) const {
    return shell.state().get(bind_);
}

MenuItems ComboBoxController::filteredItems(
    const app::AppShell& shell) const {
    const std::string query = text(shell);
    MenuItems items;
    for (const auto& option : options_) {
        if (containsIgnoreCase(option.label, query) ||
            containsIgnoreCase(option.value, query)) {
            // id = option value（onCommand 原样回传）。
            items.push_back({.id = option.value, .label = option.label});
        }
    }
    return items;
}

core::Widget ComboBoxController::build(const style::Theme& theme) const {
    // 行 = 输入字段（flex）+ 展开按钮（固定窄宽）。字段自由编辑（过滤
    // 在打开期经 bind 观察驱动）；按钮唤起（点击/键盘同路径）。
    core::Widget field = core::makeTextField(
        /*value=*/"", "type or pick…", theme.typography.body,
        core::EdgeInsets{}, /*flex=*/1.0F, fieldKey(), std::nullopt,
        std::nullopt, bind_);
    core::Widget toggle = core::withVariant(
        core::makeButton("▾", theme.typography.label, core::EdgeInsets{},
                         0.0F, key_ + ":toggle", 28.0F, std::nullopt,
                         toggleHandler()),
        core::ButtonVariant::Ghost);
    core::Widget row = core::makeRow(
        {std::move(field), std::move(toggle)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center);
    row.key = key_;
    return row;
}

void ComboBoxController::attach(app::AppShell& shell) {
    shell_ = &shell;
    shell.handlers()[toggleHandler()] = [this, &shell] {
        if (menu_.isOpen()) {
            close(shell);
        } else {
            // 显式唤起（点 ▾ / 键盘）：空过滤回退全量——过滤只在输入
            // 期生效（自由值也能看到全部选项）。
            open(shell, /*anchorTheme=*/nullptr, /*fallbackAll=*/true);
        }
    };
    menu_.onCommand = [this, &shell](const std::string& value) {
        shell.state().set(bind_, value);
        if (onSelected) {
            onSelected(value);
        }
    };
    // 打开期输入即过滤：bind 变化 → 重开面板（同锚；复开语义清旧层，
    // 高亮重置到过滤集首项）。空过滤 = 关闭（无匹配项可挑）。关闭期
    // 不观察输入（自由编辑不打扰）。
    observer_ = shell.state().subscribe(bind_, [this, &shell] {
        if (menu_.isOpen()) {
            open(shell);
        }
    });
}

void ComboBoxController::open(app::AppShell& shell,
                              const style::Theme* anchorTheme,
                              bool fallbackAll) {
    MenuItems items = filteredItems(shell);
    if (items.empty() && fallbackAll) {
        for (const auto& option : options_) {
            items.push_back({.id = option.value, .label = option.label});
        }
    }
    if (items.empty()) {
        // 输入期无匹配：不弹空窗口；已开则关闭。
        close(shell);
        return;
    }
    // 锚 = 值行矩形（主树内即视口坐标；面板在下方、不足翻上——
    // menu-controls-design §7.1 同语义）。
    core::Rect anchor{};
    if (const core::RenderNode* row =
            core::findNodeByKey(shell.root(), key_)) {
        anchor = core::Rect{core::absoluteOffset(shell.root(), row->key),
                            row->size};
    }
    menu_.openAnchored(shell, anchor, std::move(items), /*submenu=*/{},
                       anchorTheme, key_ + "-menu");
}

void ComboBoxController::close(app::AppShell& shell) {
    menu_.close(shell);
}

bool ComboBoxController::handleKey(app::AppShell& shell, core::Key key,
                                    core::KeyModifiers modifiers,
                                    char keyChar) {
    if (menu_.isOpen()) {
        return menu_.handleKey(shell, key, modifiers, keyChar);
    }
    // 字段聚焦且未开：Down / Alt+Down 展开（输入位置保留；与头文件
    // 及设计文档声明对齐——review L-1）。
    if (key == core::Key::Down &&
        (modifiers & (core::kModifierCtrl | core::kModifierGui)) == 0 &&
        shell.controller().focusedBind() == bind_) {
        open(shell, /*anchorTheme=*/nullptr, /*fallbackAll=*/true);
        return true;
    }
    return false;
}

}  // namespace lumen::widgets
