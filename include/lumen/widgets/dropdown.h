#pragma once

#include <functional>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/style/theme.h"

namespace lumen::widgets {

// M11（自用路线图）：Dropdown 浮动菜单控制器。
//
// 应用持有控制器与值行 Widget（makeDropdown 收起叶子，onClick 经
// openHandler 打开）；open 在框架级 overlay 上构建全窗 barrier + 锚定
// 菜单（边界钳制，下方不足翻向上方）；键盘 Up/Down 移动高亮、Enter
// 选中、Esc/barrier 点击关闭（handleKey 由应用 onKey 以 modal 优先级
// 接线，先于 Navigator 返回规则）；选中经 onSelected 回调通知，值行
// 焦点恢复。选项按钮语义可聚焦/可激活；expanded flag 不进语义契约
//（M5 冻结，见 roadmap M11 已知限制）。
class DropdownController {
  public:
    struct Option {
        std::string value{};
        std::string label{};
    };

    DropdownController(std::vector<Option> options,
                       std::string defaultValue = {});

    // 打开菜单：dropdownKey 定位主树值行（锚定其下方），并注册选项/
    // dismiss handler 与打开时高亮（当前值或首项）。
    void open(app::AppShell& shell, const std::string& dropdownKey);
    void close(app::AppShell& shell);
    [[nodiscard]] bool isOpen() const { return open_; }
    [[nodiscard]] const std::string& value() const { return value_; }
    void setValue(std::string value);
    // 键盘导航（Up/Down/Enter/Escape；仅打开时消费，其余返回 false）。
    bool handleKey(app::AppShell& shell, core::Key key);
    // 选中回调（UI 线程；参数 = option value；关闭菜单后触发）。
    std::function<void(const std::string&)> onSelected{};

  private:
    [[nodiscard]] std::string optionKey(std::size_t index) const;
    void registerHandlers(app::AppShell& shell);
    void refreshOverlay(app::AppShell& shell);
    void select(app::AppShell& shell, std::size_t index);
    [[nodiscard]] core::Widget buildOverlay(const style::Theme& theme,
                                            core::Size view) const;

    std::vector<Option> options_{};
    std::string value_{};
    std::size_t highlight_{0};
    bool open_{false};
    std::string dropdownKey_{};
    core::Rect anchor_{};
};

}  // namespace lumen::widgets
