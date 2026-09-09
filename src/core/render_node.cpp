#include "lumen/core/render_node.h"

namespace lumen::core {

const RenderNode* findNodeByKey(const RenderNode& root,
                                const std::string& key) {
    if (root.key == key) {
        return &root;
    }
    for (const auto& child : root.children) {
        if (const auto* found = findNodeByKey(child, key)) {
            return found;
        }
    }
    return nullptr;
}

namespace {

bool absoluteOffsetOf(const RenderNode& node, const std::string& key,
                      Offset accumulated, Offset& result) {
    const Offset origin = accumulated + node.offset;
    if (node.key == key) {
        result = origin;
        return true;
    }
    for (const auto& child : node.children) {
        if (absoluteOffsetOf(child, key, origin, result)) {
            return true;
        }
    }
    return false;
}

}  // namespace

Offset absoluteOffset(const RenderNode& root, const std::string& key) {
    Offset result{};
    absoluteOffsetOf(root, key, Offset{}, result);
    return result;
}

}  // namespace lumen::core
