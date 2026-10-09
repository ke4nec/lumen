#include "lumen/dsl/design_workbench.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <utility>

namespace lumen::dsl {

namespace {

struct ParentLocation {
    DesignNodeId parent{0};
    std::size_t index{0};
    std::string slot{};
};

[[nodiscard]] bool sameDocumentPath(const std::string& left,
                                    const std::string& right) {
    if (left == right) return true;
    std::error_code leftError;
    std::error_code rightError;
    const auto leftPath =
        std::filesystem::absolute(std::filesystem::path{left}, leftError)
            .lexically_normal();
    const auto rightPath =
        std::filesystem::absolute(std::filesystem::path{right}, rightError)
            .lexically_normal();
    return !leftError && !rightError && leftPath == rightPath;
}

const DesignNode* findNode(const DesignNode& node, DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findNode(child, id); found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findNode(child, id); found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

std::optional<ParentLocation> locateNode(const DesignNode& parent,
                                         DesignNodeId id) {
    for (std::size_t index = 0; index < parent.children.size(); ++index) {
        if (parent.children[index].id == id) {
            return ParentLocation{parent.id, index, {}};
        }
    }
    for (const auto& [slot, children] : parent.slots) {
        for (std::size_t index = 0; index < children.size(); ++index) {
            if (children[index].id == id) {
                return ParentLocation{parent.id, index, slot};
            }
        }
    }
    for (const auto& child : parent.children) {
        if (const auto found = locateNode(child, id); found.has_value()) {
            return found;
        }
    }
    for (const auto& [slot, children] : parent.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto found = locateNode(child, id); found.has_value()) {
                return found;
            }
        }
    }
    return std::nullopt;
}

bool recoverablePreviewFailure(
    const DesignPreviewFrame& frame,
    const std::vector<DesignDiagnostic>& diagnostics) {
    if (!frame.hasFrame() || diagnostics.empty()) return false;
    return std::all_of(
        diagnostics.begin(), diagnostics.end(), [](const DesignDiagnostic& item) {
            return item.code.rfind("reference.", 0) == 0 ||
                   item.code.rfind("component.", 0) == 0;
        });
}

}  // namespace

bool DesignPreviewWorkbench::openLumenSource(
    const std::string& source, std::string filename,
    DesignRuntimeContext* context) {
    const auto sourceFile = filename;
    const auto parsed = parseLumenSource(source, std::move(filename));
    if (!parsed.ok()) {
        setError(*parsed.error);
        return false;
    }
    const bool opened =
        openDocumentInternal(std::move(parsed.document), context, sourceFile);
    if (opened) resetHistory(true);
    return opened;
}

bool DesignPreviewWorkbench::openLumenFile(
    const std::string& filename, DesignRuntimeContext* context) {
    std::ifstream input(filename, std::ios::binary);
    if (!input) {
        DesignError error;
        error.code = "read.io";
        error.file = filename;
        error.message = "cannot read .lumen source file";
        setError(error);
        return false;
    }
    return openLumenSource(
        std::string{std::istreambuf_iterator<char>{input},
                    std::istreambuf_iterator<char>{}},
        filename, context);
}

bool DesignPreviewWorkbench::openDesignSource(
    const std::string& source, std::string filename,
    DesignRuntimeContext* context) {
    const auto sourceFile = filename;
    const auto read = readDesignDocument(source, std::move(filename));
    if (!read.ok()) {
        setError(*read.error);
        return false;
    }
    const bool opened =
        openDocumentInternal(std::move(read.document), context, sourceFile);
    if (opened) resetHistory(true);
    return opened;
}

bool DesignPreviewWorkbench::openDesignFile(
    const std::string& filename, DesignRuntimeContext* context) {
    const auto loaded = documentStore_.load(filename);
    if (!loaded.ok()) {
        setStoreDiagnostics(loaded.diagnostics, filename);
        return false;
    }
    const bool opened =
        openDocumentInternal(loaded.document, context, filename);
    if (!opened) return false;
    resetHistory(true);
    loadedRevision_ = loaded.revision;
    hasLoadedRevision_ = true;
    if (!loaded.diagnostics.empty()) {
        setStoreDiagnostics(loaded.diagnostics, filename);
    }
    return true;
}

bool DesignPreviewWorkbench::openDocument(DesignDocument document,
                                          DesignRuntimeContext* context) {
    const bool opened =
        openDocumentInternal(std::move(document), context, "<design>");
    if (opened) resetHistory(true);
    return opened;
}

std::optional<DesignWorkbenchSession>
DesignPreviewWorkbench::snapshotSession() const {
    if (!document_.has_value()) return std::nullopt;
    DesignWorkbenchSession session{
        *document_, sourceFile_, hasLoadedRevision_
            ? std::optional<std::uint64_t>{loadedRevision_} : std::nullopt};
    session.selection_ = selection_.state();
    session.history_ = history_;
    return session;
}

bool DesignPreviewWorkbench::restoreSession(
    const DesignWorkbenchSession& session, DesignRuntimeContext* context) {
    if (!openDocumentInternal(session.document_, context, session.sourceFile_)) {
        return false;
    }
    history_ = session.history_;
    loadedRevision_ = session.loadedRevision_.value_or(0);
    hasLoadedRevision_ = session.loadedRevision_.has_value();
    restoreSelection(session.selection_);
    return true;
}

void DesignPreviewWorkbench::markSaved(std::string filename,
                                       std::uint64_t revision) {
    if (!document_.has_value()) return;
    sourceFile_ = std::move(filename);
    loadedRevision_ = revision;
    hasLoadedRevision_ = true;
    history_.markSaved();
}

bool DesignPreviewWorkbench::openDocumentInternal(
    DesignDocument document, DesignRuntimeContext* context,
    std::string sourceFile) {
    if (sourceFile.empty()) sourceFile = "<design>";
    const bool compiled = context == nullptr
                              ? frame_.tryReplaceDocument(document)
                              : frame_.tryReplaceDocument(document, *context);
    setFrameDiagnostics(sourceFile);
    if (!compiled && !recoverablePreviewFailure(frame_, diagnostics_)) {
        return false;
    }
    document_ = std::move(document);
    sourceFile_ = std::move(sourceFile);
    loadedRevision_ = 0;
    hasLoadedRevision_ = false;
    selection_.setDocument(*document_);
    return true;
}

bool DesignPreviewWorkbench::refresh(DesignRuntimeContext* context) {
    if (!document_.has_value()) {
        diagnostics_.clear();
        return false;
    }
    return updateFrame(context);
}

bool DesignPreviewWorkbench::setProperty(DesignNodeId id, std::string property,
                                         DesignValue value) {
    return setProperty(std::vector<DesignNodeId>{id}, std::move(property),
                       std::move(value));
}

bool DesignPreviewWorkbench::setProperty(
    std::vector<DesignNodeId> ids, std::string property, DesignValue value) {
    if (ids.empty()) return false;
    std::set<DesignNodeId> affected(ids.begin(), ids.end());
    return applyEdit(
        "Edit properties", affected,
        [ids = std::move(ids), property = std::move(property),
         value = std::move(value)](DesignDocumentEditor& editor) mutable {
            for (const auto id : ids) {
                if (!editor.setProperty(id, property, value)) return false;
            }
            return true;
        });
}

bool DesignPreviewWorkbench::setProperties(
    DesignNodeId id,
    std::vector<std::pair<std::string, DesignValue>> properties) {
    if (properties.empty()) return false;
    return applyEdit(
        "Edit properties", {id},
        [id, properties = std::move(properties)](
            DesignDocumentEditor& editor) mutable {
            for (auto& [name, value] : properties) {
                if (!editor.setProperty(id, std::move(name),
                                         std::move(value))) {
                    return false;
                }
            }
            return true;
        });
}

bool DesignPreviewWorkbench::clearProperty(DesignNodeId id,
                                           std::string_view property) {
    const std::string name{property};
    return applyEdit("Clear property", {id},
                     [id, name](DesignDocumentEditor& editor) {
                         return editor.clearProperty(id, name);
                     });
}

bool DesignPreviewWorkbench::setReference(DesignNodeId id, std::string name,
                                          std::string value) {
    return applyEdit(
        "Edit reference", {id},
        [id, name = std::move(name), value = std::move(value)](
            DesignDocumentEditor& editor) mutable {
            return editor.setReference(id, std::move(name), std::move(value));
        });
}

bool DesignPreviewWorkbench::clearReference(DesignNodeId id,
                                            std::string_view name) {
    const std::string reference{name};
    return applyEdit("Clear reference", {id},
                     [id, reference](DesignDocumentEditor& editor) {
                         return editor.clearReference(id, reference);
                     });
}

std::optional<DesignNodeId> DesignPreviewWorkbench::insertNode(
    DesignNodeId parentId, std::size_t index, DesignNode node,
    std::string slot) {
    std::optional<DesignNodeId> inserted;
    const bool changed = applyEdit(
        "Insert node", {parentId},
        [parentId, index, node = std::move(node), slot = std::move(slot),
         &inserted](DesignDocumentEditor& editor) mutable {
            inserted.emplace();
            return editor.insertChild(parentId, index, std::move(node),
                                      &*inserted,
                                      std::move(slot));
        },
        [&inserted](const DesignDocument&, const DesignSelection& before) {
            auto after = before;
            if (inserted.has_value()) {
                after.ids = {*inserted};
                after.primary = *inserted;
                after.anchor = *inserted;
                after.captured.reset();
            }
            return after;
        });
    return changed ? inserted : std::nullopt;
}

std::vector<DesignNodeId> DesignPreviewWorkbench::insertNodes(
    DesignNodeId parentId, std::size_t index, std::vector<DesignNode> nodes,
    std::string slot) {
    if (nodes.empty()) return {};
    std::vector<DesignNodeId> inserted;
    const bool changed = applyEdit(
        "Insert nodes", {parentId},
        [parentId, index, nodes = std::move(nodes), slot = std::move(slot),
         &inserted](DesignDocumentEditor& editor) mutable {
            std::size_t insertionIndex = index;
            for (auto& node : nodes) {
                DesignNodeId insertedId = 0;
                if (!editor.insertChild(parentId, insertionIndex++,
                                        std::move(node), &insertedId, slot)) {
                    return false;
                }
                inserted.push_back(insertedId);
            }
            return true;
        },
        [&inserted](const DesignDocument&, const DesignSelection& before) {
            auto after = before;
            if (!inserted.empty()) {
                after.ids = std::set<DesignNodeId>(inserted.begin(),
                                                   inserted.end());
                after.primary = inserted.back();
                after.anchor = inserted.front();
                after.captured.reset();
            }
            return after;
        });
    return changed ? inserted : std::vector<DesignNodeId>{};
}

bool DesignPreviewWorkbench::removeNode(DesignNodeId id) {
    return removeNodes({id});
}

bool DesignPreviewWorkbench::removeNodes(std::vector<DesignNodeId> ids) {
    if (!document_.has_value() || ids.empty()) return false;

    std::set<DesignNodeId> selected(ids.begin(), ids.end());
    if (selected.contains(document_->root.id)) {
        setEditError("the root node cannot be removed");
        return false;
    }
    for (const auto id : selected) {
        if (findNode(document_->root, id) == nullptr) {
            setEditError("node is not present in the document");
            return false;
        }
    }

    std::vector<DesignNodeId> roots;
    std::function<void(const DesignNode&, bool)> collectRoots =
        [&](const DesignNode& node, bool selectedAncestor) {
            const bool isSelected = selected.contains(node.id);
            if (isSelected && !selectedAncestor) {
                roots.push_back(node.id);
                return;
            }
            for (const auto& child : node.children) {
                collectRoots(child, selectedAncestor || isSelected);
            }
            for (const auto& [slot, children] : node.slots) {
                (void)slot;
                for (const auto& child : children) {
                    collectRoots(child, selectedAncestor || isSelected);
                }
            }
        };
    collectRoots(document_->root, false);
    if (roots.empty()) {
        setEditError("node is not present in the document");
        return false;
    }

    const auto primary = selection_.state().primary;
    const auto primaryRoot =
        primary.has_value() && std::find(roots.begin(), roots.end(), *primary) !=
                                   roots.end()
            ? *primary
            : roots.front();
    const auto location = locateNode(document_->root, primaryRoot);
    if (!location.has_value()) {
        setEditError("node is not present in the document");
        return false;
    }
    const auto fallbackParent = location->parent;
    return applyEdit(
        "Remove nodes", std::set<DesignNodeId>(roots.begin(), roots.end()),
        [roots = std::move(roots)](DesignDocumentEditor& editor) {
            for (const auto id : roots) {
                if (!editor.removeNode(id).has_value()) return false;
            }
            return true;
        },
        [fallbackParent](const DesignDocument& after,
                         const DesignSelection&) {
            DesignSelection selection;
            const auto selectedParent = findNode(after.root, fallbackParent);
            const auto id = selectedParent == nullptr ? after.root.id
                                                      : fallbackParent;
            selection.ids = {id};
            selection.primary = id;
            selection.anchor = id;
            return selection;
        });
}

bool DesignPreviewWorkbench::moveNode(DesignNodeId id,
                                      DesignNodeId newParentId,
                                      std::size_t index, std::string slot) {
    return applyEdit(
        "Move node", {id, newParentId},
        [id, newParentId, index, slot = std::move(slot)](
            DesignDocumentEditor& editor) mutable {
            return editor.moveNode(id, newParentId, index, std::move(slot));
        });
}

bool DesignPreviewWorkbench::moveNodeRelative(DesignNodeId id, int offset) {
    return moveNodesRelative({id}, offset);
}

bool DesignPreviewWorkbench::moveNodesRelative(
    std::vector<DesignNodeId> ids, int offset) {
    if (!document_.has_value() || ids.empty() || offset == 0) return false;

    const std::set<DesignNodeId> selected(ids.begin(), ids.end());
    if (selected.contains(document_->root.id)) {
        setEditError("the root node cannot be moved");
        return false;
    }
    for (const auto id : selected) {
        if (findNode(document_->root, id) == nullptr) {
            setEditError("node is not present in the document");
            return false;
        }
    }

    std::vector<DesignNodeId> roots;
    std::function<void(const DesignNode&, bool)> collectRoots =
        [&](const DesignNode& node, bool selectedAncestor) {
            const bool isSelected = selected.contains(node.id);
            if (isSelected && !selectedAncestor) {
                roots.push_back(node.id);
                return;
            }
            for (const auto& child : node.children) {
                collectRoots(child, selectedAncestor || isSelected);
            }
            for (const auto& [slot, children] : node.slots) {
                (void)slot;
                for (const auto& child : children) {
                    collectRoots(child, selectedAncestor || isSelected);
                }
            }
        };
    collectRoots(document_->root, false);
    if (roots.empty()) {
        setEditError("node is not present in the document");
        return false;
    }

    const auto firstLocation = locateNode(document_->root, roots.front());
    if (!firstLocation.has_value()) {
        setEditError("node is not present in the document");
        return false;
    }
    const auto parentId = firstLocation->parent;
    const auto slot = firstLocation->slot;
    const auto* parent = findNode(document_->root, parentId);
    if (parent == nullptr) return false;
    const auto siblingCount = slot.empty()
                                  ? parent->children.size()
                                  : parent->slots.at(slot).size();

    struct Move {
        DesignNodeId id;
        std::size_t source;
        std::size_t target;
    };
    std::vector<Move> moves;
    moves.reserve(roots.size());
    for (const auto id : roots) {
        const auto location = locateNode(document_->root, id);
        if (!location.has_value() || location->parent != parentId ||
            location->slot != slot) {
            setEditError("selected nodes must share a sibling list");
            return false;
        }
        const auto target = static_cast<std::int64_t>(location->index) + offset;
        if (target < 0 || target >= static_cast<std::int64_t>(siblingCount)) {
            setEditError("node cannot move outside its sibling list");
            return false;
        }
        moves.push_back(
            Move{id, location->index, static_cast<std::size_t>(target)});
    }

    std::sort(moves.begin(), moves.end(), [offset](const Move& left,
                                                   const Move& right) {
        return offset < 0 ? left.source < right.source
                           : left.source > right.source;
    });
    std::set<DesignNodeId> affected(roots.begin(), roots.end());
    affected.insert(parentId);
    return applyEdit(
        "Move nodes", std::move(affected),
        [parentId, slot, moves = std::move(moves)](
            DesignDocumentEditor& editor) mutable {
            for (const auto& move : moves) {
                if (!editor.moveNode(move.id, parentId, move.target, slot)) {
                    return false;
                }
            }
            return true;
        });
}

std::optional<DesignNodeId> DesignPreviewWorkbench::duplicateNode(
    DesignNodeId id, DesignNodeId newParentId, std::size_t index,
    std::string slot) {
    std::optional<DesignNodeId> inserted;
    const bool changed = applyEdit(
        "Duplicate node", {id, newParentId},
        [id, newParentId, index, slot = std::move(slot), &inserted](
            DesignDocumentEditor& editor) mutable {
            inserted = editor.duplicateNode(id, newParentId, index,
                                            std::move(slot));
            return inserted.has_value();
        },
        [&inserted](const DesignDocument&, const DesignSelection& before) {
            auto after = before;
            if (inserted.has_value()) {
                after.ids = {*inserted};
                after.primary = *inserted;
                after.anchor = *inserted;
                after.captured.reset();
            }
            return after;
        });
    return changed ? inserted : std::nullopt;
}

std::optional<DesignNodeId> DesignPreviewWorkbench::duplicateNode(
    DesignNodeId id) {
    if (!document_.has_value()) return std::nullopt;
    const auto location = locateNode(document_->root, id);
    if (!location.has_value()) return std::nullopt;
    return duplicateNode(id, location->parent, location->index + 1,
                         location->slot);
}

bool DesignPreviewWorkbench::undo() {
    if (!document_.has_value()) return false;
    auto document = *document_;
    auto selection = selection_.state();
    if (!history_.undo(document, selection)) return false;
    document_ = std::move(document);
    restoreSelection(selection);
    const bool compiled = updateFrame(editContext_);
    return compiled || recoverablePreviewFailure(frame_, diagnostics_);
}

bool DesignPreviewWorkbench::redo() {
    if (!document_.has_value()) return false;
    auto document = *document_;
    auto selection = selection_.state();
    if (!history_.redo(document, selection)) return false;
    document_ = std::move(document);
    restoreSelection(selection);
    const bool compiled = updateFrame(editContext_);
    return compiled || recoverablePreviewFailure(frame_, diagnostics_);
}

bool DesignPreviewWorkbench::saveDesignFile(const std::string& filename) {
    const auto expected = hasLoadedRevision_ &&
                                  sameDocumentPath(sourceFile_, filename)
                              ? std::optional<std::uint64_t>{loadedRevision_}
                              : std::nullopt;
    return saveDesignFileAtRevision(filename, expected);
}

bool DesignPreviewWorkbench::overwriteDesignFile(
    const std::string& filename, std::uint64_t observedRevision) {
    return saveDesignFileAtRevision(filename, observedRevision);
}

bool DesignPreviewWorkbench::saveDesignFileAtRevision(
    const std::string& filename,
    std::optional<std::uint64_t> expectedRevision) {
    if (!document_.has_value()) {
        setEditError("no design document is open");
        return false;
    }
    std::vector<DesignError> errors;
    if (!documentStore_.save(filename, *document_, errors, expectedRevision)) {
        setStoreDiagnostics(errors, filename, true);
        return false;
    }
    const auto saved = documentStore_.load(filename);
    markSaved(filename, saved.revision);
    diagnostics_.clear();
    return true;
}

void DesignPreviewWorkbench::clear() {
    frame_.clear();
    selection_.clear();
    document_.reset();
    diagnostics_.clear();
    sourceFile_ = "<design>";
    history_.clear();
    loadedRevision_ = 0;
    hasLoadedRevision_ = false;
}

bool DesignPreviewWorkbench::applyEdit(std::string label,
                                       std::set<DesignNodeId> affectedIds,
                                       EditOperation operation,
                                       SelectionTransform selectionTransform) {
    if (!document_.has_value() || !operation) return false;

    const DesignDocument before = *document_;
    const DesignSelection beforeSelection = selection_.state();
    DesignDocumentEditor editor{before};
    if (!operation(editor)) {
        setEditError("edit rejected by the L0 document schema");
        return false;
    }
    const DesignDocument after = editor.document();
    if (after == before) return true;

    const DesignSelection afterSelection =
        selectionTransform ? selectionTransform(after, beforeSelection)
                            : beforeSelection;
    DesignDocumentCommand command;
    command.label = label;
    command.affectedIds = std::move(affectedIds);
    command.precondition = [before](const DesignDocument& candidate) {
        return candidate == before;
    };
    command.apply = [after](DesignDocument& candidate) {
        candidate = after;
        return true;
    };
    command.revert = [before](DesignDocument& candidate) {
        candidate = before;
        return true;
    };

    auto transaction = history_.begin(before, beforeSelection);
    transaction.setLabel(label);
    if (!transaction.apply(std::move(command))) {
        setEditError("document transaction rejected the edit");
        return false;
    }
    transaction.setSelectionAfter(afterSelection);

    auto prepared = editContext_ == nullptr
                        ? frame_.prepareDocument(after)
                        : frame_.prepareDocument(after, *editContext_);
    const bool recoverable = !prepared.trace.nodes.empty() &&
        !prepared.diagnostics.empty() &&
        std::all_of(prepared.diagnostics.begin(), prepared.diagnostics.end(),
                    [](const DesignError& error) {
                        return error.code.rfind("reference.", 0) == 0 ||
                               error.code.rfind("component.", 0) == 0;
                    });
    if (!prepared.ok() && !recoverable) {
        (void)frame_.tryReplaceDocument(after, std::move(prepared));
        setFrameDiagnostics(sourceFile_);
        return false;
    }

    auto committedDocument = before;
    auto committedSelection = beforeSelection;
    if (!history_.commit(committedDocument, committedSelection,
                         std::move(transaction))) {
        setEditError("document transaction could not be committed");
        return false;
    }
    document_ = std::move(committedDocument);
    restoreSelection(committedSelection);
    (void)frame_.tryReplaceDocument(*document_, std::move(prepared));
    setFrameDiagnostics(sourceFile_);
    return true;
}

void DesignPreviewWorkbench::resetHistory(bool saved) {
    history_.clear();
    if (saved) history_.markSaved();
    loadedRevision_ = 0;
    hasLoadedRevision_ = false;
}

void DesignPreviewWorkbench::restoreSelection(const DesignSelection& selection) {
    if (!document_.has_value()) {
        selection_.clear();
        return;
    }
    if (!selection_.setSelection(selection.ids, selection.primary,
                                 selection.anchor, *document_)) {
        selection_.clear();
    }
    selection_.setFocusPanel(selection.focusPanel);
    if (selection.captured.has_value()) {
        (void)selection_.capture(*selection.captured);
    } else {
        selection_.releaseCapture();
    }
}

void DesignPreviewWorkbench::setEditError(std::string message) {
    DesignError error;
    error.code = "editor.rejected";
    error.file = sourceFile_;
    error.message = std::move(message);
    diagnostics_.clear();
    appendDesignDiagnostic(
        diagnostics_,
        DesignDiagnostic::fromError(error, DesignDiagnosticStage::Schema));
}

void DesignPreviewWorkbench::setStoreDiagnostics(
    const std::vector<DesignError>& errors, const std::string& filename,
    bool saving) {
    diagnostics_.clear();
    for (const auto& error : errors) {
        auto diagnostic = DesignDiagnostic::fromError(error);
        if (saving) {
            diagnostic.recoverability = DesignDiagnosticRecoverability::BlockSave;
            if (document_.has_value()) diagnostic.documentId = document_->documentId;
        }
        if (diagnostic.file.empty() || diagnostic.file == "<design>") {
            diagnostic.file = filename;
        }
        appendDesignDiagnostic(diagnostics_, std::move(diagnostic));
    }
}

bool DesignPreviewWorkbench::updateFrame(DesignRuntimeContext* context) {
    const bool compiled = context == nullptr
                              ? frame_.update(*document_)
                              : frame_.update(*document_, *context);
    setFrameDiagnostics(sourceFile_);
    return compiled;
}

void DesignPreviewWorkbench::setFrameDiagnostics(const std::string& sourceFile) {
    diagnostics_ = frame_.diagnostics();
    for (auto& diagnostic : diagnostics_) {
        if (diagnostic.file.empty() || diagnostic.file == "<design>") {
            diagnostic.file = sourceFile;
        }
    }
}

void DesignPreviewWorkbench::setError(const DesignError& error) {
    diagnostics_.clear();
    auto diagnostic = DesignDiagnostic::fromError(error);
    appendDesignDiagnostic(diagnostics_, std::move(diagnostic));
}

const DesignNode* DesignPreviewWorkbench::findNode(const DesignNode& node,
                                                   DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findNode(child, id); found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findNode(child, id); found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

std::string DesignPreviewWorkbench::nodeKey(const DesignNode& node) {
    const auto found = node.properties.find("key");
    if (found == node.properties.end()) return {};
    const auto* value = std::get_if<std::string>(&found->second.value);
    return value == nullptr ? std::string{} : *value;
}

DesignPreviewOutlineNode DesignPreviewWorkbench::makeOutline(
    const DesignNode& node, std::string path) {
    DesignPreviewOutlineNode result{node.id, node.type, nodeKey(node),
                                    std::move(path), {}};
    result.children.reserve(node.children.size() + node.slots.size());
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        result.children.push_back(makeOutline(
            node.children[index],
            result.path + ".children[" + std::to_string(index) + "]"));
    }
    for (const auto& [slot, children] : node.slots) {
        for (std::size_t index = 0; index < children.size(); ++index) {
            result.children.push_back(makeOutline(
                children[index], result.path + ".slots[" + slot + "][" +
                                     std::to_string(index) + "]"));
        }
    }
    return result;
}

bool DesignPreviewWorkbench::selectNode(DesignNodeId id,
                                        DesignSelectionMode mode) {
    if (!document_.has_value()) return false;
    return selection_.select(id, mode, *document_);
}

bool DesignPreviewWorkbench::selectRuntimeIdentity(
    std::string_view identity, DesignSelectionMode mode) {
    const auto id = nodeForRuntimeIdentity(identity);
    return id.has_value() && selectNode(*id, mode);
}

std::optional<DesignNodeId> DesignPreviewWorkbench::nodeForRuntimeIdentity(
    std::string_view identity) const {
    for (const auto& [id, reference] : frame_.trace().nodes) {
        if (!reference.runtimeOnly && reference.runtimeIdentity == identity) {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<std::string> DesignPreviewWorkbench::runtimeIdentity(
    DesignNodeId id) const {
    const auto found = frame_.trace().nodes.find(id);
    if (found == frame_.trace().nodes.end() || found->second.runtimeOnly) {
        return std::nullopt;
    }
    return found->second.runtimeIdentity;
}

std::optional<DesignPreviewOutlineNode> DesignPreviewWorkbench::outline() const {
    if (!document_.has_value()) return std::nullopt;
    return makeOutline(document_->root, "root");
}

std::vector<DesignPreviewProperty> DesignPreviewWorkbench::properties(
    DesignNodeId id) const {
    std::vector<DesignPreviewProperty> result;
    if (!document_.has_value()) return result;
    const auto* node = findNode(document_->root, id);
    if (node == nullptr) return result;

    std::map<std::string, DesignPreviewProperty> byName;
    for (const auto& [name, value] : node->properties) {
        byName[name] = DesignPreviewProperty{name, value, std::nullopt};
    }
    for (const auto& [name, reference] : node->references) {
        auto& property = byName[name];
        property.name = name;
        property.reference = reference;
    }
    result.reserve(byName.size());
    for (auto& [name, property] : byName) {
        (void)name;
        result.push_back(std::move(property));
    }
    return result;
}

}  // namespace lumen::dsl
