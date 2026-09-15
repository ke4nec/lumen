// 桌面系统字体后端（系统字形优先；CPU-only 可用）。
//
// Windows 窗口默认走占位 5x7 点阵字，与设计稿差距主因。CPU 窗口路径
// 现在优先通过 GDI 获取系统字形（Windows 雅黑优先），非 Windows 或
// GDI 不可用时经 stb_truetype 读取系统字体文件；度量/回退与
// defaultFontStackFor 同序，并向 CpuRenderer 提供灰度字形位图。headless
// /测试默认仍用占位（确定性帧哈希不受影响），窗口路径经 fontFactory 注入。

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
};

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
        status.fallbackUsed =
            !query.family.empty() && face->family != query.family;
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
#else
        result += " raster=stb";
#endif
        return result + " " + usedDir_;
    }

  private:
    // 与 Mobile 后端同策略：空请求走 defaultFontStackFor 顺序（雅黑在
    // 中文 Windows 优先），显式族名精确匹配，粗细按文件名近似。
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
                    if (wantBold == isBoldFile(face.path)) {
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
        const FaceEntry* fallback = nullptr;
        for (const auto& face : faces_) {
            if (stbtt_FindGlyphIndex(&face.info,
                                     static_cast<int>(codePoint)) == 0) {
                continue;
            }
            if (!requested.empty() && face.family == requested) {
                if (wantBold == isBoldFile(face.path)) {
                    return &face;
                }
                if (fallback == nullptr) {
                    fallback = &face;
                }
                continue;
            }
            if (fallback == nullptr) {
                fallback = &face;
            }
        }
        return fallback;
    }

    [[nodiscard]] static bool rasterize(const FaceEntry& face,
                                        const FontQuery& query,
                                        int codePoint, float pixelHeight,
                                        GlyphBitmap* out) {
#ifdef _WIN32
        // Prefer the platform rasterizer on Windows.  GDI's gray glyph
        // bitmap is the same system font path used by desktop controls and
        // keeps small UI text from looking like a scaled pixel font.
        if (codePoint <= 0xFFFF &&
            rasterizeWindows(face, query, static_cast<wchar_t>(codePoint),
                             pixelHeight, out)) {
            return true;
        }
#endif
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
    [[nodiscard]] static bool rasterizeWindows(const FaceEntry& face,
                                               const FontQuery& query,
                                               wchar_t codePoint,
                                               float pixelHeight,
                                               GlyphBitmap* out) {
        if (out == nullptr || pixelHeight <= 0.0F || face.family.empty()) {
            return false;
        }
        HDC dc = CreateCompatibleDC(nullptr);
        if (dc == nullptr) {
            return false;
        }
        const int height = -std::max(1, static_cast<int>(std::lround(pixelHeight)));
        const int weight = std::clamp(static_cast<int>(query.weight), 100, 900);
        const HFONT font = CreateFontW(
            height, 0, 0, 0, weight, query.italic ? TRUE : FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            [&face]() {
                static thread_local std::wstring family;
                family.clear();
                const int length = MultiByteToWideChar(
                    CP_UTF8, 0, face.family.c_str(), -1, nullptr, 0);
                if (length > 1) {
                    family.resize(static_cast<std::size_t>(length));
                    MultiByteToWideChar(CP_UTF8, 0, face.family.c_str(), -1,
                                        family.data(), length);
                    family.resize(static_cast<std::size_t>(length - 1));
                }
                return family.c_str();
            }());
        if (font == nullptr) {
            DeleteDC(dc);
            return false;
        }
        const HGDIOBJ previous = SelectObject(dc, font);
        GLYPHMETRICS metrics{};
        MAT2 matrix{};
        // Keep the native GDI transform.  GGO_GRAY8_BITMAP scanlines are
        // already ordered top-to-bottom for this matrix; reversing them
        // turns every glyph upside down before it reaches the framebuffer.
        matrix.eM11.value = 1;
        matrix.eM22.value = 1;
        const DWORD format = GGO_GRAY8_BITMAP;
        const DWORD bytes = GetGlyphOutlineW(dc, codePoint, format, &metrics,
                                             0, nullptr, &matrix);
        if (bytes == GDI_ERROR || metrics.gmBlackBoxX <= 0 ||
            metrics.gmBlackBoxY <= 0) {
            SelectObject(dc, previous);
            DeleteObject(font);
            DeleteDC(dc);
            return false;
        }
        const int width = static_cast<int>(metrics.gmBlackBoxX);
        const int glyphHeight = static_cast<int>(metrics.gmBlackBoxY);
        const int rowStride = (width + 3) & ~3;
        const std::size_t requiredBytes = static_cast<std::size_t>(rowStride) *
                                           static_cast<std::size_t>(glyphHeight);
        std::vector<std::uint8_t> raw(
            std::max<std::size_t>(static_cast<std::size_t>(bytes),
                                  requiredBytes));
        if (GetGlyphOutlineW(dc, codePoint, format, &metrics,
                             static_cast<DWORD>(raw.size()), raw.data(),
                             &matrix) == GDI_ERROR) {
            SelectObject(dc, previous);
            DeleteObject(font);
            DeleteDC(dc);
            return false;
        }
        GlyphBitmap bitmap;
        bitmap.width = width;
        bitmap.height = glyphHeight;
        bitmap.bearingX = metrics.gmptGlyphOrigin.x;
        bitmap.bearingTop = metrics.gmptGlyphOrigin.y;
        bitmap.coverage.assign(static_cast<std::size_t>(width) *
                                   static_cast<std::size_t>(glyphHeight),
                               0);
        for (int row = 0; row < glyphHeight; ++row) {
            const int sourceRow = row;
            for (int col = 0; col < width; ++col) {
                const std::uint8_t level =
                    raw[static_cast<std::size_t>(sourceRow * rowStride + col)];
                bitmap.coverage[static_cast<std::size_t>(row * width + col)] =
                    static_cast<std::uint8_t>(
                        std::min(255, static_cast<int>(level) * 255 / 64));
            }
        }
        *out = std::move(bitmap);
        SelectObject(dc, previous);
        DeleteObject(font);
        DeleteDC(dc);
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
        "simhei.ttf", "segoeui.ttf", "seguisb.ttf", "seguisym.ttf",
        "seguiemj.ttf", "arial.ttf",
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
