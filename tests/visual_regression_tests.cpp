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
                    CHECK(command.strokeWidth == Approx(1.5F * command.rect.size.width / 16.0F));
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
                CHECK(adapted.slider.trackActive == accent);
                CHECK(adapted.slider.thumbOutline == accent);
                CHECK(adapted.progressBar.fill == accent);
                CHECK(adapted.tabs.indicator == accent);
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
            const auto* button = findNodeByKey(app.root(), "goto-buttons-button");
            REQUIRE(button != nullptr);
            app.shell().handlers().at(button->onClick)();
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
