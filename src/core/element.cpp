#include "lumen/core/element.h"

#include <utility>

namespace lumen::core {

Element::Element(Widget widget, Element* parent)
    : widget_(std::move(widget)), parent_(parent) {
    inflateChildren();
}

void Element::update(Widget next) {
    if (!canReuse(widget_, next)) {
        widget_ = std::move(next);
        children_.clear();
        inflateChildren();
        markDirty();
        return;
    }
    widget_ = std::move(next);
    reconcileChildren(widget_.children);
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

void Element::inflateChildren() {
    children_.reserve(widget_.children.size());
    for (const auto& child : widget_.children) {
        children_.push_back(std::make_unique<Element>(child, this));
    }
}

void Element::reconcileChildren(const std::vector<Widget>& nextChildren) {
    std::vector<std::unique_ptr<Element>> merged;
    merged.reserve(nextChildren.size());
    std::vector<bool> used(children_.size(), false);

    for (std::size_t nextIndex = 0; nextIndex < nextChildren.size();
         ++nextIndex) {
        const auto& next = nextChildren[nextIndex];
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
            owned->update(next);
            merged.push_back(std::move(owned));
        } else {
            merged.push_back(std::make_unique<Element>(next, this));
        }
    }
    children_ = std::move(merged);
}

}  // namespace lumen::core
