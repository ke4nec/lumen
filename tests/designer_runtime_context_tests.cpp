#include <catch2/catch_test_macros.hpp>

#include <memory>
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

class LeasedContext final : public DesignRuntimeContext {
  public:
    explicit LeasedContext(std::shared_ptr<int> lease)
        : lease_(std::move(lease)) {}

    [[nodiscard]] bool validatesReferences() const override { return true; }

    [[nodiscard]] bool resolveReference(DesignReferenceKind kind,
                                        const std::string& name,
                                        DesignReference& out) const override {
        out = DesignReference{kind, name, lease_};
        return true;
    }

  private:
    std::shared_ptr<int> lease_{};
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
    CHECK_FALSE(compiled.root.enabled);
    CHECK(compiled.root.invalid);
    CHECK(compiled.root.bind.empty());
    CHECK(compiled.root.onClick.empty());
    REQUIRE(compiled.session);
    REQUIRE(compiled.diagnostics.size() == 1);
    CHECK(compiled.diagnostics.front().code == "reference.missing");
    CHECK(compiled.diagnostics.front().property == "bind");
    CHECK(compiled.diagnostics.front().pos ==
          parsed.document.root.propertySources.at("bind").begin);
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
    CHECK_FALSE(compiled.root.enabled);
    CHECK(compiled.root.invalid);
}

TEST_CASE("designer unresolved references disable only their source node",
          "[designer][p3]") {
    const auto parsed = parseLumenSource(
        "page preview { Column { Button(\"Broken\", bind: missing) "
        "Button(\"Works\", onClick: save) } }");
    REQUIRE(parsed.ok());

    MapDesignRuntimeContext context;
    context.registerReference(DesignReferenceKind::Handler, "save");
    const auto compiled = compileDesignDocument(parsed.document, context);
    REQUIRE_FALSE(compiled.ok());
    REQUIRE(compiled.root.children.size() == 2);
    CHECK_FALSE(compiled.root.children[0].enabled);
    CHECK(compiled.root.children[0].invalid);
    CHECK(compiled.root.children[0].bind.empty());
    CHECK(compiled.root.children[0].onClick.empty());
    CHECK(compiled.root.children[1].enabled);
    CHECK_FALSE(compiled.root.children[1].invalid);
    CHECK(compiled.root.children[1].onClick == "save");
}

TEST_CASE("designer runtime session cancels close callbacks exactly once",
          "[designer][p3]") {
    lumen::dsl::DesignRuntimeSession session;
    int cancelled = 0;
    session.onClose([&cancelled] { ++cancelled; });

    session.close();
    session.close();
    CHECK(cancelled == 1);
    CHECK_FALSE(session.active());

    bool lateCallbackCalled = false;
    session.onClose([&lateCallbackCalled] { lateCallbackCalled = true; });
    CHECK(lateCallbackCalled);
    CHECK(cancelled == 1);
}

TEST_CASE("designer preview session retains resolved reference leases",
          "[designer][p3]") {
    const auto parsed = parseLumenSource(
        "page preview { Button(\"Save\", bind: enabled) }");
    REQUIRE(parsed.ok());
    auto lease = std::make_shared<int>(42);
    const std::weak_ptr<int> weakLease = lease;
    const auto compiled = [&] {
        LeasedContext context{lease};
        return compileDesignDocument(parsed.document, context);
    }();
    REQUIRE(compiled.ok());
    REQUIRE(compiled.session);
    CHECK(compiled.session->leaseCount() == 1);
    lease.reset();
    CHECK_FALSE(weakLease.expired());

    compiled.session->close();
    CHECK(compiled.session->leaseCount() == 0);
    CHECK(weakLease.expired());
}
