#include <catch2/catch_test_macros.hpp>

#include <set>

#include "lumen/dsl/design_editor.h"
#include "lumen/dsl/design_codec.h"

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

    auto reloaded = second;
    reloaded.root.children.pop_back();
    model.setDocument(reloaded);
    CHECK(model.state().ids.empty());
    CHECK_FALSE(model.state().primary.has_value());
    CHECK_FALSE(model.state().anchor.has_value());
}

TEST_CASE("designer selection reconciles same-document reloads",
          "[designer][f6][selection]") {
    auto document = sampleDocument();
    DesignSelectionModel model;
    REQUIRE(model.setSelection({2, 3}, 3, 2, document));
    REQUIRE(model.capture(3));

    auto reloaded = document;
    reloaded.root.children.pop_back();
    model.setDocument(reloaded);
    CHECK(model.state().ids == std::set<lumen::dsl::DesignNodeId>{2});
    CHECK(model.state().primary == 2);
    CHECK(model.state().anchor == 2);
    CHECK_FALSE(model.state().captured.has_value());
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

    auto invalidSchema = history.begin(document, selection);
    DesignDocumentCommand unknownNode;
    unknownNode.label = "Create invalid node";
    unknownNode.affectedIds = {1};
    unknownNode.apply = [](DesignDocument& candidate) {
        candidate.root.type = "Unknown";
        return true;
    };
    unknownNode.revert = [](DesignDocument& candidate) {
        candidate.root.type = "Row";
        return true;
    };
    REQUIRE(invalidSchema.apply(std::move(unknownNode)));
    CHECK_FALSE(history.commit(document, selection, std::move(invalidSchema)));
    CHECK(document.root.type == "Row");
    CHECK(history.undoSize() == 1);

    auto identityChange = history.begin(document, selection);
    DesignDocumentCommand changeIdentity;
    changeIdentity.label = "Change document identity";
    changeIdentity.affectedIds = {1};
    changeIdentity.apply = [](DesignDocument& candidate) {
        candidate.documentId = "unexpected-document";
        return true;
    };
    changeIdentity.revert = [](DesignDocument& candidate) {
        candidate.documentId = "designer-editor";
        return true;
    };
    REQUIRE(identityChange.apply(std::move(changeIdentity)));
    CHECK_FALSE(history.commit(document, selection, std::move(identityChange)));
    CHECK(document.documentId == "designer-editor");
    CHECK(history.undoSize() == 1);

    auto rootIdentityChange = history.begin(document, selection);
    DesignDocumentCommand changeRootIdentity;
    changeRootIdentity.label = "Change root node identity";
    changeRootIdentity.affectedIds = {1};
    changeRootIdentity.apply = [](DesignDocument& candidate) {
        candidate.root.id = 99;
        return true;
    };
    changeRootIdentity.revert = [](DesignDocument& candidate) {
        candidate.root.id = 1;
        return true;
    };
    REQUIRE(rootIdentityChange.apply(std::move(changeRootIdentity)));
    CHECK_FALSE(history.commit(document, selection,
                               std::move(rootIdentityChange)));
    CHECK(document.root.id == 1);
    CHECK(history.undoSize() == 1);
}

TEST_CASE("designer history merges commands and clears redo branches",
          "[designer][f6][transaction]") {
    auto document = sampleDocument();
    DesignSelection selection;
    DesignDocumentHistory history{[] {
        return DesignDocumentHistory::Clock::time_point{};
    }};

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

// G-D13 / prerequisites §4.14: merge keys and inactivity windows jointly
// define a continuous editing intent; history navigation ends that intent.
TEST_CASE("designer history merges only within the inactivity window",
          "[designer][f6][transaction][merge]") {
    using namespace std::chrono_literals;
    auto document = sampleDocument();
    DesignSelection selection;
    auto now = DesignDocumentHistory::Clock::time_point{1s};
    DesignDocumentHistory history{[&now] { return now; }};
    auto rename = [&](std::string from, std::string to) {
        auto transaction = history.begin(document, selection);
        REQUIRE(transaction.apply(renameCommand(from, to, "typing")));
        REQUIRE(history.commit(document, selection, std::move(transaction)));
    };

    rename("", "a");
    now += 750ms;
    rename("a", "ab");
    now += 750ms;
    rename("ab", "abc");
    CHECK(history.undoSize() == 1);

    SECTION("a pause starts a separate undo unit") {
        now += 751ms;
    }
    SECTION("a clock regression does not extend the intent") {
        now -= 1ms;
    }
    rename("abc", "abcd");
    CHECK(history.undoSize() == 2);
    REQUIRE(history.undo(document, selection));
    CHECK(document.pageName == "abc");
    REQUIRE(history.undo(document, selection));
    CHECK(document.pageName.empty());
    REQUIRE(history.redo(document, selection));
    CHECK(document.pageName == "abc");
    REQUIRE(history.redo(document, selection));
    CHECK(document.pageName == "abcd");
}

TEST_CASE("designer history navigation breaks command merging",
          "[designer][f6][transaction][merge]") {
    auto document = sampleDocument();
    DesignSelection selection;
    DesignDocumentHistory history{[] {
        return DesignDocumentHistory::Clock::time_point{};
    }};
    auto first = history.begin(document, selection);
    REQUIRE(first.apply(renameCommand("", "a", "typing")));
    REQUIRE(history.commit(document, selection, std::move(first)));

    SECTION("undo followed by a new branch preserves the previous unit") {
        auto separate = history.begin(document, selection);
        REQUIRE(separate.apply(renameCommand("a", "other")));
        REQUIRE(history.commit(document, selection, std::move(separate)));
        REQUIRE(history.undo(document, selection));
    }
    SECTION("redo does not resume the previous editing intent") {
        REQUIRE(history.undo(document, selection));
        REQUIRE(history.redo(document, selection));
    }
    auto next = history.begin(document, selection);
    REQUIRE(next.apply(renameCommand("a", "ab", "typing")));
    REQUIRE(history.commit(document, selection, std::move(next)));
    CHECK(history.undoSize() == 2);
    CHECK_FALSE(history.canRedo());
    REQUIRE(history.undo(document, selection));
    CHECK(document.pageName == "a");
    REQUIRE(history.undo(document, selection));
    CHECK(document.pageName.empty());
}

TEST_CASE("designer merging requires one key and a continuous editing target",
          "[designer][f6][transaction][merge]") {
    auto document = sampleDocument();
    DesignSelection selection;
    DesignDocumentHistory history{[] {
        return DesignDocumentHistory::Clock::time_point{};
    }};
    auto first = history.begin(document, selection);
    REQUIRE(first.apply(renameCommand("", "a", "typing")));
    SECTION("a previous transaction with an unmergeable command stays separate") {
        REQUIRE(first.apply(renameCommand("a", "initial")));
    }
    REQUIRE(history.commit(document, selection, std::move(first)));
    const auto before = document;

    SECTION("a new transaction with an unmergeable command stays separate") {
        auto next = history.begin(document, selection);
        REQUIRE(next.apply(renameCommand(before.pageName, "ab", "typing")));
        REQUIRE(next.apply(renameCommand("ab", "final")));
        REQUIRE(history.commit(document, selection, std::move(next)));
    }
    SECTION("differently keyed commands stay separate") {
        auto next = history.begin(document, selection);
        REQUIRE(next.apply(renameCommand(before.pageName, "ab", "typing")));
        REQUIRE(next.apply(renameCommand("ab", "final", "other")));
        REQUIRE(history.commit(document, selection, std::move(next)));
    }
    SECTION("changing selection ends the previous intent") {
        selection = DesignSelection{{2}, 2, 2, std::nullopt, "properties"};
        auto next = history.begin(document, selection);
        REQUIRE(next.apply(renameCommand(before.pageName, "ab", "typing")));
        REQUIRE(history.commit(document, selection, std::move(next)));
    }
    SECTION("changing affected nodes ends the previous intent") {
        DesignDocumentCommand editText;
        editText.affectedIds = {2};
        editText.mergeKey = "typing";
        editText.apply = [](DesignDocument& candidate) {
            candidate.root.children[0].properties["text"] =
                lumen::dsl::DesignValue{std::string{"changed"}};
            return true;
        };
        editText.revert = [](DesignDocument& candidate) {
            candidate.root.children[0].properties.erase("text");
            return true;
        };
        auto next = history.begin(document, selection);
        REQUIRE(next.apply(std::move(editText)));
        REQUIRE(history.commit(document, selection, std::move(next)));
    }
    if (document == before) {
        // Exercises the first section: a mixed previous transaction.
        auto next = history.begin(document, selection);
        REQUIRE(next.apply(renameCommand(before.pageName, "ab", "typing")));
        REQUIRE(history.commit(document, selection, std::move(next)));
    }
    CHECK(history.undoSize() == 2);
    REQUIRE(history.undo(document, selection));
    CHECK(document == before);
    REQUIRE(history.undo(document, selection));
    CHECK(document.pageName.empty());
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

TEST_CASE("designer diagnostic stages cover document and project storage origins",
          "[designer][f6][diagnostic][diagnostic-stage]") {
    const std::vector<std::pair<std::string, DesignDiagnosticStage>> origins{
        {"read.io", DesignDiagnosticStage::Read},
        {"codec.schema_version", DesignDiagnosticStage::Schema},
        {"store.read", DesignDiagnosticStage::Read},
        {"store.migration_missing", DesignDiagnosticStage::Migrate},
        {"store.schema_version", DesignDiagnosticStage::Schema},
        {"store.document_id", DesignDiagnosticStage::Schema},
        {"store.revision_conflict", DesignDiagnosticStage::Save},
        {"project.read", DesignDiagnosticStage::Read},
        {"project.codec", DesignDiagnosticStage::Read},
        {"project.codec.expected_token", DesignDiagnosticStage::Read},
        {"project.migration_exception", DesignDiagnosticStage::Migrate},
        {"project.schema_version", DesignDiagnosticStage::Schema},
        {"project.document_id", DesignDiagnosticStage::Schema},
        {"project.root", DesignDiagnosticStage::Schema},
        {"project.write", DesignDiagnosticStage::Save},
        {"project.backup", DesignDiagnosticStage::Save},
        {"project.rename", DesignDiagnosticStage::Save},
        {"project.revision_conflict", DesignDiagnosticStage::Save},
    };
    for (const auto& [code, stage] : origins) {
        INFO(code);
        const auto diagnostic = DesignDiagnostic::fromError(
            DesignError{code, "page", {}, "failure"});
        CHECK(diagnostic.code == code);
        CHECK(diagnostic.stage == stage);
        CHECK(diagnostic.recoverability ==
              (stage == DesignDiagnosticStage::Save
                   ? DesignDiagnosticRecoverability::BlockSave
                   : DesignDiagnosticRecoverability::KeepLastFrame));
    }
    const auto savingSchema = DesignDiagnostic::fromError(
        DesignError{"schema.invalid_property", "page", {}, "failure"},
        DesignDiagnosticStage::Save);
    CHECK(savingSchema.stage == DesignDiagnosticStage::Save);
    CHECK(savingSchema.recoverability ==
          DesignDiagnosticRecoverability::BlockSave);
}

TEST_CASE("designer diagnostics preserve distinct tuple locations during deduplication",
          "[designer][f6][diagnostic][diagnostic-stage]") {
    const auto original = DesignDiagnostic::fromError(
        DesignError{"reference.missing", "page.design", SourcePos{4, 7},
                    "missing", {}, {}, 2, "root.children[0]", "bind"});
    std::vector<DesignDiagnostic> diagnostics;
    appendDesignDiagnostic(diagnostics, original);
    for (std::size_t field = 0; field != 7; ++field) {
        auto distinct = original;
        switch (field) {
            case 0: distinct.code = "reference.type"; break;
            case 1: distinct.file = "other.design"; break;
            case 2: distinct.documentId = "other-page"; break;
            case 3: distinct.nodeId = 3; break;
            case 4: distinct.nodePath = "root.children[1]"; break;
            case 5: distinct.property = "onClick"; break;
            case 6: distinct.sourceSpan->end.column = 8; break;
        }
        appendDesignDiagnostic(diagnostics, std::move(distinct));
    }
    CHECK(diagnostics.size() == 8);
    appendDesignDiagnostic(diagnostics, original);
    CHECK(diagnostics.size() == 8);
    CHECK(diagnostics.front().occurrences == 2);

    auto first = original;
    first.file = "page\npart";
    first.documentId = "document";
    auto second = original;
    second.file = "page";
    second.documentId = "part\ndocument";
    CHECK(first.key() != second.key());
    appendDesignDiagnostic(diagnostics, first);
    appendDesignDiagnostic(diagnostics, second);
    CHECK(diagnostics.size() == 10);
}

TEST_CASE("designer diagnostic JSON preserves escaped text and structured locations",
          "[designer][f6][diagnostic][diagnostic-stage]") {
    auto diagnostic = DesignDiagnostic::fromError(
        DesignError{"reference.missing", "page\\\".design", SourcePos{4, 7},
                    "引用\n\t\"missing\"", "expected", "found", 2,
                    "root.children[0]", "bind"});
    diagnostic.documentId = "page\rID";
    diagnostic.severity = lumen::dsl::DesignDiagnosticSeverity::Warning;
    diagnostic.sourceSpan->end = SourcePos{5, 9};
    diagnostic.related.push_back({"definitions.design", std::nullopt, 8,
                                  std::string{"definition\x01"}});
    diagnostic.occurrences = 3;
    const auto json = lumen::dsl::serializeDesignDiagnostics({diagnostic});
    REQUIRE(lumen::dsl::isValidDesignJsonValue(json));
    CHECK(json.find("\"severity\":\"warning\"") != std::string::npos);
    CHECK(json.find("\"stage\":\"reference\"") != std::string::npos);
    CHECK(json.find("\"message\":\"引用\\n\\t\\\"missing\\\"\"") !=
          std::string::npos);
    CHECK(json.find("\"sourceSpan\":{\"begin\":{\"line\":4,\"column\":7},"
                    "\"end\":{\"line\":5,\"column\":9}}") !=
          std::string::npos);
    CHECK(json.find("\"recoverability\":\"placeholder\"") != std::string::npos);
    CHECK(json.find("\"occurrences\":3") != std::string::npos);
    CHECK(json.find("\"label\":\"definition\\u0001\"") != std::string::npos);
    CHECK(lumen::dsl::serializeDesignDiagnostics({}) == "[]");
    CHECK(lumen::dsl::serializeDesignDiagnostics({diagnostic}) == json);
}
