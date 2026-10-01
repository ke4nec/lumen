#include "lumen/dsl/design_workbench.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
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

bool selectionHasMissingNode(const DesignSelection& selection,
                             const DesignDocument& document) {
    for (const auto id : selection.ids) {
        if (findNode(document.root, id) == nullptr) {
            return true;
        }
    }
    return false;
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
        setStoreDiagnostics(loaded.diagnostics);
        return false;
    }
    const bool opened =
        openDocumentInternal(loaded.document, context, filename);
    if (!opened) return false;
    resetHistory(true);
    loadedRevision_ = loaded.revision;
    hasLoadedRevision_ = true;
    if (!loaded.diagnostics.empty()) setStoreDiagnostics(loaded.diagnostics);
    return true;
}

bool DesignPreviewWorkbench::openDocument(DesignDocument document,
                                          DesignRuntimeContext* context) {
    const bool opened =
        openDocumentInternal(std::move(document), context, "<design>");
    if (opened) resetHistory(true);
    return opened;
}

bool DesignPreviewWorkbench::openDocumentInternal(
    DesignDocument document, DesignRuntimeContext* context,
    std::string sourceFile) {
    document_ = std::move(document);
    sourceFile_ = sourceFile.empty() ? "<design>" : std::move(sourceFile);
    selection_.setDocument(*document_);
    return updateFrame(context);
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
    return applyEdit(
        "Edit property", {id},
        [id, property = std::move(property), value = std::move(value)](
            DesignDocumentEditor& editor) mutable {
            return editor.setProperty(id, std::move(property),
                                      std::move(value));
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
    if (!document_.has_value() || id == document_->root.id) {
        setEditError("the root node cannot be removed");
        return false;
    }
    const auto location = locateNode(document_->root, id);
    if (!location.has_value()) {
        setEditError("node is not present in the document");
        return false;
    }
    return applyEdit(
        "Remove node", {id, location->parent},
        [id](DesignDocumentEditor& editor) {
            return editor.removeNode(id).has_value();
        },
        [parentId = location->parent](const DesignDocument& after,
                                      const DesignSelection& before) {
            if (!selectionHasMissingNode(before, after)) return before;
            auto selection = before;
            selection.ids = {parentId};
            selection.primary = parentId;
            selection.anchor = parentId;
            selection.captured.reset();
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
    if (!document_.has_value() || offset == 0) return false;
    const auto location = locateNode(document_->root, id);
    if (!location.has_value()) return false;
    const auto* parent = findNode(document_->root, location->parent);
    if (parent == nullptr) return false;
    std::size_t siblings = parent->children.size();
    if (!location->slot.empty()) {
        const auto slot = parent->slots.find(location->slot);
        if (slot == parent->slots.end()) return false;
        siblings = slot->second.size();
    }
    const auto target = static_cast<std::int64_t>(location->index) + offset;
    if (target < 0 || target >= static_cast<std::int64_t>(siblings)) {
        setEditError("node cannot move outside its sibling list");
        return false;
    }
    return moveNode(id, location->parent,
                    static_cast<std::size_t>(target), location->slot);
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
    if (!document_.has_value()) {
        setEditError("no design document is open");
        return false;
    }
    std::vector<DesignError> errors;
    const auto expected = hasLoadedRevision_
                              ? std::optional<std::uint64_t>{loadedRevision_}
                              : std::nullopt;
    if (!documentStore_.save(filename, *document_, errors, expected)) {
        setStoreDiagnostics(errors);
        return false;
    }
    sourceFile_ = filename;
    const auto saved = documentStore_.load(filename);
    loadedRevision_ = saved.revision;
    hasLoadedRevision_ = true;
    history_.markSaved();
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

    auto committedDocument = before;
    auto committedSelection = beforeSelection;
    if (!history_.commit(committedDocument, committedSelection,
                         std::move(transaction))) {
        setEditError("document transaction could not be committed");
        return false;
    }
    document_ = std::move(committedDocument);
    restoreSelection(committedSelection);
    const bool compiled = updateFrame(editContext_);
    if (!compiled && !recoverablePreviewFailure(frame_, diagnostics_)) {
        auto rollbackDocument = *document_;
        auto rollbackSelection = selection_.state();
        if (history_.undo(rollbackDocument, rollbackSelection)) {
            document_ = std::move(rollbackDocument);
            restoreSelection(rollbackSelection);
        }
        setEditError("edited document could not be previewed");
        return false;
    }
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
    const std::vector<DesignError>& errors) {
    diagnostics_.clear();
    for (const auto& error : errors) {
        auto diagnostic = DesignDiagnostic::fromError(
            error, error.code.rfind("store.", 0) == 0
                       ? std::optional<DesignDiagnosticStage>{
                             DesignDiagnosticStage::Save}
                       : std::nullopt);
        if (diagnostic.file.empty()) diagnostic.file = sourceFile_;
        appendDesignDiagnostic(diagnostics_, std::move(diagnostic));
    }
}

bool DesignPreviewWorkbench::updateFrame(DesignRuntimeContext* context) {
    const bool compiled = context == nullptr
                              ? frame_.update(*document_)
                              : frame_.update(*document_, *context);
    diagnostics_ = frame_.diagnostics();
    for (auto& diagnostic : diagnostics_) {
        if (diagnostic.file.empty() || diagnostic.file == "<design>") {
            diagnostic.file = sourceFile_;
        }
    }
    return compiled;
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
