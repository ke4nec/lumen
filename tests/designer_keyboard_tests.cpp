#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <set>
#include <string>

#include "designer_app.h"
#include "lumen/accessibility/bridge.h"
#include "lumen/core/render_node.h"

using lumen::core::Key;
using lumen::core::Size;
using lumen::core::findNodeByKey;
using lumen::designer_app::DesignerApp;

// Prerequisites §4.18: a reader's activation must survive the UI rebuild.
TEST_CASE("designer accessible outline activation selects and locates its declaration",
          "[designer][d3][app][outline-accessibility]") {
    const auto targetIndex = GENERATE(0U, 1U);
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    DesignerApp app;
    app.attach();
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page accessible { Column(key: \"root\") {"
        " Text(\"First\", key: \"first\")"
        " Button(\"Second\", key: \"second\") } }", "accessible-outline.lumen"));
    (void)app.shell().renderFrame();
    const auto before = *app.workbench().document();
    const auto revision = app.workbench().documentRevision();
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto target = outline->children.at(targetIndex);
    const std::string rowKey = "designer-outline:item:" + target.path;
    const auto* row = findNodeByKey(app.shell().root(), rowKey);
    REQUIRE(row != nullptr);
    REQUIRE(app.shell().performAccessibilityAction(
                row->identity, lumen::accessibility::kActionActivate) ==
            lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == target.id);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{target.id});
    row = findNodeByKey(app.shell().root(), rowKey);
    REQUIRE(row != nullptr);
    CHECK(row->selected);
    CHECK(app.workbench().document() == before);
    CHECK(app.workbench().documentRevision() == revision);
    CHECK_FALSE(app.workbench().dirty());
    CHECK_FALSE(app.workbench().canUndo());
    const std::string fieldKey = "designer-property-field:" +
                                 std::to_string(target.id) + ":text";
    const auto* field = findNodeByKey(app.shell().root(), fieldKey);
    REQUIRE(field != nullptr);
    REQUIRE(app.shell().performAccessibilityAction(
                field->identity, lumen::accessibility::kActionFocus) ==
            lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();
    CHECK(app.shell().focus().focusedKey() == fieldKey);
    field = findNodeByKey(app.shell().root(), fieldKey);
    REQUIRE(field != nullptr);
    REQUIRE(app.shell().performAccessibilityAction(
                field->identity, lumen::accessibility::kActionSetValue,
                "Accessible edit") ==
            lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();
    const auto edited = *app.workbench().document();
    CHECK(std::get<std::string>(edited.root.children.at(targetIndex)
                                   .properties.at("text").value) == "Accessible edit");
    CHECK(app.workbench().dirty());
    CHECK(app.workbench().selection().primary == target.id);
    REQUIRE(app.undo());
    CHECK(app.workbench().document() == before);
    CHECK_FALSE(app.workbench().dirty());
    REQUIRE(app.redo());
    CHECK(app.workbench().document() == edited);
    CHECK(app.workbench().selection().primary == target.id);
}

// Prerequisites §4.7 / §4.18: preview text edits own their keyboard history.
TEST_CASE("designer preview text keys do not alter document structure or history",
          "[designer][d3][app][keyboard-isolation]") {
    const auto modifiers = GENERATE(lumen::core::kModifierCtrl,
                                    lumen::core::kModifierGui);
    const bool hasDocumentRedo = GENERATE(false, true);
    INFO("modifiers " << modifiers << " document redo " << hasDocumentRedo);
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") {"
        " Text(\"First\", key: \"first\")"
        " TextField(bind: draft, key: \"draft-field\")"
        " Text(\"Last\", key: \"last\") } }", "keyboard-preview.lumen"));
    (void)app.shell().renderFrame();
    app.shell().handlers().at("designer:add-text")();
    if (hasDocumentRedo) {
        app.shell().handlers().at("designer:duplicate")();
        REQUIRE(app.undo());
    }
    (void)app.shell().renderFrame();
    const auto fieldId = app.workbench().document()->root.children[1].id;
    app.shell().handlers().at("designer:select:" + std::to_string(fieldId))();
    (void)app.shell().renderFrame();
    const auto* field = findNodeByKey(app.shell().root(), "draft-field");
    REQUIRE(field != nullptr);
    app.shell().controller().focusNode(*field);
    REQUIRE(app.shell().controller().wantsTextInput());
    app.shell().textInput("abcd");
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().state().get("draft") == "abcd");
    const auto before = *app.workbench().document();
    const auto revision = app.workbench().documentRevision();
    const auto selection = app.workbench().selection();
    REQUIRE(app.workbench().dirty());
    REQUIRE(app.workbench().canUndo());
    REQUIRE(app.workbench().canRedo() == hasDocumentRedo);
    SECTION("Delete edits preview text") {
        app.shell().keyDown(Key::None, modifiers, 'a');
        app.shell().keyDown(Key::Delete);
        CHECK(app.shell().state().get("draft").empty());
    }
    SECTION("undo edits preview history") {
        app.shell().keyDown(Key::None, modifiers, 'z');
        CHECK(app.shell().state().get("draft").empty());
    }
    SECTION("redo edits preview history") {
        app.shell().controller().undo();
        REQUIRE(app.shell().state().get("draft").empty());
        app.shell().keyDown(Key::None, modifiers, 'y');
        CHECK(app.shell().state().get("draft") == "abcd");
    }
    SECTION("Shift Z redoes preview history") {
        app.shell().controller().undo();
        REQUIRE(app.shell().state().get("draft").empty());
        app.shell().keyDown(Key::None, modifiers | lumen::core::kModifierShift, 'z');
        CHECK(app.shell().state().get("draft") == "abcd");
    }
    SECTION("Up does not reorder the selected document node") {
        app.shell().keyDown(Key::Up, modifiers);
    }
    SECTION("Down does not reorder the selected document node") {
        app.shell().keyDown(Key::Down, modifiers);
    }
    (void)app.shell().renderFrame();
    CHECK(app.workbench().document() == before);
    CHECK(app.workbench().documentRevision() == revision);
    CHECK(app.workbench().selection() == selection);
    CHECK(app.workbench().dirty());
    CHECK(app.workbench().canUndo());
    CHECK(app.workbench().canRedo() == hasDocumentRedo);
    CHECK(app.shell().focus().focusedKey() == "draft-field");
}

TEST_CASE("designer declaration fields retain document undo while blocking structure keys",
          "[designer][d3][app][keyboard-isolation]") {
    const auto modifiers = GENERATE(lumen::core::kModifierCtrl,
                                    lumen::core::kModifierGui);
    const bool referenceField = GENERATE(false, true);
    INFO("modifiers " << modifiers << " reference " << referenceField);
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page declarations { Column(key: \"root\") {"
        " Text(\"First\") Button(\"Save\", onClick: save, key: \"save\")"
        " Text(\"Last\") } }", "keyboard-declarations.lumen"));
    (void)app.shell().renderFrame();
    const auto id = app.workbench().document()->root.children[1].id;
    app.shell().handlers().at("designer:select:" + std::to_string(id))();
    (void)app.shell().renderFrame();
    const auto before = *app.workbench().document();
    const std::string fieldKey =
        (referenceField ? "designer-reference-field:" : "designer-property-field:") +
        std::to_string(id) + (referenceField ? ":onClick" : ":text");
    const auto* field = findNodeByKey(app.shell().root(), fieldKey);
    REQUIRE(field != nullptr);
    app.shell().controller().focusNode(*field);
    REQUIRE(app.shell().focus().focusedKey() == fieldKey);
    app.shell().keyDown(Key::None, modifiers, 'a');
    app.shell().textInput(referenceField ? "submit" : "Changed");
    (void)app.shell().renderFrame();
    const auto edited = *app.workbench().document();
    REQUIRE(edited != before);
    REQUIRE(app.workbench().dirty());
    const auto revision = app.workbench().documentRevision();
    const auto selection = app.workbench().selection();
    for (const auto key : {Key::Up, Key::Down}) {
        app.shell().keyDown(key, modifiers);
        (void)app.shell().renderFrame();
        CHECK(app.workbench().document() == edited);
        CHECK(app.workbench().documentRevision() == revision);
        CHECK(app.workbench().selection() == selection);
        CHECK(app.shell().focus().focusedKey() == fieldKey);
    }
    app.shell().keyDown(Key::None, modifiers, 'z');
    (void)app.shell().renderFrame();
    CHECK(app.workbench().document() == before);
    CHECK_FALSE(app.workbench().dirty());
    app.shell().keyDown(Key::None, modifiers | lumen::core::kModifierShift, 'z');
    (void)app.shell().renderFrame();
    CHECK(app.workbench().document() == edited);
    CHECK(app.workbench().dirty());
    app.shell().keyDown(Key::None, modifiers, 'z');
    (void)app.shell().renderFrame();
    app.shell().keyDown(Key::None, modifiers, 'y');
    (void)app.shell().renderFrame();
    CHECK(app.workbench().document() == edited);
    CHECK(app.workbench().dirty());
}
