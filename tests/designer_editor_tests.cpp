#include <catch2/catch_test_macros.hpp>

#include <set>

#include "lumen/dsl/design_editor.h"

using lumen::dsl::DesignDiagnostic;
using lumen::dsl::DesignDiagnosticRecoverability;
using lumen::dsl::DesignDiagnosticStage;
using lumen::dsl::DesignDocument;
using lumen::dsl::DesignDocumentCommand;
using lumen::dsl::DesignDocumentHistory;
using lumen::dsl::DesignError;
using lumen::dsl::DesignSelection;
using lumen::dsl::DesignSelectionMode;
using lumen::dsl::DesignSelectionModel;
using lumen::dsl::DslError;
using lumen::dsl::SourcePos;
using lumen::dsl::appendDesignDiagnostic;

namespace {

DesignDocument sampleDocument() {
    DesignDocument document;
    document.documentId = "designer-editor";
    document.root = lumen::dsl::DesignNode{1, "Row"};
    document.root.children = {
        lumen::dsl::DesignNode{2, "Text"},
        lumen::dsl::DesignNode{3, "Button"},
    };
    return document;
}

DesignDocumentCommand renameCommand(std::string from, std::string to,
                                    std::string mergeKey = {}) {
    DesignDocumentCommand command;
    command.label = "Rename page";
    command.affectedIds = {1};
    command.mergeKey = std::move(mergeKey);
    command.precondition = [from](const DesignDocument& document) {
        return document.pageName == from;
    };
    command.apply = [to](DesignDocument& document) {
        document.pageName = to;
        return true;
    };
    command.revert = [from](DesignDocument& document) {
        document.pageName = from;
        return true;
    };
    return command;
}

}  // namespace

TEST_CASE("designer selection stays on document ids and supports modes",
          "[designer][f6][selection]") {
    const auto document = sampleDocument();
    DesignSelectionModel model;
    REQUIRE(model.select(2, DesignSelectionMode::Replace, document));
    CHECK(model.state().ids == std::set<lumen::dsl::DesignNodeId>{2});
    CHECK(model.state().primary == 2);
    CHECK(model.state().anchor == 2);

    REQUIRE(model.select(3, DesignSelectionMode::Add, document));
    CHECK(model.state().ids ==
          std::set<lumen::dsl::DesignNodeId>{2, 3});
    REQUIRE(model.capture(3));
    CHECK(model.state().captured == 3);

    REQUIRE(model.select(2, DesignSelectionMode::Toggle, document));
    CHECK(model.state().ids == std::set<lumen::dsl::DesignNodeId>{3});
    CHECK(model.state().primary == 3);
    CHECK(model.state().anchor == 3);
    CHECK_FALSE(model.select(99, DesignSelectionMode::Replace, document));
    CHECK(model.state().captured == 3);
    REQUIRE(model.select(3, DesignSelectionMode::Toggle, document));
    CHECK(model.state().ids.empty());
    CHECK_FALSE(model.state().captured.has_value());

    CHECK_FALSE(model.setSelection({2, 99}, 2, 2, document));
    REQUIRE(model.setSelection({2, 3}, 3, 2, document));
    CHECK(model.state().primary == 3);
    CHECK(model.state().anchor == 2);

    REQUIRE(model.select(2, DesignSelectionMode::Replace, document));
    REQUIRE(model.selectRange(3, document));
    CHECK(model.state().ids ==
          std::set<lumen::dsl::DesignNodeId>{2, 3});
    CHECK(model.state().primary == 3);
    CHECK(model.state().anchor == 2);
}

TEST_CASE("designer selection clears ids when document identity changes",
          "[designer][f6][selection]") {
    auto first = sampleDocument();
    auto second = first;
    second.documentId = "other-editor-document";

    DesignSelectionModel model;
    REQUIRE(model.select(2, DesignSelectionMode::Replace, first));
    REQUIRE(model.capture(2));
    model.setDocument(second);
    CHECK(model.state().ids.empty());
    CHECK_FALSE(model.state().primary.has_value());
    CHECK_FALSE(model.state().anchor.has_value());
    CHECK_FALSE(model.state().captured.has_value());

    REQUIRE(model.select(3, DesignSelectionMode::Add, second));
    CHECK(model.state().ids == std::set<lumen::dsl::DesignNodeId>{3});
    CHECK(model.state().primary == 3);
    CHECK(model.state().anchor == 3);
}

TEST_CASE("designer document transactions are atomic and restore selection",
          "[designer][f6][transaction]") {
    auto document = sampleDocument();
    DesignSelection selection;
    selection.ids = {2};
    selection.primary = 2;
    selection.anchor = 2;
    const auto original = document;
    const auto originalSelection = selection;
    DesignDocumentHistory history;
    history.markSaved();

    auto transaction = history.begin(document, selection);
    REQUIRE(transaction.apply(renameCommand("", "edited")));
    transaction.setSelectionAfter(DesignSelection{{3}, 3, 3, std::nullopt,
                                                   "properties"});
    REQUIRE(history.commit(document, selection, std::move(transaction)));
    CHECK(document.pageName == "edited");
    CHECK(selection.primary == 3);
    CHECK(history.dirty());
    CHECK(history.documentRevision() != history.savedRevision());

    REQUIRE(history.undo(document, selection));
    CHECK(document == original);
    CHECK(selection == originalSelection);
    CHECK_FALSE(history.dirty());
    REQUIRE(history.redo(document, selection));
    CHECK(document.pageName == "edited");
    CHECK(selection.primary == 3);

    auto staleSelection = history.begin(document, selection);
    REQUIRE(staleSelection.apply(renameCommand("edited", "stale")));
    const auto transactionSelection = selection;
    selection = DesignSelection{{2}, 2, 2, std::nullopt, {}};
    CHECK_FALSE(history.commit(document, selection, std::move(staleSelection)));
    CHECK(document.pageName == "edited");
    CHECK(selection.primary == 2);
    selection = transactionSelection;

    auto failed = history.begin(document, selection);
    REQUIRE(failed.apply(renameCommand("edited", "next")));
    CHECK_FALSE(failed.apply(renameCommand("wrong", "never")));
    CHECK(failed.failed());
    CHECK_FALSE(history.commit(document, selection, std::move(failed)));
    CHECK(document.pageName == "edited");
    CHECK(selection.primary == 3);
    CHECK(history.undoSize() == 1);

    auto invalidSelection = history.begin(document, selection);
    DesignDocumentCommand removeSelected;
    removeSelected.label = "Remove selected";
    removeSelected.affectedIds = {3};
    removeSelected.apply = [](DesignDocument& candidate) {
        candidate.root.children.pop_back();
        return true;
    };
    removeSelected.revert = [](DesignDocument& candidate) {
        candidate.root.children.push_back(
            lumen::dsl::DesignNode{3, "Button"});
        return true;
    };
    REQUIRE(invalidSelection.apply(std::move(removeSelected)));
    CHECK_FALSE(history.commit(document, selection,
                               std::move(invalidSelection)));
    CHECK(document.root.children.size() == 2);
    CHECK(selection.primary == 3);
    CHECK(history.undoSize() == 1);
}

TEST_CASE("designer history merges commands and clears redo branches",
          "[designer][f6][transaction]") {
    auto document = sampleDocument();
    DesignSelection selection;
    DesignDocumentHistory history;

    auto first = history.begin(document, selection);
    REQUIRE(first.apply(renameCommand("", "a", "typing")));
    REQUIRE(history.commit(document, selection, std::move(first)));
    auto second = history.begin(document, selection);
    REQUIRE(second.apply(renameCommand("a", "ab", "typing")));
    REQUIRE(history.commit(document, selection, std::move(second)));
    CHECK(history.undoSize() == 1);
    CHECK(history.redoSize() == 0);
    REQUIRE(history.undo(document, selection));
    CHECK(document.pageName.empty());
    REQUIRE(history.redo(document, selection));
    CHECK(document.pageName == "ab");

    REQUIRE(history.undo(document, selection));
    auto branch = history.begin(document, selection);
    REQUIRE(branch.apply(renameCommand("", "branch")));
    REQUIRE(history.commit(document, selection, std::move(branch)));
    CHECK_FALSE(history.canRedo());
    CHECK(document.pageName == "branch");
}

TEST_CASE("designer history does not merge across a saved revision",
          "[designer][f6][transaction]") {
    auto document = sampleDocument();
    DesignSelection selection;
    DesignDocumentHistory history;

    auto first = history.begin(document, selection);
    REQUIRE(first.apply(renameCommand("", "a", "typing")));
    REQUIRE(history.commit(document, selection, std::move(first)));
    history.markSaved();

    auto second = history.begin(document, selection);
    REQUIRE(second.apply(renameCommand("a", "ab", "typing")));
    REQUIRE(history.commit(document, selection, std::move(second)));
    CHECK(history.undoSize() == 2);

    REQUIRE(history.undo(document, selection));
    CHECK(document.pageName == "a");
    CHECK_FALSE(history.dirty());
    REQUIRE(history.undo(document, selection));
    CHECK(document.pageName.empty());
    CHECK(history.dirty());
}

TEST_CASE("designer clearing history invalidates pending transactions",
          "[designer][f6][transaction]") {
    auto document = sampleDocument();
    DesignSelection selection;
    DesignDocumentHistory history;

    auto pending = history.begin(document, selection);
    REQUIRE(pending.apply(renameCommand("", "pending")));
    history.clear();

    CHECK_FALSE(history.commit(document, selection, std::move(pending)));
    CHECK(document.pageName.empty());
    CHECK_FALSE(history.dirty());
    CHECK(history.undoSize() == 0);
}

TEST_CASE("designer diagnostics have stable stages and deduplicate by location",
          "[designer][f6][diagnostic]") {
    DesignError error{"ref.missing", "page.design", SourcePos{4, 7},
                      "missing reference", {}, {}, 2, ".root.children[0]",
                      "onClick"};
    auto diagnostic = DesignDiagnostic::fromError(error);
    CHECK(diagnostic.stage == DesignDiagnosticStage::Reference);
    CHECK(diagnostic.recoverability ==
          DesignDiagnosticRecoverability::Placeholder);
    REQUIRE(diagnostic.sourceSpan.has_value());
    CHECK(diagnostic.sourceSpan->begin == SourcePos{4, 7});
    CHECK(diagnostic.sourceSpan->end == SourcePos{4, 7});

    auto duplicate = diagnostic;
    duplicate.related.push_back(
        lumen::dsl::DesignDiagnosticRelated{"defs.design", std::nullopt, 8,
                                            "definition"});
    std::vector<DesignDiagnostic> diagnostics;
    appendDesignDiagnostic(diagnostics, diagnostic);
    appendDesignDiagnostic(diagnostics, std::move(duplicate));
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().occurrences == 2);
    CHECK(diagnostics.front().related.size() == 1);

    const auto schema = DesignDiagnostic::fromError(
        DesignError{"schema.unknown_node", "page.design", {}, "bad node"});
    CHECK(schema.stage == DesignDiagnosticStage::Schema);
    CHECK(schema.recoverability ==
          DesignDiagnosticRecoverability::KeepLastFrame);
    const auto save = DesignDiagnostic::fromError(
        DesignError{"store.write", "page.design", {}, "write failed"});
    CHECK(save.stage == DesignDiagnosticStage::Save);
    CHECK(save.recoverability == DesignDiagnosticRecoverability::BlockSave);

    const auto dsl = DesignDiagnostic::fromDslError(
        DslError{"page.lumen", SourcePos{2, 3}, "unexpected token", "}",
                 "Button"});
    CHECK(dsl.stage == DesignDiagnosticStage::Parse);
    CHECK(dsl.code == "parse.error");
}
