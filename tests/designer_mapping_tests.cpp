#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_mapping.h"
#include "lumen/dsl/design_schema.h"

using lumen::core::Offset;
using lumen::core::Rect;
using lumen::dsl::DesignCoordinateTransform;
using lumen::dsl::DesignDocument;
using lumen::dsl::DesignDocumentEditor;
using lumen::dsl::DesignNode;
using lumen::dsl::DesignNodeId;
using lumen::dsl::DesignSourceMap;
using lumen::dsl::DesignSourceSpan;
using lumen::dsl::DesignValue;
using lumen::dsl::parseLumenSource;
using lumen::dsl::SourcePos;
using lumen::dsl::readDesignDocument;
using lumen::dsl::serializeDesignDocument;
using lumen::dsl::validateDesignDocument;

TEST_CASE("designer codec preserves node and property source spans",
          "[designer][p4]") {
    const auto parsed = parseLumenSource(
        "page mapping { Button(\"Save\", variant: tonal, onClick: save) }");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.document.root.source.has_value());
    CHECK(parsed.document.root.source->end !=
          parsed.document.root.source->begin);
    REQUIRE(parsed.document.root.propertySources.contains("text"));
    REQUIRE(parsed.document.root.propertySources.contains("variant"));
    REQUIRE(parsed.document.root.propertySources.contains("onClick"));

    const auto encoded = serializeDesignDocument(parsed.document);
    const auto decoded = readDesignDocument(encoded, "mapping.design");
    REQUIRE(decoded.ok());
    CHECK(decoded.document == parsed.document);

    const auto sourceMap = DesignSourceMap::fromDocument(decoded.document);
    CHECK(sourceMap.nodeSpan(decoded.document.root.id) ==
          decoded.document.root.source);
    CHECK(sourceMap.propertySpan(decoded.document.root.id, "variant") ==
          decoded.document.root.propertySources.at("variant"));
    CHECK_FALSE(sourceMap.propertySpan(decoded.document.root.id, "missing"));
}

TEST_CASE("designer coordinates round trip through dpi zoom and pan",
          "[designer][p4]") {
    DesignCoordinateTransform transform;
    REQUIRE(transform.setDeviceScale(1.25F));
    REQUIRE(transform.setZoom(1.5F));
    REQUIRE(transform.setPan(Offset{10.0F, 20.0F}));
    CHECK_FALSE(transform.setDeviceScale(0.0F));
    CHECK_FALSE(transform.setZoom(std::nanf("")));
    CHECK_FALSE(transform.setPan(Offset{std::nanf(""), 20.0F}));
    CHECK(transform.pan() == Offset{10.0F, 20.0F});

    const Offset design{32.0F, 18.0F};
    const auto pixels = transform.designToPixels(design);
    const auto roundTrip = transform.pixelsToDesign(pixels);
    CHECK(roundTrip.x == Catch::Approx(design.x));
    CHECK(roundTrip.y == Catch::Approx(design.y));
    CHECK(transform.designToLocal(design, Offset{2.0F, 3.0F}) ==
          Offset{30.0F, 15.0F});

    const auto pixelsRect = transform.designRectToPixels(
        Rect::fromXYWH(4.0F, 6.0F, 20.0F, 10.0F));
    CHECK(pixelsRect.origin.x == Catch::Approx(20.0F));
    CHECK(pixelsRect.origin.y == Catch::Approx(36.25F));
    CHECK(pixelsRect.size.width == Catch::Approx(37.5F));
    CHECK(pixelsRect.size.height == Catch::Approx(18.75F));
}

TEST_CASE("designer structural edits preserve stable ids and validate",
          "[designer][p4]") {
    DesignDocument document;
    document.documentId = "lumen:editor";
    document.pageName = "editor";
    document.root = DesignNode{1, "Row"};
    document.root.children = {
        DesignNode{2, "Row", {}, {}, {},
                   {DesignNode{4, "Text"}}, {}, {}, std::nullopt},
        DesignNode{3, "Button"}};
    DesignDocumentEditor editor{document};

    const auto duplicate = editor.duplicateNode(2, 1, 2);
    REQUIRE(duplicate.has_value());
    CHECK(*duplicate != 2);
    CHECK(editor.document().root.children.size() == 3);
    CHECK(editor.document().root.children[0].id == 2);
    CHECK(editor.document().root.children[2].id == *duplicate);
    CHECK(editor.document().root.children[2].children.front().id != 4);

    REQUIRE(editor.moveNode(3, 2, 1));
    CHECK(editor.document().root.children[0].children[1].id == 3);
    CHECK_FALSE(editor.moveNode(2, 2, 0));

    const auto removed = editor.removeNode(4);
    REQUIRE(removed.has_value());
    CHECK(removed->type == "Text");
    CHECK_FALSE(editor.removeNode(1).has_value());
    CHECK(validateDesignDocument(editor.document()).empty());

    DesignNode inserted{0, "Text"};
    inserted.source = DesignSourceSpan{SourcePos{1, 1}, SourcePos{1, 5}};
    inserted.propertySources["text"] =
        DesignSourceSpan{SourcePos{1, 2}, SourcePos{1, 4}};
    DesignNodeId insertedId = 0;
    REQUIRE(editor.insertChild(1, 0, std::move(inserted), &insertedId));
    REQUIRE(insertedId != 0);
    CHECK(editor.document().root.children.front().id == insertedId);
    const auto sourceMap = DesignSourceMap::fromDocument(editor.document());
    CHECK_FALSE(sourceMap.nodeSpan(insertedId).has_value());
    CHECK_FALSE(sourceMap.propertySpan(insertedId, "text").has_value());
    CHECK(validateDesignDocument(editor.document()).empty());
}

TEST_CASE("designer duplicate nodes do not reuse source spans",
          "[designer][p4]") {
    const auto parsed = parseLumenSource(
        "page mapping { Column { Text(\"Original\", key: \"original\") } }");
    REQUIRE(parsed.ok());
    const auto originalId = parsed.document.root.children.front().id;
    REQUIRE(parsed.document.root.children.front().source.has_value());

    DesignDocumentEditor editor{parsed.document};
    const auto duplicate = editor.duplicateNode(
        originalId, editor.document().root.id, 1);
    REQUIRE(duplicate.has_value());
    REQUIRE(editor.document().root.children.size() == 2);
    CHECK(editor.document().root.children.front().id == originalId);
    CHECK(editor.document().root.children[1].id == *duplicate);

    const auto sourceMap = DesignSourceMap::fromDocument(editor.document());
    CHECK(sourceMap.nodeSpan(originalId).has_value());
    CHECK_FALSE(sourceMap.nodeSpan(*duplicate).has_value());
    CHECK_FALSE(sourceMap.propertySpan(*duplicate, "text").has_value());
}

TEST_CASE("designer property edits validate persistence and references",
          "[designer][f6][property]") {
    const auto parsed = parseLumenSource(
        "page mapping { Button(\"Save\", onClick: save, key: \"save\") }");
    REQUIRE(parsed.ok());

    DesignDocumentEditor editor{parsed.document};
    auto& button = editor.document().root;
    REQUIRE(button.propertySources.contains("text"));
    CHECK(editor.setProperty(
        button.id, "text", DesignValue{DesignValue::Variant{std::string{"Apply"}}}));
    CHECK(std::get<std::string>(button.properties.at("text").value) ==
          "Apply");
    CHECK_FALSE(button.propertySources.contains("text"));

    CHECK_FALSE(editor.setProperty(
        button.id, "variant",
        DesignValue{DesignValue::Variant{std::string{"tonal"}}}));
    CHECK(editor.setReference(button.id, "onClick", "saveAgain"));
    CHECK(button.references.at("onClick") == "saveAgain");
    CHECK_FALSE(button.properties.contains("onClick"));
    CHECK_FALSE(editor.setReference(button.id, "onClick", "bad-name"));
    CHECK(button.references.at("onClick") == "saveAgain");
    CHECK(editor.clearReference(button.id, "onClick"));
    CHECK(button.references.empty());
    CHECK(editor.clearProperty(button.id, "text"));
    CHECK_FALSE(button.properties.contains("text"));
    CHECK(validateDesignDocument(editor.document()).empty());

    DesignDocument previewOnly;
    previewOnly.root = DesignNode{1, "ScrollView"};
    DesignDocumentEditor previewEditor{previewOnly};
    CHECK_FALSE(previewEditor.setProperty(
        1, "scrollOffset", DesignValue{DesignValue::Variant{5.0}}));
}
