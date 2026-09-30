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

    const auto duplicate = readDesignDocument(
        R"({"documentId":"doc","format":"lumen.design","pageName":"p",
           "root":{"id":"1","type":"Column","properties":{},
                   "children":[{"id":"1","type":"Text","properties":{}}]},
           "schemaVersion":1})",
        "duplicate.design");
    REQUIRE_FALSE(duplicate.ok());
    CHECK(duplicate.error->code == "codec.duplicate_node_id");
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
