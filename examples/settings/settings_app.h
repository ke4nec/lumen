#pragma once

// v0.3 阶段8D settings 示例（plan §4 8D）：滚动列表、表单校验、弹窗、
// 导航、主题与无障碍标签的最小完整应用。M2 起帧管线由 app::AppShell\n// 拥有；本文件保留应用层职责：build 函数（读导航/表单/滚动/弹窗状态）、\n// 业务 handler、Escape/关闭统一规则与滚动控制器（plan §6.1）。
// 公共 API 保持迁移前签名——headless 集成测试与窗口循环共用。

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/damage.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"
#include "lumen/core/scroll.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/state.h"
#include "lumen/core/widget.h"
#include "lumen/render/renderer.h"
#include "lumen/style/theme.h"
#include "lumen/text/font_manager.h"
#include "lumen/widgets/form.h"
#include "lumen/widgets/navigator.h"

namespace lumen::examples {

class SettingsApp {
  public:
    SettingsApp() : shell_(configFor(this)) { initialize(); }

    SettingsApp(const SettingsApp&) = delete;
    SettingsApp& operator=(const SettingsApp&) = delete;
    SettingsApp(SettingsApp&&) = delete;
    SettingsApp& operator=(SettingsApp&&) = delete;

    // --- 装配/帧管线：转发到应用壳（M2 收敛点） ---

    void setView(core::Size size) { shell_.setView(size); }
    void setDeviceScale(float scale) { shell_.setDeviceScale(scale); }
    void setRenderer(render::Renderer* renderer) {
        shell_.setRenderer(renderer);
    }

    // M1：注入正式字体事实（Skia 后端激活时由 main 创建；空 = 占位）。
    void setFontManager(std::shared_ptr<const text::FontManager> fonts) {
        shell_.setFontManager(std::move(fonts));
    }

    void rebuildIfDirty() { shell_.rebuildIfDirty(); }

    std::uint64_t renderFrame(bool forceFullRepaint = false) {
        return shell_.renderFrame(forceFullRepaint);
    }

    // 可访问性设置变化 → 派生 Theme（font scale/high contrast/density/
    // reduce animation，visual-system §4；darkMode 由应用跟踪）。
    void setAccessibilitySettings(
        accessibility::AccessibilitySettings settings) {
        shell_.setAccessibilitySettings(settings, darkMode_);
    }
    [[nodiscard]] const accessibility::AccessibilitySettings&
    accessibilitySettings() const {
        return shell_.accessibilitySettings();
    }

    void setTheme(style::Theme theme, bool forceFullRepaint = true) {
        const style::Theme lightBaseline =
            style::Theme::light(theme.metrics.density);
        const style::Theme darkBaseline =
            style::Theme::dark(theme.metrics.density);
        if (theme.colors.pageBackground == lightBaseline.colors.pageBackground) {
            darkMode_ = false;
        } else if (theme.colors.pageBackground ==
                   darkBaseline.colors.pageBackground) {
            darkMode_ = true;
        }
        shell_.setTheme(std::move(theme), forceFullRepaint);
    }
    [[nodiscard]] const style::Theme& theme() const {
        return shell_.theme();
    }

    void tick(std::uint64_t nowMs) { shell_.tick(nowMs); }
    void swapRoot(core::Widget root) { shell_.swapRoot(std::move(root)); }
    void markDirty() { shell_.markDirty(); }

    // --- M4：平台服务动作（main 注入；测试可换 fake） ---
    struct ServiceActions {
        // 打开文件对话框（异步：结果经 onEvent → setPickedFile）。
        std::function<bool()> openFile{};
        std::function<bool()> saveFile{};
        std::function<bool()> notify{};
        std::function<bool()> openDocs{};
    };
    void setServiceActions(ServiceActions actions) {
        serviceActions_ = std::move(actions);
    }
    // 文件选择完成（FileDialogCompleted 事件落地）。
    void setPickedFile(const std::string& path) {
        pickedFile_ = path.empty() ? "(cancelled)" : path;
        shell_.markDirty();
    }
    [[nodiscard]] const std::string& pickedFile() const {
        return pickedFile_;
    }

    // --- 事件分发（Escape/返回统一规则在 configFor 的 onKey 钩子） ---

    void pointerDown(core::Offset position) { shell_.pointerDown(position); }
    void pointerMove(core::Offset position) { shell_.pointerMove(position); }
    void pointerUp(core::Offset position) { shell_.pointerUp(position); }
    void pointerCancel() { shell_.pointerCancel(); }
    void wheel(core::Offset position, core::Offset delta) {
        shell_.wheel(position, delta);
    }
    void textInput(const std::string& text) { shell_.textInput(text); }
    void textEditing(const std::string& text) { shell_.textEditing(text); }

    void keyDown(core::Key key,
                 core::KeyModifiers modifiers = core::kModifierNone,
                 char keyChar = 0) {
        shell_.keyDown(key, modifiers, keyChar);
    }

    // --- 应用状态/查询 ---

    // 应用壳访问（M2：窗口循环经 runApp 驱动）。
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
    [[nodiscard]] core::Size view() const { return shell_.view(); }
    [[nodiscard]] render::RendererCapabilities capabilities() {
        return shell_.capabilities();
    }
    [[nodiscard]] const render::PixelBuffer& pixels() const {
        return shell_.pixels();
    }
    [[nodiscard]] std::uint32_t partialRepaintCount() const {
        return shell_.partialRepaintCount();
    }

    // 语义树（无障碍结构测试）。
    [[nodiscard]] accessibility::SemanticsTree semantics() const {
        accessibility::SemanticsBuildOptions options;
        options.focus = &shell_.focus();
        return accessibility::buildSemanticsTree(shell_.root(), options);
    }

    // 语义 scroll action 的测试钩子（命中列表视口并滚动）。
    [[nodiscard]] bool scrollWheelForTests(float deltaY) {
        const core::RenderNode* list =
            core::findNodeByKey(shell_.root(), "settings-list");
        return list != nullptr &&
               scrollWheel(shell_.root(), list, deltaY);
    }

    // settings 页面：home = 滚动列表（开关/勾选/关于行）；form = 资料
    // 表单（校验 + 提交弹窗）。语义标签覆盖到每个控件。
    [[nodiscard]] core::Widget buildUi() const {
        const style::Theme& theme = shell_.theme();
        std::vector<core::Widget> page;

        if (navigator_.current() == "grid") {
            // M3：Grid 页面（最小列宽自适应，窗口变化重排）。
            std::vector<core::Widget> cells;
            for (int i = 0; i < 18; ++i) {
                core::Widget cell = mutedLabel("Tile " + std::to_string(i),
                                               theme);
                cell.key = "tile-" + std::to_string(i);
                cells.push_back(std::move(cell));
            }
            std::vector<core::Widget> pageItems;
            pageItems.push_back(
                core::withKey(titleText("Library grid", theme), "grid-title"));
            pageItems.push_back(core::withKey(
                buttonWidget("Back", "back", "back-button",
                             core::ButtonVariant::Outline),
                "back-button"));
            core::Widget grid =
                core::makeGrid(std::move(cells), 0, 160.0F, 8.0F, 8.0F,
                               "tile-grid");
            pageItems.push_back(core::withKey(std::move(grid), "tile-grid"));
            core::Widget column = core::makeColumn(
                std::move(pageItems), core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, style::spaceToken(4),
                core::EdgeInsets::all(style::spaceToken(6)));
            column.flex = 1.0F;
            page.push_back(core::withKey(
                core::withScrollOffset(
                    core::makeListView(std::move(column), "settings-list"),
                    scroll_.offset()),
                "settings-list"));
        } else if (navigator_.current() == "library") {
            // M3：千项 VirtualList 页面（可见区物化 + 实测 extent 修正）。
            std::vector<core::Widget> pageItems;
            pageItems.push_back(core::withKey(
                titleText("Library (1000 items)", theme), "library-title"));
            pageItems.push_back(core::withKey(
                buttonWidget("Back", "back", "back-button",
                             core::ButtonVariant::Outline),
                "back-button"));
            core::Widget listWidget = core::makeVirtualList(
                &library_, "library-list", std::nullopt, std::nullopt,
                200.0F);
            listWidget.flex = 1.0F;
            pageItems.push_back(core::withKey(std::move(listWidget),
                                              "library-list"));
            core::Widget column = core::makeColumn(
                std::move(pageItems), core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, style::spaceToken(4),
                core::EdgeInsets::all(style::spaceToken(6)));
            column.flex = 1.0F;
            page.push_back(std::move(column));
        } else if (navigator_.current() == "form") {
            std::vector<core::Widget> form;
            form.push_back(core::withKey(titleText("Profile", theme),
                                         "form-title"));
            form.push_back(core::withKey(mutedLabel("Nickname", theme),
                                         "nickname-label"));
            form.push_back(core::withKey(
                fieldWidget("nickname", "Nickname", "nickname-field",
                            form_.errors().count("nickname") != 0),
                "nickname-field"));
            if (form_.errors().count("nickname") != 0) {
                form.push_back(core::withKey(
                    errorText(form_.errors().at("nickname"), theme),
                    "nickname-error"));
            }
            form.push_back(
                core::withKey(mutedLabel("Email", theme), "email-label"));
            form.push_back(core::withKey(
                fieldWidget("email", "name@example.com", "email-field",
                            form_.errors().count("email") != 0),
                "email-field"));
            if (form_.errors().count("email") != 0) {
                form.push_back(core::withKey(
                    errorText(form_.errors().at("email"), theme),
                    "email-error"));
            }
            form.push_back(core::withKey(
                buttonWidget("Save", "save", "save-button",
                             core::ButtonVariant::Filled),
                "save-button"));
            form.push_back(core::withKey(
                buttonWidget("Back", "back", "back-button",
                             core::ButtonVariant::Outline),
                "back-button"));
            core::Widget column = core::makeColumn(
                std::move(form), core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, style::spaceToken(4),
                core::EdgeInsets::all(style::spaceToken(6)));
            column.flex = 1.0F;
            page.push_back(core::withKey(
                core::withScrollOffset(
                    core::makeListView(std::move(column), "settings-list"),
                    scroll_.offset()),
                "settings-list"));
        } else {
            std::vector<core::Widget> list;
            list.push_back(
                core::withKey(titleText("Settings", theme), "home-title"));
            list.push_back(core::withKey(
                buttonWidget("Edit profile", "goto-form", "goto-form-button",
                             core::ButtonVariant::Filled),
                "goto-form-button"));
            list.push_back(core::withKey(
                buttonWidget("Library grid", "goto-grid", "goto-grid-button",
                             core::ButtonVariant::Filled),
                "goto-grid-button"));
            list.push_back(core::withKey(
                buttonWidget("Browse library (1000)", "goto-library",
                             "goto-library-button",
                             core::ButtonVariant::Filled),
                "goto-library-button"));
            core::Widget notifications =
                core::makeSwitch("Notifications", "notifications",
                                 "notifications-switch");
            notifications.semanticsLabel = "Enable notifications";
            list.push_back(core::withKey(std::move(notifications),
                                         "notifications-switch"));
            core::Widget autosave = core::makeCheckbox("Autosave", "autosave",
                                                       "autosave-checkbox");
            autosave.semanticsLabel = "Autosave drafts";
            list.push_back(
                core::withKey(std::move(autosave), "autosave-checkbox"));
            // M4：平台服务区（文件选择/通知/外部链接）。
            list.push_back(
                core::withKey(titleText("Services", theme), "services-title"));
            list.push_back(core::withKey(
                buttonWidget("Open file...", "open-file", "open-file-button",
                             core::ButtonVariant::Outline),
                "open-file-button"));
            list.push_back(core::withKey(
                buttonWidget("Save file...", "save-file", "save-file-button",
                             core::ButtonVariant::Outline),
                "save-file-button"));
            list.push_back(core::withKey(
                buttonWidget("Notify", "notify", "notify-button",
                             core::ButtonVariant::Outline),
                "notify-button"));
            list.push_back(core::withKey(
                buttonWidget("Open docs", "open-docs", "open-docs-button",
                             core::ButtonVariant::Outline),
                "open-docs-button"));
            list.push_back(core::withKey(
                mutedLabel(pickedFile_.empty() ? "No file picked" : pickedFile_,
                           theme),
                "picked-file"));
            // 长列表内容：验证滚动与 key 复用。
            for (int i = 0; i < 24; ++i) {
                list.push_back(core::withKey(
                    mutedLabel("About entry " + std::to_string(i), theme),
                    "about-" + std::to_string(i)));
            }
            core::Widget column = core::makeColumn(
                std::move(list), core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Start, style::spaceToken(4),
                core::EdgeInsets::all(style::spaceToken(6)));
            column.flex = 1.0F;
            page.push_back(core::withKey(
                core::withScrollOffset(
                    core::makeListView(std::move(column), "settings-list"),
                    scroll_.offset()),
                "settings-list"));
        }

        core::Widget ui = core::makeContainer(
            core::makeColumn(std::move(page)), std::nullopt, std::nullopt,
            core::EdgeInsets{}, core::EdgeInsets{},
            shell_.theme().colors.pageBackground);
        ui.key = "root";

        if (dialogOpen_) {
            core::Widget content = core::makeColumn({
                core::withKey(titleText("Saved", theme), "dialog-title"),
                core::withKey(core::makeText("Profile updated.",
                                             theme.typography.body),
                              "dialog-body"),
                core::withKey(
                    buttonWidget("Close", "dismiss-dialog", kDialogCloseKey,
                                 core::ButtonVariant::Tonal),
                    kDialogCloseKey),
            }, core::MainAxisAlignment::Start,
               core::CrossAxisAlignment::Start, style::spaceToken(4),
               core::EdgeInsets::all(style::spaceToken(6)));
            core::Widget dialog = widgets::makeDialog(
                std::move(content), shell_.theme(), "dismiss-dialog",
                kDialogKey, shell_.view());
            ui = core::makeStack({std::move(ui), std::move(dialog)});
            ui.key = "root";
        }
        return ui;
    }

  private:
    // Dialog 身份常量（build 与焦点恢复共用，改名需同步两处）。
    static constexpr const char* kDialogKey = "saved-dialog";
    static constexpr const char* kDialogCloseKey = "dialog-close";
    // 应用壳配置：build 读应用状态；Escape/关闭统一规则、滚动 sink 与
    // modal 焦点规则以钩子注入（平台无关，M2 接口约束）。
    [[nodiscard]] static app::ShellConfig configFor(SettingsApp* self) {
        app::ShellConfig config;
        // settings 的 caret 恒不透明（确定性输出；迁移前行为）。
        config.caretBlink = false;
        config.build = [self] { return self->buildUi(); };
        // Escape/返回键统一规则：先问 modal，再问路由栈（plan §3.4）。
        config.onKey = [self](app::AppShell& shell, core::Key key,
                              core::KeyModifiers, char) {
            if (key == core::Key::Escape &&
                self->navigator_.handleBack(self->dialogOpen_)) {
                if (self->dialogOpen_) {
                    self->closeDialog();
                } else {
                    shell.markDirty();
                }
                return true;
            }
            return false;
        };
        // 滚轮/键盘滚动统一汇入 ScrollController（plan §3.4）。
        config.onWheel =
            [self](const core::RenderNode& root, const core::RenderNode* hit,
                   core::Offset /*position*/, core::Offset delta) {
                return self->scrollWheel(root, hit, delta.y);
            };
        // 关闭请求：modal → 路由栈 → 退出（plan §3.4 统一关闭规则）。
        // 与 onKey 同语义但不重入按键管线（一次关闭只消费一级）。
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
        // modal 焦点规则（plan §3.4 焦点恢复）：弹窗打开时把焦点移入
        // dialog 的 FocusScope，Tab/Enter 在域内处理。
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
        };
        return config;
    }

    // 状态 key 与业务 handler（应用层职责，plan §6.1）。
    void initialize() {
        // M3：千项虚拟列表（稳定 key = item-<index>；高度不均匀验证
        // 实测 extent 修正与锚点稳定）。
        library_.setItemCount(1000);
        library_.setEstimatedExtent(44.0F);
        library_.setItemBuilder([this](std::size_t index) {
            core::Widget item = core::makeText(
                "Entry " + std::to_string(index),
                shell_.theme().typography.body);
            item.key = "item-" + std::to_string(index);
            // 偶数项更高：触发 extent 修正路径。
            if (index % 2 == 0) {
                item.height = 64.0F;
            }
            return item;
        });

        shell_.state().set("nickname", "");
        shell_.state().set("email", "");
        shell_.state().set("notifications", "true");
        shell_.state().set("autosave", "false");
        auto& handlers = shell_.handlers();
        handlers["goto-form"] = [this] {
            navigator_.push("form");
            shell_.markDirty();
        };
        handlers["goto-grid"] = [this] {
            navigator_.push("grid");
            shell_.markDirty();
        };
        // M4：平台服务（动作经 main 注入；缺省时报告服务不可用）。
        handlers["open-file"] = [this] {
            (void)invokeService(serviceActions_.openFile, "open file");
        };
        handlers["save-file"] = [this] {
            (void)invokeService(serviceActions_.saveFile, "save file");
        };
        handlers["notify"] = [this] {
            (void)invokeService(serviceActions_.notify, "notify");
        };
        handlers["open-docs"] = [this] {
            (void)invokeService(serviceActions_.openDocs, "open docs");
        };
        handlers["goto-library"] = [this] {
            navigator_.push("library");
            shell_.markDirty();
        };
        handlers["back"] = [this] {
            navigator_.pop();
            shell_.markDirty();
        };
        handlers["save"] = [this] {
            if (form_.validate(shell_.state())) {
                dialogOpen_ = true;
            }
            shell_.markDirty();
            shell_.requestFullRepaint();
        };
        handlers["dismiss-dialog"] = [this] { closeDialog(); };

        form_.registerField(
            "nickname",
            widgets::FormController::nonEmpty("Nickname is required"));
        form_.registerField(
            "email",
            widgets::FormController::minLength(
                5, "Email must have at least 5 characters"));
    }

    bool scrollWheel(const core::RenderNode& root, const core::RenderNode* hit,
                     float deltaY) {
        // 命中的视口（interaction 已解析到 ScrollView/ListView）；键盘
        // 滚动（hit 为空）回退到页面主列表。
        const core::RenderNode* viewport = hit;
        if (viewport == nullptr) {
            viewport = core::findNodeByKey(root, "settings-list");
        }
        if (viewport == nullptr) {
            viewport = core::findNodeByKey(root, "library-list");
        }
        if (viewport == nullptr || !core::isScrollableWidget(viewport->type)) {
            return false;
        }
        if (viewport->type == core::WidgetType::VirtualList) {
            // M3：虚拟列表滚动（同一滚轮/键盘/语义输入路径）。
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
            // 键盘 Home/End 翻到头。
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

    void closeDialog() {
        dialogOpen_ = false;
        shell_.markDirty();
        shell_.requestFullRepaint();
    }

    // 服务调用统一包装：不可用/失败 → 可读诊断（picked-file 位置展示）。
    bool invokeService(const std::function<bool()>& action,
                       const char* name) {
        if (action == nullptr) {
            pickedFile_ = std::string(name) + ": service unavailable";
            shell_.markDirty();
            return false;
        }
        if (!action()) {
            pickedFile_ = std::string(name) + ": failed (see diagnostics)";
            shell_.markDirty();
            return false;
        }
        return true;
    }

    // --- 页面构建辅助（与迁移前一致，视觉全部来自 Theme token） ---
    [[nodiscard]] static core::Widget titleText(std::string text,
                                                const style::Theme& theme) {
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

    // 应用侧控制器状态（声明在 shell_ 之前：构造期 build 即可读取）。
    core::ScrollController scroll_{};
    widgets::FormController form_{};
    widgets::NavigatorController navigator_{"home"};
    bool darkMode_{true};
    bool dialogOpen_{false};
    // M3：千项库列表（可变缓存供布局期 noteExtent 回填）。
    mutable core::VirtualListController library_{};
    // M4：平台服务动作（main 注入）与最近结果展示。
    ServiceActions serviceActions_{};
    std::string pickedFile_{};

    app::AppShell shell_;
};

}  // namespace lumen::examples
