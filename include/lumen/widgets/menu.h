#pragma once

// 菜单类控件（docs/lumen-menu-controls-design.md）：ContextMenu 与
// MenuBar 共享一套菜单面板——M11 框架级 overlay + 全窗 barrier +
// collectionRow 行（hover/焦点/语义激活复用集合行路径）。
// 零新增 WidgetType / RenderCommand。
//
// 快捷键文本只展示、不执行（分发在应用 onKey；框架不建全局加速键表）。
// checkable 项的 checked 由应用维护（点击 → onCommand → 应用翻转 →
// 应用按需重开菜单）。子菜单懒构建（hasSubmenu + SubmenuProvider）。

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/geometry.h"
#include "lumen/core/icon_id.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/style/theme.h"

namespace lumen::widgets {

// 菜单项模型（menu-controls-design §5）：命令的值类型描述。
struct MenuItem {
    std::string id{};          // 稳定标识：行 key 与 onCommand 参数
    std::string label{};       // 显示文本（separator 项忽略）
    core::IconId icon{core::IconId::None};  // 左侧图标槽（None = 空槽对齐）
    bool separator{false};     // 分隔线行（不可聚焦，不进语义树语义）
    bool checkable{false};     // 勾选槽显示；checked 状态应用维护
    bool checked{false};
    std::string shortcut{};    // 展示文本（"Ctrl+S"）；仅显示，不执行
    bool hasSubmenu{false};    // true 时 submenu 回调提供子级（懒构建）
    bool enabled{true};
    char mnemonic{0};          // Alt+keyChar 助记字母（0 = 无）
};

using MenuItems = std::vector<MenuItem>;
using SubmenuProvider = std::function<MenuItems(const std::string& id)>;

// 上下文菜单控制器：open 在指针位置唤起（SecondaryPressSink 接线由应用
// 或集合控件便捷层完成）；openAnchored 供 MenuBar 等锚定唤起。
class ContextMenuController {
  public:
    // 在指针位置（逻辑坐标）打开。focus 恢复目标默认 = 唤起前焦点。
    void open(app::AppShell& shell, core::Offset position, MenuItems items,
              SubmenuProvider submenu = {},
              const style::Theme* anchorTheme = nullptr);
    // 锚定唤起（anchor.size 非零 = 栏项等锚矩形：菜单在其下方、不足翻
    // 上；size 为零 = 指针位置锚）。owner 为 key 前缀（多菜单共存用）。
    void openAnchored(app::AppShell& shell, core::Rect anchor, MenuItems items,
                      SubmenuProvider submenu = {},
                      const style::Theme* anchorTheme = nullptr,
                      std::string owner = "menu");
    void close(app::AppShell& shell);
    [[nodiscard]] bool isOpen() const { return !levels_.empty(); }
    // 当前级联深度（1 = 仅顶级；MenuBar 据此决定 Left/Right 切换顶级）。
    [[nodiscard]] std::size_t levelCount() const { return levels_.size(); }

    // 键盘导航（应用 ShellConfig.onKey 以 modal 优先级转发；仅打开时
    // 消费）。顶级 Left/Right（无子菜单动作）返回 false，交调用方处理
    //（MenuBar 顶级切换）。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers mods = core::kModifierNone,
                   char keyChar = 0);

    // 命令回调：普通项与 checkable 项统一走这里（UI 线程；关闭菜单后
    // 触发）。
    std::function<void(const std::string& id)> onCommand{};
    // 关闭后焦点恢复目标 key（空 = 唤起前焦点）。
    void setFocusRestoreKey(std::string key) {
        focusRestoreKey_ = std::move(key);
    }

  private:
    struct Level {
        MenuItems items;
        std::size_t highlight{0};  // 键盘高亮（可聚焦项索引）
        core::Rect anchor{};       // 级联锚（level 0 = 唤起锚）
        float scrollOffset{0.0F};  // 该级菜单的持久滚动位置
    };

    [[nodiscard]] std::string itemKey(std::size_t level,
                                      std::size_t index) const;
    [[nodiscard]] std::string scrollKey(std::size_t level) const;
    [[nodiscard]] std::size_t firstFocusable(const Level& level) const;
    [[nodiscard]] std::size_t nextFocusable(const Level& level,
                                            std::size_t from, int step) const;
    void registerHandlers(app::AppShell& shell);
    void eraseHandlers(app::AppShell& shell);
    void itemClicked(app::AppShell& shell, std::size_t level,
                     std::size_t index);
    void expandSubmenu(app::AppShell& shell, std::size_t level,
                       std::size_t index);
    void popLevel(app::AppShell& shell);
    void ensureHighlightVisible(app::AppShell& shell, std::size_t level);
    void refreshOverlay(app::AppShell& shell);
    void activate(app::AppShell& shell, std::size_t level,
                  std::size_t index);
    [[nodiscard]] core::Widget buildOverlay(const style::Theme& theme,
                                            core::Size view) const;
    [[nodiscard]] core::Widget buildPanel(const style::Theme& theme,
                                          const Level& level,
                                          std::size_t levelIndex,
                                          core::Size view) const;

    std::vector<Level> levels_{};
    SubmenuProvider submenu_{};
    std::string owner_{"menu"};
    std::string focusRestoreKey_{};
    // 注册过的 handler 名（close 时精确清理；随菜单规模有界）。
    std::vector<std::string> registeredHandlers_{};
    std::optional<style::Theme> overlayTheme_{};
    bool explicitTheme_{false};
};

// 菜单栏控制器：栏 = 主树普通子树（Ghost Button 行，参与 Tab/语义），
// 菜单面板 = overlay 锚定栏项下方。点击栏项打开；菜单打开期间 Left/
// Right 切换顶级菜单；Alt+助记字母直接打开（栏拥有焦点或菜单打开时）。
class MenuBarController {
  public:
    struct TopMenu {
        std::string id{};
        std::string title{};
        char mnemonic{0};
    };

    void setMenus(std::vector<TopMenu> menus);
    // 顶级菜单项级懒取（按 id）。
    void setMenuProvider(
        std::function<MenuItems(const std::string& id)> provider);
    // 子菜单级联（转发 ContextMenuController；空 = 顶级菜单为平面）。
    void setSubmenuProvider(SubmenuProvider submenu);

    // 栏 Widget（应用 build 每帧调用；栏项 key = "menu:bar:<id>"）。
    [[nodiscard]] core::Widget build() const;

    // 注册栏项 handler（打开/切换）；应用装配时调用一次。
    void attach(app::AppShell& shell);

    // 键盘（应用 onKey 转发；栏焦点或菜单打开时）。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers mods = core::kModifierNone,
                   char keyChar = 0);

    [[nodiscard]] bool isOpen() const { return menu_.isOpen(); }
    void close(app::AppShell& shell) { menu_.close(shell); }

    // 命令回调（与 ContextMenuController::onCommand 同语义）。
    std::function<void(const std::string& id)> onCommand{};

  private:
    [[nodiscard]] std::string barKey(std::size_t index) const;
    void openMenu(app::AppShell& shell, std::size_t index);

    std::vector<TopMenu> menus_{};
    std::function<MenuItems(const std::string&)> provider_{};
    SubmenuProvider submenu_{};
    ContextMenuController menu_{};
    std::size_t openIndex_{0};
    bool attached_{false};  // attach 幂等守卫（防 sink 重复注册）
};

}  // namespace lumen::widgets
