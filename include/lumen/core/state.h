#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "lumen/core/widget.h"

namespace lumen::core {

// Single-direction state flow (plan §7): the app registers keys, widgets bind
// them by name, mutations notify subscribers and the owning app rebuilds the
// affected widgets. Values are strings; richer types are serialized by the
// application.
class StateStore {
  public:
    using ObserverId = std::uint64_t;

    // Notifies subscribers only when the stored value actually changes.
    void set(std::string key, std::string value);
    [[nodiscard]] bool has(const std::string& key) const;
    // Returns "" for absent keys so bind resolution can fall back to defaults.
    [[nodiscard]] const std::string& get(const std::string& key) const;
    [[nodiscard]] std::string getOr(const std::string& key,
                                    std::string fallback) const;

    // Subscriptions fire in registration order. Unsubscribing a destroyed
    // observer (or before the store dies) is required and honored.
    ObserverId subscribe(std::string key, std::function<void()> observer);
    void unsubscribe(ObserverId id);
    [[nodiscard]] std::size_t observerCount() const;

  private:
    std::map<std::string, std::string> values_{};
    struct Subscription {
        std::string key{};
        std::function<void()> notify{};
    };
    std::map<ObserverId, Subscription> subscriptions_{};
    ObserverId nextId_{1};
};

using HandlerFn = std::function<void()>;
// Event name -> callback, registered by the application (plan §7 step 4).
using HandlerRegistry = std::map<std::string, HandlerFn>;

// Collects every non-empty `bind` key in the tree so the app can subscribe to
// exactly the state the UI reads.
[[nodiscard]] std::set<std::string> collectBindKeys(const Widget& root);

// Resolves `bind` references against the store. Idempotent: Text content is
// rebuilt from `bindPrefix` + value, TextField text becomes the stored value.
void applyBinds(Widget& root, const StateStore& store);

}  // namespace lumen::core
