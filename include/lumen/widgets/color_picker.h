#pragma once

// G-4（gap-backlog）：ColorPicker——色板 + H/S/V 滑条组合件。
//
// 纯组合件（零新增 WidgetType/RenderCommand，docs/lumen-color-picker-design.md）：
//   - 色板行：预置色 swatch（Button + 色值），点击直接落结果（H/S/V 同步
//     反推，滑条跟随）；
//   - H/S/V 三滑条（0..100 整数 bind；H ×3.6、S/V ÷100 派生）；
//   - 预览 swatch + 结果 hex 文本（bind）。
// 结果 = "#RRGGBB" 写入 resultBind；onPicked 回调同路径。全部视觉取自
// Theme 既有 token（swatch 背景为数据色——设计例外，见设计文档 §3）。

#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/geometry.h"
#include "lumen/core/widget.h"
#include "lumen/style/theme.h"

namespace lumen::widgets {

// HSV ↔ RGB 纯函数（测试锁定；h 0..360、s/v 0..1）。
[[nodiscard]] core::Color hsvToRgb(float hueDeg, float saturation,
                                   float value);
struct Hsv {
    float h{0.0F};
    float s{0.0F};
    float v{0.0F};
};
[[nodiscard]] Hsv rgbToHsv(const core::Color& color);
// "#RRGGBB" ↔ Color（非法输入 = 黑色；解析大小写不敏感）。
[[nodiscard]] std::string colorToHex(const core::Color& color);
[[nodiscard]] core::Color colorFromHex(const std::string& hex);

class ColorPickerController {
  public:
    explicit ColorPickerController(std::string resultBind,
                                   std::string key = "color-picker");

    // 组合件（色板 + 滑条 + 预览）。装配前可 setPalette。shell 读结果
    // bind 当前值（预览/hex 文本随重建刷新——Color 是值类型不走 bind）。
    [[nodiscard]] core::Widget build(
        app::AppShell& shell, const style::Theme& theme) const;
    // 装配：滑条观察（h/s/v 变化 → 派生 hex）与色板 handler。幂等。
    void attach(app::AppShell& shell);

    // 预置色板（"#RRGGBB"）；默认 12 色（Material 基准减饱和）。
    void setPalette(std::vector<std::string> hexColors);
    // 当前结果（bind 值）。
    [[nodiscard]] std::string result(const app::AppShell& shell) const;

    // 选中回调（UI 线程；参数 = "#RRGGBB"；色板点击与滑条派生同路径）。
    std::function<void(const std::string&)> onPicked{};

  private:
    [[nodiscard]] std::string channelBind(char channel) const {
        return key_ + ":" + channel;
    }
    [[nodiscard]] std::string swatchHandler(std::size_t index) const {
        return key_ + ":swatch:" + std::to_string(index);
    }
    void applyResult(app::AppShell& shell, const core::Color& color);

    std::string resultBind_{};
    std::string key_{};
    std::vector<std::string> palette_{};
    std::vector<std::string> handlers_{};
    core::StateStore::ObserverId observerH_{0};
    core::StateStore::ObserverId observerS_{0};
    core::StateStore::ObserverId observerV_{0};
};

}  // namespace lumen::widgets
