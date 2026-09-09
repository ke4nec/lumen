// Counter sample (plan §7): interactive UI over the C++ declarative DSL with
// the CPU renderer and the SDL3 platform backend. `--headless` runs the same
// frame pipeline without a window and prints stable frame hashes (plan §9).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>

#include "counter_app.h"
#include "lumen/platform/sdl3_window.h"

namespace {

using lumen::core::Key;
using lumen::core::Offset;
using lumen::examples::CounterApp;

int runHeadless() {
    CounterApp app;
    app.setView(lumen::core::Size{800.0F, 600.0F});
    std::printf("frame0 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    // Simulate: click Increment, type into the name field, resize.
    const auto centerOf = [&app](const char* key) {
        const lumen::core::RenderNode* node =
            lumen::core::findNodeByKey(app.root(), key);
        return lumen::core::absoluteOffset(app.root(), key) +
               Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    };
    app.pointerDown(centerOf("increment-button"));
    app.pointerUp(centerOf("increment-button"));
    std::printf("frame1 %016llx counter=%d\n",
                static_cast<unsigned long long>(app.renderFrame()),
                app.counterValue());

    app.pointerDown(centerOf("name-field"));
    app.pointerUp(centerOf("name-field"));
    app.textInput("Lumen");
    std::printf("frame2 %016llx name=%s\n",
                static_cast<unsigned long long>(app.renderFrame()),
                app.state().get("name").c_str());

    app.setView(lumen::core::Size{1024.0F, 768.0F});
    std::printf("frame3 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));
    return 0;
}

int runWindowed() {
    lumen::platform::Sdl3WindowDesc desc;
    desc.title = "Lumen Counter - Stage 3";
    desc.width = 800;
    desc.height = 600;
    auto window = lumen::platform::createSdl3Window(desc);
    if (window == nullptr) {
        return 1;
    }

    CounterApp app;
    app.setView(window->logicalSize());
    bool running = true;
    while (running) {
        for (auto event = window->pollEvent();
             event.type != lumen::platform::EventType::None;
             event = window->pollEvent()) {
            switch (event.type) {
                case lumen::platform::EventType::Quit:
                    running = false;
                    break;
                case lumen::platform::EventType::Resize:
                    // Root constraints track the new logical size (plan §5.3).
                    app.setView(window->logicalSize());
                    app.setDeviceScale(
                        window->drawableSize().width /
                        std::max(1.0F, window->logicalSize().width));
                    break;
                case lumen::platform::EventType::PointerDown:
                    app.pointerDown(event.position);
                    break;
                case lumen::platform::EventType::PointerUp:
                    app.pointerUp(event.position);
                    break;
                case lumen::platform::EventType::TextInput:
                    app.textInput(event.text);
                    break;
                case lumen::platform::EventType::KeyDown:
                    app.keyDown(static_cast<Key>(event.keyCode));
                    break;
                default:
                    break;
            }
        }
        window->setTextInputEnabled(app.wantsTextInput());
        app.renderFrame();
        window->present(app.pixels());
        SDL_Delay(16);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            return runHeadless();
        }
    }
    return runWindowed();
}
