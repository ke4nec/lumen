#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "lumen/dsl/project_store.h"

namespace fs = std::filesystem;
using lumen::dsl::DesignError;
using lumen::dsl::DesignProject;
using lumen::dsl::DesignProjectPage;
using lumen::dsl::DesignProjectReference;
using lumen::dsl::DesignProjectResource;
using lumen::dsl::ProjectStore;
using lumen::dsl::serializeDesignProject;

namespace {

fs::path projectPath(const char* name) {
    const auto base = fs::temp_directory_path() /
                      (std::string{"lumen-project-store-"} + name);
    std::error_code error;
    fs::remove_all(base, error);
    fs::create_directories(base, error);
    return base / "sample.lumen-project";
}

DesignProject sampleProject() {
    DesignProject project;
    project.projectId = "project.demo";
    project.name = "Demo project";
    project.root = ".";
    project.pages = {
        DesignProjectPage{"home", "pages/home.design", "Home"},
        DesignProjectPage{"settings", "pages/settings.design", "Settings"},
    };
    project.resources = {
        DesignProjectResource{"project://images/logo.png", "images/logo.png",
                              "image"},
    };
    project.references = {
        DesignProjectReference{"home", 10, "settingsLink", "settings", 20},
    };
    project.unknownFields["future"] = "{\"flag\":true}";
    return project;
}

}  // namespace

TEST_CASE("project store round trips manifest and unknown fields",
          "[designer][dp9]") {
    const auto path = projectPath("roundtrip");
    const auto project = sampleProject();
    ProjectStore store;
    std::vector<DesignError> diagnostics;
    REQUIRE(store.save(path.string(), project, diagnostics));
    CHECK(diagnostics.empty());

    const auto loaded = store.load(path.string());
    REQUIRE(loaded.ok());
    CHECK_FALSE(loaded.recovered);
    CHECK_FALSE(loaded.migrated);
    CHECK(loaded.revision != 0);
    CHECK(loaded.project == project);

    const auto encoded = serializeDesignProject(project);
    CHECK(encoded.find("lumen.project") != std::string::npos);
    CHECK(encoded.find("settingsLink") != std::string::npos);
}

TEST_CASE("project store migrates version zero without rewriting source",
          "[designer][dp9]") {
    const auto path = projectPath("migration");
    const auto project = sampleProject();
    auto source = serializeDesignProject(project);
    const auto marker = std::string{"\"schemaVersion\":1"};
    REQUIRE(source.find(marker) != std::string::npos);
    source.replace(source.find(marker), marker.size(), "\"schemaVersion\":0");
    std::ofstream(path) << source;

    ProjectStore store;
    const auto loaded = store.load(path.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.migrated);
    CHECK(loaded.project.schemaVersion == 1);
    CHECK(loaded.project == project);

    std::ifstream input(path);
    CHECK(std::string{std::istreambuf_iterator<char>{input},
                      std::istreambuf_iterator<char>()} == source);
}

TEST_CASE("project store migration diagnostics distinguish primary and recovery files",
          "[designer][dp9][diagnostic-stage]") {
    const auto path = projectPath("migration-origins");
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(dir, error);
        }
    } cleanup{path.parent_path()};
    const auto backup = ProjectStore::backupPath(path.string());
    auto source = serializeDesignProject(sampleProject());
    const auto marker = std::string{"\"schemaVersion\":1"};
    REQUIRE(source.find(marker) != std::string::npos);
    source.replace(source.find(marker), marker.size(), "\"schemaVersion\":0");
    std::ofstream(path) << source;
    std::ofstream(backup) << source;
    ProjectStore store;
    store.registerMigration(0, [](DesignProject&,
                                 std::vector<DesignError>& diagnostics) {
        diagnostics.push_back({"project.migration_failed", "<project>", {},
                               "injected failure"});
        return false;
    });
    const auto result = store.load(path.string());
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.diagnostics.size() == 2);
    CHECK(result.diagnostics[0].file == path.string());
    CHECK(result.diagnostics[1].file == backup);
    CHECK(result.diagnostics[0].code == "project.migration_failed");
    CHECK(result.diagnostics[1].code == "project.migration_failed");
}

TEST_CASE("project store isolates revisions and validates cross document refs",
          "[designer][dp9]") {
    const auto path = projectPath("validation");
    ProjectStore store;
    auto project = sampleProject();
    std::vector<DesignError> diagnostics;
    REQUIRE(store.save(path.string(), project, diagnostics));
    const auto loaded = store.load(path.string());

    auto external = project;
    external.name = "external";
    std::ofstream(path, std::ios::trunc) << serializeDesignProject(external);
    CHECK_FALSE(store.save(path.string(), project, diagnostics, loaded.revision));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == "project.revision_conflict");

    auto invalid = project;
    invalid.references.front().toDocumentId = "missing";
    CHECK_FALSE(store.save(path.string(), invalid, diagnostics));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == "project.reference");
}

TEST_CASE("project store recovers a valid backup after a truncated manifest",
          "[designer][dp9]") {
    const auto path = projectPath("recovery");
    ProjectStore store;
    auto first = sampleProject();
    std::vector<DesignError> diagnostics;
    REQUIRE(store.save(path.string(), first, diagnostics));
    first.name = "second";
    REQUIRE(store.save(path.string(), first, diagnostics));
    REQUIRE(fs::exists(ProjectStore::backupPath(path.string())));

    std::ofstream(path, std::ios::trunc) << "{\"format\":\"lumen.project\"";
    const auto recovered = store.load(path.string());
    REQUIRE(recovered.ok());
    CHECK(recovered.recovered);
    CHECK(recovered.project.name == "Demo project");
    REQUIRE_FALSE(recovered.diagnostics.empty());
}
