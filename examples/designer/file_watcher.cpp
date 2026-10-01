#include "file_watcher.h"

#include <system_error>
#include <utility>

namespace lumen::designer_app {

FileWatcher::FileWatcher(std::string filename)
    : filename_(std::move(filename)) {
    std::error_code error;
    stamp_ = std::filesystem::last_write_time(filename_, error);
}

bool FileWatcher::poll() {
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(filename_, error);
    if (error || stamp == stamp_) return false;
    stamp_ = stamp;
    return true;
}

}  // namespace lumen::designer_app
