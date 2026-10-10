// Native AT-SPI Designer fixture: the client drives the real DesignerApp
// outline and property transaction through the production provider.
// Contract: docs/lumen-accessibility-provider-design.md §6 and
// docs/lumen-designer-prerequisites.md §4.18.
#include "designer_app.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "lumen/accessibility/bridge.h"

int main(int argc, char** argv) {
    using namespace lumen;
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <temporary-output.design>\n", argv[0]);
        return 2;
    }

    designer_app::DesignerApp app;
    app.attach();
    if (!app.loadSource(
            "page designer { Column(key: \"root\") {"
            " Text(\"Title\", key: \"title\")"
            " TextField(placeholder: \"Blocked preview\", enabled: false,"
            " key: \"blocked\") } }",
            "atspi-designer.lumen")) {
        std::fprintf(stderr, "Designer AT-SPI fixture failed to load source\n");
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
        return 1;
    }
    app.shell().setAccessibilityBridge(bridge.get());
    bridge->noteWindowActive(true);

    // Validate the document as well as the provider snapshot. The client
    // writes this final Unicode value after an intermediate empty edit.
    const std::string expectedText = "重命名🙂e\xCC\x81";
    const auto declarationText = [&app] {
        return std::get<std::string>(app.workbench().document()
                                        ->root.children.front()
                                        .properties.at("text").value);
    };
    bool verified = false;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(40);
    while (std::chrono::steady_clock::now() < deadline) {
        bridge->pump();
        (void)app.shell().renderFrame();
        if (!verified && declarationText() == expectedText) {
            const auto target = app.workbench().document()->root.children.front().id;
            const auto originalDocument = *app.workbench().document();
            bool valid = app.workbench().selection().primary == target &&
                         app.workbench().selection().ids.size() == 1 &&
                         app.workbench().dirty();
            valid = valid && app.undo() && declarationText().empty();
            valid = valid && app.redo() &&
                    *app.workbench().document() == originalDocument;
            valid = valid && app.saveDesignFile(argv[1]) &&
                    !app.workbench().dirty();
            designer_app::DesignerApp reopened;
            reopened.attach();
            valid = valid && reopened.loadDesignFile(argv[1]);
            if (valid) {
                const auto& saved = *reopened.workbench().document();
                valid = saved.documentId == originalDocument.documentId &&
                        saved.root.children.front().id == target &&
                        std::get<std::string>(saved.root.children.front()
                                                 .properties.at("text").value) == expectedText;
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
    app.shell().setAccessibilityBridge(nullptr);
    return 0;
}
