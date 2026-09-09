#include "lumen/render/renderer.h"

namespace lumen::render {

std::uint64_t frameHash(const PixelBuffer& buffer) {
    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    std::uint64_t hash = kFnvOffsetBasis;
    const auto mix = [&hash](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= static_cast<std::uint8_t>(value & 0xFFU);
            hash *= kFnvPrime;
            value >>= 8;
        }
    };
    mix(static_cast<std::uint64_t>(buffer.width));
    mix(static_cast<std::uint64_t>(buffer.height));
    for (const std::uint8_t byte : buffer.rgba) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

const char* rendererStageName() {
    return "stage2";
}

}  // namespace lumen::render
