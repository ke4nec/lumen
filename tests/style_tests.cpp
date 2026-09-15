#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "lumen/accessibility/bridge.h"
#include "lumen/core/damage.h"
#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/style/resolver.h"
#include "lumen/style/state.h"
#include "lumen/style/theme.h"

// 视觉系统单元测试（docs/lumen-visual-system-design.md §10.1）：
// token 映射、主题派生、状态解析、StyleOverrides 与 RenderStyle 写入
// RenderNode/sameNode 的契约。

using Catch::Approx;
using lumen::accessibility::AccessibilitySettings;
using lumen::core::Color;
using lumen::core::ControlSize;
using lumen::core::EdgeInsets;
using lumen::core::Offset;
using lumen::core::TextStyle;
using lumen::core::Widget;
using lumen::style::InteractionStateSnapshot;
using lumen::style::PrimitivePalette;
using lumen::style::StyleContext;
using lumen::style::Typography;
using lumen::style::ControlDensity;
using lumen::style::Theme;
using lumen::style::ThemeDirection;

namespace {

struct Fixture {
    Theme theme{Theme::dark()};
    InteractionStateSnapshot interaction{};
    AccessibilitySettings settings{};
    StyleContext context{theme, interaction, settings, 1.0F};

    [[nodiscard]] lumen::core::ResolvedStyle resolve(
        const Widget& widget, const std::string& identity = "k:x") const {
        return lumen::style::resolveStyle(widget, context, identity);
    }
};

// 按值返回：调用点 `const auto& x = ...Part(fixture.resolve(...))` 把 prvalue
// 直接绑定到引用，生命周期延长到引用作用域；按引用返回会指向已销毁的
// ResolvedStyle 临时对象（MSVC 侥幸可用，GCC/Clang 下悬空读）。
lumen::core::ButtonResolvedStyle buttonPart(
    const lumen::core::ResolvedStyle& style) {
    return std::get<lumen::core::ButtonResolvedStyle>(style.component);
}

lumen::core::TextFieldResolvedStyle fieldPart(
    const lumen::core::ResolvedStyle& style) {
    return std::get<lumen::core::TextFieldResolvedStyle>(style.component);
}

lumen::core::CheckboxResolvedStyle checkboxPart(
    const lumen::core::ResolvedStyle& style) {
    return std::get<lumen::core::CheckboxResolvedStyle>(style.component);
}

lumen::core::SwitchResolvedStyle switchPart(
    const lumen::core::ResolvedStyle& style) {
    return std::get<lumen::core::SwitchResolvedStyle>(style.component);
}

}  // namespace

// --- Primitive → semantic → component 映射（§10.1） ---

TEST_CASE("style_maps_primitive_palette_to_dark_semantic_colors",
          "[style]") {
    const PrimitivePalette palette;
    const auto colors = lumen::style::darkColorScheme(palette);
    CHECK(colors.pageBackground == palette.neutral950);
    CHECK(colors.surface == palette.neutral900);
    CHECK(colors.contentPrimary == palette.neutral200);
    CHECK(colors.accent == palette.blue500);
    CHECK(colors.statusError == palette.red500);
}

TEST_CASE("style_blend_over_uses_source_over_alpha", "[style]") {
    const Color base{20, 40, 60, 0};
    const Color overlay{200, 100, 50, 128};
    const Color result = lumen::style::blendOver(base, overlay);
    CHECK(result.a == 128);
    CHECK(result.r == 200);
    CHECK(result.g == 100);
    CHECK(result.b == 50);
}

TEST_CASE("style_maps_primitive_palette_to_light_semantic_colors",
          "[style]") {
    const PrimitivePalette palette;
    const auto colors = lumen::style::lightColorScheme(palette);
    CHECK(colors.pageBackground == palette.neutral100);
    CHECK(colors.contentPrimary == palette.neutral950);
    // light/dark 不只是反色：状态叠加层方向不同（hover 用暗化）。
    CHECK(colors.hoverOverlay.r < colors.pageBackground.r);
}

TEST_CASE("style_maps_semantic_colors_to_component_tokens", "[style]") {
    const Theme theme = Theme::dark();
    const auto& colors = theme.colors;
    CHECK(theme.button.filled.background == colors.accent);
    CHECK(theme.button.filled.content == colors.onAccent);
    CHECK(theme.button.outline.border == colors.borderStrong);
    // S1：透明按钮文字与 Danger 文字使用专属角色（§6.1）。
    CHECK(theme.button.outline.content == colors.accentContent);
    CHECK(theme.button.ghost.content == colors.accentContent);
    CHECK(theme.button.danger.content == colors.onError);
    // S1：字段底色 surfaceSunken、常态边框 borderStrong（§6.3）。
    CHECK(theme.textField.background == colors.surfaceSunken);
    CHECK(theme.textField.border == colors.borderStrong);
    CHECK(theme.textField.borderFocused == colors.focusRing);
    CHECK(theme.textField.borderInvalid == colors.statusError);
    CHECK(theme.checkbox.indicatorChecked == colors.accent);
    CHECK(theme.switchControl.trackOn == colors.accent);
    CHECK(theme.dialog.scrim == colors.scrim);
    CHECK(theme.dialog.surface == colors.surfaceElevated);
}

// --- light/dark/high contrast/font scale/density/reduce animation 派生 ---

TEST_CASE("style_theme_light_and_dark_differ", "[style]") {
    const Theme dark = Theme::dark();
    const Theme light = Theme::light();
    CHECK(dark.colors.pageBackground != light.colors.pageBackground);
    CHECK(dark.colors.contentPrimary != light.colors.contentPrimary);
    // 组件 token 引用 semantic token，主题切换无需遍历控件代码。S1 起
    // 浅色 accent 加深（#3460BE），Filled 背景深浅不同。
    CHECK(dark.button.filled.background != light.button.filled.background);
    CHECK(dark.textField.background != light.textField.background);
    // 深色 Filled 压深色文字、浅色压白字（§4.2 可读性修正）。
    CHECK(dark.button.filled.content == dark.colors.onAccent);
    CHECK(light.button.filled.content == light.colors.onAccent);
}

TEST_CASE("style_theme_high_contrast_boosts_contrast", "[style]") {
    AccessibilitySettings settings;
    settings.highContrast = true;
    const Theme theme = Theme::fromSettings(settings, /*darkMode=*/true);
    const Theme plain = Theme::dark();
    CHECK(theme.colors.contentPrimary == Color{255, 255, 255, 255});
    CHECK(theme.colors.borderStrong == theme.colors.contentPrimary);
    // 焦点环与边框增强，但状态层保持可辨识（hover/pressed 仍有差异）。
    CHECK(theme.metrics.focusRingWidth > plain.metrics.focusRingWidth);
    CHECK(theme.metrics.controlBorderWidth > plain.metrics.controlBorderWidth);
    CHECK(theme.colors.hoverOverlay != theme.colors.pressedOverlay);
    // 组件 token 从 semantic 重建。
    CHECK(theme.textField.borderFocused == theme.colors.focusRing);
}

TEST_CASE("style_theme_font_scale_scales_typography_and_metrics",
          "[style]") {
    AccessibilitySettings settings;
    settings.fontScale = 1.5F;
    const Theme theme = Theme::fromSettings(settings, /*darkMode=*/false);
    const Theme plain = Theme::light();
    CHECK(theme.typography.body.fontSize ==
          Approx(plain.typography.body.fontSize * 1.5F).margin(1e-4F));
    // 控件最小高度与部件尺寸同步放大，避免文字被控件裁剪（§4）。
    CHECK(theme.metrics.minHeight[1] ==
          Approx(plain.metrics.minHeight[1] * 1.5F).margin(1e-4F));
    CHECK(theme.checkbox.indicatorSize[1] ==
          Approx(plain.checkbox.indicatorSize[1] * 1.5F).margin(1e-4F));
}

TEST_CASE("style_theme_reduce_animation_zeroes_motion", "[style]") {
    AccessibilitySettings settings;
    settings.reduceAnimation = true;
    const Theme theme = Theme::fromSettings(settings, /*darkMode=*/true);
    CHECK(theme.motion.stateTransitionMs == 0);
    CHECK(theme.motion.dialogTransitionMs == 0);
    CHECK(theme.motion.navigatorTransitionMs == 0);
    CHECK(theme.motion.caretBlinkHalfPeriodMs == 0);
}

TEST_CASE("style_theme_density_selects_metric_column", "[style]") {
    const Theme touch = Theme::dark(lumen::style::ControlDensity::Touch);
    const Theme compact = Theme::dark(lumen::style::ControlDensity::Compact);
    const Theme comfortable = Theme::dark();
    // §3.2 尺度表：Touch 48/72/144，Comfortable 40/64/120，Compact 32。
    CHECK(touch.metrics.baseIndex == 2);
    CHECK(comfortable.metrics.baseIndex == 1);
    CHECK(compact.metrics.baseIndex == 0);
    CHECK(touch.metrics.minHeight[touch.metrics.baseIndex] == 48.0F);
    CHECK(comfortable.metrics.minHeight[1] == 40.0F);
    CHECK(compact.metrics.minHeight[0] == 32.0F);
}

// --- Button：variant × 状态解析（§10.1） ---

TEST_CASE("style_button_variants_resolve_rest_state", "[style]") {
    const Fixture fixture;
    const Widget filled = lumen::core::makeButton("OK");
    const auto& style = buttonPart(fixture.resolve(filled));
    CHECK(style.common.background == fixture.theme.button.filled.background);
    CHECK(style.common.foreground == fixture.theme.button.filled.content);
    CHECK(style.common.focusWidth == 0.0F);

    Widget outline = lumen::core::withVariant(lumen::core::makeButton("OK"),
                                              lumen::core::ButtonVariant::Outline);
    const auto& outlineStyle = buttonPart(fixture.resolve(outline));
    CHECK(outlineStyle.common.border == fixture.theme.colors.borderStrong);
    CHECK(outlineStyle.common.borderWidth ==
          fixture.theme.metrics.controlBorderWidth);
}

TEST_CASE("style_button_hover_and_pressed_derive_from_base", "[style]") {
    Fixture fixture;
    const Widget button = lumen::core::makeButton("OK");
    fixture.interaction.hoveredIdentity = "k:x";
    const auto& hovered = buttonPart(fixture.resolve(button));
    CHECK(hovered.common.background ==
          lumen::style::blendOver(fixture.theme.button.filled.background,
                                  fixture.theme.colors.hoverOverlay));

    fixture.interaction.pressedIdentity = "k:x";
    const auto& pressed = buttonPart(fixture.resolve(button));
    CHECK(pressed.common.background ==
          lumen::style::blendOver(fixture.theme.button.filled.background,
                                  fixture.theme.colors.pressedOverlay));
    // pressed 覆盖 hover（§5 规则 3）。
    CHECK(pressed.common.background != hovered.common.background);
}

TEST_CASE("style_button_focused_gets_visible_ring", "[style]") {
    Fixture fixture;
    const Widget button = lumen::core::makeButton("OK");
    fixture.interaction.focusedIdentity = "k:x";
    const auto& focused = buttonPart(fixture.resolve(button));
    CHECK(focused.common.focusWidth == fixture.theme.metrics.focusRingWidth);
    CHECK(focused.common.focusRing == fixture.theme.colors.focusRing);
    // 键盘焦点环不依赖 hover 表现（§5 规则 4）。
    CHECK(focused.common.background == fixture.theme.button.filled.background);
}

TEST_CASE("style_button_disabled_wins_over_all_states", "[style]") {
    Fixture fixture;
    Widget button = lumen::core::makeButton("OK");
    button.enabled = false;
    fixture.interaction.hoveredIdentity = "k:x";
    fixture.interaction.pressedIdentity = "k:x";
    fixture.interaction.focusedIdentity = "k:x";
    const auto& disabled = buttonPart(fixture.resolve(button));
    // disabled 优先级最高：不响应 hover/pressed，也不显示焦点环。
    CHECK(disabled.common.background ==
          fixture.theme.colors.disabledBackground);
    CHECK(disabled.common.foreground == fixture.theme.colors.disabledContent);
    CHECK(disabled.common.focusWidth == 0.0F);
}

// --- TextField：focused/invalid/readonly/disabled（§10.1） ---

TEST_CASE("style_textfield_states_resolve", "[style]") {
    const Fixture fixture;
    Widget field = lumen::core::makeTextField("", "hint");
    field.bind = "name";
    const auto& rest = fieldPart(fixture.resolve(field));
    CHECK(rest.common.border == fixture.theme.textField.border);
    CHECK(!rest.focused);

    Fixture focusedFixture;
    Widget focusedField = lumen::core::makeTextField("", "hint");
    focusedFixture.interaction.focusedIdentity = "k:x";
    const auto& focused = fieldPart(focusedFixture.resolve(focusedField));
    CHECK(focused.focused);
    CHECK(focused.common.border == fixture.theme.textField.borderFocused);
    CHECK(focused.common.focusWidth == fixture.theme.metrics.focusRingWidth);

    Widget invalidField = lumen::core::withInvalid(
        lumen::core::makeTextField("", "hint"));
    const auto& invalid = fieldPart(fixture.resolve(invalidField));
    CHECK(invalid.common.border == fixture.theme.textField.borderInvalid);

    Widget readOnlyField = lumen::core::withReadOnly(
        lumen::core::makeTextField("", "hint"));
    CHECK(fieldPart(fixture.resolve(readOnlyField)).readOnly);

    Widget disabledField = lumen::core::withEnabled(
        lumen::core::makeTextField("", "hint"), false);
    const auto& disabled = fieldPart(fixture.resolve(disabledField));
    CHECK(disabled.common.background ==
          fixture.theme.colors.disabledBackground);
    CHECK(!disabled.focused);
}

TEST_CASE("style_control_metrics_come_from_theme", "[style]") {
    const Fixture fixture;
    const Widget button = lumen::core::makeButton("OK");
    const auto& style = fixture.resolve(button);
    CHECK(style.minWidth == fixture.theme.metrics.buttonMinWidth[1]);
    CHECK(style.minHeight == fixture.theme.metrics.minHeight[1]);
    CHECK(buttonPart(style).common.padding.left ==
          fixture.theme.metrics.controlPaddingX[1]);

    const Widget field = lumen::core::makeTextField("", "hint");
    const auto& fieldStyle = fixture.resolve(field);
    CHECK(fieldStyle.minWidth == fixture.theme.metrics.textFieldMinWidth[1]);
}

// --- Checkbox/Switch：checked/focused/disabled（§10.1） ---

TEST_CASE("style_checkbox_states_resolve", "[style]") {
    Fixture fixture;
    Widget checkbox = lumen::core::makeCheckbox("Auto", "auto");
    const auto& rest = checkboxPart(fixture.resolve(checkbox));
    CHECK(!rest.checked);
    CHECK(rest.indicator == fixture.theme.checkbox.indicator);

    Widget checked = lumen::core::makeCheckbox("Auto", "auto", "", true);
    const auto& checkedStyle = checkboxPart(fixture.resolve(checked));
    CHECK(checkedStyle.checked);
    CHECK(checkedStyle.indicatorChecked == fixture.theme.checkbox.indicatorChecked);

    Fixture focusedFixture;
    focusedFixture.interaction.focusedIdentity = "k:x";
    const auto& focused =
        checkboxPart(focusedFixture.resolve(lumen::core::makeCheckbox("A", "a")));
    CHECK(focused.common.focusWidth == fixture.theme.metrics.focusRingWidth);

    Widget disabled = lumen::core::withEnabled(
        lumen::core::makeCheckbox("A", "a"), false);
    const auto& disabledStyle = checkboxPart(fixture.resolve(disabled));
    CHECK(disabledStyle.indicator ==
          fixture.theme.colors.disabledBackground);
    CHECK(disabledStyle.mark == fixture.theme.colors.disabledContent);
}

TEST_CASE("style_switch_states_resolve", "[style]") {
    Fixture fixture;
    Widget control = lumen::core::makeSwitch("Sync", "sync");
    const auto& rest = switchPart(fixture.resolve(control));
    CHECK(!rest.checked);
    CHECK(rest.trackOff == fixture.theme.switchControl.trackOff);

    Widget on = lumen::core::makeSwitch("Sync", "sync", "", true);
    const auto& onStyle = switchPart(fixture.resolve(on));
    CHECK(onStyle.checked);
    CHECK(onStyle.trackOn == fixture.theme.switchControl.trackOn);

    Widget disabled = lumen::core::withEnabled(
        lumen::core::makeSwitch("Sync", "sync"), false);
    const auto& disabledStyle = switchPart(fixture.resolve(disabled));
    CHECK(disabledStyle.trackOn == fixture.theme.colors.disabledBackground);
    CHECK(disabledStyle.knob == fixture.theme.colors.disabledContent);
}

TEST_CASE("style_control_size_scales_component_parts", "[style]") {
    const Fixture fixture;
    // Comfortable 密度基准 = Medium 列；Small/Large 相对偏移（§3.2）。
    Widget small = lumen::core::withControlSize(
        lumen::core::makeCheckbox("A", "a"), ControlSize::Small);
    Widget large = lumen::core::withControlSize(
        lumen::core::makeCheckbox("A", "a"), ControlSize::Large);
    const auto& smallStyle = checkboxPart(fixture.resolve(small));
    const auto& largeStyle = checkboxPart(fixture.resolve(large));
    CHECK(smallStyle.indicatorSize == fixture.theme.checkbox.indicatorSize[0]);
    CHECK(largeStyle.indicatorSize == fixture.theme.checkbox.indicatorSize[2]);

    Widget smallSwitch = lumen::core::withControlSize(
        lumen::core::makeSwitch("S", "s"), ControlSize::Small);
    const auto& smallTrack = switchPart(fixture.resolve(smallSwitch));
    CHECK(smallTrack.trackWidth == fixture.theme.switchControl.trackWidth[0]);
    CHECK(smallTrack.trackHeight == fixture.theme.switchControl.trackHeight[0]);
}

// --- StyleOverrides：字段级覆盖与状态优先级（§10.1） ---

TEST_CASE("style_overrides_override_theme_fields", "[style]") {
    const Fixture fixture;
    const Color brand{10, 20, 30, 255};
    lumen::core::StyleOverrides overrides;
    overrides.background = brand;
    Widget button = lumen::core::withStyleOverrides(
        lumen::core::makeButton("OK"), overrides);
    const auto& style = buttonPart(fixture.resolve(button));
    CHECK(style.common.background == brand);
    CHECK(style.common.foreground == fixture.theme.button.filled.content);
}

TEST_CASE("style_overrides_foreground_recolors_text", "[style]") {
    const Fixture fixture;
    const Color brandFg{1, 2, 3, 255};
    lumen::core::StyleOverrides overrides;
    overrides.foreground = brandFg;
    Widget label = lumen::core::withStyleOverrides(
        lumen::core::makeText("hi"), overrides);
    // 按值持有：commonStyle 返回入参内部的引用，按引用绑定会悬空。
    const auto common = lumen::core::commonStyle(fixture.resolve(label));
    CHECK(common.foreground == brandFg);
    CHECK(common.text.color == brandFg);
}

TEST_CASE("style_overrides_text_honors_literal_colors", "[style]") {
    const Fixture fixture;
    // 全显式文本通道：有意的黑色按字面生效（解决“黑=未设置”歧义）。
    lumen::core::StyleOverrides overrides;
    lumen::core::TextStyle literal;
    literal.color = Color{0, 0, 0, 255};
    overrides.text = literal;
    Widget label = lumen::core::withStyleOverrides(
        lumen::core::makeText("hi"), overrides);
    const auto common = lumen::core::commonStyle(fixture.resolve(label));
    CHECK(common.text.color == Color{0, 0, 0, 255});
}

TEST_CASE("style_overrides_apply_after_state_resolution", "[style]") {
    Fixture fixture;
    fixture.interaction.pressedIdentity = "k:x";
    const Color brand{9, 9, 9, 255};
    lumen::core::StyleOverrides overrides;
    overrides.background = brand;
    Widget button = lumen::core::withStyleOverrides(
        lumen::core::makeButton("OK"), overrides);
    // 状态先折算（pressed 背景），覆盖最后应用（应用对最终值负责）。
    CHECK(buttonPart(fixture.resolve(button)).common.background == brand);
}

// --- ResolvedStyle 写入 RenderNode / sameNode / identity（§10.1） ---

TEST_CASE("style_layout_writes_resolved_style_and_identity", "[style]") {
    Widget root = lumen::core::makeColumn(
        {lumen::core::makeButton("OK", lumen::core::TextStyle{}, lumen::core::EdgeInsets{},
                                 0.0F, "ok")});
    const auto tree = lumen::layout::LayoutEngine::layout(
        root, lumen::core::Constraints::loose(lumen::core::Size{400, 300}));
    REQUIRE(tree.children.size() == 1);
    const auto& button = tree.children.front();
    CHECK(!button.identity.empty());
    CHECK(std::holds_alternative<lumen::core::ButtonResolvedStyle>(
        button.style.component));
    // 布局度量使用 resolved style：按钮至少满足主题最小尺寸。
    CHECK(button.size.width >= button.style.minWidth);
    CHECK(button.size.height >= button.style.minHeight);
}

TEST_CASE("style_state_change_breaks_samenode_for_damage", "[style]") {
    const Widget root = lumen::core::makeButton(
        "OK", lumen::core::TextStyle{}, lumen::core::EdgeInsets{}, 0.0F, "ok");
    const Theme theme = Theme::dark();
    const AccessibilitySettings settings;
    const InteractionStateSnapshot idle;
    InteractionStateSnapshot pressed;
    pressed.pressedIdentity = "/k:ok";

    const auto a = lumen::layout::LayoutEngine::layout(
        root, lumen::core::Constraints::loose(lumen::core::Size{400, 300}),
        StyleContext{theme, idle, settings});
    const auto b = lumen::layout::LayoutEngine::layout(
        root, lumen::core::Constraints::loose(lumen::core::Size{400, 300}),
        StyleContext{theme, pressed, settings});
    // identity 稳定，状态视觉变化进入 diff（§5 规则 6）。
    CHECK(a.identity == b.identity);
    CHECK(!lumen::core::sameNode(a, b));
    std::vector<lumen::core::Rect> damage;
    CHECK(lumen::core::collectDamage(a, b, damage));
    CHECK(!damage.empty());
}

TEST_CASE("style_theme_change_breaks_samenode", "[style]") {
    const Widget root = lumen::core::makeButton(
        "OK", lumen::core::TextStyle{}, lumen::core::EdgeInsets{}, 0.0F, "ok");
    const AccessibilitySettings settings;
    const InteractionStateSnapshot idle;
    const auto dark = lumen::layout::LayoutEngine::layout(
        root, lumen::core::Constraints::loose(lumen::core::Size{400, 300}),
        StyleContext{Theme::dark(), idle, settings});
    const auto light = lumen::layout::LayoutEngine::layout(
        root, lumen::core::Constraints::loose(lumen::core::Size{400, 300}),
        StyleContext{Theme::light(), idle, settings});
    CHECK(!lumen::core::sameNode(dark, light));
}

TEST_CASE("style_identity_stable_across_rebuilds", "[style]") {
    const Widget first = lumen::core::makeColumn({
        lumen::core::makeText("a"),
        lumen::core::makeButton("b", lumen::core::TextStyle{}, lumen::core::EdgeInsets{},
                                0.0F, "b"),
    });
    Widget second = lumen::core::makeColumn({
        lumen::core::makeText("a"),
        lumen::core::makeButton("b2", lumen::core::TextStyle{}, lumen::core::EdgeInsets{},
                                0.0F, "b"),
    });
    const auto constraints =
        lumen::core::Constraints::loose(lumen::core::Size{400, 300});
    const auto treeA = lumen::layout::LayoutEngine::layout(first, constraints);
    const auto treeB = lumen::layout::LayoutEngine::layout(second, constraints);
    CHECK(treeA.identity == treeB.identity);
    REQUIRE(treeA.children.size() == 2);
    REQUIRE(treeB.children.size() == 2);
    CHECK(treeA.children[0].identity == treeB.children[0].identity);
    CHECK(treeA.children[1].identity == treeB.children[1].identity);
}

TEST_CASE("style_focus_ring_does_not_change_layout_size", "[style]") {
    // 焦点环可见但不影响布局尺寸（§10.3）——同一控件 focused/rest 布局
    // 尺寸一致，仅 resolved style 变化。
    const Widget button = lumen::core::makeButton(
        "OK", lumen::core::TextStyle{}, lumen::core::EdgeInsets{}, 0.0F, "ok");
    const Theme theme = Theme::dark();
    const AccessibilitySettings settings;
    const InteractionStateSnapshot idle;
    InteractionStateSnapshot focused;
    focused.focusedIdentity = "/k:ok";
    const auto constraints =
        lumen::core::Constraints::loose(lumen::core::Size{400, 300});
    const auto rest = lumen::layout::LayoutEngine::layout(
        button, constraints, StyleContext{theme, idle, settings});
    const auto active = lumen::layout::LayoutEngine::layout(
        button, constraints, StyleContext{theme, focused, settings});
    CHECK(rest.size == active.size);
    CHECK(lumen::core::commonStyle(active.style).focusWidth > 0.0F);
    CHECK(lumen::core::commonStyle(rest.style).focusWidth == 0.0F);
}

TEST_CASE("style_override_padding_drives_layout_and_render_padding", "[style]") {
    Widget root = lumen::core::makeContainer(lumen::core::makeText("x"));
    lumen::core::StyleOverrides overrides;
    overrides.padding = EdgeInsets::all(10.0F);
    root = lumen::core::withStyleOverrides(std::move(root), overrides);

    const auto tree = lumen::layout::LayoutEngine::layout(
        root, lumen::core::Constraints::loose(lumen::core::Size{200, 200}));
    REQUIRE(tree.children.size() == 1);
    CHECK(tree.padding == EdgeInsets::all(10.0F));
    CHECK(tree.children.front().offset == lumen::core::Offset{10.0F, 10.0F});
}

// --- M11：v0.4 视觉方向变体（design/gallery.html 四方向） ---

TEST_CASE("theme_directions_derive_distinct_core_tokens", "[style]") {
    const Theme core = Theme::dark();
    const Theme ink = Theme::dark(ControlDensity::Comfortable,
                                  ThemeDirection::InkLinen);
    const Theme aurora = Theme::dark(ControlDensity::Comfortable,
                                     ThemeDirection::AuroraSignal);
    const Theme utility = Theme::dark(ControlDensity::Comfortable,
                                      ThemeDirection::UtilityContrast);
    // 四方向页面背景与 accent 互不相同。
    CHECK(core.colors.pageBackground != ink.colors.pageBackground);
    CHECK(core.colors.pageBackground != aurora.colors.pageBackground);
    CHECK(core.colors.pageBackground != utility.colors.pageBackground);
    CHECK(core.colors.accent != ink.colors.accent);
    CHECK(core.colors.accent != aurora.colors.accent);
    CHECK(core.colors.accent != utility.colors.accent);
    // CoreDark 与 v0.3 冻结默认等值（向后兼容基线）。
    CHECK(core.colors.pageBackground == PrimitivePalette{}.neutral950);
    CHECK(core.colors.accent == PrimitivePalette{}.blue500);
    // 元数据。
    CHECK(core.direction == ThemeDirection::CoreDark);
    CHECK(ink.direction == ThemeDirection::InkLinen);
    CHECK((core.darkMode && ink.darkMode));
}

TEST_CASE("theme_direction_light_variants_and_metadata", "[style]") {
    const Theme inkLight = Theme::light(ControlDensity::Comfortable,
                                        ThemeDirection::InkLinen);
    const Theme inkDark = Theme::dark(ControlDensity::Comfortable,
                                      ThemeDirection::InkLinen);
    CHECK_FALSE(inkLight.darkMode);
    CHECK(inkLight.direction == ThemeDirection::InkLinen);
    CHECK(inkLight.colors.pageBackground != inkDark.colors.pageBackground);
    // 方向内 light/dark 的 accent 来自各自模式色板的 blue500 槽位
    //（暗模式取更亮的变体保证对比度）。
    CHECK(inkLight.colors.accent ==
          lumen::style::primitivePaletteFor(ThemeDirection::InkLinen, false)
              .blue500);
    CHECK(inkDark.colors.accent ==
          lumen::style::primitivePaletteFor(ThemeDirection::InkLinen, true)
              .blue500);
}

TEST_CASE("theme_direction_metrics_override_radii_and_border", "[style]") {
    const Theme core = Theme::dark();
    CHECK(core.metrics.controlRadius[1] == 6.0F);
    CHECK(core.metrics.cardRadius == 8.0F);
    CHECK(core.metrics.controlBorderWidth == 1.0F);

    const Theme ink = Theme::dark(ControlDensity::Comfortable,
                                  ThemeDirection::InkLinen);
    CHECK(ink.metrics.controlRadius[1] == 8.0F);
    CHECK(ink.metrics.cardRadius == 12.0F);

    const Theme aurora = Theme::dark(ControlDensity::Comfortable,
                                     ThemeDirection::AuroraSignal);
    CHECK(aurora.metrics.controlRadius[1] == 9.0F);
    CHECK(aurora.metrics.cardRadius == 14.0F);

    const Theme utility = Theme::dark(ControlDensity::Comfortable,
                                      ThemeDirection::UtilityContrast);
    CHECK(utility.metrics.controlRadius[1] == 5.0F);
    CHECK(utility.metrics.cardRadius == 7.0F);
    CHECK(utility.metrics.controlBorderWidth == 2.0F);
}

TEST_CASE("theme_high_contrast_uses_direction_accent", "[style]") {
    AccessibilitySettings settings;
    settings.highContrast = true;
    const Theme ink = Theme::fromSettings(
        settings, /*darkMode=*/true, ControlDensity::Comfortable,
        ThemeDirection::InkLinen);
    // HC accent 来自方向色板的 blue300 槽位（紫），不是 CoreDark 的蓝。
    const PrimitivePalette inkPalette = lumen::style::primitivePaletteFor(
        ThemeDirection::InkLinen, true);
    CHECK(ink.colors.accent == inkPalette.blue300);
    CHECK(ink.colors.accent != PrimitivePalette{}.blue300);
    CHECK(ink.direction == ThemeDirection::InkLinen);
}

TEST_CASE("theme_from_settings_preserves_direction_across_modes", "[style]") {
    AccessibilitySettings settings;
    settings.highContrast = true;
    settings.fontScale = 1.5F;
    const Theme utilityLight = Theme::fromSettings(
        settings, /*darkMode=*/false, ControlDensity::Comfortable,
        ThemeDirection::UtilityContrast);
    CHECK(utilityLight.direction == ThemeDirection::UtilityContrast);
    CHECK_FALSE(utilityLight.darkMode);
    // 方向 Metrics 覆盖与 font scale 共存（半径不随 font scale 缩放）。
    CHECK(utilityLight.metrics.controlRadius[1] == 5.0F);
    CHECK(utilityLight.metrics.minHeight[1] > 40.0F);
    // S1：深色 onAccent 统一为黑（Aurora 浅青 accent 上的旧特判已并入
    // 通用可读性修正，§4.2）。
    const Theme aurora = Theme::fromSettings(
        AccessibilitySettings{}, true, ControlDensity::Comfortable,
        ThemeDirection::AuroraSignal);
    CHECK(aurora.colors.onAccent == Color{0, 0, 0, 255});
}

// --- M12：平台主题适配（保留派生 + accent token 链） ---

TEST_CASE("adapt_platform_theme_preserves_derivation", "[style]") {
    AccessibilitySettings settings;
    settings.highContrast = true;
    settings.fontScale = 1.5F;
    const Theme base = Theme::fromSettings(
        settings, /*darkMode=*/true, ControlDensity::Compact,
        ThemeDirection::InkLinen);
    const Theme adapted = lumen::style::adaptPlatformTheme(
        base, settings, /*darkMode=*/false, Color{200, 100, 50});
    CHECK_FALSE(adapted.darkMode);
    // 方向/密度/高对比/字体缩放全部保留（旧实现只带 fontScale 会丢弃）。
    CHECK(adapted.direction == ThemeDirection::InkLinen);
    CHECK(adapted.metrics.density == ControlDensity::Compact);
    CHECK(adapted.colors.contentPrimary == Color{0, 0, 0, 255});
    CHECK(adapted.typography.body.fontSize == Approx(14.0F * 1.5F));
    // accent 覆盖走 token 链（filled 背景/勾选指示一致派生）。
    CHECK(adapted.colors.accent == Color{200, 100, 50});
    CHECK(adapted.button.filled.background == Color{200, 100, 50});
    CHECK(adapted.checkbox.indicatorChecked == Color{200, 100, 50});
    CHECK(adapted.switchControl.trackOn == Color{200, 100, 50});
    // 不带 accent：强调色来自方向的 light 派生（dark 与 light 色板按模式
    // 取对比度变体，非 base 字面值）。
    const Theme kept = lumen::style::adaptPlatformTheme(base, settings,
                                                        false);
    // 高对比 light 的方向派生强调色（blue700 槽位，非 blue500 基线）。
    CHECK(kept.colors.accent ==
          lumen::style::primitivePaletteFor(ThemeDirection::InkLinen, false)
              .blue700);
}

// --- S1（gui-control-visual-system-task §4.2/§4.3）：调色板目标值与对比度验收 ---

namespace {

// WCAG 2.x 相对亮度与对比度（§4.3 借鉴的验算口径）。
double contrastChannel(double value) {
    value /= 255.0;
    return value <= 0.04045 ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(Color color) {
    return 0.2126 * contrastChannel(color.r) +
           0.7152 * contrastChannel(color.g) +
           0.0722 * contrastChannel(color.b);
}

double contrastRatio(Color a, Color b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

const char* directionName(ThemeDirection direction) {
    switch (direction) {
        case ThemeDirection::CoreDark: return "CoreDark";
        case ThemeDirection::InkLinen: return "InkLinen";
        case ThemeDirection::AuroraSignal: return "AuroraSignal";
        case ThemeDirection::UtilityContrast: return "UtilityContrast";
    }
    return "?";
}

}  // namespace

TEST_CASE("style_core_dark_matches_palette_targets", "[style]") {
    // §4.2 Core Dark 目标调色板逐项断言（dark / light）。
    const Theme dark = Theme::dark();
    CHECK(dark.colors.pageBackground == Color{24, 24, 27, 255});
    CHECK(dark.colors.surface == Color{39, 39, 46, 255});
    CHECK(dark.colors.surfaceElevated == Color{52, 52, 63, 255});
    CHECK(dark.colors.surfaceSunken == Color{46, 46, 54, 255});
    CHECK(dark.colors.contentPrimary == Color{241, 241, 244, 255});
    CHECK(dark.colors.contentSecondary == Color{161, 161, 170, 255});
    CHECK(dark.colors.accent == Color{86, 140, 240, 255});
    CHECK(dark.colors.onAccent == Color{0, 0, 0, 255});
    CHECK(dark.colors.accentContent == Color{168, 197, 250, 255});
    CHECK(dark.colors.accentContainer == Color{46, 60, 96, 255});
    CHECK(dark.colors.onAccentContainer == Color{224, 234, 255, 255});
    CHECK(dark.colors.borderDefault == Color{59, 59, 69, 255});
    CHECK(dark.colors.borderStrong == Color{140, 140, 152, 255});
    CHECK(dark.colors.focusRing == Color{150, 185, 250, 255});
    CHECK(dark.colors.selectionBackground == Color{86, 140, 240, 130});
    CHECK(dark.colors.statusError == Color{224, 90, 96, 255});
    CHECK(dark.colors.onError == Color{0, 0, 0, 255});
    CHECK(dark.colors.errorContent == Color{240, 150, 154, 255});
    CHECK(dark.colors.statusSuccess == Color{115, 201, 145, 255});
    CHECK(dark.colors.statusWarning == Color{226, 181, 102, 255});
    CHECK(dark.colors.disabledBackground == Color{46, 46, 54, 255});
    CHECK(dark.colors.disabledContent == Color{140, 140, 152, 255});
    CHECK(dark.colors.scrim == Color{0, 0, 0, 132});
    CHECK(dark.colors.hoverOverlay == Color{255, 255, 255, 26});
    CHECK(dark.colors.pressedOverlay == Color{0, 0, 0, 26});

    const Theme light = Theme::light();
    CHECK(light.colors.pageBackground == Color{244, 244, 245, 255});
    CHECK(light.colors.surface == Color{255, 255, 255, 255});
    CHECK(light.colors.surfaceElevated == Color{255, 255, 255, 255});
    CHECK(light.colors.surfaceSunken == Color{244, 244, 245, 255});
    CHECK(light.colors.contentPrimary == Color{24, 24, 27, 255});
    CHECK(light.colors.contentSecondary == Color{82, 82, 91, 255});
    CHECK(light.colors.accent == Color{52, 96, 190, 255});
    CHECK(light.colors.onAccent == Color{255, 255, 255, 255});
    CHECK(light.colors.accentContent == Color{35, 74, 145, 255});
    CHECK(light.colors.accentContainer == Color{224, 234, 255, 255});
    CHECK(light.colors.onAccentContainer == Color{35, 74, 145, 255});
    CHECK(light.colors.borderDefault == Color{212, 212, 216, 255});
    CHECK(light.colors.borderStrong == Color{113, 113, 122, 255});
    CHECK(light.colors.focusRing == Color{35, 74, 145, 255});
    CHECK(light.colors.selectionBackground == Color{52, 96, 190, 64});
    CHECK(light.colors.statusError == Color{170, 52, 58, 255});
    CHECK(light.colors.onError == Color{255, 255, 255, 255});
    CHECK(light.colors.errorContent == Color{170, 52, 58, 255});
    CHECK(light.colors.statusSuccess == Color{37, 111, 70, 255});
    CHECK(light.colors.statusWarning == Color{138, 87, 0, 255});
    CHECK(light.colors.disabledBackground == Color{228, 228, 231, 255});
    CHECK(light.colors.disabledContent == Color{113, 113, 122, 255});
    CHECK(light.colors.scrim == Color{0, 0, 0, 96});
    CHECK(light.colors.hoverOverlay == Color{0, 0, 0, 16});
    CHECK(light.colors.pressedOverlay == Color{0, 0, 0, 42});
}

TEST_CASE("style_contrast_text_pairs_meet_4_5_all_directions", "[style]") {
    using lumen::style::blendOver;
    const ThemeDirection directions[] = {
        ThemeDirection::CoreDark, ThemeDirection::InkLinen,
        ThemeDirection::AuroraSignal, ThemeDirection::UtilityContrast};
    for (const ThemeDirection direction : directions) {
        for (const bool darkMode : {true, false}) {
            const Theme theme =
                Theme::fromSettings(AccessibilitySettings{}, darkMode,
                                    ControlDensity::Comfortable, direction);
            const auto& colors = theme.colors;
            const std::string label = std::string(directionName(direction)) +
                                      (darkMode ? "/dark" : "/light");
            const Color surfaces[] = {
                colors.pageBackground, colors.surface,
                colors.surfaceElevated, colors.surfaceSunken};
            for (const Color surface : surfaces) {
                CHECK(contrastRatio(colors.contentPrimary, surface) >= 4.5);
            }
            // placeholder / 辅助说明所在表面（字段 surfaceSunken、面板 surface）。
            CHECK(contrastRatio(colors.contentSecondary,
                                colors.surfaceSunken) >= 4.5);
            CHECK(contrastRatio(colors.contentSecondary, colors.surface) >=
                  4.5);
            // Filled/Danger/Tonal 标签（含 hover/pressed 中间帧）。
            CHECK(contrastRatio(colors.onAccent, colors.accent) >= 4.5);
            CHECK(contrastRatio(
                      colors.onAccent,
                      blendOver(colors.accent, colors.hoverOverlay)) >= 4.5);
            CHECK(contrastRatio(
                      colors.onAccent,
                      blendOver(colors.accent, colors.pressedOverlay)) >= 4.5);
            CHECK(contrastRatio(colors.onError, colors.statusError) >= 4.5);
            CHECK(contrastRatio(
                      colors.onError,
                      blendOver(colors.statusError, colors.hoverOverlay)) >=
                  4.5);
            CHECK(contrastRatio(
                      colors.onError,
                      blendOver(colors.statusError, colors.pressedOverlay)) >=
                  4.5);
            CHECK(contrastRatio(colors.onAccentContainer,
                                colors.accentContainer) >= 4.5);
            CHECK(contrastRatio(colors.onAccentContainer,
                                blendOver(colors.accentContainer,
                                          colors.hoverOverlay)) >= 4.5);
            CHECK(contrastRatio(colors.onAccentContainer,
                                blendOver(colors.accentContainer,
                                          colors.pressedOverlay)) >= 4.5);
            // Outline/Ghost 标签（页面与卡片表面）。
            CHECK(contrastRatio(colors.accentContent,
                                colors.pageBackground) >= 4.5);
            CHECK(contrastRatio(colors.accentContent, colors.surface) >= 4.5);
            // 错误/成功/警告文案（页面表面）。
            CHECK(contrastRatio(colors.errorContent, colors.surface) >= 4.5);
            CHECK(contrastRatio(colors.statusSuccess,
                                colors.pageBackground) >= 4.5);
            CHECK(contrastRatio(colors.statusWarning,
                                colors.pageBackground) >= 4.5);
            // 选区上的正文（合成后再验算，§4.2 alpha 说明）。
            CHECK(contrastRatio(colors.contentPrimary,
                                blendOver(colors.surfaceSunken,
                                          colors.selectionBackground)) >=
                  4.5);
            INFO("direction=" << label);
        }
    }
}

TEST_CASE("style_contrast_non_text_marks_meet_3_to_1", "[style]") {
    const ThemeDirection directions[] = {
        ThemeDirection::CoreDark, ThemeDirection::InkLinen,
        ThemeDirection::AuroraSignal, ThemeDirection::UtilityContrast};
    for (const ThemeDirection direction : directions) {
        for (const bool darkMode : {true, false}) {
            const Theme theme =
                Theme::fromSettings(AccessibilitySettings{}, darkMode,
                                    ControlDensity::Comfortable, direction);
            const auto& colors = theme.colors;
            // 必须靠形状识别的轮廓：字段/Outline/未选框（borderStrong）。
            CHECK(contrastRatio(colors.borderStrong, colors.surface) >= 3.0);
            CHECK(contrastRatio(colors.borderStrong, colors.surfaceSunken) >=
                  3.0);
            // 焦点标识（页面/卡片表面相邻）。
            CHECK(contrastRatio(colors.focusRing, colors.pageBackground) >=
                  3.0);
            CHECK(contrastRatio(colors.focusRing, colors.surface) >= 3.0);
            // 勾选标记 / Radio 内点（onAccent 对 accent 填充）。
            CHECK(contrastRatio(colors.onAccent, colors.accent) >= 3.0);
        }
    }
}

TEST_CASE("style_disabled_states_stay_identifiable", "[style]") {
    const Theme dark = Theme::dark();
    CHECK(contrastRatio(dark.colors.disabledContent,
                        dark.colors.disabledBackground) >= 3.0);
    const Theme light = Theme::light();
    CHECK(contrastRatio(light.colors.disabledContent,
                        light.colors.disabledBackground) >= 3.0);
}

TEST_CASE("style_high_contrast_text_meets_7_to_1_and_drops_shadows",
          "[style]") {
    AccessibilitySettings settings;
    settings.highContrast = true;
    const Theme dark = Theme::fromSettings(settings, true);
    CHECK(contrastRatio(dark.colors.contentPrimary,
                        dark.colors.pageBackground) >= 7.0);
    const Theme light = Theme::fromSettings(settings, false);
    CHECK(contrastRatio(light.colors.contentPrimary,
                        light.colors.pageBackground) >= 7.0);
    // 边框/焦点环宽度按高对比档派生（§4.3）。
    CHECK(dark.metrics.controlBorderWidth == 2.0F);
    CHECK(dark.metrics.focusRingWidth == 3.0F);
    // 阴影不承担层级信息。
    for (const auto& level : dark.elevation.levels) {
        CHECK(level.alpha == 0);
    }
    // 新增语义角色在高对比下仍参与派生（accentContent 跟随 accent）。
    CHECK(dark.colors.accentContent == dark.colors.accent);
    CHECK(light.colors.accentContent == light.colors.accent);
}

TEST_CASE("style_elevation_levels_match_spec", "[style]") {
    const Theme theme = Theme::dark();
    const auto& levels = theme.elevation.levels;
    CHECK(levels[1].offset == Offset{0.0F, 2.0F});
    CHECK(levels[1].blur == 6.0F);
    CHECK(levels[1].alpha == 32);
    CHECK(levels[2].offset == Offset{0.0F, 4.0F});
    CHECK(levels[2].blur == 12.0F);
    CHECK(levels[2].alpha == 64);
    CHECK(levels[3].offset == Offset{0.0F, 8.0F});
    CHECK(levels[3].blur == 24.0F);
    CHECK(levels[3].alpha == 80);
    // 层级数夹取到 [1,3]；0 不经此路径。
    CHECK(theme.elevation.paramsFor(0.5F).blur == 6.0F);
    CHECK(theme.elevation.paramsFor(99.0F).blur == 24.0F);
}

TEST_CASE("style_typography_line_heights_match_spec", "[style]") {
    const Typography typography = Theme::dark().typography;
    CHECK(typography.title.fontSize == 20.0F);
    CHECK(typography.title.lineHeight == Approx(28.0F / 20.0F));
    CHECK(typography.body.lineHeight == Approx(20.0F / 14.0F));
    CHECK(typography.label.lineHeight == Approx(20.0F / 14.0F));
    CHECK(typography.caption.fontSize == 12.0F);
    CHECK(typography.caption.lineHeight == Approx(18.0F / 12.0F));
}

TEST_CASE("style_metrics_inline_icon_size_scales_with_font", "[style]") {
    const Theme theme = Theme::dark();
    CHECK(theme.metrics.inlineIconSize[0] == 16.0F);
    CHECK(theme.metrics.inlineIconSize[1] == 16.0F);
    CHECK(theme.metrics.inlineIconSize[2] == 20.0F);
    AccessibilitySettings settings;
    settings.fontScale = 2.0F;
    const Theme scaled = Theme::fromSettings(settings, true);
    CHECK(scaled.metrics.inlineIconSize[1] == Approx(32.0F));
}

TEST_CASE("adapt_platform_theme_rederives_accent_content", "[style]") {
    const Theme base = Theme::dark();
    const Theme adapted = lumen::style::adaptPlatformTheme(
        base, AccessibilitySettings{}, true, Color{74, 160, 106});
    CHECK(adapted.colors.accent == Color{74, 160, 106});
    // 透明按钮文字随 accent 重派生（混合方向按模式）。
    CHECK(adapted.colors.accentContent != base.colors.accentContent);
    CHECK(adapted.button.outline.content == adapted.colors.accentContent);
    CHECK(adapted.button.ghost.content == adapted.colors.accentContent);
    // 重派生后的 accentContent 在页面表面上仍可读。
    CHECK(contrastRatio(adapted.colors.accentContent,
                        adapted.colors.pageBackground) >= 4.5);
}
