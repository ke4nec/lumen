#include "lumen/core/damage.h"

#include <algorithm>
#include <map>

namespace lumen::core {
namespace {

// All fields except children: geometry, resolved style and semantics.
// Children are handled by the recursive alignment walk, not by this
// comparison. `style` participates so state-only visual changes (hover/
// pressed/focused/checked) produce damage（visual-system §5 规则 6）.
bool sameNodeFields(const RenderNode& a, const RenderNode& b) {
    return a.type == b.type && a.key == b.key && a.identity == b.identity &&
           a.offset == b.offset && a.size == b.size && a.padding == b.padding &&
           a.style == b.style && a.text == b.text &&
           a.placeholder == b.placeholder && a.bind == b.bind &&
           a.onClick == b.onClick && a.obscure == b.obscure &&
           a.readOnly == b.readOnly && a.multiline == b.multiline &&
           a.semanticsLabel == b.semanticsLabel &&
           a.semanticsValue == b.semanticsValue &&
           a.semanticsRole == b.semanticsRole &&
           a.semanticsActions == b.semanticsActions &&
           a.clipContent == b.clipContent &&
           a.scrollOffset == b.scrollOffset &&
           a.scrollExtent == b.scrollExtent && a.scrollAxis == b.scrollAxis &&
           a.checked == b.checked &&
           a.imageId == b.imageId && a.imageSource == b.imageSource &&
           a.enabled == b.enabled && a.invalid == b.invalid &&
           a.selected == b.selected &&
           a.scrollbarColor == b.scrollbarColor &&
           a.scrollbarThumbWidth == b.scrollbarThumbWidth &&
           a.scrollbarMinLength == b.scrollbarMinLength &&
           a.scrollbarInset == b.scrollbarInset &&
           a.iconStrokeWidth == b.iconStrokeWidth && a.icon == b.icon &&
           a.transitionAlpha == b.transitionAlpha && a.elevation == b.elevation &&
           a.shadowColor == b.shadowColor && a.shadowOffset == b.shadowOffset &&
           a.shadowBlur == b.shadowBlur && a.showScrollbar == b.showScrollbar &&
           a.scrollbarThickness == b.scrollbarThickness &&
           a.progressIndeterminate == b.progressIndeterminate &&
           a.iconRotation == b.iconRotation &&
           a.clipRounded == b.clipRounded;
}

// Conservative bounds of a subtree: the node rect union every descendant, so
// overflowing children (long text, fixed sizes) are included in damage.
void addSubtreeBounds(const RenderNode& node, Offset absolute,
                      std::vector<Rect>& out) {
    out.push_back(Rect{absolute, node.size});
    if (node.elevation > 0 && node.shadowColor.a > 0) {
        // Skia uses sigma = blur / 2; cover its kernel and antialiased edge.
        const float outset = std::max(0.0F, node.shadowBlur) * 2.0F + 1.0F;
        out.push_back(Rect::fromXYWH(absolute.x + node.shadowOffset.x - outset,
            absolute.y + node.shadowOffset.y - outset,
            node.size.width + 2.0F * outset, node.size.height + 2.0F * outset));
    }
    for (const auto& child : node.children) {
        addSubtreeBounds(child, absolute + child.offset, out);
    }
}

// Matches `current` children against `previous` children by identity; nodes
// with empty identities fall back to positional matching. Returns false
// when alignment is not possible locally (caller damages the whole parent).
struct ChildMatch {
    const RenderNode* previous;
    std::size_t previousIndex;
};

bool alignChildren(const RenderNode& previous, const RenderNode& current,
                   std::vector<ChildMatch>& matches) {
    std::map<std::string, std::size_t> byIdentity;
    for (std::size_t i = 0; i < previous.children.size(); ++i) {
        if (!previous.children[i].identity.empty()) {
            byIdentity.emplace(previous.children[i].identity, i);
        }
    }
    std::vector<bool> used(previous.children.size(), false);
    for (std::size_t i = 0; i < current.children.size(); ++i) {
        const RenderNode& child = current.children[i];
        const RenderNode* match = nullptr;
        std::size_t matchIndex = 0;
        if (!child.identity.empty()) {
            const auto it = byIdentity.find(child.identity);
            if (it != byIdentity.end() && !used[it->second]) {
                match = &previous.children[it->second];
                matchIndex = it->second;
            }
        }
        if (match == nullptr && child.identity.empty() &&
            i < previous.children.size() && !used[i] &&
            previous.children[i].identity.empty() &&
            previous.children[i].type == child.type) {
            // Positional fallback mirrors keyless Element reconciliation.
            match = &previous.children[i];
            matchIndex = i;
        }
        if (match != nullptr) {
            used[matchIndex] = true;
        }
        matches.push_back(ChildMatch{match, matchIndex});
    }
    return true;
}

void collectNodeDamage(const RenderNode& previous, const RenderNode& current,
                       Offset previousAbsolute, Offset currentAbsolute,
                       std::vector<Rect>& damage) {
    const Offset previousOrigin = previousAbsolute + previous.offset;
    const Offset currentOrigin = currentAbsolute + current.offset;
    const Rect previousRect{previousOrigin, previous.size};
    const Rect currentRect{currentOrigin, current.size};

    // Type or geometry change: repaint where the node (and, conservatively,
    // its subtree — children may overflow the parent and get relayouted by
    // e.g. a padding change) was AND where it is now.
    if (previous.type != current.type || previousRect != currentRect) {
        addSubtreeBounds(previous, previousOrigin, damage);
        addSubtreeBounds(current, currentOrigin, damage);
        return;
    }
    // Same place, different paint/semantic fields: the subtree rect covers
    // it — field changes like padding also move children within it, and
    // children are walked below only when nothing changed on this node.
    if (!sameNodeFields(previous, current)) {
        const std::size_t start = damage.size();
        addSubtreeBounds(previous, previousOrigin, damage);
        addSubtreeBounds(current, currentOrigin, damage);
        float left = currentRect.left(), top = currentRect.top();
        float right = currentRect.right(), bottom = currentRect.bottom();
        for (std::size_t i = start; i < damage.size(); ++i) {
            left = std::min(left, damage[i].left());
            top = std::min(top, damage[i].top());
            right = std::max(right, damage[i].right());
            bottom = std::max(bottom, damage[i].bottom());
        }
        damage.resize(start);
        damage.push_back(Rect::fromXYWH(left, top, right - left, bottom - top));
        return;
    }

    std::vector<ChildMatch> matches;
    alignChildren(previous, current, matches);
    for (std::size_t i = 0; i < current.children.size(); ++i) {
        const ChildMatch& match = matches[i];
        const RenderNode& child = current.children[i];
        if (match.previous == nullptr) {
            addSubtreeBounds(child, currentOrigin + child.offset, damage);
            continue;
        }
        collectNodeDamage(*match.previous, child, currentOrigin,
                          currentOrigin, damage);
    }
    // Children present before but gone now damage their old subtree.
    std::vector<bool> matched(previous.children.size(), false);
    for (const ChildMatch& match : matches) {
        if (match.previous != nullptr) {
            matched[match.previousIndex] = true;
        }
    }
    for (std::size_t i = 0; i < previous.children.size(); ++i) {
        if (!matched[i]) {
            addSubtreeBounds(previous.children[i],
                             currentOrigin + previous.children[i].offset,
                             damage);
        }
    }
}

}  // namespace

bool sameNode(const RenderNode& a, const RenderNode& b) {
    return sameNodeFields(a, b);
}

void addPaintDamage(const RenderNode& node, Offset absoluteOrigin,
                    std::vector<Rect>& damage) {
    addSubtreeBounds(node, absoluteOrigin, damage);
}

const RenderNode* findNodeByIdentity(const RenderNode& root,
                                     const std::string& identity) {
    if (root.identity == identity) {
        return &root;
    }
    for (const auto& child : root.children) {
        if (const RenderNode* found = findNodeByIdentity(child, identity)) {
            return found;
        }
    }
    return nullptr;
}

bool collectDamage(const RenderNode& previous, const RenderNode& current,
                   std::vector<Rect>& damage) {
    if (previous.identity != current.identity) {
        return false;
    }
    collectNodeDamage(previous, current, Offset{}, Offset{}, damage);
    return true;
}

std::optional<Rect> damageBounds(const std::vector<Rect>& rects,
                                 Size viewport) {
    if (rects.empty()) {
        return std::nullopt;
    }
    float left = rects.front().left();
    float top = rects.front().top();
    float right = rects.front().right();
    float bottom = rects.front().bottom();
    for (const Rect& rect : rects) {
        left = std::min(left, rect.left());
        top = std::min(top, rect.top());
        right = std::max(right, rect.right());
        bottom = std::max(bottom, rect.bottom());
    }
    const Rect viewportRect{Offset{}, viewport};
    const float clampedLeft = std::max(left, viewportRect.left());
    const float clampedTop = std::max(top, viewportRect.top());
    const float clampedRight = std::min(right, viewportRect.right());
    const float clampedBottom = std::min(bottom, viewportRect.bottom());
    if (clampedLeft >= clampedRight || clampedTop >= clampedBottom) {
        return std::nullopt;
    }
    return Rect{Offset{clampedLeft, clampedTop},
                Size{clampedRight - clampedLeft, clampedBottom - clampedTop}};
}

}  // namespace lumen::core
