#pragma once

// Widget Gallery（与 counter/settings 同级示例）：演示当前已完成控件、
// 布局、颜色方案/主题。窗口主循环由 app::runApp 驱动；本文件只保留
// 应用层职责：build 函数、状态 key 与业务 handler（plan §6.1）。
// 覆盖：Button 变体/尺寸/状态、TextField/Checkbox/Switch/Radio/Slider/
// Dropdown/Tabs/ProgressBar/Icon/Tooltip/Dialog、Row/Column(flex)/Stack/
// Container/Grid、ListView/VirtualList、Theme 深浅/密度/强调色/局部
// ThemeScope/排版/语义色板。

#include <cmath>
#include <cstdint>
#include <memory>
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
#include "lumen/text/font_manager.h"
#include "lumen/widgets/form.h"
#include "lumen/widgets/navigator.h"

namespace lumen::examples {

class GalleryApp {
  public:
    GalleryApp() : shell_(configFor(this)) { initialize(); }

    GalleryApp(const GalleryApp&) = delete;
    GalleryApp& operator=(const GalleryApp&) = delete;
    GalleryApp(GalleryApp&&) = delete;
    GalleryApp& operator=(GalleryApp&&) = delete;

    void setView(core::Size size) { shell_.setView(size); }
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
    [[nodiscard]] bool dialogOpen() const { return dialogOpen_; }
    [[nodiscard]] bool dropdownOpen() const { return dropdown_.isOpen(); }
    [[nodiscard]] widgets::DropdownController& dropdown() { return dropdown_; }
    [[nodiscard]] bool darkMode() const { return darkMode_; }
    [[nodiscard]] style::ThemeDirection direction() const {
        return direction_;
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

    // Gallery 根：Header + Body(Row: 导航栏 + 内容 ListView) + Footer。
    // Body Row 本身即 flex 布局演示；内容页按路由切换。
    [[nodiscard]] core::Widget buildUi() const {
        const style::Theme& theme = shell_.theme();
        core::Widget header = buildHeader(theme);
        core::Widget nav = buildNav(theme);
        core::Widget content = buildContent(theme);
        content.flex = 1.0F;
        core::Widget body =
            core::makeRow({std::move(nav), std::move(content)},
                          core::MainAxisAlignment::Start,
                          core::CrossAxisAlignment::Stretch, 0.0F);
        body.flex = 1.0F;
        body.key = "gallery-body";
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
            core::Widget contentDialog = core::makeColumn(
                {core::withKey(titleText("Gallery dialog", theme),
                               "dialog-title"),
                 core::withKey(
                     core::makeText("All widgets share one Theme.",
                                    theme.typography.body),
                     "dialog-body"),
                 core::withKey(
                     buttonWidget("Close", "dismiss-dialog", "dialog-close",
                                  core::ButtonVariant::Tonal),
                     "dialog-close")},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, style::spaceToken(4),
                core::EdgeInsets::all(style::spaceToken(6)));
            core::Widget dialog = widgets::makeDialog(
                std::move(contentDialog), shell_.theme(), "dismiss-dialog",
                kDialogKey, shell_.view());
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
        config.build = [self] { return self->buildUi(); };
        config.onKey = [self](app::AppShell& shell, core::Key key,
                              core::KeyModifiers, char) {
            // M11：下拉菜单键盘导航（modal 优先于路由返回规则；未打开
            // 时 Up/Down 仍走滚动路径）。
            if (self->dropdown_.handleKey(shell, key)) {
                return true;
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
                shell.controller().focusFirstFocusable(shell.root());
            }
        };
        return config;
    }

    void initialize() {
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
        handlers["switch-tab-basic"] = [this] {
            shell_.state().set("gallery-tab", "Basic");
        };
        handlers["switch-tab-more"] = [this] {
            shell_.state().set("gallery-tab", "More");
        };
        handlers["show-dialog"] = [this] {
            dialogOpen_ = true;
            shell_.markDirty();
            shell_.requestFullRepaint();
        };
        handlers["dismiss-dialog"] = [this] { closeDialog(); };
        handlers["toggle-dark"] = [this] {
            darkMode_ = !darkMode_;
            // 经 fromSettings 派生：保留方向/高对比/字体缩放/减少动画，
            // 只切换深浅（直接用 Theme::dark/light 基线会丢弃派生）。
            shell_.setTheme(style::Theme::fromSettings(
                shell_.accessibilitySettings(), darkMode_,
                shell_.theme().metrics.density, direction_));
            refreshScopePreview();
            shell_.markDirty();
        };
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
                dialogOpen_ = true;
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

    // M11：切换 v0.4 视觉方向（保留深浅/密度/可访问性派生）。
    void applyDirection(style::ThemeDirection direction) {
        direction_ = direction;
        shell_.setTheme(style::Theme::fromSettings(
            shell_.accessibilitySettings(), darkMode_,
            shell_.theme().metrics.density, direction_));
        refreshScopePreview();
        shell_.markDirty();
    }

    // ThemeScope 预览跟随当前方向（浅色变体对比展示）。
    void refreshScopePreview() {
        themeScopeData_ = style::makeThemeScopeData(style::Theme::light(
            shell_.theme().metrics.density, direction_));
    }

    void go(const std::string& route) {        if (navigator_.current() == route) {
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
        // 强调色切换：只覆盖当前主题的 accent 相关 token，保留深浅/密度/
        // 高对比等全部派生。刻意不用 adaptPlatformTheme——它只取
        // (density, fontScale) 重建基线，会丢弃已开启的高对比派生。
        style::Theme theme = shell_.theme();
        theme.colors.accent = accent;
        theme.button.filled.background = accent;
        shell_.setTheme(std::move(theme));
        shell_.markDirty();
    }

    void closeDialog() {
        dialogOpen_ = false;
        shell_.markDirty();
        shell_.requestFullRepaint();
        focusRestorePending_ = true;
    }

    // M10：视口拖动滚动与惯性推进（VirtualList 用 library 的控制器）。
    bool scrollDrag(const core::RenderNode* viewport, float deltaY,
                    core::ScrollDragPhase phase, std::uint64_t nowMs) {
        core::ScrollController* scroll = &scroll_;
        if (viewport != nullptr &&
            viewport->type == core::WidgetType::VirtualList) {
            scroll = &library_.scroll();
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

    // --- 页面骨架 ---

    [[nodiscard]] core::Widget buildHeader(const style::Theme& theme) const {
        core::Widget title = titleText("Lumen Widget Gallery", theme);
        title.key = "gallery-header-title";
        core::Widget modeLabel = mutedLabel(
            darkMode_ ? "Dark" : "Light", theme);
        modeLabel.key = "gallery-mode-label";
        core::Widget toggle = buttonWidget(
            darkMode_ ? "Switch to light" : "Switch to dark", "toggle-dark",
            "toggle-dark-button", core::ButtonVariant::Outline);
        core::Widget dialogButton = buttonWidget(
            "Show dialog", "show-dialog", "show-dialog-button",
            core::ButtonVariant::Tonal);
        core::Widget actions = core::makeRow(
            {std::move(modeLabel), core::withKey(std::move(toggle),
                                                 "toggle-dark-button"),
             core::withKey(std::move(dialogButton), "show-dialog-button")},
            core::MainAxisAlignment::End, core::CrossAxisAlignment::Center,
            style::spaceToken(2));
        actions.flex = 1.0F;
        core::Widget row = core::makeRow(
            {std::move(title), std::move(actions)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            style::spaceToken(4), core::EdgeInsets::all(style::spaceToken(4)));
        row.key = "gallery-header-row";
        return core::withKey(
            core::makeContainer(std::move(row), std::nullopt, std::nullopt,
                                core::EdgeInsets{}, core::EdgeInsets{},
                                theme.colors.surface),
            "gallery-header");
    }

    [[nodiscard]] core::Widget buildNav(const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(mutedLabel("Sections", theme),
                                      "nav-caption"));
        const std::pair<const char*, const char*> sections[] = {
            {"Home", "home"},       {"Buttons", "buttons"},
            {"Inputs", "inputs"},   {"Layout", "layout"},
            {"Lists", "lists"},     {"Feedback", "feedback"},
            {"Theme", "theme"},
        };
        for (const auto& [label, route] : sections) {
            const bool active = navigator_.current() == route;
            core::Widget button = buttonWidget(
                label, std::string("goto-") + route,
                std::string("nav-") + route,
                active ? core::ButtonVariant::Filled
                       : core::ButtonVariant::Ghost);
            if (active) {
                button = core::withSelected(std::move(button), true);
            }
            items.push_back(
                core::withKey(std::move(button), std::string("nav-") + route));
        }
        core::Widget column = core::makeColumn(
            std::move(items), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Start, style::spaceToken(2),
            core::EdgeInsets::all(style::spaceToken(4)));
        column.key = "gallery-nav-column";
        core::Widget nav = core::makeContainer(
            std::move(column), 200.0F, std::nullopt, core::EdgeInsets{},
            core::EdgeInsets{}, theme.colors.surface);
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
        } else if (route == "feedback") {
            items = buildFeedbackItems(theme);
        } else if (route == "theme") {
            items = buildThemeItems(theme);
        } else {
            items = buildHomeItems(theme);
        }
        core::Widget column = core::makeColumn(
            std::move(items), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Start, style::spaceToken(4),
            core::EdgeInsets::all(style::spaceToken(6)));
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

    [[nodiscard]] core::Widget buildFooter(const style::Theme& theme) const {
        std::string status = "Route: " + navigator_.current() +
                             " | Theme: " + (darkMode_ ? "dark" : "light") +
                             " | Dropdown: " +
                             (dropdown_.isOpen() ? "open" : "closed");
        core::Widget label = mutedLabel(std::move(status), theme);
        label.key = "gallery-footer-label";
        core::Widget row = core::makeRow(
            {std::move(label)}, core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Center, 0.0F,
            core::EdgeInsets::all(style::spaceToken(3)));
        row.key = "gallery-footer-row";
        return core::withKey(
            core::makeContainer(std::move(row), std::nullopt, std::nullopt,
                                core::EdgeInsets{}, core::EdgeInsets{},
                                theme.colors.surface),
            "gallery-footer");
    }

    // --- 各分区内容 ---

    [[nodiscard]] std::vector<core::Widget> buildHomeItems(
        const style::Theme& theme) const {
        std::vector<core::Widget> items;
        items.push_back(core::withKey(titleText("Overview", theme),
                                      "home-title"));
        items.push_back(core::withKey(
            mutedLabel("Pick a section on the left. Every control below is "
                       "live: clicks, text input, sliders and scrolling all "
                       "run through the same state/layout/paint pipeline.",
                       theme),
            "home-desc"));
        std::vector<core::Widget> cards;
        const std::pair<const char*, const char*> sections[] = {
            {"Buttons", "buttons"}, {"Inputs", "inputs"},
            {"Layout", "layout"},   {"Lists", "lists"},
            {"Feedback", "feedback"}, {"Theme", "theme"},
        };
        for (const auto& [label, route] : sections) {
            core::Widget button = buttonWidget(
                label, std::string("goto-") + route,
                std::string("goto-") + route + "-button",
                core::ButtonVariant::Filled);
            cards.push_back(core::withKey(
                std::move(button), std::string("goto-") + route + "-button"));
        }
        core::Widget grid =
            core::makeGrid(std::move(cards), 0, 160.0F, 8.0F, 8.0F,
                           "home-grid");
        items.push_back(core::withKey(std::move(grid), "home-grid"));
        items.push_back(sectionCard(
            "Live state", {core::withKey(mutedLabel("Button clicks: " +
                                                        shell_.state().get(
                                                            "button-clicks"),
                                                    theme),
                                         "home-clicks")},
            theme, "home-state-card"));
        return items;
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
                core::makeRow(std::move(variants),
                              core::MainAxisAlignment::Start,
                              core::CrossAxisAlignment::Center,
                              style::spaceToken(2)),
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
                core::makeRow(std::move(sizes),
                              core::MainAxisAlignment::Start,
                              core::CrossAxisAlignment::Center,
                              style::spaceToken(2)),
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
                core::makeRow(std::move(states),
                              core::MainAxisAlignment::Start,
                              core::CrossAxisAlignment::Center,
                              style::spaceToken(2)),
                "buttons-states-row"),
             core::withKey(buttonWidget("Back", "back", "back-button",
                                        core::ButtonVariant::Outline),
                           "back-button")},
            theme, "buttons-states-card"));
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
        core::Widget dropdown = core::makeDropdown(
            shell_.state().get("color"), "open-dropdown", "color-dropdown");
        dropdown.bind = "color";
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
        formItems.push_back(
            core::withKey(mutedLabel("Nickname", theme), "nickname-label"));
        formItems.push_back(core::withKey(
            fieldWidget("nickname", "Nickname", "nickname-field",
                        form_.errors().count("nickname") != 0),
            "nickname-field"));
        if (form_.errors().count("nickname") != 0) {
            formItems.push_back(core::withKey(
                errorText(form_.errors().at("nickname"), theme),
                "nickname-error"));
        }
        formItems.push_back(
            core::withKey(mutedLabel("Email", theme), "email-label"));
        formItems.push_back(core::withKey(
            fieldWidget("email", "name@example.com", "email-field",
                        form_.errors().count("email") != 0),
            "email-field"));
        if (form_.errors().count("email") != 0) {
            formItems.push_back(core::withKey(
                errorText(form_.errors().at("email"), theme), "email-error"));
        }
        formItems.push_back(core::withKey(
            buttonWidget("Save", "save", "save-button",
                         core::ButtonVariant::Filled),
            "save-button"));
        items.push_back(sectionCard("Form validation", std::move(formItems),
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

    [[nodiscard]] core::Widget sectionCard(
        const std::string& title, std::vector<core::Widget> children,
        const style::Theme& theme, const std::string& key) const {
        children.insert(children.begin(),
                        core::withKey(mutedLabel(title, theme), key + "-cap"));
        core::Widget column = core::makeColumn(
            std::move(children), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Start, style::spaceToken(3),
            core::EdgeInsets::all(style::spaceToken(4)));
        core::Widget card = core::makeContainer(
            std::move(column), std::nullopt, std::nullopt, core::EdgeInsets{},
            core::EdgeInsets{}, theme.colors.surface,
            core::CornerRadius::all(theme.metrics.cardRadius), key);
        return core::withKey(std::move(card), key);
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
    [[nodiscard]] static core::Widget errorText(
        std::string text, const style::Theme& theme) {
        core::StyleOverrides overrides;
        overrides.foreground = theme.colors.statusError;
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

    core::ScrollController scroll_{};
    widgets::FormController form_{};
    widgets::NavigatorController navigator_{"home"};
    bool darkMode_{true};
    style::ThemeDirection direction_{style::ThemeDirection::CoreDark};
    bool dialogOpen_{false};
    bool focusRestorePending_{false};
    mutable core::VirtualListController library_{};
    // M11：下拉浮动菜单控制器（选项 + 当前值；选中回调写状态）。
    widgets::DropdownController dropdown_{{{"Red", "Red"},
                                           {"Green", "Green"},
                                           {"Blue", "Blue"}},
                                          "Red"};
    std::shared_ptr<void> themeScopeData_{
        style::makeThemeScopeData(style::Theme::light())};

    app::AppShell shell_;
};

}  // namespace lumen::examples
