// CPU/Skia backend consistency smoke tests (plan 阶段5/§9): the same draw
// command set replayed through both renderers must produce same-sized
// buffers and visually matching content. Skia antialiases edges, so equality
// is asserted with per-pixel tolerance instead of exact bytes.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>

#include "counter_app.h"
#include "lumen/core/geometry.h"
#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/render/skia_renderer.h"
#include "lumen/style/theme.h"
#include "lumen/text/skia_font_manager.h"

using lumen::core::Color;
using lumen::core::CornerRadius;
using lumen::core::Offset;
using lumen::core::Rect;
using lumen::core::Size;
using lumen::core::TextStyle;
using lumen::render::CpuRenderer;
using lumen::render::PixelBuffer;
using lumen::render::Renderer;
using lumen::render::SkiaRenderer;
using lumen::render::TextRun;
using lumen::examples::CounterApp;

namespace {

struct DiffStats {
    std::size_t differingPixels{0};
    std::size_t totalPixels{0};
    double meanAbsDiff{0.0};
};

// Channel-wise difference with `tolerance`; premultiplied Skia output vs
// straight-alpha CPU output can differ slightly on blended edges.
[[nodiscard]] DiffStats compareBuffers(const PixelBuffer& cpu,
                                       const PixelBuffer& skia,
                                       int tolerance) {
    DiffStats stats;
    stats.totalPixels = static_cast<std::size_t>(cpu.width) *
                        static_cast<std::size_t>(cpu.height);
    double totalDiff = 0.0;
    for (std::size_t i = 0; i + 3 < cpu.rgba.size(); i += 4) {
        const int diff = std::max({std::abs(cpu.rgba[i] - skia.rgba[i]),
                                   std::abs(cpu.rgba[i + 1] - skia.rgba[i + 1]),
                                   std::abs(cpu.rgba[i + 2] - skia.rgba[i + 2])});
        totalDiff += diff;
        if (diff > tolerance) {
            ++stats.differingPixels;
        }
    }
    stats.meanAbsDiff =
        stats.totalPixels > 0 ? totalDiff / static_cast<double>(stats.totalPixels)
                              : 0.0;
    return stats;
}

}  // namespace

TEST_CASE("skia_replays_solid_rect_commands", "[skia]") {
    CpuRenderer cpu;
    SkiaRenderer skia;
    for (Renderer* renderer : {static_cast<Renderer*>(&cpu),
                               static_cast<Renderer*>(&skia)}) {
        renderer->beginFrame(Size{100.0F, 80.0F});
        renderer->drawRect(Rect::fromXYWH(10.0F, 10.0F, 60.0F, 40.0F),
                           Color::fromRGBA(255, 0, 0));
        renderer->drawRect(Rect::fromXYWH(20.0F, 20.0F, 30.0F, 30.0F),
                           Color::fromRGBA(0, 0, 255, 128),
                           CornerRadius::all(8.0F));
        renderer->endFrame();
    }
    REQUIRE(skia.pixels().width == cpu.pixels().width);
    REQUIRE(skia.pixels().height == cpu.pixels().height);
    const DiffStats stats = compareBuffers(cpu.pixels(), skia.pixels(), 12);
    // Solid interiors match exactly; only AA edges exceed the tolerance.
    CHECK(stats.differingPixels <= stats.totalPixels / 20);
    CHECK(stats.meanAbsDiff < 4.0);
}

TEST_CASE("skia_replays_clip_and_text_commands", "[skia]") {
    CpuRenderer cpu;
    SkiaRenderer skia;
    TextStyle style;
    style.color = Color::fromRGBA(255, 255, 255);
    for (Renderer* renderer : {static_cast<Renderer*>(&cpu),
                               static_cast<Renderer*>(&skia)}) {
        renderer->beginFrame(Size{120.0F, 60.0F});
        renderer->save();
        renderer->clipRect(Rect::fromXYWH(0.0F, 0.0F, 40.0F, 60.0F));
        renderer->drawRect(Rect::fromXYWH(0.0F, 0.0F, 120.0F, 60.0F),
                           Color::fromRGBA(0, 200, 0));
        renderer->restore();
        renderer->drawText(TextRun{"Count: 0", Offset{5.0F, 10.0F}}, style);
        renderer->endFrame();
    }
    REQUIRE(skia.pixels().width == cpu.pixels().width);
    REQUIRE(skia.pixels().height == cpu.pixels().height);
    // Anchor pixels: solid areas of the clipped rect and the untouched
    // region must match exactly on both backends.
    const auto readPixel = [](const PixelBuffer& buffer, int x, int y) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(buffer.width) +
             static_cast<std::size_t>(x)) *
            4;
        return Color::fromRGBA(buffer.rgba[offset], buffer.rgba[offset + 1],
                               buffer.rgba[offset + 2], buffer.rgba[offset + 3]);
    };
    CHECK(readPixel(cpu.pixels(), 20, 30) == readPixel(skia.pixels(), 20, 30));
    CHECK(readPixel(cpu.pixels(), 80, 30) == readPixel(skia.pixels(), 80, 30));
    const auto brightPixels = [](const PixelBuffer& buffer) {
        std::size_t count = 0;
        for (int y = 0; y < 60; ++y) {
            for (int x = 0; x < 40; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * buffer.width + x) * 4;
                if (buffer.rgba[offset] > 220 &&
                    buffer.rgba[offset + 1] > 220 &&
                    buffer.rgba[offset + 2] > 220) {
                    ++count;
                }
            }
        }
        return count;
    };
    CHECK(brightPixels(cpu.pixels()) > 0);
    CHECK(brightPixels(skia.pixels()) > 0);
    // Skia uses the platform font rasterizer, so glyph coverage can differ
    // substantially from the CPU placeholder font. The clip and background
    // anchors above verify command ordering; this check only requires that
    // both backends produce content in the text area.
}

TEST_CASE("skia_blits_registered_image", "[skia]") {
    PixelBuffer image;
    image.width = 2;
    image.height = 2;
    // One quadrant semi-transparent to pin the straight-alpha contract.
    image.rgba = {255, 0, 0, 255, 0, 255, 0, 255,
                  0,   0,255, 255, 255,  0, 0, 128};
    CpuRenderer cpu;
    SkiaRenderer skia;
    // Both blit the same 2x2 pattern scaled to 20x20.
    cpu.beginFrame(Size{40.0F, 40.0F});
    cpu.drawImage(cpu.registerImage(image), Rect::fromXYWH(0, 0, 20, 20));
    cpu.endFrame();
    skia.beginFrame(Size{40.0F, 40.0F});
    skia.drawImage(skia.registerImage(std::move(image)),
                   Rect::fromXYWH(0, 0, 20, 20));
    skia.endFrame();
    // Quadrant centers must land on the source colors on both backends.
    const auto readPixel = [](const PixelBuffer& buffer, int x, int y) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(buffer.width) +
             static_cast<std::size_t>(x)) *
            4;
        return Color::fromRGBA(buffer.rgba[offset], buffer.rgba[offset + 1],
                               buffer.rgba[offset + 2], buffer.rgba[offset + 3]);
    };
    CHECK(readPixel(cpu.pixels(), 4, 4) == Color::fromRGBA(255, 0, 0));
    CHECK(readPixel(skia.pixels(), 4, 4) == Color::fromRGBA(255, 0, 0));
    CHECK(readPixel(cpu.pixels(), 15, 4) == Color::fromRGBA(0, 255, 0));
    CHECK(readPixel(skia.pixels(), 15, 4) == Color::fromRGBA(0, 255, 0));
    CHECK(readPixel(cpu.pixels(), 4, 15) == Color::fromRGBA(0, 0, 255));
    CHECK(readPixel(skia.pixels(), 4, 15) == Color::fromRGBA(0, 0, 255));
    // Semi-transparent red (bottom-right) over the dark clear color:
    // (140, 12, 13, 255) on CPU; Skia premultiplies internally and may
    // round a channel by one, so its check allows +/-1.
    CHECK(readPixel(cpu.pixels(), 15, 15) ==
          Color::fromRGBA(140, 12, 13, 255));
    const Color skiaBlend = readPixel(skia.pixels(), 15, 15);
    CHECK(std::abs(skiaBlend.r - 140) <= 1);
    CHECK(std::abs(skiaBlend.g - 12) <= 1);
    CHECK(std::abs(skiaBlend.b - 13) <= 1);
    CHECK(skiaBlend.a == 255);
}

TEST_CASE("skia_paints_counter_frame_consistently", "[skia]") {
    CounterApp app;
    app.setView(Size{320.0F, 180.0F});

    app.renderFrame();  // CPU reference frame (also builds the tree).
    SkiaRenderer skia;
    skia.beginFrame(Size{320.0F, 180.0F});
    lumen::render::PaintOptions options;
    lumen::render::paintScene(skia, app.root(), options);
    skia.endFrame();

    REQUIRE(skia.pixels().width == app.pixels().width);
    REQUIRE(skia.pixels().height == app.pixels().height);

    // Anchor pixels: a blank Skia frame fails these exact checks.
    const auto readPixel = [](const PixelBuffer& buffer, int x, int y) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(buffer.width) +
             static_cast<std::size_t>(x)) *
            4;
        return Color::fromRGBA(buffer.rgba[offset], buffer.rgba[offset + 1],
                               buffer.rgba[offset + 2], buffer.rgba[offset + 3]);
    };
    const lumen::core::RenderNode* button =
        lumen::core::findNodeByKey(app.root(), "increment-button");
    REQUIRE(button != nullptr);
    // Sample the right side of the face, away from the label glyphs.
    const Offset center =
        lumen::core::absoluteOffset(app.root(), "increment-button") +
        Offset{button->size.width * 0.9F, button->size.height * 0.5F};
    // Button face is the dark-theme filled accent on both backends. The
    // expectation derives from the theme so token changes cannot silently
    // invalidate this anchor.
    const Color buttonFace = lumen::style::Theme::dark().button.filled
                                 .background;
    CHECK(readPixel(app.pixels(), static_cast<int>(center.x),
                    static_cast<int>(center.y)) ==
          readPixel(skia.pixels(), static_cast<int>(center.x),
                    static_cast<int>(center.y)));
    CHECK(readPixel(skia.pixels(), static_cast<int>(center.x),
                    static_cast<int>(center.y)) == buttonFace);

    // Text uses different font backends; the button anchor and matching frame
    // dimensions above verify the shared scene output without requiring glyph
    // byte equality.
}

TEST_CASE("skia_preserve_mode_matches_full_clear", "[skia]") {
    // Stage 6 parity: a Preserve frame whose damage clip covers the whole
    SkiaRenderer full;
    // viewport must equal a full Clear frame of the same commands.
    full.beginFrame(Size{80.0F, 60.0F});
    full.drawRect(Rect::fromXYWH(10.0F, 10.0F, 60.0F, 40.0F),
                  Color::fromRGBA(255, 0, 0), CornerRadius::all(8.0F));
    full.drawText(TextRun{"Count: 0", Offset{5.0F, 5.0F}},
                  TextStyle{});
    full.endFrame();

    SkiaRenderer partial;
    partial.beginFrame(Size{80.0F, 60.0F});
    partial.drawRect(Rect::fromXYWH(10.0F, 10.0F, 60.0F, 40.0F),
                     Color::fromRGBA(0, 255, 0), CornerRadius::all(8.0F));
    partial.endFrame();
    // Second frame preserves the first everywhere except the damage clip.
    partial.beginFrame(Size{80.0F, 60.0F},
                       SkiaRenderer::FrameMode::Preserve);
    partial.save();
    partial.clipRect(Rect::fromXYWH(10.0F, 10.0F, 60.0F, 40.0F));
    partial.drawRect(Rect::fromXYWH(10.0F, 10.0F, 60.0F, 40.0F),
                     Color::fromRGBA(255, 0, 0), CornerRadius::all(8.0F));
    partial.drawText(TextRun{"Count: 0", Offset{5.0F, 5.0F}},
                     TextStyle{});
    partial.restore();
    partial.endFrame();

    // Identical command stream within the clip → pixel-equal snapshots.
    const DiffStats stats =
        compareBuffers(full.pixels(), partial.pixels(), 8);
    CHECK(stats.differingPixels <= stats.totalPixels / 50);
    CHECK(stats.meanAbsDiff < 2.0);
}

// --- M1：真实字体事实贯通布局/绘制（Skia build 专用） ---

namespace {

lumen::core::RenderNode layoutRootOfMixedText(const std::string& text,
                                               const lumen::text::FontManager&
                                                   fonts) {
    lumen::core::Widget widget;
    widget.type = lumen::core::WidgetType::Text;
    widget.text = text;
    return lumen::layout::LayoutEngine::layout(
        widget, lumen::core::Constraints::loose(Size{400.0F, 60.0F}), fonts);
}

}  // namespace

TEST_CASE("skia_real_fonts_shape_layout_and_paint", "[skia][text]") {
    std::string diagnostic;
    auto fonts = lumen::text::createSkiaFontManager(&diagnostic);
    INFO("skia diagnostic: " << diagnostic);
    if (fonts == nullptr) {
        // CPU-only 构建不应编入本测试；若工厂返回空，必须明确报告。
        INFO("skia font manager unavailable: " << diagnostic);
        REQUIRE(diagnostic.find("skia") != std::string::npos);
        return;
    }
    INFO("available families: " << fonts->availableFamilies().size());
    if (fonts->availableFamilies().empty()) {
        // Skia 已编译但极简容器无系统字体：布局走占位回退并明确诊断，
        // 应用仍可启动（M1 缺字体可启动条款），编辑索引不受影响。
        const std::string mixed = "Count: A";
        const auto layout = lumen::text::TextLayout::layout(
            mixed, TextStyle{}, 0.0F, *fonts);
        INFO("placeholder fallback: " << layout.usedPlaceholderFallback
                                      << " diag: " << layout.fontDiagnostic);
        CHECK(layout.usedPlaceholderFallback);
        CHECK_FALSE(layout.fontDiagnostic.empty());
        CHECK(layout.graphemeCount == 8);
        return;
    }
    REQUIRE(diagnostic.find("skia") != std::string::npos);

    // 混合 LTR/RTL/CJK：真实度量（baseline 不再是占位 0.8em 的倍数关系
    // 不作跨机器断言，只验证同一份结果贯通）。
    const std::string mixed =
        "Count: \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xE4\xBD\xA0\xE5\xA5\xBD";
    const auto layout = lumen::text::TextLayout::layout(
        mixed, TextStyle{}, 0.0F, *fonts);
    INFO("backend=" << static_cast<int>(layout.fontBackend)
                    << " placeholderFallback=" << layout.usedPlaceholderFallback
                    << " diag=" << layout.fontDiagnostic
                    << " width=" << layout.size.width
                    << " baseline=" << layout.baseline
                    << " runs=" << (layout.lines.empty() ? 0 : layout.lines[0].runs.size()));
    CHECK(layout.fontBackend == lumen::text::FontBackend::Skia);
    CHECK(layout.size.width > 0.0F);
    CHECK(layout.baseline > 0.0F);
    REQUIRE(layout.lines.size() == 1);
    bool anyRealRun = false;
    for (const auto& run : layout.lines[0].runs) {
        INFO("run family=" << run.family << " placeholder=" << run.placeholder
                           << " glyphs=" << run.glyphs.size());
        if (!run.placeholder && !run.glyphs.empty()) {
            anyRealRun = true;
        }
    }
    // 字体子集镜像缺目标脚本时（如最小化 CI 镜像无 CJK/希伯来）可能全为
    // 占位：此时 shaping 无真实 run，绘制确定性仍可验证，但“真实贯通”
    // 断言应 SKIP（环境字体缺失，非逻辑错；与 GPU 缺硬件 SKIP 一致）。
    if (!anyRealRun) {
        INFO("no real shaping runs; skipping real-font paint checks");
        SKIP("system fonts lack coverage for mixed-script shaping");
    }

    // 布局与绘制共享同一份字体源：Skia 消费 shaped 命令出帧且确定性。
    const auto paint = [&](SkiaRenderer& renderer) {
        renderer.beginFrame(Size{400.0F, 60.0F});
        lumen::render::RenderCommandList commands =
            lumen::render::recordScene(
                layoutRootOfMixedText(mixed, *fonts), {}, *fonts);
        lumen::render::FrameInfo info;
        info.viewport = Size{400.0F, 60.0F};
        renderer.submit(commands, info);
        renderer.endFrame();
    };
    SkiaRenderer first;
    paint(first);
    const std::uint64_t hashA = lumen::render::frameHash(first.pixels());
    SkiaRenderer second;
    paint(second);
    CHECK(lumen::render::frameHash(second.pixels()) == hashA);
    CHECK(hashA != 0);
}
