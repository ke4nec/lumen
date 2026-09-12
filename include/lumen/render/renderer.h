#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/core/windowing.h"
#include "lumen/text/font_manager.h"

namespace lumen::render {

using ImageId = std::uint64_t;

// M1：与布局共享的 shaped 文本绘制数据（可序列化，无平台类型）。
// family 为布局解析出的覆盖族；placeholder=true 表示 glyphId 实为码点
// （占位/缺字路径），后端按占位字形绘制。
struct TextGlyphRun {
    std::string family{};
    bool placeholder{true};
    std::vector<text::ShapedGlyph> glyphs{};

    [[nodiscard]] bool operator==(const TextGlyphRun&) const = default;
};

struct TextRun {
    std::string text{};
    core::Offset origin{};
    // M1：行顶到 baseline 的距离（布局真实 ascent）；<=0 时后端回退到
    // 自身默认基线（保持旧路径兼容）。
    float baselinePx{0.0F};
    // M1：布局产出的视觉序 shaped runs（glyphs 的 xOffsetPx 相对
    // origin）。为空时后端走自身逐码点路径；布局与绘制共享同一份
    // TextLayout 时非空，光标/选区/字形位置不跨后端漂移。
    std::vector<TextGlyphRun> shapedRuns{};

    [[nodiscard]] bool operator==(const TextRun&) const = default;
};

struct PixelBuffer {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba{};

    [[nodiscard]] bool operator==(const PixelBuffer&) const = default;
};

class RenderCommandList;

// 一帧的提交元数据（v0.2 阶段7B, plan §3.1；v0.3 阶段8A 关联 WindowId）。
struct FrameInfo {
    core::Size viewport{};
    // 本次提交需要更新的区域；nullopt 表示全帧。
    std::optional<core::Rect> damage{};
    // 保留 damage 之外的上一帧像素；仅 partialSubmit 后端有效。
    bool preservePrevious{false};
    float deviceScale{1.0F};
    std::uint64_t frameIndex{0};
    std::uint64_t timestampMs{0};
    // 提交归属窗口（诊断与多窗口 surface 管理，阶段8A）。
    core::WindowId window{};
};

// 后端能力报告（plan §3.1 capabilities()）。
struct RendererCapabilities {
    // Skia GPU 后端为 true；CPU/Skia 光栅为 false。
    bool gpu{false};
    // 是否支持 damage 范围的局部提交（保留上一帧）。
    bool partialSubmit{false};
    bool text{true};
    bool images{true};
    const char* backendName{"adapter"};
};

// 本帧统计（plan §3.1 stats()）。
struct RenderStats {
    std::uint64_t framesSubmitted{0};
    // 命令录制耗时（应用侧写入）。
    double cpuBuildMs{0.0};
    double submitMs{0.0};
    double gpuWaitMs{0.0};
    std::uint64_t cacheHits{0};
    std::uint64_t uploads{0};
    std::uint64_t commandCount{0};
    // damage 裁剪丢弃的命令数。
    std::uint64_t culledCommands{0};
    bool fullFrameFallback{false};
    std::string fallbackReason{};
};

// 目标 surface 重建描述（plan §3.1 resetSurface）。nativeWindow 是不透明
// 平台句柄，只在 lumen-platform 与 Renderer 适配层解引用。window 让设备
// 重建与窗口关联（阶段8A）。
struct RenderSurfaceDesc {
    void* nativeWindow{nullptr};
    const char* windowSystem{""};
    int widthPixels{0};
    int heightPixels{0};
    float deviceScale{1.0F};
    bool vsync{true};
    core::WindowId window{};
};

// Renderer contract shared by CpuRenderer (Stage 2), SkiaRenderer (Stage 5)
// and any future backend. UI/layout layers only program against this.
//
// v0.2 (plan §3.1) adds the command path on top of the legacy immediate
// path: `submit()` replays a recorded RenderCommandList. The default
// implementation adapts onto beginFrame/save/restore/draw*/endFrame, so
// every existing backend compiles unchanged; backends override for native
// damage handling and richer stats.
class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void beginFrame(core::Size viewport) = 0;
    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void clipRect(core::Rect rect) = 0;
    virtual void drawRect(core::Rect rect, core::Color color,
                          core::CornerRadius radius = {}) = 0;
    virtual void drawText(TextRun run, core::TextStyle style) = 0;
    virtual void drawImage(ImageId id, core::Rect destination) = 0;
    virtual void endFrame() = 0;

    // --- v0.2 command path ---

    // 后端能力报告。
    [[nodiscard]] virtual RendererCapabilities capabilities() const;
    // 提交一帧命令。默认适配：beginFrame -> (damage 裁剪) -> 命令回放 ->
    // endFrame，全部经由上面的即时路径。
    virtual void submit(const RenderCommandList& commands, const FrameInfo& info);
    // 最近一次提交的统计（UI 线程内更新）。
    [[nodiscard]] virtual RenderStats stats() const;
    // 应用侧把本帧命令录制耗时写入统计（stats().cpuBuildMs）。
    void noteCpuBuildMs(double milliseconds);
    // drawable size / DPI / 设备重建后的目标重建。默认 no-op（无平台
    // surface 的后端无需处理）。
    virtual void resetSurface(const RenderSurfaceDesc& desc);

  protected:
    // 默认 submit 适配器与子类共用的一段统计记账。
    void noteAdaptedSubmit(const RenderCommandList& commands, double submitMs,
                           std::uint64_t culledCommands, bool fullFrameFallback,
                           std::string fallbackReason);

    RenderStats stats_{};
};

// Stable 64-bit FNV-1a hash over the framebuffer bytes and dimensions; used
// by headless integration tests (plan §9: 无窗口模式生成稳定 frame hash).
[[nodiscard]] std::uint64_t frameHash(const PixelBuffer& buffer);

// Stage identifier for the renderer module contract (Stage 2: CpuRenderer).
[[nodiscard]] const char* rendererStageName();

}  // namespace lumen::render
