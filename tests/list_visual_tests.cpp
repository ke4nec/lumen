// List contract: collection-controls-design §6, §10; visual-system §3.2, §5.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lumen/accessibility/semantics.h"
#include "lumen/accessibility/bridge.h"
#include "lumen/app/app_shell.h"
#include "lumen/layout/layout.h"
#include "lumen/widgets/list.h"

using namespace lumen;
using namespace lumen::core;

namespace {
struct ListFixture {
    widgets::ListController list;
    accessibility::RecordingAccessibilityBridge bridge;
    app::AppShell shell{config()};
    bool enabled{true};
    bool showFocusRing{true};
    std::size_t builds{0};

    app::ShellConfig config() {
        app::ShellConfig result;
        result.initialView = {320, 240};
        result.build = [this] {
            auto view = withEnabled(makeList(&list, "list", std::nullopt, 160.0F, 0.0F), enabled);
            view.showFocusRing = showFocusRing;
            auto after = makeButton("After");
            after.key = "after";
            after.onClick = "after";
            return makeColumn({std::move(view), std::move(after)});
        };
        result.onKey = [this](app::AppShell& target, Key key, KeyModifiers mods, char ch) {
            return target.focus().focusedKey().starts_with("list:item:") &&
                   list.handleKey(key, mods, ch);
        };
        return result;
    }
    ListFixture() {
        shell.setAccessibilityBridge(&bridge);
        list.setItemCount(4);
        list.setItemBuilder([this](std::size_t index) {
            ++builds;
            return makeText("Item " + std::to_string(index));
        });
        list.attach(shell, "list");
    }
    const RenderNode& node(const std::string& key) const {
        const auto* found = findNodeByKey(shell.root(), key);
        REQUIRE(found != nullptr);
        return *found;
    }
    const RenderNode& row(std::size_t index) const {
        return node("list:item:i" + std::to_string(index));
    }
    Offset point(std::size_t index, float x = 290.0F) const {
        const auto& r = row(index);
        return absoluteOffset(shell.root(), r.key) + Offset{x, r.size.height * 0.5F};
    }
    void frame() { static_cast<void>(shell.renderFrame()); }
    void click(std::size_t index) {
        auto p = point(index);
        shell.pointerDown(p);
        shell.pointerUp(p);
        frame();
    }
};
}

// Collection design §10.2: hiding the ring preserves focus and selection.
TEST_CASE("list_focus_ring_can_be_hidden_without_disabling_navigation", "[list-visual]") {
    ListFixture f;
    f.showFocusRing = false;
    f.frame();
    f.click(1);
    CHECK(f.shell.focus().focusedIdentity() == f.row(1).identity);
    CHECK(f.row(1).commonStyle().focusWidth == 0.0F);
    CHECK(f.list.selection().isSelected("i1"));
    const auto semantics = accessibility::buildSemanticsTree(f.shell.root(), {&f.shell.focus()});
    REQUIRE(semantics.find(f.row(1).identity) != nullptr);
    CHECK((semantics.find(f.row(1).identity)->flags & accessibility::kSemanticsFocused) != 0);
    const auto& pixels = f.shell.pixels();
    const auto y = static_cast<int>(absoluteOffset(f.shell.root(), f.row(1).key).y);
    const auto at = (y * pixels.width + 250) * 4;
    CHECK(Color{pixels.rgba[at], pixels.rgba[at + 1], pixels.rgba[at + 2], pixels.rgba[at + 3]} ==
          f.shell.theme().list.selected);
    f.shell.keyDown(Key::Down);
    f.frame();
    CHECK(f.list.selection().currentKey() == "i2");
    CHECK(f.shell.focus().focusedIdentity() == f.row(2).identity);
    CHECK(f.row(2).commonStyle().focusWidth == 0.0F);
    const auto size = f.row(2).size;
    f.showFocusRing = true;
    f.shell.markDirty();
    f.frame();
    CHECK(f.row(2).commonStyle().focusWidth == f.shell.theme().metrics.focusRingWidth);
    CHECK(f.row(2).size == size);
    const auto incremental = f.shell.pixels().rgba;
    static_cast<void>(f.shell.renderFrame(true));
    CHECK(incremental == f.shell.pixels().rgba);
}

TEST_CASE("list_rows_fill_viewport_and_follow_density_tokens", "[list-visual]") {
    for (int density = 0; density < 3; ++density) {
        ListFixture f;
        f.shell.setTheme(style::Theme::dark(static_cast<style::ControlDensity>(density)));
        f.frame();
        const auto& theme = f.shell.theme();
        const auto& view = f.node("list");
        const auto& row = f.row(0);
        CHECK(row.size.width == Catch::Approx(view.size.width - view.padding.horizontal()));
        CHECK(row.size.height == Catch::Approx(theme.metrics.minHeight[density]));
        CHECK(row.commonStyle().padding.left == theme.metrics.controlPaddingX[density]);
        CHECK(row.commonStyle().radius.topLeft == theme.metrics.controlRadius[density]);
        CHECK(row.children.front().offset.y == Catch::Approx(
            (row.size.height - row.children.front().size.height) * 0.5F));
        f.click(0); // Right-side whitespace is part of the row's hit target.
        CHECK(f.list.selection().isSelected("i0"));
    }
}

TEST_CASE("list_real_pointer_states_and_focus_resolve_by_identity", "[list-visual]") {
    ListFixture f;
    f.frame();
    const auto p = f.point(1);
    f.shell.pointerMove(p);
    f.frame();
    CHECK(f.row(1).commonStyle().background == f.shell.theme().list.hovered);
    f.shell.pointerDown(p);
    f.frame();
    CHECK(f.shell.controller().pressedIdentity() == f.row(1).identity);
    CHECK(f.row(1).commonStyle().background == f.shell.theme().list.pressed);
    f.shell.pointerUp(p);
    f.frame();
    CHECK(f.shell.focus().focusedIdentity() == f.row(1).identity);
    CHECK(f.row(1).commonStyle().focusWidth == f.shell.theme().metrics.focusRingWidth);
    CHECK(f.row(1).commonStyle().background == Color{46, 60, 96, 255});
    const auto& part = std::get<ListRowResolvedStyle>(f.row(1).style.component);
    CHECK(part.markerWidth > 0.0F);
    CHECK(part.selectionMarker.a > 0);
}

TEST_CASE("list_keyboard_materializes_target_then_resolves_focus", "[list-visual]") {
    ListFixture f;
    f.list.setItemCount(100);
    f.frame();
    f.list.setCurrentKey("i60", false);
    f.frame();
    CHECK(f.list.selection().currentKey() == "i60");
    CHECK(f.shell.focus().focusedIdentity() == f.row(60).identity);
    CHECK(f.row(60).commonStyle().focusWidth > 0.0F);
    f.shell.keyDown(Key::Down);
    f.frame();
    CHECK(f.list.selection().currentKey() == "i61");
    CHECK(f.shell.focus().focusedIdentity() == f.row(61).identity);
    CHECK(f.row(61).commonStyle().focusWidth > 0.0F);
}

TEST_CASE("list_tab_leaves_and_reenters_current_without_selecting", "[list-visual]") {
    ListFixture f;
    f.frame();
    f.shell.keyDown(Key::Tab);
    f.frame();
    CHECK(f.list.selection().currentKey() == "i0");
    CHECK(f.list.selection().selectedCount() == 0);
    f.click(1);
    f.shell.keyDown(Key::Tab);
    f.frame();
    CHECK(f.shell.focus().focusedKey() == "after");
    CHECK(f.list.selection().currentKey() == "i1");
    f.shell.keyDown(Key::Tab, kModifierShift);
    f.frame();
    CHECK(f.shell.focus().focusedIdentity() == f.row(1).identity);
    CHECK(f.list.selection().currentKey() == "i1");
    CHECK(f.list.selection().selectedKeys() == std::vector<std::string>{"i1"});
}

TEST_CASE("list_tab_leaves_when_current_scrolled_out_of_materialized_window", "[list-visual]") {
    ListFixture f;
    f.list.setItemCount(100);
    f.frame();
    f.click(0);
    f.list.scrollToKey("i60", widgets::ScrollAlignment::Start);
    f.frame();
    CHECK(findNodeByKey(f.shell.root(), "list:item:i0") == nullptr);
    f.shell.keyDown(Key::Tab);
    f.frame();
    CHECK(f.shell.focus().focusedKey() == "after");
    f.shell.keyDown(Key::Tab, kModifierShift);
    f.frame();
    CHECK(f.list.selection().currentKey() == "i60");
    CHECK(f.shell.focus().focusedIdentity() == f.row(60).identity);
}

TEST_CASE("list_disabled_metadata_blocks_pointer_keyboard_and_bulk_selection", "[list-visual]") {
    ListFixture f;
    f.list.setItemCount(1000);
    f.list.setEnabledOf([](std::size_t index) { return index % 2 != 0; });
    int activated = 0;
    f.list.onActivated = [&](const std::string&) { ++activated; };
    f.frame();
    const auto& row = f.row(0);
    CHECK_FALSE(row.enabled);
    CHECK_FALSE(row.children.front().enabled);
    CHECK(row.children.front().textStyle().color == f.shell.theme().list.disabledContent);
    const std::string disabledIdentity = row.identity;
    CHECK(f.shell.performAccessibilityAction(disabledIdentity, accessibility::kActionActivate) ==
          accessibility::SemanticsActionStatus::NotHandled);
    CHECK(f.shell.performAccessibilityAction(disabledIdentity, accessibility::kActionFocus) ==
          accessibility::SemanticsActionStatus::NotHandled);
    f.click(0);
    CHECK(f.list.selection().selectedCount() == 0);
    f.shell.keyDown(Key::Tab);
    f.frame();
    CHECK(f.list.selection().currentKey() == "i1");
    f.shell.keyDown(Key::Down);
    f.frame();
    CHECK(f.list.selection().currentKey() == "i3");
    const auto built = f.builds;
    CHECK(f.list.handleKey(Key::None, kModifierCtrl, 'a'));
    CHECK(f.list.selection().selectedCount() == 500);
    CHECK(f.builds == built); // Availability never materializes the 1000 rows.
    CHECK_FALSE(f.list.selection().isSelected("i50"));
    f.list.setCurrentKey("i50", false);
    CHECK(f.list.selection().currentKey() == "i3");
    CHECK(activated == 0);
    CHECK(f.shell.performAccessibilityAction(f.row(3).identity, accessibility::kActionActivate) ==
          accessibility::SemanticsActionStatus::Handled);
    CHECK(activated == 1);
    const std::string focusIdentity = f.row(1).identity;
    CHECK(f.shell.performAccessibilityAction(focusIdentity, accessibility::kActionFocus) ==
          accessibility::SemanticsActionStatus::Handled);
    CHECK(f.list.selection().currentKey() == "i1");
    CHECK(f.list.selection().selectedCount() == 500);
}

TEST_CASE("list_scrolled_rows_preserve_border_and_reject_border_clicks", "[list-visual]") {
    ListFixture f;
    f.list.setItemCount(100);
    f.list.selection().setSelected({"i0"});
    f.frame();
    f.list.scroll().scrollTo(7.0F);
    f.shell.markDirty();
    f.frame();
    const auto& pixels = f.shell.pixels();
    const auto colorAt = [&](int x, int y) {
        const auto at = (y * pixels.width + x) * 4;
        return Color{pixels.rgba[at], pixels.rgba[at + 1], pixels.rgba[at + 2], pixels.rgba[at + 3]};
    };
    CHECK(colorAt(120, 0) == f.shell.theme().list.separator);
    CHECK(colorAt(120, 1) == f.shell.theme().list.selected);
    f.list.selection().setSelected({});
    f.frame();
    f.shell.pointerDown({120, 0.5F});
    f.shell.pointerUp({120, 0.5F});
    f.frame();
    CHECK(f.list.selection().selectedCount() == 0);
    CHECK(f.shell.controller().pressedIdentity().empty());
}

TEST_CASE("list_disabled_content_root_and_disabled_view_disable_rows", "[list-visual]") {
    ListFixture f;
    f.list.setItemBuilder([](std::size_t) { return withEnabled(makeText("Disabled"), false); });
    f.frame();
    CHECK_FALSE(f.row(0).enabled);
    f.click(0);
    CHECK(f.list.selection().selectedCount() == 0);
    f.shell.keyDown(Key::Tab);
    f.frame();
    CHECK(f.shell.focus().focusedKey() == "after");
    f.list.setItemBuilder([](std::size_t) { return makeText("Row"); });
    f.enabled = false;
    f.shell.markDirty();
    f.frame();
    CHECK_FALSE(f.row(0).enabled);
    CHECK_FALSE(f.row(0).children.front().enabled);
}

TEST_CASE("list_empty_builder_is_automatic_and_default_uses_theme", "[list-visual]") {
    ListFixture f;
    f.list.setItemCount(0);
    f.frame();
    const auto& empty = f.node("list:empty");
    CHECK(empty.size.height >= 2 * f.shell.theme().metrics.minHeight[1]);
    REQUIRE(empty.children.size() == 2);
    CHECK(empty.children.front().type == WidgetType::Icon);
    CHECK(empty.children.back().textStyle().color == f.shell.theme().list.emptyContent);
    int calls = 0;
    f.list.setEmptyBuilder([&] {
        ++calls;
        return withKey(makeText("No matching files"), "custom-empty");
    });
    f.frame();
    CHECK(calls == 1);
    CHECK(f.node("custom-empty").text == "No matching files");
    CHECK(f.node("list").scrollExtent == 0.0F);
    f.list.setItemCount(1);
    f.frame();
    CHECK(findNodeByKey(f.shell.root(), "list:empty") == nullptr);
    CHECK(f.row(0).enabled);
}

TEST_CASE("list_theme_scope_font_scale_and_variable_height_remain_consistent", "[list-visual]") {
    widgets::ListController list;
    list.setItemCount(2);
    list.setItemBuilder([](std::size_t i) {
        auto text = makeText(i == 0 ? "Short" : "First line\nSecond line\nThird line");
        return text;
    });
    accessibility::AccessibilitySettings settings;
    settings.fontScale = 2.0F;
    settings.highContrast = true;
    auto local = style::Theme::fromSettings(settings, false, style::ControlDensity::Touch);
    local.list.selected = {12, 34, 56, 255};
    local.list.content = {12, 200, 120, 255};
    list.selection().setSelected({"i0"});
    auto scoped = makeThemeScope(makeList(&list, "scoped", 400.0F, 400.0F), &local);
    const auto tree = layout::LayoutEngine::layout(scoped, Constraints::tight({400, 400}));
    const auto* row = findNodeByKey(tree, "list:item:i0");
    REQUIRE(row != nullptr);
    CHECK(row->commonStyle().padding.left == 32.0F);
    CHECK(row->size.height >= 96.0F);
    CHECK(row->commonStyle().background == local.list.selected);
    CHECK(row->children.front().textStyle().color == local.list.content);
    CHECK(std::get<ListRowResolvedStyle>(row->style.component).markerWidth > 0.0F);
}

TEST_CASE("list_separators_last_row_and_partial_damage_match_full_frames", "[list-visual]") {
    ListFixture f;
    f.list.setItemCount(3);
    f.frame();
    CHECK(std::get<ListRowResolvedStyle>(f.row(0).style.component).separatorWidth == 1.0F);
    CHECK(std::get<ListRowResolvedStyle>(f.row(2).style.component).separatorWidth == 0.0F);
    for (int step = 0; step < 4; ++step) {
        if (step == 0) f.shell.pointerMove(f.point(0));
        if (step == 1) f.shell.pointerDown(f.point(0));
        if (step == 2) f.shell.pointerUp(f.point(0));
        if (step == 3) f.list.setCurrentKey("i2", false);
        f.frame();
        const auto incremental = f.shell.pixels().rgba;
        static_cast<void>(f.shell.renderFrame(true));
        CHECK(incremental == f.shell.pixels().rgba);
    }
    auto style = f.row(2).style;
    const auto before = std::get<ListRowResolvedStyle>(style.component);
    scaleStyleColors(style, 0.5F);
    const auto faded = std::get<ListRowResolvedStyle>(style.component);
    CHECK(faded.selectionMarker.a == scaleColorAlpha(before.selectionMarker, 0.5F).a);
    CHECK(faded.separator.a == scaleColorAlpha(before.separator, 0.5F).a);
}

TEST_CASE("list_scroll_alignment_uses_inside_border_viewport", "[list-visual]") {
    ListFixture f;
    f.list.setItemCount(100);
    f.frame();
    using Alignment = widgets::ScrollAlignment;
    for (const auto align : {Alignment::Start, Alignment::Center, Alignment::End}) {
        f.list.scrollToKey("i50", align);
        f.frame();
        const auto& view = f.node("list");
        const auto& row = f.row(50);
        const float inner = view.size.height - view.padding.vertical();
        const float expected = view.padding.top + (align == Alignment::Start ? 0.0F
            : align == Alignment::Center ? (inner - row.size.height) * 0.5F
            : inner - row.size.height);
        CHECK(row.offset.y == Catch::Approx(expected));
    }
    f.list.scrollToKey("i99", Alignment::End);
    f.frame();
    CHECK(f.row(99).offset.y + f.row(99).size.height ==
          Catch::Approx(f.node("list").size.height - f.node("list").padding.bottom));
    const float offset = f.list.scroll().offset();
    f.list.scrollToKey("i99", Alignment::Visible);
    CHECK(f.list.scroll().offset() == offset);
}

TEST_CASE("list_disabled_selection_retains_marker_and_shift_skips_disabled", "[list-visual]") {
    ListFixture f;
    f.list.setItemCount(6);
    f.list.setEnabledOf([](std::size_t i) { return i % 2 == 0; });
    f.frame();
    f.list.setCurrentKey("i0", false);
    f.list.setCurrentKey("i4", true);
    CHECK(f.list.selection().selectedKeys() == std::vector<std::string>{"i0", "i2", "i4"});
    f.list.scrollToKey("i0", widgets::ScrollAlignment::Start);
    f.list.setEnabledOf([](std::size_t) { return false; });
    f.frame();
    const auto& row = f.row(0);
    CHECK_FALSE(row.enabled);
    const auto& style = std::get<ListRowResolvedStyle>(row.style.component);
    CHECK(style.markerWidth > 0.0F);
    CHECK(style.selectionMarker == f.shell.theme().list.disabledContent);
    CHECK(row.commonStyle().focusWidth == 0.0F);
    f.enabled = false;
    f.shell.markDirty();
    f.frame();
    CHECK_FALSE(f.list.handleKey(Key::Down, kModifierNone));
}
