#pragma once

#include <memory>
#include <string>

#include "lumen/platform/platform_window.h"

namespace lumen::platform {

struct Sdl3WindowDesc {
    std::string title{"Lumen"};
    int width{800};
    int height{600};
    bool resizable{true};
    // Match OS scaling so drawable pixels track the logical size (plan §2).
    bool highPixelDensity{true};
};

// Creates an SDL3-backed window. Returns nullptr on failure; SDL's error is
// logged to stderr. SDL types stay behind the interface — only the factory
// declaration is public.
[[nodiscard]] std::unique_ptr<PlatformWindow> createSdl3Window(
    const Sdl3WindowDesc& desc);

}  // namespace lumen::platform
