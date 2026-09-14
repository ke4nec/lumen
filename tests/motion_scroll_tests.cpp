// M10（自用路线图）：动效与滚动体验测试。
// 转场驱动（transitionAlpha 写回/damage/完成回调）、整节点透明度绘制、
// 状态色过渡插值、reduceAnimation 零时长路径与静态场景零动画帧——
// 全部经注入时钟直驱 AppShell（确定性；plan §4 M10 接口约束）。

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/scroll.h"
#include "lumen/core/state.h"
#include "lumen/core/style.h"
#include "lumen/dsl/dsl.h"
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
    const auto duration =
        static_cast<std::uint64_t>(shell.theme().motion.navigatorTransitionMs);
    CHECK(duration != shell.theme().motion.dialogTransitionMs);

    shell.beginRouteTransition("overlay-card", /*entering=*/false);
    shell.tick(duration - 1);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) > 0.0F);
    shell.tick(duration);
    (void)shell.renderFrame();
    CHECK(overlayAlpha(shell) == 0.0F);
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
            container(column({core::withKey(
                                  button("Go", onClick("go")), "go-button")}),
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
