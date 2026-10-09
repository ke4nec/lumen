// G-4（docs/lumen-combo-box-design.md）：可编辑 ComboBox 测试。
// 覆盖：build/attach 装配、toggle 展开/锚定面板、过滤（label/value、
// 大小写、空结果不弹）、选择写回 bind + onSelected、键盘路径（Down
// 展开、菜单内建导航/Escape）、打开期输入即过滤。全 headless。

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/widgets/combo_box.h"

using lumen::app::AppShell;
using lumen::app::ShellConfig;
using lumen::core::Key;
using lumen::core::RenderNode;
using lumen::core::Size;
using lumen::widgets::ComboBoxController;

namespace {

const RenderNode* findByKeyDeep(const RenderNode& node,
                                const std::string& key) {
    if (node.key == key) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const RenderNode* hit = findByKeyDeep(child, key)) {
            return hit;
        }
    }
    return nullptr;
}

const RenderNode* findTextDeep(const RenderNode& node,
                               const std::string& text) {
    if (node.text == text) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const RenderNode* hit = findTextDeep(child, text)) {
            return hit;
        }
    }
    return nullptr;
}

struct Harness {
    ComboBoxController combo{
        {{"apple", "Apple"}, {"banana", "Banana"},
         {"cherry", "Cherry"}, {"durian", "Durian"}},
        "fruit", "fruit-combo"};
    AppShell shell{configFor(this)};

    static ShellConfig configFor(Harness* self) {
        ShellConfig config;
        config.initialView = Size{400.0F, 300.0F};
        config.build = [self] {
            using namespace lumen::dsl;
            lumen::core::Widget page = container(
                column({self->combo.build(
                    self->shell.theme())}),
                lumen::core::Color::fromRGBA(24, 24, 27));
            page.key = "root";
            return page;
        };
        config.onKey = [self](AppShell& shell, Key key,
                              lumen::core::KeyModifiers mods, char ch) {
            return self->combo.handleKey(shell, key, mods, ch);
        };
        return config;
    }

    Harness() {
        shell.state().set("fruit", "");
        combo.attach(shell);
        shell.rebuildIfDirty();
    }
};

}  // namespace

TEST_CASE("combo_box_build_wires_field_and_toggle", "[widgets][combo]") {
    Harness harness;
    AppShell& shell = harness.shell;
    const RenderNode* field = findByKeyDeep(shell.root(), "fruit-combo:field");
    REQUIRE(field != nullptr);
    CHECK(field->type == lumen::core::WidgetType::TextField);
    CHECK(field->bind == "fruit");
    const RenderNode* toggle =
        findByKeyDeep(shell.root(), "fruit-combo:toggle");
    REQUIRE(toggle != nullptr);
    CHECK_FALSE(toggle->onClick.empty());
}

TEST_CASE("combo_box_toggle_opens_anchored_and_select_writes_bind",
          "[widgets][combo]") {
    Harness harness;
    AppShell& shell = harness.shell;

    shell.handlers()["fruit-combo:toggle"]();
    CHECK(harness.combo.isOpen());
    REQUIRE(shell.overlayRoot() != nullptr);
    // 全量选项（空过滤 = 全部）。
    CHECK(findTextDeep(*shell.overlayRoot(), "Apple") != nullptr);
    CHECK(findTextDeep(*shell.overlayRoot(), "Durian") != nullptr);

    // 选中（键盘路径：Enter 激活高亮首项 → onCommand；Down 后 Enter
    // 则为次项）。
    std::optional<std::string> picked;
    harness.combo.onSelected = [&](const std::string& value) {
        picked = value;
    };
    CHECK(harness.combo.handleKey(shell, Key::Enter));
    CHECK_FALSE(harness.combo.isOpen());
    REQUIRE(picked.has_value());
    CHECK(*picked == "apple");
    CHECK(harness.combo.text(shell) == "apple");
    CHECK(shell.state().get("fruit") == "apple");
}

TEST_CASE("combo_box_filters_while_typing", "[widgets][combo]") {
    Harness harness;
    AppShell& shell = harness.shell;
    // 键盘展开：字段聚焦 + Down。
    const RenderNode* field =
        findByKeyDeep(shell.root(), "fruit-combo:field");
    REQUIRE(field != nullptr);
    shell.controller().focusNode(*field);
    CHECK(harness.combo.handleKey(shell, Key::Down));
    CHECK(harness.combo.isOpen());

    // 打开期输入即过滤（大小写不敏感；观察者驱动重开）。
    shell.state().set("fruit", "AN");
    REQUIRE(shell.overlayRoot() != nullptr);
    CHECK(findTextDeep(*shell.overlayRoot(), "Banana") != nullptr);
    CHECK(findTextDeep(*shell.overlayRoot(), "Apple") == nullptr);
    CHECK(findTextDeep(*shell.overlayRoot(), "Cherry") == nullptr);

    // 空结果：面板关闭（不弹空窗口）。
    shell.state().set("fruit", "zzz");
    CHECK_FALSE(harness.combo.isOpen());
}

TEST_CASE("combo_box_keyboard_navigation_and_escape", "[widgets][combo]") {
    Harness harness;
    AppShell& shell = harness.shell;
    shell.handlers()["fruit-combo:toggle"]();
    REQUIRE(harness.combo.isOpen());
    // 菜单内建导航：Down/Down/Up + Escape 关闭（焦点恢复）。
    CHECK(harness.combo.handleKey(shell, Key::Down));
    CHECK(harness.combo.handleKey(shell, Key::Down));
    CHECK(harness.combo.handleKey(shell, Key::Up));
    CHECK(harness.combo.handleKey(shell, Key::Escape));
    CHECK_FALSE(harness.combo.isOpen());

    // 未打开时非 Down 键不消费（交回应用路由）。
    CHECK_FALSE(harness.combo.handleKey(shell, Key::Up));
    // L-1 review：Alt+Down 同样展开（与文档声明对齐）；Ctrl+Down 不消费。
    const RenderNode* field2 =
        findByKeyDeep(shell.root(), "fruit-combo:field");
    REQUIRE(field2 != nullptr);
    shell.controller().focusNode(*field2);
    CHECK(harness.combo.handleKey(shell, Key::Down,
                                  lumen::core::kModifierAlt));
    CHECK(harness.combo.isOpen());
    CHECK(harness.combo.handleKey(shell, Key::Escape));
    field2 = findByKeyDeep(shell.root(), "fruit-combo:field");
    REQUIRE(field2 != nullptr);
    shell.controller().focusNode(*field2);
    CHECK_FALSE(harness.combo.handleKey(shell, Key::Down,
                                        lumen::core::kModifierCtrl));
}

TEST_CASE("combo_box_free_text_allowed", "[widgets][combo]") {
    Harness harness;
    AppShell& shell = harness.shell;
    // 自由值：不在选项内的文本保留（不强制匹配）。
    shell.state().set("fruit", "mango");
    CHECK(harness.combo.text(shell) == "mango");
    shell.handlers()["fruit-combo:toggle"]();
    CHECK(harness.combo.isOpen());
    shell.handlers()["fruit-combo:toggle"]();
    CHECK_FALSE(harness.combo.isOpen());
}
