#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "lumen/dsl/document_store.h"

namespace fs = std::filesystem;
using lumen::dsl::DesignDocument;
using lumen::dsl::DocumentStore;
using lumen::dsl::parseLumenSource;
using lumen::dsl::serializeDesignDocument;

namespace {

std::uint64_t processId() {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

fs::path tempPath(const char* tag) {
    return fs::temp_directory_path() /
           ("lumen-design-store-" + std::string(tag) + "-" +
            std::to_string(processId()));
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

    auto semanticallyBroken = second;
    semanticallyBroken.root.type = "UnknownNode";
    std::ofstream(path, std::ios::trunc) <<
        serializeDesignDocument(semanticallyBroken);
    const auto schemaRecovered = store.load(path.string());
    REQUIRE(schemaRecovered.ok());
    REQUIRE(schemaRecovered.recovered);
    CHECK(schemaRecovered.document.root.properties.at("key") ==
          first.root.properties.at("key"));

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

TEST_CASE("document store preserves the valid backup after saving recovery",
          "[designer][p5]") {
    const fs::path dir = tempPath("recovery-save");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "page.design";

    auto first = sampleDocument();
    first.pageName = "first";
    auto second = first;
    second.pageName = "second";
    DocumentStore store;
    std::vector<lumen::dsl::DesignError> diagnostics;
    REQUIRE(store.save(path.string(), first, diagnostics));
    REQUIRE(store.save(path.string(), second, diagnostics));

    std::ofstream(path, std::ios::trunc) << "{\"truncated\":";
    const auto recovered = store.load(path.string());
    REQUIRE(recovered.ok());
    REQUIRE(recovered.recovered);
    CHECK(recovered.document.pageName == "first");

    auto replacement = recovered.document;
    replacement.pageName = "recovered-edit";
    REQUIRE(store.save(path.string(), replacement, diagnostics,
                       recovered.revision));
    const auto saved = store.load(path.string());
    REQUIRE(saved.ok());
    CHECK_FALSE(saved.recovered);
    CHECK(saved.document.pageName == "recovered-edit");

    std::ofstream(path, std::ios::trunc) << "{\"truncated\":";
    const auto recoveredAgain = store.load(path.string());
    REQUIRE(recoveredAgain.ok());
    REQUIRE(recoveredAgain.recovered);
    CHECK(recoveredAgain.document.pageName == "first");
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

    auto duplicateIds = valid;
    duplicateIds.root.children.front().id = duplicateIds.root.id;
    CHECK_FALSE(store.save(path.string(), duplicateIds, diagnostics));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == "schema.duplicate_node_id");
    const auto afterDuplicate = store.load(path.string());
    REQUIRE(afterDuplicate.ok());
    CHECK(afterDuplicate.document == valid);

    auto missingIdentity = valid;
    missingIdentity.documentId.clear();
    CHECK_FALSE(store.save(path.string(), missingIdentity, diagnostics));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == "store.document_id");
    const auto afterMissingIdentity = store.load(path.string());
    REQUIRE(afterMissingIdentity.ok());
    CHECK(afterMissingIdentity.document == valid);

    auto invalidExtension = valid;
    invalidExtension.unknownFields["alsoFuture"] = "[";
    invalidExtension.unknownFields["future"] = "{\"enabled\":";
    CHECK_FALSE(store.save(path.string(), invalidExtension, diagnostics));
    REQUIRE(diagnostics.size() == 2);
    CHECK(diagnostics[0].code == "schema.invalid_unknown_field");
    CHECK(diagnostics[0].property == "alsoFuture");
    CHECK(diagnostics[1].code == "schema.invalid_unknown_field");
    CHECK(diagnostics[1].property == "future");
    const auto afterInvalidExtension = store.load(path.string());
    REQUIRE(afterInvalidExtension.ok());
    CHECK(afterInvalidExtension.document == valid);
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

TEST_CASE("document store converts throwing migrations to diagnostics",
          "[designer][p5]") {
    const fs::path dir = tempPath("migration-exception");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "legacy.design";
    const auto document = sampleDocument();
    const auto source = legacyVersionZero(document);
    std::ofstream(path) << source;

    DocumentStore store;
    store.registerMigration(0, [](DesignDocument&,
                                  std::vector<lumen::dsl::DesignError>&) -> bool {
        throw std::runtime_error("injected migration exception");
    });
    const auto result = store.load(path.string());
    CHECK_FALSE(result.ok());
    CHECK(result.document.root.id == 0);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == "store.migration_exception");
    CHECK(result.diagnostics.front().message.find("injected migration exception") !=
          std::string::npos);

    std::ifstream input(path);
    const std::string unchanged((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
    CHECK(unchanged == source);
}

TEST_CASE("document store rejects migrations that remove document identity",
          "[designer][p5]") {
    const fs::path dir = tempPath("migration-document-id");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "legacy.design";
    const auto document = sampleDocument();
    std::ofstream(path) << legacyVersionZero(document);

    DocumentStore store;
    store.registerMigration(0, [](DesignDocument& migrated,
                                  std::vector<lumen::dsl::DesignError>&) {
        migrated.documentId.clear();
        migrated.schemaVersion = 1;
        return true;
    });
    const auto result = store.load(path.string());
    CHECK_FALSE(result.ok());
    CHECK(result.document.root.id == 0);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == "store.document_id");
}

TEST_CASE("document store temporary names do not reuse legacy process-local paths",
          "[designer][p5]") {
    const fs::path dir = tempPath("temporary-name");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "page.design";

    for (int serial = 1; serial <= 128; ++serial) {
        std::ofstream(path.string() + ".tmp-" + std::to_string(serial))
            << "stale-" << serial;
        std::ofstream(path.string() + ".tmp-" + std::to_string(processId()) +
                      "-" + std::to_string(serial))
            << "stale-process-" << serial;
    }

    DocumentStore store;
    std::vector<lumen::dsl::DesignError> diagnostics;
    REQUIRE(store.save(path.string(), sampleDocument(), diagnostics));

    for (int serial = 1; serial <= 128; ++serial) {
        std::ifstream input(path.string() + ".tmp-" + std::to_string(serial));
        REQUIRE(input);
        const std::string contents((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
        CHECK(contents == "stale-" + std::to_string(serial));

        std::ifstream processInput(
            path.string() + ".tmp-" + std::to_string(processId()) + "-" +
            std::to_string(serial));
        REQUIRE(processInput);
        const std::string processContents(
            (std::istreambuf_iterator<char>(processInput)),
            std::istreambuf_iterator<char>());
        CHECK(processContents == "stale-process-" + std::to_string(serial));
    }
}

TEST_CASE("document store backup failure preserves the primary document",
          "[designer][p5]") {
    const fs::path dir = tempPath("backup-failure");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path path = dir / "page.design";

    const auto original = sampleDocument();
    std::ofstream(path) << serializeDesignDocument(original);
    REQUIRE(fs::create_directory(DocumentStore::backupPath(path.string()), ec));

    auto replacement = original;
    replacement.pageName = "replacement";
    DocumentStore store;
    std::vector<lumen::dsl::DesignError> diagnostics;
    CHECK_FALSE(store.save(path.string(), replacement, diagnostics));
    REQUIRE_FALSE(diagnostics.empty());
    CHECK(diagnostics.front().code == "store.backup");

    const auto loaded = store.load(path.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.document == original);

    bool temporaryFound = false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().rfind("page.design.tmp-", 0) ==
            0) {
            temporaryFound = true;
        }
    }
    CHECK_FALSE(temporaryFound);
}
