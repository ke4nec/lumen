#pragma once

// R9（completion-plan 缺口矩阵 · m15-roadmap §1.1 池）：示例共享小组件
// kit——Settings/Gallery 反复出现的文本/状态摘要模式提取为单点实现
//（此前两应用的 mutedLabel/errorText 逐字节重复）。输出与提取前完全
// 一致（既有示例测试/帧哈希不动即回归证据）；组件保持 Theme token 取
// 色，不引入硬编码。header-only、示例层（不进公共框架库）——模板
// （examples/template）按需取用即可保持简短。

#include <string>
#include <utility>

#include "lumen/core/widget.h"
#include "lumen/style/theme.h"

namespace lumen::examples {

// 辅助说明文本（contentSecondary；两应用既有同款逐字节提取）。
[[nodiscard]] inline core::Widget mutedLabel(std::string text,
                                             const style::Theme& theme) {
    core::StyleOverrides overrides;
    overrides.foreground = theme.colors.contentSecondary;
    return core::withStyleOverrides(core::makeText(std::move(text)),
                                    std::move(overrides));
}

// 校验错误/告警文本（errorContent + caption 档；两应用既有同款提取）。
[[nodiscard]] inline core::Widget errorText(std::string text,
                                            const style::Theme& theme) {
    core::StyleOverrides overrides;
    overrides.foreground = theme.colors.errorContent;
    overrides.text = theme.typography.caption;
    return core::withStyleOverrides(core::makeText(std::move(text)),
                                    std::move(overrides));
}

// 状态摘要行（"Label: value" 一段式；Settings 的 Volume/Progress 与
// Gallery 的状态回显共用）。值可空——空值呈现 "Label: -"（占位诚实，
// 不隐藏缺失）。
[[nodiscard]] inline core::Widget statusLine(const std::string& label,
                                             const std::string& value,
                                             const style::Theme& theme) {
    const std::string shown =
        label + ": " + (value.empty() ? std::string{"-"} : value);
    return mutedLabel(shown, theme);
}

}  // namespace lumen::examples
