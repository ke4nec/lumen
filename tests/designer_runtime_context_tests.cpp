#include <catch2/catch_test_macros.hpp>

#include "lumen/dsl/design_codec.h"

using lumen::dsl::DesignReferenceKind;
using lumen::dsl::MapDesignRuntimeContext;
using lumen::dsl::compileDesignDocument;
using lumen::dsl::parseLumenSource;

TEST_CASE("designer preview context resolves typed binding and handler names",
          "[designer][p3]") {
    const auto parsed = parseLumenSource(
        "page preview { Button(\"Save\", bind: enabled, onClick: save) }");
    REQUIRE(parsed.ok());
    MapDesignRuntimeContext context;
    context.registerReference(DesignReferenceKind::Binding, "enabled");
    context.registerReference(DesignReferenceKind::Handler, "save");
    const auto compiled = compileDesignDocument(parsed.document, context);
    REQUIRE(compiled.ok());
    REQUIRE(compiled.session);
    CHECK(compiled.session->active());
    CHECK(compiled.session->generation() != 0);
    compiled.session->close();
    CHECK_FALSE(compiled.session->active());
}

TEST_CASE("designer preview context reports missing references but keeps a frame",
          "[designer][p3]") {
    const auto parsed = parseLumenSource(
        "page preview { Button(\"Save\", bind: missing, onClick: save) }");
    REQUIRE(parsed.ok());
    MapDesignRuntimeContext context;
    context.registerReference(DesignReferenceKind::Handler, "save");
    const auto compiled = compileDesignDocument(parsed.document, context);
    REQUIRE_FALSE(compiled.ok());
    REQUIRE(compiled.root.type == lumen::core::WidgetType::Button);
    REQUIRE(compiled.session);
    REQUIRE(compiled.diagnostics.size() == 1);
    CHECK(compiled.diagnostics.front().code == "reference.missing");
    CHECK(compiled.diagnostics.front().property == "bind");
}
