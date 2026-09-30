#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lumen/dsl/design_codec.h"

using lumen::dsl::DesignReferenceKind;
using lumen::dsl::DesignReference;
using lumen::dsl::DesignRuntimeContext;
using lumen::dsl::MapDesignRuntimeContext;
using lumen::dsl::compileDesignDocument;
using lumen::dsl::parseLumenSource;

namespace {

class WrongTypeContext final : public DesignRuntimeContext {
  public:
    [[nodiscard]] bool validatesReferences() const override { return true; }

    [[nodiscard]] bool resolveReference(DesignReferenceKind kind,
                                        const std::string& name,
                                        DesignReference& out) const override {
        out = DesignReference{kind == DesignReferenceKind::Binding
                                  ? DesignReferenceKind::Handler
                                  : kind,
                              name};
        return true;
    }
};

}  // namespace

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

TEST_CASE("designer preview context rejects a wrong typed reference handle",
          "[designer][p3]") {
    const auto parsed = parseLumenSource(
        "page preview { TextField(bind: enabled) }");
    REQUIRE(parsed.ok());
    WrongTypeContext context;
    const auto compiled = compileDesignDocument(parsed.document, context);
    REQUIRE_FALSE(compiled.ok());
    REQUIRE(compiled.diagnostics.size() == 1);
    CHECK(compiled.diagnostics.front().code == "reference.type");
    CHECK(compiled.diagnostics.front().property == "bind");
}
