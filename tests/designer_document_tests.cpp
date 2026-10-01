#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "lumen/core/render_node.h"
#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_schema.h"
#include "lumen/layout/layout.h"

using lumen::core::Color;
using lumen::core::Constraints;
using lumen::core::Size;
using lumen::dsl::DesignDocument;
using lumen::dsl::DesignEnum;
using lumen::dsl::DesignNode;
using lumen::dsl::DesignRuntimeContext;
using lumen::dsl::DesignValue;
using lumen::dsl::DesignReferenceKind;
using lumen::dsl::MapDesignRuntimeContext;
using lumen::dsl::compileDesignDocument;
using lumen::dsl::parseLumenSource;
using lumen::dsl::readDesignDocument;
using lumen::dsl::serializeDesignDocument;
using lumen::dsl::validateDesignDocument;
using lumen::layout::LayoutEngine;

TEST_CASE("designer document imports and round trips the L0 DOM",
          "[designer][p1]") {
    const std::string source = R"(page dashboard {
        Column(key: "root", padding: 16, mainAxis: center) {
            Text("Hello", key: "title", color: #102030)
            Button("Save", key: "save", onClick: saveDocument)
            Checkbox("Ready", bind: ready, checked: true)
        }
    })";

    const auto parsed = parseLumenSource(source, "preview.lumen");
    REQUIRE(parsed.ok());
    CHECK(parsed.document.schemaVersion == 1);
    CHECK(parsed.document.pageName == "dashboard");
    CHECK(parsed.document.root.id == 1);
    REQUIRE(parsed.document.root.children.size() == 3);
    CHECK(parsed.document.root.children[0].id == 2);
    CHECK(parsed.document.root.children[1].id == 3);
    CHECK(parsed.document.root.children[2].id == 4);
    CHECK(parsed.document.root.children[1].references.at("onClick") ==
          "saveDocument");

    const std::string encoded = serializeDesignDocument(parsed.document);
    const auto decoded = readDesignDocument(encoded, "preview.lumen.design");
    REQUIRE(decoded.ok());
    CHECK(decoded.document == parsed.document);
    CHECK(serializeDesignDocument(decoded.document) == encoded);
}

TEST_CASE("designer codec round trips every frozen L0 node",
          "[designer][p1]") {
    const auto parsed = parseLumenSource(
        R"(page all_l0 {
            Column(key: "root", padding: 16, mainAxis: center,
                   crossAxis: stretch) {
                Container(key: "container", color: #102030) {
                    Text("inside", key: "container-text")
                }
                Row(key: "row", spacing: 4, mainAxis: spaceBetween,
                    crossAxis: center) {
                    Text("label", key: "text", fontSize: 18)
                    Button("Save", key: "button", variant: tonal, onClick: save)
                    TextField(key: "field", placeholder: "Type", bind: value,
                              maxLines: 2)
                    Checkbox("Ready", key: "checkbox", checked: true)
                    Switch("Enabled", key: "switch", checked: false)
                }
                Stack(key: "stack", alignment: center) {
                    Text("stacked", key: "stack-text", left: 4, top: 8)
                }
                ScrollView(key: "scroll") {
                    Text("scroll", key: "scroll-text")
                }
                ListView(key: "list") {
                    Text("list", key: "list-text")
                }
                FocusScope(key: "focus") {
                    Text("focus", key: "focus-text")
                }
            }
        })",
        "all-l0.lumen");
    REQUIRE(parsed.ok());

    std::set<std::string> types;
    const auto collectTypes = [&](const auto& self,
                                  const DesignNode& node) -> void {
        types.insert(node.type);
        for (const auto& child : node.children) self(self, child);
    };
    collectTypes(collectTypes, parsed.document.root);
    CHECK(types == std::set<std::string>{
                       "Button", "Checkbox", "Column", "Container",
                       "FocusScope", "ListView", "Row", "ScrollView", "Stack",
                       "Switch", "Text", "TextField"});

    auto document = parsed.document;
    document.unknownFields["future"] = "{\"enabled\":true}";
    document.root.unknownFields["futureRoot"] = "[1,2,3]";
    const auto encoded = serializeDesignDocument(document);
    const auto decoded = readDesignDocument(encoded, "all-l0.design");
    REQUIRE(decoded.ok());
    CHECK(decoded.document == document);
    CHECK(serializeDesignDocument(decoded.document) == encoded);
    CHECK(decoded.document.root.source.has_value());
    CHECK(decoded.document.root.propertySources.contains("padding"));
}

TEST_CASE("designer codec round trips the first L1 static nodes",
          "[designer][p2][designer-l1]") {
    DesignDocument document;
    document.documentId = "l1-doc";
    document.pageName = "static";
    document.root = DesignNode{1, "Grid"};
    document.root.properties["columnCount"] =
        DesignValue{DesignValue::Variant{2.0}};
    DesignNode image{2, "Image"};
    image.properties["imageSource"] = DesignValue{DesignValue::Variant{
        std::string{"project://images/cover.png"}}};
    DesignNode icon{3, "Icon"};
    icon.properties["icon"] = DesignValue{DesignValue::Variant{
        DesignEnum{"icon", "document"}}};
    document.root.children = {image, icon};

    REQUIRE(validateDesignDocument(document).empty());
    const auto encoded = serializeDesignDocument(document);
    const auto decoded = readDesignDocument(encoded, "l1.design");
    REQUIRE(decoded.ok());
    CHECK(decoded.document == document);
    CHECK(serializeDesignDocument(decoded.document) == encoded);
    CHECK(compileDesignDocument(decoded.document).ok());
}

TEST_CASE("designer document compiles to the independent C++ builder golden",
          "[designer][p1]") {
    const std::string source = R"(page preview {
        Column(key: "root", padding: 4, spacing: 8) {
            Text("Count: ", key: "label", bind: count, fontSize: 18)
            Button("Add", key: "add", variant: tonal, onClick: increment)
        }
    })";
    const auto document = parseLumenSource(source);
    REQUIRE(document.ok());

    MapDesignRuntimeContext context;
    context.registerReference(DesignReferenceKind::Binding, "count");
    context.registerReference(DesignReferenceKind::Handler, "increment");
    const auto compiled = compileDesignDocument(document.document, context);
    REQUIRE(compiled.ok());

    auto count = lumen::core::makeText(
        "Count: ", lumen::core::TextStyle{18.0F}, {}, 0.0F, "label");
    count.bind = "count";
    count.bindPrefix = count.text;
    auto add = lumen::core::makeButton("Add", {}, {}, 0.0F, "add");
    add.buttonVariant = lumen::core::ButtonVariant::Tonal;
    add.onClick = "increment";
    const auto expected = lumen::core::makeColumn(
        {std::move(count), std::move(add)},
        lumen::core::MainAxisAlignment::Start,
        lumen::core::CrossAxisAlignment::Start, 8.0F, lumen::core::EdgeInsets::all(4.0F),
        {}, "root");
    CHECK(compiled.root == expected);
    CHECK(compiled.trace.nodes.size() == 3);
    CHECK(compiled.trace.nodes.at(document.document.root.id).documentId ==
          document.document.root.id);
}

TEST_CASE("designer compile trace identities locate laid out nodes",
          "[designer][p4]") {
    const auto parsed = parseLumenSource(
        "page trace { Column { Text(\"plain\") "
        "Button(\"Keyed\", key: \"button\") } }");
    REQUIRE(parsed.ok());

    const auto compiled = compileDesignDocument(parsed.document);
    REQUIRE(compiled.ok());
    const auto renderTree = LayoutEngine::layout(
        compiled.root, Constraints::tight(Size{640.0F, 480.0F}));

    REQUIRE(compiled.trace.nodes.size() == 3);
    CHECK(compiled.trace.nodes.at(parsed.document.root.id).runtimeIdentity ==
          "/i:0");
    CHECK(compiled.trace.nodes.at(parsed.document.root.children[0].id)
              .runtimeIdentity == "/i:0/i:0");
    CHECK(compiled.trace.nodes.at(parsed.document.root.children[1].id)
              .runtimeIdentity == "/i:0/k:button");
    for (const auto& [id, reference] : compiled.trace.nodes) {
        (void)id;
        CHECK(lumen::core::findNodeByIdentity(renderTree,
                                               reference.runtimeIdentity) !=
              nullptr);
    }
}

TEST_CASE("designer compiler rejects duplicate runtime identities",
          "[designer][p4]") {
    const auto parsed = parseLumenSource(
        "page trace { Column { Text(\"first\", key: \"dup\") "
        "Text(\"second\", key: \"dup\") } }");
    REQUIRE(parsed.ok());

    const auto compiled = compileDesignDocument(parsed.document);
    REQUIRE_FALSE(compiled.ok());
    const auto diagnostic = std::find_if(
        compiled.diagnostics.begin(), compiled.diagnostics.end(),
        [](const auto& error) {
            return error.code == "compile.duplicate_runtime_identity";
        });
    REQUIRE(diagnostic != compiled.diagnostics.end());
    CHECK(diagnostic->nodeId == parsed.document.root.children[1].id);
    CHECK(diagnostic->nodePath == "root.children[1]");
    CHECK(diagnostic->property == "key");
    CHECK(diagnostic->pos ==
          parsed.document.root.children[1].propertySources.at("key").begin);
}

TEST_CASE("designer compiler preserves codec strings outside DSL escapes",
          "[designer][p1]") {
    DesignDocument document;
    document.pageName = "preview";
    document.root = DesignNode{1, "Text"};
    document.root.properties["text"] =
        DesignValue{DesignValue::Variant{std::string("A\0B", 3)}};

    const auto compiled = compileDesignDocument(document);
    REQUIRE(compiled.ok());
    CHECK(compiled.root.type == lumen::core::WidgetType::Text);
    CHECK(compiled.root.text == std::string("A\0B", 3));
    CHECK(compiled.root.bindPrefix == std::string("A\0B", 3));
}

TEST_CASE("designer document diagnostics reject unsupported and damaged input",
          "[designer][p1]") {
    DesignDocument unknown;
    unknown.pageName = "preview";
    unknown.root = DesignNode{1, "ComboBox"};
    const auto unsupported = compileDesignDocument(unknown);
    REQUIRE_FALSE(unsupported.ok());
    REQUIRE(unsupported.diagnostics.size() == 1);
    CHECK(unsupported.diagnostics.front().code == "compile.unknown_node");

    const auto damaged = readDesignDocument("{", "broken.design");
    REQUIRE_FALSE(damaged.ok());
    CHECK(damaged.error->file == "broken.design");
    CHECK(damaged.error->pos.line == 1);
    CHECK(damaged.error->pos.column == 2);

    const auto badDsl = parseLumenSource("page preview { Unknown {} }");
    REQUIRE_FALSE(badDsl.ok());
    CHECK(badDsl.error->code == "schema.error");

    const auto future = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design","pageName":"p",
           "root":{"id":"1","type":"Text","properties":{},"future":{"x":1}},
           "schemaVersion":1,"future":true})",
        "future.design");
    REQUIRE(future.ok());
    CHECK(future.document.root.unknownFields.at("future") == "{\"x\":1}");
    CHECK(future.document.unknownFields.at("future") == "true");
    const auto futureRoundTrip = readDesignDocument(
        serializeDesignDocument(future.document), "future-roundtrip.design");
    REQUIRE(futureRoundTrip.ok());
    CHECK(futureRoundTrip.document.root.unknownFields ==
          future.document.root.unknownFields);
    CHECK(futureRoundTrip.document.unknownFields ==
          future.document.unknownFields);

    const auto duplicateDocumentUnknown = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design","pageName":"p",
           "root":{"id":"1","type":"Text","properties":{}},
           "unknownFields":{"future":true},"future":false,
           "schemaVersion":1})",
        "duplicate-document-unknown.design");
    REQUIRE_FALSE(duplicateDocumentUnknown.ok());
    REQUIRE(duplicateDocumentUnknown.error.has_value());
    CHECK(duplicateDocumentUnknown.error->code ==
          "codec.duplicate_unknown_field");

    const auto duplicateNodeUnknown = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design","pageName":"p",
           "root":{"id":"1","type":"Text","properties":{},
                   "unknownFields":{"future":true},"future":false},
           "schemaVersion":1})",
        "duplicate-node-unknown.design");
    REQUIRE_FALSE(duplicateNodeUnknown.ok());
    REQUIRE(duplicateNodeUnknown.error.has_value());
    CHECK(duplicateNodeUnknown.error->code == "codec.duplicate_unknown_field");

    const auto duplicate = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design","pageName":"p",
           "root":{"id":"1","type":"Column","properties":{},
                   "children":[{"id":"1","type":"Text","properties":{}}]},
           "schemaVersion":1})",
        "duplicate.design");
    REQUIRE_FALSE(duplicate.ok());
    CHECK(duplicate.error->code == "codec.duplicate_node_id");

    const auto hugeSpan = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design","pageName":"p",
           "root":{"id":"1","type":"Text","properties":{},
                   "source":{"begin":{"line":1e308,"column":1},
                             "end":{"line":1e308,"column":2}}},
           "schemaVersion":1})",
        "huge-span.design");
    REQUIRE_FALSE(hugeSpan.ok());
    REQUIRE(hugeSpan.error.has_value());
    CHECK(hugeSpan.error->code == "codec.source_span");
}

TEST_CASE("designer codec rejects non-decimal node ids", "[designer][p1]") {
    const auto sourceForId = [](const std::string& id) {
        return std::string{
                   R"({"documentId":"doc","format":"lumen.design",
                      "pageName":"p","root":{"id":")"} +
               id + R"(","type":"Text","properties":{}},
                      "schemaVersion":1})";
    };

    for (const std::string id : {"-1", "+1", " 1"}) {
        const auto result =
            readDesignDocument(sourceForId(id), "invalid-id.design");
        REQUIRE_FALSE(result.ok());
        REQUIRE(result.error.has_value());
        CHECK(result.error->code == "codec.node_id");
    }
}

TEST_CASE("designer codec decodes Unicode escapes", "[designer][p1]") {
    const auto result = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design",
           "pageName":"\u4e2d\u6587",
           "root":{"id":"1","type":"Text","properties":{
             "text":{"kind":"string","value":"\uD83D\uDE00"}}},
           "schemaVersion":1})",
        "unicode.design");
    REQUIRE(result.ok());
    CHECK(result.document.pageName == "\xE4\xB8\xAD\xE6\x96\x87");
    const auto& text = result.document.root.properties.at("text");
    REQUIRE(std::holds_alternative<std::string>(text.value));
    CHECK(std::get<std::string>(text.value) == "\xF0\x9F\x98\x80");
    CHECK(readDesignDocument(serializeDesignDocument(result.document),
                             "unicode-roundtrip.design")
              .ok());

    const auto sourceWithPageName = [](const std::string& pageName) {
        return std::string{
                   R"({"documentId":"doc","format":"lumen.design",
                      "pageName":")"} +
               pageName + R"(","root":{"id":"1","type":"Text","properties":{}},
                      "schemaVersion":1})";
    };
    const std::string rawUtf8 =
        "\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80";
    const auto rawUtf8Result =
        readDesignDocument(sourceWithPageName(rawUtf8), "raw-utf8.design");
    REQUIRE(rawUtf8Result.ok());
    CHECK(rawUtf8Result.document.pageName == rawUtf8);
    CHECK(serializeDesignDocument(rawUtf8Result.document).find(rawUtf8) !=
          std::string::npos);

    const std::vector<std::string> invalidUtf8Cases{
        std::string{"\xC3\x28", 2},          // invalid continuation
        std::string{"\xE0\x80\x80", 3},     // overlong
        std::string{"\xED\xA0\x80", 3},     // UTF-16 surrogate
        std::string{"\xF4\x90\x80\x80", 4},  // above U+10FFFF
        std::string{"\x80", 1},              // stray continuation
        std::string{"\xF0\x90", 2},          // truncated four-byte sequence
    };
    for (const auto& invalid : invalidUtf8Cases) {
        const auto invalidResult = readDesignDocument(
            sourceWithPageName(invalid), "invalid-utf8.design");
        REQUIRE_FALSE(invalidResult.ok());
        REQUIRE(invalidResult.error.has_value());
        CHECK(invalidResult.error->code == "codec.utf8");
    }

    const auto invalidSurrogate = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design",
           "pageName":"\uDE00",
           "root":{"id":"1","type":"Text","properties":{}},
           "schemaVersion":1})",
        "invalid-unicode.design");
    REQUIRE_FALSE(invalidSurrogate.ok());
    REQUIRE(invalidSurrogate.error.has_value());
    CHECK(invalidSurrogate.error->code == "codec.escape");
}

TEST_CASE("designer codec rejects ambiguous JSON", "[designer][p1]") {
    const auto leadingZero = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design",
           "pageName":"p","root":{"id":"1","type":"Text",
           "properties":{}},"schemaVersion":01})",
        "leading-zero.design");
    REQUIRE_FALSE(leadingZero.ok());
    REQUIRE(leadingZero.error.has_value());
    CHECK(leadingZero.error->code == "codec.invalid_number");

    const auto duplicateKey = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design",
           "pageName":"p","pageName":"shadow",
           "root":{"id":"1","type":"Text","properties":{}},
           "schemaVersion":1})",
        "duplicate-key.design");
    REQUIRE_FALSE(duplicateKey.ok());
    REQUIRE(duplicateKey.error.has_value());
    CHECK(duplicateKey.error->code == "codec.duplicate_key");

    const auto missingFraction = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design",
           "pageName":"p","root":{"id":"1","type":"Text",
           "properties":{}},"schemaVersion":1.})",
        "missing-fraction.design");
    REQUIRE_FALSE(missingFraction.ok());
    REQUIRE(missingFraction.error.has_value());
    CHECK(missingFraction.error->code == "codec.invalid_number");

    const auto missingExponent = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design",
           "pageName":"p","root":{"id":"1","type":"Text",
           "properties":{}},"schemaVersion":1e+})",
        "missing-exponent.design");
    REQUIRE_FALSE(missingExponent.ok());
    REQUIRE(missingExponent.error.has_value());
    CHECK(missingExponent.error->code == "codec.invalid_number");
}

TEST_CASE("designer document does not accept a runtime-only reference in P1",
          "[designer][p1]") {
    DesignDocument document;
    document.pageName = "preview";
    document.root.id = 1;
    document.root.type = "Text";
    document.root.properties["text"] =
        DesignValue{DesignValue::Variant{std::string{"hello"}}};
    document.root.references["image"] = "logo";
    const auto compiled = compileDesignDocument(document);
    REQUIRE_FALSE(compiled.ok());
    CHECK(compiled.diagnostics.front().code == "reference.unsupported");
}
