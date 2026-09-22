// Gallery 图标资源导出工具（docs/lumen-gallery-icon-design.md §3）。
//
// 构建期运行（examples/gallery/CMakeLists.txt custom command），从
// gallery_icon.h 母版导出平台资源；产物只进构建/安装树，不提交仓库
// （AGENTS：不提交生成二进制）：
//   lumen-gallery-<size>.png × 8   —— Linux hicolor 按档安装
//   lumen-gallery.ico（8 尺寸）    —— Windows exe 资源（.rc 引用）
//   lumen-gallery.icns（5 槽位）   —— macOS .app bundle（后续接入）
// 用法：lumen-gallery-icons --out-dir <dir>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "gallery_icon.h"

namespace {

namespace fs = std::filesystem;

bool writeFile(const fs::path& path,
               const std::vector<std::uint8_t>& bytes) {
    FILE* file = std::fopen(path.string().c_str(), "wb");  // NOLINT(*-owning-memory)
    if (file == nullptr) {
        return false;
    }
    const bool ok = bytes.empty() ||
                    std::fwrite(bytes.data(), 1, bytes.size(), file) ==
                        bytes.size();
    std::fclose(file);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path outDir;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--out-dir" && i + 1 < argc) {
            outDir = argv[++i];
        }
    }
    if (outDir.empty()) {
        std::fprintf(stderr, "usage: lumen-gallery-icons --out-dir <dir>\n");
        return 2;
    }
    std::error_code ec;
    fs::create_directories(outDir, ec);
    if (ec) {
        std::fprintf(stderr, "icons: cannot create %s (%s)\n",
                     outDir.string().c_str(), ec.message().c_str());
        return 1;
    }

    std::vector<lumen::examples::GalleryIconBitmap> bitmaps;
    for (const int size : lumen::examples::kGalleryIconExportSizes) {
        lumen::examples::GalleryIconBitmap bitmap =
            lumen::examples::renderGalleryIcon(size);
        if (!writeFile(outDir / ("lumen-gallery-" + std::to_string(size) + ".png"),
                       lumen::examples::encodeGalleryIconPng(bitmap))) {
            std::fprintf(stderr, "icons: failed to write %d px png\n", size);
            return 1;
        }
        bitmaps.push_back(std::move(bitmap));
    }
    if (!writeFile(outDir / "lumen-gallery.ico",
                   lumen::examples::encodeGalleryIconIco(bitmaps))) {
        std::fprintf(stderr, "icons: failed to write ico\n");
        return 1;
    }
    if (!writeFile(outDir / "lumen-gallery.icns",
                   lumen::examples::encodeGalleryIconIcns(bitmaps))) {
        std::fprintf(stderr, "icons: failed to write icns\n");
        return 1;
    }
    std::printf("icons: wrote %zu png + ico + icns to %s\n", bitmaps.size(),
                outDir.string().c_str());
    return 0;
}
