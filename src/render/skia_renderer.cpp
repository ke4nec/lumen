#include "lumen/render/skia_renderer.h"

#include <algorithm>
#include <cmath>

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#endif
#include "include/core/SkColor.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
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

}  // namespace

struct SkiaRenderer::Impl {
    float deviceScale{1.0F};
    core::Color clearColor{core::Color::fromRGBA(24, 24, 27)};
    sk_sp<SkSurface> surface{};
    SkCanvas* canvas{nullptr};
    std::map<ImageId, sk_sp<SkImage>> images{};
    ImageId nextImageId{1};
    // Paint reused across draws within a frame (single-threaded UI loop).
    SkPaint paint{};
};

SkiaRenderer::SkiaRenderer(float deviceScale, core::Color clear)
    : impl_(std::make_unique<Impl>()) {
    impl_->deviceScale = deviceScale > 0.0F ? deviceScale : 1.0F;
    impl_->clearColor = clear;
}

SkiaRenderer::~SkiaRenderer() = default;

void SkiaRenderer::setDeviceScale(float scale) {
    if (scale > 0.0F) {
        impl_->deviceScale = scale;
    }
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
    if (impl_->canvas != nullptr) {
        impl_->canvas->clear(toSkColor(impl_->clearColor));
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
    impl_->canvas->clipRect(skRect, SkClipOp::kIntersect, true);
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

void SkiaRenderer::drawText(TextRun run, core::TextStyle style) {
    if (impl_->canvas == nullptr || run.text.empty() || style.color.a == 0) {
        return;
    }
    const float fontSize = style.fontSize > 0.0F ? style.fontSize : 14.0F;
    const float scale = impl_->deviceScale;

    impl_->paint.setStyle(SkPaint::kFill_Style);
    impl_->paint.setAntiAlias(true);
    impl_->paint.setColor(toSkColor(style.color));

    // Use Skia's UTF-8 text path so the optional backend provides real font
    // rasterization and fallback glyphs instead of the CPU placeholder font.
    const SkFontStyle fontStyle =
        style.bold ? SkFontStyle::Bold() : SkFontStyle::Normal();
    sk_sp<SkTypeface> typeface;
#ifdef _WIN32
    if (const sk_sp<SkFontMgr> fontManager = SkFontMgr_New_GDI()) {
        typeface = fontManager->matchFamilyStyle(nullptr, fontStyle);
    }
#endif
    SkFont font(typeface, fontSize * scale);
    impl_->canvas->drawSimpleText(
        run.text.data(), run.text.size(), SkTextEncoding::kUTF8,
        run.origin.x * scale, (run.origin.y + fontSize) * scale, font,
        impl_->paint);
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
    const SkImageInfo info = impl_->surface->imageInfo();
    snapshot_.width = info.width();
    snapshot_.height = info.height();
    snapshot_.rgba.assign(
        static_cast<std::size_t>(info.width()) *
            static_cast<std::size_t>(info.height()) * 4,
        0);
    // Native color type is RGBA_8888 on Windows builds, so readPixels copies
    // straight into the RGBA snapshot.
    impl_->surface->readPixels(
        info, snapshot_.rgba.data(), static_cast<std::size_t>(info.width()) * 4,
        0, 0);
}

}  // namespace lumen::render
