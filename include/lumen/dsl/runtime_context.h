#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    std::shared_ptr<const void> lifetimeToken{};

    bool operator==(const DesignReference& other) const {
        return kind == other.kind && stableName == other.stableName;
    }
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

    void registerReference(DesignReferenceKind kind, std::string name,
                           std::shared_ptr<const void> lifetimeToken = {}) {
        const ReferenceKey key{kind, name};
        references_[key] =
            DesignReference{kind, std::move(name), std::move(lifetimeToken)};
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
    using CloseCallback = std::function<void()>;

    DesignRuntimeSession()
        : generation_(nextGeneration().fetch_add(1, std::memory_order_relaxed) +
                      1) {}

    ~DesignRuntimeSession() noexcept { closeImpl(true); }

    DesignRuntimeSession(const DesignRuntimeSession&) = delete;
    DesignRuntimeSession& operator=(const DesignRuntimeSession&) = delete;
    DesignRuntimeSession(DesignRuntimeSession&&) = delete;
    DesignRuntimeSession& operator=(DesignRuntimeSession&&) = delete;

    [[nodiscard]] std::uint64_t generation() const { return generation_; }
    [[nodiscard]] bool active() const { return active_; }

    // A resolved reference can keep an application-owned source or resource
    // alive for exactly the preview session that consumed it.
    void retain(std::shared_ptr<const void> lifetimeToken) {
        if (active_ && lifetimeToken) {
            leases_.push_back(std::move(lifetimeToken));
        }
    }

    [[nodiscard]] std::size_t leaseCount() const { return leases_.size(); }

    // Preview resources register cancellation here. Closing a session is
    // idempotent and drains callbacks before close() returns.
    void onClose(CloseCallback callback) {
        if (!callback) return;
        if (!active_) {
            callback();
            return;
        }
        closeCallbacks_.push_back(std::move(callback));
    }

    void close() {
        closeImpl(false);
    }

  private:
    void closeImpl(bool suppressExceptions) {
        if (!active_) return;
        active_ = false;
        auto callbacks = std::move(closeCallbacks_);
        std::exception_ptr firstException;
        for (auto& callback : callbacks) {
            try {
                callback();
            } catch (...) {
                if (!firstException) firstException = std::current_exception();
            }
        }
        leases_.clear();
        if (!suppressExceptions && firstException) {
            std::rethrow_exception(firstException);
        }
    }

    static std::atomic<std::uint64_t>& nextGeneration() {
        static std::atomic<std::uint64_t> value{0};
        return value;
    }

    std::uint64_t generation_{0};
    bool active_{true};
    std::vector<CloseCallback> closeCallbacks_{};
    std::vector<std::shared_ptr<const void>> leases_{};
};

}  // namespace lumen::dsl
