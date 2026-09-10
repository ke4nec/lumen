// 受限图片解码（plan §3.3: worker 只做文件读取、解码和校验）。
// PNG/JPEG/TGA/BMP/PSD 走 stb_image；.lumenrgba 是仓库自定义的原始格式：
// ASCII 头 "LUMENRGBA\n<width> <height>\n" + w*h*4 字节 straight RGBA。

#include "image_decode.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "stb_image.h"

namespace lumen::render::detail {

namespace {

std::vector<std::uint8_t> readFileBytes(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    std::uint8_t buffer[8192];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        bytes.insert(bytes.end(), buffer, buffer + read);
    }
    std::fclose(file);
    return bytes;
}

constexpr char kRawMagic[] = "LUMENRGBA\n";

bool decodeRaw(const std::vector<std::uint8_t>& bytes, PixelBuffer& out) {
    const std::size_t magicLength = std::strlen(kRawMagic);
    if (bytes.size() < magicLength ||
        std::memcmp(bytes.data(), kRawMagic, magicLength) != 0) {
        return false;
    }
    const char* cursor =
        reinterpret_cast<const char*>(bytes.data()) + magicLength;
    const char* end = reinterpret_cast<const char*>(bytes.data()) + bytes.size();
    long width = 0;
    long height = 0;
    char* parseEnd = nullptr;
    width = std::strtol(cursor, &parseEnd, 10);
    if (parseEnd == cursor || parseEnd >= end) {
        return false;
    }
    cursor = parseEnd;
    height = std::strtol(cursor, &parseEnd, 10);
    if (parseEnd == cursor || parseEnd >= end) {
        return false;
    }
    cursor = parseEnd;
    if (*cursor != '\n') {
        return false;
    }
    ++cursor;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        return false;
    }
    const std::uint64_t expected =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    const auto available = static_cast<std::uint64_t>(end - cursor);
    if (available != expected) {
        return false;
    }
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.rgba.assign(cursor, end);
    return true;
}

}  // namespace

ResourceError decodeImageFile(const std::string& path, PixelBuffer& out) {
    const std::vector<std::uint8_t> bytes = readFileBytes(path);
    if (bytes.empty()) {
        return ResourceError::FileOpenFailed;
    }
    if (decodeRaw(bytes, out)) {
        return ResourceError::None;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    // 强制 4 通道：与 CpuRenderer/SkiaRenderer 的 straight RGBA 契约一致。
    stbi_uc* decoded = stbi_load_from_memory(
        bytes.data(), static_cast<int>(bytes.size()), &width, &height,
        &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        return ResourceError::DecodeFailed;
    }
    out.width = width;
    out.height = height;
    const std::size_t byteCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    out.rgba.assign(decoded, decoded + byteCount);
    stbi_image_free(decoded);
    return ResourceError::None;
}

}  // namespace lumen::render::detail
