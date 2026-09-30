#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace lumen::dsl {

enum class DesignReferenceKind {
    Binding,
    Handler,
    Theme,
    Image,
    VirtualSource,
    SplitterSource,
    Component,
};

struct DesignReference {
    DesignReferenceKind kind{DesignReferenceKind::Binding};
    std::string stableName{};

    bool operator==(const DesignReference&) const = default;
};

// P3 resolves names to typed, opaque handles. The handle deliberately carries
// no raw pointer; adapters that own a controller or resource keep it alive for
// the preview session and expose only its stable name here.
class DesignRuntimeContext {
  public:
    virtual ~DesignRuntimeContext() = default;

    // P1 contexts are offline and permissive. A preview/application context
    // overrides this to validate every reference against its registry.
    [[nodiscard]] virtual bool validatesReferences() const { return false; }

    [[nodiscard]] virtual bool resolveReference(DesignReferenceKind kind,
                                                const std::string& name,
                                                DesignReference& out) const {
        (void)kind;
        (void)name;
        (void)out;
        return false;
    }
};

class MapDesignRuntimeContext final : public DesignRuntimeContext {
  public:
    [[nodiscard]] bool validatesReferences() const override { return true; }

    void registerReference(DesignReferenceKind kind, std::string name) {
        const ReferenceKey key{kind, name};
        references_[key] = DesignReference{kind, std::move(name)};
    }

    [[nodiscard]] bool resolveReference(DesignReferenceKind kind,
                                        const std::string& name,
                                        DesignReference& out) const override {
        const auto found = references_.find({kind, name});
        if (found == references_.end()) return false;
        out = found->second;
        return true;
    }

  private:
    struct ReferenceKey {
        DesignReferenceKind kind{};
        std::string name{};

        bool operator<(const ReferenceKey& other) const {
            if (kind != other.kind) return kind < other.kind;
            return name < other.name;
        }
    };

    std::map<ReferenceKey, DesignReference> references_{};
};

class DesignRuntimeSession {
  public:
    DesignRuntimeSession()
        : generation_(nextGeneration().fetch_add(1, std::memory_order_relaxed) +
                      1) {}

    [[nodiscard]] std::uint64_t generation() const { return generation_; }
    [[nodiscard]] bool active() const { return active_; }
    void close() { active_ = false; }

  private:
    static std::atomic<std::uint64_t>& nextGeneration() {
        static std::atomic<std::uint64_t> value{0};
        return value;
    }

    std::uint64_t generation_{0};
    bool active_{true};
};

}  // namespace lumen::dsl
