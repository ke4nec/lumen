#include "lumen/text/font_manager.h"

#include "lumen/text/grapheme.h"

namespace lumen::text {
namespace {

bool isCjkCodePoint(char32_t cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) ||    // 部首/汉字
           (cp >= 0x3040 && cp <= 0x30FF) ||     // 假名
           (cp >= 0x3100 && cp <= 0x312F) ||     // 注音
           (cp >= 0xAC00 && cp <= 0xD7A3) ||     // Hangul 音节
           (cp >= 0xF900 && cp <= 0xFAFF) ||     // 兼容表意
           (cp >= 0x20000 && cp <= 0x2FA1F);     // 扩展 B-F
}

bool isEmojiCodePoint(char32_t cp) {
    return (cp >= 0x1F300 && cp <= 0x1FAFF) ||
           (cp >= 0x2600 && cp <= 0x27BF) || cp == 0x231A || cp == 0x231B ||
           (cp >= 0x1F1E6 && cp <= 0x1F1FF);
}

}  // namespace

std::string PlaceholderFontManager::resolveFamily(const FontQuery& query,
                                                  char32_t codePoint) const {
    // 请求族优先（若它本身在回退链中）；随后 latin -> cjk -> emoji。
    if (query.family == kLatinFamily || query.family == kCjkFamily ||
        query.family == kEmojiFamily) {
        return query.family;
    }
    if (isEmojiCodePoint(codePoint)) {
        return kEmojiFamily;
    }
    if (isCjkCodePoint(codePoint)) {
        return kCjkFamily;
    }
    return kLatinFamily;
}

bool PlaceholderFontManager::glyphMetrics(const FontQuery& query,
                                          char32_t codePoint,
                                          GlyphMetrics* out) const {
    if (out == nullptr) {
        return false;
    }
    // 确定性度量：三段回退共享同一套 em 度量，保证布局与绘制一致。
    if (resolveFamily(query, codePoint).empty()) {
        return false;
    }
    out->advanceEm = 0.6F;
    out->ascentEm = 0.8F;
    out->descentEm = 0.4F;
    return true;
}

std::vector<std::string> PlaceholderFontManager::availableFamilies() const {
    return {kLatinFamily, kCjkFamily, kEmojiFamily};
}

PlaceholderFontManager& PlaceholderFontManager::shared() {
    static PlaceholderFontManager instance;
    return instance;
}

// --- FontManager 默认实现（M1：不暴露 Skia 类型，可序列化 shaping） ---

FontFallbackStatus FontManager::resolveWithStatus(
    const FontQuery& query, char32_t codePoint) const {
    FontFallbackStatus status;
    status.resolvedFamily = resolveFamily(query, codePoint);
    if (status.resolvedFamily.empty()) {
        status.missing = true;
        status.fallbackUsed = false;
        status.diagnostic = "no family covers U+" +
                            std::to_string(static_cast<std::uint32_t>(codePoint));
        return status;
    }
    status.missing = false;
    status.fallbackUsed =
        !query.family.empty() && status.resolvedFamily != query.family;
    return status;
}

bool FontManager::horizontalMetrics(const FontQuery& query, float* ascentPx,
                                    float* descentPx) const {
    if (ascentPx != nullptr) {
        *ascentPx = query.sizePx > 0.0F ? query.sizePx * 0.8F : 11.2F;
    }
    if (descentPx != nullptr) {
        *descentPx = query.sizePx > 0.0F ? query.sizePx * 0.4F : 5.6F;
    }
    return true;
}

std::vector<ShapedGlyph> FontManager::shapeCluster(
    const FontQuery& query, const std::string& graphemeUtf8,
    std::uint32_t clusterIndex) const {
    if (graphemeUtf8.empty()) {
        return {};
    }
    float advancePx = 0.0F;
    char32_t firstCp = 0;
    bool first = true;
    bool anyMissing = false;
    for (const DecodedCodePoint& cp : decodeUtf8(graphemeUtf8)) {
        if (first) {
            firstCp = cp.codePoint;
            first = false;
        }
        GlyphMetrics metrics{};
        if (glyphMetrics(query, cp.codePoint, &metrics)) {
            advancePx += metrics.advanceEm * query.sizePx;
        } else {
            anyMissing = true;
        }
    }
    if (first) {
        return {};
    }
    if (anyMissing && advancePx <= 0.0F) {
        return {};
    }
    ShapedGlyph glyph;
    glyph.glyphId = static_cast<std::uint32_t>(firstCp);
    glyph.advancePx = advancePx;
    glyph.xOffsetPx = 0.0F;
    glyph.cluster = clusterIndex;
    return {glyph};
}

std::string FontManager::diagnostic() const {
    const std::vector<std::string> families = availableFamilies();
    std::string out = "backend=";
    out += fontBackendName(backend());
    out += " families=" + std::to_string(families.size());
    if (families.empty()) {
        out += " (missing system fonts, placeholder in use)";
    }
    return out;
}

}  // namespace lumen::text
