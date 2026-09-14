// M10（自用路线图）：动效与滚动体验测试。
// 转场驱动（transitionAlpha 写回/damage/完成回调）、整节点透明度绘制、
// 状态色过渡插值、reduceAnimation 零时长路径与静态场景零动画帧——
// 全部经注入时钟直驱 AppShell（确定性；plan §4 M10 接口约束）。

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
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
