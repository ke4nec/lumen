#include <catch2/catch_test_macros.hpp>

#include "lumen/dsl/design_preview_frame.h"

using lumen::dsl::DesignDiagnosticRecoverability;
using lumen::dsl::DesignPreviewFrame;
using lumen::dsl::MapDesignRuntimeContext;
using lumen::dsl::parseLumenSource;

TEST_CASE("designer preview frame keeps a placeholder for missing references",
          "[designer][f6][diagnostic]") {
    const auto parsed = parseLumenSource(
        "page preview { Button(\"Save\", bind: missing, key: \"save\") }");
    REQUIRE(parsed.ok());

    MapDesignRuntimeContext context;
    DesignPreviewFrame frame;
    CHECK_FALSE(frame.update(parsed.document, context));
    CHECK(frame.hasFrame());
    CHECK(frame.widget().type == lumen::core::WidgetType::Button);
    CHECK(frame.trace().nodes.size() == 1);
    REQUIRE(frame.diagnostics().size() == 1);
    CHECK(frame.diagnostics().front().code == "reference.missing");
    CHECK(frame.diagnostics().front().documentId == parsed.document.documentId);
    CHECK(frame.diagnostics().front().recoverability ==
          DesignDiagnosticRecoverability::Placeholder);
    REQUIRE(frame.session());
    CHECK(frame.session()->active());
}

TEST_CASE("designer preview frame preserves distinct reference locations",
          "[designer][f6][diagnostic]") {
    const auto parsed = parseLumenSource(
        "page preview { Column { Button(\"One\", bind: missing, key: \"one\") "
        "Button(\"Two\", bind: missing, key: \"two\") } }");
    REQUIRE(parsed.ok());

    MapDesignRuntimeContext context;
    DesignPreviewFrame frame;
    CHECK_FALSE(frame.update(parsed.document, context));
    REQUIRE(frame.diagnostics().size() == 2);
    CHECK(frame.diagnostics()[0].code == "reference.missing");
    CHECK(frame.diagnostics()[1].code == "reference.missing");
    CHECK(frame.diagnostics()[0].nodeId != frame.diagnostics()[1].nodeId);
    CHECK(frame.diagnostics()[0].nodePath != frame.diagnostics()[1].nodePath);
    CHECK(frame.diagnostics()[0].occurrences == 1);
    CHECK(frame.diagnostics()[1].occurrences == 1);
}

TEST_CASE("designer preview frame keeps the last good compile on failure",
          "[designer][f6][diagnostic]") {
    const auto parsed = parseLumenSource(
        "page preview { Button(\"Save\", key: \"save\") }");
    REQUIRE(parsed.ok());

    MapDesignRuntimeContext context;
    DesignPreviewFrame frame;
    REQUIRE(frame.update(parsed.document, context));
    REQUIRE(frame.hasFrame());
    const auto previousWidget = frame.widget();
    const auto previousGeneration = frame.generation();
    REQUIRE(frame.session());
    REQUIRE(frame.session()->active());

    auto broken = parsed.document;
    broken.root.type = "Unknown";
    CHECK_FALSE(frame.update(broken, context));
    CHECK(frame.hasFrame());
    CHECK(frame.widget() == previousWidget);
    CHECK(frame.generation() == previousGeneration);
    REQUIRE(frame.diagnostics().size() == 1);
    CHECK(frame.diagnostics().front().code == "compile.unknown_node");
    CHECK(frame.diagnostics().front().documentId == broken.documentId);
    CHECK(frame.diagnostics().front().recoverability ==
          DesignDiagnosticRecoverability::KeepLastFrame);
    CHECK(frame.session()->active());
}

TEST_CASE("designer preview frame closes replaced sessions and advances generation",
          "[designer][f6][diagnostic]") {
    const auto parsed = parseLumenSource(
        "page preview { Text(\"before\", key: \"label\") }");
    REQUIRE(parsed.ok());

    DesignPreviewFrame frame;
    REQUIRE(frame.update(parsed.document));
    const auto firstGeneration = frame.generation();
    const auto firstSession = frame.session();
    REQUIRE(firstSession);
    int cancelled = 0;
    firstSession->onClose([&cancelled] { ++cancelled; });

    auto changed = parsed.document;
    changed.root.properties["text"] =
        lumen::dsl::DesignValue{
            lumen::dsl::DesignValue::Variant{std::string{"after"}}};
    REQUIRE(frame.update(changed));
    CHECK(frame.generation() == firstGeneration + 1);
    CHECK_FALSE(firstSession->active());
    CHECK(cancelled == 1);
    REQUIRE(frame.session());
    CHECK(frame.session()->active());
    CHECK(frame.diagnostics().empty());
}

TEST_CASE("designer preview frame does not show another document after failure",
          "[designer][f6][diagnostic]") {
    const auto first = parseLumenSource("page first { Text(\"one\") }");
    const auto second = parseLumenSource("page second { Unknown {} }");
    REQUIRE(first.ok());
    REQUIRE_FALSE(second.ok());

    DesignPreviewFrame frame;
    auto imported = first.document;
    imported.documentId.clear();
    REQUIRE(frame.update(imported));
    auto other = imported;
    other.documentId = "other-document";
    other.root.type = "Unknown";
    CHECK_FALSE(frame.update(other));
    CHECK_FALSE(frame.hasFrame());
    CHECK_FALSE(frame.session());
    REQUIRE(frame.diagnostics().size() == 1);
    CHECK(frame.diagnostics().front().code == "compile.unknown_node");
}
