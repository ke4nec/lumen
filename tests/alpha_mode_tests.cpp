// Alpha rendering plan P1, §3.1/3.2/3.4/3.6. Expected arithmetic is independent.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <string>
#include "fixtures/alpha_v6_upload.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/resource_manager.h"
#ifdef LUMEN_HAS_SKIA_BACKEND
#include "lumen/render/skia_renderer.h"
#endif

namespace {
using namespace lumen::render;
using lumen::core::Color;
using lumen::core::Rect;
PixelBuffer red(AlphaMode mode, std::uint8_t alpha = 128) {
    return {
        1, 1, {mode == AlphaMode::Premultiplied ? alpha : std::uint8_t{255}, 0, 0, alpha}, mode};
}
void put32(std::string& blob, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        blob[offset + i] = static_cast<char>(value >> (i * 8));
    }
}
template <class Backend> void imageModes() {
    for (auto mode : {AlphaMode::Straight, AlphaMode::Premultiplied, AlphaMode::Opaque}) {
        Backend renderer(1, Color::fromRGBA(0, 0, 0, 0));
        const auto source = red(mode, mode == AlphaMode::Opaque ? 255 : 128);
        const auto id = renderer.registerImage(source);
        REQUIRE(id != 0);
        renderer.beginFrame({4, 1});
        // A previous shape's alpha must not modulate subsequent image samples.
        renderer.drawRect(Rect::fromXYWH(3, 0, 1, 1), Color::fromRGBA(255, 0, 0, 1));
        renderer.drawImage(id, Rect::fromXYWH(0, 0, 1, 1));
        renderer.endFrame();
        PixelBuffer normalized;
        REQUIRE(premultiplyRgbaInto(normalized, renderer.pixels()));
        CHECK(normalized.rgba[0] == source.rgba[3]);
        CHECK(normalized.rgba[3] == source.rgba[3]);

        RenderCommandList list;
        list.uploadImage(42, source);
        list.drawImage(42, Rect::fromXYWH(0, 0, 1, 1));
        const PixelBuffer green{1, 1, {0, 255, 0, 255}, AlphaMode::Opaque};
        list.uploadImage(42, green);
        list.drawImage(42, Rect::fromXYWH(1, 0, 1, 1));
        // Invalid replacement cannot evict the last valid resource.
        list.uploadImage(42, red(AlphaMode::Opaque));
        list.drawImage(42, Rect::fromXYWH(2, 0, 1, 1));
        list.unloadImage(42);
        list.drawImage(42, Rect::fromXYWH(3, 0, 1, 1));
        FrameInfo frame;
        frame.viewport = {4, 1};
        renderer.submit(list, frame);
        REQUIRE(premultiplyRgbaInto(normalized, renderer.pixels()));
        CHECK(normalized.rgba == std::vector<std::uint8_t>{source.rgba[3], 0, 0, source.rgba[3], 0,
                                                           255, 0, 255, 0, 255, 0, 255, 0, 0, 0,
                                                           0});
        CHECK(renderer.registerImage(red(AlphaMode::Opaque)) == 0);
        CHECK(renderer.registerImage({}) == 0);
    }
    Backend renderer;
    RenderCommandList upload;
    upload.uploadImage(1, red(AlphaMode::Opaque, 255));
    FrameInfo frame;
    frame.viewport = {1, 1};
    renderer.submit(upload, frame);
    // Explicit command IDs and immediate registration share a cache.
    CHECK(renderer.registerImage(red(AlphaMode::Straight)) != 1);
}
} // namespace

TEST_CASE("alpha_modes_are_explicit_and_hash_remains_byte_based", "[render][alpha]") {
    const PixelBuffer legacy{1, 1, {12, 34, 56, 255}};
    CHECK(legacy.alphaMode == AlphaMode::Straight);
    auto opaque = legacy;
    opaque.alphaMode = AlphaMode::Opaque;
    CHECK(opaque != legacy);
    CHECK(frameHash(opaque) == frameHash(legacy));
    CHECK(static_cast<unsigned>(AlphaMode::Straight) == 0);
    CHECK(static_cast<unsigned>(AlphaMode::Premultiplied) == 1);
    CHECK(static_cast<unsigned>(AlphaMode::Opaque) == 2);
}

TEST_CASE("alpha_conversion_matches_all_channel_alpha_pairs", "[render][alpha]") {
    PixelBuffer straight{256, 256, {}};
    std::vector<std::uint8_t> expected;
    for (unsigned a = 0; a < 256; ++a) {
        for (unsigned c = 0; c < 256; ++c) {
            straight.rgba.insert(straight.rgba.end(), {static_cast<std::uint8_t>(c), 0, 0,
                                                       static_cast<std::uint8_t>(a)});
            expected.insert(expected.end(),
                            {static_cast<std::uint8_t>(std::floor(double(c) * a / 255.0 + 0.5)), 0,
                             0, static_cast<std::uint8_t>(a)});
        }
    }
    const auto original = straight;
    PixelBuffer premul;
    REQUIRE(premultiplyRgbaInto(premul, straight));
    CHECK(straight == original);
    CHECK(premul.alphaMode == AlphaMode::Premultiplied);
    CHECK(premul.rgba == expected);
    const auto* storage = premul.rgba.data();
    REQUIRE(premultiplyRgbaInto(premul, straight));
    CHECK(premul.rgba.data() == storage);
    const auto saved = premul;
    REQUIRE(premultiplyRgbaInto(premul, premul));
    REQUIRE(premultiplyRgbaInPlace(premul));
    CHECK(premul == saved);
    REQUIRE(unpremultiplyRgbaInto(premul, premul));
    CHECK(premul.alphaMode == AlphaMode::Straight);
    const auto unpacked = premul;
    REQUIRE(unpremultiplyRgbaInPlace(premul));
    CHECK(premul == unpacked);
    // Exhaust every valid premultiplied channel independently of the forward map.
    for (unsigned a = 0; a < 256; ++a) {
        for (unsigned c = 0; c <= a; ++c) {
            PixelBuffer pixel{1,
                              1,
                              {static_cast<std::uint8_t>(c), 0, 0, static_cast<std::uint8_t>(a)},
                              AlphaMode::Premultiplied};
            REQUIRE(unpremultiplyRgbaInPlace(pixel));
            CHECK(pixel.rgba[0] == (a == 0 ? 0 : std::floor(double(c) * 255.0 / a + 0.5)));
            CHECK(pixel.rgba[3] == a);
        }
    }
    CHECK(unpacked != original); // Low alpha loses information by design.
}

TEST_CASE("alpha_conversion_rejects_invalid_input_transactionally", "[render][alpha]") {
    const auto sentinel = red(AlphaMode::Straight);
    for (const auto& invalid : std::vector<PixelBuffer>{
             {},
             {-1, 1, {}},
             {1, -1, {}},
             {1, 0, {}},
             {1, 1, {1, 2, 3}},
             {1, 1, {1, 2, 3, 4, 5}},
             {std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), {}},
             {1, 1, {0, 0, 0, 0}, static_cast<AlphaMode>(255)},
             {1, 1, {1, 0, 0, 0}, AlphaMode::Premultiplied},
             {1, 1, {0, 129, 0, 128}, AlphaMode::Premultiplied},
             red(AlphaMode::Opaque)}) {
        CHECK_FALSE(validatePixelBuffer(invalid));
        auto copy = invalid;
        CHECK_FALSE(premultiplyRgbaInPlace(copy));
        CHECK(copy == invalid);
        CHECK_FALSE(unpremultiplyRgbaInto(copy, copy));
        CHECK(copy == invalid);
        copy = sentinel;
        CHECK_FALSE(premultiplyRgbaInto(copy, invalid));
        CHECK(copy == sentinel);
        CHECK_FALSE(unpremultiplyRgbaInto(copy, invalid));
        CHECK(copy == sentinel);
    }
    CHECK(validatePixelBuffer(red(AlphaMode::Opaque), PixelValidation::Structure));
    auto opaque = red(AlphaMode::Opaque, 255);
    const auto saved = opaque;
    REQUIRE(premultiplyRgbaInPlace(opaque));
    REQUIRE(unpremultiplyRgbaInto(opaque, opaque));
    CHECK(opaque == saved);
}

TEST_CASE("alpha_resources_preserve_modes_through_device_reupload", "[resource][alpha]") {
    ResourceManager resources;
    for (auto mode : {AlphaMode::Straight, AlphaMode::Premultiplied, AlphaMode::Opaque}) {
        const auto source = red(mode, mode == AlphaMode::Opaque ? 255 : 128);
        const auto handle = resources.registerImage(source);
        REQUIRE(handle.index != 0);
        REQUIRE(resources.pixels(handle));
        CHECK(*resources.pixels(handle) == source);
        RenderCommandList list;
        resources.appendUploads(list);
        REQUIRE(list.size() == 1);
        CHECK(list.commands()[0].pixels == source);
        resources.handleDeviceRebuilt();
        list.clear();
        resources.appendUploads(list);
        REQUIRE(list.size() == 1);
        CHECK(list.commands()[0].pixels == source);
        resources.release(handle);
        list.clear();
        resources.appendUploads(list);
    }
    CHECK(resources.registerImage({}).index == 0);
    CHECK(resources.registerImage(red(AlphaMode::Opaque)).index == 0);
}

TEST_CASE("alpha_cpu_accepts_modes_and_ordered_resource_commands", "[render][alpha]") {
    imageModes<CpuRenderer>();
}

TEST_CASE("alpha_commands_read_frozen_v6_and_roundtrip_v7", "[commands][alpha]") {
    const std::string v6(reinterpret_cast<const char*>(kAlphaV6Upload), sizeof(kAlphaV6Upload));
    RenderCommandList decoded;
    REQUIRE(deserializeCommands(v6, decoded));
    REQUIRE(decoded.size() == 1);
    CHECK(decoded.commands()[0].image == 1);
    CHECK(decoded.commands()[0].pixels == PixelBuffer{1, 1, {255, 64, 17, 128}});
    decoded.drawImage(1, Rect::fromXYWH(0, 0, 1, 1));
    CpuRenderer cpu(1, Color::fromRGBA(0, 0, 0, 0));
    FrameInfo frame;
    frame.viewport = {1, 1};
    cpu.submit(decoded, frame);
    PixelBuffer normalized;
    REQUIRE(premultiplyRgbaInto(normalized, cpu.pixels()));
    CHECK(normalized.rgba == std::vector<std::uint8_t>{128, 32, 9, 128});
    for (auto mode : {AlphaMode::Straight, AlphaMode::Premultiplied, AlphaMode::Opaque}) {
        RenderCommandList list;
        list.save();
        list.uploadImage(12, red(mode, mode == AlphaMode::Opaque ? 255 : 128));
        list.drawImage(12, Rect::fromXYWH(0, 0, 1, 1));
        list.restore();
        const auto blob = serializeCommands(list);
        REQUIRE(blob.size() > 12);
        CHECK(blob[8] == 7);
        REQUIRE(deserializeCommands(blob, decoded));
        CHECK(decoded == list);
    }
}

TEST_CASE("alpha_commands_reject_corruption_without_partial_publication", "[commands][alpha]") {
    RenderCommandList original;
    original.drawRect(Rect::fromXYWH(0, 0, 1, 1), Color::fromRGBA(1, 2, 3));
    RenderCommandList list;
    list.uploadImage(1, red(AlphaMode::Premultiplied));
    const auto blob = serializeCommands(list);
    const auto modeOffset = blob.size() - 12; // 4 pixel bytes + u32 byteCount + u32 mode.
    const auto rejects = [&](const std::string& broken) {
        auto output = original;
        CHECK_FALSE(deserializeCommands(broken, output));
        CHECK(output == original);
    };
    for (const auto mode : {3U, 256U, 0xffffffffU}) {
        auto broken = blob;
        put32(broken, modeOffset, mode);
        rejects(broken);
    }
    for (const auto version : {0U, 5U, 8U, 0xffffffffU}) {
        auto broken = blob;
        put32(broken, 8, version);
        rejects(broken);
    }
    for (auto offset : {modeOffset - 8, modeOffset - 4, modeOffset + 4}) {
        auto broken = blob;
        put32(broken, offset, 0xffffffffU);
        rejects(broken);
    }
    for (std::size_t n = 0; n < blob.size(); ++n) {
        rejects(blob.substr(0, n));
    }
    auto broken = blob;
    broken[broken.size() - 4] = static_cast<char>(255); // Invalid premultiplied red.
    rejects(broken);
    broken = blob;
    put32(broken, modeOffset, 2); // Invalid opaque alpha.
    rejects(broken);
    list.clear();
    list.uploadImage(1, {});
    CHECK(serializeCommands(list).empty());
}

#ifdef LUMEN_HAS_SKIA_BACKEND
TEST_CASE("alpha_skia_accepts_modes_and_ordered_resource_commands", "[render][alpha][skia]") {
    imageModes<SkiaRenderer>();
    SkiaRenderer opaque;
    opaque.beginFrame({1, 1});
    opaque.endFrame();
    CHECK(opaque.pixels().alphaMode == AlphaMode::Opaque);
}
#endif
