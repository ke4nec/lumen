// M10（自用路线图）：动效与滚动体验测试。
// 转场驱动（transitionAlpha 写回/damage/完成回调）、整节点透明度绘制、
// 状态色过渡插值、reduceAnimation 零时长路径与静态场景零动画帧——
// 全部经注入时钟直驱 AppShell（确定性；plan §4 M10 接口约束）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/accessibility/bridge.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/render_node.h"
#include "lumen/core/scroll.h"
#include "lumen/core/state.h"
#include "lumen/core/style.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/layout/layout.h"
#include "lumen/render/render_commands.h"
#include "lumen/style/theme.h"

using lumen::app::AppShell;
using lumen::app::ShellConfig;
using lumen::core::Color;
using lumen::core::CommonResolvedStyle;
using lumen::core::Offset;
using lumen::core::RenderNode;
using lumen::core::Size;
using lumen::core::Widget;
using lumen::render::CommandType;
using lumen::render::recordScene;
using lumen::style::Theme;

namespace {

// 基础页 + 可开关覆盖层（模拟 dialog/route 子树）。
struct OverlayApp {
    bool overlay{true};

    [[nodiscard]] ShellConfig config() {
        ShellConfig config;
        config.initialView = Size{200.0F, 150.0F};
        config.caretBlink = false;
        config.build = [this] {
            using namespace lumen::dsl;
            namespace core = lumen::core;
            Widget ui = core::makeStack({
                core::withKey(
                    container(text("base"), Color::fromRGBA(24, 24, 27)),
                    "base"),
                overlay
                    ? core::withKey(
                          container(text("overlay"),
                                    Color::fromRGBA(200, 30, 30)),
                          "overlay-card")
                    : core::Widget{},
            });
            ui.key = "root";
            return ui;
        };
        return config;
    }
};

float overlayAlpha(const AppShell& shell) {
    const RenderNode* node =
        lumen::core::findNodeByKey(shell.root(), "overlay-card");
    REQUIRE(node != nullptr);
    return node->transitionAlpha;
}

// 命令录制中的首个 DrawRect 颜色（按命令序查找 alpha 证据）。
std::optional<Color> firstRectColor(const RenderNode& root) {
    const auto list = recordScene(root);
    for (const auto& command : list.commands()) {
        if (command.type == CommandType::DrawRect) {
            return command.color;
        }
    }
    return std::nullopt;
}

RenderNode solidNode(Color background, float alpha) {
    RenderNode node;
    node.type = lumen::core::WidgetType::Container;
    node.size = Size{50.0F, 50.0F};
    CommonResolvedStyle common;
    common.background = background;
    node.style.component = common;
    node.transitionAlpha = alpha;
    return node;
}

}  // namespace

// --- 整节点透明度（painter 命令路径；三后端同源） ---

TEST_CASE("painter_scales_whole_node_and_subtree_alpha", "[motion]") {
    SECTION("node alpha scales its surface color") {
        RenderNode root = solidNode(Color::fromRGBA(10, 20, 30, 200), 0.5F);
        const auto color = firstRectColor(root);
        REQUIRE(color.has_value());
        CHECK(*color == Color::fromRGBA(10, 20, 30, 100));
    }
    SECTION("children inherit parent alpha multiplicatively") {
        RenderNode root = solidNode(Color::fromRGBA(24, 24, 27, 255), 1.0F);
        RenderNode child = solidNode(Color::fromRGBA(200, 30, 30, 200), 0.5F);
        RenderNode grandchild =
            solidNode(Color::fromRGBA(90, 60, 255, 200), 0.5F);
        child.children.push_back(grandchild);
        root.children.push_back(child);
        // 祖父 alpha 链：父 0.5 → 子有效 0.25；根 1 → 父 0.5。
        const auto list = recordScene(root);
        int seenParent = 0;
        int seenChild = 0;
        int seenGrandchild = 0;
        for (const auto& command : list.commands()) {
            if (command.type != CommandType::DrawRect) {
                continue;
            }
            if (command.color == Color::fromRGBA(200, 30, 30, 100)) {
                ++seenParent;
            } else if (command.color == Color::fromRGBA(90, 60, 255, 50)) {
                ++seenGrandchild;
            } else if (command.color == Color::fromRGBA(24, 24, 27, 255)) {
                ++seenChild;
            }
        }
        CHECK(seenParent == 1);
        CHECK(seenGrandchild == 1);
        CHECK(seenChild == 1);
    }
    SECTION("fully transparent subtree emits no commands") {
        RenderNode root = solidNode(Color::fromRGBA(24, 24, 27, 255), 0.0F);
        root.children.push_back(
            solidNode(Color::fromRGBA(255, 0, 0, 255), 1.0F));
        CHECK(recordScene(root).empty());
    }
}

// --- 转场驱动（tick 时钟 + renderFrame 写回） ---

TEST_CASE("dialog_transition_fades_out_then_completes", "[motion]") {
    OverlayApp app;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) == 1.0F);

    const auto duration =
        static_cast<std::uint64_t>(shell.theme().motion.dialogTransitionMs);
    bool completed = false;
    shell.beginDialogTransition("overlay-card", /*entering=*/false,
                                [&completed, &app](AppShell& shell) {
                                    completed = true;
                                    app.overlay = false;
                                    shell.markDirty();
                                });
    // 首拍已推进极小步长（EaseIn 起点）；仍视为全不透明。
    shell.tick(1);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) > 0.99F);
    CHECK(shell.animationsActive());

    shell.tick(1 + duration / 2);
    (void)shell.renderFrame();
    const float mid = overlayAlpha(shell);
    CHECK(mid < 1.0F);
    CHECK(mid > 0.0F);

    shell.tick(1 + duration);
    CHECK(completed);
    (void)shell.renderFrame();  // onComplete 已移除子树
    CHECK(lumen::core::findNodeByKey(shell.root(), "overlay-card") ==
          nullptr);
    // retire 在完成后的下一拍清除。
    shell.tick(1 + duration + 1);
    CHECK_FALSE(shell.hasActiveTransitions());
    CHECK_FALSE(shell.animationsActive());
}

TEST_CASE("dialog_transition_enter_resolves_lazy_identity", "[motion]") {
    OverlayApp app;
    app.overlay = false;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();

    // begin 早于含覆盖层的重建：identity 延迟到首次应用时解析。
    app.overlay = true;
    shell.markDirty();
    shell.beginDialogTransition("overlay-card", /*entering=*/true);
    shell.tick(1);
    (void)shell.renderFrame();
    const auto duration =
        static_cast<std::uint64_t>(shell.theme().motion.dialogTransitionMs);
    CHECK(overlayAlpha(shell) < 1.0F);

    shell.tick(1 + duration);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) == 1.0F);
}

TEST_CASE("route_transition_uses_navigator_duration", "[motion]") {
    OverlayApp app;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();
    // S4（§9.1）：Navigator Fade 200ms（旧 350 按规格调整；与 dialog
    // 时长同值，规格一致）。
    const auto duration =
        static_cast<std::uint64_t>(shell.theme().motion.navigatorTransitionMs);
    CHECK(duration == 200);

    shell.beginRouteTransition("overlay-card", /*entering=*/false);
    shell.tick(duration - 1);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) > 0.0F);
    shell.tick(duration);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) == 0.0F);
}

TEST_CASE("route_transition_keeps_scheduling_after_its_first_paint", "[motion]") {
    OverlayApp app;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();
    shell.beginRouteTransition("overlay-card", true);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) == 0.0F);
    // runApp queries this after renderFrame: onRebuilt can start a transition
    // after the loop's tick, without a hover blend or another input event.
    CHECK(shell.animationsActive());
}

TEST_CASE("transition_terminal_sample_survives_ticks_without_a_frame", "[motion]") {
    OverlayApp app;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();
    int callbacks = 0;
    shell.beginDialogTransition("overlay-card", true,
        [&](AppShell&) { ++callbacks; });
    (void)shell.renderFrame();
    REQUIRE(overlayAlpha(shell) == 0.0F);
    const auto duration = shell.theme().motion.dialogTransitionMs;
    // VSync throttling or a hidden window can defer rendering while the event
    // loop keeps ticking. Completing the timer must not discard its last frame.
    for (std::uint64_t now = duration; now < duration + 20; ++now) {
        shell.tick(now);
    }
    CHECK(callbacks == 1);
    CHECK(shell.hasActiveTransitions());
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) == 1.0F);
    shell.tick(duration + 20);
    CHECK_FALSE(shell.hasActiveTransitions());
    CHECK_FALSE(shell.animationsActive());
}

TEST_CASE("reduce_animation_keeps_completion_callback_after_immediate_paint",
          "[motion]") {
    OverlayApp app;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();
    int callbacks = 0;
    shell.beginDialogTransition("overlay-card", true,
        [&](AppShell&) { ++callbacks; });
    (void)shell.renderFrame();
    lumen::accessibility::AccessibilitySettings settings;
    settings.reduceAnimation = true;
    shell.setAccessibilitySettings(settings);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) == 1.0F);
    shell.tick(1);
    CHECK(callbacks == 1);
    (void)shell.renderFrame();
    shell.tick(2);
    CHECK(callbacks == 1);
    CHECK_FALSE(shell.hasActiveTransitions());
    CHECK_FALSE(shell.animationsActive());
}

TEST_CASE("reduce_animation_completes_transition_first_tick", "[motion]") {
    OverlayApp app;
    AppShell shell{app.config()};
    Theme theme = shell.theme();
    theme.motion.reduceAnimation();
    shell.setTheme(theme, /*forceFullRepaint=*/false);
    shell.tick(0);
    (void)shell.renderFrame();

    bool completed = false;
    shell.beginDialogTransition("overlay-card", /*entering=*/true,
                                [&completed](AppShell&) { completed = true; });
    shell.tick(1);
    (void)shell.renderFrame();
    // 零时长：首拍即终态（alpha 1），随后退休。
    CHECK(overlayAlpha(shell) == 1.0F);
    shell.tick(2);
    CHECK_FALSE(shell.hasActiveTransitions());
    CHECK_FALSE(shell.animationsActive());
    (void)completed;
}

TEST_CASE("reduce_animation_toggle_does_not_poison_future_transitions", "[motion]") {
    OverlayApp app;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();

    lumen::accessibility::AccessibilitySettings reduced;
    reduced.reduceAnimation = true;
    shell.setAccessibilitySettings(reduced);
    shell.beginDialogTransition("overlay-card", /*entering=*/true);
    shell.tick(1);
    (void)shell.renderFrame();

    lumen::accessibility::AccessibilitySettings normal;
    shell.setAccessibilitySettings(normal);
    shell.beginDialogTransition("overlay-card", /*entering=*/false);
    shell.tick(100);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) < 1.0F);
    CHECK(overlayAlpha(shell) > 0.0F);
}

// --- 状态色过渡（motionTransitions opt-in + tick 时钟） ---

namespace {

ShellConfig hoverConfig(bool* goFlag) {
    ShellConfig config;
    config.initialView = Size{200.0F, 150.0F};
    config.caretBlink = false;
    config.motionTransitions = true;
    config.build = [goFlag] {
        using namespace lumen::dsl;
        namespace core = lumen::core;
        Widget page = core::withKey(
            container(column({core::withKey(core::withFocusRing(
                                   button("Go", onClick("go")), true),
                               "go-button")}),
                       Color::fromRGBA(24, 24, 27)),
            "page");
        page.key = "root";
        (void)goFlag;
        return page;
    };
    return config;
}

Color buttonBackground(const AppShell& shell) {
    const RenderNode* node =
        lumen::core::findNodeByKey(shell.root(), "go-button");
    REQUIRE(node != nullptr);
    return node->commonStyle().background;
}

Offset buttonCenter(const AppShell& shell) {
    const RenderNode* node =
        lumen::core::findNodeByKey(shell.root(), "go-button");
    REQUIRE(node != nullptr);
    return lumen::core::absoluteOffset(shell.root(), "go-button") +
           Offset{node->size.width * 0.5F, node->size.height * 0.5F};
}

}  // namespace

TEST_CASE("state_blend_interpolates_hover_colors_across_ticks", "[motion]") {
    bool goFlag = false;
    AppShell shell{hoverConfig(&goFlag)};
    shell.handlers()["go"] = [] {};
    shell.tick(0);
    (void)shell.renderFrame();
    const Color base = buttonBackground(shell);

    const auto duration =
        static_cast<std::uint64_t>(shell.theme().motion.stateTransitionMs);
    REQUIRE(duration > 0);

    shell.pointerMove(buttonCenter(shell));
    shell.tick(50);
    (void)shell.renderFrame();  // 捕获旧样式 + 重建；t=0 → 起点色
    CHECK(buttonBackground(shell) == base);

    shell.tick(50 + duration / 2);
    (void)shell.renderFrame();
    const Color mid = buttonBackground(shell);

    shell.tick(50 + duration);
    (void)shell.renderFrame();
    const Color settled = buttonBackground(shell);
    CHECK(settled != base);  // hover 有视觉状态（token 链派生）

    // 中点色位于两端之间（逐通道；方向无关）。
    const auto between = [](std::uint8_t a, std::uint8_t b,
                            std::uint8_t value) {
        return (a <= b && value >= a && value <= b) ||
               (b <= a && value >= b && value <= a);
    };
    CHECK(between(base.r, settled.r, mid.r));
    CHECK(between(base.g, settled.g, mid.g));
    CHECK(between(base.b, settled.b, mid.b));

    shell.tick(50 + duration + 1);
    CHECK_FALSE(shell.animationsActive());
}

// --- 静态场景红线：无转场/无状态变化时零动画帧、哈希稳定 ---

TEST_CASE("static_scene_keeps_hash_and_has_no_animation", "[motion]") {
    OverlayApp app;
    ShellConfig config = app.config();
    AppShell shell{std::move(config)};
    shell.tick(0);
    const std::uint64_t first = shell.renderFrame();
    shell.tick(1000);
    const std::uint64_t second = shell.renderFrame();
    CHECK(first == second);
    CHECK(first != 0);
    CHECK_FALSE(shell.animationsActive());
    CHECK(overlayAlpha(shell) == 1.0F);
}

// --- M10：惯性滚动（ScrollController 物理 + 交互层拖动接线） ---

TEST_CASE("fling_decelerates_and_stops_deterministically", "[motion]") {
    lumen::core::ScrollController scroll;
    scroll.updateExtents(100.0F, 1000.0F);

    // 快速上滑（手指向上 = deltaY 负 = offset 增大）后释放。
    scroll.noteDragSample(-10.0F, 100);
    scroll.noteDragSample(-10.0F, 110);
    REQUIRE(scroll.endDrag(120));
    REQUIRE(scroll.isFlinging());

    // 固定步长推进：offset 单调增大、渐缓，最终停止且不越界。
    float previous = scroll.offset();
    bool stillFlinging = true;
    for (std::uint64_t t = 140; stillFlinging; t += 16) {
        stillFlinging = scroll.stepFling(t);
        CHECK(scroll.offset() >= previous);
        previous = scroll.offset();
        CHECK(scroll.offset() <= scroll.maxScrollOffset());
    }
    CHECK_FALSE(scroll.isFlinging());
    CHECK(scroll.offset() > 0.0F);

    // 同输入序列结果完全一致（确定性物理）。
    lumen::core::ScrollController replay;
    replay.updateExtents(100.0F, 1000.0F);
    replay.noteDragSample(-10.0F, 100);
    replay.noteDragSample(-10.0F, 110);
    (void)replay.endDrag(120);
    for (std::uint64_t t = 140; replay.isFlinging(); t += 16) {
        (void)replay.stepFling(t);
    }
    CHECK(replay.offset() == scroll.offset());

    // 慢速释放不起 fling（低于起滑阈值）。
    lumen::core::ScrollController slow;
    slow.updateExtents(100.0F, 1000.0F);
    slow.noteDragSample(-1.0F, 100);
    slow.noteDragSample(-1.0F, 120);
    CHECK_FALSE(slow.endDrag(140));
    CHECK_FALSE(slow.isFlinging());

    // 新输入（滚轮）立即接管惯性。
    lumen::core::ScrollController grab;
    grab.updateExtents(100.0F, 1000.0F);
    grab.noteDragSample(-10.0F, 100);
    (void)grab.endDrag(110);
    REQUIRE(grab.isFlinging());
    (void)grab.stepFling(126);
    (void)grab.applyWheel(5.0F);
    CHECK_FALSE(grab.isFlinging());
}

// --- 水平滚动轴（ScrollController 轴化；lumen-optimization-plan P2） ---

TEST_CASE("horizontal_axis_controller_interprets_inputs_along_x",
          "[motion][scroll]") {
    lumen::core::ScrollController scroll(lumen::core::ScrollAxis::Horizontal);
    CHECK(scroll.axis() == lumen::core::ScrollAxis::Horizontal);
    scroll.updateExtents(200.0F, 1000.0F);
    CHECK(scroll.maxScrollOffset() == 800.0F);

    // 滚轮分量：正 = 内容向右滚（offset 增大）。
    REQUIRE(scroll.applyWheel(120.0F));
    CHECK(scroll.offset() == 120.0F);

    // 键盘：水平取 Left/Right 组；Page/Home/End 两轴共用。
    REQUIRE(scroll.applyKey(lumen::core::Key::Right, 200.0F));
    CHECK(scroll.offset() == 300.0F);
    REQUIRE(scroll.applyKey(lumen::core::Key::Left, 200.0F));
    CHECK(scroll.offset() == 120.0F);
    REQUIRE(scroll.applyKey(lumen::core::Key::PageDown, 200.0F));
    CHECK(scroll.offset() == 300.0F);
    // 纵向方向键不属于本轴（交由其他视口/焦点消费）。
    CHECK_FALSE(scroll.applyKey(lumen::core::Key::Down, 200.0F));
    CHECK(scroll.offset() == 300.0F);

    // 拖动：指针沿 +x 拖动 → 内容向起点滚回。
    REQUIRE(scroll.applyDrag(50.0F));
    CHECK(scroll.offset() == 250.0F);

    // 语义滚动分量：符号约定同滚轮。
    REQUIRE(scroll.semanticScroll(-90.0F));
    CHECK(scroll.offset() == 340.0F);

    REQUIRE(scroll.applyKey(lumen::core::Key::End, 200.0F));
    CHECK(scroll.offset() == 800.0F);
    REQUIRE(scroll.applyKey(lumen::core::Key::Home, 200.0F));
    CHECK(scroll.offset() == 0.0F);
    // 到边后同向输入不再变化。
    CHECK_FALSE(scroll.applyDrag(50.0F));
}

TEST_CASE("cancelled_scroll_drag_drops_velocity_before_the_next_gesture", "[motion][scroll][review]") {
    lumen::core::ScrollController scroll;
    scroll.updateExtents(200, 2000);
    scroll.noteDragSample(-30, 100);
    scroll.noteDragSample(-30, 110);
    scroll.cancelDrag();
    scroll.noteDragSample(12, 120);
    CHECK_FALSE(scroll.endDrag(120));
    scroll.noteDragSample(-30, 200);
    scroll.noteDragSample(-30, 210);
    REQUIRE(scroll.endDrag(210));
    scroll.cancelDrag();
    CHECK_FALSE(scroll.isFlinging());
    CHECK_FALSE(scroll.endDrag(220));
}

TEST_CASE("horizontal_axis_fling_uses_same_physics", "[motion][scroll]") {
    lumen::core::ScrollController scroll(lumen::core::ScrollAxis::Horizontal);
    scroll.updateExtents(200.0F, 2000.0F);

    // 指针沿 -x 快速拖动（向左甩）→ 内容获得向右滚的惯性。
    scroll.noteDragSample(-30.0F, 100);
    scroll.noteDragSample(-30.0F, 110);
    REQUIRE(scroll.endDrag(120));
    REQUIRE(scroll.isFlinging());

    float previous = scroll.offset();
    bool stillFlinging = true;
    for (std::uint64_t t = 140; stillFlinging; t += 16) {
        stillFlinging = scroll.stepFling(t);
        CHECK(scroll.offset() >= previous);
        previous = scroll.offset();
        CHECK(scroll.offset() <= scroll.maxScrollOffset());
    }
    CHECK_FALSE(scroll.isFlinging());
    CHECK(scroll.offset() > 0.0F);
}

TEST_CASE("horizontal_scroll_view_mirrors_constraints_and_applies_x_offset",
          "[motion][scroll]") {
    namespace core = lumen::core;
    // 宽行（1000x80）放进 300x200 水平视口。
    const auto page = [](core::ScrollAxis axis, float offset) {
        core::Widget content = core::makeContainer(core::makeText("wide"));
        content.width = 1000.0F;
        content.height = 80.0F;
        core::Widget view = core::makeScrollView(std::move(content), "hscroll",
                                                 300.0F, 200.0F);
        view.scrollAxis = axis;
        view.scrollOffset = offset;
        return view;
    };
    const auto layout = [&page](core::ScrollAxis axis, float offset) {
        // 宽松约束（min=0）：视口自身 300x200 可小于外层可用 400x300。
        return lumen::layout::LayoutEngine::layout(
            page(axis, offset),
            core::Constraints{0.0F, 400.0F, 0.0F, 300.0F});
    };

    // 水平：主轴（宽）不受限 → 内容保持 1000；extent = 1000 - 300。
    const auto root = layout(core::ScrollAxis::Horizontal, 0.0F);
    REQUIRE(root.children.size() == 1);
    CHECK(root.size.width == 300.0F);
    CHECK(root.size.height == 200.0F);
    CHECK(root.scrollAxis == core::ScrollAxis::Horizontal);
    CHECK(root.clipContent);
    CHECK(root.children.front().size.width == 1000.0F);
    CHECK(root.scrollExtent == 700.0F);
    // offset 应用到 X；Y 不动。
    const auto scrolled = layout(core::ScrollAxis::Horizontal, 250.0F);
    CHECK(scrolled.children.front().offset.x == -250.0F);
    CHECK(scrolled.children.front().offset.y == 0.0F);
    // 夹取到 extent。
    const auto clamped = layout(core::ScrollAxis::Horizontal, 900.0F);
    CHECK(clamped.children.front().offset.x == -700.0F);
    CHECK(clamped.scrollOffset == 700.0F);

    // 交叉轴受限：更高的子内容被钳到视口高（水平视口的镜像约束）。
    core::Widget tall = core::makeContainer(core::makeText("tall"));
    tall.width = 1000.0F;
    tall.height = 500.0F;
    core::Widget view = core::makeScrollView(std::move(tall), "tall", 300.0F,
                                             200.0F);
    view.scrollAxis = core::ScrollAxis::Horizontal;
    const auto tallRoot = lumen::layout::LayoutEngine::layout(
        std::move(view),
        core::Constraints{0.0F, 400.0F, 0.0F, 300.0F});
    CHECK(tallRoot.children.front().size.height == 200.0F);

    // 纵向默认路径零变化：同内容按纵向约束（宽钳到视口、无纵向可滚）。
    const auto vertical = layout(core::ScrollAxis::Vertical, 0.0F);
    CHECK(vertical.scrollAxis == core::ScrollAxis::Vertical);
    CHECK(vertical.children.front().size.width == 300.0F);
    CHECK(vertical.scrollExtent == 0.0F);
}

namespace {

// 可滚动页面：列表视口 + 应用侧 ScrollController（含拖动/惯性接线）。
namespace core = lumen::core;

struct ScrollApp {
    lumen::core::ScrollController scroll{};

    [[nodiscard]] ShellConfig config() {
        ShellConfig config;
        config.initialView = Size{200.0F, 300.0F};
        config.caretBlink = false;
        config.build = [this] {
            using namespace lumen::dsl;
            namespace core = lumen::core;
            std::vector<Widget> rows;
            for (int i = 0; i < 40; ++i) {
                rows.push_back(core::withKey(text("row " + std::to_string(i)),
                                             "row-" + std::to_string(i)));
            }
            Widget ui = core::withKey(
                scroll_view(core::makeColumn(std::move(rows))),
                "scroll-area");
            ui = container(std::move(ui), Color::fromRGBA(24, 24, 27));
            ui.key = "root";
            return ui;
        };
        config.onWheel =
            [this](const RenderNode&, const RenderNode* hit, core::Offset,
                   core::Offset delta) {
            if (hit == nullptr ||
                !lumen::core::isScrollableWidget(hit->type)) {
                return false;
            }
            scroll.updateExtents(hit->size.height,
                                 hit->size.height + hit->scrollExtent);
            return scroll.applyWheel(delta.y);
        };
        config.onScrollDrag =
            [this](const RenderNode*, const RenderNode* viewport,
                   core::Offset, core::Offset delta,
                   core::ScrollDragPhase phase, std::uint64_t nowMs) {
            return dragScroll(viewport, delta.y, phase, nowMs);
        };
        config.onAnimate = [this](AppShell& shell, std::uint64_t nowMs) {
            if (!scroll.isFlinging()) {
                return false;
            }
            const bool active = scroll.stepFling(nowMs);
            shell.markDirty();
            return active;
        };
        return config;
    }

    bool dragScroll(const RenderNode* viewport, float deltaY,
                    core::ScrollDragPhase phase, std::uint64_t nowMs) {
        if (viewport == nullptr) {
            if (phase == core::ScrollDragPhase::Cancel) {
                scroll.stopFling();
            }
            return false;
        }
        scroll.updateExtents(viewport->size.height,
                             viewport->size.height +
                                 viewport->scrollExtent);
        switch (phase) {
            case core::ScrollDragPhase::Begin:
                break;
            case core::ScrollDragPhase::Update:
                scroll.noteDragSample(deltaY, nowMs);
                if (scroll.applyDrag(deltaY)) {
                    return true;
                }
                return false;
            case core::ScrollDragPhase::End:
                return scroll.endDrag(nowMs);
            case core::ScrollDragPhase::Cancel:
                scroll.stopFling();
                return false;
        }
        return false;
    }
};

}  // namespace

TEST_CASE("drag_over_viewport_scrolls_and_flings_via_shell", "[motion]") {
    ScrollApp app;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();

    const RenderNode* viewport =
        lumen::core::findNodeByKey(shell.root(), "scroll-area");
    REQUIRE(viewport != nullptr);
    CHECK(viewport->scrollExtent > 0.0F);  // 布局事实：内容溢出视口
    const core::Offset start = lumen::core::absoluteOffset(shell.root(),
                                                           "scroll-area") +
                               Offset{viewport->size.width * 0.5F, 30.0F};

    // 快速上滑（手指向上移动 → 内容向上滚 → offset 增大）。
    shell.pointerDown(start);
    shell.tick(100);
    shell.pointerMove(start + Offset{0.0F, -10.0F});
    shell.tick(110);
    shell.pointerMove(start + Offset{0.0F, -20.0F});
    shell.tick(120);
    const float atRelease = app.scroll.offset();
    CHECK(atRelease > 0.0F);  // 拖动已直接滚动
    shell.pointerUp(start + Offset{0.0F, -20.0F});
    CHECK(app.scroll.isFlinging());  // 释放起惯性

    // onAnimate 随 tick 推进惯性并置脏。
    float advanced = atRelease;
    for (std::uint64_t t = 140; app.scroll.isFlinging(); t += 16) {
        shell.tick(t);
        (void)shell.renderFrame();
        advanced = app.scroll.offset();
    }
    CHECK(advanced > atRelease);
    CHECK_FALSE((shell.animationsActive() || app.scroll.isFlinging()));
}

TEST_CASE("drag_on_scrollbar_thumb_tracks_finger", "[motion]") {
    // 拇指拖拽跟手：起点落在拇指上时，拇指位移 1:1 跟随手指，
    // 内容按 scrollExtent/可滚轨道长换算（与内容拖拽反号）。
    lumen::core::ScrollController scroll;
    AppShell* liveShell = nullptr;
    ShellConfig config;
    config.initialView = Size{200.0F, 300.0F};
    config.caretBlink = false;
    config.build = [&scroll] {
        using namespace lumen::dsl;
        namespace core = lumen::core;
        std::vector<Widget> rows;
        for (int i = 0; i < 40; ++i) {
            rows.push_back(core::withKey(text("row " + std::to_string(i)),
                                         "row-" + std::to_string(i)));
        }
        Widget list = scroll_view(core::makeColumn(std::move(rows)));
        list = core::withScrollOffset(std::move(list), scroll.offset());
        Widget ui = core::withKey(core::withScrollbar(std::move(list)),
                                  "scroll-area");
        ui = container(std::move(ui), Color::fromRGBA(24, 24, 27));
        ui.key = "root";
        return ui;
    };
    config.onScrollDrag =
        [&scroll, &liveShell](const RenderNode*, const RenderNode* viewport,
                  core::Offset, core::Offset delta,
                  core::ScrollDragPhase phase, std::uint64_t nowMs) {
            if (viewport == nullptr) {
                if (phase == core::ScrollDragPhase::Cancel) {
                    scroll.stopFling();
                }
                return false;
            }
            scroll.updateExtents(viewport->size.height,
                                 viewport->size.height +
                                     viewport->scrollExtent);
            switch (phase) {
                case core::ScrollDragPhase::Begin:
                    break;
                case core::ScrollDragPhase::Update:
                    scroll.noteDragSample(delta.y, nowMs);
                    if (scroll.applyDrag(delta.y)) {
                        if (liveShell != nullptr) {
                            liveShell->markDirty();
                        }
                        return true;
                    }
                    return false;
                case core::ScrollDragPhase::End:
                    return scroll.endDrag(nowMs);
                case core::ScrollDragPhase::Cancel:
                    scroll.stopFling();
                    return false;
            }
            return false;
        };
    AppShell shell{std::move(config)};
    liveShell = &shell;
    shell.tick(0);
    (void)shell.renderFrame();

    const RenderNode* viewport =
        lumen::core::findNodeByKey(shell.root(), "scroll-area");
    REQUIRE(viewport != nullptr);
    REQUIRE(viewport->scrollbarThickness > 0.0F);
    REQUIRE(viewport->scrollExtent > 0.0F);
    scroll.updateExtents(viewport->size.height,
                         viewport->size.height + viewport->scrollExtent);
    // 滚动设计 §5：轨道 inset 独立于 hover 时变化的滑块厚度。
    const float inset = viewport->scrollbarInset;
    const float trackLength = viewport->size.height - 2.0F * inset;
    REQUIRE(trackLength > 0.0F);
    const float fraction = viewport->size.height /
                           (viewport->size.height + viewport->scrollExtent);
    const float thumbHeight =
        std::min(std::max(trackLength * fraction,
                           viewport->scrollbarMinLength),
                 trackLength);
    REQUIRE(thumbHeight > 0.0F);
    REQUIRE(trackLength - thumbHeight > 0.0F);
    const float ratio =
        viewport->scrollExtent / (trackLength - thumbHeight);
    REQUIRE(ratio > 1.0F);  // 长列表：不换算就会明显不跟手
    const core::Offset areaOrigin =
        lumen::core::absoluteOffset(shell.root(), "scroll-area");
    const core::Offset thumbPress =
        areaOrigin +
        Offset{viewport->size.width - viewport->scrollbarThickness * 0.5F,
               inset + thumbHeight * 0.5F};
    const float thumbTop0 = inset;

    // 手指下移 40px：拇指应下移 40px（跟手），offset 增大 40*ratio
    //（与内容拖拽反号——内容拖拽下移是 offset 减小）。
    constexpr float kDragDy = 40.0F;
    REQUIRE(kDragDy * ratio < scroll.maxScrollOffset());
    shell.pointerDown(thumbPress);
    shell.tick(100);
    shell.pointerMove(thumbPress + Offset{0.0F, 10.0F});
    shell.tick(110);
    (void)shell.renderFrame();
    shell.pointerMove(thumbPress + Offset{0.0F, kDragDy});
    shell.tick(120);
    (void)shell.renderFrame();
    CHECK(scroll.offset() == Catch::Approx(kDragDy * ratio).margin(1.0F));

    // 拇指实时位置验证跟手（布局树已带新 offset 重建）。
    const RenderNode* moved =
        lumen::core::findNodeByKey(shell.root(), "scroll-area");
    REQUIRE(moved != nullptr);
    const float progress = moved->scrollOffset / moved->scrollExtent;
    const float thumbTop =
        inset + progress * (trackLength - thumbHeight);
    CHECK(thumbTop == Catch::Approx(thumbTop0 + kDragDy).margin(1.0F));
    shell.pointerUp(thumbPress + Offset{0.0F, kDragDy});
}

TEST_CASE("drag_on_text_field_keeps_selection_path", "[motion]") {
    // 视口内的文本字段：拖动走选区扩展，不路由滚动。
    lumen::core::ScrollController scroll;
    ShellConfig config;
    config.initialView = Size{200.0F, 300.0F};
    config.caretBlink = false;
    config.build = [] {
        using namespace lumen::dsl;
        namespace core = lumen::core;
        Widget ui = core::withKey(
            scroll_view(core::makeColumn({
                core::withKey(text_field(bind("name"), "Name"),
                              "name-field"),
                text("padding row"),
            })),
            "scroll-area");
        ui = container(std::move(ui), Color::fromRGBA(24, 24, 27));
        ui.key = "root";
        return ui;
    };
    bool dragReachedScroll = false;
    config.onScrollDrag =
        [&dragReachedScroll](const RenderNode*, const RenderNode*,
                             core::Offset, core::Offset,
                             core::ScrollDragPhase, std::uint64_t) {
        dragReachedScroll = true;
        return false;
    };
    AppShell shell{std::move(config)};
    shell.state().set("name", "hello world");
    shell.tick(0);
    (void)shell.renderFrame();

    const core::Offset field = [&shell] {
        const RenderNode* node =
            lumen::core::findNodeByKey(shell.root(), "name-field");
        REQUIRE(node != nullptr);
        return lumen::core::absoluteOffset(shell.root(), "name-field") +
               Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    }();
    shell.pointerDown(field);
    shell.tick(100);
    shell.pointerMove(field + Offset{40.0F, 0.0F});
    shell.tick(110);
    shell.pointerUp(field + Offset{40.0F, 0.0F});

    CHECK_FALSE(dragReachedScroll);
    CHECK(shell.controller().hasSelection());  // 拖动扩展了选区
}

TEST_CASE("wheel_reports_sink_consumption", "[motion]") {
    ScrollApp app;
    AppShell shell{app.config()};
    shell.tick(0);
    (void)shell.renderFrame();

    const RenderNode* viewport =
        lumen::core::findNodeByKey(shell.root(), "scroll-area");
    REQUIRE(viewport != nullptr);
    const core::Offset inside =
        lumen::core::absoluteOffset(shell.root(), "scroll-area") +
        Offset{viewport->size.width * 0.5F, viewport->size.height * 0.5F};
    CHECK(shell.wheel(inside, Offset{0.0F, 120.0F}));
    CHECK(app.scroll.offset() > 0.0F);
    // 视口外（无命中链上的滚动视口）：未消费。
    CHECK_FALSE(shell.wheel(Offset{-50.0F, -50.0F}, Offset{0.0F, 120.0F}));
}

// 回归（M10 review）：视口内的 Slider 拖动属于滑块（M6 拖动释放按位置
// 设值），不得被滚动路由劫持。
TEST_CASE("slider_drag_inside_scroll_view_still_sets_value", "[motion]") {
    bool dragRouted = false;
    ShellConfig config;
    config.initialView = Size{200.0F, 300.0F};
    config.caretBlink = false;
    config.build = [] {
        using namespace lumen::dsl;
        namespace core = lumen::core;
        std::vector<Widget> rows;
        rows.push_back(
            core::withKey(core::makeSlider("volume"), "volume-slider"));
        for (int i = 0; i < 30; ++i) {
            rows.push_back(core::withKey(text("row " + std::to_string(i)),
                                         "row-" + std::to_string(i)));
        }
        Widget ui = core::withKey(
            scroll_view(core::makeColumn(std::move(rows))), "scroll-area");
        ui = container(std::move(ui), Color::fromRGBA(24, 24, 27));
        ui.key = "root";
        return ui;
    };
    config.onScrollDrag =
        [&dragRouted](const RenderNode*, const RenderNode*, core::Offset,
                      core::Offset, core::ScrollDragPhase, std::uint64_t) {
        dragRouted = true;
        return false;
    };
    AppShell shell{std::move(config)};
    shell.state().set("volume", "0");
    shell.tick(0);
    (void)shell.renderFrame();

    const core::Offset slider = [&shell] {
        const RenderNode* node =
            lumen::core::findNodeByKey(shell.root(), "volume-slider");
        REQUIRE(node != nullptr);
        return lumen::core::absoluteOffset(shell.root(), "volume-slider") +
               Offset{node->size.width * 0.25F, node->size.height * 0.5F};
    }();
    shell.pointerDown(slider);
    shell.tick(100);
    shell.pointerMove(slider + Offset{60.0F, 0.0F});
    shell.tick(110);
    shell.pointerUp(slider + Offset{60.0F, 0.0F});

    CHECK_FALSE(dragRouted);
    CHECK(shell.state().get("volume") != "0");
}

TEST_CASE("slider_drag_updates_value_before_release", "[motion]") {
    // 旋钮跟手：按下拖动过程中每拍按位置设值（释放前值已更新）；
    // 中间隔一次重建，验证目标按 identity 跨重建重定位。
    ShellConfig config;
    config.initialView = Size{200.0F, 300.0F};
    config.caretBlink = false;
    config.build = [] {
        using namespace lumen::dsl;
        namespace core = lumen::core;
        Widget ui = core::withKey(core::makeSlider("volume"),
                                  "volume-slider");
        ui = container(std::move(ui), Color::fromRGBA(24, 24, 27));
        ui.key = "root";
        return ui;
    };
    AppShell shell{std::move(config)};
    shell.state().set("volume", "0");
    shell.tick(0);
    (void)shell.renderFrame();

    const auto sliderX = [&shell](float fraction) {
        const RenderNode* node =
            lumen::core::findNodeByKey(shell.root(), "volume-slider");
        REQUIRE(node != nullptr);
        return lumen::core::absoluteOffset(shell.root(), "volume-slider") +
               Offset{node->size.width * fraction,
                      node->size.height * 0.5F};
    };
    const core::Offset press = sliderX(0.25F);
    shell.pointerDown(press);
    shell.tick(100);
    shell.pointerMove(press + Offset{30.0F, 0.0F});
    shell.tick(110);
    (void)shell.renderFrame();  // store 写回已置脏：重建一次
    CHECK(shell.state().get("volume") != "0");  // 还没释放，值已动
    shell.pointerMove(press + Offset{60.0F, 0.0F});
    shell.tick(120);
    (void)shell.renderFrame();

    // 期望值按与实现相同的轨道区间换算（只验证接线，不验证公式本身）。
    const RenderNode* node =
        lumen::core::findNodeByKey(shell.root(), "volume-slider");
    REQUIRE(node != nullptr);
    const auto* style =
        std::get_if<core::SliderResolvedStyle>(&node->style.component);
    REQUIRE(style != nullptr);
    const core::Offset origin =
        lumen::core::absoluteOffset(shell.root(), "volume-slider");
    const float usable = node->size.width - 2.0F * style->trackInset;
    REQUIRE(usable > 0.0F);
    const float ratio =
        std::clamp((press.x + 60.0F - origin.x - style->trackInset) / usable,
                   0.0F, 1.0F);
    const std::string expect =
        std::to_string(static_cast<int>(std::lround(ratio * 100.0F)));
    CHECK(shell.state().get("volume") == expect);

    shell.pointerUp(press + Offset{60.0F, 0.0F});
    CHECK(shell.state().get("volume") == expect);  // 释放落终值（幂等）
}

// --- M11：Tooltip hover 延迟驱动 ---

namespace {

ShellConfig tooltipConfig() {
    ShellConfig config;
    config.initialView = Size{200.0F, 150.0F};
    config.caretBlink = false;
    config.build = [] {
        using namespace lumen::dsl;
        namespace core = lumen::core;
        Widget ui = core::makeStack({
            core::withKey(container(column({core::withKey(
                                  button("Go", onClick("go")), "go-button")}),
                                    Color::fromRGBA(24, 24, 27)),
                          "page"),
            core::withStackPosition(
                core::withKey(core::makeTooltip("tip text", "the-tip"),
                              "the-tip"),
                core::Offset{10.0F, 10.0F}),
        });
        ui.key = "root";
        return ui;
    };
    return config;
}

float tipAlpha(const AppShell& shell) {
    const RenderNode* node =
        lumen::core::findNodeByKey(shell.root(), "the-tip");
    REQUIRE(node != nullptr);
    return node->transitionAlpha;
}

}  // namespace

TEST_CASE("tooltip_reveals_after_hover_delay_and_hides_on_leave",
          "[motion]") {
    AppShell shell{tooltipConfig()};
    shell.handlers()["go"] = [] {};
    shell.registerTooltip("go-button", "the-tip");
    shell.tick(0);
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) == 0.0F);  // 注册后默认隐藏（M6 常驻显示移除）

    const core::Offset center = [] {
        // build 布局与 shell 一致：按钮位于列首。
        return core::Offset{32.0F, 20.0F};
    }();
    const RenderNode* button =
        lumen::core::findNodeByKey(shell.root(), "go-button");
    REQUIRE(button != nullptr);
    const core::Offset buttonCenter =
        lumen::core::absoluteOffset(shell.root(), "go-button") +
        Offset{button->size.width * 0.5F, button->size.height * 0.5F};
    (void)center;

    const auto delay = shell.theme().motion.tooltipDelayMs;
    const auto fade = shell.theme().motion.tooltipFadeMs;
    REQUIRE(delay > 0);
    REQUIRE(fade > 0);

    shell.pointerMove(buttonCenter);
    shell.tick(100);  // hover 进入：Armed
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) == 0.0F);  // 未满延迟不可见
    // 等待期不占用连续动画帧（M11 review：定时唤醒替代空转帧）。
    CHECK_FALSE(shell.animationsActive());
    CHECK(shell.animationWakeMs().has_value());
    CHECK(*shell.animationWakeMs() == 100 + delay);

    shell.tick(100 + delay);  // 满延迟：转场开始（首拍为起点 alpha=0）
    shell.tick(100 + delay + fade / 2);  // 淡入中
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) > 0.0F);
    CHECK(tipAlpha(shell) < 1.0F);

    shell.tick(100 + delay + fade);  // 淡入完成
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) == 1.0F);

    shell.pointerMove(Offset{-50.0F, -50.0F});  // 离开锚点
    shell.tick(100 + delay + fade + 1);  // 淡出转场开始（首拍为起点 1）
    shell.tick(100 + delay + fade + 1 + fade / 2);
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) < 1.0F);
    CHECK(tipAlpha(shell) > 0.0F);

    shell.tick(100 + delay + 2 * fade + 10);  // 淡出完成 + 退休
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) == 0.0F);

    // 回归：重建后的新树不得把 tooltip 重置为可见（Hidden 态强制 0）。
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) == 0.0F);
    // 退休条目在完成后的下一拍清除；随后完全静止。
    shell.tick(100 + delay + 2 * fade + 40);
    CHECK_FALSE(shell.animationsActive());
}

TEST_CASE("tooltip_reduce_animation_shows_immediately", "[motion]") {
    AppShell shell{tooltipConfig()};
    shell.handlers()["go"] = [] {};
    shell.registerTooltip("go-button", "the-tip");
    Theme theme = shell.theme();
    theme.motion.reduceAnimation();
    shell.setTheme(theme, /*forceFullRepaint=*/false);
    shell.tick(0);
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) == 0.0F);

    const RenderNode* button =
        lumen::core::findNodeByKey(shell.root(), "go-button");
    REQUIRE(button != nullptr);
    shell.pointerMove(lumen::core::absoluteOffset(shell.root(), "go-button") +
                      Offset{button->size.width * 0.5F,
                             button->size.height * 0.5F});
    shell.tick(1);   // 零延迟同拍起转场
    shell.tick(2);   // 零时长下一拍采样即终值
    (void)shell.renderFrame();
    CHECK(tipAlpha(shell) == 1.0F);
}

// --- M11：框架级 overlay 合成层 ---

namespace {

ShellConfig overlayBaseConfig() {
    ShellConfig config;
    config.initialView = Size{200.0F, 150.0F};
    config.caretBlink = false;
    config.build = [] {
        using namespace lumen::dsl;
        namespace core = lumen::core;
        Widget ui = core::withKey(
            container(column({core::withKey(
                                 button("Under", onClick("under")), "under-button")}),
                      Color::fromRGBA(24, 24, 27)),
            "page");
        ui.key = "root";
        return ui;
    };
    return config;
}

// 全窗 barrier + 锚定菜单（与 makeDialog 同构的最小模态层）。
core::Widget menuOverlay(core::Offset menuOrigin) {
    namespace core = lumen::core;
    core::Widget barrier = core::makeContainer(
        core::makeText(""), std::nullopt, std::nullopt, core::EdgeInsets{},
        core::EdgeInsets{}, core::Color{0, 0, 0, 132});
    barrier.width = 200.0F;
    barrier.height = 150.0F;
    barrier.onClick = "overlay-dismiss";
    barrier.semanticsRole = "dialog";
    barrier.key = "overlay-barrier";
    core::Widget menu = core::withKey(
        core::withStackPosition(
            core::withKey(core::withOnClick(core::makeButton("Option"),
                                             "menu-select"),
                          "menu-option"),
            menuOrigin),
        "menu-option");
    core::Widget overlay = core::makeStack({std::move(barrier),
                                            std::move(menu)});
    overlay.key = "overlay-root";
    return overlay;
}

}  // namespace

TEST_CASE("overlay_composites_paint_hits_and_semantics", "[motion]") {
    AppShell shell{overlayBaseConfig()};
    std::string clicked;
    shell.handlers()["under"] = [&clicked] { clicked = "under"; };
    shell.handlers()["menu-select"] = [&clicked] { clicked = "menu"; };
    shell.handlers()["overlay-dismiss"] = [&clicked] { clicked = "dismiss"; };
    shell.tick(0);
    (void)shell.renderFrame();
    const std::string underIdentity =
        lumen::core::findNodeByKey(shell.root(), "under-button")->identity;

    // overlay 打开：主树 identity 不变（焦点/damage/语义稳定的前提）。
    const core::Offset menuOrigin{20.0F, 40.0F};
    shell.setOverlay(menuOverlay(menuOrigin));
    (void)shell.renderFrame();
    REQUIRE(shell.hasOverlay());
    REQUIRE(shell.overlayRoot() != nullptr);
    CHECK(lumen::core::findNodeByKey(shell.root(), "under-button")
              ->identity == underIdentity);

    // 语义：overlay 节点进入合成语义树——menu-option 的 Activate
    // action 与键盘/指针同路径分发（RecordingBridge 可观测增量）。
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    shell.setAccessibilityBridge(&bridge);
    (void)shell.renderFrame();
    REQUIRE(shell.performAccessibilityAction(
        lumen::core::findNodeByKey(*shell.overlayRoot(), "menu-option")
            ->identity,
        lumen::accessibility::kActionActivate) ==
        lumen::accessibility::SemanticsActionStatus::Handled);
    CHECK(clicked == "menu");

    // 模态语义边界（M11 review）：overlay 活跃期主树节点不可激活/
    // 滚动（事件树统一，NotHandled）。
    CHECK(shell.performAccessibilityAction(
              underIdentity, lumen::accessibility::kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::NotHandled);

    // 命中优先：点击菜单选项位置——同位置主树的 under-button 不得触发。
    clicked.clear();
    shell.pointerDown(menuOrigin + Offset{30.0F, 12.0F});
    shell.pointerUp(menuOrigin + Offset{30.0F, 12.0F});
    CHECK(clicked == "menu");

    // barrier：点击菜单外区域触发 dismiss（模态遮挡）。
    clicked.clear();
    shell.pointerDown(Offset{180.0F, 140.0F});
    shell.pointerUp(Offset{180.0F, 140.0F});
    CHECK(clicked == "dismiss");

    // 关闭：主树 identity 仍不变；事件回到主树。
    shell.clearOverlay();
    (void)shell.renderFrame();
    CHECK_FALSE(shell.hasOverlay());
    CHECK(lumen::core::findNodeByKey(shell.root(), "under-button")
              ->identity == underIdentity);
    clicked.clear();
    const RenderNode* under =
        lumen::core::findNodeByKey(shell.root(), "under-button");
    const core::Offset underCenter =
        lumen::core::absoluteOffset(shell.root(), "under-button") +
        Offset{under->size.width * 0.5F, under->size.height * 0.5F};
    shell.pointerDown(underCenter);
    shell.pointerUp(underCenter);
    CHECK(clicked == "under");
}

TEST_CASE("overlay_open_close_forces_full_repaint_and_partial_while_open",
          "[motion]") {
    AppShell shell{overlayBaseConfig()};
    shell.tick(0);
    (void)shell.renderFrame();
    const std::uint32_t partialBefore = shell.partialRepaintCount();

    shell.setOverlay(menuOverlay(core::Offset{20.0F, 40.0F}));
    (void)shell.renderFrame();
    // 打开 = 全量（partial 计数不变）。
    CHECK(shell.partialRepaintCount() == partialBefore);

    // 打开期间替换 overlay（高亮移动等）：走 overlay 子树 diff 的局部
    // damage（计数增加）。
    shell.setOverlay(menuOverlay(core::Offset{20.0F, 80.0F}));
    (void)shell.renderFrame();
    CHECK(shell.partialRepaintCount() > partialBefore);

    shell.clearOverlay();
    (void)shell.renderFrame();
    CHECK_FALSE(shell.hasOverlay());
}

// --- S5（gui-control-visual-system-task §9.1）：焦点/主题切换的即时性 ---

TEST_CASE("focus_ring_appears_full_width_on_first_blend_frame", "[motion]") {
    // 键盘焦点出现 0ms：首帧即完整焦点标识（环宽度不参与状态色插值，
    // 度量取终态）。
    bool goFlag = false;
    AppShell shell{hoverConfig(&goFlag)};
    shell.handlers()["go"] = [] {};
    shell.tick(0);
    (void)shell.renderFrame();

    const RenderNode* node =
        lumen::core::findNodeByKey(shell.root(), "go-button");
    REQUIRE(node != nullptr);
    CHECK(node->commonStyle().focusWidth == 0.0F);

    shell.controller().focusNode(*node);
    // 中途时刻（非 0 非 T）渲染：环宽必须是完整终值，不是渐变中间值。
    shell.tick(shell.theme().motion.stateTransitionMs / 2);
    (void)shell.renderFrame();
    const RenderNode* focused =
        lumen::core::findNodeByKey(shell.root(), "go-button");
    REQUIRE(focused != nullptr);
    CHECK(focused->commonStyle().focusWidth ==
          shell.theme().metrics.focusRingWidth);
}

TEST_CASE("theme_switch_converges_immediately_with_transitions_on",
          "[motion]") {
    // §9.1：主题切换 0ms——即使 motionTransitions 开启也不产生中间色
    //（状态过渡只由交互快照变化触发，主题/density 切换直接收敛）。
    bool goFlag = false;
    AppShell shell{hoverConfig(&goFlag)};
    shell.handlers()["go"] = [] {};
    shell.tick(0);
    (void)shell.renderFrame();
    const Color darkBase = buttonBackground(shell);
    CHECK(darkBase == shell.theme().button.filled.background);

    lumen::style::Theme light = lumen::style::Theme::light();
    shell.setTheme(light);
    (void)shell.renderFrame();
    // 首帧即浅色主题终值，无插值中间色。
    CHECK(buttonBackground(shell) == light.button.filled.background);
    CHECK(buttonBackground(shell) != darkBase);
}
