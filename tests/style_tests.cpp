#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
using lumen::core::TextStyle;
using lumen::core::Widget;
using lumen::style::InteractionStateSnapshot;
using lumen::style::PrimitivePalette;
using lumen::style::StyleContext;
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
    CHECK(theme.textField.background == colors.surfaceElevated);
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
    // 组件 token 引用 semantic token，主题切换无需遍历控件代码。
    CHECK(dark.button.filled.background == light.button.filled.background);
    CHECK(dark.textField.background != light.textField.background);
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
    // Aurora 近似：深字压浅青 accent。
    const Theme aurora = Theme::fromSettings(
        AccessibilitySettings{}, true, ControlDensity::Comfortable,
        ThemeDirection::AuroraSignal);
    CHECK(aurora.colors.onAccent == Color{8, 19, 33, 255});
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
