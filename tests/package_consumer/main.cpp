#include <iostream>
#include "lumen/app/app_shell.h"
#include "lumen/dsl/dsl.h"
#include "lumen/platform/sdl3_host.h"
#if SDK_WITH_SKIA
#include "lumen/render/skia_renderer.h"
#endif
#if SDK_WITH_GPU
#include "lumen/render/skia_gpu_renderer.h"
#endif

int main() {
    // Force platform/native service symbols into the link without needing a
    // desktop session. AppShell also exercises the installed render/text libs.
    lumen::platform::Sdl3ApplicationHost host;
    lumen::app::ShellConfig config;
    config.initialView = {160, 80};
    config.build = [] { return lumen::dsl::text("Installed SDK"); };
    lumen::app::AppShell shell(std::move(config));
    if (shell.renderFrame() == 0 || shell.pixels().width != 160) return 1;
#if SDK_WITH_SKIA
    lumen::render::SkiaRenderer renderer;
    renderer.beginFrame({32, 32});
    renderer.drawRect({0, 0, 32, 32}, lumen::core::Color::fromRGBA(12, 34, 56));
    renderer.endFrame();
    if (renderer.pixels().width != 32) return 2;
#endif
#if SDK_WITH_GPU
    // Link the complete GPU dependency closure; hardware rendering is covered
    // by the separate windowed acceptance. A null surface must fail safely.
    std::string diagnostic;
    if (lumen::render::createSkiaGpuRenderer({}, &diagnostic) || diagnostic.empty()) return 3;
#endif
    std::cout << "SDK consumer passed\n";
}
