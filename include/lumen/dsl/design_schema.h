#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lumen/core/widget.h"
#include "lumen/dsl/design_codec.h"

namespace lumen::dsl {

enum class PropertyKind {
    Boolean,
    Number,
    String,
    Color,
    Enum,
    Object,
    Reference,
};

enum class PropertyPersistence {
    Declaration,
    RuntimeReference,
    PreviewOnly,
    Derived,
};

// Explicit inventory for Widget fields and their document-facing aliases.
// C++20 has no standard field reflection, so additions to Widget must update
// this list and the registry coverage test together.
enum class WidgetFieldCategory {
    Structural,
    Declaration,
    RuntimeReference,
    PreviewOnly,
    Derived,
};

struct WidgetFieldSpec {
    std::string name{};
    std::string schemaName{};
    WidgetFieldCategory category{WidgetFieldCategory::Structural};
};

using WidgetFieldInventory = std::vector<WidgetFieldSpec>;

struct PropertySpec {
    std::string name{};
    PropertyKind kind{PropertyKind::String};
    PropertyPersistence persistence{PropertyPersistence::Declaration};
    DesignValue defaultValue{};
    std::vector<std::string> enumValues{};
    std::function<bool(const DesignValue&)> validate{};
    std::function<DesignValue(const core::Widget&)> get{};
    std::function<bool(core::Widget&, const DesignValue&)> set{};
};

struct NodeSchema {
    std::string type{};
    bool canBeRoot{false};
    bool isComponent{false};
    std::size_t minChildren{0};
    std::optional<std::size_t> maxChildren{};
    std::vector<std::string> allowedParents{};
    std::vector<PropertySpec> properties{};
    std::function<core::Widget()> makeDefault{};
};

[[nodiscard]] const WidgetFieldInventory& widgetFieldInventory();
[[nodiscard]] const std::vector<NodeSchema>& nodeSchemaRegistry();

[[nodiscard]] const NodeSchema* findNodeSchema(std::string_view type);

[[nodiscard]] const PropertySpec* findPropertySpec(const NodeSchema& schema,
                                                   std::string_view name);

// Validates a DOM without compiling it. Diagnostics use stable schema.* codes
// and include the node path/property so the editor can select the bad field.
[[nodiscard]] std::vector<DesignError> validateDesignDocument(
    const DesignDocument& document);

}  // namespace lumen::dsl
