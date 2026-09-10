#pragma once

#include <map>

#include "lumen/render/render_commands.h"
#include "lumen/render/renderer.h"

namespace lumen::render {

// CPU software rasterizer (plan §4.3): RGBA framebuffer with solid rects,
// rounded rects, rect clipping, basic image blitting and placeholder text
// (built-in 5x7 bitmap font; non-ASCII glyphs render as boxes). Logical
// coordinates are scaled to framebuffer pixels by `deviceScale`, which the
// app derives from PlatformWindow drawable/logical sizes.
class CpuRenderer final : public Renderer {
  public:
    // Preserve keeps the previous frame's pixels outside the region the
    // caller repaints (plan 阶段6: 脏矩形) — combine with clipRect(damage).
    // The optional damage rect (3-arg beginFrame) also erases the damaged
    // region to the clear color first, so transparent backgrounds inside it
    // match a full repaint exactly; an empty/default rect keeps the whole
    // previous frame (pure Preserve).
    enum class FrameMode { Clear, Preserve };

    explicit CpuRenderer(float deviceScale = 1.0F,
                         core::Color clear = core::Color::fromRGBA(24, 24, 27));

    void setDeviceScale(float scale);

    // Framebuffer of the last beginFrame(); present() consumes it directly.
    [[nodiscard]] const PixelBuffer& pixels() const { return buffer_; }

    // Uploads an image for drawImage(); ids are stable and opaque. Buffers
    // use straight (non-premultiplied) RGBA, matching SkiaRenderer.
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
    void drawRect(core::Rect rect, core::Color color,
                  core::CornerRadius radius = {}) override;
    void drawText(TextRun run, core::TextStyle style) override;
    void drawImage(ImageId id, core::Rect destination) override;
    void endFrame() override;

    // --- v0.2 命令路径（阶段7B）---
    // 原生 submit：damage + preserve 请求走 Preserve 帧模式并按命令
    // bounds 裁剪掉 damage 外的绘制命令；无上一帧时退回全帧并记录原因。
    [[nodiscard]] RendererCapabilities capabilities() const override;
    void submit(const RenderCommandList& commands,
                const FrameInfo& info) override;

  private:
    struct ClipRects {
        int x0{0};
        int y0{0};
        int x1{0};
        int y1{0};
    };

    [[nodiscard]] int toPixel(float logical) const;
    void blendPixel(int px, int py, core::Color color);
    void fillLogicalRect(const core::Rect& rect, core::Color color,
                         const core::CornerRadius& radius);

    float deviceScale_;
    core::Color clearColor_;
    PixelBuffer buffer_{};
    PixelBuffer previous_{};
    bool hasPrevious_{false};
    std::vector<ClipRects> clip_{};
    std::map<ImageId, PixelBuffer> images_{};
    ImageId nextImageId_{1};
};

}  // namespace lumen::render
