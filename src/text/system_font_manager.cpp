// 桌面系统字体后端（系统字形优先；CPU-only 可用）。
//
// Windows 窗口默认走占位 5x7 点阵字，与设计稿差距主因。CPU 窗口路径
// 经 stb_truetype 读取系统字体文件（Windows 雅黑/Segoe 优先）提供真实
// 字形：横向步进与轮廓位图同为字体设计值，避免 GDI 整数量化步进把字
// 距撑歪（GDI 仅承担行高 tmAscent/tmDescent）；度量/回退与
// defaultFontStackFor 同序，并向 CpuRenderer 提供灰度字形位图。headless
// /测试默认仍用占位（确定性帧哈希不受影响），窗口路径经 setFontManager
// 注入。

#include "lumen/text/system_font_manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef LUMEN_HAS_STB_TRUETYPE
#include "stb_truetype.h"
#endif

#ifdef _WIN32
// 取系统字体目录，并使用 GDI 字形位图 API；不创建窗口。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lumen::text {
namespace {

#ifdef LUMEN_HAS_STB_TRUETYPE

constexpr std::size_t kMaxFiles = 256;
// 文件数和加载后的 face 数共用此上限，三个缓存都为 face 保留 8 位。
static_assert(kMaxFiles <= (1U << 8));
constexpr std::size_t kMaxFileBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaxBitmapCache = 4096;

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool isFontFile(const std::filesystem::path& path) {
    std::string ext = lowerAscii(path.extension().string());
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc" ||
           ext == ".otc";
}

bool isBoldFile(const std::string& path) {
    const std::string lower = lowerAscii(path);
    return lower.find("bold") != std::string::npos ||
           lower.find("-black") != std::string::npos ||
           lower.find("-heavy") != std::string::npos ||
           lower.find("msyhbd") != std::string::npos;
}

// OS/2 表 usWeightClass（100..900；缺表/越界返回 -1）。文件名启发
// 式认不出 segoeuib/arialbd/tahomabd 这类 Windows 粗体（无 "bold"
// 子串），按 OS/2 为准，解析失败才回退文件名。
int weightClassForFace(const unsigned char* data, std::size_t size,
                       int fontOffset) {
    if (data == nullptr || fontOffset < 0) {
        return -1;
    }
    const auto start = static_cast<std::size_t>(fontOffset);
    if (start + 12 > size) {
        return -1;
    }
    const int numTables =
        (static_cast<int>(data[start + 4]) << 8) | data[start + 5];
    if (numTables <= 0 || numTables > 96) {
        return -1;
    }
    for (int i = 0; i < numTables; ++i) {
        const std::size_t rec = start + 12 + static_cast<std::size_t>(i) * 16;
        if (rec + 16 > size) {
            return -1;
        }
        if (data[rec] != 'O' || data[rec + 1] != 'S' ||
            data[rec + 2] != '/' || data[rec + 3] != '2') {
            continue;
        }
        const std::size_t tab =
            (static_cast<std::size_t>(data[rec + 8]) << 24) |
            (static_cast<std::size_t>(data[rec + 9]) << 16) |
            (static_cast<std::size_t>(data[rec + 10]) << 8) |
            static_cast<std::size_t>(data[rec + 11]);
        const std::size_t length =
            (static_cast<std::size_t>(data[rec + 12]) << 24) |
            (static_cast<std::size_t>(data[rec + 13]) << 16) |
            (static_cast<std::size_t>(data[rec + 14]) << 8) |
            static_cast<std::size_t>(data[rec + 15]);
        if (length < 6 || tab + 6 > size) {
            return -1;
        }
        return (static_cast<int>(data[tab + 4]) << 8) | data[tab + 5];
    }
    return -1;
}

// 文件名 → 粗略 family（"msyh.ttc" → "msyh"），name 表缺失时回退。
std::string familyFromFile(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string name =
        slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dash = name.find('-');
    if (dash != std::string::npos) {
        name = name.substr(0, dash);
    }
    const std::size_t dot = name.find('.');
    if (dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    return lowerAscii(name);
}

// name 表 family（id=1，Windows UCS-2）→ 小写 ASCII；CJK 族名无
// ASCII 覆盖时返回空（调用方回退文件名）。
std::string familyFromNameTable(const stbtt_fontinfo& info) {
    int length = 0;
    const char* raw =
        stbtt_GetFontNameString(&info, &length, 3, 1, 0x409, 1);
    if (raw == nullptr || length <= 0 || length > 512) {
        return {};
    }
    // UTF-16BE：高位为 0 的 ASCII 对半取低字节。
    std::string family;
    for (int i = 0; i + 1 < length && family.size() < 64; i += 2) {
        const auto hi =
            static_cast<unsigned char>(raw[i]);
        const auto lo =
            static_cast<unsigned char>(raw[i + 1]);
        if (hi == 0 && lo >= ' ' && lo < '\x7F') {
            family.push_back(static_cast<char>(std::tolower(lo)));
        }
    }
    // 去掉首尾空格。
    while (!family.empty() && family.front() == ' ') {
        family.erase(family.begin());
    }
    while (!family.empty() && family.back() == ' ') {
        family.pop_back();
    }
    return family;
}

#ifdef _WIN32

// GetTextFaceW 可能返回本地化族名（如“微软雅黑”），不能直接与英文
// 查询比较。读取 GDI 实际选中字体的英文 name 记录，避免把别名误报
// 为替换，也不把 CreateFontW 成功当成命中请求族。
bool selectedGdiFamilyMatches(HDC dc, const std::string& requested) {
    constexpr DWORD kNameTable = 0x656D616E;  // GDI tag: 'name'
    const DWORD size = GetFontData(dc, kNameTable, 0, nullptr, 0);
    if (size == GDI_ERROR || size < 6 || size > 1024U * 1024U) {
        return false;
    }
    std::vector<unsigned char> table(size);
    if (GetFontData(dc, kNameTable, 0, table.data(), size) != size) {
        return false;
    }
    const auto u16 = [&table](std::size_t offset) -> std::size_t {
        return (static_cast<std::size_t>(table[offset]) << 8) |
               table[offset + 1];
    };
    const std::size_t count = u16(2);
    const std::size_t strings = u16(4);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t record = 6 + i * 12;
        if (record + 12 > table.size()) {
            break;
        }
        // 部分字重把子族放进 legacy family（如 Segoe UI Semibold），
        // typographic family（16）仍是 Segoe UI，也属于合法命中。
        const auto nameId = u16(record + 6);
        if (u16(record) != 3 || u16(record + 2) != 1 ||
            u16(record + 4) != 0x409 || (nameId != 1 && nameId != 16)) {
            continue;
        }
        const std::size_t length = u16(record + 8);
        const std::size_t offset = strings + u16(record + 10);
        if (length == 0 || length > 512 || offset + length > table.size()) {
            continue;
        }
        std::string family;
        for (std::size_t j = 0; j + 1 < length; j += 2) {
            const unsigned char hi = table[offset + j];
            const unsigned char lo = table[offset + j + 1];
            if (hi == 0 && lo >= ' ' && lo < 0x7F) {
                family.push_back(static_cast<char>(lo));
            }
        }
        const auto first = family.find_first_not_of(' ');
        if (first == std::string::npos) {
            continue;
        }
        if (lowerAscii(family.substr(first, family.find_last_not_of(' ') -
                                               first + 1)) == requested) {
            return true;
        }
    }
    return false;
}
#endif

std::vector<std::string> defaultDirectories() {
    std::vector<std::string> dirs;
#ifdef _WIN32
    wchar_t winDir[MAX_PATH] = {};
    const UINT got = GetWindowsDirectoryW(winDir, MAX_PATH);
    if (got > 0 && got < MAX_PATH) {
        std::filesystem::path fonts =
            std::filesystem::path(winDir) / L"Fonts";
        dirs.push_back(fonts.string());
    }
    dirs.push_back("C:/Windows/Fonts");
#elif defined(__APPLE__)
    dirs.push_back("/System/Library/Fonts");
    dirs.push_back("/Library/Fonts");
    if (const char* home = std::getenv("HOME")) {
        dirs.push_back(std::string(home) + "/Library/Fonts");
    }
#elif defined(__linux__)
    dirs.push_back("/usr/share/fonts");
    dirs.push_back("/usr/local/share/fonts");
    if (const char* home = std::getenv("HOME")) {
        dirs.push_back(std::string(home) + "/.fonts");
        dirs.push_back(std::string(home) + "/.local/share/fonts");
    }
#else
    dirs.push_back("/usr/share/fonts");
#endif
    return dirs;
}

struct FaceEntry {
    std::string family{};
    std::string path{};
    std::size_t blobIndex{0};
    stbtt_fontinfo info{};
    int unitsPerEm{1000};
    int ascentEm{0};
    int descentEm{0};
    // OS/2 usWeightClass（100..900；-1 = 无表，回退文件名启发）。
    int weightClass{-1};
};

// 粗体判定：OS/2 优先（阈值与 faceFor 的 wantBold 同为 >= 600），
// 无表时回退文件名启发。
bool faceIsBold(const FaceEntry& face) {
    if (face.weightClass >= 1 && face.weightClass <= 1000) {
        return face.weightClass >= 600;
    }
    return isBoldFile(face.path);
}

class SystemFontManagerImpl final : public SystemFontManager {
  public:
    SystemFontManagerImpl(std::vector<std::vector<unsigned char>> blobs,
                          std::vector<FaceEntry> faces, std::string usedDir)
        : blobs_(std::move(blobs)),
          faces_(std::move(faces)),
          usedDir_(std::move(usedDir)) {}

    [[nodiscard]] bool supportsShaping() const override { return false; }

    [[nodiscard]] std::string resolveFamily(
        const FontQuery& query, char32_t codePoint) const override {
        const auto status = resolveWithStatus(query, codePoint);
        return status.missing ? std::string{} : status.resolvedFamily;
    }

    [[nodiscard]] FontFallbackStatus resolveWithStatus(
        const FontQuery& query, char32_t codePoint) const override {
        FontFallbackStatus status;
        const FaceEntry* face = faceFor(query, codePoint);
        if (face == nullptr) {
            status.missing = true;
            status.diagnostic =
                "no system font covers U+" +
                std::to_string(static_cast<std::uint32_t>(codePoint));
            return status;
        }
        status.resolvedFamily = face->family;
        // 族名比较不区分大小写：请求 "Segoe UI" 命中 "segoe ui" 是精确
        // 命中，不应记为回退（否则诊断误报、调用方误判）。
        status.fallbackUsed = !query.family.empty() &&
                              face->family != lowerAscii(query.family);
        return status;
    }

    [[nodiscard]] bool glyphMetrics(const FontQuery& query,
                                     char32_t codePoint,
                                     GlyphMetrics* out) const override {
        if (out == nullptr) {
            return false;
        }
        const FaceEntry* face = faceFor(query, codePoint);
        if (face == nullptr) {
            return false;
        }
        // 横向步进与位图都取字体设计值（stb hmtx/轮廓光栅）：曾用 GDI
        // GGO_METRICS 的 gmCellIncX（整数量化步进）配 GDI grid-fit 位图，
        // 逐字形累积把字距系统性撑歪（14px 下 "Lumen" 的字符节奏肉眼
        // 可辨地偏离设计稿）；度量和墨宽必须同一坐标系。
        const int glyph = stbtt_FindGlyphIndex(
            &face->info, static_cast<int>(codePoint));
        if (glyph == 0) {
            return false;
        }
        int advance = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&face->info, glyph, &advance, &bearing);
        out->advanceEm =
            face->unitsPerEm > 0
                ? static_cast<float>(advance) /
                      static_cast<float>(face->unitsPerEm)
                : 0.6F;
        out->ascentEm = face->unitsPerEm > 0
                            ? static_cast<float>(face->ascentEm) /
                                  static_cast<float>(face->unitsPerEm)
                            : 0.8F;
        out->descentEm = face->unitsPerEm > 0
                             ? std::abs(static_cast<float>(face->descentEm) /
                                        static_cast<float>(face->unitsPerEm))
                             : 0.2F;
        // 字体级 hhea 度量未必包住每个轮廓（如 Segoe UI Emoji）。布局
        // 还要容纳实际位图的上下界，否则纯 emoji 会在顶部被节点裁剪。
        const float size = query.sizePx > 0.0F ? query.sizePx : 14.0F;
        const float scale = stbtt_ScaleForMappingEmToPixels(&face->info, size);
        int top = 0;
        int bottom = 0;
        stbtt_GetGlyphBitmapBox(&face->info, glyph, scale, scale, nullptr,
                               &top, nullptr, &bottom);
        out->ascentEm = std::max(out->ascentEm, static_cast<float>(-top) / size);
        out->descentEm = std::max(out->descentEm, static_cast<float>(bottom) / size);
        return true;
    }

    [[nodiscard]] bool horizontalMetrics(const FontQuery& query,
                                         float* ascentPx,
                                         float* descentPx) const override {
        const FaceEntry* face = faceFor(query, U' ');
        if (face == nullptr) {
            return false;
        }
        const float size = query.sizePx > 0.0F ? query.sizePx : 14.0F;
#ifdef _WIN32
        // 同 glyphMetrics：行高基线与 GDI 位图同源，避免 baseline 漂移
        // 导致下行部被裁（“底部显示不全”）。
        {
            float ascent = 0.0F;
            float descent = 0.0F;
            if (gdiVertical(*face, query, size, &ascent, &descent)) {
                if (ascentPx != nullptr) {
                    *ascentPx = ascent;
                }
                if (descentPx != nullptr) {
                    *descentPx = descent;
                }
                return true;
            }
        }
#endif
        // FontQuery::sizePx is an em size.  Keep metrics and bitmap raster
        // on the same scale; ScaleForPixelHeight uses the face's ascender /
        // descender box and otherwise makes glyph advances drift from
        // TextLayout.
        const float scale =
            stbtt_ScaleForMappingEmToPixels(&face->info, size);
        if (ascentPx != nullptr) {
            *ascentPx = static_cast<float>(face->ascentEm) * scale;
        }
        if (descentPx != nullptr) {
            *descentPx = std::abs(static_cast<float>(face->descentEm) * scale);
        }
        return true;
    }

    [[nodiscard]] bool bitmapFor(char32_t codePoint, const FontQuery& query,
                                 float deviceScale,
                                 GlyphBitmap* out) const override {
        if (out == nullptr || deviceScale <= 0.0F) {
            return false;
        }
        const FaceEntry* face = faceFor(query, codePoint);
        if (face == nullptr) {
            return false;
        }
        const float sizePx = query.sizePx > 0.0F ? query.sizePx : 14.0F;
        const float pixelHeight = sizePx * deviceScale;
        if (pixelHeight <= 0.0F || pixelHeight > 256.0F) {
            return false;
        }
        const std::size_t faceIndex =
            static_cast<std::size_t>(face - faces_.data());
        // 缓存键：face + 码点 + 像素高度 + weight/italic（字号按 1/16
        // 取整，同一字号稳定命中）。
        const auto quantized =
            static_cast<std::uint32_t>(std::lround(pixelHeight * 16.0F));
        const std::uint64_t key =
            ((static_cast<std::uint64_t>(faceIndex) & 0xFFULL) << 56) |
            ((static_cast<std::uint64_t>(
                  static_cast<std::uint32_t>(codePoint)) & 0xFFFFFFULL)
             << 32) |
            ((static_cast<std::uint64_t>(quantized) & 0xFFFFFFULL) << 8) |
            ((static_cast<std::uint64_t>(
                 std::clamp(static_cast<int>(query.weight), 100, 900) / 100)
             & 0x0FULL)
             << 1) |
            (query.italic ? 1ULL : 0ULL);
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            if (const auto it = bitmapCache_.find(key);
                it != bitmapCache_.end()) {
                *out = it->second;
                return !out->empty();
            }
        }
        GlyphBitmap bitmap;
        const bool ok = rasterize(*face, query, static_cast<int>(codePoint),
                                  pixelHeight, &bitmap);
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (bitmapCache_.size() >= kMaxBitmapCache) {
            bitmapCache_.clear();
        }
        bitmapCache_.emplace(key, bitmap);
        *out = bitmap;
        return ok && !bitmap.empty();
    }

    [[nodiscard]] std::vector<std::string> availableFamilies()
        const override {
        std::vector<std::string> families;
        for (const auto& face : faces_) {
            if (std::find(families.begin(), families.end(), face.family) ==
                families.end()) {
                families.push_back(face.family);
            }
        }
        return families;
    }

    [[nodiscard]] std::size_t familyCount() const override {
        return faces_.size();
    }

    [[nodiscard]] std::string diagnostic() const override {
        std::string result = "backend=system faces=" +
                             std::to_string(faces_.size());
#ifdef _WIN32
        result += " raster=gdi+stb";
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            for (const auto& family : gdiSubstitutions_) {
                result += " gdi-substitution=" + family + " (using stb)";
            }
        }
#else
        result += " raster=stb";
#endif
        return result + " " + usedDir_;
    }

  private:
    // 空请求走 defaultFontStackFor 顺序（拉丁恒为 Segoe 优先，CJK 为
    // 雅黑优先，均按码点脚本逐字回退），显式族名精确匹配，粗细按 OS/2
    // usWeightClass（>= 600 为粗，与 wantBold 同阈值）。
    [[nodiscard]] const FaceEntry* faceFor(const FontQuery& query,
                                           char32_t codePoint) const {
        const std::string requested = lowerAscii(query.family);
        const bool wantBold = static_cast<int>(query.weight) >= 600;
        if (requested.empty()) {
            for (const std::string& candidate :
                 defaultFontStackFor(codePoint)) {
                const std::string want = lowerAscii(candidate);
                const FaceEntry* boldMiss = nullptr;
                for (const auto& face : faces_) {
                    if (face.family != want) {
                        continue;
                    }
                    if (stbtt_FindGlyphIndex(
                            &face.info,
                            static_cast<int>(codePoint)) == 0) {
                        continue;
                    }
                    if (wantBold == faceIsBold(face)) {
                        return &face;
                    }
                    if (boldMiss == nullptr) {
                        boldMiss = &face;
                    }
                }
                if (boldMiss != nullptr) {
                    return boldMiss;
                }
            }
        }
        // 显式族名：族命中的粗细未中者优先于其他族的覆盖者。旧实现把
        // 首个覆盖者（常为先加载的雅黑）直接记为 fallback，导致
        // family="Segoe UI" 的粗体请求被雅黑劫持。
        const FaceEntry* fallback = nullptr;
        const FaceEntry* familyFallback = nullptr;
        for (const auto& face : faces_) {
            if (stbtt_FindGlyphIndex(&face.info,
                                     static_cast<int>(codePoint)) == 0) {
                continue;
            }
            if (!requested.empty() && face.family == requested) {
                if (wantBold == faceIsBold(face)) {
                    return &face;
                }
                if (familyFallback == nullptr) {
                    familyFallback = &face;
                }
                continue;
            }
            if (fallback == nullptr) {
                fallback = &face;
            }
        }
        if (familyFallback != nullptr) {
            return familyFallback;
        }
        return fallback;
    }

    [[nodiscard]] bool rasterize(const FaceEntry& face, const FontQuery&,
                                 int codePoint, float pixelHeight,
                                 GlyphBitmap* out) const {
        // 字形光栅一律走 stb（无 hinting 的设计值灰度位图），与
        // glyphMetrics 的设计步进同源；曾优先 GDI 光栅，其 grid-fit 墨宽
        // 与设计步进不一致导致字距失真（CPU 渲染管线，非系统控件路径）。
        const float scale =
            stbtt_ScaleForMappingEmToPixels(&face.info, pixelHeight);
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&face.info, codePoint, scale, scale, &x0,
                                    &y0, &x1, &y1);
        const int width = x1 - x0;
        const int height = y1 - y0;
        if (width <= 0 || height <= 0 || width > 512 || height > 512) {
            return false;  // 空白字形（空格）或异常：调用方按 advance 留白。
        }
        GlyphBitmap bitmap;
        bitmap.width = width;
        bitmap.height = height;
        bitmap.bearingX = x0;
        bitmap.bearingTop = -y0;
        bitmap.coverage.assign(static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(height),
                               0);
        stbtt_MakeCodepointBitmap(&face.info, bitmap.coverage.data(), width,
                                  height, width, scale, scale, codePoint);
        *out = std::move(bitmap);
        return true;
    }

#ifdef _WIN32
    // 度量和光栅共用创建/选择/校验；替换字体时两者均回退到已加载的
    // stb face，并留下诊断，不能混用替换字体的位图和原字体的度量。
    [[nodiscard]] HFONT createGdiFont(HDC dc, const FaceEntry& face,
                                     const FontQuery& query, float pixelHeight,
                                     HGDIOBJ* previous) const {
        if (face.family.empty() || pixelHeight <= 0.0F) {
            return nullptr;
        }
        const int height =
            -std::max(1, static_cast<int>(std::lround(pixelHeight)));
        const int weight =
            std::clamp(static_cast<int>(query.weight), 100, 900);
        std::wstring family;
        const int length = MultiByteToWideChar(CP_UTF8, 0, face.family.c_str(),
                                              -1, nullptr, 0);
        if (length > 1) {
            family.resize(static_cast<std::size_t>(length));
            MultiByteToWideChar(CP_UTF8, 0, face.family.c_str(), -1,
                                family.data(), length);
            family.resize(static_cast<std::size_t>(length - 1));
        }
        const HFONT font = CreateFontW(height, 0, 0, 0, weight,
                           query.italic ? TRUE : FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, family.c_str());
        if (font == nullptr) {
            return nullptr;
        }
        *previous = SelectObject(dc, font);
        if (*previous == nullptr || *previous == HGDI_ERROR) {
            DeleteObject(font);
            return nullptr;
        }
        if (!selectedGdiFamilyMatches(dc, face.family)) {
            SelectObject(dc, *previous);
            DeleteObject(font);
            std::lock_guard<std::mutex> lock(cacheMutex_);
            gdiSubstitutions_.insert(face.family);
            return nullptr;
        }
        return font;
    }

    // GDI 垂直度量（逻辑像素；字体级，可缓存）。失败返回 false 走 stb。
    [[nodiscard]] bool gdiVertical(const FaceEntry& face,
                                   const FontQuery& query, float sizePx,
                                   float* ascentPx,
                                   float* descentPx) const {
        if (sizePx <= 0.0F || sizePx > 256.0F) {
            return false;
        }
        const std::size_t faceIndex =
            static_cast<std::size_t>(&face - faces_.data());
        const auto quantized =
            static_cast<std::uint32_t>(std::lround(sizePx * 16.0F));
        const std::uint64_t key =
            ((static_cast<std::uint64_t>(faceIndex) & 0xFFULL) << 32) |
            ((static_cast<std::uint64_t>(quantized) & 0xFFFFFFULL) << 8) |
            ((static_cast<std::uint64_t>(
                  std::clamp(static_cast<int>(query.weight), 100, 900) / 100) &
              0x0FULL)
             << 1) |
            (query.italic ? 1ULL : 0ULL);
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            if (const auto it = gdiVerticalCache_.find(key);
                it != gdiVerticalCache_.end()) {
                if (ascentPx != nullptr) {
                    *ascentPx = it->second.first;
                }
                if (descentPx != nullptr) {
                    *descentPx = it->second.second;
                }
                return true;
            }
        }
        HDC dc = CreateCompatibleDC(nullptr);
        if (dc == nullptr) {
            return false;
        }
        HGDIOBJ previous = nullptr;
        HFONT font = createGdiFont(dc, face, query, sizePx, &previous);
        if (font == nullptr) {
            DeleteDC(dc);
            return false;
        }
        TEXTMETRICW tm{};
        const bool ok = GetTextMetricsW(dc, &tm) != 0;
        const float ascent =
            ok ? static_cast<float>(tm.tmAscent) : 0.0F;
        const float descent =
            ok ? static_cast<float>(tm.tmDescent) : 0.0F;
        SelectObject(dc, previous);
        DeleteObject(font);
        DeleteDC(dc);
        if (!ok || ascent <= 0.0F) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            if (gdiVerticalCache_.size() >= kMaxGdiCacheSize) {
                gdiVerticalCache_.clear();
            }
            gdiVerticalCache_.emplace(key, std::make_pair(ascent, descent));
        }
        if (ascentPx != nullptr) {
            *ascentPx = ascent;
        }
        if (descentPx != nullptr) {
            *descentPx = descent;
        }
        return true;
    }


#endif

    // 字体文件字节（faces_ 的 stbtt_fontinfo 指向其中；构造后不再
    // 变动，移动外层 vector 不影响内层 data 指针）。
    std::vector<std::vector<unsigned char>> blobs_{};
    std::vector<FaceEntry> faces_{};
    std::string usedDir_{};
    mutable std::mutex cacheMutex_{};
    mutable std::unordered_map<std::uint64_t, GlyphBitmap> bitmapCache_{};
#ifdef _WIN32
    // GDI 行高度量缓存（热路径命中，避免每 cluster 建 DC）与替换族记录。
    static constexpr std::size_t kMaxGdiCacheSize = 8192;
    mutable std::unordered_map<std::uint64_t, std::pair<float, float>>
        gdiVerticalCache_{};
    mutable std::unordered_set<std::string> gdiSubstitutions_{};
#endif
};

std::vector<unsigned char> readFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxFileBytes) {
        return {};
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        return {};
    }
    return bytes;
}

// 扫描单个目录（非递归 + 一层子目录，覆盖 /usr/share/fonts 布局；
// Windows Fonts 为扁平目录，一次遍历即全）。
std::vector<std::filesystem::path> listFontFiles(const std::string& dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        return files;
    }
    // Windows 优先：雅黑/宋体/黑体/Segoe 先行，保证默认栈首选有面可用
    //（目录遍历顺序不确定，不能依赖它）。
    const std::vector<std::string> kPreferred = {
        "msyh.ttc",  "msyhbd.ttc", "msyhl.ttc", "simsun.ttc",
        "simhei.ttf", "segoeui.ttf", "segoeuib.ttf", "seguisb.ttf",
        "seguisym.ttf", "seguiemj.ttf", "arial.ttf", "arialbd.ttf",
    };
    auto take = [&](const std::filesystem::path& path) {
        if (files.size() >= kMaxFiles || !isFontFile(path)) {
            return;
        }
        if (std::find(files.begin(), files.end(), path) == files.end()) {
            files.push_back(path);
        }
    };
    for (const std::string& name : kPreferred) {
        std::error_code probeEc;
        std::filesystem::path candidate =
            std::filesystem::path(dir) / name;
        if (std::filesystem::is_regular_file(candidate, probeEc)) {
            take(candidate);
        }
    }
    for (const auto& entry : it) {
        if (files.size() >= kMaxFiles) {
            break;
        }
        std::error_code entryEc;
        if (entry.is_directory(entryEc) && !entryEc) {
            std::filesystem::directory_iterator sub(entry.path(), entryEc);
            if (entryEc) {
                continue;
            }
            for (const auto& subEntry : sub) {
                if (files.size() >= kMaxFiles) {
                    break;
                }
                take(subEntry.path());
            }
            continue;
        }
        take(entry.path());
    }
    return files;
}

#endif  // LUMEN_HAS_STB_TRUETYPE

}  // namespace

std::unique_ptr<SystemFontManager> createSystemFontManager(
    std::string* diagnostic, std::vector<std::string> directories) {
#ifdef LUMEN_HAS_STB_TRUETYPE
    if (directories.empty()) {
        directories = defaultDirectories();
    }
    for (const auto& dir : directories) {
        const std::vector<std::filesystem::path> files = listFontFiles(dir);
        if (files.empty()) {
            continue;
        }
        std::vector<std::vector<unsigned char>> blobs;
        std::vector<FaceEntry> faces;
        for (const auto& path : files) {
            std::vector<unsigned char> bytes = readFile(path);
            if (bytes.empty()) {
                continue;
            }
            int count = stbtt_GetNumberOfFonts(bytes.data());
            if (count <= 0) {
                count = 1;  // 单 face 文件返回 0（stb 契约）。
            }
            if (count > 64) {
                count = 64;
            }
            const std::string pathStr = path.string();
            // 文件字节先入库（同文件多 face 共享一份拷贝；stbtt info
            // 指向其中，构造后不再变动）。
            blobs.push_back(std::move(bytes));
            const std::size_t blobIndex = blobs.size() - 1;
            unsigned char* base = blobs.back().data();
            const std::size_t facesBefore = faces.size();
            for (int index = 0; index < count && faces.size() < kMaxFiles;
                 ++index) {
                const int offset =
                    stbtt_GetFontOffsetForIndex(base, index);
                if (offset < 0) {
                    break;
                }
                FaceEntry entry;
                entry.path = pathStr;
                entry.blobIndex = blobIndex;
                if (stbtt_InitFont(&entry.info, base, offset) == 0) {
                    break;
                }
                entry.family = familyFromNameTable(entry.info);
                if (entry.family.empty()) {
                    entry.family = familyFromFile(pathStr);
                }
                // OS/2 粗细（segoeuib/arialbd 等文件名无 "bold" 标记，
                // 必须读表；失败（-1）则查询期回退文件名启发）。
                entry.weightClass = weightClassForFace(
                    base, blobs.back().size(), offset);
                // head 表 unitsPerEm（大端；stbtt_fontinfo 无该成员）。
                entry.unitsPerEm = 1000;
                if (entry.info.head > 0) {
                    const unsigned char* headTable =
                        entry.info.data + entry.info.head;
                    const int upm = (static_cast<int>(headTable[18]) << 8) |
                                    headTable[19];
                    if (upm > 0) {
                        entry.unitsPerEm = upm;
                    }
                }
                int ascent = 0;
                int descent = 0;
                int lineGap = 0;
                stbtt_GetFontVMetrics(&entry.info, &ascent, &descent,
                                      &lineGap);
                entry.ascentEm = ascent;
                entry.descentEm = descent;
                faces.push_back(std::move(entry));
            }
            if (faces.size() == facesBefore) {
                // 该文件无可用 face：收回字节。
                blobs.pop_back();
            }
        }
        if (faces.empty()) {
            continue;
        }
        if (diagnostic != nullptr) {
            *diagnostic = "system fonts: " + dir + " (" +
                          std::to_string(faces.size()) + " faces)";
#ifdef _WIN32
            *diagnostic += " raster=gdi+stb";
#else
            *diagnostic += " raster=stb";
#endif
        }
        return std::make_unique<SystemFontManagerImpl>(
            std::move(blobs), std::move(faces), dir);
    }
    if (diagnostic != nullptr) {
        *diagnostic = "system fonts: no font files in " +
                      (directories.empty() ? std::string("<none>")
                                           : directories.front());
    }
    return nullptr;
#else
    if (diagnostic != nullptr) {
        *diagnostic = "system fonts: stb_truetype unavailable";
    }
    (void)directories;
    return nullptr;
#endif
}

}  // namespace lumen::text
