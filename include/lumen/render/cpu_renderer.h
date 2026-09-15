#pragma once

#include <map>
#include <memory>

#include "lumen/render/render_commands.h"
#include "lumen/render/renderer.h"
#include "lumen/text/system_font_manager.h"

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

    // 系统字体光栅（窗口路径经 AppShell::setFontManager 转发；空 = 占
    // 位 5x7 点阵字）。注入后 drawText 使用 SystemFontManager 的系统字
    // 形位图（Windows 优先 GDI 雅黑，其他平台回退 stb），排版
    // advance/baseline 与同一管理器的度量一致；headless/测试不注入，
    // 保持帧哈希确定性。
    void setSystemFonts(
        std::shared_ptr<const text::SystemFontManager> fonts) {
        systemFonts_ = std::move(fonts);
    }
    [[nodiscard]] bool hasSystemFonts() const {
        return systemFonts_ != nullptr;
    }

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
    // M6：矢量图标 —— 软件线条光栅（粗线 = 垂直/水平/对角微偏移多遍）。
    void drawIcon(std::vector<std::vector<core::Offset>> polylines,
                  core::Rect box, core::Color color,
                  float strokeWidth) override;
    // M6：阴影降级 —— 无模糊：token 阴影色的偏移扁平面（确定性近似；
    // 命令与 Skia/GPU 一致，像素由后端能力决定）。
    void drawShadow(core::Rect elevatedBox, core::Color color,
                    core::Offset offset, float blur) override;
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
    // 灰度 coverage 混合（字形抗锯齿）：coverage 折进 alpha 后走同一
    // source-over 路径。
    void blendCoveragePixel(int px, int py, core::Color color,
                            std::uint8_t coverage);
    // 系统字体字形光栅（false = 无位图，调用方回退占位/留白）。
    bool drawSystemGlyph(std::uint32_t codePoint, const std::string& family,
                         float penX, float baselineY, float fontSize,
                         core::TextStyle style);
    void fillLogicalRect(const core::Rect& rect, core::Color color,
                         const core::CornerRadius& radius);
    // M1：单个占位字形盒（glyphId = 码点；advance/位置来自布局）。
    void drawPlaceholderGlyph(std::uint32_t codePoint, float glyphX,
                              float topY, float lineHeight, float glyphScale,
                              float advance, core::TextStyle style);

    float deviceScale_;
    core::Color clearColor_;
    PixelBuffer buffer_{};
    PixelBuffer previous_{};
    bool hasPrevious_{false};
    std::vector<ClipRects> clip_{};
    std::map<ImageId, PixelBuffer> images_{};
    ImageId nextImageId_{1};
    std::shared_ptr<const text::SystemFontManager> systemFonts_{};
};

}  // namespace lumen::render
