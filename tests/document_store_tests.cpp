#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <unistd.h>

#include "lumen/dsl/document_store.h"

namespace fs = std::filesystem;
using lumen::dsl::DesignDocument;
using lumen::dsl::DocumentStore;
using lumen::dsl::parseLumenSource;
using lumen::dsl::serializeDesignDocument;

namespace {

fs::path tempPath(const char* tag) {
    return fs::temp_directory_path() /
           ("lumen-design-store-" + std::string(tag) + "-" +
            std::to_string(::getpid()));
}

DesignDocument sampleDocument() {
    const auto parsed = parseLumenSource("page store { Row { Text(\"saved\") } }");
    return parsed.ok() ? parsed.document : DesignDocument{};
}

std::string legacyVersionZero(const DesignDocument& document) {
    std::string encoded = serializeDesignDocument(document);
    const std::string current = "\"schemaVersion\":1";
    const auto at = encoded.find(current);
    if (at == std::string::npos) return {};
    encoded.replace(at, current.size(), "\"schemaVersion\":0");
    return encoded;
}

}  // namespace

TEST_CASE("document store saves, loads, migrates, and preserves extensions",
          "[designer][p5]") {
    const fs::path dir = tempPath("roundtrip");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "page.design";

    auto document = sampleDocument();
    document.unknownFields["future"] = "{\"enabled\":true}";
    DocumentStore store;
    std::vector<lumen::dsl::DesignError> diagnostics;
    REQUIRE(store.save(path.string(), document, diagnostics));
    CHECK(diagnostics.empty());
    CHECK_FALSE(fs::exists(DocumentStore::backupPath(path.string())));

    const auto loaded = store.load(path.string());
    REQUIRE(loaded.ok());
    CHECK_FALSE(loaded.recovered);
    CHECK_FALSE(loaded.migrated);
    CHECK(loaded.document == document);
    CHECK(loaded.revision != 0);

    auto external = document;
    external.pageName = "external-edit";
    std::ofstream(path, std::ios::trunc) << serializeDesignDocument(external);
    CHECK_FALSE(store.save(path.string(), document, diagnostics,
                           loaded.revision));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == "store.revision_conflict");
    const auto afterConflict = store.load(path.string());
    REQUIRE(afterConflict.ok());
    CHECK(afterConflict.document.pageName == "external-edit");

    const fs::path legacy = dir / "legacy.design";
    std::ofstream(legacy) << legacyVersionZero(document);
    const auto migrated = store.load(legacy.string());
    REQUIRE(migrated.ok());
    CHECK_FALSE(migrated.recovered);
    CHECK(migrated.migrated);
    CHECK(migrated.document.schemaVersion == 1);
    CHECK(migrated.document.unknownFields == document.unknownFields);
}

TEST_CASE("document store recovers the last valid backup after corruption",
          "[designer][p5]") {
    const fs::path dir = tempPath("recovery");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "page.design";

    auto first = sampleDocument();
    first.root.properties["key"] =
        lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{"first"}};
    auto second = first;
    second.root.properties["key"] =
        lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{"second"}};
    DocumentStore store;
    std::vector<lumen::dsl::DesignError> diagnostics;
    REQUIRE(store.save(path.string(), first, diagnostics));
    REQUIRE(store.save(path.string(), second, diagnostics));
    REQUIRE(fs::exists(DocumentStore::backupPath(path.string())));

    std::ofstream(path, std::ios::trunc) << "{\"truncated\":";
    const auto recovered = store.load(path.string());
    REQUIRE(recovered.ok());
    REQUIRE(recovered.recovered);
    REQUIRE_FALSE(recovered.diagnostics.empty());
    CHECK(recovered.diagnostics.front().code.rfind("codec.", 0) == 0);
    CHECK(recovered.document.root.properties.at("key") == first.root.properties.at("key"));

    const fs::path noBackup = dir / "no-backup.design";
    std::ofstream(noBackup) << "broken";
    const auto failed = store.load(noBackup.string());
    CHECK_FALSE(failed.ok());
    CHECK_FALSE(failed.recovered);
    CHECK(failed.document.root.id == 0);
}

TEST_CASE("document store rejects invalid saves without touching the file",
          "[designer][p5]") {
    const fs::path dir = tempPath("validation");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "page.design";
    DocumentStore store;
    std::vector<lumen::dsl::DesignError> diagnostics;
    const auto valid = sampleDocument();
    REQUIRE(store.save(path.string(), valid, diagnostics));

    auto invalid = valid;
    invalid.root.type = "UnknownNode";
    CHECK_FALSE(store.save(path.string(), invalid, diagnostics));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == "schema.unknown_node");
    const auto stillValid = store.load(path.string());
    REQUIRE(stillValid.ok());
    CHECK(stillValid.document == valid);

    auto unsupported = valid;
    unsupported.schemaVersion = 2;
    CHECK_FALSE(store.save(path.string(), unsupported, diagnostics));
    CHECK(diagnostics.front().code == "store.schema_version");
}

TEST_CASE("document store migration failure blocks publication",
          "[designer][p5]") {
    const fs::path dir = tempPath("migration");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "legacy.design";
    const auto document = sampleDocument();
    std::ofstream(path) << legacyVersionZero(document);

    DocumentStore store;
    store.registerMigration(0, [](DesignDocument&,
                                  std::vector<lumen::dsl::DesignError>& diagnostics) {
        diagnostics.push_back(lumen::dsl::DesignError{
            "store.migration_failed", "<design>", {}, "injected failure", {},
            {}, 0, {}, {}});
        return false;
    });
    const auto result = store.load(path.string());
    CHECK_FALSE(result.ok());
    CHECK(result.document.root.id == 0);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == "store.migration_failed");
}
