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

}  // namespace lumen::core
