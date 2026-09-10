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
    // 以 SDL_WINDOW_OPENGL 创建窗口供 Skia GPU 适配使用（v0.2 阶段7C）。
    // 此时窗口没有 SDL 呈现器；CPU present 返回 Rejected，交换由 GPU 适配
    // 的 endFrame 完成。
    bool opengl{false};
    // Present directly through a software window surface after GPU failure.
    // Mutually exclusive with opengl; never selects an SDL GPU renderer.
    // Requires a native framebuffer (Windows/X11); otherwise creation fails.
    bool softwarePresentation{false};
};

// Creates an SDL3-backed window. Returns nullptr on failure; SDL's error is
// logged to stderr. SDL types stay behind the interface — only the factory
// declaration is public.
[[nodiscard]] std::unique_ptr<PlatformWindow> createSdl3Window(
    const Sdl3WindowDesc& desc);

}  // namespace lumen::platform
