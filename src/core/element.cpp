#include "lumen/core/element.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lumen::core {
namespace {

// Keyed sibling 查找键（type + key；canReuse 的等价口径）。
struct KeyedSiblingKey {
    WidgetType type{WidgetType::Container};
    std::string key{};
    bool operator==(const KeyedSiblingKey& other) const {
        return type == other.type && key == other.key;
    }
};
struct KeyedSiblingKeyHash {
    std::size_t operator()(const KeyedSiblingKey& lookup) const noexcept {
        std::size_t hash = std::hash<std::string>{}(lookup.key);
        return hash * 31u +
               static_cast<std::size_t>(static_cast<int>(lookup.type));
    }
};
}  // namespace

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
    bool sameOrder = children_.size() == nextChildren.size();
    for (std::size_t i = 0; sameOrder && i < children_.size(); ++i) {
        sameOrder = canReuse(children_[i]->widget(), nextChildren[i]);
    }
    if (sameOrder) {
        for (std::size_t i = 0; i < children_.size(); ++i) {
            children_[i]->update(std::move(nextChildren[i]));
        }
        return;
    }

    std::vector<std::unique_ptr<Element>> merged;
    merged.reserve(nextChildren.size());
    std::vector<bool> used(children_.size(), false);

    // Keyed sibling 索引：线性搜索是 O(n²)，大 keyed 同级列表（如设计器
    // 画布参考线每帧物化 1000+ keyed 装饰节点）每帧可烧掉数十毫秒。n 超
    // 阈值时建 (type,key)→旧下标队列，把搜索降到 O(n)；小列表保持扫描
    //（局部性更好，无哈希开销）。队列按下标升序消费，与扫描"取最低未用
    // 下标"逐项等价（含重复 key 的状态归属不变）；无 key 节点仍走位置口径。
    constexpr std::size_t kKeyedIndexThreshold = 32;
    struct KeyedQueue {
        std::vector<std::size_t> indices;
        std::size_t head{0};
    };
    std::unordered_map<KeyedSiblingKey, KeyedQueue, KeyedSiblingKeyHash>
        keyedIndex;
    const bool useKeyedIndex =
        children_.size() > kKeyedIndexThreshold ||
        nextChildren.size() > kKeyedIndexThreshold;
    if (useKeyedIndex) {
        keyedIndex.reserve(children_.size() * 2 + 1);
        for (std::size_t i = 0; i < children_.size(); ++i) {
            const Widget& old = children_[i]->widget();
            if (old.key.empty()) continue;
            KeyedSiblingKey lookup{old.type, old.key};
            keyedIndex[std::move(lookup)].indices.push_back(i);
        }
    }

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
        } else if (useKeyedIndex) {
            // 与线性扫描等价：取该 (type,key) 下最低未用旧下标。
            KeyedSiblingKey lookup{next.type, next.key};
            const auto found = keyedIndex.find(lookup);
            if (found != keyedIndex.end()) {
                KeyedQueue& queue = found->second;
                while (queue.head < queue.indices.size() &&
                       used[queue.indices[queue.head]]) {
                    ++queue.head;
                }
                if (queue.head < queue.indices.size()) {
                    reusedIndex = queue.indices[queue.head++];
                    reused = children_[reusedIndex].get();
                }
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
