#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "lumen/dsl/design_resources.h"

using lumen::dsl::DesignDiagnostic;
using lumen::dsl::DesignDiagnosticRecoverability;
using lumen::dsl::DesignResourceAuthorizer;
using lumen::dsl::DesignResourceDiagnosticContext;
using lumen::dsl::DesignResourceKind;
using lumen::dsl::DesignResourcePolicy;
using lumen::dsl::DesignRuntimeSession;
using lumen::dsl::DesignPreviewGeneration;

namespace {

std::filesystem::path resourceRoot() {
    const auto serial = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-resource-fixture-" +
                       std::to_string(serial));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "images", error);
    std::ofstream(root / "images" / "icon.png") << "fixture";
    return root;
}

}  // namespace

TEST_CASE("designer resource policy returns stable authorized references",
          "[designer][f6][resource]") {
    const auto root = resourceRoot();
    DesignResourcePolicy policy;
    policy.allowRoot("PROJECT", root);
    policy.allowKind(DesignResourceKind::Image);
    DesignResourceAuthorizer authorizer{policy};
    std::vector<DesignDiagnostic> diagnostics;
    const auto reference = authorizer.authorize(
        DesignResourceKind::Image, "project://images/./icon.png",
        DesignResourceDiagnosticContext{"page.design", "doc-1", 2, ".root",
                                        "icon"},
        diagnostics);
    REQUIRE(reference.has_value());
    CHECK(reference->uri() == "project://images/icon.png");
    CHECK(reference->relativePath == "images/icon.png");
    CHECK(diagnostics.empty());
}

TEST_CASE("designer resource policy rejects capabilities schemes and escapes",
          "[designer][f6][resource]") {
    const auto root = resourceRoot();
    DesignResourcePolicy policy;
    policy.allowRoot("project", root);
    policy.allowKind(DesignResourceKind::Image);
    DesignResourceAuthorizer authorizer{policy};
    const DesignResourceDiagnosticContext context{"page.design", "doc-1", 2,
                                                 ".root", "source"};
    std::vector<DesignDiagnostic> diagnostics;
    CHECK_FALSE(authorizer.authorize(DesignResourceKind::Font,
                                     "project://images/icon.png", context,
                                     diagnostics));
    CHECK(diagnostics.back().code == "resource.capability_denied");
    CHECK(diagnostics.back().recoverability ==
          DesignDiagnosticRecoverability::Placeholder);
    CHECK_FALSE(authorizer.authorize(DesignResourceKind::Image,
                                     "https://example.test/image.png", context,
                                     diagnostics));
    CHECK(diagnostics.back().code == "resource.scheme_denied");
    CHECK_FALSE(authorizer.authorize(DesignResourceKind::Image,
                                     "project://../outside.png", context,
                                     diagnostics));
    CHECK(diagnostics.back().code == "resource.path_escape");
}

TEST_CASE("designer resource policy blocks symlink escapes",
          "[designer][f6][resource]") {
    const auto root = resourceRoot();
    const auto outside = root.parent_path() / "lumen-designer-resource-outside";
    std::error_code error;
    std::filesystem::remove_all(outside, error);
    std::filesystem::create_directories(outside, error);
    std::ofstream(outside / "secret.png") << "secret";
    std::filesystem::create_symlink(outside, root / "images" / "linked", error);
    if (error) {
        SUCCEED("symlinks are unavailable on this platform");
        return;
    }

    DesignResourcePolicy policy;
    policy.allowRoot("project", root);
    policy.allowKind(DesignResourceKind::Image);
    DesignResourceAuthorizer authorizer{policy};
    std::vector<DesignDiagnostic> diagnostics;
    CHECK_FALSE(authorizer.authorize(DesignResourceKind::Image,
                                     "project://images/linked/secret.png",
                                     {}, diagnostics));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.back().code == "resource.path_escape");
}

TEST_CASE("designer preview generations discard late results and closed sessions",
          "[designer][f6][resource]") {
    DesignRuntimeSession session;
    DesignPreviewGeneration generation{"doc-1", session.generation()};
    const auto first = generation.beginCompile();
    CHECK(generation.accepts(first, session));
    const auto second = generation.beginCompile();
    CHECK_FALSE(generation.accepts(first, session));
    CHECK(generation.accepts(second, session));
    generation.invalidate();
    CHECK_FALSE(generation.accepts(second, session));
    const auto third = generation.beginCompile();
    REQUIRE(generation.accepts(third, session));
    session.close();
    CHECK_FALSE(generation.accepts(third, session));
    const auto closedGeneration = generation.compileGeneration();
    generation.close();
    generation.close();
    CHECK(generation.compileGeneration() == closedGeneration + 1);
    CHECK_FALSE(generation.accepts(generation.beginCompile(), session));
}
