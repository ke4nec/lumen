#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <variant>

#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_workbench.h"

using lumen::dsl::DesignPreviewWorkbench;
using lumen::dsl::DesignSelectionMode;

TEST_CASE("designer D2 workbench exposes outline properties and trace selection",
          "[designer][d2]") {
    DesignPreviewWorkbench workbench;
    REQUIRE(workbench.openLumenSource(
        "page preview { Column(key: \"root\") {"
        " Text(\"Title\", key: \"title\")"
        " Button(\"Save\", onClick: save, key: \"save\")"
        " } }"));
    REQUIRE(workbench.document().has_value());
    REQUIRE(workbench.frame().hasFrame());

    const auto outline = workbench.outline();
    REQUIRE(outline.has_value());
    CHECK(outline->id == workbench.document()->root.id);
    CHECK(outline->key == "root");
    REQUIRE(outline->children.size() == 2);
    CHECK(outline->children[0].path == "root.children[0]");
    CHECK(outline->children[0].key == "title");

    const auto saveId = outline->children[1].id;
    const auto properties = workbench.properties(saveId);
    REQUIRE(properties.size() == 3);
    CHECK(properties[0].name == "key");
    REQUIRE(properties[0].value.has_value());
    CHECK(std::get<std::string>(properties[0].value->value) == "save");
    CHECK(properties[1].name == "onClick");
    REQUIRE(properties[1].reference.has_value());
    CHECK(*properties[1].reference == "save");
    CHECK(properties[2].name == "text");
    REQUIRE(properties[2].value.has_value());
    CHECK(std::get<std::string>(properties[2].value->value) == "Save");

    REQUIRE(workbench.selectRuntimeIdentity("/k:root/k:save"));
    CHECK(workbench.selection().primary == saveId);
    CHECK(workbench.runtimeIdentity(saveId) == "/k:root/k:save");
    CHECK(workbench.selectNode(outline->children[0].id,
                              DesignSelectionMode::Add));
    CHECK(workbench.selection().ids.size() == 2);

    const auto documentId = workbench.document()->documentId;
    const auto encoded = serializeDesignDocument(*workbench.document());
    REQUIRE(workbench.openDesignSource(encoded, "preview.design"));
    CHECK(workbench.document()->documentId == documentId);
    CHECK(workbench.frame().hasFrame());
}

TEST_CASE("designer D2 workbench preserves the last frame on document errors",
          "[designer][d2]") {
    DesignPreviewWorkbench workbench;
    REQUIRE(workbench.openLumenSource(
        "page preview { Text(\"stable\", key: \"text\") }",
        "preview.lumen"));
    const auto originalDocument = workbench.document();
    const auto originalGeneration = workbench.frame().generation();

    CHECK_FALSE(workbench.openLumenSource("page preview {", "broken.lumen"));
    REQUIRE(workbench.document() == originalDocument);
    CHECK(workbench.frame().hasFrame());
    CHECK(workbench.frame().generation() == originalGeneration);
    REQUIRE(workbench.diagnostics().size() == 1);
    CHECK(workbench.diagnostics().front().code.rfind("parse.", 0) == 0);
    CHECK(workbench.diagnostics().front().file == "broken.lumen");

    auto invalid = *originalDocument;
    invalid.root.type = "Unknown";
    CHECK_FALSE(workbench.openDesignSource(
        serializeDesignDocument(invalid), "broken.design"));
    CHECK(workbench.frame().hasFrame());
    CHECK(workbench.frame().generation() == originalGeneration);
    REQUIRE(workbench.diagnostics().size() == 1);
    CHECK(workbench.diagnostics().front().code == "compile.unknown_node");
    CHECK(workbench.diagnostics().front().file == "broken.design");
}

TEST_CASE("designer D2 workbench clears all session data explicitly",
          "[designer][d2]") {
    DesignPreviewWorkbench workbench;
    REQUIRE(workbench.openLumenSource("page preview { Text(\"x\") }"));
    REQUIRE(workbench.selectNode(workbench.document()->root.id));
    workbench.clear();
    CHECK_FALSE(workbench.document().has_value());
    CHECK_FALSE(workbench.frame().hasFrame());
    CHECK(workbench.selection().ids.empty());
    CHECK(workbench.diagnostics().empty());
    CHECK_FALSE(workbench.outline().has_value());
}

TEST_CASE("designer D2 workbench reports file read failures",
          "[designer][d2]") {
    DesignPreviewWorkbench workbench;
    CHECK_FALSE(workbench.openLumenFile(
        "/lumen/this-file-does-not-exist/design.lumen"));
    REQUIRE(workbench.diagnostics().size() == 1);
    CHECK(workbench.diagnostics().front().code == "read.io");
    CHECK(workbench.diagnostics().front().file ==
          "/lumen/this-file-does-not-exist/design.lumen");
    CHECK_FALSE(workbench.document().has_value());
    CHECK_FALSE(workbench.frame().hasFrame());
}

TEST_CASE("designer D3 workbench edits L0 declarations with history and saves",
          "[designer][d3]") {
    DesignPreviewWorkbench workbench;
    REQUIRE(workbench.openLumenSource(
        "page preview { Column(key: \"root\") {"
        " Text(\"Title\", key: \"title\")"
        " Button(\"Save\", key: \"save\")"
        " } }",
        "preview.lumen"));
    const auto outline = workbench.outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() == 2);
    const auto titleId = outline->children.front().id;
    REQUIRE(workbench.selectNode(titleId));
    const auto initialGeneration = workbench.frame().generation();

    REQUIRE(workbench.setProperty(
        titleId, "text",
        lumen::dsl::DesignValue{
            lumen::dsl::DesignValue::Variant{std::string{"Edited"}}}));
    CHECK(workbench.dirty());
    CHECK(workbench.canUndo());
    CHECK(workbench.frame().generation() > initialGeneration);
    auto properties = workbench.properties(titleId);
    REQUIRE(properties.size() == 2);
    CHECK(std::get<std::string>(properties.back().value->value) == "Edited");

    REQUIRE(workbench.undo());
    CHECK_FALSE(workbench.dirty());
    CHECK(workbench.canRedo());
    properties = workbench.properties(titleId);
    REQUIRE(properties.size() == 2);
    CHECK(std::get<std::string>(properties.back().value->value) == "Title");

    REQUIRE(workbench.redo());
    CHECK(workbench.dirty());
    properties = workbench.properties(titleId);
    REQUIRE(properties.size() == 2);
    CHECK(std::get<std::string>(properties.back().value->value) == "Edited");

    const auto path = std::filesystem::temp_directory_path() /
                      ("lumen-designer-d3-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       ".design");
    REQUIRE(workbench.saveDesignFile(path.string()));
    CHECK_FALSE(workbench.dirty());
    CHECK(workbench.canUndo());

    DesignPreviewWorkbench reopened;
    REQUIRE(reopened.openDesignFile(path.string()));
    REQUIRE(reopened.document().has_value());
    CHECK(reopened.document()->documentId ==
          workbench.document()->documentId);
    const auto reopenedOutline = reopened.outline();
    REQUIRE(reopenedOutline.has_value());
    CHECK(reopened.properties(reopenedOutline->children.front().id).back()
              .value == properties.back().value);
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + ".bak", error);
}

TEST_CASE("designer D3 workbench rejects runtime preview properties",
          "[designer][d3]") {
    DesignPreviewWorkbench workbench;
    REQUIRE(workbench.openLumenSource(
        "page preview { Button(\"Save\", showFocusRing: true) }"));
    const auto outline = workbench.outline();
    REQUIRE(outline.has_value());
    const auto buttonId = outline->id;
    CHECK_FALSE(workbench.setProperty(
        buttonId, "focusWidth",
        lumen::dsl::DesignValue{
            lumen::dsl::DesignValue::Variant{2.0}}));
    CHECK_FALSE(workbench.dirty());
    REQUIRE(workbench.diagnostics().size() == 1);
    CHECK(workbench.diagnostics().front().code == "editor.rejected");
}
