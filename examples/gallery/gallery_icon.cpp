// Gallery icon asset encoders.
//
// The drawing master stays in gallery_icon.h because the runtime needs only
// RGBA pixels. File formats belong here: PNG is delegated to stb_image_write;
// ICO and ICNS only wrap those PNG payloads in their platform resource tables.

#include "stb_image_write.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "gallery_icon.h"

namespace lumen::examples {
namespace {

void appendU16LE(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void appendU32LE(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void appendU32BE(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

bool hasValidPixels(const GalleryIconBitmap& image) {
    if (image.size <= 0 || image.size > std::numeric_limits<int>::max() / 4) {
        return false;
    }
    const auto size = static_cast<std::size_t>(image.size);
    return image.rgba.size() == size * size * 4U;
}

void appendPngBytes(void* context, void* data, int byteCount) {
    if (context == nullptr || data == nullptr || byteCount <= 0) {
        return;
    }
    auto& output = *static_cast<std::vector<std::uint8_t>*>(context);
    const auto* begin = static_cast<const std::uint8_t*>(data);
    output.insert(output.end(), begin, begin + byteCount);
}

}  // namespace

std::vector<std::uint8_t> encodeGalleryIconPng(
    const GalleryIconBitmap& image) {
    if (!hasValidPixels(image)) {
        return {};
    }

    const int stride = image.size * 4;
    std::vector<std::uint8_t> png;
    if (stbi_write_png_to_func(&appendPngBytes, &png, image.size, image.size,
                               4, image.rgba.data(), stride) == 0 ||
        png.empty()) {
        return {};
    }
    return png;
}

std::vector<std::uint8_t> encodeGalleryIconIco(
    const std::vector<GalleryIconBitmap>& images) {
    if (images.empty() || images.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {};
    }

    std::vector<std::vector<std::uint8_t>> pngs;
    pngs.reserve(images.size());
    for (const GalleryIconBitmap& image : images) {
        if (image.size <= 0 || image.size > 256) {
            return {};
        }
        std::vector<std::uint8_t> png = encodeGalleryIconPng(image);
        if (png.empty()) {
            return {};
        }
        pngs.push_back(std::move(png));
    }

    std::vector<std::uint8_t> out;
    out.reserve(6U + 16U * images.size());
    appendU16LE(out, 0);  // reserved
    appendU16LE(out, 1);  // icon
    appendU16LE(out, static_cast<std::uint16_t>(images.size()));

    std::uint64_t offset = 6U + 16U * images.size();
    for (std::size_t i = 0; i < images.size(); ++i) {
        if (offset > std::numeric_limits<std::uint32_t>::max() ||
            pngs[i].size() > std::numeric_limits<std::uint32_t>::max()) {
            return {};
        }
        const int size = images[i].size;
        out.push_back(size == 256 ? 0 : static_cast<std::uint8_t>(size));
        out.push_back(size == 256 ? 0 : static_cast<std::uint8_t>(size));
        out.push_back(0);  // palette
        out.push_back(0);  // reserved
        appendU16LE(out, 1);   // planes
        appendU16LE(out, 32);  // bits per pixel
        appendU32LE(out, static_cast<std::uint32_t>(pngs[i].size()));
        appendU32LE(out, static_cast<std::uint32_t>(offset));
        offset += pngs[i].size();
    }
    for (const std::vector<std::uint8_t>& png : pngs) {
        out.insert(out.end(), png.begin(), png.end());
    }
    return out;
}

std::vector<std::uint8_t> encodeGalleryIconIcns(
    const std::vector<GalleryIconBitmap>& images) {
    struct Slot {
        int size;
        char type[4];
    };
    static constexpr Slot kSlots[] = {{16, {'i', 'c', 'p', '4'}},
                                      {32, {'i', 'c', '1', '1'}},
                                      {64, {'i', 'c', '1', '2'}},
                                      {128, {'i', 'c', '0', '7'}},
                                      {256, {'i', 'c', '0', '8'}}};

    std::vector<std::uint8_t> body;
    for (const Slot& slot : kSlots) {
        const auto it = std::find_if(
            images.begin(), images.end(), [slot](const GalleryIconBitmap& image) {
                return image.size == slot.size;
            });
        if (it == images.end()) {
            continue;
        }
        const std::vector<std::uint8_t> png = encodeGalleryIconPng(*it);
        if (png.empty() || png.size() > std::numeric_limits<std::uint32_t>::max() - 8U) {
            return {};
        }
        body.insert(body.end(), {static_cast<std::uint8_t>(slot.type[0]),
                                 static_cast<std::uint8_t>(slot.type[1]),
                                 static_cast<std::uint8_t>(slot.type[2]),
                                 static_cast<std::uint8_t>(slot.type[3])});
        appendU32BE(body, static_cast<std::uint32_t>(png.size() + 8U));
        body.insert(body.end(), png.begin(), png.end());
    }
    if (body.empty() || body.size() > std::numeric_limits<std::uint32_t>::max() - 8U) {
        return {};
    }

    std::vector<std::uint8_t> out{'i', 'c', 'n', 's'};
    appendU32BE(out, static_cast<std::uint32_t>(body.size() + 8U));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}  // namespace lumen::examples
