#include "lumen/dsl/project_store.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace lumen::dsl {
namespace {

constexpr std::uint32_t kCurrentProjectSchemaVersion = 1;
std::atomic<std::uint64_t> gTemporaryProjectFileId{1};

[[nodiscard]] DesignError projectError(const std::string& code,
                                       const std::string& file,
                                       const std::string& message) {
    return DesignError{code, file, SourcePos{}, message, {}, {}, 0, {}, {}};
}

struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    Kind kind{Kind::Null};
    bool boolean{false};
    double number{0.0};
    std::string string{};
    std::vector<JsonValue> array{};
    std::map<std::string, JsonValue> object{};
};

class JsonParser {
  public:
    JsonParser(const std::string& source, std::string filename)
        : source_(source), filename_(std::move(filename)) {}

    [[nodiscard]] std::optional<JsonValue> parse() {
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();
        if (value.has_value() && index_ != source_.size()) {
            fail("project.codec.trailing_input");
        }
        return error_.has_value() ? std::nullopt : value;
    }

    [[nodiscard]] const std::optional<DesignError>& error() const {
        return error_;
    }

  private:
    void skipWhitespace() {
        while (index_ < source_.size() &&
               std::isspace(static_cast<unsigned char>(source_[index_]))) {
            ++index_;
        }
    }

    void fail(const std::string& code, const std::string& message = {}) {
        if (error_.has_value()) return;
        error_ = projectError(code, filename_, message.empty() ? code : message);
    }

    [[nodiscard]] bool consume(char expected) {
        skipWhitespace();
        if (index_ >= source_.size() || source_[index_] != expected) {
            fail("project.codec.expected_token",
                 std::string{"expected '"} + expected + "'");
            return false;
        }
        ++index_;
        return true;
    }

    [[nodiscard]] std::optional<JsonValue> parseValue() {
        skipWhitespace();
        if (index_ >= source_.size()) {
            fail("project.codec.unexpected_eof", "expected a JSON value");
            return std::nullopt;
        }
        switch (source_[index_]) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': {
                auto parsed = parseString();
                if (!parsed.has_value()) return std::nullopt;
                JsonValue value;
                value.kind = JsonValue::Kind::String;
                value.string = std::move(*parsed);
                return value;
            }
            case 't': return parseLiteral("true", JsonValue::Kind::Boolean, true);
            case 'f': return parseLiteral("false", JsonValue::Kind::Boolean, false);
            case 'n': return parseLiteral("null", JsonValue::Kind::Null);
            default: return parseNumber();
        }
    }

    [[nodiscard]] std::optional<JsonValue> parseLiteral(const char* text,
                                                        JsonValue::Kind kind,
                                                        bool boolean = false) {
        const std::string literal{text};
        if (source_.compare(index_, literal.size(), literal) != 0) {
            fail("project.codec.invalid_literal");
            return std::nullopt;
        }
        index_ += literal.size();
        JsonValue value;
        value.kind = kind;
        value.boolean = boolean;
        return value;
    }

    [[nodiscard]] std::optional<std::string> parseString() {
        if (!consume('"')) return std::nullopt;
        std::string result;
        while (index_ < source_.size()) {
            const char value = source_[index_++];
            if (value == '"') return result;
            if (value == '\\') {
                if (index_ >= source_.size()) break;
                const char escaped = source_[index_++];
                switch (escaped) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default:
                        fail("project.codec.escape", "unsupported JSON escape");
                        return std::nullopt;
                }
            } else if (static_cast<unsigned char>(value) < 0x20U) {
                fail("project.codec.control_character");
                return std::nullopt;
            } else {
                result += value;
            }
        }
        fail("project.codec.unterminated_string");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> parseNumber() {
        const std::size_t begin = index_;
        if (index_ < source_.size() && source_[index_] == '-') ++index_;
        while (index_ < source_.size() &&
               std::isdigit(static_cast<unsigned char>(source_[index_]))) {
            ++index_;
        }
        if (index_ < source_.size() && source_[index_] == '.') {
            ++index_;
            while (index_ < source_.size() &&
                   std::isdigit(static_cast<unsigned char>(source_[index_]))) {
                ++index_;
            }
        }
        if (index_ < source_.size() &&
            (source_[index_] == 'e' || source_[index_] == 'E')) {
            ++index_;
            if (index_ < source_.size() &&
                (source_[index_] == '+' || source_[index_] == '-')) ++index_;
            while (index_ < source_.size() &&
                   std::isdigit(static_cast<unsigned char>(source_[index_]))) {
                ++index_;
            }
        }
        const std::string text = source_.substr(begin, index_ - begin);
        if (text.empty()) {
            fail("project.codec.invalid_number");
            return std::nullopt;
        }
        char* end = nullptr;
        const double number = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size() || !std::isfinite(number)) {
            fail("project.codec.invalid_number");
            return std::nullopt;
        }
        JsonValue value;
        value.kind = JsonValue::Kind::Number;
        value.number = number;
        return value;
    }

    [[nodiscard]] std::optional<JsonValue> parseArray() {
        if (!consume('[')) return std::nullopt;
        JsonValue value;
        value.kind = JsonValue::Kind::Array;
        skipWhitespace();
        if (index_ < source_.size() && source_[index_] == ']') {
            ++index_;
            return value;
        }
        for (;;) {
            auto child = parseValue();
            if (!child.has_value()) return std::nullopt;
            value.array.push_back(std::move(*child));
            skipWhitespace();
            if (index_ < source_.size() && source_[index_] == ']') {
                ++index_;
                return value;
            }
            if (!consume(',')) return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<JsonValue> parseObject() {
        if (!consume('{')) return std::nullopt;
        JsonValue value;
        value.kind = JsonValue::Kind::Object;
        skipWhitespace();
        if (index_ < source_.size() && source_[index_] == '}') {
            ++index_;
            return value;
        }
        for (;;) {
            auto name = parseString();
            if (!name.has_value() || !consume(':')) return std::nullopt;
            auto child = parseValue();
            if (!child.has_value()) return std::nullopt;
            if (!value.object.emplace(*name, std::move(*child)).second) {
                fail("project.codec.duplicate_field", "duplicate object field");
                return std::nullopt;
            }
            skipWhitespace();
            if (index_ < source_.size() && source_[index_] == '}') {
                ++index_;
                return value;
            }
            if (!consume(',')) return std::nullopt;
        }
    }

    const std::string& source_;
    std::string filename_;
    std::size_t index_{0};
    std::optional<DesignError> error_{};
};

[[nodiscard]] JsonValue jsonString(std::string value) {
    JsonValue result;
    result.kind = JsonValue::Kind::String;
    result.string = std::move(value);
    return result;
}

[[nodiscard]] JsonValue jsonNumber(std::uint64_t value) {
    JsonValue result;
    result.kind = JsonValue::Kind::Number;
    result.number = static_cast<double>(value);
    return result;
}

[[nodiscard]] JsonValue jsonId(std::uint64_t value) {
    return jsonString(std::to_string(value));
}

[[nodiscard]] JsonValue jsonObject(std::map<std::string, JsonValue> value) {
    JsonValue result;
    result.kind = JsonValue::Kind::Object;
    result.object = std::move(value);
    return result;
}

[[nodiscard]] JsonValue jsonArray(std::vector<JsonValue> value) {
    JsonValue result;
    result.kind = JsonValue::Kind::Array;
    result.array = std::move(value);
    return result;
}

void appendJson(const JsonValue& value, std::string& output) {
    switch (value.kind) {
        case JsonValue::Kind::Null: output += "null"; return;
        case JsonValue::Kind::Boolean: output += value.boolean ? "true" : "false"; return;
        case JsonValue::Kind::Number: {
            std::ostringstream stream;
            stream << std::setprecision(17) << value.number;
            output += stream.str();
            return;
        }
        case JsonValue::Kind::String:
            output += '"';
            for (const char character : value.string) {
                switch (character) {
                    case '"': output += "\\\""; break;
                    case '\\': output += "\\\\"; break;
                    case '\b': output += "\\b"; break;
                    case '\f': output += "\\f"; break;
                    case '\n': output += "\\n"; break;
                    case '\r': output += "\\r"; break;
                    case '\t': output += "\\t"; break;
                    default: output += character; break;
                }
            }
            output += '"';
            return;
        case JsonValue::Kind::Array:
            output += '[';
            for (std::size_t index = 0; index < value.array.size(); ++index) {
                if (index != 0) output += ',';
                appendJson(value.array[index], output);
            }
            output += ']';
            return;
        case JsonValue::Kind::Object:
            output += '{';
            for (auto iterator = value.object.begin(); iterator != value.object.end(); ++iterator) {
                if (iterator != value.object.begin()) output += ',';
                appendJson(jsonString(iterator->first), output);
                output += ':';
                appendJson(iterator->second, output);
            }
            output += '}';
            return;
    }
}

[[nodiscard]] std::string jsonText(const JsonValue& value) {
    std::string output;
    appendJson(value, output);
    return output;
}

[[nodiscard]] const JsonValue* member(const JsonValue& value,
                                      const char* name) {
    if (value.kind != JsonValue::Kind::Object) return nullptr;
    const auto found = value.object.find(name);
    return found == value.object.end() ? nullptr : &found->second;
}

[[nodiscard]] bool stringValue(const JsonValue* value, std::string& out) {
    if (value == nullptr || value->kind != JsonValue::Kind::String) return false;
    out = value->string;
    return true;
}

[[nodiscard]] bool integerValue(const JsonValue* value, std::uint64_t& out) {
    if (value == nullptr) return false;
    if (value->kind == JsonValue::Kind::String) {
        if (value->string.empty()) return false;
        char* end = nullptr;
        const auto parsed = std::strtoull(value->string.c_str(), &end, 10);
        if (end != value->string.c_str() + value->string.size()) return false;
        out = static_cast<std::uint64_t>(parsed);
        return true;
    }
    if (value->kind != JsonValue::Kind::Number ||
        !std::isfinite(value->number) || value->number < 0.0 ||
        std::floor(value->number) != value->number ||
        value->number > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        return false;
    }
    out = static_cast<std::uint64_t>(value->number);
    return true;
}

[[nodiscard]] JsonValue encodePage(const DesignProjectPage& page) {
    return jsonObject({{"documentId", jsonString(page.documentId)},
                       {"pageName", jsonString(page.pageName)},
                       {"path", jsonString(page.path)}});
}

[[nodiscard]] JsonValue encodeResource(const DesignProjectResource& resource) {
    return jsonObject({{"kind", jsonString(resource.kind)},
                       {"path", jsonString(resource.path)},
                       {"uri", jsonString(resource.uri)}});
}

[[nodiscard]] JsonValue encodeReference(const DesignProjectReference& reference) {
    return jsonObject({{"fromDocumentId", jsonString(reference.fromDocumentId)},
        {"fromNodeId", jsonId(reference.fromNodeId)},
                       {"name", jsonString(reference.name)},
                       {"toDocumentId", jsonString(reference.toDocumentId)},
        {"toNodeId", jsonId(reference.toNodeId)}});
}

[[nodiscard]] bool readPage(const JsonValue& value, DesignProjectPage& page) {
    return value.kind == JsonValue::Kind::Object &&
           stringValue(member(value, "documentId"), page.documentId) &&
           stringValue(member(value, "path"), page.path) &&
           stringValue(member(value, "pageName"), page.pageName);
}

[[nodiscard]] bool readResource(const JsonValue& value,
                                DesignProjectResource& resource) {
    return value.kind == JsonValue::Kind::Object &&
           stringValue(member(value, "uri"), resource.uri) &&
           stringValue(member(value, "path"), resource.path) &&
           stringValue(member(value, "kind"), resource.kind);
}

[[nodiscard]] bool readReference(const JsonValue& value,
                                 DesignProjectReference& reference) {
    std::uint64_t fromNode = 0;
    std::uint64_t toNode = 0;
    return value.kind == JsonValue::Kind::Object &&
           stringValue(member(value, "fromDocumentId"), reference.fromDocumentId) &&
           integerValue(member(value, "fromNodeId"), fromNode) &&
           stringValue(member(value, "name"), reference.name) &&
           stringValue(member(value, "toDocumentId"), reference.toDocumentId) &&
           integerValue(member(value, "toNodeId"), toNode) &&
           ((reference.fromNodeId = fromNode), (reference.toNodeId = toNode), true);
}

[[nodiscard]] std::uint64_t revisionOf(const std::string& content) {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    for (const unsigned char byte : content) {
        hash ^= byte;
        hash *= prime;
    }
    return hash;
}

[[nodiscard]] std::optional<std::uint64_t> fileRevision(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    const std::string source((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    return input.bad() ? std::nullopt : std::optional{revisionOf(source)};
}

[[nodiscard]] std::string temporaryPath(const std::string& path) {
    namespace fs = std::filesystem;
    for (;;) {
        const auto serial = gTemporaryProjectFileId.fetch_add(1);
#ifdef _WIN32
        const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
        const auto process = static_cast<unsigned long long>(::getpid());
#endif
        const std::string candidate = path + ".tmp-" + std::to_string(process) +
                                      "-" + std::to_string(serial);
        std::error_code error;
        if (!fs::exists(candidate, error) || error) return candidate;
    }
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
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    std::filesystem::rename(temporary, path, error);
    return !error;
#endif
}

}  // namespace

std::string serializeDesignProject(const DesignProject& project) {
    std::vector<JsonValue> pages;
    for (const auto& page : project.pages) pages.push_back(encodePage(page));
    std::vector<JsonValue> resources;
    for (const auto& resource : project.resources) {
        resources.push_back(encodeResource(resource));
    }
    std::vector<JsonValue> references;
    for (const auto& reference : project.references) {
        references.push_back(encodeReference(reference));
    }
    std::map<std::string, JsonValue> root{
        {"format", jsonString("lumen.project")},
        {"name", jsonString(project.name)},
        {"pages", jsonArray(std::move(pages))},
        {"projectId", jsonString(project.projectId)},
        {"references", jsonArray(std::move(references))},
        {"resources", jsonArray(std::move(resources))},
        {"root", jsonString(project.root)},
        {"schemaVersion", jsonNumber(project.schemaVersion)},
    };
    if (!project.unknownFields.empty()) {
        std::map<std::string, JsonValue> unknown;
        for (const auto& [name, raw] : project.unknownFields) {
            JsonParser parser(raw, "<project.unknown-field>");
            const auto value = parser.parse();
            if (value.has_value()) unknown.emplace(name, *value);
        }
        root.emplace("unknownFields", jsonObject(std::move(unknown)));
    }
    return jsonText(jsonObject(std::move(root)));
}

ProjectLoadResult readDesignProject(const std::string& source,
                                    std::string filename) {
    ProjectLoadResult result;
    JsonParser parser(source, filename);
    const auto value = parser.parse();
    if (!value.has_value()) {
        result.diagnostics.push_back(*parser.error());
        return result;
    }
    std::uint64_t schema = 0;
    std::string format;
    if (value->kind != JsonValue::Kind::Object ||
        !stringValue(member(*value, "format"), format) ||
        format != "lumen.project" ||
        !integerValue(member(*value, "schemaVersion"), schema) ||
        !stringValue(member(*value, "projectId"), result.project.projectId) ||
        !stringValue(member(*value, "name"), result.project.name) ||
        !stringValue(member(*value, "root"), result.project.root)) {
        result.diagnostics.push_back(projectError(
            "project.codec.fields", filename,
            "format, schemaVersion, projectId, name and root are required"));
        result.project = {};
        return result;
    }
    result.project.schemaVersion = static_cast<std::uint32_t>(schema);
    const auto* pages = member(*value, "pages");
    const auto* resources = member(*value, "resources");
    const auto* references = member(*value, "references");
    if (pages == nullptr || pages->kind != JsonValue::Kind::Array ||
        resources == nullptr || resources->kind != JsonValue::Kind::Array ||
        references == nullptr || references->kind != JsonValue::Kind::Array) {
        result.diagnostics.push_back(projectError(
            "project.codec.collections", filename,
            "pages, resources and references must be arrays"));
        result.project = {};
        return result;
    }
    for (const auto& valueItem : pages->array) {
        DesignProjectPage page;
        if (!readPage(valueItem, page)) {
            result.diagnostics.push_back(projectError(
                "project.codec.page", filename, "invalid page entry"));
            result.project = {};
            return result;
        }
        result.project.pages.push_back(std::move(page));
    }
    for (const auto& valueItem : resources->array) {
        DesignProjectResource resource;
        if (!readResource(valueItem, resource)) {
            result.diagnostics.push_back(projectError(
                "project.codec.resource", filename, "invalid resource entry"));
            result.project = {};
            return result;
        }
        result.project.resources.push_back(std::move(resource));
    }
    for (const auto& valueItem : references->array) {
        DesignProjectReference reference;
        if (!readReference(valueItem, reference)) {
            result.diagnostics.push_back(projectError(
                "project.codec.reference", filename, "invalid reference entry"));
            result.project = {};
            return result;
        }
        result.project.references.push_back(std::move(reference));
    }
    if (const auto* unknown = member(*value, "unknownFields"); unknown != nullptr) {
        if (unknown->kind != JsonValue::Kind::Object) {
            result.diagnostics.push_back(projectError(
                "project.codec.unknown_fields", filename,
                "unknownFields must be an object"));
            result.project = {};
            return result;
        }
        for (const auto& [name, raw] : unknown->object) {
            result.project.unknownFields.emplace(name, jsonText(raw));
        }
    }
    static const std::set<std::string> known{
        "format", "name", "pages", "projectId", "references", "resources",
        "root", "schemaVersion", "unknownFields"};
    for (const auto& [name, raw] : value->object) {
        if (!known.contains(name)) result.project.unknownFields.emplace(name, jsonText(raw));
    }
    return result;
}

ProjectStore::ProjectStore() {
    migrations_[0] = [](DesignProject& project,
                        std::vector<DesignError>&) {
        project.schemaVersion = kCurrentProjectSchemaVersion;
        return true;
    };
}

void ProjectStore::registerMigration(std::uint32_t sourceVersion,
                                      Migration migration) {
    if (migration) migrations_[sourceVersion] = std::move(migration);
}

std::string ProjectStore::backupPath(const std::string& path) {
    return path + ".bak";
}

bool ProjectStore::migrate(DesignProject& project,
                           std::vector<DesignError>& diagnostics) const {
    while (project.schemaVersion < kCurrentProjectSchemaVersion) {
        const auto found = migrations_.find(project.schemaVersion);
        if (found == migrations_.end()) {
            diagnostics.push_back(projectError(
                "project.migration_missing", "<project>",
                "no migration is registered for schemaVersion " +
                    std::to_string(project.schemaVersion)));
            return false;
        }
        const auto sourceVersion = project.schemaVersion;
        try {
            if (!found->second(project, diagnostics)) return false;
        } catch (const std::exception& exception) {
            diagnostics.push_back(projectError(
                "project.migration_exception", "<project>", exception.what()));
            return false;
        }
        if (project.schemaVersion != sourceVersion + 1) {
            diagnostics.push_back(projectError(
                "project.migration_version", "<project>",
                "migration must advance schemaVersion by exactly one"));
            return false;
        }
    }
    if (project.schemaVersion > kCurrentProjectSchemaVersion) {
        diagnostics.push_back(projectError(
            "project.schema_version", "<project>",
            "project schemaVersion is newer than this store"));
        return false;
    }
    return true;
}

std::vector<DesignError> ProjectStore::validate(const DesignProject& project,
                                                const std::string& file) {
    std::vector<DesignError> diagnostics;
    if (project.projectId.empty()) {
        diagnostics.push_back(projectError("project.project_id", file,
                                           "projectId is required"));
    }
    if (project.name.empty()) {
        diagnostics.push_back(projectError("project.name", file,
                                           "project name is required"));
    }
    if (project.root.empty()) {
        diagnostics.push_back(projectError("project.root", file,
                                           "project root is required"));
    }
    if (project.schemaVersion != kCurrentProjectSchemaVersion) {
        diagnostics.push_back(projectError("project.schema_version", file,
                                           "only the current schemaVersion can be saved"));
    }
    std::set<std::string> documentIds;
    for (const auto& page : project.pages) {
        if (page.documentId.empty() || page.path.empty() || page.pageName.empty()) {
            diagnostics.push_back(projectError("project.page", file,
                                               "page documentId, path and pageName are required"));
        }
        if (!documentIds.insert(page.documentId).second) {
            diagnostics.push_back(projectError("project.duplicate_page", file,
                                               "page documentId must be unique"));
        }
    }
    std::set<std::string> resourceUris;
    for (const auto& resource : project.resources) {
        if (resource.uri.empty() || resource.path.empty() || resource.kind.empty()) {
            diagnostics.push_back(projectError("project.resource", file,
                                               "resource uri, path and kind are required"));
        }
        if (!resourceUris.insert(resource.uri).second) {
            diagnostics.push_back(projectError("project.duplicate_resource", file,
                                               "resource uri must be unique"));
        }
    }
    for (const auto& reference : project.references) {
        if (!documentIds.contains(reference.fromDocumentId) ||
            !documentIds.contains(reference.toDocumentId) ||
            reference.fromNodeId == 0 || reference.toNodeId == 0 ||
            reference.name.empty()) {
            diagnostics.push_back(projectError(
                "project.reference", file,
                "cross-document reference must identify existing documents, nodes and name"));
        }
    }
    for (const auto& [name, raw] : project.unknownFields) {
        JsonParser parser(raw, file);
        if (!parser.parse().has_value()) {
            auto diagnostic = projectError("project.invalid_unknown_field", file,
                                           "unknown field must contain a JSON value");
            diagnostic.property = name;
            diagnostics.push_back(std::move(diagnostic));
        }
    }
    return diagnostics;
}

ProjectLoadResult ProjectStore::load(const std::string& path) const {
    namespace fs = std::filesystem;
    const auto read = [&](const std::string& candidate,
                          std::vector<DesignError>& errors) {
        std::ifstream input(candidate, std::ios::binary);
        if (!input) {
            errors.push_back(projectError("project.read", candidate,
                                          "unable to open project manifest"));
            return std::optional<DesignProject>{};
        }
        const std::string source((std::istreambuf_iterator<char>(input)),
                                 std::istreambuf_iterator<char>());
        auto parsed = readDesignProject(source, candidate);
        errors = std::move(parsed.diagnostics);
        if (parsed.project.projectId.empty()) return std::optional<DesignProject>{};
        return std::optional{std::move(parsed.project)};
    };
    const auto prepare = [&](DesignProject project, const std::string& sourceFile,
                             bool recovered,
                             std::uint64_t revision,
                             std::vector<DesignError> diagnostics) {
        ProjectLoadResult result;
        result.project = std::move(project);
        result.recovered = recovered;
        result.revision = revision;
        result.migrated = result.project.schemaVersion != kCurrentProjectSchemaVersion;
        result.diagnostics = std::move(diagnostics);
        const auto firstNewDiagnostic = result.diagnostics.size();
        if (!migrate(result.project, result.diagnostics)) {
            result.project = {};
        } else {
            const auto schemaDiagnostics = validate(result.project, sourceFile);
            result.diagnostics.insert(result.diagnostics.end(), schemaDiagnostics.begin(),
                                      schemaDiagnostics.end());
            if (!schemaDiagnostics.empty()) result.project = {};
        }
        for (auto index = firstNewDiagnostic;
             index < result.diagnostics.size(); ++index) {
            auto& diagnostic = result.diagnostics[index];
            if (diagnostic.file.empty() || diagnostic.file == "<project>") {
                diagnostic.file = sourceFile;
            }
        }
        return result;
    };

    std::vector<DesignError> primaryDiagnostics;
    if (const auto primary = read(path, primaryDiagnostics); primary.has_value()) {
        auto result = prepare(*primary, path, false, fileRevision(path).value_or(0),
                              std::move(primaryDiagnostics));
        if (result.ok()) return result;
        primaryDiagnostics = std::move(result.diagnostics);
    }
    std::error_code existsError;
    const std::string backup = backupPath(path);
    if (!fs::exists(backup, existsError) || existsError) {
        return ProjectLoadResult{DesignProject{}, std::move(primaryDiagnostics), false,
                                 false, 0};
    }
    std::vector<DesignError> backupDiagnostics;
    if (const auto recovered = read(backup, backupDiagnostics); recovered.has_value()) {
        auto result = prepare(*recovered, backup, true, fileRevision(path).value_or(0),
                              std::move(primaryDiagnostics));
        result.diagnostics.insert(result.diagnostics.end(), backupDiagnostics.begin(),
                                  backupDiagnostics.end());
        return result;
    }
    primaryDiagnostics.insert(primaryDiagnostics.end(), backupDiagnostics.begin(),
                              backupDiagnostics.end());
    return ProjectLoadResult{DesignProject{}, std::move(primaryDiagnostics), false,
                             false, 0};
}

bool ProjectStore::save(const std::string& path, const DesignProject& project,
                        std::vector<DesignError>& diagnostics,
                        std::optional<std::uint64_t> expectedRevision) const {
    diagnostics = validate(project, path);
    if (!diagnostics.empty()) return false;
    namespace fs = std::filesystem;
    const auto revisionMatches = [&] {
        if (!expectedRevision.has_value()) return true;
        return expectedRevision == fileRevision(path).value_or(0);
    };
    if (!revisionMatches()) {
        diagnostics.push_back(projectError("project.revision_conflict", path,
                                           "project changed after it was loaded"));
        return false;
    }
    const std::string temporary = temporaryPath(path);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            diagnostics.push_back(projectError("project.write", path,
                                               "unable to open temporary project"));
            return false;
        }
        const std::string encoded = serializeDesignProject(project);
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.flush();
        if (!output) {
            output.close();
            std::error_code cleanup;
            fs::remove(temporary, cleanup);
            diagnostics.push_back(projectError("project.write", path,
                                               "unable to write temporary project"));
            return false;
        }
        output.close();
    }
    if (!revisionMatches()) {
        std::error_code cleanup;
        fs::remove(temporary, cleanup);
        diagnostics.push_back(projectError("project.revision_conflict", path,
                                           "project changed after it was loaded"));
        return false;
    }
    std::error_code error;
    const bool hasPrimary = fs::exists(path, error);
    if (error) {
        fs::remove(temporary, error);
        diagnostics.push_back(projectError("project.backup", path,
                                           "unable to inspect project manifest"));
        return false;
    }
    if (hasPrimary) {
        // Preserve a valid recovery copy when the primary manifest was already
        // damaged and this save is replacing a recovered session.
        bool primaryIsValid = false;
        std::ifstream input(path, std::ios::binary);
        if (input) {
            const std::string source((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
            auto parsed = readDesignProject(source, path);
            auto candidate = std::move(parsed.project);
            primaryIsValid = parsed.diagnostics.empty() &&
                             !candidate.projectId.empty() &&
                             migrate(candidate, parsed.diagnostics) &&
                             validate(candidate, path).empty();
        }
        if (primaryIsValid &&
            (!fs::copy_file(path, backupPath(path),
                            fs::copy_options::overwrite_existing, error) || error)) {
            fs::remove(temporary, error);
            diagnostics.push_back(projectError("project.backup", path,
                                               "unable to create recovery copy"));
            return false;
        }
    }
    if (!revisionMatches()) {
        fs::remove(temporary, error);
        diagnostics.push_back(projectError("project.revision_conflict", path,
                                           "project changed after it was loaded"));
        return false;
    }
    if (!atomicReplace(temporary, path, error)) {
        fs::remove(temporary, error);
        diagnostics.push_back(projectError("project.rename", path,
                                           "unable to atomically replace project"));
        return false;
    }
    return true;
}

}  // namespace lumen::dsl
