#include "lumen/dsl/design_preview.h"

#include <utility>

namespace lumen::dsl {

const DesignNode* DesignPreviewState::findNode(const DesignNode& node,
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

bool DesignPreviewState::setNodeValue(
    NodeValues& values, DesignNodeId id, std::string property,
    DesignValue value, const DesignDocument& document) {
    if (id == 0 || property.empty() || findNode(document.root, id) == nullptr) {
        return false;
    }
    values[id][std::move(property)] = std::move(value);
    return true;
}

bool DesignPreviewState::setRuntimeValue(DesignNodeId id, std::string property,
                                         DesignValue value,
                                         const DesignDocument& document) {
    return setNodeValue(runtimeValues_, id, std::move(property),
                        std::move(value), document);
}

bool DesignPreviewState::setVisualOverride(
    DesignNodeId id, std::string property, DesignValue value,
    const DesignDocument& document) {
    return setNodeValue(visualOverrides_, id, std::move(property),
                        std::move(value), document);
}

bool DesignPreviewState::setBindingSnapshot(std::string name,
                                            DesignValue value) {
    if (name.empty()) return false;
    bindingSnapshots_[std::move(name)] = std::move(value);
    return true;
}

std::optional<DesignValue> DesignPreviewState::value(
    DesignNodeId id, std::string_view property,
    const DesignDocument& document) const {
    if (findNode(document.root, id) == nullptr || property.empty()) {
        return std::nullopt;
    }
    const auto visualNode = visualOverrides_.find(id);
    if (visualNode != visualOverrides_.end()) {
        const auto visual = visualNode->second.find(std::string{property});
        if (visual != visualNode->second.end()) return visual->second;
    }
    const auto runtimeNode = runtimeValues_.find(id);
    if (runtimeNode != runtimeValues_.end()) {
        const auto runtime = runtimeNode->second.find(std::string{property});
        if (runtime != runtimeNode->second.end()) return runtime->second;
    }
    const auto* node = findNode(document.root, id);
    const auto declared = node->properties.find(std::string{property});
    if (declared == node->properties.end()) return std::nullopt;
    return declared->second;
}

std::optional<DesignValue> DesignPreviewState::bindingSnapshot(
    std::string_view name) const {
    const auto found = bindingSnapshots_.find(std::string{name});
    if (found == bindingSnapshots_.end()) return std::nullopt;
    return found->second;
}

}  // namespace lumen::dsl
