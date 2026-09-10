#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lumen/accessibility/bridge.h"
#include "lumen/core/geometry.h"
#include "lumen/core/state.h"
#include "lumen/core/widget.h"

namespace lumen::widgets {

// v0.3 阶段8D (plan §3.4): Theme token。组件不直接读取 SDL 或系统主题
// API；高对比/字体缩放由 AccessibilitySettings 派生（阶段8C 只读查询）。
struct Theme {
    // 表面与文本。
    core::Color pageBackground{24, 24, 27, 255};
    core::Color surface{39, 39, 46, 255};
    core::Color surfaceElevated{52, 52, 62, 255};
    core::Color text{228, 228, 234, 255};
    core::Color textMuted{140, 140, 152, 255};
    // 主操作色。
    core::Color primary{86, 140, 240, 255};
    core::Color onPrimary{240, 244, 255, 255};
    // 状态。
    core::Color error{224, 90, 96, 255};
    core::Color barrier{0, 0, 0, 132};
    // 间距 token（逻辑像素）。
    float spacingUnit{8.0F};
    // 排版。
    core::TextStyle titleStyle{core::TextStyle{}};   // 20px
    core::TextStyle bodyStyle{core::TextStyle{}};    // 14px
    core::TextStyle captionStyle{core::TextStyle{}}; // 12px

    [[nodiscard]] static Theme dark();
    [[nodiscard]] static Theme light();
    // 按可访问性设置派生：高对比增强分隔，字体缩放放大排版。
    [[nodiscard]] static Theme fromSettings(
        const accessibility::AccessibilitySettings& settings,
        bool darkMode = true);
};

// 深度优先把 Theme token 应用到未着色的文本与容器（显式颜色保留）。
// 应用在 applyBinds 之后、layout 之前调用。
void applyTheme(core::Widget& root, const Theme& theme);

// Theme 感知的常用构建器（薄封装，语义与内置组件一致）。
[[nodiscard]] core::Widget themedButton(std::string label,
                                        std::string onClick,
                                        const Theme& theme,
                                        std::string key = {});
[[nodiscard]] core::Widget themedTextField(std::string bind,
                                           std::string placeholder,
                                           const Theme& theme,
                                           std::string key = {});
[[nodiscard]] core::Widget themedLabel(std::string text, const Theme& theme,
                                       bool muted = false);
[[nodiscard]] core::Widget themedTitle(std::string text, const Theme& theme);
// 表单错误提示行（text 为空时不渲染内容）。
[[nodiscard]] core::Widget themedError(std::string text, const Theme& theme);

}  // namespace lumen::widgets
