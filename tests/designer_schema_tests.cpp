#include <algorithm>
#include <limits>
#include <memory>
#include <set>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "lumen/core/splitter.h"
#include "lumen/dsl/design_schema.h"
#include "lumen/dsl/runtime_context.h"

using lumen::dsl::DesignDocument;
using lumen::dsl::DesignEnum;
using lumen::dsl::DesignNode;
using lumen::dsl::DesignValue;
using lumen::dsl::DesignReferenceKind;
using lumen::dsl::MapDesignRuntimeContext;
using lumen::dsl::compileDesignDocument;
using lumen::dsl::findNodeSchema;
using lumen::dsl::findPropertySpec;
using lumen::dsl::nodeSchemaRegistry;
using lumen::dsl::parseLumenSource;
using lumen::dsl::validateDesignDocument;
using lumen::dsl::widgetFieldInventory;

TEST_CASE("designer schema registry covers the L0 through L3 node set",
          "[designer][p2]") {
    REQUIRE(nodeSchemaRegistry().size() == 37);
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
    for (const auto* type : {"Grid", "Image", "Icon", "Slider", "ProgressBar",
                             "Radio", "Tooltip", "Dropdown", "Tabs",
                             "ThemeScope", "VirtualList", "List", "Tree",
                             "TreeList", "Splitter", "ComboBox", "ColorPicker",
                             "Spin", "ToolBar", "StatusBar", "Menu", "DialogHost",
                             "Navigator", "Form", "DataGrid"}) {
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
    MapDesignRuntimeContext context;
    for (const auto& schema : nodeSchemaRegistry()) {
        if (!schema.isComponent) continue;
        context.registerComponentBuilder(
            schema.type,
            [](const DesignNode&, const lumen::dsl::DesignComponentContext&) {
                lumen::dsl::DesignComponentResult result;
                result.widget = lumen::core::makeContainerLeaf();
                return result;
            });
    }
    for (const auto& schema : nodeSchemaRegistry()) {
        DesignDocument document;
        document.pageName = "schema-compile";
        document.root = DesignNode{1, schema.type};
        if (schema.type == "Splitter") {
            document.root.children = {DesignNode{2, "Text"},
                                      DesignNode{3, "Text"}};
        }
        for (const auto& property : schema.properties) {
            if (property.persistence ==
                lumen::dsl::PropertyPersistence::Declaration) {
                document.root.properties[property.name] = property.defaultValue;
            }
        }

        const auto compiled = compileDesignDocument(document, context);
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

TEST_CASE("designer compiler delegates L3 composition to a typed builder",
          "[designer][p3][designer-l3]") {
    DesignDocument document;
    document.pageName = "component-preview";
    document.root = DesignNode{1, "ComboBox"};
    document.root.properties["key"] =
        DesignValue{DesignValue::Variant{std::string{"picker"}}};
    document.root.references["component"] = "pickerController";
    document.root.slots["content"] = {DesignNode{2, "Text"}};

    int controller = 7;
    const auto lease = std::make_shared<int>(11);
    MapDesignRuntimeContext context;
    context.registerTypedReference(DesignReferenceKind::Component,
                                   "pickerController", &controller, lease);
    bool receivedController = false;
    context.registerComponentBuilder(
        "ComboBox", [&](const DesignNode& node,
                         const lumen::dsl::DesignComponentContext& component) {
            CHECK(node.type == "ComboBox");
            CHECK(node.slots.contains("content"));
            receivedController = component.controller == &controller;
            CHECK(component.session != nullptr);
            lumen::dsl::DesignComponentResult result;
            result.widget = lumen::core::makeButton("generated");
            result.lifetimeToken = std::make_shared<int>(13);
            return result;
        });

    const auto compiled = compileDesignDocument(document, context);
    REQUIRE(compiled.ok());
    CHECK(receivedController);
    CHECK(compiled.root.type == lumen::core::WidgetType::Button);
    CHECK(compiled.root.key == "picker");
    REQUIRE(compiled.session != nullptr);
    CHECK(compiled.session->leaseCount() == 2);
}

TEST_CASE("designer compiler keeps a component placeholder when its builder is missing",
          "[designer][p3][designer-l3]") {
    DesignDocument document;
    document.pageName = "component-placeholder";
    document.root = DesignNode{1, "Form"};

    const auto compiled = compileDesignDocument(document);
    REQUIRE_FALSE(compiled.ok());
    CHECK(compiled.root.type == lumen::core::WidgetType::Container);
    CHECK_FALSE(compiled.root.enabled);
    CHECK(compiled.root.invalid);
    const auto diagnostic = std::find_if(
        compiled.diagnostics.begin(), compiled.diagnostics.end(),
        [](const auto& error) { return error.code == "component.missing"; });
    REQUIRE(diagnostic != compiled.diagnostics.end());
    CHECK(diagnostic->nodeId == document.root.id);
}

TEST_CASE("designer component slots propagate unresolved references to the root",
          "[designer][p3][designer-l3]") {
    DesignDocument document;
    document.root = DesignNode{1, "Form"};
    document.root.slots["content"] = {DesignNode{2, "Button"}};
    document.root.slots["content"].front().references["onClick"] = "missing";

    MapDesignRuntimeContext context;
    context.registerComponentBuilder(
        "Form", [](const DesignNode&, const lumen::dsl::DesignComponentContext&) {
            lumen::dsl::DesignComponentResult result;
            result.widget = lumen::core::makeContainerLeaf();
            return result;
        });

    const auto compiled = compileDesignDocument(document, context);
    REQUIRE_FALSE(compiled.ok());
    CHECK_FALSE(compiled.root.enabled);
    CHECK(compiled.root.invalid);
    const auto diagnostic = std::find_if(
        compiled.diagnostics.begin(), compiled.diagnostics.end(),
        [](const auto& error) { return error.code == "reference.missing"; });
    REQUIRE(diagnostic != compiled.diagnostics.end());
    CHECK(diagnostic->nodePath == "root.slots[content][0]");
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

TEST_CASE("designer compiler applies the remaining L1 static controls",
          "[designer][p2][designer-l1]") {
    DesignDocument document;
    document.pageName = "l1-controls";
    document.root = DesignNode{1, "Column"};

    DesignNode slider{2, "Slider"};
    slider.properties["width"] = DesignValue{DesignValue::Variant{180.0}};
    DesignNode progress{3, "ProgressBar"};
    progress.properties["text"] =
        DesignValue{DesignValue::Variant{std::string{"50"}}};
    progress.properties["progressIndeterminate"] =
        DesignValue{DesignValue::Variant{true}};
    DesignNode radio{4, "Radio"};
    radio.properties["text"] =
        DesignValue{DesignValue::Variant{std::string{"Plan"}}};
    radio.properties["checked"] = DesignValue{DesignValue::Variant{true}};
    DesignNode tooltip{5, "Tooltip"};
    tooltip.properties["text"] =
        DesignValue{DesignValue::Variant{std::string{"Tip"}}};
    DesignNode dropdown{6, "Dropdown"};
    dropdown.properties["text"] =
        DesignValue{DesignValue::Variant{std::string{"Selected"}}};
    DesignNode tabs{7, "Tabs"};
    tabs.children = {DesignNode{8, "Button"}, DesignNode{9, "Button"}};
    DesignNode themeScope{10, "ThemeScope"};
    themeScope.children = {DesignNode{11, "Text"}};
    document.root.children = {slider, progress, radio, tooltip, dropdown, tabs,
                             themeScope};

    REQUIRE(validateDesignDocument(document).empty());
    const auto compiled = compileDesignDocument(document);
    REQUIRE(compiled.ok());
    REQUIRE(compiled.root.children.size() == 7);
    CHECK(compiled.root.children[0].showFocusRing);
    CHECK(compiled.root.children[0].width == 180.0F);
    CHECK(compiled.root.children[1].text == "50");
    CHECK(compiled.root.children[1].progressIndeterminate);
    CHECK(compiled.root.children[2].text == "Plan");
    CHECK(compiled.root.children[2].checked);
    CHECK(compiled.root.children[3].text == "Tip");
    CHECK(compiled.root.children[4].text == "Selected");
    CHECK(compiled.root.children[4].buttonVariant ==
          lumen::core::ButtonVariant::Outline);
    REQUIRE(compiled.root.children[5].children.size() == 2);
    CHECK(compiled.root.children[5].children[0].showFocusRing);
    CHECK(compiled.root.children[5].children[1].showFocusRing);
    REQUIRE(compiled.root.children[6].children.size() == 1);
    CHECK(compiled.root.children[6].children[0].type ==
          lumen::core::WidgetType::Text);
}

namespace {

class TestVirtualSource final : public lumen::core::VirtualListSource {
  public:
    [[nodiscard]] std::size_t itemCount() const override { return 0; }
    [[nodiscard]] float estimatedExtent() const override { return 24.0F; }
    [[nodiscard]] float extentOf(std::size_t) const override { return 24.0F; }
    [[nodiscard]] float scrollOffset() const override { return 0.0F; }
    [[nodiscard]] float totalExtent() const override { return 0.0F; }
    [[nodiscard]] float offsetOfIndex(std::size_t) const override { return 0.0F; }
    [[nodiscard]] std::pair<std::size_t, std::size_t> visibleRange(
        float, float) const override {
        return {0, 0};
    }
    [[nodiscard]] lumen::core::Widget buildItem(std::size_t) const override {
        return {};
    }
    void noteExtent(std::size_t, float) const override {}
};

class TestSplitterSource final : public lumen::core::SplitterSource {
  public:
    [[nodiscard]] float offsetPx() const override { return 100.0F; }
    [[nodiscard]] float minLeading() const override { return 48.0F; }
    [[nodiscard]] float minTrailing() const override { return 48.0F; }
    [[nodiscard]] float initialOffset() const override { return 100.0F; }
    [[nodiscard]] bool seeded() const override { return true; }
    [[nodiscard]] float extentPx() const override { return 400.0F; }
    void noteLayout(float, float) const override {}
    void dragTo(float) const override {}
    void stepBy(float) const override {}
    void stepToEdge(bool) const override {}
    void reset() const override {}
};

}  // namespace

TEST_CASE("designer compiler injects L2 source handles and leases",
          "[designer][p3][designer-l2]") {
    TestVirtualSource virtualSource;
    TestSplitterSource splitterSource;
    auto lease = std::make_shared<int>(7);
    MapDesignRuntimeContext context;
    context.registerTypedReference(DesignReferenceKind::VirtualSource, "rows",
                                   &virtualSource, lease);
    context.registerTypedReference(DesignReferenceKind::SplitterSource, "split",
                                   &splitterSource, lease);

    DesignDocument document;
    document.pageName = "l2-sources";
    document.root = DesignNode{1, "Column"};
    DesignNode list{2, "List"};
    list.references["virtualSource"] = "rows";
    list.properties["collectionSelectionMode"] = DesignValue{
        DesignValue::Variant{DesignEnum{"collectionSelectionMode", "single"}}};
    DesignNode splitter{3, "Splitter"};
    splitter.references["splitterSource"] = "split";
    splitter.children = {DesignNode{4, "Text"}, DesignNode{5, "Text"}};
    document.root.children = {list, splitter};

    REQUIRE(validateDesignDocument(document).empty());
    const auto compiled = compileDesignDocument(document, context);
    REQUIRE(compiled.ok());
    REQUIRE(compiled.session);
    CHECK(compiled.session->leaseCount() == 2);
    REQUIRE(compiled.root.children.size() == 2);
    CHECK(compiled.root.children[0].virtualSource == &virtualSource);
    CHECK(compiled.root.children[0].collectionSelectionMode == 1);
    CHECK(compiled.root.children[1].splitterSource == &splitterSource);
    compiled.session->close();
    CHECK_FALSE(compiled.session->active());
}

TEST_CASE("designer compiler keeps L2 source nodes inspectable when references are missing",
          "[designer][p3][designer-l2]") {
    DesignDocument document;
    document.root = DesignNode{1, "List"};
    document.root.references["virtualSource"] = "missing";

    MapDesignRuntimeContext context;
    const auto compiled = compileDesignDocument(document, context);
    REQUIRE_FALSE(compiled.ok());
    REQUIRE(compiled.root.type == lumen::core::WidgetType::List);
    CHECK_FALSE(compiled.root.enabled);
    CHECK(compiled.root.invalid);
    CHECK(compiled.root.virtualSource == nullptr);
    REQUIRE(compiled.diagnostics.size() == 1);
    CHECK(compiled.diagnostics.front().code == "reference.missing");
    CHECK(compiled.diagnostics.front().property == "virtualSource");
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
