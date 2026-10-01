#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lumen/dsl/design_codec.h"

namespace lumen::dsl {

struct DesignProjectPage {
    std::string documentId{};
    std::string path{};
    std::string pageName{};

    bool operator==(const DesignProjectPage&) const = default;
};

struct DesignProjectResource {
    std::string uri{};
    std::string path{};
    std::string kind{};

    bool operator==(const DesignProjectResource&) const = default;
};

struct DesignProjectReference {
    std::string fromDocumentId{};
    std::uint64_t fromNodeId{0};
    std::string name{};
    std::string toDocumentId{};
    std::uint64_t toNodeId{0};

    bool operator==(const DesignProjectReference&) const = default;
};

// Project metadata is deliberately separate from DesignDocument. A project
// manifest owns paths and cross-document declarations; page files remain
// independently loadable and keep their own revision and recovery files.
struct DesignProject {
    std::uint32_t schemaVersion{1};
    std::string projectId{};
    std::string name{};
    std::string root{};
    std::vector<DesignProjectPage> pages{};
    std::vector<DesignProjectResource> resources{};
    std::vector<DesignProjectReference> references{};
    std::map<std::string, std::string> unknownFields{};

    bool operator==(const DesignProject&) const = default;
};

struct ProjectLoadResult {
    DesignProject project{};
    std::vector<DesignError> diagnostics{};
    bool recovered{false};
    bool migrated{false};
    std::uint64_t revision{0};

    [[nodiscard]] bool ok() const {
        return !project.projectId.empty() &&
               (diagnostics.empty() || recovered);
    }
};

class ProjectStore {
  public:
    using Migration =
        std::function<bool(DesignProject&, std::vector<DesignError>&)>;

    ProjectStore();

    // Registers the step from sourceVersion to sourceVersion + 1.
    void registerMigration(std::uint32_t sourceVersion, Migration migration);

    [[nodiscard]] ProjectLoadResult load(const std::string& path) const;
    [[nodiscard]] bool save(
        const std::string& path, const DesignProject& project,
        std::vector<DesignError>& diagnostics,
        std::optional<std::uint64_t> expectedRevision = std::nullopt) const;

    [[nodiscard]] static std::string backupPath(const std::string& path);

  private:
    [[nodiscard]] bool migrate(DesignProject& project,
                               std::vector<DesignError>& diagnostics) const;
    [[nodiscard]] static std::vector<DesignError> validate(
        const DesignProject& project, const std::string& file);

    std::map<std::uint32_t, Migration> migrations_{};
};

[[nodiscard]] std::string serializeDesignProject(const DesignProject& project);
[[nodiscard]] ProjectLoadResult readDesignProject(
    const std::string& source, std::string filename = "<project>");

}  // namespace lumen::dsl
