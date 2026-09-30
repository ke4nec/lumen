#include <catch2/catch_test_macros.hpp>

#include "designer_app.h"
#include "lumen/accessibility/bridge.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/render_node.h"

using lumen::accessibility::kActionActivate;
using lumen::accessibility::SemanticsRole;
using lumen::core::Key;
using lumen::core::Offset;
using lumen::core::Size;
using lumen::core::absoluteOffset;
using lumen::core::findNodeByKey;
using lumen::designer_app::DesignerApp;

TEST_CASE("designer app exposes the D2 shell and semantic controls",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto& root = app.shell().root();
    REQUIRE(findNodeByKey(root, "designer-toolbar") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-outline-view") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-canvas") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-properties-panel") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-diagnostics") != nullptr);

    const auto semantics = app.shell().buildSemanticsSnapshot();
    bool hasButton = false;
    for (const auto& [id, node] : semantics.nodes) {
        (void)id;
        if (node.role == SemanticsRole::Button && !node.label.empty()) {
            hasButton = true;
            break;
        }
    }
    CHECK(hasButton);
    REQUIRE(app.workbench().selection().primary.has_value());
}

TEST_CASE("designer app selects preview nodes and keeps the last frame on errors",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto expected = outline->children.front().id;
    const auto* preview = findNodeByKey(app.shell().root(), "title");
    REQUIRE(preview != nullptr);
    const auto origin = absoluteOffset(app.shell().root(), "title");
    const Offset center = origin +
                          Offset{preview->size.width * 0.5F,
                                 preview->size.height * 0.5F};
    app.shell().pointerDown(center);
    app.shell().pointerUp(center);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(*app.workbench().selection().primary == expected);

    const auto generation = app.workbench().frame().generation();
    CHECK_FALSE(app.loadSource("page broken {", "broken.lumen"));
    CHECK(app.workbench().frame().hasFrame());
    CHECK(app.workbench().frame().generation() == generation);
    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().file == "broken.lumen");
}

TEST_CASE("designer app keeps keyboard and semantic activation on one path",
          "[designer][d2][app][a11y]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto* density = findNodeByKey(app.shell().root(), "designer-density");
    REQUIRE(density != nullptr);
    const auto beforeDensity = app.shell().theme().metrics.density;
    CHECK(app.shell().performAccessibilityAction(
              density->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    CHECK(app.shell().theme().metrics.density != beforeDensity);

    app.shell().focus().clearFocus();
    app.shell().keyDown(Key::Tab);
    CHECK(app.shell().focus().focusedKey() == "designer-theme");
    app.shell().keyDown(Key::Tab);
    CHECK(app.shell().focus().focusedKey() == "designer-density");
    app.shell().keyDown(Key::Tab);
    CHECK(app.shell().focus().focusedKey() == "designer-preview-state");
    app.shell().keyDown(Key::Tab);
    CHECK(app.shell().focus().focusedKey().starts_with(
        "designer-outline:item:"));

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    app.shell().keyDown(Key::Down);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(*app.workbench().selection().primary == outline->children.front().id);
}
