#include <string>

#include <catch2/catch_test_macros.hpp>

#include "lumen/dsl/design_codec.h"

using lumen::core::Color;
using lumen::dsl::DesignDocument;
using lumen::dsl::DesignNode;
using lumen::dsl::DesignRuntimeContext;
using lumen::dsl::DesignValue;
using lumen::dsl::DesignReferenceKind;
using lumen::dsl::MapDesignRuntimeContext;
using lumen::dsl::compileDesignDocument;
using lumen::dsl::parseLumen;
using lumen::dsl::parseLumenSource;
using lumen::dsl::readDesignDocument;
using lumen::dsl::serializeDesignDocument;

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

TEST_CASE("designer document compiles to the existing widget golden",
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
    const auto expected = parseLumen(source);
    REQUIRE(expected.ok());
    CHECK(compiled.root == expected.root);
    CHECK(compiled.trace.nodes.size() == 3);
    CHECK(compiled.trace.nodes.at(document.document.root.id).documentId ==
          document.document.root.id);
}

TEST_CASE("designer document diagnostics reject unsupported and damaged input",
          "[designer][p1]") {
    DesignDocument unknown;
    unknown.pageName = "preview";
    unknown.root = DesignNode{1, "Grid"};
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
