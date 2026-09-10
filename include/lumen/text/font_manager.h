#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen::text {

// v0.3 阶段8B (plan §3.2): 字体管理契约。
//
// FontManager 描述字体族、weight/style、字体回退与系统字体查询；
// Renderer/Widget/DSL 不接触 Skia 或平台字体类型。CPU-only 构建使用
// PlaceholderFontManager 的确定性度量（正式桌面 shaping 由可选的 Skia
// 实现提供，契约不变）。

enum class FontWeight : std::uint16_t {
    Thin = 100,
    ExtraLight = 200,
    Light = 300,
    Normal = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
    ExtraBold = 800,
    Black = 900,
};

// 字体查询：样式属性 + 请求族。
struct FontQuery {
    std::string family{};
    FontWeight weight{FontWeight::Normal};
    bool italic{false};
    float sizePx{14.0F};
};

// 单个字形度量（em 相对）。
struct GlyphMetrics {
    float advanceEm{0.6F};
    float ascentEm{0.8F};
    float descentEm{0.4F};
};

class FontManager {
  public:
    virtual ~FontManager() = default;

    // 回退链解析：为 codePoint 找到覆盖它的族名；返回空表示缺字。
    [[nodiscard]] virtual std::string resolveFamily(
        const FontQuery& query, char32_t codePoint) const = 0;
    // 字形度量（按查询尺寸换算为像素）。
    [[nodiscard]] virtual bool glyphMetrics(const FontQuery& query,
                                            char32_t codePoint,
                                            GlyphMetrics* out) const = 0;
    // 系统字体查询（诊断/设置页用）。
    [[nodiscard]] virtual std::vector<std::string> availableFamilies()
        const = 0;
};

// CPU-only 确定性实现：latin/cjk/emoji 三段回退链，所有字形统一
// 0.6em advance / 0.8em ascent / 0.4em descent（与既有布局度量一致，
// 保证布局与绘制共用同一份数据时在不同后端不漂移）。
class PlaceholderFontManager final : public FontManager {
  public:
    [[nodiscard]] std::string resolveFamily(
        const FontQuery& query, char32_t codePoint) const override;
    [[nodiscard]] bool glyphMetrics(const FontQuery& query,
                                    char32_t codePoint,
                                    GlyphMetrics* out) const override;
    [[nodiscard]] std::vector<std::string> availableFamilies()
        const override;

    // 进程级默认实例（布局/绘制的共享字体源）。
    [[nodiscard]] static PlaceholderFontManager& shared();

    PlaceholderFontManager() = default;

  private:
    static constexpr const char* kLatinFamily = "lumen-latin";
    static constexpr const char* kCjkFamily = "lumen-cjk";
    static constexpr const char* kEmojiFamily = "lumen-emoji";
};

}  // namespace lumen::text
