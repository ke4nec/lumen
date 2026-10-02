#include <cstdio>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "designer_app.h"
#include "file_watcher.h"
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
        } else if (std::strcmp(argv[index], "--file") == 0) {
            if (index + 1 >= argc || argv[index + 1][0] == '-') {
                options.error = "--file requires a value";
                break;
            }
            options.filename = argv[++index];
        } else {
            options.error = "unknown option: ";
            options.error += argv[index];
            break;
        }
    }
    return options;
}

bool isDesignFile(const std::string& filename) {
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension == ".design";
}

bool isProjectFile(const std::string& filename) {
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension == ".lumen-project" || extension == ".lumenproject";
}

int runHeadless(lumen::designer_app::DesignerApp& app,
                bool requestedFileLoaded) {
    app.shell().setView(lumen::core::Size{1280.0F, 800.0F});
    const auto frame = app.shell().renderFrame();
    std::printf("frame0 %016llx\n",
                static_cast<unsigned long long>(frame));
    std::printf("document %d\n", app.workbench().document().has_value() ? 1 : 0);
    std::printf("diagnostics %zu\n", app.workbench().diagnostics().size());
    return app.workbench().frame().hasFrame() && requestedFileLoaded ? 0 : 1;
}

int runWindowed(lumen::designer_app::DesignerApp& app,
                const Options& designerOptions, bool requestedFileLoaded) {
    // A requested startup document is part of the window smoke contract. A
    // later watch reload may still keep the last valid frame in the session.
    if (!requestedFileLoaded && !designerOptions.watch) return 1;
    lumen::platform::Sdl3ApplicationHost host;
    app.setFileDialogRequester([&host](bool forSave,
                                       const std::string& defaultName) {
        if (!host.capabilities().fileDialogs) {
            return std::string{"file dialogs are unavailable"};
        }
        lumen::platform::FileDialogRequest request;
        request.title = forSave ? "Save Lumen design" : "Open Lumen document";
        request.filters = forSave
                              ? std::vector<std::string>{"*.lumen-project",
                                                         "*.design"}
                              : std::vector<std::string>{"*.lumen-project",
                                                         "*.design", "*.lumen"};
        request.defaultName = defaultName;
        request.forSave = forSave;
        const auto result = host.requestFileDialog({}, request);
        return result.ok ? std::string{} : result.message;
    });
    lumen::app::RunOptions runOptions;
    runOptions.windowDesc.title = "Lumen Designer";
    runOptions.windowDesc.width = 1280;
    runOptions.windowDesc.height = 800;
    runOptions.maxFrames = designerOptions.maxFrames;
    runOptions.resourceManager = app.resourceManager();
    runOptions.onEvent = [&app](lumen::app::AppShell&,
                                const lumen::core::HostEvent& event) {
        if (event.type == lumen::core::HostEventType::FileDialogCompleted) {
            app.handleFileDialogResult(event.filePaths, event.text);
        }
    };
    std::optional<lumen::designer_app::FileWatcher> watcher;
    if (designerOptions.watch && !designerOptions.filename.empty()) {
        watcher.emplace(designerOptions.filename);
        std::printf("watching %s for changes\n",
                    designerOptions.filename.c_str());
        runOptions.poll = [&watcher, &app](lumen::app::AppShell&,
                                           std::uint64_t /*nowMs*/) {
            if (!watcher.has_value()) return false;
            if (!watcher->poll()) return false;
            const bool loaded = isProjectFile(watcher->filename())
                                    ? app.loadProjectFile(watcher->filename())
                                    : (isDesignFile(watcher->filename())
                                           ? app.loadDesignFile(watcher->filename())
                                           : app.loadFile(watcher->filename()));
            std::printf(loaded ? "ui reloaded\n"
                               : "ui reload failed (kept previous UI)\n");
            return true;
        };
    }
    lumen::app::RunOptions previewOptions;
    previewOptions.windowDesc.title = "Lumen Preview";
    previewOptions.windowDesc.width = 960;
    previewOptions.windowDesc.height = 640;
    previewOptions.maxFrames = designerOptions.maxFrames;
    previewOptions.resourceManager = app.resourceManager();
    const int result = lumen::app::runApp(
        {{&app.shell(), std::move(runOptions)},
         {&app.previewShell(), std::move(previewOptions)}},
        host);
    if (result == 0) std::printf("designer_window_smoke pass\n");
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (!options.error.empty()) {
        std::fprintf(stderr, "usage error: %s\n", options.error.c_str());
        return 2;
    }
    lumen::designer_app::DesignerApp app;
    std::error_code resourceRootError;
    const auto resourceRoot = std::filesystem::current_path(resourceRootError);
    if (!resourceRootError) app.setResourceRoot(resourceRoot);
    app.attach();
    bool requestedFileLoaded = true;
    if (!options.filename.empty()) {
        if (isProjectFile(options.filename)) {
            requestedFileLoaded = app.loadProjectFile(options.filename);
        } else if (isDesignFile(options.filename)) {
            requestedFileLoaded = app.loadDesignFile(options.filename);
        } else {
            requestedFileLoaded = app.loadFile(options.filename);
        }
    }
    try {
        return options.headless
                   ? runHeadless(app, requestedFileLoaded)
                   : runWindowed(app, options, requestedFileLoaded);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
