#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lumen/core/widget.h"
#include "lumen/dsl/design_document.h"
#include "lumen/dsl/design_mapping.h"
#include "lumen/dsl/runtime_context.h"

namespace lumen::dsl {

struct DesignError {
    std::string code{};
    std::string file{};
    SourcePos pos{};
    std::string message{};
    std::string expected{};
    std::string found{};
    DesignNodeId nodeId{0};
    std::string nodePath{};
    std::string property{};

    [[nodiscard]] std::string format() const;
};

struct DesignParseResult {
    DesignDocument document{};
    std::optional<DesignError> error{};

    [[nodiscard]] bool ok() const { return !error.has_value(); }
};

struct DesignReadResult {
    DesignDocument document{};
    std::optional<DesignError> error{};

    [[nodiscard]] bool ok() const { return !error.has_value(); }
};

struct CompiledNodeRef {
    DesignNodeId documentId{0};
    std::string runtimeIdentity{};
    std::string key{};
    bool runtimeOnly{false};

    bool operator==(const CompiledNodeRef&) const = default;
};

struct CompileTrace {
    std::map<DesignNodeId, CompiledNodeRef> nodes{};

    bool operator==(const CompileTrace&) const = default;
};

struct DesignCompileResult {
    core::Widget root{};
    CompileTrace trace{};
    DesignSourceMap sourceMap{};
    std::vector<DesignError> diagnostics{};
    std::shared_ptr<DesignRuntimeSession> session{};

    [[nodiscard]] bool ok() const { return diagnostics.empty(); }
};

[[nodiscard]] DesignParseResult parseLumenSource(
    const std::string& source, std::string filename = "<memory>");

[[nodiscard]] DesignReadResult readDesignDocument(
    const std::string& source, std::string filename = "<memory>");

[[nodiscard]] std::string serializeDesignDocument(
    const DesignDocument& document);

// Validates one raw JSON value stored in an unknown-field extension.
// Extension values must remain structured JSON so serialization cannot
// silently change their type to a string.
[[nodiscard]] bool isValidDesignJsonValue(std::string_view source);

[[nodiscard]] DesignCompileResult compileDesignDocument(
    const DesignDocument& document, DesignRuntimeContext& context);

// Convenience overload for headless P1 callers. It uses an empty context and
// does not enable any runtime reference or application side effects.
[[nodiscard]] DesignCompileResult compileDesignDocument(
    const DesignDocument& document);

}  // namespace lumen::dsl
