// Native AT-SPI client fixture: real D-Bus provider and normal AppShell actions.
#include "lumen/app/app_shell.h"
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
    using namespace lumen;
    app::ShellConfig config;
    config.initialView = {320, 240};
    app::AppShell* app = nullptr;
    config.build = [&] {
        auto button = core::makeButton(app->state().get("clicked") == "yes" ? "Applied" : "Apply");
        button.key = "apply";
        button.onClick = "apply";
        auto slider = core::makeSlider("value", "volume");
        slider.semanticsLabel = "Volume";
        return core::makeColumn({std::move(button), std::move(slider)});
    };
    app::AppShell shell(config);
    app = &shell;
    shell.state().set("value", "50");
    shell.handlers()["apply"] = [&] {
        shell.state().set("clicked", "yes");
        shell.markDirty();
    };
    accessibility::PlatformAccessibilityHost host;
    host.applicationName = "Lumen AT-SPI test";
    host.dispatch = [&](const std::string& id, std::uint32_t action,
                        const std::string& value, float delta) {
        return shell.performAccessibilityAction(id, action, value, delta);
    };
    std::string diagnostics;
    auto bridge = accessibility::createPlatformAccessibilityBridge(host, &diagnostics);
    if (!bridge || !bridge->available()) {
        std::fprintf(stderr, "AT-SPI unavailable: %s\n", diagnostics.c_str());
        return 1;
    }
    shell.setAccessibilityBridge(bridge.get());
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < end) {
        bridge->pump();
        static_cast<void>(shell.renderFrame());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    shell.setAccessibilityBridge(nullptr);
}
