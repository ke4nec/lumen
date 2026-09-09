#pragma once

// Internal: the built-in 5x7 placeholder font shared by CpuRenderer and
// SkiaRenderer (plan §4.3 占位文本). Both backends draw the same glyph
// geometry so backend-consistency smoke tests are comparable.

#include <cstddef>
#include <cstdint>
#include <string>

namespace lumen::render::detail {

// True when glyph cell pixel (gx, gy) is lit for `codePoint`. Cells are 5x7
// for ASCII 32..126; non-ASCII code points render as a 4x5 box.
[[nodiscard]] bool glyphPixel(std::uint32_t codePoint, int gx, int gy);

// Decodes one UTF-8 code point starting at *i; advances past it.
[[nodiscard]] std::uint32_t decodeCodePoint(const std::string& text,
                                            std::size_t& i);

}  // namespace lumen::render::detail
