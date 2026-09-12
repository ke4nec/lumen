#include "lumen/text/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

#include "lumen/text/bidi.h"

namespace lumen::text {
namespace {

float effectiveFontSize(const core::TextStyle& style) {
    return style.fontSize > 0.0F ? style.fontSize : 14.0F;
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
    std::string family{};
    BidiClass bidi{BidiClass::L};
    bool fallbackUsed{false};
    bool missing{false};
    std::vector<ShapedGlyph> glyphs{};
};

BidiClass clusterBidiClass(const std::string& grapheme) {
    for (const DecodedCodePoint& cp : decodeUtf8(grapheme)) {
        const BidiClass cls = bidiClassFor(cp.codePoint);
        if (cls != BidiClass::Neutral) {
            return cls;
        }
    }
    return BidiClass::Neutral;
}

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
    // 平局取阅读前进方向的“下一个”边界：LTR 与 RTL 都是较大 k（RTL
    // 行末边界与最左 cluster 左边缘重合时，取行末）。
    std::size_t best = 0;
    float bestDistance = std::abs(x - line.graphemeX[0]);
    for (std::size_t k = 1; k <= line.graphemeCount; ++k) {
        const float distance = std::abs(x - line.graphemeX[k]);
        if (distance <= bestDistance) {
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

TextLayoutResult TextLayout::layout(const std::string& text,
                                    const core::TextStyle& style,
                                    float maxWidth, const FontManager& fonts) {
    TextLayoutResult result;
    const float fontSize = effectiveFontSize(style);
    FontQuery query;
    query.family = style.family;
    // Keep the legacy `bold` flag and the explicit weight in one query.  A
    // default-weight bold style must select a bold face for both measurement
    // and glyph shaping.
    query.weight = static_cast<FontWeight>(
        style.bold ? std::max(style.weight, 700) : style.weight);
    query.italic = style.italic;
    query.sizePx = fontSize;
    // M1：水平度量走 FontManager（Skia 为真实 ascent/descent，占位为
    // 0.8em/0.4em）；失败时回退到既有默认值，保证可启动。
    float ascentPx = fontSize * 0.8F;
    float descentPx = fontSize * 0.4F;
    (void)fonts.horizontalMetrics(query, &ascentPx, &descentPx);
    const float multiplier = style.lineHeight > 0.0F ? style.lineHeight : 1.2F;
    // lineHeight 仍由 style 倍数派生（与 effectiveLineHeightPx 一致），
    // baseline 取真实 ascent，保证 Skia 布局不再使用占位度量。
    result.lineHeightPx = fontSize * multiplier;
    result.baseline = ascentPx;
    result.fontBackend = fonts.backend();
    result.usedPlaceholderFallback = fonts.backend() == FontBackend::Placeholder;

    const std::vector<std::size_t> boundaries = graphemeBoundaries(text);
    const std::size_t count = boundaries.size() - 1;
    result.graphemeCount = count;

    // 每 cluster shaping（含 letterSpacing；缺字回退到占位 advance）。
    std::size_t fallbackClusters = 0;
    std::size_t missingClusters = 0;
    std::vector<ClusterInfo> clusters(count);
    for (std::size_t i = 0; i < count; ++i) {
        clusters[i].text =
            text.substr(boundaries[i], boundaries[i + 1] - boundaries[i]);
        clusters[i].hardBreak = clusters[i].text == "\n" ||
                                clusters[i].text == "\r\n";
        if (clusters[i].hardBreak) {
            clusters[i].advance = 0.0F;
            clusters[i].bidi = BidiClass::Neutral;
            continue;
        }
        clusters[i].bidi = clusterBidiClass(clusters[i].text);
        std::vector<ShapedGlyph> shaped = fonts.shapeCluster(
            query, clusters[i].text, static_cast<std::uint32_t>(i));
        // 首码点的回退状态（编辑索引不随回退改变，仅记录诊断）。
        const std::vector<DecodedCodePoint> decoded =
            decodeUtf8(clusters[i].text);
        if (!decoded.empty()) {
            const FontFallbackStatus status =
                fonts.resolveWithStatus(query, decoded.front().codePoint);
            clusters[i].family = status.resolvedFamily;
            clusters[i].fallbackUsed = status.fallbackUsed;
            clusters[i].missing = status.missing || shaped.empty();
        }
        if (shaped.empty()) {
            // 缺字：占位 advance + 明确 fallback，不改变 grapheme 索引。
            clusters[i].missing = true;
            clusters[i].family = "lumen-missing";
            ShapedGlyph placeholder;
            placeholder.glyphId = decoded.empty()
                                      ? 0xFFFD
                                      : static_cast<std::uint32_t>(
                                            decoded.front().codePoint);
            placeholder.advancePx = fontSize * 0.6F;
            placeholder.xOffsetPx = 0.0F;
            placeholder.cluster = static_cast<std::uint32_t>(i);
            shaped = {placeholder};
        }
        float advancePx = 0.0F;
        for (const ShapedGlyph& glyph : shaped) {
            advancePx += glyph.advancePx;
        }
        clusters[i].advance = advancePx + style.letterSpacing;
        clusters[i].glyphs = std::move(shaped);
        if (clusters[i].fallbackUsed || clusters[i].missing) {
            ++fallbackClusters;
        }
        if (clusters[i].missing) {
            ++missingClusters;
        }
    }
    if (fonts.backend() != FontBackend::Placeholder &&
        missingClusters > 0) {
        // 只有真正缺字（无任何族覆盖）才降级为占位 advance；字体族回退
        // （fallback 到其他真实族）不是占位降级。
        result.usedPlaceholderFallback = true;
    }

    std::vector<ShapedGlyph> ellipsisGlyphs =
        fonts.shapeCluster(query, kEllipsis, 0);
    float ellipsisAdvance = 0.0F;
    for (const ShapedGlyph& glyph : ellipsisGlyphs) {
        ellipsisAdvance += glyph.advancePx;
    }
    if (ellipsisGlyphs.empty()) {
        ellipsisAdvance = fontSize * 0.6F;
    }
    ellipsisAdvance += style.letterSpacing;
    // 省略号的实际覆盖族（Skia 路径需要真实族名才能解析 typeface；
    // style.family 为空时不能用硬编码占位族名，否则 glyph 不可见）。
    const FontFallbackStatus ellipsisStatus =
        fonts.resolveWithStatus(query, 0x2026);
    const std::string ellipsisFamily =
        ellipsisStatus.resolvedFamily.empty() ? "lumen-latin"
                                              : ellipsisStatus.resolvedFamily;
    const bool ellipsisMissing =
        ellipsisStatus.missing || ellipsisGlyphs.empty();

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

    // 行几何 + 双向视觉序（M1：混合 LTR/RTL 按 UAX#9 子集重排，
    // grapheme 边界仍为编辑索引）。
    const bool baseRtl = style.direction == core::TextDirection::Rtl;
    result.rtl = baseRtl;
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
        // 逻辑索引 0..logicalCount-1；省略号为末尾的合成逻辑位。
        std::vector<float> advances(raw.count);
        std::vector<const std::string*> texts(raw.count);
        std::vector<BidiClass> classes(raw.count);
        for (std::size_t i = 0; i < raw.count; ++i) {
            advances[i] = clusters[raw.start + i].advance;
            texts[i] = &clusters[raw.start + i].text;
            classes[i] = clusters[raw.start + i].bidi;
        }
        bool hasEllipsis = false;
        if (raw.appendEllipsis) {
            advances.push_back(ellipsisAdvance);
            texts.push_back(nullptr);  // 省略号占位
            classes.push_back(BidiClass::Neutral);
            line.graphemeCount += 1;
            hasEllipsis = true;
        }
        const std::size_t logicalCount = advances.size();
        // 省略号的合成逻辑位（编辑索引连续：行尾 cluster 位）。
        const std::size_t ellipsisLogical = logicalCount - 1;

        const std::vector<BidiRun> runs =
            resolveBidiRuns(classes, baseRtl);
        const std::vector<std::size_t> order =
            visualOrder(logicalCount, runs);

        line.graphemeX.assign(logicalCount + 1, 0.0F);
        float total = 0.0F;
        for (float advance : advances) {
            total += advance;
        }
        const auto textOf = [&](std::size_t logical) -> const std::string& {
            static const std::string kEllipsisText{kEllipsis};
            return texts[logical] != nullptr ? *texts[logical]
                                             : kEllipsisText;
        };
        // 视觉序从左到右累积；logical cluster 左边缘 = 其视觉位置。
        std::vector<float> leftEdge(logicalCount, 0.0F);
        float cursor = 0.0F;
        line.visual.clear();
        for (std::size_t visualPos = 0; visualPos < logicalCount;
             ++visualPos) {
            const std::size_t logical = order[visualPos];
            leftEdge[logical] = cursor;
            cursor += advances[logical];
            line.visual += textOf(logical);
        }
        for (std::size_t logical = 0; logical < logicalCount; ++logical) {
            line.graphemeX[logical] = leftEdge[logical];
        }
        // 段落结束边界：LTR 在最右，RTL 在最左（与既有纯段行为一致）。
        line.graphemeX[logicalCount] = baseRtl ? 0.0F : total;
        line.width = total;
        maxLineWidth = std::max(maxLineWidth, total);

        // Shaped runs：视觉序中同族连续合并；xOffset 为左边缘。缺字
        // cluster 标记 placeholder，让 CPU 后端走占位字形并可诊断。
        const bool isPlaceholder =
            fonts.backend() == FontBackend::Placeholder;
        std::string runFamily;
        bool runMissing = false;
        TextLine::ShapedRun* run = nullptr;
        for (std::size_t visualPos = 0; visualPos < logicalCount;
             ++visualPos) {
            const std::size_t logical = order[visualPos];
            const bool isEllipsis =
                hasEllipsis && logical == ellipsisLogical;
            bool clusterMissing = false;
            std::string family;
            std::vector<ShapedGlyph> glyphs;
            if (isEllipsis) {
                family = ellipsisFamily;
                glyphs = ellipsisGlyphs;
                for (ShapedGlyph& glyph : glyphs) {
                    // 合成省略号的 cluster 指向行尾逻辑位。
                    glyph.cluster =
                        static_cast<std::uint32_t>(raw.start + raw.count);
                }
                if (glyphs.empty()) {
                    ShapedGlyph placeholder;
                    placeholder.glyphId = 0x2026;
                    placeholder.advancePx = ellipsisAdvance;
                    placeholder.cluster =
                        static_cast<std::uint32_t>(raw.start + raw.count);
                    glyphs = {placeholder};
                }
            } else {
                const ClusterInfo& cluster = clusters[raw.start + logical];
                family = cluster.family.empty() ? "lumen-latin"
                                                : cluster.family;
                glyphs = cluster.glyphs;
                clusterMissing = cluster.missing;
            }
            if (isEllipsis) {
                clusterMissing = ellipsisMissing;
            }
            if (run == nullptr || runFamily != family ||
                runMissing != clusterMissing) {
                line.runs.push_back(TextLine::ShapedRun{});
                run = &line.runs.back();
                run->family = family;
                run->placeholder = isPlaceholder || clusterMissing;
                runFamily = family;
                runMissing = clusterMissing;
            }
            const float base = leftEdge[logical];
            const float per = glyphs.empty()
                                  ? 0.0F
                                  : advances[logical] /
                                        static_cast<float>(glyphs.size());
            for (std::size_t g = 0; g < glyphs.size(); ++g) {
                ShapedGlyph shaped = glyphs[g];
                shaped.xOffsetPx = base + per * static_cast<float>(g);
                if (!isEllipsis) {
                    shaped.cluster = static_cast<std::uint32_t>(raw.start +
                                                                logical);
                }
                run->glyphs.push_back(shaped);
            }
        }
        result.lines.push_back(std::move(line));
    }

    result.size = core::Size{
        maxLineWidth,
        static_cast<float>(result.lines.size()) * result.lineHeightPx};
    result.fontDiagnostic =
        "backend=" + std::string(fontBackendName(fonts.backend())) +
        " families=" + std::to_string(fonts.familyCount()) +
        " fallbackClusters=" + std::to_string(fallbackClusters) +
        " missingClusters=" + std::to_string(missingClusters);
    return result;
}

bool TextLayoutCache::Key::operator<(const Key& other) const {
    if (text != other.text) {
        return text < other.text;
    }
    if (fontSize != other.fontSize) {
        return fontSize < other.fontSize;
    }
    if (bold != other.bold) {
        return bold < other.bold;
    }
    if (family != other.family) {
        return family < other.family;
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
    if (backend != other.backend) {
        return backend < other.backend;
    }
    if (manager != other.manager) {
        return std::less<const FontManager*>{}(manager, other.manager);
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
    key.bold = style.bold;
    key.family = style.family;
    key.weight = style.weight;
    key.italic = style.italic;
    key.letterSpacing = style.letterSpacing;
    key.lineHeight = style.lineHeight;
    key.maxLines = style.maxLines;
    key.overflow = style.overflow;
    key.direction = style.direction;
    key.maxWidth = maxWidth;
    key.backend = fonts.backend();
    key.manager = &fonts;

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
