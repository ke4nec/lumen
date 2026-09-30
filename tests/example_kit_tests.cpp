// R9（completion-plan 缺口矩阵）：示例共享组件 kit 的契约测试——
// mutedLabel/errorText 与提取前逐字节同构（Gallery/Settings 既有同款），
// statusLine 为"Label: value"状态摘要的 单点实现（空值占位 "-"）。
// 视觉值全部经 Theme token（StyleOverrides），无硬编码色。

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "example_kit.h"
#include "lumen/core/widget.h"
#include "lumen/style/theme.h"

using lumen::examples::errorText;
using lumen::examples::mutedLabel;
using lumen::examples::statusLine;
using namespace lumen;

TEST_CASE("example_kit_text_components_carry_theme_overrides",
          "[examples][r9]") {
    const style::Theme theme = style::Theme::dark();

    // mutedLabel：Text + contentSecondary 覆盖（提取前同构）。
    const core::Widget muted = mutedLabel("hint text", theme);
    CHECK(muted.type == core::WidgetType::Text);
    CHECK(muted.text == "hint text");
    CHECK(muted.styleOverrides.foreground == theme.colors.contentSecondary);

    // errorText：errorContent + caption 档（提取前同构）。
    const core::Widget error = errorText("invalid email", theme);
    CHECK(error.text == "invalid email");
    CHECK(error.styleOverrides.foreground == theme.colors.errorContent);
    CHECK(error.styleOverrides.text.has_value());
    CHECK(*error.styleOverrides.text == theme.typography.caption);
}

TEST_CASE("example_kit_status_line_formats_and_placeholders",
          "[examples][r9]") {
    const style::Theme theme = style::Theme::dark();

    const core::Widget line = statusLine("Volume", "40", theme);
    CHECK(line.type == core::WidgetType::Text);
    CHECK(line.text == "Volume: 40");
    CHECK(line.styleOverrides.foreground == theme.colors.contentSecondary);

    // 空值占位 "-"（不隐藏缺失）。
    const core::Widget empty = statusLine("Last file", "", theme);
    CHECK(empty.text == "Last file: -");
}
