#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lumen/core/widget.h"

namespace lumen::dsl {

struct DesignNode;
class DesignRuntimeSession;

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
    const void* handle{};

    bool operator==(const DesignReference& other) const {
        return kind == other.kind && stableName == other.stableName;
    }
};

// A widgets-layer composition is built by the application adapter. The DSL
// only carries the node and an opaque controller handle; it never includes a
// concrete ComboBox/Spin/Dialog controller header.
struct DesignComponentContext {
    const void* controller{nullptr};
    DesignRuntimeSession* session{nullptr};
};

struct DesignComponentResult {
    std::optional<core::Widget> widget{};
    std::shared_ptr<const void> lifetimeToken{};
    std::string diagnosticCode{};
    std::string diagnosticMessage{};

    [[nodiscard]] bool ok() const { return widget.has_value(); }
};

using DesignComponentBuilder = std::function<DesignComponentResult(
    const DesignNode&, const DesignComponentContext&)>;

// P3 resolves names to typed, opaque handles. Runtime compilation may copy the
// handle into a Widget source field, while the adapter's lease keeps that
// application-owned object alive for the preview session.
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

    [[nodiscard]] virtual DesignComponentResult buildComponent(
        const DesignNode& node, const DesignComponentContext& componentContext) const {
        (void)node;
        (void)componentContext;
        return DesignComponentResult{
            std::nullopt, {}, "component.missing",
            "no component builder is registered for this node type"};
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

    void registerTypedReference(DesignReferenceKind kind, std::string name,
                                const void* handle,
                                std::shared_ptr<const void> lifetimeToken = {}) {
        const ReferenceKey key{kind, name};
        references_[key] = DesignReference{kind, std::move(name),
                                           std::move(lifetimeToken), handle};
    }

    [[nodiscard]] bool resolveReference(DesignReferenceKind kind,
                                        const std::string& name,
                                        DesignReference& out) const override {
        const auto found = references_.find({kind, name});
        if (found == references_.end()) return false;
        out = found->second;
        return true;
    }

    void registerComponentBuilder(std::string type,
                                   DesignComponentBuilder builder) {
        if (builder) {
            componentBuilders_[std::move(type)] = std::move(builder);
        } else {
            componentBuilders_.erase(type);
        }
    }

    [[nodiscard]] DesignComponentResult buildComponent(
        const DesignNode& node,
        const DesignComponentContext& componentContext) const override;

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
    std::map<std::string, DesignComponentBuilder> componentBuilders_{};
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
