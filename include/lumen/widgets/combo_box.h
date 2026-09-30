#pragma once

// G-4（gap-backlog）：可编辑 ComboBox——TextField + 复用菜单 overlay。
//
// 值 = TextField 自由文本（bind）；展开 = ContextMenuController 的锚定
// 面板（barrier/键盘/语义/滚动全复用，menu-controls-design §6）；输入
// 即过滤（大小写不敏感子串，label/value 都参与）；Down/Alt+Down 展开、
// Escape 关闭、Enter 选高亮（菜单内建键盘）。选中写回 bind 并回调
// onSelected；文本不在选项内 = 自由值（不强制匹配——工具应用常见）。
//
// 应用装配（一次）：attach(shell)（toggle handler + bind 观察 + 菜单
// 回调）；build(theme) 进主模板；onKey 转发 handleKey（打开时 modal
// 优先，Down 展开在字段聚焦时消费）。视觉：字段/按钮全部既有 token
//（docs/lumen-combo-box-design.md）。

#include <functional>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/style/theme.h"
#include "lumen/widgets/menu.h"

namespace lumen::widgets {

class ComboBoxController {
  public:
    struct Option {
        std::string value{};
        std::string label{};
    };

    ComboBoxController(std::vector<Option> options, std::string bind,
                       std::string key = "combo");

    // 值行（TextField + 展开按钮；展开按钮 onClick = <key>:toggle）。
    [[nodiscard]] core::Widget build(const style::Theme& theme) const;
    // 装配：toggle handler、bind 变更观察（打开期输入即过滤）、菜单回调。
    void attach(app::AppShell& shell);

    void open(app::AppShell& shell, const style::Theme* anchorTheme = nullptr,
              bool fallbackAll = false);
    void close(app::AppShell& shell);
    [[nodiscard]] bool isOpen() const { return menu_.isOpen(); }

    // 键盘：菜单打开 → 菜单内建导航；字段聚焦且未开 → Down/Alt+Down
    // 展开（消费）；其余 false 交回调用方。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers modifiers = core::kModifierNone,
                   char keyChar = 0);

    // 选中回调（UI 线程；参数 = option value）。
    std::function<void(const std::string&)> onSelected{};
    // 当前文本（bind 值）。
    [[nodiscard]] std::string text(const app::AppShell& shell) const;

  private:
    [[nodiscard]] std::string toggleHandler() const {
        return key_ + ":toggle";
    }
    [[nodiscard]] std::string fieldKey() const { return key_ + ":field"; }
    [[nodiscard]] MenuItems filteredItems(const app::AppShell& shell) const;

    std::vector<Option> options_{};
    std::string bind_{};
    std::string key_{};
    ContextMenuController menu_{};
    core::StateStore::ObserverId observer_{0};
    app::AppShell* shell_{nullptr};
};

}  // namespace lumen::widgets
