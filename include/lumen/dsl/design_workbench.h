#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumen/dsl/design_editor.h"
#include "lumen/dsl/design_preview_frame.h"
#include "lumen/dsl/document_store.h"

namespace lumen::dsl {

struct DesignPreviewProperty {
    std::string name{};
    std::optional<DesignValue> value{};
    std::optional<std::string> reference{};

    bool operator==(const DesignPreviewProperty&) const = default;
};

struct DesignPreviewOutlineNode {
    DesignNodeId id{0};
    std::string type{};
    std::string key{};
    std::string path{};
    std::vector<DesignPreviewOutlineNode> children{};

    bool operator==(const DesignPreviewOutlineNode&) const = default;
};

// Headless D2 model. It owns the document being inspected and composes the
// preview frame, document selection, outline, and read-only property views.
// Runtime contexts are borrowed for open/refresh. An optional edit context can
// also be registered by an application adapter so document transactions and
// undo/redo can rebuild component previews without owning the adapter.
class DesignPreviewWorkbench {
  public:
    [[nodiscard]] bool openLumenSource(
        const std::string& source, std::string filename = "<memory>",
        DesignRuntimeContext* context = nullptr);
    [[nodiscard]] bool openLumenFile(
        const std::string& filename,
        DesignRuntimeContext* context = nullptr);
    [[nodiscard]] bool openDesignSource(
        const std::string& source, std::string filename = "<memory>",
        DesignRuntimeContext* context = nullptr);
    [[nodiscard]] bool openDesignFile(
        const std::string& filename, DesignRuntimeContext* context = nullptr);
    [[nodiscard]] bool openDocument(
        DesignDocument document, DesignRuntimeContext* context = nullptr);
    [[nodiscard]] bool refresh(DesignRuntimeContext* context = nullptr);

    void setEditRuntimeContext(DesignRuntimeContext* context) {
        editContext_ = context;
    }

    // D3 L0 editing is declaration-only. Runtime preview values remain in
    // DesignPreviewState and never enter these document transactions.
    [[nodiscard]] bool setProperty(DesignNodeId id, std::string property,
                                    DesignValue value);
    // Applies one declaration property to every selected node as one
    // undoable transaction. Schema validation is all-or-nothing.
    [[nodiscard]] bool setProperty(std::vector<DesignNodeId> ids,
                                   std::string property, DesignValue value);
    // Applies several declaration properties as one undoable document edit.
    // This is used by gesture-based controls such as canvas resizing, where
    // width/height/position together form one user intent.
    [[nodiscard]] bool setProperties(
        DesignNodeId id,
        std::vector<std::pair<std::string, DesignValue>> properties);
    [[nodiscard]] bool clearProperty(DesignNodeId id,
                                     std::string_view property);
    [[nodiscard]] bool setReference(DesignNodeId id, std::string name,
                                    std::string value);
    [[nodiscard]] bool clearReference(DesignNodeId id,
                                      std::string_view name);
    [[nodiscard]] std::optional<DesignNodeId> insertNode(
        DesignNodeId parentId, std::size_t index, DesignNode node,
        std::string slot = {});
    // Inserts a group of sibling nodes as one undoable document transaction.
    // The returned ids preserve insertion order and are selected together.
    [[nodiscard]] std::vector<DesignNodeId> insertNodes(
        DesignNodeId parentId, std::size_t index,
        std::vector<DesignNode> nodes, std::string slot = {});
    [[nodiscard]] bool removeNode(DesignNodeId id);
    // Removes a set of top-level selected nodes as one undoable transaction.
    [[nodiscard]] bool removeNodes(std::vector<DesignNodeId> ids);
    [[nodiscard]] bool moveNode(DesignNodeId id, DesignNodeId newParentId,
                                std::size_t index, std::string slot = {});
    [[nodiscard]] bool moveNodeRelative(DesignNodeId id, int offset);
    // Moves top-level selected nodes within one sibling list as one undoable
    // transaction, preserving their selection and relative order.
    [[nodiscard]] bool moveNodesRelative(std::vector<DesignNodeId> ids,
                                          int offset);
    [[nodiscard]] std::optional<DesignNodeId> duplicateNode(DesignNodeId id,
                                                              DesignNodeId newParentId,
                                                              std::size_t index,
                                                              std::string slot = {});
    [[nodiscard]] std::optional<DesignNodeId> duplicateNode(DesignNodeId id);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool saveDesignFile(const std::string& filename);

    [[nodiscard]] bool dirty() const { return history_.dirty(); }
    [[nodiscard]] bool canUndo() const { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const { return history_.canRedo(); }
    [[nodiscard]] std::uint64_t documentRevision() const {
        return history_.documentRevision();
    }

    void clear();

    [[nodiscard]] const std::optional<DesignDocument>& document() const {
        return document_;
    }
    [[nodiscard]] const DesignPreviewFrame& frame() const { return frame_; }
    [[nodiscard]] const std::vector<DesignDiagnostic>& diagnostics() const {
        return diagnostics_;
    }
    [[nodiscard]] const DesignSelection& selection() const {
        return selection_.state();
    }

    [[nodiscard]] bool selectNode(
        DesignNodeId id,
        DesignSelectionMode mode = DesignSelectionMode::Replace);
    [[nodiscard]] bool selectRuntimeIdentity(
        std::string_view identity,
        DesignSelectionMode mode = DesignSelectionMode::Replace);
    [[nodiscard]] std::optional<DesignNodeId> nodeForRuntimeIdentity(
        std::string_view identity) const;
    [[nodiscard]] std::optional<std::string> runtimeIdentity(
        DesignNodeId id) const;

    [[nodiscard]] std::optional<DesignPreviewOutlineNode> outline() const;
    [[nodiscard]] std::vector<DesignPreviewProperty> properties(
        DesignNodeId id) const;

  private:
    [[nodiscard]] bool openDocumentInternal(
        DesignDocument document, DesignRuntimeContext* context,
        std::string sourceFile);
    [[nodiscard]] bool updateFrame(DesignRuntimeContext* context);
    void setError(const DesignError& error);
    [[nodiscard]] static const DesignNode* findNode(const DesignNode& node,
                                                    DesignNodeId id);
    [[nodiscard]] static DesignPreviewOutlineNode makeOutline(
        const DesignNode& node, std::string path);
    [[nodiscard]] static std::string nodeKey(const DesignNode& node);

    using EditOperation = std::function<bool(DesignDocumentEditor&)>;
    using SelectionTransform =
        std::function<DesignSelection(const DesignDocument&,
                                      const DesignSelection&)>;

    [[nodiscard]] bool applyEdit(std::string label,
                                 std::set<DesignNodeId> affectedIds,
                                 EditOperation operation,
                                 SelectionTransform selectionTransform = {});
    void resetHistory(bool saved);
    void restoreSelection(const DesignSelection& selection);
    void setEditError(std::string message);
    void setStoreDiagnostics(const std::vector<DesignError>& errors);

    std::optional<DesignDocument> document_{};
    DesignPreviewFrame frame_{};
    DesignSelectionModel selection_{};
    DesignDocumentHistory history_{};
    DocumentStore documentStore_{};
    std::vector<DesignDiagnostic> diagnostics_{};
    std::string sourceFile_{"<design>"};
    std::uint64_t loadedRevision_{0};
    bool hasLoadedRevision_{false};
    DesignRuntimeContext* editContext_{nullptr};
};

}  // namespace lumen::dsl
