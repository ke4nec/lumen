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
//
// M1（自用路线图）：FontManager 公共接口不暴露 Skia 类型；shaped run 的
// 生命周期由 TextLayout 管理，RenderCommand 只接收可序列化的文本绘制
// 数据；字体不可用时返回明确 fallback 状态，不能静默改变编辑索引。

enum class FontBackend : std::uint8_t {
    Placeholder,
    Skia,
    Mobile,
};

[[nodiscard]] inline const char* fontBackendName(FontBackend backend) {
    switch (backend) {
        case FontBackend::Skia:
            return "skia";
        case FontBackend::Mobile:
            return "mobile";
        default:
            return "placeholder";
    }
}

// 各平台默认字体栈（空 family = 用它）。
//
// TextStyle::family 为空表示“平台默认”：解析时按码点脚本在该栈中
// 挑选首个覆盖者，缺字再走系统兜底（Skia matchFamilyStyleCharacter /
// 移动端目录扫描），保证中英文默认就有可读字体而不依赖调用方传名。
[[nodiscard]] std::vector<std::string> defaultFontStack();
[[nodiscard]] std::vector<std::string> defaultFontStackFor(char32_t codePoint);
[[nodiscard]] std::string defaultFontFamily();
// 系统 UI 语言是否偏好 CJK 字体（中文/日文/韩文系统返回 true）。
//
// Windows 走 GetUserDefaultUILanguage；macOS/iOS 走偏好语言列表
// （CFLocaleCopyPreferredLanguages，环境变量做回退）；其他平台走
// LC_ALL/LC_CTYPE/LANG/LANGUAGE 环境变量（首个有效值决定，zh/ja/ko
// 前缀为 true）。结果进程内缓存，切换系统语言需重启进程。空 family
// 的拉丁栈据此排序：CJK 系统连拉丁字母/数字也优先 CJK 字体，保证中文
// 界面字重/观感统一。
[[nodiscard]] bool systemUiPrefersCjkFont();
// 应用层覆盖默认字体栈：设置后 `family` 为空的文本走该栈（按序取首个
// 覆盖码点者），不再用系统语言默认。传空等价于清除。
//
// 优先级：TextStyle.family 显式指定 > 应用覆盖栈 > 系统语言默认。
// 须在首帧前调用（已布局的 shaped 缓存按空 family 键复用，不会追溯）。
void setDefaultFontStackOverride(std::vector<std::string> stack);
void clearDefaultFontStackOverride();

// 字体回退状态：明确区分“命中请求族”“回退到其他族”“缺字”。
struct FontFallbackStatus {
    std::string resolvedFamily{};
    // 请求族不可用而使用了回退族。
    bool fallbackUsed{false};
    // 没有任何可用族覆盖该码点（调用方必须走占位绘制 + 诊断）。
    bool missing{false};
    std::string diagnostic{};

    bool operator==(const FontFallbackStatus&) const = default;
};

// 可序列化的 shaped 字形（无 Skia/平台类型，可进 RenderCommand）。
struct ShapedGlyph {
    // 字形 id（占位路径为首码点；Skia 路径为真实 glyph id）。
    std::uint32_t glyphId{0};
    // 像素 advance（含 letterSpacing 由布局叠加，见 TextLayout）。
    float advancePx{0.0F};
    // 行内 x 偏移（布局填充，供命中测试/诊断）。
    float xOffsetPx{0.0F};
    // 所属 grapheme cluster 索引（编辑索引，不随回退改变）。
    std::uint32_t cluster{0};

    bool operator==(const ShapedGlyph&) const = default;
};

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

    [[nodiscard]] virtual FontBackend backend() const = 0;
    [[nodiscard]] virtual bool supportsShaping() const { return false; }

    // 回退链解析：为 codePoint 找到覆盖它的族名；返回空表示缺字。
    [[nodiscard]] virtual std::string resolveFamily(
        const FontQuery& query, char32_t codePoint) const = 0;
    // 带状态的回退解析：明确 fallback/missing/诊断，不改变编辑索引。
    [[nodiscard]] virtual FontFallbackStatus resolveWithStatus(
        const FontQuery& query, char32_t codePoint) const;
    // 字形度量（按查询尺寸换算为像素）。
    [[nodiscard]] virtual bool glyphMetrics(const FontQuery& query,
                                            char32_t codePoint,
                                            GlyphMetrics* out) const = 0;
    // 水平度量（ascent/descent 像素；占位为 0.8em/0.4em，Skia 为真实值）。
    [[nodiscard]] virtual bool horizontalMetrics(const FontQuery& query,
                                                float* ascentPx,
                                                float* descentPx) const;
    // 单 cluster shaping（默认：码点即字形 id，advance 为度量和）。
    // Skia 实现覆盖为真实 glyph id + 真实 advance；返回空表示缺字。
    [[nodiscard]] virtual std::vector<ShapedGlyph> shapeCluster(
        const FontQuery& query, const std::string& graphemeUtf8,
        std::uint32_t clusterIndex) const;
    // 系统字体查询（诊断/设置页用）。
    [[nodiscard]] virtual std::vector<std::string> availableFamilies()
        const = 0;
    // M4 review：廉价族计数（诊断字符串热路径用；默认实现走全量枚举，
    // Skia 实现覆盖为构造期缓存——避免每次布局 ~50ms 的 fontconfig
    // 枚举）。
    [[nodiscard]] virtual std::size_t familyCount() const {
        return availableFamilies().size();
    }
    // 诊断字符串（后端/族数/缺字说明；缺字体时仍可启动）。
    [[nodiscard]] virtual std::string diagnostic() const;
};

// CPU-only 确定性实现：latin/cjk/emoji 三段回退链，所有字形统一
// 0.6em advance / 0.8em ascent / 0.4em descent（与既有布局度量一致，
// 保证布局与绘制共用同一份数据时在不同后端不漂移）。
class PlaceholderFontManager final : public FontManager {
  public:
    [[nodiscard]] FontBackend backend() const override {
        return FontBackend::Placeholder;
    }
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
