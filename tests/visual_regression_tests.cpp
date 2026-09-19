#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gallery_app.h"
#include "lumen/core/damage.h"
#include "lumen/core/text_field.h"
#include "lumen/text/grapheme.h"

using namespace lumen;
using namespace lumen::core;
using Catch::Approx;

namespace {

RenderNode layoutControl(const Widget& widget, const style::Theme& theme,
                         bool focused = false, Size limit = {400, 300}) {
    style::InteractionStateSnapshot interaction;
    accessibility::AccessibilitySettings accessibility;
    style::StyleContext context{theme, interaction, accessibility, 1.0F};
    auto result = layout::LayoutEngine::layout(widget, Constraints::loose(limit), context);
    if (focused) {
        interaction.focusedIdentity = result.identity;
        result = layout::LayoutEngine::layout(widget, Constraints::loose(limit), context);
    }
    return result;
}

}  // namespace

TEST_CASE("field_scroll_hit_testing_and_ime_match_painted_caret", "[visual][regression]") {
    for (const bool obscure : {false, true}) {
        for (const bool multiline : {false, true}) {
            CAPTURE(obscure, multiline);
            app::ShellConfig config;
            config.caretBlink = false;
            config.build = [=] {
                auto field = makeTextField("", "hint");
                field.bind = "value";
                field.key = "field";
                field.width = 120;
                field.height = multiline ? 220.0F : 44.0F;
                field.multiline = multiline;
                field.obscure = obscure;
                return makeColumn({field});
            };
            app::AppShell shell(config);
            shell.state().set("value", std::string(40, 'a'));
            (void)shell.renderFrame();
            shell.controller().focusNode(*findNodeByKey(shell.root(), "field"));
            (void)shell.renderFrame();
            for (const bool composing : {false, true}) {
                CAPTURE(composing);
                if (composing) {
                    shell.textEditing("中文");
                    (void)shell.renderFrame();
                }
                const auto* field = findNodeByKey(shell.root(), "field");
                REQUIRE(field != nullptr);
                render::PaintOptions options;
                options.caretGraphemes = shell.controller().caretGraphemes();
                options.selectionStart = shell.controller().selectionStart();
                options.composition = shell.controller().composition();
                const auto commands = render::recordScene(shell.root(), options);
                bool sawCaret = false;
                for (const auto& command : commands.commands()) {
                    if (command.type == render::CommandType::DrawRect &&
                        command.rect.size.width == 1.0F &&
                        command.color == std::get<TextFieldResolvedStyle>(field->style.component).caret) {
                        sawCaret = true;
                        CHECK(command.rect == shell.focusedTextRect());
                    }
                }
                REQUIRE(sawCaret);
                const auto caret = shell.focusedTextRect();
                CHECK(caret.origin.x >= 0);
                CHECK(caret.origin.x + caret.size.width <= field->size.width);
                CHECK(shell.focusedCaretOffset() == static_cast<int>(caret.origin.x));
                if (!composing) {
                    // Click exactly the displayed caret, including vertical centering.
                    const Offset point = caret.origin + Offset{0, caret.size.height * 0.5F};
                    shell.pointerDown(point);
                    shell.pointerUp(point);
                    CHECK(shell.controller().caretGraphemes() == 40);
                }
            }
        }
    }
}

TEST_CASE("empty_password_field_shows_preedit_and_placeholder", "[visual][regression]") {
    auto field = layoutControl(withObscure(makeTextField("", "Password"), true), style::Theme::dark());
    CHECK(textFieldDisplay(field).text == "Password");
    const auto display = textFieldDisplay(field, "中文", 2);
    CHECK_FALSE(display.showingPlaceholder);
    CHECK(display.compositionLength == 2);
    CHECK(text::graphemeCount(display.text) == 2);
    CHECK(display.text != "中文");
}

TEST_CASE("choice_targets_reserve_focus_space_without_resizing_parts", "[visual][regression]") {
    for (const auto density : {style::ControlDensity::Compact, style::ControlDensity::Comfortable,
                               style::ControlDensity::Touch}) {
        for (const bool highContrast : {false, true}) {
            accessibility::AccessibilitySettings settings;
            settings.highContrast = highContrast;
            const auto theme = style::Theme::fromSettings(settings, true, density);
            for (const auto size : {ControlSize::Small, ControlSize::Medium, ControlSize::Large}) {
                for (auto widget : {makeCheckbox("", "a", "cb", true),
                                     makeSwitch("", "b", "sw", true),
                                     makeRadio("", "c", "radio", true)}) {
                    widget.controlSize = size;
                    const auto normal = layoutControl(widget, theme);
                    const auto focused = layoutControl(widget, theme, true);
                    CHECK(normal.size.height >= normal.style.minHeight);
                    CHECK(focused.size == normal.size);
                    auto before = render::recordScene(normal).commands();
                    auto after = render::recordScene(focused).commands();
                    std::vector<render::RenderCommand> withoutFocus;
                    int focusCount = 0;
                    for (const auto& command : after) {
                        if (command.type == render::CommandType::DrawRectStroke &&
                            command.color == theme.colors.focusRing &&
                            command.strokeWidth == theme.metrics.focusRingWidth) {
                            ++focusCount;
                            CHECK(command.rect.origin.x >= 0);
                            CHECK(command.rect.origin.y >= 0);
                            CHECK(command.rect.origin.x + command.rect.size.width <= focused.size.width);
                            CHECK(command.rect.origin.y + command.rect.size.height <= focused.size.height);
                        } else {
                            withoutFocus.push_back(command);
                        }
                    }
                    CHECK(focusCount == 1);
                    CHECK(withoutFocus == before);
                }
            }
        }
    }
}

TEST_CASE("icon_stroke_uses_actual_glyph_box_in_wide_hosts", "[visual][regression]") {
    const auto theme = style::Theme::dark();
    for (const float width : {100.0F, 240.0F}) {
        auto checkbox = makeCheckbox("Wide", "v", "cb", true);
        checkbox.width = width;
        for (const auto& widget : {makeImage(0, "", width, 80.0F), checkbox}) {
            const auto node = layoutControl(widget, theme);
            bool sawIcon = false;
            const auto commands = render::recordScene(node);
            for (const auto& command : commands.commands()) {
                if (command.type == render::CommandType::DrawIcon) {
                    sawIcon = true;
                    CHECK(command.strokeWidth == Approx(1.8F * command.rect.size.width / 16.0F));
                }
            }
            REQUIRE(sawIcon);
        }
    }
}

TEST_CASE("tooltip_measurement_matches_wrapped_paint_content_width", "[visual][regression]") {
    for (const bool explicitWidth : {false, true}) {
        auto tooltip = makeTooltip("abcdefghijklm", "tip");
        if (explicitWidth) {
            tooltip.width = 100;
        }
        const auto node = layoutControl(tooltip, style::Theme::dark(), false,
                                        {explicitWidth ? 300.0F : 100.0F, 300});
        const auto layout = text::TextLayout::layout(node.text, node.textStyle(),
            node.size.width - node.padding.horizontal(), text::PlaceholderFontManager::shared());
        CHECK(node.size.height >= layout.size.height + node.padding.vertical());
        REQUIRE(layout.lines.size() == 2);
    }
}

TEST_CASE("theme_scaling_survives_high_contrast_and_platform_accent", "[visual][regression]") {
    for (const bool dark : {false, true}) {
        for (const bool highContrast : {false, true}) {
            for (const float scale : {1.0F, 1.5F, 2.0F}) {
                accessibility::AccessibilitySettings settings;
                settings.highContrast = highContrast;
                const auto base = style::Theme::fromSettings(settings, dark);
                settings.fontScale = scale;
                const auto scaled = style::Theme::fromSettings(settings, dark);
                const Color accent{200, 80, 150, 255};
                const auto adapted = style::adaptPlatformTheme(scaled, settings, dark, accent);
                for (const auto& theme : {scaled, adapted}) {
                    CHECK(theme.radio.indicatorSize[1] == base.radio.indicatorSize[1] * scale);
                    CHECK(theme.checkbox.indicatorSize[1] == base.checkbox.indicatorSize[1] * scale);
                    CHECK(theme.switchControl.trackWidth[1] == base.switchControl.trackWidth[1] * scale);
                    CHECK(theme.slider.thumbDiameter[1] == base.slider.thumbDiameter[1] * scale);
                    CHECK(theme.radio.labelGap == base.radio.labelGap * scale);
                    CHECK(theme.tabs.tabPaddingX == base.tabs.tabPaddingX * scale);
                    CHECK(theme.tooltip.paddingX == base.tooltip.paddingX * scale);
                    CHECK(theme.metrics.focusRingWidth == base.metrics.focusRingWidth);
                }
                CHECK(adapted.slider.trackActive == adapted.colors.accent);
                CHECK(adapted.slider.thumbOutline == adapted.colors.accent);
                CHECK(adapted.progressBar.fill == adapted.colors.accent);
                CHECK(adapted.tabs.indicator == adapted.colors.accent);
                CHECK(adapted.tabs.selectedContent == adapted.colors.accentContent);
            }
        }
    }
}

TEST_CASE("tabs_respect_disabled_content_and_explicit_overrides", "[visual][regression]") {
    const auto theme = style::Theme::dark();
    for (const bool selected : {false, true}) {
        auto tab = withEnabled(withKey(makeButton("Tab"), "tab"), false);
        tab.selected = selected;
        auto root = layoutControl(makeTabs({tab}), theme);
        CHECK(findNodeByKey(root, "tab")->commonStyle().foreground == theme.colors.disabledContent);
        for (const auto color : {Color::transparent(), Color{0, 0, 0, 255}}) {
            tab.styleOverrides.foreground = color;
            tab.styleOverrides.padding = EdgeInsets{};
            root = layoutControl(makeTabs({tab}), theme);
            CHECK(findNodeByKey(root, "tab")->commonStyle().foreground == color);
            CHECK(findNodeByKey(root, "tab")->padding == EdgeInsets{});
        }
    }
}

TEST_CASE("dropdown_options_and_shadow_inherit_anchor_theme", "[visual][regression]") {
    accessibility::AccessibilitySettings settings;
    settings.fontScale = 1.5F;
    auto local = style::Theme::fromSettings(settings, false);
    local.elevation.shadowColor = Color{180, 30, 90, 255};
    app::ShellConfig config;
    config.build = [&] {
        return makeThemeScope(makeColumn({makeDropdown("A", "open", "dd", 160)}), &local);
    };
    app::AppShell shell(config);
    (void)shell.renderFrame();
    std::vector<widgets::DropdownController::Option> options{{"A", "A"}};
    for (int i = 0; i < 20; ++i) {
        options.push_back({std::to_string(i), "Option"});
    }
    widgets::DropdownController controller(options, "A");
    controller.open(shell, "dd", &local);
    for (int refresh = 0; refresh < 2; ++refresh) {
        (void)shell.renderFrame();
        const auto* option = findNodeByKey(*shell.overlayRoot(), "dd-opt-2");
        REQUIRE(option != nullptr);
        CHECK(option->commonStyle().foreground == local.button.ghost.content);
        CHECK(option->textStyle().fontSize == local.typography.label.fontSize);
        CHECK(option->size.height == local.metrics.minHeight[1]);
        const auto* menu = findNodeByKey(*shell.overlayRoot(), "dd-menu");
        REQUIRE(menu != nullptr);
        CHECK(menu->shadowColor.r == local.elevation.shadowColor.r);
        CHECK(menu->shadowColor.g == local.elevation.shadowColor.g);
        REQUIRE(controller.handleKey(shell, Key::Down));
    }
}

TEST_CASE("scrollbar_token_changes_damage_and_follow_subtree_opacity", "[visual][regression]") {
    auto content = makeText("content");
    content.height = 400;
    const auto before = layoutControl(withScrollbar(makeScrollView(content, "scroll", 120, 100)),
                                      style::Theme::dark());
    for (int change = 0; change < 3; ++change) {
        auto after = before;
        if (change == 0) after.scrollbarColor = Color{255, 0, 0, 255};
        if (change == 1) after.scrollbarThumbWidth = 7;
        if (change == 2) after.scrollbarMinLength = 50;
        std::vector<Rect> damage;
        REQUIRE(collectDamage(before, after, damage));
        REQUIRE_FALSE(damage.empty());
        render::CpuRenderer partial, full;
        const Size viewport{120, 100};
        partial.submit(render::recordScene(before), render::FrameInfo{viewport});
        partial.submit(render::recordScene(after),
                       render::FrameInfo{viewport, damageBounds(damage, viewport), true});
        full.submit(render::recordScene(after), render::FrameInfo{viewport});
        CHECK(partial.pixels() == full.pixels());
    }
    auto root = before;
    root.transitionAlpha = 0.5F;
    for (const float parentAlpha : {1.0F, 0.5F}) {
        RenderNode parent;
        parent.size = root.size;
        parent.transitionAlpha = parentAlpha;
        parent.children.push_back(root);
        const auto commands = render::recordScene(parent);
        bool sawThumb = false;
        for (const auto& command : commands.commands()) {
            if (command.type == render::CommandType::DrawRect &&
                command.rect.size.width == root.scrollbarThumbWidth) {
                sawThumb = true;
                CHECK(command.color.a == (parentAlpha == 1.0F ? 128 : 64));
            }
        }
        REQUIRE(sawThumb);
    }
    root.transitionAlpha = 0;
    CHECK(render::recordScene(root).commands().empty());
}

TEST_CASE("gallery_state_matrix_wraps_with_window_and_font_scale", "[visual][regression]") {
    for (const float width : {600.0F, 800.0F, 1024.0F}) {
        for (const float scale : {1.0F, 1.5F, 2.0F}) {
            CAPTURE(width, scale);
            examples::GalleryApp app;
            app.setView({width, 900});
            (void)app.renderFrame();
            REQUIRE(app.showSample("buttons"));
            accessibility::AccessibilitySettings settings;
            settings.fontScale = scale;
            app.setAccessibilitySettings(settings);
            (void)app.renderFrame();
            const auto* matrix = findNodeByKey(app.root(), "buttons-matrix");
            REQUIRE(matrix != nullptr);
            const Offset origin = absoluteOffset(app.root(), "buttons-matrix");
            for (const auto* variant : {"Filled", "Tonal", "Outline", "Ghost", "Danger"}) {
                for (const auto* state : {"Normal", "Hover", "Press", "Focus", "Foc+Prs", "Disabled"}) {
                    const auto key = std::string("matrix-") + variant + "-" + state;
                    const auto* cell = findNodeByKey(app.root(), key);
                    REQUIRE(cell != nullptr);
                    const Offset position = absoluteOffset(app.root(), key);
                    CHECK(position.x >= origin.x - 0.01F);
                    CHECK(position.x + cell->size.width <= origin.x + matrix->size.width + 0.01F);
                    CHECK(position.x + cell->size.width <= width);
                }
            }
        }
    }
}

TEST_CASE("visual_transitions_reverse_cancel_callbacks_and_retire", "[visual][regression]") {
    app::ShellConfig config;
    config.caretBlink = false;
    config.build = [] { return makeColumn({withKey(makeButton("dialog"), "card")}); };
    app::AppShell shell(config);
    (void)shell.renderFrame();
    int stale = 0, current = 0;
    shell.beginTransition({"card", 1, 0, 200, Easing::Linear,
        [&](app::AppShell&) { ++stale; }});
    shell.tick(80);
    (void)shell.renderFrame();
    const float visible = findNodeByKey(shell.root(), "card")->transitionAlpha;
    shell.beginTransition({"card", 0, 1, 200, Easing::Linear,
        [&](app::AppShell&) { ++current; }});
    (void)shell.renderFrame();
    CHECK(findNodeByKey(shell.root(), "card")->transitionAlpha == Approx(visible));
    shell.tick(300);
    (void)shell.renderFrame();
    shell.tick(301);
    CHECK(stale == 0);
    CHECK(current == 1);
    CHECK_FALSE(shell.animationsActive());
}

TEST_CASE("visual_state_motion_tracks_checked_and_theme_changes", "[visual][regression]") {
    bool checked = false, enabled = true;
    app::ShellConfig config;
    config.caretBlink = false;
    config.motionTransitions = true;
    config.build = [&] {
        auto control = makeSwitch("Wrap label", "");
        control.key = "switch";
        control.checked = checked;
        control.enabled = enabled;
        return makeColumn({control});
    };
    app::AppShell shell(config);
    const auto part = [&]() -> const SwitchResolvedStyle& {
        return std::get<SwitchResolvedStyle>(findNodeByKey(shell.root(), "switch")->style.component);
    };
    (void)shell.renderFrame();
    shell.tick(10);
    checked = true;
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK(part().knobPosition == 0);
    shell.tick(60);
    (void)shell.renderFrame();
    CHECK(part().knobPosition == Approx(0.75F));
    CHECK(shell.animationsActive());
    checked = false;
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK(part().knobPosition == Approx(0.75F));
    shell.setTheme(style::Theme::light());
    (void)shell.renderFrame();
    CHECK(part().knobPosition == 0);
    CHECK(part().trackOff == shell.theme().switchControl.trackOff);
    shell.tick(300);
    (void)shell.renderFrame();
    CHECK_FALSE(shell.animationsActive());
    checked = true;
    enabled = false;
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK(part().knobPosition == 1);
    CHECK(part().trackOn == shell.theme().colors.disabledBackground);
}

TEST_CASE("visual_tabs_wrap_and_choices_grow_for_long_labels", "[visual][regression]") {
    const auto theme = style::Theme::dark();
    const auto tabs = layoutControl(makeTabs({makeButton("First tab"),
        makeButton("Second tab"), makeButton("Third tab")}), theme, false, {150, 600});
    REQUIRE(tabs.children.size() == 3);
    CHECK(tabs.children.back().offset.y > tabs.children.front().offset.y);
    for (const auto& child : tabs.children) {
        CHECK(child.offset.x + child.size.width <= tabs.size.width);
        CHECK(child.offset.y + child.size.height <= tabs.size.height);
    }
    for (auto widget : {makeCheckbox("A long label which must wrap over several lines", ""),
                        makeRadio("A long label which must wrap over several lines", ""),
                        makeSwitch("A long label which must wrap over several lines", "")}) {
        const auto wide = layoutControl(widget, theme, false, {600, 600});
        widget.width = 140;
        const auto narrow = layoutControl(widget, theme, false, {600, 600});
        CHECK(narrow.size.height > wide.size.height);
        const auto focused = layoutControl(widget, theme, true, {600, 600});
        CHECK(focused.size == narrow.size);
    }
}

TEST_CASE("visual_dialog_bounds_body_scroll_and_fixed_actions", "[visual][regression]") {
    const auto theme = style::Theme::dark();
    std::vector<Widget> paragraphs;
    for (int i = 0; i < 30; ++i) paragraphs.push_back(makeText("Long dialog body wraps within the card."));
    for (const auto view : {Size{320, 240}, Size{220, 300}, Size{600, 700}}) {
        const auto build = [&](float offset) {
            return layoutControl(widgets::makeDialog(makeColumn(paragraphs),
                withKey(makeButton("Confirm"), "action"), theme, "dismiss", "dialog", view, offset),
                theme, false, view);
        };
        const auto root = build(0);
        const auto* card = findNodeByKey(root, "dialog-card");
        const auto* body = findNodeByKey(root, "dialog-body-scroll");
        const auto* action = findNodeByKey(root, "action");
        REQUIRE(card); REQUIRE(body); REQUIRE(action);
        const auto position = absoluteOffset(root, card->key);
        CHECK(position.x >= 16);
        CHECK(position.y >= 16);
        CHECK(position.x + card->size.width <= view.width - 16 + 0.01F);
        CHECK(position.y + card->size.height <= view.height - 16 + 0.01F);
        CHECK(absoluteOffset(root, "action").y + action->size.height <= view.height - 16);
        REQUIRE(body->scrollExtent > 0);
        const auto scrolled = build(100);
        CHECK(findNodeByKey(scrolled, "dialog-body-scroll")->scrollOffset == Approx(100));
        CHECK(absoluteOffset(scrolled, "action") == absoluteOffset(root, "action"));
    }
}

TEST_CASE("visual_dialog_keeps_icon_only_actions", "[visual][regression]") {
    const auto theme = style::Theme::dark();
    auto iconAction = withIcon(makeButton(""), IconId::Close);
    iconAction.key = "icon-action";
    const auto dialog = widgets::makeDialog(
        makeText("Body"), std::move(iconAction), theme, "dismiss", "icon-dialog",
        Size{320, 240});
    const auto root = layoutControl(dialog, theme, false, {320, 240});
    const auto* action = findNodeByKey(root, "icon-action");
    REQUIRE(action != nullptr);
    CHECK(action->type == WidgetType::Button);
    CHECK(static_cast<IconId>(action->icon) == IconId::Close);
}

TEST_CASE("visual_form_support_preserves_field_identity_and_single_line_space", "[visual][regression]") {
    const auto theme = style::Theme::light();
    const auto build = [&](std::string error) {
        auto field = withKey(makeTextField("value"), "field");
        field.invalid = !error.empty();
        return layoutControl(makeColumn({widgets::makeFormField("Label", field, error, theme, "field"),
                                         withKey(makeButton("Next"), "next")}), theme, false, {260, 600});
    };
    const auto initial = build("");
    const auto error = build("Required");
    const auto longError = build("This long validation explanation wraps to several lines in a narrow form field.");
    CHECK(findNodeByKey(initial, "field-error")->size.height >= 18);
    CHECK(findNodeByKey(initial, "field")->identity == findNodeByKey(error, "field")->identity);
    CHECK(absoluteOffset(initial, "next") == absoluteOffset(error, "next"));
    CHECK(absoluteOffset(longError, "next").y > absoluteOffset(initial, "next").y);
    CHECK(findNodeByKey(error, "field-error")->commonStyle().text.color == theme.colors.errorContent);
}

TEST_CASE("visual_dropdown_wheel_resize_and_theme_stay_in_sync", "[visual][regression]") {
    app::ShellConfig config;
    config.initialView = {320, 280};
    config.caretBlink = false;
    config.build = [] { return makeStack({withStackPosition(makeDropdown("0", "open", "pick", 180), {40, 90})}); };
    int backgroundScroll = 0;
    config.onScrollDrag = [&](const RenderNode*, const RenderNode*, Offset, Offset,
                              ScrollDragPhase, std::uint64_t) { ++backgroundScroll; return true; };
    config.onWheel = [&](const RenderNode&, const RenderNode*, Offset, Offset) { ++backgroundScroll; return true; };
    app::AppShell shell(config);
    std::vector<widgets::DropdownController::Option> options;
    for (int i = 0; i < 30; ++i) options.push_back({std::to_string(i), "Option " + std::to_string(i)});
    widgets::DropdownController dropdown(options, "0");
    (void)shell.renderFrame();
    dropdown.open(shell, "pick");
    (void)shell.renderFrame();
    const auto* viewport = findNodeByKey(*shell.overlayRoot(), "pick-menu-scroll");
    REQUIRE(viewport);
    const auto position = absoluteOffset(*shell.overlayRoot(), viewport->key);
    CHECK(shell.wheel(position + Offset{20, 20}, {0, 80}));
    (void)shell.renderFrame();
    CHECK(findNodeByKey(*shell.overlayRoot(), "pick-menu-scroll")->scrollOffset > 0);
    CHECK(backgroundScroll == 0);
    const float beforeDrag = findNodeByKey(*shell.overlayRoot(), "pick-menu-scroll")->scrollOffset;
    shell.pointerDown(position + Offset{20, 55});
    shell.pointerMove(position + Offset{20, 25});
    shell.pointerUp(position + Offset{20, 25});
    (void)shell.renderFrame();
    CHECK(findNodeByKey(*shell.overlayRoot(), "pick-menu-scroll")->scrollOffset > beforeDrag);
    CHECK(backgroundScroll == 0);
    shell.setView({260, 220});
    shell.setTheme(style::Theme::light());
    (void)shell.renderFrame();
    const auto* menu = findNodeByKey(*shell.overlayRoot(), "pick-menu");
    REQUIRE(menu);
    const auto menuPosition = absoluteOffset(*shell.overlayRoot(), menu->key);
    CHECK(menuPosition.x + menu->size.width <= 252.01F);
    CHECK(menuPosition.y + menu->size.height <= 212.01F);
    CHECK(menu->commonStyle().background == shell.theme().colors.surfaceElevated);
    const auto* option = findNodeByKey(*shell.overlayRoot(), "pick-opt-1");
    REQUIRE(option);
    CHECK(std::get<ButtonResolvedStyle>(option->style.component).reserveIconSpace);
    CHECK(std::get<ButtonResolvedStyle>(option->style.component).alignContentStart);
    dropdown.close(shell);
    CHECK(shell.focus().focusedKey() == "pick");
}

TEST_CASE("visual_tooltip_ports_out_of_clip_flips_and_cancels_on_press", "[visual][regression]") {
    bool present = true;
    app::ShellConfig config;
    config.initialView = {240, 240};
    config.caretBlink = false;
    config.build = [&] {
        auto anchor = withKey(makeButton("Hover"), "anchor");
        anchor.onClick = "noop";
        auto contents = makeColumn({anchor, makeTooltip("Tooltip text", "tip")});
        auto scroll = makeScrollView(std::move(contents), "scroll", 180, 42);
        return present ? makeStack({withStackPosition(std::move(scroll), {20, 190})}) : makeStack({});
    };
    app::AppShell shell(config);
    shell.handlers()["noop"] = [] {};
    shell.registerTooltip("anchor", "tip");
    shell.tick(0);
    (void)shell.renderFrame();
    shell.pointerMove({50, 210});
    shell.tick(10);
    shell.tick(410);
    shell.tick(530);
    const auto partial = shell.renderFrame();
    CHECK(findNodeByKey(shell.root(), "tip")->transitionAlpha == 1);
    CHECK(partial == shell.renderFrame(true));
    // Tooltip surface must be above the anchor, outside its scrolling clip.
    const auto color = shell.theme().tooltip.surface;
    bool above = false;
    for (int y = 100; y < 185; ++y) {
        for (int x = 20; x < 200; ++x) {
            const auto offset = (y * shell.pixels().width + x) * 4;
            const auto& rgba = shell.pixels().rgba;
            above = above || (rgba[offset] == color.r && rgba[offset + 1] == color.g && rgba[offset + 2] == color.b);
        }
    }
    CHECK(above);
    shell.pointerDown({50, 210});
    shell.tick(600);
    (void)shell.renderFrame();
    CHECK(findNodeByKey(shell.root(), "tip")->transitionAlpha == 0);
    CHECK_FALSE(shell.animationWakeMs().has_value());
    shell.pointerUp({50, 210});
    present = false;
    shell.markDirty();
    (void)shell.renderFrame();
    shell.tick(1000);
    CHECK_FALSE(shell.animationsActive());
}

TEST_CASE("visual_disabled_transparent_variants_keep_surface_visible", "[visual][regression]") {
    for (auto variant : {ButtonVariant::Outline, ButtonVariant::Ghost}) {
        auto widget = withEnabled(withVariant(makeButton("Disabled"), variant), false);
        auto node = layoutControl(widget, style::Theme::light());
        CHECK(node.commonStyle().background.a == 0);
        CHECK(node.commonStyle().focusWidth == 0);
    }
}

TEST_CASE("visual_scope_theme_changes_snap_motion_and_refresh_tooltip", "[visual][regression]") {
    auto local = style::Theme::light();
    bool checked = false;
    app::ShellConfig config;
    config.initialView = {320, 240};
    config.caretBlink = false;
    config.motionTransitions = true;
    config.build = [&] {
        auto control = withKey(makeSwitch("Anchor", ""), "anchor");
        control.checked = checked;
        return makeColumn({makeThemeScope(control, &local), makeTooltip("Tip", "tip")});
    };
    app::AppShell shell(config);
    shell.registerTooltip("anchor", "tip");
    shell.tick(0);
    (void)shell.renderFrame();
    checked = true;
    shell.markDirty();
    (void)shell.renderFrame();
    shell.tick(50);
    (void)shell.renderFrame();
    local = style::Theme::dark();
    shell.markDirty();
    (void)shell.renderFrame();
    const auto& part = std::get<SwitchResolvedStyle>(findNodeByKey(shell.root(), "anchor")->style.component);
    CHECK(part.knobPosition == 1);
    CHECK(part.trackOn == local.switchControl.trackOn);
    local = style::Theme::light();
    shell.markDirty();
    (void)shell.renderFrame();
    shell.controller().focusNode(*findNodeByKey(shell.root(), "anchor"));
    shell.tick(60);
    shell.tick(460);
    shell.tick(580);
    const auto hash = shell.renderFrame();
    CHECK(hash == shell.renderFrame(true));
    const auto anchor = findNodeByKey(shell.root(), "anchor");
    const auto& pixels = shell.pixels();
    // 采样点取气泡内部（避开 AA 圆角弧线与边框带：弧上像素是与背景的
    // 混合值，不再等于纯表面色）。
    const int y = int(anchor->size.height + 13);
    const int index = (y * pixels.width + 13) * 4;
    REQUIRE(index + 3 < static_cast<int>(pixels.rgba.size()));
    CHECK(pixels.rgba[index] == local.tooltip.surface.r);
    CHECK(pixels.rgba[index + 1] == local.tooltip.surface.g);
    CHECK(pixels.rgba[index + 2] == local.tooltip.surface.b);
    (void)shell.wheel({20, 20}, {0, 20});
    shell.tick(600);
    (void)shell.renderFrame();
    CHECK(findNodeByKey(shell.root(), "tip")->transitionAlpha == 0);
}

TEST_CASE("visual_damage_covers_removed_shadow_and_alpha", "[visual][regression]") {
    RenderNode before;
    before.identity = "panel";
    before.offset = {40, 40};
    before.size = {80, 60};
    before.elevation = 2;
    before.shadowColor = {0, 0, 0, 120};
    before.shadowOffset = {0, 4};
    before.shadowBlur = 8;
    auto after = before;
    after.elevation = 0;
    after.transitionAlpha = 0;
    std::vector<Rect> damage;
    REQUIRE(collectDamage(before, after, damage));
    const auto bounds = damageBounds(damage, {240, 200});
    REQUIRE(bounds);
    CHECK(bounds->left() < 40);
    CHECK(bounds->right() > 120);
    CHECK(bounds->bottom() > 100);
}
