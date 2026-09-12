#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen::text {

// M1 双向段落处理（UAX#9 确定性子集）。
//
// 目标：混合 LTR/RTL 文本的命中测试可靠，编辑索引仍为 grapheme 边界。
// 非目标：完整 UBA（显式嵌入控制、镜像括号、数字定形仅近似）。
//
// - 强方向类：L（拉丁/CJK/emoji/符号）、R（希伯来）、AL（阿拉伯，视同 R）。
// - 弱类 EN（ASCII 数字）视同 L，AN（阿拉伯指示数字）视同 R。
// - 中性字符（空格/标点）取两侧强方向，歧义时取段落方向。
// - 视觉序按 UBA L2 重排（奇数 level 段逆序）；grapheme 边界不变。

enum class BidiClass : std::uint8_t {
    L,
    R,
    Neutral,
};

[[nodiscard]] BidiClass bidiClassFor(char32_t codePoint);

struct BidiRun {
    // 逻辑 cluster 范围。
    std::size_t start{0};
    std::size_t length{0};
    // 嵌入 level：偶数 LTR，奇数 RTL。
    int level{0};

    bool operator==(const BidiRun&) const = default;
};

// 按段落基础方向计算逻辑 cluster 的 level runs。
// clusters: 每 cluster 的首强方向码点（调用方按 grapheme 首强字符分类）。
[[nodiscard]] std::vector<BidiRun> resolveBidiRuns(
    const std::vector<BidiClass>& clusterClasses, bool baseRtl);

// 逻辑顺序 → 视觉顺序（从左到右的逻辑索引序列）。
[[nodiscard]] std::vector<std::size_t> visualOrder(
    std::size_t count, const std::vector<BidiRun>& runs);

}  // namespace lumen::text
