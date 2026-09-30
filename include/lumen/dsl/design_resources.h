#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "lumen/dsl/design_editor.h"
#include "lumen/dsl/runtime_context.h"

namespace lumen::dsl {

enum class DesignResourceKind : std::uint8_t {
    Image,
    Font,
    Theme,
    Data,
    Component,
};

struct DesignResourceReference {
    DesignResourceKind kind{DesignResourceKind::Image};
    std::string scheme{};
    std::string relativePath{};

    bool operator==(const DesignResourceReference&) const = default;

    [[nodiscard]] std::string uri() const {
        return scheme + "://" + relativePath;
    }
};

// A policy is value data owned by the document session. It grants no access
// until both a scheme root and a resource kind have been explicitly enabled.
class DesignResourcePolicy {
  public:
    void allowRoot(std::string scheme, std::filesystem::path root);
    void allowKind(DesignResourceKind kind) { allowedKinds_.insert(kind); }

    [[nodiscard]] bool allowsKind(DesignResourceKind kind) const {
        return allowedKinds_.contains(kind);
    }
    [[nodiscard]] const std::map<std::string, std::filesystem::path>& roots()
        const {
        return roots_;
    }

  private:
    std::map<std::string, std::filesystem::path> roots_{};
    std::set<DesignResourceKind> allowedKinds_{};
};

struct DesignResourceDiagnosticContext {
    std::string file{};
    std::string documentId{};
    DesignNodeId nodeId{0};
    std::string nodePath{};
    std::string property{};
};

class DesignResourceAuthorizer {
  public:
    explicit DesignResourceAuthorizer(DesignResourcePolicy policy)
        : policy_(std::move(policy)) {}

    [[nodiscard]] std::optional<DesignResourceReference> authorize(
        DesignResourceKind kind, std::string_view uri,
        const DesignResourceDiagnosticContext& context,
        std::vector<DesignDiagnostic>& diagnostics) const;

  private:
    DesignResourcePolicy policy_{};
};

struct DesignPreviewToken {
    std::string documentId{};
    std::uint64_t sessionGeneration{0};
    std::uint64_t compileGeneration{0};

    bool operator==(const DesignPreviewToken&) const = default;
};

// Generation checks make late image/font/controller completions harmless.
// The caller also supplies the session at acceptance time so close() is
// observed even when an async worker finishes after the preview window closes.
class DesignPreviewGeneration {
  public:
    DesignPreviewGeneration(std::string documentId,
                            std::uint64_t sessionGeneration)
        : documentId_(std::move(documentId)),
          sessionGeneration_(sessionGeneration) {}

    [[nodiscard]] DesignPreviewToken beginCompile();
    [[nodiscard]] bool accepts(const DesignPreviewToken& token) const;
    [[nodiscard]] bool accepts(const DesignPreviewToken& token,
                               const DesignRuntimeSession& session) const;
    void invalidate() { ++compileGeneration_; }
    void close() {
        if (!active_) return;
        active_ = false;
        ++compileGeneration_;
    }

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] std::uint64_t compileGeneration() const {
        return compileGeneration_;
    }

  private:
    std::string documentId_{};
    std::uint64_t sessionGeneration_{0};
    std::uint64_t compileGeneration_{0};
    bool active_{true};
};

[[nodiscard]] const char* designResourceKindName(DesignResourceKind kind);

}  // namespace lumen::dsl
