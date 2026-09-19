// 菜单类控件（docs/lumen-menu-controls-design.md）测试：Secondary 按键
// 穿透（不进点击/拖动路径 + sink 消费两路）、ContextMenu 打开/定位/键盘
// 全契约（跳过分隔线与禁用项/Enter 激活/Esc/Tab/barrier 关闭/焦点恢复）、
// checkable 勾选与快捷键仅展示、子菜单级联（Right 展开/Left 回父级）、
// MenuBar 构建/锚定打开/左右切换/Alt 助记与语义角色。
//
// 命名遵循项目测试规范（行为命名，*_tests.cpp）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/widgets/menu.h"

using namespace lumen;
using namespace lumen::core;
using lumen::widgets::ContextMenuController;
using lumen::widgets::MenuBarController;
using lumen::widgets::MenuItem;
using lumen::widgets::MenuItems;

namespace {

MenuItems sampleItems() {
    return MenuItems{
        MenuItem{.id = "open", .label = "打开",
                 .icon = core::IconId::Search, .shortcut = "Ctrl+O"},
        MenuItem{.id = "sep", .separator = true},
        MenuItem{.id = "cut", .label = "剪切", .shortcut = "Ctrl+X"},
        MenuItem{.id = "paste", .label = "粘贴", .shortcut = "Ctrl+V",
                 .enabled = false},
        MenuItem{.id = "sort", .label = "排序方式", .hasSubmenu = true},
        MenuItem{.id = "check", .label = "显示侧栏", .checkable = true,
                 .checked = true, .shortcut = "Ctrl+B", .mnemonic = 's'},
    };
}

class MenuApp {
  public:
    app::AppShell shell{makeConfig()};
    ContextMenuController context;
    std::vector<std::string> commands;

    static app::ShellConfig makeConfig() {
        app::ShellConfig config;
        config.build = [] {
            // 命中目标：一个常驻按钮（Secondary 不应触发它）。
            return makeColumn(
                {withKey(makeButton("Target", TextStyle{}, EdgeInsets{},
                                    0.0F, "target", 120.0F, 40.0F,
                                    "target-click"),
                         "target")},
                MainAxisAlignment::Center, CrossAxisAlignment::Center);
        };
        return config;
    }

    MenuApp() {
        shell.setView(Size{800.0F, 600.0F});
        context.onCommand = [this](const std::string& id) {
            commands.push_back(id);
        };
        shell.rebuildIfDirty();
    }

    void openSample() {
        context.open(shell, Offset{100.0F, 100.0F}, sampleItems());
    }

    [[nodiscard]] const RenderNode* overlayItem(std::size_t level,
                                                std::size_t index) const {
        const RenderNode* overlay = shell.overlayRoot();
        if (overlay == nullptr) {
            return nullptr;
        }
        return findNodeByKey(
            *overlay, "ctx:m" + std::to_string(level) + ":i" +
                          std::to_string(index));
    }
};

}  // namespace

TEST_CASE("secondary_press_skips_click_and_presses", "[widgets][menu]") {
    MenuApp app;
    int clicked = 0;
    app.shell.handlers()["target-click"] = [&clicked] { ++clicked; };
    const Offset center = absoluteOffset(app.shell.root(), "target") +
                          Offset{60.0F, 20.0F};

    // Primary 点击：handler 触发（对照组）。
    app.shell.pointerDown(center);
    app.shell.pointerUp(center);
    CHECK(clicked == 1);

    // Secondary 按下：不触发点击、不产生按压目标。
    app.shell.pointerDown(center, core::kModifierNone,
                          core::PointerButton::Secondary);
    app.shell.pointerUp(center, core::PointerButton::Secondary);
    CHECK(clicked == 1);
    CHECK(app.shell.controller().pressedKey().empty());
}

TEST_CASE("secondary_press_sink_receives_hit_chain", "[widgets][menu]") {
    MenuApp app;
    int sinkCalls = 0;
    bool sawTarget = false;
    app.shell.controller().addSecondaryPressSink(
        [&](const std::vector<const RenderNode*>& chain, Offset) {
            ++sinkCalls;
            for (const RenderNode* node : chain) {
                if (node->key == "target") {
                    sawTarget = true;
                }
            }
            return true;
        });
    const Offset center = absoluteOffset(app.shell.root(), "target") +
                          Offset{60.0F, 20.0F};
    app.shell.pointerDown(center, core::kModifierNone,
                          core::PointerButton::Secondary);
    CHECK(sinkCalls == 1);
    CHECK(sawTarget);
    // sink 链按注册序问询：消费即停——后注册的 sink 不再被问询。
    int other = 0;
    app.shell.controller().addSecondaryPressSink(
        [&](const std::vector<const RenderNode*>&, Offset) {
            ++other;
            return false;
        });
    app.shell.pointerDown(center, core::kModifierNone,
                          core::PointerButton::Secondary);
    CHECK(sinkCalls == 2);
    CHECK(other == 0);
}

TEST_CASE("context_menu_opens_at_pointer_and_lists_items",
          "[widgets][menu]") {
    MenuApp app;
    app.openSample();
    REQUIRE(app.shell.overlayRoot() != nullptr);
    REQUIRE(app.context.isOpen());
    // 全部可交互项物化且行 key 前缀为 ctx:m0:iN（分隔线是纯视觉行，
    // 不携带 key/焦点/语义）。
    for (std::size_t i : {std::size_t{0}, std::size_t{2}, std::size_t{3},
                          std::size_t{4}, std::size_t{5}}) {
        CHECK(app.overlayItem(0, i) != nullptr);
    }
    CHECK(app.overlayItem(0, 1) == nullptr);  // separator 无 key
    // 面板在指针附近展开（原点 = 指针 + (2,2)，钳制 8px 边距内）。
    const RenderNode* panel =
        findNodeByKey(*app.shell.overlayRoot(), "ctx:panel:0");
    REQUIRE(panel != nullptr);
    CHECK(panel->offset.x >= 8.0F);
    CHECK(panel->offset.y >= 8.0F);
    CHECK(panel->size.width > 0.0F);
    // 打开即聚焦首个可聚焦项（键盘就绪）。
    const RenderNode* focused = findNodeByIdentity(
        *app.shell.overlayRoot(), app.shell.focus().focusedIdentity());
    CHECK(focused == app.overlayItem(0, 0));
}

TEST_CASE("context_menu_keyboard_skips_separator_and_disabled",
          "[widgets][menu]") {
    MenuApp app;
    app.openSample();
    // 0(open) → Down → 2(cut)：跳过分隔线；再 Down → 4(sort)：跳过禁用。
    CHECK(app.context.handleKey(app.shell, Key::Down));
    CHECK(app.shell.focus().focusedKey() == "ctx:m0:i2");
    CHECK(app.context.handleKey(app.shell, Key::Down));
    CHECK(app.shell.focus().focusedKey() == "ctx:m0:i4");
    // End → 末个可聚焦项（5.check，无子菜单路径仍可达）；Home 回首项。
    CHECK(app.context.handleKey(app.shell, Key::End));
    CHECK(app.shell.focus().focusedKey() == "ctx:m0:i5");
    CHECK(app.context.handleKey(app.shell, Key::Home));
    CHECK(app.shell.focus().focusedKey() == "ctx:m0:i0");
}

TEST_CASE("context_menu_enter_activates_and_restores_focus",
          "[widgets][menu]") {
    MenuApp app;
    // 先聚焦常驻按钮（关闭后恢复目标）。
    if (const RenderNode* target =
            findNodeByKey(app.shell.root(), "target")) {
        app.shell.controller().focusNode(*target);
    }
    app.openSample();
    app.context.handleKey(app.shell, Key::Down);   // 0(open) → 2(cut)：跳过分隔线
    REQUIRE(app.shell.focus().focusedKey() == "ctx:m0:i2");
    CHECK(app.context.handleKey(app.shell, Key::Enter));
    CHECK(app.commands == std::vector<std::string>{"cut"});
    CHECK(!app.context.isOpen());
    CHECK(app.shell.overlayRoot() == nullptr);
    CHECK(app.shell.focus().focusedKey() == "target");
}

TEST_CASE("context_menu_barrier_click_closes", "[widgets][menu]") {
    MenuApp app;
    app.openSample();
    REQUIRE(app.context.isOpen());
    // 点击 barrier 区域（左上角远离面板）。
    app.shell.pointerDown(Offset{20.0F, 580.0F});
    app.shell.pointerUp(Offset{20.0F, 580.0F});
    CHECK(!app.context.isOpen());
    CHECK(app.shell.overlayRoot() == nullptr);
    CHECK(app.commands.empty());
}

TEST_CASE("context_menu_barrier_is_transparent_input_modal",
          "[widgets][menu]") {
    // barrier 仅输入模态：视觉透明（不压暗内容——Dialog scrim 只属于
    // 对话框），Dismiss 语义保留。
    MenuApp app;
    app.openSample();
    const RenderNode* barrier =
        findNodeByKey(*app.shell.overlayRoot(), "ctx-menu-barrier");
    REQUIRE(barrier != nullptr);
    CHECK(barrier->commonStyle().background ==
          core::Color::transparent());
    CHECK(barrier->semanticsActions == accessibility::kActionDismiss);
    CHECK(barrier->rect().size == app.shell.view());
}

TEST_CASE("context_menu_rows_hover_highlight", "[widgets][menu]") {
    // 集合行 hover 追踪（interaction 侧 collectionRow 谓词）：悬停行
    // 解析为 hoverOverlay 背景，未悬停行保持透明；禁用行不承载。
    MenuApp app;
    app.openSample();
    const RenderNode* hovered = app.overlayItem(0, 2);   // 剪切
    const RenderNode* other = app.overlayItem(0, 4);     // 排序方式
    REQUIRE(hovered != nullptr);
    REQUIRE(other != nullptr);
    app.shell.pointerMove(absoluteOffset(*app.shell.overlayRoot(),
                                         hovered->key) +
                          Offset{hovered->size.width * 0.5F,
                                 hovered->size.height * 0.5F});
    (void)app.shell.renderFrame();
    hovered = app.overlayItem(0, 2);
    other = app.overlayItem(0, 4);
    REQUIRE(hovered != nullptr);
    REQUIRE(other != nullptr);
    CHECK(hovered->commonStyle().background ==
          app.shell.theme().colors.hoverOverlay);
    CHECK(other->commonStyle().background == core::Color::transparent());
}

TEST_CASE("menu_rows_suppress_focus_ring", "[widgets][menu]") {
    // 菜单行 current 指示由动能矩形承载：焦点行不叠 2px 内嵌环（环叠
    // 选中底色双指示冗余且过亮；集合行保持底+环视觉不受影响）。
    MenuApp app;
    app.openSample();
    const RenderNode* focused = app.overlayItem(0, 0);
    REQUIRE(focused != nullptr);
    CHECK(focused->commonStyle().focusWidth == 0.0F);
}

TEST_CASE("menu_long_label_ellipsizes_single_line", "[widgets][menu]") {
    // §10.1：超宽标签单行省略号——不换行、行高与常规行一致（动能
    // 矩形/滚动推导都以行高稳定为前提）。
    MenuApp app;
    MenuItems items{
        MenuItem{.id = "short", .label = "打开"},
        MenuItem{.id = "long",
                 .label = "一个特别长的菜单项标签用于验证超宽省略号不换行"},
        MenuItem{.id = "sc", .label = "快捷",
                 .shortcut = "Ctrl+Shift+Alt+Delete"},
    };
    app.context.open(app.shell, Offset{100.0F, 100.0F}, std::move(items));
    const RenderNode* shortRow = app.overlayItem(0, 0);
    const RenderNode* longRow = app.overlayItem(0, 1);
    REQUIRE(shortRow != nullptr);
    REQUIRE(longRow != nullptr);
    CHECK(longRow->size.height ==
          Catch::Approx(shortRow->size.height).margin(0.01F));
    CHECK(longRow->size.height > 0.0F);
    // 标签样式接线：单行 + Ellipsis（行内第 2 子 = slot 容器之后的
    // 文本节点）。
    REQUIRE(longRow->children.size() >= 2);
    const RenderNode* labelNode = nullptr;
    for (const auto& child : longRow->children) {
        if (child.type == WidgetType::Text) {
            labelNode = &child;
            break;
        }
    }
    REQUIRE(labelNode != nullptr);
    CHECK(labelNode->commonStyle().text.maxLines == 1);
    CHECK(labelNode->commonStyle().text.overflow ==
          core::TextOverflow::Ellipsis);
    CHECK(labelNode->size.height <= longRow->size.height);
}

TEST_CASE("context_menu_checkable_and_shortcut_display_only",
          "[widgets][menu]") {
    MenuApp app;
    app.openSample();
    // checkable + checked：行内图标槽携带 Check。
    const RenderNode* checkRow = app.overlayItem(0, 5);
    REQUIRE(checkRow != nullptr);
    REQUIRE(checkRow->children.size() >= 2);
    // 槽位容器 → Icon 子节点。
    const RenderNode& slotBox = checkRow->children[0];
    REQUIRE(!slotBox.children.empty());
    CHECK(static_cast<core::IconId>(slotBox.children[0].icon) ==
          core::IconId::Check);
    // 快捷键列 = 行尾 Text（仅展示；菜单不注册任何 Ctrl+S 分发）。
    bool shortcutText = false;
    for (const auto& child : checkRow->children) {
        if (child.type == WidgetType::Text && child.text == "Ctrl+B") {
            shortcutText = true;
        }
    }
    CHECK(shortcutText);
}

TEST_CASE("context_menu_submenu_cascade_right_left_escape",
          "[widgets][menu]") {
    MenuApp app;
    app.context.open(
        app.shell, Offset{100.0F, 100.0F}, sampleItems(),
        [](const std::string& id) {
            return id == "sort"
                       ? MenuItems{MenuItem{.id = "by-name",
                                            .label = "按名称",
                                            .checked = true},
                                   MenuItem{.id = "by-time",
                                            .label = "按时间"}}
                       : MenuItems{};
        });
    // 高亮到 sort 项后 Right 展开子级。
    app.context.handleKey(app.shell, Key::End);   // 高亮 5(check)
    app.context.handleKey(app.shell, Key::Up);    // 回到 4(sort)
    REQUIRE(app.shell.focus().focusedKey() == "ctx:m0:i4");
    CHECK(app.context.handleKey(app.shell, Key::Right));
    CHECK(app.context.levelCount() == 2);
    CHECK(app.shell.focus().focusedKey() == "ctx:m1:i0");
    // Left 回父级；Escape 逐级关闭。
    CHECK(app.context.handleKey(app.shell, Key::Left));
    CHECK(app.context.levelCount() == 1);
    CHECK(app.shell.focus().focusedKey() == "ctx:m0:i4");
    CHECK(app.context.handleKey(app.shell, Key::Escape));
    CHECK(!app.context.isOpen());
    // Tab 关闭全部（模态不逃逸）。
    app.openSample();
    CHECK(app.context.handleKey(app.shell, Key::Tab));
    CHECK(!app.context.isOpen());
}

TEST_CASE("context_menu_submenu_item_click_activates_command",
          "[widgets][menu]") {
    MenuApp app;
    app.context.open(
        app.shell, Offset{100.0F, 100.0F}, sampleItems(),
        [](const std::string& id) {
            return id == "sort"
                       ? MenuItems{MenuItem{.id = "by-name",
                                            .label = "按名称"}}
                       : MenuItems{};
        });
    app.context.handleKey(app.shell, Key::End);
    app.context.handleKey(app.shell, Key::Up);
    app.context.handleKey(app.shell, Key::Right);
    REQUIRE(app.context.levelCount() == 2);
    app.context.handleKey(app.shell, Key::Enter);
    CHECK(app.commands == std::vector<std::string>{"by-name"});
    CHECK(!app.context.isOpen());
}

TEST_CASE("context_menu_mnemonic_alt_letter_activates",
          "[widgets][menu]") {
    MenuApp app;
    app.openSample();
    // check 项 mnemonic 's'：Alt+s 直接激活。
    CHECK(app.context.handleKey(app.shell, Key::None,
                                core::kModifierAlt, 's'));
    CHECK(app.commands == std::vector<std::string>{"check"});
    CHECK(!app.context.isOpen());
}

TEST_CASE("context_menu_items_carry_menu_semantics", "[widgets][menu]") {
    MenuApp app;
    app.openSample();
    const RenderNode* panel =
        findNodeByKey(*app.shell.overlayRoot(), "ctx:panel:0");
    REQUIRE(panel != nullptr);
    accessibility::SemanticsRole role{};
    REQUIRE(accessibility::semanticsRoleFromName(panel->semanticsRole,
                                                 &role));
    CHECK(role == accessibility::SemanticsRole::Menu);
    const RenderNode* item = app.overlayItem(0, 0);
    REQUIRE(item != nullptr);
    REQUIRE(accessibility::semanticsRoleFromName(item->semanticsRole,
                                                 &role));
    CHECK(role == accessibility::SemanticsRole::MenuItem);
    CHECK(item->semanticsLabel == "打开");

    const auto tree =
        accessibility::buildSemanticsTree(*app.shell.overlayRoot());
    const RenderNode* submenu = app.overlayItem(0, 4);
    const RenderNode* checked = app.overlayItem(0, 5);
    REQUIRE(submenu != nullptr);
    REQUIRE(checked != nullptr);
    const accessibility::SemanticsNode* submenuSemantic =
        tree.find(submenu->identity);
    const accessibility::SemanticsNode* checkedSemantic =
        tree.find(checked->identity);
    REQUIRE(submenuSemantic != nullptr);
    REQUIRE(checkedSemantic != nullptr);
    CHECK(submenuSemantic->value == "hasSubmenu=true");
    CHECK((checkedSemantic->flags & accessibility::kSemanticsChecked) != 0);
}

TEST_CASE("context_menu_wheel_persists_scroll_offset",
          "[widgets][menu]") {
    MenuApp app;
    app.shell.setView(Size{300.0F, 180.0F});
    app.shell.rebuildIfDirty();
    MenuItems items;
    for (int i = 0; i < 20; ++i) {
        items.push_back(MenuItem{.id = "item-" + std::to_string(i),
                                 .label = "Item " + std::to_string(i)});
    }
    app.context.open(app.shell, Offset{20.0F, 20.0F}, std::move(items));
    REQUIRE(app.shell.overlayRoot() != nullptr);
    const std::string key = "ctx:m0:scroll";
    const RenderNode* viewport =
        findNodeByKey(*app.shell.overlayRoot(), key);
    REQUIRE(viewport != nullptr);
    REQUIRE(viewport->scrollExtent > 0.0F);
    const Offset point = absoluteOffset(*app.shell.overlayRoot(), key) +
                         Offset{20.0F, 20.0F};
    CHECK(app.shell.wheel(point, Offset{0.0F, 60.0F}));
    app.shell.rebuildIfDirty();
    viewport = findNodeByKey(*app.shell.overlayRoot(), key);
    REQUIRE(viewport != nullptr);
    CHECK(viewport->scrollOffset == Catch::Approx(60.0F));
}

TEST_CASE("context_menu_in_panel_drag_keeps_menu_open",
          "[widgets][menu]") {
    MenuApp app;
    app.shell.setView(Size{300.0F, 180.0F});
    app.shell.rebuildIfDirty();
    MenuItems items;
    for (int i = 0; i < 20; ++i) {
        items.push_back(MenuItem{.id = "item-" + std::to_string(i),
                                 .label = "Item " + std::to_string(i)});
    }
    app.context.open(app.shell, Offset{20.0F, 20.0F}, std::move(items));
    REQUIRE(app.shell.overlayRoot() != nullptr);
    REQUIRE(findNodeByKey(*app.shell.overlayRoot(), "ctx:m0:scroll") !=
            nullptr);  // 超长菜单：面板内是滚动视口
    // 面板内按下并拖动超过 slop：首版无菜单内拖拽滚动，菜单保持打开
    //（拖动起点在菜单视口上不构成关闭）。
    const Offset anchor =
        absoluteOffset(*app.shell.overlayRoot(), "ctx:m0:i2") +
        Offset{40.0F, 8.0F};
    app.shell.pointerDown(anchor);
    app.shell.pointerMove(anchor + Offset{0.0F, 30.0F});
    app.shell.pointerUp(anchor + Offset{0.0F, 30.0F});
    CHECK(app.context.isOpen());
    app.shell.rebuildIfDirty();
    const RenderNode* viewport = findNodeByKey(*app.shell.overlayRoot(),
                                               "ctx:m0:scroll");
    REQUIRE(viewport != nullptr);
    CHECK(viewport->scrollOffset == Catch::Approx(0.0F));
}

TEST_CASE("context_menu_wheel_outside_closes_menu",
          "[widgets][menu]") {
    MenuApp app;
    app.shell.setView(Size{300.0F, 180.0F});
    app.shell.rebuildIfDirty();
    // 指针锚面板：x ∈ [22, 242]；(280, 160) 在面板右缘之外（barrier）。
    app.context.open(app.shell, Offset{20.0F, 20.0F}, sampleItems());
    REQUIRE(app.context.isOpen());
    // 滚轮（任意位置）关闭（menu-controls-design §6.4）。
    CHECK(app.shell.wheel(Offset{280.0F, 160.0F}, Offset{0.0F, 60.0F}));
    CHECK(!app.context.isOpen());
}

TEST_CASE("secondary_activity_does_not_disturb_primary_click",
          "[widgets][menu]") {
    MenuApp app;
    int clicked = 0;
    app.shell.handlers()["target-click"] = [&clicked] { ++clicked; };
    const Offset center = absoluteOffset(app.shell.root(), "target") +
                          Offset{60.0F, 20.0F};
    // 主键按压期间插入右键按下/释放：主键点击照常触发（Secondary 不进
    // 入 press/armed/drag 路径，menu-controls-design §6.2）。
    app.shell.pointerDown(center);
    app.shell.pointerDown(Offset{400.0F, 500.0F}, core::kModifierNone,
                          core::PointerButton::Secondary);
    app.shell.pointerUp(Offset{400.0F, 500.0F},
                        core::PointerButton::Secondary);
    app.shell.pointerUp(center);
    CHECK(clicked == 1);
}

TEST_CASE("menu_bar_builds_row_and_opens_anchored_below",
          "[widgets][menu]") {
    app::AppShell shell{MenuApp::makeConfig()};
    shell.setView(Size{800.0F, 600.0F});
    MenuBarController bar;
    bar.setMenus({{"file", "文件(F)", 'f'}, {"view", "视图(V)", 'v'},
                  {"help", "帮助(H)", 'h'}});
    bar.setMenuProvider([](const std::string& id) {
        return id == "file" ? MenuItems{MenuItem{.id = "new",
                                                 .label = "新建窗口",
                                                 .shortcut = "Ctrl+N"}}
                            : MenuItems{MenuItem{.id = "about",
                                                 .label = "关于"}};
    });
    bar.attach(shell);
    // 栏并入主树。
    Widget page = makeColumn({bar.build(shell.theme())}, MainAxisAlignment::Start,
                             CrossAxisAlignment::Start);
    shell.swapRoot(std::move(page));
    shell.rebuildIfDirty();
    const RenderNode* fileItem = findNodeByKey(shell.root(), "menu:bar:file");
    REQUIRE(fileItem != nullptr);

    // 点击栏项：菜单锚定其下方（面板顶 ≥ 栏项底）。
    const Offset itemCenter = absoluteOffset(shell.root(), "menu:bar:file") +
                              Offset{30.0F, 16.0F};
    shell.pointerDown(itemCenter);
    shell.pointerUp(itemCenter);
    REQUIRE(bar.isOpen());
    const RenderNode* panel =
        findNodeByKey(*shell.overlayRoot(), "menubar:panel:0");
    REQUIRE(panel != nullptr);
    // Opening the menu rebuilds the main tree; the pre-click node is stale.
    fileItem = findNodeByKey(shell.root(), "menu:bar:file");
    REQUIRE(fileItem != nullptr);
    const float barBottom = absoluteOffset(shell.root(), "menu:bar:file").y +
                            fileItem->size.height;
    CHECK(panel->offset.y >= barBottom);

    // 顶级 Right：切换到视图菜单（provider 项随之变化）。
    CHECK(bar.handleKey(shell, Key::Right));
    panel = findNodeByKey(*shell.overlayRoot(), "menubar:panel:0");
    REQUIRE(panel != nullptr);
    CHECK(shell.focus().focusedKey() == "menubar:m0:i0");

    // Esc：关闭并焦点恢复栏项。
    CHECK(bar.handleKey(shell, Key::Escape));
    CHECK(!bar.isOpen());
    CHECK(shell.focus().focusedKey() == "menu:bar:view");
}

TEST_CASE("menu_bar_alt_mnemonic_opens_menu", "[widgets][menu]") {
    app::AppShell shell{MenuApp::makeConfig()};
    shell.setView(Size{800.0F, 600.0F});
    MenuBarController bar;
    bar.setMenus({{"file", "文件(F)", 'f'}});
    bar.setMenuProvider([](const std::string&) {
        return MenuItems{MenuItem{.id = "new", .label = "新建窗口"}};
    });
    bar.attach(shell);
    shell.swapRoot(makeColumn({bar.build(shell.theme())}, MainAxisAlignment::Start,
                              CrossAxisAlignment::Start));
    shell.rebuildIfDirty();

    CHECK(bar.handleKey(shell, Key::None, core::kModifierAlt, 'f'));
    CHECK(bar.isOpen());
    // 命令经统一 onCommand 转发。
    std::string fired;
    bar.onCommand = [&](const std::string& id) { fired = id; };
    bar.handleKey(shell, Key::Enter);
    CHECK(fired == "new");
    CHECK(!bar.isOpen());
}

TEST_CASE("menu_bar_hover_switches_open_top_level_menu",
          "[widgets][menu]") {
    app::AppShell shell{MenuApp::makeConfig()};
    shell.setView(Size{800.0F, 600.0F});
    MenuBarController bar;
    bar.setMenus({{"file", "文件(F)", 'f'}, {"view", "视图(V)", 'v'}});
    bar.setMenuProvider([](const std::string& id) {
        return id == "file"
                   ? MenuItems{MenuItem{.id = "new", .label = "新建"}}
                   : MenuItems{MenuItem{.id = "zoom", .label = "缩放"}};
    });
    std::string fired;
    bar.onCommand = [&](const std::string& id) { fired = id; };
    bar.attach(shell);
    shell.swapRoot(makeColumn({bar.build(shell.theme())}, MainAxisAlignment::Start,
                              CrossAxisAlignment::Start));
    shell.rebuildIfDirty();

    const RenderNode* file = findNodeByKey(shell.root(), "menu:bar:file");
    REQUIRE(file != nullptr);
    const Offset fileCenter = absoluteOffset(shell.root(), file->key) +
                              Offset{file->size.width * 0.5F,
                                     file->size.height * 0.5F};
    shell.pointerDown(fileCenter);
    shell.pointerUp(fileCenter);
    REQUIRE(bar.isOpen());

    const RenderNode* view = findNodeByKey(shell.root(), "menu:bar:view");
    REQUIRE(view != nullptr);
    const Offset viewCenter = absoluteOffset(shell.root(), view->key) +
                              Offset{view->size.width * 0.5F,
                                     view->size.height * 0.5F};
    shell.pointerMove(viewCenter);
    REQUIRE(bar.isOpen());
    CHECK(shell.focus().focusedKey() == "menubar:m0:i0");
    CHECK(bar.handleKey(shell, Key::Enter));
    CHECK(fired == "zoom");
}
