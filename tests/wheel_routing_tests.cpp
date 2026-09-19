// Scroll design §4: wheel routing skips viewports without a usable range.
#include <catch2/catch_test_macros.hpp>

#include "lumen/app/app_shell.h"
#include "lumen/widgets/list.h"
#include "lumen/widgets/tree.h"

using namespace lumen;
using namespace lumen::core;

namespace {
struct FlatTree final : widgets::TreeModel {
    std::size_t count{1};
    std::size_t childCount(const std::string& key) const override { return key.empty() ? count : 0; }
    std::string childAt(const std::string&, std::size_t i) const override { return "node" + std::to_string(i); }
    bool hasChildren(const std::string&) const override { return false; }
    Widget buildRow(const std::string& key, std::size_t) const override { return makeText(key); }
};

struct NestedScrollApp {
    WidgetType kind{WidgetType::List};
    bool intermediate{false};
    bool horizontal{false};
    float contentWidth{200.0F};
    std::size_t count{1};
    ScrollController outerScroll;
    ScrollController innerScroll;
    VirtualListController virtualList;
    widgets::ListController list;
    FlatTree model;
    widgets::TreeController tree;
    widgets::TreeListController table;
    std::vector<std::string> sinkTargets;
    app::AppShell shell{config()};

    NestedScrollApp() {
        tree.setModel(&model);
        table.setModel(&model);
        virtualList.setItemBuilder([](std::size_t) { return makeContainerLeaf(200, 40); });
        list.setItemBuilder([](std::size_t) { return makeText("Item"); });
        setCount(1);
    }
    void setCount(std::size_t value) {
        count = model.count = value;
        virtualList.setItemCount(value);
        list.setItemCount(value);
        tree.modelChanged();
        table.modelChanged();
        shell.markDirty();
    }
    app::ShellConfig config() {
        app::ShellConfig config;
        config.initialView = {200, 200};
        config.build = [this] {
            Widget inner;
            if (kind == WidgetType::VirtualList) inner = makeVirtualList(&virtualList, "inner", 200, 100);
            else if (kind == WidgetType::List) inner = makeList(&list, "inner", 200, 100);
            else if (kind == WidgetType::Tree) inner = makeTree(&tree, "inner", 200, 100);
            else if (kind == WidgetType::TreeList) inner = makeTreeList(&table, nullptr, false, "inner", 200, 100);
            else {
                auto content = makeContainerLeaf(contentWidth, static_cast<float>(count) * 40.0F);
                inner = kind == WidgetType::ListView
                    ? makeListView(std::move(content), "inner", 200, 100)
                    : makeScrollView(std::move(content), "inner", 200, 100);
                inner.scrollAxis = horizontal ? ScrollAxis::Horizontal : ScrollAxis::Vertical;
                inner.scrollOffset = innerScroll.offset();
            }
            // Intentionally hidden scrollbar: range, not decoration, owns input.
            inner.showScrollbar = false;
            if (intermediate) inner = makeScrollView(std::move(inner), "middle", 200, 100);
            auto content = makeColumn({std::move(inner), makeContainerLeaf(200, 600)});
            return withScrollOffset(makeListView(std::move(content), "outer", 200, 200), outerScroll.offset());
        };
        config.onWheel = [this](const RenderNode&, const RenderNode* target, Offset, Offset delta) {
            REQUIRE(target != nullptr);
            sinkTargets.push_back(target->key);
            auto& controller = target->key == "inner" ? innerScroll : outerScroll;
            const bool xAxis = target->scrollAxis == ScrollAxis::Horizontal;
            const float viewport = xAxis ? target->size.width : target->size.height;
            controller.updateExtents(viewport, viewport + target->scrollExtent);
            const bool changed = controller.applyWheel(xAxis ? delta.x : delta.y);
            if (changed) shell.markDirty();
            return changed;
        };
        return config;
    }
    void frame() { static_cast<void>(shell.renderFrame()); }
    const RenderNode& node(const char* key) const {
        const auto* result = findNodeByKey(shell.root(), key);
        REQUIRE(result != nullptr);
        return *result;
    }
    ScrollController& innerController() {
        const auto* source = node("inner").virtualSource;
        return source == nullptr ? innerScroll : *source->scrollController();
    }
};

const WidgetType viewportKinds[] = {WidgetType::ScrollView, WidgetType::ListView,
    WidgetType::VirtualList, WidgetType::List, WidgetType::Tree, WidgetType::TreeList};
}

TEST_CASE("wheel_bubbles_past_empty_and_fitting_nested_viewports", "[scroll][wheel-routing]") {
    for (const auto kind : viewportKinds) {
        for (const std::size_t count : {0U, 1U}) {
            CAPTURE(static_cast<int>(kind), count);
            NestedScrollApp app;
            app.kind = kind;
            app.intermediate = true;
            app.setCount(count);
            app.frame();
            REQUIRE(app.node("inner").scrollExtent == 0);
            REQUIRE(app.node("middle").scrollExtent == 0);
            REQUIRE(app.node("outer").scrollExtent > 0);
            CHECK(app.shell.wheel({50, 30}, {0, 30}));
            CHECK(app.outerScroll.offset() == 30);
            CHECK(app.innerController().offset() == 0);
            CHECK(app.sinkTargets == std::vector<std::string>{"outer"});
        }
    }
}

TEST_CASE("wheel_keeps_scrollable_children_in_charge_with_hidden_scrollbars", "[scroll][wheel-routing]") {
    for (const auto kind : viewportKinds) {
        CAPTURE(static_cast<int>(kind));
        NestedScrollApp app;
        app.kind = kind;
        app.setCount(20);
        app.frame();
        REQUIRE(app.node("inner").scrollExtent > 0);
        CHECK(app.shell.wheel({50, 30}, {0, 30}));
        CHECK(app.innerController().offset() == 30);
        CHECK(app.outerScroll.offset() == 0);
        // Preserve the established boundary policy for overflowing children.
        auto& inner = app.innerController();
        inner.scrollTo(inner.maxScrollOffset());
        app.shell.markDirty();
        app.frame();
        CHECK_FALSE(app.shell.wheel({50, 30}, {0, 30}));
        CHECK(app.outerScroll.offset() == 0);
        // Once the content fits again, the next event belongs to the parent.
        app.setCount(1);
        app.frame();
        CHECK(app.shell.wheel({50, 30}, {0, 30}));
        CHECK(app.outerScroll.offset() == 30);
    }
}

TEST_CASE("wheel_bubbling_recomputes_axis_from_original_delta", "[scroll][wheel-routing]") {
    NestedScrollApp app;
    app.kind = WidgetType::ScrollView;
    app.horizontal = true;
    app.contentWidth = 500;
    app.frame();
    REQUIRE(app.node("inner").scrollExtent == 300);
    CHECK(app.shell.wheel({50, 30}, {0, 25}));
    CHECK(app.outerScroll.offset() == 25);
    CHECK(app.innerScroll.offset() == 0);
    app.outerScroll.scrollTo(0);
    app.shell.markDirty();
    app.frame();
    CHECK(app.shell.wheel({50, 30}, {0, 40}, kModifierShift));
    CHECK(app.innerScroll.offset() == 40);
    CHECK(app.outerScroll.offset() == 0);
    CHECK(app.shell.wheel({50, 30}, {15, 25}));
    CHECK(app.innerScroll.offset() == 55);
    CHECK(app.outerScroll.offset() == 0);
    app.contentWidth = 200;
    app.shell.markDirty();
    app.frame();
    CHECK(app.shell.wheel({50, 30}, {0, 20}, kModifierShift));
    CHECK(app.outerScroll.offset() == 20);
}

TEST_CASE("wheel_bubbling_stays_inside_modal_event_tree", "[scroll][wheel-routing]") {
    NestedScrollApp app;
    int overlayCalls = 0;
    app.shell.setOverlayBuilder([]() -> std::optional<Widget> {
        return makeScrollView(makeContainerLeaf(200, 30), "overlay", 200, 100);
    }, [&](const RenderNode&, const RenderNode*, Offset, Offset) {
        ++overlayCalls;
        return false;
    });
    app.frame();
    CHECK_FALSE(app.shell.wheel({50, 30}, {0, 30}));
    CHECK(app.outerScroll.offset() == 0);
    CHECK(app.sinkTargets.empty());
    CHECK(overlayCalls == 1);
}
