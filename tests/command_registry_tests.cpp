// G-1（docs/lumen-command-dispatch-design.md）：窗口内命令分发层测试。
// 覆盖域优先级（FocusDomain > Window）、模态屏蔽、键冲突仲裁、禁用态、
// 字段编辑保护（内建和弦让位）、菜单显示串派生与激活同路径——全部
// headless（AppShell 直驱，无平台窗口）。

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/widgets/menu.h"

using lumen::app::AppShell;
using lumen::app::CommandScope;
using lumen::app::CommandSpec;
using lumen::app::KeyBinding;
using lumen::app::ShellConfig;
using lumen::core::Key;
using lumen::core::KeyModifiers;
using lumen::core::makeFocusScope;
using lumen::core::RenderNode;
using lumen::core::Size;
using lumen::core::Widget;
using lumen::core::withKey;
using lumen::dsl::button;
using lumen::dsl::column;
using lumen::dsl::container;
using lumen::dsl::onClick;
using lumen::dsl::text;
using lumen::dsl::text_field;
using lumen::widgets::ContextMenuController;
using lumen::widgets::MenuItem;

namespace {

// 主树：普通按钮（域外）+ FocusScope "editor" 内的按钮与字段（域内）。
ShellConfig commandTestConfig() {
    ShellConfig config;
    config.initialView = Size{800.0F, 600.0F};
    config.build = [] {
        using namespace lumen::dsl;
        // FocusScope 直接以 key "editor" 挂载（withKey 会改写 key，导致
        // 域名与节点 key 分离）。
        Widget scope = makeFocusScope(
            column({withKey(button("In", onClick("in-btn")), "in-btn"),
                    withKey(text_field(lumen::dsl::bind("name")), "in-field")}),
            "editor");
        Widget page = container(
            column({withKey(button("Out", onClick("out-btn")), "out-btn"),
                    std::move(scope)}),
            lumen::core::Color::fromRGBA(24, 24, 27));
        page.key = "root";
        return page;
    };
    return config;
}

const RenderNode* findNodeWithText(const RenderNode& node,
                                   const std::string& want) {
    if (node.text == want) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const RenderNode* hit = findNodeWithText(child, want)) {
            return hit;
        }
    }
    return nullptr;
}

const RenderNode* findNodeByKeyDeep(const RenderNode& node,
                                    const std::string& key) {
    if (node.key == key) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const RenderNode* hit = findNodeByKeyDeep(child, key)) {
            return hit;
        }
    }
    return nullptr;
}

CommandSpec makeCommand(std::string id, KeyBinding binding, int* counter,
                        CommandScope scope = CommandScope::Window,
                        std::string domain = {}) {
    CommandSpec spec;
    spec.id = std::move(id);
    spec.binding = binding;
    spec.scope = scope;
    spec.domain = std::move(domain);
    spec.invoke = [counter](AppShell&) { ++*counter; };
    return spec;
}

}  // namespace

TEST_CASE("command_binding_label_formats_display_string",
          "[app][commands]") {
    CHECK(lumen::app::bindingLabel(
              KeyBinding::chord('s', lumen::core::kModifierCtrl)) == "Ctrl+S");
    CHECK(lumen::app::bindingLabel(KeyBinding::chord(
              'z', lumen::core::kModifierCtrl | lumen::core::kModifierShift)) ==
          "Ctrl+Shift+Z");
    CHECK(lumen::app::bindingLabel(
              KeyBinding::chord('f', lumen::core::kModifierAlt)) == "Alt+F");
    CHECK(lumen::app::bindingLabel(KeyBinding::chord(
              'q', lumen::core::kModifierCtrl | lumen::core::kModifierGui)) ==
          "Ctrl+Cmd+Q");
    CHECK(lumen::app::bindingLabel(KeyBinding::plain(Key::Escape)) == "Esc");
    CHECK(lumen::app::bindingLabel(KeyBinding::plain(Key::F10)) == "F10");
    CHECK(lumen::app::bindingLabel(KeyBinding::plain(Key::Alt)) == "Alt");
    CHECK(lumen::app::bindingLabel(KeyBinding::plain(
              Key::Enter, lumen::core::kModifierShift)) == "Shift+Enter");
    KeyBinding none;
    CHECK(lumen::app::bindingLabel(none).empty());
}

TEST_CASE("menu_function_key_command_dispatches_while_text_field_is_focused", "[app][commands]") {
    // Command dispatch design section 4: menu keys do not edit field text.
    AppShell shell{commandTestConfig()};
    shell.rebuildIfDirty();
    const RenderNode* field = findNodeByKeyDeep(shell.root(), "in-field");
    REQUIRE(field != nullptr);
    shell.controller().focusNode(*field);
    REQUIRE(shell.controller().wantsTextInput());
    shell.state().set("name", "unchanged");
    int fired = 0;
    shell.commands().registerCommand(makeCommand("menu", KeyBinding::plain(Key::F10), &fired));
    shell.keyDown(Key::F10);
    CHECK(fired == 1);
    CHECK(shell.state().get("name") == "unchanged");
    CHECK(shell.controller().wantsTextInput());
}

TEST_CASE("chord_command_dispatches_from_window_keydown", "[app][commands]") {
    AppShell shell{commandTestConfig()};
    int fired = 0;
    shell.commands().registerCommand(makeCommand(
        "save", KeyBinding::chord('s', lumen::core::kModifierCtrl), &fired));
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 's');
    CHECK(fired == 1);
    // 大小写归一（keyChar 平台层可能送大写）。
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 'S');
    CHECK(fired == 2);
    // 修饰键不同不命中。
    shell.keyDown(Key::None,
                  lumen::core::kModifierCtrl | lumen::core::kModifierShift,
                  's');
    CHECK(fired == 2);
}

TEST_CASE("focus_domain_command_outranks_window_command", "[app][commands]") {
    AppShell shell{commandTestConfig()};
    shell.rebuildIfDirty();
    int domainFired = 0;
    int windowFired = 0;
    const KeyBinding binding =
        KeyBinding::chord('f', lumen::core::kModifierCtrl);
    shell.commands().registerCommand(makeCommand(
        "win-find", binding, &windowFired));
    shell.commands().registerCommand(makeCommand(
        "editor-find", binding, &domainFired, CommandScope::FocusDomain,
        "editor"));

    // 无焦点：Window 兜底生效（FocusDomain 不命中）。
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 'f');
    CHECK(windowFired == 1);
    CHECK(domainFired == 0);

    // 焦点进入 editor 域：FocusDomain 优先。
    if (const RenderNode* inBtn =
            findNodeByKeyDeep(shell.root(), "in-btn")) {
        shell.controller().focusNode(*inBtn);
    }
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 'f');
    CHECK(windowFired == 1);
    CHECK(domainFired == 1);

    // 焦点回到域外：Window 恢复生效。
    if (const RenderNode* outBtn =
            findNodeByKeyDeep(shell.root(), "out-btn")) {
        shell.controller().focusNode(*outBtn);
    }
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 'f');
    CHECK(windowFired == 2);
    CHECK(domainFired == 1);
}

TEST_CASE("modal_masks_window_and_domain_commands", "[app][commands]") {
    AppShell shell{commandTestConfig()};
    shell.rebuildIfDirty();
    int windowFired = 0;
    int modalFired = 0;
    const KeyBinding binding =
        KeyBinding::chord('s', lumen::core::kModifierCtrl);
    shell.commands().registerCommand(makeCommand(
        "save", binding, &windowFired));
    shell.commands().registerCommand(makeCommand(
        "modal-save", binding, &modalFired, CommandScope::Modal));

    // 应用声明模态：Window 屏蔽、Modal 域生效。
    shell.setModalActive(true);
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 's');
    CHECK(windowFired == 0);
    CHECK(modalFired == 1);

    shell.setModalActive(false);
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 's');
    CHECK(windowFired == 1);
    CHECK(modalFired == 1);

    // 框架模态 overlay（菜单类）：同样屏蔽 Window 命令。
    shell.setOverlayBuilder([] { return std::optional<Widget>{}; });
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 's');
    CHECK(windowFired == 1);
    shell.clearOverlay();
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 's');
    CHECK(windowFired == 2);
}

TEST_CASE("duplicate_binding_conflict_is_recorded_and_first_wins",
          "[app][commands]") {
    AppShell shell{commandTestConfig()};
    int first = 0;
    int second = 0;
    const KeyBinding binding =
        KeyBinding::chord('p', lumen::core::kModifierCtrl);
    shell.commands().registerCommand(makeCommand("print", binding, &first));
    shell.commands().registerCommand(
        makeCommand("print-all", binding, &second));
    const auto& conflicts = shell.commands().conflicts();
    REQUIRE(conflicts.size() == 1);
    CHECK(conflicts[0].firstId == "print");
    CHECK(conflicts[0].secondId == "print-all");
    CHECK(conflicts[0].binding == "Ctrl+P");
    // 首注册者胜。
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 'p');
    CHECK(first == 1);
    CHECK(second == 0);
    // 不同域的同绑定不算冲突。
    shell.commands().registerCommand(makeCommand(
        "editor-print", binding, &second, CommandScope::FocusDomain,
        "editor"));
    CHECK(shell.commands().conflicts().size() == 1);
}

TEST_CASE("disabled_command_does_not_dispatch", "[app][commands]") {
    AppShell shell{commandTestConfig()};
    int fired = 0;
    bool enabled = false;
    CommandSpec spec = makeCommand(
        "save", KeyBinding::chord('s', lumen::core::kModifierCtrl), &fired);
    spec.enabled = [&enabled] { return enabled; };
    shell.commands().registerCommand(std::move(spec));
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 's');
    CHECK(fired == 0);
    CHECK_FALSE(shell.invokeCommand("save"));
    enabled = true;
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 's');
    CHECK(fired == 1);
    CHECK(shell.invokeCommand("save"));
    CHECK(fired == 2);
    // 未知 id。
    CHECK_FALSE(shell.invokeCommand("nope"));
}

TEST_CASE("field_edit_chords_yield_to_text_editing", "[app][commands]") {
    AppShell shell{commandTestConfig()};
    shell.rebuildIfDirty();
    int undoFired = 0;
    int saveFired = 0;
    shell.commands().registerCommand(makeCommand(
        "undo", KeyBinding::chord('z', lumen::core::kModifierCtrl),
        &undoFired));
    shell.commands().registerCommand(makeCommand(
        "save", KeyBinding::chord('s', lumen::core::kModifierCtrl),
        &saveFired));

    // 字段聚焦：内建编辑和弦（Ctrl+Z）归字段；非内建和弦（Ctrl+S）
    // 仍分发——输入中保存是桌面工具的基本预期。
    if (const RenderNode* field =
            findNodeByKeyDeep(shell.root(), "in-field")) {
        shell.controller().focusNode(*field);
    }
    REQUIRE(shell.controller().wantsTextInput());
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 'z');
    CHECK(undoFired == 0);
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 's');
    CHECK(saveFired == 1);

    // 无字段焦点：Ctrl+Z 分发命令（应用级撤销）。
    shell.controller().focusNode(shell.root());
    shell.controller().releaseFieldFocus();
    shell.keyDown(Key::None, lumen::core::kModifierCtrl, 'z');
    CHECK(undoFired == 1);
}

TEST_CASE("plain_key_command_falls_back_after_interaction",
          "[app][commands]") {
    AppShell shell{commandTestConfig()};
    shell.rebuildIfDirty();
    int escapeFired = 0;
    int enterFired = 0;
    int activated = 0;
    shell.handlers()["out-btn"] = [&activated] { ++activated; };
    shell.commands().registerCommand(makeCommand(
        "close-panel", KeyBinding::plain(Key::Escape), &escapeFired));
    shell.commands().registerCommand(makeCommand(
        "default-action", KeyBinding::plain(Key::Enter), &enterFired));

    // 无焦点：纯键命令兜底触发。
    shell.keyDown(Key::Escape);
    CHECK(escapeFired == 1);
    shell.keyDown(Key::Enter);
    CHECK(enterFired == 1);

    // 聚焦按钮：Enter 激活按钮（交互层优先），命令不分发。
    if (const RenderNode* outBtn =
            findNodeByKeyDeep(shell.root(), "out-btn")) {
        shell.controller().focusNode(*outBtn);
    }
    shell.keyDown(Key::Enter);
    CHECK(activated == 1);
    CHECK(enterFired == 1);

    // 字段聚焦：Escape 由字段消费（清焦点），命令不分发。
    if (const RenderNode* field =
            findNodeByKeyDeep(shell.root(), "in-field")) {
        shell.controller().focusNode(*field);
    }
    REQUIRE(shell.controller().wantsTextInput());
    shell.keyDown(Key::Escape);
    CHECK(escapeFired == 1);
    CHECK_FALSE(shell.controller().wantsTextInput());
}

TEST_CASE("menu_items_derive_shortcut_and_route_activation",
          "[app][commands]") {
    AppShell shell{commandTestConfig()};
    int fired = 0;
    bool enabled = true;
    CommandSpec spec = makeCommand(
        "save", KeyBinding::chord('s', lumen::core::kModifierCtrl), &fired);
    spec.enabled = [&enabled] { return enabled; };
    shell.commands().registerCommand(std::move(spec));

    ContextMenuController menu;
    int legacyFired = 0;
    menu.onCommand = [&legacyFired](const std::string&) { ++legacyFired; };
    MenuItem item;
    item.id = "save-item";
    item.label = "Save";
    item.command = "save";
    item.shortcut = "Ctrl+O";  // 注册表派生覆盖应用手写串（单一数据源）。
    menu.openAnchored(shell, lumen::core::Rect{{10, 10}, {0, 0}}, {item});
    REQUIRE(shell.overlayRoot() != nullptr);
    REQUIRE(findNodeWithText(*shell.overlayRoot(), "Ctrl+S") != nullptr);
    CHECK(findNodeWithText(*shell.overlayRoot(), "Ctrl+O") == nullptr);

    // 激活走注册表（与键盘同路径）；旧 onCommand 不触发。
    REQUIRE(menu.handleKey(shell, Key::Down));
    REQUIRE(menu.handleKey(shell, Key::Enter));
    CHECK(fired == 1);
    CHECK(legacyFired == 0);

    // 禁用命令派生禁用行（键盘不可激活）。
    enabled = false;
    menu.openAnchored(shell, lumen::core::Rect{{10, 10}, {0, 0}}, {item});
    menu.handleKey(shell, Key::Down);
    menu.handleKey(shell, Key::Enter);
    CHECK(fired == 1);
    menu.close(shell);

    // 未注册 command id 的项：保持旧 onCommand 路径。
    enabled = true;
    MenuItem plain;
    plain.id = "legacy";
    plain.label = "Legacy";
    plain.command = "not-registered";
    menu.openAnchored(shell, lumen::core::Rect{{10, 10}, {0, 0}}, {plain});
    menu.handleKey(shell, Key::Down);
    menu.handleKey(shell, Key::Enter);
    CHECK(legacyFired == 1);
}

TEST_CASE("command_registry_registration_is_guarded", "[app][commands]") {
    AppShell shell{commandTestConfig()};
    auto& registry = shell.commands();
    int fired = 0;
    const CommandSpec spec = makeCommand(
        "save", KeyBinding::chord('s', lumen::core::kModifierCtrl), &fired);
    registry.registerCommand(spec);
    // 同 id 覆盖不算自冲突。
    registry.registerCommand(spec);
    CHECK(registry.find("save") != nullptr);
    CHECK(registry.conflicts().empty());
    CHECK(registry.bindingLabelFor("save") == "Ctrl+S");
    // 无绑定命令：显示串为空但可经 invokeCommand 触发。
    CommandSpec unbound = makeCommand("about", KeyBinding{}, &fired);
    registry.registerCommand(std::move(unbound));
    CHECK(registry.bindingLabelFor("about").empty());
    CHECK(shell.invokeCommand("about"));
    CHECK(fired == 1);
    // 空 id/空回调拒绝注册。
    registry.registerCommand({});
    CommandSpec noInvoke = makeCommand("", KeyBinding{}, &fired);
    noInvoke.id = "ok";
    noInvoke.invoke = nullptr;
    registry.registerCommand(std::move(noInvoke));
    CHECK(registry.find("ok") == nullptr);
    // 注销后不可触发。
    registry.unregisterCommand("save");
    CHECK(registry.find("save") == nullptr);
    CHECK(registry.bindingLabelFor("save").empty());
    CHECK_FALSE(shell.invokeCommand("save"));
    CHECK(fired == 1);
}
