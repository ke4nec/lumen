#pragma once

#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/core/widget.h"

namespace lumen::core {

// Layout + paint input produced from an Element/Widget tree.
// `offset` is the border-box origin relative to the parent content box
// (parent border-box origin plus parent padding); `size` is the border-box
// size (includes padding, excludes margin).
struct RenderNode {
    WidgetType type{WidgetType::Container};
    std::string key{};
    Offset offset{};
    Size size{};
    std::vector<RenderNode> children{};

    [[nodiscard]] Rect rect() const { return Rect{offset, size}; }
};

// Depth-first lookup by key; returns nullptr when absent.
[[nodiscard]] const RenderNode* findNodeByKey(const RenderNode& root,
                                              const std::string& key);

}  // namespace lumen::core
