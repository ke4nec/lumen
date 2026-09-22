// Scroll design §4/§5, visual system §7.4: scrollbar chrome owns its hit area.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lumen/app/app_shell.h"
#include "lumen/core/scrollbar.h"
#include "lumen/platform/fake_host.h"
#include "lumen/widgets/list.h"
#include "lumen/widgets/tree.h"
#include "lumen/widgets/dropdown.h"
#include "lumen/widgets/menu.h"

using namespace lumen;
using namespace lumen::core;

namespace {
struct ScrollTree final : widgets::TreeModel {
    std::size_t count{30};
    std::size_t childCount(const std::string& key) const override { return key.empty() ? count : 0; }
    std::string childAt(const std::string&, std::size_t i) const override { return std::to_string(i); }
    bool hasChildren(const std::string&) const override { return false; }
    Widget buildRow(const std::string& key, std::size_t) const override { return makeText(key); }
};

struct ScrollFixture {
    WidgetType kind;
    ScrollAxis axis;
    bool enabled{true};
    bool show{true};
    bool sideSlider{false};
    float contentLength{1000};
    int clicks{0};
    int dragEnds{0};
    int dragCancels{0};
    ScrollController scroll;
    VirtualListController virtualList;
    widgets::ListController list;
    ScrollTree model;
    widgets::TreeController tree;
    widgets::TreeListController table;
    app::AppShell shell;

    explicit ScrollFixture(WidgetType type = WidgetType::ScrollView,
                           ScrollAxis direction = ScrollAxis::Vertical)
        : kind(type), axis(direction), shell(config()) {
        virtualList.setItemCount(30);
        virtualList.setEstimatedExtent(40);
        virtualList.setItemBuilder([](std::size_t) { return makeContainerLeaf(200.0F, 40.0F); });
        list.setItemCount(30);
        list.setItemBuilder([](std::size_t) { return makeText("Item"); });
        tree.setModel(&model);
        table.setModel(&model);
        if (kind == WidgetType::List) list.attach(shell, "scroll");
        if (kind == WidgetType::Tree) tree.attach(shell, "scroll");
        if (kind == WidgetType::TreeList) table.attach(shell, "scroll");
        shell.handlers()["content-click"] = [this] { ++clicks; };
        frame();
    }
    app::ShellConfig config() {
        app::ShellConfig config;
        config.initialView = {240, 240};
        config.build = [this] {
            Widget viewport;
            if (kind == WidgetType::VirtualList) viewport = makeVirtualList(&virtualList, "scroll", 200.0F, 200.0F);
            else if (kind == WidgetType::List) viewport = makeList(&list, "scroll", 200.0F, 200.0F);
            else if (kind == WidgetType::Tree) viewport = makeTree(&tree, "scroll", 200.0F, 200.0F);
            else if (kind == WidgetType::TreeList) viewport = makeTreeList(&table, nullptr, false, "scroll", 200.0F, 200.0F);
            else {
                auto content = makeButton("Covered content");
                content.onClick = "content-click";
                content.width = axis == ScrollAxis::Horizontal ? contentLength : 200;
                content.height = axis == ScrollAxis::Vertical ? contentLength : 200;
                viewport = makeScrollView(std::move(content), "scroll", 200.0F, 200.0F);
                viewport.type = kind;
                viewport.scrollAxis = axis;
                viewport.scrollOffset = scroll.offset();
            }
            viewport.enabled = enabled;
            viewport.showScrollbar = show;
            std::vector<Widget> children{withStackPosition(std::move(viewport), {20, 20})};
            if (sideSlider) {
                Widget slider;
                slider.type = WidgetType::Slider;
                slider.bind = "side-slider";
                slider.width = 20.0F; slider.height = 120.0F;
                children.push_back(withStackPosition(slider, {220, 20}));
            }
            return makeStack(std::move(children));
        };
        config.onWheel = [this](const RenderNode&, const RenderNode* node, Offset, Offset delta) {
            if (!node) return false;
            updateExtents(*node);
            const bool moved = scroll.applyWheel(axis == ScrollAxis::Horizontal ? delta.x : delta.y);
            if (moved) shell.markDirty();
            return moved;
        };
        config.onScrollDrag = [this](const RenderNode*, const RenderNode* node, Offset, Offset delta,
                                    ScrollDragPhase phase, std::uint64_t now) {
            if (phase == ScrollDragPhase::Cancel || phase == ScrollDragPhase::Begin) {
                if (phase == ScrollDragPhase::Cancel) ++dragCancels;
                scroll.cancelDrag();
                return false;
            }
            if (!node) return false;
            updateExtents(*node);
            if (phase == ScrollDragPhase::End) { ++dragEnds; return scroll.endDrag(now); }
            const float amount = axis == ScrollAxis::Horizontal ? delta.x : delta.y;
            scroll.noteDragSample(amount, now);
            const bool moved = scroll.applyDrag(amount);
            if (moved) shell.markDirty();
            return moved;
        };
        return config;
    }
    void updateExtents(const RenderNode& node) {
        const float length = axis == ScrollAxis::Horizontal ? node.size.width : node.size.height;
        scroll.updateExtents(length, length + node.scrollExtent);
    }
    void frame() { static_cast<void>(shell.renderFrame()); }
    const RenderNode& node() const {
        const auto* node = findNodeByKey(shell.root(), "scroll");
        REQUIRE(node != nullptr);
        return *node;
    }
    ScrollController& controller() {
        return node().virtualSource ? *node().virtualSource->scrollController() : scroll;
    }
    ScrollbarGeometry geometry() const {
        const auto result = scrollbarGeometry(node());
        REQUIRE(result.has_value());
        return *result;
    }
    Offset hitPoint() const {
        const auto hit = geometry().thumbHit;
        // Hit the expanded margin, outside the visible thumb.
        return absoluteOffset(shell.root(), "scroll") +
            (axis == ScrollAxis::Horizontal ? Offset{hit.origin.x + hit.size.width * .5F, hit.origin.y + .5F}
                                           : Offset{hit.origin.x + .5F, hit.origin.y + hit.size.height * .5F});
    }
};
}

TEST_CASE("scrollbar_all_viewports_hover_capture_and_drag_in_both_directions", "[scrollbar]") {
    for (const auto kind : {WidgetType::ScrollView, WidgetType::ListView, WidgetType::VirtualList,
                           WidgetType::List, WidgetType::Tree, WidgetType::TreeList}) {
        for (const auto axis : {ScrollAxis::Vertical, ScrollAxis::Horizontal}) {
            if (axis == ScrollAxis::Horizontal && kind != WidgetType::ScrollView && kind != WidgetType::ListView) continue;
            CAPTURE(static_cast<int>(kind), static_cast<int>(axis));
            ScrollFixture app(kind, axis);
            const auto point = app.hitPoint();
            const auto before = app.geometry().thumb;
            app.shell.pointerMove(point);
            CHECK(app.shell.pointerCursor() == PointerCursor::PointingHand);
            app.frame();
            CHECK(app.node().scrollbarThumbWidth == app.shell.theme().scrollbar.activeThumbWidth);
            CHECK(app.node().scrollbarColor == app.shell.theme().scrollbar.hovered);
            CHECK(app.shell.renderFrame() == app.shell.renderFrame(true));
            app.shell.pointerDown(point);
            app.frame();
            CHECK(app.node().scrollbarColor == app.shell.theme().scrollbar.dragged);
            const auto delta = axis == ScrollAxis::Horizontal ? Offset{20, 0} : Offset{0, 20};
            app.shell.tick(20);
            app.shell.pointerMove(point + delta);
            app.frame();
            REQUIRE(app.controller().offset() > 0);
            const auto after = app.geometry().thumb;
            CHECK((axis == ScrollAxis::Horizontal ? after.origin.x - before.origin.x : after.origin.y - before.origin.y)
                  == Catch::Approx(20).margin(.01F));
            const float advanced = app.controller().offset();
            app.shell.pointerMove(point + Offset{delta.x * .5F, delta.y * .5F});
            app.frame();
            CHECK(app.controller().offset() < advanced);
            app.shell.pointerMove({500, 500});
            CHECK(app.shell.pointerCursor() == PointerCursor::PointingHand);
            app.frame();
            CHECK(app.controller().offset() == Catch::Approx(app.controller().maxScrollOffset()));
            app.shell.pointerUp({500, 500});
            CHECK(app.shell.pointerCursor() == PointerCursor::Arrow);
            CHECK_FALSE(app.controller().isFlinging());
            CHECK(app.dragEnds == 0);
            CHECK(app.clicks == 0);
            CHECK(app.list.selection().selectedKeys().empty());
            CHECK(app.tree.selection().selectedKeys().empty());
        }
    }
}

TEST_CASE("scrollbar_track_pages_without_activating_covered_content", "[scrollbar]") {
    for (const auto axis : {ScrollAxis::Vertical, ScrollAxis::Horizontal}) {
        ScrollFixture app(WidgetType::ScrollView, axis);
        const auto origin = absoluteOffset(app.shell.root(), "scroll");
        const auto track = app.geometry().track;
        const auto p = origin + (axis == ScrollAxis::Vertical
            ? Offset{track.origin.x + 1, track.bottom() - 1}
            : Offset{track.right() - 1, track.origin.y + 1});
        app.shell.pointerDown(p);
        app.shell.pointerUp(p);
        app.frame();
        CHECK(app.controller().offset() == Catch::Approx(180));
        CHECK(app.clicks == 0);
        app.sideSlider = true;
        app.shell.state().set("side-slider", "20");
        app.shell.markDirty(); app.frame();
        app.shell.pointerDown(p);
        app.shell.pointerUp({230, 60});
        CHECK(app.shell.state().get("side-slider") == "20");
    }
}

TEST_CASE("scrollbar_disabled_hidden_disappearing_and_cancel_clear_cursor", "[scrollbar]") {
    ScrollFixture app;
    const auto p = app.hitPoint();
    app.enabled = false;
    app.shell.markDirty(); app.frame();
    app.shell.pointerMove(p);
    CHECK(app.shell.pointerCursor() == PointerCursor::Arrow);
    app.shell.pointerDown(p); app.shell.pointerMove(p + Offset{0, 40}); app.shell.pointerUp(p);
    CHECK(app.scroll.offset() == 0);
    CHECK_FALSE(app.shell.wheel(p, {0, 40}));
    app.enabled = true;
    app.show = false;
    app.shell.markDirty(); app.frame();
    app.shell.pointerMove(p);
    CHECK(app.shell.pointerCursor() == PointerCursor::Arrow);
    app.show = true;
    app.shell.markDirty(); app.frame();
    app.shell.pointerDown(p);
    CHECK(app.shell.pointerCursor() == PointerCursor::PointingHand);
    app.shell.pointerCancel();
    CHECK(app.shell.pointerCursor() == PointerCursor::Arrow);
    app.shell.pointerDown(p);
    app.contentLength = 20;
    app.shell.markDirty(); app.frame();
    CHECK_FALSE(scrollbarGeometry(app.node()).has_value());
    CHECK(app.shell.pointerCursor() == PointerCursor::Arrow);
    app.shell.pointerMove({500, 500});
    CHECK(app.scroll.offset() == 0);
}

TEST_CASE("scrollbar_theme_density_scale_and_tiny_geometry_are_bounded", "[scrollbar]") {
    for (const auto density : {style::ControlDensity::Compact, style::ControlDensity::Comfortable, style::ControlDensity::Touch}) {
        ScrollFixture app;
        accessibility::AccessibilitySettings settings;
        settings.fontScale = 2;
        settings.highContrast = true;
        app.shell.setTheme(style::Theme::fromSettings(settings, false, density));
        app.frame();
        CHECK(app.node().scrollbarThickness >= 24);
        CHECK(app.node().scrollbarThumbWidth >= 12);
        const auto p = app.hitPoint();
        app.shell.pointerMove(p); app.frame();
        CHECK(app.node().scrollbarColor == app.shell.theme().scrollbar.hovered);
        auto tiny = app.node();
        tiny.size = {9, 14};
        const auto geometry = scrollbarGeometry(tiny);
        if (geometry) {
            CHECK(geometry->thumb.right() <= 9);
            CHECK(geometry->thumb.bottom() <= 14);
            CHECK(geometry->travel == 0);
        }
    }
}

TEST_CASE("run_app_maps_scrollbar_hand_cursor_and_restores_arrow", "[scrollbar][app]") {
    ScrollFixture app;
    platform::FakeApplicationHost host;
    REQUIRE(host.initialize());
    host.createWindow({});
    const auto id = host.windowIds().front();
    host.pushPointerMove(id, app.hitPoint());
    host.pushPointerDown(id, app.hitPoint());
    host.pushPointerMove(id, {500, 500});
    host.pushPointerUp(id, {500, 500});
    host.pushQuit();
    app::RunOptions options;
    options.windowDesc.width = 240;
    options.windowDesc.height = 240;
    CHECK(app::runApp(app.shell, host, options) == 0);
    REQUIRE(host.cursorCalls.size() >= 2);
    CHECK(host.cursorCalls.front().second == platform::SystemCursor::PointingHand);
    CHECK(host.cursorCalls.back().second == platform::SystemCursor::Arrow);
}

TEST_CASE("scrollbar_dropdown_and_long_menu_drag_without_selecting_or_scrolling_background", "[scrollbar][widgets]") {
    for (const bool menu : {false, true}) {
        CAPTURE(menu);
        int background = 0, selected = 0;
        app::ShellConfig config;
        config.initialView = {320, 240};
        config.build = [] { return makeStack({withStackPosition(makeDropdown("0", "open", "pick", 180.0F), {20, 20})}); };
        config.onWheel = [&](const RenderNode&, const RenderNode*, Offset, Offset) { ++background; return true; };
        config.onScrollDrag = [&](const RenderNode*, const RenderNode*, Offset, Offset, ScrollDragPhase, std::uint64_t) {
            ++background; return true;
        };
        app::AppShell shell(config);
        std::vector<widgets::DropdownController::Option> options;
        widgets::MenuItems items;
        for (int i = 0; i < 30; ++i) {
            options.push_back({std::to_string(i), "Option " + std::to_string(i)});
            items.push_back({.id = std::to_string(i), .label = "Item " + std::to_string(i)});
        }
        widgets::DropdownController dropdown(options, "0");
        widgets::ContextMenuController context;
        dropdown.onSelected = [&](const std::string&) { ++selected; };
        context.onCommand = [&](const std::string&) { ++selected; };
        static_cast<void>(shell.renderFrame());
        if (menu) context.open(shell, {20, 20}, items);
        else dropdown.open(shell, "pick");
        static_cast<void>(shell.renderFrame());
        const auto key = menu ? "ctx:m0:scroll" : "pick-menu-scroll";
        REQUIRE(shell.overlayRoot());
        const auto* viewport = findNodeByKey(*shell.overlayRoot(), key);
        REQUIRE(viewport);
        const auto bar = scrollbarGeometry(*viewport);
        REQUIRE(bar);
        const auto point = absoluteOffset(*shell.overlayRoot(), key) +
            bar->thumbHit.origin + Offset{1, bar->thumbHit.size.height * .5F};
        shell.pointerMove(point);
        CHECK(shell.pointerCursor() == PointerCursor::PointingHand);
        shell.pointerDown(point);
        shell.pointerMove(point + Offset{0, 30});
        static_cast<void>(shell.renderFrame());
        REQUIRE(shell.overlayRoot());
        CHECK(findNodeByKey(*shell.overlayRoot(), key)->scrollOffset > 0);
        shell.pointerUp(point + Offset{0, 30});
        CHECK(selected == 0);
        CHECK(background == 0);
        REQUIRE(shell.overlayRoot());
        // Positive wheel offset advances the same way in both overlay types.
        const float before = findNodeByKey(*shell.overlayRoot(), key)->scrollOffset;
        CHECK(shell.wheel(point, {0, 15}));
        static_cast<void>(shell.renderFrame());
        CHECK(findNodeByKey(*shell.overlayRoot(), key)->scrollOffset == Catch::Approx(before + 15));
        if (menu) context.close(shell); else dropdown.close(shell);
        CHECK(shell.pointerCursor() == PointerCursor::Arrow);
    }
}

TEST_CASE("scrollbar_parent_chrome_wins_over_nested_scrollbar_and_content", "[scrollbar]") {
    ScrollFixture app;
    RenderNode outer = app.node();
    outer.offset = {};
    outer.children = {app.node()};
    outer.children.front().offset = {};
    outer.children.front().identity = "inner";
    const auto bar = scrollbarGeometry(outer);
    REQUIRE(bar);
    std::vector<const RenderNode*> chain;
    const auto* hit = hitTestChain(outer, bar->thumbHit.origin + Offset{1, 5}, chain);
    CHECK(hit == &outer);
    REQUIRE(chain.size() == 1);
}

TEST_CASE("scrollbar_cancelled_release_cannot_edit_an_unpressed_control", "[scrollbar][review]") {
    // Scroll design §5: invalidating a captured thumb also consumes its release.
    for (const bool disappear : {false, true}) {
        ScrollFixture app;
        app.sideSlider = true;
        app.shell.state().set("side-slider", "20");
        app.shell.markDirty(); app.frame();
        app.shell.pointerDown(app.hitPoint());
        if (disappear) {
            app.show = false;
            app.shell.markDirty(); app.frame();
        } else app.shell.pointerCancel();
        app.shell.pointerUp({230, 60});
        CHECK(app.shell.state().get("side-slider") == "20");
    }
}

TEST_CASE("scrollbar_source_capture_cancel_does_not_reach_application_sink", "[scrollbar][review]") {
    for (const auto kind : {WidgetType::VirtualList, WidgetType::List, WidgetType::Tree, WidgetType::TreeList}) {
        ScrollFixture app(kind);
        app.shell.pointerDown(app.hitPoint());
        app.shell.pointerCancel();
        CHECK(app.dragCancels == 0);
    }
    ScrollFixture app;
    app.shell.pointerDown(app.hitPoint());
    app.shell.pointerCancel();
    CHECK(app.dragCancels == 1);
}

TEST_CASE("scrollbar_overlay_change_cancels_the_original_drag_sink", "[scrollbar][review]") {
    ScrollFixture app;
    int overlayCancels = 0;
    app.shell.pointerDown(app.hitPoint());
    app.shell.setOverlayBuilder([]() -> std::optional<Widget> { return makeContainerLeaf(240.0F, 240.0F); }, {},
        [&](const RenderNode*, const RenderNode*, Offset, Offset, ScrollDragPhase phase, std::uint64_t) {
            if (phase == ScrollDragPhase::Cancel) ++overlayCancels;
            return false;
        });
    app.frame();
    CHECK(app.dragCancels == 1);
    CHECK(overlayCancels == 0);
}

TEST_CASE("horizontal_scrollbar_keyboard_rejects_vertical_arrow_keys", "[scrollbar][review]") {
    ScrollFixture app(WidgetType::ScrollView, ScrollAxis::Horizontal);
    app.shell.controller().focusNode(app.node());
    CHECK_FALSE(app.shell.controller().scrollKey(app.shell.root(), Key::Down));
    CHECK(app.scroll.offset() == 0);
    CHECK(app.shell.controller().scrollKey(app.shell.root(), Key::Right));
    CHECK(app.scroll.offset() == 120);
    app.frame();
    CHECK_FALSE(app.shell.controller().scrollKey(app.shell.root(), Key::Up));
    CHECK(app.scroll.offset() == 120);
}

// Scroll design §4: a child viewport owns routing even inside a source list.
TEST_CASE("keyboard_scroll_targets_nearest_viewport_from_child_focus", "[scrollbar][keyboard]") {
    VirtualListController outer;
    ScrollController inner(ScrollAxis::Horizontal);
    outer.setItemCount(20);
    outer.setEstimatedExtent(160);
    outer.setItemBuilder([&](std::size_t i) {
        if (i != 0) return makeContainerLeaf(240, 160);
        auto button = withKey(makeButton("Child"), "child");
        button.width = 1000;
        button.height = 100;
        auto viewport = makeScrollView(std::move(button), "inner", 240, 160);
        viewport.scrollAxis = ScrollAxis::Horizontal;
        viewport.scrollOffset = inner.offset();
        return viewport;
    });
    app::ShellConfig config;
    config.initialView = {240, 240};
    config.build = [&] { return makeVirtualList(&outer, "outer", 240, 240); };
    config.onWheel = [&](const RenderNode&, const RenderNode* node, Offset, Offset delta) {
        REQUIRE(node);
        CHECK(node->key == "inner");
        CHECK(delta.y == 0);
        inner.updateExtents(node->size.width, node->size.width + node->scrollExtent);
        return inner.applyWheel(delta.x);
    };
    app::AppShell shell(config);
    static_cast<void>(shell.renderFrame());
    const auto* child = findNodeByKey(shell.root(), "child");
    REQUIRE(child);
    shell.controller().focusNode(*child);
    for (auto key : {Key::Right, Key::PageDown, Key::End}) {
        shell.keyDown(key);
        CHECK(inner.offset() > 0);
        CHECK(outer.scrollController()->offset() == 0);
    }
    CHECK(inner.offset() == inner.maxScrollOffset());
    shell.keyDown(Key::Home);
    CHECK(inner.offset() == 0);
    shell.keyDown(Key::Up);
    CHECK(outer.scrollController()->offset() == 0);
}

// Scroll design §4: editable children keep their keyboard actions.
TEST_CASE("horizontal_viewport_preserves_editor_and_slider_keyboard_priority", "[scrollbar][keyboard]") {
    for (const auto kind : {WidgetType::TextField, WidgetType::Slider}) {
        int scrollCalls = 0;
        app::ShellConfig config;
        config.initialView = {240, 120};
        config.build = [&] {
            auto child = kind == WidgetType::Slider ? makeSlider("value", "child")
                : withKey(makeTextField("abc"), "child");
            child.bind = "value";
            child.width = 600;
            return withScrollAxis(makeScrollView(std::move(child), "viewport", 240, 120),
                                  ScrollAxis::Horizontal);
        };
        config.onWheel = [&](const RenderNode&, const RenderNode*, Offset, Offset) {
            ++scrollCalls;
            return true;
        };
        app::AppShell shell(config);
        shell.state().set("value", kind == WidgetType::Slider ? "50" : "abc");
        static_cast<void>(shell.renderFrame());
        const auto* child = findNodeByKey(shell.root(), "child");
        REQUIRE(child);
        shell.controller().focusNode(*child);
        shell.keyDown(Key::Left);
        CHECK(scrollCalls == 0);
        if (kind == WidgetType::Slider) CHECK(shell.state().get("value") == "45");
        else CHECK(shell.state().get("value") == "abc");
    }
}

// Scroll design §5: opaque content must not cover the inset viewport ring.
TEST_CASE("scroll_view_focus_ring_is_visible_above_content_without_layout_change", "[scrollbar][render]") {
    bool enabled = true;
    app::ShellConfig config;
    config.initialView = {200, 120};
    config.build = [&] {
        auto content = makeContainerLeaf(600, 120);
        content.color = Color{20, 30, 40, 255};
        auto viewport = makeScrollView(std::move(content), "viewport", 200, 120);
        viewport.scrollAxis = ScrollAxis::Horizontal;
        viewport.showFocusRing = true;
        viewport.enabled = enabled;
        return viewport;
    };
    app::AppShell shell(config);
    static_cast<void>(shell.renderFrame());
    const auto before = shell.pixels();
    const auto size = shell.root().size;
    const auto extent = shell.root().scrollExtent;
    shell.controller().focusNode(shell.root());
    shell.markDirty();
    static_cast<void>(shell.renderFrame());
    const auto& common = commonStyle(shell.root().style);
    CHECK(common.focusWidth == shell.theme().metrics.focusRingWidth);
    const std::size_t pixel = (60 * 200) * 4;
    CHECK(shell.pixels().rgba[pixel] == shell.theme().colors.focusRing.r);
    CHECK(shell.pixels().rgba[pixel + 1] == shell.theme().colors.focusRing.g);
    CHECK(shell.pixels().rgba[pixel + 2] == shell.theme().colors.focusRing.b);
    CHECK(shell.root().size == size);
    CHECK(shell.root().scrollExtent == extent);
    CHECK(shell.renderFrame() == shell.renderFrame(true));
    shell.focus().clearFocus();
    shell.markDirty();
    static_cast<void>(shell.renderFrame());
    CHECK(shell.pixels().rgba == before.rgba);
    enabled = false;
    shell.controller().focusNode(shell.root());
    shell.markDirty();
    static_cast<void>(shell.renderFrame());
    CHECK(commonStyle(shell.root().style).focusWidth == 0);
}

TEST_CASE("scrollbar_capture_does_not_switch_menu_bar_on_pointer_crossing", "[scrollbar][review]") {
    widgets::MenuBarController bar;
    bar.setMenus({{"file", "File"}, {"view", "View"}});
    bar.setMenuProvider([](const std::string& id) {
        widgets::MenuItems items;
        for (int i = 0; i < 30; ++i) items.push_back({.id = id + std::to_string(i), .label = id});
        return items;
    });
    app::ShellConfig config;
    config.initialView = {400, 300};
    config.build = [&] { return makeStack({bar.build(style::Theme::dark())}); };
    app::AppShell shell(config);
    bar.attach(shell);
    static_cast<void>(shell.renderFrame());
    const auto center = [&](const char* key) {
        const auto* node = findNodeByKey(shell.root(), key);
        REQUIRE(node);
        return absoluteOffset(shell.root(), key) + Offset{node->size.width * .5F, node->size.height * .5F};
    };
    shell.pointerDown(center("menu:bar:file")); shell.pointerUp(center("menu:bar:file"));
    static_cast<void>(shell.renderFrame());
    REQUIRE(shell.overlayRoot());
    const auto* viewport = findNodeByKey(*shell.overlayRoot(), "menubar:m0:scroll");
    REQUIRE(viewport);
    const auto thumb = scrollbarGeometry(*viewport);
    REQUIRE(thumb);
    const auto point = absoluteOffset(*shell.overlayRoot(), viewport->key) + thumb->thumbHit.origin + Offset{1, 5};
    shell.pointerDown(point);
    shell.pointerMove(center("menu:bar:view"));
    static_cast<void>(shell.renderFrame());
    REQUIRE(shell.overlayRoot());
    CHECK(findNodeByKey(*shell.overlayRoot(), "menubar:m0:i0")->semanticsLabel == "file");
    CHECK_FALSE(shell.controller().draggedScrollbarIdentity().empty());
    shell.pointerUp(center("menu:bar:view"));
    shell.pointerMove(center("menu:bar:view"));
    static_cast<void>(shell.renderFrame());
    CHECK(findNodeByKey(*shell.overlayRoot(), "menubar:m0:i0")->semanticsLabel == "view");
}
