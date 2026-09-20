// Alpha plan P0/P4: fixed Gallery captures, without changing application UI.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "gallery_app.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: lumen-alpha-gallery OUTPUT_DIRECTORY\n");
        return 2;
    }
    try {
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        std::ofstream manifest(directory / "frames.json");
        manifest << "[\n";
        bool first = true;
        for (bool dark : {true, false}) {
            for (float dpi : {1.0F, 1.25F, 2.0F}) {
                for (const std::string state :
                     {"rest", "hover", "pressed", "maximized", "menu", "controls", "buttons"}) {
                    lumen::examples::GalleryApp app;
                    app.setView({1280, 800});
                    app.setDeviceScale(dpi);
                    lumen::accessibility::AccessibilitySettings settings;
                    settings.reduceAnimation = true;
                    app.setAccessibilitySettings(settings);
                    app.setTheme(lumen::style::Theme::fromSettings(settings, dark));
                    app.shell().setClearColor(lumen::core::Color::fromRGBA(0, 0, 0, 0));
                    const std::string route =
                        state == "controls" || state == "buttons" ? state : "home";
                    if (!app.showSample(route)) {
                        throw std::runtime_error("missing Gallery route");
                    }
                    (void)app.renderFrame(true);
                    if (state == "hover" || state == "pressed") {
                        app.shell().setVisualPreviewState(
                            "window-close",
                            lumen::style::WidgetState{.hovered = state == "hover",
                                                      .pressed = state == "pressed"});
                    } else if (state == "maximized") {
                        app.noteWindowMaximized(true);
                    } else if (state == "menu") {
                        app.shell().keyDown(lumen::core::Key::None, lumen::core::kModifierAlt, 'f');
                        if (!app.menuBarOpen()) {
                            throw std::runtime_error("menu did not open");
                        }
                    }
                    const auto hash = app.renderFrame(true);
                    const auto& pixels = app.pixels();
                    const auto name = std::string(dark ? "dark-" : "light-") +
                                      std::to_string(static_cast<int>(dpi * 100)) + "-" + state;
                    std::ofstream raw(directory / (name + ".rgba"), std::ios::binary);
                    raw.write(reinterpret_cast<const char*>(pixels.rgba.data()),
                              pixels.rgba.size());
                    std::ofstream meta(directory / (name + ".rgba.txt"));
                    // P0 actual CPU representation. Replaced with buffer mode in P1.
                    meta << "width=" << pixels.width << "\nheight=" << pixels.height
                         << "\nalpha_mode=straight\nfonts=placeholder\n";
                    if (!raw || !meta) {
                        throw std::runtime_error("frame write failed");
                    }
                    manifest << (first ? "" : ",\n") << "  {\"name\": \"" << name
                             << "\", \"frame_hash\": \"" << std::hex << hash << std::dec << "\"}";
                    first = false;
                }
            }
        }
        manifest << "\n]\n";
        if (!manifest) {
            throw std::runtime_error("manifest write failed");
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
