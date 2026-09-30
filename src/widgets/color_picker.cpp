// G-4：ColorPicker 实现（契约见 color_picker.h 与
// docs/lumen-color-picker-design.md）。纯组合件：色板 Button 行 +
// H/S/V Slider + 预览 swatch；HSV 派生写结果 bind，色板点击反推滑条。

#include "lumen/widgets/color_picker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/style/tokens.h"

namespace lumen::widgets {

core::Color hsvToRgb(float hueDeg, float saturation, float value) {
    const float h = std::fmod(std::fmax(hueDeg, 0.0F), 360.0F);
    const float s = std::clamp(saturation, 0.0F, 1.0F);
    const float v = std::clamp(value, 0.0F, 1.0F);
    const float c = v * s;
    const float sector = h / 60.0F;
    const float x = c * (1.0F - std::fabs(std::fmod(sector, 2.0F) - 1.0F));
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (sector < 1.0F) {
        r = c;
        g = x;
    } else if (sector < 2.0F) {
        r = x;
        g = c;
    } else if (sector < 3.0F) {
        g = c;
        b = x;
    } else if (sector < 4.0F) {
        g = x;
        b = c;
    } else if (sector < 5.0F) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    const float m = v - c;
    const auto to8 = [m](float channel) {
        return static_cast<std::uint8_t>(
            std::lround(std::clamp(channel + m, 0.0F, 1.0F) * 255.0F));
    };
    return core::Color::fromRGBA(to8(r), to8(g), to8(b));
}

Hsv rgbToHsv(const core::Color& color) {
    const float r = color.r / 255.0F;
    const float g = color.g / 255.0F;
    const float b = color.b / 255.0F;
    const float max = std::max({r, g, b});
    const float min = std::min({r, g, b});
    const float delta = max - min;
    float h = 0.0F;
    if (delta > 0.0F) {
        if (max == r) {
            h = 60.0F * std::fmod((g - b) / delta, 6.0F);
        } else if (max == g) {
            h = 60.0F * ((b - r) / delta + 2.0F);
        } else {
            h = 60.0F * ((r - g) / delta + 4.0F);
        }
        if (h < 0.0F) {
            h += 360.0F;
        }
    }
    const float s = max <= 0.0F ? 0.0F : delta / max;
    return Hsv{h, s, max};
}

std::string colorToHex(const core::Color& color) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r, color.g,
                  color.b);
    return buffer;
}

core::Color colorFromHex(const std::string& hex) {
    // 宽容解析：可选 #，6 位十六进制；非法 = 黑色（结构化降级）。
    std::string digits = hex;
    if (!digits.empty() && digits.front() == '#') {
        digits.erase(digits.begin());
    }
    if (digits.size() != 6) {
        return core::Color::fromRGBA(0, 0, 0);
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int values[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const int hi = nibble(digits[i * 2]);
        const int lo = nibble(digits[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return core::Color::fromRGBA(0, 0, 0);
        }
        values[i] = hi * 16 + lo;
    }
    return core::Color::fromRGBA(static_cast<std::uint8_t>(values[0]), static_cast<std::uint8_t>(values[1]), static_cast<std::uint8_t>(values[2]));
}

ColorPickerController::ColorPickerController(std::string resultBind,
                                             std::string key)
    : resultBind_(std::move(resultBind)),
      key_(std::move(key)),
      palette_{"#EF5350", "#EC407A", "#AB47BC", "#7E57C2", "#5C6BC0",
               "#42A5F5", "#29B6F6", "#26A69A", "#66BB6A", "#9CCC65",
               "#FFEE58", "#FFA726"} {}

void ColorPickerController::setPalette(std::vector<std::string> hexColors) {
    palette_ = std::move(hexColors);
}

std::string ColorPickerController::result(
    const app::AppShell& shell) const {
    return shell.state().get(resultBind_);
}

void ColorPickerController::applyResult(app::AppShell& shell,
                                        const core::Color& color) {
    const std::string hex = colorToHex(color);
    if (shell.state().get(resultBind_) == hex) {
        return;
    }
    shell.state().set(resultBind_, hex);
    if (onPicked) {
        onPicked(hex);
    }
}

void ColorPickerController::attach(app::AppShell& shell) {
    // 色板 handler（幂等：重 attach 前清旧注册）。
    for (const auto& handler : handlers_) {
        shell.handlers().erase(handler);
    }
    handlers_.clear();
    for (std::size_t i = 0; i < palette_.size(); ++i) {
        const std::string hex = palette_[i];
        handlers_.push_back(swatchHandler(i));
        shell.handlers()[handlers_.back()] = [this, &shell, hex] {
            const core::Color color = colorFromHex(hex);
            const Hsv hsv = rgbToHsv(color);
            // 滑条同步反推（H ×3.6 回写）。反推期间抑制观察者派生——
            // 三次通道写只在末尾产生一次结果通知（M-4）。
            applyingSwatch_ = true;
            shell.state().set(channelBind('h'),
                              std::to_string(
                                  static_cast<int>(std::lround(hsv.h / 3.6F))));
            shell.state().set(channelBind('s'),
                              std::to_string(static_cast<int>(
                                  std::lround(hsv.s * 100.0F))));
            shell.state().set(channelBind('v'),
                              std::to_string(static_cast<int>(
                                  std::lround(hsv.v * 100.0F))));
            applyingSwatch_ = false;
            applyResult(shell, color);
        };
    }
    // 滑条观察：任一通道变化 → 派生 hex（色板反推期间抑制；无变化时
    // applyResult 幂等返回）。
    const auto derive = [this, &shell] {
        if (applyingSwatch_) {
            return;
        }
        const int h = std::atoi(shell.state().get(channelBind('h')).c_str());
        const int s = std::atoi(shell.state().get(channelBind('s')).c_str());
        const int v = std::atoi(shell.state().get(channelBind('v')).c_str());
        applyResult(shell, hsvToRgb(h * 3.6F, s / 100.0F, v / 100.0F));
    };
    if (observerH_ == 0) {
        observerH_ = shell.state().subscribe(channelBind('h'), derive);
        observerS_ = shell.state().subscribe(channelBind('s'), derive);
        observerV_ = shell.state().subscribe(channelBind('v'), derive);
    }
}

core::Widget ColorPickerController::build(
    app::AppShell& shell, const style::Theme& theme) const {
    const std::string currentHex = result(shell);
    std::vector<core::Widget> parts;
    // 预览 + 结果文本（同排：预览 swatch 24px、hex label）。
    core::Widget preview = core::makeContainerLeaf(
        24.0F, 24.0F, core::EdgeInsets{}, core::EdgeInsets{},
        core::Color::fromRGBA(0, 0, 0));
    // 预览色来自结果 bind：组合件层用 BoundColor（StateStore 字符串解析
    // 的应用职责）——这里以 swatch handler 行为为准，预览随重建刷新
    //（读 bind 的 build 期值）。
    preview.color = colorFromHex(currentHex);
    preview.key = key_ + ":preview";
    core::Widget previewRow = core::makeRow(
        {std::move(preview),
         core::makeText(currentHex.empty() ? "#000000" : currentHex,
                        theme.typography.label)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
        style::spaceToken(2));
    parts.push_back(std::move(previewRow));
    // H/S/V 滑条（通道 bind 独立于结果 bind：无写环）。
    parts.push_back(core::makeSlider(channelBind('h'), key_ + ":slider-h"));
    parts.push_back(core::makeSlider(channelBind('s'), key_ + ":slider-s"));
    parts.push_back(core::makeSlider(channelBind('v'), key_ + ":slider-v"));
    // 色板行（数据色 swatch——设计文档 §3 例外条款）。
    std::vector<core::Widget> swatches;
    for (std::size_t i = 0; i < palette_.size(); ++i) {
        core::Widget swatch = core::withVariant(
            core::makeButton("", theme.typography.label, core::EdgeInsets{},
                             0.0F, key_ + ":swatch:" + std::to_string(i),
                             20.0F, 20.0F, swatchHandler(i)),
            core::ButtonVariant::Ghost);
        swatch.color = colorFromHex(palette_[i]);
        swatch.radius = core::CornerRadius::all(4.0F);
        swatches.push_back(std::move(swatch));
    }
    core::Widget paletteRow = core::makeRow(
        std::move(swatches), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Center, style::spaceToken(1));
    paletteRow.key = key_ + ":palette";
    parts.push_back(std::move(paletteRow));
    core::Widget column =
        core::makeColumn(std::move(parts), core::MainAxisAlignment::Start,
                         core::CrossAxisAlignment::Start,
                         style::spaceToken(2));
    column.key = key_;
    return column;
}

}  // namespace lumen::widgets
