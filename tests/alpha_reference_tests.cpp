// Independent arithmetic oracle, frozen before migration (alpha plan P0/§5).
// Never call production conversion/blend helpers to derive expected values.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include "lumen/render/cpu_renderer.h"
#ifdef LUMEN_HAS_SKIA_BACKEND
#include "lumen/render/skia_renderer.h"
#endif

namespace {
using Pixel = std::array<unsigned, 4>;
unsigned roundedProduct(unsigned x, unsigned a) {
    return static_cast<unsigned>(std::floor(static_cast<double>(x) * a / 255.0 + 0.5));
}
Pixel premultiplied(Pixel value) {
    for (int c = 0; c < 3; ++c) {
        value[c] = roundedProduct(value[c], value[3]);
    }
    return value;
}
Pixel over(Pixel source, Pixel destination) {
    const auto s = premultiplied(source);
    for (int c = 0; c < 4; ++c) {
        destination[c] = s[c] + roundedProduct(destination[c], 255 - s[3]);
    }
    return destination;
}
Pixel first(const lumen::render::PixelBuffer& pixels) {
    return {pixels.rgba[0], pixels.rgba[1], pixels.rgba[2], pixels.rgba[3]};
}
} // namespace

TEST_CASE("alpha_reference_multilayer_quantization_budget", "[render][alpha]") {
    using namespace lumen;
    constexpr std::array<unsigned, 7> alphas{0, 1, 2, 127, 128, 254, 255};
    unsigned largestIntegerError = 0, largestFloatError = 0;
    std::uint32_t seed = 0x53ab29;
    const auto random = [&seed]() {
        seed = seed * 1664525U + 1013904223U;
        return seed >> 24;
    };
    for (int sample = 0; sample < 1024; ++sample) {
        render::CpuRenderer cpu(1, core::Color::fromRGBA(0, 0, 0, 0));
        cpu.beginFrame({1, 1});
        Pixel expected{};
        std::array<double, 4> precise{};
        for (int layer = 0; layer < 8; ++layer) {
            Pixel color{random(), random(), random(), alphas[(sample + layer) % alphas.size()]};
            expected = over(color, expected);
            const double alpha = color[3] / 255.0;
            for (int c = 0; c < 3; ++c) {
                precise[c] = color[c] * alpha + precise[c] * (1 - alpha);
            }
            precise[3] = color[3] + precise[3] * (1 - alpha);
            cpu.drawRect(core::Rect::fromXYWH(0, 0, 1, 1),
                         core::Color::fromRGBA(static_cast<std::uint8_t>(color[0]),
                                               static_cast<std::uint8_t>(color[1]),
                                               static_cast<std::uint8_t>(color[2]),
                                               static_cast<std::uint8_t>(color[3])));
            cpu.endFrame();
            // P2 output is already premultiplied; the independent P0 oracle
            // and frozen error ceiling are unchanged. Integer accumulation is exact.
            const auto actual = first(cpu.pixels());
            CHECK(actual == expected);
            CHECK(actual[3] == expected[3]);
            for (int c = 0; c < 4; ++c) {
                const auto integerError = static_cast<unsigned>(
                    std::abs(static_cast<int>(actual[c]) - static_cast<int>(expected[c])));
                const auto floatError =
                    static_cast<unsigned>(std::ceil(std::abs(actual[c] - precise[c])));
                largestIntegerError = std::max(largestIntegerError, integerError);
                largestFloatError = std::max(largestFloatError, floatError);
            }
            cpu.beginFrame({1, 1}, render::CpuRenderer::FrameMode::Preserve);
        }
    }
    INFO("max integer error=" << largestIntegerError
                              << ", max float error ceiling=" << largestFloatError);
    CHECK(largestIntegerError <= 4);
    CHECK(largestFloatError <= 5);
}

TEST_CASE("alpha_reference_single_layer_and_image_roundtrip", "[render][alpha]") {
    using namespace lumen;
    for (const auto alpha : {0, 1, 2, 127, 128, 254, 255}) {
        render::CpuRenderer source(1, core::Color::fromRGBA(0, 0, 0, 0));
        source.beginFrame({4, 4});
        source.drawRect(core::Rect::fromXYWH(0, 0, 4, 4),
                        core::Color::fromRGBA(255, 64, 17, static_cast<std::uint8_t>(alpha)));
        source.endFrame();
        const auto actual = first(source.pixels());
        CHECK(actual == premultiplied({255, 64, 17, static_cast<unsigned>(alpha)}));
        render::CpuRenderer target(1, core::Color::fromRGBA(0, 0, 0, 0));
        const auto id = target.registerImage(source.pixels());
        REQUIRE(id != 0);
        target.beginFrame({4, 4});
        target.drawImage(id, core::Rect::fromXYWH(0, 0, 4, 4));
        target.endFrame();
        CHECK(target.pixels() == source.pixels());
    }
}

// P0 reproduced red=256 -> uint8 zero. P2's premultiplied accumulator must
// match the independent model exactly, including low-alpha follow-up draws.
TEST_CASE("alpha_source_over_does_not_wrap_straight_channels", "[render][alpha]") {
    using namespace lumen;
    render::CpuRenderer cpu(1, core::Color::fromRGBA(0, 0, 0, 0));
    cpu.beginFrame({1, 1});
    cpu.drawRect(core::Rect::fromXYWH(0, 0, 1, 1), core::Color::fromRGBA(255, 0, 0, 128));
    cpu.drawRect(core::Rect::fromXYWH(0, 0, 1, 1), core::Color::fromRGBA(255, 0, 0, 1));
    cpu.endFrame();
    const auto expected = over({255, 0, 0, 1}, over({255, 0, 0, 128}, {}));
    CHECK(expected == Pixel{128, 0, 0, 128});
    CHECK(first(cpu.pixels()) == expected);
}

TEST_CASE("alpha_reference_rounded_solid_edges_keep_color", "[render][alpha]") {
    using namespace lumen;
    render::CpuRenderer cpu(1, core::Color::fromRGBA(0, 0, 0, 0));
    cpu.beginFrame({24, 24});
    cpu.clipRounded(core::Rect::fromXYWH(0, 0, 24, 24), core::CornerRadius::all(9));
    cpu.drawRect(core::Rect::fromXYWH(0, 0, 24, 24), core::Color::fromRGBA(255, 0, 0));
    cpu.endFrame();
    unsigned edgePixels = 0;
    const auto& bytes = cpu.pixels().rgba;
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        if (bytes[i + 3] > 0 && bytes[i + 3] < 255) {
            ++edgePixels;
            CHECK(bytes[i] == bytes[i + 3]);
            CHECK(bytes[i + 1] == 0);
            CHECK(bytes[i + 2] == 0);
        }
    }
    CHECK(edgePixels > 0);
}

#ifdef LUMEN_HAS_SKIA_BACKEND
TEST_CASE("alpha_reference_skia_readback_is_premultiplied", "[render][alpha][skia]") {
    using namespace lumen;
    render::SkiaRenderer skia(1, core::Color::fromRGBA(0, 0, 0, 0));
    skia.beginFrame({1, 1});
    skia.drawRect(core::Rect::fromXYWH(0, 0, 1, 1), core::Color::fromRGBA(255, 0, 0, 128));
    skia.endFrame();
    CHECK(first(skia.pixels()) == Pixel{128, 0, 0, 128});
}
#endif
