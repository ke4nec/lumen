// M9：移动字体后端（stb_truetype 光栅化系统字体；SDL-free，无 Skia）。

#include "lumen/text/mobile_font_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <unordered_map>

#ifdef LUMEN_HAS_STB_TRUETYPE
#include "stb_truetype.h"
#endif

#include "lumen/text/grapheme.h"

namespace lumen::text {
namespace {

#ifdef LUMEN_HAS_STB_TRUETYPE

// 字体文件条目：一个文件可含多 face（.ttc）；每 face 记录其家族名与
// stbtt 字体信息（偏移量形式——stb 对 ttc 用 fontOffset）。
struct FaceEntry {
    std::string family{};
    std::string path{};
    int fontIndex{0};
    stbtt_fontinfo info{};
    int unitsPerEm{0};
    int ascentEm{0};
    int descentEm{0};
};

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

// 文件名 → 粗略 family（"NotoSansCJK-Regular.ttc" → "notosanscjk"）。
std::string familyFromFile(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
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

bool isBoldFile(const std::string& path) {
    const std::string lower = lowerAscii(path);
    return lower.find("bold") != std::string::npos ||
           lower.find("-black") != std::string::npos ||
           lower.find("-heavy") != std::string::npos;
}

class MobileFontManager final : public FontManager {
  public:
    explicit MobileFontManager(std::vector<FaceEntry> faces,
                               std::string diagnostic)
        : faces_(std::move(faces)),
          diagnostic_(std::move(diagnostic)) {}

    [[nodiscard]] FontBackend backend() const override {
        return FontBackend::Mobile;
    }
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
                "no mobile font covers U+" +
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
        const float scale = stbtt_ScaleForMappingEmToPixels(
            &face->info, query.sizePx > 0.0F ? query.sizePx : 14.0F);
        int advance = 0;
        int leftSideBearing = 0;
        const int glyph = stbtt_FindGlyphIndex(
            &face->info, static_cast<int>(codePoint));
        if (glyph == 0) {
            return false;
        }
        stbtt_GetGlyphHMetrics(&face->info, glyph, &advance,
                               &leftSideBearing);
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
                             ? static_cast<float>(face->descentEm) /
                                   static_cast<float>(face->unitsPerEm)
                             : 0.2F;
        (void)scale;
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
        const float scale =
            stbtt_ScaleForMappingEmToPixels(&face->info, size);
        if (ascentPx != nullptr) {
            *ascentPx = static_cast<float>(face->ascentEm) * scale;
        }
        if (descentPx != nullptr) {
            *descentPx = static_cast<float>(face->descentEm) * scale;
        }
        return true;
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
        return "backend=mobile faces=" + std::to_string(faces_.size()) +
               " " + diagnostic_;
    }

  private:
    // 覆盖码点的 face：请求族名（文件名近似）优先；空请求走平台默认
    // 栈顺序，其后 CJK/emoji 专用 face，最后任意覆盖者。粗细按文件名
    // 粗略匹配。
    [[nodiscard]] const FaceEntry* faceFor(const FontQuery& query,
                                           char32_t codePoint) const {
        const std::string requested = lowerAscii(query.family);
        const bool wantBold =
            static_cast<int>(query.weight) >= 600;
        if (requested.empty()) {
            // 平台默认栈优先：保证空 family 就有确定、可预期的字体，
            // 而非依赖目录 ls 顺序。
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
            const int glyph = stbtt_FindGlyphIndex(
                &face.info, static_cast<int>(codePoint));
            if (glyph == 0) {
                continue;  // 不覆盖该码点。
            }
            if (!requested.empty() && face.family == requested &&
                (wantBold == isBoldFile(face.path) || fallback == nullptr)) {
                if (wantBold == isBoldFile(face.path)) {
                    return &face;  // 族名 + 粗细双匹配。
                }
            }
            if (fallback == nullptr) {
                fallback = &face;
            }
            // CJK/emoji 优先于拉丁默认（码点非 ASCII 且 face 名含 cjk/
            // emoji 时立即采用）。
            const bool isCjkOrEmojiFace =
                face.family.find("cjk") != std::string::npos ||
                face.family.find("emoji") != std::string::npos;
            const bool nonAscii = codePoint > 0x7F;
            if (nonAscii && isCjkOrEmojiFace) {
                return &face;
            }
        }
        return fallback;
    }

    std::vector<FaceEntry> faces_{};
    std::string diagnostic_{};
};

// 目录扫描：读字体文件（.ttf/.otf/.ttc/.otc），stbtt 打开并登记全部
// face。文件数上限与单文件大小上限防御性设置（系统目录可被恶意填充）。
constexpr std::size_t kMaxFiles = 256;
constexpr std::size_t kMaxFileBytes = 64U * 1024U * 1024U;

std::vector<char> readFile(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0 || static_cast<std::size_t>(size) > kMaxFileBytes) {
        std::fclose(file);
        return {};
    }
    std::vector<char> bytes(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
    if (read != bytes.size()) {
        return {};
    }
    return bytes;
}

std::vector<std::string> defaultDirectories() {
    return {"/system/fonts", "/System/Library/Fonts", "/usr/share/fonts"};
}

std::string joinPath(const std::string& dir, const char* name) {
    if (!dir.empty() && dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

std::vector<FaceEntry> loadDirectory(const std::string& dir) {
    std::vector<FaceEntry> faces;
    std::string listing;
    // POSIX 目录扫描（无 <filesystem> 依赖歧义；Android Bionic 支持）。
    // Windows 桌面构建同样编译本文件（未使用），_popen 保证可编译。
    std::string cmd = "ls -1 '" + dir + "' 2>/dev/null";
#ifdef _WIN32
    std::FILE* pipe = _popen(cmd.c_str(), "r");
#else
    std::FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return faces;
    }
    char line[512];
    while (std::fgets(line, sizeof(line), pipe) != nullptr &&
           faces.size() < kMaxFiles) {
        std::string name(line);
        while (!name.empty() &&
               (name.back() == '\n' || name.back() == '\r')) {
            name.pop_back();
        }
        const std::string lower = lowerAscii(name);
        const bool isFont =
            lower.size() > 4 &&
            (lower.compare(lower.size() - 4, 4, ".ttf") == 0 ||
             lower.compare(lower.size() - 4, 4, ".otf") == 0 ||
             lower.compare(lower.size() - 4, 4, ".ttc") == 0 ||
             lower.compare(lower.size() - 4, 4, ".otc") == 0);
        if (!isFont) {
            continue;
        }
        const std::string path = joinPath(dir, name.c_str());
        std::vector<char> bytes = readFile(path);
        if (bytes.empty()) {
            continue;
        }
        int count = 0;
        const int offset = stbtt_GetNumberOfFonts(
            reinterpret_cast<const unsigned char*>(bytes.data()));
        if (offset >= 0) {
            // stbtt_GetNumberOfFonts 返回 ttc face 数；单文件返回 0
            //（stbtt 契约：ttc 返回正数，否则 0 且 offset 即 0 号 face）。
            count = offset > 0 ? offset : 1;
        }
        if (count <= 0 || count > 64) {
            count = 1;
        }
        for (int face = 0; face < count &&
                           faces.size() < kMaxFiles; ++face) {
            FaceEntry entry;
            entry.path = path;
            entry.fontIndex = face;
            const int fontOffset = stbtt_GetFontOffsetForIndex(
                reinterpret_cast<const unsigned char*>(bytes.data()),
                face);
            if (fontOffset < 0) {
                break;
            }
            if (stbtt_InitFont(&entry.info,
                               reinterpret_cast<const unsigned char*>(
                                   bytes.data()),
                               fontOffset) == 0) {
                break;
            }
            // 家族名：name 表的 family（id=1，Windows UCS-2）优先，
            // 回退文件名。
            int nameLength = 0;
            const char* name1 = stbtt_GetFontNameString(
                &entry.info, &nameLength, 3, 1, 0x409, 1);
            if (name1 != nullptr && nameLength > 0 && nameLength <= 512) {
                // UTF-16BE：高位为 0 的 ASCII 对半取低字节（CJK 族名无
                // ASCII 覆盖时留空，调用方回退文件名）。
                std::string family;
                for (int c = 0; c + 1 < nameLength && family.size() < 64;
                     c += 2) {
                    const auto hi =
                        static_cast<unsigned char>(name1[c]);
                    const auto lo =
                        static_cast<unsigned char>(name1[c + 1]);
                    if (hi == 0 && lo >= ' ' && lo < '\x7F') {
                        family.push_back(static_cast<char>(std::tolower(
                            static_cast<unsigned char>(lo))));
                    }
                }
                while (!family.empty() && family.front() == ' ') {
                    family.erase(family.begin());
                }
                while (!family.empty() && family.back() == ' ') {
                    family.pop_back();
                }
                if (!family.empty()) {
                    entry.family = family;
                }
            }
            if (entry.family.empty()) {
                entry.family = familyFromFile(name);
            }
            // head 表 unitsPerEm（stbtt_fontinfo 无该成员，手动大端读）。
            entry.unitsPerEm = 1000;
            if (entry.info.head > 0) {
                const unsigned char* head =
                    entry.info.data + entry.info.head;
                entry.unitsPerEm =
                    (static_cast<int>(head[18]) << 8) | head[19];
                if (entry.unitsPerEm <= 0) {
                    entry.unitsPerEm = 1000;
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
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return faces;
}

#endif  // LUMEN_HAS_STB_TRUETYPE

}  // namespace

std::unique_ptr<FontManager> createMobileFontManager(
    std::string* diagnostic, std::vector<std::string> directories) {
#ifdef LUMEN_HAS_STB_TRUETYPE
    if (directories.empty()) {
        directories = defaultDirectories();
    }
    std::vector<FaceEntry> faces;
    std::string usedDir;
    for (const auto& dir : directories) {
        faces = loadDirectory(dir);
        if (!faces.empty()) {
            usedDir = dir;
            break;
        }
    }
    if (faces.empty()) {
        if (diagnostic != nullptr) {
            *diagnostic = "mobile fonts: no font files in " +
                          directories.front();
        }
        return nullptr;
    }
    if (diagnostic != nullptr) {
        *diagnostic = "mobile fonts: " + usedDir + " (" +
                      std::to_string(faces.size()) + " faces)";
    }
    return std::make_unique<MobileFontManager>(std::move(faces),
                                               usedDir);
#else
    if (diagnostic != nullptr) {
        *diagnostic = "mobile fonts: stb_truetype unavailable";
    }
    return nullptr;
#endif
}

}  // namespace lumen::text
