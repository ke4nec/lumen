#include <catch2/catch_test_macros.hpp>

#include "lumen/accessibility/bridge.h"
#include "lumen/core/geometry.h"
#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/style/resolver.h"
#include "lumen/style/state.h"
#include "lumen/style/theme.h"

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
    // 焦点视觉现在经由布局折算：用携带焦点快照的 StyleContext 重新布局，
    // resolved style 进入 RenderNode（visual-system §5）。
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    const lumen::style::InteractionStateSnapshot interaction{
        "", "", focus.focusedIdentity()};
    const lumen::accessibility::AccessibilitySettings settings;
    const auto focusedRoot = lumen::layout::LayoutEngine::layout(
        widget, lumen::core::Constraints::tight(Size{100.0F, 40.0F}),
        lumen::style::StyleContext{theme, interaction, settings});
    renderer.beginFrame(Size{100.0F, 40.0F});
    lumen::render::paintScene(renderer, focusedRoot,
                              lumen::render::PaintOptions{});
    CHECK(lumen::render::frameHash(renderer.pixels()) != before);
}

TEST_CASE("theme_metric_change_updates_control_layout", "[render][style]") {
    // Theme 指标变化 → 控件最小尺寸/布局结果随之变化（§10.2）。
    const auto button = lumen::core::makeButton("OK");
    const lumen::core::Constraints constraints =
        lumen::core::Constraints::loose(Size{400.0F, 300.0F});
    const lumen::style::Theme comfortable = lumen::style::Theme::dark();
    const lumen::style::Theme touch = lumen::style::Theme::dark(
        lumen::style::ControlDensity::Touch);
    const lumen::accessibility::AccessibilitySettings settings;
    const lumen::style::InteractionStateSnapshot idle;
    const auto medium = lumen::layout::LayoutEngine::layout(
        button, constraints,
        lumen::style::StyleContext{comfortable, idle, settings});
    const auto large = lumen::layout::LayoutEngine::layout(
        button, constraints, lumen::style::StyleContext{touch, idle, settings});
    CHECK(medium.size.height == comfortable.metrics.minHeight[1]);
    CHECK(large.size.height == touch.metrics.minHeight[2]);
    CHECK(large.size.height > medium.size.height);
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

// --- S1（gui-control-visual-system-task §9.2）：描边命令与透明表面 ---

TEST_CASE("cpu_renderer_rect_stroke_leaves_interior_transparent", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    // 红色页面底，其上画 4 px 蓝色描边；内部必须保持页面底色（不是蓝色
    // 实心块），矩形外也不受影响。
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 80.0F),
                      Color::fromRGBA(255, 0, 0));
    renderer.drawRectStroke(Rect::fromXYWH(10.0F, 10.0F, 40.0F, 30.0F),
                            Color::fromRGBA(0, 0, 255), CornerRadius::zero(),
                            4.0F);
    renderer.endFrame();
    const auto& px = renderer.pixels();
    CHECK(pixelAt(px, 30, 25) == Color::fromRGBA(255, 0, 0));   // 内部
    CHECK(pixelAt(px, 11, 11) == Color::fromRGBA(0, 0, 255));   // 左上环带
    CHECK(pixelAt(px, 49, 39) == Color::fromRGBA(0, 0, 255));   // 右下环带
    CHECK(pixelAt(px, 5, 5) == Color::fromRGBA(255, 0, 0));     // 外部
}

TEST_CASE("cpu_renderer_rect_stroke_rounded_corners_stay_inside", "[render]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 80.0F),
                      Color::fromRGBA(255, 0, 0));
    // 圆角描边：拐角外部的页面底色不受影响（不越界）。
    renderer.drawRectStroke(Rect::fromXYWH(20.0F, 20.0F, 40.0F, 40.0F),
                            Color::fromRGBA(0, 0, 255),
                            CornerRadius::all(10.0F), 2.0F);
    renderer.endFrame();
    const auto& px = renderer.pixels();
    CHECK(pixelAt(px, 21, 21) == Color::fromRGBA(255, 0, 0));  // 圆角挖空区
    CHECK(pixelAt(px, 40, 20) == Color::fromRGBA(0, 0, 255));  // 顶边直段环带
    CHECK(pixelAt(px, 40, 40) == Color::fromRGBA(255, 0, 0));  // 内部
}

TEST_CASE("painter_outline_button_stays_transparent_over_page", "[render]") {
    // Outline 变体按钮放在有色页面上：内部保持页面颜色，不被 borderStrong
    // 填充（§9.2 实心块回归）。
    lumen::core::Widget button;
    button.type = lumen::core::WidgetType::Button;
    button.text = "";
    button.buttonVariant = lumen::core::ButtonVariant::Outline;
    button.width = 40.0F;
    button.height = 20.0F;

    lumen::core::Widget page;
    page.type = lumen::core::WidgetType::Container;
    page.color = Color::fromRGBA(255, 0, 0);
    page.width = 100.0F;
    page.height = 80.0F;
    page.children = {button};

    const auto root = lumen::layout::LayoutEngine::layout(
        page, lumen::core::Constraints::unbounded());
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    lumen::render::paintScene(renderer, root, {});
    renderer.endFrame();
    const auto& px = renderer.pixels();
    // 按钮内部中心 = 页面红；顶边直段 = borderStrong 描边（角部被圆角
    // 挖空，取 x=20 避开半径 6 的拐角）。
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    CHECK(pixelAt(px, 20, 10) == Color::fromRGBA(255, 0, 0));
    CHECK(pixelAt(px, 20, 0) == theme.colors.borderStrong);
}

// --- CPU 抗锯齿（SDF 覆盖率）与双缓冲 ---

TEST_CASE("cpu_aa_smooths_rect_edge_and_corner", "[render][aa]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    // 白色圆角矩形画在暗底上：分数边界像素与底色混合（不是全有/全无
    // 的硬边），内部仍是纯色。顶边取 20.4：像素 20 的中心 20.5 距边界
    // 0.1 → 半覆盖量级的混合。
    renderer.drawRect(Rect::fromXYWH(20.0F, 20.4F, 40.0F, 30.0F),
                      Color::fromRGBA(255, 255, 255),
                      CornerRadius::all(8.0F));
    renderer.endFrame();
    const auto& px = renderer.pixels();
    // 内部纯白。
    CHECK(pixelAt(px, 40, 35) == Color::fromRGBA(255, 255, 255));
    // 分数边界行：介于底色与纯白之间，且向内单调变亮。
    const Color edge = pixelAt(px, 40, 20);
    const Color inner = pixelAt(px, 40, 22);
    CHECK(edge.r > 24);      // 不是纯底色
    CHECK(edge.r < 255);     // 也不是纯表面
    CHECK(edge.r < inner.r);
    // 圆角外深处的像素保持底色（AA 不越界扩散）。
    CHECK(pixelAt(px, 18, 18) == Color::fromRGBA(24, 24, 27, 255));
    // 角弧上存在中间值像素（平滑过渡的直接证据）。
    int blended = 0;
    for (int y = 18; y < 30; ++y) {
        for (int x = 18; x < 30; ++x) {
            const std::uint8_t r = pixelAt(px, x, y).r;
            if (r > 30 && r < 250) {
                ++blended;
            }
        }
    }
    CHECK(blended >= 4);

    // 整数对齐的几何保持锐利（边界恰在像素网格上时不引入模糊）。
    CpuRenderer aligned;
    aligned.beginFrame(Size{40.0F, 30.0F});
    aligned.drawRect(Rect::fromXYWH(10.0F, 10.0F, 20.0F, 10.0F),
                     Color::fromRGBA(255, 255, 255));
    aligned.endFrame();
    CHECK(pixelAt(aligned.pixels(), 20, 9) ==
          Color::fromRGBA(24, 24, 27, 255));  // 边界外整像素干净
    CHECK(pixelAt(aligned.pixels(), 20, 10) ==
          Color::fromRGBA(255, 255, 255));    // 边界内整像素全强度
}

TEST_CASE("cpu_aa_stroke_and_icon_use_partial_coverage", "[render][aa]") {
    CpuRenderer renderer;
    renderer.beginFrame(Size{100.0F, 80.0F});
    // 斜线图标（无圆角矩形）：对角线上的中间值像素证明线条 AA。
    renderer.drawIcon(
        std::vector<std::vector<Offset>>{{Offset{0.2F, 0.2F},
                                          Offset{0.8F, 0.8F}}},
        Rect::fromXYWH(20.0F, 20.0F, 40.0F, 40.0F),
        Color::fromRGBA(255, 255, 255), 2.0F);
    renderer.endFrame();
    const auto& px = renderer.pixels();
    int blended = 0;
    int solid = 0;
    for (int y = 20; y < 60; ++y) {
        for (int x = 20; x < 60; ++x) {
            const std::uint8_t r = pixelAt(px, x, y).r;
            if (r > 30 && r < 250) {
                ++blended;
            } else if (r == 255) {
                ++solid;
            }
        }
    }
    CHECK(solid > 10);      // 线芯全强度
    CHECK(blended >= 8);    // 两侧 AA 过渡带

    // 描边环带：分数边界的上下沿出现 AA 过渡（整数对齐处保持锐利）。
    CpuRenderer stroked;
    stroked.beginFrame(Size{100.0F, 80.0F});
    stroked.drawRectStroke(Rect::fromXYWH(20.0F, 20.4F, 40.0F, 30.0F),
                           Color::fromRGBA(255, 255, 255), CornerRadius::all(8.0F),
                           2.0F);
    stroked.endFrame();
    const auto& spx = stroked.pixels();
    CHECK(pixelAt(spx, 40, 21) == Color::fromRGBA(255, 255, 255));  // 环带内部
    int strokeBlend = 0;
    for (int y = 18; y < 25; ++y) {
        const std::uint8_t r = pixelAt(spx, 40, y).r;
        if (r > 30 && r < 250) {
            ++strokeBlend;
        }
    }
    CHECK(strokeBlend >= 1);  // 分数边界沿的 AA 过渡
}

TEST_CASE("cpu_double_buffer_returns_completed_frame", "[render][aa]") {
    CpuRenderer renderer;
    // 帧 1：红色；endFrame 后 pixels() 是红色完成帧。
    renderer.beginFrame(Size{10.0F, 10.0F});
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F),
                      Color::fromRGBA(255, 0, 0));
    renderer.endFrame();
    CHECK(pixelAt(renderer.pixels(), 5, 5) == Color::fromRGBA(255, 0, 0));
    // 帧 2：绿色覆盖；完成帧切换为绿色（back/front 隔离，无残留）。
    renderer.beginFrame(Size{10.0F, 10.0F});
    renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F),
                      Color::fromRGBA(0, 255, 0));
    renderer.endFrame();
    CHECK(pixelAt(renderer.pixels(), 5, 5) == Color::fromRGBA(0, 255, 0));
    // 帧间无内存增长（swap 而非拷贝）无法直接断言容量，但连续多帧稳定。
    for (int i = 0; i < 8; ++i) {
        renderer.beginFrame(Size{10.0F, 10.0F});
        renderer.drawRect(Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F),
                          Color::fromRGBA(0, 0, 255));
        renderer.endFrame();
    }
    CHECK(pixelAt(renderer.pixels(), 5, 5) == Color::fromRGBA(0, 0, 255));
}
