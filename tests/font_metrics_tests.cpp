#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>

#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/text/system_font_manager.h"
#include "lumen/text/text_layout.h"

#ifdef LUMEN_HAS_SKIA_BACKEND
#include "lumen/render/skia_renderer.h"
#include "lumen/text/skia_font_manager.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef DrawText
#include "stb_truetype.h"
#endif

using namespace lumen;

TEST_CASE("system_font_text_clip_preserves_cjk_and_mixed_ink",
          "[text][system-fonts][render]") {
    auto loaded = text::createSystemFontManager();
    if (!loaded) {
        SKIP("No system fonts available");
    }
    const std::shared_ptr<text::SystemFontManager> fonts(std::move(loaded));
    for (const float size : {10.0F, 14.0F, 36.0F}) {
        for (const std::string value : {"曩中，", "Ag曩中，", "😀"}) {
            CAPTURE(size, value);
            core::TextStyle style;
            style.fontSize = size;
            style.lineHeight = 1.03F;
            style.color = core::Color::fromRGBA(255, 255, 255);
            const auto layout = text::TextLayout::layout(value, style, 0, *fonts);
            CHECK(layout.lineHeightPx == Catch::Approx(size * 1.03F));
            text::FontQuery query;
            query.sizePx = size;
            for (const auto& cp : text::decodeUtf8(value)) {
                text::GlyphBitmap bitmap;
                REQUIRE(fonts->bitmapFor(cp.codePoint, query, 1, &bitmap));
                CHECK(layout.baseline - bitmap.bearingTop >= 0);
                CHECK(layout.baseline - bitmap.bearingTop + bitmap.height <=
                      std::ceil(layout.lineBoxHeightPx));
            }

            // 相同的 drawText 命令，比较正常节点 clip 与取消垂直 clip
            // 后的完整 framebuffer，防止“度量看似足够”却仍丢墨迹。
            auto node = layout::LayoutEngine::layout(
                core::makeText(value, style),
                core::Constraints::loose({300, 150}), *fonts);
            node.offset.y = 20;  // 上方也留出空间，检测负的 inkTop。
            const auto commands = render::recordScene(node, {}, *fonts);
            for (const float dpi : {1.0F, 1.5F, 2.0F}) {
                CAPTURE(dpi);
                render::CpuRenderer clipped(dpi);
                render::CpuRenderer unbounded(dpi);
                clipped.setSystemFonts(fonts);
                unbounded.setSystemFonts(fonts);
                clipped.beginFrame({300, 150});
                unbounded.beginFrame({300, 150});
                render::paintScene(clipped, node, {}, *fonts);
                for (const auto& command : commands.commands()) {
                    if (command.type == render::CommandType::DrawText) {
                        // 保留相同的水平裁剪；本测试只比较垂直完整性。
                        unbounded.clipRect({{0, 0}, {node.size.width, 150}});
                        unbounded.drawText(command.textRun, command.textStyle);
                    }
                }
                clipped.endFrame();
                unbounded.endFrame();
                CHECK(render::frameHash(clipped.pixels()) ==
                      render::frameHash(unbounded.pixels()));
            }
        }
    }
}

#ifdef LUMEN_HAS_SKIA_BACKEND
TEST_CASE("skia_text_clip_preserves_fallback_and_emoji_ink",
          "[text][skia][render]") {
    const auto fonts = text::createSkiaFontManager();
    REQUIRE(fonts);
    for (const float size : {10.0F, 14.0F, 36.0F}) {
        for (const std::string value : {"中", "Ag中", "😀"}) {
            CAPTURE(size, value);
            core::TextStyle style;
            style.fontSize = size;
            style.lineHeight = 1.03F;
            style.color = core::Color::fromRGBA(255, 255, 255);
            auto node = layout::LayoutEngine::layout(
                core::makeText(value, style),
                core::Constraints::loose({300, 150}), *fonts);
            node.offset.y = 20;
            const auto commands = render::recordScene(node, {}, *fonts);
            for (const float dpi : {1.0F, 1.5F, 2.0F}) {
                CAPTURE(dpi);
                render::SkiaRenderer clipped;
                render::SkiaRenderer unbounded;
                clipped.setDeviceScale(dpi);
                unbounded.setDeviceScale(dpi);
                clipped.beginFrame({300, 150});
                unbounded.beginFrame({300, 150});
                render::paintScene(clipped, node, {}, *fonts);
                for (const auto& command : commands.commands()) {
                    if (command.type == render::CommandType::DrawText) {
                        unbounded.clipRect({{0, 0}, {node.size.width, 150}});
                        unbounded.drawText(command.textRun, command.textStyle);
                    }
                }
                clipped.endFrame();
                unbounded.endFrame();
                CHECK(render::frameHash(clipped.pixels()) ==
                      render::frameHash(unbounded.pixels()));
            }
        }
    }
}
#endif

#ifdef _WIN32
TEST_CASE("gdi_family_validation_accepts_localized_aliases_and_weights",
          "[text][system-fonts]") {
    const auto fonts = text::createSystemFontManager();
    REQUIRE(fonts);
    for (const std::string family : {"Microsoft YaHei", "microsoft yahei", "Segoe UI"}) {
        for (const auto weight : {text::FontWeight::Normal, text::FontWeight::SemiBold,
                                  text::FontWeight::ExtraBold}) {
            CAPTURE(family, static_cast<int>(weight));
            text::FontQuery query;
            query.family = family;
            query.weight = weight;
            text::GlyphMetrics metrics;
            text::GlyphBitmap bitmap;
            REQUIRE(fonts->glyphMetrics(query, U'A', &metrics));
            REQUIRE(fonts->bitmapFor(U'A', query, 1, &bitmap));
            CHECK(fonts->diagnostic().find("gdi-substitution=") == std::string::npos);
        }
    }
}

namespace {

struct TemporaryFont {
    std::filesystem::path directory;
    std::filesystem::path file;

    TemporaryFont() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() /
                    ("lumen-font-metrics-" + std::to_string(stamp));
        REQUIRE(std::filesystem::create_directory(directory));
        file = directory / "lumennoname.ttf";
    }
    ~TemporaryFont() {
        std::error_code error;
        std::filesystem::remove(file, error);
        std::filesystem::remove(directory, error);
    }
};

}  // namespace

TEST_CASE("gdi_font_substitution_uses_loaded_font_and_reports_diagnostic",
          "[text][system-fonts]") {
    wchar_t windowsDirectory[MAX_PATH]{};
    REQUIRE(GetWindowsDirectoryW(windowsDirectory, MAX_PATH) > 0);
    std::ifstream source(std::filesystem::path(windowsDirectory) / "Fonts" /
                             "segoeui.ttf",
                         std::ios::binary);
    if (!source) {
        SKIP("Segoe UI fixture font unavailable");
    }
    std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(source),
                                     std::istreambuf_iterator<char>()};
    REQUIRE(bytes.size() > 12);
    const int tables = (static_cast<int>(bytes[4]) << 8) | bytes[5];
    bool removedName = false;
    for (int i = 0; i < tables; ++i) {
        const std::size_t record = 12 + static_cast<std::size_t>(i) * 16;
        REQUIRE(record + 16 <= bytes.size());
        if (std::string(reinterpret_cast<const char*>(bytes.data() + record), 4) ==
            "name") {
            // 保留真实字形/度量，只隐藏 name 表，迫使查询使用文件名 stem。
            bytes[record] = 'x';
            removedName = true;
            break;
        }
    }
    REQUIRE(removedName);
    const TemporaryFont fixture;
    {
        std::ofstream out(fixture.file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }
    const auto fonts = text::createSystemFontManager(nullptr, {fixture.directory.string()});
    REQUIRE(fonts);
    text::FontQuery query;
    query.sizePx = 14;
    REQUIRE(fonts->resolveFamily(query, U'W') == "lumennoname");
    text::GlyphMetrics metrics;
    REQUIRE(fonts->glyphMetrics(query, U'W', &metrics));
    stbtt_fontinfo info{};
    REQUIRE(stbtt_InitFont(&info, bytes.data(), 0) != 0);
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&info, 'W', &advance, &bearing);
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, query.sizePx);
    CHECK(metrics.advanceEm * query.sizePx == Catch::Approx(advance * scale));
    int ascent = 0;
    int descent = 0;
    int gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    float actualAscent = 0;
    float actualDescent = 0;
    REQUIRE(fonts->horizontalMetrics(query, &actualAscent, &actualDescent));
    CHECK(actualAscent == Catch::Approx(ascent * scale));
    CHECK(actualDescent == Catch::Approx(-descent * scale));
    text::GlyphBitmap bitmap;
    REQUIRE(fonts->bitmapFor(U'W', query, 1, &bitmap));
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
    unsigned char* expected = stbtt_GetCodepointBitmap(
        &info, scale, scale, 'W', &width, &height, &left, &top);
    REQUIRE(expected != nullptr);
    const std::vector<unsigned char> coverage(expected, expected + width * height);
    stbtt_FreeBitmap(expected, nullptr);
    CHECK(bitmap.coverage == coverage);
    CHECK(bitmap.bearingTop == -top);
    CHECK(fonts->diagnostic().find("gdi-substitution=lumennoname (using stb)") !=
          std::string::npos);
}
#endif

