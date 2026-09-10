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
    // v0.3 阶段8B TextField 属性（painter/interaction 消费）。
    bool obscure{false};
    bool readOnly{false};
    bool multiline{false};
    // v0.3 阶段8C 语义覆盖（见 Widget 同名字段）。
    std::string semanticsLabel{};
    std::string semanticsValue{};
    std::string semanticsRole{};
    std::uint32_t semanticsActions{0};

    // v0.3 阶段8D：滚动视口与选择状态。
    bool clipContent{false};     // ScrollView/ListView 视口裁剪
    float scrollOffset{0.0F};    // 当前滚动偏移（已应用到子 offset）
    float scrollExtent{0.0F};    // 可滚动的最大范围（内容-视口）
    bool checked{false};         // Checkbox/Switch 状态

    std::vector<RenderNode> children{};

    // Field-wise equality (all fields including children); damage tracking
    // compares single nodes without children via sameNode() in damage.cpp.
    [[nodiscard]] bool operator==(const RenderNode& other) const = default;

    [[nodiscard]] Rect rect() const { return Rect{offset, size}; }
};

// True when every field except `children` matches; sibling of operator==
// used to decide whether a subtree needs repainting.
[[nodiscard]] bool sameNode(const RenderNode& a, const RenderNode& b);

// Depth-first lookup by identity path (layout assigns these); the preferred
// way to relocate a node across rebuilds for damage and gesture tracking.
[[nodiscard]] const RenderNode* findNodeByIdentity(const RenderNode& root,
                                                   const std::string& identity);

// Depth-first lookup by key; returns nullptr when absent.
[[nodiscard]] const RenderNode* findNodeByKey(const RenderNode& root,
                                              const std::string& key);

// Absolute (root-relative) origin of the node with `key`; {0,0} when absent.
[[nodiscard]] Offset absoluteOffset(const RenderNode& root,
                                    const std::string& key);

}  // namespace lumen::core
