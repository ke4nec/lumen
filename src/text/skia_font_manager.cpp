// M1：Skia-backed FontManager（桌面正式字体/回退/shaping）。
//
// 公共头文件不暴露 Skia 类型（pimpl）；本文件仅在 LUMEN_ENABLE_SKIA 时
// 编入 lumen-text。CPU-only 构建由同名存根提供工厂（返回 nullptr + 诊断）。

#include "lumen/text/skia_font_manager.h"

#ifdef LUMEN_HAS_SKIA_TEXT

#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#elif defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#endif

#include "lumen/text/grapheme.h"

namespace lumen::text {
namespace {

SkFontStyle toSkStyle(const FontQuery& query) {
    const int weight = std::clamp(static_cast<int>(query.weight), 100, 900);
    const SkFontStyle::Slant slant =
        query.italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant;
    return SkFontStyle(weight, SkFontStyle::kNormal_Width, slant);
}

std::string skStringToStd(const SkString& s) {
    return std::string(s.c_str(), s.size());
}

}  // namespace

struct SkiaFontManager::Impl {
    sk_sp<SkFontMgr> mgr{};
    std::string initDiagnostic{};
    mutable std::map<std::string, sk_sp<SkTypeface>> faceCache{};
    // M4 review：按码点回退解析缓存（matchFamilyStyleCharacter 一次
    // ~1ms 的 fontconfig 查询；文本场景重复字符多，未缓存时每字符
    // 2 次 → 每次布局数十毫秒）。
    mutable std::map<std::uint64_t, sk_sp<SkTypeface>> charFaceCache{};
    mutable std::mutex mutex{};
    // M4 review：族计数缓存（fontconfig 全量枚举 ~50ms，诊断字符串的
    // 热路径不得反复触发；构造后首次查询时计算一次）。
    mutable std::optional<std::size_t> cachedFamilyCount{};

    [[nodiscard]] std::string cacheKey(const FontQuery& query,
                                       const std::string& family) const {
        return family + "|" + std::to_string(static_cast<int>(query.weight)) +
               "|" + (query.italic ? "i" : "n");
    }

    sk_sp<SkTypeface> typefaceForFamily(const FontQuery& query,
                                        const std::string& family) const {
        if (!mgr) {
            return nullptr;
        }
        const std::string key = cacheKey(query, family);
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto it = faceCache.find(key);
            if (it != faceCache.end()) {
                return it->second;
            }
        }
        const char* name = family.empty() ? nullptr : family.c_str();
        sk_sp<SkTypeface> face = mgr->matchFamilyStyle(name, toSkStyle(query));
        std::lock_guard<std::mutex> lock(mutex);
        faceCache.emplace(key, face);
        return face;
    }

    [[nodiscard]] static std::uint64_t charCacheKey(const FontQuery& query,
                                                    char32_t cp) {
        // 键折叠：cp(21bit) | italic(1) | weight(12) | family 哈希折叠
        // 到高位；冲突概率可忽略（族名集合小）。
        const std::uint64_t familyHash = static_cast<std::uint64_t>(
            std::hash<std::string>{}(query.family) & 0xFFFF'FFF8ULL);
        return familyHash |
               (static_cast<std::uint64_t>(cp) << 32) |
               (static_cast<std::uint64_t>(
                    std::clamp(static_cast<int>(query.weight), 100, 900))
                << 21) |
               (query.italic ? 2ULL : 0ULL) |
               1ULL;
    }

    sk_sp<SkTypeface> typefaceForChar(const FontQuery& query,
                                      const std::string& family,
                                      char32_t cp) const {
        if (!mgr) {
            return nullptr;
        }
        const std::uint64_t key = charCacheKey(query, cp);
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto it = charFaceCache.find(key);
            if (it != charFaceCache.end()) {
                return it->second;  // 未命中覆盖族时为空（负缓存）。
            }
        }
        // 请求族优先：命中且覆盖码点则直接用。
        if (!family.empty()) {
            sk_sp<SkTypeface> requested =
                typefaceForFamily(query, family);
            if (requested &&
                requested->unicharToGlyph(
                    static_cast<SkUnichar>(cp)) != 0) {
                std::lock_guard<std::mutex> lock(mutex);
                charFaceCache.emplace(key, requested);
                return requested;
            }
        }
        const char* name = family.empty() ? nullptr : family.c_str();
        sk_sp<SkTypeface> matched = mgr->matchFamilyStyleCharacter(
            name, toSkStyle(query), nullptr, 0,
            static_cast<SkUnichar>(cp));
        std::lock_guard<std::mutex> lock(mutex);
        charFaceCache.emplace(key, matched);  // 负缓存：缺字也记。
        return matched;
    }
};

SkiaFontManager::SkiaFontManager() : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
    impl_->mgr = SkFontMgr_New_GDI();
    impl_->initDiagnostic =
        impl_->mgr ? "Skia GDI FontMgr" : "GDI FontMgr creation failed";
#elif defined(__linux__)
    impl_->mgr = SkFontMgr_New_FontConfig(nullptr);
    impl_->initDiagnostic =
        impl_->mgr ? "Skia FontConfig FontMgr" : "FontConfig creation failed";
#elif defined(__APPLE__)
    impl_->mgr = SkFontMgr_New_CoreText(nullptr);
    impl_->initDiagnostic =
        impl_->mgr ? "Skia CoreText FontMgr"
                   : "CoreText FontMgr creation failed";
#else
    impl_->initDiagnostic = "no platform FontMgr port for this target";
#endif
    if (impl_->mgr && impl_->mgr->countFamilies() == 0) {
        impl_->initDiagnostic += " (no families enumerated)";
    }
}

SkiaFontManager::~SkiaFontManager() = default;

std::string SkiaFontManager::resolveFamily(
    const FontQuery& query, char32_t codePoint) const {
    const FontFallbackStatus status = resolveWithStatus(query, codePoint);
    return status.missing ? std::string{} : status.resolvedFamily;
}

FontFallbackStatus SkiaFontManager::resolveWithStatus(
    const FontQuery& query, char32_t codePoint) const {
    FontFallbackStatus status;
    if (!impl_->mgr) {
        status.missing = true;
        status.diagnostic = "skia FontMgr unavailable; " + impl_->initDiagnostic;
        return status;
    }
    sk_sp<SkTypeface> face =
        impl_->typefaceForChar(query, query.family, codePoint);
    if (!face) {
        status.missing = true;
        status.diagnostic = "no system font covers U+" +
                            std::to_string(static_cast<std::uint32_t>(codePoint));
        return status;
    }
    SkString name;
    face->getFamilyName(&name);
    status.resolvedFamily = skStringToStd(name);
    status.missing = false;
    status.fallbackUsed = !query.family.empty() &&
                          status.resolvedFamily != query.family;
    return status;
}

bool SkiaFontManager::glyphMetrics(const FontQuery& query,
                                   char32_t codePoint,
                                   GlyphMetrics* out) const {
    if (out == nullptr) {
        return false;
    }
    sk_sp<SkTypeface> face =
        impl_->typefaceForChar(query, query.family, codePoint);
    if (!face) {
        return false;
    }
    const float sizePx = query.sizePx > 0.0F ? query.sizePx : 14.0F;
    SkFont font(face, sizePx);
    // 单码点 UTF-8 编码后度量。
    char utf8[4];
    std::size_t len = 0;
    const std::uint32_t cp = static_cast<std::uint32_t>(codePoint);
    if (cp < 0x80) {
        utf8[0] = static_cast<char>(cp);
        len = 1;
    } else if (cp < 0x800) {
        utf8[0] = static_cast<char>(0xC0 | (cp >> 6));
        utf8[1] = static_cast<char>(0x80 | (cp & 0x3F));
        len = 2;
    } else if (cp < 0x10000) {
        utf8[0] = static_cast<char>(0xE0 | (cp >> 12));
        utf8[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | (cp & 0x3F));
        len = 3;
    } else {
        utf8[0] = static_cast<char>(0xF0 | (cp >> 18));
        utf8[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        utf8[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        utf8[3] = static_cast<char>(0x80 | (cp & 0x3F));
        len = 4;
    }
    const float advancePx =
        font.measureText(utf8, len, SkTextEncoding::kUTF8);
    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    out->advanceEm = advancePx / sizePx;
    out->ascentEm = -metrics.fAscent / sizePx;
    out->descentEm = metrics.fDescent / sizePx;
    return true;
}

bool SkiaFontManager::horizontalMetrics(const FontQuery& query,
                                        float* ascentPx,
                                        float* descentPx) const {
    sk_sp<SkTypeface> face =
        impl_->typefaceForFamily(query, query.family);
    if (!face && impl_->mgr) {
        face = impl_->mgr->matchFamilyStyle(nullptr, toSkStyle(query));
    }
    if (!face) {
        return false;
    }
    const float sizePx = query.sizePx > 0.0F ? query.sizePx : 14.0F;
    SkFont font(face, sizePx);
    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    if (ascentPx != nullptr) {
        *ascentPx = -metrics.fAscent;
    }
    if (descentPx != nullptr) {
        *descentPx = metrics.fDescent;
    }
    return true;
}

std::vector<ShapedGlyph> SkiaFontManager::shapeCluster(
    const FontQuery& query, const std::string& graphemeUtf8,
    std::uint32_t clusterIndex) const {
    if (graphemeUtf8.empty()) {
        return {};
    }
    const std::vector<DecodedCodePoint> decoded = decodeUtf8(graphemeUtf8);
    if (decoded.empty()) {
        return {};
    }
    const char32_t firstCp = decoded.front().codePoint;
    sk_sp<SkTypeface> face =
        impl_->typefaceForChar(query, query.family, firstCp);
    if (!face) {
        return {};
    }
    if (face->unicharToGlyph(static_cast<SkUnichar>(firstCp)) == 0) {
        return {};
    }
    const float sizePx = query.sizePx > 0.0F ? query.sizePx : 14.0F;
    SkFont font(face, sizePx);
    const float advancePx = font.measureText(
        graphemeUtf8.data(), graphemeUtf8.size(), SkTextEncoding::kUTF8);
    ShapedGlyph glyph;
    glyph.glyphId = static_cast<std::uint32_t>(
        face->unicharToGlyph(static_cast<SkUnichar>(firstCp)));
    glyph.advancePx = advancePx;
    glyph.xOffsetPx = 0.0F;
    glyph.cluster = clusterIndex;
    return {glyph};
}

std::vector<std::string> SkiaFontManager::availableFamilies() const {
    std::vector<std::string> families;
    if (!impl_->mgr) {
        return families;
    }
    const int count = impl_->mgr->countFamilies();
    families.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        SkString name;
        impl_->mgr->getFamilyName(i, &name);
        families.push_back(skStringToStd(name));
    }
    return families;
}

std::size_t SkiaFontManager::familyCount() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->cachedFamilyCount.has_value()) {
        impl_->cachedFamilyCount =
            impl_->mgr ? static_cast<std::size_t>(impl_->mgr->countFamilies())
                       : std::size_t{0};
    }
    return *impl_->cachedFamilyCount;
}

std::string SkiaFontManager::diagnostic() const {
    std::string out = "backend=skia ";
    out += impl_->initDiagnostic;
    if (impl_->mgr) {
        out += " families=" + std::to_string(familyCount());
    } else {
        out += " (unavailable, placeholder fallback in TextLayout)";
    }
    return out;
}

std::unique_ptr<FontManager> createSkiaFontManager(
    std::string* diagnostic) {
    auto manager = std::unique_ptr<FontManager>(new SkiaFontManager());
    if (diagnostic != nullptr) {
        *diagnostic = manager->diagnostic();
    }
    return manager;
}

}  // namespace lumen::text

#else  // !LUMEN_HAS_SKIA_TEXT

#include "lumen/text/skia_font_manager.h"

namespace lumen::text {

struct SkiaFontManager::Impl {};

SkiaFontManager::~SkiaFontManager() = default;

std::string SkiaFontManager::resolveFamily(
    const FontQuery&, char32_t) const {
    return {};
}

FontFallbackStatus SkiaFontManager::resolveWithStatus(
    const FontQuery&, char32_t) const {
    FontFallbackStatus status;
    status.missing = true;
    status.diagnostic =
        "Skia text backend not compiled (CPU-only build)";
    return status;
}

bool SkiaFontManager::glyphMetrics(const FontQuery&, char32_t,
                                   GlyphMetrics*) const {
    return false;
}

bool SkiaFontManager::horizontalMetrics(const FontQuery&, float*,
                                        float*) const {
    return false;
}

std::vector<ShapedGlyph> SkiaFontManager::shapeCluster(
    const FontQuery&, const std::string&, std::uint32_t) const {
    return {};
}

std::vector<std::string> SkiaFontManager::availableFamilies() const {
    return {};
}

std::size_t SkiaFontManager::familyCount() const { return 0; }

std::string SkiaFontManager::diagnostic() const {
    return "backend=skia (not compiled, CPU-only build)";
}

std::unique_ptr<FontManager> createSkiaFontManager(
    std::string* diagnostic) {
    if (diagnostic != nullptr) {
        *diagnostic =
            "Skia text backend not compiled (CPU-only build); "
            "placeholder fonts in use";
    }
    return nullptr;
}

}  // namespace lumen::text

#endif  // LUMEN_HAS_SKIA_TEXT
