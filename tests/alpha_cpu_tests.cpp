// Alpha plan P2: accumulation, coverage and framebuffer lifetime, §3.1/§3.3.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "lumen/render/cpu_renderer.h"

namespace {
using namespace lumen;
using namespace lumen::render;
unsigned product(unsigned c, unsigned a) {
    return static_cast<unsigned>(std::floor(double(c) * a / 255.0 + 0.5));
}
void checkPureRed(const PixelBuffer& pixels) {
    REQUIRE(validatePixelBuffer(pixels));
    unsigned edges = 0;
    for (std::size_t i = 0; i < pixels.rgba.size(); i += 4) {
        CHECK(pixels.rgba[i] == pixels.rgba[i + 3]);
        CHECK(pixels.rgba[i + 1] == 0);
        CHECK(pixels.rgba[i + 2] == 0);
        edges += pixels.rgba[i + 3] > 0 && pixels.rgba[i + 3] < 255;
    }
    CHECK(edges > 0);
}
} // namespace

TEST_CASE("alpha_cpu_clear_and_publication_preserve_explicit_proofs", "[render][alpha]") {
    for (const auto alpha : {0, 1, 2, 127, 128, 254, 255}) {
        CpuRenderer cpu(1, core::Color::fromRGBA(255, 64, 17, std::uint8_t(alpha)));
        CHECK(cpu.pixels().alphaMode == AlphaMode::Premultiplied);
        cpu.beginFrame({4, 4});
        const auto mode = alpha == 255 ? AlphaMode::Opaque : AlphaMode::Premultiplied;
        CHECK(cpu.pixels().alphaMode == mode); // Legacy first-frame view is also truthful.
        CHECK(cpu.pixels().rgba[0] == alpha);
        CHECK(cpu.pixels().rgba[1] == product(64, alpha));
        CHECK(cpu.pixels().rgba[2] == product(17, alpha));
        CHECK(cpu.pixels().rgba[3] == alpha);
        cpu.endFrame();
        const auto completed = cpu.pixels();
        const auto* published = cpu.pixels().rgba.data();
        cpu.beginFrame({4, 4}, CpuRenderer::FrameMode::Preserve);
        cpu.drawRect(core::Rect::fromXYWH(0, 0, 2, 2), core::Color::fromRGBA(255, 0, 0, 127));
        CHECK(cpu.pixels() == completed);
        CHECK(cpu.pixels().rgba.data() == published);
        cpu.endFrame();
        CHECK(cpu.pixels().alphaMode == mode);
        CHECK(validatePixelBuffer(cpu.pixels()));
        const auto* storage = cpu.pixels().rgba.data();
        for (int i = 0; i < 20; ++i) {
            CHECK(cpu.pixels().rgba.data() == storage);
        }
    }
}

TEST_CASE("alpha_cpu_configuration_invalidates_preserve_but_keeps_published_frame",
          "[render][alpha]") {
    CpuRenderer cpu(1, core::Color::fromRGBA(255, 0, 0, 255));
    RenderCommandList empty;
    FrameInfo info;
    info.viewport = {4, 4};
    cpu.submit(empty, info);
    const auto original = cpu.pixels();
    cpu.setClearColor(core::Color::fromRGBA(0, 255, 0, 128));
    CHECK(cpu.pixels() == original);
    info.preservePrevious = true;
    info.damage = core::Rect::fromXYWH(0, 0, 1, 1);
    cpu.submit(empty, info);
    CHECK(cpu.stats().fullFrameFallback);
    CHECK(cpu.pixels().alphaMode == AlphaMode::Premultiplied);
    CHECK(cpu.pixels().rgba == std::vector<std::uint8_t>{
                                   0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128,
                                   0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128,
                                   0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128,
                                   0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128, 0, 128});
    cpu.setClearColor(core::Color::fromRGBA(0, 255, 0, 128));
    cpu.submit(empty, info);
    CHECK_FALSE(cpu.stats().fullFrameFallback);
    const auto beforeScale = cpu.pixels();
    cpu.setDeviceScale(2);
    CHECK(cpu.pixels() == beforeScale);
    info.deviceScale = 2;
    cpu.submit(empty, info);
    CHECK(cpu.stats().fullFrameFallback);
    CHECK(cpu.pixels().width == 8);
    CHECK(validatePixelBuffer(cpu.pixels()));
    info.viewport = {5, 5};
    cpu.submit(empty, info);
    CHECK(cpu.stats().fullFrameFallback);
    CHECK(cpu.pixels().width == 10);
    // Changing configuration during an immediate frame must also invalidate its reuse.
    cpu.beginFrame({5, 5});
    cpu.setClearColor(core::Color::fromRGBA(0, 0, 255, 255));
    cpu.endFrame();
    cpu.submit(empty, info);
    CHECK(cpu.stats().fullFrameFallback);
    CHECK(cpu.pixels().alphaMode == AlphaMode::Opaque);
}

TEST_CASE("alpha_cpu_damage_replay_matches_full_frames_and_modes", "[render][alpha]") {
    for (float scale : {1.0F, 1.25F, 2.0F}) {
        for (unsigned alpha : {0U, 127U, 255U}) {
            CAPTURE(scale, alpha);
            const auto clear = core::Color::fromRGBA(15, 30, 45, std::uint8_t(alpha));
            CpuRenderer partial(scale, clear), full(scale, clear);
            FrameInfo info;
            info.viewport = {32, 24};
            info.deviceScale = scale;
            for (int i = 0; i < 6; ++i) {
                RenderCommandList scene;
                scene.drawRect(core::Rect::fromXYWH(1, 1, 30, 22), {255, 40, 20, 96},
                               core::CornerRadius::all(5));
                scene.drawRect(core::Rect::fromXYWH(4.0F + static_cast<float>(i), 6, 8, 8), {10, 180, 240, 128});
                auto damage = info;
                damage.preservePrevious = i != 0;
                damage.damage =
                    i == 0 ? std::nullopt : std::optional{core::Rect::fromXYWH(3.0F + static_cast<float>(i), 6, 9, 8)};
                partial.submit(scene, damage);
                full.submit(scene, info);
                CHECK(partial.pixels() == full.pixels());
                CHECK(partial.pixels().alphaMode ==
                      (alpha == 255 ? AlphaMode::Opaque : AlphaMode::Premultiplied));
                CHECK(validatePixelBuffer(partial.pixels()));
            }
        }
    }
}

TEST_CASE("alpha_cpu_images_scale_premultiplied_channels_with_clip_coverage", "[render][alpha]") {
    for (float scale : {1.0F, 1.25F, 2.0F}) {
        CpuRenderer cpu(scale, core::Color::transparent());
        const PixelBuffer source{1, 1, {128, 0, 0, 128}, AlphaMode::Premultiplied};
        const auto id = cpu.registerImage(source);
        REQUIRE(id != 0);
        cpu.beginFrame({24, 24});
        cpu.clipRounded(core::Rect::fromXYWH(0, 0, 24, 24), core::CornerRadius::all(9));
        cpu.clipRounded(core::Rect::fromXYWH(1, 1, 22, 22), core::CornerRadius::all(8));
        cpu.drawImage(id, core::Rect::fromXYWH(0, 0, 24, 24));
        cpu.endFrame();
        checkPureRed(cpu.pixels());
        const auto center =
            (cpu.pixels().height / 2 * cpu.pixels().width + cpu.pixels().width / 2) * 4;
        CHECK(cpu.pixels().rgba[center] == 128); // No repeated image alpha multiplication.
    }
}

TEST_CASE("alpha_cpu_all_color_primitives_keep_premultiplied_edges", "[render][alpha]") {
    CpuRenderer cpu(1.25F, core::Color::transparent());
    cpu.beginFrame({120, 80});
    cpu.clipRounded(core::Rect::fromXYWH(0, 0, 120, 80), core::CornerRadius::all(9));
    const auto red = core::Color::fromRGBA(255, 0, 0, 128);
    cpu.drawRect(core::Rect::fromXYWH(1, 1, 30, 30), red, core::CornerRadius::all(6));
    cpu.drawRectStroke(core::Rect::fromXYWH(34, 1, 30, 30), red, core::CornerRadius::all(6), 2);
    core::TextStyle style;
    style.color = red;
    cpu.drawText({"Alpha", {2, 40}}, style);
    cpu.drawIcon({{{0, 0}, {1, 1}}, {{1, 0}, {0, 1}}}, core::Rect::fromXYWH(80, 40, 20, 20), red,
                 2);
    cpu.drawShadow(core::Rect::fromXYWH(70, 5, 24, 24), red, {3, 3}, 7);
    cpu.endFrame();
    checkPureRed(cpu.pixels());
}
