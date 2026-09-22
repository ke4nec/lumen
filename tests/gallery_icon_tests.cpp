// Gallery 应用图标（Core Dark 方向）测试：几何契约、光栅分层规则、
// PNG/ICO/ICNS 容器与 runApp 窗口图标装配（docs/lumen-gallery-icon-design.md
// §4 验收；母版见 examples/gallery/gallery_icon.h，设计稿
// design/gallery-icon.html 方向 01）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "gallery_icon.h"
#include "lumen/app/app_shell.h"
#include "lumen/platform/fake_host.h"

using lumen::app::AppShell;
using lumen::app::RunOptions;
using lumen::app::ShellConfig;
using lumen::core::Size;
using lumen::core::Widget;
using lumen::examples::GalleryIconBitmap;
using lumen::platform::FakeApplicationHost;

namespace {

struct Pixel {
    std::uint8_t r, g, b, a;
};

Pixel pixelAt(const GalleryIconBitmap& bitmap, int x, int y) {
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.size) +
         static_cast<std::size_t>(x)) * 4U;
    return {bitmap.rgba[index], bitmap.rgba[index + 1], bitmap.rgba[index + 2],
            bitmap.rgba[index + 3]};
}

bool nearChannel(int actual, int expected, int tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

ShellConfig iconShellConfig() {
    ShellConfig config;
    config.initialView = Size{320.0F, 240.0F};
    config.build = [] { return Widget{}; };  // 默认 Container。
    return config;
}

}  // namespace

// --- 几何契约：手调像素网格（≤32）与比例档（≥48） ---

TEST_CASE("gallery_icon_geometry_uses_pixel_grid_below_48", "[gallery-icon]") {
    const auto g16 = lumen::examples::galleryIconGeometry(16);
    REQUIRE(g16.margin == 3.0);
    REQUIRE(g16.box == 10.0);
    REQUIRE(g16.stroke == 3.0);
    REQUIRE(g16.flatBadge);
    const auto g32 = lumen::examples::galleryIconGeometry(32);
    REQUIRE(g32.margin == 6.0);
    REQUIRE(g32.box == 20.0);
    REQUIRE(g32.stroke == 6.0);
    REQUIRE(g32.flatBadge);
    const auto g64 = lumen::examples::galleryIconGeometry(64);
    REQUIRE_FALSE(g64.flatBadge);
    REQUIRE(g64.margin == Catch::Approx(0.19 * 64).margin(0.001));
    REQUIRE(g64.stroke == Catch::Approx(0.17 * 64).margin(0.001));
}

// --- 光栅：尺寸、圆角透明、双色标记与分层规则 ---

TEST_CASE("gallery_icon_renders_all_export_sizes", "[gallery-icon]") {
    for (const int size : lumen::examples::kGalleryIconExportSizes) {
        const GalleryIconBitmap bitmap = lumen::examples::renderGalleryIcon(size);
        REQUIRE(bitmap.size == size);
        REQUIRE(bitmap.rgba.size() ==
                static_cast<std::size_t>(size) * size * 4U);
        // 圆角外保持透明（左上角在 22.5% 圆角外）。
        REQUIRE(pixelAt(bitmap, 0, 0).a == 0);
        // 徽章中心不透明。
        REQUIRE(pixelAt(bitmap, size / 2, size / 4).a == 255);
    }
}

TEST_CASE("gallery_icon_bars_match_core_dark_tokens", "[gallery-icon]") {
    const GalleryIconBitmap bitmap = lumen::examples::renderGalleryIcon(128);
    const auto geometry = lumen::examples::galleryIconGeometry(128);
    // 竖笔（Column）中心：白 #f1f1f4。
    const int vx = static_cast<int>(geometry.margin + geometry.stroke / 2);
    const Pixel vertical = pixelAt(bitmap, vx, 64);
    REQUIRE(nearChannel(vertical.r, 0xF1, 4));
    REQUIRE(nearChannel(vertical.g, 0xF1, 4));
    REQUIRE(nearChannel(vertical.b, 0xF4, 4));
    REQUIRE(vertical.a == 255);
    // 横笔（Row）中心：accent #568cf0。
    const int hy =
        static_cast<int>(geometry.margin + geometry.box - geometry.stroke / 2);
    const Pixel horizontal = pixelAt(bitmap, 64, hy);
    REQUIRE(nearChannel(horizontal.r, 0x56, 4));
    REQUIRE(nearChannel(horizontal.g, 0x8C, 4));
    REQUIRE(nearChannel(horizontal.b, 0xF0, 4));
    REQUIRE(horizontal.a == 255);
}

TEST_CASE("gallery_icon_flat_badge_below_48_gradient_above", "[gallery-icon]") {
    // ≤32 任务栏/小图档：平面 #262630，无渐变无内缘。
    const GalleryIconBitmap small = lumen::examples::renderGalleryIcon(16);
    const Pixel flat = pixelAt(small, 2, 8);
    REQUIRE(nearChannel(flat.r, 0x26, 2));
    REQUIRE(nearChannel(flat.g, 0x26, 2));
    REQUIRE(nearChannel(flat.b, 0x30, 2));
    // ≥48 渐变档：上亮下暗（双阶渐变可感知）。
    const GalleryIconBitmap large = lumen::examples::renderGalleryIcon(64);
    const Pixel top = pixelAt(large, 4, 8);
    const Pixel bottom = pixelAt(large, 4, 58);
    REQUIRE(top.r > bottom.r + 4);
    // 内缘环：外边界像素列整列混白，比内部亮（inset 环贴边 1px）。
    const Pixel ring = pixelAt(large, 0, 32);
    const Pixel interior = pixelAt(large, 4, 32);
    REQUIRE(ring.a == 255);
    REQUIRE(ring.r > interior.r + 8);
}

TEST_CASE("gallery_icon_pixel_grid_keeps_bar_edges_on_integers",
          "[gallery-icon]") {
    // 16px 手调网格：竖笔占 x∈[3,6)，x=4 为笔芯白，x=2 仍为徽章底。
    const GalleryIconBitmap bitmap = lumen::examples::renderGalleryIcon(16);
    const Pixel bar = pixelAt(bitmap, 4, 8);
    REQUIRE(nearChannel(bar.r, 0xF1, 6));
    REQUIRE(nearChannel(bar.b, 0xF4, 6));
    const Pixel badge = pixelAt(bitmap, 2, 8);
    REQUIRE(nearChannel(badge.r, 0x26, 2));
    REQUIRE(badge.a == 255);
}

TEST_CASE("gallery_icon_rejects_non_positive_size", "[gallery-icon]") {
    // 前置条件守卫：空位图按 WindowIcon width<=0 语义安全跳过。
    const GalleryIconBitmap empty = lumen::examples::renderGalleryIcon(0);
    REQUIRE(empty.size == 0);
    REQUIRE(empty.rgba.empty());
}

// --- 容器：PNG/ICO/ICNS 结构（自产自检，消费方为系统壳层） ---

TEST_CASE("gallery_icon_png_stream_is_well_formed", "[gallery-icon]") {
    const GalleryIconBitmap bitmap = lumen::examples::renderGalleryIcon(32);
    const std::vector<std::uint8_t> png =
        lumen::examples::encodeGalleryIconPng(bitmap);
    REQUIRE(png.size() > 8);
    const std::uint8_t signature[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A,
                                      0x1A, 0x0A};
    for (std::size_t i = 0; i < sizeof(signature); ++i) {
        REQUIRE(png[i] == signature[i]);
    }
    // IHDR：宽高 32（大端低字节落在 19/23）、bit depth 8、RGBA(6)。
    REQUIRE(png[12] == 'I');
    REQUIRE(png[13] == 'H');
    REQUIRE(png[14] == 'D');
    REQUIRE(png[15] == 'R');
    REQUIRE(png[19] == 32);
    REQUIRE(png[23] == 32);
    REQUIRE(png[24] == 8);
    REQUIRE(png[25] == 6);
    // zlib 流头（stored deflate 0x78 0x01）与 IEND 收尾。
    std::size_t cursor = 8;
    bool sawIdat = false;
    bool sawIend = false;
    while (cursor + 8 <= png.size()) {
        const std::uint32_t length = (static_cast<std::uint32_t>(png[cursor]) << 24) |
                                     (static_cast<std::uint32_t>(png[cursor + 1]) << 16) |
                                     (static_cast<std::uint32_t>(png[cursor + 2]) << 8) |
                                     png[cursor + 3];
        const char a = static_cast<char>(png[cursor + 4]);
        const char b = static_cast<char>(png[cursor + 5]);
        const char c = static_cast<char>(png[cursor + 6]);
        const char d = static_cast<char>(png[cursor + 7]);
        if (a == 'I' && b == 'D' && c == 'A' && d == 'T') {
            sawIdat = true;
            REQUIRE(png[cursor + 8] == 0x78);
            REQUIRE(png[cursor + 9] == 0x01);
        }
        if (a == 'I' && b == 'E' && c == 'N' && d == 'D') {
            sawIend = true;
            REQUIRE(cursor + 12 == png.size());
        }
        cursor += 8 + length + 4;
    }
    REQUIRE(sawIdat);
    REQUIRE(sawIend);
}

TEST_CASE("gallery_icon_ico_bundles_all_export_sizes", "[gallery-icon]") {
    std::vector<GalleryIconBitmap> bitmaps;
    for (const int size : lumen::examples::kGalleryIconExportSizes) {
        bitmaps.push_back(lumen::examples::renderGalleryIcon(size));
    }
    const std::vector<std::uint8_t> ico =
        lumen::examples::encodeGalleryIconIco(bitmaps);
    REQUIRE(ico.size() > 6);
    REQUIRE(ico[0] == 0);
    REQUIRE(ico[1] == 0);
    REQUIRE(ico[2] == 1);  // type icon（小端）
    REQUIRE(ico[3] == 0);
    const int count = ico[4] | (ico[5] << 8);
    REQUIRE(count == 8);
    // 每个条目指向 PNG 流（Vista+ PNG-in-ICO），尾条目落在文件末尾。
    std::uint32_t offset = 6 + 16 * count;
    for (int i = 0; i < count; ++i) {
        const int entry = 6 + 16 * i;
        const int declared = ico[entry] == 0 ? 256 : ico[entry];
        REQUIRE(declared == lumen::examples::kGalleryIconExportSizes[i]);
        const std::uint32_t size =
            ico[entry + 8] | (ico[entry + 9] << 8) | (ico[entry + 10] << 16) |
            (static_cast<std::uint32_t>(ico[entry + 11]) << 24);
        const std::uint32_t at =
            ico[entry + 12] | (ico[entry + 13] << 8) | (ico[entry + 14] << 16) |
            (static_cast<std::uint32_t>(ico[entry + 15]) << 24);
        REQUIRE(at == offset);
        REQUIRE(ico[at] == 0x89);
        REQUIRE(ico[at + 1] == 0x50);  // 'P'
        offset += size;
    }
    REQUIRE(offset == ico.size());
}

TEST_CASE("gallery_icon_icns_covers_macos_slots", "[gallery-icon]") {
    std::vector<GalleryIconBitmap> bitmaps;
    for (const int size : lumen::examples::kGalleryIconExportSizes) {
        bitmaps.push_back(lumen::examples::renderGalleryIcon(size));
    }
    const std::vector<std::uint8_t> icns =
        lumen::examples::encodeGalleryIconIcns(bitmaps);
    REQUIRE(icns.size() > 8);
    REQUIRE(icns[0] == 'i');
    REQUIRE(icns[1] == 'c');
    REQUIRE(icns[2] == 'n');
    REQUIRE(icns[3] == 's');
    const std::uint32_t total =
        (static_cast<std::uint32_t>(icns[4]) << 24) |
        (static_cast<std::uint32_t>(icns[5]) << 16) |
        (static_cast<std::uint32_t>(icns[6]) << 8) | icns[7];
    REQUIRE(total == icns.size());
    // 期望槽位序列（16/32/64/128/256 → icp4/ic11/ic12/ic07/ic08）。
    const std::vector<std::string> types = {"icp4", "ic11", "ic12", "ic07",
                                            "ic08"};
    std::size_t cursor = 8;
    std::size_t slot = 0;
    while (cursor + 8 <= icns.size()) {
        const std::string type(reinterpret_cast<const char*>(&icns[cursor]), 4);
        REQUIRE(slot < types.size());
        REQUIRE(type == types[slot]);
        const std::uint32_t length =
            (static_cast<std::uint32_t>(icns[cursor + 4]) << 24) |
            (static_cast<std::uint32_t>(icns[cursor + 5]) << 16) |
            (static_cast<std::uint32_t>(icns[cursor + 6]) << 8) |
            icns[cursor + 7];
        REQUIRE(length >= 8);
        REQUIRE(icns[cursor + 8] == 0x89);  // 槽内为 PNG
        cursor += length;
        ++slot;
    }
    REQUIRE(slot == types.size());
    REQUIRE(cursor == icns.size());
}

// --- runApp 装配：provider 送达宿主、空图标与失败安全降级 ---

TEST_CASE("run_app_sets_window_icon_from_options", "[app][gallery-icon]") {
    FakeApplicationHost host;
    AppShell shell{iconShellConfig()};
    shell.setView(Size{320.0F, 240.0F});
    (void)shell.renderFrame();
    REQUIRE(host.initialize());

    RunOptions options;
    options.maxFrames = 1;
    const GalleryIconBitmap expected = lumen::examples::renderGalleryIcon(48);
    options.windowIcon = [&expected] {
        return lumen::platform::WindowIcon{expected.size, expected.size,
                                           expected.rgba};
    };
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    REQUIRE(host.iconCalls.size() == 1);
    REQUIRE(host.iconCalls[0].icon.width == 48);
    REQUIRE(host.iconCalls[0].icon.height == 48);
    REQUIRE(host.iconCalls[0].icon.rgba == expected.rgba);
}

TEST_CASE("run_app_skips_window_icon_when_provider_returns_empty",
          "[app][gallery-icon]") {
    FakeApplicationHost host;
    AppShell shell{iconShellConfig()};
    shell.setView(Size{320.0F, 240.0F});
    (void)shell.renderFrame();
    REQUIRE(host.initialize());

    RunOptions options;
    options.maxFrames = 1;
    options.windowIcon = [] { return lumen::platform::WindowIcon{}; };
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    REQUIRE(host.iconCalls.empty());
}

TEST_CASE("run_app_survives_window_icon_failure", "[app][gallery-icon]") {
    FakeApplicationHost host;
    host.setIconFailure(
        lumen::platform::ServiceResult::failed("icon rejected"));
    AppShell shell{iconShellConfig()};
    shell.setView(Size{320.0F, 240.0F});
    (void)shell.renderFrame();
    REQUIRE(host.initialize());

    RunOptions options;
    options.maxFrames = 1;
    const GalleryIconBitmap bitmap = lumen::examples::renderGalleryIcon(16);
    options.windowIcon = [&bitmap] {
        return lumen::platform::WindowIcon{bitmap.size, bitmap.size,
                                           bitmap.rgba};
    };
    // 失败只降级诊断，不阻塞启动。
    REQUIRE(lumen::app::runApp(shell, host, options) == 0);
    REQUIRE(host.iconCalls.size() == 1);
}
