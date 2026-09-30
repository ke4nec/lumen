#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lumen/dsl/design_preview_frame.h"

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
// Runtime contexts are borrowed only for the duration of open/refresh.
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
    [[nodiscard]] bool openDocument(
        DesignDocument document, DesignRuntimeContext* context = nullptr);
    [[nodiscard]] bool refresh(DesignRuntimeContext* context = nullptr);

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

    std::optional<DesignDocument> document_{};
    DesignPreviewFrame frame_{};
    DesignSelectionModel selection_{};
    std::vector<DesignDiagnostic> diagnostics_{};
    std::string sourceFile_{"<design>"};
};

}  // namespace lumen::dsl
