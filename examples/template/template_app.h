// lumen-template（G-6，docs/lumen-getting-started.md 的可运行对应物）：
// 「最小真实应用」脚手架——自定义标题栏 + 菜单（命令注册表）+
// StateStore + Preferences 持久化 + 运行时诊断。新工具应用从此拷改，
// 不再从 settings/gallery 搬演示代码。
//
// 结构（约 300 行，全部走框架既有便利层）：
//   - 标题栏：品牌 + MenuBar + 弹性拖拽区 + 窗口控制（titlebar-design
//     §5 的最小子集；窗口命令经宿主回调）。
//   - 命令：G-1 命令注册表（Ctrl+N 重置 / Ctrl+B 侧栏 / Ctrl+Q 退出），
//     菜单项 .command 派生快捷键串（单一数据源）。
//   - 状态：StateStore（counter/sidebar）；变更即持久化（G-7）。
//   - 对话框：G-4a DialogHost（"重置计数器？"确认）。
//   - 诊断：G-2 diagnosticsDirectory（崩溃兜底 + 持久日志）。

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#include "lumen/app/app_shell.h"
#include "lumen/core/preferences.h"
#include "lumen/diagnostics/runtime_diagnostics.h"
#include "lumen/widgets/dialog_host.h"
#include "lumen/widgets/menu.h"

namespace lumen::template_app {

using app::AppShell;
using app::ShellConfig;
using core::Preferences;

// 应用主体：控制器声明在 shell_ 之前（构造期 build 即可读取）。
class TemplateApp {
  public:
    explicit TemplateApp(bool persistent = true) {
        persistent_ = persistent;
        loadState();
    }

    [[nodiscard]] app::AppShell& shell() { return shell_; }
    // headless 冒烟查询：便利对话框是否打开。
    [[nodiscard]] bool dialogsBusy() const { return dialogs_.busy(); }
    // headless 冒烟关闭持久化（帧哈希确定性；真窗口路径不受影响）。
    void setPersistent(bool enabled) { persistent_ = enabled; }

    // 窗口命令（自定义标题栏 → 宿主；main.cpp 注入平台回调）。
    struct WindowCommands {
        std::function<void()> minimize;
        std::function<void()> toggleMaximize;
        std::function<void()> requestClose;
    };
    void setWindowCommands(WindowCommands commands) {
        windowCommands_ = std::move(commands);
    }

    // 窗口最大化状态回显（标题栏按钮图标切换；runApp onEvent 转发）。
    void noteWindowMaximized(bool maximized) { maximized_ = maximized; }

  private:
    // --- 持久化（G-7：变更即保存；损坏降级为空表重建） ---
    void loadState() {
        if (!persistent_) {
            return;
        }
        const std::string dir =
            diagnostics::defaultDiagnosticsDirectory("lumen-template");
        if (dir.empty()) {
            return;
        }
        Preferences prefs;
        if (prefs.load((std::filesystem::path(dir) / "state.dat").string())) {
            shell_.state().set("counter",
                               std::to_string(prefs.getInt("counter", 0)));
            shell_.state().set("sidebar",
                               prefs.getBool("sidebar", true) ? "true"
                                                              : "false");
        }
    }

    void persist() const {
        if (!persistent_) {
            return;
        }
        const std::string dir =
            diagnostics::defaultDiagnosticsDirectory("lumen-template");
        if (dir.empty()) {
            return;
        }
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        Preferences prefs;
        prefs.setInt("counter", std::atoi(shell_.state().get("counter").c_str()));
        prefs.setBool("sidebar", shell_.state().get("sidebar") == "true");
        (void)prefs.save((std::filesystem::path(dir) / "state.dat").string());
    }

    // --- 业务动作（命令/菜单/handler 共用） ---
    void bump() {
        const int counter =
            std::atoi(shell_.state().get("counter").c_str());
        shell_.state().set("counter", std::to_string(counter + 1));
        persist();
    }

    void requestReset() {
        dialogs_.showConfirm(shell_, "重置计数器？",
                             "当前计数将归零，此操作可再次确认。",
                             {"重置", "取消"},
                             [this](bool accepted) {
                                 if (accepted) {
                                     shell_.state().set("counter", "0");
                                     persist();
                                 }
                             });
    }

    // 退出：经宿主窗口关闭请求（合成 WindowCloseRequested → runApp
    // 统一关闭规则）。requestClose 的返回值语义是"已消费"（模态拦下），
    // 不代表应用退出——命令路径直接调用它不会停主循环（review H-3）。
    void quitApp() {
        if (windowCommands_.requestClose) {
            windowCommands_.requestClose();
            return;
        }
        (void)shell_.requestClose();
    }

    void toggleSidebar() {
        const bool on = shell_.state().get("sidebar") != "true";
        shell_.state().set("sidebar", on ? "true" : "false");
        persist();
    }

  public:
    // --- G-1：窗口命令注册（菜单 .command 与键盘同一注册表） ---
    void registerCommands() {
        auto& commands = shell_.commands();
        const auto action = [this](void (TemplateApp::*member)()) {
            return [this, member](AppShell&) { (this->*member)(); };
        };
        commands.registerCommand(
            {.id = "bump",
             .label = "计数 +1",
             .binding = app::KeyBinding::chord('n', core::kModifierCtrl),
             .invoke = action(&TemplateApp::bump)});
        commands.registerCommand(
            {.id = "reset",
             .label = "重置…",
             .invoke = action(&TemplateApp::requestReset)});
        commands.registerCommand(
            {.id = "toggle-sidebar",
             .label = "显示侧栏",
             .binding = app::KeyBinding::chord('b', core::kModifierCtrl),
             .invoke = action(&TemplateApp::toggleSidebar)});
        commands.registerCommand(
            {.id = "quit",
             .label = "退出",
             .binding = app::KeyBinding::chord('q', core::kModifierCtrl),
             .invoke = action(&TemplateApp::quitApp)});
    }

    // --- UI ---

    // 标题栏（titlebar-design §5 最小子集）：品牌 + 菜单 + 拖拽区 +
    // 窗口控制。整行 windowDrag：空白拖动移窗、双击最大化由平台提供；
    // 按钮/菜单命中链更深，自然排除。
    [[nodiscard]] core::Widget buildTitleBar() const {
        const style::Theme& theme = shell_.theme();
        core::Widget brand = core::makeText("Lumen Template",
                                            theme.typography.label);
        brand.key = "template-brand";
        core::Widget menus = core::withKey(menuBar_.build(theme),
                                           "template-menubar");
        core::Widget drag = core::makeRow({}, core::MainAxisAlignment::Start,
                                          core::CrossAxisAlignment::Center);
        drag.key = "template-drag";
        drag.flex = 1.0F;
        const auto windowButton = [&](core::IconId icon, std::string key) {
            core::Widget button = core::makeButton(
                "", theme.typography.label, core::EdgeInsets{}, 0.0F, key,
                44.0F, 32.0F);
            button.icon = icon;
            return core::withVariant(core::withKey(std::move(button), key),
                                     core::ButtonVariant::Ghost);
        };
        core::Widget actions = core::makeRow(
            {windowButton(core::IconId::Minus, "window-minimize"),
             windowButton(maximized_ ? core::IconId::Restore
                                     : core::IconId::Maximize,
                          "window-maximize"),
             windowButton(core::IconId::Close, "window-close")},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch);
        actions.key = "template-window-actions";
        core::Widget row = core::makeRow(
            {std::move(brand), std::move(menus), std::move(drag),
             std::move(actions)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            8.0F, core::EdgeInsets::only(12.0F, 0.0F, 0.0F, 0.0F));
        row.height = 48.0F;
        row = core::withWindowDrag(std::move(row));
        row.key = "template-titlebar";
        core::Widget divider = core::makeContainerLeaf(
            std::nullopt, 1.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.borderDefault, "template-titlebar-divider");
        return core::makeColumn({std::move(row), std::move(divider)},
                                core::MainAxisAlignment::Start,
                                core::CrossAxisAlignment::Stretch);
    }

    [[nodiscard]] core::Widget buildBody() const {
        const style::Theme& theme = shell_.theme();
        const bool sidebar = shell_.state().get("sidebar") == "true";
        std::vector<core::Widget> columns;
        if (sidebar) {
            core::Widget side = core::makeColumn(
                {core::makeText("侧栏", theme.typography.label),
                 core::makeText("Ctrl+B 切换", theme.typography.body)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, style::spaceToken(2));
            side.key = "template-sidebar";
            side.width = 160.0F;
            side.color = theme.colors.surfaceSunken;
            side.padding = core::EdgeInsets::all(12.0F);
            columns.push_back(std::move(side));
        }
        const std::string counter = shell_.state().get("counter");
        core::Widget main = core::makeColumn(
            {core::makeText("计数：" + counter, theme.typography.title),
             core::withKey(
                 core::withVariant(
                     core::makeButton("计数 +1（Ctrl+N）",
                                      theme.typography.label,
                                      core::EdgeInsets{}, 0.0F, "bump-btn",
                                      std::nullopt, std::nullopt, "bump"),
                     core::ButtonVariant::Filled),
                 "bump-btn"),
             core::makeText("菜单「文件 → 重置…」演示确认对话框",
                            theme.typography.body)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Start,
            style::spaceToken(3));
        main.key = "template-main";
        main.padding = core::EdgeInsets::all(24.0F);
        main.flex = 1.0F;
        columns.push_back(std::move(main));
        core::Widget body = core::makeRow(std::move(columns),
                                          core::MainAxisAlignment::Start,
                                          core::CrossAxisAlignment::Stretch);
        body.flex = 1.0F;
        return body;
    }

    [[nodiscard]] core::Widget buildUi() {
        const style::Theme& theme = shell_.theme();
        core::Widget ui =
            core::makeColumn({buildTitleBar(), buildBody()},
                             core::MainAxisAlignment::Start,
                             core::CrossAxisAlignment::Stretch);
        ui.color = theme.colors.pageBackground;
        ui.key = "root";
        // G-4a：对话框子树（打开时叠加为栈层）。
        if (std::optional<core::Widget> dialog = dialogs_.build(shell_)) {
            ui = core::makeStack({std::move(ui), std::move(*dialog)});
            ui.key = "root";
        }
        return ui;
    }

    // --- 装配（main.cpp 调用一次） ---
    void attach() {
        shell_.state().set("counter", shell_.state().get("counter").empty()
                                          ? "0"
                                          : shell_.state().get("counter"));
        shell_.state().set("sidebar", shell_.state().get("sidebar").empty()
                                          ? "true"
                                          : shell_.state().get("sidebar"));
        menuBar_.setMenus({{"file", "文件(F)", 'f'}, {"view", "视图(V)", 'v'}});
        menuBar_.setMenuProvider([this](const std::string& id) {
            widgets::MenuItems items;
            if (id == "file") {
                items.push_back({.id = "bump", .label = "计数 +1",
                                 .command = "bump"});
                items.push_back({.id = "reset", .label = "重置…",
                                 .command = "reset"});
                items.push_back({.id = "sep", .separator = true});
                items.push_back({.id = "quit", .label = "退出",
                                 .command = "quit"});
            } else if (id == "view") {
                items.push_back({.id = "toggle-sidebar", .label = "显示侧栏",
                                 .checkable = true,
                                 .checked = shell_.state().get("sidebar") ==
                                            "true",
                                 .command = "toggle-sidebar"});
            }
            return items;
        });
        menuBar_.attach(shell_);
        registerCommands();
        // 菜单命令统一回显（未注册 id 的旧路径兜底）。
        const auto forward = [this](const std::string& id) {
            if (id == "bump") {
                bump();
            } else if (id == "reset") {
                requestReset();
            } else if (id == "toggle-sidebar") {
                toggleSidebar();
            }
            shell_.markDirty();
        };
        menuBar_.onCommand = forward;
        auto& handlers = shell_.handlers();
        handlers["bump"] = [this] { bump(); };
        handlers["window-minimize"] = [this] {
            if (windowCommands_.minimize) {
                windowCommands_.minimize();
            }
        };
        handlers["window-maximize"] = [this] {
            if (windowCommands_.toggleMaximize) {
                windowCommands_.toggleMaximize();
            }
        };
        handlers["window-close"] = [this] {
            if (windowCommands_.requestClose) {
                windowCommands_.requestClose();
                return;
            }
            (void)shell_.requestClose();
        };
    }

    // --- ShellConfig（四钩子接线；模板的统一返回规则） ---
    [[nodiscard]] static ShellConfig configFor(TemplateApp* self) {
        ShellConfig config;
        config.initialView = core::Size{800.0F, 560.0F};
        config.build = [self] { return self->buildUi(); };
        // Escape：对话框优先，其次菜单（modal 优先于一切；plan §3.4）。
        config.onKey = [self](AppShell& shell, core::Key key,
                              core::KeyModifiers mods, char ch) {
            if (self->dialogs_.handleKey(shell, key, mods, ch)) {
                return true;
            }
            if (self->menuBar_.handleKey(shell, key, mods, ch)) {
                return true;
            }
            return false;
        };
        // 窗口关闭请求：对话框打开时消费（不退出）。
        config.onCloseRequested = [self](AppShell& shell) {
            return self->dialogs_.handleCloseRequested(shell);
        };
        // 重建后：对话框焦点安置/恢复。
        config.onRebuilt = [self](AppShell& shell) {
            self->dialogs_.onRebuilt(shell);
        };
        return config;
    }

  private:
    app::AppShell shell_{configFor(this)};
    widgets::MenuBarController menuBar_{};
    widgets::DialogHost dialogs_{};
    WindowCommands windowCommands_{};
    bool maximized_{false};
    bool persistent_{true};
};

}  // namespace lumen::template_app
