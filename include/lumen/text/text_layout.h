#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/text/font_manager.h"
#include "lumen/text/grapheme.h"

namespace lumen::text {

// v0.3 阶段8B (plan §3.2): 文本布局契约。
//
// 按约束测量、换行、ellipsis、baseline、字形位置与命中测试都出自同一
// 份 TextLayout；布局（lumen-layout）与绘制（painter）必须共用它，否则
// 光标、选区和绘制宽度会在不同后端漂移。逻辑坐标；与 Renderer 像素坐
// 标由 deviceScale 分隔。

struct TextLine {
    // 逻辑 cluster 范围（相对整段文本）。
    std::size_t startGrapheme{0};
    std::size_t graphemeCount{0};
    // 字节范围（同上）。
    std::size_t startByte{0};
    std::size_t byteLength{0};
    float width{0.0F};
    // graphemeX[k] = 逻辑 cluster k 的左边缘 x；长度 = graphemeCount+1，
    // 末元素为段落结束边界（LTR = 行宽，RTL = 0，混合按段落方向）。
    std::vector<float> graphemeX{};
    // 行文本（视觉序；RTL/混合按双向重排）。
    std::string visual{};
    // 可序列化的 shaped runs（视觉序分组；生命周期由本结果拥有，
    // RenderCommand 只接收文本绘制数据，不持有本结构）。
    struct ShapedRun {
        std::string family{};
        bool placeholder{true};
        std::vector<ShapedGlyph> glyphs{};

        bool operator==(const ShapedRun&) const = default;
    };
    std::vector<ShapedRun> runs{};
};

struct TextLayoutResult {
    std::vector<TextLine> lines{};
    core::Size size{};
    // 可见文本各字体 ascent 的最大值；所有行共享同一基线偏移。
    float baseline{0.0F};
    // 相邻行原点/基线的步进，严格使用 TextStyle.lineHeight 倍数。
    float lineHeightPx{0.0F};
    // 单行绘制/编辑框：至少容纳 max(ascent) + max(descent)。紧行高下
    // 行框可以重叠；size.height 包含最后一行完整行框，不裁掉下行部。
    float lineBoxHeightPx{0.0F};
    bool ellipsized{false};
    // 段落方向（命中测试平局时按阅读方向取边界）。
    bool rtl{false};
    // grapheme 总数（含被 ellipsis 裁掉的）。
    std::size_t graphemeCount{0};
    // M1：布局使用的字体事实（与 Skia renderer 共享同一份结果时，
    // Skia 路径为 Skia 后端；CPU 为占位；缺字体时 fallback 明确报告）。
    FontBackend fontBackend{FontBackend::Placeholder};
    bool usedPlaceholderFallback{true};
    std::string fontDiagnostic{};

    // 命中测试：x（相对布局原点）→ 最近 cluster 边界索引。多行文本按
    // 行高映射到行。
    [[nodiscard]] std::size_t positionToGrapheme(float x, float y) const;
    // cluster 边界索引（可等于 graphemeCount）→ x 偏移与所在行。
    [[nodiscard]] float graphemeToX(std::size_t graphemeIndex,
                                    std::size_t* lineIndex = nullptr) const;
};

class TextLayout {
  public:
    // 布局一段文本。maxWidth <= 0 表示不换行（单行度量）。style.maxLines
    // 与 overflow 决定行数上限与截断；'\n' 为硬换行。
    [[nodiscard]] static TextLayoutResult layout(const std::string& text,
                                                 const core::TextStyle& style,
                                                 float maxWidth,
                                                 const FontManager& fonts);

    // ellipsis 字符。
    static constexpr const char* kEllipsis = "\xE2\x80\xA6";  // …
};

// 布局缓存（plan §3.2 布局缓存 + 命中率）：键为文本/样式/宽度。缓存返回
// 拷贝，值不可变，UI 线程独占。
class TextLayoutCache {
  public:
    struct Stats {
        std::uint64_t hits{0};
        std::uint64_t misses{0};
        std::size_t entries{0};
    };

    [[nodiscard]] TextLayoutResult compute(const std::string& text,
                                           const core::TextStyle& style,
                                           float maxWidth,
                                           const FontManager& fonts);
    void clear();
    [[nodiscard]] const Stats& stats() const { return stats_; }

    // 默认容量（条目数）；超限按最旧淘汰。
    static constexpr std::size_t kDefaultCapacity = 256;

  private:
    struct Key {
        std::string text{};
        float fontSize{0.0F};
        bool bold{false};
        std::string family{};
        int weight{400};
        bool italic{false};
        float letterSpacing{0.0F};
        float lineHeight{0.0F};
        std::size_t maxLines{0};
        core::TextOverflow overflow{core::TextOverflow::Clip};
        core::TextDirection direction{core::TextDirection::Ltr};
        float maxWidth{0.0F};
        // M1：不同字体后端度量不同，缓存键必须区分后端。
        FontBackend backend{FontBackend::Placeholder};
        // Two managers can share a backend while resolving different font
        // sets (for example, separate test or platform instances).
        const FontManager* manager{nullptr};

        [[nodiscard]] bool operator<(const Key& other) const;
    };

    struct Entry {
        TextLayoutResult result{};
        std::uint64_t lastUse{0};
    };

    std::map<Key, Entry> entries_{};
    std::uint64_t useCounter_{0};
    Stats stats_{};
};

}  // namespace lumen::text
