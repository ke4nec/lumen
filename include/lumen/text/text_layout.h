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
    // graphemeX[k] = 逻辑 cluster k 的起始 x；长度 = graphemeCount+1，
    // 末元素为行末边界（LTR = 行宽，RTL = 0）。
    std::vector<float> graphemeX{};
    // 行文本（视觉序；RTL 行为逻辑逆序）。
    std::string visual{};
};

struct TextLayoutResult {
    std::vector<TextLine> lines{};
    core::Size size{};
    // 首行 baseline 距顶部。
    float baseline{0.0F};
    float lineHeightPx{0.0F};
    bool ellipsized{false};
    // 段落方向（命中测试平局时按阅读方向取边界）。
    bool rtl{false};
    // grapheme 总数（含被 ellipsis 裁掉的）。
    std::size_t graphemeCount{0};

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

  private:
    // 单 cluster 的像素 advance（含 letterSpacing）。
    [[nodiscard]] static float graphemeAdvance(const std::string& grapheme,
                                               const core::TextStyle& style,
                                               const FontManager& fonts);
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
        int weight{400};
        bool italic{false};
        float letterSpacing{0.0F};
        float lineHeight{0.0F};
        std::size_t maxLines{0};
        core::TextOverflow overflow{core::TextOverflow::Clip};
        core::TextDirection direction{core::TextDirection::Ltr};
        float maxWidth{0.0F};

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
