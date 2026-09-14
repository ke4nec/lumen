#include "lumen/text/font_manager.h"

#include <cctype>
#include <cstdlib>
#include <mutex>
#include <utility>

#include "lumen/text/grapheme.h"

#ifdef _WIN32
// 只取 LANGID 判定（GetUserDefaultUILanguage），不引入 GDI/窗口依赖。
#include <windows.h>
#elif defined(__APPLE__)
// 系统偏好语言（Finder 启动的应用无 LANG 环境变量，必须走原生 API）。
#include <CoreFoundation/CoreFoundation.h>
#endif

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

// "zh-CN"/"zh_CN.UTF-8"/"ja-JP" 等语言标记是否为中日韩。
bool isCjkLocaleTag(const char* value) {
    if (value == nullptr || value[0] == '\0' || value[1] == '\0') {
        return false;
    }
    const char c0 = static_cast<char>(
        std::tolower(static_cast<unsigned char>(value[0])));
    const char c1 = static_cast<char>(
        std::tolower(static_cast<unsigned char>(value[1])));
    return (c0 == 'z' && c1 == 'h') || (c0 == 'j' && c1 == 'a') ||
           (c0 == 'k' && c1 == 'o');
}

#if defined(__APPLE__)

// macOS/iOS：偏好语言列表首项（"zh-Hans-CN"/"ja-JP"/"ko-KR"）；
// Finder 启动的应用没有 LANG，只能走 CoreFoundation。
bool detectAppleCjkPreference() {
    bool cjk = false;
    CFArrayRef languages = CFLocaleCopyPreferredLanguages();
    if (languages != nullptr) {
        if (CFArrayGetCount(languages) > 0) {
            const auto* top = static_cast<CFStringRef>(
                CFArrayGetValueAtIndex(languages, 0));
            if (top != nullptr) {
                char tag[32] = {};
                if (CFStringGetCString(top, tag, sizeof(tag),
                                       kCFStringEncodingASCII)) {
                    cjk = isCjkLocaleTag(tag);
                }
            }
        }
        CFRelease(languages);
    }
    return cjk;
}

#endif

// 探测系统 UI 语言（进程内缓存一次；语言切换需重启进程生效）。
bool detectSystemCjkPreference() {
#if defined(_WIN32)
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_CHINESE:
        case LANG_JAPANESE:
        case LANG_KOREAN:
            return true;
        default:
            return false;
    }
#elif defined(__APPLE__)
    if (detectAppleCjkPreference()) {
        return true;
    }
    // 回退环境变量（终端/测试场景）。
    for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG", "LANGUAGE"}) {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            continue;
        }
        // "C"/"POSIX" 视为未设置，继续看下一级。
        if ((value[0] == 'C' || value[0] == 'c') &&
            (value[1] == '\0' || value[1] == '.' || value[1] == '_')) {
            continue;
        }
        if (value[0] == 'P' && value[1] == 'O') {
            continue;  // "POSIX"
        }
        return isCjkLocaleTag(value);
    }
    return false;
#else
    // POSIX 优先级 LC_ALL > LC_CTYPE > LANG；Ubuntu 中文桌面另设
    // LANGUAGE=zh_CN:zh，一并检查。"C"/"POSIX" 跳过看下一级。
    for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG", "LANGUAGE"}) {
        const char* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0') {
            continue;
        }
        if ((value[0] == 'C' || value[0] == 'c') &&
            (value[1] == '\0' || value[1] == '.' || value[1] == '_')) {
            continue;
        }
        if (value[0] == 'P' && value[1] == 'O') {
            continue;  // "POSIX"
        }
        return isCjkLocaleTag(value);
    }
    return false;
#endif
}

}  // namespace

bool systemUiPrefersCjkFont() {
    static const bool cached = detectSystemCjkPreference();
    return cached;
}

namespace {

// 应用覆盖栈（空 = 未设置，走系统语言默认；读写加锁，调用方热路径
// 有 shaped 缓存，频率低）。
std::mutex g_overrideMutex;
std::vector<std::string> g_overrideStack;

std::vector<std::string> currentOverride() {
    std::lock_guard<std::mutex> lock(g_overrideMutex);
    return g_overrideStack;
}

}  // namespace

void setDefaultFontStackOverride(std::vector<std::string> stack) {
    std::lock_guard<std::mutex> lock(g_overrideMutex);
    g_overrideStack = std::move(stack);
}

void clearDefaultFontStackOverride() {
    std::lock_guard<std::mutex> lock(g_overrideMutex);
    g_overrideStack.clear();
}

// --- 各平台默认字体栈（空 family 的解析依据） ---
//
// 顺序即优先级：拉丁码点走拉丁优先栈，CJK 走 CJK 优先栈，emoji 走
// emoji 栈；调用方（Skia/移动/渲染回退）再按“首个覆盖码点者”挑选，
// 避免中英文混排时全落到同一族导致字重/观感割裂。

std::vector<std::string> defaultFontStack() {
    if (std::vector<std::string> override = currentOverride();
        !override.empty()) {
        return override;
    }
#if defined(_WIN32)
    if (systemUiPrefersCjkFont()) {
        return {"Microsoft YaHei", "Segoe UI", "SimSun", "SimHei", "Arial",
                "Segoe UI Emoji", "Segoe UI Symbol"};
    }
    return {"Segoe UI", "Microsoft YaHei", "SimSun", "SimHei", "Arial",
            "Segoe UI Emoji", "Segoe UI Symbol"};
#elif defined(__ANDROID__)
    if (systemUiPrefersCjkFont()) {
        return {"Noto Sans CJK SC", "Roboto", "Noto Sans SC", "Noto Sans",
                "Droid Sans", "Noto Color Emoji"};
    }
    return {"Roboto", "Noto Sans CJK SC", "Noto Sans SC", "Noto Sans",
            "Droid Sans", "Noto Color Emoji"};
#elif defined(__APPLE__)
    if (systemUiPrefersCjkFont()) {
        return {"PingFang SC", "SF Pro Text", "Hiragino Sans GB",
                "Helvetica Neue", "Helvetica", "Arial",
                "Apple Color Emoji"};
    }
    return {"SF Pro Text", "PingFang SC", "Helvetica Neue",
            "Hiragino Sans GB", "Helvetica", "Arial", "Apple Color Emoji"};
#elif defined(__linux__)
    if (systemUiPrefersCjkFont()) {
        return {"Noto Sans CJK SC", "Noto Sans", "Noto Sans SC",
                "WenQuanYi Micro Hei", "DejaVu Sans", "Sans",
                "Noto Color Emoji"};
    }
    return {"Noto Sans", "Noto Sans CJK SC", "Noto Sans SC",
            "WenQuanYi Micro Hei", "DejaVu Sans", "Sans",
            "Noto Color Emoji"};
#else
    return {"Sans", "Arial", "DejaVu Sans", "Noto Sans",
            "Noto Sans CJK SC"};
#endif
}

std::vector<std::string> defaultFontStackFor(char32_t codePoint) {
    // 应用覆盖优先：原样返回应用指定的顺序，不再按脚本拆分。
    if (std::vector<std::string> override = currentOverride();
        !override.empty()) {
        return override;
    }
#if defined(_WIN32)
    if (isEmojiCodePoint(codePoint)) {
        return {"Segoe UI Emoji", "Segoe UI Symbol", "Microsoft YaHei",
                "Segoe UI", "Arial"};
    }
    if (isCjkCodePoint(codePoint)) {
        return {"Microsoft YaHei", "SimSun", "SimHei", "Segoe UI", "Arial"};
    }
    if (systemUiPrefersCjkFont()) {
        // 中文系统：拉丁字母/数字也优先雅黑，与中文正文观感统一。
        return {"Microsoft YaHei", "Segoe UI", "SimSun", "Arial"};
    }
    return {"Segoe UI", "Arial", "Tahoma", "Microsoft YaHei", "SimSun"};
#elif defined(__ANDROID__)
    if (isEmojiCodePoint(codePoint)) {
        return {"Noto Color Emoji", "Noto Sans CJK SC", "Roboto"};
    }
    if (isCjkCodePoint(codePoint)) {
        return {"Noto Sans CJK SC", "Noto Sans SC", "Roboto", "Noto Sans"};
    }
    if (systemUiPrefersCjkFont()) {
        return {"Noto Sans CJK SC", "Roboto", "Noto Sans", "Droid Sans"};
    }
    return {"Roboto", "Noto Sans", "Noto Sans CJK SC", "Droid Sans"};
#elif defined(__APPLE__)
    if (isEmojiCodePoint(codePoint)) {
        return {"Apple Color Emoji", "PingFang SC", "SF Pro Text"};
    }
    if (isCjkCodePoint(codePoint)) {
        return {"PingFang SC", "Hiragino Sans GB", "STHeiti", "SF Pro Text",
                "Helvetica Neue", "Arial"};
    }
    if (systemUiPrefersCjkFont()) {
        return {"PingFang SC", "Hiragino Sans GB", "SF Pro Text",
                "Helvetica Neue", "Arial"};
    }
    return {"SF Pro Text", "Helvetica Neue", "Helvetica", "Arial",
            "PingFang SC", "Hiragino Sans GB"};
#elif defined(__linux__)
    if (isEmojiCodePoint(codePoint)) {
        return {"Noto Color Emoji", "Noto Sans CJK SC", "Noto Sans",
                "DejaVu Sans"};
    }
    if (isCjkCodePoint(codePoint)) {
        return {"Noto Sans CJK SC", "Noto Sans SC", "WenQuanYi Micro Hei",
                "Noto Sans", "DejaVu Sans", "Sans"};
    }
    if (systemUiPrefersCjkFont()) {
        return {"Noto Sans CJK SC", "Noto Sans", "WenQuanYi Micro Hei",
                "DejaVu Sans", "Sans"};
    }
    return {"Noto Sans", "DejaVu Sans", "Noto Sans CJK SC",
            "WenQuanYi Micro Hei", "Sans"};
#else
    (void)codePoint;
    return defaultFontStack();
#endif
}

std::string defaultFontFamily() {
    const std::vector<std::string> stack = defaultFontStack();
    return stack.empty() ? std::string{} : stack.front();
}

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
