#include "lumen/text/grapheme.h"

#include <algorithm>

namespace lumen::text {
namespace {

bool isContinuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

// --- 组合类/并入前一 cluster 的码点（扩展 grapheme 近似区段表） ---

bool isCombiningMark(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) ||   // Combining Diacritical Marks
           (cp >= 0x0483 && cp <= 0x0489) ||    // Cyrillic
           (cp >= 0x0591 && cp <= 0x05BD) ||    // Hebrew points
           cp == 0x05BF || cp == 0x05C1 || cp == 0x05C2 ||
           (cp >= 0x05C4 && cp <= 0x05C5) || cp == 0x05C7 ||
           (cp >= 0x0610 && cp <= 0x061A) ||    // Arabic marks
           (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
           (cp >= 0x06D6 && cp <= 0x06DC) ||
           (cp >= 0x06DF && cp <= 0x06E4) ||
           (cp >= 0x06E7 && cp <= 0x06E8) ||
           (cp >= 0x06EA && cp <= 0x06ED) || cp == 0x0711 ||
           (cp >= 0x0730 && cp <= 0x074A) ||
           (cp >= 0x07A6 && cp <= 0x07B0) ||
           (cp >= 0x07EB && cp <= 0x07F3) ||
           (cp >= 0x0816 && cp <= 0x0819) ||
           (cp >= 0x081B && cp <= 0x0823) ||
           (cp >= 0x0825 && cp <= 0x0827) ||
           (cp >= 0x0829 && cp <= 0x082D) ||
           (cp >= 0x0859 && cp <= 0x085B) ||
           (cp >= 0x0E31 && cp <= 0x0E3A) ||   // Thai
           (cp >= 0x0EB1 && cp <= 0x0EBC) ||   // Lao
           (cp >= 0x0F71 && cp <= 0x0F84) ||   // Tibetan
           (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) ||
           (cp >= 0x20D0 && cp <= 0x20F0) ||
           cp == 0x3099 || cp == 0x309A ||      // Kana voicing
           (cp >= 0xFE20 && cp <= 0xFE2F);
}

bool isVariationSelector(char32_t cp) {
    return (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0x180B && cp <= 0x180D);
}

bool isEmojiModifier(char32_t cp) {
    return cp >= 0x1F3FB && cp <= 0x1F3FF;  // Fitzpatrick skin tones
}

bool isRegionalIndicator(char32_t cp) {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

bool isZeroWidthJoiner(char32_t cp) { return cp == 0x200D; }

bool isHangulL(char32_t cp) { return cp >= 0x1100 && cp <= 0x115F; }
bool isHangulV(char32_t cp) { return cp >= 0x1160 && cp <= 0x11A7; }
bool isHangulT(char32_t cp) { return cp >= 0x11A8 && cp <= 0x11FF; }

bool isEmojiPresentationBase(char32_t cp) {
    // 常见 emoji 基座（ZWJ 序列两侧允许出现的成分）。
    return (cp >= 0x1F300 && cp <= 0x1FAFF) ||
           (cp >= 0x2600 && cp <= 0x27BF) || cp == 0x231A || cp == 0x231B ||
           cp == 0x200D;
}

bool isAsciiWordChar(char32_t cp) {
    return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
           (cp >= '0' && cp <= '9');
}

// cluster 分类（词边界用）：ASCII 词字符 / 其他可视频符（CJK、emoji、
// 假名等各自成词）/ 空白 / 标点。
enum class ClusterClass { AsciiWord, Symbol, Whitespace, Punctuation };

ClusterClass classifyCluster(char32_t first) {
    if (isAsciiWordChar(first)) {
        return ClusterClass::AsciiWord;
    }
    if (first == ' ' || first == '\t' || first == '\n' || first == '\r' ||
        first == 0x3000) {
        return ClusterClass::Whitespace;
    }
    if ((first >= 0x2E80 && first <= 0x9FFF) ||   // CJK 部首/汉字
        (first >= 0x3040 && first <= 0x30FF) ||   // 假名
        (first >= 0xAC00 && first <= 0xD7A3) ||   // Hangul 音节
        (first >= 0x1F300 && first <= 0x1FAFF) || // emoji
        (first >= 0x2600 && first <= 0x27BF)) {
        return ClusterClass::Symbol;
    }
    return ClusterClass::Punctuation;
}

}  // namespace

Utf8Validation validateUtf8(const std::string& text) {
    Utf8Validation result;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        char32_t value = 0;
        if (byte < 0x80U) {
            length = 1;
            value = byte;
        } else if ((byte & 0xE0U) == 0xC0U) {
            length = 2;
            value = byte & 0x1FU;
        } else if ((byte & 0xF0U) == 0xE0U) {
            length = 3;
            value = byte & 0x0FU;
        } else if ((byte & 0xF8U) == 0xF0U) {
            length = 4;
            value = byte & 0x07U;
        } else {
            result.valid = false;
            result.errorByteOffset = i;
            result.description = "invalid lead byte";
            return result;
        }
        if (i + length > text.size()) {
            result.valid = false;
            result.errorByteOffset = i;
            result.description = "truncated sequence";
            return result;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const auto cont = static_cast<unsigned char>(text[i + k]);
            if (!isContinuation(cont)) {
                result.valid = false;
                result.errorByteOffset = i + k;
                result.description = "expected continuation byte";
                return result;
            }
            value = (value << 6) | (cont & 0x3FU);
        }
        // 超长编码 / 代理区 / 越界。
        const char32_t kMinCodePoint[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (value < kMinCodePoint[length] || value > 0x10FFFF ||
            (value >= 0xD800 && value <= 0xDFFF)) {
            result.valid = false;
            result.errorByteOffset = i;
            result.description = "invalid encoded code point";
            return result;
        }
        i += length;
    }
    return result;
}

std::vector<DecodedCodePoint> decodeUtf8(const std::string& text) {
    std::vector<DecodedCodePoint> decoded;
    decoded.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        char32_t value = byte;
        if ((byte & 0x80U) != 0U) {
            if ((byte & 0xE0U) == 0xC0U) {
                length = 2;
                value = byte & 0x1FU;
            } else if ((byte & 0xF0U) == 0xE0U) {
                length = 3;
                value = byte & 0x0FU;
            } else if ((byte & 0xF8U) == 0xF0U) {
                length = 4;
                value = byte & 0x07U;
            } else {
                decoded.push_back(DecodedCodePoint{0xFFFD, 1});
                ++i;
                continue;
            }
            bool ok = i + length <= text.size();
            for (std::size_t k = 1; ok && k < length; ++k) {
                const auto cont = static_cast<unsigned char>(text[i + k]);
                if (!isContinuation(cont)) {
                    ok = false;
                    break;
                }
                value = (value << 6) | (cont & 0x3FU);
            }
            if (!ok) {
                decoded.push_back(DecodedCodePoint{0xFFFD, 1});
                ++i;
                continue;
            }
        }
        decoded.push_back(DecodedCodePoint{value, length});
        i += length;
    }
    return decoded;
}

std::vector<std::size_t> graphemeBoundaries(const std::string& text) {
    const std::vector<DecodedCodePoint> decoded = decodeUtf8(text);
    std::vector<std::size_t> boundaries;
    boundaries.reserve(decoded.size() + 1);
    boundaries.push_back(0);
    if (decoded.empty()) {
        return boundaries;
    }

    std::size_t byte = 0;
    // 前一可见成分的状态。
    char32_t prev = 0;
    bool prevIsRegional = false;
    int regionalRun = 0;

    for (std::size_t i = 0; i < decoded.size(); ++i) {
        const char32_t cp = decoded[i].codePoint;
        bool boundary = true;  // i 与前一码点之间是否开新 cluster
        if (i > 0) {
            // CR LF 合一。
            if (prev == '\r' && cp == '\n') {
                boundary = false;
            } else if (isCombiningMark(cp)) {
                boundary = false;
            } else if (isVariationSelector(cp)) {
                boundary = false;
            } else if (isEmojiModifier(cp) && isEmojiPresentationBase(prev)) {
                boundary = false;
            } else if (isZeroWidthJoiner(cp)) {
                // GB9：ZWJ 并入前一 cluster（不断在 ZWJ 之前）。
                boundary = false;
            } else if (isZeroWidthJoiner(prev)) {
                // ZWJ 把下一个成分并入（emoji ZWJ 序列，GB11）。
                boundary = false;
            } else if (isRegionalIndicator(cp) && prevIsRegional &&
                       regionalRun % 2 == 1) {
                // 区域指示符两两配对（旗帜）。
                boundary = false;
            } else if (isHangulV(cp) && (isHangulL(prev) || isHangulV(prev))) {
                boundary = false;
            } else if (isHangulT(cp) &&
                       (isHangulV(prev) || isHangulT(prev))) {
                boundary = false;
            }
        }
        if (boundary && i > 0) {
            boundaries.push_back(byte);
            regionalRun = 0;
        }
        if (isRegionalIndicator(cp)) {
            regionalRun += 1;
        }
        prevIsRegional = isRegionalIndicator(cp);
        prev = cp;
        byte += decoded[i].bytes;
    }
    boundaries.push_back(text.size());
    // 去重（空串防御；正常路径不产生重复）。
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    return boundaries;
}

std::size_t graphemeCount(const std::string& text) {
    return graphemeBoundaries(text).size() - 1;
}

std::string graphemeSubstring(const std::string& text, std::size_t beginGrapheme,
                              std::size_t endGrapheme) {
    const std::vector<std::size_t> boundaries = graphemeBoundaries(text);
    const std::size_t count = boundaries.size() - 1;
    const std::size_t begin = std::min(beginGrapheme, count);
    const std::size_t end = std::min(std::max(endGrapheme, begin), count);
    return text.substr(boundaries[begin], boundaries[end] - boundaries[begin]);
}

std::size_t graphemeByteOffset(const std::string& text,
                               std::size_t graphemeIndex) {
    const std::vector<std::size_t> boundaries = graphemeBoundaries(text);
    return boundaries[std::min(graphemeIndex, boundaries.size() - 1)];
}

std::size_t graphemeIndexOfByte(const std::string& text,
                                std::size_t byteOffset) {
    const std::vector<std::size_t> boundaries = graphemeBoundaries(text);
    const std::size_t clamped = std::min(byteOffset, text.size());
    // 找到 <= clamped 的最大边界；落在 cluster 内部时归入该 cluster。
    std::size_t index = 0;
    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
        if (boundaries[i] <= clamped) {
            index = i;
        } else {
            break;
        }
    }
    return index;
}

std::size_t wordRangeContaining(const std::string& text,
                                std::size_t graphemeIndex,
                                std::size_t* endGrapheme) {
    const std::vector<std::size_t> boundaries = graphemeBoundaries(text);
    const std::size_t count = boundaries.size() - 1;
    if (count == 0) {
        if (endGrapheme != nullptr) {
            *endGrapheme = 0;
        }
        return 0;
    }
    const std::vector<DecodedCodePoint> decoded = decodeUtf8(text);
    // cluster 首字节 → 该 cluster 的首 code point（按字节游标顺序查找）。
    const auto firstCp = [&](std::size_t i) {
        const std::size_t beginByte = boundaries[i];
        std::size_t cursor = 0;
        for (const auto& cp : decoded) {
            if (cursor >= beginByte) {
                return cp.codePoint;
            }
            cursor += cp.bytes;
        }
        return static_cast<char32_t>(0);
    };
    const std::size_t index = std::min(graphemeIndex, count - 1);
    const ClusterClass klass = classifyCluster(firstCp(index));
    if (klass == ClusterClass::Whitespace || klass == ClusterClass::Punctuation) {
        // 空白/标点：双击退化为单 cluster 选中。
        if (endGrapheme != nullptr) {
            *endGrapheme = index + 1;
        }
        return index;
    }
    std::size_t begin = index;
    std::size_t end = index + 1;
    if (klass == ClusterClass::AsciiWord) {
        while (begin > 0 &&
               classifyCluster(firstCp(begin - 1)) == ClusterClass::AsciiWord) {
            --begin;
        }
        while (end < count &&
               classifyCluster(firstCp(end)) == ClusterClass::AsciiWord) {
            ++end;
        }
    }
    // Symbol（CJK/emoji）单个 cluster 即词。
    if (endGrapheme != nullptr) {
        *endGrapheme = end;
    }
    return begin;
}

}  // namespace lumen::text
