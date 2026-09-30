#include <cstdio>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "designer_app.h"
#include "lumen/platform/sdl3_host.h"

namespace {

struct Options {
    bool headless{false};
    bool watch{false};
    std::uint64_t maxFrames{0};
    std::string filename{};
    std::string error{};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--headless") == 0) {
            options.headless = true;
        } else if (std::strcmp(argv[index], "--watch") == 0) {
            options.watch = true;
        } else if (std::strcmp(argv[index], "--max-frames") == 0 &&
                   index + 1 < argc) {
            const char* begin = argv[++index];
            const char* end = begin + std::strlen(begin);
            const auto parsed = std::from_chars(begin, end, options.maxFrames,
                                                10);
            if (parsed.ec != std::errc{} || parsed.ptr != end) {
                options.error = "--max-frames expects an unsigned integer";
                break;
            }
        } else if (std::strcmp(argv[index], "--max-frames") == 0) {
            options.error = "--max-frames requires a value";
            break;
        } else if (std::strcmp(argv[index], "--file") == 0 &&
                   index + 1 < argc) {
            options.filename = argv[++index];
        }
    }
    return options;
}

class FileWatcher {
  public:
    explicit FileWatcher(std::string filename)
        : filename_(std::move(filename)) {
        refreshStamp();
    }

    [[nodiscard]] bool poll(lumen::designer_app::DesignerApp& app) {
        std::error_code error;
        const auto stamp = std::filesystem::last_write_time(filename_, error);
        if (error || stamp == stamp_) return false;
        stamp_ = stamp;
        (void)app.loadFile(filename_);
        return true;
    }

  private:
    void refreshStamp() {
        std::error_code error;
        stamp_ = std::filesystem::last_write_time(filename_, error);
    }

    std::string filename_{};
    std::filesystem::file_time_type stamp_{};
};

int runHeadless(lumen::designer_app::DesignerApp& app) {
    app.shell().setView(lumen::core::Size{1280.0F, 800.0F});
    const auto frame = app.shell().renderFrame();
    std::printf("frame0 %016llx\n",
                static_cast<unsigned long long>(frame));
    std::printf("document %d\n", app.workbench().document().has_value() ? 1 : 0);
    std::printf("diagnostics %zu\n", app.workbench().diagnostics().size());
    return app.workbench().frame().hasFrame() ? 0 : 1;
}

int runWindowed(lumen::designer_app::DesignerApp& app,
                const Options& designerOptions) {
    lumen::platform::Sdl3ApplicationHost host;
    lumen::app::RunOptions runOptions;
    runOptions.windowDesc.title = "Lumen Designer";
    runOptions.windowDesc.width = 1280;
    runOptions.windowDesc.height = 800;
    runOptions.maxFrames = designerOptions.maxFrames;
    std::optional<FileWatcher> watcher;
    if (designerOptions.watch && !designerOptions.filename.empty()) {
        watcher.emplace(designerOptions.filename);
        std::printf("watching %s for changes\n",
                    designerOptions.filename.c_str());
        runOptions.poll = [&watcher, &app](lumen::app::AppShell&,
                                           std::uint64_t /*nowMs*/) {
            if (!watcher.has_value()) return false;
            return watcher->poll(app);
        };
    }
    return lumen::app::runApp(app.shell(), host, runOptions);
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (!options.error.empty()) {
        std::fprintf(stderr, "usage error: %s\n", options.error.c_str());
        return 2;
    }
    lumen::designer_app::DesignerApp app;
    app.attach();
    if (!options.filename.empty()) {
        (void)app.loadFile(options.filename);
    }
    try {
        return options.headless ? runHeadless(app) : runWindowed(app, options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
