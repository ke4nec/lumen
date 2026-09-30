#include "lumen/dsl/design_mapping.h"

#include <cmath>
#include <functional>
#include <limits>

namespace lumen::dsl {

DesignSourceMap DesignSourceMap::fromDocument(const DesignDocument& document) {
    DesignSourceMap result;
    std::function<void(const DesignNode&)> visit = [&](const DesignNode& node) {
        if (node.source.has_value()) {
            result.nodes_[node.id] = *node.source;
        }
        if (!node.propertySources.empty()) {
            result.properties_[node.id] = node.propertySources;
        }
        for (const auto& child : node.children) visit(child);
        for (const auto& [slot, children] : node.slots) {
            (void)slot;
            for (const auto& child : children) visit(child);
        }
    };
    visit(document.root);
    return result;
}

std::optional<DesignSourceSpan> DesignSourceMap::nodeSpan(
    DesignNodeId id) const {
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) return std::nullopt;
    return found->second;
}

std::optional<DesignSourceSpan> DesignSourceMap::propertySpan(
    DesignNodeId id, std::string_view property) const {
    const auto node = properties_.find(id);
    if (node == properties_.end()) return std::nullopt;
    const auto found = node->second.find(std::string{property});
    if (found == node->second.end()) return std::nullopt;
    return found->second;
}

bool DesignCoordinateTransform::setDeviceScale(float value) {
    if (!std::isfinite(value) || value <= 0.0F) return false;
    deviceScale_ = value;
    return true;
}

bool DesignCoordinateTransform::setZoom(float value) {
    if (!std::isfinite(value) || value <= 0.0F) return false;
    zoom_ = value;
    return true;
}

core::Offset DesignCoordinateTransform::pixelsToDesign(
    core::Offset pixels) const {
    const core::Offset logical{pixels.x / deviceScale_,
                               pixels.y / deviceScale_};
    return core::Offset{(logical.x - pan_.x) / zoom_,
                        (logical.y - pan_.y) / zoom_};
}

core::Offset DesignCoordinateTransform::designToPixels(
    core::Offset design) const {
    const core::Offset logical{design.x * zoom_ + pan_.x,
                               design.y * zoom_ + pan_.y};
    return core::Offset{logical.x * deviceScale_, logical.y * deviceScale_};
}

core::Offset DesignCoordinateTransform::designToLocal(
    core::Offset design, core::Offset nodeOrigin) const {
    return design - nodeOrigin;
}

core::Rect DesignCoordinateTransform::designRectToPixels(
    core::Rect design) const {
    return core::Rect{
        designToPixels(design.origin),
        core::Size{design.size.width * zoom_ * deviceScale_,
                   design.size.height * zoom_ * deviceScale_}};
}

DesignNode* DesignDocumentEditor::findNode(DesignNode& node,
                                            DesignNodeId id) {
    if (node.id == id) return &node;
    for (auto& child : node.children) {
        if (auto* found = findNode(child, id); found != nullptr) return found;
    }
    for (auto& [slot, children] : node.slots) {
        (void)slot;
        for (auto& child : children) {
            if (auto* found = findNode(child, id); found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

const DesignNode* DesignDocumentEditor::findNode(const DesignNode& node,
                                                 DesignNodeId id) const {
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

std::optional<DesignDocumentEditor::ParentLocation>
DesignDocumentEditor::locateNode(DesignNodeId id) {
    std::function<std::optional<ParentLocation>(DesignNode&)> visit =
        [&](DesignNode& parent) -> std::optional<ParentLocation> {
        for (std::size_t index = 0; index < parent.children.size(); ++index) {
            if (parent.children[index].id == id) {
                return ParentLocation{&parent, &parent.children, index, {}};
            }
        }
        for (auto& [slot, children] : parent.slots) {
            for (std::size_t index = 0; index < children.size(); ++index) {
                if (children[index].id == id) {
                    return ParentLocation{&parent, &children, index, slot};
                }
            }
        }
        for (auto& child : parent.children) {
            if (auto found = visit(child); found.has_value()) return found;
        }
        for (auto& [slot, children] : parent.slots) {
            (void)slot;
            for (auto& child : children) {
                if (auto found = visit(child); found.has_value()) return found;
            }
        }
        return std::nullopt;
    };
    return visit(document_.root);
}

bool DesignDocumentEditor::contains(const DesignNode& node,
                                    DesignNodeId id) const {
    return findNode(node, id) != nullptr;
}

void DesignDocumentEditor::collectIds(
    const DesignNode& node, std::map<DesignNodeId, bool>& ids) {
    ids[node.id] = true;
    for (const auto& child : node.children) collectIds(child, ids);
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) collectIds(child, ids);
    }
}

std::optional<DesignNodeId> DesignDocumentEditor::allocateId(
    std::map<DesignNodeId, bool>& used) {
    while (nextId_ != 0 && used.find(nextId_) != used.end()) {
        if (nextId_ == std::numeric_limits<DesignNodeId>::max()) {
            nextId_ = 0;
        } else {
            ++nextId_;
        }
    }
    if (nextId_ == 0) return std::nullopt;
    const DesignNodeId result = nextId_;
    used[result] = true;
    if (nextId_ == std::numeric_limits<DesignNodeId>::max()) {
        nextId_ = 0;
    } else {
        ++nextId_;
    }
    return result;
}

bool DesignDocumentEditor::normalizeIds(
    DesignNode& node, std::map<DesignNodeId, bool>& used) {
    if (node.id == 0 || used.find(node.id) != used.end()) {
        const auto fresh = allocateId(used);
        if (!fresh.has_value()) return false;
        node.id = *fresh;
    } else {
        used[node.id] = true;
        if (node.id >= nextId_ &&
            node.id != std::numeric_limits<DesignNodeId>::max()) {
            nextId_ = node.id + 1;
        }
    }
    for (auto& child : node.children) {
        if (!normalizeIds(child, used)) return false;
    }
    for (auto& [slot, children] : node.slots) {
        (void)slot;
        for (auto& child : children) {
            if (!normalizeIds(child, used)) return false;
        }
    }
    return true;
}

bool DesignDocumentEditor::insertRaw(DesignNodeId parentId,
                                     std::size_t index, DesignNode node,
                                     std::string_view slot,
                                     DesignNodeId* insertedId) {
    auto* parent = findNode(document_.root, parentId);
    if (parent == nullptr) return false;

    std::vector<DesignNode>* siblings = nullptr;
    if (slot.empty()) {
        siblings = &parent->children;
    } else {
        const auto found = parent->slots.find(std::string{slot});
        if (found == parent->slots.end()) {
            if (index != 0) return false;
            siblings = &parent->slots[std::string{slot}];
        } else {
            siblings = &found->second;
        }
    }
    if (index > siblings->size()) return false;

    std::map<DesignNodeId, bool> used;
    collectIds(document_.root, used);
    std::map<DesignNodeId, bool> incoming;
    collectIds(node, incoming);
    for (const auto& [id, present] : incoming) {
        (void)present;
        if (id == 0 || used.find(id) != used.end()) return false;
    }
    const DesignNodeId newId = node.id;
    siblings->insert(siblings->begin() + static_cast<std::ptrdiff_t>(index),
                     std::move(node));
    if (insertedId != nullptr) *insertedId = newId;
    return true;
}

bool DesignDocumentEditor::insertChild(DesignNodeId parentId,
                                       std::size_t index, DesignNode node,
                                       DesignNodeId* insertedId,
                                       std::string slot) {
    std::map<DesignNodeId, bool> used;
    collectIds(document_.root, used);
    if (!normalizeIds(node, used)) return false;
    return insertRaw(parentId, index, std::move(node), slot, insertedId);
}

std::optional<DesignNode> DesignDocumentEditor::removeNode(DesignNodeId id) {
    auto location = locateNode(id);
    if (!location.has_value()) return std::nullopt;
    DesignNode removed = std::move((*location->siblings)[location->index]);
    location->siblings->erase(location->siblings->begin() +
                              static_cast<std::ptrdiff_t>(location->index));
    return removed;
}

bool DesignDocumentEditor::moveNode(DesignNodeId id, DesignNodeId newParentId,
                                    std::size_t index, std::string slot) {
    if (id == document_.root.id) return false;
    auto* source = findNode(document_.root, id);
    if (source == nullptr || id == newParentId ||
        contains(*source, newParentId)) {
        return false;
    }
    const auto location = locateNode(id);
    if (!location.has_value()) return false;
    const DesignNodeId oldParentId = location->parent->id;
    const std::size_t oldIndex = location->index;
    const std::string oldSlot = location->slot;
    DesignNode moved = std::move((*location->siblings)[location->index]);
    location->siblings->erase(location->siblings->begin() +
                              static_cast<std::ptrdiff_t>(location->index));
    DesignNode restore = moved;
    if (insertRaw(newParentId, index, std::move(moved), slot, nullptr)) {
        return true;
    }

    // Restore the source when the destination was invalid. The original
    // parent cannot be inside the moved subtree, so its identity remains.
    return insertRaw(oldParentId, oldIndex, std::move(restore), oldSlot,
                     nullptr);
}

std::optional<DesignNodeId> DesignDocumentEditor::duplicateNode(
    DesignNodeId id, DesignNodeId newParentId, std::size_t index,
    std::string slot) {
    const auto* source = findNode(document_.root, id);
    if (source == nullptr) return std::nullopt;
    DesignNode copy = *source;
    std::map<DesignNodeId, bool> used;
    collectIds(document_.root, used);
    if (!normalizeIds(copy, used)) return std::nullopt;
    const DesignNodeId insertedId = copy.id;
    if (!insertRaw(newParentId, index, std::move(copy), slot, nullptr)) {
        return std::nullopt;
    }
    return insertedId;
}

}  // namespace lumen::dsl
