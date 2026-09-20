#pragma once

#include <map>
#include <memory>

#include "lumen/render/renderer.h"

namespace lumen::render {

// Skia backend (plan §4.3/阶段5): maps the same draw commands onto Skia's
// raster surface for antialiased output. Skia types are confined to the
// implementation via pimpl so this header stays dependency-free and the
// adapter only links when the build enables it (LUMEN_ENABLE_SKIA).
//
// Text is rasterized through Skia's UTF-8 font path; Skia adds antialiasing
// and premultiplied blending while sharing the Renderer command contract.
class SkiaRenderer final : public Renderer {
  public:
    // Preserve replays the previous frame so only the clipped damage area
    // needs repainting (mirrors CpuRenderer, plan 阶段6).
    enum class FrameMode { Clear, Preserve };

    explicit SkiaRenderer(float deviceScale = 1.0F,
                          core::Color clear = core::Color::fromRGBA(24, 24, 27));
    ~SkiaRenderer() override;

    SkiaRenderer(const SkiaRenderer&) = delete;
    SkiaRenderer& operator=(const SkiaRenderer&) = delete;
    SkiaRenderer(SkiaRenderer&&) = delete;
    SkiaRenderer& operator=(SkiaRenderer&&) = delete;

    void setDeviceScale(float scale);

    // RGBA snapshot of the last frame (written by endFrame); feed it to
    // PlatformWindow::present or frameHash for backend comparisons.
    [[nodiscard]] const PixelBuffer& pixels() const { return snapshot_; }

    // Uploads an image for drawImage(); straight (non-premultiplied) RGBA,
    // the same contract CpuRenderer::registerImage implements.
    ImageId registerImage(PixelBuffer image);
    // Frees a registered image; drawing a freed id is a no-op (resource
    // lifecycle, plan 阶段6).
    void unregisterImage(ImageId id);
    void clearImages();

    void beginFrame(core::Size viewport) override;
    void beginFrame(core::Size viewport, FrameMode mode);
    void beginFrame(core::Size viewport, FrameMode mode, core::Rect damage);
    void save() override;
    void restore() override;
    void clipRect(core::Rect rect) override;
    // 圆角裁剪（Renderer 注释同）：clipRRect（AA 边界与 CPU SDF 覆盖
    // 率门控同语义）。
    void clipRounded(core::Rect rect, core::CornerRadius radius) override;
    void drawRect(core::Rect rect, core::Color color,
                  core::CornerRadius radius = {}) override;
    // S1：圆角描边（SkPaint stroke；内部透明，§9.2）。
    void drawRectStroke(core::Rect rect, core::Color color,
                        core::CornerRadius radius, float width) override;
    void drawText(TextRun run, core::TextStyle style) override;
    void drawImage(ImageId id, core::Rect destination) override;
    // M6：矢量图标（SkPath stroke）与阴影（blur mask filter）。
    void drawIcon(std::vector<std::vector<core::Offset>> polylines,
                  core::Rect box, core::Color color,
                  float strokeWidth) override;
    void drawShadow(core::Rect elevatedBox, core::Color color,
                    core::Offset offset, float blur) override;
    void endFrame() override;

    // v0.2 阶段7B: 报告光栅后端能力；submit 走 Renderer 默认适配器。
    [[nodiscard]] RendererCapabilities capabilities() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    PixelBuffer snapshot_{};
};

}  // namespace lumen::render
