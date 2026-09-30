#include <catch2/catch_test_macros.hpp>

#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_editor.h"
#include "lumen/dsl/design_preview.h"

using lumen::dsl::DesignDocument;
using lumen::dsl::DesignDocumentHistory;
using lumen::dsl::DesignPreviewState;
using lumen::dsl::DesignValue;
using lumen::dsl::parseLumenSource;
using lumen::dsl::serializeDesignDocument;

namespace {

DesignDocument sampleDocument() {
    const auto parsed = parseLumenSource(
        "page preview { ScrollView { Text(\"declared\") } }");
    return parsed.document;
}

}  // namespace

TEST_CASE("designer preview values shadow declarations without mutating DOM",
          "[designer][f6][preview]") {
    auto document = sampleDocument();
    REQUIRE(document.root.id != 0);
    const auto original = document;
    DesignPreviewState preview;
    const auto textId = document.root.children.front().id;
    REQUIRE(preview.setRuntimeValue(
        textId, "text", DesignValue{DesignValue::Variant{"runtime"}},
        document));
    CHECK(preview.value(textId, "text", document)->value ==
          DesignValue::Variant{"runtime"});
    REQUIRE(preview.setVisualOverride(
        textId, "text", DesignValue{DesignValue::Variant{"hover"}}, document));
    CHECK(preview.value(textId, "text", document)->value ==
          DesignValue::Variant{"hover"});
    CHECK(document == original);
    CHECK(preview.setBindingSnapshot(
        "counter", DesignValue{DesignValue::Variant{"42"}}));
    CHECK(preview.bindingSnapshot("counter")->value ==
          DesignValue::Variant{"42"});
}

TEST_CASE("designer preview state does not dirty or serialize into a document",
          "[designer][f6][preview]") {
    auto document = sampleDocument();
    DesignPreviewState preview;
    DesignDocumentHistory history;
    history.markSaved();
    const auto encoded = serializeDesignDocument(document);
    REQUIRE(preview.setVisualOverride(
        document.root.id, "hovered",
        DesignValue{DesignValue::Variant{true}}, document));
    REQUIRE(preview.setBindingSnapshot(
        "enabled", DesignValue{DesignValue::Variant{false}}));
    CHECK_FALSE(history.dirty());
    CHECK(serializeDesignDocument(document) == encoded);
    CHECK_FALSE(preview.empty());
    preview.clear();
    CHECK(preview.empty());
}

TEST_CASE("designer preview rejects values for deleted or invalid nodes",
          "[designer][f6][preview]") {
    auto document = sampleDocument();
    DesignPreviewState preview;
    CHECK_FALSE(preview.setRuntimeValue(
        999, "text", DesignValue{DesignValue::Variant{"orphan"}}, document));
    CHECK_FALSE(preview.setVisualOverride(
        document.root.id, "", DesignValue{DesignValue::Variant{true}},
        document));
    CHECK_FALSE(preview.value(999, "text", document).has_value());
    CHECK_FALSE(preview.bindingSnapshot("missing").has_value());
}

TEST_CASE("designer preview-only properties cannot enter the document",
          "[designer][f6][preview]") {
    auto parsed = parseLumenSource(
        "page preview { ScrollView { Text(\"body\") } }");
    REQUIRE(parsed.ok());
    parsed.document.root.properties["scrollOffset"] =
        DesignValue{DesignValue::Variant{12.0}};

    const auto compiled = lumen::dsl::compileDesignDocument(parsed.document);
    REQUIRE_FALSE(compiled.ok());
    REQUIRE(compiled.diagnostics.size() == 1);
    CHECK(compiled.diagnostics.front().code ==
          "schema.non_persistent_property");
    CHECK(compiled.diagnostics.front().property == "scrollOffset");
}
