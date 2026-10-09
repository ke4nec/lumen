#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <set>
#include <string>
#include <utility>
#include <variant>

#include "designer_app.h"
#include "lumen/core/render_node.h"

using lumen::core::Offset;
using lumen::core::Size;
using lumen::core::absoluteOffset;
using lumen::core::findNodeByKey;
using lumen::designer_app::DesignerApp;

namespace {

void configureCanvas(DesignerApp& app, int dpi, int zoomSteps) {
    for (int step = 0; step != dpi; ++step) {
        app.shell().handlers().at("designer:dpi")();
    }
    for (int step = 0; step != zoomSteps; ++step) {
        app.shell().handlers().at("designer:zoom-in")();
    }
    app.shell().handlers().at("designer:pan-right")();
    app.shell().handlers().at("designer:pan-down")();
    (void)app.shell().renderFrame();
}

lumen::core::Rect bounds(DesignerApp& app, const std::string& key) {
    const auto* node = findNodeByKey(app.shell().root(), key);
    REQUIRE(node != nullptr);
    return {absoluteOffset(app.shell().root(), key), node->size};
}

}  // namespace

// G-D14 / prerequisites §4.15: AppShell receives host logical coordinates.
TEST_CASE("designer canvas marquee and guides track nodes across dpi zoom and pan",
          "[designer][d3][app][designer-transform][canvas-coordinates]") {
    for (int dpi = 0; dpi != 3; ++dpi) {
        for (int zoomSteps : {0, 2}) {
            INFO("dpi tier " << dpi << " zoom steps " << zoomSteps);
            DesignerApp app;
            app.attach();
            app.shell().setView(Size{1280.0F, 800.0F});
            REQUIRE(app.loadSource(
                "page coordinates { Stack(key: \"root\", width: 400, height: 300) {"
                " Text(\"First\", key: \"first\", width: 80, height: 48, left: 80, top: 64)"
                " Text(\"Second\", key: \"second\", width: 80, height: 48, left: 240, top: 176)"
                " } }", "canvas-coordinates.lumen"));
            configureCanvas(app, dpi, zoomSteps);
            const auto before = *app.workbench().document();
            const auto revision = app.workbench().documentRevision();
            const auto firstId = before.root.children[0].id;
            const auto secondId = before.root.children[1].id;
            const auto first = bounds(app, "first");
            const auto canvas = bounds(app, "designer-canvas");
            const Offset center = first.origin +
                Offset{first.size.width * 0.5F, first.size.height * 0.5F};
            app.shell().pointerDown(center);
            app.shell().pointerUp(center);
            CHECK(app.workbench().selection().primary == firstId);
            CHECK(app.workbench().document() == before);
            CHECK(app.workbench().documentRevision() == revision);
            const Offset start = canvas.origin + Offset{4.0F, 4.0F};
            const Offset end = first.origin +
                Offset{first.size.width + 2.0F, first.size.height + 2.0F};
            app.shell().pointerDown(start);
            app.shell().pointerMove(end);
            (void)app.shell().renderFrame();
            REQUIRE(app.shell().controller().dragSessionActive());
            REQUIRE(app.shell().overlayRoot() != nullptr);
            const auto* rectangle = findNodeByKey(*app.shell().overlayRoot(),
                                                  "designer-canvas-selection-rect");
            REQUIRE(rectangle != nullptr);
            const auto rectangleOrigin =
                absoluteOffset(*app.shell().overlayRoot(), rectangle->key);
            CHECK(rectangleOrigin.x == Catch::Approx(start.x));
            CHECK(rectangleOrigin.y == Catch::Approx(start.y));
            CHECK(rectangle->size.width == Catch::Approx(end.x - start.x));
            CHECK(rectangle->size.height == Catch::Approx(end.y - start.y));
            CHECK(app.workbench().document() == before);
            app.shell().pointerUp(end);
            (void)app.shell().renderFrame();
            CHECK(app.workbench().selection().ids ==
                  std::set<lumen::dsl::DesignNodeId>{firstId});
            CHECK_FALSE(app.workbench().selection().ids.contains(secondId));
            CHECK(app.shell().overlayRoot() == nullptr);
            app.shell().handlers().at("designer:canvas-guides")();
            (void)app.shell().renderFrame();
            const auto guide = bounds(app, "designer-canvas-guide-frame");
            const auto selected = bounds(app, "first");
            CHECK(guide.origin.x == Catch::Approx(selected.origin.x - 6.0F));
            CHECK(guide.origin.y == Catch::Approx(selected.origin.y - 6.0F));
            CHECK(guide.size.width == Catch::Approx(selected.size.width + 12.0F));
            CHECK(guide.size.height == Catch::Approx(selected.size.height + 12.0F));
            const auto selection = app.workbench().selection();
            app.shell().pointerDown(start);
            app.shell().pointerMove(end + Offset{100.0F, 100.0F});
            (void)app.shell().renderFrame();
            REQUIRE(app.shell().controller().dragSessionActive());
            app.shell().keyDown(lumen::core::Key::Escape);
            (void)app.shell().renderFrame();
            CHECK_FALSE(app.shell().controller().dragSessionActive());
            CHECK(app.shell().overlayRoot() == nullptr);
            CHECK(app.workbench().selection() == selection);
            CHECK(app.workbench().document() == before);
            CHECK(app.workbench().documentRevision() == revision);
            CHECK_FALSE(app.workbench().dirty());
            CHECK_FALSE(app.workbench().canUndo());
        }
    }
}

TEST_CASE("designer canvas resize preserves design coordinates across dpi zoom and nested parents",
          "[designer][d3][app][designer-transform][canvas-coordinates]") {
    for (bool nested : {false, true}) {
        for (int dpi = 0; dpi != 3; ++dpi) {
            for (int zoomSteps : {0, 2}) {
                INFO("nested " << nested << " dpi tier " << dpi << " zoom steps " << zoomSteps);
                DesignerApp app;
                app.attach();
                app.shell().setView(Size{1280.0F, 800.0F});
                const std::string stack =
                    "Stack(key: \"parent\", width: 360, height: 260, padding: 16) {"
                    " Text(\"Node\", key: \"node\", width: 80, height: 48, margin: 8, left: 80, top: 64) }";
                REQUIRE(app.loadSource("page resize { " + (nested
                    ? "Container(key: \"outer\", padding: 16) { " + stack + " }"
                    : stack) + " }", "resize-coordinates.lumen"));
                configureCanvas(app, dpi, zoomSteps);
                const auto before = *app.workbench().document();
                const auto& node = nested ? before.root.children[0].children[0]
                                          : before.root.children[0];
                const auto id = node.id;
                app.shell().handlers().at("designer:select:" + std::to_string(id))();
                const auto selection = app.workbench().selection();
                app.shell().handlers().at("designer:canvas-guides")();
                (void)app.shell().renderFrame();
                const auto original = bounds(app, "node");
                const auto handle = bounds(app, "designer-canvas-handle:nw");
                const Offset start = handle.origin +
                    Offset{handle.size.width * 0.5F, handle.size.height * 0.5F};
                const float zoom = zoomSteps == 0 ? 1.0F : 1.21F;
                const Offset end = start + Offset{-8.0F * zoom, -8.0F * zoom};
                app.shell().pointerDown(start);
                app.shell().pointerMove(end);
                (void)app.shell().renderFrame();
                REQUIRE(app.shell().controller().dragSessionActive());
                CHECK(app.workbench().document() == before);
                const auto guide = bounds(app, "designer-canvas-guide-frame");
                CHECK(guide.origin.x == Catch::Approx(original.origin.x - 8.0F * zoom - 6.0F));
                CHECK(guide.origin.y == Catch::Approx(original.origin.y - 8.0F * zoom - 6.0F));
                CHECK(guide.size.width == Catch::Approx(88.0F * zoom + 12.0F));
                CHECK(guide.size.height == Catch::Approx(56.0F * zoom + 12.0F));
                // A release can arrive farther away than the last Move event.
                app.shell().pointerUp(start + Offset{-16.0F * zoom, -16.0F * zoom});
                (void)app.shell().renderFrame();
                const auto& edited = nested
                    ? app.workbench().document()->root.children[0].children[0]
                    : app.workbench().document()->root.children[0];
                const auto number = [&](const std::string& name) {
                    REQUIRE(edited.properties.contains(name));
                    return std::get<double>(edited.properties.at(name).value);
                };
                CHECK(number("left") == Catch::Approx(64.0));
                CHECK(number("top") == Catch::Approx(48.0));
                CHECK(number("width") == Catch::Approx(96.0));
                CHECK(number("height") == Catch::Approx(64.0));
                CHECK(app.workbench().selection() == selection);
                CHECK(app.workbench().dirty());
                REQUIRE(app.undo());
                CHECK(app.workbench().document() == before);
                CHECK(app.workbench().selection() == selection);
                CHECK_FALSE(app.workbench().dirty());
                CHECK_FALSE(app.workbench().canUndo());
                REQUIRE(app.redo());
                CHECK(app.workbench().dirty());
                REQUIRE(app.undo());
                (void)app.shell().renderFrame();
                const auto cancel = bounds(app, "designer-canvas-handle:nw");
                const Offset cancelStart = cancel.origin +
                    Offset{cancel.size.width * 0.5F, cancel.size.height * 0.5F};
                app.shell().pointerDown(cancelStart);
                app.shell().pointerMove(cancelStart + Offset{24.0F * zoom, 16.0F * zoom});
                (void)app.shell().renderFrame();
                REQUIRE(app.shell().controller().dragSessionActive());
                app.shell().keyDown(lumen::core::Key::Escape);
                (void)app.shell().renderFrame();
                CHECK_FALSE(app.shell().controller().dragSessionActive());
                CHECK(app.workbench().document() == before);
                CHECK(app.workbench().selection() == selection);
                CHECK_FALSE(app.workbench().dirty());
                CHECK_FALSE(app.workbench().canUndo());
                CHECK(app.workbench().canRedo());
            }
        }
    }
}

// G-D14 / prerequisites §4.15: drag transactions use frozen design coordinates.
TEST_CASE("designer canvas resize uses every handle in parent design units",
          "[designer][d3][app][canvas-coordinates]") {
    for (bool positioned : {false, true}) {
        for (const std::string handleName : {"nw", "n", "ne", "e", "se", "s", "sw", "w"}) {
            INFO("positioned " << positioned << " handle " << handleName);
            DesignerApp app;
            app.attach();
            app.shell().setView(Size{1280.0F, 800.0F});
            const std::string layout = positioned
                ? "Stack(key: \"parent\", width: 360, height: 260, padding: 16)"
                : "Column(key: \"parent\", width: 360, height: 260, padding: 16, crossAxis: center)";
            REQUIRE(app.loadSource(
                "page handles { Container(padding: 16) { " + layout + " {"
                " Text(\"Node\", key: \"node\", width: 80, height: 48, margin: 8" +
                (positioned ? ", left: 80, top: 64" : "") + ") } } }", "resize-handles.lumen"));
            configureCanvas(app, 2, 2);
            const auto before = *app.workbench().document();
            const auto id = before.root.children[0].children[0].id;
            app.shell().handlers().at("designer:select:" + std::to_string(id))();
            app.shell().handlers().at("designer:canvas-guides")();
            (void)app.shell().renderFrame();
            const auto handle = bounds(app, "designer-canvas-handle:" + handleName);
            const Offset start = handle.origin +
                Offset{handle.size.width * 0.5F, handle.size.height * 0.5F};
            const bool west = handleName.find('w') != std::string::npos;
            const bool east = handleName.find('e') != std::string::npos;
            const bool north = handleName.find('n') != std::string::npos;
            const bool south = handleName.find('s') != std::string::npos;
            const Offset delta{west ? -16.0F : east ? 16.0F : 0.0F,
                               north ? -16.0F : south ? 16.0F : 0.0F};
            const Offset end = start + Offset{delta.x * 1.21F, delta.y * 1.21F};
            app.shell().pointerDown(start);
            app.shell().pointerMove(end);
            REQUIRE(app.shell().controller().dragSessionActive());
            app.shell().pointerUp(end);
            const auto& edited = app.workbench().document()->root.children[0].children[0];
            if (positioned) {
                CHECK(std::get<double>(edited.properties.at("left").value) == Catch::Approx(west ? 64.0 : 80.0));
                CHECK(std::get<double>(edited.properties.at("top").value) == Catch::Approx(north ? 48.0 : 64.0));
            } else {
                CHECK_FALSE(edited.properties.contains("left"));
                CHECK_FALSE(edited.properties.contains("top"));
            }
            for (const auto& [name, expected] : {
                     std::pair{"width", west || east ? 96.0 : 80.0},
                     std::pair{"height", north || south ? 64.0 : 48.0}}) {
                CHECK(std::get<double>(edited.properties.at(name).value) == Catch::Approx(expected));
            }
            REQUIRE(app.undo());
            CHECK(app.workbench().document() == before);
            CHECK_FALSE(app.workbench().canUndo());
        }
    }
}

TEST_CASE("designer canvas view changes cancel resize and stale revisions cannot commit",
          "[designer][d3][app][canvas-coordinates]") {
    for (const std::string action : {"zoom-in", "pan-right", "zoom-reset", "dpi", "edit"}) {
        INFO("action " << action);
        DesignerApp app;
        app.attach();
        app.shell().setView(Size{1280.0F, 800.0F});
        REQUIRE(app.loadSource(
            "page cancel { Stack(width: 360, height: 260) {"
            " Text(\"Node\", key: \"node\", width: 80, height: 48, left: 80, top: 64)"
            " } }", "resize-cancel.lumen"));
        configureCanvas(app, 1, 2);
        const auto id = app.workbench().document()->root.children[0].id;
        app.shell().handlers().at("designer:select:" + std::to_string(id))();
        app.shell().handlers().at("designer:canvas-guides")();
        (void)app.shell().renderFrame();
        const auto selection = app.workbench().selection();
        const auto handle = bounds(app, "designer-canvas-handle:se");
        const Offset start = handle.origin + Offset{4.0F, 4.0F};
        const Offset end = start + Offset{16.0F * 1.21F, 16.0F * 1.21F};
        app.shell().pointerDown(start);
        app.shell().pointerMove(end);
        REQUIRE(app.shell().controller().dragSessionActive());
        if (action == "edit") {
            REQUIRE(app.workbench().setProperty(id, "text", lumen::dsl::DesignValue{
                lumen::dsl::DesignValue::Variant{std::string{"Changed"}}}));
        } else {
            app.shell().handlers().at("designer:" + action)();
            CHECK_FALSE(app.shell().controller().dragSessionActive());
        }
        const auto expected = *app.workbench().document();
        const auto revision = app.workbench().documentRevision();
        app.shell().pointerUp(end);
        (void)app.shell().renderFrame();
        CHECK(app.workbench().document() == expected);
        CHECK(app.workbench().documentRevision() == revision);
        CHECK(app.workbench().selection() == selection);
        CHECK(app.workbench().dirty() == (action == "edit"));
        if (action == "edit") {
            REQUIRE(app.undo());
        }
        CHECK_FALSE(app.workbench().canUndo());
    }
}

TEST_CASE("designer canvas root resize excludes view pan from design snapping",
          "[designer][d3][app][canvas-coordinates]") {
    for (int dpi = 0; dpi != 3; ++dpi) {
        for (int zoomSteps : {0, 2}) {
            INFO("dpi tier " << dpi << " zoom steps " << zoomSteps);
            DesignerApp app;
            app.attach();
            app.shell().setView(Size{1280.0F, 800.0F});
            REQUIRE(app.loadSource(
                "page root { Text(\"Root\", key: \"node\", width: 80, height: 48) }",
                "resize-root.lumen"));
            configureCanvas(app, dpi, zoomSteps);
            const auto before = *app.workbench().document();
            app.shell().handlers().at("designer:select:" + std::to_string(before.root.id))();
            app.shell().handlers().at("designer:canvas-guides")();
            (void)app.shell().renderFrame();
            const auto handle = bounds(app, "designer-canvas-handle:se");
            const Offset start = handle.origin + Offset{4.0F, 4.0F};
            const float zoom = zoomSteps == 0 ? 1.0F : 1.21F;
            const Offset end = start + Offset{16.0F * zoom, 16.0F * zoom};
            app.shell().pointerDown(start);
            app.shell().pointerMove(end);
            REQUIRE(app.shell().controller().dragSessionActive());
            app.shell().pointerUp(end);
            const auto& edited = app.workbench().document()->root;
            CHECK(std::get<double>(edited.properties.at("width").value) == Catch::Approx(96.0));
            CHECK(std::get<double>(edited.properties.at("height").value) == Catch::Approx(64.0));
            CHECK_FALSE(edited.properties.contains("left"));
            CHECK_FALSE(edited.properties.contains("top"));
            REQUIRE(app.undo());
            CHECK(app.workbench().document() == before);
            CHECK_FALSE(app.workbench().canUndo());
        }
    }
}
