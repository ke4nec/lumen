#include "lumen/render/skia_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>

#include "skia_text.h"

#include "lumen/text/font_manager.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#elif defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#endif
#include "include/core/SkColor.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"

namespace lumen::render {
namespace {

[[nodiscard]] SkColor toSkColor(core::Color color) {
    return SkColorSetARGB(color.a, color.r, color.g, color.b);
}

SkUnichar decodeUtf8(const char* bytes, std::size_t length) {
    if (length == 0) {
        return 0;
    }
    const auto byte = [&](std::size_t i) {
        return static_cast<unsigned char>(bytes[i]);
    };
    const unsigned char first = byte(0);
    if (first < 0x80) {
        return first;
    }
    if (first >= 0xC2 && first <= 0xDF && length >= 2) {
        return ((first & 0x1F) << 6) | (byte(1) & 0x3F);
    }
    if (first >= 0xE0 && first <= 0xEF && length >= 3) {
        return ((first & 0x0F) << 12) | ((byte(1) & 0x3F) << 6) |
               (byte(2) & 0x3F);
    }
    if (first >= 0xF0 && first <= 0xF4 && length >= 4) {
        return ((first & 0x07) << 18) | ((byte(1) & 0x3F) << 12) |
               ((byte(2) & 0x3F) << 6) | (byte(3) & 0x3F);
    }
    return 0xFFFD;
}

std::size_t utf8SequenceLength(const char* bytes, std::size_t length) {
    if (length == 0) {
        return 0;
    }
    const unsigned char first = static_cast<unsigned char>(bytes[0]);
    const std::size_t expected = first < 0x80 ? 1 : first < 0xE0 ? 2
                                    : first < 0xF0              ? 3
                                                                 : 4;
    return std::min(expected, length);
}

}  // namespace

struct SkiaRenderer::Impl {
    float deviceScale{1.0F};
    core::Color clearColor{core::Color::fromRGBA(24, 24, 27)};
    sk_sp<SkSurface> surface{};
    SkCanvas* canvas{nullptr};
    std::map<ImageId, sk_sp<SkImage>> images{};
    ImageId nextImageId{1};
    // Snapshot of the previous frame for Preserve mode (plan 阶段6).
    sk_sp<SkImage> lastFrame{};
    // Paint reused across draws within a frame (single-threaded UI loop).
    SkPaint paint{};
    // M1：平台字体管理器缓存（FontConfig/GDI 初始化昂贵；缺失时保持
    // null 并走回退路径，见 drawText）。
    sk_sp<SkFontMgr> fontMgr{};
    // M7 review：回退逐码点路径的字形解析缓存（manager 本身也成员化
    // ——旧实现每次 drawText 重建 FontConfig + 每码点查询，1080p 文本
    // 密集场景 ~3s/帧）。缓存键包含字体族、字重、斜体和码点，避免
    // 首帧样式污染后续文本。
    sk_sp<SkFontMgr> fallbackMgr{};
    std::map<std::string, sk_sp<SkTypeface>> fallbackBaseCache{};
    std::map<std::string, sk_sp<SkTypeface>> fallbackGlyphs{};
};

SkiaRenderer::SkiaRenderer(float deviceScale, core::Color clear)
    : impl_(std::make_unique<Impl>()) {
    impl_->deviceScale = deviceScale > 0.0F ? deviceScale : 1.0F;
    impl_->clearColor = clear;
}

SkiaRenderer::~SkiaRenderer() = default;

void SkiaRenderer::setDeviceScale(float scale) {
    if (scale > 0.0F && scale != impl_->deviceScale) {
        impl_->deviceScale = scale;
        // Pixel dimensions change with the scale; the preserved snapshot is
        // stale until the next full frame.
        impl_->lastFrame.reset();
    }
}

void SkiaRenderer::unregisterImage(ImageId id) { impl_->images.erase(id); }

void SkiaRenderer::clearImages() { impl_->images.clear(); }

RendererCapabilities SkiaRenderer::capabilities() const {
    // 光栅后端：无 GPU surface，不做 Preserve 局部提交；命令路径经由
    // Renderer 默认适配器回放（阶段7B）。
    RendererCapabilities caps;
    caps.backendName = "skia-raster";
    caps.partialSubmit = false;
    return caps;
}

ImageId SkiaRenderer::registerImage(PixelBuffer image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() !=
            static_cast<std::size_t>(image.width) *
                static_cast<std::size_t>(image.height) * 4) {
        return 0;  // Invalid buffers are rejected; 0 is never a valid id.
    }
    // Straight (non-premultiplied) RGBA matches CpuRenderer's blending
    // contract; Skia premultiplies internally when copying the raster.
    const SkImageInfo info = SkImageInfo::Make(
        image.width, image.height, kRGBA_8888_SkColorType,
        kUnpremul_SkAlphaType);
    const SkPixmap pixmap(info, image.rgba.data(),
                          static_cast<std::size_t>(image.width) * 4);
    const ImageId id = impl_->nextImageId++;
    impl_->images.emplace(id, SkImages::RasterFromPixmapCopy(pixmap));
    return id;
}

void SkiaRenderer::beginFrame(core::Size viewport) {
    beginFrame(viewport, FrameMode::Clear);
}

void SkiaRenderer::beginFrame(core::Size viewport, FrameMode mode) {
    beginFrame(viewport, mode, core::Rect{});
}

void SkiaRenderer::beginFrame(core::Size viewport, FrameMode mode,
                              core::Rect damage) {
    const int width =
        std::max(1, static_cast<int>(std::lround(
                        static_cast<double>(viewport.width) * impl_->deviceScale)));
    const int height =
        std::max(1, static_cast<int>(std::lround(
                        static_cast<double>(viewport.height) * impl_->deviceScale)));
    impl_->surface = SkSurfaces::Raster(
        SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                          kPremul_SkAlphaType));
    impl_->canvas = impl_->surface ? impl_->surface->getCanvas() : nullptr;
    if (impl_->canvas == nullptr) {
        return;
    }
    const bool canPreserve = mode == FrameMode::Preserve && impl_->lastFrame &&
                             impl_->lastFrame->width() == width &&
                             impl_->lastFrame->height() == height;
    const bool scoped = damage.size.width > 0.0F && damage.size.height > 0.0F;
    if (canPreserve && !scoped) {
        // Pure Preserve: replay the previous frame; the caller scopes the
        // repaint with clipRect.
        impl_->canvas->drawImage(impl_->lastFrame, 0.0F, 0.0F,
                                 SkSamplingOptions(SkFilterMode::kNearest,
                                                   SkMipmapMode::kNone));
        return;
    }
    impl_->canvas->clear(toSkColor(impl_->clearColor));
    if (canPreserve) {
        // Damage-scoped Preserve: replay the previous frame everywhere
        // OUTSIDE the damage rect (mirrors CpuRenderer's band copy).
        impl_->canvas->save();
        const SkRect skDamage = SkRect::MakeXYWH(
            damage.left() * impl_->deviceScale,
            damage.top() * impl_->deviceScale,
            damage.size.width * impl_->deviceScale,
            damage.size.height * impl_->deviceScale);
        impl_->canvas->clipRect(skDamage, SkClipOp::kDifference, false);
        impl_->canvas->drawImage(impl_->lastFrame, 0.0F, 0.0F,
                                 SkSamplingOptions(SkFilterMode::kNearest,
                                                   SkMipmapMode::kNone));
        impl_->canvas->restore();
    }
}

void SkiaRenderer::save() {
    if (impl_->canvas != nullptr) {
        impl_->canvas->save();
    }
}

void SkiaRenderer::restore() {
    if (impl_->canvas != nullptr) {
        impl_->canvas->restore();
    }
}

void SkiaRenderer::clipRect(core::Rect rect) {
    if (impl_->canvas == nullptr) {
        return;
    }
    const SkRect skRect = SkRect::MakeXYWH(
        rect.left() * impl_->deviceScale, rect.top() * impl_->deviceScale,
        rect.size.width * impl_->deviceScale,
        rect.size.height * impl_->deviceScale);
    // Hard edges match CpuRenderer clipping (exact partial-vs-full parity).
    impl_->canvas->clipRect(skRect, SkClipOp::kIntersect, false);
}

void SkiaRenderer::drawRect(core::Rect rect, core::Color color,
                            core::CornerRadius radius) {
    if (impl_->canvas == nullptr || color.a == 0) {
        return;
    }
    const float scale = impl_->deviceScale;
    const SkRect skRect = SkRect::MakeXYWH(
        rect.left() * scale, rect.top() * scale, rect.size.width * scale,
        rect.size.height * scale);
    impl_->paint.setStyle(SkPaint::kFill_Style);
    impl_->paint.setAntiAlias(true);
    impl_->paint.setColor(toSkColor(color));
    const float minSide = std::min(rect.size.width, rect.size.height) * 0.5F;
    const float rTL = std::clamp(radius.topLeft, 0.0F, minSide);
    const float rTR = std::clamp(radius.topRight, 0.0F, minSide);
    const float rBR = std::clamp(radius.bottomRight, 0.0F, minSide);
    const float rBL = std::clamp(radius.bottomLeft, 0.0F, minSide);
    if (rTL <= 0.0F && rTR <= 0.0F && rBR <= 0.0F && rBL <= 0.0F) {
        impl_->canvas->drawRect(skRect, impl_->paint);
        return;
    }
    const SkVector radii[4] = {
        SkVector::Make(rTL * scale, rTL * scale),
        SkVector::Make(rTR * scale, rTR * scale),
        SkVector::Make(rBR * scale, rBR * scale),
        SkVector::Make(rBL * scale, rBL * scale),
    };
    SkRRect rounded;
    rounded.setRectRadii(skRect, radii);
    impl_->canvas->drawRRect(rounded, impl_->paint);
}

void SkiaRenderer::drawRectStroke(core::Rect rect, core::Color color,
                                  core::CornerRadius radius, float width) {
    if (impl_->canvas == nullptr || color.a == 0 || width <= 0.0F) {
        return;
    }
    const float scale = impl_->deviceScale;
    // Skia stroke 以路径中心线外扩/内收各 width/2；这里希望环带整体落在
    // 矩形内部（与 CPU 后端的“内缩内缘”一致），故对矩形内缩 width/2。
    const float inset = width * 0.5F;
    const SkRect skRect = SkRect::MakeXYWH(
        (rect.left() + inset) * scale, (rect.top() + inset) * scale,
        std::max(0.0F, rect.size.width - 2.0F * inset) * scale,
        std::max(0.0F, rect.size.height - 2.0F * inset) * scale);
    impl_->paint.setStyle(SkPaint::kStroke_Style);
    impl_->paint.setAntiAlias(true);
    impl_->paint.setColor(toSkColor(color));
    impl_->paint.setStrokeWidth(width * scale);
    // 圆角同步内缩 width/2，使环带外缘贴近原始形状（与 CPU 内缩几何
    // 的中心线一致）。
    const float minSide = std::min(skRect.width(), skRect.height()) * 0.5F;
    const float rTL = std::clamp(radius.topLeft - inset, 0.0F, minSide);
    const float rTR = std::clamp(radius.topRight - inset, 0.0F, minSide);
    const float rBR = std::clamp(radius.bottomRight - inset, 0.0F, minSide);
    const float rBL = std::clamp(radius.bottomLeft - inset, 0.0F, minSide);
    if (rTL <= 0.0F && rTR <= 0.0F && rBR <= 0.0F && rBL <= 0.0F) {
        impl_->canvas->drawRect(skRect, impl_->paint);
        return;
    }
    const SkVector radii[4] = {
        SkVector::Make(rTL * scale, rTL * scale),
        SkVector::Make(rTR * scale, rTR * scale),
        SkVector::Make(rBR * scale, rBR * scale),
        SkVector::Make(rBL * scale, rBL * scale),
    };
    SkRRect rounded;
    rounded.setRectRadii(skRect, radii);
    impl_->canvas->drawRRect(rounded, impl_->paint);
}

void SkiaRenderer::drawText(TextRun run, core::TextStyle style) {
    if (impl_->canvas == nullptr || run.text.empty() || style.color.a == 0) {
        return;
    }
    const float fontSize = style.fontSize > 0.0F ? style.fontSize : 14.0F;
    const float scale = impl_->deviceScale;

    impl_->paint.setStyle(SkPaint::kFill_Style);
    impl_->paint.setAntiAlias(true);
    impl_->paint.setColor(toSkColor(style.color));

    // M1：优先消费布局共享的 shaped 数据（真实 glyph id + 布局 advance/
    // baseline）；存在占位 run（缺字/CPU 数据）时回退旧逐码点路径。
    bool hasRealShaping = false;
    bool hasPlaceholderRun = false;
    for (const TextGlyphRun& glyphRun : run.shapedRuns) {
        if (glyphRun.placeholder) {
            hasPlaceholderRun = true;
        } else if (!glyphRun.glyphs.empty()) {
            hasRealShaping = true;
        }
    }
    if (hasRealShaping && !hasPlaceholderRun) {
        // 字体管理器按 renderer 生命周期缓存（与 GPU 后端一致）。
        if (impl_->fontMgr == nullptr) {
            impl_->fontMgr = skia_text::makePlatformFontMgr();
        }
        if (impl_->fontMgr != nullptr) {
            skia_text::drawShapedText(impl_->canvas, impl_->fontMgr.get(),
                                      run, style, scale);
            return;
        }
        // 平台字体管理器不可用：走下方旧逐码点路径（默认 typeface）。
    }

    // Use Skia's UTF-8 text path so the optional backend provides real font
    // rasterization and fallback glyphs instead of the CPU placeholder font.
    // M7 review：FontConfig/GDI/CoreText manager 与码点字形解析全部缓存于
    // Impl（旧实现每次调用重建 manager —— FontConfig 初始化 ~10ms × 每文本
    // run，加上每码点无缓存查询，文本密集帧可达秒级）。三端统一经
    // makePlatformFontMgr 获取（Windows GDI / Linux FontConfig /
    // macOS CoreText），避免 macOS 回退路径长期走默认字体。
    const int fontWeight = std::clamp(
        style.bold ? std::max(style.weight, 700) : style.weight, 100, 900);
    const SkFontStyle fontStyle(
        fontWeight, SkFontStyle::kNormal_Width,
        style.italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant);
    if (impl_->fallbackMgr == nullptr) {
        impl_->fallbackMgr = skia_text::makePlatformFontMgr();
    }
    sk_sp<SkFontMgr>& fontManager = impl_->fallbackMgr;
    const auto matchDefaultStack = [&](SkUnichar codePoint) -> sk_sp<SkTypeface> {
        // 空 family 回退：先按平台默认栈挑覆盖者，再走系统兜底。
        const char* requested =
            style.family.empty() ? nullptr : style.family.c_str();
        if (style.family.empty()) {
            for (const std::string& candidate :
                 text::defaultFontStackFor(
                     static_cast<char32_t>(codePoint))) {
                sk_sp<SkTypeface> face =
                    fontManager->matchFamilyStyle(candidate.c_str(), fontStyle);
                if (face && face->unicharToGlyph(codePoint) != 0) {
                    return face;
                }
            }
        } else if (requested != nullptr) {
            sk_sp<SkTypeface> face =
                fontManager->matchFamilyStyle(requested, fontStyle);
            if (face && face->unicharToGlyph(codePoint) != 0) {
                return face;
            }
        }
        return fontManager->matchFamilyStyleCharacter(
            requested, fontStyle, nullptr, 0, codePoint);
    };
    sk_sp<SkTypeface> typeface;
    if (fontManager != nullptr) {
        const std::string baseKey =
            style.family + "|" + std::to_string(fontWeight) + "|" +
            (style.italic ? "italic" : "upright");
        const auto baseCached = impl_->fallbackBaseCache.find(baseKey);
        if (baseCached != impl_->fallbackBaseCache.end()) {
            typeface = baseCached->second;
        } else if (style.family.empty()) {
            // 基线字体取拉丁默认栈首个真实命中的族。
            typeface = matchDefaultStack(static_cast<SkUnichar>('A'));
            if (!typeface) {
                typeface = fontManager->matchFamilyStyle(nullptr, fontStyle);
            }
            impl_->fallbackBaseCache.emplace(baseKey, typeface);
        } else {
            typeface = fontManager->matchFamilyStyle(style.family.c_str(),
                                                     fontStyle);
            impl_->fallbackBaseCache.emplace(baseKey, typeface);
        }
    }
    float x = run.origin.x * scale;
    const float baseline = (run.origin.y + fontSize) * scale;
    for (std::size_t offset = 0; offset < run.text.size();) {
        const std::size_t length =
            utf8SequenceLength(run.text.data() + offset, run.text.size() - offset);
        const SkUnichar codePoint = decodeUtf8(run.text.data() + offset, length);
        sk_sp<SkTypeface> glyphTypeface = typeface;
        if (fontManager &&
            (!glyphTypeface ||
             glyphTypeface->unicharToGlyph(codePoint) == 0)) {
            const std::string cacheKey =
                style.family + "|" + std::to_string(fontWeight) + "|" +
                (style.italic ? "italic" : "upright") + "|" +
                std::to_string(codePoint);
            const auto cached = impl_->fallbackGlyphs.find(cacheKey);
            if (cached != impl_->fallbackGlyphs.end()) {
                glyphTypeface = cached->second;
            } else {
                glyphTypeface = matchDefaultStack(codePoint);
                impl_->fallbackGlyphs.emplace(cacheKey, glyphTypeface);
            }
        }
        SkFont font(glyphTypeface, fontSize * scale);
        impl_->canvas->drawSimpleText(run.text.data() + offset, length,
                                      SkTextEncoding::kUTF8, x, baseline, font,
                                      impl_->paint);
        x += font.measureText(run.text.data() + offset, length,
                              SkTextEncoding::kUTF8);
        offset += length;
    }
}

void SkiaRenderer::drawIcon(
    std::vector<std::vector<core::Offset>> polylines, core::Rect box,
    core::Color color, float strokeWidth) {
    if (impl_->canvas == nullptr || polylines.empty() || color.a == 0) {
        return;
    }
    const float scale = impl_->deviceScale;
    SkPath path;
    for (const auto& polyline : polylines) {
        if (polyline.empty()) {
            continue;
        }
        path.moveTo((box.origin.x + polyline.front().x * box.size.width) *
                        scale,
                    (box.origin.y + polyline.front().y * box.size.height) *
                        scale);
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            path.lineTo(
                (box.origin.x + polyline[i].x * box.size.width) * scale,
                (box.origin.y + polyline[i].y * box.size.height) * scale);
        }
    }
    impl_->paint.setStyle(SkPaint::kStroke_Style);
    impl_->paint.setAntiAlias(true);
    impl_->paint.setColor(toSkColor(color));
    impl_->paint.setStrokeWidth(strokeWidth * scale);
    impl_->paint.setStrokeCap(SkPaint::kRound_Cap);
    impl_->paint.setStrokeJoin(SkPaint::kRound_Join);
    impl_->canvas->drawPath(path, impl_->paint);
}

void SkiaRenderer::drawShadow(core::Rect elevatedBox, core::Color color,
                              core::Offset offset, float blur) {
    if (impl_->canvas == nullptr || color.a == 0) {
        return;
    }
    const float scale = impl_->deviceScale;
    const SkRect rect = SkRect::MakeXYWH(
        (elevatedBox.origin.x + offset.x) * scale,
        (elevatedBox.origin.y + offset.y) * scale,
        elevatedBox.size.width * scale, elevatedBox.size.height * scale);
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setAntiAlias(true);
    paint.setColor(toSkColor(color));
    if (blur > 0.0F) {
        paint.setMaskFilter(SkMaskFilter::MakeBlur(
            kNormal_SkBlurStyle, blur * 0.5F * scale));
    }
    impl_->canvas->drawRect(rect, paint);
}

void SkiaRenderer::drawImage(ImageId id, core::Rect destination) {
    const auto it = impl_->images.find(id);
    if (impl_->canvas == nullptr || it == impl_->images.end() ||
        !it->second) {
        return;
    }
    const SkRect dest = SkRect::MakeXYWH(
        destination.left() * impl_->deviceScale,
        destination.top() * impl_->deviceScale,
        destination.size.width * impl_->deviceScale,
        destination.size.height * impl_->deviceScale);
    impl_->paint.setStyle(SkPaint::kFill_Style);
    impl_->paint.setAntiAlias(true);
    // Nearest-neighbor sampling matches CpuRenderer's blit (consistency).
    impl_->canvas->drawImageRect(
        it->second, dest,
        SkSamplingOptions(SkFilterMode::kNearest, SkMipmapMode::kNone),
        &impl_->paint);
}

void SkiaRenderer::endFrame() {
    if (impl_->surface == nullptr) {
        return;
    }
    // Snapshot for Preserve frames (draw cache, plan 阶段6).
    impl_->lastFrame = impl_->surface->makeImageSnapshot();
    const SkImageInfo info = impl_->surface->imageInfo();
    snapshot_.width = info.width();
    snapshot_.height = info.height();
    snapshot_.rgba.assign(
        static_cast<std::size_t>(info.width()) *
            static_cast<std::size_t>(info.height()) * 4,
        0);
    // The raster surface is created as RGBA_8888, so readPixels copies
    // straight into the RGBA snapshot on all platforms.
    impl_->surface->readPixels(
        info, snapshot_.rgba.data(), static_cast<std::size_t>(info.width()) * 4,
        0, 0);
}

}  // namespace lumen::render
