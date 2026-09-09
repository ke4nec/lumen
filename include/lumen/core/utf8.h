#pragma once

#include <cstddef>
#include <string>

namespace lumen::core {

// UTF-8 helpers shared by text measurement (layout) and field editing
// (interaction). Malformed input degrades instead of failing: lengths count
// lead bytes, offsets clamp to the string bounds.
[[nodiscard]] std::size_t utf8Length(const std::string& text);
[[nodiscard]] std::size_t utf8OffsetAt(const std::string& text,
                                       std::size_t codePointIndex);
[[nodiscard]] std::string utf8Insert(const std::string& text,
                                     std::size_t codePointIndex,
                                     const std::string& inserted);
[[nodiscard]] std::string utf8EraseBefore(const std::string& text,
                                          std::size_t codePointIndex);
[[nodiscard]] std::string utf8EraseAfter(const std::string& text,
                                         std::size_t codePointIndex);

}  // namespace lumen::core
