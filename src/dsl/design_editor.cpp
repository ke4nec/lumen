#include "lumen/dsl/design_editor.h"

#include <sstream>
#include <utility>

namespace lumen::dsl {
namespace {

[[nodiscard]] DesignDiagnosticStage stageForCode(std::string_view code) {
    if (code.rfind("store.read", 0) == 0 || code.rfind("codec.", 0) == 0) {
        return DesignDiagnosticStage::Read;
    }
    if (code.rfind("parse.", 0) == 0 || code == "compile.dsl") {
        return DesignDiagnosticStage::Parse;
    }
    if (code.rfind("store.migration", 0) == 0) {
        return DesignDiagnosticStage::Migrate;
    }
    if (code.rfind("schema.", 0) == 0 || code == "store.schema_version") {
        return DesignDiagnosticStage::Schema;
    }
    if (code.rfind("reference.", 0) == 0 || code.rfind("ref.", 0) == 0) {
        return DesignDiagnosticStage::Reference;
    }
    if (code.rfind("store.", 0) == 0) {
        return DesignDiagnosticStage::Save;
    }
    return DesignDiagnosticStage::Compile;
}

[[nodiscard]] DesignDiagnosticRecoverability recoverabilityForStage(
    DesignDiagnosticStage stage) {
    switch (stage) {
        case DesignDiagnosticStage::Reference:
            return DesignDiagnosticRecoverability::Placeholder;
        case DesignDiagnosticStage::Save:
            return DesignDiagnosticRecoverability::BlockSave;
        case DesignDiagnosticStage::Read:
        case DesignDiagnosticStage::Parse:
        case DesignDiagnosticStage::Migrate:
        case DesignDiagnosticStage::Schema:
        case DesignDiagnosticStage::Compile:
            return DesignDiagnosticRecoverability::KeepLastFrame;
    }
    return DesignDiagnosticRecoverability::KeepLastFrame;
}

[[nodiscard]] std::optional<DesignNodeId> firstId(
    const std::set<DesignNodeId>& ids) {
    if (ids.empty()) return std::nullopt;
    return *ids.begin();
}

}  // namespace

bool DesignSelectionModel::contains(const DesignNode& node, DesignNodeId id) {
    if (node.id == id) return true;
    for (const auto& child : node.children) {
        if (contains(child, id)) return true;
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (contains(child, id)) return true;
        }
    }
    return false;
}

bool DesignSelectionModel::validId(DesignNodeId id,
                                   const DesignDocument& document) {
    return id != 0 && contains(document.root, id);
}

bool DesignSelectionModel::select(DesignNodeId id, DesignSelectionMode mode,
                                  const DesignDocument& document) {
    if (!validId(id, document)) return false;

    switch (mode) {
        case DesignSelectionMode::Replace:
            state_.ids = {id};
            state_.primary = id;
            state_.anchor = id;
            break;
        case DesignSelectionMode::Add:
            state_.ids.insert(id);
            state_.primary = id;
            state_.anchor = id;
            break;
        case DesignSelectionMode::Toggle:
            if (state_.ids.erase(id) == 0) {
                state_.ids.insert(id);
                state_.primary = id;
            } else if (state_.primary == id) {
                state_.primary = firstId(state_.ids);
            }
            if (state_.captured == id) state_.captured.reset();
            state_.anchor = id;
            if (state_.ids.empty()) {
                state_.primary.reset();
                state_.anchor.reset();
            }
            break;
    }
    return true;
}

bool DesignSelectionModel::setSelection(
    std::set<DesignNodeId> ids, std::optional<DesignNodeId> primary,
    std::optional<DesignNodeId> anchor, const DesignDocument& document) {
    for (const auto id : ids) {
        if (!validId(id, document)) return false;
    }
    if (primary.has_value() && !ids.contains(*primary)) return false;
    if (anchor.has_value() && !ids.contains(*anchor)) return false;
    state_.ids = std::move(ids);
    state_.primary = primary;
    state_.anchor = anchor;
    if (state_.ids.empty()) {
        state_.primary.reset();
        state_.anchor.reset();
    }
    if (state_.captured.has_value() &&
        !state_.ids.contains(*state_.captured)) {
        state_.captured.reset();
    }
    return true;
}

bool DesignSelectionModel::capture(DesignNodeId id) {
    if (!state_.ids.contains(id)) return false;
    state_.captured = id;
    return true;
}

void DesignSelectionModel::clear() {
    state_.ids.clear();
    state_.primary.reset();
    state_.anchor.reset();
    state_.captured.reset();
}

DesignDocumentTransaction::DesignDocumentTransaction(
    const DesignDocument& document, const DesignSelection& selection,
    std::uint64_t baseRevision)
    : before_(document),
      working_(document),
      selectionBefore_(selection),
      selectionAfter_(selection),
      baseRevision_(baseRevision) {}

bool DesignDocumentTransaction::apply(DesignDocumentCommand command) {
    if (failed_ || !command.apply || !command.revert) {
        failed_ = true;
        return false;
    }
    if (command.precondition && !command.precondition(working_)) {
        failed_ = true;
        return false;
    }
    DesignDocument candidate = working_;
    if (!command.apply(candidate)) {
        failed_ = true;
        return false;
    }
    working_ = std::move(candidate);
    commands_.push_back(std::move(command));
    return true;
}

DesignDocumentTransaction DesignDocumentHistory::begin(
    const DesignDocument& document, const DesignSelection& selection) const {
    return DesignDocumentTransaction(document, selection, documentRevision_);
}

bool DesignDocumentHistory::commit(
    DesignDocument& document, DesignSelection& selection,
    DesignDocumentTransaction transaction) {
    if (transaction.failed() || transaction.empty() ||
        transaction.baseRevision_ != documentRevision_ ||
        transaction.before_ != document) {
        return false;
    }

    if (cursor_ < entries_.size()) entries_.erase(entries_.begin() + cursor_,
                                                    entries_.end());

    Entry entry;
    entry.before = transaction.before_;
    entry.after = transaction.working_;
    entry.selectionBefore = transaction.selectionBefore_;
    entry.selectionAfter = transaction.selectionAfter_;
    entry.beforeRevision = documentRevision_;
    entry.afterRevision = nextRevision_++;
    entry.label = transaction.label_;
    if (entry.label.empty() && !transaction.commands_.empty()) {
        entry.label = transaction.commands_.front().label;
    }
    entry.commands = transaction.commands_;
    for (const auto& command : entry.commands) {
        entry.affectedIds.insert(command.affectedIds.begin(),
                                 command.affectedIds.end());
        if (entry.mergeKey.empty()) entry.mergeKey = command.mergeKey;
    }

    if (cursor_ != 0 && !entry.mergeKey.empty() &&
        entries_[cursor_ - 1].mergeKey == entry.mergeKey) {
        Entry& previous = entries_[cursor_ - 1];
        previous.after = entry.after;
        previous.selectionAfter = entry.selectionAfter;
        previous.afterRevision = entry.afterRevision;
        previous.label = entry.label.empty() ? previous.label : entry.label;
        previous.affectedIds.insert(entry.affectedIds.begin(),
                                    entry.affectedIds.end());
        previous.commands.insert(previous.commands.end(), entry.commands.begin(),
                                  entry.commands.end());
        document = previous.after;
        selection = previous.selectionAfter;
        documentRevision_ = previous.afterRevision;
        return true;
    }

    entries_.push_back(std::move(entry));
    ++cursor_;
    const Entry& committed = entries_.back();
    document = committed.after;
    selection = committed.selectionAfter;
    documentRevision_ = committed.afterRevision;
    return true;
}

bool DesignDocumentHistory::undo(DesignDocument& document,
                                 DesignSelection& selection) {
    if (!canUndo()) return false;
    const Entry& entry = entries_[cursor_ - 1];
    if (document != entry.after || documentRevision_ != entry.afterRevision) {
        return false;
    }
    DesignDocument candidate = document;
    for (auto it = entry.commands.rbegin(); it != entry.commands.rend(); ++it) {
        if (!it->revert || !it->revert(candidate)) return false;
    }
    if (candidate != entry.before) return false;
    document = std::move(candidate);
    selection = entry.selectionBefore;
    documentRevision_ = entry.beforeRevision;
    --cursor_;
    return true;
}

bool DesignDocumentHistory::redo(DesignDocument& document,
                                 DesignSelection& selection) {
    if (!canRedo()) return false;
    const Entry& entry = entries_[cursor_];
    if (document != entry.before || documentRevision_ != entry.beforeRevision) {
        return false;
    }
    DesignDocument candidate = document;
    for (const auto& command : entry.commands) {
        if (command.precondition && !command.precondition(candidate)) {
            return false;
        }
        if (!command.apply || !command.apply(candidate)) return false;
    }
    if (candidate != entry.after) return false;
    document = std::move(candidate);
    selection = entry.selectionAfter;
    documentRevision_ = entry.afterRevision;
    ++cursor_;
    return true;
}

void DesignDocumentHistory::clear() {
    entries_.clear();
    cursor_ = 0;
    documentRevision_ = 0;
    savedRevision_ = 0;
    nextRevision_ = 1;
}

std::string DesignDiagnostic::key() const {
    std::ostringstream out;
    out << code << '\n' << file << '\n' << documentId << '\n' << property
        << '\n' << nodeId << '\n' << nodePath;
    if (sourceSpan.has_value()) {
        out << '\n' << sourceSpan->begin.line << ':'
            << sourceSpan->begin.column << '-'
            << sourceSpan->end.line << ':' << sourceSpan->end.column;
    }
    return out.str();
}

DesignDiagnostic DesignDiagnostic::fromError(
    const DesignError& error, std::optional<DesignDiagnosticStage> stage) {
    const auto resolvedStage = stage.value_or(stageForCode(error.code));
    DesignDiagnostic diagnostic;
    diagnostic.code = error.code;
    diagnostic.stage = resolvedStage;
    diagnostic.message = error.message;
    diagnostic.expected = error.expected;
    diagnostic.found = error.found;
    diagnostic.file = error.file;
    diagnostic.sourceSpan = DesignSourceSpan{error.pos, error.pos};
    diagnostic.nodeId = error.nodeId;
    diagnostic.nodePath = error.nodePath;
    diagnostic.property = error.property;
    diagnostic.recoverability = recoverabilityForStage(resolvedStage);
    return diagnostic;
}

DesignDiagnostic DesignDiagnostic::fromDslError(const DslError& error,
                                                std::string code) {
    DesignError designError{std::move(code), error.file, error.pos,
                            error.message, error.expected, error.found, 0, {},
                            {}};
    return fromError(designError, DesignDiagnosticStage::Parse);
}

void appendDesignDiagnostic(std::vector<DesignDiagnostic>& diagnostics,
                            DesignDiagnostic diagnostic) {
    const auto key = diagnostic.key();
    for (auto& existing : diagnostics) {
        if (existing.key() == key) {
            existing.occurrences += diagnostic.occurrences;
            existing.related.insert(existing.related.end(), diagnostic.related.begin(),
                                    diagnostic.related.end());
            return;
        }
    }
    diagnostics.push_back(std::move(diagnostic));
}

const char* designDiagnosticStageName(DesignDiagnosticStage stage) {
    switch (stage) {
        case DesignDiagnosticStage::Read: return "read";
        case DesignDiagnosticStage::Parse: return "parse";
        case DesignDiagnosticStage::Migrate: return "migrate";
        case DesignDiagnosticStage::Schema: return "schema";
        case DesignDiagnosticStage::Reference: return "reference";
        case DesignDiagnosticStage::Compile: return "compile";
        case DesignDiagnosticStage::Save: return "save";
    }
    return "compile";
}

const char* designDiagnosticRecoverabilityName(
    DesignDiagnosticRecoverability recoverability) {
    switch (recoverability) {
        case DesignDiagnosticRecoverability::Continue: return "continue";
        case DesignDiagnosticRecoverability::Placeholder: return "placeholder";
        case DesignDiagnosticRecoverability::KeepLastFrame:
            return "keep-last-frame";
        case DesignDiagnosticRecoverability::BlockSave: return "block-save";
    }
    return "keep-last-frame";
}

}  // namespace lumen::dsl
