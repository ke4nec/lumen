#pragma once

// v0.3 阶段8D settings 示例（plan §4 8D）：滚动列表、表单校验、弹窗、
// 导航、主题与无障碍标签的最小完整应用。窗口与 headless 共用同一管线
//（events -> state -> reconcile -> layout -> paint），counter 继续作为
// 最小回归样例。

#include <chrono>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/core/damage.h"
#include "lumen/core/element.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"
#include "lumen/core/scroll.h"
#include "lumen/core/state.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/style/state.h"
#include "lumen/style/theme.h"
#include "lumen/widgets/form.h"
#include "lumen/widgets/navigator.h"

namespace lumen::examples {

namespace {

// 视觉系统 builder（visual-system §6.1）：语义属性 + StyleOverrides 表达
// 个性化；旧 themed* helper 已删除，chrome 由 Theme 派生。
core::Widget titleText(std::string text, const style::Theme& theme) {
    return core::makeText(std::move(text), theme.typography.title);
}

core::Widget mutedLabel(std::string text, const style::Theme& theme) {
    core::StyleOverrides overrides;
    overrides.foreground = theme.colors.contentSecondary;
    return core::withStyleOverrides(core::makeText(std::move(text)),
                                    std::move(overrides));
}

core::Widget errorText(std::string text, const style::Theme& theme) {
    core::StyleOverrides overrides;
    overrides.foreground = theme.colors.statusError;
    overrides.text = theme.typography.caption;
    return core::withStyleOverrides(core::makeText(std::move(text)),
                                    std::move(overrides));
}

core::Widget fieldWidget(std::string bind, std::string placeholder,
                         std::string key, bool invalid) {
    core::Widget field = core::makeTextField(
        "", std::move(placeholder), core::TextStyle{}, core::EdgeInsets{},
        0.0F, std::move(key));
    field.bind = std::move(bind);
    return core::withInvalid(std::move(field), invalid);
}

core::Widget buttonWidget(std::string label, std::string onClick,
                          std::string key, core::ButtonVariant variant) {
    core::Widget button = core::makeButton(
        std::move(label), core::TextStyle{}, core::EdgeInsets{}, 0.0F,
        std::move(key), std::nullopt, std::nullopt, std::move(onClick));
    return core::withVariant(std::move(button), variant);
}

}  // namespace

class SettingsApp {
  public:
    SettingsApp() {
        initialize();
    }

    SettingsApp(const SettingsApp&) = delete;
    SettingsApp& operator=(const SettingsApp&) = delete;

    void setView(core::Size size) {
        if (size.width <= 0.0F || size.height <= 0.0F) {
            return;
        }
        view_ = size;
        dirty_ = true;
        fullRepaintPending_ = true;
    }

    void setDeviceScale(float scale) {
        deviceScale_ = scale;
        cpuRenderer_.setDeviceScale(scale);
        fullRepaintPending_ = true;
    }

    void setRenderer(render::Renderer* renderer) {
        externalRenderer_ = renderer;
        framePainted_ = false;
        fullRepaintPending_ = true;
    }

    // settings 页面：home = 滚动列表（开关/勾选/关于行）；form = 资料
    // 表单（校验 + 提交弹窗）。语义标签覆盖到每个控件。
    [[nodiscard]] core::Widget buildUi() const {
        const style::Theme& theme = theme_;
        std::vector<core::Widget> page;

        if (navigator_.current() == "form") {
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
            core::EdgeInsets{}, core::EdgeInsets{}, theme_.colors.pageBackground);
        ui.key = "root";

        if (dialogOpen_) {
            core::Widget content = core::makeColumn({
                core::withKey(titleText("Saved", theme_), "dialog-title"),
                core::withKey(core::makeText("Profile updated.",
                                             theme_.typography.body),
                              "dialog-body"),
                core::withKey(
                    buttonWidget("Close", "dismiss-dialog", "dialog-close",
                                 core::ButtonVariant::Tonal),
                    "dialog-close"),
            }, core::MainAxisAlignment::Start,
               core::CrossAxisAlignment::Start, style::spaceToken(4),
               core::EdgeInsets::all(style::spaceToken(6)));
            core::Widget dialog = widgets::makeDialog(
                std::move(content), theme_, "dismiss-dialog", "saved-dialog",
                view_);
            ui = core::makeStack({std::move(ui), std::move(dialog)});
            ui.key = "root";
        }
        return ui;
    }

    void rebuildIfDirty() {
        if (!dirty_) {
            return;
        }
        core::Widget next = buildUi();
        core::applyBinds(next, state_);
        element_->update(std::move(next));
        syncSubscriptions(core::collectBindKeys(element_->widget()));
        core::RenderNode fresh = layout::LayoutEngine::layout(
            element_->widget(), core::Constraints::tight(view_),
            styleContext());
        if (hasPreviousRoot_) {
            treeDamageValid_ =
                treeDamageValid_ &&
                core::collectDamage(previousRoot_, fresh, pendingDamage_);
        } else {
            treeDamageValid_ = false;
            pendingDamage_.clear();
        }
        root_ = fresh;
        previousRoot_ = std::move(fresh);
        hasPreviousRoot_ = true;
        rebuiltThisFrame_ = true;
        dirty_ = false;
        // modal 焦点规则（plan §3.4 焦点恢复）：弹窗打开时把焦点移入
        // dialog 的 FocusScope，Tab/Enter 在域内处理。
        if (dialogOpen_ &&
            focus_.focusedIdentity().find("saved-dialog") ==
                std::string::npos) {
            const core::RenderNode* close =
                core::findNodeByKey(root_, "dialog-close");
            if (close != nullptr) {
                controller_.focusNode(*close);
            }
        }
    }

    // 视觉系统应用入口（visual-system §6.3）：应用拥有 Theme，每帧提供
    // StyleContext（Theme × 交互快照 × 可访问性设置）。快照是成员——
    // StyleContext 只持引用，临时对象会悬空。
    void syncInteractionSnapshot() {
        interactionSnapshot_ = style::InteractionStateSnapshot{
            controller_.hoveredIdentity(), controller_.pressedIdentity(),
            focus_.focusedIdentity()};
    }

    [[nodiscard]] style::StyleContext styleContext() const {
        return style::StyleContext{theme_, interactionSnapshot_,
                                   accessibility_, deviceScale_};
    }

    std::uint64_t renderFrame(bool forceFullRepaint = false) {
        // 交互快照变化 → 重建（状态折算进 resolved style，diff 产生
        // damage；visual-system §5 规则 6）。
        syncInteractionSnapshot();
        if (!(interactionSnapshot_ == lastInteraction_)) {
            lastInteraction_ = interactionSnapshot_;
            dirty_ = true;
        }
        rebuildIfDirty();
        render::PaintOptions options;
        options.caretGraphemes = controller_.caretGraphemes();
        options.selectionStart = controller_.selectionStart();
        options.selectionEnd = controller_.selectionEnd();
        options.hasSelection = controller_.hasSelection();
        options.composition = controller_.composition();

        const std::string& focusedIdentity = focus_.focusedIdentity();
        const bool optionsChanged =
            focusedIdentity != lastFocusedIdentity_ ||
            options.caretGraphemes != lastCaret_ ||
            options.selectionStart != lastSelectionStart_ ||
            options.selectionEnd != lastSelectionEnd_ ||
            options.composition != lastComposition_;
        const bool needPaint = forceFullRepaint || fullRepaintPending_ ||
                               !framePainted_ || rebuiltThisFrame_ ||
                               optionsChanged;
        if (!needPaint) {
            rebuiltThisFrame_ = false;
            return lastFrameHash_;
        }

        std::vector<core::Rect> damage = pendingDamage_;
        if (optionsChanged) {
            if (focusedIdentity != lastFocusedIdentity_ ||
                options.caretGraphemes != lastCaret_) {
                addNodeRect(damage, focusedIdentity, focus_.focusedKey());
                addNodeRect(damage, lastFocusedIdentity_, "");
            }
        }

        render::Renderer& renderer =
            externalRenderer_ != nullptr ? *externalRenderer_ : cpuRenderer_;
        const auto bounds = core::damageBounds(damage, view_);
        const bool partial =
            !forceFullRepaint && !fullRepaintPending_ &&
            externalRenderer_ == nullptr && framePainted_ &&
            treeDamageValid_ && bounds.has_value();
        const auto buildStart = std::chrono::steady_clock::now();
        render::RenderCommandList commands =
            render::recordScene(root_, options);
        renderer.noteCpuBuildMs(
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - buildStart)
                .count());
        render::FrameInfo info;
        info.viewport = view_;
        info.deviceScale = deviceScale_;
        info.frameIndex = frameIndex_;
        if (partial) {
            info.damage = bounds;
            info.preservePrevious = true;
            ++partialRepaintCount_;
        }
        renderer.submit(commands, info);
        frameIndex_ += 1;
        element_->clearDirtyTree();

        lastFocusedIdentity_ = focusedIdentity;
        lastCaret_ = options.caretGraphemes;
        lastSelectionStart_ = options.selectionStart;
        lastSelectionEnd_ = options.selectionEnd;
        lastComposition_ = options.composition;
        framePainted_ = true;
        fullRepaintPending_ = false;
        rebuiltThisFrame_ = false;
        pendingDamage_.clear();
        treeDamageValid_ = true;
        if (externalRenderer_ == nullptr) {
            lastFrameHash_ = render::frameHash(cpuRenderer_.pixels());
            return lastFrameHash_;
        }
        return 0;
    }

    // 事件入口（与 counter 相同的“先重建再命中”规则）。
    void pointerDown(core::Offset position) {
        rebuildIfDirty();
        controller_.pointerDown(root_, position, lastTickMs_);
    }
    void pointerMove(core::Offset position) {
        rebuildIfDirty();
        controller_.pointerMove(root_, position);
    }
    void pointerUp(core::Offset position) {
        rebuildIfDirty();
        controller_.pointerUp(root_, position);
    }
    void pointerCancel() { controller_.pointerCancel(); }
    void wheel(core::Offset position, core::Offset delta) {
        rebuildIfDirty();
        controller_.wheel(root_, position, delta);
    }
    void textInput(const std::string& text) { controller_.textInput(text); }
    void textEditing(const std::string& text) {
        controller_.setComposition(text);
    }
    void keyDown(core::Key key, core::KeyModifiers modifiers = core::kModifierNone,
                 char keyChar = 0) {
        rebuildIfDirty();
        // Escape/返回键统一规则：先问 modal，再问路由栈（plan §3.4）。
        if (key == core::Key::Escape &&
            navigator_.handleBack(dialogOpen_)) {
            if (dialogOpen_) {
                closeDialog();
            } else {
                dirty_ = true;
            }
            return;
        }
        controller_.keyDown(root_, key, modifiers, keyChar);
    }

    // --- 测试/宿主查询 ---
    [[nodiscard]] const core::StateStore& state() const { return state_; }
    [[nodiscard]] const core::RenderNode& root() const { return root_; }
    [[nodiscard]] const core::InteractionController& controller() const {
        return controller_;
    }
    [[nodiscard]] core::ScrollController& scroll() { return scroll_; }
    [[nodiscard]] widgets::NavigatorController& navigator() {
        return navigator_;
    }
    [[nodiscard]] widgets::FormController& form() { return form_; }
    [[nodiscard]] bool dialogOpen() const { return dialogOpen_; }
    [[nodiscard]] core::Size view() const { return view_; }
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
        theme_ = std::move(theme);
        dirty_ = true;
        fullRepaintPending_ = fullRepaintPending_ || forceFullRepaint;
    }
    [[nodiscard]] const style::Theme& theme() const { return theme_; }
    // 可访问性设置变化 → 派生 Theme 并请求全量重绘（font scale/high
    // contrast/density/reduce animation，visual-system §4）。
    void setAccessibilitySettings(
        accessibility::AccessibilitySettings settings) {
        accessibility_ = settings;
        theme_ = style::Theme::fromSettings(accessibility_, darkMode_,
                                             theme_.metrics.density);
        dirty_ = true;
        fullRepaintPending_ = true;
    }
    [[nodiscard]] const accessibility::AccessibilitySettings&
    accessibilitySettings() const {
        return accessibility_;
    }
    [[nodiscard]] render::RendererCapabilities capabilities() {
        return externalRenderer_ != nullptr ? externalRenderer_->capabilities()
                                            : cpuRenderer_.capabilities();
    }
    [[nodiscard]] const render::PixelBuffer& pixels() const {
        return cpuRenderer_.pixels();
    }
    [[nodiscard]] std::uint32_t partialRepaintCount() const {
        return partialRepaintCount_;
    }
    void tick(std::uint64_t nowMs) { lastTickMs_ = nowMs; }
    void swapRoot(core::Widget root) {
        // 热重载：仅换模板，状态/滚动/路由保留。
        (void)root;
        dirty_ = true;
    }
    void markDirty() { dirty_ = true; }

    // 语义树（无障碍结构测试）。
    [[nodiscard]] accessibility::SemanticsTree semantics() const {
        accessibility::SemanticsBuildOptions options;
        options.focus = &focus_;
        return accessibility::buildSemanticsTree(root_, options);
    }

    // 语义 scroll action 的测试钩子（命中列表视口并滚动）。
    [[nodiscard]] bool scrollWheelForTests(float deltaY) {
        const core::RenderNode* list = core::findNodeByKey(root_, "settings-list");
        return list != nullptr && scrollWheel(root_, list, deltaY);
    }

  private:
    void initialize() {
        state_.set("nickname", "");
        state_.set("email", "");
        state_.set("notifications", "true");
        state_.set("autosave", "false");
        handlers_["goto-form"] = [this] {
            navigator_.push("form");
            dirty_ = true;
        };
        handlers_["back"] = [this] {
            navigator_.pop();
            dirty_ = true;
        };
        handlers_["save"] = [this] {
            if (form_.validate(state_)) {
                dialogOpen_ = true;
            }
            dirty_ = true;
            fullRepaintPending_ = true;
        };
        handlers_["dismiss-dialog"] = [this] { closeDialog(); };

        form_.registerField(
            "nickname",
            widgets::FormController::nonEmpty("Nickname is required"));
        form_.registerField(
            "email",
            widgets::FormController::minLength(
                5, "Email must have at least 5 characters"));

        element_.emplace(buildUi());

        // 滚轮/键盘滚动统一汇入 ScrollController（plan §3.4）。
        controller_.setWheelSink(
            [this](const core::RenderNode& root, const core::RenderNode* hit,
                   core::Offset /*position*/, core::Offset delta) {
                return scrollWheel(root, hit, delta.y);
            });
        syncSubscriptions(core::collectBindKeys(buildUi()));
    }

    bool scrollWheel(const core::RenderNode& root, const core::RenderNode* hit,
                     float deltaY) {
        // 命中的视口（interaction 已解析到 ScrollView/ListView）；键盘
        // 滚动（hit 为空）回退到页面主列表。
        const core::RenderNode* viewport = hit;
        if (viewport == nullptr) {
            viewport = core::findNodeByKey(root, "settings-list");
        }
        if (viewport == nullptr || !core::isScrollableWidget(viewport->type)) {
            return false;
        }
        scroll_.updateExtents(viewport->size.height,
                              viewport->size.height +
                                  viewport->scrollExtent);
        if (std::abs(deltaY) > 1e8F) {
            // 键盘 Home/End 翻到头。
            scroll_.scrollTo(deltaY > 0 ? scroll_.maxScrollOffset() : 0.0F);
            dirty_ = true;
            return true;
        }
        const bool changed = scroll_.applyWheel(deltaY);
        if (changed) {
            dirty_ = true;
        }
        return changed;
    }

    void closeDialog() {
        dialogOpen_ = false;
        dirty_ = true;
        fullRepaintPending_ = true;
    }

    static const core::RenderNode* findByIdentity(const core::RenderNode& node,
                                                  const std::string& identity,
                                                  core::Offset& origin) {
        if (node.identity == identity) {
            return &node;
        }
        for (const auto& child : node.children) {
            core::Offset childOrigin = origin + child.offset;
            if (const core::RenderNode* found =
                    findByIdentity(child, identity, childOrigin)) {
                origin = childOrigin;
                return found;
            }
        }
        return nullptr;
    }

    void addNodeRect(std::vector<core::Rect>& damage,
                     const std::string& identity, const std::string& key) {
        const core::RenderNode* node = nullptr;
        core::Offset origin{};
        if (!identity.empty()) {
            node = findByIdentity(root_, identity, origin);
        }
        if (node == nullptr && !key.empty()) {
            node = core::findNodeByKey(root_, key);
            if (node != nullptr) {
                origin = core::absoluteOffset(root_, key);
            }
        }
        if (node != nullptr) {
            damage.push_back(core::Rect{origin, node->size});
        }
    }

    void syncSubscriptions(const std::set<std::string>& keys) {
        for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
            if (keys.count(it->first) == 0) {
                state_.unsubscribe(it->second);
                it = subscriptions_.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& key : keys) {
            if (subscriptions_.count(key) == 0) {
                subscriptions_.emplace(
                    key, state_.subscribe(key, [this] { dirty_ = true; }));
            }
        }
    }

    core::StateStore state_{};
    core::HandlerRegistry handlers_{};
    std::map<std::string, core::StateStore::ObserverId> subscriptions_{};
    core::FocusManager focus_{};
    core::InteractionController controller_{state_, handlers_, focus_};
    core::ScrollController scroll_{};
    widgets::FormController form_{};
    widgets::NavigatorController navigator_{"home"};
    style::Theme theme_{style::Theme::dark()};
    bool darkMode_{true};
    accessibility::AccessibilitySettings accessibility_{};
    bool dialogOpen_{false};

    std::optional<core::Element> element_{};
    core::RenderNode root_{};
    core::RenderNode previousRoot_{};
    bool hasPreviousRoot_{false};
    std::vector<core::Rect> pendingDamage_{};
    bool treeDamageValid_{true};
    bool rebuiltThisFrame_{false};
    bool framePainted_{false};
    bool fullRepaintPending_{false};
    // 视觉系统：交互快照（hover/press/focus）驱动重建。
    style::InteractionStateSnapshot interactionSnapshot_{};
    style::InteractionStateSnapshot lastInteraction_{};
    std::string lastFocusedIdentity_{};
    std::size_t lastCaret_{0};
    std::size_t lastSelectionStart_{0};
    std::size_t lastSelectionEnd_{0};
    std::string lastComposition_{};
    std::uint64_t lastFrameHash_{0};
    std::uint32_t partialRepaintCount_{0};
    core::Size view_{800.0F, 600.0F};
    float deviceScale_{1.0F};
    std::uint64_t frameIndex_{0};
    std::uint64_t lastTickMs_{0};
    render::CpuRenderer cpuRenderer_{1.0F};
    render::Renderer* externalRenderer_{nullptr};
    bool dirty_{true};
};

}  // namespace lumen::examples
