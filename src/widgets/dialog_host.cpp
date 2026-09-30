// G-4：对话框便利层实现（契约见 dialog_host.h 与
// docs/lumen-dialog-host-design.md）。视觉全部来自 Theme 既有 token
//（visual-system §7.4 DialogTokens；标题 title、正文 body、按钮默认
// variant）；行为与 settings 手拼 Dialog 同源（同契约测试锁定）。

#include "lumen/widgets/dialog_host.h"

#include <utility>

#include "lumen/core/render_node.h"
#include "lumen/style/tokens.h"
#include "lumen/widgets/navigator.h"

namespace lumen::widgets {

namespace {

constexpr const char* kOkHandler = "dialog-host:ok";
constexpr const char* kCancelHandler = "dialog-host:cancel";
constexpr const char* kDismissHandler = "dialog-host:dismiss";

core::Widget makeLabel(std::string text, const core::TextStyle& style,
                       std::string key) {
    return core::withKey(core::makeText(std::move(text), style), key);
}

}  // namespace

void DialogHost::showMessage(app::AppShell& shell, std::string title,
                             std::string body, Buttons buttons,
                             std::function<void()> onDismiss) {
    if (busy()) {
        return;  // busy 拒绝发生在改写任何状态/回调之前（嵌套请求无副作用）
    }
    onDismiss_ = std::move(onDismiss);
    onConfirm_ = {};
    onPrompt_ = {};
    show(shell, Kind::Message, std::move(title), std::move(body), {},
         std::move(buttons));
}

void DialogHost::showConfirm(app::AppShell& shell, std::string title,
                             std::string body, Buttons buttons,
                             std::function<void(bool)> onResult) {
    if (busy()) {
        return;
    }
    onDismiss_ = {};
    onPrompt_ = {};
    onConfirm_ = std::move(onResult);
    show(shell, Kind::Confirm, std::move(title), std::move(body), {},
         std::move(buttons));
}

void DialogHost::showPrompt(app::AppShell& shell, std::string title,
                            std::string body, std::string initial,
                            Buttons buttons,
                            std::function<void(std::optional<std::string>)>
                                onResult) {
    if (busy()) {
        return;
    }
    onDismiss_ = {};
    onConfirm_ = {};
    onPrompt_ = std::move(onResult);
    show(shell, Kind::Prompt, std::move(title), std::move(body),
         std::move(initial), std::move(buttons));
}

void DialogHost::show(app::AppShell& shell, Kind kind, std::string title,
                      std::string body, std::string initial,
                      Buttons buttons) {
    kind_ = kind;
    closing_ = false;
    title_ = std::move(title);
    body_ = std::move(body);
    initial_ = std::move(initial);
    buttons_ = std::move(buttons);
    if (kind_ == Kind::Prompt) {
        shell.state().set(promptBind(), initial_);
    }
    returnFocusKey_ = shell.focus().focusedKey();
    focusSettlePending_ = true;
    registerHandlers(shell);
    shell.markDirty();
    if (shell.motionEnabled()) {
        shell.beginDialogTransition(dialogKey(), /*entering=*/true);
    }
}

void DialogHost::registerHandlers(app::AppShell& shell) {
    shell.handlers()[kOkHandler] = [this, &shell] { accept(shell); };
    shell.handlers()[kCancelHandler] = [this, &shell] { cancel(shell); };
    shell.handlers()[kDismissHandler] = [this, &shell] { cancel(shell); };
}

void DialogHost::accept(app::AppShell& shell) {
    if (!busy() || closing_) {
        return;
    }
    switch (kind_) {
        case Kind::Message: {
            std::function<void()> callback = std::move(onDismiss_);
            finish(shell);
            if (callback) {
                callback();
            }
            return;
        }
        case Kind::Confirm: {
            std::function<void(bool)> callback = std::move(onConfirm_);
            finish(shell);
            if (callback) {
                callback(true);
            }
            return;
        }
        case Kind::Prompt: {
            const std::string text = shell.state().get(promptBind());
            std::function<void(std::optional<std::string>)> callback =
                std::move(onPrompt_);
            finish(shell);
            if (callback) {
                callback(text);
            }
            return;
        }
        case Kind::None:
            return;
    }
}

void DialogHost::cancel(app::AppShell& shell) {
    if (!busy() || closing_) {
        return;
    }
    switch (kind_) {
        case Kind::Message: {
            std::function<void()> callback = std::move(onDismiss_);
            finish(shell);
            if (callback) {
                callback();
            }
            return;
        }
        case Kind::Confirm: {
            std::function<void(bool)> callback = std::move(onConfirm_);
            finish(shell);
            if (callback) {
                callback(false);
            }
            return;
        }
        case Kind::Prompt: {
            std::function<void(std::optional<std::string>)> callback =
                std::move(onPrompt_);
            finish(shell);
            if (callback) {
                callback(std::nullopt);
            }
            return;
        }
        case Kind::None:
            return;
    }
}

void DialogHost::finish(app::AppShell& shell) {
    const auto retire = [this](app::AppShell& active) {
        kind_ = Kind::None;
        closing_ = false;
        focusSettlePending_ = true;
        active.markDirty();
        active.requestFullRepaint();
    };
    if (shell.motionEnabled()) {
        closing_ = true;
        shell.beginDialogTransition(dialogKey(), /*entering=*/false, retire);
        shell.markDirty();
        return;
    }
    retire(shell);
}

bool DialogHost::handleKey(app::AppShell& shell, core::Key key,
                           core::KeyModifiers modifiers, char keyChar) {
    (void)modifiers;
    (void)keyChar;
    if (key == core::Key::Escape && busy() && !closing_) {
        cancel(shell);
        return true;
    }
    return false;
}

bool DialogHost::handleCloseRequested(app::AppShell& shell) {
    if (busy() && !closing_) {
        cancel(shell);
        return true;
    }
    return false;
}

void DialogHost::onRebuilt(app::AppShell& shell) {
    if (!focusSettlePending_) {
        return;
    }
    focusSettlePending_ = false;
    if (busy()) {
        // 对话框打开：焦点安置进域内（prompt 优先聚焦字段；其余首个
        // 可聚焦——通常首个按钮）。
        if (kind_ == Kind::Prompt) {
            if (const core::RenderNode* field = core::findNodeByKey(
                    shell.root(), "dialog-host-field")) {
                shell.controller().focusNode(*field);
                return;
            }
        }
        if (const core::RenderNode* dialog =
                core::findNodeByKey(shell.root(), dialogKey())) {
            shell.controller().focusFirstFocusable(*dialog);
        }
        return;
    }
    // 关闭后：恢复唤起前焦点（消失则交给应用既有恢复规则）。
    if (!returnFocusKey_.empty()) {
        if (const core::RenderNode* target =
                core::findNodeByKey(shell.root(), returnFocusKey_)) {
            shell.controller().focusNode(*target);
            return;
        }
    }
    shell.focus().clearFocus();
}

std::string DialogHost::promptText(const app::AppShell& shell) const {
    return kind_ == Kind::Prompt ? shell.state().get(promptBind()) : "";
}

std::optional<core::Widget> DialogHost::build(
    const app::AppShell& shell) const {
    if (!busy()) {
        return std::nullopt;
    }
    const style::Theme& theme = shell.theme();
    std::vector<core::Widget> content;
    content.push_back(makeLabel(title_, theme.typography.title,
                                "dialog-host-title"));
    content.push_back(core::withKey(
        core::makeText(body_, theme.typography.body), "dialog-host-body"));
    if (kind_ == Kind::Prompt) {
        content.push_back(core::withKey(
            core::makeTextField(initial_, /*placeholder=*/"input",
                                theme.typography.body,
                                core::EdgeInsets{}, /*flex=*/0.0F,
                                "dialog-host-field", std::nullopt,
                                std::nullopt, promptBind()),
            "dialog-host-field"));
    }
    core::Widget body = core::makeColumn(
        std::move(content), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Start, style::spaceToken(3));

    // 操作区：主按钮（Tonal）居右；Cancel 仅 Confirm/Prompt。
    std::vector<core::Widget> actions;
    if (kind_ != Kind::Message) {
        actions.push_back(core::withKey(
            core::withVariant(
                core::makeButton(buttons_.cancel, theme.typography.label,
                                 core::EdgeInsets{}, 0.0F,
                                 "dialog-host-cancel", std::nullopt,
                                 std::nullopt, kCancelHandler),
                core::ButtonVariant::Ghost),
            "dialog-host-cancel"));
    }
    actions.push_back(core::withKey(
        core::withVariant(
            core::makeButton(buttons_.ok, theme.typography.label,
                             core::EdgeInsets{}, 0.0F, "dialog-host-ok",
                             std::nullopt, std::nullopt, kOkHandler),
            core::ButtonVariant::Tonal),
        "dialog-host-ok"));
    core::Widget actionRow = core::makeRow(
        std::move(actions), core::MainAxisAlignment::End,
        core::CrossAxisAlignment::Center, style::spaceToken(2));

    return makeDialog(std::move(body), std::move(actionRow), theme,
                      kDismissHandler, dialogKey(), shell.view());
}

}  // namespace lumen::widgets
