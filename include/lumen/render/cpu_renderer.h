#pragma once

#include <map>
#include <memory>
#include <optional>

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

    void setDeviceScale(float scale) override;

    // 透明窗口（WindowDesc.transparent）：清屏色改为全透明（圆角外的
    // 像素不被不透明底填充；实际透明合成取决于宿主能力）。不透明
    // 窗口保持默认深底（无合成器依赖）。
    void setClearColor(core::Color clear);

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

    // Actual Premultiplied/Opaque framebuffer; no conversion or copy on reads.
    // Framebuffer of the last completed frame; endFrame() swaps it with the
    // drawing buffer without copying at publication. Mid-frame reads
    // see the previous completed frame; before the first completed frame the
    // drawing buffer itself is returned (legacy mid-frame read semantics).
    [[nodiscard]] const PixelBuffer& pixels() const {
        return hasFront_ ? front_ : buffer_;
    }

    // Uploads an image for drawImage(); ids are stable and opaque. Buffers
    // accept all validated modes and normalize once to Premultiplied/Opaque.
    // Invalid buffers return 0. Draws never convert cached image representation.
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
    // 圆角裁剪形状（设备像素空间；与 fill/stroke 的 AA 距离场同构）。
    // 公开：编译单元内的 SDF/形状工具函数以 DeviceShape 别名引用。
    struct ClipShape {
        float x0{0.0F};
        float y0{0.0F};
        float x1{0.0F};
        float y1{0.0F};
        float rTL{0.0F};
        float rTR{0.0F};
        float rBL{0.0F};
        float rBR{0.0F};
    };

    void clipRect(core::Rect rect) override;
    // 圆角裁剪（Renderer 注释同）：SDF 覆盖率乘子门控栈顶像素写入。
    void clipRounded(core::Rect rect, core::CornerRadius radius) override;
    void drawRect(core::Rect rect, core::Color color,
                  core::CornerRadius radius = {}) override;
    // S1：圆角描边（环带）——外圆角矩形包含且内圆角矩形（内缩 width）
    // 不包含的像素才混合，内部保持透明（§9.2）。
    void drawRectStroke(core::Rect rect, core::Color color,
                        core::CornerRadius radius, float width) override;
    void drawText(TextRun run, core::TextStyle style) override;
    void drawImage(ImageId id, core::Rect destination) override;
    // M6：矢量图标 —— 软件线条光栅（粗线 = 垂直/水平/对角微偏移多遍）。
    void drawIcon(std::vector<std::vector<core::Offset>> polylines,
                  core::Rect box, core::Color color,
                  float strokeWidth) override;
    // M6：阴影 —— 软模糊：偏移矩形的 alpha 掩膜经可分离 box blur 近似
    // 高斯（σ 与 SkiaRenderer 的 kNormal_SkBlurStyle 同口径），再以
    // token 阴影色逐像素 coverage 混合；blur=0 时退回偏移扁平面。
    void drawShadow(core::Rect elevatedBox, core::Color color,
                    core::Offset offset, float blur) override;
    void endFrame() override;

    // --- v0.2 命令路径（阶段7B）---
    // 原生 submit：damage 向外对齐设备像素后走 Preserve，并按命令
    // bounds 裁剪；无同尺寸上一帧时退回全帧并记录原因。
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
    // 栈式裁剪状态：scissor + 活跃圆角形状列表（save 拷贝栈顶 → 嵌套
    // 圆角裁剪自然求交）。
    struct ClipState {
        ClipRects rect{};
        std::vector<ClipShape> rounded{};
    };

    [[nodiscard]] int toPixel(float logical) const;
    [[nodiscard]] ClipRects pixelRect(core::Rect rect) const;
    [[nodiscard]] ClipRects rasterBounds(float left, float top, float right,
                                         float bottom) const;
    void fillSpan(int y, int x0, int x1, core::Color color);
    void blendPixel(int px, int py, core::Color color);
    // Image bytes are already premultiplied: coverage scales all four channels.
    void blendImagePixel(int px, int py, const std::uint8_t* rgba);
    // Common source-over accumulator; channels here are NOT a straight Color.
    void compositePixel(int px, int py, unsigned r, unsigned g, unsigned b, unsigned a);
    // 灰度 coverage 混合（字形抗锯齿）：coverage 折进 alpha 后走同一
    // source-over 路径。
    void blendCoveragePixel(int px, int py, core::Color color,
                            std::uint8_t coverage);
    // 圆角裁剪覆盖率（0..1；无活跃圆角裁剪恒 1）。像素写入统一经此
    // 门控（Color/字形/图标走 blendPixel，图片走 blendImagePixel）。
    [[nodiscard]] float roundedCoverage(int px, int py) const;
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
    // 软阴影的单向 box blur（滑动窗口，域外计 0——能量守恒的卷积语义）。
    void boxBlurPass(std::vector<float>& src, std::vector<float>& dst,
                     int width, int height, int radius, bool horizontal);

    float deviceScale_;
    core::Color clearColor_;
    // 双缓冲：buffer_ = 绘制目标（back），front_ = 最近完成帧（present/
    // pixels() 读取）。endFrame 交换存储；Preserve 先同步 back 中过时的
    // 像素，再清理当前 damage。绘制期间 front 始终保留上一完成帧。
    PixelBuffer buffer_{};
    PixelBuffer front_{};
    bool hasFront_{false};
    // Publication and reuse are separate: configuration changes invalidate
    // Preserve without exposing an unfinished back buffer through pixels().
    bool frontReusable_{false};
    bool frameMatchesConfig_{false};
    // 上次局部 submit 改动的像素区域；空值表示 back 需要全量同步。
    std::optional<ClipRects> backDamage_{};
    std::vector<ClipState> clip_{};
    // 栈顶是否携带圆角裁剪（无则像素写入零额外成本）。
    bool roundedActive_{false};
    std::map<ImageId, PixelBuffer> images_{};
    ImageId nextImageId_{1};
    std::shared_ptr<const text::SystemFontManager> systemFonts_{};
    // 软阴影 scratch（按命令尺寸重灌，避免逐命令分配）。
    std::vector<float> shadowMask_{};
    std::vector<float> shadowScratch_{};
};

}  // namespace lumen::render
