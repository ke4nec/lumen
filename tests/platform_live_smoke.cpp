// Real desktop acceptance probe.  Unlike platform_smoke_tests.cpp this tool
// never selects the dummy SDL video driver: the caller chooses X11, Wayland,
// Cocoa, or Win32 through the environment.

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "lumen/platform/sdl3_host.h"
#include "lumen/render/renderer.h"

namespace {

struct Options {
    int seconds{10};
    bool resizeBurst{false};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--seconds" && index + 1 < argc) {
            options.seconds = std::max(1, std::atoi(argv[++index]));
        } else if (argument == "--resize-burst") {
            options.resizeBurst = true;
        } else {
            std::fprintf(stderr, "usage: %s [--seconds N] [--resize-burst]\n",
                         argv[0]);
            std::exit(2);
        }
    }
    return options;
}

bool hasResizeEvent(lumen::platform::Sdl3ApplicationHost& host,
                    lumen::core::WindowId id) {
    lumen::core::HostEvent event;
    bool resized = false;
    while (host.pollEvent(event)) {
        if (event.window == id && event.type == lumen::core::HostEventType::Resize) {
            resized = true;
        }
    }
    return resized;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    lumen::platform::Sdl3ApplicationHost host;
    if (!host.initialize()) {
        std::fprintf(stderr, "host initialize failed\n");
        return 1;
    }
    const lumen::platform::WindowDesc desc{
        "Lumen platform acceptance", 640, 420, true, true, false, false,
        false, false};
    const auto id = host.createWindow(desc);
    if (!id.has_value()) {
        std::fprintf(stderr, "window creation failed\n");
        return 2;
    }
    auto* window = host.platformWindow(*id);
    if (window == nullptr) {
        std::fprintf(stderr, "platform window unavailable\n");
        return 3;
    }

    // Exercise the platform clipboard in the same process as the window.
    auto* clipboard = host.clipboard();
    const std::string clipboardValue = "lumen-live-clipboard";
    if (clipboard == nullptr || !clipboard->setText(clipboardValue) ||
        clipboard->text() != clipboardValue) {
        std::fprintf(stderr, "clipboard round-trip failed\n");
        return 4;
    }

    // Start the real text-input session and update the candidate anchor.  The
    // OS IME may not show a candidate until the user types; SDL still routes
    // the session to the native input method on each desktop.
    auto* textInput = host.textInputSession(*id);
    if (textInput == nullptr) {
        std::fprintf(stderr, "text input session unavailable\n");
        return 5;
    }
    textInput->start();
    textInput->setEditingState(lumen::platform::TextInputEditingState{
        "", 0, 0, true, 0, 0,
        lumen::core::Rect::fromXYWH(24.0F, 24.0F, 200.0F, 32.0F)});

    lumen::render::PixelBuffer pixels;
    pixels.width = 640;
    pixels.height = 420;
    pixels.alphaMode = lumen::render::AlphaMode::Opaque;
    pixels.rgba.assign(static_cast<std::size_t>(pixels.width) * pixels.height * 4,
                       32);
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(options.seconds);
    bool resized = false;
    std::size_t presented = 0;
    std::size_t resizeRequests = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (options.resizeBurst && (presented % 8U) == 0U) {
            const int width = 520 + static_cast<int>((presented / 8U) % 5U) * 48;
            const int height = 340 + static_cast<int>((presented / 8U) % 4U) * 24;
            SDL_Window* native = static_cast<SDL_Window*>(
                window->nativeSurface().nativeWindow);
            SDL_SetWindowSize(native, width, height);
            ++resizeRequests;
        }
        pixels.rgba[0] = static_cast<std::uint8_t>(elapsed % 255);
        if (window->present(pixels) != lumen::platform::PresentResult::Ok) {
            std::fprintf(stderr, "present failed: %s\n", SDL_GetError());
            return 6;
        }
        ++presented;
        resized = hasResizeEvent(host, *id) || resized;
        host.waitForEvents(8);
    }
    textInput->stop();
    resized = hasResizeEvent(host, *id) || resized;
    if (options.resizeBurst && (!resized || resizeRequests == 0)) {
        std::fprintf(stderr, "resize burst produced no resize event\n");
        return 7;
    }

    const char* driver = SDL_GetCurrentVideoDriver();
    std::printf("{\"driver\":\"%s\",\"seconds\":%d,\"frames\":%zu,"
                "\"clipboard\":true,\"ime_session\":true,"
                "\"resize_event\":%s,\"resize_requests\":%zu}\n",
                driver != nullptr ? driver : "unknown", options.seconds,
                presented, resized ? "true" : "false", resizeRequests);
    host.destroyWindow(*id);
    host.shutdown();
    return 0;
}
