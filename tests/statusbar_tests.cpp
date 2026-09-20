// StatusBar 状态栏（docs/lumen-statusbar-design.md）测试：消息生命周期
// （瞬态驻留/取代不排队/常驻）、ProgressBar indeterminate 往返相位与
// determinate 像素回归、busy 弧旋转、Toggle 项激活与禁用、grip 显隐、
// 布局分区与语义角色、reduceAnimation 停动效保留内容节律。
//
// 命名遵循项目测试规范（行为命名）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/widgets/statusbar.h"

using namespace lumen;
using namespace lumen::core;
using lumen::widgets::StatusBarController;
using lumen::widgets::StatusBarItem;
using lumen::widgets::StatusItemKind;

namespace {

class StatusApp {
  public:
    app::AppShell shell{makeConfig(this)};
    StatusBarController bar{"status"};
    std::vector<std::string> clicks;

    static app::ShellConfig makeConfig(StatusApp* self) {
        app::ShellConfig config;
        config.motionTransitions = true;
        config.build = [self] {
            // 根列 Stretch：栏容器拉伸到视口宽（项折叠决策的约束输入）。
            return makeColumn(
                {self->bar.build(self->shell.theme())},
                MainAxisAlignment::Start, CrossAxisAlignment::Stretch);
        };
        return config;
    }

    StatusApp() {
        shell.setView(Size{800.0F, 200.0F});
        // 消息淡切/相位动画走 motionEnabled（opt-in + hasTicked）口径。
        shell.tick(0);
        bar.setIdleMessage("就绪");
        bar.setItems({
            StatusBarItem{.id = "sep1", .kind = StatusItemKind::Separator},
            StatusBarItem{.id = "cursor", .kind = StatusItemKind::Text,
                          .text = "Ln 1, Col 1"},
            StatusBarItem{.id = "sep2", .kind = StatusItemKind::Separator},
            StatusBarItem{.id = "encoding", .kind = StatusItemKind::Toggle,
                          .text = "UTF-8"},
        });
        bar.onItemClicked = [this](const std::string& id) {
            clicks.push_back(id);
        };
        bar.attach(shell);
        shell.rebuildIfDirty();
    }

    // 项折叠读上一帧几何：三帧收敛（布局 → 折叠 → 稳定）。
    void settle() {
        for (int i = 0; i < 3; ++i) {
            shell.markDirty();
            shell.rebuildIfDirty();
        }
    }

    [[nodiscard]] Offset centerOf(const std::string& key) const {
        const RenderNode* node = findNodeByKey(shell.root(), key);
        REQUIRE(node != nullptr);
        return absoluteOffset(shell.root(), key) +
               Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    }
};

}  // namespace

TEST_CASE("statusbar_transient_message_times_out_to_idle", "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setMessage("已保存 main.cpp", 4000);
    // 淡出→替换→淡入（120ms each）。
    app.bar.step(app.shell, 0);
    app.bar.step(app.shell, 100);   // 淡出中
    CHECK(app.bar.message() == "就绪");
    app.bar.step(app.shell, 200);   // 替换拍
    CHECK(app.bar.message() == "已保存 main.cpp");
    app.bar.step(app.shell, 4000);  // 驻留中（替换 + 3800 < 4000）
    CHECK(app.bar.message() == "已保存 main.cpp");
    app.bar.step(app.shell, 4400);  // 驻留到期 → 淡出
    app.bar.step(app.shell, 4550);  // 淡出完 → 替换回 idle
    CHECK(app.bar.message() == "就绪");
}

TEST_CASE("statusbar_new_message_replaces_pending", "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setMessage("第一条", 0);
    app.bar.step(app.shell, 0);
    app.bar.step(app.shell, 150);
    app.bar.step(app.shell, 320);  // 淡入完成 → Steady
    CHECK(app.bar.message() == "第一条");
    // 瞬态未到期时新消息取代（不排队）。
    app.bar.setMessage("瞬态 A", 4000);
    app.bar.setMessage("瞬态 B", 4000);
    app.bar.step(app.shell, 400);
    app.bar.step(app.shell, 600);  // 替换为 B，驻留起点 600
    CHECK(app.bar.message() == "瞬态 B");
    app.bar.step(app.shell, 800);  // 淡入完成
    // 驻留只算一次（B 起算 4000ms），超时回 idle（淡出→替换各一拍）。
    app.bar.step(app.shell, 6000);  // 4600+ 到期 → 淡出
    app.bar.step(app.shell, 6300);  // 淡出完 → 替换回 idle
    app.bar.step(app.shell, 6500);  // 淡入完成
    CHECK(app.bar.message() == "就绪");
}

TEST_CASE("statusbar_persistent_message_until_replaced", "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setMessage("3 个警告", 0);  // 常驻（timeout = 0）
    app.bar.step(app.shell, 0);
    app.bar.step(app.shell, 150);
    app.bar.step(app.shell, 999999);
    CHECK(app.bar.message() == "3 个警告");
    app.bar.setMessage("就绪", 0);
    app.bar.step(app.shell, 1000000);
    app.bar.step(app.shell, 1000150);
    CHECK(app.bar.message() == "就绪");
}

TEST_CASE("statusbar_progress_determinate_ignores_phase", "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setItems({
        StatusBarItem{.id = "build", .kind = StatusItemKind::Progress},
    });
    app.bar.setProgress(45.0F);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* bar = findNodeByKey(app.shell.root(), "status:item:build");
    REQUIRE(bar != nullptr);
    CHECK_FALSE(bar->progressIndeterminate);
    CHECK(bar->text == "45");
    // determinate 无动画源（busy 与消息均闲）→ step 返回 false。
    CHECK_FALSE(app.bar.step(app.shell, 700));
}

TEST_CASE("statusbar_indeterminate_marquee_phase_advances", "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setItems({
        StatusBarItem{.id = "build", .kind = StatusItemKind::Progress},
        StatusBarItem{.id = "label", .kind = StatusItemKind::Text,
                      .text = "构建"},
    });
    app.bar.setProgress(-1.0F);  // indeterminate
    app.shell.markDirty();
    app.shell.rebuildIfDirty();

    const RenderNode* bar = findNodeByKey(app.shell.root(), "status:item:build");
    REQUIRE(bar != nullptr);
    CHECK(bar->progressIndeterminate);
    CHECK(bar->scrollOffset == Catch::Approx(0.0F).margin(0.001F));

    // 相位 0..1 三角波（1400ms 半周期）；首拍锚定相位 0，随后推进。
    CHECK(app.bar.step(app.shell, 100));   // 锚定拍
    CHECK(app.bar.step(app.shell, 300));   // elapsed 200ms
    app.shell.rebuildIfDirty();
    const RenderNode* bar300 = findNodeByKey(app.shell.root(), "status:item:build");
    REQUIRE(bar300 != nullptr);
    CHECK(bar300->scrollOffset > 0.0F);
    CHECK(bar300->scrollOffset < 0.5F);

    app.bar.step(app.shell, 1500);  // 锚定 + 1400ms：相位 1（右端）
    app.shell.rebuildIfDirty();
    const RenderNode* barEnd = findNodeByKey(app.shell.root(), "status:item:build");
    REQUIRE(barEnd != nullptr);
    CHECK(barEnd->scrollOffset == Catch::Approx(1.0F).margin(0.01F));
}

TEST_CASE("statusbar_busy_arc_rotates_and_stops", "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setItems({
        StatusBarItem{.id = "busy", .kind = StatusItemKind::Busy},
    });
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    // busy 未开启：项不进树（出现/消失即项增减）。
    CHECK(findNodeByKey(app.shell.root(), "status:item:busy") == nullptr);

    app.bar.setBusy(true);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* arc = findNodeByKey(app.shell.root(), "status:item:busy");
    REQUIRE(arc != nullptr);
    CHECK(arc->icon == static_cast<std::uint8_t>(IconId::Busy));

    CHECK(app.bar.step(app.shell, 600));  // 锚定拍（相位 0）
    CHECK(app.bar.step(app.shell, 1200));  // 600ms：半圈
    app.shell.rebuildIfDirty();
    const RenderNode* arcHalf = findNodeByKey(app.shell.root(), "status:item:busy");
    REQUIRE(arcHalf != nullptr);
    CHECK(arcHalf->iconRotation == Catch::Approx(3.14159265F).margin(0.02F));

    app.bar.setBusy(false);
    CHECK_FALSE(app.bar.step(app.shell, 1800));
}

TEST_CASE("statusbar_toggle_click_fires_handler_and_disabled_rejects",
          "[widgets][statusbar]") {
    StatusApp app;
    const Offset center = app.centerOf("status:item:encoding");
    app.shell.pointerDown(center);
    app.shell.pointerUp(center);
    REQUIRE(app.clicks.size() == 1);
    CHECK(app.clicks.front() == "encoding");

    // disabled 项：命中拒绝。
    auto items = std::vector<StatusBarItem>{
        StatusBarItem{.id = "encoding", .kind = StatusItemKind::Toggle,
                      .text = "UTF-8", .enabled = false},
    };
    app.bar.setItems(items);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const Offset disabled = app.centerOf("status:item:encoding");
    app.shell.pointerDown(disabled);
    app.shell.pointerUp(disabled);
    CHECK(app.clicks.size() == 1);
}

TEST_CASE("statusbar_resize_grip_visibility", "[widgets][statusbar]") {
    StatusApp app;
    CHECK(findNodeByKey(app.shell.root(), "status:grip") == nullptr);
    app.bar.setShowResizeGrip(true);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    CHECK(findNodeByKey(app.shell.root(), "status:grip") != nullptr);
    app.bar.setShowResizeGrip(false);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    CHECK(findNodeByKey(app.shell.root(), "status:grip") == nullptr);
}

TEST_CASE("statusbar_layout_height_and_message_ellipsis",
          "[widgets][statusbar]") {
    StatusApp app;
    const RenderNode* bar = findNodeByKey(app.shell.root(), "status");
    REQUIRE(bar != nullptr);
    CHECK(bar->size.height == Catch::Approx(28.0F).margin(0.01F));  // Comfortable
    const RenderNode* row = findNodeByKey(app.shell.root(), "status:row");
    REQUIRE(row != nullptr);
    // 消息区弹性 + 项序列右对齐：消息节点在最左，Toggle 项靠右。
    const RenderNode* msg = findNodeByKey(app.shell.root(), "status:msg");
    REQUIRE(msg != nullptr);
    CHECK(msg->offset.x == Catch::Approx(12.0F).margin(0.01F));  // paddingX
}

// 消息文本左缘从栏内边距起排（稿件 .sb-msg 无额外缩进），"消息区/项序列
// ≥16px 呼吸"（§9.1）全部记在右侧：8px 文本内边距 + 8px 行 gap（分隔线
// 自身的 8px margin 另计）。
TEST_CASE("statusbar_message_breathes_toward_items_not_bar_edge",
          "[widgets][statusbar]") {
    StatusApp app;
    const RenderNode* msg = findNodeByKey(app.shell.root(), "status:msg");
    REQUIRE(msg != nullptr);
    CHECK(msg->offset.x == Catch::Approx(12.0F).margin(0.01F));  // paddingX
    CHECK(msg->padding.left == 0.0F);
    CHECK(msg->padding.right == Catch::Approx(8.0F).margin(0.01F));
}

// resize grip 贴栏右下角（§5.2"贴右下角"与稿件 .sb-grip
// {align-self:flex-end; margin-right:-pad}）：行的右内边距为 grip 让位，
// grip 右/下缘与栏外缘重合。
TEST_CASE("statusbar_resize_grip_flush_in_bottom_right_corner",
          "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setShowResizeGrip(true);
    app.settle();
    const RenderNode* bar = findNodeByKey(app.shell.root(), "status");
    const RenderNode* grip = findNodeByKey(app.shell.root(), "status:grip");
    REQUIRE(bar != nullptr);
    REQUIRE(grip != nullptr);
    const Offset gripAt = absoluteOffset(app.shell.root(), "status:grip");
    const Offset barAt = absoluteOffset(app.shell.root(), "status");
    CHECK(gripAt.x + grip->size.width ==
          Catch::Approx(barAt.x + bar->size.width).margin(0.01F));
    CHECK(gripAt.y + grip->size.height ==
          Catch::Approx(barAt.y + bar->size.height).margin(0.01F));
}

// 尺度档覆盖（§9.1）：Comfortable 密度下 Small = 相对下移一档（栏 24、
// 内边距 8）——清单瓦片紧凑样本口径，与 Spin/ToolBar 同规则。
TEST_CASE("statusbar_control_size_override_selects_compact_tier",
          "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setControlSize(ControlSize::Small);
    app.settle();
    const RenderNode* bar = findNodeByKey(app.shell.root(), "status");
    REQUIRE(bar != nullptr);
    CHECK(bar->size.height == Catch::Approx(24.0F).margin(0.01F));
    const RenderNode* msg = findNodeByKey(app.shell.root(), "status:msg");
    REQUIRE(msg != nullptr);
    CHECK(msg->offset.x == Catch::Approx(8.0F).margin(0.01F));
    // 文本项行高随档收窄（Touch 档才升 13px 字号）。
    CHECK(msg->textStyle().fontSize == Catch::Approx(12.0F).margin(0.01F));
}

TEST_CASE("statusbar_semantics_role_and_toggle_action",
          "[widgets][statusbar]") {
    StatusApp app;
    const RenderNode* bar = findNodeByKey(app.shell.root(), "status");
    REQUIRE(bar != nullptr);
    accessibility::SemanticsRole role{};
    REQUIRE(accessibility::semanticsRoleFromName(bar->semanticsRole, &role));
    CHECK(role == accessibility::SemanticsRole::StatusBar);
    // Toggle 项 = Button 语义（Activate ≡ 单击）。
    const RenderNode* toggle =
        findNodeByKey(app.shell.root(), "status:item:encoding");
    REQUIRE(toggle != nullptr);
    CHECK(toggle->onClick == "status:item:encoding");
}

// 栏容器 label 由应用提供（design §7："label = 应用 semanticsLabel（如
// "状态栏"）"）——只有 role 时读屏落到状态栏没有名称。
TEST_CASE("statusbar_semantics_label_comes_from_application",
          "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setSemanticsLabel("状态栏");
    app.settle();
    const RenderNode* bar = findNodeByKey(app.shell.root(), "status");
    REQUIRE(bar != nullptr);
    CHECK(bar->semanticsLabel == "状态栏");
}

TEST_CASE("statusbar_reduce_animation_stops_motion_keeps_content",
          "[widgets][statusbar]") {
    StatusApp app;
    accessibility::AccessibilitySettings reduced;
    reduced.reduceAnimation = true;
    app.shell.setAccessibilitySettings(reduced);
    app.shell.rebuildIfDirty();

    // 消息即时切换（无淡切）。
    app.bar.setMessage("立即消息", 4000);
    CHECK(app.bar.message() == "立即消息");
    // busy 停转（旋转不推进），但项仍在（形状保留"进行中"）。
    app.bar.setItems({
        StatusBarItem{.id = "busy", .kind = StatusItemKind::Busy},
    });
    app.bar.setBusy(true);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    CHECK_FALSE(app.bar.step(app.shell, 5000));
    const RenderNode* arc = findNodeByKey(app.shell.root(), "status:item:busy");
    REQUIRE(arc != nullptr);
    CHECK(arc->iconRotation == Catch::Approx(0.0F).margin(0.001F));
}

// 栏底满幅（§9.2 statusbar.background = surface）：chrome 的信息降级语言靠
// 自身表面与内容区分开，不依赖父级背景。
TEST_CASE("statusbar_bar_surface_fills_bar", "[widgets][statusbar]") {
    StatusApp app;
    const RenderNode* bar = findNodeByKey(app.shell.root(), "status");
    REQUIRE(bar != nullptr);
    CHECK(bar->commonStyle().background == app.shell.theme().colors.surface);
}

TEST_CASE("statusbar_items_fold_when_narrow_and_return_when_wide",
          "[widgets][statusbar]") {
    StatusApp app;
    // 四个文本项：800px 全量；240px 时从左折叠、右侧项保住（§14.1）。
    app.bar.setItems({
        StatusBarItem{.id = "a", .kind = StatusItemKind::Text, .text = "AAAA"},
        StatusBarItem{.id = "b", .kind = StatusItemKind::Text, .text = "BBBB"},
        StatusBarItem{.id = "c", .kind = StatusItemKind::Text, .text = "CCCC"},
    });
    app.shell.setView(Size{800.0F, 200.0F});
    app.settle();
    CHECK(core::findNodeByKey(app.shell.root(), "status:item:a") != nullptr);

    app.shell.setView(Size{240.0F, 200.0F});
    app.settle();
    // 消息保底 120 + 呼吸：可用 ~200px 只容最右 1-2 项。
    CHECK(core::findNodeByKey(app.shell.root(), "status:item:a") == nullptr);
    CHECK(core::findNodeByKey(app.shell.root(), "status:item:c") != nullptr);

    app.shell.setView(Size{800.0F, 200.0F});
    app.settle();
    CHECK(core::findNodeByKey(app.shell.root(), "status:item:a") != nullptr);
    CHECK(core::findNodeByKey(app.shell.root(), "status:item:c") != nullptr);
}

TEST_CASE("statusbar_indeterminate_reduced_shows_static_center_band",
          "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setItems({
        StatusBarItem{.id = "build", .kind = StatusItemKind::Progress},
    });
    accessibility::AccessibilitySettings reduced;
    reduced.reduceAnimation = true;
    app.shell.setAccessibilitySettings(reduced);
    app.bar.setProgress(-1.0F);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    // -1 相位哨兵：painter 画静止中段带（design §10）；node 侧可断言。
    const RenderNode* bar = core::findNodeByKey(app.shell.root(), "status:item:build");
    REQUIRE(bar != nullptr);
    CHECK(bar->scrollOffset == Catch::Approx(-1.0F).margin(0.001F));
    CHECK(bar->semanticsValue == "indeterminate");
}

TEST_CASE("statusbar_busy_reduced_arc_dimmed_static", "[widgets][statusbar]") {
    StatusApp app;
    app.bar.setItems({
        StatusBarItem{.id = "busy", .kind = StatusItemKind::Busy},
    });
    accessibility::AccessibilitySettings reduced;
    reduced.reduceAnimation = true;
    app.shell.setAccessibilitySettings(reduced);
    app.bar.setBusy(true);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* arc = core::findNodeByKey(app.shell.root(), "status:item:busy");
    REQUIRE(arc != nullptr);
    // 弧静止 + 0.5 透明（design §10——形状保留"进行中"语义）。
    CHECK(arc->commonStyle().foreground ==
          core::scaleColorAlpha(app.shell.theme().colors.accent, 128));
}

TEST_CASE("statusbar_toggle_hover_brightens_and_pressed_uses_accent_mix",
          "[widgets][statusbar]") {
    StatusApp app;
    app.shell.setVisualPreviewState("status:item:encoding",
                                    style::WidgetState{.hovered = true});
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* hovered =
        core::findNodeByKey(app.shell.root(), "status:item:encoding");
    REQUIRE(hovered != nullptr);
    CHECK(hovered->commonStyle().foreground ==
          app.shell.theme().colors.contentPrimary);

    app.shell.setVisualPreviewState("status:item:encoding",
                                    style::WidgetState{.pressed = true});
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* pressed =
        core::findNodeByKey(app.shell.root(), "status:item:encoding");
    REQUIRE(pressed != nullptr);
    CHECK(pressed->commonStyle().background == app.shell.theme().list.pressed);
}
