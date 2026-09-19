// v0.2 阶段7C GPU smoke（plan §5/§7E）：硬件 GPU 只作增强 smoke；探测
// 失败的环境跳过并保留诊断。有 GPU 时走完整路径——隐藏 GL 窗口、
// Ganesh 初始化、命令提交与设备健康查询。

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>

#include <limits>
#include <memory>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/render/painter.h"
#include "lumen/render/renderer.h"
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
            const auto widget = makeList(&list, "list", info.viewport.width, info.viewport.height);
            auto tree = LayoutEngine::layout(widget, Constraints::tight(info.viewport), context);
            const auto* row = findNodeByKey(tree, "list:item:i0");
            REQUIRE(row);
            interaction.focusedIdentity = row->identity;
            list.scroll().scrollTo(scroll);
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
            CHECK(sample(80, 20) == theme.list.selected);
            CHECK(sample(4, 20) == theme.list.selectionMarker);
            CHECK(sample(80, 0) == theme.list.separator);
            if (scroll == 0.0F) CHECK(sample(80, 1) == theme.colors.focusRing);
        }
    }
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
