// G-4（docs/lumen-color-picker-design.md）：ColorPicker 测试。
// 覆盖：HSV↔RGB 纯函数（主色相/灰/黑/白往返）、hex 解析（大小写/#/非法
// 降级）、色板点击（结果落 bind + onPicked + 滑条反推）、滑条派生
// （h/s/v → hex）、无写环。全部 headless。

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/widget.h"
#include "lumen/core/render_node.h"
#include "lumen/dsl/dsl.h"
#include "lumen/widgets/color_picker.h"

using lumen::app::AppShell;
using lumen::app::ShellConfig;
using lumen::core::RenderNode;
using lumen::core::Size;
using lumen::widgets::colorFromHex;
using lumen::widgets::colorToHex;
using lumen::widgets::hsvToRgb;
using lumen::widgets::rgbToHsv;

namespace {

const RenderNode* findByKeyDeep(const RenderNode& node,
                                const std::string& key) {
    if (node.key == key) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const RenderNode* hit = findByKeyDeep(child, key)) {
            return hit;
        }
    }
    return nullptr;
}

bool sameRgb(lumen::core::Color a, lumen::core::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

}  // namespace

TEST_CASE("color_hsv_rgb_roundtrip_primaries", "[widgets][color-picker]") {
    const lumen::core::Color red = hsvToRgb(0, 1, 1);
    CHECK(sameRgb(red, lumen::core::Color::fromRGBA(255, 0, 0)));
    const lumen::core::Color green = hsvToRgb(120, 1, 1);
    CHECK(sameRgb(green, lumen::core::Color::fromRGBA(0, 255, 0)));
    const lumen::core::Color blue = hsvToRgb(240, 1, 1);
    CHECK(sameRgb(blue, lumen::core::Color::fromRGBA(0, 0, 255)));
    CHECK(sameRgb(hsvToRgb(0, 0, 0), lumen::core::Color::fromRGBA(0, 0, 0)));
    CHECK(sameRgb(hsvToRgb(0, 0, 1), lumen::core::Color::fromRGBA(255, 255, 255)));
    // 任意色往返（灰色 sat=0 无色相概念，不入往返）。
    for (float hue = 0; hue < 360; hue += 30) {
        for (float sat : {0.35F, 0.8F, 1.0F}) {
            const lumen::core::Color color = hsvToRgb(hue, sat, 0.9F);
            const lumen::widgets::Hsv back = rgbToHsv(color);
            CHECK(std::abs(hue - back.h) < 1.0F);
            CHECK(std::abs(sat - back.s) < 0.01F);
            CHECK(std::abs(0.9F - back.v) < 0.01F);
        }
    }
}

TEST_CASE("color_hex_parse_degrades_gracefully", "[widgets][color-picker]") {
    CHECK(colorToHex(lumen::core::Color::fromRGBA(255, 0, 0)) == "#FF0000");
    CHECK(sameRgb(colorFromHex("#ff0000"), lumen::core::Color::fromRGBA(255, 0, 0)));
    CHECK(sameRgb(colorFromHex("00FF7F"), lumen::core::Color::fromRGBA(0, 255, 127)));
    // 非法输入 = 黑色（结构化降级，不抛异常）。
    CHECK(sameRgb(colorFromHex("nope"), lumen::core::Color::fromRGBA(0, 0, 0)));
    CHECK(sameRgb(colorFromHex("#12345"), lumen::core::Color::fromRGBA(0, 0, 0)));
    CHECK(sameRgb(colorFromHex("#GGGGGG"), lumen::core::Color::fromRGBA(0, 0, 0)));
}

TEST_CASE("color_picker_swatch_click_sets_result_and_sliders",
          "[widgets][color-picker]") {
    ShellConfig config;
    config.initialView = Size{400.0F, 320.0F};
    lumen::widgets::ColorPickerController picker("accent-hex", "picker");
    config.build = [&picker] {
        using namespace lumen::dsl;
        lumen::core::Widget page = container(
            column({withKey(button("dummy", onClick("x")), "dummy")}),
            lumen::core::Color::fromRGBA(24, 24, 27));
        page.key = "root";
        return page;
    };
    AppShell shell{config};
    std::string picked;
    int pickedCount = 0;
    picker.onPicked = [&](const std::string& hex) {
        picked = hex;
        ++pickedCount;
    };
    picker.attach(shell);
    shell.rebuildIfDirty();

    // 第 6 格（#42A5F5 蓝）点击：结果 + 通道反推。
    shell.handlers()["picker:swatch:5"]();
    CHECK(shell.state().get("accent-hex") == "#42A5F5");
    CHECK(picked == "#42A5F5");
    // M-4 review：单次点击单次回调（三次通道反推只在末尾通知一次）。
    CHECK(pickedCount == 1);
    const int h = std::atoi(shell.state().get("picker:h").c_str());
    const int s = std::atoi(shell.state().get("picker:s").c_str());
    const int v = std::atoi(shell.state().get("picker:v").c_str());
    // 反推经整数通道量化（h 3.6°/格）——派生色的色相/饱和度与目标
    // 一致（差 ≤1 格；RGB 通道差由色相格距放大，不做逐通道断言）。
    const lumen::core::Color derived =
        hsvToRgb(h * 3.6F, s / 100.0F, v / 100.0F);
    const lumen::widgets::Hsv targetHsv =
        rgbToHsv(colorFromHex("#42A5F5"));
    const lumen::widgets::Hsv derivedHsv = rgbToHsv(derived);
    const float dh = std::fabs(targetHsv.h - derivedHsv.h);
    CHECK(std::fmin(dh, 360.0F - dh) <= 3.6F);
    CHECK(std::fabs(targetHsv.s - derivedHsv.s) <= 0.02F);
    CHECK(std::fabs(targetHsv.v - derivedHsv.v) <= 0.02F);

    // 滑条派生：改通道 → hex 更新（与 onPicked 同路径）。
    shell.state().set("picker:h", "0");
    shell.state().set("picker:s", "100");
    shell.state().set("picker:v", "100");
    CHECK(shell.state().get("accent-hex") == "#FF0000");
    CHECK(picked == "#FF0000");
    // 同值再写：无重复回调（applyResult 幂等）。
    picked.clear();
    const int countBefore = pickedCount;
    shell.state().set("picker:v", "100");
    CHECK(picked.empty());
    CHECK(pickedCount == countBefore);
}

TEST_CASE("color_picker_build_renders_preview_and_palette",
          "[widgets][color-picker]") {
    struct Harness {
        lumen::widgets::ColorPickerController picker{"accent-hex", "picker"};
        AppShell shell{configFor(this)};

        static ShellConfig configFor(Harness* self) {
            ShellConfig config;
            config.initialView = Size{400.0F, 320.0F};
            config.build = [self] {
                using namespace lumen::dsl;
                lumen::core::Widget page = container(
                    column({self->picker.build(self->shell,
                                               self->shell.theme())}),
                    lumen::core::Color::fromRGBA(24, 24, 27));
                page.key = "root";
                return page;
            };
            return config;
        }
    };
    Harness harness;
    harness.picker.setPalette({"#EF5350", "#EC407A"});
    harness.picker.attach(harness.shell);
    harness.shell.state().set("accent-hex", "#42A5F5");
    harness.shell.rebuildIfDirty();

    // 结构：预览、3 滑条、2 色板格。
    CHECK(findByKeyDeep(harness.shell.root(), "picker:preview") != nullptr);
    CHECK(findByKeyDeep(harness.shell.root(), "picker:slider-h") != nullptr);
    CHECK(findByKeyDeep(harness.shell.root(), "picker:slider-s") != nullptr);
    CHECK(findByKeyDeep(harness.shell.root(), "picker:slider-v") != nullptr);
    CHECK(findByKeyDeep(harness.shell.root(), "picker:swatch:0") != nullptr);
    CHECK(findByKeyDeep(harness.shell.root(), "picker:swatch:1") != nullptr);
    CHECK(findByKeyDeep(harness.shell.root(), "picker:swatch:2") == nullptr);
    // 预览随结果刷新（重建后读 bind 当前值；RenderNode 经 resolved
    // style 的 common 背景携带）。
    const RenderNode* preview =
        findByKeyDeep(harness.shell.root(), "picker:preview");
    REQUIRE(preview != nullptr);
    const auto& common =
        lumen::core::commonStyle(preview->style);
    CHECK(sameRgb(common.background, colorFromHex("#42A5F5")));
}
