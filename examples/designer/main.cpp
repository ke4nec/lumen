#include <cstdio>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "designer_app.h"
#include "file_watcher.h"
#include "lumen/accessibility/bridge.h"
#include "lumen/platform/sdl3_host.h"

namespace {

struct Options {
    bool headless{false};
    bool acceptance{false};
    bool watch{false};
    bool dumpDiagnostics{false};
    std::uint64_t maxFrames{0};
    std::string filename{};
    std::string error{};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--headless") == 0) {
            options.headless = true;
        } else if (std::strcmp(argv[index], "--acceptance") == 0) {
            options.acceptance = true;
        } else if (std::strcmp(argv[index], "--watch") == 0) {
            options.watch = true;
        } else if (std::strcmp(argv[index], "--dump-diagnostics") == 0) {
            options.dumpDiagnostics = true;
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

void dumpDiagnostics(const lumen::designer_app::DesignerApp& app) {
    std::printf("diagnostics_json %s\n",
                lumen::dsl::serializeDesignDiagnostics(app.diagnostics()).c_str());
}

int runHeadless(lumen::designer_app::DesignerApp& app,
                bool requestedFileLoaded, bool dump) {
    app.shell().setView(lumen::core::Size{1280.0F, 800.0F});
    const auto frame = app.shell().renderFrame();
    std::printf("frame0 %016llx\n",
                static_cast<unsigned long long>(frame));
    std::printf("document %d\n", app.workbench().document().has_value() ? 1 : 0);
    std::printf("diagnostics %zu\n", app.diagnostics().size());
    if (dump) dumpDiagnostics(app);
    return app.workbench().frame().hasFrame() && requestedFileLoaded ? 0 : 1;
}

const lumen::dsl::DesignNode* findFirstTextNode(
    const lumen::dsl::DesignNode& node) {
    if (node.type == "Text" && node.properties.contains("text")) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* found = findFirstTextNode(child); found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findFirstTextNode(child); found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

int runAcceptance(lumen::designer_app::DesignerApp& app,
                  bool requestedFileLoaded, const std::string& filename) {
    using namespace lumen;
    if (!requestedFileLoaded || filename.empty() || !isDesignFile(filename) ||
        !app.workbench().document().has_value()) {
        std::fprintf(stderr,
                     "designer acceptance requires a loaded .design file\n");
        return 1;
    }

    app.shell().setView(core::Size{1280.0F, 800.0F});
    accessibility::RecordingAccessibilityBridge accessibilityBridge;
    app.shell().setAccessibilityBridge(&accessibilityBridge);
    (void)app.shell().renderFrame();
    const auto* textNode = findFirstTextNode(app.workbench().document()->root);
    if (textNode == nullptr) {
        std::fprintf(stderr, "designer acceptance fixture has no editable Text node\n");
        return 1;
    }
    const auto textValue = textNode->properties.at("text");
    const auto* original = std::get_if<std::string>(&textValue.value);
    if (original == nullptr) {
        std::fprintf(stderr, "designer acceptance Text.text is not a string\n");
        return 1;
    }
    const std::string originalText = *original;
    const std::string editedText = originalText + " (acceptance)";
    const auto nodeId = textNode->id;
    const auto select = app.shell().handlers().find(
        "designer:select:" + std::to_string(nodeId));
    if (select == app.shell().handlers().end()) {
        std::fprintf(stderr, "designer acceptance cannot select Text node %llu\n",
                     static_cast<unsigned long long>(nodeId));
        return 1;
    }
    select->second();
    (void)app.shell().renderFrame();
    if (app.workbench().selection().primary != nodeId) {
        std::fprintf(stderr, "designer acceptance selection did not publish\n");
        return 1;
    }

    const std::string fieldKey =
        "designer-property-field:" + std::to_string(nodeId) + ":text";
    const auto* field = core::findNodeByKey(app.shell().root(), fieldKey);
    if (field == nullptr) {
        std::fprintf(stderr, "designer acceptance property field is missing\n");
        return 1;
    }
    if (app.shell().performAccessibilityAction(
            field->identity, accessibility::kActionFocus) !=
        accessibility::SemanticsActionStatus::Handled ||
        app.shell().performAccessibilityAction(
            field->identity, accessibility::kActionSetValue, editedText) !=
        accessibility::SemanticsActionStatus::Handled) {
        std::fprintf(stderr, "designer acceptance property action was rejected\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    const auto readText = [&app] {
        const auto& document = *app.workbench().document();
        const auto* node = findFirstTextNode(document.root);
        if (node == nullptr) return std::string{};
        const auto property = node->properties.find("text");
        if (property == node->properties.end()) return std::string{};
        const auto* value = std::get_if<std::string>(&property->second.value);
        return value == nullptr ? std::string{} : *value;
    };
    if (readText() != editedText || !app.workbench().dirty()) {
        std::fprintf(stderr, "designer acceptance property edit did not commit\n");
        return 1;
    }
    if (!app.undo()) {
        std::fprintf(stderr, "designer acceptance undo failed\n");
        return 1;
    }
    if (readText() != originalText || !app.redo() || readText() != editedText) {
        std::fprintf(stderr, "designer acceptance undo/redo did not restore text\n");
        return 1;
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path saved =
        std::filesystem::temp_directory_path() /
        ("lumen-designer-acceptance-" + std::to_string(stamp) + ".design");
    std::error_code cleanup;
    std::filesystem::remove(saved, cleanup);
    if (!app.saveDesignFile(saved.string())) {
        std::fprintf(stderr, "designer acceptance save failed\n");
        std::filesystem::remove(saved, cleanup);
        return 1;
    }

    designer_app::DesignerApp reopened;
    reopened.attach();
    if (!reopened.loadDesignFile(saved.string()) ||
        !reopened.workbench().document().has_value()) {
        std::fprintf(stderr, "designer acceptance reopen failed\n");
        std::filesystem::remove(saved, cleanup);
        return 1;
    }
    const auto& reopenedDocument = *reopened.workbench().document();
    const auto* reopenedText = findFirstTextNode(reopenedDocument.root);
    std::string reopenedValue;
    if (reopenedText != nullptr) {
        const auto value = reopenedText->properties.find("text");
        if (value != reopenedText->properties.end()) {
            if (const auto* text = std::get_if<std::string>(&value->second.value);
                text != nullptr) {
                reopenedValue = *text;
            }
        }
    }
    if (reopenedDocument.documentId != app.workbench().document()->documentId ||
        reopenedText == nullptr || reopenedText->id != nodeId ||
        reopenedValue != editedText || !reopened.diagnostics().empty()) {
        std::fprintf(stderr, "designer acceptance reopen changed declaration\n");
        std::filesystem::remove(saved, cleanup);
        return 1;
    }
    std::filesystem::remove(saved, cleanup);
    std::printf("designer_acceptance document %s\n",
                reopenedDocument.documentId.c_str());
    std::printf("designer_acceptance selected %llu\n",
                static_cast<unsigned long long>(nodeId));
    std::printf("designer_acceptance edit_save_reopen pass\n");
    return 0;
}

int runWindowed(lumen::designer_app::DesignerApp& app,
                const Options& designerOptions, bool requestedFileLoaded) {
    if (designerOptions.dumpDiagnostics) {
        app.shell().setView(lumen::core::Size{1280.0F, 800.0F});
        (void)app.shell().renderFrame();
        dumpDiagnostics(app);
    }
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
        runOptions.poll = [&watcher, &app,
                           dump = designerOptions.dumpDiagnostics](lumen::app::AppShell&,
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
            if (dump) {
                (void)app.shell().renderFrame();
                dumpDiagnostics(app);
            }
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
        if (options.acceptance) {
            return runAcceptance(app, requestedFileLoaded, options.filename);
        }
        return options.headless
                   ? runHeadless(app, requestedFileLoaded, options.dumpDiagnostics)
                   : runWindowed(app, options, requestedFileLoaded);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
