#pragma once

// Widget Gallery（与 counter/settings 同级示例）：演示当前已完成控件、
// 布局、颜色方案/主题。窗口主循环由 app::runApp 驱动；本文件只保留
// 应用层职责：build 函数、状态 key 与业务 handler（plan §6.1）。
// 壳层与 Overview 首屏对齐 design/gallery.html v1「Core Dark」设计稿
//（2026-09 尺度优化：正文/标签 14px、辅助 12px、主控件 40px、紧凑样本
// 32px、4px 网格）：自定义标题栏（lumen-titlebar-design：品牌/MenuBar/
// 拖拽区/状态胶囊/窗口控制一条 48px 行，无边框窗口自绘 caption）、
// 200px 侧栏（<1024 收窄 168；导航 + Live state 注记）、kicker/hero/
// 指标卡/双栏面板（Control inventory、DSL 快照、Resolved tokens、
// Theme controls）、双侧页脚；主内容 <720 上下堆叠。视觉全部来自
// Theme token。
// 覆盖：Button 变体/尺寸/状态、TextField/Checkbox/Switch/Radio/Slider/
// Dropdown/Tabs/ProgressBar/Icon/Tooltip/Dialog、Row/Column(flex)/Stack/
// Container/Grid、ListView/VirtualList、Theme 深浅/密度/强调色/局部
// ThemeScope/排版/语义色板。
// 菜单类控件与分栏（menu/splitter-controls 设计稿 §11.3/§10.3，对齐
// design/gallery.html 增补）：MenuBar 嵌入标题栏（File/View/Help，
// 点击/Alt+助记打开，打开后 ←/→ 切换顶级）、主内容区右键
// ContextMenu、侧栏|内容 Splitter（拖动/键盘步进/双击复位；未手动调节
// 时跟随 200/168 断点）、Menus 分区演示页与 Overview 清单新瓷砖。

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"
#include "lumen/core/scroll.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/state.h"
#include "lumen/widgets/dropdown.h"
#include "lumen/core/widget.h"
#include "lumen/render/renderer.h"
#include "lumen/style/theme.h"
#include "lumen/style/resolver.h"
#include "lumen/text/font_manager.h"
#include "lumen/widgets/form.h"
#include "lumen/widgets/list.h"
#include "lumen/widgets/menu.h"
#include "lumen/widgets/navigator.h"
#include "lumen/widgets/spin.h"
#include "lumen/widgets/splitter.h"
#include "lumen/widgets/statusbar.h"
#include "lumen/widgets/toolbar.h"
#include "lumen/widgets/tree.h"

namespace lumen::examples {

class GalleryApp {
  public:
    GalleryApp() : shell_(configFor(this)) { initialize(); }

    GalleryApp(const GalleryApp&) = delete;
    GalleryApp& operator=(const GalleryApp&) = delete;
    GalleryApp(GalleryApp&&) = delete;
    GalleryApp& operator=(GalleryApp&&) = delete;

    void setView(core::Size size) {
        shell_.setView(size);
        // Splitter 未被手动调节时跟随 200/168 响应式断点（设计稿侧栏宽）。
        syncSidebarBreakpoint();
    }
    // Deterministic entry for screenshots; uses the same routed page and scroll.
    bool showSample(const std::string& route, const std::string& key = {}) {
        go(route);
        (void)renderFrame();
        if (key.empty()) return true;
        const auto* node = core::findNodeByKey(root(), key);
        const auto* viewport = core::findNodeByKey(root(), "gallery-list");
        if (!node || !viewport) return false;
        scroll_.updateExtents(viewport->size.height, viewport->size.height + viewport->scrollExtent);
        const auto position = core::absoluteOffset(root(), key);
        const auto top = core::absoluteOffset(root(), "gallery-list");
        scroll_.scrollTo(scroll_.offset() + position.y - top.y - 12.0F);
        shell_.markDirty();
        return true;
    }
    void setDeviceScale(float scale) { shell_.setDeviceScale(scale); }
    void setRenderer(render::Renderer* renderer) {
        shell_.setRenderer(renderer);
    }
    void setFontManager(std::shared_ptr<const text::FontManager> fonts) {
        shell_.setFontManager(std::move(fonts));
    }
    void rebuildIfDirty() { shell_.rebuildIfDirty(); }
    std::uint64_t renderFrame(bool forceFullRepaint = false) {
        return shell_.renderFrame(forceFullRepaint);
    }
    void setAccessibilitySettings(
        accessibility::AccessibilitySettings settings) {
        shell_.setAccessibilitySettings(settings, darkMode_);
        refreshScopePreview();
    }
    [[nodiscard]] const accessibility::AccessibilitySettings&
    accessibilitySettings() const {
        return shell_.accessibilitySettings();
    }
    void setTheme(style::Theme theme, bool forceFullRepaint = true) {
        // M11：Theme 携带派生元数据，替代按 pageBackground 色值反推的
        // 启发式（多变体方向下色值比较会失真）。
        darkMode_ = theme.darkMode;
        direction_ = theme.direction;
        shell_.setTheme(std::move(theme), forceFullRepaint);
        refreshScopePreview();
    }
    [[nodiscard]] const style::Theme& theme() const { return shell_.theme(); }
    void tick(std::uint64_t nowMs) { shell_.tick(nowMs); }
    void swapRoot(core::Widget root) { shell_.swapRoot(std::move(root)); }
    void markDirty() { shell_.markDirty(); }

    void pointerDown(core::Offset position) { shell_.pointerDown(position); }
    void pointerMove(core::Offset position) { shell_.pointerMove(position); }
    void pointerUp(core::Offset position) { shell_.pointerUp(position); }
    void pointerCancel() { shell_.pointerCancel(); }
    void wheel(core::Offset position, core::Offset delta) {
        // 消费状态由调用方按需读取（示例转发不区分）。
        (void)shell_.wheel(position, delta);
    }
    void textInput(const std::string& text) { shell_.textInput(text); }
    void textEditing(const std::string& text) { shell_.textEditing(text); }
    void keyDown(core::Key key,
                 core::KeyModifiers modifiers = core::kModifierNone,
                 char keyChar = 0) {
        shell_.keyDown(key, modifiers, keyChar);
    }

    [[nodiscard]] app::AppShell& shell() { return shell_; }
    [[nodiscard]] const app::AppShell& shell() const { return shell_; }
    [[nodiscard]] const core::StateStore& state() const {
        return shell_.state();
    }
    [[nodiscard]] const core::RenderNode& root() const { return shell_.root(); }
    [[nodiscard]] const core::InteractionController& controller() const {
        return shell_.controller();
    }
    [[nodiscard]] core::ScrollController& scroll() { return scroll_; }
    [[nodiscard]] widgets::NavigatorController& navigator() {
        return navigator_;
    }
    [[nodiscard]] widgets::FormController& form() { return form_; }
    [[nodiscard]] widgets::ListController& collectionList() {
        return collectionList_;
    }
    [[nodiscard]] widgets::TreeController& collectionTree() {
        return collectionTree_;
    }
    [[nodiscard]] widgets::TreeListController& collectionTable() {
        return collectionTable_;
    }
    // Collections 回显（测试/截图断言用）。
    [[nodiscard]] const std::string& lastActivatedKey() const {
        return lastActivatedKey_;
    }
    [[nodiscard]] const std::string& lastSortColumn() const {
        return lastSortColumn_;
    }
    [[nodiscard]] bool lastSortDescending() const {
        return lastSortDescending_;
    }
    [[nodiscard]] bool dialogOpen() const { return dialogOpen_; }
    [[nodiscard]] bool dropdownOpen() const { return dropdown_.isOpen(); }
    [[nodiscard]] widgets::DropdownController& dropdown() { return dropdown_; }
    // 菜单类控件与分栏（测试/回显断言用）。
    [[nodiscard]] const std::string& lastMenuCommand() const {
        return lastMenuCommand_;
    }
    [[nodiscard]] bool menuBarOpen() const { return menuBar_.isOpen(); }
    [[nodiscard]] bool sidebarVisible() const { return sidebarVisible_; }
    [[nodiscard]] widgets::SplitterController& sidebarSplitter() {
        return sidebarSplitter_;
    }
    // 自定义标题栏（lumen-titlebar-design §4.4）：平台窗口命令（main.cpp
    // 窗口装配注入；headless/采样路径为空——handler 仍执行并记录命令，
    // close 回退 shell.requestClose() 保持统一关闭语义可测）。
    struct WindowCommands {
        std::function<void()> minimize{};
        std::function<void()> toggleMaximize{};
        std::function<void()> requestClose{};
    };
    void setWindowCommands(WindowCommands commands) {
        windowCommands_ = std::move(commands);
    }
    // 窗口命令回显（测试/截图断言用）：最近一次 window-minimize/
    // window-maximize/window-close。
    [[nodiscard]] const std::string& lastWindowCommand() const {
        return lastWindowCommand_;
    }
    // 最大化状态（WindowMaximized/WindowRestored 经 onEvent 注入）：
    // 切换窗口控制 Maximize/Restore 图标。
    void noteWindowMaximized(bool maximized) {
        if (windowMaximized_ == maximized) {
            return;
        }
        windowMaximized_ = maximized;
        shell_.markDirty();
    }
    [[nodiscard]] bool windowMaximized() const { return windowMaximized_; }
    [[nodiscard]] bool darkMode() const { return darkMode_; }
    [[nodiscard]] style::ThemeDirection direction() const {
        return direction_;
    }
    [[nodiscard]] bool followSystemTheme() const {
        return followSystemTheme_;
    }
    /// M12：系统主题偏好（SystemThemeChanged 时由装配层注入 host 能力；
    /// 仅开启"跟随系统"时重派生——方向/高对比/密度/字体缩放全保留）。
    void setSystemThemePreference(
        bool prefersDark,
        std::optional<core::Color> accentColor = std::nullopt) {
        systemPrefersDark_ = prefersDark;
        systemAccent_ = accentColor;
        if (followSystemTheme_) {
            applySystemTheme();
        }
    }
    [[nodiscard]] core::Size view() const { return shell_.view(); }
    [[nodiscard]] const render::PixelBuffer& pixels() const {
        return shell_.pixels();
    }

    [[nodiscard]] accessibility::SemanticsTree semantics() const {
        accessibility::SemanticsBuildOptions options;
        options.focus = &shell_.focus();
        return accessibility::buildSemanticsTree(shell_.root(), options);
    }

    // 设计稿容器断点的应用侧映射（@container gallery/content）：视口宽
    // 即画板宽。断点取开区间（<1024/<720）：默认 1024×768 视口保持完整
    // 布局，与 CSS max-width 含端点仅差 1px 边界。<1024 收窄侧栏/主内
    // 边距并隐藏窗口状态；<720 折叠导航（框架无导航图标目录，64px 图标
    // 栏以 header 下拉近似）；主内容 <720 上下堆叠、<480 单列指标。
    [[nodiscard]] bool compactNavigation() const {
        return shell_.view().width <
               720.0F * shell_.accessibilitySettings().fontScale;
    }
    [[nodiscard]] bool narrowWindow() const {
        return shell_.view().width <
               1024.0F * shell_.accessibilitySettings().fontScale;
    }
    [[nodiscard]] float sidebarWidth() const {
        return narrowWindow() ? 168.0F : 200.0F;
    }
    [[nodiscard]] float contentPadding() const {
        return narrowWindow() ? 24.0F : 32.0F;
    }
    // 主内容可用宽（决定内容栅格单双列与窄版指标卡）。Splitter 接管后
    // 侧栏宽取控制器实际 offset（未手动调节时 = 断点宽），并扣除分隔条
    // 6px 轨道占位；框架按密度在轨道两侧透明扩展命中区。
    [[nodiscard]] float contentColumnWidth() const {
        const bool sidebarShown =
            !compactNavigation() && sidebarVisible_;
        const float sidebar =
            sidebarShown ? sidebarSplitter_.offset() : 0.0F;
        const float track =
            sidebarShown ? core::kSplitterTrackThickness : 0.0F;
        return shell_.view().width - sidebar - track -
               2.0F * contentPadding();
    }

    // Gallery 根：自定义标题栏（MenuBar 嵌入）+ Body(Splitter: 导航栏 |
    // 内容 ListView) + Footer。Body 的 Splitter 即分栏控件在窗口 chrome 的
    // 使用演示（拖动/键盘/双击复位，framework 物化分隔条）；内容页按
    // 路由切换。
    [[nodiscard]] core::Widget buildUi() {
        const style::Theme& theme = shell_.theme();
        core::Widget titleBar = buildTitleBar(theme);
        core::Widget body;
        if (compactNavigation() || !sidebarVisible_) {
            core::Widget content = buildContent(theme);
            content.flex = 1.0F;
            body = std::move(content);
        } else {
            core::Widget nav = buildNav(theme);
            core::Widget content = buildContent(theme);
            content.flex = 1.0F;
            body = core::makeSplitter(&sidebarSplitter_, std::move(nav),
                                      std::move(content),
                                      /*horizontal=*/true, "gallery-body");
        }
        body.flex = 1.0F;
        core::Widget footer = buildFooter(theme);
        std::vector<core::Widget> pageRows;
        pageRows.push_back(std::move(titleBar));
        pageRows.push_back(std::move(body));
        pageRows.push_back(std::move(footer));
        core::Widget page = core::makeColumn(
            std::move(pageRows),
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Start,
            0.0F);
        page.flex = 1.0F;
        core::Widget ui = core::makeContainer(
            std::move(page), std::nullopt, std::nullopt, core::EdgeInsets{},
            core::EdgeInsets{}, theme.colors.pageBackground);
        // 透明窗口圆角：根背景四角圆角（标题栏顶角由自身 surface 圆角
        // 覆盖；最大化归零——design/gallery.html .is-maximized）。
        ui.radius = windowMaximized_
                        ? core::CornerRadius::zero()
                        : core::CornerRadius::all(kWindowRadius);
        ui.key = "root";

        if (dialogOpen_) {
            // S4（§8.2）：整体 padding 24 由 makeDialog 施加；标题到正文
            // 12、正文到操作区 24（spacing 12 + margin 12）。
            core::Widget dialogBody = core::withKey(
                core::makeText("All widgets share one Theme.",
                               theme.typography.body),
                "dialog-body");
            core::Widget close = core::withKey(
                buttonWidget("Close", "dismiss-dialog", "dialog-close",
                             core::ButtonVariant::Tonal),
                "dialog-close");
            core::Widget contentDialog = core::makeColumn(
                {core::withKey(titleText("Gallery dialog", theme),
                               "dialog-title"),
                 std::move(dialogBody)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, style::spaceToken(3));
            core::Widget dialog = widgets::makeDialog(
                std::move(contentDialog), std::move(close), shell_.theme(),
                "dismiss-dialog", kDialogKey, shell_.view(),
                dialogScroll_.offset(),
                windowMaximized_ ? 0.0F : kWindowRadius);
            ui = core::makeStack({std::move(ui), std::move(dialog)});
            ui.key = "root";
        }
        return ui;
    }

  private:
    static constexpr const char* kDialogKey = "gallery-dialog";
    // 透明窗口圆角（design/gallery.html .gallery-window:16px；最大化归
    // 零）：根容器四角 + 标题栏顶角 + 对话框 scrim 同半径。
    static constexpr float kWindowRadius = 16.0F;
    static constexpr const char* kDialogCloseKey = "dialog-close";

    [[nodiscard]] static app::ShellConfig configFor(GalleryApp* self) {
        app::ShellConfig config;
        config.caretBlink = false;
        config.motionTransitions = true;
        config.build = [self] { return self->buildUi(); };
        config.onKey = [self](app::AppShell& shell, core::Key key,
                              core::KeyModifiers modifiers, char keyChar) {
            // M11：下拉菜单键盘导航（modal 优先于路由返回规则；未打开
            // 时 Up/Down 仍走滚动路径）。
            if (self->navigationMenu_.handleKey(shell, key)) return true;
            if (self->dropdown_.handleKey(shell, key)) {
                return true;
            }
            // 菜单类控件（menu-controls-design §6/§7）：菜单打开期间
            // Up/Down/Enter/Esc/Tab/Alt+助记字母全部由菜单消费（modal
            // 优先；未打开时 handleKey 不消费，键盘仍走滚动/焦点路径）。
            if (self->contextMenu_.handleKey(shell, key, modifiers,
                                             keyChar)) {
                return true;
            }
            if (self->menuBar_.handleKey(shell, key, modifiers, keyChar)) {
                return true;
            }
            // Spin 键盘契约（焦点在本字段时消费）与 ToolBar 漫游/溢出
            // 面板键盘（Controls 页样本；design §6.2）。
            if (self->controlsOpacity_.handleKey(shell, key, modifiers,
                                                 keyChar)) {
                return true;
            }
            if (self->controlsFontSize_.handleKey(shell, key, modifiers,
                                                  keyChar)) {
                return true;
            }
            if (self->controlsToolBar_.handleKey(shell, key, modifiers,
                                                 keyChar)) {
                return true;
            }
            // 集合控件键盘契约（collection-design §6.4/§7.4）：焦点位于
            // 某集合的行内时，导航键交给该集合的控制器（Up/Down/Home/
            // End/PageUp/PageDown、树 Left/Right、Ctrl+A）。按行 key 的
            // owner 前缀路由，三个集合互不串扰。
            if (self->navigator_.current() == "collections") {
                const std::string& focused = shell.focus().focusedKey();
                if (focused.find("collection-list") == 0) {
                    if (self->collectionList_.handleKey(key, modifiers,
                                                        keyChar)) {
                        return true;
                    }
                } else if (focused.find("collection-table") == 0) {
                    if (self->collectionTable_.handleKey(key, modifiers,
                                                         keyChar)) {
                        return true;
                    }
                } else if (focused.find("collection-tree") == 0) {
                    if (self->collectionTree_.handleKey(key, modifiers,
                                                        keyChar)) {
                        return true;
                    }
                }
            }
            if (key == core::Key::Escape &&
                self->navigator_.handleBack(self->dialogOpen_)) {
                if (self->dialogOpen_) {
                    self->closeDialog();
                } else {
                    shell.markDirty();
                    self->focusRestorePending_ = true;
                }
                return true;
            }
            return false;
        };
        config.onWheel =
            [self](const core::RenderNode& root, const core::RenderNode* hit,
                   core::Offset position, core::Offset delta) {
                // Spin 滚轮步进（指针在控件上每档 ±step；design §6.3）。
                if (self->controlsOpacity_.handleWheel(self->shell_, position,
                                                       delta)) {
                    return true;
                }
                if (self->controlsFontSize_.handleWheel(self->shell_,
                                                        position, delta)) {
                    return true;
                }
                return self->scrollWheel(root, hit, delta.y);
            };
        // M10：视口拖动滚动（触摸/指针）与惯性推进（与 settings 同形）。
        config.onScrollDrag =
            [self](const core::RenderNode*, const core::RenderNode* viewport,
                   core::Offset, core::Offset delta,
                   core::ScrollDragPhase phase, std::uint64_t nowMs) {
                return self->scrollDrag(viewport, delta.y, phase, nowMs);
            };
        config.onAnimate = [self](app::AppShell& shell,
                                  std::uint64_t nowMs) {
            // Spin 按住自动重复 + StatusBar 消息/进度/busy 节律。
            bool active = self->controlsOpacity_.step(shell, nowMs);
            active = self->controlsFontSize_.step(shell, nowMs) || active;
            active = self->controlsStatusBar_.step(shell, nowMs) || active;
            return self->advanceFling(shell, nowMs) || active;
        };
        config.onCloseRequested = [self](app::AppShell& shell) {
            if (self->dialogOpen_) {
                self->closeDialog();
                return true;
            }
            if (self->navigator_.handleBack(false)) {
                shell.markDirty();
                return true;
            }
            return false;
        };
        config.onRebuilt = [self](app::AppShell& shell) {
            if (self->lastRoute_ != self->navigator_.current()) {
                if (!self->lastRoute_.empty() && shell.motionEnabled())
                    shell.beginRouteTransition("gallery-list", true);
                self->lastRoute_ = self->navigator_.current();
            }
            if (self->dialogOpen_ &&
                shell.focus().focusedIdentity().find(kDialogKey) ==
                    std::string::npos) {
                const core::RenderNode* close =
                    core::findNodeByKey(shell.root(), kDialogCloseKey);
                if (close != nullptr) {
                    shell.controller().focusNode(*close);
                }
            }
            if (self->focusRestorePending_) {
                self->focusRestorePending_ = false;
                const auto* target = self->dialogReturnKey_.empty() ? nullptr :
                    core::findNodeByKey(shell.root(), self->dialogReturnKey_);
                if (target && target->enabled) shell.controller().focusNode(*target);
                else shell.controller().focusFirstFocusable(shell.root());
                self->dialogReturnKey_.clear();
            }
        };
        return config;
    }

    void initialize() {
        navigationMenu_.onSelected = [this](const std::string& route) { go(route); };
        initializeVisualPreviews();
        setupCollections();
        library_.setItemCount(1000);
        // M11：Tooltip hover 延迟驱动（anchor → tooltip 关联）。
        shell_.registerTooltip("tooltip-anchor-button", "showcase-tip");
        // M11：下拉选中回调（关闭菜单后写状态重建）。
        dropdown_.onSelected = [this](const std::string& name) {
            pickColor(name);
        };
        library_.setEstimatedExtent(44.0F);
        library_.setItemBuilder([this](std::size_t index) {
            core::Widget item = core::makeText(
                "Entry " + std::to_string(index),
                shell_.theme().typography.body);
            item.key = "item-" + std::to_string(index);
            if (index % 2 == 0) {
                item.height = 64.0F;
            }
            return item;
        });

        core::StateStore& state = shell_.state();
        state.set("button-clicks", "0");
        state.set("username", "");
        state.set("password", "");
        state.set("bio", "");
        state.set("search", "");
        state.set("nickname", "");
        state.set("email", "");
        state.set("notifications", "true");
        state.set("collection-focus-rings", "false");
        state.set("autosave", "false");
        state.set("plan-free", "true");
        state.set("plan-pro", "false");
        state.set("volume", "40");
        state.set("demo-progress", "64");
        state.set("color", "Red");
        state.set("gallery-tab", "Basic");

        auto& handlers = shell_.handlers();
        handlers["goto-home"] = [this] { go("home"); };
        handlers["goto-buttons"] = [this] { go("buttons"); };
        handlers["goto-inputs"] = [this] { go("inputs"); };
        handlers["goto-layout"] = [this] { go("layout"); };
        handlers["goto-lists"] = [this] { go("lists"); };
        handlers["goto-collections"] = [this] { go("collections"); };
        handlers["goto-menus"] = [this] { go("menus"); };
        handlers["goto-controls"] = [this] { go("controls"); };
        handlers["goto-feedback"] = [this] { go("feedback"); };
        handlers["goto-theme"] = [this] { go("theme"); };
        // 自定义标题栏：窗口命令（lumen-titlebar-design §4.4）——命令
        // 始终记录（headless 断言）；平台回调由 main.cpp 注入，close 无
        // 宿主时回退统一关闭规则（modal 优先消费）。
        handlers["window-minimize"] = [this] {
            lastWindowCommand_ = "window-minimize";
            if (windowCommands_.minimize) {
                windowCommands_.minimize();
            }
        };
        handlers["window-maximize"] = [this] {
            lastWindowCommand_ = "window-maximize";
            if (windowCommands_.toggleMaximize) {
                windowCommands_.toggleMaximize();
            }
        };
        handlers["window-close"] = [this] {
            lastWindowCommand_ = "window-close";
            if (windowCommands_.requestClose) {
                windowCommands_.requestClose();
                return;
            }
            (void)shell_.requestClose();
        };
        handlers["back"] = [this] {
            navigator_.pop();
            scroll_.scrollTo(0.0F);
            shell_.markDirty();
            focusRestorePending_ = true;
        };
        handlers["bump-clicks"] = [this] {
            const int clicks =
                std::atoi(shell_.state().get("button-clicks").c_str());
            shell_.state().set("button-clicks",
                               std::to_string(clicks + 1));
        };
        handlers["open-dropdown"] = [this] {
            // M11：Dropdown 浮动菜单（值行/按钮点击打开；选中经控制器
            // onSelected 回调写状态）。
            dropdown_.open(shell_, "color-dropdown");
        };
        // Tooltip 锚点演示按钮（hover 显示气泡；点击无操作）。
        handlers["noop"] = [] {};
        handlers["cycle-list-mode"] = [this] {
            const auto mode = static_cast<unsigned>(collectionList_.selection().mode());
            collectionList_.setSelectionMode(static_cast<widgets::SelectionMode>((mode + 1) % 4));
            shell_.markDirty();
        };
        handlers["open-navigation"] = [this] {
            dropdown_.close(shell_);
            navigationMenu_.setValue(navigator_.current());
            navigationMenu_.open(shell_, "compact-navigation");
        };
        handlers["switch-tab-basic"] = [this] {
            shell_.state().set("gallery-tab", "Basic");
        };
        handlers["switch-tab-more"] = [this] {
            shell_.state().set("gallery-tab", "More");
        };
        handlers["show-dialog"] = [this] {
            openDialog();
            shell_.markDirty();
            shell_.requestFullRepaint();
        };
        handlers["dismiss-dialog"] = [this] { closeDialog(); };
        handlers["toggle-dark"] = [this] { setDarkMode(!darkMode_); };
        // Overview 的 Theme controls 面板：Dark/Light 为显式目标态（点击
        // 当前态无操作），复用与 toggle 相同的派生链。
        handlers["set-dark"] = [this] { setDarkMode(true); };
        handlers["set-light"] = [this] { setDarkMode(false); };
        handlers["cycle-density"] = [this] {
            using style::ControlDensity;
            const ControlDensity current =
                shell_.theme().metrics.density;
            const ControlDensity next =
                current == ControlDensity::Compact
                    ? ControlDensity::Comfortable
                    : (current == ControlDensity::Comfortable
                           ? ControlDensity::Touch
                           : ControlDensity::Compact);
            // 以当前可访问性设置重派生（方向/字体缩放/高对比/减少动画
            // 保留）。
            style::Theme nextTheme = style::Theme::fromSettings(
                shell_.accessibilitySettings(), darkMode_, next, direction_);
            shell_.setTheme(std::move(nextTheme));
            shell_.markDirty();
        };
        // M11：v0.4 视觉方向切换（design/gallery.html 四方向；与深浅/
        // 密度/可访问性正交组合，经 fromSettings 保留全部派生）。
        handlers["set-direction-core"] = [this] {
            applyDirection(style::ThemeDirection::CoreDark);
        };
        handlers["set-direction-ink"] = [this] {
            applyDirection(style::ThemeDirection::InkLinen);
        };
        handlers["set-direction-aurora"] = [this] {
            applyDirection(style::ThemeDirection::AuroraSignal);
        };
        handlers["set-direction-utility"] = [this] {
            applyDirection(style::ThemeDirection::UtilityContrast);
        };
        // M12：跟随系统主题开关（默认关，保 headless 确定性）。
        handlers["toggle-follow-system"] = [this] {
            followSystemTheme_ = !followSystemTheme_;
            if (followSystemTheme_) {
                applySystemTheme();
            }
            shell_.markDirty();
        };
        handlers["toggle-contrast"] = [this] {
            auto settings = shell_.accessibilitySettings();
            settings.highContrast = !settings.highContrast;
            shell_.setAccessibilitySettings(settings, darkMode_);
            shell_.markDirty();
        };
        handlers["accent-blue"] = [this] {
            applyAccent(core::Color::fromRGBA(86, 140, 240));
        };
        handlers["accent-green"] = [this] {
            applyAccent(core::Color::fromRGBA(74, 160, 106));
        };
        handlers["accent-amber"] = [this] {
            applyAccent(core::Color::fromRGBA(204, 152, 64));
        };
        handlers["save"] = [this] {
            if (form_.validate(shell_.state())) {
                openDialog();
            }
            shell_.markDirty();
            shell_.requestFullRepaint();
        };

        // 菜单类控件（menu-controls-design §11.3，对齐 design/gallery.html
        // 增补）：窗口 chrome 菜单栏（File/View/Help）+ 主内容区右键菜单 +
        // 命令统一回显。checkable 项状态由应用维护（点击 → onCommand →
        // 翻转 → 重开菜单时经 provider 读取）。
        menuBar_.setMenus({{"file", "File", 'f'},
                           {"view", "View", 'v'},
                           {"help", "Help", 'h'}});
        menuBar_.setMenuProvider([this](const std::string& id) {
            return barMenuItems(id);
        });
        menuBar_.setSubmenuProvider([this](const std::string& id) {
            return barMenuItems("sub:" + id);
        });
        menuBar_.attach(shell_);
        // Spin/ToolBar/StatusBar（2026-09）：Controls 页样本装配。
        controlsOpacity_.attach(shell_);
        controlsFontSize_.setRange(12.0, 24.0);
        controlsFontSize_.setStep(0.5);
        controlsFontSize_.setDecimals(1);
        controlsFontSize_.attach(shell_);
        shell_.state().set("gal-grid", "false");
        controlsToolBar_.setItems({
            {"new", core::IconId::Plus, "New", "Ctrl+N"},
            {"open", core::IconId::Folder, "Open"},
            {"sep", core::IconId::None, "", "", true},
            {"undo", core::IconId::Undo, "Undo", "", false, false, false,
             false, false},
            {"grid", core::IconId::Grid, "Grid", "", false, true},
        });
        controlsToolBar_.onCommand = [this](const std::string& id) {
            lastControlsCommand_ = id;
            if (id == "grid") {
                const bool on = shell_.state().get("gal-grid") != "true";
                shell_.state().set("gal-grid", on ? "true" : "false");
                controlsToolBar_.setChecked("grid", on);
                // toggle → StatusBar busy 联动（toolbar/statusbar-design
                // 演示场景：同一命令由应用联动两处状态）。
                controlsStatusBar_.setBusy(on);
            }
            shell_.markDirty();
        };
        controlsToolBar_.attach(shell_);
        controlsStatusBar_.attach(shell_);
        const auto forwardCommand = [this](const std::string& id) {
            handleMenuCommand(id);
        };
        menuBar_.onCommand = forwardCommand;
        contextMenu_.onCommand = forwardCommand;
        // 右键通道：Menus 分区演示行（menu-target: 前缀）行级菜单，主内
        // 容区（gallery-list 子树）通用菜单（设计稿右键 app-main）。
        shell_.controller().addSecondaryPressSink(
            [this](const std::vector<const core::RenderNode*>& chain,
                   core::Offset position) {
                const core::RenderNode* target = nullptr;
                bool inContent = false;
                for (const core::RenderNode* node : chain) {
                    if (node->key.rfind("menu-target:", 0) == 0) {
                        target = node;
                    }
                    if (node->key == "gallery-list") {
                        inContent = true;
                    }
                }
                if (target != nullptr) {
                    contextMenu_.open(shell_, position,
                                      targetContextMenuItems(target->key));
                    return true;
                }
                if (inContent) {
                    contextMenu_.open(shell_, position,
                                      contentContextMenuItems());
                    return true;
                }
                return false;
            });
        // Splitter：手动调节标记（此后断点不再覆盖；Reset pane layout
        // 复位并恢复断点跟随）。
        sidebarSplitter_.onOffsetChanged = [this](float) {
            if (!syncingSidebarBreakpoint_) {
                sidebarUserAdjusted_ = true;
            }
        };

        form_.registerField(
            "nickname",
            widgets::FormController::nonEmpty("Nickname is required"));
        form_.registerField(
            "email", widgets::FormController::minLength(
                         5, "Email must have at least 5 characters"));
    }

    // Collections 装配（collection-design §11.3）：数据模型 + 三控制器
    // attach。attach 已内置：行 handler 幂等注册、激活 sink、选择回调
    // → markDirty、区间选择的行序序列。
    void setupCollections() {
        // --- List：200 行资产清单（Extended 选择 + 激活回显） ---
        collectionList_.setItemCount(200);
        collectionList_.setSelectionMode(widgets::SelectionMode::Extended);
        collectionList_.setEnabledOf([](std::size_t index) { return index % 7 != 6; });
        collectionList_.setItemBuilder([this](std::size_t index) {
            char number[8];
            std::snprintf(number, sizeof(number), "%03zu", index);
            char size[24];
            std::snprintf(size, sizeof(size), "%.1f KB",
                          4.0F + static_cast<float>(index) * 1.5F);
            core::Widget name = core::makeText(
                std::string("asset-") + number + ".png",
                shell_.theme().typography.body);
            name.flex = 1.0F;
            name.textStyle.maxLines = 1;
            name.textStyle.overflow = core::TextOverflow::Ellipsis;
            core::StyleOverrides muted;
            muted.foreground = shell_.theme().colors.contentSecondary;
            core::Widget sizeText = core::withStyleOverrides(
                core::makeText(size), std::move(muted));
            auto icon = core::makeIcon(core::IconId::Document);
            icon.styleOverrides.foreground = shell_.theme().colors.contentSecondary;
            std::vector<core::Widget> cells{std::move(icon), std::move(name)};
            if (index % 5 == 0) {
                auto badge = core::makeContainer(core::makeText("NEW", shell_.theme().typography.caption));
                badge.color = shell_.theme().colors.accentContainer;
                badge.radius = core::CornerRadius::all(shell_.theme().metrics.controlRadius[shell_.theme().metrics.baseIndex]);
                badge.padding = core::EdgeInsets::symmetric(style::spaceToken(1), 0.0F);
                cells.push_back(std::move(badge));
            }
            cells.push_back(std::move(sizeText));
            return core::makeRow(
                std::move(cells),
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center,
                shell_.theme().metrics.controlGap[shell_.theme().metrics.baseIndex]);
        });
        collectionList_.onActivated = [this](const std::string& key) {
            lastActivatedKey_ = key;
            shell_.markDirty();
        };
        collectionList_.attach(shell_, "collection-list");
        collectionEmptyList_.attach(shell_, "collection-empty-list");

        // --- Tree：仓库目录骨架（键路径全局唯一；初始展开两层） ---
        collectionTreeModel_.setRoots({"tree:src", "tree:docs",
                                       "tree:tests", "tree:cmake",
                                       "tree:readme"});
        collectionTreeModel_.setEntries({
            {"tree:src", {"src", {"tree:src:core", "tree:src:widgets",
                                  "tree:src:app"}}},
            {"tree:src:core", {"core/", {"tree:core:widget",
                                         "tree:core:layout",
                                         "tree:core:painter"}}},
            {"tree:src:widgets", {"widgets/", {"tree:widget:list",
                                               "tree:widget:tree"}}},
            {"tree:src:app", {"app/", {"tree:app:shell"}}},
            {"tree:docs", {"docs/", {"tree:doc:plan",
                                     "tree:doc:visual",
                                     "tree:doc:collection"}}},
            {"tree:tests", {"tests/", {"tree:test:collection"}}},
            {"tree:cmake", {"cmake/", {"tree:cmake:deps"}}},
            {"tree:core:widget", {"widget.cpp", {}}, },
            {"tree:core:layout", {"layout.cpp", {}}, },
            {"tree:core:painter", {"painter.cpp", {}}, },
            {"tree:widget:list", {"list.cpp", {}}, },
            {"tree:widget:tree", {"tree.cpp", {}}, },
            {"tree:app:shell", {"app_shell.cpp", {}}, },
            {"tree:doc:plan", {"lumen-gui-framework-plan.md", {}}, },
            {"tree:doc:visual", {"lumen-visual-system-design.md", {}}, },
            {"tree:doc:collection", {"lumen-collection-controls-design.md", {}}, },
            {"tree:test:collection", {"collection_tests.cpp", {}}, },
            {"tree:cmake:deps", {"dependencies.cmake", {}}, },
            {"tree:readme", {"README.md", {}}, },
        });
        collectionTree_.setModel(&collectionTreeModel_);
        collectionTree_.setSelectionMode(widgets::SelectionMode::Single);
        collectionTree_.onActivated = [this](const std::string&) {
            shell_.markDirty();
        };
        collectionTree_.attach(shell_, "collection-tree");
        collectionEmptyTree_.attach(shell_, "collection-empty-tree");
        collectionTree_.expand("tree:src");
        collectionTree_.expand("tree:src:core");
        collectionTree_.selection().setSelected({"tree:core:widget"});

        // --- TreeList：依赖清单（列系统 + 排序钩子由应用执行） ---
        collectionTableModel_.setRows({
            {"dep-core", "lumen-core", "Static lib", 4200.0F},
            {"dep-widgets", "lumen-widgets", "Static lib", 1840.0F},
            {"dep-layout", "lumen-layout", "Static lib", 640.0F},
            {"dep-render", "lumen-render", "Static lib", 2100.0F},
            {"dep-text", "lumen-text", "Static lib", 980.0F},
            {"dep-style", "lumen-style", "Static lib", 312.0F},
            {"dep-sdl3", "SDL3", "Shared", 2600.0F},
            {"dep-catch2", "Catch2", "Interface", 1100.0F},
            {"dep-stb", "stb", "Interface", 768.0F},
        });
        collectionTable_.setModel(&collectionTableModel_);
        collectionTable_.setSelectionMode(widgets::SelectionMode::Single);
        collectionTable_.setColumns({
            {"name", "Name", 0.0F, 1.0F, 120.0F, true, true},
            {"type", "Type", 96.0F, 0.0F, 48.0F, true, false},
            {"size", "Size", 72.0F, 0.0F, 48.0F, true, true},
        });
        collectionTable_.setCellBuilder(
            [this](const std::string& rowKey, const std::string& columnId) {
                const auto* row = collectionTableModel_.rowOf(rowKey);
                if (row == nullptr) {
                    return core::makeText(rowKey);
                }
                if (columnId == "name") {
                    return core::makeText(row->name,
                                          shell_.theme().typography.body);
                }
                if (columnId == "type") {
                    core::StyleOverrides muted;
                    muted.foreground =
                        shell_.theme().colors.contentSecondary;
                    return core::withStyleOverrides(
                        core::makeText(row->type), std::move(muted));
                }
                char size[24];
                std::snprintf(size, sizeof(size), "%.1f KB", row->sizeKb);
                core::StyleOverrides muted;
                muted.foreground = shell_.theme().colors.contentSecondary;
                return core::withStyleOverrides(
                    core::makeText(size), std::move(muted));
            });
        collectionTable_.onHeaderClick =
            [this](const std::string& columnId, bool descending) {
                sortCollectionTable(columnId, descending);
            };
        collectionTable_.attach(shell_, "collection-table");
    }

    // TreeList 排序：框架只回调与记录指示器，行序由应用重排（key 稳定
    // → 选择随行保留）。
    void sortCollectionTable(const std::string& columnId, bool descending) {
        lastSortColumn_ = columnId;
        lastSortDescending_ = descending;
        auto& rows = collectionTableModel_.rows();
        std::sort(rows.begin(), rows.end(),
                  [columnId, descending](
                      const CollectionTableModel::Row& a,
                      const CollectionTableModel::Row& b) {
                      int cmp = 0;
                      if (columnId == "type") {
                          cmp = a.type.compare(b.type);
                      } else if (columnId == "size") {
                          cmp = a.sizeKb < b.sizeKb   ? -1
                                : a.sizeKb > b.sizeKb ? 1
                                                     : 0;
                      } else {
                          cmp = a.name.compare(b.name);
                      }
                      if (cmp == 0) {
                          cmp = a.id.compare(b.id);
                      }
                      return descending ? cmp > 0 : cmp < 0;
                  });
        collectionTable_.modelChanged();
        shell_.markDirty();
    }

    // M11：切换 v0.4 视觉方向（保留深浅/密度/可访问性派生）。
    void applyDirection(style::ThemeDirection direction) {
        direction_ = direction;
        shell_.setTheme(style::Theme::fromSettings(
            shell_.accessibilitySettings(), darkMode_,
            shell_.theme().metrics.density, direction_));
        refreshScopePreview();
        shell_.markDirty();
    }

    // 深浅切换统一入口：经 fromSettings 派生，保留方向/高对比/字体缩放/
    // 减少动画（直接用 Theme::dark/light 基线会丢弃派生）。
    void setDarkMode(bool dark) {
        if (darkMode_ == dark) {
            return;
        }
        darkMode_ = dark;
        shell_.setTheme(style::Theme::fromSettings(
            shell_.accessibilitySettings(), darkMode_,
            shell_.theme().metrics.density, direction_));
        refreshScopePreview();
        shell_.markDirty();
    }

    // M12：跟随系统主题（adaptPlatformTheme：深浅切换保留全部派生）。
    void applySystemTheme() {
        shell_.setTheme(style::adaptPlatformTheme(
            shell_.theme(), shell_.accessibilitySettings(),
            systemPrefersDark_, systemAccent_));
        darkMode_ = shell_.theme().darkMode;
    }

    // ThemeScope 预览跟随当前方向（浅色变体对比展示）。
    void refreshScopePreview() {
        themeScopeData_ = style::makeThemeScopeData(style::Theme::fromSettings(
            shell_.accessibilitySettings(), false, shell_.theme().metrics.density, direction_));
    }

    void go(const std::string& route) {
        if (navigator_.current() == route) {
            return;
        }
        // 路由栈保持单层：先回根再 push，保证 Back 恒回 home。
        navigator_.popToRoot();
        if (route != "home") {
            navigator_.push(route);
        }
        scroll_.scrollTo(0.0F);
        shell_.markDirty();
        focusRestorePending_ = true;
    }

    void pickColor(const std::string& name) {
        shell_.state().set("color", name);
        shell_.markDirty();
    }

    void applyAccent(core::Color accent) {
        // 强调色切换沿同一 semantic→component 链重建，确保 Button、
        // Checkbox、Switch 等所有依赖 accent 的组件保持一致，并保留
        // 当前方向、深浅与可访问性派生。
        shell_.setTheme(style::adaptPlatformTheme(
            shell_.theme(), shell_.accessibilitySettings(),
            shell_.theme().darkMode, accent));
        shell_.markDirty();
    }

    void openDialog() {
        if (!dialogOpen_) dialogReturnKey_ = shell_.focus().focusedKey();
        dialogOpen_ = true;
        dialogClosing_ = false;
        dialogScroll_.scrollTo(0);
        shell_.markDirty();
        if (shell_.motionEnabled()) shell_.beginDialogTransition(kDialogKey, true);
    }

    void closeDialog() {
        if (!dialogOpen_ || dialogClosing_) return;
        const auto finish = [this](app::AppShell& shell) {
            dialogOpen_ = false;
            dialogClosing_ = false;
            shell.markDirty();
            shell.requestFullRepaint();
            focusRestorePending_ = true;
        };
        if (shell_.motionEnabled()) {
            dialogClosing_ = true;
            shell_.beginDialogTransition(kDialogKey, false, finish);
        } else finish(shell_);
    }

    // M10：视口拖动滚动与惯性推进。VirtualList/List/Tree/TreeList（源视
    // 口）由框架直接驱动源控制器并推进惯性；这里只处理应用侧滚动状态。
    bool scrollDrag(const core::RenderNode* viewport, float deltaY,
                    core::ScrollDragPhase phase, std::uint64_t nowMs) {
        core::ScrollController* scroll = &scroll_;
        if (viewport && viewport->key == std::string(kDialogKey) + "-body-scroll") {
            scroll = &dialogScroll_;
        } else if (viewport && viewport->key == "gallery-scrollview") {
            scroll = &scrollViewScroll_;
        }
        switch (phase) {
            case core::ScrollDragPhase::Begin:
                scroll->cancelDrag();
                return true;
            case core::ScrollDragPhase::Update:
                if (viewport != nullptr) {
                    scroll->updateExtents(
                        viewport->size.height,
                        viewport->size.height + viewport->scrollExtent);
                }
                scroll->noteDragSample(deltaY, nowMs);
                if (scroll->applyDrag(deltaY)) {
                    shell_.markDirty();
                    return true;
                }
                return false;
            case core::ScrollDragPhase::End:
                if (scroll->endDrag(nowMs)) {
                    shell_.markDirty();
                    return true;
                }
                return false;
            case core::ScrollDragPhase::Cancel:
                if (viewport) scroll->cancelDrag();
                else {
                    scroll_.cancelDrag();
                    dialogScroll_.cancelDrag();
                    scrollViewScroll_.cancelDrag();
                }
                return false;
        }
        return false;
    }

    bool advanceFling(app::AppShell& shell, std::uint64_t nowMs) {
        // 源视口（library_/collections）的惯性由框架 advanceSourceFling
        // 推进（AppShell::tick 内）；这里只推进应用侧外层滚动。
        bool active = false;
        for (auto* scroll : {&scroll_, &dialogScroll_, &scrollViewScroll_}) {
            if (scroll->isFlinging()) {
                shell.markDirty();
                active = scroll->stepFling(nowMs) || active;
            }
        }
        return active;
    }

    bool scrollWheel(const core::RenderNode& root, const core::RenderNode* hit,
                     float deltaY) {
        if (hit && (hit->key == std::string(kDialogKey) + "-body-scroll" || hit->key == "gallery-scrollview")) {
            auto& scroll = hit->key == "gallery-scrollview" ? scrollViewScroll_ : dialogScroll_;
            scroll.updateExtents(hit->size.height, hit->size.height + hit->scrollExtent);
            const bool changed = scroll.applyWheel(deltaY);
            if (changed) shell_.markDirty();
            return changed;
        }

        const core::RenderNode* viewport = hit;
        if (viewport == nullptr) {
            viewport = core::findNodeByKey(root, "gallery-list");
        }
        if (viewport == nullptr) {
            viewport = core::findNodeByKey(root, "gallery-library");
        }
        if (viewport == nullptr || !core::isScrollableWidget(viewport->type)) {
            return false;
        }
        // VirtualList/List/Tree/TreeList（源视口）的滚轮已由框架直接驱
        // 动源控制器（interaction 拦截），这里只处理应用侧滚动状态：
        // 弹窗正文与外层 gallery-list。
        scroll_.updateExtents(viewport->size.height,
                              viewport->size.height +
                                  viewport->scrollExtent);
        if (std::abs(deltaY) > 1e8F) {
            scroll_.scrollTo(deltaY > 0 ? scroll_.maxScrollOffset() : 0.0F);
            shell_.markDirty();
            return true;
        }
        const bool changed = scroll_.applyWheel(deltaY);
        if (changed) {
            shell_.markDirty();
        }
        return changed;
    }

    // --- 页面骨架（design/gallery.html v1 Core Dark 壳层） ---

    // 自定义标题栏（lumen-titlebar-design §5；无边框窗口自绘 caption）：
    // 品牌 + MenuBar（chrome 用法，面板锚定栏项下方打开）+ 弹性拖拽区
    //（右侧状态胶囊）+ 窗口控制（44×32 贴合右上角，Windows 惯例）一条
    // 48px 行。整行标记 windowDrag：空白可拖动移窗、双击最大化由平台
    // hit-test 原生提供；菜单项/窗口按钮/紧凑导航下拉命中链更深，自然
    // 排除拖拽。≤1024 隐藏状态胶囊；≤720 折叠标题文字（留品牌标）。
    [[nodiscard]] core::Widget buildTitleBar(const style::Theme& theme) const {
        const float markSize = brandMarkStyle(theme).fontSize * 2.0F;
        core::Widget mark = core::makeRow(
            {core::makeText("L", brandMarkStyle(theme))},
            core::MainAxisAlignment::Center, core::CrossAxisAlignment::Center,
            0.0F, core::EdgeInsets{}, core::EdgeInsets{},
            "gallery-brand-mark", markSize, markSize);
        mark.color = theme.colors.accent;
        mark.radius = core::CornerRadius::all(markSize * 0.25F);
        core::Widget title = core::makeText("Lumen Widget Gallery",
                                            headerTitleStyle(theme));
        title.key = "gallery-titlebar-title";
        title.textStyle.maxLines = 1;
        title.textStyle.overflow = core::TextOverflow::Ellipsis;
        core::Widget brand = core::makeRow(
            {std::move(mark), std::move(title)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            10.0F, core::EdgeInsets::only(0.0F, 0.0F, 12.0F, 0.0F));
        if (compactNavigation()) {
            // ≤720：只留品牌标（设计稿折叠标题文字）。
            brand.children.pop_back();
        }
        brand.key = "gallery-brand";

        core::Widget menus =
            core::withKey(menuBar_.build(theme), "gallery-menubar");

        // 拖拽区：弹性空白（窗口拖动 hit-test 的主要落点），右端状态
        // 胶囊 + 视口尺寸（≥1024 显示）。
        std::vector<core::Widget> dragChildren;
        if (!narrowWindow()) {
            core::Widget pill = core::makeRow(
                {core::makeText("Desktop preview", statusPillStyle(theme))},
                core::MainAxisAlignment::Center,
                core::CrossAxisAlignment::Center, 0.0F,
                core::EdgeInsets::symmetric(8.0F, 4.0F), core::EdgeInsets{},
                "gallery-status-pill", std::nullopt,
                // 设计稿 24px（4px 上下内距 + 单行文字）；fontScale 放大
                // 时按行高推导，避免固定高度裁字。
                std::max(24.0F, statusPillStyle(theme).fontSize + 8.0F));
            pill.color = theme.colors.accentContainer;
            pill.radius = core::CornerRadius::all(12.0F);
            const core::Size view = shell_.view();
            core::Widget viewLabel = smallLabel(
                std::to_string(static_cast<int>(view.width)) + " × " +
                    std::to_string(static_cast<int>(view.height)),
                theme);
            core::Widget status = core::makeRow(
                {std::move(pill), std::move(viewLabel)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center, 8.0F);
            status.key = "gallery-status";
            dragChildren.push_back(std::move(status));
        }
        core::Widget drag = core::makeRow(
            std::move(dragChildren), core::MainAxisAlignment::End,
            core::CrossAxisAlignment::Center, 8.0F,
            core::EdgeInsets::only(0.0F, 0.0F, 8.0F, 0.0F));
        drag.key = "gallery-titlebar-drag";
        drag.flex = 1.0F;

        core::Widget actions = core::makeRow(
            {windowButton(core::IconId::Minus, "window-minimize", theme),
             windowButton(windowMaximized_ ? core::IconId::Restore
                                           : core::IconId::Maximize,
                          "window-maximize", theme),
             windowButton(core::IconId::Close, "window-close", theme,
                          core::ButtonVariant::WindowClose)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
            0.0F);
        actions.key = "gallery-window-actions";

        // 内容行（品牌/菜单/拖拽区）垂直居中；窗口控制独立在水平
        // padding 之外——通高贴合右上角（design/gallery.html titlebar：
        // align-items:stretch、右缘无 padding、caption 48px 一条行）。
        const float barPaddingX = compactNavigation() ? 12.0F : 16.0F;
        core::Widget content = core::makeRow(
            {std::move(brand), std::move(menus), std::move(drag)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            8.0F, core::EdgeInsets::only(barPaddingX, 0.0F, 0.0F, 0.0F));
        content.flex = 1.0F;
        core::Widget row = core::makeRow(
            {std::move(content), std::move(actions)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
            0.0F);
        // 行高 47 + 1px 分隔线 = 总高 48（design/gallery.html titlebar
        // min-height 48 为 border-box：内容 47 + 底边线 1）。窗口控制
        // 随 Stretch 通高贴合右上角。
        row.height = 47.0F * (theme.typography.body.fontSize / 14.0F);
        row = core::withWindowDrag(std::move(row));
        row.key = "gallery-titlebar-row";
        core::Widget bottom = core::makeContainerLeaf(
            std::nullopt, 1.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.borderDefault, "gallery-titlebar-divider");
        std::vector<core::Widget> barRows;
        barRows.push_back(std::move(row));
        // 紧凑路由下拉：窄窗口自动收起侧栏，或侧栏被 View 菜单手动隐藏
        //（否则指针端无导航入口）。位于标题栏下（自身非拖拽区）。
        if (compactNavigation() || !sidebarVisible_) {
            auto navigation = core::makeDropdown(routeDisplayName(navigator_.current()),
                "open-navigation", "compact-navigation");
            barRows.push_back(core::makeContainer(std::move(navigation), std::nullopt,
                std::nullopt, core::EdgeInsets::only(16, 0, 16, 12)));
        }
        barRows.push_back(std::move(bottom));
        core::Widget titleBar = core::makeColumn(
            std::move(barRows), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 0.0F);
        core::Widget bar = core::makeContainer(std::move(titleBar),
                                               std::nullopt, std::nullopt,
                                               core::EdgeInsets{},
                                               core::EdgeInsets{},
                                               theme.colors.surface);
        // 透明窗口圆角（design/gallery.html gallery-window:16px，
        // is-maximized 归零）：标题栏负责顶角（surface 底自绘圆角），
        // 根容器负责四角兜底与底角。
        bar.radius = windowMaximized_
                         ? core::CornerRadius::zero()
                         : core::CornerRadius{kWindowRadius, kWindowRadius,
                                              0.0F, 0.0F};
        return core::withKey(std::move(bar), "gallery-titlebar");
    }

    // 侧栏：分区标签 + 路由导航（当前项 Tonal 强调）+ 分隔线 + Live state
    // 注记（设计稿虚线边框以实线近似）。背景即页面底色。宽 200px，
    // ≤1024 收窄 168px（设计稿容器断点）。
    [[nodiscard]] core::Widget buildNav(const style::Theme& theme) const {
        const std::pair<const char*, const char*> sections[] = {
            {"Overview", "home"}, {"Buttons", "buttons"},
            {"Inputs", "inputs"},   {"Layout", "layout"},
            {"Lists", "lists"},     {"Collections", "collections"},
            {"Menus", "menus"},     {"Controls", "controls"},
            {"Feedback", "feedback"},
            {"Theme", "theme"},
        };
        std::vector<core::Widget> navItems;
        for (const auto& [label, route] : sections) {
            const bool active = navigator_.current() == route;
            core::Widget button = buttonWidget(
                label, std::string("goto-") + route,
                std::string("nav-") + route,
                active ? core::ButtonVariant::Tonal
                       : core::ButtonVariant::Ghost);
            if (active) {
                button = core::withSelected(std::move(button), true);
            }
            navItems.push_back(
                core::withKey(std::move(button), std::string("nav-") + route));
        }
        core::Widget navList = core::makeColumn(
            std::move(navItems), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 4.0F);
        navList.key = "gallery-nav-list";

        std::vector<core::Widget> items;
        core::Widget caption = smallCapsLabel("SECTIONS", theme);
        caption.margin = core::EdgeInsets::only(12.0F, 0.0F, 12.0F, 12.0F);
        items.push_back(core::withKey(std::move(caption), "nav-caption"));
        items.push_back(std::move(navList));
        core::Widget divider = core::makeContainerLeaf(
            std::nullopt, 1.0F, core::EdgeInsets{},
            core::EdgeInsets::symmetric(12.0F, 24.0F),
            theme.colors.borderDefault, "nav-divider");
        items.push_back(std::move(divider));
        items.push_back(liveStateNote(theme));

        core::Widget column = core::makeColumn(
            std::move(items), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 0.0F,
            core::EdgeInsets::only(12.0F, 24.0F, 12.0F, 24.0F));
        column.key = "gallery-nav-column";
        core::Widget nav = core::makeContainer(
            std::move(column), sidebarWidth(), std::nullopt, core::EdgeInsets{},
            core::EdgeInsets{}, theme.colors.pageBackground);
        nav.key = "gallery-nav";
        return nav;
    }

    [[nodiscard]] core::Widget buildContent(const style::Theme& theme) {
        std::vector<core::Widget> items;
        const std::string& route = navigator_.current();
        if (route == "buttons") {
            items = buildButtonsItems(theme);
        } else if (route == "inputs") {
            items = buildInputsItems(theme);
        } else if (route == "layout") {
            items = buildLayoutItems(theme);
        } else if (route == "lists") {
            items = buildListsItems(theme);
        } else if (route == "collections") {
            items = buildCollectionsItems(theme);
        } else if (route == "menus") {
            items = buildMenusItems(theme);
        } else if (route == "controls") {
            items = buildControlsItems(theme);
        } else if (route == "feedback") {
            items = buildFeedbackItems(theme);
        } else if (route == "theme") {
            items = buildThemeItems(theme);
        } else {
            items = buildHomeItems(theme);
        }
        appendControlMatrices(items, route, theme);
        core::Widget column = core::makeColumn(
            std::move(items), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Start, style::spaceToken(4),
            core::EdgeInsets::all(contentPadding()));
        column.flex = 1.0F;
        core::Widget list = core::makeListView(std::move(column),
                                               "gallery-list");
        list.flex = 1.0F;
        // 滚动条常显：演示 ScrollbarTokens（厚度/颜色来自 Theme）。
        list = core::withScrollbar(std::move(list), true);
        return core::withKey(
            core::withScrollOffset(std::move(list), scroll_.offset()),
            "gallery-list");
    }

    // 页脚：渲染器状态（左）+ 路由/主题/下拉状态（右）。
    // 设计稿 ≤720 折叠为单行状态（右侧路由信息隐藏）。
    [[nodiscard]] core::Widget buildFooter(const style::Theme& theme) const {
        if (compactNavigation()) {
            auto label = smallLabel("CPU · " + routeDisplayName(navigator_.current()) +
                " · " + (darkMode_ ? "Dark" : "Light"), theme);
            label.key = "gallery-footer-label";
            auto footer = core::makeContainer(std::move(label), std::nullopt, std::nullopt,
                core::EdgeInsets::symmetric(16.0F, 12.0F));
            footer.key = "gallery-footer";
            return footer;
        }
        core::Widget dot = core::makeContainerLeaf(
            6.0F, 6.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.statusSuccess, "footer-dot");
        dot.radius = core::CornerRadius::all(3.0F);
        core::Widget left = core::makeRow(
            {std::move(dot),
             smallLabel("Renderer ready · CPU fallback available", theme)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            7.0F);
        left.key = "gallery-footer-status";
        std::string status = "Route: " + routeDisplayName(navigator_.current()) +
                             " | Theme: " + (darkMode_ ? "dark" : "light") +
                             " | Dropdown: " +
                             (dropdown_.isOpen() ? "open" : "closed");
        core::Widget right = smallLabel(std::move(status), theme);
        right.key = "gallery-footer-label";
        core::Widget spacer;
        spacer.flex = 1.0F;
        core::Widget top = core::makeContainerLeaf(
            std::nullopt, 1.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.borderDefault, "gallery-footer-divider");
        core::Widget row = core::makeRow(
            {std::move(left), std::move(spacer), std::move(right)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            style::spaceToken(3),
            core::EdgeInsets::symmetric(style::spaceToken(5), 12.0F));
        row.key = "gallery-footer-row";
        core::Widget footer = core::makeColumn(
            {std::move(top), std::move(row)}, core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 0.0F);
        return core::withKey(
            core::makeContainer(std::move(footer), std::nullopt, std::nullopt,
                                core::EdgeInsets{}, core::EdgeInsets{},
                                theme.colors.pageBackground),
            "gallery-footer");
    }

    // --- 各分区内容 ---

    // Overview 首屏（design v1 Core Dark）：kicker/hero/主操作 + 指标卡三联
    // + 双栏面板（Control inventory / DSL 快照 | Resolved tokens / Theme
    // controls）。全部为真控件：点击与 StateStore 联动。主内容 <720 上下
    // 堆叠、<480 指标单列（设计稿 content 容器断点）。
    [[nodiscard]] std::vector<core::Widget> buildHomeItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        const bool stackColumns = contentColumnWidth() < 720.0F;
        const bool narrowMetrics = contentColumnWidth() < 480.0F;

        // 内容头：kicker + hero + 副文案 | 主操作（+ Show dialog）。
        core::Widget primary = core::withIcon(
            buttonWidget("Show dialog", "show-dialog", "show-dialog-button",
                         core::ButtonVariant::Filled),
            core::IconId::Plus);
        core::Widget hero = narrowMetrics
            ? core::makeText("Build a clear UI language",
                             heroStyle(24.0F, theme))
            : heroText("Build a clear UI language", theme);
        core::Widget headText = core::makeColumn(
            {core::withKey(
                 kickerText("WIDGET SYSTEM / " +
                                toUpper(routeDisplayName(
                                    navigator_.current())),
                            theme),
                 "home-kicker"),
             core::withKey(std::move(hero), "home-hero"),
             core::withKey(
                 heroDescription(
                     "Every control below is live. The same StateStore, "
                     "layout and paint pipeline powers the desktop sample.",
                     theme),
                 "home-desc")},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Start,
            8.0F);
        headText.flex = 1.0F;
        core::Widget head =
            stackColumns
                ? core::makeColumn({std::move(headText), std::move(primary)},
                                   core::MainAxisAlignment::Start,
                                   core::CrossAxisAlignment::Start, 16.0F)
                : core::makeRow({std::move(headText), std::move(primary)},
                                core::MainAxisAlignment::Start,
                                core::CrossAxisAlignment::Center,
                                style::spaceToken(6));
        items.push_back(core::withKey(std::move(head), "home-head"));

        // 指标卡三联：点击计数 / 物化窗口 / 主题与密度。
        std::vector<core::Widget> metrics;
        metrics.push_back(metricCard("Button clicks",
                                     shell_.state().get("button-clicks"),
                                     "+12% this session", theme,
                                     "metric-clicks", true, narrowMetrics));
        metrics.push_back(metricCard("Visible nodes", "1,000",
                                     "VirtualList ready", theme,
                                     "metric-nodes", false, narrowMetrics));
        metrics.push_back(metricCard("Theme", darkMode_ ? "Dark" : "Light",
                                     densityName(theme.metrics.density) +
                                         " density",
                                     theme, "metric-theme", false,
                                     narrowMetrics));
        core::Widget metricGrid =
            narrowMetrics
                ? core::makeColumn(std::move(metrics),
                                   core::MainAxisAlignment::Start,
                                   core::CrossAxisAlignment::Stretch, 8.0F)
                : core::makeRow(std::move(metrics),
                                core::MainAxisAlignment::Start,
                                core::CrossAxisAlignment::Stretch,
                                style::spaceToken(3));
        items.push_back(core::withKey(std::move(metricGrid), "metric-grid"));

        // 双栏：左 = 控件清单 + DSL 快照；右 = 语义 token + 主题控制。
        // 间距 16、比例 1.4 : 1（设计稿 content-grid）；窄内容上下堆叠。
        core::Widget contentGrid;
        if (stackColumns) {
            contentGrid = core::makeColumn(
                {inventoryPanel(theme), codePanel(theme), tokensPanel(theme),
                 themeControlsPanel(theme)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Stretch, style::spaceToken(4));
        } else {
            core::Widget left = core::makeColumn(
                {inventoryPanel(theme), codePanel(theme)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Stretch, style::spaceToken(4));
            left.flex = 1.4F;
            core::Widget right = core::makeColumn(
                {tokensPanel(theme), themeControlsPanel(theme)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Stretch, style::spaceToken(4));
            right.flex = 1.0F;
            contentGrid = core::makeRow({std::move(left), std::move(right)},
                                        core::MainAxisAlignment::Start,
                                        core::CrossAxisAlignment::Stretch,
                                        style::spaceToken(4));
        }
        items.push_back(
            core::withKey(std::move(contentGrid), "home-content-grid"));
        return items;
    }

    // Control inventory：分区瓷砖（预览即真控件，点击整格跳转分区）。
    [[nodiscard]] core::Widget inventoryPanel(
        const style::Theme& theme) const {
        std::vector<core::Widget> tiles;
        tiles.push_back(buttonsTile(theme));
        tiles.push_back(inputsTile(theme));
        tiles.push_back(togglesTile(theme));
        tiles.push_back(layoutTile(theme));
        tiles.push_back(splitterTile(theme));
        tiles.push_back(listsTile(theme));
        tiles.push_back(collectionsTile(theme));
        tiles.push_back(menusTile(theme));
        tiles.push_back(feedbackTile(theme));
        std::vector<core::Widget> body;
        body.push_back(panelHead("Control inventory", "8 sections", theme));
        // 组件卡最小 176px、间距 12（设计稿 auto-fit 网格）：保证瓦片内
        // 迷你控件（双按钮/输入框）不被压缩裁字。
        body.push_back(core::withKey(
            core::makeGrid(std::move(tiles), 0, 176.0F, 12.0F, 12.0F,
                           "inventory-grid"),
            "inventory-grid"));
        return panelCard(std::move(body), theme, "inventory-panel");
    }

    // C++ DSL 快照（设计稿只读代码面板；行号 + 代码为静态展示）。
    [[nodiscard]] core::Widget codePanel(const style::Theme& theme) const {
        static const char* kLines[] = {
            "auto gallery = makeColumn({",
            "  menuBar({file, view, help}),",
            "  splitter(sidebar, mainPane),",
            "  button(\"Filled\", variant::filled),",
            "  textField(\"username\"),",
            "  virtualList(1000),",
            "});",
        };
        std::vector<core::Widget> rows;
        for (std::size_t i = 0;
             i < sizeof(kLines) / sizeof(kLines[0]); ++i) {
            core::Widget number = codeText(std::to_string(i + 1), theme,
                                            theme.colors.contentSecondary);
            number.width = 20.0F;
            rows.push_back(core::withKey(
                core::makeRow({std::move(number),
                               codeText(kLines[i], theme,
                                        theme.colors.contentPrimary)},
                              core::MainAxisAlignment::Start,
                              core::CrossAxisAlignment::Center, 12.0F),
                "code-line-" + std::to_string(i + 1)));
        }
        std::vector<core::Widget> body;
        body.push_back(panelHead("C++ DSL snapshot", "read-only", theme));
        body.push_back(core::withKey(
            core::makeColumn(std::move(rows), core::MainAxisAlignment::Start,
                             core::CrossAxisAlignment::Start, 4.0F),
            "code-lines"));
        return panelCard(std::move(body), theme, "code-panel");
    }

    // Resolved tokens：当前 Theme 的语义色实况（切方向/强调色即联动）。
    [[nodiscard]] core::Widget tokensPanel(const style::Theme& theme) const {
        const std::tuple<core::Color, std::string, std::string> tokens[] = {
            {theme.colors.pageBackground, "pageBackground",
             hexColor(theme.colors.pageBackground)},
            {theme.colors.accent, "accent", hexColor(theme.colors.accent)},
            {theme.colors.statusSuccess, "status.success",
             hexColor(theme.colors.statusSuccess)},
            {theme.colors.borderStrong, "border.strong", "1 px"},
        };
        std::vector<core::Widget> rows;
        for (const auto& [color, name, value] : tokens) {
            if (!rows.empty()) {
                rows.push_back(core::makeContainerLeaf(
                    std::nullopt, 1.0F, core::EdgeInsets{},
                    core::EdgeInsets{}, theme.colors.borderDefault, ""));
            }
            // 色板 12×12 / radius 3 + line-strong 描边（设计稿 token-swatch）。
            core::Widget swatch = core::makeContainerLeaf(
                12.0F, 12.0F, core::EdgeInsets{}, core::EdgeInsets{}, color,
                "token-swatch-" + name);
            swatch.radius = core::CornerRadius::all(3.0F);
            core::StyleOverrides swatchBorder;
            swatchBorder.border = theme.colors.borderStrong;
            swatchBorder.borderWidth = 1.0F;
            core::Widget nameLabel = smallLabel(name, theme);
            nameLabel.flex = 1.0F;
            rows.push_back(core::withKey(
                core::makeRow(
                    {core::withStyleOverrides(std::move(swatch),
                                              std::move(swatchBorder)),
                     std::move(nameLabel),
                     codeText(value, theme,
                              theme.colors.contentSecondary)},
                    core::MainAxisAlignment::Start,
                    core::CrossAxisAlignment::Center, 8.0F,
                    core::EdgeInsets::symmetric(0.0F, 12.0F)),
                "token-row-" + name));
        }
        std::vector<core::Widget> body;
        body.push_back(panelHead("Resolved tokens", "Current palette", theme));
        body.push_back(core::withKey(
            core::makeColumn(std::move(rows), core::MainAxisAlignment::Start,
                             core::CrossAxisAlignment::Start, 0.0F),
            "token-rows"));
        return panelCard(std::move(body), theme, "token-panel");
    }

    // Theme controls：深浅/高对比快捷开关 + 主题色点。
    [[nodiscard]] core::Widget themeControlsPanel(
        const style::Theme& theme) const {
        core::Widget dark = buttonWidget("Dark", "set-dark",
                                         "home-toggle-dark",
                                         darkMode_
                                             ? core::ButtonVariant::Filled
                                             : core::ButtonVariant::Outline);
        core::Widget light = buttonWidget("Light", "set-light",
                                          "home-toggle-light",
                                          !darkMode_
                                              ? core::ButtonVariant::Filled
                                              : core::ButtonVariant::Outline);
        core::Widget contrast =
            buttonWidget("High contrast", "toggle-contrast",
                         "home-toggle-contrast",
                         core::ButtonVariant::Ghost);
        core::Widget controls = core::makeRow(
            {std::move(dark), std::move(light), std::move(contrast)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            style::spaceToken(2));
        controls.key = "theme-controls-row";
        core::Widget bgDot = core::makeContainerLeaf(
            24.0F, 24.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.pageBackground, "theme-dot-bg");
        bgDot.radius = core::CornerRadius::all(12.0F);
        core::StyleOverrides bgDotBorder;
        bgDotBorder.border = theme.colors.borderStrong;
        bgDotBorder.borderWidth = 1.0F;
        core::Widget accentDot = core::makeContainerLeaf(
            24.0F, 24.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.accent, "theme-dot-accent");
        accentDot.radius = core::CornerRadius::all(12.0F);
        core::Widget successDot = core::makeContainerLeaf(
            24.0F, 24.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.statusSuccess, "theme-dot-success");
        successDot.radius = core::CornerRadius::all(12.0F);
        core::Widget dots = core::makeRow(
            {core::withStyleOverrides(std::move(bgDot),
                                      std::move(bgDotBorder)),
             std::move(accentDot), std::move(successDot)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            8.0F);
        dots.key = "theme-dots";
        std::vector<core::Widget> body;
        body.push_back(panelHead("Theme controls", "interactive", theme));
        body.push_back(core::withKey(std::move(controls),
                                     "theme-controls-row"));
        body.push_back(core::withKey(std::move(dots), "theme-dots"));
        return panelCard(std::move(body), theme, "theme-controls-panel");
    }

    [[nodiscard]] core::Widget buttonsTile(const style::Theme& theme) const {
        // Filled = 分区入口（语义 Button）；Outline = 计数器直连。
        core::Widget filled = core::withControlSize(
            buttonWidget("Filled", "goto-buttons", "goto-buttons-button",
                         core::ButtonVariant::Filled),
            core::ControlSize::Small);
        core::Widget outline = core::withControlSize(
            buttonWidget("Outline", "bump-clicks", "btn-tile-outline",
                         core::ButtonVariant::Outline),
            core::ControlSize::Small);
        return tileShell(core::makeRow({core::withKey(std::move(filled),
                                                      "goto-buttons-button"),
                                        core::withKey(std::move(outline),
                                                      "btn-tile-outline")},
                                       core::MainAxisAlignment::Start,
                                       core::CrossAxisAlignment::Center,
                                       style::spaceToken(2)),
                         "Buttons", theme, "tile-buttons", "goto-buttons");
    }

    [[nodiscard]] core::Widget inputsTile(const style::Theme& theme) const {
        core::Widget field = core::makeTextField("", "TextField",
                                                 core::TextStyle{},
                                                 core::EdgeInsets{}, 0.0F,
                                                 "tile-input-field");
        field.bind = "search";
        field = core::withControlSize(std::move(field),
                                      core::ControlSize::Small);
        field.flex = 1.0F;
        return tileShell(
            core::makeRow({core::withKey(std::move(field), "tile-input-field")},
                          core::MainAxisAlignment::Start,
                          core::CrossAxisAlignment::Center, 0.0F),
            "Inputs", theme, "tile-inputs", "goto-inputs");
    }

    [[nodiscard]] core::Widget togglesTile(const style::Theme& theme) const {
        // 同一 bind 的 Checkbox/Switch 对（默认 true = 设计稿选中态）；
        // 紧凑样本（16px 勾选框 / 32×18 开关）。
        core::Widget check = core::makeCheckbox("", "notifications",
                                                "tile-notifications");
        check.semanticsLabel = "Preview notifications";
        check = core::withControlSize(std::move(check),
                                      core::ControlSize::Small);
        core::Widget toggle = core::makeSwitch("", "notifications",
                                               "tile-notifications-switch");
        toggle.semanticsLabel = "Preview notifications";
        toggle = core::withControlSize(std::move(toggle),
                                       core::ControlSize::Small);
        return tileShell(
            core::makeRow({core::withKey(std::move(check),
                                         "tile-notifications"),
                           core::withKey(std::move(toggle),
                                         "tile-notifications-switch")},
                          core::MainAxisAlignment::Start,
                          core::CrossAxisAlignment::Center,
                          style::spaceToken(2)),
            "Toggles", theme, "tile-toggles", "goto-inputs");
    }

    [[nodiscard]] core::Widget layoutTile(const style::Theme& theme) const {
        auto bar = [&theme](core::Color color, const std::string& key,
                            bool bordered) {
            core::Widget leaf = core::makeContainerLeaf(
                std::nullopt, 32.0F, core::EdgeInsets{}, core::EdgeInsets{},
                color, key);
            leaf.flex = 1.0F;
            leaf.radius = core::CornerRadius::all(4.0F);
            if (bordered) {
                // 设计稿第三根条：surface-alt 底 + line-strong 描边。
                core::StyleOverrides overrides;
                overrides.border = theme.colors.borderStrong;
                overrides.borderWidth = 1.0F;
                return core::withKey(
                    core::withStyleOverrides(std::move(leaf),
                                             std::move(overrides)),
                    key);
            }
            return core::withKey(std::move(leaf), key);
        };
        return tileShell(
            core::makeRow({bar(theme.colors.accentContainer, "tile-layout-a",
                               false),
                           bar(theme.colors.accent, "tile-layout-b", false),
                           bar(theme.colors.surfaceElevated, "tile-layout-c",
                               true)},
                          core::MainAxisAlignment::Start,
                          core::CrossAxisAlignment::Center, 4.0F),
            "Layout", theme, "tile-layout", "goto-layout");
    }

    // Splitter 瓦片：迷你分栏预览（两窗格 + 分隔条 active 态）。
    [[nodiscard]] core::Widget splitterTile(
        const style::Theme& theme) const {
        auto pane = [&theme](const std::string& key, bool bordered) {
            core::Widget leaf = core::makeContainerLeaf(
                std::nullopt, 32.0F, core::EdgeInsets{}, core::EdgeInsets{},
                theme.colors.accentContainer, key);
            leaf.flex = 1.0F;
            leaf.radius = core::CornerRadius::all(4.0F);
            if (bordered) {
                core::StyleOverrides overrides;
                overrides.border = theme.colors.borderStrong;
                overrides.borderWidth = 1.0F;
                return core::withStyleOverrides(std::move(leaf),
                                                 std::move(overrides));
            }
            return leaf;
        };
        // 分隔条：3px accent（hover/drag 视觉态；与真控件 active 线同宽）。
        core::Widget divider = core::makeContainerLeaf(
            3.0F, std::nullopt, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.accent, "tile-split-divider");
        return tileShell(
            core::makeRow(
                {pane("tile-split-leading", false),
                 core::withKey(std::move(divider), "tile-split-divider"),
                 pane("tile-split-trailing", true)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center, 5.0F),
            "Splitter", theme, "tile-splitter", "goto-layout");
    }

    [[nodiscard]] core::Widget listsTile(const style::Theme& theme) const {
        auto row = [&theme](const char* number, const std::string& key) {
            core::Widget dot = core::makeContainerLeaf(
                6.0F, 6.0F, core::EdgeInsets{}, core::EdgeInsets{},
                theme.colors.accent, key + "-dot");
            dot.radius = core::CornerRadius::all(3.0F);
            return core::makeRow(
                {core::withKey(std::move(dot), key + "-dot"),
                 smallLabel("Virtual row " + std::string(number), theme)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center, 8.0F);
        };
        return tileShell(
            core::makeColumn(
                {core::withKey(row("001", "tile-list-row-1"), "tile-list-row-1"),
                 core::withKey(row("002", "tile-list-row-2"), "tile-list-row-2")},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, 4.0F),
            "Lists", theme, "tile-lists", "goto-lists");
    }

    // Collections 瓦片：迷你树（分支行 + 缩进 + Tonal 选中行）。
    [[nodiscard]] core::Widget collectionsTile(
        const style::Theme& theme) const {
        auto branchRow = [&theme](core::IconId icon, const std::string& label,
                                  float indent, const std::string& key) {
            core::Widget chevron = core::makeIcon(
                icon, key + "-chevron");
            chevron =
                core::withControlSize(std::move(chevron),
                                      core::ControlSize::Small);
            core::Widget row = core::makeRow(
                {std::move(chevron), smallStrong(label, theme)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center, 4.0F,
                core::EdgeInsets::only(indent, 0.0F, 0.0F, 0.0F));
            return core::withKey(std::move(row), key);
        };
        core::Widget selected = buttonWidget(
            "widget.cpp", "noop", "tile-collection-selected",
            core::ButtonVariant::Tonal);
        selected = core::withControlSize(std::move(selected),
                                          core::ControlSize::Small);
        selected.alignContentStart = true;
        selected.padding = core::EdgeInsets::only(24.0F, 0.0F, 8.0F, 0.0F);
        core::Widget preview = core::makeColumn(
            {branchRow(core::IconId::ChevronDown, "src", 0.0F,
                       "tile-collection-src"),
             core::withKey(std::move(selected), "tile-collection-selected"),
             branchRow(core::IconId::ChevronRight, "layout", 8.0F,
                       "tile-collection-layout")},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Start,
            3.0F);
        return tileShell(std::move(preview), "Collections", theme,
                         "tile-collections", "goto-collections");
    }

    // Menus 瓦片：迷你菜单面板（label + 快捷键展示列 + 禁用行）。
    [[nodiscard]] core::Widget menusTile(const style::Theme& theme) const {
        auto itemRow = [&theme](const std::string& label,
                                const std::string& shortcut,
                                const std::string& key, bool disabled) {
            // 行文本与 smallLabel 同排版（12px/500）；禁用行仅换 disabled 色。
            core::TextStyle style = scaledStyle(12.0F, 500, theme);
            style.color = disabled ? theme.colors.disabledContent
                                   : theme.colors.contentSecondary;
            core::Widget text = core::makeText(label, style);
            text.flex = 1.0F;
            return core::withKey(
                core::makeRow(
                    {std::move(text),
                     codeText(shortcut, theme,
                              disabled ? theme.colors.disabledContent
                                       : theme.colors.contentSecondary)},
                    core::MainAxisAlignment::Start,
                    core::CrossAxisAlignment::Center, 8.0F,
                    core::EdgeInsets::symmetric(7.0F, 0.0F)),
                key);
        };
        core::Widget panel = core::makeColumn(
            {itemRow("Open", "Enter", "tile-menu-open", false),
             itemRow("Copy path", "Ctrl+C", "tile-menu-copy", false),
             itemRow("Paste", "Ctrl+V", "tile-menu-paste", true)},
            core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 2.0F,
            core::EdgeInsets::all(3.0F));
        core::StyleOverrides overrides;
        overrides.background = theme.colors.surfaceElevated;
        overrides.border = theme.colors.borderDefault;
        overrides.borderWidth = 1.0F;
        overrides.radius = core::CornerRadius::all(6.0F);
        return tileShell(
            core::withStyleOverrides(std::move(panel), std::move(overrides)),
            "Menus", theme, "tile-menus", "goto-menus");
    }

    [[nodiscard]] core::Widget feedbackTile(const style::Theme& theme) const {
        core::Widget bar = core::makeProgressBar(
            shell_.state().get("demo-progress"), "tile-progress-bar");
        bar.bind = "demo-progress";
        bar = core::withControlSize(std::move(bar),
                                    core::ControlSize::Small);
        core::Widget label = smallLabel("Progress", theme);
        label.flex = 1.0F;
        core::Widget value = smallStrong(
            shell_.state().get("demo-progress") + "%", theme);
        core::Widget header = core::makeRow(
            {std::move(label), std::move(value)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            8.0F);
        return tileShell(
            core::makeColumn({core::withKey(std::move(header),
                                            "tile-progress-label"),
                              core::withKey(std::move(bar),
                                            "tile-progress-bar")},
                             core::MainAxisAlignment::Start,
                             core::CrossAxisAlignment::Start, 8.0F),
            "Feedback", theme, "tile-feedback", "goto-feedback");
    }

    [[nodiscard]] static style::WidgetState previewState(const std::string& name) {
        style::WidgetState state;
        state.hovered = name == "Hover";
        state.pressed = name == "Press" || name == "Foc+Prs";
        state.focused = name == "Focus" || name.find("Focused") != std::string::npos || name == "Foc+Prs";
        state.disabled = name.find("Disabled") != std::string::npos;
        state.checked = name.find("Checked") != std::string::npos;
        state.invalid = name.find("Invalid") != std::string::npos;
        state.selected = name.find("Selected") != std::string::npos;
        return state;
    }

    void initializeVisualPreviews() {
        for (const auto* name : {"Normal", "Hover", "Press", "Focus", "Selected",
                                 "Selected+Focused", "Disabled", "Disabled+Selected"}) {
            shell_.setVisualPreviewState(std::string("tree-preview-") + name, previewState(name));
        }
        for (const auto* name : {"Normal", "Hover", "Press", "Focus", "Selected",
                                 "Selected+Focused", "Disabled"}) {
            shell_.setVisualPreviewState(std::string("list-preview-") + name, previewState(name));
        }
        for (const auto* variant : {"Filled", "Tonal", "Outline", "Ghost", "Danger"}) {
            for (const auto* name : {"Normal", "Hover", "Press", "Focus", "Foc+Prs", "Disabled"}) {
                shell_.setVisualPreviewState(std::string("matrix-") + variant + "-" + name, previewState(name));
            }
        }
        for (const auto* control : {"Text", "Icon", "TextField", "Checkbox", "Switch", "Radio",
                                   "Dropdown", "Tabs", "Slider", "ProgressBar", "Tooltip", "Image"}) {
            for (const auto* name : {"Normal", "Hover", "Press", "Focus", "Disabled", "Checked",
                                    "Checked+Disabled", "Checked+Focused", "Selected+Focused", "Invalid",
                                    "Invalid+Focused", "ReadOnly+Focused", "Small", "Medium", "Large",
                                    "Zero", "Full"}) {
                const auto key = std::string("sample-") + control + "-" + name;
                shell_.setVisualPreviewState(key, previewState(name));
                shell_.setVisualPreviewState(key + "-tab", previewState(name));
                shell_.setVisualPreviewState(key + "-other", {});
            }
        }
    }

    [[nodiscard]] core::Widget controlMatrix(const std::string& name, core::Widget prototype,
                                             const std::string& builder, const style::Theme& theme,
                                             bool interactive) const {
        std::vector<std::string> states{"Normal"};
        if (interactive) states.insert(states.end(), {"Hover", "Press", "Focus", "Disabled"});
        if (name == "Checkbox" || name == "Switch" || name == "Radio") {
            states.insert(states.end(), {"Checked", "Checked+Disabled", "Checked+Focused"});
        }
        if (name == "TextField") states.insert(states.end(), {"Invalid", "Invalid+Focused", "ReadOnly+Focused"});
        if (name == "Tabs" || name == "Dropdown") states.push_back("Selected+Focused");
        if (name == "Slider" || name == "ProgressBar") states.insert(states.end(), {"Zero", "Full"});
        if (name != "Tooltip" && name != "Text" && name != "Image") {
            states.insert(states.end(), {"Small", "Medium", "Large"});
        }
        std::vector<core::Widget> cells;
        for (const auto& state : states) {
            auto cell = prototype;
            const auto snapshot = previewState(state);
            // State matrix explicitly demonstrates Focus states: opt in to the ring.
            cell.showFocusRing = snapshot.focused;
            cell.key = "sample-" + name + "-" + state;
            cell.enabled = false;
            cell.bind.clear();
            cell.onClick.clear();
            cell.checked = snapshot.checked;
            cell.selected = snapshot.selected;
            cell.invalid = snapshot.invalid;
            cell.readOnly = state == "ReadOnly+Focused";
            if (state == "Small") cell.controlSize = core::ControlSize::Small;
            if (state == "Large") cell.controlSize = core::ControlSize::Large;
            if (state == "Zero") cell.text = "0";
            if (state == "Full") cell.text = "100";
            if (name == "Tabs") {
                cell.children.front().key = cell.key + "-tab";
                cell.children.front().selected = true;
                cell.children.back().key = cell.key + "-other";
                for (auto& child : cell.children) {
                    child.enabled = false;
                    child.controlSize = cell.controlSize;
                }
            }
            if (name == "Icon") {
                const int offset = cell.controlSize == core::ControlSize::Small ? -1 :
                                   cell.controlSize == core::ControlSize::Large ? 1 : 0;
                const int index = std::clamp(int(theme.metrics.baseIndex) + offset, 0, 2);
                cell.width = cell.height = theme.metrics.inlineIconSize[index];
            }
            cells.push_back(core::makeColumn({mutedLabel(state, theme), std::move(cell)},
                core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 4.0F));
        }
        return sectionCard(name + " · states / sizes",
            {mutedLabel(builder, theme), mutedLabel("Tokens: metrics / typography / focusRing / disabledContent", theme),
             core::withKey(core::makeGrid(std::move(cells), 0,
                 std::max(160.0F, theme.typography.label.fontSize * 12.0F), 12.0F, 16.0F),
                 "samples-" + name)}, theme, "samples-" + name + "-card");
    }

    void appendControlMatrices(std::vector<core::Widget>& items, const std::string& route,
                               const style::Theme& theme) const {
        const auto add = [&](const std::string& name, core::Widget widget,
                             const std::string& builder, bool interactive = true) {
            items.push_back(controlMatrix(name, std::move(widget), builder, theme, interactive));
        };
        if (route == "inputs") {
            add("TextField", core::makeTextField("输入 Text", "Placeholder"), "makeTextField(\"Text\", \"Placeholder\")");
            add("Checkbox", core::makeCheckbox("中文与 English 长标签换行", ""), "makeCheckbox(\"Label\", \"checked\")");
            add("Switch", core::makeSwitch("中文与 English 长标签换行", ""), "makeSwitch(\"Label\", \"checked\")");
            add("Radio", core::makeRadio("中文与 English 长标签换行", ""), "makeRadio(\"Label\", \"selected\")");
            add("Dropdown", core::makeDropdown("Selected value", ""), "makeDropdown(\"Value\", \"open\")");
            add("Tabs", core::makeTabs({core::makeButton("Selected"), core::makeButton("Other")}), "makeTabs({makeButton(\"First\"), makeButton(\"Second\")})");
        } else if (route == "feedback") {
            auto slider = core::makeSlider("");
            slider.text = "50";
            add("Slider", std::move(slider), "makeSlider(\"value\")");
            add("ProgressBar", core::makeProgressBar("50"), "makeProgressBar(\"50\")", false);
            add("Tooltip", core::makeTooltip("提示说明 Supporting text"), "makeTooltip(\"Tip\")", false);
        } else if (route == "layout") {
            add("Text", core::makeText("中文与 English 长文本自动换行。"), "makeText(\"Text\")", false);
            add("Icon", core::makeIcon(core::IconId::Check), "makeIcon(IconId::Check)", false);
            add("Image", core::makeImage(0, "", 120.0F, 64.0F), "makeImage(0, \"\", 120, 64)", false);
        }
    }

    [[nodiscard]] std::vector<core::Widget> buildButtonsItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Buttons", theme),
                                      "buttons-title"));
        items.push_back(core::withKey(
            mutedLabel("Variants, sizes and states. All variants share one "
                       "click handler that bumps the counter below.",
                       theme),
            "buttons-desc"));

        std::vector<core::Widget> variants;
        const std::pair<const char*, core::ButtonVariant> variantList[] = {
            {"Filled", core::ButtonVariant::Filled},
            {"Tonal", core::ButtonVariant::Tonal},
            {"Outline", core::ButtonVariant::Outline},
            {"Ghost", core::ButtonVariant::Ghost},
            {"Danger", core::ButtonVariant::Danger},
        };
        for (const auto& [label, variant] : variantList) {
            core::Widget button = buttonWidget(
                label, "bump-clicks",
                std::string("btn-variant-") + label, variant);
            variants.push_back(core::withKey(
                std::move(button), std::string("btn-variant-") + label));
        }
        items.push_back(sectionCard(
            "Variants",
            {core::withKey(
                core::makeGrid(std::move(variants), 0, theme.typography.label.fontSize * 8.0F,
                               style::spaceToken(2), style::spaceToken(2)),
                "buttons-variants-row"),
             core::withKey(mutedLabel("Clicked " +
                                          shell_.state().get("button-clicks") +
                                          " times",
                                      theme),
                           "buttons-clicks-label")},
            theme, "buttons-variants-card"));

        std::vector<core::Widget> sizes;
        const std::pair<const char*, core::ControlSize> sizeList[] = {
            {"Small", core::ControlSize::Small},
            {"Medium", core::ControlSize::Medium},
            {"Large", core::ControlSize::Large},
        };
        for (const auto& [label, size] : sizeList) {
            core::Widget button = buttonWidget(std::string(label),
                                               "bump-clicks",
                                               std::string("btn-size-") + label,
                                               core::ButtonVariant::Filled);
            button = core::withControlSize(std::move(button), size);
            sizes.push_back(core::withKey(
                std::move(button), std::string("btn-size-") + label));
        }
        items.push_back(sectionCard(
            "Sizes",
            {core::withKey(
                core::makeGrid(std::move(sizes), 0, theme.typography.label.fontSize * 8.0F,
                               style::spaceToken(2), style::spaceToken(2)),
                "buttons-sizes-row")},
            theme, "buttons-sizes-card"));

        core::Widget enabled = buttonWidget("Enabled", "bump-clicks",
                                            "btn-enabled",
                                            core::ButtonVariant::Filled);
        core::Widget disabled = core::withEnabled(
            buttonWidget("Disabled", "bump-clicks", "btn-disabled",
                         core::ButtonVariant::Filled),
            false);
        core::Widget selected = core::withSelected(
            buttonWidget("Selected", "bump-clicks", "btn-selected",
                         core::ButtonVariant::Tonal),
            true);
        core::Widget withIcon = core::withIcon(
            buttonWidget("Add", "bump-clicks", "btn-icon-add",
                         core::ButtonVariant::Outline),
            core::IconId::Plus);
        core::Widget dangerIcon = core::withIcon(
            buttonWidget("Delete", "bump-clicks", "btn-icon-delete",
                         core::ButtonVariant::Danger),
            core::IconId::Close);
        std::vector<core::Widget> states;
        states.push_back(core::withKey(std::move(enabled), "btn-enabled"));
        states.push_back(core::withKey(std::move(disabled), "btn-disabled"));
        states.push_back(core::withKey(std::move(selected), "btn-selected"));
        states.push_back(core::withKey(std::move(withIcon), "btn-icon-add"));
        states.push_back(
            core::withKey(std::move(dangerIcon), "btn-icon-delete"));
        items.push_back(sectionCard(
            "States",
            {core::withKey(
                core::makeGrid(std::move(states), 0, theme.typography.label.fontSize * 8.0F,
                               style::spaceToken(2), style::spaceToken(2)),
                "buttons-states-row"),
             core::withKey(buttonWidget("Back", "back", "back-button",
                                        core::ButtonVariant::Outline),
                           "back-button")},
            theme, "buttons-states-card"));

        // S5（§10.2）：强制状态矩阵——五变体 × Normal/Hovered/Pressed/
        // Focused/Focused+Pressed/Disabled。预览快照只送到 StyleResolver；
        // 真正的 focusWidth、状态色和部件走正常绘制，实际输入仍禁用。
        struct MatrixColumn {
            const char* label;
            bool hovered;
            bool pressed;
            bool focused;
        };
        const MatrixColumn matrixColumns[] = {
            {"Normal", false, false, false},   {"Hover", true, false, false},
            {"Press", false, true, false},     {"Focus", false, false, true},
            {"Foc+Prs", false, true, true},
            {"Disabled", false, false, false},
        };
        std::vector<core::Widget> matrixRows;
        for (const auto& [label, variant] : variantList) {
            std::vector<core::Widget> cells;
            for (const auto& column : matrixColumns) {
                const std::string key = std::string("matrix-") + label +
                                        "-" + column.label;
                core::Widget cell = core::makeButton(label);
                cell.buttonVariant = variant;
                cell.enabled = false; // Preview state affects style only.
                // State matrix explicitly demonstrates Focus: opt in to the ring.
                cell.showFocusRing = column.focused;
                cells.push_back(core::makeColumn(
                    {mutedLabel(column.label, theme),
                     core::withKey(std::move(cell), key)},
                    core::MainAxisAlignment::Start,
                    core::CrossAxisAlignment::Stretch, style::spaceToken(1)));
            }
            // Keep each caption with its preview when narrow windows or larger
            // fonts reduce the number of columns.
            const float cellWidth = std::max(
                theme.metrics.buttonMinWidth[theme.metrics.baseIndex],
                theme.typography.label.fontSize * 8.0F);
            matrixRows.push_back(core::makeColumn(
                {core::withKey(mutedLabel(label, theme),
                               std::string("matrix-") + label + "-label"),
                 core::makeGrid(std::move(cells), 0, cellWidth,
                                style::spaceToken(1), style::spaceToken(2))},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Stretch, style::spaceToken(1)));
        }
        // 长标签：中英文混排在窄按钮内单行省略（§10.2 布局维度样本）；
        // 包一层 Row 避免被 Stretch 列拉宽（显式 200 宽生效）。
        core::Widget longLabel = buttonWidget(
            "很长的按钮标签会单行省略 Long labels ellipsize", "bump-clicks",
            "btn-long-label", core::ButtonVariant::Outline);
        longLabel.width = 200.0F;
        items.push_back(sectionCard(
            "State matrix (forced previews)",
            {core::withKey(
                 core::makeColumn(std::move(matrixRows),
                                  core::MainAxisAlignment::Start,
                                  core::CrossAxisAlignment::Stretch,
                                  style::spaceToken(1)),
                 "buttons-matrix"),
             core::withKey(
                 core::makeRow({std::move(longLabel)},
                               core::MainAxisAlignment::Start,
                               core::CrossAxisAlignment::Start),
                 "btn-long-label-row"),
             core::withKey(mutedLabel(
                               "窄容器内的长标签单行省略（中英文混排）",
                               theme),
                           "buttons-long-label-note")},
            theme, "buttons-matrix-card"));
        return items;
    }

    [[nodiscard]] std::vector<core::Widget> buildInputsItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Inputs", theme),
                                      "inputs-title"));
        items.push_back(core::withKey(
            mutedLabel("Text fields, toggles, slider, dropdown and tabs. "
                       "Toggles and the slider write StateStore directly.",
                       theme),
            "inputs-desc"));

        std::vector<core::Widget> fields;
        fields.push_back(core::withKey(mutedLabel("Username", theme),
                                       "username-label"));
        fields.push_back(core::withKey(
            fieldWidget("username", "e.g. lumen", "username-field", false),
            "username-field"));
        fields.push_back(core::withKey(mutedLabel("Password", theme),
                                       "password-label"));
        core::Widget password =
            fieldWidget("password", "Password", "password-field", false);
        password = core::withObscure(std::move(password), true);
        fields.push_back(core::withKey(std::move(password), "password-field"));
        fields.push_back(core::withKey(mutedLabel("Bio (multiline)", theme),
                                       "bio-label"));
        core::Widget bio = fieldWidget("bio", "Tell us more", "bio-field",
                                       false);
        bio = core::withMultiline(std::move(bio), true);
        fields.push_back(core::withKey(std::move(bio), "bio-field"));
        core::Widget searchRow = core::makeRow(
            {core::withKey(core::makeIcon(core::IconId::Search,
                                          "search-icon"),
                           "search-icon"),
             core::withKey(fieldWidget("search", "Search…", "search-field",
                                       false),
                           "search-field")},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            style::spaceToken(2));
        searchRow.key = "search-row";
        searchRow.flex = 0.0F;
        // TextField 占剩余宽度：flex 由字段本身携带。
        searchRow.children.back().flex = 1.0F;
        fields.push_back(
            core::withKey(std::move(searchRow), "search-row"));
        items.push_back(sectionCard("Text fields", std::move(fields), theme,
                                    "inputs-fields-card"));

        core::Widget notifications =
            core::makeSwitch("Notifications", "notifications",
                             "notifications-switch");
        notifications.semanticsLabel = "Enable notifications";
        core::Widget autosave = core::makeCheckbox("Autosave", "autosave",
                                                   "autosave-checkbox");
        autosave.semanticsLabel = "Autosave drafts";
        core::Widget radioFree =
            core::makeRadio("Free plan", "plan-free", "plan-free");
        radioFree.checked = shell_.state().get("plan-free") == "true";
        core::Widget radioPro =
            core::makeRadio("Pro plan", "plan-pro", "plan-pro");
        radioPro.checked = shell_.state().get("plan-pro") == "true";
        items.push_back(sectionCard(
            "Toggles",
            {core::withKey(std::move(notifications),
                           "notifications-switch"),
             core::withKey(std::move(autosave), "autosave-checkbox"),
             core::withKey(
                 mutedLabel("Radios toggle their own bind (plan §3.4).",
                            theme),
                 "radio-note"),
             core::withKey(std::move(radioFree), "plan-free"),
             core::withKey(std::move(radioPro), "plan-pro")},
            theme, "inputs-toggles-card"));

        std::vector<core::Widget> selection;
        selection.push_back(core::withKey(
            mutedLabel("Volume: " + shell_.state().get("volume"), theme),
            "volume-label"));
        selection.push_back(core::withKey(core::makeSlider("volume",
                                                           "volume-slider"),
                                          "volume-slider"));
        selection.push_back(core::withKey(
            mutedLabel("Color: " + shell_.state().get("color"), theme),
            "color-label"));
        // M11：Dropdown 浮动菜单（收起叶子；点击值行/按钮经控制器在
        // 框架级 overlay 上展开，键盘 Up/Down/Enter/Esc 可导航）。
        // S3（§6.7）：展开时值行 Chevron 换向 ChevronUp。
        core::Widget dropdown = core::makeDropdown(
            shell_.state().get("color"), "open-dropdown", "color-dropdown");
        dropdown.bind = "color";
        if (dropdown_.isOpen()) {
            dropdown.icon = core::IconId::ChevronUp;
        }
        selection.push_back(
            core::withKey(std::move(dropdown), "color-dropdown"));
        selection.push_back(core::withKey(
            buttonWidget("Open dropdown", "open-dropdown",
                         "toggle-dropdown-button",
                         core::ButtonVariant::Outline),
            "toggle-dropdown-button"));
        std::vector<core::Widget> tabButtons;
        for (const char* tab : {"Basic", "More"}) {
            core::Widget tabButton = core::makeButton(
                std::string(tab), core::TextStyle{}, core::EdgeInsets{}, 0.0F,
                std::string("tab-") + tab, std::nullopt, std::nullopt,
                std::string("switch-tab-") +
                    (std::string(tab) == "Basic" ? "basic" : "more"));
            tabButton.selected = shell_.state().get("gallery-tab") == tab;
            tabButtons.push_back(std::move(tabButton));
        }
        selection.push_back(core::withKey(
            core::makeTabs(std::move(tabButtons), "gallery-tabs"),
            "gallery-tabs"));
        selection.push_back(core::withKey(
            mutedLabel("Active tab: " + shell_.state().get("gallery-tab"),
                       theme),
            "gallery-tab-label"));
        items.push_back(sectionCard("Selection", std::move(selection), theme,
                                    "inputs-selection-card"));

        // 迷你表单：校验 + invalid + 错误文案（与 settings 同契约）。
        std::vector<core::Widget> formItems;
        formItems.push_back(widgets::makeFormField("Nickname",
            fieldWidget("nickname", "Nickname", "nickname-field", form_.errors().count("nickname") != 0),
            form_.errors().count("nickname") ? form_.errors().at("nickname") : "",
            theme, "nickname"));
        formItems.push_back(widgets::makeFormField("Email",
            fieldWidget("email", "name@example.com", "email-field", form_.errors().count("email") != 0),
            form_.errors().count("email") ? form_.errors().at("email") : "",
            theme, "email"));
        formItems.push_back(core::withKey(
            buttonWidget("Save", "save", "save-button",
                         core::ButtonVariant::Filled),
            "save-button"));
        items.push_back(sectionCard("Form validation", {core::makeColumn(std::move(formItems),
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, style::spaceToken(4))},
            theme, "inputs-form-card"));

        std::vector<core::Widget> tail;
        tail.push_back(core::withKey(
            buttonWidget("Back", "back", "back-button",
                         core::ButtonVariant::Outline),
            "back-button"));
        items.push_back(sectionCard("Navigation", std::move(tail), theme,
                                    "inputs-nav-card"));
        return items;
    }

    [[nodiscard]] std::vector<core::Widget> buildLayoutItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Layout", theme),
                                      "layout-title"));
        items.push_back(core::withKey(
            mutedLabel("Row/Column flex, Stack, Container box model and Grid. "
                       "Resize the window to see flex and adaptive grid "
                       "reflow.",
                       theme),
            "layout-desc"));

        auto box = [](core::Color color, const std::string& key, float flex) {
            core::Widget leaf = core::makeContainerLeaf(
                64.0F, 40.0F, core::EdgeInsets{}, core::EdgeInsets{}, color,
                key);
            leaf.flex = flex;
            return core::withKey(std::move(leaf), key);
        };
        const core::Color accent = theme.colors.accent;
        const core::Color tonal = theme.colors.accentContainer;
        const core::Color surface = theme.colors.surfaceElevated;

        std::vector<core::Widget> rowStart{
            box(accent, "row-start-a", 0.0F), box(tonal, "row-start-b", 0.0F),
            box(surface, "row-start-c", 0.0F)};
        std::vector<core::Widget> rowCenter{
            box(accent, "row-center-a", 0.0F), box(tonal, "row-center-b", 0.0F)};
        std::vector<core::Widget> rowBetween{
            box(accent, "row-between-a", 0.0F),
            box(tonal, "row-between-b", 0.0F),
            box(surface, "row-between-c", 0.0F)};
        std::vector<core::Widget> rowCards;
        rowCards.push_back(core::withKey(
            core::makeRow(std::move(rowStart),
                          core::MainAxisAlignment::Start,
                          core::CrossAxisAlignment::Center,
                          style::spaceToken(2)),
            "row-start"));
        rowCards.push_back(core::withKey(
            core::makeRow(std::move(rowCenter),
                          core::MainAxisAlignment::Center,
                          core::CrossAxisAlignment::Center,
                          style::spaceToken(2)),
            "row-center"));
        rowCards.push_back(core::withKey(
            core::makeRow(std::move(rowBetween),
                          core::MainAxisAlignment::SpaceBetween,
                          core::CrossAxisAlignment::Center,
                          style::spaceToken(2)),
            "row-between"));
        items.push_back(sectionCard("Row alignment", std::move(rowCards),
                                    theme, "layout-row-card"));

        std::vector<core::Widget> flexChildren{
            box(accent, "flex-a", 1.0F), box(tonal, "flex-b", 2.0F),
            box(surface, "flex-c", 1.0F)};
        core::Widget flexRow = core::makeRow(
            std::move(flexChildren), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Center, style::spaceToken(2));
        flexRow.key = "flex-row";
        items.push_back(sectionCard(
            "Flex (1:2:1)",
            {core::withKey(std::move(flexRow), "flex-row"),
             core::withKey(mutedLabel("Free width is split 1:2:1.", theme),
                           "flex-desc")},
            theme, "layout-flex-card"));

        core::Widget back = core::makeContainerLeaf(
            180.0F, 110.0F, core::EdgeInsets{}, core::EdgeInsets{}, surface,
            "stack-back");
        core::Widget mid = core::makeContainerLeaf(
            120.0F, 70.0F, core::EdgeInsets{}, core::EdgeInsets{}, tonal,
            "stack-mid");
        core::Widget front = core::makeContainerLeaf(
            64.0F, 40.0F, core::EdgeInsets{}, core::EdgeInsets{}, accent,
            "stack-front");
        front = core::withStackPosition(std::move(front),
                                        core::Offset{96.0F, 56.0F});
        core::Widget stack = core::makeStack(
            {core::withKey(std::move(back), "stack-back"),
             core::withKey(std::move(mid), "stack-mid"),
             core::withKey(std::move(front), "stack-front")},
            core::StackAlignment::TopLeft);
        stack.key = "demo-stack";
        items.push_back(sectionCard(
            "Stack",
            {core::withKey(std::move(stack), "demo-stack"),
             core::withKey(mutedLabel("Front box uses explicit offset.",
                                      theme),
                           "stack-desc")},
            theme, "layout-stack-card"));

        core::Widget padded = core::makeContainer(
            core::makeText("Padded", theme.typography.body), 140.0F, 56.0F,
            core::EdgeInsets::all(style::spaceToken(4)), core::EdgeInsets{},
            tonal, core::CornerRadius::all(theme.metrics.cardRadius),
            "container-padded");
        core::Widget margined = core::makeContainer(
            core::makeText("Margin", theme.typography.body), 140.0F, 56.0F,
            core::EdgeInsets{}, core::EdgeInsets::all(style::spaceToken(3)),
            surface, core::CornerRadius::all(theme.metrics.cardRadius),
            "container-margined");
        std::vector<core::Widget> boxRow;
        boxRow.push_back(core::withKey(std::move(padded),
                                       "container-padded"));
        boxRow.push_back(core::withKey(std::move(margined),
                                       "container-margined"));
        // Image 未就绪占位：imageId=0 时绘制表面+边框+中心叉，
        // source 保留语义与诊断信息。
        boxRow.push_back(core::withKey(
            core::makeImage(0, "gallery/placeholder", 120.0F, 80.0F,
                            "demo-image"),
            "demo-image"));
        items.push_back(sectionCard(
            "Container",
            {core::withKey(
                core::makeRow(std::move(boxRow),
                              core::MainAxisAlignment::Start,
                              core::CrossAxisAlignment::Center,
                              style::spaceToken(3)),
                "container-row")},
            theme, "layout-container-card"));

        std::vector<core::Widget> fixedCells;
        for (int i = 0; i < 6; ++i) {
            core::Widget cell = mutedLabel("F" + std::to_string(i), theme);
            cell.key = "fixed-" + std::to_string(i);
            fixedCells.push_back(std::move(cell));
        }
        std::vector<core::Widget> adaptiveCells;
        for (int i = 0; i < 8; ++i) {
            core::Widget cell = mutedLabel("A" + std::to_string(i), theme);
            cell.key = "adaptive-" + std::to_string(i);
            adaptiveCells.push_back(std::move(cell));
        }
        items.push_back(sectionCard(
            "Grid",
            {core::withKey(
                core::makeGrid(std::move(fixedCells), 3, 0.0F, 8.0F, 8.0F,
                               "fixed-grid"),
                "fixed-grid"),
             core::withKey(
                 core::makeGrid(std::move(adaptiveCells), 0, 140.0F, 8.0F,
                                8.0F, "adaptive-grid"),
                 "adaptive-grid"),
             core::withKey(buttonWidget("Back", "back", "back-button",
                                        core::ButtonVariant::Outline),
                           "back-button")},
            theme, "layout-grid-card"));
        return items;
    }

    [[nodiscard]] std::vector<core::Widget> buildListsItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Lists", theme),
                                      "lists-title"));
        items.push_back(core::withKey(
            mutedLabel("Outer page scrolls via the main ListView; the library "
                       "below is a 1000-item VirtualList that only "
                       "materializes the visible window.",
                       theme),
            "lists-desc"));
        items.push_back(sectionCard(
            "Scroll state",
            {core::withKey(mutedLabel("Offset: " +
                                          std::to_string(static_cast<int>(
                                              scroll_.offset())),
                                      theme),
                           "lists-offset-label")},
            theme, "lists-state-card"));

        // ScrollView：固定视口 + 不限主轴内容（与 ListView 同滚动契约，
        // 区别是语义 role；内容裁剪由 clipContent 保证）。
        std::vector<core::Widget> scrollRows;
        for (int i = 0; i < 8; ++i) {
            core::Widget row = mutedLabel("Scroll row " + std::to_string(i),
                                          theme);
            row.key = "scroll-row-" + std::to_string(i);
            scrollRows.push_back(std::move(row));
        }
        core::Widget scrollView = core::makeScrollView(
            core::makeColumn(std::move(scrollRows),
                             core::MainAxisAlignment::Start,
                             core::CrossAxisAlignment::Start,
                             style::spaceToken(2)),
            "gallery-scrollview", std::nullopt, 140.0F);
        scrollView.scrollOffset = scrollViewScroll_.offset();
        scrollView.showScrollbar = true;
        items.push_back(sectionCard(
            "ScrollView (fixed viewport)",
            {core::withKey(std::move(scrollView), "gallery-scrollview")},
            theme, "lists-scrollview-card"));

        // 固定高度：在外层 ListView（主轴无界）内必须给显式高度；
        // 不设 flex——无界容器内 flex 无意义。
        core::Widget listWidget = core::makeVirtualList(
            &library_, "gallery-library", std::nullopt, 320.0F, 200.0F);
        items.push_back(sectionCard(
            "VirtualList (1000)",
            {core::withKey(std::move(listWidget), "gallery-library"),
             core::withKey(buttonWidget("Back", "back", "back-button",
                                        core::ButtonVariant::Outline),
                           "back-button")},
            theme, "lists-library-card"));
        return items;
    }

    // Collections 分区（docs/lumen-collection-controls-design.md §11.3）：
    // List / Tree / TreeList 三控件同页对照。三个集合共享 M3 虚拟化引擎
    //（O(visible) 物化）与 SelectionModel；外层 ListView 主轴无界，集合
    // 控件须给显式高度（与 Lists 页 VirtualList 同约束）。
    [[nodiscard]] std::vector<core::Widget> buildCollectionsItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Collections", theme),
                                      "collections-title"));
        items.push_back(core::withKey(
            mutedLabel("List, Tree and TreeList share one virtualized "
                       "engine and one selection model. Rows materialize "
                       "O(visible); selection state lives in the "
                       "controllers, keyed by row identity.",
                       theme),
            "collections-desc"));
        const bool showFocusRing = shell_.state().get("collection-focus-rings") == "true";
        items.push_back(core::makeCheckbox("Show collection focus rings", "collection-focus-rings",
                                           "collection-focus-rings"));

        // --- List：Extended 选择 + 双击/Enter 激活 + Ctrl+A ---
        // 内层视口常显滚动条（与外层 gallery-list 同 token 路径；内容
        // 超出固定高度时右侧出现 Thumb）。
        core::Widget listWidget = core::withScrollbar(core::makeList(
            &collectionList_, "collection-list", std::nullopt, 300.0F));
        listWidget.showFocusRing = showFocusRing;
        items.push_back(sectionCard(
            "List — selection & activation",
            {core::withKey(mutedLabel(collectionListStatus(), theme),
                           "collection-list-status"),
             core::withKey(core::withOnClick(core::makeButton("Change selection mode"), "cycle-list-mode"),
                           "collection-list-mode"),
             core::withKey(std::move(listWidget), "collection-list"),
             core::withKey(mutedLabel("Click selects; Ctrl+click toggles; "
                                      "Shift+click extends; double-click or "
                                      "Enter activates; Ctrl+A selects all.",
                                      theme),
                           "collection-list-hint")},
            theme, "collections-list-card", "200 rows · every seventh row disabled"));

        // Deterministic visual samples use the same ListController row shell.
        std::vector<core::Widget> states;
        for (const auto* name : {"Normal", "Hover", "Press", "Focus", "Selected",
                                 "Selected+Focused", "Disabled"}) {
            auto row = collectionList_.buildItem(0);
            row.key = std::string("list-preview-") + name;
            // 状态矩阵按状态演示 Focus（与按钮矩阵同口径）：聚焦行显式
            // 开环，不受页面开关影响——开关只对照交互式 List/Tree 视口。
            row.showFocusRing = previewState(name).focused;
            row.onClick.clear();
            row.semanticsActions = 0;
            row.children = {core::makeIcon(core::IconId::Document),
                            core::makeText(name, theme.typography.body)};
            row.children.back().flex = 1.0F;
            row.spacing = theme.metrics.controlGap[theme.metrics.baseIndex];
            const bool disabled = std::string(name) == "Disabled";
            row.enabled = !disabled;
            if (disabled) {
                for (auto& child : row.children) child.styleOverrides.foreground = theme.list.disabledContent;
            }
            states.push_back(std::move(row));
        }
        items.push_back(sectionCard("List — state matrix",
            {core::withKey(core::makeColumn(std::move(states), core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Stretch), "collection-list-states")},
            theme, "collections-list-states-card", "Preview states"));
        items.push_back(sectionCard("List — empty state",
            {core::makeList(&collectionEmptyList_, "collection-empty-list", std::nullopt,
                theme.metrics.minHeight[theme.metrics.baseIndex] * 3.0F)},
            theme, "collections-list-empty-card"));

        // --- Tree：层级模型 + 键盘 Left/Right + 按 key 的展开状态 ---
        core::Widget treeWidget = core::withScrollbar(core::makeTree(
            &collectionTree_, "collection-tree", std::nullopt, 280.0F));
        treeWidget.showFocusRing = showFocusRing;
        items.push_back(sectionCard(
            "Tree — hierarchy & lazy model",
            {core::withKey(mutedLabel(collectionTreeStatus(), theme),
                           "collection-tree-status"),
             core::withKey(std::move(treeWidget), "collection-tree"),
             core::withKey(mutedLabel("Chevrons toggle expansion without "
                                      "changing selection; Left collapses "
                                      "or jumps to parent, Right expands.",
                                      theme),
                           "collection-tree-hint")},
            theme, "collections-tree-card", "Single · repo skeleton"));

        std::vector<core::Widget> treeStates;
        for (const auto* name : {"Normal", "Hover", "Press", "Focus", "Selected",
                                 "Selected+Focused", "Disabled", "Disabled+Selected"}) {
            auto chevron = core::makeButton("");
            chevron.icon = core::IconId::ChevronRight;
            chevron.treePart = core::TreePart::Chevron;
            auto row = core::makeRow({std::move(chevron), core::makeText(name)},
                core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center);
            row.treePart = core::TreePart::Row;
            row.collectionRow = true;
            row.key = std::string("tree-preview-") + name;
            // 状态矩阵按状态演示 Focus（与按钮矩阵同口径）：聚焦行显式
            // 开环，不受页面开关影响——开关只对照交互式 List/Tree 视口。
            const bool rowFocused = previewState(name).focused;
            row.showFocusRing = rowFocused;
            row.children.front().key = row.key + ":chevron";
            row.children.front().showFocusRing = rowFocused;
            row.children.back().flex = 1.0F;
            if (std::string(name).find("Disabled") != std::string::npos) {
                row.enabled = false;
                for (auto& child : row.children) {
                    child.enabled = false;
                    child.styleOverrides.foreground = theme.tree.row.disabledContent;
                }
            }
            treeStates.push_back(std::move(row));
        }
        items.push_back(sectionCard("Tree — state matrix",
            {core::withKey(core::makeColumn(std::move(treeStates), core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Stretch), "collection-tree-states")},
            theme, "collections-tree-states-card", "Selection and focus are independent"));
        items.push_back(sectionCard("Tree — empty state",
            {core::makeTree(&collectionEmptyTree_, "collection-empty-tree", std::nullopt,
                theme.metrics.minHeight[theme.metrics.baseIndex] * 3.0F)},
            theme, "collections-tree-empty-card"));

        // --- TreeList：列系统 + 粘性表头 + 排序钩子 ---
        core::Widget tableWidget = core::withScrollbar(core::makeTreeList(
            &collectionTable_, &collectionTable_.columns(), true,
            "collection-table", std::nullopt, 280.0F));
        tableWidget.showFocusRing = showFocusRing;
        items.push_back(sectionCard(
            "TreeList — columns & sticky header",
            {core::withKey(mutedLabel(collectionTableStatus(), theme),
                           "collection-table-status"),
             core::withKey(std::move(tableWidget), "collection-table"),
             core::withKey(mutedLabel("Header stays pinned while rows "
                                      "scroll; clicking a sortable column "
                                      "asks the app to sort (framework "
                                      "never reorders data).",
                                      theme),
                           "collection-table-hint")},
            theme, "collections-table-card", "deps · sortable"));
        return items;
    }

    // Collections 状态行（选择/展开/排序实时回显；build 期读取控制器）。
    [[nodiscard]] std::string collectionListStatus() const {
        const auto& selection = collectionList_.selection();
        constexpr const char* modes[] = {"None", "Single", "Multiple", "Extended"};
        std::string text = std::string(modes[static_cast<unsigned>(selection.mode())]) + " · Selected: " +
                           std::to_string(selection.selectedCount()) +
                           " · Current: " +
                           (selection.currentKey().empty()
                                ? std::string("—")
                                : selection.currentKey());
        if (!lastActivatedKey_.empty()) {
            text += " · Activated: " + lastActivatedKey_;
        }
        return text;
    }
    [[nodiscard]] std::string collectionTreeStatus() const {
        const auto& selection = collectionTree_.selection();
        return "Visible rows: " +
               std::to_string(collectionTree_.visibleRows().size()) +
               " · Current: " +
               (selection.currentKey().empty() ? std::string("—")
                                               : selection.currentKey());
    }
    [[nodiscard]] std::string collectionTableStatus() const {
        if (lastSortColumn_.empty()) {
            return "Sort: natural order";
        }
        return "Sort: " + lastSortColumn_ +
               (lastSortDescending_ ? " · descending" : " · ascending");
    }

    // --- Controls（lumen-{spin,toolbar,statusbar}-design §11.3）---

    // Controls 分区：Spin 数值步进（整数/小数）、ToolBar（命令组 +
    // toggle + 溢出折叠）、StatusBar（消息/进度/busy）实时样本。
    [[nodiscard]] std::vector<core::Widget> buildControlsItems(
        const style::Theme& theme) {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Controls", theme),
                                      "controls-title"));
        items.push_back(core::withKey(
            mutedLabel("Spin steps values with click-and-hold repeat, "
                       "keyboard and wheel; ToolBar folds trailing items "
                       "into an overflow panel when narrow; StatusBar "
                       "carries transient messages, progress and busy "
                       "rhythm. All three compose existing widgets with "
                       "zero new widget types.",
                       theme),
            "controls-desc"));

        items.push_back(sectionCard(
            "Spin — bounded numeric stepping",
            {core::makeRow(
                 {core::makeText("Opacity", theme.typography.label),
                  controlsOpacity_.build(theme)},
                 core::MainAxisAlignment::Start,
                 core::CrossAxisAlignment::Center, 16.0F),
             core::makeRow(
                 {core::makeText("Font size", theme.typography.label),
                  controlsFontSize_.build(theme)},
                 core::MainAxisAlignment::Start,
                 core::CrossAxisAlignment::Center, 16.0F),
             core::withKey(mutedLabel("Hold ▲/▼ for auto-repeat (500ms "
                                      "delay, 60ms step) · type + Enter "
                                      "commits · Esc reverts · Up/Down/"
                                      "PgUp/PgDn/Home/End · wheel.",
                                      theme),
                           "controls-spin-hint")},
            theme, "controls-spin-card"));

        items.push_back(sectionCard(
            "ToolBar — commands, toggles, overflow",
            {controlsToolBar_.build(shell_, theme),
             core::withKey(mutedLabel("Last command: " + lastControlsCommand_,
                                      theme),
                           "controls-command-label"),
             core::withKey(mutedLabel("Tab enters the bar, Left/Right roam, "
                                      "Enter activates; toggles hold a "
                                      "persistent surface; narrowing the "
                                      "window folds the tail into the "
                                      "chevron panel.",
                                      theme),
                           "controls-tb-hint")},
            theme, "controls-toolbar-card"));

        controlsStatusBar_.setItems({
            {"sep", widgets::StatusItemKind::Separator, core::IconId::None,
             "", true, 0.0F},
            {"cursor", widgets::StatusItemKind::Text, core::IconId::None,
             "Ln 1, Col 1", true, 0.0F},
        });
        items.push_back(sectionCard(
            "StatusBar — messages, progress, busy",
            {controlsStatusBar_.build(theme),
             core::withKey(mutedLabel("The \"Grid\" toggle above drives the "
                                      "status bar busy arc; use the Inputs "
                                      "page message actions for transient "
                                      "status text.",
                                      theme),
                           "controls-sb-hint")},
            theme, "controls-statusbar-card"));

        return items;
    }

    // --- Menus（menu-controls-design §11.3，对齐 design/gallery.html 增补）---

    // Menus 分区：ContextMenu 行级演示（右键唤起）+ chrome 菜单栏实时
    // 状态回显（菜单栏本体在窗口 chrome，见 buildUi）。
    [[nodiscard]] std::vector<core::Widget> buildMenusItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Menus", theme),
                                      "menus-title"));
        items.push_back(core::withKey(
            mutedLabel("ContextMenu and MenuBar share one overlay panel: "
                       "separators, checkable items, shortcut display and "
                       "lazy submenus. The bar under the window title is "
                       "the live MenuBar; right-click targets below (or any "
                       "content row) for the context menu. Open bar items "
                       "turn tonal with an accent underline; panels fade "
                       "and rise in while the keyboard highlight slides "
                       "between rows (motion tokens; reduce-animation "
                       "instant).",
                       theme),
            "menus-desc"));

        std::vector<core::Widget> targets;
        for (const char* name : {"notes.md", "design.md", "build.log"}) {
            targets.push_back(core::withKey(
                buttonWidget(name, "", "menu-target:" + std::string(name),
                             core::ButtonVariant::Outline),
                "menu-target:" + std::string(name)));
        }
        items.push_back(sectionCard(
            "ContextMenu — pointer anchored",
            {core::withKey(
                 mutedLabel("Last command: " + lastMenuCommand_, theme),
                 "menu-command-label"),
             core::withKey(
                 core::makeColumn(std::move(targets),
                                  core::MainAxisAlignment::Start,
                                  core::CrossAxisAlignment::Stretch, 8.0F),
                 "menu-targets"),
             core::withKey(mutedLabel("Right-click a row to open · Up/Down "
                                      "move · Enter activates · Esc closes. "
                                      "Shortcut text is display-only.",
                                      theme),
                           "menus-ctx-hint")},
            theme, "menus-context-card"));

        items.push_back(sectionCard(
            "MenuBar — chrome & cascade",
            {core::withKey(mutedLabel(viewMenuStatus(), theme),
                           "menus-bar-status"),
             core::withKey(mutedLabel("Click File / View / Help in the "
                                      "window chrome, or press Alt+F / "
                                      "Alt+V / Alt+H. With a menu open, "
                                      "Left / Right switch top menus; Enter "
                                      "or Right expands submenu items.",
                                      theme),
                           "menus-bar-hint")},
            theme, "menus-bar-card"));
        return items;
    }

    // View 菜单状态回显（checkable 项的应用侧真状态）。
    [[nodiscard]] std::string viewMenuStatus() const {
        std::string text =
            "Sidebar: " + std::string(sidebarVisible_ ? "shown" : "hidden") +
            " · Pane: " +
            std::to_string(static_cast<int>(sidebarSplitter_.offset())) +
            " px (" + (sidebarUserAdjusted_ ? "adjusted" : "breakpoint") +
            ") · Density: " + densityName(shell_.theme().metrics.density) +
            " · Follow system theme: " +
            (followSystemTheme_ ? "on" : "off");
        return text;
    }

    // 菜单栏顶级项（menu-controls-design §5；对齐设计稿 File/View/Help
    // 内容）。checkable 项构建期读取应用状态（点击 → onCommand → 翻转 →
    // 重开菜单经 provider 再读取）。
    [[nodiscard]] widgets::MenuItems barMenuItems(
        const std::string& id) const {
        widgets::MenuItems items;
        if (id == "file") {
            items.push_back({.id = "new-window", .label = "New window",
                             .shortcut = "Ctrl+N"});
            items.push_back({.id = "open", .label = "Open…",
                             .icon = core::IconId::Search,
                             .shortcut = "Ctrl+O"});
            items.push_back({.id = "sep", .separator = true});
            items.push_back({.id = "save", .label = "Save",
                             .shortcut = "Ctrl+S"});
            items.push_back({.id = "save-as", .label = "Save As…",
                             .shortcut = "Ctrl+Shift+S",
                             .enabled = false});
        } else if (id == "view") {
            items.push_back({.id = "toggle-sidebar",
                             .label = "Show sidebar",
                             .checkable = true,
                             .checked = sidebarVisible_,
                             .shortcut = "Ctrl+B"});
            items.push_back({.id = "reset-panes",
                             .label = "Reset pane layout",
                             .shortcut = "Ctrl+0"});
            items.push_back({.id = "sep", .separator = true});
            items.push_back({.id = "density", .label = "Density",
                             .hasSubmenu = true});
            items.push_back({.id = "follow-theme",
                             .label = "Follow system theme"});
        } else if (id == "help") {
            items.push_back({.id = "shortcuts",
                             .label = "Shortcut overview",
                             .shortcut = "Ctrl+/"});
            items.push_back({.id = "about", .label = "About Lumen",
                             .icon = core::IconId::Info});
        } else if (id == "sub:density") {
            using style::ControlDensity;
            const ControlDensity current =
                shell_.theme().metrics.density;
            items.push_back({.id = "density-compact",
                             .label = "Compact (32px)",
                             .checkable = true,
                             .checked =
                                 current == ControlDensity::Compact});
            items.push_back({.id = "density-comfortable",
                             .label = "Comfortable (40px)",
                             .checkable = true,
                             .checked =
                                 current == ControlDensity::Comfortable});
            items.push_back({.id = "density-touch",
                             .label = "Touch (48px)",
                             .checkable = true,
                             .checked = current == ControlDensity::Touch});
        }
        return items;
    }

    // 主内容区通用右键菜单（设计稿 app-main 右键形态）。
    [[nodiscard]] widgets::MenuItems contentContextMenuItems() const {
        widgets::MenuItems items;
        items.push_back({.id = "open", .label = "Open",
                         .icon = core::IconId::Search,
                         .shortcut = "Enter"});
        items.push_back({.id = "copy-path", .label = "Copy path",
                         .shortcut = "Ctrl+C"});
        items.push_back({.id = "paste", .label = "Paste",
                         .shortcut = "Ctrl+V", .enabled = false});
        items.push_back({.id = "sep", .separator = true});
        items.push_back({.id = "props", .label = "Properties…",
                         .icon = core::IconId::Info,
                         .shortcut = "Alt+Enter"});
        return items;
    }

    // Menus 分区演示行行级菜单（rowKey = "menu-target:<name>"）。
    [[nodiscard]] widgets::MenuItems targetContextMenuItems(
        const std::string& rowKey) const {
        const std::string name =
            rowKey.size() > 12 ? rowKey.substr(12) : rowKey;
        widgets::MenuItems items;
        items.push_back({.id = "open:" + name, .label = "Open",
                         .icon = core::IconId::Search,
                         .shortcut = "Enter"});
        items.push_back({.id = "rename:" + name, .label = "Rename…"});
        items.push_back({.id = "sep", .separator = true});
        items.push_back({.id = "copy:" + name, .label = "Copy path",
                         .shortcut = "Ctrl+C"});
        items.push_back({.id = "remove:" + name, .label = "Delete",
                         .enabled = false});
        items.push_back({.id = "props:" + name, .label = "Properties…",
                         .icon = core::IconId::Info,
                         .shortcut = "Alt+Enter"});
        return items;
    }

    // 菜单命令统一入口（menu-controls-design §6：激活 = 关闭后回调）。
    // checkable/状态类命令改应用状态；未接业务命令仅回显。
    void handleMenuCommand(const std::string& id) {
        lastMenuCommand_ = id;
        if (id == "toggle-sidebar") {
            sidebarVisible_ = !sidebarVisible_;
        } else if (id == "reset-panes") {
            // 复位分隔条并恢复断点跟随（双击分隔条等价路径在框架侧）。
            sidebarUserAdjusted_ = false;
            syncSidebarBreakpoint();
        } else if (id == "follow-theme") {
            followSystemTheme_ = !followSystemTheme_;
            if (followSystemTheme_) {
                applySystemTheme();
            }
        } else if (id == "about") {
            openDialog();
            shell_.requestFullRepaint();
        } else if (id.rfind("density-", 0) == 0) {
            using style::ControlDensity;
            ControlDensity density = ControlDensity::Comfortable;
            if (id == "density-compact") {
                density = ControlDensity::Compact;
            } else if (id == "density-touch") {
                density = ControlDensity::Touch;
            }
            applyDensity(density);
        }
        shell_.markDirty();
    }

    // 密度档位（与 View 菜单子菜单联动；保留全部派生与可访问性设置）。
    void applyDensity(style::ControlDensity density) {
        style::Theme nextTheme = style::Theme::fromSettings(
            shell_.accessibilitySettings(), darkMode_, density, direction_);
        shell_.setTheme(std::move(nextTheme));
        shell_.markDirty();
    }

    // Splitter 断点同步：未被手动调节时，侧栏跟随 200/168 响应式宽度
    //（设计稿侧栏断点）；手动拖动/键盘后保持用户位置（KeepOffset）。
    void syncSidebarBreakpoint() {
        if (sidebarUserAdjusted_) {
            return;
        }
        syncingSidebarBreakpoint_ = true;
        sidebarSplitter_.setResetOffset(sidebarWidth());
        sidebarSplitter_.resetToInitial();
        syncingSidebarBreakpoint_ = false;
    }

    [[nodiscard]] std::vector<core::Widget> buildFeedbackItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Feedback", theme),
                                      "feedback-title"));
        items.push_back(core::withKey(
            mutedLabel("Progress, icons, tooltip and dialog.", theme),
            "feedback-desc"));

        core::Widget bar =
            core::makeProgressBar(shell_.state().get("demo-progress"),
                                  "progress-bar");
        bar.bind = "demo-progress";
        items.push_back(sectionCard(
            "Progress",
            {core::withKey(
                 core::makeSlider("demo-progress", "progress-slider"),
                 "progress-slider"),
             core::withKey(std::move(bar), "progress-bar"),
             core::withKey(mutedLabel("Progress: " +
                                          shell_.state().get("demo-progress"),
                                      theme),
                           "progress-label")},
            theme, "feedback-progress-card"));

        std::vector<core::Widget> icons;
        const std::pair<core::IconId, const char*> iconList[] = {
            {core::IconId::Check, "check"},
            {core::IconId::Close, "close"},
            {core::IconId::ChevronDown, "chevron-down"},
            {core::IconId::ChevronRight, "chevron-right"},
            {core::IconId::ChevronLeft, "chevron-left"},
            {core::IconId::ChevronUp, "chevron-up"},
            {core::IconId::Plus, "plus"},
            {core::IconId::Minus, "minus"},
            {core::IconId::Search, "search"},
            {core::IconId::Info, "info"},
            {core::IconId::Alert, "alert"},
        };
        for (const auto& [id, name] : iconList) {
            icons.push_back(core::withKey(
                core::makeIcon(id, std::string("icon-") + name),
                std::string("icon-") + name));
        }
        items.push_back(sectionCard(
            "Icons",
            {core::withKey(
                 core::makeRow(std::move(icons),
                               core::MainAxisAlignment::Start,
                               core::CrossAxisAlignment::Center,
                               style::spaceToken(3)),
                 "icons-row"),
             // M11：Tooltip hover 延迟驱动——锚点按钮 hover 停留后气泡
             // 淡入（Stack 悬浮定位，不再常驻占位）。
             core::withKey(
                 core::makeStack({
                     core::withKey(
                         buttonWidget("Hover me", "noop",
                                      "tooltip-anchor-button",
                                      core::ButtonVariant::Outline),
                         "tooltip-anchor-button"),
                     core::withStackPosition(
                         core::withKey(core::makeTooltip(
                                            "Gallery showcase tip",
                                            "showcase-tip"),
                                        "showcase-tip"),
                         core::Offset{0.0F, 48.0F}),
                 }),
                 "tooltip-stack"),
             core::withKey(mutedLabel("Hover the button: the tooltip "
                                      "fades in after a short delay.",
                                      theme),
                           "tooltip-desc")},
            theme, "feedback-icons-card"));

        core::StyleOverrides errorOverride;
        errorOverride.foreground = theme.colors.statusError;
        core::Widget errorSample = core::withStyleOverrides(
            core::makeText("Error: check your input.",
                           theme.typography.body),
            std::move(errorOverride));
        core::StyleOverrides successOverride;
        successOverride.foreground = theme.colors.statusSuccess;
        core::Widget successSample = core::withStyleOverrides(
            core::makeText("Success: profile saved.",
                           theme.typography.body),
            std::move(successOverride));
        core::StyleOverrides warningOverride;
        warningOverride.foreground = theme.colors.statusWarning;
        core::Widget warningSample = core::withStyleOverrides(
            core::makeText("Warning: unsaved changes.",
                           theme.typography.body),
            std::move(warningOverride));
        items.push_back(sectionCard(
            "Status",
            {core::withKey(std::move(errorSample), "status-error"),
             core::withKey(std::move(successSample), "status-success"),
             core::withKey(std::move(warningSample), "status-warning"),
             core::withKey(buttonWidget("Show dialog", "show-dialog",
                                        "show-dialog-button-feedback",
                                        core::ButtonVariant::Filled),
                           "show-dialog-button-feedback"),
             core::withKey(buttonWidget("Back", "back", "back-button",
                                        core::ButtonVariant::Outline),
                           "back-button")},
            theme, "feedback-status-card"));
        return items;
    }

    [[nodiscard]] std::vector<core::Widget> buildThemeItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Theme", theme),
                                      "theme-title"));
        items.push_back(core::withKey(
            mutedLabel("Semantic color scheme, typography, density and a "
                       "local ThemeScope preview.",
                       theme),
            "theme-desc"));

        items.push_back(sectionCard(
            "Mode & density",
            {core::withKey(
                 buttonWidget(darkMode_ ? "Switch to light" : "Switch to dark",
                              "toggle-dark", "toggle-dark-button-theme",
                              core::ButtonVariant::Filled),
                 "toggle-dark-button-theme"),
             core::withKey(
                 buttonWidget(followSystemTheme_ ? "Follow system: on"
                                                 : "Follow system: off",
                              "toggle-follow-system",
                              "follow-system-button",
                              core::ButtonVariant::Outline),
                 "follow-system-button"),
             core::withKey(
                 core::makeRow(
                     {core::withKey(
                          buttonWidget("Core", "set-direction-core",
                                       "direction-core-button",
                                       direction_ ==
                                               style::ThemeDirection::CoreDark
                                           ? core::ButtonVariant::Filled
                                           : core::ButtonVariant::Outline),
                          "direction-core-button"),
                      core::withKey(
                          buttonWidget("Ink", "set-direction-ink",
                                       "direction-ink-button",
                                       direction_ ==
                                               style::ThemeDirection::InkLinen
                                           ? core::ButtonVariant::Filled
                                           : core::ButtonVariant::Outline),
                          "direction-ink-button"),
                      core::withKey(
                          buttonWidget("Aurora", "set-direction-aurora",
                                       "direction-aurora-button",
                                       direction_ ==
                                               style::ThemeDirection::
                                                   AuroraSignal
                                           ? core::ButtonVariant::Filled
                                           : core::ButtonVariant::Outline),
                          "direction-aurora-button"),
                      core::withKey(
                          buttonWidget("Utility", "set-direction-utility",
                                       "direction-utility-button",
                                       direction_ ==
                                               style::ThemeDirection::
                                                   UtilityContrast
                                           ? core::ButtonVariant::Filled
                                           : core::ButtonVariant::Outline),
                          "direction-utility-button")},
                     core::MainAxisAlignment::Start,
                     core::CrossAxisAlignment::Center, style::spaceToken(2)),
                 "direction-row"),
             core::withKey(
                 mutedLabel("Direction: " + directionName(direction_), theme),
                 "direction-label"),
             core::withKey(
                 buttonWidget(
                     "Density: " + densityName(theme.metrics.density) +
                         " (tap to cycle)",
                     "cycle-density", "cycle-density-button",
                     core::ButtonVariant::Tonal),
                 "cycle-density-button"),
             core::withKey(
                 buttonWidget(
                     shell_.accessibilitySettings().highContrast
                         ? "High contrast: on"
                         : "High contrast: off",
                     "toggle-contrast", "toggle-contrast-button",
                     core::ButtonVariant::Outline),
                 "toggle-contrast-button"),
             core::withKey(
                 core::makeRow(
                     {core::withKey(
                          buttonWidget("Blue", "accent-blue",
                                       "accent-blue-button",
                                       core::ButtonVariant::Outline),
                          "accent-blue-button"),
                      core::withKey(
                          buttonWidget("Green", "accent-green",
                                       "accent-green-button",
                                       core::ButtonVariant::Outline),
                          "accent-green-button"),
                      core::withKey(
                          buttonWidget("Amber", "accent-amber",
                                       "accent-amber-button",
                                       core::ButtonVariant::Outline),
                          "accent-amber-button")},
                     core::MainAxisAlignment::Start,
                     core::CrossAxisAlignment::Center, style::spaceToken(2)),
                 "accent-row")},
            theme, "theme-mode-card"));

        std::vector<core::Widget> swatches;
        const std::pair<core::Color, const char*> colors[] = {
            {theme.colors.pageBackground, "pageBackground"},
            {theme.colors.surface, "surface"},
            {theme.colors.surfaceElevated, "surfaceElevated"},
            {theme.colors.contentPrimary, "contentPrimary"},
            {theme.colors.contentSecondary, "contentSecondary"},
            {theme.colors.accent, "accent"},
            {theme.colors.onAccent, "onAccent"},
            {theme.colors.accentContainer, "accentContainer"},
            {theme.colors.borderDefault, "borderDefault"},
            {theme.colors.borderStrong, "borderStrong"},
            {theme.colors.focusRing, "focusRing"},
            {theme.colors.statusError, "statusError"},
            {theme.colors.statusSuccess, "statusSuccess"},
            {theme.colors.statusWarning, "statusWarning"},
        };
        for (const auto& [color, name] : colors) {
            swatches.push_back(swatch(color, name, theme));
        }
        core::Widget swatchGrid =
            core::makeGrid(std::move(swatches), 0, 200.0F, 8.0F, 8.0F,
                           "swatch-grid");
        items.push_back(sectionCard(
            "Color scheme",
            {core::withKey(std::move(swatchGrid), "swatch-grid")}, theme,
            "theme-colors-card"));

        core::Widget titleSample =
            core::makeText("Title type", theme.typography.title);
        core::Widget labelSample =
            core::makeText("Label type", theme.typography.label);
        core::Widget bodySample =
            core::makeText("Body type", theme.typography.body);
        core::Widget captionSample =
            core::makeText("Caption type", theme.typography.caption);
        items.push_back(sectionCard(
            "Typography",
            {core::withKey(std::move(titleSample), "type-title"),
             core::withKey(std::move(labelSample), "type-label"),
             core::withKey(std::move(bodySample), "type-body"),
             core::withKey(std::move(captionSample), "type-caption")},
            theme, "theme-type-card"));

        core::Widget scopeBody = core::makeColumn(
            {core::withKey(mutedLabel("Inside light scope", theme),
                           "scope-label"),
             core::withKey(core::withIcon(core::makeButton("Scoped"),
                                          core::IconId::Check),
                           "scope-button")});
        core::Widget scope =
            core::makeThemeScope(std::move(scopeBody), themeScopeData_.get());
        items.push_back(sectionCard(
            "ThemeScope (light preview)",
            {core::withKey(std::move(scope), "theme-scope"),
             core::withKey(buttonWidget("Back", "back", "back-button",
                                        core::ButtonVariant::Outline),
                           "back-button")},
            theme, "theme-scope-card"));
        return items;
    }

    // --- 小组件辅助（与 settings 同风格：视觉全部来自 Theme token） ---

    // 分区卡片 = 面板（design panel）：面板头（标题 + 右侧 meta）+ 内容列，
    // 表面 + 1px 边框 + 卡片圆角。Overview 的四个面板复用同一容器。
    [[nodiscard]] core::Widget sectionCard(
        const std::string& title, std::vector<core::Widget> children,
        const style::Theme& theme, const std::string& key,
        const std::string& meta = "") const {
        std::vector<core::Widget> body;
        body.push_back(
            core::withKey(panelHead(title, meta, theme), key + "-cap"));
        for (core::Widget& child : children) {
            body.push_back(std::move(child));
        }
        return panelCard(std::move(body), theme, key);
    }

    // 面板容器：surface 背景 + borderDefault 1px + cardRadius；面板头到
    // 内容 16、面板间距 16（设计稿 4px 网格节奏）。
    [[nodiscard]] core::Widget panelCard(std::vector<core::Widget> children,
                                         const style::Theme& theme,
                                         const std::string& key) const {
        core::Widget column = core::makeColumn(
            std::move(children), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, style::spaceToken(4),
            core::EdgeInsets::all(style::spaceToken(4)));
        core::StyleOverrides overrides;
        overrides.background = theme.colors.surface;
        overrides.border = theme.colors.borderDefault;
        overrides.borderWidth = 1.0F;
        overrides.radius = core::CornerRadius::all(theme.metrics.cardRadius);
        return core::withKey(
            core::withStyleOverrides(std::move(column), std::move(overrides)),
            key);
    }

    // 面板头：标题（14px/650）+ 弹性空隙 + meta（12px muted）。
    [[nodiscard]] core::Widget panelHead(const std::string& title,
                                         const std::string& meta,
                                         const style::Theme& theme) const {
        std::vector<core::Widget> row{panelTitleText(title, theme)};
        if (!meta.empty()) {
            core::Widget spacer;
            spacer.flex = 1.0F;
            row.push_back(std::move(spacer));
            row.push_back(smallLabel(meta, theme));
        }
        return core::makeRow(std::move(row), core::MainAxisAlignment::Start,
                             core::CrossAxisAlignment::Center,
                             style::spaceToken(3));
    }

    // 指标卡：label / value / delta 三行（delta 默认 muted，正向变化才用
    // 状态色）；窄内容（<480）改为左说明右数值两列（设计稿响应式）。
    [[nodiscard]] core::Widget metricCard(const std::string& label,
                                          const std::string& value,
                                          const std::string& delta,
                                          const style::Theme& theme,
                                          const std::string& key, bool positive,
                                          bool compact) const {
        core::Widget content;
        if (compact) {
            core::Widget texts = core::makeColumn(
                {core::withKey(smallLabel(label, theme), key + "-label"),
                 core::withKey(deltaText(delta, theme, positive),
                               key + "-delta")},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, 4.0F);
            texts.flex = 1.0F;
            content = core::makeRow(
                {std::move(texts),
                 core::withKey(metricValueText(value, theme), key + "-value")},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center, 12.0F);
        } else {
            content = core::makeColumn(
                {core::withKey(smallLabel(label, theme), key + "-label"),
                 core::withKey(metricValueText(value, theme), key + "-value"),
                 core::withKey(deltaText(delta, theme, positive),
                               key + "-delta")},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, 6.0F);
        }
        core::Widget column = core::makeColumn(
            {std::move(content)}, core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Start, 0.0F, core::EdgeInsets::all(16.0F));
        column.flex = 1.0F;
        core::StyleOverrides overrides;
        overrides.background = theme.colors.surface;
        overrides.border = theme.colors.borderDefault;
        overrides.borderWidth = 1.0F;
        overrides.radius = core::CornerRadius::all(theme.metrics.cardRadius);
        return core::withKey(
            core::withStyleOverrides(std::move(column), std::move(overrides)),
            key);
    }

    // 清单瓷砖：surface-alt 背景 + 1px 边框 + (cardRadius-2) 圆角；
    // 整格可点（onClick 跳转分区），内部预览为真控件优先命中。
    [[nodiscard]] core::Widget tileShell(core::Widget preview,
                                         const std::string& title,
                                         const style::Theme& theme,
                                         const std::string& key,
                                         const std::string& onClick) const {
        core::Widget body = core::makeColumn(
            {core::withKey(tileTitleText(title, theme), key + "-title"),
             std::move(preview)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Start,
            8.0F, core::EdgeInsets::all(style::spaceToken(3)));
        body.onClick = onClick;
        core::StyleOverrides overrides;
        overrides.background = theme.colors.surfaceElevated;
        overrides.border = theme.colors.borderDefault;
        overrides.borderWidth = 1.0F;
        const float radius =
            theme.metrics.cardRadius > 2.0F ? theme.metrics.cardRadius - 2.0F
                                            : 0.0F;
        overrides.radius = core::CornerRadius::all(radius);
        return core::withKey(
            core::withStyleOverrides(std::move(body), std::move(overrides)),
            key);
    }

    // 侧栏 Live state 注记（设计稿虚线边框以实线近似；圆角 8）。
    [[nodiscard]] core::Widget liveStateNote(
        const style::Theme& theme) const {
        core::Widget column = core::makeColumn(
            {core::withKey(smallStrong("Live state", theme, 600),
                           "nav-note-title"),
             core::withKey(
                 smallLabel("Button clicks · " +
                                shell_.state().get("button-clicks"),
                            theme),
                 "nav-note-clicks"),
             core::withKey(smallLabel(
                               "Density · " +
                                   densityName(theme.metrics.density),
                               theme),
                           "nav-note-density")},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Start,
            4.0F, core::EdgeInsets::all(style::spaceToken(3)));
        core::StyleOverrides overrides;
        overrides.background = theme.colors.surfaceElevated;
        overrides.border = theme.colors.borderDefault;
        overrides.borderWidth = 1.0F;
        overrides.radius = core::CornerRadius::all(8.0F);
        return core::withKey(
            core::withStyleOverrides(std::move(column), std::move(overrides)),
            "nav-note");
    }

    // 窗口控制按钮（lumen-titlebar-design §5 / design/gallery.html
    // caption-button）：44px 宽、通高（外层标题栏行 Stretch 拉到 48px
    // 行高、右缘贴合窗口角），Tab 可聚焦；handler 即命令 id
    //（window-minimize/maximize/close，见 initialize）。close 传
    // WindowClose 变体：rest 幽灵、hover 实心警示红 + 反色（Windows
    // 惯例）；min/max 保持 Ghost（hover 常规表面派生）。
    [[nodiscard]] static core::Widget windowButton(
        core::IconId icon, const std::string& onClick,
        const style::Theme& theme,
        core::ButtonVariant variant = core::ButtonVariant::Ghost) {
        const float scale = theme.typography.body.fontSize / 14.0F;
        core::Widget button = core::makeButton(
            "", core::TextStyle{}, core::EdgeInsets{}, 0.0F, onClick,
            44.0F * scale, std::nullopt, onClick);
        button.buttonVariant = variant;
        // 设计稿 caption-button 无圆角（design/gallery.html titlebar）：
        // hover 高亮与 close 实心红都是通高直角矩形；按钮默认
        // controlRadius（resolver §7.1）对 chrome 件以 overrides 归零。
        button.styleOverrides.radius = core::CornerRadius::zero();
        // 图标盒 14px（design/gallery.html caption-button svg 14px；
        // 描边随盒宽折算 ≈1.6）。
        button.styleOverrides.iconSize = 14.0F * scale;
        return core::withIcon(std::move(button), icon);
    }

    [[nodiscard]] core::Widget swatch(core::Color color,
                                      const std::string& name,
                                      const style::Theme& theme) const {
        core::Widget box = core::makeContainerLeaf(
            48.0F, 24.0F, core::EdgeInsets{}, core::EdgeInsets{}, color,
            "swatch-" + name);
        core::Widget label = mutedLabel(name, theme);
        core::Widget row = core::makeRow(
            {core::withKey(std::move(box), "swatch-" + name),
             core::withKey(std::move(label), "swatch-label-" + name)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            style::spaceToken(2));
        return core::withKey(std::move(row), "swatch-row-" + name);
    }

    [[nodiscard]] static core::Widget titleText(
        std::string text, const style::Theme& theme) {
        return core::makeText(std::move(text), theme.typography.title);
    }
    [[nodiscard]] static core::Widget mutedLabel(
        std::string text, const style::Theme& theme) {
        core::StyleOverrides overrides;
        overrides.foreground = theme.colors.contentSecondary;
        return core::withStyleOverrides(core::makeText(std::move(text)),
                                        std::move(overrides));
    }

    // --- 排版辅助（design v1 2026-09 优化尺度：正文/标签 14px、辅助说明
    // 与代码 12px、hero 32px/600、指标值 28px/650；随 fontScale 同步缩放） ---

    // 显式字号基线 14px 对齐 typography.body，缩放比例保持可访问性一致。
    [[nodiscard]] static core::TextStyle scaledStyle(
        float size, int weight, const style::Theme& theme) {
        const float scale = theme.typography.body.fontSize / 14.0F;
        core::TextStyle style;
        style.fontSize = size * scale;
        style.weight = weight;
        return style;
    }
    [[nodiscard]] static core::Widget smallLabel(std::string text,
                                                 const style::Theme& theme) {
        core::TextStyle style = scaledStyle(12.0F, 500, theme);
        style.color = theme.colors.contentSecondary;
        return core::makeText(std::move(text), style);
    }
    // 内容头 kicker（accentContent/大写/字距 .08em）与 hero 标题。
    [[nodiscard]] static core::Widget kickerText(std::string text,
                                                 const style::Theme& theme) {
        core::TextStyle style = scaledStyle(12.0F, 600, theme);
        style.letterSpacing = 0.08F * style.fontSize;
        style.color = theme.colors.accentContent;
        return core::makeText(std::move(text), style);
    }
    [[nodiscard]] static core::TextStyle heroStyle(float size,
                                                   const style::Theme& theme) {
        core::TextStyle style = scaledStyle(size, 600, theme);
        style.letterSpacing = -0.025F * style.fontSize;
        style.lineHeight = 1.25F;
        return style;
    }
    [[nodiscard]] static core::Widget heroText(std::string text,
                                               const style::Theme& theme) {
        return core::makeText(std::move(text), heroStyle(32.0F, theme));
    }
    [[nodiscard]] static core::Widget heroDescription(
        std::string text, const style::Theme& theme) {
        core::TextStyle style = scaledStyle(14.0F, 400, theme);
        style.color = theme.colors.contentSecondary;
        style.lineHeight = 20.0F / 14.0F;
        return core::makeText(std::move(text), style);
    }
    [[nodiscard]] static core::Widget smallCapsLabel(
        std::string text, const style::Theme& theme) {
        core::TextStyle style = scaledStyle(12.0F, 600, theme);
        style.letterSpacing = 0.08F * style.fontSize;
        style.color = theme.colors.contentSecondary;
        return core::makeText(std::move(text), style);
    }
    [[nodiscard]] static core::Widget smallStrong(
        std::string text, const style::Theme& theme, int weight = 700) {
        return core::makeText(std::move(text),
                              scaledStyle(12.0F, weight, theme));
    }
    // 面板标题（panel-title 14px/650）。
    [[nodiscard]] static core::Widget panelTitleText(
        std::string text, const style::Theme& theme) {
        return core::makeText(std::move(text), scaledStyle(14.0F, 650, theme));
    }
    [[nodiscard]] static core::Widget tileTitleText(
        std::string text, const style::Theme& theme) {
        core::TextStyle style = scaledStyle(12.0F, 600, theme);
        return core::makeText(std::move(text), style);
    }
    [[nodiscard]] static core::Widget metricValueText(
        std::string text, const style::Theme& theme) {
        core::TextStyle style = scaledStyle(28.0F, 650, theme);
        style.lineHeight = 1.25F;
        return core::makeText(std::move(text), style);
    }
    // 指标 delta：默认 muted，正向变化才用状态色（设计稿 .positive）。
    [[nodiscard]] static core::Widget deltaText(
        std::string text, const style::Theme& theme, bool positive) {
        core::TextStyle style = scaledStyle(12.0F, 400, theme);
        style.color = positive ? theme.colors.statusSuccess
                               : theme.colors.contentSecondary;
        return core::makeText(std::move(text), style);
    }
    [[nodiscard]] static core::Widget codeText(std::string text,
                                               const style::Theme& theme,
                                               core::Color color) {
        core::TextStyle style = scaledStyle(12.0F, 400, theme);
        style.lineHeight = 18.0F / 12.0F;
        style.color = color;
        return core::makeText(std::move(text), style);
    }
    [[nodiscard]] static core::TextStyle brandMarkStyle(
        const style::Theme& theme) {
        core::TextStyle style = scaledStyle(14.0F, 800, theme);
        style.color = theme.colors.onAccent;
        return style;
    }
    [[nodiscard]] static core::TextStyle headerTitleStyle(
        const style::Theme& theme) {
        return scaledStyle(14.0F, 650, theme);
    }
    [[nodiscard]] static core::TextStyle statusPillStyle(
        const style::Theme& theme) {
        core::TextStyle style = scaledStyle(12.0F, 600, theme);
        style.color = theme.colors.accentContent;
        return style;
    }

    [[nodiscard]] static std::string hexColor(core::Color color) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.r,
                      color.g, color.b);
        return std::string(buffer);
    }
    [[nodiscard]] static std::string routeDisplayName(
        const std::string& route) {
        if (route == "home") {
            return "Overview";
        }
        if (route.empty()) {
            return route;
        }
        std::string name = route;
        name[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(name[0])));
        return name;
    }
    [[nodiscard]] static std::string toUpper(std::string text) {
        for (char& c : text) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return text;
    }
    [[nodiscard]] static core::Widget errorText(
        std::string text, const style::Theme& theme) {
        core::StyleOverrides overrides;
        overrides.foreground = theme.colors.errorContent;
        overrides.text = theme.typography.caption;
        return core::withStyleOverrides(core::makeText(std::move(text)),
                                        std::move(overrides));
    }
    [[nodiscard]] static core::Widget fieldWidget(std::string bind,
                                                  std::string placeholder,
                                                  std::string key,
                                                  bool invalid) {
        core::Widget field = core::makeTextField(
            "", std::move(placeholder), core::TextStyle{}, core::EdgeInsets{},
            0.0F, std::move(key));
        field.bind = std::move(bind);
        return core::withInvalid(std::move(field), invalid);
    }
    [[nodiscard]] static core::Widget buttonWidget(
        std::string label, std::string onClick, std::string key,
        core::ButtonVariant variant) {
        core::Widget button = core::makeButton(
            std::move(label), core::TextStyle{}, core::EdgeInsets{}, 0.0F,
            std::move(key), std::nullopt, std::nullopt, std::move(onClick));
        return core::withVariant(std::move(button), variant);
    }
    [[nodiscard]] static std::string densityName(
        style::ControlDensity density) {
        using style::ControlDensity;
        if (density == ControlDensity::Compact) {
            return "Compact";
        }
        if (density == ControlDensity::Touch) {
            return "Touch";
        }
        return "Comfortable";
    }

    [[nodiscard]] static std::string directionName(
        style::ThemeDirection direction) {
        switch (direction) {
            case style::ThemeDirection::CoreDark:
                return "Core Dark";
            case style::ThemeDirection::InkLinen:
                return "Ink & Linen";
            case style::ThemeDirection::AuroraSignal:
                return "Aurora Signal";
            case style::ThemeDirection::UtilityContrast:
                return "Utility Contrast";
        }
        return "Core Dark";
    }

    // --- Collections 数据模型（GalleryApp 拥有；buildRow 经 this 取主题） ---

    // 树模型：仓库目录骨架。key 为路径式全局唯一标识（叶/分支共用
    // entries_ 查询；roots_ 为根级子键序列）。
    class CollectionTreeModel final : public widgets::TreeModel {
      public:
        struct Entry {
            std::string label{};
            std::vector<std::string> children{};
        };

        explicit CollectionTreeModel(const GalleryApp* app) : app_(app) {}

        void setRoots(std::vector<std::string> roots) {
            roots_ = std::move(roots);
        }
        void setEntries(std::map<std::string, Entry> entries) {
            entries_ = std::move(entries);
        }

        [[nodiscard]] std::size_t childCount(
            const std::string& parent) const override {
            if (parent.empty()) {
                return roots_.size();
            }
            const auto it = entries_.find(parent);
            return it != entries_.end() ? it->second.children.size() : 0;
        }
        [[nodiscard]] std::string childAt(const std::string& parent,
                                          std::size_t index) const override {
            if (parent.empty()) {
                return index < roots_.size() ? roots_[index]
                                             : std::string{};
            }
            const auto it = entries_.find(parent);
            if (it == entries_.end() || index >= it->second.children.size()) {
                return {};
            }
            return it->second.children[index];
        }
        [[nodiscard]] bool hasChildren(const std::string& key) const override {
            const auto it = entries_.find(key);
            return it != entries_.end() && !it->second.children.empty();
        }
        [[nodiscard]] core::Widget buildRow(
            const std::string& key, std::size_t /*depth*/) const override {
            const auto it = entries_.find(key);
            const std::string& label = it != entries_.end() &&
                                               !it->second.label.empty()
                                           ? it->second.label
                                           : key;
            const auto& theme = app_->shell_.theme();
            auto icon = core::makeIcon(hasChildren(key) ? core::IconId::Folder : core::IconId::Document);
            icon.styleOverrides.foreground = theme.tree.chevronContent;
            auto text = core::makeText(label);
            text.flex = 1.0F;
            std::vector<core::Widget> parts{std::move(icon), std::move(text)};
            if (hasChildren(key)) {
                auto count = core::makeText(std::to_string(childCount(key)) + " items", theme.typography.caption);
                count.styleOverrides.foreground = theme.colors.contentSecondary;
                parts.push_back(std::move(count));
            }
            auto row = core::makeRow(std::move(parts), core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center, theme.metrics.controlGap[theme.metrics.baseIndex]);
            row.semanticsLabel = label;
            return row;
        }

      private:
        const GalleryApp* app_{};
        std::vector<std::string> roots_{};
        std::map<std::string, Entry> entries_{};
    };

    // 表格模型：扁平行（根级子键 = 行 id）。排序由 GalleryApp 执行后
    // modelChanged（框架永不重排数据）。
    class CollectionTableModel final : public widgets::TreeModel {
      public:
        struct Row {
            std::string id{};
            std::string name{};
            std::string type{};
            float sizeKb{0.0F};
        };

        explicit CollectionTableModel(const GalleryApp* app) : app_(app) {}

        void setRows(std::vector<Row> rows) { rows_ = std::move(rows); }
        [[nodiscard]] std::vector<Row>& rows() { return rows_; }
        [[nodiscard]] const Row* rowOf(const std::string& id) const {
            for (const auto& row : rows_) {
                if (row.id == id) {
                    return &row;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::size_t childCount(
            const std::string& parent) const override {
            return parent.empty() ? rows_.size() : 0;
        }
        [[nodiscard]] std::string childAt(const std::string& parent,
                                          std::size_t index) const override {
            if (!parent.empty() || index >= rows_.size()) {
                return {};
            }
            return rows_[index].id;
        }
        [[nodiscard]] bool hasChildren(const std::string&) const override {
            return false;
        }
        [[nodiscard]] core::Widget buildRow(
            const std::string& key, std::size_t) const override {
            // TreeList 列模式下单元格经 cellBuilder 构建；此实现仅为
            // 未设 cellBuilder 时的回退（首列显示行名）。
            const auto* row = rowOf(key);
            return core::makeText(row != nullptr ? row->name : key,
                                  app_->shell_.theme().typography.body);
        }

      private:
        const GalleryApp* app_{};
        std::vector<Row> rows_{};
    };

    core::ScrollController scroll_{};
    core::ScrollController dialogScroll_{};
    core::ScrollController scrollViewScroll_{};
    widgets::FormController form_{};
    widgets::NavigatorController navigator_{"home"};
    bool darkMode_{true};
    style::ThemeDirection direction_{style::ThemeDirection::CoreDark};
    bool followSystemTheme_{false};
    bool systemPrefersDark_{false};
    std::optional<core::Color> systemAccent_{};
    bool dialogOpen_{false};
    bool dialogClosing_{false};
    std::string dialogReturnKey_{};
    bool focusRestorePending_{false};
    std::string lastRoute_{};
    mutable core::VirtualListController library_{};
    // Collections（模型先于控制器声明 → 析构序安全：模型后于控制器销毁）。
    CollectionTreeModel collectionTreeModel_{this};
    CollectionTableModel collectionTableModel_{this};
    widgets::ListController collectionList_{};
    widgets::ListController collectionEmptyList_{};
    widgets::TreeController collectionTree_{};
    widgets::TreeController collectionEmptyTree_{};
    widgets::TreeListController collectionTable_{};
    std::string lastActivatedKey_{};
    std::string lastSortColumn_{};
    bool lastSortDescending_{false};
    // M11：下拉浮动菜单控制器（选项 + 当前值；选中回调写状态）。
    widgets::DropdownController navigationMenu_{{{"home", "Overview"}, {"buttons", "Buttons"},
        {"inputs", "Inputs"}, {"layout", "Layout"}, {"lists", "Lists"},
        {"collections", "Collections"}, {"menus", "Menus"}, {"controls", "Controls"},
        {"feedback", "Feedback"},
        {"theme", "Theme"}}, "home"};
    // 菜单类控件（menu-controls-design §11.3）：chrome 菜单栏（File/
    // View/Help）+ 右键菜单与最近命令回显。checkable 状态由应用维护。
    widgets::ContextMenuController contextMenu_{};
    widgets::MenuBarController menuBar_{};
    std::string lastMenuCommand_{"(none)"};
    // Splitter（splitter-design §10.3）：侧栏|内容主分栏（chrome 用法
    // 演示）。未手动调节时跟随 200/168 响应式断点。
    widgets::SplitterController sidebarSplitter_{200.0F};
    // Spin/ToolBar/StatusBar 控件（lumen-{spin,toolbar,statusbar}-design
    // §11.3）：Controls 页样本（toggle 经 StateStore 维护）。
    widgets::SpinController controlsOpacity_{72.0, "gal-opacity"};
    widgets::SpinController controlsFontSize_{13.0, "gal-font"};
    widgets::ToolBarController controlsToolBar_{"gal-tb"};
    widgets::StatusBarController controlsStatusBar_{"gal-sb"};
    std::string lastControlsCommand_{"(none)"};
    bool sidebarUserAdjusted_{false};
    bool syncingSidebarBreakpoint_{false};
    bool sidebarVisible_{true};
    // 自定义标题栏（lumen-titlebar-design §4.4）：平台窗口命令注入点、
    // 命令回显与最大化态（图标切换）。
    WindowCommands windowCommands_{};
    std::string lastWindowCommand_{"(none)"};
    bool windowMaximized_{false};
    widgets::DropdownController dropdown_{{{"Red", "Red"},
                                           {"Green", "Green"},
                                           {"Blue", "Blue"}},
                                          "Red"};
    std::shared_ptr<void> themeScopeData_{
        style::makeThemeScopeData(style::Theme::light())};

    app::AppShell shell_;
};

}  // namespace lumen::examples
