#include "lumen/core/state.h"

#include <utility>
#include <vector>

namespace lumen::core {

void StateStore::set(std::string key, std::string value) {
    auto it = values_.find(key);
    if (it != values_.end() && it->second == value) {
        return;
    }
    if (it != values_.end()) {
        it->second = std::move(value);
    } else {
        values_.emplace(key, std::move(value));
    }
    // Snapshot matching IDs before invoking callbacks. A callback may remove
    // any subscription or add a new one; additions start on the next change.
    std::vector<ObserverId> matching;
    matching.reserve(subscriptions_.size());
    for (const auto& [id, subscription] : subscriptions_) {
        if (subscription.key == key) {
            matching.push_back(id);
        }
    }
    for (const ObserverId id : matching) {
        const auto current = subscriptions_.find(id);
        if (current == subscriptions_.end()) {
            continue;
        }
        std::function<void()> notify = current->second.notify;
        notify();
    }
}

bool StateStore::has(const std::string& key) const {
    return values_.find(key) != values_.end();
}

const std::string& StateStore::get(const std::string& key) const {
    static const std::string kEmpty{};
    const auto it = values_.find(key);
    return it != values_.end() ? it->second : kEmpty;
}

std::string StateStore::getOr(const std::string& key, std::string fallback) const {
    const auto it = values_.find(key);
    return it != values_.end() ? it->second : fallback;
}

StateStore::ObserverId StateStore::subscribe(
    std::string key, std::function<void()> observer) {
    const ObserverId id = nextId_++;
    subscriptions_.emplace(id, Subscription{std::move(key), std::move(observer)});
    return id;
}

void StateStore::unsubscribe(ObserverId id) {
    subscriptions_.erase(id);
}

std::size_t StateStore::observerCount() const {
    return subscriptions_.size();
}

std::set<std::string> collectBindKeys(const Widget& root) {
    std::set<std::string> keys;
    std::function<void(const Widget&)> walk = [&](const Widget& node) {
        if (!node.bind.empty()) {
            keys.insert(node.bind);
        }
        for (const auto& child : node.children) {
            walk(child);
        }
    };
    walk(root);
    return keys;
}

void applyBinds(Widget& root, const StateStore& store) {
    if (!root.bind.empty()) {
        const std::string& value = store.get(root.bind);
        if (root.type == WidgetType::TextField) {
            root.text = value;
        } else if (root.type == WidgetType::Text) {
            root.text = root.bindPrefix + value;
        } else if (root.type == WidgetType::Checkbox ||
                   root.type == WidgetType::Switch) {
            // v0.3 阶段8D：选中状态绑定（宽容解析；框架写回 "true"/"false"）。
            root.checked = value == "true" || value == "1" || value == "on";
        }
    }
    for (auto& child : root.children) {
        applyBinds(child, store);
    }
}

}  // namespace lumen::core
