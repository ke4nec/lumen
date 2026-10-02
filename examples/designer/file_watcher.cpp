#include "file_watcher.h"

#include <system_error>
#include <utility>

namespace lumen::designer_app {

FileWatcher::FileWatcher(std::string filename)
    : filename_(std::move(filename)) {
    std::error_code error;
    stamp_ = std::filesystem::last_write_time(filename_, error);
    exists_ = !error;
}

bool FileWatcher::poll() {
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(filename_, error);
    if (error) {
        if (!exists_) return false;
        exists_ = false;
        stamp_ = std::filesystem::file_time_type{};
        return true;
    }
    if (exists_ && stamp == stamp_) return false;
    exists_ = true;
    stamp_ = stamp;
    return true;
}

}  // namespace lumen::designer_app
