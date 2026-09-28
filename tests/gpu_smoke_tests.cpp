// v0.2 阶段7C GPU smoke（plan §5/§7E）：硬件 GPU 只作增强 smoke；探测
// 失败的环境跳过并保留诊断。有 GPU 时走完整路径——隐藏 GL 窗口、
// Ganesh 初始化、命令提交与设备健康查询。

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <cmath>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "lumen/core/icon_id.h"
#include "lumen/core/widget.h"
#include "lumen/core/scrollbar.h"
#include "lumen/dsl/dsl.h"
#include "lumen/layout/layout.h"
#include "lumen/render/painter.h"
#include "lumen/render/renderer.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/resource_manager.h"
#include "lumen/render/skia_gpu_renderer.h"
#include "lumen/style/theme.h"
#include "lumen/widgets/list.h"
#include "lumen/widgets/tree.h"

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

// Keep SDL alive until after the renderer and its window are destroyed, even
// when a REQUIRE aborts a test. CTest runs GPU tests in separate processes.
struct VideoSession {
    ~VideoSession() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }
};

using TestWindow = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;

// GPU 平价（lumen-skia-gpu-parity-plan §5）共用的读回与采样助手：
// y 翻转与既有用例同式（采样行取 h-1-y）；锚点像素取 lround 设备坐标。
std::vector<unsigned char> readBackFramebuffer(int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    REQUIRE(glGetError() == GL_NO_ERROR);
    return pixels;
}

lumen::core::Color samplePixel(const std::vector<unsigned char>& pixels,
                               int width, int height, float x, float y,
                               float scale) {
    const auto at = ((height - 1 - static_cast<int>(std::lround(y * scale))) *
                         width +
                     static_cast<int>(std::lround(x * scale))) * 4;
    return lumen::core::Color{pixels[at], pixels[at + 1], pixels[at + 2],
                              pixels[at + 3]};
}

bool nearColor(const lumen::core::Color& actual,
               const lumen::core::Color& expected, int tolerance) {
    return std::abs(static_cast<int>(actual.r) - static_cast<int>(expected.r)) <= tolerance &&
           std::abs(static_cast<int>(actual.g) - static_cast<int>(expected.g)) <= tolerance &&
           std::abs(static_cast<int>(actual.b) - static_cast<int>(expected.b)) <= tolerance;
}

// 通道最大差：混合色断言用（与两个端点的距离都不小于阈值）。
int colorDistance(const lumen::core::Color& a, const lumen::core::Color& b) {
    return std::max({std::abs(static_cast<int>(a.r) - static_cast<int>(b.r)),
                     std::abs(static_cast<int>(a.g) - static_cast<int>(b.g)),
                     std::abs(static_cast<int>(a.b) - static_cast<int>(b.b))});
}

// 线身命中：锚点 3×3 设备像素邻域内至少一点"更接近墨色而非背景"。
// 段中点/弧顶点与像素网格的相位差会把单点采样落到 AA 边缘（1.5px
// 线宽的实心核约 ±0.25px），且各 GL 光栅化器的 AA 分布不同——
// llvmpipe 中心像素常为全覆盖，Apple 软件 GL 可能把覆盖摊到多像素而
// 无一全饱和（2026-09-28 macOS CI 实测）。以与两端点的相对距离做
// 存在性判定：不锁覆盖率，但命令被丢弃（全背景）时必然失败。
// px/py 为 GL 设备坐标（原点左下），读回行取 h-1-y。
bool strokeHits(const std::vector<unsigned char>& pixels, int width,
                int height, float x, float y, float scale,
                const lumen::core::Color& ink,
                const lumen::core::Color& background) {
    const int px = static_cast<int>(std::lround(x * scale));
    const int py = static_cast<int>(std::lround(y * scale));
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int sx = px + dx;
            const int sy = py + dy;
            if (sx < 0 || sx >= width || sy < 0 || sy >= height) {
                continue;
            }
            const auto at =
                (static_cast<std::size_t>(height - 1 - sy) * width + sx) * 4;
            const lumen::core::Color pixel{pixels[at], pixels[at + 1],
                                           pixels[at + 2], pixels[at + 3]};
            if (colorDistance(pixel, ink) < colorDistance(pixel, background)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

TEST_CASE("gpu_scrollbar_states_and_axes_match_tokens_at_both_scales", "[gpu][scrollbar]") {
    using namespace lumen;
    using namespace lumen::core;
    std::string diagnostics;
    if (!render::probeSkiaGpuAvailable(&diagnostics)) SKIP("GPU unavailable: " << diagnostics);
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-scrollbar-test", 240, 200,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 240;
    desc.heightPixels = 200;
    desc.allowSwap = false;
    auto renderer = render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer);
    const auto theme = style::Theme::dark();
    accessibility::AccessibilitySettings settings;
    style::InteractionStateSnapshot interaction;
    style::StyleContext context{theme, interaction, settings, 1};
    for (const float scale : {1.0F, 2.0F}) {
        for (const auto axis : {ScrollAxis::Vertical, ScrollAxis::Horizontal}) {
            FrameInfo info;
            info.viewport = {240 / scale, 200 / scale};
            info.deviceScale = scale;
            auto widget = withScrollbar(makeScrollView(makeContainerLeaf(800.0F, 800.0F), "scroll",
                info.viewport.width, info.viewport.height));
            widget.scrollAxis = axis;
            widget.scrollOffset = 80;
            const auto rest = LayoutEngine::layout(widget, Constraints::tight(info.viewport), context);
            for (int state = 0; state < 4; ++state) {
                CAPTURE(scale, static_cast<int>(axis), state);
                interaction.hoveredScrollbarIdentity = state == 1 ? rest.identity : "";
                interaction.draggedScrollbarIdentity = state == 2 ? rest.identity : "";
                widget.enabled = state != 3;
                const auto tree = LayoutEngine::layout(widget, Constraints::tight(info.viewport), context);
                const auto geometry = scrollbarGeometry(tree);
                REQUIRE(geometry);
                renderer->submit(recordScene(tree), info);
                REQUIRE(render::skiaGpuRendererAlive(*renderer));
                std::vector<unsigned char> pixels(240 * 200 * 4);
                glReadBuffer(GL_BACK);
                glReadPixels(0, 0, 240, 200, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                REQUIRE(glGetError() == GL_NO_ERROR);
                const auto thumb = geometry->thumb;
                const int x = static_cast<int>((thumb.origin.x + thumb.size.width * .5F) * scale);
                const int y = static_cast<int>((thumb.origin.y + thumb.size.height * .5F) * scale);
                const auto at = ((199 - y) * 240 + x) * 4;
                const Color actual{pixels[at], pixels[at + 1], pixels[at + 2], pixels[at + 3]};
                const Color expected = state == 3 ? theme.scrollbar.disabled : state == 2 ? theme.scrollbar.dragged
                    : state == 1 ? theme.scrollbar.hovered : theme.scrollbar.rest;
                CHECK(actual == expected);
                // Hover/pressed only expand thickness; the axial position is stable.
                const auto original = scrollbarGeometry(rest);
                REQUIRE(original);
                CHECK((axis == ScrollAxis::Horizontal ? thumb.origin.x : thumb.origin.y) ==
                      (axis == ScrollAxis::Horizontal ? original->thumb.origin.x : original->thumb.origin.y));
            }
            interaction = {};
        }
    }
}

// Alpha plan P1 §3.4: GPU uploads preserve the declared representation. This
// readback is test-only; production GPU presentation still performs no readback.
TEST_CASE("gpu_alpha_modes_survive_upload_replacement_and_unload", "[gpu][alpha]") {
    using namespace lumen;
    using render::AlphaMode;
    std::string diagnostics;
    if (!render::probeSkiaGpuAvailable(&diagnostics)) {
        SKIP("GPU unavailable: " << diagnostics);
    }
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-alpha-test", 80, 16,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 80;
    desc.heightPixels = 16;
    desc.allowSwap = false;
    auto renderer = render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer);
    render::RenderCommandList commands;
    commands.drawRect(core::Rect::fromXYWH(0, 0, 80, 16), {0, 0, 0, 255});
    int slot = 0;
    for (auto mode : {AlphaMode::Straight, AlphaMode::Premultiplied, AlphaMode::Opaque}) {
        const std::uint8_t alpha = mode == AlphaMode::Opaque ? 255 : 128;
        commands.uploadImage(77, {1, 1,
            {mode == AlphaMode::Premultiplied ? alpha : std::uint8_t{255}, 0, 0, alpha}, mode});
        commands.drawImage(77, core::Rect::fromXYWH(float(slot++ * 16), 0, 16, 16));
    }
    commands.uploadImage(77, {1, 1, {255, 0, 0, 128}, AlphaMode::Opaque});
    commands.drawImage(77, core::Rect::fromXYWH(48, 0, 16, 16));
    commands.unloadImage(77);
    commands.drawImage(77, core::Rect::fromXYWH(64, 0, 16, 16));
    FrameInfo frame;
    frame.viewport = {80, 16};
    renderer->submit(commands, frame);
    REQUIRE(render::skiaGpuRendererAlive(*renderer));
    std::vector<unsigned char> pixels(80 * 16 * 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, 80, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    REQUIRE(glGetError() == GL_NO_ERROR);
    const int expected[] = {128, 128, 255, 255, 0};
    for (int i = 0; i < 5; ++i) {
        CAPTURE(i);
        const auto offset = (8 * 80 + i * 16 + 8) * 4;
        CHECK(pixels[offset] == expected[i]);
        CHECK(pixels[offset + 1] == 0);
        CHECK(pixels[offset + 2] == 0);
    }
}

TEST_CASE("gpu_alpha_resources_reupload_after_rebuild_and_cpu_fallback", "[gpu][alpha]") {
    using namespace lumen;
    using namespace lumen::render;
    std::string diagnostics;
    if (!probeSkiaGpuAvailable(&diagnostics)) {
        SKIP("GPU unavailable: " << diagnostics);
    }
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-alpha-rebuild", 48, 16,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    ResourceManager resources;
    std::vector<ResourceHandle> handles;
    for (auto mode : {AlphaMode::Straight, AlphaMode::Premultiplied, AlphaMode::Opaque}) {
        const std::uint8_t alpha = mode == AlphaMode::Opaque ? 255 : 128;
        handles.push_back(resources.registerImage({1, 1,
            {mode == AlphaMode::Premultiplied ? alpha : std::uint8_t{255}, 0, 0, alpha}, mode}));
    }
    const auto replay = [&] {
        RenderCommandList commands;
        commands.drawRect(core::Rect::fromXYWH(0, 0, 48, 16), {0, 0, 0, 255});
        resources.appendUploads(commands);
        for (std::size_t i = 0; i < handles.size(); ++i) {
            commands.drawImage(resources.imageId(handles[i]), core::Rect::fromXYWH(float(i * 16), 0, 16, 16));
        }
        return commands;
    };
    FrameInfo info;
    info.viewport = {48, 16};
    for (int rebuild = 0; rebuild < 2; ++rebuild) {
        SkiaGpuRendererDesc desc;
        desc.sdlWindow = window.get();
        desc.widthPixels = 48;
        desc.heightPixels = 16;
        desc.allowSwap = false;
        auto gpu = createSkiaGpuRenderer(desc, &diagnostics);
        REQUIRE(gpu);
        gpu->submit(replay(), info);
        CHECK(gpu->stats().uploads == 3);
        REQUIRE(skiaGpuRendererAlive(*gpu));
        std::vector<std::uint8_t> pixels(48 * 16 * 4);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, 48, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        REQUIRE(glGetError() == GL_NO_ERROR);
        for (int i = 0; i < 3; ++i) {
            CHECK(pixels[(8 * 48 + 8 + i * 16) * 4] == (i == 2 ? 255 : 128));
        }
        gpu.reset(); // Real context teardown, then the existing recovery notification.
        resources.handleDeviceRebuilt();
    }
    CpuRenderer fallback(1, {0, 0, 0, 255});
    fallback.submit(replay(), info);
    CHECK(fallback.stats().uploads == 3);
    CHECK(fallback.pixels().alphaMode == AlphaMode::Opaque);
    for (int i = 0; i < 3; ++i) {
        CHECK(fallback.pixels().rgba[(8 * 48 + 8 + i * 16) * 4] == (i == 2 ? 255 : 128));
    }
    RenderCommandList idle;
    resources.appendUploads(idle);
    CHECK(idle.empty());
}

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
        SKIP("GPU unavailable; counter_gpu_fallback exercises CPU fallback");
    }

    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow(
        "lumen-gpu-test", 128, 96, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    REQUIRE(window != nullptr);

    lumen::render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
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

    // Reusing the same surface must still restore the renderer's GL context.
    const auto context = SDL_GL_GetCurrentContext();
    REQUIRE(SDL_GL_MakeCurrent(nullptr, nullptr));
    info.damage = lumen::core::Rect::fromXYWH(0, 0, 64, 48);
    info.preservePrevious = true;
    renderer->submit(recordScene(LayoutEngine::layout(
                         sampleScene(), Constraints::tight(info.viewport))), info);
    CHECK(SDL_GL_GetCurrentContext() == context);
    CHECK(renderer->stats().framesSubmitted == 2);
    CHECK(renderer->stats().fullFrameFallback);
    CHECK(lumen::render::skiaGpuRendererAlive(*renderer));

    // resize → resetSurface → 再提交一帧（FrameInfo 视口与 surface 尺寸
    // 保持一致：包装的 FBO 尺寸必须匹配窗口实际后备缓冲）。
    lumen::render::RenderSurfaceDesc surface;
    REQUIRE(SDL_SetWindowSize(window.get(), 256, 192));
    REQUIRE(SDL_SyncWindow(window.get()));
    surface.nativeWindow = window.get();
    surface.windowSystem = "sdl3";
    surface.widthPixels = 256;
    surface.heightPixels = 192;
    renderer->resetSurface(surface);
    FrameInfo resized = info;
    resized.viewport = Size{256.0F, 192.0F};
    renderer->submit(recordScene(LayoutEngine::layout(
                         sampleScene(), Constraints::tight(Size{256.0F, 192.0F}))),
                     resized);
    CHECK(renderer->stats().framesSubmitted == 3);
    CHECK(lumen::render::skiaGpuRendererAlive(*renderer));
}

TEST_CASE("gpu_factory_rejects_failed_surface_and_allows_retry", "[gpu]") {
    std::string diagnostics;
    if (!lumen::render::probeSkiaGpuAvailable(&diagnostics)) {
        SKIP("GPU unavailable: " << diagnostics);
    }
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow(
        "lumen-gpu-init-failure", 128, 96, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    REQUIRE(window != nullptr);
    lumen::render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    // Exceed every device's render-target limit without allocating a large
    // window or depending on a particular driver's out-of-memory behavior.
    desc.widthPixels = std::numeric_limits<int>::max();
    desc.heightPixels = 96;
    desc.allowSwap = false;
    auto renderer = lumen::render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer == nullptr);
    CHECK(diagnostics == "surface-size-exceeds-device-limit");

    desc.widthPixels = 128;
    renderer = lumen::render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer != nullptr);
    CHECK(diagnostics.empty());
    CHECK(lumen::render::skiaGpuRendererAlive(*renderer));
}

TEST_CASE("gpu_surface_failure_marks_renderer_dead_and_stops_submissions", "[gpu]") {
    std::string diagnostics;
    if (!lumen::render::probeSkiaGpuAvailable(&diagnostics)) {
        SKIP("GPU unavailable: " << diagnostics);
    }
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow(
        "lumen-gpu-runtime-failure", 128, 96, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    REQUIRE(window != nullptr);
    lumen::render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 128;
    desc.heightPixels = 96;
    desc.allowSwap = false;
    auto renderer = lumen::render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer != nullptr);
    FrameInfo info;
    info.viewport = Size{128.0F, 96.0F};
    const auto commands = recordScene(LayoutEngine::layout(
        sampleScene(), Constraints::tight(info.viewport)));
    renderer->submit(commands, info);
    REQUIRE(renderer->stats().framesSubmitted == 1);

    lumen::render::RenderSurfaceDesc surface;
    surface.widthPixels = std::numeric_limits<int>::max();
    surface.heightPixels = 96;
    renderer->resetSurface(surface);
    CHECK_FALSE(lumen::render::skiaGpuRendererAlive(*renderer));
    CHECK(renderer->stats().fallbackReason == "surface-size-exceeds-device-limit");
    renderer->submit(commands, info);
    CHECK(renderer->stats().framesSubmitted == 1);
    CHECK_FALSE(lumen::render::skiaGpuRendererAlive(*renderer));
}

TEST_CASE("gpu_stroke_readback_preserves_transparent_interiors_and_clip", "[gpu][visual]") {
    std::string diagnostics;
    if (!lumen::render::probeSkiaGpuAvailable(&diagnostics)) SKIP("GPU unavailable: " << diagnostics);
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-stroke-test", 240, 200,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    lumen::render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 240;
    desc.heightPixels = 200;
    desc.allowSwap = false;
    auto renderer = lumen::render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer);
    REQUIRE(renderer->capabilities().gpu);
    lumen::render::RenderCommandList commands;
    commands.drawRect(lumen::core::Rect::fromXYWH(0, 0, 240, 200), {20, 40, 80, 255});
    commands.save();
    commands.clipRect(lumen::core::Rect::fromXYWH(10, 10, 80, 60));
    commands.drawRectStroke(lumen::core::Rect::fromXYWH(15, 15, 60, 40), {240, 60, 20, 255},
                            lumen::core::CornerRadius::all(6), 2);
    commands.restore();
    for (const float scale : {1.0F, 1.25F, 1.5F, 2.0F}) {
        FrameInfo info;
        info.viewport = {240.0F / scale, 200.0F / scale};
        info.deviceScale = scale;
        renderer->submit(commands, info);
        REQUIRE(lumen::render::skiaGpuRendererAlive(*renderer));
        std::vector<unsigned char> pixels(240 * 200 * 4);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, 240, 200, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        REQUIRE(glGetError() == GL_NO_ERROR);
        const auto sample = [&](int x, int y, int channel) {
            return pixels[((199 - static_cast<int>(y * scale)) * 240 +
                           static_cast<int>(x * scale)) * 4 + channel];
        };
        CHECK(sample(45, 30, 0) == 20);
        CHECK(sample(45, 30, 2) == 80);
        CHECK(sample(45, 16, 0) > 180);
        CHECK(sample(45, 16, 2) < 60);
        CHECK(sample(45, 8, 2) == 80);
    }
}

TEST_CASE("gpu_list_readback_preserves_selection_focus_and_scrolled_border", "[gpu][list-visual]") {
    using namespace lumen;
    using namespace lumen::core;
    std::string diagnostics;
    if (!render::probeSkiaGpuAvailable(&diagnostics)) SKIP("GPU unavailable: " << diagnostics);
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-list-test", 240, 200,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 240;
    desc.heightPixels = 200;
    desc.allowSwap = false;
    auto renderer = render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer);
    REQUIRE(renderer->capabilities().gpu);
    const auto theme = style::Theme::dark();
    accessibility::AccessibilitySettings settings;
    style::InteractionStateSnapshot interaction;
    style::StyleContext context{theme, interaction, settings, 1.0F};
    widgets::ListController list;
    list.setItemCount(12);
    list.setItemBuilder([](std::size_t) { return makeText("Item"); });
    list.selection().setSelected({"i0"});
    for (const float scale : {1.0F, 2.0F}) {
        for (const float scroll : {0.0F, 7.0F}) {
            CAPTURE(scale, scroll);
            FrameInfo info;
            info.viewport = {240.0F / scale, 200.0F / scale};
            info.deviceScale = scale;
            // visual-system §6.1: focus rings default off. This fixture
            // explicitly tests a visible ring and its inset selection marker.
            const auto widget = withFocusRing(
                makeList(&list, "list", info.viewport.width, info.viewport.height), true);
            auto tree = LayoutEngine::layout(widget, Constraints::tight(info.viewport), context);
            const auto* row = findNodeByKey(tree, "list:item:i0");
            REQUIRE(row);
            interaction.focusedIdentity = row->identity;
            list.scroll().scrollTo(scroll);
            tree = LayoutEngine::layout(widget, Constraints::tight(info.viewport), context);
            REQUIRE(findNodeByKey(tree, "list:item:i0")->commonStyle().focusWidth > 0.0F);
            renderer->submit(recordScene(tree), info);
            REQUIRE(render::skiaGpuRendererAlive(*renderer));
            std::vector<unsigned char> pixels(240 * 200 * 4);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, 240, 200, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            REQUIRE(glGetError() == GL_NO_ERROR);
            const auto sample = [&](int x, int y) {
                const auto at = ((199 - static_cast<int>(y * scale)) * 240 +
                                 static_cast<int>(x * scale)) * 4;
                return Color{pixels[at], pixels[at + 1], pixels[at + 2], 255};
            };
            CHECK(sample(80, 20) == theme.list.selected);
            CHECK(sample(4, 20) == theme.list.selectionMarker);
            CHECK(sample(80, 0) == theme.list.separator);
            if (scroll == 0.0F) CHECK(sample(80, 1) == theme.colors.focusRing);
        }
    }
}

// GPU 平价 §5.1（lumen-skia-gpu-parity-plan）：DrawIcon 回放锚点断言。
// 段中点/弧顶点线身命中（3×3 设备像素邻域，见 strokeHits）；空隙与
// 盒外 = 背景；Busy 3/4 弧是 cap/join 与曲线覆盖的敏感探针；末段直调
// 即时路径 drawIcon 覆写（§3.3）。此前 GPU 回放静默丢弃 DrawIcon——
// 本用例即防回归锚。
TEST_CASE("gpu_icon_readback_paints_stroke_and_keeps_gaps", "[gpu][visual]") {
    using namespace lumen;
    using namespace lumen::core;
    std::string diagnostics;
    if (!render::probeSkiaGpuAvailable(&diagnostics)) SKIP("GPU unavailable: " << diagnostics);
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-icon-test", 64, 96,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 64;
    desc.heightPixels = 96;
    desc.allowSwap = false;
    auto renderer = render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer);
    const Color background{24, 24, 27, 255};
    const Color foreground{240, 240, 240, 255};
    const Rect checkBox{Offset{8.0F, 6.0F}, Size{16.0F, 16.0F}};
    const Rect busyBox{Offset{8.0F, 26.0F}, Size{16.0F, 16.0F}};
    const auto& checkLine = iconPolylines(IconId::Check).front();
    const auto& busyArc = iconPolylines(IconId::Busy).front();
    const auto anchorOn = [](const Offset& point, const Rect& box) {
        return Offset{box.origin.x + point.x * box.size.width,
                      box.origin.y + point.y * box.size.height};
    };
    for (const float scale : {1.0F, 2.0F}) {
        CAPTURE(scale);
        FrameInfo info;
        info.viewport = {64.0F / scale, 96.0F / scale};
        info.deviceScale = scale;
        render::RenderCommandList commands;
        commands.drawRect(Rect::fromXYWH(0, 0, 64, 96), background);
        commands.drawIcon(iconPolylines(IconId::Check), checkBox, foreground,
                          1.5F);
        commands.drawIcon(iconPolylines(IconId::Busy), busyBox, foreground,
                          1.5F);
        renderer->submit(commands, info);
        REQUIRE(render::skiaGpuRendererAlive(*renderer));
        const auto pixels = readBackFramebuffer(64, 96);
        const auto sample = [&](const Offset& point) {
            return samplePixel(pixels, 64, 96, point.x, point.y, scale);
        };
        // Check：每段折线段中点线身命中（锚点取自目录数据本身；相位
        // 鲁棒见 strokeHits）。
        for (std::size_t i = 0; i + 1 < checkLine.size(); ++i) {
            const Offset mid{(checkLine[i].x + checkLine[i + 1].x) * 0.5F,
                             (checkLine[i].y + checkLine[i + 1].y) * 0.5F};
            const auto anchor = anchorOn(mid, checkBox);
            CAPTURE(i, anchor.x, anchor.y);
            CHECK(strokeHits(pixels, 64, 96, anchor.x, anchor.y, scale,
                             foreground, background));
        }
        // Busy：弧顶点 i=4/12/20 恰在路径上（起点 135° 顺时针 270°，
        // 24 段——分别落在 180°/270°/0°）。
        for (const std::size_t i : {std::size_t{4}, std::size_t{12},
                                    std::size_t{20}}) {
            REQUIRE(i < busyArc.size());
            const auto anchor = anchorOn(busyArc[i], busyBox);
            CAPTURE(i, anchor.x, anchor.y);
            CHECK(strokeHits(pixels, 64, 96, anchor.x, anchor.y, scale,
                             foreground, background));
        }
        // 空隙与盒外：Check 盒内上缘、Busy 圆心、Check 盒外右侧 = 背景。
        CHECK(nearColor(sample(Offset{16.0F, 8.0F}), background, 2));
        CHECK(nearColor(sample(Offset{16.0F, 34.0F}), background, 2));
        CHECK(nearColor(sample(Offset{28.0F, 8.0F}), background, 2));
        // 即时路径覆写（§3.3）：经命令回放清屏后直接调用覆写入口，
        // 必须同样着墨（基类默认实现为 no-op——缺覆写时此处读回背景）。
        render::RenderCommandList backgroundOnly;
        backgroundOnly.drawRect(Rect::fromXYWH(0, 0, 64, 96), background);
        renderer->submit(backgroundOnly, info);
        renderer->drawIcon(iconPolylines(IconId::Check), checkBox, foreground,
                           1.5F);
        renderer->endFrame();
        REQUIRE(render::skiaGpuRendererAlive(*renderer));
        const auto immediate = readBackFramebuffer(64, 96);
        const Offset firstMid{(checkLine[0].x + checkLine[1].x) * 0.5F,
                              (checkLine[0].y + checkLine[1].y) * 0.5F};
        const auto anchor = anchorOn(firstMid, checkBox);
        CHECK(strokeHits(immediate, 64, 96, anchor.x, anchor.y, scale,
                         foreground, background));
        CHECK(nearColor(
            samplePixel(immediate, 64, 96, 16.0F, 8.0F, scale), background, 2));
    }
}

// GPU 平价 §5.2：DrawShadow 回放断言。偏移/模糊经 transform.tx/ty 与
// strokeWidth 编码（与 CPU 回放逐字一致）；σ = blur*0.5*scale。断言：
// 表面未被阴影污染、偏移外缘呈两色混合、3σ 之外回落纯背景、blur=0
// 为无滤镜偏移矩形（决策点 D1：跟随 Skia 光栅）。
TEST_CASE("gpu_shadow_readback_blur_offset_and_falloff", "[gpu][visual]") {
    using namespace lumen;
    using namespace lumen::core;
    std::string diagnostics;
    if (!render::probeSkiaGpuAvailable(&diagnostics)) SKIP("GPU unavailable: " << diagnostics);
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-shadow-test", 160, 160,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 160;
    desc.heightPixels = 160;
    desc.allowSwap = false;
    auto renderer = render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer);
    const Color background{24, 24, 27, 255};
    const Color surface{130, 140, 255, 255};
    const Color shadow{255, 90, 90, 255};
    // 几何按 scale=2 的半幅视口（80×80 逻辑）内布置。
    const Rect box{Offset{16.0F, 12.0F}, Size{48.0F, 20.0F}};
    const Offset offset{5.0F, 7.0F};
    const float blur = 8.0F;
    const Rect blurZeroBox{Offset{16.0F, 58.0F}, Size{48.0F, 14.0F}};
    for (const float scale : {1.0F, 2.0F}) {
        CAPTURE(scale);
        FrameInfo info;
        info.viewport = {160.0F / scale, 160.0F / scale};
        info.deviceScale = scale;
        render::RenderCommandList commands;
        commands.drawRect(Rect::fromXYWH(0, 0, 160, 160), background);
        // painter 真实次序（paintNode）：阴影先画、表面覆盖。
        commands.drawShadow(box, shadow, offset, blur);
        commands.drawRect(box, surface);
        commands.drawShadow(blurZeroBox, shadow, offset, 0.0F);
        renderer->submit(commands, info);
        REQUIRE(render::skiaGpuRendererAlive(*renderer));
        const auto pixels = readBackFramebuffer(160, 160);
        const auto sample = [&](float x, float y) {
            return samplePixel(pixels, 160, 160, x, y, scale);
        };
        // 表面中心 = 表面色（阴影不污染前景）。
        CHECK(sample(40.0F, 22.0F) == surface);
        // 偏移矩形底边（12+20+7=39）+3px：σ=4 的高斯核内 → 两色混合，
        // 与两个端点的通道最大差都不小于 16（不依赖精确混合比）。
        const Color mixed = sample(40.0F, 42.0F);
        CHECK(colorDistance(mixed, background) >= 16);
        CHECK(colorDistance(mixed, shadow) >= 16);
        // 3σ 之外（≈ 盒边 + 2*blur = 55）：高斯尾清零，回落纯背景。
        CHECK(nearColor(sample(40.0F, 55.0F), background, 4));
        // blur=0（D1）：无滤镜、全 alpha 的偏移矩形 {21,65,48,14}——
        // 内部精确等值；原盒左侧未被阴影覆盖处为纯背景。
        CHECK(sample(45.0F, 72.0F) == shadow);
        CHECK(sample(18.0F, 72.0F) == background);
    }
}

// GPU 平价 §5.3：painter → 命令 → GPU 回放全链路。此前按钮前导图标与
// elevation 阴影在 GPU 回放被静默丢弃——本用例从录制命令反解锚点
//（几何/颜色取自命令本身），断言像素真实着墨：painter 漏发命令时
// REQUIRE 先失败，回放丢弃时像素断言失败——两层缺口都无法静默。
TEST_CASE("gpu_painter_scene_paints_leading_icon_and_elevation_shadow", "[gpu][visual]") {
    using namespace lumen;
    using namespace lumen::core;
    std::string diagnostics;
    if (!render::probeSkiaGpuAvailable(&diagnostics)) SKIP("GPU unavailable: " << diagnostics);
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-painter-parity-test", 128, 96,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 128;
    desc.heightPixels = 96;
    desc.allowSwap = false;
    auto renderer = render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer);
    const auto theme = style::Theme::light();
    accessibility::AccessibilitySettings settings;
    style::InteractionStateSnapshot interaction;
    style::StyleContext context{theme, interaction, settings, 1.0F};
    Widget button = withLeadingIcon(makeButton("Add item"), IconId::Plus);
    button.key = "btn";
    Widget card = makeContainerLeaf(80.0F, 20.0F);
    card.key = "card";
    card.elevation = 2.0F;  // ElevationTokens L2：(0,4)/12/64。
    Widget page;
    page.type = WidgetType::Column;
    page.color = Color::fromRGBA(250, 250, 250);
    page.children = {button, card};
    const auto tree =
        LayoutEngine::layout(page, Constraints::tight(Size{128.0F, 96.0F}), context);
    const auto commands = recordScene(tree);
    // 注意：commands() 返回引用——range-for 里链式迭代 recordScene(...)
    // 的返回值是悬垂 UB（见 visual_m6 前导图标用例注），先落具名局部。
    const auto all = commands.commands();
    const render::RenderCommand* icon = nullptr;
    const render::RenderCommand* shadow = nullptr;
    for (const auto& command : all) {
        if (command.type == render::CommandType::DrawIcon && icon == nullptr) {
            icon = &command;
        }
        if (command.type == render::CommandType::DrawShadow && shadow == nullptr) {
            shadow = &command;
        }
    }
    REQUIRE(icon != nullptr);
    REQUIRE(shadow != nullptr);
    FrameInfo info;
    info.viewport = Size{128.0F, 96.0F};
    renderer->submit(commands, info);
    REQUIRE(render::skiaGpuRendererAlive(*renderer));
    const auto pixels = readBackFramebuffer(128, 96);
    // 图标线身锚点：首段中点命中，期望色取自命令本身。
    const auto& line = icon->polylines.front();
    const float iconX = icon->rect.origin.x +
                        (line[0].x + line[1].x) * 0.5F * icon->rect.size.width;
    const float iconY = icon->rect.origin.y +
                        (line[0].y + line[1].y) * 0.5F * icon->rect.size.height;
    CAPTURE(iconX, iconY);
    CHECK(strokeHits(pixels, 128, 96, iconX, iconY, 1.0F, icon->color,
                     Color::fromRGBA(250, 250, 250)));
    // 阴影强带：L2 offset=(0,4)——表面底边与偏移阴影底边之间的 4px 条带
    // 在阴影矩形内部（阶跃边缘高斯响应 0.5·erfc(d/(σ√2))，内部 ≈0.63），
    // 黑色阴影 alpha 64 在浅背景上至少压暗 20/通道；3σ 之外回落纯背景。
    //（阈值按响应模型标定：带中点预期压暗约 40。）
    const float bandX = shadow->rect.origin.x + shadow->rect.size.width * 0.5F;
    const float bandY = shadow->rect.origin.y + shadow->rect.size.height +
                        shadow->transform.ty * 0.5F;
    const float farY = shadow->rect.origin.y + shadow->rect.size.height +
                       shadow->transform.ty + shadow->strokeWidth * 2.0F;
    CAPTURE(bandX, bandY, farY);
    // 强带 ±1px 三点取最大：条带宽 4px，±1 仍在带内；模糊实现的响应
    // 峰值位置可能差一像素（Apple 软件 GL vs llvmpipe）。
    bool banded = false;
    for (const float dy : {-1.0F, 0.0F, 1.0F}) {
        const Color c = samplePixel(pixels, 128, 96, bandX, bandY + dy, 1.0F);
        if (250 - static_cast<int>(c.r) >= 20 &&
            250 - static_cast<int>(c.g) >= 20 &&
            250 - static_cast<int>(c.b) >= 20) {
            banded = true;
            break;
        }
    }
    CHECK(banded);
    const Color beyond = samplePixel(pixels, 128, 96, bandX, farY, 1.0F);
    CHECK(nearColor(beyond, Color::fromRGBA(250, 250, 250), 6));
}

// Collection design §7/§10: indented content keeps a full-width selected
// surface, marker and inner focus ring under GPU clipping and device scaling.
TEST_CASE("gpu_tree_readback_preserves_indented_selection_and_border", "[gpu][tree-visual]") {
    using namespace lumen;
    using namespace lumen::core;
    struct Model final : widgets::TreeModel {
        std::size_t childCount(const std::string& key) const override {
            return key.empty() ? 1 : key == "root" ? 12 : 0;
        }
        std::string childAt(const std::string& key, std::size_t i) const override {
            return key.empty() ? "root" : "child" + std::to_string(i);
        }
        bool hasChildren(const std::string& key) const override { return key == "root"; }
        Widget buildRow(const std::string&, std::size_t) const override { return makeText(""); }
    } model;
    std::string diagnostics;
    if (!render::probeSkiaGpuAvailable(&diagnostics)) SKIP("GPU unavailable: " << diagnostics);
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    VideoSession video;
    TestWindow window(SDL_CreateWindow("lumen-tree-test", 240, 200,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN), SDL_DestroyWindow);
    REQUIRE(window);
    render::SkiaGpuRendererDesc desc;
    desc.sdlWindow = window.get();
    desc.widthPixels = 240;
    desc.heightPixels = 200;
    desc.allowSwap = false;
    auto renderer = render::createSkiaGpuRenderer(desc, &diagnostics);
    REQUIRE(renderer);
    REQUIRE(renderer->capabilities().gpu);
    const auto theme = style::Theme::dark();
    accessibility::AccessibilitySettings settings;
    style::InteractionStateSnapshot interaction;
    style::StyleContext context{theme, interaction, settings, 1.0F};
    widgets::TreeController controller;
    controller.setModel(&model);
    controller.expand("root");
    controller.selection().setSelected({"child0"});
    for (const float scale : {1.0F, 2.0F}) {
        for (const float scroll : {0.0F, 7.0F}) {
            for (const bool showFocusRing : {true, false}) {
                CAPTURE(scale, scroll, showFocusRing);
                FrameInfo info;
                info.viewport = {240.0F / scale, 200.0F / scale};
                info.deviceScale = scale;
                const auto widget = withFocusRing(
                    makeTree(&controller, "tree", info.viewport.width, info.viewport.height), showFocusRing);
                auto tree = LayoutEngine::layout(widget, Constraints::tight(info.viewport), context);
                const auto* row = findNodeByKey(tree, "tree:item:child0");
                REQUIRE(row);
                interaction.focusedIdentity = row->identity;
                controller.scroll().scrollTo(scroll);
                tree = LayoutEngine::layout(widget, Constraints::tight(info.viewport), context);
                renderer->submit(recordScene(tree), info);
                REQUIRE(render::skiaGpuRendererAlive(*renderer));
                std::vector<unsigned char> pixels(240 * 200 * 4);
                glReadBuffer(GL_BACK);
                glReadPixels(0, 0, 240, 200, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                REQUIRE(glGetError() == GL_NO_ERROR);
                const auto sample = [&](int x, int y) {
                    const auto at = ((199 - static_cast<int>(y * scale)) * 240 +
                                     static_cast<int>(x * scale)) * 4;
                    return Color{pixels[at], pixels[at + 1], pixels[at + 2], 255};
                };
                CHECK(sample(100, 60) == theme.tree.row.selected);
                CHECK(sample(showFocusRing ? 4 : 2, 60) == theme.tree.row.selectionMarker);
                CHECK(sample(100, 0) == theme.tree.row.separator);
                CHECK(sample(100, static_cast<int>(41 - scroll)) ==
                      (showFocusRing ? theme.colors.focusRing : theme.tree.row.selected));
            }
        }
    }
}
