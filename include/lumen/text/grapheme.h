#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lumen::text {

// v0.3 阶段8B (plan §3.2): grapheme cluster 与 UTF-8 工具。
//
// 编辑模型（TextEditingValue）与文本布局都以 grapheme cluster 索引为
// 稳定单位；平台字节偏移转换只发生在适配层。分段是扩展 grapheme
// cluster 的确定性近似：组合附加符号（Mn/Mc/Me 常用区段）、ZWJ emoji
// 序列、变体选择符、肤色修饰符、区域指示符（旗帜）配对、Hangul
// 音节组分与 CRLF 均并入同一 cluster；混合方向重排不在边界判定内。

struct Utf8Validation {
    bool valid{true};
    // 首个非法字节的偏移；valid 时为 0。
    std::size_t errorByteOffset{0};
    std::string description{};
};

// 严格 UTF-8 校验（plan §5.1：UTF-8 错误）。拒绝截断序列、超长编码、
// 代理区码点与超过 U+10FFFF 的值。
[[nodiscard]] Utf8Validation validateUtf8(const std::string& text);

struct DecodedCodePoint {
    char32_t codePoint{0};
    std::size_t bytes{1};
};

// 解码；非法序列按 U+FFFD、长度 1 处理（降级不失败）。
[[nodiscard]] std::vector<DecodedCodePoint> decodeUtf8(
    const std::string& text);

// grapheme 边界（字节偏移）。结果长度恒为 graphemeCount+1，首元素 0，
// 末元素 text.size()。
[[nodiscard]] std::vector<std::size_t> graphemeBoundaries(
    const std::string& text);

[[nodiscard]] std::size_t graphemeCount(const std::string& text);

// [beginGrapheme, endGrapheme) 的字节切片；参数先夹取到边界。
[[nodiscard]] std::string graphemeSubstring(const std::string& text,
                                            std::size_t beginGrapheme,
                                            std::size_t endGrapheme);

// grapheme 索引 → 字节偏移（越界夹取）。
[[nodiscard]] std::size_t graphemeByteOffset(const std::string& text,
                                             std::size_t graphemeIndex);

// 字节偏移 → grapheme 索引；非边界偏移向后取所在 cluster。
[[nodiscard]] std::size_t graphemeIndexOfByte(const std::string& text,
                                              std::size_t byteOffset);

// 词边界（双击选中与 Ctrl+左右跳词用）：ASCII 字母数字连续段为一个词，
// CJK/emoji 每个 cluster 自成一个词，空白/标点不属于任何词。
[[nodiscard]] std::size_t wordRangeContaining(const std::string& text,
                                              std::size_t graphemeIndex,
                                              std::size_t* endGrapheme);

}  // namespace lumen::text
