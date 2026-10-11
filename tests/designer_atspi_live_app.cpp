// Native AT-SPI Designer fixture: the client drives the real DesignerApp
// outline and property transaction through the production provider.
// Contract: docs/lumen-accessibility-provider-design.md §6 and
// docs/lumen-designer-prerequisites.md §4.18.
#include "designer_app.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include "lumen/accessibility/bridge.h"

int main(int argc, char** argv) {
    using namespace lumen;
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <temporary-output.design>\n", argv[0]);
        return 2;
    }

    const std::filesystem::path fixturePath =
        std::filesystem::path(argv[1]).concat(".fixture.design");
    {
        std::ofstream fixture(fixturePath, std::ios::binary | std::ios::trunc);
        if (!fixture) {
            std::fprintf(stderr, "Designer AT-SPI fixture cannot be created\n");
            return 1;
        }
        fixture << R"DESIGN({
  "documentId": "atspi-designer",
  "format": "lumen.design",
  "pageName": "designer",
  "schemaVersion": 1,
  "root": {
    "id": "1",
    "type": "Column",
    "properties": {"key": {"kind": "string", "value": "root"}},
    "children": [
      {"id": "2", "type": "Text", "properties": {
        "key": {"kind": "string", "value": "title"},
        "text": {"kind": "string", "value": "Title"}
      }},
      {"id": "3", "type": "TextField", "properties": {
        "key": {"kind": "string", "value": "blocked"},
        "placeholder": {"kind": "string", "value": "Blocked preview"},
        "enabled": {"kind": "bool", "value": false}
      }},
      {"id": "4", "type": "ThemeScope", "properties": {
        "key": {"kind": "string", "value": "theme-scope"}
      }, "references": {"theme": "light"}}
    ]
  }
})DESIGN";
    }

    designer_app::DesignerApp app;
    app.attach();
    if (!app.loadDesignFile(fixturePath.string())) {
        std::fprintf(stderr, "Designer AT-SPI fixture failed to load source\n");
        std::error_code ignored;
        std::filesystem::remove(fixturePath, ignored);
        return 1;
    }
    app.shell().setView(core::Size{1280.0F, 800.0F});

    accessibility::PlatformAccessibilityHost host;
    host.applicationName = "Lumen Designer AT-SPI test";
    host.dispatch = [&app](const std::string& id, std::uint32_t action,
                           const std::string& value, float scrollDelta) {
        return app.shell().performAccessibilityAction(id, action, value,
                                                       scrollDelta);
    };

    std::unique_ptr<accessibility::AccessibilityBridge> bridge;
    host.activateWindow = [&bridge] {
        if (bridge == nullptr) return false;
        bridge->noteWindowActive(true);
        return true;
    };
    std::string diagnostics;
    bridge = accessibility::createPlatformAccessibilityBridge(host, &diagnostics);
    if (bridge == nullptr || !bridge->available()) {
        std::fprintf(stderr, "AT-SPI unavailable: %s\n", diagnostics.c_str());
        std::error_code ignored;
        std::filesystem::remove(fixturePath, ignored);
        return 1;
    }
    app.shell().setAccessibilityBridge(bridge.get());
    bridge->noteWindowActive(true);

    // Validate the document as well as the provider snapshot. The client
    // writes this final Unicode value after an intermediate empty edit.
    const std::string expectedText = "重命名🙂e\xCC\x81";
    const std::string expectedTheme = "dark";
    const auto declarationText = [&app] {
        return std::get<std::string>(app.workbench().document()
                                        ->root.children.front()
                                        .properties.at("text").value);
    };
    const auto declarationTheme = [&app] {
        return app.workbench().document()->root.children.back().references.at(
            "theme");
    };
    bool verified = false;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(40);
    while (std::chrono::steady_clock::now() < deadline) {
        bridge->pump();
        (void)app.shell().renderFrame();
        if (!verified && declarationText() == expectedText &&
            declarationTheme() == expectedTheme) {
            const auto textTarget =
                app.workbench().document()->root.children.front().id;
            const auto themeTarget =
                app.workbench().document()->root.children.back().id;
            const auto originalDocument = *app.workbench().document();
            bool valid = app.workbench().selection().primary == themeTarget &&
                         app.workbench().selection().ids.size() == 1 &&
                         app.workbench().dirty();
            valid = valid && app.undo() && declarationTheme() == "light" &&
                    declarationText() == expectedText;
            valid = valid && app.undo() && declarationText().empty();
            valid = valid && app.redo() && declarationText() == expectedText;
            valid = valid && app.redo() && declarationTheme() == expectedTheme &&
                    *app.workbench().document() == originalDocument;
            valid = valid && app.saveDesignFile(argv[1]) &&
                    !app.workbench().dirty();
            designer_app::DesignerApp reopened;
            reopened.attach();
            valid = valid && reopened.loadDesignFile(argv[1]);
            if (valid) {
                const auto& saved = *reopened.workbench().document();
                valid = saved.documentId == originalDocument.documentId &&
                        saved.root.children.front().id == textTarget &&
                        saved.root.children.back().id == themeTarget &&
                        std::get<std::string>(saved.root.children.front()
                                                 .properties.at("text").value) == expectedText &&
                        saved.root.children.back().references.at("theme") ==
                            expectedTheme;
            }
            if (!valid) {
                std::fprintf(stderr, "Designer AT-SPI document transaction failed\n");
                app.shell().setAccessibilityBridge(nullptr);
                return 1;
            }
            verified = true;
            std::printf("designer_atspi_transaction pass\n");
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::error_code ignored;
    std::filesystem::remove(fixturePath, ignored);
    app.shell().setAccessibilityBridge(nullptr);
    return 0;
}
