#pragma once

#include <memory>
#include <string>

#include "lumen/text/font_manager.h"

namespace lumen::text {

// M1：Skia-backed FontManager（桌面正式字体/回退/shaping）。
//
// 公共接口不暴露 Skia 类型（pimpl）；shaped run 的生命周期由 TextLayout
// 管理，RenderCommand 只接收可序列化的 ShapedGlyph/TextRun。
// CPU-only 构建仍可包含本头文件，但工厂返回 nullptr 并给出诊断。
class SkiaFontManager final : public FontManager {
  public:
    [[nodiscard]] FontBackend backend() const override {
        return FontBackend::Skia;
    }
    [[nodiscard]] bool supportsShaping() const override { return true; }

    [[nodiscard]] std::string resolveFamily(
        const FontQuery& query, char32_t codePoint) const override;
    [[nodiscard]] FontFallbackStatus resolveWithStatus(
        const FontQuery& query, char32_t codePoint) const override;
    [[nodiscard]] bool glyphMetrics(const FontQuery& query,
                                    char32_t codePoint,
                                    GlyphMetrics* out) const override;
    [[nodiscard]] bool horizontalMetrics(const FontQuery& query,
                                         float* ascentPx,
                                         float* descentPx) const override;
    [[nodiscard]] std::vector<ShapedGlyph> shapeCluster(
        const FontQuery& query, const std::string& graphemeUtf8,
        std::uint32_t clusterIndex) const override;
    [[nodiscard]] std::vector<std::string> availableFamilies()
        const override;
    [[nodiscard]] std::string diagnostic() const override;

    ~SkiaFontManager() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    SkiaFontManager();
    friend std::unique_ptr<FontManager> createSkiaFontManager(
        std::string* diagnostic);
};

// 创建 Skia 字体管理器；CPU-only 构建或无系统字体时返回 nullptr，
// diagnostic 说明原因（调用方仍可用占位启动，不阻塞 UI 线程）。
[[nodiscard]] std::unique_ptr<FontManager> createSkiaFontManager(
    std::string* diagnostic = nullptr);

}  // namespace lumen::text
