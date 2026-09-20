#pragma once

// ToolBar 工具栏（docs/lumen-toolbar-design.md，2026-09）：Ghost Button
// 子树 + 栏级控制器——分组分隔、toggle 持久底色、tooltip（M11 注册机
// 制）、尾部溢出折叠（复用 M14 菜单面板）。零新增 WidgetType /
// RenderCommand。
//
// 溢出决策（design §6.3）：宽度不足时从尾部折叠（labelMode 项优先），
// 尾部出现溢出按钮（ChevronDown）→ 点击弹 M14 面板（icon + label 列，
// 键盘导航复用菜单契约）。决策输入 = 上一帧布局几何（barKey 行宽 + 各
// 项实测宽缓存；Splitter"二次收敛"同口径——首帧全量，次帧折叠/回位），
// 控制器不自测文本。
//
// 键盘（design §6.2，按 MenuBar 栏件既有口径调整）：项为普通可聚焦
// Button（参与 Tab 遍历，MenuBar 先例）；Left/Right/Home/End 在项间漫
// 游焦点、Enter/Space 激活（框架既有）、Down 打开溢出面板、Esc 关闭。
// 焦点环恒开启（makeTabs 同口径的框架自建键盘件，visual-system §6.1）。
//
// toggle 激活态 = Chrome 变体（hover 表面派生 + 前景提亮、pressed = List
// pressed）+ checked 持久底 accentContainer（稿件 .is-checked，与 MenuBar
// 打开态同语言）；Widget.checked 直声明 → kSemanticsChecked 同步。checked
// 由应用维护（MenuItem §5 同口径）。

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/geometry.h"
#include "lumen/core/icon_id.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/style/theme.h"
#include "lumen/widgets/menu.h"

namespace lumen::widgets {

// 工具栏项模型（toolbar-design §5）：与菜单项平行的命令描述。同一命令
// 在菜单和工具栏出现时由应用保证 id 一致并联动状态——框架不建注册表。
struct ToolBarItem {
    std::string id{};          // 稳定标识：项 key 与 onCommand 参数
    core::IconId icon{core::IconId::None};  // icon-only 项必填
    std::string label{};       // tooltip 文本；labelMode 项的栏内文本
    std::string shortcut{};    // tooltip 尾随展示（"Ctrl+S"；仅显示）
    bool separator{false};     // 分组分隔线（其余字段忽略）
    bool checkable{false};     // toggle 项：checked 持久底色
    bool checked{false};       // 应用维护（StateStore 事实来源）
    bool labelMode{false};     // icon + 文本水平排列（默认 icon-only）
    bool enabled{true};

    [[nodiscard]] bool operator==(const ToolBarItem&) const = default;
};

class ToolBarController {
  public:
    explicit ToolBarController(std::string key = "tb");

    void setItems(std::vector<ToolBarItem> items);

    // toggle 项 checked 更新（应用维护状态的写回通道；id 未匹配时忽略）。
    void setChecked(const std::string& id, bool checked);

    // 尺度档（design §9.1）：Medium = 跟随密度档，Small/Large 相对密度
    // 上下移一档（Spin/StatusBar 同口径）——紧凑瓦片样本需要脱离全局密度
    // 单独取 32px 档。
    void setControlSize(core::ControlSize size);
    // 栏容器语义 label（design §7：由应用提供，如"主工具栏"）。
    void setSemanticsLabel(std::string label);

    // 栏 Widget（应用 build 每帧调用；shell 提供上一帧布局几何供溢出
    // 决策——menu/splitter 的 build(theme) 之上多一个壳引用）。溢出集
    // 合变化时 markDirty 触发二次收敛重建。
    [[nodiscard]] core::Widget build(app::AppShell& shell,
                                     const style::Theme& theme) const;

    // 注册项/溢出 handler 与 tooltip 关联；应用装配时调用一次
    //（setItems 在 attach 之后调用会自动补注册）。
    void attach(app::AppShell& shell);

    // 键盘（应用 ShellConfig.onKey 转发）：面板打开时优先转发菜单契约；
    // 否则 Left/Right/Home/End 漫游焦点、Down（焦点在溢出按钮）开面板。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers mods = core::kModifierNone,
                   char keyChar = 0);

    [[nodiscard]] bool isOpen() const { return menu_.isOpen(); }
    void close(app::AppShell& shell) { menu_.close(shell); }
    // 溢出集合只读查询（测试/语义用；原序）。
    [[nodiscard]] const std::vector<std::string>& overflowedIds() const {
        return overflowIds_;
    }

    // 命令回调（与 ContextMenuController::onCommand 同语义；checkable
    // 项激活后应用翻转状态并重建）。
    std::function<void(const std::string& id)> onCommand{};

  private:
    [[nodiscard]] std::string barKey() const;
    [[nodiscard]] std::string rowKey() const;
    [[nodiscard]] std::string itemKey(const std::string& id) const;
    [[nodiscard]] std::string tipKey(const std::string& id) const;
    [[nodiscard]] std::string overflowKey() const;
    // 溢出决策（toolbar-design §6.3）：上一帧行宽 vs 项宽缓存；折叠从
    // 尾部、labelMode 优先、折空分隔线消失、至少保留首项。
    void recomputeOverflow(app::AppShell& shell) const;
    void registerHandlers(app::AppShell& shell) const;
    void registerTooltips(app::AppShell& shell) const;
    void openOverflow(app::AppShell& shell) const;
    [[nodiscard]] std::vector<MenuItem> panelItems() const;
    [[nodiscard]] float itemWidth(const ToolBarItem& item,
                                  const style::Theme& theme) const;

    std::string key_{};
    std::string semanticsLabel_{};
    core::ControlSize controlSize_{core::ControlSize::Medium};
    std::vector<ToolBarItem> items_{};
    // 面板控制器（build 内 openOverflow 会写动画状态——mutable，MenuBar
    // build/attach 契约同口径）。
    mutable ContextMenuController menu_{};
    app::AppShell* shell_{nullptr};
    bool attached_{false};
    // 溢出状态（build 内回填；mutable —— build 契约与 MenuBar 相同）。
    mutable std::vector<std::string> overflowIds_{};
    mutable std::map<std::string, float> widthCache_{};
};

}  // namespace lumen::widgets
