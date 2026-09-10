#pragma once

#include <algorithm>
#include <memory>

#include "counter_app.h"
#include "lumen/platform/sdl3_window.h"

namespace lumen::examples {

// OpenGL windows have no CPU presenter. Replace the window while retaining
// the app's state, focus and logical size. Detach the borrowed renderer first;
// GPU resources and their context must die before the window/video subsystem.
inline bool recreateCpuWindow(CounterApp& app,
                              std::unique_ptr<render::Renderer>& gpuRenderer,
                              std::unique_ptr<platform::PlatformWindow>& window,
                              platform::Sdl3WindowDesc desc) {
    const core::Size logical = window->logicalSize();
    desc.width = std::max(1, static_cast<int>(logical.width));
    desc.height = std::max(1, static_cast<int>(logical.height));
    desc.opengl = false;
    desc.softwarePresentation = true;
    window->setTextInputEnabled(false);
    app.cancelComposition();
    app.setRenderer(nullptr);
    gpuRenderer.reset();
    window.reset();
    window = platform::createSdl3Window(desc);
    if (window == nullptr) {
        return false;
    }
    app.setView(window->logicalSize());
    app.setDeviceScale(window->drawableSize().width /
                       std::max(1.0F, window->logicalSize().width));
    window->setTextInputEnabled(app.wantsTextInput());
    return true;
}

}  // namespace lumen::examples
