#include "lumen/core/element.h"

#include <utility>

namespace lumen::core {

Element::Element(Widget widget, Element* parent)
    : parent_(parent) {
    // M7：children 先移交（O(1) move），widget_ 存去子化快照。
    std::vector<Widget> children = std::move(widget.children);
    widget_ = std::move(widget);
    inflateFrom(std::move(children));
}

void Element::update(Widget next) {
    // M7：children 移交（O(1)）→ 去子化快照 → 子孙逐个 move
    //（reconcile 全程零 Widget 拷贝）。
    std::vector<Widget> nextChildren = std::move(next.children);
    if (!canReuse(widget_, next)) {
        widget_ = std::move(next);
        children_.clear();
        inflateFrom(std::move(nextChildren));
        markDirty();
        return;
    }
    widget_ = std::move(next);
    reconcileChildren(std::move(nextChildren));
    markDirty();
}

void Element::markDirty() {
    dirty_ = true;
}

void Element::clearDirty() {
    dirty_ = false;
}

void Element::clearDirtyTree() {
    dirty_ = false;
    for (auto& child : children_) {
        child->clearDirtyTree();
    }
}

bool Element::isDirty() const {
    return dirty_;
}

const Widget& Element::widget() const {
    return widget_;
}

Element* Element::parent() const {
    return parent_;
}

const std::vector<std::unique_ptr<Element>>& Element::children() const {
    return children_;
}

bool Element::canReuse(const Widget& current, const Widget& next) {
    return current.type == next.type && current.key == next.key;
}

void Element::inflateFrom(std::vector<Widget>&& nextChildren) {
    children_.reserve(nextChildren.size());
    for (auto& child : nextChildren) {
        children_.push_back(
            std::make_unique<Element>(std::move(child), this));
    }
}

void Element::reconcileChildren(std::vector<Widget>&& nextChildren) {
    std::vector<std::unique_ptr<Element>> merged;
    merged.reserve(nextChildren.size());
    std::vector<bool> used(children_.size(), false);

    for (std::size_t nextIndex = 0; nextIndex < nextChildren.size();
         ++nextIndex) {
        auto& next = nextChildren[nextIndex];
        Element* reused = nullptr;
        std::size_t reusedIndex = 0;
        if (next.key.empty()) {
            // Keyless widgets have positional identity. Never move their
            // runtime state across sibling positions during reconciliation.
            if (nextIndex < children_.size() && !used[nextIndex] &&
                canReuse(children_[nextIndex]->widget(), next)) {
                reused = children_[nextIndex].get();
                reusedIndex = nextIndex;
            }
        } else {
            // Keyed widgets may move, so search all unmatched old siblings.
            for (std::size_t i = 0; i < children_.size(); ++i) {
                if (used[i]) {
                    continue;
                }
                if (canReuse(children_[i]->widget(), next)) {
                    reused = children_[i].get();
                    reusedIndex = i;
                    break;
                }
            }
        }
        if (reused != nullptr) {
            used[reusedIndex] = true;
            std::unique_ptr<Element> owned = std::move(children_[reusedIndex]);
            owned->update(std::move(next));
            merged.push_back(std::move(owned));
        } else {
            merged.push_back(std::make_unique<Element>(std::move(next), this));
        }
    }
    children_ = std::move(merged);
}

}  // namespace lumen::core
