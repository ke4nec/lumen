#include "lumen/text/font_manager.h"

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

}  // namespace lumen::text
