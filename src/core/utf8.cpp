#include "lumen/core/utf8.h"

#include <algorithm>

namespace lumen::core {
namespace {

// Advances one code point from byte position i; lead bytes determine the
// length, continuation bytes are skipped as part of it.
std::size_t codePointSizeAt(const std::string& text, std::size_t i) {
    const auto byte = static_cast<unsigned char>(text[i]);
    if ((byte & 0x80U) == 0U) {
        return 1;
    }
    if ((byte & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((byte & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((byte & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 1;
}

bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
}

}  // namespace

std::size_t utf8Length(const std::string& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        i += codePointSizeAt(text, i);
        ++count;
    }
    return count;
}

std::size_t utf8OffsetAt(const std::string& text, std::size_t codePointIndex) {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < text.size() && count < codePointIndex) {
        i += codePointSizeAt(text, i);
        ++count;
    }
    // Truncated multi-byte sequences can advance past the end; clamp so the
    // returned offset is always a valid string position.
    return std::min(i, text.size());
}

std::string utf8Insert(const std::string& text, std::size_t codePointIndex,
                       const std::string& inserted) {
    const std::size_t at = utf8OffsetAt(text, codePointIndex);
    return text.substr(0, at) + inserted + text.substr(at);
}

std::string utf8EraseBefore(const std::string& text,
                            std::size_t codePointIndex) {
    std::size_t at = utf8OffsetAt(text, codePointIndex);
    if (at == 0) {
        return text;
    }
    std::size_t start = at - 1;
    while (start > 0 && isContinuation(text[start])) {
        --start;
    }
    return text.substr(0, start) + text.substr(at);
}

std::string utf8EraseAfter(const std::string& text,
                           std::size_t codePointIndex) {
    const std::size_t at = utf8OffsetAt(text, codePointIndex);
    if (at >= text.size()) {
        return text;
    }
    const std::size_t end = std::min(at + codePointSizeAt(text, at),
                                     text.size());
    return text.substr(0, at) + text.substr(end);
}

}  // namespace lumen::core
