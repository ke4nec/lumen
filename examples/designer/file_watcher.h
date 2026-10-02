#pragma once

#include <filesystem>
#include <string>

namespace lumen::designer_app {

class FileWatcher {
  public:
    explicit FileWatcher(std::string filename);

    [[nodiscard]] bool poll();
    [[nodiscard]] const std::string& filename() const { return filename_; }

  private:
    std::string filename_{};
    bool exists_{false};
    std::filesystem::file_time_type stamp_{};
};

}  // namespace lumen::designer_app
