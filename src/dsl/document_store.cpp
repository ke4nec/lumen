#include "lumen/dsl/document_store.h"
#include "lumen/dsl/design_schema.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lumen::dsl {
namespace {

constexpr std::uint32_t kCurrentSchemaVersion = 1;
std::atomic<std::uint64_t> gTemporaryFileId{1};

[[nodiscard]] std::uint64_t revisionOf(const std::string& content) {
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffset;
    for (const unsigned char byte : content) {
        hash ^= byte;
        hash *= kPrime;
    }
    return hash;
}

[[nodiscard]] std::optional<std::uint64_t> fileRevision(
    const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    if (input.bad()) return std::nullopt;
    return revisionOf(source);
}

bool atomicReplace(const std::string& temporary, const std::string& path,
                   std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(std::filesystem::path(temporary).wstring().c_str(),
                    std::filesystem::path(path).wstring().c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        error.clear();
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()),
                            std::system_category());
    return false;
#else
    std::filesystem::rename(temporary, path, error);
    return !error;
#endif
}

[[nodiscard]] std::string temporaryPath(const std::string& path) {
    const auto serial = gTemporaryFileId.fetch_add(1);
    return path + ".tmp-" + std::to_string(serial);
}

[[nodiscard]] DesignError storeError(const std::string& code,
                                     const std::string& file,
                                     const std::string& message) {
    return DesignError{code, file, SourcePos{}, message, {}, {}, 0, {}, {}};
}

[[nodiscard]] DesignReadResult readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return DesignReadResult{
            DesignDocument{},
            storeError("store.read", path, "unable to open document")};
    }
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    if (input.bad()) {
        return DesignReadResult{
            DesignDocument{},
            storeError("store.read", path, "unable to read document")};
    }
    return readDesignDocument(source, path);
}

}  // namespace

DocumentStore::DocumentStore() {
    migrations_[0] = [](DesignDocument& document,
                         std::vector<DesignError>&) {
        // Version 0 was the initial codec shape. Missing source maps and
        // extension containers already have their value defaults in v1.
        document.schemaVersion = kCurrentSchemaVersion;
        return true;
    };
}

void DocumentStore::registerMigration(std::uint32_t sourceVersion,
                                      Migration migration) {
    if (migration) migrations_[sourceVersion] = std::move(migration);
}

std::string DocumentStore::backupPath(const std::string& path) {
    return path + ".bak";
}

DesignError DocumentStore::errorAt(const std::string& code,
                                   const std::string& file,
                                   const std::string& message) {
    return storeError(code, file, message);
}

bool DocumentStore::migrate(DesignDocument& document,
                            std::vector<DesignError>& diagnostics) const {
    while (document.schemaVersion < kCurrentSchemaVersion) {
        const auto found = migrations_.find(document.schemaVersion);
        if (found == migrations_.end()) {
            diagnostics.push_back(errorAt(
                "store.migration_missing", "<design>",
                "no migration is registered for schemaVersion " +
                    std::to_string(document.schemaVersion)));
            return false;
        }
        const std::uint32_t sourceVersion = document.schemaVersion;
        if (!found->second(document, diagnostics)) return false;
        if (document.schemaVersion != sourceVersion + 1) {
            diagnostics.push_back(errorAt(
                "store.migration_version", "<design>",
                "migration must advance schemaVersion by exactly one"));
            return false;
        }
    }
    if (document.schemaVersion > kCurrentSchemaVersion) {
        diagnostics.push_back(errorAt("store.schema_version", "<design>",
                                      "document schemaVersion is newer than this store"));
        return false;
    }
    return true;
}

DocumentLoadResult DocumentStore::load(const std::string& path) const {
    namespace fs = std::filesystem;
    const auto primary = readFile(path);
    if (primary.ok()) {
        DocumentLoadResult result;
        result.document = primary.document;
        result.revision = fileRevision(path).value_or(0);
        result.migrated = result.document.schemaVersion != kCurrentSchemaVersion;
        if (!migrate(result.document, result.diagnostics)) {
            result.document = {};
            return result;
        }
        const auto schemaDiagnostics = validateDesignDocument(result.document);
        result.diagnostics.insert(result.diagnostics.end(),
                                  schemaDiagnostics.begin(),
                                  schemaDiagnostics.end());
        if (!schemaDiagnostics.empty()) result.document = {};
        return result;
    }

    const std::string backup = backupPath(path);
    std::error_code existsError;
    if (!fs::exists(backup, existsError) || existsError) {
        return DocumentLoadResult{DesignDocument{}, {*primary.error}, false, false};
    }
    const auto recovered = readFile(backup);
    if (!recovered.ok()) {
        return DocumentLoadResult{
            DesignDocument{}, {*primary.error, *recovered.error}, false, false};
    }
    DocumentLoadResult result;
    result.document = recovered.document;
    result.recovered = true;
    result.revision = fileRevision(path).value_or(0);
    result.diagnostics.push_back(*primary.error);
    result.migrated = result.document.schemaVersion != kCurrentSchemaVersion;
    if (!migrate(result.document, result.diagnostics)) {
        result.document = {};
        return result;
    }
    const auto schemaDiagnostics = validateDesignDocument(result.document);
    result.diagnostics.insert(result.diagnostics.end(), schemaDiagnostics.begin(),
                              schemaDiagnostics.end());
    if (!schemaDiagnostics.empty()) result.document = {};
    return result;
}

bool DocumentStore::save(const std::string& path,
                         const DesignDocument& document,
                         std::vector<DesignError>& diagnostics,
                         std::optional<std::uint64_t> expectedRevision) const {
    diagnostics.clear();
    if (document.schemaVersion != kCurrentSchemaVersion) {
        diagnostics.push_back(errorAt(
            "store.schema_version", path,
            "only the current schemaVersion can be saved"));
        return false;
    }
    diagnostics = validateDesignDocument(document);
    if (!diagnostics.empty()) return false;

    namespace fs = std::filesystem;
    if (expectedRevision.has_value()) {
        const auto currentRevision = fileRevision(path);
        const bool fileExists = currentRevision.has_value();
        const bool matches = expectedRevision.value() ==
                             (fileExists ? *currentRevision : 0);
        if (!matches) {
            diagnostics.push_back(errorAt(
                "store.revision_conflict", path,
                "document changed after it was loaded"));
            return false;
        }
    }
    const std::string temporary = temporaryPath(path);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            diagnostics.push_back(
                errorAt("store.write", path, "unable to open temporary document"));
            return false;
        }
        const std::string encoded = serializeDesignDocument(document);
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.flush();
        if (!output) {
            output.close();
            std::error_code cleanupError;
            fs::remove(temporary, cleanupError);
            diagnostics.push_back(
                errorAt("store.write", path, "unable to write temporary document"));
            return false;
        }
    }

    std::error_code ec;
    const bool hasPrimary = fs::exists(path, ec);
    if (ec) {
        std::error_code cleanupError;
        fs::remove(temporary, cleanupError);
        diagnostics.push_back(
            errorAt("store.backup", path, "unable to inspect document"));
        return false;
    }
    if (hasPrimary) {
        if (!fs::copy_file(path, backupPath(path),
                           fs::copy_options::overwrite_existing, ec) || ec) {
            std::error_code cleanupError;
            fs::remove(temporary, cleanupError);
            diagnostics.push_back(
                errorAt("store.backup", path, "unable to create recovery copy"));
            return false;
        }
    }
    ec.clear();
    if (!atomicReplace(temporary, path, ec)) {
        std::error_code cleanupError;
        fs::remove(temporary, cleanupError);
        diagnostics.push_back(
            errorAt("store.rename", path, "unable to atomically replace document"));
        return false;
    }
    return true;
}

}  // namespace lumen::dsl
