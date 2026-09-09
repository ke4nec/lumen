#pragma once

#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/core/widget.h"

namespace lumen::core {

// Layout + paint + interaction input produced from an Element/Widget tree
// (plan §4.1: layout result, paint properties, hit area, child order).
// `offset` is the border-box origin relative to the parent border-box origin;
// `size` is the border-box size (includes padding, excludes margin).
struct RenderNode {
    WidgetType type{WidgetType::Container};
    std::string key{};
    // Stable path identity for keyless nodes; keyed nodes include their key.
    std::string identity{};

    Offset offset{};
    Size size{};
    EdgeInsets padding{};

    // Paint properties and stage-3 semantics copied from the Widget so the
    // painter and interaction controller only walk this tree.
    Color color{Color::transparent()};
    CornerRadius radius{CornerRadius::zero()};
    std::string text{};
    TextStyle textStyle{};
    std::string placeholder{};
    std::string bind{};
    std::string onClick{};

    std::vector<RenderNode> children{};

    [[nodiscard]] Rect rect() const { return Rect{offset, size}; }
};

// Depth-first lookup by key; returns nullptr when absent.
[[nodiscard]] const RenderNode* findNodeByKey(const RenderNode& root,
                                              const std::string& key);

// Absolute (root-relative) origin of the node with `key`; {0,0} when absent.
[[nodiscard]] Offset absoluteOffset(const RenderNode& root,
                                    const std::string& key);

}  // namespace lumen::core
