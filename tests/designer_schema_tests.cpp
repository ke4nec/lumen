#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "lumen/dsl/design_schema.h"

using lumen::dsl::DesignDocument;
using lumen::dsl::DesignEnum;
using lumen::dsl::DesignNode;
using lumen::dsl::DesignValue;
using lumen::dsl::findNodeSchema;
using lumen::dsl::findPropertySpec;
using lumen::dsl::nodeSchemaRegistry;
using lumen::dsl::parseLumenSource;
using lumen::dsl::validateDesignDocument;

TEST_CASE("designer schema registry covers the frozen L0 node set",
          "[designer][p2]") {
    REQUIRE(nodeSchemaRegistry().size() == 12);
    for (const auto& schema : nodeSchemaRegistry()) {
        REQUIRE(schema.canBeRoot);
        REQUIRE(schema.makeDefault);
        CHECK(schema.makeDefault().type != lumen::core::WidgetType::Grid);
        const auto* key = findPropertySpec(schema, "key");
        REQUIRE(key != nullptr);
        REQUIRE(key->get);
        REQUIRE(key->set);
        auto widget = schema.makeDefault();
        const DesignValue value{DesignValue::Variant{std::string{"stable"}}};
        REQUIRE(key->set(widget, value));
        CHECK(key->get(widget) == value);
    }
    CHECK(findNodeSchema("Grid") == nullptr);
}

TEST_CASE("designer schema validates types, enums, ranges and structure",
          "[designer][p2]") {
    const auto valid = parseLumenSource(
        "page p { Button(\"Save\", variant: tonal, weight: 400) }");
    REQUIRE(valid.ok());
    CHECK(validateDesignDocument(valid.document).empty());

    auto invalid = valid.document;
    invalid.root.properties["variant"] = DesignValue{DesignValue::Variant{
        DesignEnum{"variant", "not-a-variant"}}};
    invalid.root.properties["weight"] =
        DesignValue{DesignValue::Variant{450.0}};
    auto diagnostics = validateDesignDocument(invalid);
    REQUIRE(diagnostics.size() == 2);
    CHECK(std::all_of(diagnostics.begin(), diagnostics.end(), [](const auto& error) {
        return error.code == "schema.invalid_property";
    }));

    auto unknown = valid.document;
    unknown.root.properties["futureProperty"] =
        DesignValue{DesignValue::Variant{true}};
    diagnostics = validateDesignDocument(unknown);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().code == "schema.unknown_property");

    auto tooMany = parseLumenSource("page p { Text(\"x\") { Text(\"y\") } }");
    REQUIRE_FALSE(tooMany.ok());
}
