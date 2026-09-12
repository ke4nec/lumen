#include "lumen/text/bidi.h"

#include <algorithm>

namespace lumen::text {

BidiClass bidiClassFor(char32_t cp) {
    // 希伯来文：强 R。
    if ((cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0xFB1D && cp <= 0xFB4F)) {
        return BidiClass::R;
    }
    // 阿拉伯文及相关：视同 R（AL 按 R 处理，M1 近似）。
    if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
        (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
        (cp >= 0xFE70 && cp <= 0xFEFF)) {
        return BidiClass::R;
    }
    // 阿拉伯指示数字：视同 R（AN 近似）。
    if (cp >= 0x0660 && cp <= 0x0669) {
        return BidiClass::R;
    }
    // ASCII 数字归 L（EN 近似为 L，保证数字不产生额外 level）。
    if (cp >= 0x30 && cp <= 0x39) {
        return BidiClass::L;
    }
    // 空白/标点/符号：中性，消解时取两侧强方向。
    if (cp == 0x20 || cp == 0x09 || cp == 0x0A || cp == 0x0D ||
        cp == 0x3000) {
        return BidiClass::Neutral;
    }
    if ((cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) ||
        (cp >= 0x5B && cp <= 0x60) || (cp >= 0x7B && cp <= 0x7E) ||
        (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x3000 && cp <= 0x303F) ||
        (cp >= 0xFF00 && cp <= 0xFFEF)) {
        return BidiClass::Neutral;
    }
    // 其余一律按 L 处理：拉丁/CJK/假名/Hangul/emoji。
    return BidiClass::L;
}

std::vector<BidiRun> resolveBidiRuns(
    const std::vector<BidiClass>& clusterClasses, bool baseRtl) {
    const std::size_t count = clusterClasses.size();
    if (count == 0) {
        return {};
    }
    const int paragraphLevel = baseRtl ? 1 : 0;
    // N1–N2 近似：中性取两侧强方向一致值，否则取段落方向。
    std::vector<BidiClass> resolved = clusterClasses;
    for (std::size_t i = 0; i < count; ++i) {
        if (resolved[i] != BidiClass::Neutral) {
            continue;
        }
        BidiClass prev = BidiClass::Neutral;
        for (std::size_t k = i; k-- > 0;) {
            if (clusterClasses[k] != BidiClass::Neutral) {
                prev = clusterClasses[k];
                break;
            }
        }
        BidiClass next = BidiClass::Neutral;
        for (std::size_t k = i + 1; k < count; ++k) {
            if (clusterClasses[k] != BidiClass::Neutral) {
                next = clusterClasses[k];
                break;
            }
        }
        if (prev != BidiClass::Neutral && prev == next) {
            resolved[i] = prev;
        } else {
            resolved[i] = baseRtl ? BidiClass::R : BidiClass::L;
        }
    }
    // W1–W7 近似：强字符定 level。
    std::vector<int> levels(count, paragraphLevel);
    for (std::size_t i = 0; i < count; ++i) {
        if (resolved[i] == BidiClass::R) {
            levels[i] = 1;
        } else {
            levels[i] = baseRtl ? 2 : 0;
        }
    }
    // 压缩为 runs。
    std::vector<BidiRun> runs;
    std::size_t runStart = 0;
    for (std::size_t i = 1; i <= count; ++i) {
        if (i == count || levels[i] != levels[runStart]) {
            runs.push_back(BidiRun{runStart, i - runStart, levels[runStart]});
            runStart = i;
        }
    }
    return runs;
}

std::vector<std::size_t> visualOrder(std::size_t count,
                                     const std::vector<BidiRun>& runs) {
    std::vector<std::size_t> order;
    order.reserve(count);
    for (const BidiRun& run : runs) {
        for (std::size_t i = 0; i < run.length; ++i) {
            order.push_back(run.start + i);
        }
    }
    if (order.size() != count) {
        order.clear();
        for (std::size_t i = 0; i < count; ++i) {
            order.push_back(i);
        }
        return order;
    }
    auto levelOf = [&runs](std::size_t logical) {
        for (const BidiRun& run : runs) {
            if (logical >= run.start && logical < run.start + run.length) {
                return run.level;
            }
        }
        return 0;
    };
    // UBA L2：从最高 level 向最低奇数 level（含）逐层逆序“level ≥ 当前”
    // 的连续段。RTL 段落（基准 level 1）因此整体逆序；LTR 段落中的
    // 嵌入 R 段只逆序自身。全 L（level 0）不发生重排。
    int maxLevel = 0;
    for (const BidiRun& run : runs) {
        maxLevel = std::max(maxLevel, run.level);
    }
    for (int level = maxLevel; level >= 1; --level) {
        std::size_t i = 0;
        while (i < order.size()) {
            if (levelOf(order[i]) < level) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < order.size() && levelOf(order[j]) >= level) {
                ++j;
            }
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i),
                         order.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }
    }
    return order;
}

}  // namespace lumen::text
