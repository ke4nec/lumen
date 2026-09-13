// Skia Ganesh GPU 后端（v0.2 阶段7C, plan §3.1/7C）。
//
// 只实现 Ganesh + GL 路径：SDL3 建 GL 上下文（Windows WGL / Linux GLX 或
// EGL 由 SDL 探测选择），GrGLMakeNativeInterface 取过程地址，
// GrDirectContexts::MakeGL 建上下文，FBO 0 包装成 SkSurface。命令回放与
// SkiaRenderer 光栅后端共用同一套坐标语义（deviceScale 乘进每个矩形、
// 逐码点字体回退、top-left 文本原点）。

#include "lumen/render/skia_gpu_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

// windows.h（经 GL 头引入）把 DrawText 定义成宏，撞上 CommandType 枚举。
#ifdef DrawText
#undef DrawText
#endif

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
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
#include "include/core/SkTypeface.h"
#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#elif defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#endif
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"

#include "skia_text.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/gpu/gl/GrGLTypes.h"

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

class SkiaGpuRenderer final : public Renderer {
  public:
    SkiaGpuRenderer(SDL_Window* window, core::Color clear,
                    std::string* diagnostics, bool allowSwap = true)
        : window_(window), clearColor_(clear), allowSwap_(allowSwap) {
        if (!SDL_GL_LoadLibrary(nullptr)) {
            fail(diagnostics, std::string("SDL_GL_LoadLibrary: ") + SDL_GetError());
            return;
        }
        glLibraryLoaded_ = true;
        glContext_ = SDL_GL_CreateContext(window_);
        if (glContext_ == nullptr) {
            fail(diagnostics, std::string("SDL_GL_CreateContext: ") + SDL_GetError());
            return;
        }
        if (!SDL_GL_MakeCurrent(window_, glContext_)) {
            fail(diagnostics, std::string("SDL_GL_MakeCurrent: ") + SDL_GetError());
            return;
        }
        sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
        if (interface == nullptr) {
            fail(diagnostics, "GrGLMakeNativeInterface returned null");
            return;
        }
        grContext_ = GrDirectContexts::MakeGL(interface);
        if (grContext_ == nullptr) {
            fail(diagnostics, "GrDirectContexts::MakeGL returned null");
            return;
        }
        fontMgr_ = skia_text::makePlatformFontMgr();
        if (fontMgr_ == nullptr) {
            fail(diagnostics, "system font manager unavailable");
            return;
        }
        alive_ = true;
    }

    ~SkiaGpuRenderer() override {
        // GPU 对象只能在属主线程销毁（plan §3.3）；本类自创建起即归 UI
        // 线程所有。先释放 GPU 资源再拆上下文。
        if (grContext_ != nullptr &&
            !SDL_GL_MakeCurrent(window_, glContext_)) {
            grContext_->abandonContext();
        }
        images_.clear();
        surface_.reset();
        grContext_.reset();
        if (glContext_ != nullptr) {
            SDL_GL_DestroyContext(glContext_);
        }
        if (glLibraryLoaded_) {
            SDL_GL_UnloadLibrary();
        }
    }

    SkiaGpuRenderer(const SkiaGpuRenderer&) = delete;
    SkiaGpuRenderer& operator=(const SkiaGpuRenderer&) = delete;

    [[nodiscard]] bool isAlive() const { return alive_; }

    void setVSync(bool enabled) {
        if (makeCurrent()) {
            SDL_GL_SetSwapInterval(enabled ? 1 : 0);
        }
    }

    [[nodiscard]] RendererCapabilities capabilities() const override {
        RendererCapabilities caps;
        caps.backendName = "skia-gpu";
        caps.gpu = true;
        caps.partialSubmit = false;
        return caps;
    }

    void resetSurface(const RenderSurfaceDesc& desc) override {
        if (!makeCurrent()) {
            return;
        }
        // 尺寸/DPI/设备重建：丢弃旧 surface，下一帧按新尺寸重建。
        surface_.reset();
        deviceScale_ = desc.deviceScale > 0.0F ? desc.deviceScale : 1.0F;
        setVSync(desc.vsync);
        if (desc.widthPixels > 0 && desc.heightPixels > 0) {
            (void)ensureSurface(desc.widthPixels, desc.heightPixels);
        }
    }

    void submit(const RenderCommandList& commands, const FrameInfo& info) override {
        if (!alive_) {
            return;
        }
        const auto start = std::chrono::steady_clock::now();
        deviceScale_ = info.deviceScale > 0.0F ? info.deviceScale : deviceScale_;
        const int width = pixelLength(info.viewport.width);
        const int height = pixelLength(info.viewport.height);
        if (!ensureSurface(width, height)) {
            return;
        }

        SkCanvas* canvas = surface_->getCanvas();
        canvas->clear(toSkColor(clearColor_));
        // GPU 后端无 Preserve 能力：damage+preserve 请求退回全帧绘制并
        // 记录原因（与默认适配器的语义一致，plan §2.3）。
        const bool wantedPartial =
            info.damage.has_value() && info.preservePrevious;
        for (const auto& command : commands.commands()) {
            replayCommand(canvas, command);
        }
        const auto flushStart = std::chrono::steady_clock::now();
        if (!presentFrame()) {
            return;
        }
        const auto end = std::chrono::steady_clock::now();

        stats_.framesSubmitted += 1;
        stats_.submitMs =
            std::chrono::duration<double, std::milli>(end - start).count();
        stats_.gpuWaitMs =
            std::chrono::duration<double, std::milli>(end - flushStart).count();
        stats_.commandCount = commands.size();
        stats_.fullFrameFallback = wantedPartial;
        stats_.fallbackReason =
            wantedPartial ? "gpu-backend-has-no-partial-submit" : "";
    }

    // --- 即时路径转发（旧调用方编译兼容；帧边界在 endFrame 交换）---

    void beginFrame(core::Size viewport) override {
        immediateViewport_ = viewport;
        if (alive_) {
            (void)ensureSurface(pixelLength(viewport.width),
                                pixelLength(viewport.height));
            if (surface_ != nullptr) {
                surface_->getCanvas()->clear(toSkColor(clearColor_));
            }
        }
    }
    void save() override {
        if (immediateCanvas() != nullptr) {
            immediateCanvas()->save();
        }
    }
    void restore() override {
        if (immediateCanvas() != nullptr) {
            immediateCanvas()->restore();
        }
    }
    void clipRect(core::Rect rect) override {
        if (immediateCanvas() != nullptr) {
            immediateCanvas()->clipRect(scaled(rect), SkClipOp::kIntersect, false);
        }
    }
    void drawRect(core::Rect rect, core::Color color,
                  core::CornerRadius radius) override {
        if (immediateCanvas() != nullptr) {
            paintRect(immediateCanvas(), rect, color, radius);
        }
    }
    void drawText(TextRun run, core::TextStyle style) override {
        if (immediateCanvas() != nullptr) {
            paintText(immediateCanvas(), run, style);
        }
    }
    void drawImage(ImageId id, core::Rect destination) override {
        if (immediateCanvas() != nullptr) {
            blitImage(immediateCanvas(), id, destination);
        }
    }
    void endFrame() override {
        if (!alive_ || surface_ == nullptr) {
            return;
        }
        (void)presentFrame();
    }

  private:
    [[nodiscard]] int pixelLength(float logical) const {
        return std::max(1, static_cast<int>(std::lround(
                               static_cast<double>(logical) * deviceScale_)));
    }

    void fail(std::string* diagnostics, std::string message) {
        alive_ = false;
        stats_.fullFrameFallback = true;
        stats_.fallbackReason = message;
        if (grContext_ != nullptr) {
            // Lost contexts must not receive GL calls during resource teardown.
            grContext_->abandonContext();
        }
        if (diagnostics != nullptr) {
            *diagnostics = message;
        }
        std::fprintf(stderr, "lumen skia-gpu: %s\n", message.c_str());
    }

    [[nodiscard]] bool makeCurrent() {
        if (!alive_) {
            return false;
        }
        if (!SDL_GL_MakeCurrent(window_, glContext_)) {
            fail(nullptr, "gl-context-lost");
            return false;
        }
        if (grContext_->abandoned()) {
            fail(nullptr, "gpu-device-lost");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool presentFrame() {
        if (!makeCurrent()) {
            return false;
        }
        grContext_->flushAndSubmit();
        if (grContext_->abandoned()) {
            fail(nullptr, "gpu-device-lost");
            return false;
        }
        if (allowSwap_ && !SDL_GL_SwapWindow(window_)) {
            fail(nullptr, std::string("gl-swap-failed: ") + SDL_GetError());
            return false;
        }
        return true;
    }

    [[nodiscard]] bool ensureSurface(int width, int height) {
        // Check even when reusing a surface: device loss need not coincide
        // with a resize, and another renderer may have changed the context.
        if (!makeCurrent()) {
            return false;
        }
        if (surface_ != nullptr && width_ == width && height_ == height) {
            return true;
        }
        if (width > grContext_->maxRenderTargetSize() ||
            height > grContext_->maxRenderTargetSize()) {
            fail(nullptr, "surface-size-exceeds-device-limit");
            return false;
        }
        GrGLFramebufferInfo framebuffer{};
        framebuffer.fFBOID = 0;
        framebuffer.fFormat = GL_RGBA8;
        const GrBackendRenderTarget target =
            GrBackendRenderTargets::MakeGL(width, height, 0, 0, framebuffer);
        surface_ = SkSurfaces::WrapBackendRenderTarget(
            grContext_.get(), target, kBottomLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType, nullptr, nullptr);
        if (surface_ == nullptr) {
            fail(nullptr, "wrap-backend-rendertarget-failed");
            return false;
        }
        width_ = width;
        height_ = height;
        return true;
    }

    [[nodiscard]] SkCanvas* immediateCanvas() {
        return alive_ && surface_ != nullptr ? surface_->getCanvas() : nullptr;
    }

    [[nodiscard]] SkRect scaled(const core::Rect& rect) const {
        return SkRect::MakeXYWH(rect.left() * deviceScale_,
                                rect.top() * deviceScale_,
                                rect.size.width * deviceScale_,
                                rect.size.height * deviceScale_);
    }

    void replayCommand(SkCanvas* canvas, const RenderCommand& command) {
        switch (command.type) {
            case CommandType::Save:
                canvas->save();
                break;
            case CommandType::Restore:
                canvas->restore();
                break;
            case CommandType::ClipRect:
                canvas->clipRect(scaled(command.rect), SkClipOp::kIntersect,
                                 false);
                break;
            case CommandType::DrawRect:
                paintRect(canvas, command.rect, command.color, command.radius);
                break;
            case CommandType::DrawText:
                paintText(canvas, command.textRun, command.textStyle);
                break;
            case CommandType::DrawImage:
                blitImage(canvas, command.image, command.rect);
                break;
            case CommandType::UploadImage:
                uploadImage(command.image, command.pixels);
                break;
            case CommandType::UnloadImage:
                images_.erase(command.image);
                break;
        }
    }

    void paintRect(SkCanvas* canvas, const core::Rect& rect, core::Color color,
                   const core::CornerRadius& radius) {
        if (color.a == 0) {
            return;
        }
        const float scale = deviceScale_;
        const SkRect skRect = scaled(rect);
        SkPaint paint;
        paint.setStyle(SkPaint::kFill_Style);
        paint.setAntiAlias(true);
        paint.setColor(toSkColor(color));
        const float minSide = std::min(rect.size.width, rect.size.height) * 0.5F;
        const float rTL = std::clamp(radius.topLeft, 0.0F, minSide);
        const float rTR = std::clamp(radius.topRight, 0.0F, minSide);
        const float rBR = std::clamp(radius.bottomRight, 0.0F, minSide);
        const float rBL = std::clamp(radius.bottomLeft, 0.0F, minSide);
        if (rTL <= 0.0F && rTR <= 0.0F && rBR <= 0.0F && rBL <= 0.0F) {
            canvas->drawRect(skRect, paint);
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
        canvas->drawRRect(rounded, paint);
    }

    void paintText(SkCanvas* canvas, const TextRun& run,
                   const core::TextStyle& style) {
        if (run.text.empty() || style.color.a == 0 || fontMgr_ == nullptr) {
            return;
        }
        const float fontSize = style.fontSize > 0.0F ? style.fontSize : 14.0F;
        const float scale = deviceScale_;
        SkPaint paint;
        paint.setStyle(SkPaint::kFill_Style);
        paint.setAntiAlias(true);
        paint.setColor(toSkColor(style.color));
        // M1：优先消费布局共享的 shaped 数据（真实 glyph id + 布局
        // advance/baseline）；存在占位 run 时回退旧逐码点路径。
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
            skia_text::drawShapedText(canvas, fontMgr_.get(), run, style,
                                      scale);
            return;
        }
        // 与 SkiaRenderer 光栅后端相同的逐码点路径：真实字体光栅化 +
        // 缺字回退，top-left 原点 + fontSize 基线。
        const SkFontStyle fontStyle =
            style.bold ? SkFontStyle::Bold() : SkFontStyle::Normal();
        sk_sp<SkTypeface> typeface =
            fontMgr_->matchFamilyStyle(nullptr, fontStyle);
        float x = run.origin.x * scale;
        const float baseline = (run.origin.y + fontSize) * scale;
        for (std::size_t offset = 0; offset < run.text.size();) {
            const std::size_t length = utf8SequenceLength(
                run.text.data() + offset, run.text.size() - offset);
            const SkUnichar codePoint =
                decodeUtf8(run.text.data() + offset, length);
            sk_sp<SkTypeface> glyphTypeface = typeface;
            if (!glyphTypeface ||
                glyphTypeface->unicharToGlyph(codePoint) == 0) {
                glyphTypeface = fontMgr_->matchFamilyStyleCharacter(
                    nullptr, fontStyle, nullptr, 0, codePoint);
            }
            if (glyphTypeface == nullptr) {
                break;
            }
            SkFont font(glyphTypeface, fontSize * scale);
            font.setEdging(SkFont::Edging::kAntiAlias);
            canvas->drawSimpleText(run.text.data() + offset, length,
                                   SkTextEncoding::kUTF8, x, baseline, font,
                                   paint);
            x += font.measureText(run.text.data() + offset, length,
                                  SkTextEncoding::kUTF8);
            offset += length;
        }
    }

    void blitImage(SkCanvas* canvas, ImageId id, const core::Rect& destination) {
        const auto it = images_.find(id);
        if (it == images_.end() || !it->second) {
            return;
        }
        // 拉满目标矩形，语义与 CPU/Skia 光栅一致；Ganesh 首绘时把 raster
        // 图像上传为纹理并缓存。
        const SkRect source = SkRect::MakeWH(
            static_cast<float>(it->second->width()),
            static_cast<float>(it->second->height()));
        canvas->drawImageRect(it->second, source, scaled(destination),
                              SkSamplingOptions(SkFilterMode::kLinear,
                                                SkMipmapMode::kNone),
                              nullptr, SkCanvas::kFast_SrcRectConstraint);
    }

    void uploadImage(ImageId id, const PixelBuffer& pixels) {
        if (pixels.width <= 0 || pixels.height <= 0 ||
            pixels.rgba.size() !=
                static_cast<std::size_t>(pixels.width) *
                    static_cast<std::size_t>(pixels.height) * 4U) {
            return;
        }
        const SkImageInfo info = SkImageInfo::Make(
            pixels.width, pixels.height, kRGBA_8888_SkColorType,
            kUnpremul_SkAlphaType);
        const SkPixmap pixmap(info, pixels.rgba.data(),
                              static_cast<std::size_t>(pixels.width) * 4);
        sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(pixmap);
        if (image != nullptr) {
            images_.erase(id);
            images_.emplace(id, std::move(image));
            stats_.uploads += 1;
        }
    }

    SDL_Window* window_{nullptr};
    SDL_GLContext glContext_{nullptr};
    core::Color clearColor_{};
    sk_sp<GrDirectContext> grContext_{};
    sk_sp<SkSurface> surface_{};
    sk_sp<SkFontMgr> fontMgr_{};
    std::map<ImageId, sk_sp<SkImage>> images_{};
    core::Size immediateViewport_{};
    int width_{0};
    int height_{0};
    float deviceScale_{1.0F};
    bool allowSwap_{true};
    bool alive_{false};
    bool glLibraryLoaded_{false};
};

}  // namespace

std::unique_ptr<Renderer> createSkiaGpuRenderer(const SkiaGpuRendererDesc& desc,
                                                std::string* diagnostics) {
    if (diagnostics != nullptr) {
        diagnostics->clear();
    }
    if (desc.sdlWindow == nullptr || desc.windowSystem == nullptr ||
        std::string(desc.windowSystem) != "sdl3") {
        if (diagnostics != nullptr) {
            *diagnostics = "unsupported native surface (expected sdl3 window)";
        }
        return nullptr;
    }
    auto renderer = std::make_unique<SkiaGpuRenderer>(
        static_cast<SDL_Window*>(desc.sdlWindow), desc.clear, diagnostics,
        desc.allowSwap);
    if (!renderer->isAlive()) {
        return nullptr;
    }
    RenderSurfaceDesc surface;
    surface.nativeWindow = desc.sdlWindow;
    surface.windowSystem = desc.windowSystem;
    surface.widthPixels = desc.widthPixels;
    surface.heightPixels = desc.heightPixels;
    surface.deviceScale = desc.deviceScale;
    surface.vsync = desc.vsync;
    renderer->resetSurface(surface);
    if (!renderer->isAlive()) {
        if (diagnostics != nullptr) {
            *diagnostics = renderer->stats().fallbackReason;
        }
        return nullptr;
    }
    return renderer;
}

bool probeSkiaGpuAvailable(std::string* diagnostics) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        if (diagnostics != nullptr) {
            *diagnostics = std::string("SDL_Init: ") + SDL_GetError();
        }
        return false;
    }
    // 隐藏窗口上完整走一遍 GL + Ganesh 初始化，随即销毁（无副作用探测）。
    SDL_Window* window = SDL_CreateWindow(
        "lumen-gpu-probe", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        if (diagnostics != nullptr) {
            *diagnostics = std::string("probe window: ") + SDL_GetError();
        }
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    bool available = false;
    {
        SkiaGpuRendererDesc desc;
        desc.sdlWindow = window;
        desc.widthPixels = 64;
        desc.heightPixels = 64;
        desc.allowSwap = false;
        available = createSkiaGpuRenderer(desc, diagnostics) != nullptr;
    }
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return available;
}

bool skiaGpuRendererAlive(const Renderer& renderer) {
    const auto* gpu = dynamic_cast<const SkiaGpuRenderer*>(&renderer);
    return gpu != nullptr && gpu->isAlive();
}

}  // namespace lumen::render
