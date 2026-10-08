#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "designer_app.h"
#include "file_watcher.h"
#include "lumen/accessibility/bridge.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/render_node.h"
#include "lumen/core/splitter.h"
#include "lumen/core/virtual_list.h"
#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_schema.h"
#include "lumen/dsl/project_store.h"
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

void writeRawRgba(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "LUMENRGBA\n2 2\n";
    const std::array<unsigned char, 16> pixels{
        220, 80, 60, 255, 220, 80, 60, 255,
        220, 80, 60, 255, 220, 80, 60, 255};
    file.write(reinterpret_cast<const char*>(pixels.data()),
               static_cast<std::streamsize>(pixels.size()));
}

const lumen::dsl::DesignNode* findDesignNodeById(
    const lumen::dsl::DesignNode& node, lumen::dsl::DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findDesignNodeById(child, id);
            found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findDesignNodeById(child, id);
                found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

const lumen::dsl::DesignPreviewOutlineNode* findOutlineNodeById(
    const lumen::dsl::DesignPreviewOutlineNode& node,
    lumen::dsl::DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findOutlineNodeById(child, id);
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

TEST_CASE("designer app loads authorized image resources into both previews",
          "[designer][f6][resource][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-image-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root / "images", error);
    REQUIRE_FALSE(error);
    writeRawRgba(root / "images" / "hero.lumenrgba");

    DesignerApp app;
    app.setResourceRoot(root);
    app.attach();
    lumen::dsl::DesignDocument document;
    document.documentId = "image-preview";
    document.pageName = "image";
    document.root = lumen::dsl::DesignNode{1, "Column"};
    lumen::dsl::DesignNode image{2, "Image"};
    image.properties["key"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{std::string{"hero"}}};
    image.properties["imageSource"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{
            std::string{"project://images/./hero.lumenrgba"}}};
    image.properties["width"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{80.0}};
    image.properties["height"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{40.0}};
    document.root.children.push_back(std::move(image));
    REQUIRE(app.workbench().openDocument(std::move(document)));

    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    const auto* placeholder = findNodeByKey(app.shell().root(), "hero");
    REQUIRE(placeholder != nullptr);
    CHECK(placeholder->imageId == 0);
    CHECK(app.resourceManager()->diagnostics().requested == 1);
    CHECK(app.diagnostics().empty());

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline &&
           app.resourceManager()->diagnostics().loads == 0) {
        (void)app.resourceManager()->pumpCompletions();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(app.resourceManager()->diagnostics().loads == 1);
    app.shell().markDirty();
    (void)app.shell().renderFrame();
    const auto* loaded = findNodeByKey(app.shell().root(), "hero");
    REQUIRE(loaded != nullptr);
    CHECK(loaded->imageId != 0);
    app.shell().handlers().at("designer:run")();
    app.previewShell().setView(Size{960.0F, 640.0F});
    (void)app.previewShell().renderFrame();
    const auto* previewLoaded =
        findNodeByKey(app.previewShell().root(), "hero");
    REQUIRE(previewLoaded != nullptr);
    CHECK(previewLoaded->imageId != 0);

    std::filesystem::remove_all(root, error);
}

TEST_CASE("designer app reports denied image resources without requesting files",
          "[designer][f6][resource][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-denied-image-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);

    DesignerApp app;
    app.setResourceRoot(root);
    app.attach();
    lumen::dsl::DesignDocument document;
    document.documentId = "denied-image";
    document.pageName = "image";
    document.root = lumen::dsl::DesignNode{1, "Image"};
    document.root.properties["key"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{std::string{"denied"}}};
    document.root.properties["imageSource"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{
            std::string{"https://example.test/hero.png"}}};
    REQUIRE(app.workbench().openDocument(std::move(document)));
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    REQUIRE(app.diagnostics().size() == 1);
    CHECK(app.diagnostics().front().code == "resource.scheme_denied");
    CHECK(app.resourceManager()->diagnostics().requested == 0);
    const auto* denied = findNodeByKey(app.shell().root(), "denied");
    REQUIRE(denied != nullptr);
    CHECK(denied->imageId == 0);

    std::filesystem::remove_all(root, error);
}

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
        "designer-new-project", "designer-open", "designer-save",
        "designer-save-as"};
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
        "designer-toolbox:Splitter",  "designer-toolbox:ComboBox",
        "designer-toolbox:ColorPicker", "designer-toolbox:Spin",
        "designer-toolbox:ToolBar", "designer-toolbox:StatusBar",
        "designer-toolbox:Menu", "designer-toolbox:DialogHost",
        "designer-toolbox:Navigator",
        "designer-toolbox:Form", "designer-toolbox:DataGrid"};
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

TEST_CASE("designer app mirrors extended outline selection into the canvas",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() >= 3);
    const auto firstId = outline->children[0].id;
    const auto secondId = outline->children[1].id;
    const auto thirdId = outline->children[2].id;
    const auto* firstRow = findNodeByKey(
        app.shell().root(),
        "designer-outline:item:" + outline->children[0].path);
    const auto* thirdRow = findNodeByKey(
        app.shell().root(),
        "designer-outline:item:" + outline->children[2].path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(thirdRow != nullptr);

    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto thirdOrigin = absoluteOffset(app.shell().root(), thirdRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset thirdPoint =
        thirdOrigin + Offset{thirdRow->size.width * 0.5F,
                             thirdRow->size.height * 0.5F};

    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId});

    app.shell().pointerDown(thirdPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(thirdPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == thirdId);
    CHECK(app.workbench().selection().ids.contains(firstId));
    CHECK(app.workbench().selection().ids.contains(secondId));
    CHECK(app.workbench().selection().ids.contains(thirdId));

    const auto* firstCanvas = findNodeByKey(
        app.shell().root(), outline->children[0].key);
    const auto* secondCanvas = findNodeByKey(
        app.shell().root(), outline->children[1].key);
    const auto* thirdCanvas = findNodeByKey(
        app.shell().root(), outline->children[2].key);
    REQUIRE(firstCanvas != nullptr);
    REQUIRE(secondCanvas != nullptr);
    REQUIRE(thirdCanvas != nullptr);
    CHECK(firstCanvas->selected);
    CHECK(secondCanvas->selected);
    CHECK(thirdCanvas->selected);

    const auto* secondRow = findNodeByKey(
        app.shell().root(),
        "designer-outline:item:" + outline->children[1].path);
    REQUIRE(secondRow != nullptr);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(secondPoint, lumen::core::kModifierCtrl);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.workbench().selection().ids.contains(secondId));
    CHECK_FALSE(findNodeByKey(app.shell().root(), outline->children[1].key)
                    ->selected);
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

TEST_CASE("designer app frame-selects canvas siblings",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() >= 2);
    const auto firstId = outline->children[0].id;
    const auto secondId = outline->children[1].id;
    const auto* first = findNodeByKey(app.shell().root(), "title");
    const auto* second = findNodeByKey(app.shell().root(), "save");
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(canvas != nullptr);
    const auto canvasOrigin = absoluteOffset(app.shell().root(), canvas->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), second->key);
    const Offset start = canvasOrigin + Offset{4.0F, 4.0F};
    const Offset end = secondOrigin +
                       Offset{std::max(1.0F, second->size.width - 1.0F),
                              std::max(1.0F, second->size.height - 1.0F)};

    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    REQUIRE(app.shell().overlayRoot() != nullptr);
    REQUIRE(findNodeByKey(*app.shell().overlayRoot(),
                          "designer-canvas-selection-rect") != nullptr);
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    CHECK(app.workbench().selection().primary == secondId);
    CHECK(app.shell().overlayRoot() == nullptr);
}

TEST_CASE("designer app transforms canvas preview without editing the document",
          "[designer][d3][app][designer-transform]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"Title\", key: "
        "\"node\", width: 120, height: 30) } }",
        "transform.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const std::string nodeKey = outline->children.front().key;
    REQUIRE_FALSE(nodeKey.empty());
    const auto* before = findNodeByKey(app.shell().root(), nodeKey);
    REQUIRE(before != nullptr);
    const auto beforeSize = before->size;
    const auto beforeOrigin = absoluteOffset(app.shell().root(), nodeKey);
    const auto revision = app.workbench().documentRevision();
    const auto initialGeneration = app.workbench().frame().generation();
    CHECK_FALSE(app.workbench().dirty());

    app.shell().handlers().at("designer:zoom-in")();
    (void)app.shell().renderFrame();
    const auto* zoomed = findNodeByKey(app.shell().root(), nodeKey);
    REQUIRE(zoomed != nullptr);
    CHECK(zoomed->size.width == Catch::Approx(beforeSize.width * 1.1F));
    CHECK(zoomed->size.height == Catch::Approx(beforeSize.height * 1.1F));
    CHECK(app.workbench().documentRevision() == revision);
    CHECK_FALSE(app.workbench().dirty());
    CHECK(app.workbench().frame().generation() > initialGeneration);
    const auto zoomGeneration = app.workbench().frame().generation();

    app.shell().handlers().at("designer:pan-right")();
    (void)app.shell().renderFrame();
    const auto pannedOrigin = absoluteOffset(app.shell().root(), nodeKey);
    CHECK(pannedOrigin.x > beforeOrigin.x);
    CHECK(app.workbench().documentRevision() == revision);
    CHECK_FALSE(app.workbench().dirty());
    CHECK(app.workbench().frame().generation() == zoomGeneration);

    app.shell().handlers().at("designer:zoom-reset")();
    (void)app.shell().renderFrame();
    const auto* reset = findNodeByKey(app.shell().root(), nodeKey);
    REQUIRE(reset != nullptr);
    const auto resetOrigin = absoluteOffset(app.shell().root(), nodeKey);
    CHECK(reset->size.width == Catch::Approx(beforeSize.width));
    CHECK(reset->size.height == Catch::Approx(beforeSize.height));
    CHECK(resetOrigin.x == Catch::Approx(beforeOrigin.x));
    CHECK(resetOrigin.y == Catch::Approx(beforeOrigin.y));
    CHECK(app.workbench().documentRevision() == revision);
    CHECK_FALSE(app.workbench().dirty());
    CHECK(app.workbench().frame().generation() > zoomGeneration);
}

TEST_CASE("designer app selects transformed canvas nodes across dpi and zoom",
          "[designer][d3][app][designer-transform]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"Title\", key: "
        "\"node\", width: 120, height: 30) } }",
        "transform-selection.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto nodeId = outline->children.front().id;
    const auto revision = app.workbench().documentRevision();
    const auto selectTransformedNode = [&] {
        const auto* node = findNodeByKey(app.shell().root(), "node");
        REQUIRE(node != nullptr);
        const auto origin = absoluteOffset(app.shell().root(), "node");
        const Offset center{origin.x + node->size.width * 0.5F,
                            origin.y + node->size.height * 0.5F};
        app.shell().pointerDown(center);
        app.shell().pointerUp(center);
        (void)app.shell().renderFrame();
        REQUIRE(app.workbench().selection().primary.has_value());
        CHECK(*app.workbench().selection().primary == nodeId);
        CHECK(app.workbench().selection().ids.size() == 1);
        CHECK_FALSE(app.workbench().dirty());
    };

    selectTransformedNode();
    app.shell().handlers().at("designer:dpi")();
    (void)app.shell().renderFrame();
    CHECK(app.shell().styleContext().deviceScale == 1.25F);
    app.shell().handlers().at("designer:zoom-in")();
    (void)app.shell().renderFrame();
    selectTransformedNode();

    app.shell().handlers().at("designer:dpi")();
    (void)app.shell().renderFrame();
    CHECK(app.shell().styleContext().deviceScale == 2.0F);
    app.shell().handlers().at("designer:zoom-in")();
    (void)app.shell().renderFrame();
    selectTransformedNode();
    CHECK(app.workbench().documentRevision() == revision);
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
    app.shell().keyDown(Key::Escape);
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

TEST_CASE("designer app scopes interaction preview to duplicate keyed nodes",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column { Column { Button(\"First\", key: \"dup\") } "
        "Row { Button(\"Second\", key: \"dup\") } } }",
        "duplicate-key-preview.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() == 2);
    REQUIRE(outline->children[0].children.size() == 1);
    REQUIRE(outline->children[1].children.size() == 1);
    const auto firstId = outline->children[0].children[0].id;
    const auto secondId = outline->children[1].children[0].id;
    const auto firstKey = "designer:node:" + std::to_string(firstId);
    const auto secondKey = "designer:node:" + std::to_string(secondId);
    REQUIRE(findNodeByKey(app.shell().root(), firstKey) != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(), secondKey) != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "dup") == nullptr);

    const auto preview = app.shell().handlers().find("designer:preview-state");
    REQUIRE(preview != app.shell().handlers().end());
    const auto firstSelect = app.shell().handlers().find(
        "designer:select:" + std::to_string(firstId));
    REQUIRE(firstSelect != app.shell().handlers().end());
    firstSelect->second();
    preview->second();
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().styleContext().previewStates != nullptr);
    CHECK(app.shell().styleContext().previewStates->at(firstKey).hovered);
    CHECK(app.shell().styleContext().previewStates->find(secondKey) ==
          app.shell().styleContext().previewStates->end());

    const auto secondSelect = app.shell().handlers().find(
        "designer:select:" + std::to_string(secondId));
    REQUIRE(secondSelect != app.shell().handlers().end());
    secondSelect->second();
    (void)app.shell().renderFrame();
    CHECK(app.shell().styleContext().previewStates->at(firstKey) ==
          lumen::style::WidgetState{});
    CHECK(app.shell().styleContext().previewStates->at(secondKey).hovered);
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

TEST_CASE("designer app edits a shared multi-selection property in one transaction",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"A\", key: \"a\") "
        "Text(\"B\", key: \"b\") Button(\"C\", key: \"c\") } }",
        "multi-property.lumen"));
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto firstId = children[0].id;
    const auto secondId = children[1].id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().ids ==
            std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    const std::string bind =
        "designer:property:" + std::to_string(secondId) + ":text";
    REQUIRE(findNodeByKey(
        app.shell().root(),
        "designer-property-field:" + std::to_string(secondId) + ":text"));
    app.shell().state().set(bind, "Shared");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(std::get<std::string>(
              app.workbench().document()->root.children[0].properties.at("text").value) ==
          "Shared");
    CHECK(std::get<std::string>(
              app.workbench().document()->root.children[1].properties.at("text").value) ==
          "Shared");
    CHECK(app.workbench().canUndo());
    REQUIRE(app.undo());
    CHECK(std::get<std::string>(
              app.workbench().document()->root.children[0].properties.at("text").value) ==
          "A");
    CHECK(std::get<std::string>(
              app.workbench().document()->root.children[1].properties.at("text").value) ==
          "B");
    CHECK_FALSE(app.workbench().dirty());
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
        "Tree",      "TreeList",   "Splitter", "ComboBox", "ColorPicker",
        "Spin",      "ToolBar",    "StatusBar", "Menu", "DialogHost",
        "Navigator",
        "Form",      "DataGrid"};

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

TEST_CASE("designer app copies and pastes a selected node as one transaction",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 2);
    const auto sourceId = initial->children.front().id;
    const auto select = app.shell().handlers().find(
        "designer:select:" + std::to_string(sourceId));
    REQUIRE(select != app.shell().handlers().end());
    select->second();
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == sourceId);
    CHECK_FALSE(app.shell().focus().focusedKey().starts_with(
        "designer-property-field:"));

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'c');
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Copied Text");
    CHECK_FALSE(app.shell().focus().focusedKey().starts_with(
        "designer-property-field:"));
    CHECK_FALSE(app.shell().focus().focusedKey().starts_with(
        "designer-reference-field:"));
    CHECK(app.workbench().selection().primary == sourceId);
    CHECK_FALSE(app.workbench().dirty());
    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'v');
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Pasted Text");

    const auto afterPaste = app.workbench().outline();
    REQUIRE(afterPaste.has_value());
    REQUIRE(afterPaste->children.size() == initial->children.size() + 1);
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto pastedId = *app.workbench().selection().primary;
    CHECK(pastedId != sourceId);
    CHECK(afterPaste->children[1].id == pastedId);
    CHECK(afterPaste->children[1].type == initial->children.front().type);
    CHECK(app.workbench().dirty());

    REQUIRE(app.undo());
    (void)app.shell().renderFrame();
    CHECK(app.workbench().outline()->children.size() == initial->children.size());
    CHECK(app.workbench().selection().primary == sourceId);
}

TEST_CASE("designer app copies multi-selection across parents as one transaction",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Row(key: \"source\") { "
        "Text(\"A\", key: \"a\") Button(\"B\", key: \"b\") } "
        "Column(key: \"target\") { Text(\"Existing\", key: \"existing\") } "
        "} }",
        "multi-copy.lumen"));
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& root = app.workbench().document()->root;
    REQUIRE(root.children.size() == 2);
    const auto& source = root.children[0];
    const auto& target = root.children[1];
    REQUIRE(source.children.size() == 2);
    REQUIRE(target.children.size() == 1);
    const auto firstId = source.children[0].id;
    const auto secondId = source.children[1].id;
    const auto targetId = target.id;
    const auto targetChildId = target.children.front().id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'c');
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Copied 2 nodes");
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    CHECK_FALSE(app.workbench().dirty());

    const auto targetOutline = findOutlineNodeById(*outline, targetChildId);
    REQUIRE(targetOutline != nullptr);
    const auto* targetRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + targetOutline->path);
    REQUIRE(targetRow != nullptr);
    const auto targetOrigin = absoluteOffset(app.shell().root(), targetRow->key);
    const Offset targetPoint =
        targetOrigin + Offset{targetRow->size.width * 0.5F,
                              targetRow->size.height * 0.5F};
    app.shell().pointerDown(targetPoint);
    app.shell().pointerUp(targetPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == targetChildId);
    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'v');
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Pasted 2 nodes");

    REQUIRE(app.workbench().document().has_value());
    const auto* pastedTarget = findDesignNodeById(
        app.workbench().document()->root, targetId);
    REQUIRE(pastedTarget != nullptr);
    REQUIRE(pastedTarget->children.size() == 3);
    const auto pastedFirstId = pastedTarget->children[1].id;
    const auto pastedSecondId = pastedTarget->children[2].id;
    CHECK(pastedFirstId != firstId);
    CHECK(pastedSecondId != secondId);
    CHECK(pastedTarget->children[1].type == "Text");
    CHECK(pastedTarget->children[2].type == "Button");
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{pastedFirstId, pastedSecondId});
    CHECK(app.workbench().selection().primary == pastedSecondId);
    CHECK(app.workbench().dirty());

    REQUIRE(app.undo());
    REQUIRE(app.workbench().document().has_value());
    pastedTarget = findDesignNodeById(app.workbench().document()->root,
                                      targetId);
    REQUIRE(pastedTarget != nullptr);
    CHECK(pastedTarget->children.size() == 1);
    CHECK(app.workbench().selection().primary == targetChildId);

    REQUIRE(app.redo());
    REQUIRE(app.workbench().document().has_value());
    pastedTarget = findDesignNodeById(app.workbench().document()->root,
                                      targetId);
    REQUIRE(pastedTarget != nullptr);
    CHECK(pastedTarget->children.size() == 3);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{pastedFirstId, pastedSecondId});
}

TEST_CASE("designer app duplicates multi-selection with unique keys",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"A\", key: \"a\") "
        "Button(\"B\", key: \"b\") Text(\"C\", key: \"c\") } }",
        "multi-duplicate.lumen"));
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto firstId = children[0].id;
    const auto secondId = children[1].id;
    const auto thirdId = children[2].id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().ids ==
            std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    app.shell().handlers().at("designer:duplicate")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Duplicated 2 nodes");
    REQUIRE(app.workbench().document().has_value());
    const auto& duplicated = app.workbench().document()->root.children;
    REQUIRE(duplicated.size() == 5);
    const auto duplicateFirstId = duplicated[2].id;
    const auto duplicateSecondId = duplicated[3].id;
    CHECK(duplicateFirstId != firstId);
    CHECK(duplicateSecondId != secondId);
    CHECK(std::get<std::string>(
              duplicated[2].properties.at("key").value) == "a-copy");
    CHECK(std::get<std::string>(
              duplicated[3].properties.at("key").value) == "b-copy");
    CHECK(duplicated[4].id == thirdId);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{duplicateFirstId,
                                               duplicateSecondId});

    REQUIRE(app.undo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 3);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    REQUIRE(app.redo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 5);
    CHECK(app.workbench().selection().primary == duplicateSecondId);
}

TEST_CASE("designer app deletes multi-selection as one transaction",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"A\", key: \"a\") "
        "Button(\"B\", key: \"b\") Text(\"C\", key: \"c\") } }",
        "multi-delete.lumen"));
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto firstId = children[0].id;
    const auto secondId = children[1].id;
    const auto rootId = app.workbench().document()->root.id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    app.shell().keyDown(Key::Delete);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Removed 2 nodes");
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 1);
    CHECK(app.workbench().document()->root.children.front().id != firstId);
    CHECK(app.workbench().selection().primary == rootId);

    REQUIRE(app.undo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 3);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    REQUIRE(app.redo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 1);
    CHECK(app.workbench().selection().primary == rootId);
}

TEST_CASE("designer app moves multi-selection as one transaction",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"A\", key: \"a\") "
        "Button(\"B\", key: \"b\") Text(\"C\", key: \"c\") } }",
        "multi-move.lumen"));
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto firstId = children[0].id;
    const auto secondId = children[1].id;
    const auto thirdId = children[2].id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().ids ==
            std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    app.shell().handlers().at("designer:move-down")();
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children[0].id == thirdId);
    CHECK(app.workbench().document()->root.children[1].id == firstId);
    CHECK(app.workbench().document()->root.children[2].id == secondId);
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Moved 2 nodes");
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    REQUIRE(app.undo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children[0].id == firstId);
    CHECK(app.workbench().document()->root.children[1].id == secondId);
    CHECK(app.workbench().document()->root.children[2].id == thirdId);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    REQUIRE(app.redo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children[0].id == thirdId);
    CHECK(app.workbench().selection().primary == secondId);
    CHECK(app.workbench().selection().anchor == secondId);
}

TEST_CASE("designer gallery covers every registered schema without diagnostics",
          "[designer][d2][gallery]") {
    DesignerApp app;
    app.attach();
    const auto gallery = std::filesystem::path{__FILE__}
                              .parent_path()
                              .parent_path() /
                          "examples/designer/gallery.design";
    REQUIRE(app.loadDesignFile(gallery.string()));
    CHECK(app.diagnostics().empty());

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    std::set<std::string> types;
    std::function<void(const lumen::dsl::DesignPreviewOutlineNode&)> visit =
        [&](const lumen::dsl::DesignPreviewOutlineNode& node) {
            types.insert(node.type);
            for (const auto& child : node.children) visit(child);
        };
    visit(*outline);
    const auto& schemas = lumen::dsl::nodeSchemaRegistry();
    CHECK(types.size() == schemas.size());
    for (const auto& schema : schemas) {
        CHECK(types.contains(schema.type));
    }
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

TEST_CASE("designer app previews deterministic L2 source adapters",
          "[designer][d3][app][designer-l2]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto rootId = outline->id;
    const auto selectRoot = [&] {
        const auto handler = app.shell().handlers().find(
            "designer:select:" + std::to_string(rootId));
        REQUIRE(handler != app.shell().handlers().end());
        handler->second();
        (void)app.shell().renderFrame();
    };
    const auto activate = [&](const char* type) {
        const auto* button = findNodeByKey(
            app.shell().root(), std::string{"designer-toolbox:"} + type);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };

    selectRoot();
    activate("List");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto listId = *app.workbench().selection().primary;
    const std::string listReference =
        "designer:reference:" + std::to_string(listId) + ":virtualSource";
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-reference-field:" + std::to_string(listId) +
                    ":virtualSource") != nullptr);
    app.shell().state().set(listReference, "preview_rows");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.back().references.at(
              "virtualSource") == "preview_rows");
    CHECK(app.workbench().diagnostics().empty());
    const auto* list = findNodeByKey(
        app.shell().root(), "designer:node:" + std::to_string(listId));
    REQUIRE(list != nullptr);
    REQUIRE(list->virtualSource != nullptr);
    CHECK(list->virtualSource->itemCount() == 12);
    CHECK(list->children.size() > 0);
    const auto* listSource =
        dynamic_cast<const lumen::core::VirtualListController*>(list->virtualSource);
    REQUIRE(listSource != nullptr);
    CHECK(listSource->lastMaterializedItems() == list->children.size());
    CHECK(listSource->peakMaterializedItems() >=
          listSource->lastMaterializedItems());
    const auto* dynamicRow = findNodeByKey(
        app.shell().root(), "designer-preview-row:0");
    REQUIRE(dynamicRow != nullptr);
    CHECK(dynamicRow->onClick == "designer:select:" +
                                  std::to_string(listId));
    selectRoot();
    dynamicRow = findNodeByKey(app.shell().root(), "designer-preview-row:0");
    REQUIRE(dynamicRow != nullptr);
    const auto rowOrigin = absoluteOffset(app.shell().root(),
                                           "designer-preview-row:0");
    const Offset rowCenter{rowOrigin.x + dynamicRow->size.width * 0.5F,
                           rowOrigin.y + dynamicRow->size.height * 0.5F};
    app.shell().pointerDown(rowCenter);
    app.shell().pointerUp(rowCenter);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(*app.workbench().selection().primary == listId);
    CHECK(app.workbench().selection().ids.size() == 1);
    REQUIRE(app.workbench().frame().session() != nullptr);
    CHECK(app.workbench().frame().session()->leaseCount() == 1);

    selectRoot();
    activate("Splitter");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto splitterId = *app.workbench().selection().primary;
    const std::string splitterReference =
        "designer:reference:" + std::to_string(splitterId) +
        ":splitterSource";
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-reference-field:" + std::to_string(splitterId) +
                    ":splitterSource") != nullptr);
    app.shell().state().set(splitterReference, "preview_splitter");
    (void)app.shell().renderFrame();
    CHECK(app.workbench().diagnostics().empty());
    const auto* splitter = findNodeByKey(
        app.shell().root(),
        "split:div:designer:node:" + std::to_string(splitterId));
    REQUIRE(splitter != nullptr);
    REQUIRE(splitter->splitterSource != nullptr);
    CHECK(app.workbench().frame().session()->leaseCount() == 2);
    const auto document = lumen::dsl::serializeDesignDocument(
        *app.workbench().document());
    const auto revision = app.workbench().documentRevision();
    const auto oldSession = app.workbench().frame().session();
    splitter->splitterSource->stepBy(16.0F);
    CHECK(lumen::dsl::serializeDesignDocument(*app.workbench().document()) ==
          document);
    CHECK(app.workbench().documentRevision() == revision);

    app.shell().handlers().at("designer:run")();
    (void)app.shell().renderFrame();
    CHECK_FALSE(oldSession->active());
    CHECK(oldSession->leaseCount() == 0);
    CHECK(app.workbench().frame().session()->leaseCount() == 2);
    CHECK(lumen::dsl::serializeDesignDocument(*app.workbench().document()) ==
          document);
    CHECK(app.workbench().documentRevision() == revision);

    app.shell().handlers().at("designer:select:" + std::to_string(listId))();
    (void)app.shell().renderFrame();
    app.shell().handlers().at("designer:duplicate")();
    (void)app.shell().renderFrame();
    std::vector<const lumen::core::VirtualListSource*> sources;
    for (const auto& child : app.workbench().frame().widget().children) {
        if (child.virtualSource != nullptr) sources.push_back(child.virtualSource);
    }
    REQUIRE(sources.size() == 2);
    CHECK(sources[0] != sources[1]);
    CHECK(sources[0]->scrollController() != sources[1]->scrollController());
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

TEST_CASE("designer app previews input component adapters",
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

    for (const std::string type : {"ComboBox", "ColorPicker", "Spin"}) {
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

TEST_CASE("designer app previews menu navigator and form adapters",
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

    for (const std::string type : {"Menu", "DialogHost", "Navigator", "Form"}) {
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
        const auto id = outline->children.back().id;
        CHECK(outline->children.back().type == type);
        CHECK(app.workbench().diagnostics().empty());
        REQUIRE(app.workbench().frame().hasFrame());
        REQUIRE(!app.workbench().frame().widget().children.empty());
        const std::string prefix =
            "designer:component:" + type + ":" + std::to_string(id) + ":";
        CHECK(app.workbench().frame().widget().children.back().key.starts_with(
            prefix));

        if (type == "DialogHost") {
            const auto* open = findNodeByKey(
                app.shell().root(), prefix + "dialog-open-button");
            REQUIRE(open != nullptr);
            CHECK(app.shell().performAccessibilityAction(
                      open->identity, kActionActivate) ==
                  lumen::accessibility::SemanticsActionStatus::Handled);
            (void)app.shell().renderFrame();
            REQUIRE(findNodeByKey(app.shell().root(), "dialog-host") != nullptr);
            app.shell().keyDown(Key::Escape);
            (void)app.shell().renderFrame();
            CHECK(findNodeByKey(app.shell().root(), "dialog-host") == nullptr);
        } else if (type == "Navigator") {
            const auto* routeButton = findNodeByKey(
                app.shell().root(), prefix + "navigator-button:details");
            REQUIRE(routeButton != nullptr);
            CHECK(app.shell().performAccessibilityAction(
                      routeButton->identity, kActionActivate) ==
                  lumen::accessibility::SemanticsActionStatus::Handled);
            (void)app.shell().renderFrame();
            const auto* routeLabel =
                findNodeByKey(app.shell().root(), prefix + "navigator-route");
            REQUIRE(routeLabel != nullptr);
            CHECK(routeLabel->text == "Route: details");
        } else if (type == "Form") {
            const auto* submit =
                findNodeByKey(app.shell().root(), prefix + "submit-button");
            REQUIRE(submit != nullptr);
            CHECK(app.shell().performAccessibilityAction(
                      submit->identity, kActionActivate) ==
                  lumen::accessibility::SemanticsActionStatus::Handled);
            (void)app.shell().renderFrame();
            const auto* error =
                findNodeByKey(app.shell().root(), prefix + "form-name-error");
            REQUIRE(error != nullptr);
            CHECK(error->text == "Name is required");
        }

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
    const auto rootId = initial->id;
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
    CHECK(app.workbench().selection().primary == rootId);
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

TEST_CASE("designer app cancels outline drag with Escape",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 2);
    const auto rootId = initial->id;
    const auto firstId = initial->children[0].id;
    const auto secondId = initial->children[1].id;
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + initial->children[0].path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + initial->children[1].path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.25F};

    app.shell().pointerDown(firstPoint);
    app.shell().pointerMove(secondPoint);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children[0].id == firstId);
    CHECK(app.workbench().outline()->children[1].id == secondId);
    CHECK(app.workbench().selection().primary == rootId);
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

TEST_CASE("designer app opens, switches, and saves a multi document project",
          "[designer][dp9][d3][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-project-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    std::filesystem::create_directories(root / "images", error);
    REQUIRE_FALSE(error);
    {
        std::ofstream resource(root / "images" / "logo.png",
                               std::ios::binary | std::ios::trunc);
        REQUIRE(resource.good());
        resource << "designer-logo";
    }
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};

    DesignerApp seed;
    seed.attach();
    REQUIRE(seed.loadSource("page home { Text(\"Home\") }", "home.lumen"));
    REQUIRE(seed.saveDesignFile((root / "home.design").string()));
    const auto homeId = seed.workbench().document()->documentId;
    REQUIRE(seed.loadSource("page settings { Text(\"Settings\") }",
                            "settings.lumen"));
    REQUIRE(seed.saveDesignFile((root / "settings.design").string()));
    const auto settingsId = seed.workbench().document()->documentId;

    lumen::dsl::DesignProject project;
    project.projectId = "designer.project";
    project.name = "Designer project";
    project.root = ".";
    project.pages = {
        {homeId, "home.design", "Home"},
        {settingsId, "settings.design", "Settings"},
    };
    project.resources = {
        {"project://images/logo.png", "images/logo.png", "image"},
    };
    lumen::dsl::ProjectStore projectStore;
    std::vector<lumen::dsl::DesignError> diagnostics;
    const auto manifest = root / "demo.lumen-project";
    REQUIRE(projectStore.save(manifest.string(), project, diagnostics));

    DesignerApp app;
    app.attach();
    REQUIRE(app.loadProjectFile(manifest.string()));
    REQUIRE(app.project().has_value());
    CHECK(app.project()->pages.size() == 2);
    CHECK(app.activeProjectDocumentId() == homeId);
    CHECK(app.workbench().document()->documentId == homeId);
    CHECK(app.projectDiagnostics().empty());

    REQUIRE(app.switchProjectDocument(settingsId));
    CHECK(app.activeProjectDocumentId() == settingsId);
    CHECK(app.workbench().document()->pageName == "settings");
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-project-panel") != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-project-page:" + settingsId) !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-project-resources-heading") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-project-resource:0") !=
          nullptr);

    REQUIRE(app.saveProjectFile(manifest.string()));
    const auto saved = projectStore.load(manifest.string());
    REQUIRE(saved.ok());
    CHECK(saved.project.pages[1].documentId == settingsId);

    REQUIRE(app.switchProjectDocument(homeId));
    REQUIRE(app.workbench().setProperty(
        app.workbench().document()->root.id, "text",
        lumen::dsl::DesignValue{
            lumen::dsl::DesignValue::Variant{std::string{"Edited home"}}}));
    REQUIRE(app.saveProjectFile(manifest.string()));
    CHECK_FALSE(app.workbench().dirty());
    REQUIRE(app.workbench().setProperty(
        app.workbench().document()->root.id, "text",
        lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
            std::string{"Edited home again"}}}));
    const auto homePath = root / "home.design";
    const auto settingsPath = root / "settings.design";
    lumen::dsl::DocumentStore documentStore;
    auto externalSettings = documentStore.load(settingsPath.string());
    REQUIRE(externalSettings.ok());
    externalSettings.document.pageName = "External settings";
    std::vector<lumen::dsl::DesignError> externalDiagnostics;
    REQUIRE(documentStore.save(settingsPath.string(),
                               externalSettings.document, externalDiagnostics));
    const auto homeBeforeConflict = documentStore.load(homePath.string());
    REQUIRE(homeBeforeConflict.ok());
    CHECK_FALSE(app.saveProjectFile(manifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "store.revision_conflict");
    const auto homeAfterConflict = documentStore.load(homePath.string());
    REQUIRE(homeAfterConflict.ok());
    CHECK(homeAfterConflict.document == homeBeforeConflict.document);

    const auto copyRoot = root / "copy";
    std::filesystem::create_directories(copyRoot, error);
    REQUIRE_FALSE(error);
    const auto copyManifest = copyRoot / "copy.lumen-project";
    REQUIRE(app.saveProjectFile(copyManifest.string()));
    CHECK(app.projectDiagnostics().empty());
    CHECK(std::filesystem::exists(copyRoot / "home.design"));
    CHECK(std::filesystem::exists(copyRoot / "settings.design"));
    REQUIRE(std::filesystem::exists(copyRoot / "images" / "logo.png"));
    std::ifstream copiedResource(copyRoot / "images" / "logo.png",
                                 std::ios::binary);
    REQUIRE(copiedResource.good());
    CHECK(std::string{std::istreambuf_iterator<char>{copiedResource},
                      std::istreambuf_iterator<char>()} == "designer-logo");

    DesignerApp copied;
    copied.attach();
    REQUIRE(copied.loadProjectFile(copyManifest.string()));
    CHECK(copied.activeProjectDocumentId() == homeId);
    CHECK(copied.workbench().document()->root.properties.at("text") ==
          lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
              std::string{"Edited home again"}}});

    auto unsafeResourceProject = project;
    unsafeResourceProject.projectId = "unsafe.resource";
    unsafeResourceProject.name = "Unsafe resource";
    unsafeResourceProject.resources.front().path = "../outside.png";
    const auto unsafeManifest = copyRoot / "unsafe-resource.lumen-project";
    std::vector<lumen::dsl::DesignError> unsafeDiagnostics;
    REQUIRE(projectStore.save(unsafeManifest.string(), unsafeResourceProject,
                              unsafeDiagnostics));
    REQUIRE(app.loadProjectFile(unsafeManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "project.resource_path");
    CHECK_FALSE(app.saveProjectFile(unsafeManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "project.resource_path");
    REQUIRE(app.loadProjectFile(copyManifest.string()));

    auto missingResourceProject = project;
    missingResourceProject.projectId = "missing.resource";
    missingResourceProject.name = "Missing resource";
    missingResourceProject.resources.front().path = "images/missing.png";
    const auto missingResourceManifest = copyRoot / "missing-resource.lumen-project";
    std::vector<lumen::dsl::DesignError> missingResourceDiagnostics;
    REQUIRE(projectStore.save(missingResourceManifest.string(),
                              missingResourceProject,
                              missingResourceDiagnostics));
    REQUIRE(app.loadProjectFile(missingResourceManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "project.resource_missing");
    REQUIRE(app.loadProjectFile(copyManifest.string()));

    auto directoryResourceProject = project;
    directoryResourceProject.projectId = "directory.resource";
    directoryResourceProject.name = "Directory resource";
    directoryResourceProject.resources.front().path = "images";
    const auto directoryResourceManifest =
        copyRoot / "directory-resource.lumen-project";
    std::vector<lumen::dsl::DesignError> directoryResourceDiagnostics;
    REQUIRE(projectStore.save(directoryResourceManifest.string(),
                              directoryResourceProject,
                              directoryResourceDiagnostics));
    REQUIRE(app.loadProjectFile(directoryResourceManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "project.resource_type");
    REQUIRE(app.loadProjectFile(copyManifest.string()));

    const auto badManifest = copyRoot / "missing-pages.lumen-project";
    auto missingProject = project;
    missingProject.projectId = "missing.pages";
    missingProject.name = "Missing pages";
    missingProject.pages.front().path = "missing/home.design";
    missingProject.pages.back().path = "missing/settings.design";
    std::vector<lumen::dsl::DesignError> missingDiagnostics;
    REQUIRE(projectStore.save(badManifest.string(), missingProject,
                              missingDiagnostics));
    CHECK_FALSE(app.loadProjectFile(badManifest.string()));
    REQUIRE(app.project().has_value());
    CHECK(app.project()->projectId == project.projectId);
    CHECK(app.activeProjectDocumentId() == homeId);
    CHECK(app.workbench().document()->documentId == homeId);
    CHECK(app.saveProjectFile(copyManifest.string()));
}

TEST_CASE("designer project diagnostics switch to the affected document",
          "[designer][dp9][d3][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-project-diagnostics-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};

    const auto homePath = root / "home.design";
    const auto settingsPath = root / "settings.design";
    DesignerApp seed;
    seed.attach();
    REQUIRE(seed.loadSource("page home { Text(\"Home\") }", "home.lumen"));
    REQUIRE(seed.saveDesignFile(homePath.string()));
    const auto homeId = seed.workbench().document()->documentId;
    REQUIRE(seed.loadSource("page settings { Text(\"Settings\") }",
                            "settings.lumen"));
    REQUIRE(seed.saveDesignFile(settingsPath.string()));
    REQUIRE(seed.saveDesignFile(settingsPath.string()));
    const auto settingsId = seed.workbench().document()->documentId;

    std::ofstream(settingsPath, std::ios::trunc) << "{\"truncated\":";
    lumen::dsl::DesignProject project;
    project.projectId = "diagnostics.project";
    project.name = "Diagnostics project";
    project.root = ".";
    project.pages = {{homeId, "home.design", "Home"},
                     {settingsId, "settings.design", "Settings"}};
    lumen::dsl::ProjectStore projectStore;
    std::vector<lumen::dsl::DesignError> saveDiagnostics;
    const auto manifest = root / "diagnostics.lumen-project";
    REQUIRE(projectStore.save(manifest.string(), project, saveDiagnostics));

    DesignerApp app;
    app.attach();
    REQUIRE(app.loadProjectFile(manifest.string()));
    CHECK(app.activeProjectDocumentId() == homeId);
    REQUIRE(app.projectDiagnostics().size() == 1);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    const auto diagnostic = findNodeByKey(app.shell().root(), "designer-diagnostic:0");
    REQUIRE(diagnostic != nullptr);
    REQUIRE(app.shell().handlers().contains("designer:diagnostic:0"));
    app.shell().handlers().at("designer:diagnostic:0")();
    CHECK(app.activeProjectDocumentId() == settingsId);
    CHECK(app.workbench().document()->documentId == settingsId);
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

TEST_CASE("designer app creates a project from the current document",
          "[designer][dp9][d3][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-new-project-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};

    DesignerApp app;
    app.attach();
    REQUIRE(app.loadSource("page editor { Text(\"Editor\") }",
                           "editor.lumen"));
    const auto documentId = app.workbench().document()->documentId;
    bool requestedForSave = false;
    std::string requestedDefaultName;
    app.setFileDialogRequester(
        [&](bool forSave, const std::string& defaultName) {
            requestedForSave = forSave;
            requestedDefaultName = defaultName;
            return std::string{};
        });

    app.shell().handlers().at("designer:new-project")();
    CHECK(requestedForSave);
    CHECK(requestedDefaultName == "untitled.lumen-project");
    const auto manifest = root / "editor.lumen-project";
    app.handleFileDialogResult({manifest.string()});

    REQUIRE(app.project().has_value());
    REQUIRE(app.project()->pages.size() == 1);
    CHECK(app.project()->pages.front().documentId == documentId);
    CHECK(app.activeProjectDocumentId() == documentId);
    CHECK(app.workbench().document()->documentId == documentId);
    CHECK(std::filesystem::exists(root / "main.design"));
    lumen::dsl::ProjectStore store;
    const auto loaded = store.load(manifest.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.project.pages.front().documentId == documentId);
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

    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    REQUIRE_FALSE(removeError);
    CHECK(watcher.poll());
    CHECK_FALSE(app.loadFile(path.string()));
    CHECK(app.workbench().frame().generation() == goodGeneration);
    REQUIRE(app.workbench().diagnostics().size() == 1);

    write("page watch { Text(\"Restored\", key: \"title\") }");
    CHECK(watcher.poll());
    REQUIRE(app.loadFile(path.string()));
    (void)app.shell().renderFrame();
    CHECK(app.workbench().frame().generation() > goodGeneration);
}
