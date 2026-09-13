#pragma once

#include <string>

#include "lumen/accessibility/bridge.h"
#include "lumen/core/style.h"
#include "lumen/core/widget.h"
#include "lumen/style/state.h"
#include "lumen/style/theme.h"

namespace lumen::style {

// M6：ThemeScope 布局期主题覆盖（UI 线程；子树解析切换到覆盖主题，
// 析构恢复父主题；ThemeScope Widget 的 shared_ptr<void> 由布局层还原）。
class ScopedThemeOverride {
  public:
    explicit ScopedThemeOverride(const Theme& theme);
    ~ScopedThemeOverride();
    ScopedThemeOverride(const ScopedThemeOverride&) = delete;
    ScopedThemeOverride& operator=(const ScopedThemeOverride&) = delete;

  private:
    const Theme* previous_;
};

// 样式解析入口（docs/lumen-visual-system-design.md §5）。
//
// LayoutEngine 在布局前对每个节点解析一次样式并写入 RenderNode；resolver
// 不修改 Widget（声明式输入），RenderNode 是本帧的不可变结果。identity
// 是 layout 分配的稳定路径，用于交互快照查询。

struct StyleContext {
    const Theme& theme;
    const InteractionStateSnapshot& interaction;
    const accessibility::AccessibilitySettings& accessibility;
    float deviceScale{1.0F};
};

// 解析 Widget 在 (Theme × 交互状态 × 可访问性) 下的最终样式。
// identity 为节点稳定标识（layout 逐层拼接）；传入空串时交互状态全部
// 为 false（headless/度量场景）。
[[nodiscard]] core::ResolvedStyle resolveStyle(const core::Widget& widget,
                                               const StyleContext& context,
                                               const std::string& identity);

// 便捷查询：上下文中的高对比度增强（焦点环加宽、边框增强）。
[[nodiscard]] inline bool highContrastActive(
    const accessibility::AccessibilitySettings& settings) {
    return settings.highContrast;
}

}  // namespace lumen::style
