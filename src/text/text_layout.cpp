#include "lumen/text/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace lumen::text {
namespace {

float effectiveFontSize(const core::TextStyle& style) {
    return style.fontSize > 0.0F ? style.fontSize : 14.0F;
}

float effectiveLineHeightPx(const core::TextStyle& style) {
    const float multiplier = style.lineHeight > 0.0F ? style.lineHeight : 1.2F;
    return effectiveFontSize(style) * multiplier;
}

bool isWhitespaceCluster(const std::string& grapheme) {
    if (grapheme.empty()) {
        return false;
    }
    switch (grapheme.front()) {
        case ' ':
        case '\t':
            return true;
        default:
            return grapheme == "\xE3\x80\x80";  // 全角空格
    }
}

// CJK/emoji cluster 之后允许直接断行（词间无空格的语言）。
bool isBreakOpportunityCluster(const std::string& grapheme) {
    if (grapheme.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(grapheme.front());
    if (first < 0x80U) {
        return false;
    }
    // 非 ASCII 且非空白：按 CJK/emoji 可断近似（组合序列以首字节分类）。
    return !isWhitespaceCluster(grapheme);
}

struct ClusterInfo {
    std::string text{};
    float advance{0.0F};
    bool hardBreak{false};  // '\n'
};

}  // namespace

std::size_t TextLayoutResult::positionToGrapheme(float x, float y) const {
    if (lines.empty()) {
        return 0;
    }
    std::size_t lineIndex = 0;
    if (lineHeightPx > 0.0F && lines.size() > 1) {
        const auto picked = static_cast<std::size_t>(
            std::max(0.0F, std::floor(y / lineHeightPx)));
        lineIndex = std::min(picked, lines.size() - 1);
    }
    const TextLine& line = lines[lineIndex];
    if (line.graphemeX.empty()) {
        return line.startGrapheme;
    }
    // 最近边界：在所有 cluster 边界（graphemeX[0..count]，含行末）中找
    // 距 x 最近的；RTL 行的边界集合相同（位置逆序），此算法方向无关。
    // 平局按阅读方向取“下一个”边界：LTR 取较大 k，RTL 取较小 k。
    std::size_t best = 0;
    float bestDistance = std::abs(x - line.graphemeX[0]);
    for (std::size_t k = 1; k <= line.graphemeCount; ++k) {
        const float distance = std::abs(x - line.graphemeX[k]);
        const bool wins =
            distance < bestDistance ||
            (!rtl && distance == bestDistance);
        if (wins) {
            bestDistance = distance;
            best = k;
        }
    }
    return line.startGrapheme + best;
}

float TextLayoutResult::graphemeToX(std::size_t graphemeIndex,
                                    std::size_t* lineIndex) const {
    if (lines.empty()) {
        return 0.0F;
    }
    const std::size_t index = std::min(graphemeIndex, this->graphemeCount);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const TextLine& line = lines[i];
        if (index >= line.startGrapheme &&
            index <= line.startGrapheme + line.graphemeCount) {
            if (lineIndex != nullptr) {
                *lineIndex = i;
            }
            const std::size_t local = index - line.startGrapheme;
            return line.graphemeX[std::min(local, line.graphemeCount)];
        }
    }
    if (lineIndex != nullptr) {
        *lineIndex = lines.size() - 1;
    }
    const TextLine& last = lines.back();
    return last.graphemeX.back();
}

float TextLayout::graphemeAdvance(const std::string& grapheme,
                                  const core::TextStyle& style,
                                  const FontManager& fonts) {
    const float fontSize = effectiveFontSize(style);
    FontQuery query;
    query.family = style.family;
    query.weight = static_cast<FontWeight>(style.weight);
    query.italic = style.italic;
    query.sizePx = fontSize;
    float advance = 0.0F;
    for (const DecodedCodePoint& cp : decodeUtf8(grapheme)) {
        GlyphMetrics metrics{};
        if (fonts.glyphMetrics(query, cp.codePoint, &metrics)) {
            advance += metrics.advanceEm * fontSize;
        }
    }
    return advance + style.letterSpacing;
}

TextLayoutResult TextLayout::layout(const std::string& text,
                                    const core::TextStyle& style,
                                    float maxWidth, const FontManager& fonts) {
    TextLayoutResult result;
    const float fontSize = effectiveFontSize(style);
    result.lineHeightPx = effectiveLineHeightPx(style);
    result.baseline = fontSize * 0.8F;  // placeholder ascent 0.8em

    const std::vector<std::size_t> boundaries = graphemeBoundaries(text);
    const std::size_t count = boundaries.size() - 1;
    result.graphemeCount = count;

    // 每 cluster 度量。
    std::vector<ClusterInfo> clusters(count);
    for (std::size_t i = 0; i < count; ++i) {
        clusters[i].text =
            text.substr(boundaries[i], boundaries[i + 1] - boundaries[i]);
        clusters[i].advance = graphemeAdvance(clusters[i].text, style, fonts);
        clusters[i].hardBreak = clusters[i].text == "\n" ||
                                clusters[i].text == "\r\n";
    }

    const float ellipsisAdvance =
        graphemeAdvance(kEllipsis, style, fonts);

    // 硬换行分段。
    std::vector<std::pair<std::size_t, std::size_t>> segments;
    std::size_t segmentStart = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (clusters[i].hardBreak) {
            segments.emplace_back(segmentStart, i - segmentStart);
            segmentStart = i + 1;
        }
    }
    segments.emplace_back(segmentStart, count - segmentStart);

    // 贪心换行；maxWidth <= 0 时不换行（仅硬换行）。
    struct RawLine {
        std::size_t start{0};
        std::size_t count{0};
        bool appendEllipsis{false};
    };
    std::vector<RawLine> rawLines;
    const bool wrap = maxWidth > 0.0F;
    for (const auto& [start, length] : segments) {
        if (length == 0) {
            rawLines.push_back(RawLine{start, 0});
            continue;
        }
        std::size_t lineStart = start;
        float width = 0.0F;
        std::size_t lastBreak = start;  // 允许断行的下一 cluster 起点
        for (std::size_t i = start; i < start + length; ++i) {
            const float advance = clusters[i].advance;
            if (wrap && width + advance > maxWidth && i > lineStart) {
                if (lastBreak > lineStart) {
                    rawLines.push_back(RawLine{lineStart, lastBreak - lineStart});
                    lineStart = lastBreak;
                    width = 0.0F;
                    // 丢弃行首空白（软换行标准行为）。
                    while (lineStart < start + length &&
                           isWhitespaceCluster(clusters[lineStart].text) &&
                           !clusters[lineStart].hardBreak) {
                        ++lineStart;
                    }
                    if (lineStart > i) {
                        // 宽 cluster 单独成行后仍超出：继续累积。
                        width = 0.0F;
                        for (std::size_t k = i; k < lineStart; ++k) {
                            width += clusters[k].advance;
                        }
                        continue;
                    }
                } else {
                    rawLines.push_back(RawLine{lineStart, i - lineStart});
                    lineStart = i;
                    width = 0.0F;
                }
            }
            width += advance;
            if (isWhitespaceCluster(clusters[i].text) ||
                isBreakOpportunityCluster(clusters[i].text)) {
                lastBreak = i + 1;
            }
        }
        rawLines.push_back(
            RawLine{lineStart, start + length - lineStart});
    }

    // maxLines 截断。
    const bool limitLines = style.maxLines > 0 && rawLines.size() > style.maxLines;
    if (limitLines) {
        rawLines.resize(style.maxLines);
        const bool ellipsize =
            style.overflow == core::TextOverflow::Ellipsis ||
            style.overflow == core::TextOverflow::Fade;
        if (ellipsize) {
            result.ellipsized = true;
            RawLine& last = rawLines.back();
            float width = 0.0F;
            for (std::size_t i = 0; i < last.count; ++i) {
                width += clusters[last.start + i].advance;
            }
            std::size_t kept = last.count;
            while (kept > 0 && width + ellipsisAdvance > maxWidth) {
                width -= clusters[last.start + kept - 1].advance;
                --kept;
            }
            last.count = kept;
            last.appendEllipsis = true;
        }
    }

    // 行几何 + RTL 视觉序。
    const bool rtl = style.direction == core::TextDirection::Rtl;
    result.rtl = rtl;
    float maxLineWidth = 0.0F;
    for (const RawLine& raw : rawLines) {
        TextLine line;
        line.startGrapheme = raw.start;
        line.graphemeCount = raw.count;
        line.startByte = boundaries[raw.start];
        const std::size_t endCluster = raw.start + raw.count;
        line.byteLength =
            (raw.count == 0
                 ? 0
                 : boundaries[endCluster] - boundaries[raw.start]);

        // 有效 cluster 序列：文本 cluster +（可选）合成的省略号。
        std::vector<float> advances(raw.count);
        std::vector<const std::string*> texts(raw.count);
        for (std::size_t i = 0; i < raw.count; ++i) {
            advances[i] = clusters[raw.start + i].advance;
            texts[i] = &clusters[raw.start + i].text;
        }
        if (raw.appendEllipsis) {
            advances.push_back(ellipsisAdvance);
            texts.push_back(nullptr);  // 省略号占位
            line.graphemeCount += 1;
        }
        const std::size_t visualCount = advances.size();

        line.graphemeX.assign(visualCount + 1, 0.0F);
        float total = 0.0F;
        for (std::size_t i = 0; i < visualCount; ++i) {
            total += advances[i];
        }
        const auto textOf = [&](std::size_t i) -> const std::string& {
            static const std::string kEllipsisText{kEllipsis};
            return texts[i] != nullptr ? *texts[i] : kEllipsisText;
        };
        if (rtl) {
            // 视觉逆序：逻辑 cluster k 起始 x = 其右侧 cluster 宽度和。
            float running = total;
            for (std::size_t i = 0; i < visualCount; ++i) {
                running -= advances[i];
                line.graphemeX[i] = running;
            }
            line.graphemeX[visualCount] = 0.0F;  // 行末边界在最左。
            for (std::size_t i = visualCount; i-- > 0;) {
                line.visual += textOf(i);
            }
        } else {
            float running = 0.0F;
            for (std::size_t i = 0; i < visualCount; ++i) {
                line.graphemeX[i] = running;
                running += advances[i];
                line.visual += textOf(i);
            }
            line.graphemeX[visualCount] = total;
        }
        line.width = total;
        maxLineWidth = std::max(maxLineWidth, total);
        result.lines.push_back(std::move(line));
    }

    result.size = core::Size{
        maxLineWidth,
        static_cast<float>(result.lines.size()) * result.lineHeightPx};
    return result;
}

bool TextLayoutCache::Key::operator<(const Key& other) const {
    if (text != other.text) {
        return text < other.text;
    }
    if (fontSize != other.fontSize) {
        return fontSize < other.fontSize;
    }
    if (weight != other.weight) {
        return weight < other.weight;
    }
    if (italic != other.italic) {
        return italic < other.italic;
    }
    if (letterSpacing != other.letterSpacing) {
        return letterSpacing < other.letterSpacing;
    }
    if (lineHeight != other.lineHeight) {
        return lineHeight < other.lineHeight;
    }
    if (maxLines != other.maxLines) {
        return maxLines < other.maxLines;
    }
    if (overflow != other.overflow) {
        return overflow < other.overflow;
    }
    if (direction != other.direction) {
        return direction < other.direction;
    }
    return maxWidth < other.maxWidth;
}

TextLayoutResult TextLayoutCache::compute(const std::string& text,
                                          const core::TextStyle& style,
                                          float maxWidth,
                                          const FontManager& fonts) {
    Key key;
    key.text = text;
    key.fontSize = style.fontSize;
    key.weight = style.weight;
    key.italic = style.italic;
    key.letterSpacing = style.letterSpacing;
    key.lineHeight = style.lineHeight;
    key.maxLines = style.maxLines;
    key.overflow = style.overflow;
    key.direction = style.direction;
    key.maxWidth = maxWidth;

    const auto it = entries_.find(key);
    if (it != entries_.end()) {
        stats_.hits += 1;
        it->second.lastUse = ++useCounter_;
        return it->second.result;
    }
    stats_.misses += 1;
    TextLayoutResult result = TextLayout::layout(text, style, maxWidth, fonts);
    if (entries_.size() >= kDefaultCapacity) {
        // 淘汰最旧。
        auto oldest = entries_.begin();
        for (auto entry = entries_.begin(); entry != entries_.end(); ++entry) {
            if (entry->second.lastUse < oldest->second.lastUse) {
                oldest = entry;
            }
        }
        entries_.erase(oldest);
    }
    entries_.emplace(key, Entry{result, ++useCounter_});
    stats_.entries = entries_.size();
    return result;
}

void TextLayoutCache::clear() {
    entries_.clear();
    stats_.entries = 0;
}

}  // namespace lumen::text
