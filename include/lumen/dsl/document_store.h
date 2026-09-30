#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lumen/dsl/design_codec.h"

namespace lumen::dsl {

struct DocumentLoadResult {
    DesignDocument document{};
    std::vector<DesignError> diagnostics{};
    bool recovered{false};
    bool migrated{false};
    std::uint64_t revision{0};

    [[nodiscard]] bool ok() const {
        return document.root.id != 0 && (diagnostics.empty() || recovered);
    }
};

class DocumentStore {
  public:
    using Migration = std::function<bool(DesignDocument&, std::vector<DesignError>&)>;

    DocumentStore();

    // Registers the step from sourceVersion to sourceVersion + 1. A later
    // registration replaces the previous step for the same source version.
    void registerMigration(std::uint32_t sourceVersion, Migration migration);

    [[nodiscard]] DocumentLoadResult load(const std::string& path) const;
    [[nodiscard]] bool save(const std::string& path,
                            const DesignDocument& document,
                            std::vector<DesignError>& diagnostics,
                            std::optional<std::uint64_t> expectedRevision =
                                std::nullopt) const;

    [[nodiscard]] static std::string backupPath(const std::string& path);

  private:
    [[nodiscard]] bool migrate(DesignDocument& document,
                               std::vector<DesignError>& diagnostics) const;
    [[nodiscard]] static DesignError errorAt(const std::string& code,
                                             const std::string& file,
                                             const std::string& message);

    std::map<std::uint32_t, Migration> migrations_{};
};

}  // namespace lumen::dsl
