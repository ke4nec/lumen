// Tree: collection-controls-design §7/§9.3/§10; visual-system §3.2/§5.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <set>

#include "lumen/accessibility/bridge.h"
#include "lumen/layout/layout.h"
#include "lumen/widgets/tree.h"

using namespace lumen;
using namespace lumen::core;

namespace {
struct TreeModel final : widgets::TreeModel {
    std::size_t extra{0};
    mutable std::size_t builds{0}, queries{0};
    std::set<std::string> disabled;
    bool disableContent{false};
    std::size_t childCount(const std::string& key) const override {
        ++queries;
        return key.empty() ? 2 + extra : key == "root" ? 2 : key == "branch" ? 1 : 0;
    }
    std::string childAt(const std::string& key, std::size_t i) const override {
        if (key.empty()) return i == 0 ? "root" : i == 1 ? "last" : "extra" + std::to_string(i);
        return key == "root" ? (i == 0 ? "branch" : "file") : "leaf";
    }
    bool hasChildren(const std::string& key) const override { return key == "root" || key == "branch"; }
    bool isEnabled(const std::string& key) const override { return !disabled.contains(key); }
    Widget buildRow(const std::string& key, std::size_t) const override {
        ++builds;
        return withEnabled(makeText(key), !disableContent);
    }
};

struct TreeFixture {
    TreeModel model;
    widgets::TreeController tree;
    accessibility::RecordingAccessibilityBridge bridge;
    app::AppShell shell{config()};
    bool enabled{true};
    bool showFocusRing{true};
    ControlSize size{ControlSize::Medium};
    int activations{0};
    app::ShellConfig config() {
        app::ShellConfig c;
        c.initialView = {320, 280};
        c.build = [this] {
            auto view = withEnabled(makeTree(&tree, "tree", std::nullopt, 200.0F, 0.0F), enabled);
            view.controlSize = size;
            view.showFocusRing = showFocusRing;
            auto after = withKey(makeButton("After"), "after");
            after.onClick = "after";
            return makeColumn({std::move(view), std::move(after)});
        };
        c.onKey = [this](app::AppShell& target, Key key, KeyModifiers mods, char ch) {
            return target.focus().focusedKey().starts_with("tree:item:") && tree.handleKey(key, mods, ch);
        };
        return c;
    }
    TreeFixture() {
        shell.setAccessibilityBridge(&bridge);
        tree.setModel(&model);
        tree.attach(shell, "tree");
        tree.onActivated = [this](const std::string&) { ++activations; };
    }
    const RenderNode& node(const std::string& key) const {
        const auto* n = findNodeByKey(shell.root(), key);
        REQUIRE(n != nullptr);
        return *n;
    }
    const RenderNode& row(const std::string& key) const { return node("tree:item:" + key); }
    Offset point(const std::string& key) const {
        return absoluteOffset(shell.root(), row(key).key) + Offset{290, row(key).size.height * 0.5F};
    }
    void frame() { static_cast<void>(shell.renderFrame()); }
    void click(const std::string& key) {
        const auto p = point(key);
        shell.pointerDown(p); shell.pointerUp(p); frame();
    }
    Color pixel(int x, int y) const {
        const auto& p = shell.pixels();
        const auto at = (y * p.width + x) * 4;
        return {p.rgba[at], p.rgba[at+1], p.rgba[at+2], p.rgba[at+3]};
    }
};
}

// Collection design §10.2: the viewport applies the setting to rows/arrows.
TEST_CASE("tree_focus_ring_setting_preserves_semantics_and_collapse_focus", "[tree-visual]") {
    TreeFixture f;
    f.showFocusRing = false;
    f.tree.expand("root");
    f.frame();
    f.click("branch");
    CHECK(f.row("branch").commonStyle().focusWidth == 0.0F);
    CHECK(f.shell.focus().focusedIdentity() == f.row("branch").identity);
    CHECK(f.tree.selection().isSelected("branch"));
    const auto id = f.node("tree:chev:branch").identity;
    CHECK(f.shell.performAccessibilityAction(id, accessibility::kActionFocus) ==
          accessibility::SemanticsActionStatus::Handled);
    f.frame();
    CHECK(f.shell.focus().focusedIdentity() == f.node("tree:chev:branch").identity);
    CHECK(f.node("tree:chev:branch").commonStyle().focusWidth == 0.0F);
    f.tree.setCurrentKey("branch");
    f.frame();
    f.shell.keyDown(Key::Right);
    f.frame();
    CHECK(f.tree.isExpanded("branch"));
    f.tree.setCurrentKey("leaf");
    f.frame();
    f.tree.collapse("branch");
    f.frame();
    CHECK(f.tree.selection().currentKey() == "branch");
    CHECK(f.shell.focus().focusedIdentity() == f.row("branch").identity);
    CHECK(f.row("branch").commonStyle().focusWidth == 0.0F);
    const auto geometry = f.row("branch").size;
    f.showFocusRing = true;
    f.shell.markDirty();
    f.frame();
    CHECK(f.row("branch").commonStyle().focusWidth == f.shell.theme().metrics.focusRingWidth);
    CHECK(f.row("branch").size == geometry);
    const auto incremental = f.shell.pixels().rgba;
    static_cast<void>(f.shell.renderFrame(true));
    CHECK(incremental == f.shell.pixels().rgba);
}

TEST_CASE("tree_full_row_geometry_follows_density_and_indents_chevrons", "[tree-visual]") {
    for (int d=0; d<3; ++d) {
        TreeFixture f;
        f.shell.setTheme(style::Theme::dark(static_cast<style::ControlDensity>(d)));
        f.tree.expand("root"); f.tree.expand("branch"); f.frame();
        const auto& theme = f.shell.theme();
        CHECK(f.row("root").size.width == Catch::Approx(318));
        CHECK(f.row("branch").size.width == f.row("root").size.width);
        CHECK(f.row("root").size.height == theme.metrics.minHeight[d]);
        CHECK(f.row("root").padding.left == theme.metrics.controlPaddingX[d]);
        CHECK(f.row("root").commonStyle().radius.topLeft == theme.metrics.controlRadius[d]);
        const auto& a = f.node("tree:chev:root");
        const auto& b = f.node("tree:chev:branch");
        CHECK(b.offset.x - a.offset.x == theme.tree.indentStep);
        CHECK(a.size == Size{24,24});
        CHECK(a.commonStyle().foreground == theme.tree.chevronContent);
        CHECK(f.row("leaf").children.front().size.width == a.size.width);
        f.click("root");
        CHECK(f.tree.selection().isSelected("root"));
    }
}

TEST_CASE("tree_selected_marker_focus_and_pointer_states_match_design", "[tree-visual]") {
    TreeFixture f; f.frame();
    const auto p = f.point("root");
    f.shell.pointerMove(p); f.frame();
    CHECK(f.row("root").commonStyle().background == f.shell.theme().tree.row.hovered);
    f.shell.pointerDown(p); f.frame();
    CHECK(f.row("root").commonStyle().background == f.shell.theme().tree.row.pressed);
    f.shell.pointerUp(p); f.frame();
    CHECK(f.row("root").commonStyle().background == Color{46,60,96,255});
    CHECK(f.row("root").commonStyle().focusWidth == 2);
    CHECK(f.shell.focus().focusedIdentity() == f.row("root").identity);
    CHECK(f.pixel(250,20) == Color{46,60,96,255}); // Far beyond the label.
    CHECK(f.pixel(4,20) == f.shell.theme().tree.row.selectionMarker);
    CHECK(f.pixel(150,1) == f.shell.theme().colors.focusRing);
    f.shell.keyDown(Key::Tab); f.frame();
    CHECK(f.shell.focus().focusedKey() == "after");
    CHECK(f.row("root").commonStyle().focusWidth == 0);
    CHECK(f.pixel(2,20) == f.shell.theme().tree.row.selectionMarker);
    CHECK(f.pixel(250,20) == Color{46,60,96,255});
}

TEST_CASE("tree_chevron_is_independent_and_collapse_recovers_hidden_focus", "[tree-visual]") {
    TreeFixture f; f.tree.expand("root"); f.tree.expand("branch"); f.frame();
    f.tree.setCurrentKey("leaf"); f.frame();
    const auto selected = f.tree.selection().selectedKeys();
    const auto& chev = f.node("tree:chev:branch");
    const auto p = absoluteOffset(f.shell.root(), chev.key) + Offset{12,12};
    f.shell.pointerDown(p); f.shell.pointerUp(p); f.frame();
    CHECK_FALSE(f.tree.isExpanded("branch"));
    CHECK(f.tree.selection().selectedKeys() == selected);
    CHECK(f.tree.selection().currentKey() == "branch");
    CHECK(f.shell.focus().focusedIdentity() == f.row("branch").identity);
    f.shell.keyDown(Key::Right); f.frame();
    CHECK(f.tree.isExpanded("branch"));
    f.shell.keyDown(Key::Right); f.frame();
    CHECK(f.tree.selection().currentKey() == "leaf");
    f.tree.collapseAll(); f.frame();
    CHECK(f.tree.selection().currentKey() == "root");
    CHECK(f.shell.focus().focusedIdentity() == f.row("root").identity);
}

TEST_CASE("tree_tab_and_semantics_keep_current_separate_from_selection", "[tree-visual]") {
    TreeFixture f; f.frame();
    f.shell.keyDown(Key::Tab); f.frame();
    CHECK(f.tree.selection().currentKey() == "root");
    CHECK(f.tree.selection().selectedCount() == 0);
    f.shell.keyDown(Key::Tab); f.frame();
    CHECK(f.shell.focus().focusedKey() == "after");
    f.shell.keyDown(Key::Tab, kModifierShift); f.frame();
    CHECK(f.shell.focus().focusedIdentity() == f.row("root").identity);
    auto id = f.row("root").identity;
    CHECK(f.shell.performAccessibilityAction(id, accessibility::kActionExpand) == accessibility::SemanticsActionStatus::Handled);
    f.frame();
    CHECK(f.tree.isExpanded("root"));
    CHECK(f.tree.selection().selectedCount() == 0);
    const auto semantics = accessibility::buildSemanticsTree(f.shell.root());
    CHECK(semantics.find(f.row("root").identity)->value == "true");
    CHECK((semantics.find(f.row("root").identity)->actions & accessibility::kActionCollapse) != 0);
    id = f.row("root").identity;
    CHECK(f.shell.performAccessibilityAction(id, accessibility::kActionActivate) == accessibility::SemanticsActionStatus::Handled);
    CHECK(f.activations == 1);
    CHECK(f.tree.selection().selectedCount() == 0);
    CHECK(f.shell.performAccessibilityAction(id, accessibility::kActionCollapse) == accessibility::SemanticsActionStatus::Handled);
    f.frame();
    CHECK_FALSE(f.tree.isExpanded("root"));
    f.shell.keyDown(Key::Enter); f.frame();
    CHECK(f.activations == 2);
    const auto selected = f.tree.selection().selectedKeys();
    id = f.node("tree:chev:root").identity;
    CHECK(f.shell.performAccessibilityAction(id, accessibility::kActionActivate) == accessibility::SemanticsActionStatus::Handled);
    f.frame();
    CHECK(f.tree.isExpanded("root"));
    CHECK(f.tree.selection().selectedKeys() == selected);
}

TEST_CASE("tree_control_size_and_leaf_alignment_are_stable", "[tree-visual]") {
    TreeFixture f; f.size = ControlSize::Small; f.tree.expand("root"); f.frame();
    CHECK(f.row("root").size.height == 32);
    CHECK(f.row("root").padding.left == 8);
    CHECK(f.row("branch").children.back().offset.x == f.row("file").children.back().offset.x);
    f.size = ControlSize::Large; f.shell.markDirty(); f.frame();
    CHECK(f.row("root").size.height == 48);
    CHECK(f.row("root").padding.left == 16);
    CHECK(f.node("tree:chev:root").size == Size{24,24});
}

TEST_CASE("tree_disabled_range_selection_and_double_click_activation", "[tree-visual]") {
    TreeFixture f; f.tree.expand("root"); f.tree.expand("branch");
    f.tree.setSelectionMode(widgets::SelectionMode::Extended);
    f.model.disabled.insert("branch"); f.frame();
    f.tree.setCurrentKey("root"); f.tree.setCurrentKey("file", true); f.frame();
    CHECK(f.tree.selection().selectedKeys() == std::vector<std::string>{"root","leaf","file"});
    f.shell.tick(1000); f.click("root");
    f.shell.tick(1100); f.click("root");
    CHECK(f.activations == 1);
}

TEST_CASE("tree_disabled_metadata_and_content_block_all_input_paths", "[tree-visual]") {
    TreeFixture f; f.model.disabled.insert("root");
    f.tree.selection().setSelected({"root"}); f.frame();
    CHECK_FALSE(f.row("root").enabled);
    CHECK_FALSE(f.node("tree:chev:root").enabled);
    CHECK(std::get<ListRowResolvedStyle>(f.row("root").style.component).selectionMarker == f.shell.theme().tree.row.disabledContent);
    f.tree.selection().clear(); f.click("root");
    CHECK(f.tree.selection().selectedCount() == 0);
    auto id = f.row("root").identity;
    CHECK(f.shell.performAccessibilityAction(id, accessibility::kActionExpand) == accessibility::SemanticsActionStatus::NotHandled);
    CHECK(f.shell.performAccessibilityAction(id, accessibility::kActionActivate) == accessibility::SemanticsActionStatus::NotHandled);
    f.shell.keyDown(Key::Tab); f.frame();
    CHECK(f.tree.selection().currentKey() == "last");
    f.tree.setSelectionMode(widgets::SelectionMode::Extended);
    f.tree.handleKey(Key::None, kModifierCtrl, 'a');
    CHECK(f.tree.selection().selectedKeys() == std::vector<std::string>{"last"});
    f.model.disableContent = true; f.tree.modelChanged(); f.frame();
    CHECK_FALSE(f.row("last").enabled);
    f.model.disableContent = false; f.tree.modelChanged(); f.enabled = false; f.shell.markDirty(); f.frame();
    CHECK_FALSE(f.row("last").enabled);
    CHECK_FALSE(f.tree.handleKey(Key::Down, kModifierNone));
}

TEST_CASE("tree_empty_state_and_custom_placeholder_are_automatic", "[tree-visual]") {
    TreeFixture f; f.tree.setModel(nullptr); f.frame();
    const auto& empty = f.node("tree:empty");
    REQUIRE(empty.children.size() == 2);
    CHECK(empty.children.front().icon == static_cast<std::uint8_t>(IconId::Folder));
    CHECK(empty.children.front().size == Size{24,24});
    CHECK(empty.children.back().textStyle().color == f.shell.theme().tree.row.emptyContent);
    CHECK(empty.size.height >= 80);
    f.size = ControlSize::Small; f.shell.markDirty(); f.frame();
    CHECK(f.node("tree:empty").padding.left == 8);
    f.tree.setEmptyBuilder([] { return withKey(makeText("No matching nodes"), "custom-empty"); }); f.frame();
    CHECK(f.node("custom-empty").text == "No matching nodes");
    f.tree.setModel(&f.model); f.frame();
    CHECK(findNodeByKey(f.shell.root(), "tree:empty") == nullptr);
}

TEST_CASE("tree_theme_scope_scales_geometry_once_and_preserves_overrides", "[tree-visual]") {
    TreeModel model; widgets::TreeController controller; controller.setModel(&model); controller.expand("root");
    controller.selection().setSelected({"branch"});
    accessibility::AccessibilitySettings settings; settings.fontScale = 2; settings.highContrast = true;
    auto theme = style::Theme::fromSettings(settings, false, style::ControlDensity::Touch);
    theme.tree.row.selected = {12,34,56,255}; theme.tree.row.content = {10,200,100,255};
    auto view = makeThemeScope(makeTree(&controller,"view",400.0F,400.0F), &theme);
    const auto root = layout::LayoutEngine::layout(view, Constraints::tight({400,400}));
    const auto* row = findNodeByKey(root,"tree:item:branch"); REQUIRE(row != nullptr);
    CHECK(row->size.height >= 96);
    CHECK(row->padding.left == 72); // 16 * 2 + depth 1 * 20 * 2.
    CHECK(row->commonStyle().background == theme.tree.row.selected);
    CHECK(row->children.back().textStyle().color == theme.tree.row.content);
    CHECK(row->children.front().size == Size{48,48});
    CHECK(std::get<ListRowResolvedStyle>(row->style.component).markerWidth == 6);
    CHECK(std::get<ButtonResolvedStyle>(row->children.front().style.component).iconSize == 32);
    const auto adapted = style::adaptPlatformTheme(theme, settings, false, Color{200,40,80,255});
    CHECK(adapted.tree.row.selectionMarker == adapted.colors.accent);
    CHECK(adapted.tree.indentStep == 40);
}

TEST_CASE("tree_virtual_navigation_and_expand_all_are_bounded", "[tree-visual]") {
    TreeFixture f; f.model.extra = 10000; f.tree.modelChanged(); f.frame();
    CHECK(f.model.builds < 20);
    const auto queries = f.model.queries;
    CHECK_FALSE(f.tree.expandAll(5));
    CHECK(f.model.queries - queries < 20);
    CHECK_FALSE(f.tree.isExpanded("root"));
    f.tree.setCurrentKey("extra9000"); f.frame();
    CHECK(f.shell.focus().focusedIdentity() == f.row("extra9000").identity);
    CHECK(f.row("extra9000").offset.y + f.row("extra9000").size.height <= 199.01F);
    CHECK(f.model.builds < 40);
    f.shell.keyDown(Key::Tab); f.frame(); CHECK(f.shell.focus().focusedKey() == "after");
}

TEST_CASE("tree_border_separators_and_incremental_frames_remain_consistent", "[tree-visual]") {
    TreeFixture f; f.frame();
    CHECK(std::get<ListRowResolvedStyle>(f.row("root").style.component).separatorWidth == 1);
    CHECK(std::get<ListRowResolvedStyle>(f.row("last").style.component).separatorWidth == 0);
    for (int step=0; step<6; ++step) {
        if (step==0) f.shell.pointerMove(f.point("root"));
        if (step==1) f.shell.pointerDown(f.point("root"));
        if (step==2) f.shell.pointerUp(f.point("root"));
        if (step==3) f.tree.expand("root");
        if (step==4) f.tree.setCurrentKey("branch");
        if (step==5) f.tree.collapse("root");
        f.frame(); const auto incremental = f.shell.pixels().rgba;
        static_cast<void>(f.shell.renderFrame(true));
        CHECK(incremental == f.shell.pixels().rgba);
    }
    f.tree.expand("root"); f.tree.expand("branch"); f.frame();
    f.tree.scroll().scrollTo(7); f.shell.markDirty(); f.frame();
    CHECK(f.pixel(150,0) == f.shell.theme().tree.row.separator);
    f.tree.selection().clear(); f.frame();
    f.shell.pointerDown({150,0.5F}); f.shell.pointerUp({150,0.5F}); f.frame();
    CHECK(f.tree.selection().selectedCount() == 0);
}
