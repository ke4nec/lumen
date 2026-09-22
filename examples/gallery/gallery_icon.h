// Gallery 应用图标（Core Dark 方向，design/gallery-icon.html 01 选定落地）。
//
// 双色字母标「L」：竖笔 = Column（content.primary）、横笔 = Row（accent），
// 框架两组布局原语构成 Gallery 首字母。徽章为深色双阶渐变 + 1px 内缘
// 高光（≥48）；≤32 任务栏/小图档收敛为平面双色，笔画按手调像素网格
// 取整（docs/lumen-gallery-icon-design.md 几何契约）。
//
// 本头文件只承载图标母版和 RGBA 光栅化：运行时窗口图标（examples/gallery/
// main.cpp 按 DPI 现场光栅化，系统不放大位图）与构建期资源导出共用同一
// 几何。PNG/ICO/ICNS 编码在 gallery_icon.cpp 中使用第三方 stb_image_write
// 和轻量平台容器封装，避免把图片格式实现放进公共头文件。

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lumen::examples {

// 直通 RGBA8 位图（宽=高）。
struct GalleryIconBitmap {
    int size{0};
    std::vector<std::uint8_t> rgba{};
};

// 出口尺寸（design/gallery-icon.html 阶梯）：20 供 125% DPI 列表，
// 28 为标题栏品牌位（非出口，运行时按需光栅化）。
inline constexpr int kGalleryIconExportSizes[] = {16, 20, 24, 32,
                                                  48, 64, 128, 256};

// 标记几何（px）：margin = 徽章内边距，box = 标记盒边长（正方形 L 的
// 外沿），stroke = 笔宽；flatBadge = 平面双色档（≤32）。16/20/24/32 为
// 手调整数像素网格（条宽/坐标对齐设备像素，visual-system §8 lround 口径），
// 其余按比例缩放。
struct GalleryIconGeometry {
    double margin{0.0};
    double box{0.0};
    double stroke{0.0};
    bool flatBadge{false};
};

[[nodiscard]] inline GalleryIconGeometry galleryIconGeometry(int size) {
    switch (size) {
        case 16:
            return {3.0, 10.0, 3.0, true};
        case 20:
            return {4.0, 12.0, 4.0, true};
        case 24:
            return {5.0, 14.0, 4.0, true};
        case 32:
            return {6.0, 20.0, 6.0, true};
        default:
            // ≥48 比例缩放（渐变档）；28 等非出口小档按基础比例 + 平面。
            return {0.19 * static_cast<double>(size),
                    0.62 * static_cast<double>(size),
                    0.17 * static_cast<double>(size), size < 48};
    }
}

namespace gallery_icon_detail {

// 圆角矩形 SDF（< 0 = 内部）：中心 + 半宽高 + 圆角的标准有符号距离式。
[[nodiscard]] inline float roundedRectSdf(float px, float py, float cx,
                                          float cy, float hw, float hh,
                                          float radius) {
    const float qx = std::abs(px - cx) - (hw - radius);
    const float qy = std::abs(py - cy) - (hh - radius);
    const float ax = std::max(qx, 0.0F);
    const float ay = std::max(qy, 0.0F);
    return std::sqrt(ax * ax + ay * ay) +
           std::min(std::max(qx, qy), 0.0F) - radius;
}

// 8×8 超采样覆盖率（0..1）。
[[nodiscard]] inline float sampleCoverage(int px, int py, float cx, float cy,
                                          float hw, float hh, float radius) {
    constexpr int kSamples = 8;
    int inside = 0;
    for (int sy = 0; sy < kSamples; ++sy) {
        for (int sx = 0; sx < kSamples; ++sx) {
            const float x = static_cast<float>(px) +
                            (static_cast<float>(sx) + 0.5F) / kSamples;
            const float y = static_cast<float>(py) +
                            (static_cast<float>(sy) + 0.5F) / kSamples;
            if (roundedRectSdf(x, y, cx, cy, hw, hh, radius) < 0.0F) {
                ++inside;
            }
        }
    }
    return static_cast<float>(inside) / (kSamples * kSamples);
}

struct Rgb {
    double r{0.0};
    double g{0.0};
    double b{0.0};
};

[[nodiscard]] inline Rgb lerp(const Rgb& from, const Rgb& to, double t) {
    return {from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t,
            from.b + (to.b - from.b) * t};
}

// --- Core Dark token（与 design/gallery-icon.html 01 同源） ---
constexpr Rgb kBadgeGradientTop{0x30 / 255.0, 0x30 / 255.0, 0x3a / 255.0};
constexpr Rgb kBadgeGradientMid{0x24 / 255.0, 0x24 / 255.0, 0x2f / 255.0};
constexpr Rgb kBadgeGradientBottom{0x1d / 255.0, 0x1d / 255.0, 0x26 / 255.0};
constexpr Rgb kBadgeFlat{0x26 / 255.0, 0x26 / 255.0, 0x30 / 255.0};
constexpr Rgb kBarVertical{0xf1 / 255.0, 0xf1 / 255.0, 0xf4 / 255.0};
constexpr Rgb kBarHorizontal{0x56 / 255.0, 0x8c / 255.0, 0xf0 / 255.0};

}  // namespace gallery_icon_detail

// 光栅化 Core Dark 图标母版。任意正尺寸可用（运行时按 DPI 光栅化大尺寸
// 母版，导出走 kGalleryIconExportSizes）；size<=0 返回空位图（消费方
// RunOptions.windowIcon 以 width<=0 语义跳过，不致误设）。
[[nodiscard]] inline GalleryIconBitmap renderGalleryIcon(int size) {
    using namespace gallery_icon_detail;
    GalleryIconBitmap bitmap;
    if (size <= 0) {
        return bitmap;
    }
    bitmap.size = size;
    bitmap.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4U, 0U);

    const GalleryIconGeometry geometry = galleryIconGeometry(size);
    const float badgeRadius = 0.225F * static_cast<float>(size);
    const float center = static_cast<float>(size) * 0.5F;
    const float margin = static_cast<float>(geometry.margin);
    const float box = static_cast<float>(geometry.box);
    const float stroke = static_cast<float>(geometry.stroke);
    // 胶囊端笔画：圆角 = 半笔宽。
    const float barRadius = stroke * 0.5F;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float badgeCoverage =
                sampleCoverage(x, y, center, center, center, center, badgeRadius);
            if (badgeCoverage <= 0.0F) {
                continue;  // 圆角外保持透明。
            }
            // 徽章底色：平面档（≤32）单色；渐变档 0 → 0.6 → 1 双段。
            Rgb color = geometry.flatBadge
                            ? kBadgeFlat
                            : lerp(kBadgeGradientTop, kBadgeGradientMid,
                                   std::min(static_cast<double>(y + 1) / size / 0.6, 1.0));
            if (!geometry.flatBadge && static_cast<double>(y + 1) / size > 0.6) {
                color = lerp(kBadgeGradientMid, kBadgeGradientBottom,
                             (static_cast<double>(y + 1) / size - 0.6) / 0.4);
            }
            // 内缘环（渐变档）：整圈白 5%，上半再加顶部高光至 9%
            //（design/gallery-icon.html 01 的 inset 阴影组合）。
            if (!geometry.flatBadge) {
                const float innerCoverage =
                    sampleCoverage(x, y, center, center, center - 1.0F,
                                   center - 1.0F, badgeRadius - 1.0F);
                const float ring = std::max(0.0F, badgeCoverage - innerCoverage);
                if (ring > 0.0F) {
                    const double alpha = y < size / 2 ? 0.09 : 0.05;
                    color = lerp(color, Rgb{1.0, 1.0, 1.0}, alpha * ring);
                }
            }
            // 双色 L：横笔 Row 在下（先画）、竖笔 Column 覆盖交角。
            const float horizontalCoverage =
                sampleCoverage(x, y, margin + box * 0.5F, margin + box - barRadius,
                               box * 0.5F, barRadius, barRadius);
            color = lerp(color, kBarHorizontal, horizontalCoverage);
            const float verticalCoverage =
                sampleCoverage(x, y, margin + barRadius, margin + box * 0.5F,
                               barRadius, box * 0.5F, barRadius);
            color = lerp(color, kBarVertical, verticalCoverage);

            std::uint8_t* pixel =
                bitmap.rgba.data() +
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                 static_cast<std::size_t>(x)) * 4U;
            pixel[0] = static_cast<std::uint8_t>(
                std::lround(std::clamp(color.r, 0.0, 1.0) * 255.0));
            pixel[1] = static_cast<std::uint8_t>(
                std::lround(std::clamp(color.g, 0.0, 1.0) * 255.0));
            pixel[2] = static_cast<std::uint8_t>(
                std::lround(std::clamp(color.b, 0.0, 1.0) * 255.0));
            pixel[3] = static_cast<std::uint8_t>(
                std::lround(std::clamp(badgeCoverage, 0.0F, 1.0F) * 255.0));
        }
    }
    return bitmap;
}

// Asset encoders live in gallery_icon.cpp so this header stays focused on the
// design master and remains cheap to include from the runtime application.
[[nodiscard]] std::vector<std::uint8_t> encodeGalleryIconPng(
    const GalleryIconBitmap& image);
[[nodiscard]] std::vector<std::uint8_t> encodeGalleryIconIco(
    const std::vector<GalleryIconBitmap>& images);
[[nodiscard]] std::vector<std::uint8_t> encodeGalleryIconIcns(
    const std::vector<GalleryIconBitmap>& images);

}  // namespace lumen::examples
