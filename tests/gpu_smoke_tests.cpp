// v0.2 阶段7C GPU smoke（plan §5/§7E）：硬件 GPU 只作增强 smoke；探测
// 失败的环境跳过并保留诊断。有 GPU 时走完整路径——隐藏 GL 窗口、
// Ganesh 初始化、命令提交与设备健康查询。

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

#include <memory>
#include <string>

#include <SDL3/SDL.h>

#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/render/painter.h"
#include "lumen/render/renderer.h"
#include "lumen/render/skia_gpu_renderer.h"

using lumen::core::Constraints;
using lumen::core::Size;
using lumen::core::Widget;
using lumen::core::WidgetType;
using lumen::layout::LayoutEngine;
using lumen::render::FrameInfo;
using lumen::render::Renderer;
using lumen::render::recordScene;

namespace {

Widget sampleScene() {
    Widget text;
    text.type = WidgetType::Text;
    text.text = "GPU smoke";
    Widget page;
    page.type = WidgetType::Container;
    page.color = lumen::core::Color::fromRGBA(24, 24, 27);
    page.children = {text};
    return page;
}

}  // namespace

TEST_CASE("gpu_probe_reports_availability_with_diagnostics", "[gpu]") {
    std::string diagnostics;
    const bool available = lumen::render::probeSkiaGpuAvailable(&diagnostics);
    if (!available) {
        INFO("gpu unavailable: " << diagnostics);
    }
    // 无论可用与否都必须给出可解释的结果：可用 → true；不可用 → 明确
    // 诊断原因（plan 7C 出口条件：GPU 诊断日志包含选择结果和回退原因）。
    CHECK((available || !diagnostics.empty()));
}

TEST_CASE("gpu_renderer_submits_frame_or_reports_fallback", "[gpu]") {
    std::string diagnostics;
    if (!lumen::render::probeSkiaGpuAvailable(&diagnostics)) {
        INFO("skipping: no GPU (" << diagnostics << ")");
        SUCCEED("gpu unavailable — fallback path covered by cpu tests");
        return;
    }

    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window = SDL_CreateWindow(
        "lumen-gpu-test", 128, 96, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    REQUIRE(window != nullptr);

    lumen::render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window;
    desc.widthPixels = 128;
    desc.heightPixels = 96;
    // 隐藏窗口：交换在部分驱动上会阻塞，提交止步于 flush（desc 语义）。
    desc.allowSwap = false;
    std::unique_ptr<Renderer> renderer =
        lumen::render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer != nullptr);

    const auto caps = renderer->capabilities();
    CHECK(caps.gpu);
    CHECK(std::string(caps.backendName) == "skia-gpu");
    CHECK_FALSE(caps.partialSubmit);

    FrameInfo info;
    info.viewport = Size{128.0F, 96.0F};
    renderer->submit(recordScene(LayoutEngine::layout(
                         sampleScene(), Constraints::tight(Size{128.0F, 96.0F}))),
                     info);
    const auto stats = renderer->stats();
    CHECK(stats.framesSubmitted == 1);
    CHECK(stats.commandCount > 0);
    CHECK(lumen::render::skiaGpuRendererAlive(*renderer));

    // resize → resetSurface → 再提交一帧（FrameInfo 视口与 surface 尺寸
    // 保持一致：包装的 FBO 尺寸必须匹配窗口实际后备缓冲）。
    lumen::render::RenderSurfaceDesc surface;
    surface.nativeWindow = window;
    surface.windowSystem = "sdl3";
    surface.widthPixels = 256;
    surface.heightPixels = 192;
    renderer->resetSurface(surface);
    FrameInfo resized = info;
    resized.viewport = Size{256.0F, 192.0F};
    renderer->submit(recordScene(LayoutEngine::layout(
                         sampleScene(), Constraints::tight(Size{256.0F, 192.0F}))),
                     resized);
    CHECK(renderer->stats().framesSubmitted == 2);
    CHECK(lumen::render::skiaGpuRendererAlive(*renderer));

    // 销毁顺序：先释放渲染器（GL 上下文与 GPU 资源），再销毁窗口与
    // 视频子系统——存活上下文上销毁窗口会阻塞部分驱动。
    renderer.reset();
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
