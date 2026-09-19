// Splitter 分栏控件（docs/lumen-splitter-design.md）测试：布局分配与
// min 钳制、窄窗比例压缩、框架拖动接管（跟手/顶住）、键盘步进与到边、
// 双击复位、resize keep-offset、分隔条焦点/语义与 Widget 体积预算。
//
// 命名遵循项目测试规范（行为命名，*_tests.cpp）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/splitter.h"
#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/platform/fake_host.h"
#include "lumen/render/renderer.h"
#include "lumen/widgets/splitter.h"

using namespace lumen;
using namespace lumen::core;
using lumen::widgets::SplitterController;
using lumen::layout::LayoutEngine;

namespace {

constexpr float kDefaultHit = core::splitterHitExtent(1);  // Comfortable 16px

class SplitterApp {
  public:
    app::AppShell shell{makeConfig()};
    SplitterController splitter{200.0F};
    int offsetChanges{0};

    static app::ShellConfig makeConfig() {
        app::ShellConfig config;
        config.build = [] { return Widget{}; };
        return config;
    }

    SplitterApp() {
        splitter.onOffsetChanged = [this](float) { ++offsetChanges; };
        build();
    }

    void build(bool horizontal = true) {
        Widget leading = makeText("Leading");
        Widget trailing = makeText("Trailing");
        shell.swapRoot(core::makeSplitter(&splitter, std::move(leading),
                                           std::move(trailing), horizontal,
                                           "main"));
        shell.rebuildIfDirty();
    }

    // 分隔条命中点（根相对中心）。先同步重建：setView 只置脏，测试直
    // 驱必须先拿到新几何再计算命中。
    Offset dividerCenter() {
        shell.rebuildIfDirty();
        const RenderNode* divider =
            findNodeByKey(shell.root(), "split:div:main");
        REQUIRE(divider != nullptr);
        const Offset origin = absoluteOffset(shell.root(), "split:div:main");
        return Offset{origin.x + divider->size.width * 0.5F,
                      origin.y + divider->size.height * 0.5F};
    }
};

}  // namespace

TEST_CASE("splitter_layout_allocates_two_panes_and_divider",
          "[widgets][splitter]") {
    SplitterController splitter{200.0F};
    const auto root = LayoutEngine::layout(
        core::makeSplitter(&splitter, makeText("L"), makeText("R"), true,
                           "s"),
        Constraints::tight(Size{400.0F, 300.0F}));
    REQUIRE(root.type == WidgetType::Splitter);
    REQUIRE(root.children.size() == 3);  // leading + divider + trailing
    CHECK(root.children[0].size.width == Catch::Approx(200.0F));
    CHECK(root.children[0].size.height == Catch::Approx(300.0F));
    // 分隔条：Comfortable 16px 命中宽、交叉轴满、key 与源齐备。
    const RenderNode& divider = root.children[1];
    CHECK(divider.type == WidgetType::Button);
    CHECK(divider.key == "split:div:s");
    CHECK(divider.size.width == Catch::Approx(kDefaultHit));
    CHECK(divider.size.height == Catch::Approx(300.0F));
    CHECK(divider.splitterSource == &splitter);
    CHECK(divider.collectionRow);  // Tab 可聚焦
    CHECK(divider.offset.x == Catch::Approx(195.0F));
    // 命中区透明覆盖固定 6px 轨道；trailing 只扣轨道宽。
    CHECK(root.children[2].offset.x ==
          Catch::Approx(200.0F + core::kSplitterTrackThickness));
    CHECK(root.children[2].size.width ==
          Catch::Approx(400.0F - 200.0F -
                        core::kSplitterTrackThickness));
    // 布局回填：钳制后位置与 extent。
    CHECK(splitter.seeded());
    CHECK(splitter.offset() == Catch::Approx(200.0F));
}

TEST_CASE("splitter_programmatic_offset_before_layout_is_preserved",
          "[widgets][splitter]") {
    SplitterController splitter{200.0F};
    splitter.setOffset(280.0F);
    const auto root = LayoutEngine::layout(
        core::makeSplitter(&splitter, makeText("L"), makeText("R"), true,
                           "s"),
        Constraints::tight(Size{400.0F, 100.0F}));
    CHECK(splitter.offset() == Catch::Approx(280.0F));
    CHECK(root.children[0].size.width == Catch::Approx(280.0F));
}

TEST_CASE("splitter_hit_extent_follows_control_density",
          "[widgets][splitter]") {
    const auto dividerWidth = [](style::ControlDensity density) {
        SplitterController splitter{100.0F};
        const style::Theme theme = style::Theme::dark(density);
        const style::InteractionStateSnapshot interaction;
        const accessibility::AccessibilitySettings settings;
        const auto root = LayoutEngine::layout(
            core::makeSplitter(&splitter, makeText("L"), makeText("R"),
                               true, "density"),
            Constraints::tight(Size{300.0F, 100.0F}),
            style::StyleContext{theme, interaction, settings});
        return root.children[1].size.width;
    };

    CHECK(dividerWidth(style::ControlDensity::Compact) ==
          Catch::Approx(12.0F));
    CHECK(dividerWidth(style::ControlDensity::Comfortable) ==
          Catch::Approx(16.0F));
    CHECK(dividerWidth(style::ControlDensity::Touch) ==
          Catch::Approx(24.0F));
}

TEST_CASE("splitter_layout_clamps_to_min_panes", "[widgets][splitter]") {
    SplitterController splitter{500.0F};
    splitter.setMinLeading(48.0F);
    splitter.setMinTrailing(48.0F);
    const auto root = LayoutEngine::layout(
        core::makeSplitter(&splitter, makeText("L"), makeText("R"), true,
                           "s"),
        Constraints::tight(Size{400.0F, 100.0F}));
    // mainMax = 400 - 6 = 394；上限 = 394 - 48 = 346。
    CHECK(splitter.offset() == Catch::Approx(346.0F));
    CHECK(root.children[2].size.width == Catch::Approx(48.0F));
}

TEST_CASE("splitter_narrow_window_squeezes_proportionally",
          "[widgets][splitter]") {
    SplitterController splitter{200.0F};
    splitter.setMinLeading(48.0F);
    splitter.setMinTrailing(48.0F);
    // 宽 60：mainMax = 54 < 96 → 两窗格按最小值比例对半。
    const auto root = LayoutEngine::layout(
        core::makeSplitter(&splitter, makeText("L"), makeText("R"), true,
                           "s"),
        Constraints::tight(Size{60.0F, 100.0F}));
    CHECK(root.children[0].size.width == Catch::Approx(27.0F));
    CHECK(root.children[2].size.width == Catch::Approx(27.0F));
}

TEST_CASE("splitter_drag_follows_pointer_and_clamps_at_max",
          "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    app.splitter.setMinLeading(48.0F);
    app.splitter.setMinTrailing(48.0F);
    const Offset center = app.dividerCenter();

    // 拖动 +100（200 → 300）。
    app.shell.pointerDown(center);
    app.shell.pointerMove(Offset{center.x + 100.0F, center.y});
    CHECK(app.splitter.offset() == Catch::Approx(300.0F));
    CHECK(app.offsetChanges > 0);
    app.shell.pointerUp(Offset{center.x + 100.0F, center.y});

    // 拖过头：顶住 max（400 - 6 - 48 = 346）。
    app.shell.pointerDown(app.dividerCenter());
    app.shell.pointerMove(Offset{center.x + 1000.0F, center.y});
    CHECK(app.splitter.offset() == Catch::Approx(346.0F));
    app.shell.pointerUp(Offset{center.x + 1000.0F, center.y});
}

TEST_CASE("splitter_drag_takes_precedence_over_scroll_viewport",
          "[widgets][splitter]") {
    // 分隔条命中即独占：即使祖先链含滚动视口，拖动仍是分栏（不滚视口）。
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    const Offset center = app.dividerCenter();
    const float before = app.splitter.offset();
    app.shell.pointerDown(center);
    app.shell.pointerMove(Offset{center.x + 40.0F, center.y});
    CHECK(app.splitter.offset() == Catch::Approx(before + 40.0F));
    // 布局随拖动重排（重建发生在下一帧入口：显式同步后核对几何）。
    app.shell.rebuildIfDirty();
    const RenderNode* divider =
        findNodeByKey(app.shell.root(), "split:div:main");
    REQUIRE(divider != nullptr);
    const float dividerCenter =
        absoluteOffset(app.shell.root(), "split:div:main").x +
        divider->size.width * 0.5F;
    CHECK(dividerCenter ==
          Catch::Approx(before + 40.0F + kSplitterTrackThickness * 0.5F));
    app.shell.pointerUp(Offset{center.x + 40.0F, center.y});
}

TEST_CASE("splitter_keyboard_steps_and_edges", "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    app.splitter.setMinLeading(48.0F);
    app.splitter.setMinTrailing(48.0F);
    // Tab 建立键盘焦点（松手即失焦：点击不再残留焦点）。
    app.shell.keyDown(Key::Tab);

    app.shell.keyDown(Key::Right);
    CHECK(app.splitter.offset() == Catch::Approx(216.0F));  // 200 + 16
    app.shell.keyDown(Key::Left);
    CHECK(app.splitter.offset() == Catch::Approx(200.0F));
    app.shell.keyDown(Key::End);
    CHECK(app.splitter.offset() == Catch::Approx(346.0F));
    app.shell.keyDown(Key::Home);
    CHECK(app.splitter.offset() == Catch::Approx(48.0F));
}

// Tab 聚焦分隔条：painter 的 3px accent 线由 focusWidth>0 驱动（painter
// splitter 分支），环关闭时聚焦态会退回 1px rest 线不可见——框架 chrome
// 与 makeDialog actions 同口径显式开环（visual-system §6.1）。
TEST_CASE("splitter_keyboard_focus_paints_active_line", "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    app.shell.keyDown(Key::Tab);
    REQUIRE(app.shell.focus().focusedKey() == "split:div:main");
    (void)app.shell.renderFrame();
    const auto* divider = findNodeByKey(app.shell.root(), "split:div:main");
    REQUIRE(divider != nullptr);
    CHECK(divider->commonStyle().focusWidth ==
          app.shell.theme().metrics.focusRingWidth);
    CHECK(divider->commonStyle().focusRing ==
          app.shell.theme().colors.focusRing);
}

// 松手即失焦（高亮跟鼠标走）：按压期间建焦（拖住时方向键可用），
// 释放/取消即清除；Tab/语义聚焦不经过按压路径，不受影响。
TEST_CASE("splitter_releases_focus_on_pointer_up", "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    const Offset center = app.dividerCenter();

    // 按压建立焦点，拖动中焦点仍在。
    app.shell.pointerDown(center);
    CHECK(app.shell.focus().focusedKey() == "split:div:main");
    app.shell.pointerMove(Offset{center.x + 40.0F, center.y});
    CHECK(app.shell.focus().focusedKey() == "split:div:main");

    // 松手即失焦（200 + 40 拖动生效，高亮不再残留）。
    app.shell.pointerUp(Offset{center.x + 40.0F, center.y});
    CHECK(app.splitter.offset() == Catch::Approx(240.0F));
    CHECK(app.shell.focus().focusedIdentity().empty());

    // Tab 仍可聚焦（键盘路径不受影响），方向键步进。
    app.shell.keyDown(Key::Tab);
    CHECK(app.shell.focus().focusedKey() == "split:div:main");
    app.shell.keyDown(Key::Right);
    CHECK(app.splitter.offset() == Catch::Approx(256.0F));

    // 取消路径同样清除本次按压建立的焦点。
    app.shell.pointerDown(app.dividerCenter());
    REQUIRE(app.shell.focus().focusedKey() == "split:div:main");
    app.shell.pointerCancel();
    CHECK(app.shell.focus().focusedIdentity().empty());
}

TEST_CASE("splitter_double_click_resets_to_initial", "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    const Offset center = app.dividerCenter();
    // 先拖离 initial。
    app.shell.pointerDown(center);
    app.shell.pointerMove(Offset{center.x + 90.0F, center.y});
    app.shell.pointerUp(Offset{center.x + 90.0F, center.y});
    REQUIRE(app.splitter.offset() == Catch::Approx(290.0F));

    // 双击（400ms 窗口内两次干净点击；tick 控制时间戳）。
    app.shell.tick(1000);
    app.shell.pointerDown(app.dividerCenter());
    app.shell.pointerUp(app.dividerCenter());
    app.shell.tick(1200);
    app.shell.pointerDown(app.dividerCenter());
    app.shell.pointerUp(app.dividerCenter());
    CHECK(app.splitter.offset() == Catch::Approx(200.0F));
}

TEST_CASE("splitter_resize_keeps_offset_clamped", "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{600.0F, 300.0F});
    const Offset center = app.dividerCenter();
    app.shell.pointerDown(center);
    app.shell.pointerMove(Offset{center.x + 100.0F, center.y});
    app.shell.pointerUp(Offset{center.x + 100.0F, center.y});
    REQUIRE(app.splitter.offset() == Catch::Approx(300.0F));

    // 变窄（keep-offset 钳制）：400 - 6 - 48 = 346 上限不受影响，300 保持。
    app.shell.setView(Size{400.0F, 300.0F});
    app.shell.rebuildIfDirty();  // 钳制发生在下一次布局的 noteLayout
    CHECK(app.splitter.offset() == Catch::Approx(300.0F));
    REQUIRE(findNodeByKey(app.shell.root(), "split:div:main") != nullptr);
    // 再窄到 200：上限 = 200 - 6 - 48 = 146 → 钳制。
    app.shell.setView(Size{200.0F, 300.0F});
    app.shell.rebuildIfDirty();
    CHECK(app.splitter.offset() == Catch::Approx(146.0F));
}

TEST_CASE("splitter_vertical_orientation_stacks_panes",
          "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    app.build(/*horizontal=*/false);
    const auto root = app.shell.root();
    REQUIRE(root.children.size() == 3);
    // 分隔条：交叉轴满宽、Comfortable 16px 高。
    CHECK(root.children[1].size.width == Catch::Approx(400.0F));
    CHECK(root.children[1].size.height == Catch::Approx(kDefaultHit));
    CHECK(root.children[1].offset.y == Catch::Approx(195.0F));
    // 垂直键盘：Tab 聚焦后 Up/Down 步进。
    app.shell.keyDown(Key::Tab);
    app.shell.keyDown(Key::Down);
    CHECK(app.splitter.offset() == Catch::Approx(216.0F));
}

TEST_CASE("splitter_divider_carries_splitter_semantics",
          "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    app.shell.rebuildIfDirty();
    const RenderNode* divider =
        findNodeByKey(app.shell.root(), "split:div:main");
    REQUIRE(divider != nullptr);
    CHECK(divider->semanticsRole == "splitter");
    accessibility::SemanticsRole role{};
    REQUIRE(accessibility::semanticsRoleFromName(divider->semanticsRole,
                                                 &role));
    CHECK(role == accessibility::SemanticsRole::Splitter);
    CHECK(divider->semanticsValue == "51%");

    accessibility::SemanticsBuildOptions options;
    options.focus = &app.shell.focus();
    auto tree = accessibility::buildSemanticsTree(app.shell.root(), options);
    const accessibility::SemanticsNode* semantic =
        tree.find(divider->identity);
    REQUIRE(semantic != nullptr);
    CHECK((semantic->actions & accessibility::kActionFocus) != 0);
    CHECK((semantic->actions & accessibility::kActionSetValue) != 0);
    CHECK((semantic->actions & accessibility::kActionActivate) == 0);

    accessibility::SemanticsActionContext context;
    context.root = &app.shell.root();
    context.handlers = &app.shell.handlers();
    context.focus = &app.shell.focus();
    context.controller = &app.shell.controller();
    CHECK(accessibility::performSemanticsAction(
              tree, context, divider->identity,
              accessibility::kActionSetValue, "75%") ==
          accessibility::SemanticsActionStatus::Handled);
    CHECK(app.splitter.offset() == Catch::Approx(295.5F));
    app.shell.rebuildIfDirty();
    divider = findNodeByKey(app.shell.root(), "split:div:main");
    REQUIRE(divider != nullptr);
    CHECK(divider->semanticsValue == "75%");
}

TEST_CASE("splitter_widget_size_budget", "[core][widgets][splitter]") {
    // M7 体积门槛（实测口径）：集合控件后 Release 基线 816B，Splitter
    // 源指针 +8B → 824B；P2 水平滚动轴标志 +1B 触发对齐 → 832B
    //（Debug 工具链调试迭代器开销分档同集合规则）。
#ifdef NDEBUG
    CHECK(sizeof(Widget) <= 832);
#else
    CHECK(sizeof(Widget) <= 936);
#endif
}

namespace {

Color pixelAt(const render::PixelBuffer& buffer, int x, int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.width) +
         static_cast<std::size_t>(x)) *
        4;
    return Color::fromRGBA(buffer.rgba[offset], buffer.rgba[offset + 1],
                           buffer.rgba[offset + 2], buffer.rgba[offset + 3]);
}

}  // namespace

// 分隔条轨道绘制（splitter-design §9.2）：rest 1px border.strong 居中
//（Ghost border token 派生；回归守护：token 默认透明曾使 rest 线不可
// 见）；hover 3px focusRing 满覆盖。AA 光栅下 rest 半覆盖像素取不等断言。
TEST_CASE("splitter_divider_paints_rest_line_and_hover_widens",
          "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    app.shell.rebuildIfDirty();
    (void)app.shell.renderFrame();

    const Offset origin = absoluteOffset(app.shell.root(), "split:div:main");
    const int midY = static_cast<int>(origin.y + 150.0F);
    const Color clear = pixelAt(app.shell.pixels(),
                                static_cast<int>(origin.x) + 2, midY);
    // rest：轨道空白处 = 清屏底，1px 线（x 5.5–6.5）半覆盖可见。
    const int centerX =
        static_cast<int>(origin.x + kDefaultHit * 0.5F);
    const Color rest = pixelAt(app.shell.pixels(), centerX, midY);
    CHECK(rest != clear);
    CHECK(rest.a == 255);

    // hover：3px focusRing（4.5–7.5，中心像素满覆盖 → 精确色值）。
    app.shell.pointerMove(
        Offset{origin.x + kDefaultHit * 0.5F, origin.y + 150.0F});
    (void)app.shell.renderFrame();
    const Color hover = pixelAt(app.shell.pixels(), centerX, midY);
    CHECK(hover == app.shell.theme().colors.focusRing);
}

// 悬停/拖动光标契约（splitter-design §7）：命中分隔条 → ResizeEW/NS；
// 拖动期间保持方向（指针可移出当前密度命中区）；离开 → Arrow。宿主适配层
//（runApp）映射 SystemCursor 落到 ApplicationHost::setCursor。
TEST_CASE("splitter_hover_and_drag_report_resize_cursor",
          "[widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{400.0F, 300.0F});
    CHECK(app.shell.pointerCursor() == PointerCursor::Arrow);

    app.shell.pointerMove(app.dividerCenter());
    CHECK(app.shell.pointerCursor() == PointerCursor::ResizeEW);

    // 窗格内 → 默认箭头。
    app.shell.pointerMove(Offset{20.0F, 150.0F});
    CHECK(app.shell.pointerCursor() == PointerCursor::Arrow);

    // 拖动分隔条：指针移出轨道仍保持 ResizeEW。
    const Offset center = app.dividerCenter();
    app.shell.pointerDown(center);
    app.shell.pointerMove(Offset{380.0F, 20.0F});
    CHECK(app.shell.pointerCursor() == PointerCursor::ResizeEW);
    app.shell.pointerUp(Offset{380.0F, 20.0F});
    CHECK(app.shell.pointerCursor() == PointerCursor::Arrow);

    // 垂直分栏 → ResizeNS。
    app.build(/*horizontal=*/false);
    app.shell.pointerMove(app.dividerCenter());
    CHECK(app.shell.pointerCursor() == PointerCursor::ResizeNS);
}

TEST_CASE("run_app_applies_splitter_cursor_to_host",
          "[app][widgets][splitter]") {
    SplitterApp app;
    app.shell.setView(Size{800.0F, 600.0F});
    const Offset divider = app.dividerCenter();
    lumen::platform::FakeApplicationHost host;
    REQUIRE(host.initialize());
    host.createWindow({});
    const auto id = host.windowIds().front();
    host.pushPointerMove(id, divider);
    host.pushPointerMove(id, Offset{20.0F, 20.0F});
    host.pushQuit();

    CHECK(app::runApp(app.shell, host, app::RunOptions{}) == 0);
    REQUIRE(host.cursorCalls.size() >= 2);
    CHECK(host.cursorCalls[host.cursorCalls.size() - 2].second ==
          lumen::platform::SystemCursor::ResizeEW);
    CHECK(host.cursorCalls.back().second ==
          lumen::platform::SystemCursor::Arrow);
}
