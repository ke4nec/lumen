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
#include "lumen/style/theme.h"

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

const lumen::dsl::DesignNode* findFirstNodeOfType(
    const lumen::dsl::DesignNode& node, std::string_view type) {
    if (node.type == type) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findFirstNodeOfType(child, type);
            found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findFirstNodeOfType(child, type);
                found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

const lumen::dsl::DesignNode* findNodeById(
    const lumen::dsl::DesignNode& node, lumen::dsl::DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findNodeById(child, id); found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findNodeById(child, id); found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

const lumen::core::Widget* findFirstWidgetOfType(
    const lumen::core::Widget& widget, lumen::core::WidgetType type) {
    if (widget.type == type) return &widget;
    for (const auto& child : widget.children) {
        if (const auto* found = findFirstWidgetOfType(child, type);
            found != nullptr) {
            return found;
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
    // B 默认 Toolbox，大纲验收前切到结构页（workspace-design §3）。
    if (const auto outlineTab =
            app.shell().handlers().find("designer:lefttab-outline");
        outlineTab != app.shell().handlers().end()) {
        outlineTab->second();
        (void)app.shell().renderFrame();
    }
    const auto initialRevision = app.workbench().documentRevision();
    const auto checkEnvironment = [&app, initialRevision] {
        (void)app.shell().renderFrame();
        if (app.workbench().dirty() ||
            app.workbench().documentRevision() != initialRevision) {
            return false;
        }
        constexpr const char* kLayoutKeys[] = {
            "designer-outline-view", "designer-canvas",
            "designer-properties-panel", "designer-diagnostics"};
        const auto semantics = app.shell().buildSemanticsSnapshot();
        for (const auto* key : kLayoutKeys) {
            const auto* renderNode = core::findNodeByKey(app.shell().root(), key);
            if (renderNode == nullptr || renderNode->size.width <= 0.0F ||
                renderNode->size.height <= 0.0F) {
                return false;
            }
            const auto* semantic = semantics.find(renderNode->identity);
            if (semantic == nullptr || semantic->bounds.size.width <= 0.0F ||
                semantic->bounds.size.height <= 0.0F) {
                return false;
            }
        }
        return true;
    };
    const auto environmentFailure = [] {
        std::fprintf(stderr,
                     "designer acceptance DPI/theme environment check failed\n");
        return 1;
    };
    if (!checkEnvironment()) return environmentFailure();
    for (const auto view : {core::Size{1024.0F, 640.0F},
                            core::Size{1600.0F, 900.0F}}) {
        app.shell().setView(view);
        if (!checkEnvironment()) return environmentFailure();
    }
    const auto& handlers = app.shell().handlers();
    const auto activateEnvironment = [&](const char* handler) {
        const auto found = handlers.find(handler);
        if (found == handlers.end()) return false;
        found->second();
        return checkEnvironment();
    };
    if (!activateEnvironment("designer:dpi") ||
        app.shell().styleContext().deviceScale != 1.25F ||
        !activateEnvironment("designer:font-scale") ||
        app.shell().accessibilitySettings().fontScale != 1.25F ||
        !activateEnvironment("designer:contrast") ||
        !app.shell().accessibilitySettings().highContrast) {
        return environmentFailure();
    }
    // Restore the defaults before the document transaction so the acceptance
    // frame and saved preview do not depend on the environment probe state.
    if (!activateEnvironment("designer:dpi") ||
        !activateEnvironment("designer:dpi") ||
        !activateEnvironment("designer:font-scale") ||
        !activateEnvironment("designer:font-scale") ||
        !activateEnvironment("designer:contrast")) {
        return environmentFailure();
    }
    app.shell().setView(core::Size{1280.0F, 800.0F});
    if (!checkEnvironment() || app.shell().styleContext().deviceScale != 1.0F ||
        app.shell().accessibilitySettings().fontScale != 1.0F ||
        app.shell().accessibilitySettings().highContrast) {
        return environmentFailure();
    }
    std::printf("designer_acceptance dpi_theme pass\n");
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

    const auto* themeNode =
        findFirstNodeOfType(app.workbench().document()->root, "ThemeScope");
    if (themeNode == nullptr) {
        std::fprintf(stderr, "designer acceptance fixture has no ThemeScope node\n");
        return 1;
    }
    const auto themeId = themeNode->id;
    const auto themeSelect = app.shell().handlers().find(
        "designer:select:" + std::to_string(themeId));
    if (themeSelect == app.shell().handlers().end()) {
        std::fprintf(stderr, "designer acceptance cannot select ThemeScope %llu\n",
                     static_cast<unsigned long long>(themeId));
        return 1;
    }
    themeSelect->second();
    (void)app.shell().renderFrame();
    const std::string themeFieldKey =
        "designer-reference-field:" + std::to_string(themeId) + ":theme";
    if (core::findNodeByKey(app.shell().root(), themeFieldKey) == nullptr) {
        std::fprintf(stderr, "designer acceptance ThemeScope reference field is missing\n");
        return 1;
    }
    const auto setTheme = [&](std::string value) {
        const auto* field = core::findNodeByKey(app.shell().root(), themeFieldKey);
        if (field == nullptr) return false;
        return app.shell().performAccessibilityAction(
                   field->identity, accessibility::kActionFocus) ==
                   accessibility::SemanticsActionStatus::Handled &&
               app.shell().performAccessibilityAction(
                   field->identity, accessibility::kActionSetValue, value) ==
                   accessibility::SemanticsActionStatus::Handled;
    };
    if (!setTheme("light")) {
        std::fprintf(stderr, "designer acceptance light ThemeScope edit was rejected\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    const auto themeReference = [&app, themeId] {
        const auto* node = findNodeById(app.workbench().document()->root, themeId);
        if (node == nullptr) return std::string{};
        const auto reference = node->references.find("theme");
        return reference == node->references.end() ? std::string{}
                                                   : reference->second;
    };
    if (themeReference() != "light" || !app.workbench().diagnostics().empty()) {
        std::fprintf(stderr, "designer acceptance light ThemeScope edit did not commit\n");
        return 1;
    }
    const auto* lightPreview = findFirstWidgetOfType(
        app.workbench().frame().widget(), core::WidgetType::ThemeScope);
    if (lightPreview == nullptr || lightPreview->themeOverride == nullptr ||
        static_cast<const style::Theme*>(lightPreview->themeOverride)->colors.pageBackground !=
            style::Theme::light().colors.pageBackground) {
        std::fprintf(stderr, "designer acceptance light ThemeScope preview is invalid\n");
        return 1;
    }
    if (!setTheme("dark")) {
        std::fprintf(stderr, "designer acceptance dark ThemeScope edit was rejected\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (themeReference() != "dark" || !app.workbench().diagnostics().empty()) {
        std::fprintf(stderr, "designer acceptance dark ThemeScope edit did not commit\n");
        return 1;
    }
    const auto* darkPreview = findFirstWidgetOfType(
        app.workbench().frame().widget(), core::WidgetType::ThemeScope);
    if (darkPreview == nullptr || darkPreview->themeOverride == nullptr ||
        static_cast<const style::Theme*>(darkPreview->themeOverride)->colors.pageBackground !=
            style::Theme::dark().colors.pageBackground) {
        std::fprintf(stderr, "designer acceptance dark ThemeScope preview is invalid\n");
        return 1;
    }
    if (!app.undo() || themeReference() != "light") {
        std::fprintf(stderr, "designer acceptance ThemeScope undo failed\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (!app.redo()) {
        std::fprintf(stderr, "designer acceptance ThemeScope undo/redo failed\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (themeReference() != "dark") {
        std::fprintf(stderr, "designer acceptance ThemeScope redo failed\n");
        return 1;
    }

    const auto invokeHandler = [&app](const std::string& name) {
        const auto found = app.shell().handlers().find(name);
        if (found == app.shell().handlers().end()) return false;
        found->second();
        return true;
    };
    // Leaving a declaration field is part of the structural command flow;
    // release its text-input domain before invoking keyboard-equivalent moves.
    app.shell().controller().releaseFieldFocus();
    app.shell().focus().clearFocus();
    const auto rootId = app.workbench().document()->root.id;
    if (!invokeHandler("designer:select:" + std::to_string(rootId))) {
        std::fprintf(stderr, "designer acceptance root selection handler is missing\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (app.workbench().selection().primary != rootId) {
        std::fprintf(stderr, "designer acceptance root selection failed\n");
        return 1;
    }
    const auto rootChildCountBefore = app.workbench().document()->root.children.size();
    if (!invokeHandler("designer:add-text")) {
        std::fprintf(stderr, "designer acceptance insert handler is missing\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (app.workbench().document()->root.children.size() !=
            rootChildCountBefore + 1 ||
        !app.workbench().selection().primary.has_value()) {
        std::fprintf(stderr, "designer acceptance insert did not commit\n");
        return 1;
    }
    const auto insertedId = *app.workbench().selection().primary;
    if (findNodeById(app.workbench().document()->root, insertedId) == nullptr) {
        std::fprintf(stderr, "designer acceptance inserted node is missing\n");
        return 1;
    }
    if (!invokeHandler("designer:duplicate")) {
        std::fprintf(stderr, "designer acceptance duplicate handler is missing\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (app.workbench().document()->root.children.size() !=
            rootChildCountBefore + 2 ||
        !app.workbench().selection().primary.has_value()) {
        std::fprintf(stderr, "designer acceptance duplicate did not commit\n");
        return 1;
    }
    const auto duplicateId = *app.workbench().selection().primary;
    if (duplicateId == insertedId ||
        findNodeById(app.workbench().document()->root, duplicateId) == nullptr) {
        std::fprintf(stderr, "designer acceptance duplicate identity is invalid\n");
        return 1;
    }
    const auto revisionBeforeCopy = app.workbench().documentRevision();
    if (!invokeHandler("designer:copy")) {
        std::fprintf(stderr, "designer acceptance copy handler is missing\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (app.workbench().documentRevision() != revisionBeforeCopy ||
        app.workbench().dirty() == false || !invokeHandler("designer:paste")) {
        std::fprintf(stderr, "designer acceptance copy/paste setup failed\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (app.workbench().document()->root.children.size() !=
            rootChildCountBefore + 3 ||
        !app.workbench().selection().primary.has_value()) {
        std::fprintf(stderr, "designer acceptance paste did not commit\n");
        return 1;
    }
    const auto pastedId = *app.workbench().selection().primary;
    if (pastedId == insertedId || pastedId == duplicateId ||
        findNodeById(app.workbench().document()->root, pastedId) == nullptr) {
        std::fprintf(stderr, "designer acceptance paste identity is invalid\n");
        return 1;
    }
    const auto indexOf = [&app](dsl::DesignNodeId id) {
        const auto& children = app.workbench().document()->root.children;
        for (std::size_t index = 0; index < children.size(); ++index) {
            if (children[index].id == id) return index;
        }
        return children.size();
    };
    const auto pastedIndex = indexOf(pastedId);
    if (pastedIndex == 0 || !invokeHandler("designer:move-up")) {
        std::fprintf(stderr, "designer acceptance move-up setup failed\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (indexOf(pastedId) != pastedIndex - 1 ||
        app.workbench().selection().primary != pastedId ||
        !invokeHandler("designer:move-down")) {
        std::fprintf(stderr,
                     "designer acceptance reorder did not commit (before=%zu after=%zu selected=%llu)\n",
                     pastedIndex, indexOf(pastedId),
                     static_cast<unsigned long long>(
                         app.workbench().selection().primary.value_or(0)));
        return 1;
    }
    (void)app.shell().renderFrame();
    if (indexOf(pastedId) != pastedIndex ||
        !invokeHandler("designer:remove")) {
        std::fprintf(stderr, "designer acceptance remove setup failed\n");
        return 1;
    }
    (void)app.shell().renderFrame();
    if (app.workbench().document()->root.children.size() !=
            rootChildCountBefore + 2 ||
        findNodeById(app.workbench().document()->root, pastedId) != nullptr ||
        !app.workbench().dirty() || !app.workbench().diagnostics().empty()) {
        std::fprintf(stderr, "designer acceptance structural edit did not commit\n");
        return 1;
    }
    const auto finalRootChildCount =
        app.workbench().document()->root.children.size();

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
    const auto* reopenedTheme = findNodeById(reopenedDocument.root, themeId);
    const auto* reopenedInserted = findNodeById(reopenedDocument.root, insertedId);
    const auto* reopenedDuplicate = findNodeById(reopenedDocument.root, duplicateId);
    const auto* reopenedPasted = findNodeById(reopenedDocument.root, pastedId);
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
        reopenedValue != editedText || reopenedTheme == nullptr ||
        reopenedTheme->references.find("theme") == reopenedTheme->references.end() ||
        reopenedTheme->references.at("theme") != "dark" ||
        reopenedInserted == nullptr || reopenedDuplicate == nullptr ||
        reopenedPasted != nullptr ||
        reopenedDocument.root.children.size() != finalRootChildCount ||
        !reopened.diagnostics().empty()) {
        std::fprintf(stderr, "designer acceptance reopen changed declaration\n");
        std::filesystem::remove(saved, cleanup);
        return 1;
    }
    std::filesystem::remove(saved, cleanup);
    std::printf("designer_acceptance document %s\n",
                reopenedDocument.documentId.c_str());
    std::printf("designer_acceptance selected %llu\n",
                static_cast<unsigned long long>(nodeId));
    std::printf("designer_acceptance theme_reference pass\n");
    std::printf("designer_acceptance structure_edit pass\n");
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
    // 未指定文件时以内置默认样例（1024×768 可拖画板）开箱运行；显式
    // 指定但加载失败仍按窗口冒烟契约退出，watch 模式保留上一有效帧。
    if (!designerOptions.filename.empty() && !requestedFileLoaded &&
        !designerOptions.watch) {
        return 1;
    }
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
    // 默认编辑器窗口 1024×768：常用桌面设计档；1024×768 画板经
    // fit-to-view 缩放居中呈现（designer_app maybeFitCanvasView）。
    runOptions.windowDesc.width = 1024;
    runOptions.windowDesc.height = 768;
    runOptions.maxFrames = designerOptions.maxFrames;
    runOptions.resourceManager = app.resourceManager();
    // 桌面系统字体由 runApp 默认注入（systemFonts 能力；见
    // RunOptions.fontFactory 注释）；headless/acceptance 不经过 runApp，
    // 保持占位度量与帧哈希确定性。
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
    // 运行预览不再常驻第二个 OS 窗口：startPreview 成功后在设计窗口内
    // 以模态覆盖层呈现（按需出现，停止/Esc 即隐藏）；previewShell 保留
    // 供 headless 测试与独立会话使用，不再创建 OS 窗口。
    const int result =
        lumen::app::runApp(app.shell(), host, std::move(runOptions));
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
