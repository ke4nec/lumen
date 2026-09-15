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
        hasPrevious_ = false;
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
        mode == FrameMode::Preserve && hasPrevious_ &&
        previous_.width == buffer_.width && previous_.height == buffer_.height;
    if (canPreserve && damage.size.width <= 0.0F &&
        damage.size.height <= 0.0F) {
        // Pure Preserve: start from the previous frame so untouched pixels
        // survive; the caller scopes the repaint with clipRect.
        buffer_.rgba = previous_.rgba;
    } else {
        buffer_.rgba.assign(bytes, 0);
        for (std::size_t i = 0; i + 3 < buffer_.rgba.size(); i += 4) {
            buffer_.rgba[i] = clearColor_.r;
            buffer_.rgba[i + 1] = clearColor_.g;
            buffer_.rgba[i + 2] = clearColor_.b;
            buffer_.rgba[i + 3] = clearColor_.a;
        }
        if (canPreserve) {
            // Damage-scoped Preserve: keep the previous frame everywhere
            // OUTSIDE the damage rect; inside it the clear color shows until
            // the caller repaints, matching a full repaint exactly even over
            // transparent backgrounds.
            const int dx0 = std::clamp(toPixel(damage.left()), 0, buffer_.width);
            const int dy0 = std::clamp(toPixel(damage.top()), 0, buffer_.height);
            const int dx1 = std::clamp(toPixel(damage.right()), 0, buffer_.width);
            const int dy1 = std::clamp(toPixel(damage.bottom()), 0, buffer_.height);
            const auto copyBand = [&](int y0, int y1, int x0, int x1) {
                for (int y = y0; y < y1; ++y) {
                    const std::size_t dst =
                        static_cast<std::size_t>(y) * buffer_.width * 4;
                    const std::size_t src =
                        static_cast<std::size_t>(y) * previous_.width * 4;
                    std::copy_n(previous_.rgba.begin() + src + x0 * 4,
                                (x1 - x0) * 4, buffer_.rgba.begin() + dst + x0 * 4);
                }
            };
            copyBand(0, dy0, 0, buffer_.width);                 // above
            copyBand(dy1, buffer_.height, 0, buffer_.width);    // below
            copyBand(dy0, dy1, 0, dx0);                         // left
            copyBand(dy0, dy1, dx1, buffer_.width);             // right
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

// 圆角矩形的逻辑空间形状（半径已夹取到半边长）。fill 与 stroke 共用同
// 一包含判定，边缘/拐角语义一致（像素中心采样）。
struct RoundedRectShape {
    core::Rect rect{};
    float rTL{0.0F};
    float rTR{0.0F};
    float rBL{0.0F};
    float rBR{0.0F};
    bool round{false};
};

RoundedRectShape shapeFor(const core::Rect& rect,
                          const core::CornerRadius& radius) {
    const float minSide = std::min(rect.size.width, rect.size.height) * 0.5F;
    RoundedRectShape shape;
    shape.rect = rect;
    shape.rTL = std::clamp(radius.topLeft, 0.0F, minSide);
    shape.rTR = std::clamp(radius.topRight, 0.0F, minSide);
    shape.rBL = std::clamp(radius.bottomLeft, 0.0F, minSide);
    shape.rBR = std::clamp(radius.bottomRight, 0.0F, minSide);
    shape.round = shape.rTL > 0.0F || shape.rTR > 0.0F || shape.rBL > 0.0F ||
                  shape.rBR > 0.0F;
    return shape;
}

bool insideShape(const RoundedRectShape& shape, float lx, float ly) {
    if (lx < shape.rect.left() || lx >= shape.rect.right() ||
        ly < shape.rect.top() || ly >= shape.rect.bottom()) {
        return false;
    }
    if (!shape.round) {
        return true;
    }
    float dx = 0.0F;
    float dy = 0.0F;
    float r = 0.0F;
    if (lx < shape.rect.left() + shape.rTL && ly < shape.rect.top() + shape.rTL) {
        r = shape.rTL;
        dx = lx - (shape.rect.left() + r);
        dy = ly - (shape.rect.top() + r);
    } else if (lx >= shape.rect.right() - shape.rTR &&
               ly < shape.rect.top() + shape.rTR) {
        r = shape.rTR;
        dx = lx - (shape.rect.right() - r);
        dy = ly - (shape.rect.top() + r);
    } else if (lx < shape.rect.left() + shape.rBL &&
               ly >= shape.rect.bottom() - shape.rBL) {
        r = shape.rBL;
        dx = lx - (shape.rect.left() + r);
        dy = ly - (shape.rect.bottom() - r);
    } else if (lx >= shape.rect.right() - shape.rBR &&
               ly >= shape.rect.bottom() - shape.rBR) {
        r = shape.rBR;
        dx = lx - (shape.rect.right() - r);
        dy = ly - (shape.rect.bottom() - r);
    }
    if (r > 0.0F && dx * dx + dy * dy > r * r) {
        return false;
    }
    return true;
}

}  // namespace

void CpuRenderer::fillLogicalRect(const core::Rect& rect, core::Color color,
                                  const core::CornerRadius& radius) {
    if (color.a == 0 || rect.size.width <= 0.0F || rect.size.height <= 0.0F) {
        return;
    }
    const RoundedRectShape shape = shapeFor(rect, radius);

    int x0 = std::max(0, toPixel(rect.left()));
    int y0 = std::max(0, toPixel(rect.top()));
    int x1 = std::min(buffer_.width, toPixel(rect.right()));
    int y1 = std::min(buffer_.height, toPixel(rect.bottom()));
    const float inv = 1.0F / deviceScale_;

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            // Pixel-center sampling in logical space keeps edges and corners
            // consistent with the half-open hit-test geometry.
            const float lx = (static_cast<float>(px) + 0.5F) * inv;
            const float ly = (static_cast<float>(py) + 0.5F) * inv;
            if (insideShape(shape, lx, ly)) {
                blendPixel(px, py, color);
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
    const RoundedRectShape outer = shapeFor(rect, radius);
    // 内缘 = 外形内缩 width（圆角同步内缩；与 Skia stroke 几何一致）。
    // 内缩退化（width ≥ 半边长）时整个外矩形都是环带。
    const float innerW = rect.size.width - 2.0F * width;
    const float innerH = rect.size.height - 2.0F * width;
    bool hasInner = innerW > 0.0F && innerH > 0.0F;
    RoundedRectShape inner{};
    if (hasInner) {
        inner.rect = core::Rect{
            core::Offset{rect.origin.x + width, rect.origin.y + width},
            core::Size{innerW, innerH}};
        core::CornerRadius innerRadius{
            std::max(0.0F, outer.rTL - width),
            std::max(0.0F, outer.rTR - width),
            std::max(0.0F, outer.rBL - width),
            std::max(0.0F, outer.rBR - width)};
        const float innerMinSide = std::min(innerW, innerH) * 0.5F;
        inner.rTL = std::clamp(innerRadius.topLeft, 0.0F, innerMinSide);
        inner.rTR = std::clamp(innerRadius.topRight, 0.0F, innerMinSide);
        inner.rBL = std::clamp(innerRadius.bottomLeft, 0.0F, innerMinSide);
        inner.rBR = std::clamp(innerRadius.bottomRight, 0.0F, innerMinSide);
        inner.round = inner.rTL > 0.0F || inner.rTR > 0.0F ||
                      inner.rBL > 0.0F || inner.rBR > 0.0F;
    }

    int x0 = std::max(0, toPixel(rect.left()));
    int y0 = std::max(0, toPixel(rect.top()));
    int x1 = std::min(buffer_.width, toPixel(rect.right()));
    int y1 = std::min(buffer_.height, toPixel(rect.bottom()));
    const float inv = 1.0F / deviceScale_;

    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float lx = (static_cast<float>(px) + 0.5F) * inv;
            const float ly = (static_cast<float>(py) + 0.5F) * inv;
            if (!insideShape(outer, lx, ly)) {
                continue;
            }
            if (hasInner && insideShape(inner, lx, ly)) {
                continue;
            }
            blendPixel(px, py, color);
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
    const float scale = deviceScale_;
    // 归一化 → 设备像素；线宽以设备像素计（至少 1）。
    const int widthPx = std::max(1, static_cast<int>(std::lround(
                                        strokeWidth * scale)));
    const auto toDevice = [&](const core::Offset& point) {
        return std::pair<float, float>{
            (box.origin.x + point.x * box.size.width) * scale,
            (box.origin.y + point.y * box.size.height) * scale};
    };
    const auto strokeSegment = [&](float x0, float y0, float x1, float y1) {
        // Bresenham-ish 数值步进（亚像素端点）。
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float length = std::max(std::abs(dx), std::abs(dy));
        if (length <= 0.0F) {
            return;
        }
        const int steps = static_cast<int>(std::ceil(length * 2.0F));
        const float stepX = dx / static_cast<float>(steps);
        const float stepY = dy / static_cast<float>(steps);
        float x = x0;
        float y = y0;
        for (int i = 0; i <= steps; ++i) {
            // 粗线：方形笔刷（宽度取半，四邻域 + 自身）。
            const int brush = widthPx / 2;
            const int px = static_cast<int>(std::lround(x));
            const int py = static_cast<int>(std::lround(y));
            for (int oy = -brush; oy <= brush; ++oy) {
                for (int ox = -brush; ox <= brush; ++ox) {
                    blendPixel(px + ox, py + oy, color);
                }
            }
            x += stepX;
            y += stepY;
        }
    };
    for (const auto& polyline : polylines) {
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            const auto [x0, y0] = toDevice(polyline[i - 1]);
            const auto [x1, y1] = toDevice(polyline[i]);
            strokeSegment(x0, y0, x1, y1);
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
    // Snapshot for the next Preserve frame (draw cache, plan 阶段6).
    previous_ = buffer_;
    hasPrevious_ = true;
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
        if (hasPrevious_) {
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
