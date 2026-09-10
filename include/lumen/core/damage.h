#pragma once

#include <optional>
#include <vector>

#include "lumen/core/render_node.h"

namespace lumen::core {

// Dirty-rect computation between two render trees (plan 阶段6: 脏矩形).
//
// Children are aligned by their layout-assigned identity path (keyed nodes
// carry their key, keyless nodes a positional path), mirroring how Element
// reconciliation reuses subtrees. A node contributes damage when it appears
// or disappears (whole subtree bounds), when its absolute rect changed (the
// old AND the new area), or when any non-child field changed (its rect is
// sufficient because children are unchanged). Otherwise the walk descends
// so a deep edit damages only that subtree.
//
// Returns false when the roots themselves cannot be aligned; callers must
// then repaint the full frame.
[[nodiscard]] bool collectDamage(const RenderNode& previous,
                                 const RenderNode& current,
                                 std::vector<Rect>& damage);

// Union of `rects` intersected with the viewport; nullopt when empty.
[[nodiscard]] std::optional<Rect> damageBounds(const std::vector<Rect>& rects,
                                               Size viewport);

}  // namespace lumen::core
