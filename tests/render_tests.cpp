#include <catch2/catch_test_macros.hpp>

#include "lumen/core/geometry.h"
#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"

using lumen::core::Color;
using lumen::core::CornerRadius;
using lumen::core::Offset;
using lumen::core::Rect;
using lumen::core::Size;
using lumen::core::TextStyle;
using lumen::render::CpuRenderer;
using lumen::render::PixelBuffer;
using lumen::render::TextRun;

namespace {

Color pixelAt(const PixelBuffer& buffer, int x, int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.width) +
         static_cast<std::size_t>(x)) *
        4;
    return Color::fromRGBA(buffer.rgba[offset], buffer.rgba[offset + 1],
                           buffer.rgba[offset + 2], buffer.rgba[offset + 3]);
}

bool isClearColor(const PixelBuffer& buffer, int x, int y) {
    const Color c = pixelAt(buffer, x, y);
    return c == Color::fromRGBA(24, 24, 27, 255);
}

}  // namespace

TEST_CASE("cpu_renderer_clears_frame_to_background", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    REQUIRE(renderer.pixels().width == 100);
    REQUIRE(renderer.pixels().height == 80);
    CHECK(isClearColor(renderer.pixels(), 0, 0));
    CHECK(isClearColor(renderer.pixels(), 99, 79));
}

TEST_CASE("cpu_renderer_scales_logical_viewport", "[render]") {
    CpuRenderer renderer(2.0F);
    renderer.beginFrame(Size{100.0F, 50.0F});
    CHECK(renderer.pixels().width == 200);
    CHECK(renderer.pixels().height == 100);
}

TEST_CASE("cpu_renderer_draws_solid_rect", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    renderer.drawRect(Rect::fromXYWH(10.0F, 10.0F, 20.0F, 20.0F),
                      Color::fromRGBA(255, 0, 0));
    CHECK(pixelAt(renderer.pixels(), 20, 20) == Color::fromRGBA(255, 0, 0));
    CHECK(isClearColor(renderer.pixels(), 5, 5));
    // Half-open right edge: pixel centers at 30 logical are outside.
    CHECK(isClearColor(renderer.pixels(), 30, 20));
}

TEST_CASE("cpu_renderer_rounded_corners_stay_background", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    renderer.drawRect(Rect::fromXYWH(10.0F, 10.0F, 40.0F, 40.0F),
                      Color::fromRGBA(255, 0, 0), CornerRadius::all(10.0F));
    // Corner pixel centers sit outside the 10px quarter circles.
    CHECK(isClearColor(renderer.pixels(), 11, 11));
    CHECK(isClearColor(renderer.pixels(), 48, 11));
    CHECK(isClearColor(renderer.pixels(), 11, 48));
    CHECK(isClearColor(renderer.pixels(), 48, 48));
    // Center and edge midpoints are filled.
    CHECK(pixelAt(renderer.pixels(), 30, 30) == Color::fromRGBA(255, 0, 0));
    CHECK(pixelAt(renderer.pixels(), 11, 30) == Color::fromRGBA(255, 0, 0));
    CHECK(pixelAt(renderer.pixels(), 30, 11) == Color::fromRGBA(255, 0, 0));
}

TEST_CASE("cpu_renderer_clip_rect_bounds_drawing", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    renderer.save();
    renderer.clipRect(Rect::fromXYWH(0.0F, 0.0F, 50.0F, 50.0F));
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 100.0F),
                      Color::fromRGBA(0, 255, 0));
    CHECK(pixelAt(renderer.pixels(), 25, 25) == Color::fromRGBA(0, 255, 0));
    CHECK(isClearColor(renderer.pixels(), 75, 75));
    renderer.restore();
    renderer.drawRect(Rect::fromXYWH(60.0F, 60.0F, 10.0F, 10.0F),
                      Color::fromRGBA(0, 0, 255));
    // Restoring re-enables drawing outside the previous clip.
    CHECK(pixelAt(renderer.pixels(), 65, 65) == Color::fromRGBA(0, 0, 255));
}

TEST_CASE("cpu_renderer_clip_nests_and_pops", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 100.0F});
    renderer.save();
    renderer.clipRect(Rect::fromXYWH(0.0F, 0.0F, 60.0F, 60.0F));
    renderer.save();
    renderer.clipRect(Rect::fromXYWH(40.0F, 40.0F, 60.0F, 60.0F));
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 100.0F),
                      Color::fromRGBA(255, 255, 255));
    // Only the intersection (40..60, 40..60) was painted.
    CHECK(pixelAt(renderer.pixels(), 50, 50) ==
          Color::fromRGBA(255, 255, 255));
    CHECK(isClearColor(renderer.pixels(), 30, 30));
    CHECK(isClearColor(renderer.pixels(), 70, 70));
    renderer.restore();
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F),
                      Color::fromRGBA(0, 255, 0));
    // Outer clip (0..60) is active again after the pop.
    CHECK(pixelAt(renderer.pixels(), 5, 5) == Color::fromRGBA(0, 255, 0));
}

TEST_CASE("cpu_renderer_placeholder_text_renders_glyphs", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{200.0F, 100.0F});
    TextStyle style;
    style.color = Color::fromRGBA(255, 255, 255);
    renderer.drawText(TextRun{"Count: 0", Offset{10.0F, 10.0F}}, style);
    int litPixels = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (pixelAt(renderer.pixels(), x, y) ==
                Color::fromRGBA(255, 255, 255)) {
                ++litPixels;
            }
        }
    }
    CHECK(litPixels > 30);
}

TEST_CASE("cpu_renderer_renders_cjk_as_box_glyphs", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 40.0F});
    TextStyle style;
    style.color = Color::fromRGBA(255, 255, 255);
    // "你" is 3 UTF-8 bytes; it must occupy one glyph cell, not three.
    renderer.drawText(TextRun{"\xe4\xbd\xa0", Offset{5.0F, 5.0F}}, style);
    bool boxDrawn = false;
    bool outsideCell = false;
    for (int y = 5; y < 22; ++y) {
        for (int x = 5; x < 20; ++x) {
            const bool lit = pixelAt(renderer.pixels(), x, y) ==
                             Color::fromRGBA(255, 255, 255);
            if (lit && x < 13) {
                boxDrawn = true;
            }
            if (lit && x > 15) {
                outsideCell = true;
            }
        }
    }
    CHECK(boxDrawn);
    CHECK_FALSE(outsideCell);
}

TEST_CASE("cpu_renderer_blends_semi_transparent_color", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{10.0F, 10.0F});
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F),
                      Color::fromRGBA(255, 0, 0, 128));
    const Color c = pixelAt(renderer.pixels(), 5, 5);
    CHECK(c.r == 140);
    CHECK(c.g == 12);
    CHECK(c.b == 13);
    CHECK(c.a == 255);
}

TEST_CASE("cpu_renderer_blits_registered_image", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{10.0F, 10.0F});
    PixelBuffer image;
    image.width = 2;
    image.height = 2;
    image.rgba = {255, 0, 0, 255, 0, 255, 0, 255,
                  0,   0, 255, 255, 255, 255, 0, 255};
    const auto id = renderer.registerImage(std::move(image));
    renderer.drawImage(id, Rect::fromXYWH(0.0F, 0.0F, 4.0F, 4.0F));
    CHECK(pixelAt(renderer.pixels(), 1, 1) == Color::fromRGBA(255, 0, 0));
    CHECK(pixelAt(renderer.pixels(), 3, 1) == Color::fromRGBA(0, 255, 0));
    CHECK(pixelAt(renderer.pixels(), 1, 3) == Color::fromRGBA(0, 0, 255));
    CHECK(pixelAt(renderer.pixels(), 3, 3) == Color::fromRGBA(255, 255, 0));
}

TEST_CASE("cpu_renderer_skips_unknown_image", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{10.0F, 10.0F});
    renderer.drawImage(999, Rect::fromXYWH(0.0F, 0.0F, 4.0F, 4.0F));
    CHECK(isClearColor(renderer.pixels(), 1, 1));
}

TEST_CASE("cpu_renderer_rejects_short_image_buffers", "[render]") {
    CpuRenderer renderer;
    PixelBuffer image;
    image.width = 2;
    image.height = 2;
    image.rgba = {255, 0, 0, 255};
    CHECK(renderer.registerImage(std::move(image)) == 0);
    renderer.beginFrame(Size{10.0F, 10.0F});
    CHECK_NOTHROW(renderer.drawImage(0, Rect::fromXYWH(0.0F, 0.0F, 2.0F,
                                                         2.0F)));
}

TEST_CASE("cpu_renderer_preserves_straight_alpha_across_image_roundtrip",
          "[render]") {
    CpuRenderer layer(1.0F, Color::transparent());
    layer.beginFrame(Size{1.0F, 1.0F});
    layer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 1.0F, 1.0F),
                   Color::fromRGBA(255, 0, 0, 128));

    CpuRenderer composite(1.0F, Color::fromRGBA(0, 0, 0));
    composite.beginFrame(Size{1.0F, 1.0F});
    const auto id = composite.registerImage(layer.pixels());
    composite.drawImage(id, Rect::fromXYWH(0.0F, 0.0F, 1.0F, 1.0F));

    CpuRenderer direct(1.0F, Color::fromRGBA(0, 0, 0));
    direct.beginFrame(Size{1.0F, 1.0F});
    direct.drawRect(Rect::fromXYWH(0.0F, 0.0F, 1.0F, 1.0F),
                    Color::fromRGBA(255, 0, 0, 128));
    CHECK(composite.pixels().rgba == direct.pixels().rgba);
}

TEST_CASE("frame_hash_is_stable_and_sensitive", "[render]") {
    PixelBuffer a;
    a.width = 2;
    a.height = 2;
    a.rgba = {10, 20, 30, 255, 10, 20, 30, 255, 10, 20, 30, 255, 10, 20, 30, 255};
    PixelBuffer b = a;
    CHECK(lumen::render::frameHash(a) == lumen::render::frameHash(b));
    b.rgba[0] = 11;
    CHECK(lumen::render::frameHash(a) != lumen::render::frameHash(b));
    b = a;
    b.width = 4;
    CHECK(lumen::render::frameHash(a) != lumen::render::frameHash(b));
}

TEST_CASE("painter_renders_readable_default_text_on_dark_background",
          "[render]") {
    // DSL-built text carries the default (black) TextStyle; the painter must
    // substitute a readable tone on its dark chrome.
    const lumen::core::RenderNode root = lumen::layout::LayoutEngine::layout(
        lumen::core::makeText("Count: 0"),
        lumen::core::Constraints::loose(Size{200.0F, 100.0F}));
    CpuRenderer renderer;
    renderer.beginFrame(Size{200.0F, 100.0F});
    lumen::render::paintScene(renderer, root);
    int litPixels = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (pixelAt(renderer.pixels(), x, y).r > 100) {
                ++litPixels;
            }
        }
    }
    CHECK(litPixels > 10);
}

TEST_CASE("painter_clips_field_content_to_its_rect", "[render]") {
    // 16 glyphs at 8.4px advance overflow the 60px field; the overflow must
    // be clipped instead of bleeding over neighboring content.
    const lumen::core::RenderNode root = lumen::layout::LayoutEngine::layout(
        lumen::core::makeTextField("0123456789ABCDEF", "", {}, {}, 0.0F,
                                   "field", 60.0F, std::nullopt),
        lumen::core::Constraints::loose(Size{200.0F, 100.0F}));
    REQUIRE(root.size.width == 60.0F);
    CpuRenderer renderer;
    renderer.beginFrame(Size{200.0F, 100.0F});
    lumen::render::paintScene(renderer, root);
    for (int y = 0; y < 100; ++y) {
        for (int x = 62; x < 160; ++x) {
            // Field background is (46,46,54); any lit text pixel right of the
            // field rect means the content was not clipped.
            INFO("x=" << x << " y=" << y);
            CHECK(pixelAt(renderer.pixels(), x, y).r <= 100);
        }
    }
}

TEST_CASE("painter_uses_text_padding_for_content_origin", "[render]") {
    TextStyle style;
    style.color = Color::fromRGBA(255, 0, 0);
    const auto root = lumen::layout::LayoutEngine::layout(
        lumen::core::makeText("A", style, {}, 0.0F, "", std::nullopt,
                              std::nullopt, lumen::core::EdgeInsets::all(10.0F)),
        lumen::core::Constraints::loose(Size{100.0F, 100.0F}));
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 100.0F});
    lumen::render::paintScene(renderer, root);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            CHECK(pixelAt(renderer.pixels(), x, y) != Color::fromRGBA(255, 0, 0));
        }
    }
}

TEST_CASE("painter_shows_focus_for_keyless_bound_field", "[render]") {
    lumen::core::StateStore store;
    store.set("name", "");
    lumen::core::HandlerRegistry handlers;
    lumen::core::FocusManager focus;
    lumen::core::InteractionController controller(store, handlers, focus);
    const auto widget = lumen::dsl::text_field(lumen::dsl::bind("name"),
                                               lumen::dsl::placeholder("Name"));
    const auto root = lumen::layout::LayoutEngine::layout(
        widget, lumen::core::Constraints::tight(Size{100.0F, 40.0F}));
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 40.0F});
    lumen::render::paintScene(renderer, root);
    const auto before = lumen::render::frameHash(renderer.pixels());
    controller.pointerDown(root, Offset{20.0F, 20.0F});
    REQUIRE(controller.wantsTextInput());
    REQUIRE(!focus.focusedIdentity().empty());
    renderer.beginFrame(Size{100.0F, 40.0F});
    lumen::render::paintScene(
        renderer, root,
        lumen::render::PaintOptions{"", focus.focusedIdentity(), "", "", 0});
    CHECK(lumen::render::frameHash(renderer.pixels()) != before);
}

// --- Stage 6: preserve-mode partial redraw and image lifecycle. ---

TEST_CASE("cpu_preserve_mode_keeps_pixels_outside_damage", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 80.0F),
                      Color::fromRGBA(0, 0, 255));
    renderer.endFrame();

    // Partial frame: repaint only the left half in red.
    renderer.beginFrame(Size{100.0F, 80.0F},
                        CpuRenderer::FrameMode::Preserve);
    renderer.save();
    renderer.clipRect(Rect::fromXYWH(0.0F, 0.0F, 50.0F, 80.0F));
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 80.0F),
                      Color::fromRGBA(255, 0, 0));
    renderer.restore();
    renderer.endFrame();

    CHECK(pixelAt(renderer.pixels(), 25, 40) == Color::fromRGBA(255, 0, 0));
    // Outside the damaged clip: the previous frame survives untouched.
    CHECK(pixelAt(renderer.pixels(), 75, 40) == Color::fromRGBA(0, 0, 255));
}

TEST_CASE("cpu_preserve_falls_back_to_clear_on_first_frame", "[render]") {
    CpuRenderer renderer;
    // No previous frame exists yet: Preserve degrades to a full clear.
    renderer.beginFrame(Size{20.0F, 10.0F},
                        CpuRenderer::FrameMode::Preserve);
    CHECK(isClearColor(renderer.pixels(), 5, 5));
}

TEST_CASE("cpu_image_lifecycle_unregister_and_clear", "[render]") {
    CpuRenderer renderer;
    PixelBuffer image;
    image.width = 1;
    image.height = 1;
    image.rgba = {0, 255, 0, 255};
    const auto id = renderer.registerImage(std::move(image));
    renderer.beginFrame(Size{10.0F, 10.0F});
    renderer.drawImage(id, Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F));
    renderer.endFrame();
    CHECK(pixelAt(renderer.pixels(), 5, 5) == Color::fromRGBA(0, 255, 0));

    // Freed images draw nothing; ids are never recycled.
    renderer.unregisterImage(id);
    renderer.beginFrame(Size{10.0F, 10.0F});
    renderer.drawImage(id, Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F));
    renderer.endFrame();
    CHECK(isClearColor(renderer.pixels(), 5, 5));

    const auto second = renderer.registerImage([&] {
        PixelBuffer green;
        green.width = 1;
        green.height = 1;
        green.rgba = {0, 255, 0, 255};
        return green;
    }());
    CHECK(second != id);
    renderer.clearImages();
    renderer.beginFrame(Size{10.0F, 10.0F});
    renderer.drawImage(second, Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F));
    renderer.endFrame();
    CHECK(isClearColor(renderer.pixels(), 5, 5));
}

TEST_CASE("cpu_preserve_erases_inside_damage_to_clear_color", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 80.0F),
                      Color::fromRGBA(0, 0, 255));
    renderer.endFrame();

    // Damage-scoped Preserve with NO repaint at all: inside the damage rect
    // the frame shows the clear color (like a full repaint), outside it the
    // previous frame survives — ghosts cannot linger either way.
    renderer.beginFrame(Size{100.0F, 80.0F},
                        CpuRenderer::FrameMode::Preserve,
                        Rect::fromXYWH(10.0F, 10.0F, 30.0F, 20.0F));
    renderer.endFrame();
    CHECK(isClearColor(renderer.pixels(), 25, 20));
    CHECK(pixelAt(renderer.pixels(), 75, 40) == Color::fromRGBA(0, 0, 255));
}
