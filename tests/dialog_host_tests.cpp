// G-4（docs/lumen-dialog-host-design.md）：对话框便利层测试。
// 契约与手拼 makeDialog 对齐（同契约锁定）：模态 barrier、FocusScope
// 域、Escape/关闭统一规则、焦点恢复、键盘激活与 prompt 输入。全部
// headless（AppShell 直驱）。

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/widgets/dialog_host.h"

using lumen::app::AppShell;
using lumen::app::ShellConfig;
using lumen::core::Key;
using lumen::core::RenderNode;
using lumen::core::Size;
using lumen::widgets::DialogHost;

namespace {

const RenderNode* findByKeyDeep(const RenderNode& node, const std::string& key,
                                bool (*pred)(const RenderNode&) = nullptr) {
    if (node.key == key && (pred == nullptr || pred(node))) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const RenderNode* hit = findByKeyDeep(child, key, pred)) {
            return hit;
        }
    }
    return nullptr;
}

// 装配了 DialogHost 四钩子的最小应用（与文档示例同形）。
struct Harness {
    DialogHost dialogs{};
    AppShell shell{configFor(this)};

    Harness() {
        shell.state().set("count", "0");
        shell.rebuildIfDirty();
    }

    static lumen::core::Widget basePage() {
        using namespace lumen::dsl;
        lumen::core::Widget page = container(
            column({withKey(button("Open", onClick("open")), "open-btn")}),
            lumen::core::Color::fromRGBA(24, 24, 27));
        page.key = "root";
        return page;
    }

    static ShellConfig configFor(Harness* self) {
        ShellConfig config;
        config.initialView = Size{400.0F, 300.0F};
        // build：主模板 + 对话框子树（打开时叠加为栈层）。
        config.build = [self] {
            lumen::core::Widget ui = basePage();
            if (std::optional<lumen::core::Widget> dialog =
                    self->dialogs.build(self->shell)) {
                ui = lumen::core::makeStack(
                    {std::move(ui), std::move(*dialog)});
                ui.key = "root";
            }
            return ui;
        };
        // modal 优先于路由返回规则（plan §3.4）。
        config.onKey = [self](AppShell& shell, Key key,
                              lumen::core::KeyModifiers mods, char ch) {
            return self->dialogs.handleKey(shell, key, mods, ch);
        };
        // 窗口关闭请求：对话框打开时消费（不退出应用）。
        config.onCloseRequested = [self](AppShell& shell) {
            return self->dialogs.handleCloseRequested(shell);
        };
        // 焦点安置/恢复。
        config.onRebuilt = [self](AppShell& shell) {
            self->dialogs.onRebuilt(shell);
        };
        return config;
    }
};

}  // namespace

TEST_CASE("dialog_host_message_ok_path", "[widgets][dialog-host]") {
    Harness harness;
    AppShell& shell = harness.shell;
    DialogHost& dialogs = harness.dialogs;

    int dismissed = 0;
    // 唤起前聚焦主树按钮（关闭后恢复目标）。
    const RenderNode* openBtn = findByKeyDeep(shell.root(), "open-btn");
    REQUIRE(openBtn != nullptr);
    shell.controller().focusNode(*openBtn);
    dialogs.showMessage(shell, "Saved", "Profile updated.",
                       {"Close"}, [&] { ++dismissed; });
    REQUIRE(dialogs.busy());
    shell.markDirty();
    shell.rebuildIfDirty();

    // 契约：模态结构 = barrier（onDismiss + 语义 dialog）+ FocusScope 域
    //（与手拼 makeDialog 同构；key 后缀见 navigator.cpp）。
    const RenderNode* barrier = findByKeyDeep(
        shell.root(), "dialog-host-barrier");
    REQUIRE(barrier != nullptr);
    CHECK(barrier->onClick == "dialog-host:dismiss");
    CHECK(barrier->semanticsRole == "dialog");

    // 焦点安置：首个可聚焦（OK 按钮）。
    CHECK(shell.focus().focusedKey().find("dialog-host") !=
          std::string::npos);
    // 键盘同路径：Enter 激活聚焦按钮 → onDismiss。
    shell.keyDown(Key::Enter);
    CHECK(dismissed == 1);
    CHECK_FALSE(dialogs.busy());
    shell.markDirty();
    shell.rebuildIfDirty();
    CHECK(findByKeyDeep(shell.root(), "dialog-host") == nullptr);
    // 关闭后焦点恢复唤起前目标（open 按钮）。
    CHECK(shell.focus().focusedKey().find("open-btn") != std::string::npos);
}

TEST_CASE("dialog_host_confirm_paths", "[widgets][dialog-host]") {
    Harness harness;
    AppShell& shell = harness.shell;
    DialogHost& dialogs = harness.dialogs;

    std::optional<bool> result;
    dialogs.showConfirm(shell, "Overwrite?", "File exists.",
                        {"Overwrite", "Skip"},
                        [&](bool accepted) { result = accepted; });
    shell.markDirty();
    shell.rebuildIfDirty();

    // OK 按钮 handler 接线。
    const RenderNode* ok =
        findByKeyDeep(shell.root(), "dialog-host-ok");
    REQUIRE(ok != nullptr);
    CHECK(ok->onClick == "dialog-host:ok");
    const RenderNode* cancel =
        findByKeyDeep(shell.root(), "dialog-host-cancel");
    REQUIRE(cancel != nullptr);
    CHECK(cancel->onClick == "dialog-host:cancel");

    // Escape = 取消（统一规则）。
    CHECK(dialogs.handleKey(shell, Key::Escape));
    CHECK(result.has_value());
    CHECK_FALSE(*result);
    CHECK_FALSE(dialogs.busy());

    // 重开走 OK 路径。
    dialogs.showConfirm(shell, "Overwrite?", "File exists.", {},
                        [&](bool accepted) { result = accepted; });
    shell.markDirty();
    shell.rebuildIfDirty();
    shell.handlers()["dialog-host:ok"]();
    CHECK(result.has_value());
    CHECK(*result);

    // barrier（ dismissing handler）= 取消。
    dialogs.showConfirm(shell, "Overwrite?", "File exists.", {},
                        [&](bool accepted) { result = accepted; });
    shell.markDirty();
    shell.rebuildIfDirty();
    shell.handlers()["dialog-host:dismiss"]();
    CHECK(result.has_value());
    CHECK_FALSE(*result);
}

TEST_CASE("dialog_host_close_request_consumed_while_open",
          "[widgets][dialog-host]") {
    Harness harness;
    AppShell& shell = harness.shell;
    DialogHost& dialogs = harness.dialogs;
    CHECK_FALSE(dialogs.handleCloseRequested(shell));
    int dismissed = 0;
    dialogs.showMessage(shell, "Hi", "body", {}, [&] { ++dismissed; });
    CHECK(dialogs.handleCloseRequested(shell));
    CHECK(dismissed == 1);
    CHECK_FALSE(dialogs.busy());
}

TEST_CASE("dialog_host_prompt_input_flow", "[widgets][dialog-host]") {
    Harness harness;
    AppShell& shell = harness.shell;
    DialogHost& dialogs = harness.dialogs;

    std::optional<std::optional<std::string>> result;
    dialogs.showPrompt(shell, "Name?", "Enter your name.", "Ada", {},
                       [&](std::optional<std::string> value) {
                           result = value;
                       });
    shell.markDirty();
    shell.rebuildIfDirty();

    // 初始值 + 焦点进字段（光标置末尾）。
    CHECK(dialogs.promptText(shell) == "Ada");
    CHECK(shell.controller().focusedBind() == "dialog-host:prompt");
    // 输入路径与普通字段一致（textInput）。
    shell.textInput("Lovelace");
    CHECK(dialogs.promptText(shell) == "AdaLovelace");
    // OK → 提交文本。
    shell.handlers()["dialog-host:ok"]();
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK(**result == "AdaLovelace");

    // Escape → nullopt。
    result.reset();
    dialogs.showPrompt(shell, "Name?", "Enter your name.", "x", {},
                       [&](std::optional<std::string> value) {
                           result = value;
                       });
    shell.markDirty();
    shell.rebuildIfDirty();
    CHECK(dialogs.handleKey(shell, Key::Escape));
    REQUIRE(result.has_value());
    CHECK_FALSE(result->has_value());
}

TEST_CASE("dialog_host_chained_show_during_closing_not_dropped",
          "[widgets][dialog-host]") {
    // M-1 review：motion 模式下 cancel 的退出转场窗口期内，回调里
    // 连环 showMessage 不得静默丢失（队列暂存，retire 后安装）。
    Harness harness;
    AppShell& shell = harness.shell;
    DialogHost& dialogs = harness.dialogs;
    // motion 需要 opt-in + tick 驱动（同 menu-motion 口径）。
    // Harness 的 config 未开 motionTransitions——本用例单独构造。
    struct MotionHarness {
        DialogHost dialogs{};
        AppShell shell{configFor(this)};
        static ShellConfig configFor(MotionHarness* self) {
            ShellConfig config;
            config.initialView = Size{400.0F, 300.0F};
            config.motionTransitions = true;
            config.build = [self] {
                lumen::core::Widget ui = Harness::basePage();
                if (std::optional<lumen::core::Widget> dialog =
                        self->dialogs.build(self->shell)) {
                    ui = lumen::core::makeStack(
                        {std::move(ui), std::move(*dialog)});
                    ui.key = "root";
                }
                return ui;
            };
            config.onKey = [self](AppShell& shell, Key key,
                                  lumen::core::KeyModifiers mods, char ch) {
                return self->dialogs.handleKey(shell, key, mods, ch);
            };
            config.onCloseRequested = [self](AppShell& shell) {
                return self->dialogs.handleCloseRequested(shell);
            };
            config.onRebuilt = [self](AppShell& shell) {
                self->dialogs.onRebuilt(shell);
            };
            return config;
        }
    };
    MotionHarness motion;
    AppShell& mshell = motion.shell;
    DialogHost& mdialogs = motion.dialogs;
    mshell.state().set("count", "0");

    int dismissedA = 0;
    mdialogs.showMessage(mshell, "A", "first", {}, [&] {
        ++dismissedA;
        // 回调内连环开第二个对话框（此刻处于退出转场窗口期）。
        mdialogs.showMessage(mshell, "B", "second");
    });
    mshell.markDirty();
    mshell.rebuildIfDirty();
    mshell.tick(0);
    // Escape 触发取消 → 退出转场 → 回调 → B 进入队列。
    CHECK(mdialogs.handleKey(mshell, Key::Escape));
    CHECK(dismissedA == 1);
    // 队列中的 B 在转场完成（retire）后安装。
    mshell.tick(200);
    mshell.markDirty();
    mshell.rebuildIfDirty();
    REQUIRE(mdialogs.busy());
    const RenderNode* body =
        findByKeyDeep(mshell.root(), "dialog-host-body");
    REQUIRE(body != nullptr);
    CHECK(body->text == "second");
}

TEST_CASE("dialog_host_prompt_enter_submits", "[widgets][dialog-host]") {
    // L-6 review：prompt 字段聚焦时 Enter = 提交（onKey 层先行截获，
    // 字段自身的 Enter 失焦语义不吞掉提交预期）。
    Harness harness;
    AppShell& shell = harness.shell;
    DialogHost& dialogs = harness.dialogs;
    std::optional<std::optional<std::string>> result;
    dialogs.showPrompt(shell, "Name?", "Enter your name.", "Ada", {},
                       [&](std::optional<std::string> value) {
                           result = value;
                       });
    shell.markDirty();
    shell.rebuildIfDirty();
    REQUIRE(shell.controller().focusedBind() == "dialog-host:prompt");
    shell.textInput("Lovelace");
    // 经 shell.keyDown（onKey 路由）——与真实键盘同路径。
    shell.keyDown(Key::Enter);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK(**result == "AdaLovelace");
    CHECK_FALSE(dialogs.busy());
}

TEST_CASE("dialog_host_rejects_nested_convenience", "[widgets][dialog-host]") {
    Harness harness;
    AppShell& shell = harness.shell;
    DialogHost& dialogs = harness.dialogs;
    int first = 0;
    int second = 0;
    dialogs.showMessage(shell, "A", "a", {}, [&] { ++first; });
    dialogs.showConfirm(shell, "B", "b", {}, [&](bool) { ++second; });
    REQUIRE(dialogs.busy());
    shell.markDirty();
    shell.rebuildIfDirty();
    // 第二个请求被 busy 拒绝：仍是第一个对话框。
    const RenderNode* body = findByKeyDeep(shell.root(), "dialog-host-body");
    REQUIRE(body != nullptr);
    CHECK(body->text == "a");
    shell.handlers()["dialog-host:ok"]();
    CHECK(first == 1);
    CHECK(second == 0);
}
