// Spin 数值步进（docs/lumen-spin-design.md）测试：值模型（钳制/snap/
// decimals/wrap）、提交解析（invalid/Escape/失焦 revert）、stepper 单击
// 与按住自动重复节奏、到界顶住、键盘全契约、滚轮、密度派生与语义。
//
// 命名遵循项目测试规范（行为命名）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/widgets/spin.h"

using namespace lumen;
using namespace lumen::core;
using lumen::widgets::SpinController;

namespace {

// build 读取控制器状态（field 文本经 store bind），照 MotionBarApp 的
// NSDMI 捕获 this 模式。
class SpinApp {
  public:
    app::AppShell shell{makeConfig(this)};
    SpinController spin;
    std::vector<double> changes;
    std::vector<double> commits;

    static app::ShellConfig makeConfig(SpinApp* self) {
        app::ShellConfig config;
        config.build = [self] {
            return makeColumn(
                {self->spin.build(self->shell.theme())},
                MainAxisAlignment::Start, CrossAxisAlignment::Start);
        };
        config.onKey = [self](app::AppShell& shell, Key key,
                              KeyModifiers mods, char keyChar) {
            return self->spin.handleKey(shell, key, mods, keyChar);
        };
        return config;
    }

    explicit SpinApp(double initial = 40.0)
        : spin{initial, "opacity"} {
        shell.setView(Size{800.0F, 600.0F});
        spin.setLabel("不透明度");
        spin.onValueChanged = [this](double v) { changes.push_back(v); };
        spin.onCommitted = [this](double v) { commits.push_back(v); };
        spin.attach(shell);
        shell.rebuildIfDirty();
    }

    [[nodiscard]] Offset centerOf(const std::string& key) const {
        const RenderNode* node = findNodeByKey(shell.root(), key);
        REQUIRE(node != nullptr);
        return absoluteOffset(shell.root(), key) +
               Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    }

    void focusField() {
        const RenderNode* field = findNodeByKey(shell.root(), "opacity:field");
        REQUIRE(field != nullptr);
        shell.controller().focusNode(*field);
    }
};

}  // namespace

TEST_CASE("spin_value_clamps_snaps_and_formats", "[widgets][spin]") {
    SpinController spin{40.0, "v"};
    spin.setRange(0.0, 100.0);

    spin.setValue(45.6);
    CHECK(spin.value() == 46.0);  // step 1 snap（四舍五入）
    spin.setValue(-5.0);
    CHECK(spin.value() == 0.0);   // 下界钳制
    spin.setValue(250.0);
    CHECK(spin.value() == 100.0);  // 上界钳制

    spin.setStep(5.0);
    spin.setValue(42.0);
    CHECK(spin.value() == 40.0);  // step 5 网格 snap
    CHECK(spin.formatValue() == "40");

    spin.setStep(0.1);  // 小数步进：snap 网格 + decimals 精度联动
    spin.setDecimals(1);
    spin.setValue(10.06);
    CHECK(spin.value() == Catch::Approx(10.1));
    CHECK(spin.formatValue() == "10.1");
    spin.setValue(10.04);
    CHECK(spin.value() == Catch::Approx(10.0));
}

TEST_CASE("spin_wrap_cycles_range", "[widgets][spin]") {
    SpinController spin{0.0, "w"};
    spin.setRange(0.0, 10.0);
    spin.setStep(1.0);
    spin.setWrap(true);

    spin.setValue(-1.0);
    CHECK(spin.value() == 0.0);   // 直接设值：钳制不环绕（GTK 口径）
    spin.stepDown();
    CHECK(spin.value() == 10.0);  // 步进环绕：min 之下 → max
    spin.stepUp();
    CHECK(spin.value() == 0.0);   // max 之上 → min

    // 无 wrap：到界顶住。
    SpinController held{0.0, "h"};
    held.setRange(0.0, 10.0);
    held.stepDown();
    CHECK(held.value() == 0.0);
}

TEST_CASE("spin_commit_text_parses_and_rejects", "[widgets][spin]") {
    SpinApp app{40.0};
    CHECK(app.spin.commitText("55"));
    CHECK(app.spin.value() == 55.0);
    CHECK(app.commits.size() == 1);

    CHECK_FALSE(app.spin.commitText("1２a"));   // 非数字拒绝
    CHECK_FALSE(app.spin.commitText(""));
    CHECK(app.spin.value() == 55.0);            // 值不变
    CHECK(app.commits.size() == 1);

    CHECK(app.spin.commitText("120"));
    CHECK(app.spin.value() == 100.0);           // 提交同样钳制
}

TEST_CASE("spin_stepper_click_steps_and_hold_repeats", "[widgets][spin]") {
    SpinApp app{40.0};
    const Offset up = app.centerOf("spin:up:opacity");
    const Offset down = app.centerOf("spin:down:opacity");

    // 单击 ▲：+step。
    app.shell.pointerDown(up);
    app.shell.pointerUp(up);
    CHECK(app.spin.value() == 41.0);

    // 单击 ▼：-step。
    app.shell.pointerDown(down);
    app.shell.pointerUp(down);
    CHECK(app.spin.value() == 40.0);

    // 按住 ▲：立即一步 → 500ms 延迟 → 60ms 间隔连续步进（Win32 节奏）。
    app.shell.pointerDown(up);
    CHECK(app.shell.controller().pressedKey() == "spin:up:opacity");
    CHECK(app.spin.step(app.shell, 1000));        // 首拍：立即 +1
    CHECK(app.spin.value() == 41.0);
    CHECK(app.spin.step(app.shell, 1400));        // 延迟期内：无步进
    CHECK(app.spin.value() == 41.0);
    CHECK(app.spin.step(app.shell, 1500));        // 500ms 到：+1
    CHECK(app.spin.value() == 42.0);
    CHECK(app.spin.step(app.shell, 1555));        // 未到 60ms：无步进
    CHECK(app.spin.value() == 42.0);
    CHECK(app.spin.step(app.shell, 1560));        // 间隔到：+1
    CHECK(app.spin.value() == 43.0);

    // 释放：重复停止；按住已步进 → 释放单击被吞并（不重复 +1）。
    app.shell.pointerUp(up);
    CHECK_FALSE(app.spin.step(app.shell, 1600));
    CHECK(app.spin.value() == 43.0);
}

TEST_CASE("spin_hold_release_without_step_fires_single_click",
          "[widgets][spin]") {
    // 快速单击（pointerDown → pointerUp 之间无 step tick）：单击 handler
    // 兜底步进一次。
    SpinApp app{40.0};
    const Offset up = app.centerOf("spin:up:opacity");
    app.shell.pointerDown(up);
    app.shell.pointerUp(up);
    CHECK(app.spin.value() == 41.0);
}

TEST_CASE("spin_at_bound_clamps_and_fades_chevron", "[widgets][spin]") {
    SpinApp app{100.0};  // 初始即 max
    // 步进顶住：无位移、无回调（Splitter"顶住"手感）。
    app.spin.stepUp();
    CHECK(app.spin.value() == 100.0);
    CHECK(app.changes.empty());
    CHECK(app.spin.atMax());
    CHECK(app.spin.atMin() == false);

    // 到界 chevron 淡化（overrides 在状态折算后应用——hover 亦不恢复）。
    app.shell.rebuildIfDirty();
    const RenderNode* up = findNodeByKey(app.shell.root(), "spin:up:opacity");
    REQUIRE(up != nullptr);
    CHECK(up->commonStyle().foreground ==
          app.shell.theme().colors.disabledContent);
}

TEST_CASE("spin_keyboard_full_contract", "[widgets][spin]") {
    SpinApp app{40.0};
    app.focusField();

    app.shell.keyDown(Key::Up);
    CHECK(app.spin.value() == 41.0);
    app.shell.keyDown(Key::Down);
    CHECK(app.spin.value() == 40.0);
    app.shell.keyDown(Key::PageUp);
    CHECK(app.spin.value() == 50.0);
    app.shell.keyDown(Key::PageDown);
    CHECK(app.spin.value() == 40.0);
    app.shell.keyDown(Key::End);
    CHECK(app.spin.value() == 100.0);
    app.shell.keyDown(Key::Home);
    CHECK(app.spin.value() == 0.0);

    // 键入非法 → invalid 语义；Escape 恢复已提交值。
    app.shell.textInput("abc");
    app.shell.rebuildIfDirty();
    const RenderNode* row = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(row != nullptr);
    CHECK(app.spin.editingInvalid());
    app.shell.keyDown(Key::Escape);
    CHECK_FALSE(app.spin.editingInvalid());
    CHECK(app.spin.value() == 0.0);

    // 清空后键入合法值 → Enter 提交（onCommitted）。
    app.shell.keyDown(Key::Backspace);
    app.shell.textInput("77");
    app.shell.keyDown(Key::Enter);
    CHECK(app.spin.value() == 77.0);
    REQUIRE(app.commits.size() == 1);
    CHECK(app.commits.back() == 77.0);
}

TEST_CASE("spin_field_is_single_tab_stop_before_steppers", "[widgets][spin]") {
    SpinApp app{40.0};
    // Tab 序：field → ▲ → ▼（stepper 是键盘可达的附属 chrome；MenuBar
    // 栏项同口径）→ 后续无。design"单一停靠点"按实现口径调整（见文档
    // §14 追记）。
    app.focusField();
    app.shell.keyDown(Key::Tab);
    CHECK(app.shell.focus().focusedKey() == "spin:up:opacity");
    app.shell.keyDown(Key::Tab);
    CHECK(app.shell.focus().focusedKey() == "spin:down:opacity");
    app.shell.keyDown(Key::Tab);
    CHECK(app.shell.focus().focusedKey() != "spin:up:opacity");
}

TEST_CASE("spin_wheel_steps_within_bounds", "[widgets][spin]") {
    SpinApp app{40.0};
    const Offset overField = app.centerOf("opacity");
    CHECK(app.spin.handleWheel(app.shell, overField, Offset{0.0F, -60.0F}));
    CHECK(app.spin.value() == 41.0);
    CHECK(app.spin.handleWheel(app.shell, overField, Offset{0.0F, 60.0F}));
    CHECK(app.spin.value() == 40.0);
    // 控件外不消费。
    CHECK_FALSE(app.spin.handleWheel(app.shell, Offset{700.0F, 500.0F},
                                     Offset{0.0F, -60.0F}));
    // 上界后滚轮顶住。
    app.spin.setValue(100.0);
    CHECK(app.spin.handleWheel(app.shell, overField, Offset{0.0F, -60.0F}));
    CHECK(app.spin.value() == 100.0);
}

TEST_CASE("spin_density_scales_stepper", "[widgets][spin]") {
    SpinApp app{40.0};
    const auto* row = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(row != nullptr);
    CHECK(row->size.height == Catch::Approx(40.0F).margin(0.01F));
    const RenderNode* up = findNodeByKey(app.shell.root(), "spin:up:opacity");
    REQUIRE(up != nullptr);
    CHECK(up->size.width == Catch::Approx(28.0F).margin(0.01F));   // Medium
    // 内容高 = 外高 40 − 上下边框各 1；中缝 1px 取整拆分（floor），上半格 18。
    CHECK(up->size.height == Catch::Approx(18.0F).margin(0.01F));

    app.spin.setControlSize(ControlSize::Large);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* rowLarge = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(rowLarge != nullptr);
    CHECK(rowLarge->size.height == Catch::Approx(48.0F).margin(0.01F));
    const RenderNode* upLarge =
        findNodeByKey(app.shell.root(), "spin:up:opacity");
    REQUIRE(upLarge != nullptr);
    CHECK(upLarge->size.width == Catch::Approx(32.0F).margin(0.01F));
}

// §9.1 尺度表：控件最小宽 = 边框内缩 2 + textfield 最小宽（96/120/144）+
// 1px 分隔线 + stepper 宽（24/28/32），且整控件按内容收拢（稿件
// design/spin.html .spin{width:max-content}，border-box 口径）。field 的
// flex 预算只在容器窄于内容时压缩（shrinkWrap）——否则 Spin 作为非 flex
// 子节点放进拉伸的 Row 会吞掉整行剩余宽并撑破父容器（gallery Controls
// 卡片溢出回归）。
TEST_CASE("spin_sizes_to_content_not_container", "[widgets][spin]") {
    SpinApp app{72.0};
    const auto* row = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(row != nullptr);
    CHECK(row->size.width == Catch::Approx(151.0F).margin(0.01F));

    app.spin.setControlSize(ControlSize::Small);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const auto* small = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(small != nullptr);
    CHECK(small->size.width == Catch::Approx(123.0F).margin(0.01F));
    CHECK(small->size.height == Catch::Approx(32.0F).margin(0.01F));

    app.spin.setControlSize(ControlSize::Large);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const auto* large = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(large != nullptr);
    CHECK(large->size.width == Catch::Approx(179.0F).margin(0.01F));
}

TEST_CASE("spin_semantics_role_and_value", "[widgets][spin]") {
    SpinApp app{40.0};
    const RenderNode* row = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(row != nullptr);
    accessibility::SemanticsRole role{};
    REQUIRE(accessibility::semanticsRoleFromName(row->semanticsRole, &role));
    CHECK(role == accessibility::SemanticsRole::SpinButton);

    const auto tree =
        accessibility::buildSemanticsTree(app.shell.root());
    const auto* semantic = tree.find(row->identity);
    REQUIRE(semantic != nullptr);
    CHECK(semantic->value == "40");
    CHECK(semantic->label == "不透明度");
    CHECK((semantic->actions & accessibility::kActionSetValue) != 0);
}

TEST_CASE("spin_invalid_border_reflects_editing_state", "[widgets][spin]") {
    SpinApp app{40.0};
    app.focusField();
    app.shell.textInput("x");
    app.shell.rebuildIfDirty();
    const RenderNode* row = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(row != nullptr);
    CHECK(row->commonStyle().border ==
          app.shell.theme().textField.borderInvalid);
    // 恢复合法文本 → 边框回正常（focused 边框）。
    app.shell.keyDown(Key::Escape);
    app.shell.rebuildIfDirty();
    const RenderNode* rowAfter = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(rowAfter != nullptr);
    CHECK(rowAfter->commonStyle().border ==
          app.shell.theme().textField.borderFocused);
}

TEST_CASE("spin_disabled_rejects_all_input", "[widgets][spin]") {
    SpinApp app{40.0};
    app.spin.setEnabled(false);
    app.shell.markDirty();
    app.shell.rebuildIfDirty();

    // 按钮：命中拒绝（disabled 不武装点击）。
    const Offset up = app.centerOf("spin:up:opacity");
    app.shell.pointerDown(up);
    app.shell.pointerUp(up);
    CHECK(app.spin.value() == 40.0);

    // 键盘/滚轮：不消费。
    app.focusField();
    CHECK_FALSE(app.spin.handleKey(app.shell, Key::Up));
    CHECK_FALSE(app.spin.handleWheel(app.shell, up, Offset{0.0F, -60.0F}));
    CHECK(app.spin.value() == 40.0);

    // 视觉：field disabled 底色、stepper chevron disabledContent。
    const RenderNode* field = findNodeByKey(app.shell.root(), "opacity:field");
    REQUIRE(field != nullptr);
    CHECK(field->commonStyle().background ==
          app.shell.theme().colors.disabledBackground);
    const RenderNode* upNode =
        findNodeByKey(app.shell.root(), "spin:up:opacity");
    REQUIRE(upNode != nullptr);
    CHECK(upNode->commonStyle().foreground ==
          app.shell.theme().colors.disabledContent);
}

TEST_CASE("spin_hold_stops_when_pointer_leaves_hit_area",
          "[widgets][spin]") {
    SpinApp app{40.0};
    const Offset up = app.centerOf("spin:up:opacity");
    app.shell.pointerDown(up);
    CHECK(app.spin.step(app.shell, 1000));  // 首拍步进
    CHECK(app.spin.value() == 41.0);
    // 移出命中区（超过拖动 slop）：按住重复停止（design §6.1）。
    app.shell.pointerMove(up + Offset{80.0F, 0.0F});
    CHECK(app.shell.controller().isDragging());
    CHECK_FALSE(app.spin.step(app.shell, 1600));
    CHECK(app.spin.value() == 41.0);
}

TEST_CASE("spin_stepper_hover_brightens_and_pressed_uses_accent_mix",
          "[widgets][spin]") {
    SpinApp app{40.0};
    // hover（preview state 注入；design §9.3：表面派生 + 前景 primary）。
    app.shell.setVisualPreviewState(
        "spin:up:opacity", style::WidgetState{.hovered = true});
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* up = findNodeByKey(app.shell.root(), "spin:up:opacity");
    REQUIRE(up != nullptr);
    CHECK(up->commonStyle().foreground ==
          app.shell.theme().colors.contentPrimary);
    CHECK(up->commonStyle().background ==
          style::blendOver(core::Color::transparent(),
                          app.shell.theme().colors.hoverOverlay));

    // pressed（按住/自动重复中）：List pressed（surface/accent 0.32 混合）。
    app.shell.setVisualPreviewState(
        "spin:up:opacity", style::WidgetState{.pressed = true});
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* pressed =
        findNodeByKey(app.shell.root(), "spin:up:opacity");
    REQUIRE(pressed != nullptr);
    CHECK(pressed->commonStyle().background == app.shell.theme().list.pressed);
}

// 稿件 field.focus() 口径：点 stepper 也要整控件 focused 蓝边（此前只有点
// 进 field 才蓝）。指针点按先清焦点，tapStep/step 收拢回 field。
TEST_CASE("spin_stepper_click_shows_focused_border", "[widgets][spin]") {
    SpinApp app{40.0};
    const Offset up = app.centerOf("spin:up:opacity");
    app.shell.pointerDown(up);
    app.shell.pointerUp(up);
    app.shell.rebuildIfDirty();
    CHECK(app.shell.focus().focusedKey() == "opacity:field");
    const RenderNode* row = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(row != nullptr);
    CHECK(row->commonStyle().border ==
          app.shell.theme().textField.borderFocused);
}

// Tab 到 ▲ 后方向键仍步进（焦点不动，整控件保持蓝边）。
TEST_CASE("spin_stepper_focused_arrows_step", "[widgets][spin]") {
    SpinApp app{40.0};
    app.focusField();
    app.shell.keyDown(Key::Tab);
    REQUIRE(app.shell.focus().focusedKey() == "spin:up:opacity");
    app.shell.keyDown(Key::Up);
    CHECK(app.spin.value() == 41.0);
    CHECK(app.shell.focus().focusedKey() == "spin:up:opacity");
    app.shell.keyDown(Key::Down);
    CHECK(app.spin.value() == 40.0);
}

// Tab 到 ▲/▼（三停靠点）同样整控件蓝边——键盘在 stepper 上不丢焦点指示。
TEST_CASE("spin_stepper_focus_shows_focused_border", "[widgets][spin]") {
    SpinApp app{40.0};
    app.focusField();
    app.shell.keyDown(Key::Tab);
    REQUIRE(app.shell.focus().focusedKey() == "spin:up:opacity");
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* row = findNodeByKey(app.shell.root(), "opacity");
    REQUIRE(row != nullptr);
    CHECK(row->commonStyle().border ==
          app.shell.theme().textField.borderFocused);
}

// 到界（atBound）：chevron 淡化 + hover/pressed 背景抑制（稿件
// .at-bound:hover{background:transparent}——顶住无位移，不给误导性高亮）。
TEST_CASE("spin_at_bound_hover_suppresses_background", "[widgets][spin]") {
    SpinApp app{100.0};  // 初始即 max，▲ 到界
    app.shell.setVisualPreviewState(
        "spin:up:opacity", style::WidgetState{.hovered = true});
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* up = findNodeByKey(app.shell.root(), "spin:up:opacity");
    REQUIRE(up != nullptr);
    CHECK(up->commonStyle().foreground ==
          app.shell.theme().colors.disabledContent);
    CHECK(up->commonStyle().background == core::Color::transparent());
}

// 中缝 1px 分隔线落在整数像素上（取整拆分）：线像素为实色 borderDefault，
// 上下相邻像素为底色——半像素 19.5 时线会虚成两行各 50% 灰。
TEST_CASE("spin_mid_separator_is_crisp_single_px", "[widgets][spin]") {
    SpinApp app{40.0};
    (void)app.shell.renderFrame(true);
    const auto& pixels = app.shell.pixels();
    const auto at = [&](int x, int y) {
        const std::size_t o = (std::size_t(y) * pixels.width + x) * 4;
        return core::Color::fromRGBA(pixels.rgba[o], pixels.rgba[o + 1],
                                     pixels.rgba[o + 2], pixels.rgba[o + 3]);
    };
    // Medium：内容原点 y=1（边框内缩），上半格 18 → 中缝 y=19；cluster 左缘
    // x=122（1 padding + 120 field + 1 竖线）。
    const core::Color line = at(135, 19);
    CHECK(line == app.shell.theme().colors.borderDefault);
    CHECK(at(135, 18) == app.shell.theme().textField.background);
    CHECK(at(135, 20) == app.shell.theme().textField.background);
}

// 稿件 .spin{overflow:hidden} + .spin-step 无 border-radius（§5"外缘右侧圆角
// 随 controlRadius，内缘直角"）：stepper 填充贴满半格、由外框圆角门控。按钮
// 自带 controlRadius 时 hover/pressed 会缩成悬浮药丸；不裁剪则方形填充又盖过
// 右缘角部（visual-system §11.1 第一防线）。
TEST_CASE("spin_stepper_fill_is_square_and_clipped_by_frame",
          "[widgets][spin]") {
    SpinApp app{72.0};
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* up = findNodeByKey(app.shell.root(), "spin:up:opacity");
    REQUIRE(up != nullptr);
    CHECK(up->commonStyle().radius == core::CornerRadius::zero());

    // 行原点 (0,0)、宽 151（边框内缩 1px padding）：cluster 左缘 = 第 122
    // 列。贴边像素的增强幅度与按钮内部同强 = 方形填充；圆角内缩时贴边像素
    // 几乎不变。
    const auto luma = [&](int x, int y) {
        const auto& p = app.shell.pixels();
        const std::size_t o = (std::size_t(y) * p.width + x) * 4;
        return (unsigned(p.rgba[o]) * 30 + unsigned(p.rgba[o + 1]) * 59 +
                unsigned(p.rgba[o + 2]) * 11) / 100;
    };
    (void)app.shell.renderFrame(true);
    const unsigned edgeBefore = luma(122, 2);
    const unsigned coreBefore = luma(125, 9);  // 填充参照（避开 chevron 图标）
    const unsigned cornerBefore = luma(150, 0);  // 外框右上角
    app.shell.setVisualPreviewState("spin:up:opacity",
                                    style::WidgetState{.hovered = true});
    (void)app.shell.renderFrame(true);
    const int edgeDelta = int(luma(122, 2)) - int(edgeBefore);
    const int coreDelta = int(luma(125, 9)) - int(coreBefore);
    REQUIRE(coreDelta > 0);
    CHECK(edgeDelta >= coreDelta);
    // overflow:hidden：方形填充也不得盖过外框圆角——角部像素不随 hover 变。
    CHECK(luma(150, 0) == cornerBefore);
}

// chevron 设备对齐（render drawIcon 原点按设备像素取整）：奇数高半格
//（内容 38 − 中缝 1 → 18/19）的图标逻辑居中必有一格落半像素（19px 盒装
// 14px 图标 → 2.5），不对齐则 ▼ 比 ▲ 虚散（墨散布更广）。中间值使上下
// 同亮度，直接对比墨量与峰值。
TEST_CASE("spin_stepper_chevrons_align_to_device_pixels",
          "[widgets][spin]") {
    SpinApp app{18.0};  // 中间值：上下均非到界
    (void)app.shell.renderFrame(true);
    const auto& pixels = app.shell.pixels();
    auto inkOf = [&](const RenderNode* node) {
        const Offset origin = absoluteOffset(app.shell.root(), node->key);
        const int x0 = static_cast<int>(origin.x);
        const int y0 = static_cast<int>(origin.y);
        const int w = static_cast<int>(node->size.width);
        const int h = static_cast<int>(node->size.height);
        unsigned peak = 0;
        int count = 0;
        for (int y = y0; y < y0 + h; ++y)
            for (int x = x0; x < x0 + w; ++x) {
                const std::size_t o =
                    (static_cast<std::size_t>(y) * pixels.width + x) * 4;
                const unsigned v =
                    (unsigned(pixels.rgba[o]) * 30 +
                     unsigned(pixels.rgba[o + 1]) * 59 +
                     unsigned(pixels.rgba[o + 2]) * 11) /
                    100;
                if (v > 110) {
                    ++count;
                    peak = std::max(peak, v);
                }
            }
        return std::pair<unsigned, int>{peak, count};
    };
    const RenderNode* up = findNodeByKey(app.shell.root(), "spin:up:opacity");
    const RenderNode* down =
        findNodeByKey(app.shell.root(), "spin:down:opacity");
    REQUIRE(up != nullptr);
    REQUIRE(down != nullptr);
    const auto [upPeak, upCount] = inkOf(up);
    const auto [downPeak, downCount] = inkOf(down);
    CHECK(upPeak == downPeak);
    CHECK(upCount == downCount);
}
