#pragma once

// Widget Gallery（与 counter/settings 同级示例）：演示当前已完成控件、
// 布局、颜色方案/主题。窗口主循环由 app::runApp 驱动；本文件只保留
// 应用层职责：build 函数、状态 key 与业务 handler（plan §6.1）。
// 壳层与 Overview 首屏对齐 design/gallery.html v1「Core Dark」设计稿
//（2026-09 尺度优化：正文/标签 14px、辅助 12px、主控件 40px、紧凑样本
// 32px、4px 网格）：窗口顶栏（品牌标/状态胶囊/窗口操作）、200px 侧栏
//（<1024 收窄 168；导航 + Live state 注记）、kicker/hero/指标卡/双栏
// 面板（Control inventory、DSL 快照、Resolved tokens、Theme controls）、
// 双侧页脚；主内容 <720 上下堆叠。视觉全部来自 Theme token。
// 覆盖：Button 变体/尺寸/状态、TextField/Checkbox/Switch/Radio/Slider/
// Dropdown/Tabs/ProgressBar/Icon/Tooltip/Dialog、Row/Column(flex)/Stack/
// Container/Grid、ListView/VirtualList、Theme 深浅/密度/强调色/局部
// ThemeScope/排版/语义色板。

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
#include "lumen/widgets/navigator.h"
#include "lumen/widgets/tree.h"

namespace lumen::examples {

class GalleryApp {
  public:
    GalleryApp() : shell_(configFor(this)) { initialize(); }

    GalleryApp(const GalleryApp&) = delete;
    GalleryApp& operator=(const GalleryApp&) = delete;
    GalleryApp(GalleryApp&&) = delete;
    GalleryApp& operator=(GalleryApp&&) = delete;

    void setView(core::Size size) { shell_.setView(size); }
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
    // 主内容可用宽（决定内容栅格单双列与窄版指标卡）。
    [[nodiscard]] float contentColumnWidth() const {
        const float sidebar = compactNavigation() ? 0.0F : sidebarWidth();
        return shell_.view().width - sidebar - 2.0F * contentPadding();
    }

    // Gallery 根：Header + Body(Row: 导航栏 + 内容 ListView) + Footer。
    // Body Row 本身即 flex 布局演示；内容页按路由切换。
    [[nodiscard]] core::Widget buildUi() const {
        const style::Theme& theme = shell_.theme();
        core::Widget header = buildHeader(theme);
        core::Widget nav = buildNav(theme);
        core::Widget content = buildContent(theme);
        content.flex = 1.0F;
        core::Widget divider = core::makeContainerLeaf(
            1.0F, std::nullopt, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.borderDefault, "gallery-body-divider");
        core::Widget body = compactNavigation() ? std::move(content) :
            core::makeRow({std::move(nav), std::move(divider), std::move(content)},
                          core::MainAxisAlignment::Start,
                          core::CrossAxisAlignment::Stretch, 0.0F);
        body.flex = 1.0F;
        if (!compactNavigation()) body.key = "gallery-body";
        core::Widget footer = buildFooter(theme);
        core::Widget page = core::makeColumn(
            {std::move(header), std::move(body), std::move(footer)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Start,
            0.0F);
        page.flex = 1.0F;
        core::Widget ui = core::makeContainer(
            std::move(page), std::nullopt, std::nullopt, core::EdgeInsets{},
            core::EdgeInsets{}, theme.colors.pageBackground);
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
                std::move(contentDialog), std::move(close), shell_.theme(), "dismiss-dialog",
                kDialogKey, shell_.view(), dialogScroll_.offset());
            ui = core::makeStack({std::move(ui), std::move(dialog)});
            ui.key = "root";
        }
        return ui;
    }

  private:
    static constexpr const char* kDialogKey = "gallery-dialog";
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
                   core::Offset /*position*/, core::Offset delta) {
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
            return self->advanceFling(shell, nowMs);
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
        handlers["goto-feedback"] = [this] { go("feedback"); };
        handlers["goto-theme"] = [this] { go("theme"); };
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
            core::StyleOverrides muted;
            muted.foreground = shell_.theme().colors.contentSecondary;
            core::Widget sizeText = core::withStyleOverrides(
                core::makeText(size), std::move(muted));
            return core::makeRow(
                {std::move(name), std::move(sizeText)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center, 8.0F);
        });
        collectionList_.onActivated = [this](const std::string& key) {
            lastActivatedKey_ = key;
            shell_.markDirty();
        };
        collectionList_.attach(shell_, "collection-list");

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
        collectionTree_.expand("tree:src");
        collectionTree_.expand("tree:src:core");

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

    // M10：视口拖动滚动与惯性推进（VirtualList 用 library 的控制器）。
    bool scrollDrag(const core::RenderNode* viewport, float deltaY,
                    core::ScrollDragPhase phase, std::uint64_t nowMs) {
        core::ScrollController* scroll = &scroll_;
        if (viewport != nullptr &&
            viewport->type == core::WidgetType::VirtualList) {
            scroll = &library_.scroll();
        }
        if (viewport && viewport->key == std::string(kDialogKey) + "-body-scroll") {
            scroll = &dialogScroll_;
        }
        switch (phase) {
            case core::ScrollDragPhase::Begin:
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
                scroll->stopFling();
                return false;
        }
        return false;
    }

    bool advanceFling(app::AppShell& shell, std::uint64_t nowMs) {
        bool active = false;
        if (scroll_.isFlinging()) {
            active = scroll_.stepFling(nowMs);
            shell.markDirty();
        }
        if (library_.scroll().isFlinging()) {
            active = library_.scroll().stepFling(nowMs) || active;
            shell.markDirty();
        }
        return active;
    }

    bool scrollWheel(const core::RenderNode& root, const core::RenderNode* hit,
                     float deltaY) {
        if (hit && hit->key == std::string(kDialogKey) + "-body-scroll") {
            dialogScroll_.updateExtents(hit->size.height, hit->size.height + hit->scrollExtent);
            const bool changed = dialogScroll_.applyWheel(deltaY);
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
        if (viewport->type == core::WidgetType::VirtualList) {
            library_.scroll().updateExtents(
                viewport->size.height,
                viewport->size.height + viewport->scrollExtent);
            if (std::abs(deltaY) > 1e8F) {
                library_.scroll().scrollTo(deltaY > 0
                                               ? library_.scroll()
                                                     .maxScrollOffset()
                                               : 0.0F);
                shell_.markDirty();
                return true;
            }
            const bool moved = library_.scroll().applyWheel(deltaY);
            if (moved) {
                shell_.markDirty();
            }
            return moved;
        }
        // 集合控件（List/Tree/TreeList）：按节点 key 路由到所属控制器的
        // ScrollController（三个集合可共存，滚轮只作用于命中链最近的视口）。
        if (viewport->type == core::WidgetType::List ||
            viewport->type == core::WidgetType::Tree ||
            viewport->type == core::WidgetType::TreeList) {
            core::ScrollController* scroller = nullptr;
            if (viewport->key == "collection-list") {
                scroller = &collectionList_.scroll();
            } else if (viewport->key == "collection-tree") {
                scroller = &collectionTree_.scroll();
            } else if (viewport->key == "collection-table") {
                scroller = &collectionTable_.scroll();
            }
            if (scroller != nullptr) {
                scroller->updateExtents(
                    viewport->size.height,
                    viewport->size.height + viewport->scrollExtent);
                if (std::abs(deltaY) > 1e8F) {
                    scroller->scrollTo(deltaY > 0
                                           ? scroller->maxScrollOffset()
                                           : 0.0F);
                    shell_.markDirty();
                    return true;
                }
                const bool moved = scroller->applyWheel(deltaY);
                if (moved) {
                    shell_.markDirty();
                }
                return moved;
            }
        }
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

    // 窗口顶栏：品牌标 + 标题 | 状态胶囊 + 视口尺寸 | 窗口操作（装饰性
    // 图标——真实窗口控件由 OS 标题栏提供）。
    [[nodiscard]] core::Widget buildHeader(const style::Theme& theme) const {
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
        title.key = "gallery-header-title";
        title.textStyle.maxLines = 1;
        title.textStyle.overflow = core::TextOverflow::Ellipsis;
        title.flex = 1.0F;
        core::Widget brand = core::makeRow(
            {std::move(mark), std::move(title)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            10.0F);
        brand.key = "gallery-brand";
        brand.flex = 1.0F;

        core::Widget pill = core::makeRow(
            {core::makeText("Desktop preview", statusPillStyle(theme))},
            core::MainAxisAlignment::Center, core::CrossAxisAlignment::Center,
            0.0F, core::EdgeInsets::symmetric(8.0F, 4.0F), core::EdgeInsets{},
            "gallery-status-pill", std::nullopt,
            // 设计稿 24px（4px 上下内距 + 单行文字）；fontScale 放大时
            // 按行高推导，避免固定高度裁字。
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
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            8.0F);
        status.key = "gallery-status";

        core::Widget actions = core::makeRow(
            {windowAction(core::IconId::Minus, "window-minimize", theme),
             windowAction(core::IconId::Maximize, "window-maximize", theme),
             windowAction(core::IconId::Close, "window-close", theme)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            4.0F);
        actions.key = "gallery-window-actions";

        // 设计稿 ≤1024 隐藏窗口状态（窗口操作保留）；≤720 顶栏收窄内距。
        std::vector<core::Widget> rowChildren;
        rowChildren.push_back(std::move(brand));
        core::Widget spacer;
        spacer.flex = 1.0F;
        rowChildren.push_back(std::move(spacer));
        if (!narrowWindow()) {
            rowChildren.push_back(std::move(status));
        }
        rowChildren.push_back(std::move(actions));
        const float headerPaddingX = compactNavigation() ? 16.0F : 20.0F;
        core::Widget row = core::makeRow(
            std::move(rowChildren),
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            style::spaceToken(4),
            core::EdgeInsets::symmetric(headerPaddingX, 12.0F));
        row.key = "gallery-header-row";
        core::Widget bottom = core::makeContainerLeaf(
            std::nullopt, 1.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.borderDefault, "gallery-header-divider");
        std::vector<core::Widget> headerRows;
        headerRows.push_back(std::move(row));
        if (compactNavigation()) {
            auto navigation = core::makeDropdown(routeDisplayName(navigator_.current()),
                "open-navigation", "compact-navigation");
            headerRows.push_back(core::makeContainer(std::move(navigation), std::nullopt,
                std::nullopt, core::EdgeInsets::only(20, 0, 20, 12)));
        }
        headerRows.push_back(std::move(bottom));
        core::Widget header = core::makeColumn(
            std::move(headerRows), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 0.0F);
        return core::withKey(
            core::makeContainer(std::move(header), std::nullopt, std::nullopt,
                                core::EdgeInsets{}, core::EdgeInsets{},
                                theme.colors.surface),
            "gallery-header");
    }

    // 侧栏：分区标签 + 路由导航（当前项 Tonal 强调）+ 分隔线 + Live state
    // 注记（设计稿虚线边框以实线近似）。背景即页面底色。宽 200px，
    // ≤1024 收窄 168px（设计稿容器断点）。
    [[nodiscard]] core::Widget buildNav(const style::Theme& theme) const {
        const std::pair<const char*, const char*> sections[] = {
            {"Overview", "home"}, {"Buttons", "buttons"},
            {"Inputs", "inputs"},   {"Layout", "layout"},
            {"Lists", "lists"},     {"Collections", "collections"},
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

    [[nodiscard]] core::Widget buildContent(const style::Theme& theme) const {
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

    // Control inventory：六个分区瓷砖（预览即真控件，点击整格跳转分区）。
    [[nodiscard]] core::Widget inventoryPanel(
        const style::Theme& theme) const {
        std::vector<core::Widget> tiles;
        tiles.push_back(buttonsTile(theme));
        tiles.push_back(inputsTile(theme));
        tiles.push_back(togglesTile(theme));
        tiles.push_back(layoutTile(theme));
        tiles.push_back(listsTile(theme));
        tiles.push_back(collectionsTile(theme));
        tiles.push_back(feedbackTile(theme));
        std::vector<core::Widget> body;
        body.push_back(panelHead("Control inventory", "7 sections", theme));
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

        // --- List：Extended 选择 + 双击/Enter 激活 + Ctrl+A ---
        core::Widget listWidget = core::makeList(
            &collectionList_, "collection-list", std::nullopt, 300.0F);
        items.push_back(sectionCard(
            "List — selection & activation",
            {core::withKey(mutedLabel(collectionListStatus(), theme),
                           "collection-list-status"),
             core::withKey(std::move(listWidget), "collection-list"),
             core::withKey(mutedLabel("Click selects; Ctrl+click toggles; "
                                      "Shift+click extends; double-click or "
                                      "Enter activates; Ctrl+A selects all.",
                                      theme),
                           "collection-list-hint")},
            theme, "collections-list-card", "Extended · 200 rows"));

        // --- Tree：层级模型 + 键盘 Left/Right + 按 key 的展开状态 ---
        core::Widget treeWidget = core::makeTree(
            &collectionTree_, "collection-tree", std::nullopt, 280.0F);
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

        // --- TreeList：列系统 + 粘性表头 + 排序钩子 ---
        core::Widget tableWidget = core::makeTreeList(
            &collectionTable_, &collectionTable_.columns(), true,
            "collection-table", std::nullopt, 280.0F);
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
        std::string text = "Selected: " +
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

    // 窗口操作图标（装饰性；真实窗口控件由 OS 标题栏提供）。设计稿 32×32。
    [[nodiscard]] static core::Widget windowAction(
        core::IconId icon, const std::string& key,
        const style::Theme& theme) {
        core::StyleOverrides overrides;
        overrides.foreground = theme.colors.contentSecondary;
        const float size = 32.0F * (theme.typography.body.fontSize / 14.0F);
        return core::withStyleOverrides(
            core::makeIcon(icon, key, size, size), std::move(overrides));
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
            return core::makeText(label, app_->shell_.theme().typography.body);
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
    widgets::TreeController collectionTree_{};
    widgets::TreeListController collectionTable_{};
    std::string lastActivatedKey_{};
    std::string lastSortColumn_{};
    bool lastSortDescending_{false};
    // M11：下拉浮动菜单控制器（选项 + 当前值；选中回调写状态）。
    widgets::DropdownController navigationMenu_{{{"home", "Overview"}, {"buttons", "Buttons"},
        {"inputs", "Inputs"}, {"layout", "Layout"}, {"lists", "Lists"},
        {"collections", "Collections"}, {"feedback", "Feedback"}, {"theme", "Theme"}}, "home"};
    widgets::DropdownController dropdown_{{{"Red", "Red"},
                                           {"Green", "Green"},
                                           {"Blue", "Blue"}},
                                          "Red"};
    std::shared_ptr<void> themeScopeData_{
        style::makeThemeScopeData(style::Theme::light())};

    app::AppShell shell_;
};

}  // namespace lumen::examples
