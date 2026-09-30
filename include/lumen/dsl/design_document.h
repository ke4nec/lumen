#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/dsl/text_dsl.h"

namespace lumen::dsl {

using DesignNodeId = std::uint64_t;

struct DesignSourceSpan {
    SourcePos begin{};
    SourcePos end{};

    bool operator==(const DesignSourceSpan&) const = default;
};

struct DesignEnum {
    std::string domain{};
    std::string value{};

    bool operator==(const DesignEnum&) const = default;
};

struct DesignValue {
    using Variant = std::variant<std::monostate, bool, double, std::string,
                                 core::Color, DesignEnum>;

    Variant value{};

    bool operator==(const DesignValue&) const = default;
};

struct DesignNode {
    DesignNodeId id{0};
    std::string type{};
    std::map<std::string, DesignValue> properties{};
    std::map<std::string, std::string> references{};
    std::vector<DesignNode> children{};
    std::map<std::string, std::vector<DesignNode>> slots{};
    std::map<std::string, std::string> unknownFields{};
    std::optional<DesignSourceSpan> source{};

    bool operator==(const DesignNode&) const = default;
};

struct DesignDocument {
    std::uint32_t schemaVersion{1};
    std::string documentId{};
    std::string pageName{};
    DesignNode root{};
    std::map<std::string, std::string> unknownFields{};

    bool operator==(const DesignDocument&) const = default;
};

}  // namespace lumen::dsl
