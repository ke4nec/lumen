// Alpha plan P3, §3.4/§5.1: cross-backend frames and preserved translucent pixels.
#include <catch2/catch_test_macros.hpp>
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/skia_renderer.h"

using namespace lumen;
using namespace lumen::render;

TEST_CASE("alpha_skia_preserve_copies_translucent_pixels_without_reblending", "[alpha][skia]") {
    for (float scale : {1.0F, 1.25F, 2.0F}) {
        for (unsigned alpha : {0U, 128U, 255U}) {
            CAPTURE(scale, alpha);
            SkiaRenderer renderer(scale, {64, 32, 16, std::uint8_t(alpha)});
            const auto paint = [&] {
                renderer.drawRect(core::Rect::fromXYWH(0, 0, 12, 12), {128, 20, 250, 96});
            };
            renderer.beginFrame({12, 12});
            paint();
            renderer.endFrame();
            const auto original = renderer.pixels();
            for (int frame = 0; frame < 3; ++frame) {
                renderer.beginFrame({12, 12}, SkiaRenderer::FrameMode::Preserve);
                CHECK(renderer.pixels() == original);
                renderer.endFrame();
                CHECK(renderer.pixels() == original);
                const auto damage = core::Rect::fromXYWH(4, 4, 4, 4);
                renderer.beginFrame({12, 12}, SkiaRenderer::FrameMode::Preserve, damage);
                renderer.clipRect(damage);
                paint();
                renderer.endFrame();
                CHECK(renderer.pixels() == original);
                CHECK(validatePixelBuffer(renderer.pixels()));
            }
        }
    }
}

TEST_CASE("alpha_cpu_skia_frames_roundtrip_as_images_and_commands", "[alpha][skia]") {
    CpuRenderer cpu(1, core::Color::transparent());
    cpu.beginFrame({7, 1});
    unsigned column = 0;
    for (unsigned a : {0U, 1U, 2U, 127U, 128U, 254U, 255U}) {
        cpu.drawRect(core::Rect::fromXYWH(float(column++), 0, 1, 1),
                     {255, 64, 17, std::uint8_t(a)});
    }
    cpu.endFrame();
    const auto source = cpu.pixels();
    SkiaRenderer skia(1, core::Color::transparent());
    RenderCommandList commands, decoded;
    commands.uploadImage(12, source);
    commands.drawImage(12, core::Rect::fromXYWH(0, 0, 7, 1));
    REQUIRE(deserializeCommands(serializeCommands(commands), decoded));
    FrameInfo info;
    info.viewport = {7, 1};
    skia.submit(decoded, info);
    CHECK(skia.pixels() == source);
    CpuRenderer returned(1, core::Color::transparent());
    const auto id = returned.registerImage(skia.pixels());
    REQUIRE(id != 0);
    returned.beginFrame({7, 1});
    returned.drawImage(id, core::Rect::fromXYWH(0, 0, 7, 1));
    returned.endFrame();
    CHECK(returned.pixels() == source);
    cpu.submit(decoded, info);
    CHECK(cpu.pixels() == skia.pixels());
}
