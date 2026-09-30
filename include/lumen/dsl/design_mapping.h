#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/dsl/design_document.h"

namespace lumen::dsl {

// Source locations used by editor navigation and diagnostics. The map owns
// only value data, so it remains valid after the source document is released.
class DesignSourceMap {
  public:
    static DesignSourceMap fromDocument(const DesignDocument& document);

    [[nodiscard]] std::optional<DesignSourceSpan> nodeSpan(
        DesignNodeId id) const;
    [[nodiscard]] std::optional<DesignSourceSpan> propertySpan(
        DesignNodeId id, std::string_view property) const;

  private:
    std::map<DesignNodeId, DesignSourceSpan> nodes_{};
    std::map<DesignNodeId, std::map<std::string, DesignSourceSpan>> properties_{};
};

// Converts between window pixels, host logical coordinates, and design
// coordinates. Pan is expressed in logical canvas coordinates.
class DesignCoordinateTransform {
  public:
    [[nodiscard]] float deviceScale() const { return deviceScale_; }
    [[nodiscard]] float zoom() const { return zoom_; }
    [[nodiscard]] core::Offset pan() const { return pan_; }

    [[nodiscard]] bool setDeviceScale(float value);
    [[nodiscard]] bool setZoom(float value);
    [[nodiscard]] bool setPan(core::Offset value);

    [[nodiscard]] core::Offset pixelsToDesign(core::Offset pixels) const;
    [[nodiscard]] core::Offset designToPixels(core::Offset design) const;
    [[nodiscard]] core::Offset designToLocal(core::Offset design,
                                              core::Offset nodeOrigin) const;
    [[nodiscard]] core::Rect designRectToPixels(core::Rect design) const;

  private:
    float deviceScale_{1.0F};
    float zoom_{1.0F};
    core::Offset pan_{};
};

// Small value-oriented editor for structural P4 operations. The optional slot
// name selects a named slot; an empty name addresses the ordinary children.
class DesignDocumentEditor {
  public:
    explicit DesignDocumentEditor(DesignDocument document)
        : document_(std::move(document)) {}

    [[nodiscard]] DesignDocument& document() { return document_; }
    [[nodiscard]] const DesignDocument& document() const { return document_; }

    [[nodiscard]] bool insertChild(DesignNodeId parentId, std::size_t index,
                                   DesignNode node,
                                   DesignNodeId* insertedId = nullptr,
                                   std::string slot = {});
    [[nodiscard]] std::optional<DesignNode> removeNode(DesignNodeId id);
    [[nodiscard]] bool moveNode(DesignNodeId id, DesignNodeId newParentId,
                                std::size_t index, std::string slot = {});
    [[nodiscard]] std::optional<DesignNodeId> duplicateNode(
        DesignNodeId id, DesignNodeId newParentId, std::size_t index,
        std::string slot = {});

  private:
    struct ParentLocation {
        DesignNode* parent{nullptr};
        std::vector<DesignNode>* siblings{nullptr};
        std::size_t index{0};
        std::string slot{};
    };

    [[nodiscard]] DesignNode* findNode(DesignNode& node, DesignNodeId id);
    [[nodiscard]] const DesignNode* findNode(const DesignNode& node,
                                             DesignNodeId id) const;
    [[nodiscard]] std::optional<ParentLocation> locateNode(DesignNodeId id);
    [[nodiscard]] bool contains(const DesignNode& node, DesignNodeId id) const;
    [[nodiscard]] bool normalizeIds(DesignNode& node,
                                    std::map<DesignNodeId, bool>& used);
    [[nodiscard]] std::optional<DesignNodeId> allocateId(
        std::map<DesignNodeId, bool>& used);
    [[nodiscard]] bool insertRaw(DesignNodeId parentId, std::size_t index,
                                 DesignNode node, std::string_view slot,
                                 DesignNodeId* insertedId);
    static void collectIds(const DesignNode& node,
                           std::map<DesignNodeId, bool>& ids);

    DesignDocument document_{};
    DesignNodeId nextId_{1};
};

}  // namespace lumen::dsl
