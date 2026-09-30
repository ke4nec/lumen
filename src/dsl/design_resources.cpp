#include "lumen/dsl/design_resources.h"

#include <cctype>
#include <system_error>
#include <utility>

namespace lumen::dsl {
namespace {

[[nodiscard]] bool isWithin(const std::filesystem::path& root,
                            const std::filesystem::path& candidate) {
    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != root.end() && candidateIt != candidate.end();
         ++rootIt, ++candidateIt) {
        if (*rootIt != *candidateIt) return false;
    }
    return rootIt == root.end();
}

[[nodiscard]] bool hasParentSegment(const std::filesystem::path& path) {
    for (const auto& part : path) {
        if (part == "..") return true;
    }
    return false;
}

[[nodiscard]] std::string lowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    }
    return value;
}

void resourceError(const char* code, const char* message,
                   const DesignResourceDiagnosticContext& context,
                   std::vector<DesignDiagnostic>& diagnostics) {
    DesignError error{code,
                      context.file,
                      {},
                      message,
                      {},
                      {},
                      context.nodeId,
                      context.nodePath,
                      context.property};
    auto diagnostic = DesignDiagnostic::fromError(
        error, DesignDiagnosticStage::Reference);
    diagnostic.documentId = context.documentId;
    diagnostic.recoverability = DesignDiagnosticRecoverability::Placeholder;
    appendDesignDiagnostic(diagnostics, std::move(diagnostic));
}

}  // namespace

void DesignResourcePolicy::allowRoot(std::string scheme,
                                     std::filesystem::path root) {
    scheme = lowerAscii(std::move(scheme));
    if (scheme.empty() || root.empty()) return;
    roots_[std::move(scheme)] = std::move(root);
}

std::optional<DesignResourceReference> DesignResourceAuthorizer::authorize(
    DesignResourceKind kind, std::string_view uri,
    const DesignResourceDiagnosticContext& context,
    std::vector<DesignDiagnostic>& diagnostics) const {
    if (!policy_.allowsKind(kind)) {
        resourceError("resource.capability_denied",
                      "resource kind is not enabled for this preview",
                      context, diagnostics);
        return std::nullopt;
    }

    const auto separator = uri.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        resourceError("resource.invalid_uri", "resource URI has no scheme",
                      context, diagnostics);
        return std::nullopt;
    }
    const std::string scheme = lowerAscii(std::string{uri.substr(0, separator)});
    const auto rootFound = policy_.roots().find(scheme);
    if (rootFound == policy_.roots().end()) {
        resourceError("resource.scheme_denied",
                      "resource URI scheme is not authorized", context,
                      diagnostics);
        return std::nullopt;
    }

    const std::string pathText{uri.substr(separator + 3)};
    if (pathText.empty()) {
        resourceError("resource.invalid_uri", "resource URI path is empty",
                      context, diagnostics);
        return std::nullopt;
    }
    const std::filesystem::path requested(pathText);
    if (requested.is_absolute() || requested.has_root_name() ||
        requested.has_root_directory()) {
        resourceError("resource.path_escape",
                      "resource URI must stay below its authorized root",
                      context, diagnostics);
        return std::nullopt;
    }
    const auto normalized = requested.lexically_normal();
    if (hasParentSegment(normalized)) {
        resourceError("resource.path_escape",
                      "resource URI must stay below its authorized root",
                      context, diagnostics);
        return std::nullopt;
    }

    std::error_code error;
    const auto canonicalRoot =
        std::filesystem::weakly_canonical(rootFound->second, error);
    if (error || !std::filesystem::is_directory(canonicalRoot, error) ||
        error) {
        resourceError("resource.root_unavailable",
                      "resource authorization root is unavailable", context,
                      diagnostics);
        return std::nullopt;
    }
    error.clear();
    const auto candidate = std::filesystem::weakly_canonical(
        canonicalRoot / normalized, error);
    if (error || !isWithin(canonicalRoot, candidate)) {
        resourceError("resource.path_escape",
                      "resource URI resolves outside its authorized root",
                      context, diagnostics);
        return std::nullopt;
    }
    const auto stablePath = candidate.lexically_relative(canonicalRoot);
    if (stablePath.empty() || hasParentSegment(stablePath)) {
        resourceError("resource.path_escape",
                      "resource URI resolves outside its authorized root",
                      context, diagnostics);
        return std::nullopt;
    }
    return DesignResourceReference{kind, scheme, stablePath.generic_string()};
}

DesignPreviewToken DesignPreviewGeneration::beginCompile() {
    if (active_) ++compileGeneration_;
    return DesignPreviewToken{documentId_, sessionGeneration_,
                              compileGeneration_};
}

bool DesignPreviewGeneration::accepts(const DesignPreviewToken& token) const {
    return active_ && token.documentId == documentId_ &&
           token.sessionGeneration == sessionGeneration_ &&
           token.compileGeneration != 0 &&
           token.compileGeneration == compileGeneration_;
}

bool DesignPreviewGeneration::accepts(
    const DesignPreviewToken& token, const DesignRuntimeSession& session) const {
    return accepts(token) && session.active() &&
           session.generation() == sessionGeneration_;
}

const char* designResourceKindName(DesignResourceKind kind) {
    switch (kind) {
        case DesignResourceKind::Image: return "image";
        case DesignResourceKind::Font: return "font";
        case DesignResourceKind::Theme: return "theme";
        case DesignResourceKind::Data: return "data";
        case DesignResourceKind::Component: return "component";
    }
    return "resource";
}

}  // namespace lumen::dsl
