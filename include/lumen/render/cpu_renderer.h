#pragma once

#include <map>

#include "lumen/render/renderer.h"

namespace lumen::render {

// CPU software rasterizer (plan §4.3): RGBA framebuffer with solid rects,
// rounded rects, rect clipping, basic image blitting and placeholder text
// (built-in 5x7 bitmap font; non-ASCII glyphs render as boxes). Logical
// coordinates are scaled to framebuffer pixels by `deviceScale`, which the
// app derives from PlatformWindow drawable/logical sizes.
class CpuRenderer final : public Renderer {
  public:
    explicit CpuRenderer(float deviceScale = 1.0F,
                         core::Color clear = core::Color::fromRGBA(24, 24, 27));

    void setDeviceScale(float scale);

    // Framebuffer of the last beginFrame(); present() consumes it directly.
    [[nodiscard]] const PixelBuffer& pixels() const { return buffer_; }

    // Uploads an image for drawImage(); ids are stable and opaque.
    ImageId registerImage(PixelBuffer image);

    void beginFrame(core::Size viewport) override;
    void save() override;
    void restore() override;
    void clipRect(core::Rect rect) override;
    void drawRect(core::Rect rect, core::Color color,
                  core::CornerRadius radius = {}) override;
    void drawText(TextRun run, core::TextStyle style) override;
    void drawImage(ImageId id, core::Rect destination) override;
    void endFrame() override;

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
    std::vector<ClipRects> clip_{};
    std::map<ImageId, PixelBuffer> images_{};
    ImageId nextImageId_{1};
};

}  // namespace lumen::render
