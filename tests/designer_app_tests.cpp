#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "designer_app.h"
#include "file_watcher.h"
#include "lumen/accessibility/bridge.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/render_node.h"
#include "lumen/style/state.h"

using lumen::accessibility::kActionActivate;
using lumen::accessibility::SemanticsRole;
using lumen::core::Key;
using lumen::core::Offset;
using lumen::core::Size;
using lumen::core::absoluteOffset;
using lumen::core::findNodeByKey;
using lumen::designer_app::DesignerApp;
using lumen::designer_app::FileWatcher;

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
    CHECK(app.shell().focus().focusedKey() == "designer-dpi");
    app.shell().keyDown(Key::Tab);
    CHECK(app.shell().focus().focusedKey() == "designer-font-scale");
    app.shell().keyDown(Key::Tab);
    CHECK(app.shell().focus().focusedKey() == "designer-contrast");
    app.shell().keyDown(Key::Tab);
    CHECK(app.shell().focus().focusedKey() == "designer-preview-state");
    app.shell().keyDown(Key::Tab);
    CHECK(app.shell().focus().focusedKey() == "designer-canvas-guides");
    const std::vector<std::string> previewKeys = {"designer-run",
                                                  "designer-debug"};
    for (const auto& key : previewKeys) {
        app.shell().keyDown(Key::Tab);
        CHECK(app.shell().focus().focusedKey() == key);
    }
    const std::vector<std::string> structureKeys = {
        "designer-add-text", "designer-duplicate", "designer-remove",
        "designer-move-up", "designer-move-down"};
    for (const auto& key : structureKeys) {
        app.shell().keyDown(Key::Tab);
        CHECK(app.shell().focus().focusedKey() == key);
    }
    const std::vector<std::string> fileKeys = {
        "designer-open", "designer-save", "designer-save-as"};
    for (const auto& key : fileKeys) {
        app.shell().keyDown(Key::Tab);
        CHECK(app.shell().focus().focusedKey() == key);
    }
    const std::vector<std::string> toolboxKeys = {
        "designer-toolbox:Container", "designer-toolbox:Row",
        "designer-toolbox:Column",    "designer-toolbox:Stack",
        "designer-toolbox:Text",      "designer-toolbox:Button",
        "designer-toolbox:TextField", "designer-toolbox:ScrollView",
        "designer-toolbox:ListView",  "designer-toolbox:Checkbox",
        "designer-toolbox:Switch",    "designer-toolbox:FocusScope",
        "designer-toolbox:Grid",      "designer-toolbox:Image",
        "designer-toolbox:Icon",      "designer-toolbox:Slider",
        "designer-toolbox:ProgressBar", "designer-toolbox:Radio",
        "designer-toolbox:Tooltip",   "designer-toolbox:Dropdown",
        "designer-toolbox:Tabs",      "designer-toolbox:ThemeScope",
        "designer-toolbox:VirtualList", "designer-toolbox:List",
        "designer-toolbox:Tree",      "designer-toolbox:TreeList",
        "designer-toolbox:Splitter",  "designer-toolbox:ToolBar",
        "designer-toolbox:StatusBar", "designer-toolbox:DataGrid"};
    for (const auto& key : toolboxKeys) {
        app.shell().keyDown(Key::Tab);
        CHECK(app.shell().focus().focusedKey() == key);
    }
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

TEST_CASE("designer app renders canvas alignment guides for the selection",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // The first frame establishes the canvas geometry used to place the
    // non-modal guide layer; enabling it forces the first geometry-aware
    // rebuild.
    app.shell().handlers().at("designer:canvas-guides")();
    (void)app.shell().renderFrame();

    REQUIRE(findNodeByKey(app.shell().root(), "designer-canvas-guides") !=
            nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-ruler-top") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-grid-dot:0:0") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-guide-frame") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-handle:nw") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-dimensions") != nullptr);
    bool guideInSemantics = false;
    for (const auto& [id, node] : app.shell().buildSemanticsSnapshot().nodes) {
        (void)node;
        if (id.find("designer-canvas-guide-frame") != std::string::npos ||
            id.find("designer-canvas-guide-v") != std::string::npos ||
            id.find("designer-canvas-guide-h") != std::string::npos ||
            id.find("designer-canvas-handle") != std::string::npos) {
            guideInSemantics = true;
            break;
        }
    }
    CHECK_FALSE(guideInSemantics);

    app.shell().handlers().at("designer:canvas-guides")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-guide-frame") == nullptr);
    app.shell().handlers().at("designer:canvas-guides")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-guide-frame") != nullptr);
}

TEST_CASE("designer app resizes a selected node with one snapped transaction",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto titleId = outline->children.front().id;
    const auto select = app.shell().handlers().find(
        "designer:select:" + std::to_string(titleId));
    REQUIRE(select != app.shell().handlers().end());
    select->second();
    app.shell().handlers().at("designer:canvas-guides")();
    (void)app.shell().renderFrame();

    const auto* handle =
        findNodeByKey(app.shell().root(), "designer-canvas-handle:e");
    REQUIRE(handle != nullptr);
    const auto handleOrigin = absoluteOffset(app.shell().root(), handle->key);
    const Offset start =
        handleOrigin + Offset{handle->size.width * 0.5F,
                              handle->size.height * 0.5F};
    const Offset end = start + Offset{17.0F, 0.0F};
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    const auto properties = app.workbench().properties(titleId);
    const auto width = std::find_if(
        properties.begin(), properties.end(),
        [](const auto& property) { return property.name == "width"; });
    REQUIRE(width != properties.end());
    REQUIRE(width->value.has_value());
    const auto* widthValue = std::get_if<double>(&width->value->value);
    REQUIRE(widthValue != nullptr);
    CHECK(*widthValue >= 8.0);
    CHECK(std::fmod(*widthValue, 8.0) == 0.0);
    CHECK(app.workbench().dirty());

    REQUIRE(app.undo());
    const auto restored = app.workbench().properties(titleId);
    CHECK(std::find_if(restored.begin(), restored.end(), [](const auto& property) {
              return property.name == "width";
          }) == restored.end());

    (void)app.shell().renderFrame();
    handle = findNodeByKey(app.shell().root(), "designer-canvas-handle:e");
    REQUIRE(handle != nullptr);
    const auto cancelOrigin = absoluteOffset(app.shell().root(), handle->key);
    const Offset cancelStart =
        cancelOrigin + Offset{handle->size.width * 0.5F,
                              handle->size.height * 0.5F};
    app.shell().pointerDown(cancelStart);
    app.shell().pointerMove(cancelStart + Offset{24.0F, 0.0F});
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().pointerCancel();
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.shell().controller().dragSessionActive());
    const auto cancelled = app.workbench().properties(titleId);
    CHECK(std::find_if(cancelled.begin(), cancelled.end(),
                       [](const auto& property) {
                           return property.name == "width";
                       }) == cancelled.end());
}

TEST_CASE("designer app resizes stack children with position in one transaction",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Stack(key: \"root\", width: 300, height: 200) { "
        "Text(\"Title\", key: \"node\", width: 40, height: 30, left: 20, "
        "top: 20) } }",
        "stack-resize.lumen"));
    (void)app.shell().renderFrame();
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto nodeId = outline->children.front().id;
    const auto select = app.shell().handlers().find(
        "designer:select:" + std::to_string(nodeId));
    REQUIRE(select != app.shell().handlers().end());
    select->second();
    app.shell().handlers().at("designer:canvas-guides")();
    (void)app.shell().renderFrame();

    const auto* handle =
        findNodeByKey(app.shell().root(), "designer-canvas-handle:nw");
    REQUIRE(handle != nullptr);
    const auto handleOrigin = absoluteOffset(app.shell().root(), handle->key);
    const Offset start =
        handleOrigin + Offset{handle->size.width * 0.5F,
                              handle->size.height * 0.5F};
    const Offset end = start + Offset{-11.0F, -11.0F};
    std::vector<const lumen::core::RenderNode*> hitChain;
    REQUIRE(lumen::core::hitTestChain(app.shell().root(), start, hitChain));
    CHECK(std::any_of(hitChain.begin(), hitChain.end(), [](const auto* node) {
        return node != nullptr && node->key == "designer-canvas-handle:nw";
    }));
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    const auto properties = app.workbench().properties(nodeId);
    const auto hasProperty = [&](std::string_view name) {
        return std::find_if(properties.begin(), properties.end(),
                            [name](const auto& property) {
                                return property.name == name &&
                                       property.value.has_value();
                            }) != properties.end();
    };
    CHECK(hasProperty("width"));
    CHECK(hasProperty("height"));
    CHECK(hasProperty("left"));
    CHECK(hasProperty("top"));
    REQUIRE(app.undo());
    const auto restored = app.workbench().properties(nodeId);
    const auto restoredValue = [&](std::string_view name) {
        const auto found = std::find_if(
            restored.begin(), restored.end(), [name](const auto& property) {
                return property.name == name;
            });
        REQUIRE(found != restored.end());
        REQUIRE(found->value.has_value());
        return std::get<double>(found->value->value);
    };
    CHECK(restoredValue("width") == 40.0);
    CHECK(restoredValue("height") == 30.0);
    CHECK(restoredValue("left") == 20.0);
    CHECK(restoredValue("top") == 20.0);
}

TEST_CASE("designer app previews theme density dpi and accessibility inputs",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    app.shell().setSystemAccessibilitySettings(
        lumen::accessibility::AccessibilitySettings{false, true, 1.0F});
    (void)app.shell().renderFrame();

    const auto activate = [&](const char* key) {
        const auto* node = findNodeByKey(app.shell().root(), key);
        REQUIRE(node != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  node->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
    };

    const bool dark = app.shell().theme().darkMode;
    const auto density = app.shell().theme().metrics.density;
    CHECK(app.shell().styleContext().deviceScale == 1.0F);
    CHECK(app.shell().accessibilitySettings().fontScale == 1.0F);
    CHECK_FALSE(app.shell().accessibilitySettings().highContrast);
    CHECK(app.shell().accessibilitySettings().reduceAnimation);

    activate("designer-theme");
    CHECK(app.shell().theme().darkMode != dark);
    activate("designer-density");
    CHECK(app.shell().theme().metrics.density != density);
    activate("designer-dpi");
    CHECK(app.shell().styleContext().deviceScale == 1.25F);
    activate("designer-font-scale");
    CHECK(app.shell().accessibilitySettings().fontScale == 1.25F);
    activate("designer-contrast");
    CHECK(app.shell().accessibilitySettings().highContrast);
    CHECK(app.shell().accessibilitySettings().reduceAnimation);
    app.shell().setSystemAccessibilitySettings(
        lumen::accessibility::AccessibilitySettings{false, false, 1.0F});
    CHECK_FALSE(app.shell().accessibilitySettings().reduceAnimation);

    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-dpi")->text == "DPI 125%");
    CHECK(findNodeByKey(app.shell().root(), "designer-font-scale")->text ==
          "Font 125%");
    CHECK(findNodeByKey(app.shell().root(), "designer-contrast")->text ==
          "Contrast on");
}

TEST_CASE("designer app runs and stops the current preview session",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto activate = [&](const char* key) {
        const auto* button = findNodeByKey(app.shell().root(), key);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };

    activate("designer-run");
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Preview running  /  current session");
    CHECK(findNodeByKey(app.shell().root(), "designer-stop")->enabled);

    activate("designer-debug");
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Debug preview running  /  current session");
    CHECK(app.shell().frameDebugSnapshot().nodeCount > 0);

    activate("designer-stop");
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Preview stopped");
    CHECK_FALSE(findNodeByKey(app.shell().root(), "designer-stop")->enabled);
}

TEST_CASE("designer app mirrors running preview into an independent shell",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    app.previewShell().setView(Size{960.0F, 640.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"Preview\", "
        "key: \"title\") } }",
        "independent-preview.lumen"));
    (void)app.shell().renderFrame();
    (void)app.previewShell().renderFrame();
    REQUIRE(findNodeByKey(app.previewShell().root(),
                          "designer-preview-status") != nullptr);

    const auto* run = findNodeByKey(app.shell().root(), "designer-run");
    REQUIRE(run != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              run->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.previewShell().renderFrame();
    CHECK(findNodeByKey(app.previewShell().root(), "title") != nullptr);

    const auto* debug = findNodeByKey(app.shell().root(), "designer-debug");
    REQUIRE(debug != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              debug->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();
    (void)app.previewShell().renderFrame();
    CHECK(app.previewShell().frameDebugSnapshot().nodeCount > 0);

    auto broken = *app.workbench().document();
    broken.root.type = "Unknown";
    CHECK_FALSE(app.workbench().openDocument(std::move(broken)));
    run = findNodeByKey(app.shell().root(), "designer-run");
    REQUIRE(run != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              run->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();
    (void)app.previewShell().renderFrame();
    CHECK(findNodeByKey(app.previewShell().root(), "title") != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Run failed  /  kept previous preview");

    const auto* stop = findNodeByKey(app.shell().root(), "designer-stop");
    REQUIRE(stop != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              stop->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.previewShell().renderFrame();
    CHECK(findNodeByKey(app.previewShell().root(),
                        "designer-preview-status") != nullptr);
}

TEST_CASE("designer app cycles keyed interaction state previews",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"Title\", key: \"title\") "
        "Button(\"Save\", key: \"save\", showFocusRing: true) } }",
        "state-preview.lumen"));
    (void)app.shell().renderFrame();
    const auto frameGeneration = app.workbench().frame().generation();

    const auto activatePreview = [&] {
        const auto* button =
            findNodeByKey(app.shell().root(), "designer-preview-state");
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };
    const auto checkState = [&](const char* label, const char* key,
                                lumen::style::WidgetState expected) {
        const auto* button =
            findNodeByKey(app.shell().root(), "designer-preview-state");
        REQUIRE(button != nullptr);
        CHECK(button->text == label);
        const auto context = app.shell().styleContext();
        REQUIRE(context.previewStates != nullptr);
        const auto found = context.previewStates->find(key);
        REQUIRE(found != context.previewStates->end());
        CHECK(found->second == expected);
        CHECK(app.workbench().frame().generation() == frameGeneration);
    };

    const auto* initialPreview =
        findNodeByKey(app.shell().root(), "designer-preview-state");
    REQUIRE(initialPreview != nullptr);
    CHECK(initialPreview->text == "Hover state");
    activatePreview();
    checkState("Press state", "root",
               lumen::style::WidgetState{.hovered = true});

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() >= 2);
    const auto saveId = outline->children[1].id;
    const auto saveHandler =
        app.shell().handlers().find("designer:select:" +
                                    std::to_string(saveId));
    REQUIRE(saveHandler != app.shell().handlers().end());
    saveHandler->second();
    (void)app.shell().renderFrame();
    checkState("Press state", "save",
               lumen::style::WidgetState{.hovered = true});

    activatePreview();
    checkState("Focus state", "save",
               lumen::style::WidgetState{.pressed = true});
    activatePreview();
    checkState("Clear state", "save",
               lumen::style::WidgetState{.focused = true});
    const auto* focusedSave = findNodeByKey(app.shell().root(), "save");
    REQUIRE(focusedSave != nullptr);
    CHECK(focusedSave->commonStyle().focusWidth > 0.0F);
    activatePreview();
    checkState("Hover state", "save", lumen::style::WidgetState{});
}

TEST_CASE("designer app previews interaction state for keyless nodes",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { "
        "Button(\"Save\", showFocusRing: true) } }",
        "keyless-state-preview.lumen"));
    (void)app.shell().renderFrame();

    const auto activatePreview = [&] {
        const auto* button =
            findNodeByKey(app.shell().root(), "designer-preview-state");
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };
    activatePreview();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() == 1);
    const auto keylessId = outline->children.front().id;
    CHECK(outline->children.front().key.empty());
    const auto previewKey = "designer:node:" + std::to_string(keylessId);
    const auto handler = app.shell().handlers().find(
        "designer:select:" + std::to_string(keylessId));
    REQUIRE(handler != app.shell().handlers().end());
    handler->second();
    (void)app.shell().renderFrame();

    const auto context = app.shell().styleContext();
    REQUIRE(context.previewStates != nullptr);
    const auto found = context.previewStates->find(previewKey);
    REQUIRE(found != context.previewStates->end());
    CHECK(found->second == lumen::style::WidgetState{.hovered = true});

    activatePreview();
    const auto pressed = app.shell().styleContext().previewStates->find(previewKey);
    REQUIRE(pressed != app.shell().styleContext().previewStates->end());
    CHECK(pressed->second == lumen::style::WidgetState{.pressed = true});
    activatePreview();
    const auto focused = app.shell().styleContext().previewStates->find(previewKey);
    REQUIRE(focused != app.shell().styleContext().previewStates->end());
    CHECK(focused->second == lumen::style::WidgetState{.focused = true});
    const auto* focusedNode = findNodeByKey(app.shell().root(), previewKey);
    REQUIRE(focusedNode != nullptr);
    CHECK(focusedNode->commonStyle().focusWidth > 0.0F);
}

TEST_CASE("designer app edits declaration properties and routes undo redo",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() >= 1);
    const auto titleId = outline->children.front().id;
    const auto handler = app.shell().handlers().find(
        "designer:select:" + std::to_string(titleId));
    REQUIRE(handler != app.shell().handlers().end());
    handler->second();
    (void)app.shell().renderFrame();

    const std::string bind = "designer:property:" + std::to_string(titleId) +
                             ":text";
    REQUIRE(findNodeByKey(
        app.shell().root(),
        "designer-property-field:" + std::to_string(titleId) + ":text"));
    app.shell().state().set(bind, "Edited in panel");
    (void)app.shell().renderFrame();
    auto properties = app.workbench().properties(titleId);
    REQUIRE(properties.size() == 2);
    CHECK(std::get<std::string>(properties.back().value->value) ==
          "Edited in panel");
    CHECK(app.workbench().dirty());

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'z');
    (void)app.shell().renderFrame();
    properties = app.workbench().properties(titleId);
    CHECK(std::get<std::string>(properties.back().value->value) == "Preview title");
    CHECK_FALSE(app.workbench().dirty());

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'y');
    (void)app.shell().renderFrame();
    properties = app.workbench().properties(titleId);
    CHECK(std::get<std::string>(properties.back().value->value) ==
          "Edited in panel");
    CHECK(app.workbench().dirty());
}

TEST_CASE("designer app routes L0 structure commands",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto initialChildren = initial->children.size();
    const std::vector<std::string> toolboxTypes = {
        "Container", "Row",       "Column",    "Stack",
        "Text",      "Button",    "TextField", "ScrollView",
        "ListView",  "Checkbox",  "Switch",    "FocusScope",
        "Grid",      "Image",      "Icon",      "Slider",
        "ProgressBar", "Radio",    "Tooltip",   "Dropdown",
        "Tabs",      "ThemeScope", "VirtualList", "List",
        "Tree",      "TreeList",   "Splitter", "ToolBar", "StatusBar",
        "DataGrid"};

    const auto activate = [&](const char* key) {
        const auto* button = findNodeByKey(app.shell().root(), key);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };

    for (const auto& type : toolboxTypes) {
        REQUIRE(findNodeByKey(app.shell().root(), "designer-toolbox:" + type));
    }

    activate("designer-add-text");
    auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() == initialChildren + 1);
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto insertedId = *app.workbench().selection().primary;
    CHECK(insertedId != rootId);

    activate("designer-duplicate");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.size() == initialChildren + 2);
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto duplicateId = *app.workbench().selection().primary;
    CHECK(duplicateId != insertedId);

    activate("designer-move-up");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.back().id != duplicateId);

    activate("designer-move-down");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.back().id == duplicateId);

    activate("designer-remove");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.size() == initialChildren + 1);
    CHECK(app.workbench().selection().primary == rootId);

    CHECK(app.undo());
    CHECK(app.workbench().selection().primary == duplicateId);
    CHECK(app.workbench().outline()->children.size() == initialChildren + 2);

    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(rootId));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();
    activate("designer-toolbox:FocusScope");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.size() == initialChildren + 3);
    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(outline->children.back().id ==
          *app.workbench().selection().primary);
    CHECK(outline->children.back().type == "FocusScope");
}

TEST_CASE("designer app creates L1 and L2 toolbox nodes with schema defaults",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(rootId));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    const auto activate = [&](const char* type) {
        const auto* button = findNodeByKey(
            app.shell().root(), std::string{"designer-toolbox:"} + type);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };

    activate("Image");
    auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto imageId = *app.workbench().selection().primary;
    CHECK(outline->children.back().type == "Image");
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-property-field:" + std::to_string(imageId) +
                    ":imageSource") != nullptr);
    app.shell().state().set(
        "designer:property:" + std::to_string(imageId) + ":imageSource",
        "assets/hero.png");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    const auto& imageNode = app.workbench().document()->root.children.back();
    REQUIRE(imageNode.properties.contains("imageSource"));
    CHECK(std::get<std::string>(imageNode.properties.at("imageSource").value) ==
          "assets/hero.png");

    const auto rootHandlerAgain = app.shell().handlers().find(
        "designer:select:" + std::to_string(rootId));
    REQUIRE(rootHandlerAgain != app.shell().handlers().end());
    rootHandlerAgain->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();
    activate("Splitter");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto splitterId = *app.workbench().selection().primary;
    REQUIRE(outline->children.back().type == "Splitter");
    CHECK(outline->children.back().children.size() == 2);
    CHECK(splitterId != imageId);
    CHECK(findNodeByKey(
              app.shell().root(),
              "designer-reference-field:" + std::to_string(splitterId) +
                  ":splitterSource") != nullptr);
    CHECK(app.workbench().diagnostics().empty());
}

TEST_CASE("designer app exposes L2 source references and offline diagnostics",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(rootId));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    const auto* listButton = findNodeByKey(
        app.shell().root(), "designer-toolbox:List");
    REQUIRE(listButton != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              listButton->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto listId = *app.workbench().selection().primary;
    REQUIRE(outline->children.back().type == "List");
    const std::string bind =
        "designer:reference:" + std::to_string(listId) + ":virtualSource";
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-reference-field:" + std::to_string(listId) +
                    ":virtualSource") != nullptr);

    app.shell().state().set(bind, "rows");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document()->root.children.back().references
                .contains("virtualSource"));
    CHECK(app.workbench().document()->root.children.back().references.at(
              "virtualSource") == "rows");
    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().code == "reference.missing");
    CHECK(app.workbench().diagnostics().front().property == "virtualSource");

    app.shell().state().set(bind, "");
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.workbench().document()->root.children.back().references
                    .contains("virtualSource"));
    CHECK(app.workbench().diagnostics().empty());
}

TEST_CASE("designer app previews its first L3 DataGrid component",
          "[designer][d3][app][designer-l3]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(initial->id));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    const auto* dataGridButton = findNodeByKey(
        app.shell().root(), "designer-toolbox:DataGrid");
    REQUIRE(dataGridButton != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              dataGridButton->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    CHECK(outline->children.back().type == "DataGrid");
    CHECK(app.workbench().diagnostics().empty());
    REQUIRE(app.workbench().frame().hasFrame());
    CHECK_FALSE(app.workbench().frame().widget().children.empty());
    CHECK(app.workbench().frame().widget().children.back().key.starts_with(
        "designer:component:DataGrid:"));

    CHECK(app.undo());
    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children.back().type == "Row");
    CHECK(app.redo());
    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children.back().type == "DataGrid");
}

TEST_CASE("designer app previews toolbar and statusbar components",
          "[designer][d3][app][designer-l3]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(initial->id));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    for (const std::string type : {"ToolBar", "StatusBar"}) {
        const auto* button = findNodeByKey(
            app.shell().root(), "designer-toolbox:" + type);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();

        const auto outline = app.workbench().outline();
        REQUIRE(outline.has_value());
        REQUIRE(!outline->children.empty());
        CHECK(outline->children.back().type == type);
        CHECK(app.workbench().diagnostics().empty());
        REQUIRE(app.workbench().frame().hasFrame());
        REQUIRE(!app.workbench().frame().widget().children.empty());
        CHECK(app.workbench().frame().widget().children.back().key.starts_with(
            "designer:component:" + type + ":"));

        CHECK(app.undo());
        REQUIRE(app.workbench().outline().has_value());
        CHECK(app.workbench().outline()->children.back().type == "Row");
    }
}

TEST_CASE("designer app edits named runtime references",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Button(\"Save\", onClick: save, key: \"save\") }",
        "reference-editor.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto buttonId = outline->id;
    const std::string bind =
        "designer:reference:" + std::to_string(buttonId) + ":onClick";
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-reference-field:" +
                              std::to_string(buttonId) + ":onClick"));

    app.shell().state().set(bind, "submit");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document()->root.references.contains("onClick"));
    CHECK(app.workbench().document()->root.references.at("onClick") ==
          "submit");
    CHECK(app.workbench().dirty());

    app.shell().state().set(bind, "not-valid");
    (void)app.shell().renderFrame();
    CHECK(app.shell().state().get(bind) == "submit");
    CHECK(app.workbench().document()->root.references.at("onClick") ==
          "submit");

    app.shell().state().set(bind, "");
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.workbench().document()->root.references.contains("onClick"));
    REQUIRE(app.undo());
    (void)app.shell().renderFrame();
    CHECK(app.workbench().document()->root.references.at("onClick") ==
          "submit");
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-reference-field:" +
                            std::to_string(buttonId) + ":onClick"));
}

TEST_CASE("designer app exposes named references in the center panel",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page refs { Button(\"Save\", bind: prefs_value, "
        "onClick: app_save, key: \"save\") }",
        "references.lumen"));
    (void)app.shell().renderFrame();

    REQUIRE(findNodeByKey(app.shell().root(), "designer-center-tabs") !=
            nullptr);
    const auto sourceTab =
        findNodeByKey(app.shell().root(), "designer-tab-source");
    REQUIRE(sourceTab != nullptr);
    CHECK(sourceTab->enabled);

    app.shell().handlers().at("designer:tab-source")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-source-panel") !=
            nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-source-line-content:1") != nullptr);
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-source-line-content:1")
              ->text.find("Button") != std::string::npos);

    app.shell().handlers().at("designer:tab-references")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-references-panel") !=
            nullptr);
    REQUIRE(findNodeByKey(app.shell().root(), "designer-references") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(), "designer-references-status") !=
            nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-references-status")->text ==
          "2 refs  /  0 missing");

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto saveId = outline->id;
    const auto* row = findNodeByKey(
        app.shell().root(),
        "designer-references:item:designer-reference:" +
            std::to_string(saveId) + ":onClick");
    REQUIRE(row != nullptr);
    CHECK(app.shell().controller().activateCollectionRow(*row));
    CHECK(app.workbench().selection().primary == saveId);
}

TEST_CASE("designer app routes structural keyboard commands",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 2);
    const auto rootId = initial->id;
    const auto titleId = initial->children.front().id;
    const auto saveId = initial->children[1].id;
    const auto selectTitle = app.shell().handlers().find(
        "designer:select:" + std::to_string(titleId));
    REQUIRE(selectTitle != app.shell().handlers().end());
    selectTitle->second();
    (void)app.shell().renderFrame();

    app.shell().keyDown(Key::Down, lumen::core::kModifierCtrl);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().primary == titleId);
    REQUIRE(app.workbench().outline()->children[0].id == saveId);
    CHECK(app.workbench().outline()->children[1].id == titleId);

    app.shell().keyDown(Key::Delete);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == rootId);
    CHECK(app.workbench().outline()->children[0].id == saveId);

    CHECK(app.undo());
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == titleId);
    CHECK(app.workbench().outline()->children[1].id == titleId);
}

TEST_CASE("designer app clears preview state across structural edits",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 1);
    const auto titleId = initial->children.front().id;
    const auto selectTitle = app.shell().handlers().find(
        "designer:select:" + std::to_string(titleId));
    REQUIRE(selectTitle != app.shell().handlers().end());
    selectTitle->second();
    const auto preview = app.shell().handlers().find("designer:preview-state");
    REQUIRE(preview != app.shell().handlers().end());
    preview->second();
    (void)app.shell().renderFrame();

    const auto previewKey = "title";
    REQUIRE(app.shell().styleContext().previewStates != nullptr);
    REQUIRE(app.shell().styleContext().previewStates->contains(previewKey));
    CHECK(app.shell().styleContext().previewStates->at(previewKey).hovered);

    app.shell().keyDown(Key::Delete);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().styleContext().previewStates->contains(previewKey));
    CHECK(app.shell().styleContext().previewStates->at(previewKey) ==
          lumen::style::WidgetState{});

    REQUIRE(app.undo());
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().styleContext().previewStates->contains(previewKey));
    CHECK(app.shell().styleContext().previewStates->at(previewKey) ==
          lumen::style::WidgetState{});
}

TEST_CASE("designer app reorders outline rows by pointer drag",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 2);
    const auto titleId = initial->children.front().id;
    const auto saveId = initial->children[1].id;
    const auto* titleRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + initial->children.front().path);
    const auto* saveRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + initial->children[1].path);
    REQUIRE(titleRow != nullptr);
    REQUIRE(saveRow != nullptr);
    const auto titleOrigin = absoluteOffset(app.shell().root(), titleRow->key);
    const auto saveOrigin = absoluteOffset(app.shell().root(), saveRow->key);
    const Offset titlePoint =
        titleOrigin + Offset{titleRow->size.width * 0.5F,
                             titleRow->size.height * 0.5F};
    const Offset savePoint =
        saveOrigin + Offset{saveRow->size.width * 0.5F,
                            saveRow->size.height * 0.25F};

    app.shell().pointerDown(titlePoint);
    (void)app.shell().renderFrame();
    app.shell().pointerMove(savePoint);
    (void)app.shell().renderFrame();
    CHECK(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(savePoint);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children[0].id == saveId);
    CHECK(app.workbench().outline()->children[1].id == titleId);
    CHECK(app.workbench().selection().primary == titleId);

    REQUIRE(app.undo());
    CHECK(app.workbench().outline()->children[0].id == titleId);
    CHECK(app.workbench().outline()->children[1].id == saveId);
    CHECK(app.workbench().selection().primary == titleId);
}

TEST_CASE("designer app inserts toolbox nodes by pointer drag",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto* toolbox = findNodeByKey(app.shell().root(),
                                        "designer-toolbox:Text");
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(toolbox != nullptr);
    REQUIRE(canvas != nullptr);
    const auto toolboxOrigin = absoluteOffset(app.shell().root(), toolbox->key);
    const auto canvasOrigin = absoluteOffset(app.shell().root(), canvas->key);
    const Offset toolboxPoint =
        toolboxOrigin + Offset{toolbox->size.width * 0.5F,
                               toolbox->size.height * 0.5F};
    const Offset canvasPoint =
        canvasOrigin + Offset{canvas->size.width * 0.5F,
                              canvas->size.height * 0.5F};

    app.shell().pointerDown(toolboxPoint);
    (void)app.shell().renderFrame();
    app.shell().pointerMove(canvasPoint);
    (void)app.shell().renderFrame();
    CHECK(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(canvasPoint);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children.size() ==
          initial->children.size() + 1);
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto insertedId = *app.workbench().selection().primary;
    CHECK(insertedId != rootId);
    CHECK(app.workbench().outline()->children.back().id == insertedId);
    CHECK(app.workbench().outline()->children.back().type == "Text");
}

TEST_CASE("designer app navigates actionable diagnostics to their node",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    auto broken = *app.workbench().document();
    REQUIRE(!broken.root.children.empty());
    const auto childId = broken.root.children.front().id;
    broken.root.children.front().type = "Unknown";
    CHECK_FALSE(app.workbench().openDocument(std::move(broken)));
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().nodeId == childId);
    const auto* diagnostic =
        findNodeByKey(app.shell().root(), "designer-diagnostic:0");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->type == lumen::core::WidgetType::Button);
    CHECK(diagnostic->onClick == "designer:diagnostic:0");
    CHECK(diagnostic->size.width > 0.0F);
    CHECK(diagnostic->size.height > 0.0F);
    const auto origin = absoluteOffset(app.shell().root(), diagnostic->key);
    const Offset center = origin +
                          Offset{diagnostic->size.width * 0.5F,
                                 diagnostic->size.height * 0.5F};
    CAPTURE(origin.x, origin.y, diagnostic->size.width,
            diagnostic->size.height, center.x, center.y);
    std::vector<const lumen::core::RenderNode*> hitChain;
    REQUIRE(lumen::core::hitTestChain(app.shell().root(), center, hitChain));
    REQUIRE(!hitChain.empty());
    CHECK(hitChain.front()->key == "designer-diagnostic:0");
    app.shell().pointerDown(center);
    app.shell().pointerUp(center);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(*app.workbench().selection().primary == childId);
    CHECK(findNodeByKey(app.shell().root(), "designer-source-panel") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-source-line:1") !=
          nullptr);
}

TEST_CASE("designer app reopens private design files",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto path = std::filesystem::temp_directory_path() /
                      ("lumen-designer-app-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       ".design");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};

    REQUIRE(app.saveDesignFile(path.string()));
    const auto savedDocument = app.workbench().document();
    REQUIRE(savedDocument.has_value());
    REQUIRE(app.loadSource("page other { Text(\"Other\") }",
                           "other.lumen"));
    REQUIRE(app.loadDesignFile(path.string()));
    CHECK(app.workbench().document() == savedDocument);
    CHECK(app.workbench().frame().hasFrame());
    CHECK(app.workbench().diagnostics().empty());

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << "{ broken";
    }
    const auto generation = app.workbench().frame().generation();
    CHECK_FALSE(app.loadDesignFile(path.string()));
    CHECK(app.workbench().frame().generation() == generation);
    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().file == path.string());
}

TEST_CASE("designer app routes file dialog commands through one path",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto path = std::filesystem::temp_directory_path() /
                      ("lumen-designer-dialog-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       ".design");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + ".bak", error);
        }
    } cleanup{path};

    bool requestedForSave = false;
    std::string requestedDefaultName;
    app.setFileDialogRequester(
        [&](bool forSave, const std::string& defaultName) {
            requestedForSave = forSave;
            requestedDefaultName = defaultName;
            return std::string{};
        });

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'o');
    CHECK_FALSE(requestedForSave);
    app.handleFileDialogResult({});
    app.shell().keyDown(Key::None,
                        lumen::core::kModifierCtrl | lumen::core::kModifierShift,
                        's');
    CHECK(requestedForSave);
    app.handleFileDialogResult({});

    app.shell().handlers().at("designer:save-as")();
    CHECK(requestedForSave);
    CHECK(requestedDefaultName == "untitled.design");
    app.handleFileDialogResult({path.string()});
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(app.workbench().dirty());

    REQUIRE(app.loadSource("page other { Text(\"Other\") }",
                           "other.lumen"));
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Ready  /  other.lumen");
    app.shell().handlers().at("designer:open")();
    CHECK_FALSE(requestedForSave);
    app.handleFileDialogResult({path.string()});
    CHECK(app.workbench().diagnostics().empty());
    CHECK(app.workbench().document().has_value());

    app.shell().handlers().at("designer:open")();
    app.handleFileDialogResult({}, "cancelled");
    CHECK(findNodeByKey(app.shell().root(), "designer-status") != nullptr);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "File dialog failed: cancelled");
}

TEST_CASE("designer file watcher reloads valid files and keeps the last frame on errors",
          "[designer][d2][watch]") {
    const auto path = std::filesystem::temp_directory_path() /
                      ("lumen-designer-watch-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       ".lumen");
    const auto removeFile = [&path] {
        std::error_code error;
        std::filesystem::remove(path, error);
    };
    struct Cleanup {
        const std::function<void()> remove;
        ~Cleanup() { remove(); }
    } cleanup{removeFile};

    const auto write = [&path](const std::string& source) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        file << source;
        REQUIRE(file.good());
    };
    const auto stamp = [&path](auto offset) {
        std::error_code error;
        const auto current = std::filesystem::last_write_time(path, error);
        REQUIRE_FALSE(error);
        std::filesystem::last_write_time(path, current + offset, error);
        REQUIRE_FALSE(error);
    };

    write("page watch { Text(\"First\", key: \"title\") }");
    FileWatcher watcher(path.string());
    DesignerApp app;
    app.attach();
    REQUIRE(app.loadFile(path.string()));
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    const auto firstGeneration = app.workbench().frame().generation();
    CHECK_FALSE(watcher.poll());

    write("page watch { Text(\"Second\", key: \"title\") }");
    stamp(std::chrono::seconds{1});
    CHECK(watcher.poll());
    REQUIRE(app.loadFile(path.string()));
    (void)app.shell().renderFrame();
    CHECK(app.workbench().frame().generation() > firstGeneration);

    const auto goodGeneration = app.workbench().frame().generation();
    write("page watch {");
    stamp(std::chrono::seconds{2});
    CHECK(watcher.poll());
    CHECK_FALSE(app.loadFile(path.string()));
    CHECK(app.workbench().frame().generation() == goodGeneration);
    REQUIRE(app.workbench().diagnostics().size() == 1);
}
