// Spin 数值步进（docs/lumen-spin-design.md）测试：值模型（钳制/snap/
// decimals/wrap）、提交解析（invalid/Escape/失焦 revert）、stepper 单击
// 与按住自动重复节奏、到界顶住、键盘全契约、滚轮、密度派生与语义。
//
// 命名遵循项目测试规范（行为命名）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
    CHECK(up->size.height == Catch::Approx(19.5F).margin(0.01F));  // 半高（中缝 1px）

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
