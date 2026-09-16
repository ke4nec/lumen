#include "lumen/render/cpu_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

#include "placeholder_font.h"

namespace lumen::render {
namespace {
// Placeholder glyphs live in detail::placeholder_font so SkiaRenderer
// draws identical glyph geometry (backend consistency).
using detail::decodeCodePoint;
using detail::glyphPixel;

}  // namespace

CpuRenderer::CpuRenderer(float deviceScale, core::Color clear)
    : deviceScale_(deviceScale > 0.0F ? deviceScale : 1.0F),
      clearColor_(clear) {}

namespace {

bool validPixelBuffer(const PixelBuffer& buffer) {
    if (buffer.width <= 0 || buffer.height <= 0) {
        return false;
    }
    const auto width = static_cast<std::size_t>(buffer.width);
    const auto height = static_cast<std::size_t>(buffer.height);
    if (height > std::numeric_limits<std::size_t>::max() / width) {
        return false;
    }
    const auto pixels = width * height;
    return pixels <= std::numeric_limits<std::size_t>::max() / 4U &&
           buffer.rgba.size() == pixels * 4U;
}

}  // namespace

void CpuRenderer::setDeviceScale(float scale) {
    if (scale > 0.0F && scale != deviceScale_) {
        deviceScale_ = scale;
        // Pixel dimensions change with the scale; the preserved previous
        // frame is stale until the next full frame.
        hasFront_ = false;
    }
}

ImageId CpuRenderer::registerImage(PixelBuffer image) {
    if (!validPixelBuffer(image)) {
        return 0;
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
    buffer_.width = std::max(1, toPixel(viewport.width));
    buffer_.height = std::max(1, toPixel(viewport.height));
    const std::size_t bytes =
        static_cast<std::size_t>(buffer_.width) *
        static_cast<std::size_t>(buffer_.height) * 4;
    const bool canPreserve =
        mode == FrameMode::Preserve && hasFront_ &&
        front_.width == buffer_.width && front_.height == buffer_.height;
    if (canPreserve) {
        // 双缓冲 Preserve（零拷贝）：交换使 buffer_ 携带上一完成帧；
        // 未触碰像素原样存活，调用方用 clipRect 约束重绘范围。
        std::swap(buffer_, front_);
        if (damage.size.width > 0.0F && damage.size.height > 0.0F) {
            // Damage-scoped Preserve：仅把损坏区清成底色（等价全帧重绘
            // 的语义，但不需要整帧拷贝——旧的"四周拷贝"被就地清底取
            // 代）。
            const int dx0 =
                std::clamp(toPixel(damage.left()), 0, buffer_.width);
            const int dy0 =
                std::clamp(toPixel(damage.top()), 0, buffer_.height);
            const int dx1 =
                std::clamp(toPixel(damage.right()), 0, buffer_.width);
            const int dy1 =
                std::clamp(toPixel(damage.bottom()), 0, buffer_.height);
            for (int y = dy0; y < dy1; ++y) {
                std::size_t offset =
                    static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(buffer_.width) * 4;
                for (int x = dx0; x < dx1; ++x) {
                    buffer_.rgba[offset] = clearColor_.r;
                    buffer_.rgba[offset + 1] = clearColor_.g;
                    buffer_.rgba[offset + 2] = clearColor_.b;
                    buffer_.rgba[offset + 3] = clearColor_.a;
                    offset += 4;
                }
            }
        }
    } else {
        buffer_.rgba.assign(bytes, 0);
        for (std::size_t i = 0; i + 3 < buffer_.rgba.size(); i += 4) {
            buffer_.rgba[i] = clearColor_.r;
            buffer_.rgba[i + 1] = clearColor_.g;
            buffer_.rgba[i + 2] = clearColor_.b;
            buffer_.rgba[i + 3] = clearColor_.a;
        }
    }
    clip_.clear();
    clip_.push_back(ClipRects{0, 0, buffer_.width, buffer_.height});
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
    }
}

void CpuRenderer::clipRect(core::Rect rect) {
    if (clip_.empty()) {
        return;
    }
    ClipRects active = clip_.back();
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
    clip_.back() = active;
}

void CpuRenderer::blendPixel(int px, int py, core::Color color) {
    if (clip_.empty()) {
        return;
    }
    const ClipRects& active = clip_.back();
    if (px < active.x0 || px >= active.x1 || py < active.y0 || py >= active.y1) {
        return;
    }
    if (px < 0 || py < 0 || px >= buffer_.width || py >= buffer_.height) {
        return;
    }
    std::uint8_t* d = &buffer_.rgba[(static_cast<std::size_t>(py) *
                                     static_cast<std::size_t>(buffer_.width) +
                                     static_cast<std::size_t>(px)) *
                                    4];
    if (color.a == 255) {
        d[0] = color.r;
        d[1] = color.g;
        d[2] = color.b;
        d[3] = 255;
        return;
    }
    if (color.a == 0) {
        return;
    }
    const unsigned srcA = color.a;
    const unsigned dstA = d[3];
    const unsigned invSrcA = 255U - srcA;
    const unsigned outA = srcA + (dstA * invSrcA + 127U) / 255U;
    if (outA == 0U) {
        return;
    }
    const auto blendChannel = [srcA, dstA, invSrcA, outA](unsigned src,
                                                            unsigned dst) {
        const unsigned value = src * srcA +
                                (dst * dstA * invSrcA + 127U) / 255U;
        return static_cast<std::uint8_t>((value + outA / 2U) / outA);
    };
    d[0] = blendChannel(color.r, d[0]);
    d[1] = blendChannel(color.g, d[1]);
    d[2] = blendChannel(color.b, d[2]);
    d[3] = static_cast<std::uint8_t>(outA);
}

namespace {

// 圆角矩形的设备像素空间形状（AA 用；边界为实数，半径已夹取到半边
// 长）。fill/stroke/图标共用同一距离场，边角语义跨图元一致。
struct DeviceShape {
    float x0{0.0F};
    float y0{0.0F};
    float x1{0.0F};
    float y1{0.0F};
    float rTL{0.0F};
    float rTR{0.0F};
    float rBL{0.0F};
    float rBR{0.0F};
};

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

// 廉价的"整像素在内"判定（快速区填充用）：边界内缩 1px 的盒 + 圆角
// 象限测试，保守但不计算平方根。
bool deepInside(const DeviceShape& inset, float x, float y) {
    if (inset.x1 - inset.x0 <= 0.0F || inset.y1 - inset.y0 <= 0.0F) {
        return false;
    }
    if (x < inset.x0 || x >= inset.x1 || y < inset.y0 || y >= inset.y1) {
        return false;
    }
    float dx = 0.0F;
    float dy = 0.0F;
    float r = 0.0F;
    if (x < inset.x0 + inset.rTL && y < inset.y0 + inset.rTL) {
        r = inset.rTL;
        dx = x - (inset.x0 + r);
        dy = y - (inset.y0 + r);
    } else if (x >= inset.x1 - inset.rTR && y < inset.y0 + inset.rTR) {
        r = inset.rTR;
        dx = x - (inset.x1 - r);
        dy = y - (inset.y0 + r);
    } else if (x < inset.x0 + inset.rBL && y >= inset.y1 - inset.rBL) {
        r = inset.rBL;
        dx = x - (inset.x0 + r);
        dy = y - (inset.y1 - r);
    } else if (x >= inset.x1 - inset.rBR && y >= inset.y1 - inset.rBR) {
        r = inset.rBR;
        dx = x - (inset.x1 - r);
        dy = y - (inset.y1 - r);
    }
    return !(r > 0.0F && dx * dx + dy * dy > r * r);
}

}  // namespace

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
    const int x0 = std::max(0, static_cast<int>(std::floor(shape.x0 - 0.5F)));
    const int y0 = std::max(0, static_cast<int>(std::floor(shape.y0 - 0.5F)));
    const int x1 = std::min(buffer_.width,
                            static_cast<int>(std::ceil(shape.x1 + 0.5F)));
    const int y1 = std::min(buffer_.height,
                            static_cast<int>(std::ceil(shape.y1 + 0.5F)));

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float cx = static_cast<float>(px) + 0.5F;
            const float cy = static_cast<float>(py) + 0.5F;
            if (deepInside(fast, cx, cy)) {
                blendPixel(px, py, color);
                continue;
            }
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
    const int x0 = std::max(0, static_cast<int>(std::floor(outer.x0 - 0.5F)));
    const int y0 = std::max(0, static_cast<int>(std::floor(outer.y0 - 0.5F)));
    const int x1 = std::min(buffer_.width,
                            static_cast<int>(std::ceil(outer.x1 + 0.5F)));
    const int y1 = std::min(buffer_.height,
                            static_cast<int>(std::ceil(outer.y1 + 0.5F)));
    const bool hasInner =
        inner.x1 - inner.x0 > 0.0F && inner.y1 - inner.y0 > 0.0F;

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float cx = static_cast<float>(px) + 0.5F;
            const float cy = static_cast<float>(py) + 0.5F;
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
    color.a = static_cast<std::uint8_t>(
        (static_cast<unsigned>(color.a) * coverage + 127U) / 255U);
    blendPixel(px, py, color);
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
    text::GlyphBitmap bitmap;
    if (!systemFonts_->bitmapFor(static_cast<char32_t>(codePoint), query,
                                 deviceScale_, &bitmap)) {
        // 空白字形（空格）或缺字：不绘制（调用方已按 advance 留白）。
        return false;
    }
    const int penDeviceX = toPixel(penX);
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
    const int x0 = std::max(0, static_cast<int>(std::floor(
                                   minX - halfWidth - 1.0F)));
    const int y0 = std::max(0, static_cast<int>(std::floor(
                                   minY - halfWidth - 1.0F)));
    const int x1 = std::min(buffer_.width,
                            static_cast<int>(std::ceil(
                                maxX + halfWidth + 1.0F)));
    const int y1 = std::min(buffer_.height,
                            static_cast<int>(std::ceil(
                                maxY + halfWidth + 1.0F)));
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

void CpuRenderer::drawShadow(core::Rect elevatedBox, core::Color color,
                             core::Offset offset, float blur) {
    (void)blur;  // CPU 无模糊：扁平面近似。
    if (color.a == 0) {
        return;
    }
    // 降级：token 阴影色的偏移矩形（透明度衰减，视觉近似层级）。
    core::Color flat = color;
    flat.a = static_cast<std::uint8_t>(std::lround(
        static_cast<float>(flat.a) * 0.5F));
    fillLogicalRect(core::Rect{elevatedBox.origin + offset,
                               elevatedBox.size},
                    flat, {});
}

void CpuRenderer::drawImage(ImageId id, core::Rect destination) {
    const auto it = images_.find(id);
    if (it == images_.end()) {
        return;
    }
    const PixelBuffer& image = it->second;
    if (!validPixelBuffer(image) ||
        destination.size.width <= 0.0F || destination.size.height <= 0.0F) {
        return;
    }
    const int x0 = std::max(0, toPixel(destination.left()));
    const int y0 = std::max(0, toPixel(destination.top()));
    const int x1 = std::min(buffer_.width, toPixel(destination.right()));
    const int y1 = std::min(buffer_.height, toPixel(destination.bottom()));
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
            blendPixel(px, py,
                       core::Color::fromRGBA(image.rgba[offset],
                                             image.rgba[offset + 1],
                                             image.rgba[offset + 2],
                                             image.rgba[offset + 3]));
        }
    }
}

void CpuRenderer::endFrame() {
    // 双缓冲提交：交换 back/front（O(1)）——present/pixels() 读取完成帧，
    // 下一帧绘制进入旧 front（会被 Clear/Preserve 重建）。
    std::swap(buffer_, front_);
    hasFront_ = true;
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
        if (hasFront_) {
            partial = true;
        } else {
            // 无法证明上一帧覆盖当前 viewport（首帧/DPI 变化后），按
            // plan §2.3 退回全帧绘制并记录原因。
            fallbackReason = "no-previous-frame";
        }
    }

    const RenderCommandList* effective = &commands;
    RenderCommandList culled{};
    if (partial) {
        culled = cullCommandsOutside(commands, *info.damage);
        effective = &culled;
        beginFrame(info.viewport, FrameMode::Preserve, *info.damage);
        save();
        clipRect(*info.damage);
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
                if (validPixelBuffer(command.pixels)) {
                    images_.erase(command.image);
                    images_.emplace(command.image, command.pixels);
                    ++uploads;
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
