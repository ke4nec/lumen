// 菜单动效（M14，design/menubar-variants.html 版本 A+D）测试：面板打开
// 淡入 + 上升/级联滑入（menuOpenFadeMs 同一进度）、键盘高亮动能矩形滑移
//（menuHighlightSlideMs，跨分隔线高度变形）、MenuBar 打开态（Tonal +
// 下划线渐入）、reduceAnimation 首拍终态、不经 tick 的直驱即时终态与
// overlay animate 生命周期。
//
// 驱动口径与 motion_scroll_tests 同源：motionTransitions opt-in + tick 固
// 定时间戳（应用持钟，headless 确定性）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "lumen/accessibility/bridge.h"
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

// motionTransitions opt-in 的上下文菜单宿主。
class MotionMenuApp {
  public:
    app::AppShell shell{makeConfig()};
    ContextMenuController context;

    static app::ShellConfig makeConfig() {
        app::ShellConfig config;
        config.motionTransitions = true;
        config.build = [] {
            return makeColumn(
                {withKey(makeButton("Target", TextStyle{}, EdgeInsets{},
                                    0.0F, "target", 120.0F, 40.0F,
                                    "target-click"),
                         "target")},
                MainAxisAlignment::Center, CrossAxisAlignment::Center);
        };
        return config;
    }

    MotionMenuApp() {
        shell.setView(Size{800.0F, 600.0F});
        shell.rebuildIfDirty();
    }

    void openSample() {
        context.open(shell, Offset{100.0F, 100.0F}, sampleItems());
    }

    [[nodiscard]] const RenderNode* panel() const {
        return shell.overlayRoot() == nullptr
                   ? nullptr
                   : findNodeByKey(*shell.overlayRoot(), "ctx:panel:0");
    }

    [[nodiscard]] const RenderNode* marquee() const {
        return shell.overlayRoot() == nullptr
                   ? nullptr
                   : findNodeByKey(*shell.overlayRoot(), "ctx:hl:0");
    }

    [[nodiscard]] const RenderNode* row(std::size_t level,
                                        std::size_t index) const {
        return shell.overlayRoot() == nullptr
                   ? nullptr
                   : findNodeByKey(*shell.overlayRoot(),
                                   "ctx:m" + std::to_string(level) + ":i" +
                                       std::to_string(index));
    }
};

// motionTransitions opt-in 的菜单栏宿主：config.build 每次重建重取
// bar.build(theme)——栏的 Tonal/下划线是主树状态，静态 swapRoot 模板
// 不会随动效采样刷新。
class MotionBarApp {
  public:
    app::AppShell shell{makeConfig(this)};
    MenuBarController bar;
    std::vector<std::string> commands;

    static app::ShellConfig makeConfig(MotionBarApp* self) {
        app::ShellConfig config;
        config.motionTransitions = true;
        config.build = [self] {
            return makeColumn({self->bar.build(self->shell.theme())},
                              MainAxisAlignment::Start, CrossAxisAlignment::Start);
        };
        return config;
    }

    MotionBarApp() {
        shell.setView(Size{800.0F, 600.0F});
        bar.setMenus({{"file", "File", 'f'}, {"view", "View", 'v'}});
        bar.setMenuProvider([](const std::string& id) {
            return id == "file"
                       ? MenuItems{MenuItem{.id = "new", .label = "New",
                                            .shortcut = "Ctrl+N"},
                                   MenuItem{.id = "sep", .separator = true},
                                   MenuItem{.id = "close", .label = "Close"}}
                       : MenuItems{MenuItem{.id = "zoomIn",
                                            .label = "Zoom In"}};
        });
        bar.onCommand = [this](const std::string& id) {
            commands.push_back(id);
        };
        bar.attach(shell);
        shell.rebuildIfDirty();
    }

    // 键盘打开（避免指针 hover 混入背景色断言）：聚焦栏项后 Down。
    void openByKeyboard(const std::string& id) {
        const RenderNode* item = findNodeByKey(shell.root(), "menu:bar:" + id);
        REQUIRE(item != nullptr);
        shell.controller().focusNode(*item);
        REQUIRE(bar.handleKey(shell, Key::Down));
    }

    [[nodiscard]] const RenderNode* barButton(const std::string& id) const {
        return findNodeByKey(shell.root(), "menu:bar:" + id);
    }

    [[nodiscard]] const RenderNode* underline(const std::string& id) const {
        return findNodeByKey(shell.root(), "menu:bar:" + id + ":ul");
    }
};

}  // namespace

TEST_CASE("menu_open_fades_and_rises_with_ticks", "[widgets][menu][motion]") {
    MotionMenuApp app;
    app.shell.tick(100);  // motionEnabled 需已被 tick 驱动

    app.openSample();
    const RenderNode* panel = app.panel();
    REQUIRE(panel != nullptr);
    const float finalY = panel->offset.y;  // open 后未经 tick 的直驱终态
    CHECK(panel->transitionAlpha == 1.0F);

    // 首拍起表：alpha 0 + 上升 6px（版本 A）。
    app.shell.tick(100);
    app.shell.rebuildIfDirty();
    panel = app.panel();
    REQUIRE(panel != nullptr);
    CHECK(panel->transitionAlpha == 0.0F);
    CHECK(panel->offset.y == Catch::Approx(finalY + 6.0F).margin(0.01F));
    CHECK(app.shell.animationsActive());

    // 中段：EaseOut 单调推进（顺带压一次完整绘制——painter 整树透明度
    // 与 overlay damage 路径冒烟）。
    app.shell.tick(160);
    (void)app.shell.renderFrame();
    panel = app.panel();
    REQUIRE(panel != nullptr);
    CHECK(panel->transitionAlpha > 0.0F);
    CHECK(panel->transitionAlpha < 1.0F);
    CHECK(panel->offset.y > finalY);
    CHECK(panel->offset.y < finalY + 6.0F);

    // 终拍：alpha 1、上升归零、动画源退休。
    app.shell.tick(100 + 120);
    app.shell.rebuildIfDirty();
    panel = app.panel();
    REQUIRE(panel != nullptr);
    CHECK(panel->transitionAlpha == 1.0F);
    CHECK(panel->offset.y == Catch::Approx(finalY).margin(0.01F));
    CHECK_FALSE(app.shell.animationsActive());
}

TEST_CASE("menu_keyboard_highlight_marquee_slides_between_rows",
          "[widgets][menu][motion]") {
    MotionMenuApp app;
    app.shell.tick(0);
    app.openSample();
    app.shell.tick(200);  // 打开动效完成
    app.shell.rebuildIfDirty();

    // 动效口径：高亮背景由动能矩形承载（行不折算 selected——两口径
    // 统一，current 语义由焦点表达）。
    const RenderNode* marquee = app.marquee();
    REQUIRE(marquee != nullptr);
    const RenderNode* row0 = app.row(0, 0);
    const RenderNode* row2 = app.row(0, 2);
    REQUIRE(row0 != nullptr);
    REQUIRE(row2 != nullptr);
    CHECK(app.row(0, 0)->selected == false);
    // 矩形与行精确对齐（x/宽度/顶部——column 内边距偏移同域）。
    CHECK(marquee->offset.x == Catch::Approx(row0->offset.x).margin(0.01F));
    CHECK(marquee->offset.y == Catch::Approx(row0->offset.y).margin(0.01F));
    CHECK(marquee->size.width ==
          Catch::Approx(row0->size.width).margin(0.01F));
    CHECK(marquee->size.height ==
          Catch::Approx(row0->size.height).margin(0.01F));

    // Down：0(open) → 2(cut)，跨分隔线滑移（跳过 index 1）。pending 首
    // 拍起表：迁移后的第一次 tick 盖章（sample(0) = 起点），此后推进。
    REQUIRE(app.context.handleKey(app.shell, Key::Down));
    app.shell.tick(300);
    app.shell.rebuildIfDirty();
    marquee = app.marquee();
    REQUIRE(marquee != nullptr);
    CHECK(marquee->offset.y == Catch::Approx(row0->offset.y).margin(0.01F));

    app.shell.tick(300 + 45);  // 45/90ms：滑移中段
    app.shell.rebuildIfDirty();
    marquee = app.marquee();
    REQUIRE(marquee != nullptr);
    CHECK(marquee->offset.y > row0->offset.y);
    CHECK(marquee->offset.y < row2->offset.y);

    app.shell.tick(300 + 90);
    app.shell.rebuildIfDirty();
    marquee = app.marquee();
    REQUIRE(marquee != nullptr);
    CHECK(marquee->offset.y == Catch::Approx(row2->offset.y).margin(0.01F));
    CHECK(marquee->offset.x == Catch::Approx(row2->offset.x).margin(0.01F));
    CHECK(marquee->size.width ==
          Catch::Approx(row2->size.width).margin(0.01F));
    CHECK(marquee->size.height ==
          Catch::Approx(row2->size.height).margin(0.01F));
    CHECK_FALSE(app.shell.animationsActive());
}

TEST_CASE("menubar_open_state_tonal_underline_and_switch",
          "[widgets][menu][motion]") {
    MotionBarApp app;
    app.shell.tick(0);

    app.openByKeyboard("file");
    // 栏项切换 Tonal（打开态，§10.4），其余保持 Ghost 透明。
    const RenderNode* fileBtn = app.barButton("file");
    const RenderNode* viewBtn = app.barButton("view");
    REQUIRE(fileBtn != nullptr);
    REQUIRE(viewBtn != nullptr);
    CHECK(fileBtn->commonStyle().background ==
          app.shell.theme().button.tonal.background);
    CHECK(viewBtn->commonStyle().background ==
          app.shell.theme().button.ghost.background);

    // 下划线渐入：首拍 0 → 终拍 1。
    app.shell.tick(10);
    app.shell.rebuildIfDirty();
    const RenderNode* ul = app.underline("file");
    REQUIRE(ul != nullptr);
    CHECK(ul->transitionAlpha == 0.0F);
    app.shell.tick(10 + 120);
    app.shell.rebuildIfDirty();
    ul = app.underline("file");
    REQUIRE(ul != nullptr);
    CHECK(ul->transitionAlpha == 1.0F);

    // 顶级切换（Right）：下划线随锚点迁移并重放渐入（版本 A 契约）。
    REQUIRE(app.bar.handleKey(app.shell, Key::Right));
    app.shell.tick(140);
    app.shell.rebuildIfDirty();
    CHECK(app.underline("file") == nullptr);
    ul = app.underline("view");
    REQUIRE(ul != nullptr);
    CHECK(ul->transitionAlpha == 0.0F);
    app.shell.tick(140 + 120);
    app.shell.rebuildIfDirty();
    ul = app.underline("view");
    REQUIRE(ul != nullptr);
    CHECK(ul->transitionAlpha == 1.0F);

    // 关闭：Tonal/下划线随栏重建消失。
    app.bar.close(app.shell);
    app.shell.rebuildIfDirty();
    CHECK(app.underline("view") == nullptr);
    CHECK(app.barButton("view")->commonStyle().background ==
          app.shell.theme().button.ghost.background);
}

TEST_CASE("reduce_animation_menu_motion_reaches_end_on_first_tick",
          "[widgets][menu][motion]") {
    MotionBarApp app;
    app.shell.tick(0);
    accessibility::AccessibilitySettings reduced;
    reduced.reduceAnimation = true;
    app.shell.setAccessibilitySettings(reduced);
    app.shell.rebuildIfDirty();

    app.openByKeyboard("file");
    app.shell.tick(10);
    app.shell.rebuildIfDirty();
    // 零时长：首拍即达终态（下划线 1、面板 1、动画源不活跃）。
    const RenderNode* ul = app.underline("file");
    REQUIRE(ul != nullptr);
    CHECK(ul->transitionAlpha == 1.0F);
    const RenderNode* panel = findNodeByKey(*app.shell.overlayRoot(),
                                            "menubar:panel:0");
    REQUIRE(panel != nullptr);
    CHECK(panel->transitionAlpha == 1.0F);
    CHECK_FALSE(app.shell.animationsActive());

    // 高亮滑移同样零时长：Down 后动能矩形直达目标行（跳过 sep：0→2）。
    REQUIRE(app.bar.handleKey(app.shell, Key::Down));
    app.shell.tick(20);
    app.shell.rebuildIfDirty();
    const RenderNode* marquee =
        findNodeByKey(*app.shell.overlayRoot(), "menubar:hl:0");
    const RenderNode* target =
        findNodeByKey(*app.shell.overlayRoot(), "menubar:m0:i2");
    REQUIRE(marquee != nullptr);
    REQUIRE(target != nullptr);
    CHECK(marquee->offset.y == Catch::Approx(target->offset.y).margin(0.01F));
}

// 悬停展开发生在 tick 内（stepPendingSubmenu 在 stepMotion 之前）：
// 同拍新增层级立即起表，首绘即动效起点（alpha 0 + 级联滑入位移），
// 不得以 Level 默认终态先绘制一帧——慢帧率（Debug）下该终态闪帧
// 呈现"子菜单先整幅出现、再消失重放动效"。键盘/直驱展开（事件阶
// 段，展开后无同拍采样）保持即时终态口径（见下方对照段）。
TEST_CASE("submenu_hover_expand_first_frame_starts_animation",
          "[widgets][menu][motion]") {
    MotionMenuApp app;
    app.shell.tick(0);
    app.context.open(
        app.shell, Offset{100.0F, 100.0F}, sampleItems(),
        [](const std::string& id) {
            return id == "sort"
                       ? MenuItems{MenuItem{.id = "name", .label = "名称"}}
                       : MenuItems{};
        });
    app.shell.tick(200);  // 顶级打开动效完成
    app.shell.rebuildIfDirty();
    const RenderNode* sortRow = app.row(0, 4);
    REQUIRE(sortRow != nullptr);

    app.shell.pointerMove(
        absoluteOffset(*app.shell.overlayRoot(), sortRow->key) +
        Offset{sortRow->size.width * 0.5F, sortRow->size.height * 0.5F});
    app.shell.tick(500);
    app.shell.rebuildIfDirty();
    REQUIRE(app.context.levelCount() == 2);

    const RenderNode* panel =
        findNodeByKey(*app.shell.overlayRoot(), "ctx:panel:1");
    REQUIRE(panel != nullptr);
    const float startX = panel->offset.x;
    CHECK(panel->transitionAlpha == 0.0F);  // 首绘即淡入起点，无终态闪帧

    // 同一 EaseOut 进度推进至终态：滑入位移收敛（placeRight → 终态左移）。
    app.shell.tick(500 + 120);
    app.shell.rebuildIfDirty();
    panel = findNodeByKey(*app.shell.overlayRoot(), "ctx:panel:1");
    REQUIRE(panel != nullptr);
    CHECK(panel->transitionAlpha == 1.0F);
    CHECK(panel->offset.x == Catch::Approx(startX - 4.0F).margin(0.01F));
    CHECK_FALSE(app.shell.animationsActive());

    // 对照：键盘展开不经本拍采样（事件阶段）——直驱重建保持即时终态。
    app.context.close(app.shell);
    app.shell.tick(700);
    app.context.open(
        app.shell, Offset{100.0F, 100.0F}, sampleItems(),
        [](const std::string& id) {
            return id == "sort"
                       ? MenuItems{MenuItem{.id = "name", .label = "名称"}}
                       : MenuItems{};
        });
    REQUIRE(app.context.handleKey(app.shell, Key::Down));  // 0 → 2（剪切）
    REQUIRE(app.context.handleKey(app.shell, Key::Down));  // 2 → 4（排序）
    REQUIRE(app.context.handleKey(app.shell, Key::Right));
    app.shell.rebuildIfDirty();
    panel = findNodeByKey(*app.shell.overlayRoot(), "ctx:panel:1");
    REQUIRE(panel != nullptr);
    CHECK(panel->transitionAlpha == 1.0F);
}

TEST_CASE("submenu_hover_expands_after_delay_and_pass_through_does_not",
          "[widgets][menu][motion]") {
    MotionMenuApp app;
    app.shell.tick(0);
    app.context.open(
        app.shell, Offset{100.0F, 100.0F}, sampleItems(),
        [](const std::string& id) {
            return id == "sort"
                       ? MenuItems{MenuItem{.id = "name", .label = "名称"},
                                   MenuItem{.id = "time", .label = "时间"}}
                       : MenuItems{};
        });
    const RenderNode* sortRow = app.row(0, 4);  // 排序方式（hasSubmenu）
    REQUIRE(sortRow != nullptr);

    // 默认口径（menuSubmenuHoverMs = 0）：悬停即展开（首拍触发）。
    app.shell.pointerMove(absoluteOffset(*app.shell.overlayRoot(), sortRow->key) +
                          Offset{sortRow->size.width * 0.5F,
                                 sortRow->size.height * 0.5F});
    app.shell.tick(500);
    app.shell.rebuildIfDirty();
    CHECK(app.row(1, 0) != nullptr);
    CHECK(app.context.levelCount() == 2);

    // 去抖口径（显式 300ms）：未到不展开、到点展开、掠过不展开。
    app.context.close(app.shell);
    app.shell.tick(600);
    app.shell.theme().motion.menuSubmenuHoverMs = 300;
    app.context.open(
        app.shell, Offset{100.0F, 100.0F}, sampleItems(),
        [](const std::string& id) {
            return id == "sort"
                       ? MenuItems{MenuItem{.id = "name", .label = "名称"}}
                       : MenuItems{};
        });
    sortRow = app.row(0, 4);
    REQUIRE(sortRow != nullptr);
    app.shell.pointerMove(absoluteOffset(*app.shell.overlayRoot(), sortRow->key) +
                          Offset{sortRow->size.width * 0.5F,
                                 sortRow->size.height * 0.5F});
    app.shell.tick(1000);
    CHECK(findNodeByKey(*app.shell.overlayRoot(), "ctx:m1:i0") == nullptr);
    // 打开/高亮 tween 已结束，但 300ms 悬停计时尚未到期：必须继续
    // 请求 tick，不能被 stepMotion 的活动态输出覆盖而退回空闲轮询。
    app.shell.tick(1000 + 150);
    app.shell.paintFrame();  // 消费主树焦点颜色过渡，排除无关动画源。
    app.shell.tick(1000 + 200);
    CHECK(app.shell.animationsActive());
    CHECK(app.context.levelCount() == 1);
    app.shell.tick(1000 + 300);
    app.shell.rebuildIfDirty();
    CHECK(app.row(1, 0) != nullptr);

    app.context.close(app.shell);
    app.shell.tick(1400);
    app.context.open(
        app.shell, Offset{100.0F, 100.0F}, sampleItems(),
        [](const std::string& id) {
            return id == "sort"
                       ? MenuItems{MenuItem{.id = "name", .label = "名称"}}
                       : MenuItems{};
        });
    sortRow = app.row(0, 4);
    REQUIRE(sortRow != nullptr);
    app.shell.pointerMove(absoluteOffset(*app.shell.overlayRoot(), sortRow->key) +
                          Offset{sortRow->size.width * 0.5F,
                                 sortRow->size.height * 0.5F});
    app.shell.tick(1500);
    const RenderNode* plainRow = app.row(0, 2);  // 剪切（无子菜单）
    REQUIRE(plainRow != nullptr);
    app.shell.pointerMove(absoluteOffset(*app.shell.overlayRoot(), plainRow->key) +
                          Offset{plainRow->size.width * 0.5F,
                                 plainRow->size.height * 0.5F});
    app.shell.tick(1500 + 300);
    CHECK(app.context.levelCount() == 1);
}

TEST_CASE("submenu_hover_sibling_collapses_cascade", "[widgets][menu]") {
    MotionMenuApp app;
    app.shell.tick(0);
    app.context.open(
        app.shell, Offset{100.0F, 100.0F}, sampleItems(),
        [](const std::string& id) {
            return id == "sort"
                       ? MenuItems{MenuItem{.id = "name", .label = "名称"}}
                       : MenuItems{};
        });
    // 键盘展开 sort（0 → 2 → 4 → Right）。
    REQUIRE(app.context.handleKey(app.shell, Key::Down));
    REQUIRE(app.context.handleKey(app.shell, Key::Down));
    REQUIRE(app.context.handleKey(app.shell, Key::Right));
    app.shell.rebuildIfDirty();
    REQUIRE(app.row(1, 0) != nullptr);

    // 悬停同级其他项（level 0 的剪切）：级联立即收起（桌面惯例）。
    const RenderNode* cutRow = app.row(0, 2);
    REQUIRE(cutRow != nullptr);
    app.shell.pointerMove(absoluteOffset(*app.shell.overlayRoot(), cutRow->key) +
                          Offset{cutRow->size.width * 0.5F,
                                 cutRow->size.height * 0.5F});
    CHECK(app.row(1, 0) == nullptr);
    CHECK(app.context.levelCount() == 1);

    // 悬停级联源行自身：不收起（指针在源行与子面板间移动的往返稳定）。
    REQUIRE(app.context.handleKey(app.shell, Key::Right));
    app.shell.rebuildIfDirty();
    REQUIRE(app.row(1, 0) != nullptr);
    const RenderNode* sortRow = app.row(0, 4);
    REQUIRE(sortRow != nullptr);
    app.shell.pointerMove(absoluteOffset(*app.shell.overlayRoot(), sortRow->key) +
                          Offset{sortRow->size.width * 0.5F,
                                 sortRow->size.height * 0.5F});
    CHECK(app.row(1, 0) != nullptr);
    CHECK(app.context.levelCount() == 2);
}

TEST_CASE("menu_direct_drive_without_tick_keeps_instant_final_state",
          "[widgets][menu][motion]") {
    MotionMenuApp app;
    // motionTransitions 开启但从不 tick：动效 pending 不起表，直驱输出
    // 为即时终态（面板 alpha 1、无位移）；动能矩形常驻静态承载高亮
    //（与行 selected 背景同色同矩形——像素等价、语义统一）。
    app.openSample();
    (void)app.shell.renderFrame();

    const RenderNode* panel = app.panel();
    REQUIRE(panel != nullptr);
    CHECK(panel->transitionAlpha == 1.0F);
    const RenderNode* row0 = app.row(0, 0);
    const RenderNode* marquee = app.marquee();
    REQUIRE(row0 != nullptr);
    REQUIRE(marquee != nullptr);
    CHECK(row0->selected == false);
    CHECK(marquee->offset.y == Catch::Approx(row0->offset.y).margin(0.01F));
    CHECK(marquee->offset.x == Catch::Approx(row0->offset.x).margin(0.01F));
    CHECK(marquee->commonStyle().background ==
          app.shell.theme().colors.selectionBackground);
}

TEST_CASE("menu_overlay_animate_sink_lifecycle", "[widgets][menu][motion]") {
    MotionMenuApp app;
    app.shell.tick(0);
    app.openSample();
    CHECK(app.shell.hasOverlay());
    app.shell.tick(10);
    CHECK(app.shell.animationsActive());

    // 关闭：overlay 与动画源一并解除，重开正常。
    app.context.close(app.shell);
    CHECK_FALSE(app.shell.hasOverlay());
    app.shell.tick(500);
    CHECK_FALSE(app.shell.animationsActive());

    app.openSample();
    app.shell.tick(600);
    app.shell.rebuildIfDirty();
    const RenderNode* panel = app.panel();
    REQUIRE(panel != nullptr);
    CHECK(panel->transitionAlpha == 0.0F);
    app.shell.tick(600 + 120);
    app.shell.rebuildIfDirty();
    CHECK(app.panel()->transitionAlpha == 1.0F);
}
