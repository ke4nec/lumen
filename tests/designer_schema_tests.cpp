#include <algorithm>
#include <limits>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "lumen/dsl/design_schema.h"

using lumen::dsl::DesignDocument;
using lumen::dsl::DesignEnum;
using lumen::dsl::DesignNode;
using lumen::dsl::DesignValue;
using lumen::dsl::compileDesignDocument;
using lumen::dsl::findNodeSchema;
using lumen::dsl::findPropertySpec;
using lumen::dsl::nodeSchemaRegistry;
using lumen::dsl::parseLumenSource;
using lumen::dsl::validateDesignDocument;
using lumen::dsl::widgetFieldInventory;

TEST_CASE("designer schema registry covers the L0 and first L1 node set",
          "[designer][p2]") {
    REQUIRE(nodeSchemaRegistry().size() == 15);
    for (const auto& schema : nodeSchemaRegistry()) {
        REQUIRE(schema.canBeRoot);
        REQUIRE(schema.makeDefault);
        const auto* key = findPropertySpec(schema, "key");
        REQUIRE(key != nullptr);
        REQUIRE(key->get);
        REQUIRE(key->set);
        auto widget = schema.makeDefault();
        const DesignValue value{DesignValue::Variant{std::string{"stable"}}};
        REQUIRE(key->set(widget, value));
        CHECK(key->get(widget) == value);
    }
    for (const auto* type : {"Grid", "Image", "Icon"}) {
        REQUIRE(findNodeSchema(type) != nullptr);
    }
}

TEST_CASE("designer schema properties round trip through their Widget accessors",
          "[designer][p2]") {
    for (const auto& schema : nodeSchemaRegistry()) {
        auto widget = schema.makeDefault();
        for (const auto& property : schema.properties) {
            if (property.persistence ==
                lumen::dsl::PropertyPersistence::RuntimeReference) {
                CHECK_FALSE(property.get);
                CHECK_FALSE(property.set);
                continue;
            }
            REQUIRE(property.get);
            REQUIRE(property.set);
            auto value = property.defaultValue;
            if (std::holds_alternative<std::monostate>(value.value)) {
                value = DesignValue{DesignValue::Variant{37.0}};
            }
            REQUIRE(property.set(widget, value));
            CHECK(property.get(widget) == value);
        }

        const auto width = findPropertySpec(schema, "width");
        REQUIRE(width != nullptr);
        REQUIRE(width->set);
        REQUIRE(width->set(widget, DesignValue{}));
        CHECK(width->get(widget) == DesignValue{});
        const auto height = findPropertySpec(schema, "height");
        REQUIRE(height != nullptr);
        REQUIRE(height->get);
        REQUIRE(height->set);
        REQUIRE(height->set(widget, DesignValue{}));
        CHECK(height->get(widget) == DesignValue{});
    }
}

TEST_CASE("designer compiler applies every registered declaration through the registry",
          "[designer][p2]") {
    for (const auto& schema : nodeSchemaRegistry()) {
        DesignDocument document;
        document.pageName = "schema-compile";
        document.root = DesignNode{1, schema.type};
        for (const auto& property : schema.properties) {
            if (property.persistence ==
                lumen::dsl::PropertyPersistence::Declaration) {
                document.root.properties[property.name] = property.defaultValue;
            }
        }

        const auto compiled = compileDesignDocument(document);
        REQUIRE(compiled.ok());
        for (const auto& property : schema.properties) {
            if (property.persistence !=
                lumen::dsl::PropertyPersistence::Declaration) {
                continue;
            }
            REQUIRE(property.get);
            CHECK(property.get(compiled.root) ==
                  document.root.properties.at(property.name));
        }
    }
}

TEST_CASE("designer compiler applies first L1 static widget declarations",
          "[designer][p2][designer-l1]") {
    DesignDocument document;
    document.pageName = "l1-static";
    document.root = DesignNode{1, "Grid"};
    document.root.properties["columnCount"] =
        DesignValue{DesignValue::Variant{3.0}};
    document.root.properties["columnGap"] =
        DesignValue{DesignValue::Variant{8.0}};
    document.root.properties["rowGap"] =
        DesignValue{DesignValue::Variant{12.0}};

    DesignNode image{2, "Image"};
    image.properties["imageSource"] =
        DesignValue{DesignValue::Variant{std::string{"project://hero.png"}}};
    image.properties["width"] = DesignValue{DesignValue::Variant{120.0}};
    image.properties["height"] = DesignValue{DesignValue::Variant{80.0}};

    DesignNode icon{3, "Icon"};
    icon.properties["icon"] = DesignValue{DesignValue::Variant{
        DesignEnum{"icon", "search"}}};
    document.root.children = {image, icon};

    CHECK(validateDesignDocument(document).empty());
    const auto compiled = compileDesignDocument(document);
    REQUIRE(compiled.ok());
    REQUIRE(compiled.root.type == lumen::core::WidgetType::Grid);
    CHECK(compiled.root.gridColumnCount == 3);
    CHECK(compiled.root.gridColumnGap == 8.0F);
    CHECK(compiled.root.gridRowGap == 12.0F);
    REQUIRE(compiled.root.children.size() == 2);
    CHECK(compiled.root.children[0].type == lumen::core::WidgetType::Image);
    CHECK(compiled.root.children[0].imageSource == "project://hero.png");
    CHECK(compiled.root.children[0].width == 120.0F);
    CHECK(compiled.root.children[0].height == 80.0F);
    CHECK(compiled.root.children[1].type == lumen::core::WidgetType::Icon);
    CHECK(compiled.root.children[1].icon == lumen::core::IconId::Search);

    auto invalidGrid = document;
    invalidGrid.root.properties["columnCount"] =
        DesignValue{DesignValue::Variant{1.5}};
    const auto invalidGridDiagnostics = validateDesignDocument(invalidGrid);
    REQUIRE(invalidGridDiagnostics.size() == 1);
    CHECK(invalidGridDiagnostics.front().code == "schema.invalid_property");
    CHECK(invalidGridDiagnostics.front().property == "columnCount");

    auto invalidImage = document;
    invalidImage.root = image;
    invalidImage.root.children.push_back(DesignNode{4, "Text"});
    const auto invalidImageDiagnostics = validateDesignDocument(invalidImage);
    REQUIRE(invalidImageDiagnostics.size() == 1);
    CHECK(invalidImageDiagnostics.front().code == "schema.children");
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
    const auto variantDiagnostic = std::find_if(
        diagnostics.begin(), diagnostics.end(), [](const auto& error) {
            return error.property == "variant";
        });
    REQUIRE(variantDiagnostic != diagnostics.end());
    CHECK(variantDiagnostic->pos ==
          valid.document.root.propertySources.at("variant").begin);

    auto wrongDomain = valid.document;
    wrongDomain.root.properties["variant"] = DesignValue{DesignValue::Variant{
        DesignEnum{"mainAxis", "tonal"}}};
    diagnostics = validateDesignDocument(wrongDomain);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().code == "schema.invalid_property");

    auto outOfRange = valid.document;
    outOfRange.root.properties["width"] =
        DesignValue{DesignValue::Variant{std::numeric_limits<double>::max()}};
    outOfRange.root.properties["maxLines"] =
        DesignValue{DesignValue::Variant{1.5}};
    diagnostics = validateDesignDocument(outOfRange);
    REQUIRE(diagnostics.size() == 2);
    CHECK(std::all_of(diagnostics.begin(), diagnostics.end(),
                      [](const auto& error) {
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

    DesignDocument duplicateIds;
    duplicateIds.root = DesignNode{1, "Column"};
    duplicateIds.root.children = {DesignNode{1, "Text"}};
    diagnostics = validateDesignDocument(duplicateIds);
    REQUIRE(std::find_if(diagnostics.begin(), diagnostics.end(),
                         [](const auto& error) {
                             return error.code == "schema.duplicate_node_id";
                         }) != diagnostics.end());
}

TEST_CASE("designer schema inventory covers registry persistence categories",
          "[designer][p2]") {
    const auto& inventory = widgetFieldInventory();
    REQUIRE(inventory.size() == 85);

    std::set<std::string> names;
    std::set<std::string> schemaNames;
    for (const auto& field : inventory) {
        REQUIRE_FALSE(field.name.empty());
        CHECK(names.insert(field.name).second);
        if (!field.schemaName.empty()) {
            CHECK(schemaNames.insert(field.schemaName).second);
        }
    }

    for (const auto& schema : nodeSchemaRegistry()) {
        for (const auto& property : schema.properties) {
            const auto found = std::find_if(
                inventory.begin(), inventory.end(), [&](const auto& field) {
                    return field.schemaName == property.name;
                });
            REQUIRE(found != inventory.end());
            const auto expected = [&] {
                switch (property.persistence) {
                    case lumen::dsl::PropertyPersistence::Declaration:
                        return lumen::dsl::WidgetFieldCategory::Declaration;
                    case lumen::dsl::PropertyPersistence::RuntimeReference:
                        return lumen::dsl::WidgetFieldCategory::RuntimeReference;
                    case lumen::dsl::PropertyPersistence::PreviewOnly:
                        return lumen::dsl::WidgetFieldCategory::PreviewOnly;
                    case lumen::dsl::PropertyPersistence::Derived:
                        return lumen::dsl::WidgetFieldCategory::Derived;
                }
                return lumen::dsl::WidgetFieldCategory::Structural;
            }();
            CHECK(found->category == expected);
        }
    }

    const auto parsed = parseLumenSource(
        "page p { Button(\"Save\", bind: enabled) }");
    REQUIRE(parsed.ok());
    auto misplaced = parsed.document;
    misplaced.root.properties["bind"] =
        DesignValue{DesignValue::Variant{std::string{"enabled"}}};
    const auto diagnostics = validateDesignDocument(misplaced);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().code == "schema.reference_location");
    CHECK(diagnostics.front().property == "bind");
}
