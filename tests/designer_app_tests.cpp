#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

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
