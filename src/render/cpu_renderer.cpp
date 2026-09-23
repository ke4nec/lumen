#include "lumen/render/cpu_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "placeholder_font.h"
#include "pixel_math.h"

namespace lumen::render {
namespace {
// Placeholder glyphs live in detail::placeholder_font so SkiaRenderer
// draws identical glyph geometry (backend consistency).
using detail::decodeCodePoint;
using detail::glyphPixel;

// Repeat a premultiplied RGBA pixel without a checked vector access for every
// channel. Used by full clears, damage clears and fully covered opaque spans.
void fillRgba(std::uint8_t* dst, std::size_t bytes, const std::uint8_t* pixel) {
    if (bytes == 0) return;
    if (pixel[0] == pixel[1] && pixel[0] == pixel[2] && pixel[0] == pixel[3]) {
        std::memset(dst, pixel[0], bytes);
        return;
    }
    std::memcpy(dst, pixel, 4);
    for (std::size_t filled = 4; filled < bytes;) {
        const auto count = std::min(filled, bytes - filled);
        std::memcpy(dst + filled, dst, count);
        filled += count;
    }
}

}  // namespace

CpuRenderer::CpuRenderer(float deviceScale, core::Color clear)
    : deviceScale_(deviceScale > 0.0F ? deviceScale : 1.0F),
      clearColor_(clear) {
    buffer_.alphaMode = front_.alphaMode = AlphaMode::Premultiplied;
}

void CpuRenderer::setDeviceScale(float scale) {
    if (scale > 0.0F && scale != deviceScale_) {
        deviceScale_ = scale;
        // Pixel dimensions change with the scale; the preserved previous
        // frame is stale until the next full frame.
        frontReusable_ = false;
        frameMatchesConfig_ = false;
        backDamage_.reset();
    }
}

void CpuRenderer::setClearColor(core::Color clear) {
    if (clearColor_ != clear) {
        clearColor_ = clear;
        frontReusable_ = false;
        frameMatchesConfig_ = false;
        backDamage_.reset();
    }
}

ImageId CpuRenderer::registerImage(PixelBuffer image) {
    if (!premultiplyRgbaInPlace(image)) {
        return 0;
    }
    while (images_.contains(nextImageId_)) {
        ++nextImageId_;
    }
    const ImageId id = nextImageId_++;
    images_.emplace(id, std::move(image));
    return id;
}

int CpuRenderer::toPixel(float logical) const {
    return static_cast<int>(std::lround(static_cast<double>(logical) *
                                        static_cast<double>(deviceScale_)));
}

void CpuRenderer::beginFrame(core::Size viewport) {
    beginFrame(viewport, FrameMode::Clear);
}

void CpuRenderer::beginFrame(core::Size viewport, FrameMode mode) {
    beginFrame(viewport, mode, core::Rect{});
}

void CpuRenderer::beginFrame(core::Size viewport, FrameMode mode,
                             core::Rect damage) {
    const int width = std::max(1, toPixel(viewport.width));
    const int height = std::max(1, toPixel(viewport.height));
    const std::size_t bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    const bool canPreserve =
        mode == FrameMode::Preserve && hasFront_ && frontReusable_ &&
        front_.width == width && front_.height == height;
    const std::uint8_t clearBytes[] = {
        static_cast<std::uint8_t>(detail::mul255(clearColor_.r, clearColor_.a)),
        static_cast<std::uint8_t>(detail::mul255(clearColor_.g, clearColor_.a)),
        static_cast<std::uint8_t>(detail::mul255(clearColor_.b, clearColor_.a)), clearColor_.a};
    if (canPreserve) {
        // Keep the published front intact. After a partial submit only that
        // frame's damage differs between the two buffers, so synchronize it.
        if (backDamage_ && buffer_.width == width && buffer_.height == height &&
            buffer_.rgba.size() == bytes) {
            const auto& dirty = *backDamage_;
            for (int y = dirty.y0; y < dirty.y1; ++y) {
                const auto offset = (static_cast<std::size_t>(y) * width +
                                     dirty.x0) * 4;
                std::copy_n(front_.rgba.data() + offset,
                            (dirty.x1 - dirty.x0) * 4,
                            buffer_.rgba.data() + offset);
            }
        } else {
            buffer_ = front_;
        }
        buffer_.alphaMode = front_.alphaMode;
        if (damage.size.width > 0.0F && damage.size.height > 0.0F) {
            const auto dirty = pixelRect(damage);
            for (int y = dirty.y0; y < dirty.y1; ++y) {
                const auto offset = (static_cast<std::size_t>(y) * width + dirty.x0) * 4;
                fillRgba(buffer_.rgba.data() + offset,
                         static_cast<std::size_t>(dirty.x1 - dirty.x0) * 4, clearBytes);
            }
        }
    } else {
        buffer_.width = width;
        buffer_.height = height;
        buffer_.alphaMode = clearColor_.a == 255 ? AlphaMode::Opaque : AlphaMode::Premultiplied;
        buffer_.rgba.resize(bytes);
        fillRgba(buffer_.rgba.data(), bytes, clearBytes);
    }
    frameMatchesConfig_ = true;
    backDamage_.reset();
    clip_.clear();
    clip_.push_back(ClipState{ClipRects{0, 0, buffer_.width, buffer_.height},
                              {}});
    roundedActive_ = false;
}

CpuRenderer::ClipRects CpuRenderer::pixelRect(core::Rect rect) const {
    const int x0 = std::clamp(toPixel(rect.left()), 0, buffer_.width);
    const int y0 = std::clamp(toPixel(rect.top()), 0, buffer_.height);
    return {x0, y0, std::clamp(toPixel(rect.right()), x0, buffer_.width),
                    std::clamp(toPixel(rect.bottom()), y0, buffer_.height)};
}

CpuRenderer::ClipRects CpuRenderer::rasterBounds(float left, float top,
                                                float right, float bottom) const {
    if (clip_.empty()) return {};
    const auto& clip = clip_.back().rect;
    return {std::max(clip.x0, static_cast<int>(std::floor(left))),
            std::max(clip.y0, static_cast<int>(std::floor(top))),
            std::min(clip.x1, static_cast<int>(std::ceil(right))),
            std::min(clip.y1, static_cast<int>(std::ceil(bottom)))};
}

void CpuRenderer::fillCoveredSpan(int y, int x0, int x1, core::Color color) {
    if (x0 >= x1 || color.a == 0) return;
    if (color.a != 255) {
        const unsigned r = detail::mul255(color.r, color.a);
        const unsigned g = detail::mul255(color.g, color.a);
        const unsigned b = detail::mul255(color.b, color.a);
        for (int x = x0; x < x1; ++x) compositePixel(x, y, r, g, b, color.a);
        return;
    }
    auto* dst = buffer_.rgba.data() +
        (static_cast<std::size_t>(y) * buffer_.width + x0) * 4;
    const std::uint8_t pixel[]{color.r, color.g, color.b, 255};
    const auto bytes = static_cast<std::size_t>(x1 - x0) * 4;
    fillRgba(dst, bytes, pixel);
}

void CpuRenderer::save() {
    if (!clip_.empty()) {
        clip_.push_back(clip_.back());
    }
}

void CpuRenderer::restore() {
    // The base clip pushed by beginFrame is never popped, so unbalanced
    // restores degrade to full-viewport clipping.
    if (clip_.size() > 1) {
        clip_.pop_back();
        roundedActive_ = !clip_.empty() && !clip_.back().rounded.empty();
    }
}

void CpuRenderer::clipRect(core::Rect rect) {
    if (clip_.empty()) {
        return;
    }
    ClipRects active = clip_.back().rect;
    active.x0 = std::max(active.x0, toPixel(rect.left()));
    active.y0 = std::max(active.y0, toPixel(rect.top()));
    active.x1 = std::min(active.x1, toPixel(rect.right()));
    active.y1 = std::min(active.y1, toPixel(rect.bottom()));
    // Normalize inverted intersections to an empty region.
    if (active.x1 < active.x0) {
        active.x1 = active.x0;
    }
    if (active.y1 < active.y0) {
        active.y1 = active.y0;
    }
    clip_.back().rect = active;
}


void CpuRenderer::blendPixel(int px, int py, core::Color color) {
    if (clip_.empty()) {
        return;
    }
    const ClipRects& active = clip_.back().rect;
    if (px < active.x0 || px >= active.x1 || py < active.y0 || py >= active.y1) {
        return;
    }
    if (px < 0 || py < 0 || px >= buffer_.width || py >= buffer_.height) {
        return;
    }
    if (roundedActive_) {
        const float clipCov = roundedCoverage(px, py);
        if (clipCov <= 0.0F) {
            return;
        }
        if (clipCov < 1.0F) {
            // 圆角边界带：裁剪覆盖率折进 alpha（source-over 语义下与
            // 逐层覆盖率乘子等价；blendCoveragePixel 复合同理）。
            color.a = static_cast<std::uint8_t>(std::lround(
                std::clamp(static_cast<float>(color.a) * clipCov, 0.0F,
                           255.0F)));
            if (color.a == 0) {
                return;
            }
        }
    }
    if (color.a == 255) {
        compositePixel(px, py, color.r, color.g, color.b, 255);
    } else if (color.a != 0) {
        compositePixel(px, py, detail::mul255(color.r, color.a),
                       detail::mul255(color.g, color.a), detail::mul255(color.b, color.a), color.a);
    }
}

void CpuRenderer::blendImagePixel(int px, int py, const std::uint8_t* rgba) {
    if (clip_.empty() || rgba[3] == 0) return;
    const auto& active = clip_.back().rect;
    if (px < active.x0 || px >= active.x1 || py < active.y0 || py >= active.y1 ||
        px < 0 || py < 0 || px >= buffer_.width || py >= buffer_.height) return;
    if (roundedActive_) {
        const float coverage = roundedCoverage(px, py);
        if (coverage <= 0.0F) return;
        if (coverage < 1.0F) {
            // Preserve the existing clip rounding and scale RGB + A together.
            const auto covered = [coverage](std::uint8_t channel) {
                return static_cast<unsigned>(std::lround(channel * coverage));
            };
            compositePixel(px, py, covered(rgba[0]), covered(rgba[1]),
                           covered(rgba[2]), covered(rgba[3]));
            return;
        }
    }
    compositePixel(px, py, rgba[0], rgba[1], rgba[2], rgba[3]);
}

// This scalar operation is used for each covered pixel. Inlining avoids spilling
// seven arguments per sample on MSVC; the integer compositing rules stay shared.
#if defined(_MSC_VER)
__forceinline
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline)) inline
#else
inline
#endif
void CpuRenderer::compositePixel(int px, int py, unsigned r, unsigned g, unsigned b, unsigned a) {
    if (a == 0) return;
    auto* d = buffer_.rgba.data() + (static_cast<std::size_t>(py) * buffer_.width + px) * 4;
    if (a == 255) {
        d[0] = static_cast<std::uint8_t>(r);
        d[1] = static_cast<std::uint8_t>(g);
        d[2] = static_cast<std::uint8_t>(b);
        d[3] = 255;
        return;
    }
    const unsigned inverse = 255U - a;
    d[0] = static_cast<std::uint8_t>(r + detail::mul255(d[0], inverse));
    d[1] = static_cast<std::uint8_t>(g + detail::mul255(d[1], inverse));
    d[2] = static_cast<std::uint8_t>(b + detail::mul255(d[2], inverse));
    d[3] = static_cast<std::uint8_t>(a + detail::mul255(d[3], inverse));
}

namespace {

// 圆角矩形的设备像素空间形状（AA 用；边界为实数，半径已夹取到半边
// 长）。fill/stroke/图标/圆角裁剪共用同一距离场，边角语义跨图元一致。
// 类型即 CpuRenderer::ClipShape（成员函数定义处可见私有嵌套类型）。
using DeviceShape = CpuRenderer::ClipShape;

// 设备空间形状：逻辑 rect/radius × deviceScale（无 toPixel 舍入——AA
// 覆盖率需要亚像素精度的边界）。
DeviceShape deviceShapeFor(const core::Rect& rect,
                           const core::CornerRadius& radius, float scale) {
    const float w = rect.size.width * scale;
    const float h = rect.size.height * scale;
    const float minSide = std::min(w, h) * 0.5F;
    DeviceShape shape;
    shape.x0 = rect.origin.x * scale;
    shape.y0 = rect.origin.y * scale;
    shape.x1 = shape.x0 + w;
    shape.y1 = shape.y0 + h;
    shape.rTL = std::clamp(radius.topLeft * scale, 0.0F, minSide);
    shape.rTR = std::clamp(radius.topRight * scale, 0.0F, minSide);
    shape.rBL = std::clamp(radius.bottomLeft * scale, 0.0F, minSide);
    shape.rBR = std::clamp(radius.bottomRight * scale, 0.0F, minSide);
    return shape;
}

// 形状整体内缩 inset（半径同步内缩、夹取 ≥0；退化为空时 bounds 反转，
// 调用方用 width/height ≤0 判定）。
DeviceShape insetShape(const DeviceShape& shape, float inset) {
    DeviceShape out;
    out.x0 = shape.x0 + inset;
    out.y0 = shape.y0 + inset;
    out.x1 = shape.x1 - inset;
    out.y1 = shape.y1 - inset;
    out.rTL = std::max(0.0F, shape.rTL - inset);
    out.rTR = std::max(0.0F, shape.rTR - inset);
    out.rBL = std::max(0.0F, shape.rBL - inset);
    out.rBR = std::max(0.0F, shape.rBR - inset);
    return out;
}

// 圆角矩形有符号距离（设备像素；边界 0，外正内负）。四角半径按点所在
// 象限取（标准 rounded-box SDF 的逐角变体）。
float sdRoundedRect(const DeviceShape& s, float x, float y) {
    const float halfW = (s.x1 - s.x0) * 0.5F;
    const float halfH = (s.y1 - s.y0) * 0.5F;
    if (halfW <= 0.0F || halfH <= 0.0F) {
        // 退化形状：取中心点距离为正（完全在外）。
        const float dx = std::max(s.x0 - x, x - s.x1);
        const float dy = std::max(s.y0 - y, y - s.y1);
        return std::max(dx, dy) + 1.0F;
    }
    const float px = x - (s.x0 + halfW);
    const float py = y - (s.y0 + halfH);
    const float r = px < 0.0F ? (py < 0.0F ? s.rTL : s.rBL)
                              : (py < 0.0F ? s.rTR : s.rBR);
    const float qx = std::abs(px) - halfW + r;
    const float qy = std::abs(py) - halfH + r;
    const float ax = std::max(qx, 0.0F);
    const float ay = std::max(qy, 0.0F);
    return std::min(std::max(qx, qy), 0.0F) + std::sqrt(ax * ax + ay * ay) -
           r;
}

// Conservative fully covered span for one scanline. Corner bands still use
// the original SDF; the solid interior needs no per-pixel geometry evaluation.
std::pair<int, int> solidSpan(const DeviceShape& inset, float y) {
    if (inset.x1 - inset.x0 <= 0.0F || inset.y1 - inset.y0 <= 0.0F) {
        return {0, 0};
    }
    if (y < inset.y0 || y >= inset.y1) {
        return {0, 0};
    }
    const float leftRadius = y < inset.y0 + inset.rTL ? inset.rTL :
        (y >= inset.y1 - inset.rBL ? inset.rBL : 0.0F);
    const float rightRadius = y < inset.y0 + inset.rTR ? inset.rTR :
        (y >= inset.y1 - inset.rBR ? inset.rBR : 0.0F);
    return {static_cast<int>(std::ceil(inset.x0 + leftRadius - 0.5F)),
            static_cast<int>(std::ceil(inset.x1 - rightRadius - 0.5F))};
}

}  // namespace

void CpuRenderer::fillSpan(int y, int x0, int x1, core::Color color) {
    // titlebar-design §16: preserve the original coverage at all clip edges.
    // Intersect conservative interior spans once per scanline instead of
    // testing every background pixel against every rounded clip shape.
    if (x0 >= x1 || color.a == 0) return;
    if (!roundedActive_) {
        fillCoveredSpan(y, x0, x1, color);
        return;
    }
    int covered0 = x0;
    int covered1 = x1;
    const float cy = static_cast<float>(y) + 0.5F;
    for (const ClipShape& shape : clip_.back().rounded) {
        const auto span = solidSpan(insetShape(shape, 1.0F), cy);
        covered0 = std::max(covered0, span.first);
        covered1 = std::min(covered1, span.second);
        if (covered0 >= covered1) {
            for (int x = x0; x < x1; ++x) blendPixel(x, y, color);
            return;
        }
    }
    for (int x = x0; x < covered0; ++x) blendPixel(x, y, color);
    fillCoveredSpan(y, covered0, covered1, color);
    for (int x = covered1; x < x1; ++x) blendPixel(x, y, color);
}

void CpuRenderer::fillLogicalRect(const core::Rect& rect, core::Color color,
                                  const core::CornerRadius& radius) {
    if (color.a == 0 || rect.size.width <= 0.0F || rect.size.height <= 0.0F) {
        return;
    }
    // AA 光栅：像素覆盖率来自圆角矩形 SDF（1px 边界带内插值，带外整
    // 填充/跳过）。大面积背景只有边界带付出距离场成本。
    const DeviceShape shape =
        deviceShapeFor(rect, radius, deviceScale_);
    const DeviceShape fast = insetShape(shape, 1.0F);
    const auto [x0, y0, x1, y1] = rasterBounds(
        shape.x0 - 0.5F, shape.y0 - 0.5F, shape.x1 + 0.5F, shape.y1 + 0.5F);
    if (x0 >= x1 || y0 >= y1) return;

    for (int py = y0; py < y1; ++py) {
        const float cy = static_cast<float>(py) + 0.5F;
        const auto span = solidSpan(fast, cy);
        const int solid0 = std::clamp(span.first, x0, x1);
        const int solid1 = std::clamp(span.second, x0, x1);
        fillSpan(py, solid0, solid1, color);
        for (int px = x0; px < x1; ++px) {
            if (px == solid0 && solid1 > solid0) {
                px = solid1;
                if (px >= x1) break;
            }
            const float cx = static_cast<float>(px) + 0.5F;
            const float d = sdRoundedRect(shape, cx, cy);
            if (d <= -0.5F) {
                blendPixel(px, py, color);
            } else if (d < 0.5F) {
                // 边界带：覆盖率 = 1 - (d + 0.5)，与几何覆盖近似一致。
                const float coverage = 0.5F - d;
                blendCoveragePixel(
                    px, py, color,
                    static_cast<std::uint8_t>(std::lround(
                        std::clamp(coverage, 0.0F, 1.0F) * 255.0F)));
            }
        }
    }
}

void CpuRenderer::drawRect(core::Rect rect, core::Color color,
                           core::CornerRadius radius) {
    fillLogicalRect(rect, color, radius);
}

void CpuRenderer::drawRectStroke(core::Rect rect, core::Color color,
                                 core::CornerRadius radius, float width) {
    if (color.a == 0 || rect.size.width <= 0.0F || rect.size.height <= 0.0F ||
        width <= 0.0F) {
        return;
    }
    // AA 描边：环带覆盖率 = 外形覆盖 − 内形覆盖（同一 SDF，两端夹取后
    // 饱和相减；内缩退化为空时整个外矩形都是环带）。
    const float scale = deviceScale_;
    const DeviceShape outer = deviceShapeFor(rect, radius, scale);
    const DeviceShape inner = insetShape(outer, width * scale);
    const auto [x0, y0, x1, y1] = rasterBounds(
        outer.x0 - 0.5F, outer.y0 - 0.5F, outer.x1 + 0.5F, outer.y1 + 0.5F);
    if (x0 >= x1 || y0 >= y1) return;
    const bool hasInner =
        inner.x1 - inner.x0 > 0.0F && inner.y1 - inner.y0 > 0.0F;
    const DeviceShape emptyInterior = insetShape(inner, 1.0F);

    for (int py = y0; py < y1; ++py) {
        const float cy = static_cast<float>(py) + 0.5F;
        const auto span = solidSpan(emptyInterior, cy);
        const int skip0 = std::clamp(span.first, x0, x1);
        const int skip1 = std::clamp(span.second, x0, x1);
        for (int px = x0; px < x1; ++px) {
            if (px == skip0 && skip1 > skip0) {
                px = skip1;
                if (px >= x1) break;
            }
            const float cx = static_cast<float>(px) + 0.5F;
            const float dOuter = sdRoundedRect(outer, cx, cy);
            if (dOuter >= 0.5F) {
                continue;
            }
            const float covOuter =
                dOuter <= -0.5F ? 1.0F : 0.5F - dOuter;
            float covInner = 0.0F;
            if (hasInner) {
                const float dInner = sdRoundedRect(inner, cx, cy);
                covInner = dInner <= -0.5F
                               ? 1.0F
                               : (dInner >= 0.5F ? 0.0F : 0.5F - dInner);
            }
            const float coverage = std::clamp(covOuter - covInner, 0.0F, 1.0F);
            if (coverage <= 0.0F) {
                continue;
            }
            if (coverage >= 1.0F) {
                blendPixel(px, py, color);
            } else {
                blendCoveragePixel(
                    px, py, color,
                    static_cast<std::uint8_t>(
                        std::lround(coverage * 255.0F)));
            }
        }
    }
}

void CpuRenderer::blendCoveragePixel(int px, int py, core::Color color,
                                       std::uint8_t coverage) {
    if (coverage == 0 || color.a == 0) {
        return;
    }
    if (coverage == 255) {
        blendPixel(px, py, color);
        return;
    }
    color.a = static_cast<std::uint8_t>(detail::mul255(color.a, coverage));
    blendPixel(px, py, color);
}

float CpuRenderer::roundedCoverage(int px, int py) const {
    if (clip_.empty() || clip_.back().rounded.empty()) {
        return 1.0F;
    }
    const auto& state = clip_.back();
    const auto& interior = state.roundedInterior;
    if (px >= interior.x0 && px < interior.x1 &&
        py >= interior.y0 && py < interior.y1) {
        return 1.0F;
    }
    const float cx = static_cast<float>(px) + 0.5F;
    const float cy = static_cast<float>(py) + 0.5F;
    float coverage = 1.0F;
    for (const ClipShape& shape : state.rounded) {
        const float d = sdRoundedRect(shape, cx, cy);
        if (d >= 0.5F) {
            return 0.0F;
        }
        if (d > -0.5F) {
            // 与 fillLogicalRect 边界带同口径（1px 带内线性）。
            coverage = std::min(coverage, 0.5F - d);
        }
    }
    return std::clamp(coverage, 0.0F, 1.0F);
}

void CpuRenderer::clipRounded(core::Rect rect, core::CornerRadius radius) {
    // 圆角裁剪：栈顶追加分角形状（save 拷贝栈顶语义下嵌套自然求交），
    // 矩形部分取外接盒与现有 scissor 求交；像素写入按 SDF 覆盖率乘子
    // 门控（roundedCoverage）。
    if (clip_.empty() || rect.size.width <= 0.0F ||
        rect.size.height <= 0.0F) {
        return;
    }
    ClipShape shape = deviceShapeFor(rect, radius, deviceScale_);
    ClipRects active = clip_.back().rect;
    active.x0 = std::max(
        active.x0, static_cast<int>(std::floor(shape.x0 - 0.5F)));
    active.y0 = std::max(
        active.y0, static_cast<int>(std::floor(shape.y0 - 0.5F)));
    active.x1 = std::min(
        active.x1, static_cast<int>(std::ceil(shape.x1 + 0.5F)));
    active.y1 = std::min(
        active.y1, static_cast<int>(std::ceil(shape.y1 + 0.5F)));
    if (active.x1 < active.x0) active.x1 = active.x0;
    if (active.y1 < active.y0) active.y1 = active.y0;
    clip_.back().rect = active;
    const bool anyRadius = shape.rTL > 0.0F || shape.rTR > 0.0F ||
                           shape.rBL > 0.0F || shape.rBR > 0.0F;
    if (anyRadius) {
        // Glyphs/images/icons use individual samples rather than fillSpan.
        // Cache a conservative central rectangle so those interior samples
        // also avoid re-evaluating every clip's SDF. Keep a full pixel inset
        // beyond the corner radii; fractional edge coverage stays scalar.
        ClipRects interior{
            static_cast<int>(std::ceil(shape.x0 + std::max(shape.rTL, shape.rBL) + 0.5F)),
            static_cast<int>(std::ceil(shape.y0 + std::max(shape.rTL, shape.rTR) + 0.5F)),
            static_cast<int>(std::ceil(shape.x1 - std::max(shape.rTR, shape.rBR) - 1.5F)),
            static_cast<int>(std::ceil(shape.y1 - std::max(shape.rBL, shape.rBR) - 1.5F))};
        if (!clip_.back().rounded.empty()) {
            const auto& previous = clip_.back().roundedInterior;
            interior.x0 = std::max(interior.x0, previous.x0);
            interior.y0 = std::max(interior.y0, previous.y0);
            interior.x1 = std::min(interior.x1, previous.x1);
            interior.y1 = std::min(interior.y1, previous.y1);
        }
        clip_.back().roundedInterior = interior;
        clip_.back().rounded.push_back(shape);
        roundedActive_ = true;
    }
}

// 系统字体字形：penX/baselineY 为逻辑坐标（advance 定位与 TextLayout
// 同源），位图为 device 像素 coverage。返回 false 表示无位图（空白字
// 形/缺字），调用方回退占位盒或留白。
bool CpuRenderer::drawSystemGlyph(std::uint32_t codePoint,
                                  const std::string& family, float penX,
                                  float baselineY, float fontSize,
                                  core::TextStyle style) {
    text::FontQuery query;
    query.family = !family.empty() ? family : style.family;
    query.weight = static_cast<text::FontWeight>(
        style.bold ? std::max(style.weight, 700) : style.weight);
    query.italic = style.italic;
    query.sizePx = fontSize > 0.0F ? fontSize : 14.0F;
    // 横向子像素定位：pen 落到设备像素 floor，小数部分经位图光栅平移
    // 轮廓（1/4 px 量化）。旧实现逐字形 lround 取整，advance 的亚像素
    // 累积被逐字重置，小字号无 hinting 时字距肉眼可见地忽宽忽窄。
    const float deviceX = penX * deviceScale_;
    const int penDeviceX = static_cast<int>(std::floor(deviceX));
    const float subPixelShift = deviceX - static_cast<float>(penDeviceX);
    text::GlyphBitmap bitmap;
    if (!systemFonts_->bitmapFor(static_cast<char32_t>(codePoint), query,
                                 deviceScale_, &bitmap, subPixelShift)) {
        // 空白字形（空格）或缺字：不绘制（调用方已按 advance 留白）。
        return false;
    }
    const int baselineDeviceY = toPixel(baselineY);
    for (int row = 0; row < bitmap.height; ++row) {
        for (int col = 0; col < bitmap.width; ++col) {
            const std::uint8_t coverage =
                bitmap.coverage[static_cast<std::size_t>(row) *
                                    static_cast<std::size_t>(bitmap.width) +
                                static_cast<std::size_t>(col)];
            if (coverage == 0) {
                continue;
            }
            const int px = penDeviceX + bitmap.bearingX + col;
            const int py = baselineDeviceY - bitmap.bearingTop + row;
            blendCoveragePixel(px, py, style.color, coverage);
            if (style.bold) {
                // 合成粗体：1px 右移涂抹（与占位路径同策略）。
                blendCoveragePixel(px + 1, py, style.color, coverage);
            }
        }
    }
    return true;
}

void CpuRenderer::drawText(TextRun run, core::TextStyle style) {
    if (run.text.empty() || style.color.a == 0) {
        return;
    }
    const float fontSize = style.fontSize > 0.0F ? style.fontSize : 14.0F;
    const float glyphScale = fontSize / 14.0F;
    const float lineHeight = fontSize * 1.2F;

    // 系统字体路径：shaped advance 定位 + 真实基线（TextLayout 已用同
    // 一管理器算出 baselinePx），字形逐码点光栅。失败逐字形回退占位。
    if (systemFonts_ != nullptr) {
        const float baseline =
            run.baselinePx > 0.0F ? run.origin.y + run.baselinePx
                                  : run.origin.y + fontSize * 0.8F;
        if (!run.shapedRuns.empty()) {
            for (const TextGlyphRun& glyphRun : run.shapedRuns) {
                for (const text::ShapedGlyph& glyph : glyphRun.glyphs) {
                    if (glyph.glyphId == 0) {
                        continue;
                    }
                    const float penX = run.origin.x + glyph.xOffsetPx;
                    if (glyphRun.placeholder) {
                        drawPlaceholderGlyph(glyph.glyphId, penX,
                                             run.origin.y, lineHeight,
                                             glyphScale, glyph.advancePx,
                                             style);
                        continue;
                    }
                    if (!drawSystemGlyph(glyph.glyphId, glyphRun.family,
                                         penX, baseline, fontSize, style)) {
                        drawPlaceholderGlyph(glyph.glyphId, penX,
                                             run.origin.y, lineHeight,
                                             glyphScale, glyph.advancePx,
                                             style);
                    }
                }
            }
            return;
        }
        const float advance = fontSize * 0.6F;
        std::size_t glyphIndex = 0;
        for (std::size_t i = 0; i < run.text.size();) {
            const std::uint32_t codePoint = decodeCodePoint(run.text, i);
            const float penX =
                run.origin.x + advance * static_cast<float>(glyphIndex);
            if (!drawSystemGlyph(codePoint, style.family, penX, baseline,
                                 fontSize, style)) {
                drawPlaceholderGlyph(codePoint, penX, run.origin.y,
                                     lineHeight, glyphScale, advance, style);
            }
            ++glyphIndex;
        }
        return;
    }

    // M1：占位 shaped 数据（glyphId = 码点）按布局 xOffsetPx 定位，
    // 字形位置/advance 与 TextLayout 完全一致（letterSpacing、多码点
    // cluster 不再漂移）。Skia 度量的 shaped 数据被 CPU 回退消费时走
    // 下方旧逐码点路径（占位度量的既有降级）。
    bool allPlaceholder = !run.shapedRuns.empty();
    for (const TextGlyphRun& glyphRun : run.shapedRuns) {
        if (!glyphRun.placeholder) {
            allPlaceholder = false;
            break;
        }
    }
    if (allPlaceholder) {
        for (const TextGlyphRun& glyphRun : run.shapedRuns) {
            for (const text::ShapedGlyph& glyph : glyphRun.glyphs) {
                drawPlaceholderGlyph(glyph.glyphId,
                                     run.origin.x + glyph.xOffsetPx,
                                     run.origin.y, lineHeight, glyphScale,
                                     glyph.advancePx, style);
            }
        }
        return;
    }

    const float advance = fontSize * 0.6F;
    std::size_t glyphIndex = 0;
    for (std::size_t i = 0; i < run.text.size();) {
        const std::uint32_t codePoint = decodeCodePoint(run.text, i);
        const float glyphX = run.origin.x + advance *
                          static_cast<float>(glyphIndex);
        drawPlaceholderGlyph(codePoint, glyphX, run.origin.y, lineHeight,
                             glyphScale, advance, style);
        ++glyphIndex;
    }
}

void CpuRenderer::drawPlaceholderGlyph(std::uint32_t codePoint, float glyphX,
                                       float topY, float lineHeight,
                                       float glyphScale, float advance,
                                       core::TextStyle style) {
    const float inv = 1.0F / deviceScale_;
    const int x0 = std::max(0, toPixel(glyphX));
    const int y0 = std::max(0, toPixel(topY));
    const int x1 = std::min(buffer_.width, toPixel(glyphX + advance));
    const int y1 = std::min(buffer_.height, toPixel(topY + lineHeight));
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float lx = (static_cast<float>(px) + 0.5F) * inv;
            const float ly = (static_cast<float>(py) + 0.5F) * inv;
            const float gx = (lx - glyphX) / glyphScale;
            const float gy = (ly - topY) / glyphScale;
            if (!glyphPixel(codePoint, static_cast<int>(gx),
                            static_cast<int>(gy))) {
                continue;
            }
            blendPixel(px, py, style.color);
            if (style.bold) {
                // Cheap bold: 1px rightward smear.
                blendPixel(px + 1, py, style.color);
            }
        }
    }
}

void CpuRenderer::drawIcon(std::vector<std::vector<core::Offset>> polylines,
                           core::Rect box, core::Color color,
                           float strokeWidth) {
    if (color.a == 0 || box.size.width <= 0.0F || box.size.height <= 0.0F) {
        return;
    }
    if (polylines.empty()) {
        return;
    }
    const float scale = deviceScale_;
    // 图标原点设备对齐（字形 toPixel 同口径 lround）：逻辑居中常给出半
    // 像素原点（如 Spin 奇数高半格的 chevron：19px 盒装 14px 图标 → 2.5），
    // 不对齐则描边虚散、上下不对称；尺寸不变。SkiaRenderer::drawIcon 同式
    // 镜像，保持双后端一致。
    box.origin.x =
        static_cast<float>(std::lround(box.origin.x * scale) / scale);
    box.origin.y =
        static_cast<float>(std::lround(box.origin.y * scale) / scale);
    // 归一化 → 设备像素；线宽以设备像素计（至少覆盖 1px）。
    const float halfWidth =
        std::max(0.5F, strokeWidth * scale * 0.5F);
    const auto toDevice = [&](const core::Offset& point) {
        return std::pair<float, float>{
            (box.origin.x + point.x * box.size.width) * scale,
            (box.origin.y + point.y * box.size.height) * scale};
    };
    // 折线整体包围盒（含 AA 边界 halfWidth + 1px）。图标尺寸 16–24px，
    // 覆盖率缓冲极小。
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const auto& polyline : polylines) {
        for (const auto& point : polyline) {
            const auto [x, y] = toDevice(point);
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    // Empty paths must not convert the sentinel bounds to integer pixels.
    if (minX > maxX || minY > maxY) return;
    const auto [x0, y0, x1, y1] = rasterBounds(
        minX - halfWidth - 1.0F, minY - halfWidth - 1.0F,
        maxX + halfWidth + 1.0F, maxY + halfWidth + 1.0F);
    const int width = x1 - x0;
    const int height = y1 - y0;
    if (width <= 0 || height <= 0) {
        return;
    }
    // AA 线条：逐像素取到线段的有符号距离（端点距离自然形成圆帽，与
    // Skia Round_Cap/Join 一致）；多段共存的像素取最大覆盖率（避免折点
    // 多次叠加加深），一次混合。
    std::vector<float> coverage(static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height));
    const auto strokeSegment = [&](float sx0, float sy0, float sx1,
                                   float sy1) {
        const float dx = sx1 - sx0;
        const float dy = sy1 - sy0;
        const float lengthSq = dx * dx + dy * dy;
        const int segX0 = std::max(x0, static_cast<int>(std::floor(
                                           std::min(sx0, sx1) - halfWidth -
                                           1.0F)));
        const int segY0 = std::max(y0, static_cast<int>(std::floor(
                                           std::min(sy0, sy1) - halfWidth -
                                           1.0F)));
        const int segX1 = std::min(x1, static_cast<int>(std::ceil(
                                           std::max(sx0, sx1) + halfWidth +
                                           1.0F)));
        const int segY1 = std::min(y1, static_cast<int>(std::ceil(
                                           std::max(sy0, sy1) + halfWidth +
                                           1.0F)));
        for (int py = segY0; py < segY1; ++py) {
            for (int px = segX0; px < segX1; ++px) {
                const float cx = static_cast<float>(px) + 0.5F;
                const float cy = static_cast<float>(py) + 0.5F;
                float t = 0.0F;
                if (lengthSq > 0.0F) {
                    t = std::clamp(((cx - sx0) * dx + (cy - sy0) * dy) /
                                       lengthSq,
                                   0.0F, 1.0F);
                }
                const float ex = sx0 + t * dx - cx;
                const float ey = sy0 + t * dy - cy;
                const float distance = std::sqrt(ex * ex + ey * ey);
                const float cov = halfWidth + 0.5F - distance;
                if (cov <= 0.0F) {
                    continue;
                }
                float& slot =
                    coverage[static_cast<std::size_t>(py - y0) *
                                 static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(px - x0)];
                slot = std::max(slot, std::min(cov, 1.0F));
            }
        }
    };
    for (const auto& polyline : polylines) {
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            const auto [ax, ay] = toDevice(polyline[i - 1]);
            const auto [bx, by] = toDevice(polyline[i]);
            strokeSegment(ax, ay, bx, by);
        }
    }
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float cov =
                coverage[static_cast<std::size_t>(py - y0) *
                             static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(px - x0)];
            if (cov <= 0.0F) {
                continue;
            }
            if (cov >= 1.0F) {
                blendPixel(px, py, color);
            } else {
                blendCoveragePixel(
                    px, py, color,
                    static_cast<std::uint8_t>(std::lround(cov * 255.0F)));
            }
        }
    }
}

void CpuRenderer::boxBlurPass(std::vector<float>& src,
                              std::vector<float>& dst, int width, int height,
                              int radius, bool horizontal) {
    const float norm = 1.0F / static_cast<float>(2 * radius + 1);
    if (horizontal) {
        for (int y = 0; y < height; ++y) {
            const float* row =
                &src[static_cast<std::size_t>(y) * width];
            float* out = &dst[static_cast<std::size_t>(y) * width];
            float sum = 0.0F;
            for (int i = 0; i <= std::min(radius, width - 1); ++i) {
                sum += row[i];
            }
            for (int x = 0; x < width; ++x) {
                out[x] = sum * norm;
                const int add = x + radius + 1;
                const int remove = x - radius;
                if (add < width) {
                    sum += row[add];
                }
                if (remove >= 0) {
                    sum -= row[remove];
                }
            }
        }
    } else {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0F;
            for (int i = 0; i <= std::min(radius, height - 1); ++i) {
                sum += src[static_cast<std::size_t>(i) * width + x];
            }
            for (int y = 0; y < height; ++y) {
                dst[static_cast<std::size_t>(y) * width + x] = sum * norm;
                const int add = y + radius + 1;
                const int remove = y - radius;
                if (add < height) {
                    sum += src[static_cast<std::size_t>(add) * width + x];
                }
                if (remove >= 0) {
                    sum -= src[static_cast<std::size_t>(remove) * width + x];
                }
            }
        }
    }
}

void CpuRenderer::drawShadow(core::Rect elevatedBox, core::Color color,
                             core::Offset offset, float blur) {
    if (color.a == 0 || elevatedBox.size.width <= 0.0F ||
        elevatedBox.size.height <= 0.0F) {
        return;
    }
    if (blur <= 0.0F) {
        // blur=0 防御路径：token 阴影色的偏移扁平面（保留 M6 降级行为）。
        core::Color flat = color;
        flat.a = static_cast<std::uint8_t>(std::lround(
            static_cast<float>(flat.a) * 0.5F));
        fillLogicalRect(core::Rect{elevatedBox.origin + offset,
                                   elevatedBox.size},
                        flat, {});
        return;
    }
    // 软阴影：偏移矩形 → 设备像素 alpha 掩膜 → 3 次 H+V 可分离 box
    // blur 近似高斯 → 以阴影色逐像素 coverage 混合。σ 与
    // SkiaRenderer::drawShadow 的 kNormal_SkBlurStyle（blur*0.5*scale）
    // 同口径；damage 层早已按 blur*2+1 外扩（core/damage.cpp），命令/
    // 序列化零改动。box 宽 = σ*sqrt(12/3+1) = σ*sqrt(5)（标准 3-pass
    // 高斯近似）；掩膜域扩 3σ+半径，高斯尾之外清零。
    const float scale = deviceScale_;
    const float sigma = blur * 0.5F * scale;
    const int radius = std::max(1, static_cast<int>(std::lround(
        (sigma * 2.2360679F - 1.0F) * 0.5F)));
    const int spread =
        static_cast<int>(std::lround(sigma * 3.0F)) + radius + 2;
    const float rectX = (elevatedBox.origin.x + offset.x) * scale;
    const float rectY = (elevatedBox.origin.y + offset.y) * scale;
    const float rectW = elevatedBox.size.width * scale;
    const float rectH = elevatedBox.size.height * scale;
    const auto [x0, y0, x1, y1] = rasterBounds(
        rectX - static_cast<float>(spread),
        rectY - static_cast<float>(spread),
        rectX + rectW + static_cast<float>(spread),
        rectY + rectH + static_cast<float>(spread));
    const int width = x1 - x0;
    const int height = y1 - y0;
    if (width <= 0 || height <= 0) {
        return;
    }
    shadowMask_.assign(static_cast<std::size_t>(width) *
                           static_cast<std::size_t>(height),
                       0.0F);
    // 矩形 coverage：盒式滤波边界（各轴 min(边距,1) 相乘）。
    for (int py = y0; py < y1; ++py) {
        const float cy = static_cast<float>(py) + 0.5F;
        const float fy = std::clamp(
            std::min(cy - rectY, rectY + rectH - cy), 0.0F, 1.0F);
        if (fy <= 0.0F) {
            continue;
        }
        for (int px = x0; px < x1; ++px) {
            const float cx = static_cast<float>(px) + 0.5F;
            const float fx = std::clamp(
                std::min(cx - rectX, rectX + rectW - cx), 0.0F, 1.0F);
            if (fx <= 0.0F) {
                continue;
            }
            shadowMask_[static_cast<std::size_t>(py - y0) * width +
                        static_cast<std::size_t>(px - x0)] = fx * fy;
        }
    }
    shadowScratch_.assign(shadowMask_.size(), 0.0F);
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurPass(shadowMask_, shadowScratch_, width, height, radius,
                    true);
        boxBlurPass(shadowScratch_, shadowMask_, width, height, radius,
                    false);
    }
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float cov = std::clamp(
                shadowMask_[static_cast<std::size_t>(py - y0) * width +
                            static_cast<std::size_t>(px - x0)],
                0.0F, 1.0F);
            if (cov <= 0.0F) {
                continue;
            }
            if (cov >= 1.0F) {
                blendPixel(px, py, color);
            } else {
                blendCoveragePixel(
                    px, py, color,
                    static_cast<std::uint8_t>(std::lround(cov * 255.0F)));
            }
        }
    }
}

void CpuRenderer::drawImage(ImageId id, core::Rect destination) {
    const auto it = images_.find(id);
    if (it == images_.end()) {
        return;
    }
    const PixelBuffer& image = it->second;
    if (!validatePixelBuffer(image, PixelValidation::Structure) ||
        destination.size.width <= 0.0F || destination.size.height <= 0.0F ||
        clip_.empty()) {
        return;
    }
    const auto& clip = clip_.back().rect;
    const int x0 = std::max(clip.x0, toPixel(destination.left()));
    const int y0 = std::max(clip.y0, toPixel(destination.top()));
    const int x1 = std::min(clip.x1, toPixel(destination.right()));
    const int y1 = std::min(clip.y1, toPixel(destination.bottom()));
    const float inv = 1.0F / deviceScale_;

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float lx = (static_cast<float>(px) + 0.5F) * inv;
            const float ly = (static_cast<float>(py) + 0.5F) * inv;
            if (lx < destination.left() || lx >= destination.right() ||
                ly < destination.top() || ly >= destination.bottom()) {
                continue;
            }
            // Nearest-neighbor sampling into the destination rectangle.
            const float u =
                (lx - destination.left()) / destination.size.width;
            const float v =
                (ly - destination.top()) / destination.size.height;
            const int sx = std::min(
                image.width - 1,
                std::max(0, static_cast<int>(u * image.width)));
            const int sy = std::min(
                image.height - 1,
                std::max(0, static_cast<int>(v * image.height)));
            const std::size_t offset =
                (static_cast<std::size_t>(sy) *
                     static_cast<std::size_t>(image.width) +
                 static_cast<std::size_t>(sx)) *
                4;
            blendImagePixel(px, py, image.rgba.data() + offset);
        }
    }
}

void CpuRenderer::endFrame() {
    // 双缓冲提交：交换 back/front（O(1)）——present/pixels() 读取完成帧，
    // 下一帧绘制进入旧 front（会被 Clear/Preserve 重建）。
    std::swap(buffer_, front_);
    hasFront_ = true;
    frontReusable_ = frameMatchesConfig_;
}

RendererCapabilities CpuRenderer::capabilities() const {
    RendererCapabilities caps;
    caps.backendName = "cpu";
    // Preserve 帧模式支持 damage 范围的局部提交（plan §3.1）。
    caps.partialSubmit = true;
    return caps;
}

void CpuRenderer::submit(const RenderCommandList& commands,
                         const FrameInfo& info) {
    const auto start = std::chrono::steady_clock::now();
    if (info.deviceScale > 0.0F) {
        setDeviceScale(info.deviceScale);
    }

    const bool wantsPartial = info.damage.has_value() && info.preservePrevious;
    bool partial = false;
    std::string fallbackReason{};
    if (wantsPartial) {
        if (hasFront_ && frontReusable_ && front_.width == std::max(1, toPixel(info.viewport.width)) &&
            front_.height == std::max(1, toPixel(info.viewport.height))) {
            partial = true;
        } else {
            // 无法证明上一帧覆盖当前 viewport（首帧/DPI 变化后），按
            // plan §2.3 退回全帧绘制并记录原因。
            fallbackReason = "no-previous-frame";
        }
    }

    const RenderCommandList* effective = &commands;
    RenderCommandList culled{};
    core::Rect damage{};
    if (partial) {
        // AA can cover a pixel whose center lies outside the logical damage.
        // Round outwards before culling, clearing and clipping, so all three
        // operations repaint the same complete device pixels.
        const float left =
            std::floor(info.damage->left() * deviceScale_) / deviceScale_;
        const float top =
            std::floor(info.damage->top() * deviceScale_) / deviceScale_;
        const float right =
            std::ceil(info.damage->right() * deviceScale_) / deviceScale_;
        const float bottom =
            std::ceil(info.damage->bottom() * deviceScale_) / deviceScale_;
        damage = core::Rect::fromXYWH(left, top, right - left, bottom - top);
        culled = cullCommandsOutside(commands, damage);
        effective = &culled;
        beginFrame(info.viewport, FrameMode::Preserve, damage);
        save();
        clipRect(damage);
    } else {
        beginFrame(info.viewport);
    }

    std::uint64_t uploads = 0;
    for (const auto& command : effective->commands()) {
        switch (command.type) {
            case CommandType::Save:
                save();
                break;
            case CommandType::Restore:
                restore();
                break;
            case CommandType::ClipRect:
                clipRect(command.rect);
                break;
            case CommandType::ClipRounded:
                clipRounded(command.rect, command.radius);
                break;
            case CommandType::DrawRect:
                drawRect(command.rect, command.color, command.radius);
                break;
            case CommandType::DrawRectStroke:
                drawRectStroke(command.rect, command.color, command.radius,
                               command.strokeWidth);
                break;
            case CommandType::DrawText:
                drawText(command.textRun, command.textStyle);
                break;
            case CommandType::DrawImage:
                drawImage(command.image, command.rect);
                break;
            // 与基类适配路径（renderer.cpp）同集：图标/阴影命令必须在
            // 原生 submit 中同样执行，否则 CPU 后端静默丢失矢量图标与
            // 层级阴影（paintScene 直绘路径不经过命令分发，掩盖过该缺陷）。
            case CommandType::DrawIcon:
                drawIcon(command.polylines, command.rect, command.color,
                         command.strokeWidth);
                break;
            case CommandType::DrawShadow:
                drawShadow(command.rect, command.color,
                           core::Offset{command.transform.tx,
                                        command.transform.ty},
                           command.strokeWidth);
                break;
            case CommandType::UploadImage:
                // 资源管理器分配的显式 id；覆盖同 id 旧数据（设备重建
                // 后的重新上传走同一命令，plan §3.3）。
                {
                    PixelBuffer image;
                    // Same one-time premultiplication as registerImage; validate before replacing.
                    if (command.image != 0 && premultiplyRgbaInto(image, command.pixels)) {
                        images_.insert_or_assign(command.image, std::move(image));
                        ++uploads;
                    }
                }
                break;
            case CommandType::UnloadImage:
                unregisterImage(command.image);
                break;
        }
    }
    if (partial) {
        restore();
    }
    endFrame();
    if (partial) {
        // The outer clip bounded every write, including the clear. After the
        // swap, only this region of the back buffer is older than the front.
        backDamage_ = pixelRect(damage);
    }

    stats_.framesSubmitted += 1;
    stats_.submitMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start)
            .count();
    stats_.commandCount = commands.size();
    stats_.culledCommands = commands.size() - effective->size();
    stats_.fullFrameFallback = wantsPartial && !partial;
    stats_.fallbackReason = std::move(fallbackReason);
    stats_.uploads += uploads;
}

void CpuRenderer::unregisterImage(ImageId id) { images_.erase(id); }

void CpuRenderer::clearImages() { images_.clear(); }

}  // namespace lumen::render
