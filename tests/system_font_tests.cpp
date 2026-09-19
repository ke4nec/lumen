// 桌面系统字体后端测试（Windows 雅黑 / CPU 真实字形）。
//
// 缺系统字体（最小容器等）的环境跳过断言、明确 SUCCEED；显式无
// 效目录的失败路径全平台确定性覆盖。

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#include "lumen/render/cpu_renderer.h"
#include "lumen/text/system_font_manager.h"
#include "lumen/text/text_layout.h"

using namespace lumen;

namespace {

std::shared_ptr<text::SystemFontManager> loadSystemFonts() {
    std::string diagnostic;
    auto fonts = text::createSystemFontManager(&diagnostic);
    if (fonts == nullptr) {
        INFO("no system fonts: " << diagnostic);
        return nullptr;
    }
    return std::shared_ptr<text::SystemFontManager>(std::move(fonts));
}

}  // namespace

TEST_CASE("system_fonts_fail_cleanly_for_missing_directories",
          "[text][system-fonts]") {
    std::string diagnostic;
    auto fonts = text::createSystemFontManager(
        &diagnostic, {"C:/definitely-not-a-font-dir-xyz"});
    CHECK(fonts == nullptr);
    CHECK_FALSE(diagnostic.empty());
}

TEST_CASE("system_fonts_cover_latin_and_cjk", "[text][system-fonts]") {
    const auto fonts = loadSystemFonts();
    if (fonts == nullptr) {
        SUCCEED("environment has no system fonts; nothing to rasterize");
        return;
    }
    CHECK(fonts->backend() == text::FontBackend::System);
    CHECK_FALSE(fonts->availableFamilies().empty());
#ifdef _WIN32
    // 中文 Windows 默认栈首选雅黑：族必须存在且覆盖 CJK。
    bool hasYaHei = false;
    for (const auto& family : fonts->availableFamilies()) {
        if (family == "microsoft yahei") {
            hasYaHei = true;
        }
    }
    CHECK(hasYaHei);
#endif

    text::FontQuery query;
    query.sizePx = 14.0F;
    text::GlyphMetrics metrics{};
    REQUIRE(fonts->glyphMetrics(query, U'A', &metrics));
    CHECK(metrics.advanceEm > 0.0F);
    REQUIRE(fonts->glyphMetrics(query, U'\u4E2D', &metrics));
    CHECK(metrics.advanceEm > 0.0F);
    const auto cjk = fonts->resolveWithStatus(query, U'\u4E2D');
    CHECK_FALSE(cjk.missing);
#ifdef _WIN32
    // The Windows CJK stack is intentionally anchored on Microsoft YaHei;
    // this prevents a directory-enumeration order from silently selecting a
    // different installed face.
    CHECK(cjk.resolvedFamily == "microsoft yahei");
    // 拉丁文恒走 Segoe（原生界面行为）：中文系统也不得回落雅黑，否则
    // 小字号粗体拉丁字母侧轴承过紧、看起来“黏在一起”。
    const auto latin = fonts->resolveWithStatus(query, U'A');
    CHECK_FALSE(latin.missing);
    CHECK(latin.resolvedFamily == "segoe ui");
    CHECK_FALSE(latin.fallbackUsed);
    // 显式族名 + 粗体不得被先加载的雅黑劫持（faceFor 族命中优先）。
    text::FontQuery boldQuery;
    boldQuery.sizePx = 13.0F;
    boldQuery.weight = text::FontWeight::ExtraBold;
    boldQuery.family = "Segoe UI";
    const auto boldLatin = fonts->resolveWithStatus(boldQuery, U'L');
    CHECK_FALSE(boldLatin.missing);
    CHECK(boldLatin.resolvedFamily == "segoe ui");
    CHECK_FALSE(boldLatin.fallbackUsed);
#endif
    float ascent = 0.0F;
    float descent = 0.0F;
    REQUIRE(fonts->horizontalMetrics(query, &ascent, &descent));
    CHECK(ascent > 0.0F);
    CHECK(descent > 0.0F);

    text::GlyphBitmap bitmap;
    REQUIRE(fonts->bitmapFor(U'A', query, 1.0F, &bitmap));
    CHECK_FALSE(bitmap.empty());
    CHECK(bitmap.coverage.size() ==
          static_cast<std::size_t>(bitmap.width) *
              static_cast<std::size_t>(bitmap.height));
    // GGO_GRAY8_BITMAP is already top-to-bottom.  An accidental row flip
    // makes the capital F wider at the bottom, which is visibly upside down.
    text::GlyphBitmap fBitmap;
    REQUIRE(fonts->bitmapFor(U'F', query, 1.0F, &fBitmap));
    auto inkInRow = [&fBitmap](int row) {
        std::size_t count = 0;
        for (int col = 0; col < fBitmap.width; ++col) {
            count += fBitmap.coverage[static_cast<std::size_t>(
                         row * fBitmap.width + col)] > 32;
        }
        return count;
    };
    CHECK(inkInRow(0) > inkInRow(fBitmap.height - 1));
    // 空白字形无位图（调用方按 advance 留白，不画占位盒）。
    CHECK_FALSE(fonts->bitmapFor(U' ', query, 1.0F, &bitmap));
}

TEST_CASE("cpu_renderer_system_fonts_draw_real_glyphs",
          "[render][system-fonts]") {
    const auto fonts = loadSystemFonts();
    if (fonts == nullptr) {
        SUCCEED("environment has no system fonts; nothing to rasterize");
        return;
    }
    render::TextRun run;
    run.text = "Ai";
    run.origin = core::Offset{10.0F, 10.0F};
    run.baselinePx = 11.2F;  // 14px * 0.8（与占位 ascent 同口径）。
    render::TextGlyphRun glyphs;
    glyphs.family = {};
    glyphs.placeholder = false;
    text::FontQuery query;
    query.sizePx = 14.0F;
    float pen = 0.0F;
    for (char32_t cp : {U'A', U'i'}) {
        text::GlyphMetrics metrics{};
        REQUIRE(fonts->glyphMetrics(query, cp, &metrics));
        text::ShapedGlyph glyph;
        glyph.glyphId = static_cast<std::uint32_t>(cp);
        glyph.advancePx = metrics.advanceEm * query.sizePx;
        glyph.xOffsetPx = pen;
        glyph.cluster = 0;
        pen += glyph.advancePx;
        glyphs.glyphs.push_back(glyph);
    }
    run.shapedRuns = {glyphs};
    core::TextStyle style;
    style.fontSize = 14.0F;
    style.color = core::Color::fromRGBA(255, 255, 255);

    render::CpuRenderer placeholder;
    placeholder.beginFrame(core::Size{200.0F, 60.0F});
    placeholder.drawText(run, style);
    placeholder.endFrame();

    render::CpuRenderer raster;
    raster.setSystemFonts(fonts);
    REQUIRE(raster.hasSystemFonts());
    raster.beginFrame(core::Size{200.0F, 60.0F});
    raster.drawText(run, style);
    raster.endFrame();

    // 真实字形像素与占位点阵不同（形状变了），且确有墨迹落纸。
    CHECK(render::frameHash(placeholder.pixels()) !=
          render::frameHash(raster.pixels()));
    std::size_t inked = 0;
    for (std::size_t i = 0; i + 3 < raster.pixels().rgba.size(); i += 4) {
        if (raster.pixels().rgba[i] != 24 ||
            raster.pixels().rgba[i + 1] != 24 ||
            raster.pixels().rgba[i + 2] != 24) {
            ++inked;
        }
    }
    CHECK(inked > 100);
}

TEST_CASE("system_font_layout_keeps_ltr_visual_order",
          "[text][system-fonts]") {
    const auto fonts = loadSystemFonts();
    if (fonts == nullptr) {
        SUCCEED("environment has no system fonts; nothing to lay out");
        return;
    }
    core::TextStyle style;
    style.fontSize = 14.0F;
    const auto layout = text::TextLayout::layout(
        "ABC中文", style, 0.0F, *fonts);
    REQUIRE(layout.lines.size() == 1);
    CHECK(layout.lines[0].visual == "ABC中文");
    REQUIRE(layout.lines[0].runs.size() >= 1);
    float previous = -1.0F;
    for (const auto& run : layout.lines[0].runs) {
        for (const auto& glyph : run.glyphs) {
            CHECK(glyph.xOffsetPx >= previous);
            previous = glyph.xOffsetPx;
        }
    }
}

TEST_CASE("system_font_advance_is_not_pixel_quantized",
          "[text][system-fonts]") {
    // 横向步进曾取 GDI GGO_METRICS 的 gmCellIncX——整数量化步进逐字形
    // 累积把字距系统性撑歪（标题栏 "L u m e n" 肉眼可辨，渲染 PNG 评审
    // 定位）。现在度量与位图同源走字体设计值（stb hmtx 分数步进），
    // 本测试钉住：常用字形/字号组合中必须出现非整数步进。
    const auto fonts = loadSystemFonts();
    if (fonts == nullptr) {
        SUCCEED("environment has no system fonts; nothing to measure");
        return;
    }
    bool sawFractional = false;
    for (const float size : {13.0F, 14.0F, 15.0F, 16.0F, 17.0F}) {
        text::FontQuery query;
        query.sizePx = size;
        for (const char32_t codePoint :
             {U'L', U'u', U'm', U'e', U'n', U'W', U'G', U'a'}) {
            text::GlyphMetrics metrics{};
            if (!fonts->glyphMetrics(query, codePoint, &metrics)) {
                continue;
            }
            const float advancePx = metrics.advanceEm * size;
            const float fractional = advancePx - std::floor(advancePx);
            if (fractional > 0.05F && fractional < 0.95F) {
                sawFractional = true;
            }
        }
    }
    CHECK(sawFractional);
}
