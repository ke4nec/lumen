#pragma once

#include <cstdint>

#include "lumen/core/geometry.h"
#include "lumen/core/icon_id.h"

namespace lumen::style {

// 视觉系统 token 三层模型（docs/lumen-visual-system-design.md §3.1）。
// Primitive token 只描述调色板与基础尺度，控件不得直接使用；semantic 与
// component token 由 Theme 持有（theme.h）。本头文件保持平台无关、仅依
// 赖 core 几何值类型。

// --- 颜色工具（确定性派生，无平台依赖） ---

// 线性混合：t=0 返回 a，t=1 返回 b（alpha 同样插值）。
[[nodiscard]] core::Color mixColors(core::Color a, core::Color b, float t);

// alpha 合成：把 overlay 按其 alpha 压到 base 上（source-over）。
[[nodiscard]] core::Color blendOver(core::Color base, core::Color overlay);

// --- Primitive palette ---

// 跨平台默认调色板（§3：Lumen 默认视觉风格，平台适配层只提供输入）。
struct PrimitivePalette {
    core::Color neutral050{250, 250, 250, 255};
    core::Color neutral100{244, 244, 245, 255};
    core::Color neutral200{228, 228, 231, 255};
    core::Color neutral300{212, 212, 216, 255};
    core::Color neutral400{161, 161, 170, 255};
    core::Color neutral500{140, 140, 152, 255};
    core::Color neutral600{82, 82, 91, 255};
    core::Color neutral700{60, 60, 70, 255};
    core::Color neutral800{46, 46, 54, 255};
    core::Color neutral900{39, 39, 46, 255};
    core::Color neutral950{24, 24, 27, 255};

    core::Color blue100{224, 234, 255, 255};
    core::Color blue300{150, 185, 250, 255};
    core::Color blue500{86, 140, 240, 255};
    core::Color blue700{52, 96, 190, 255};

    core::Color red300{240, 150, 154, 255};
    core::Color red500{224, 90, 96, 255};
    core::Color red700{170, 52, 58, 255};

    core::Color green500{74, 160, 106, 255};
    core::Color amber500{204, 152, 64, 255};

    core::Color white{255, 255, 255, 255};
    core::Color black{0, 0, 0, 255};

    bool operator==(const PrimitivePalette&) const = default;
};

// --- 基础尺度 ---

// 4 logical px 基础网格；space.1..space.12 = grid * step（§3.2）。
constexpr float kSpaceGrid = 4.0F;
[[nodiscard]] constexpr float spaceToken(int step) {
    return kSpaceGrid * static_cast<float>(step);
}

// 圆角档位：radius.1 = 4，radius.2 = 6/8（按密度），radius.pill = 高度的
// 一半（由组件解析时计算）。

// 时长档位（毫秒）：reduceAnimation 派生时全部归零（§4）。
constexpr std::uint32_t kDurationFastMs = 100;
constexpr std::uint32_t kDurationNormalMs = 200;
constexpr std::uint32_t kDurationSlowMs = 350;

// --- 图标契约（§8 冻结扩展） ---

// 图标以语义 ID 表达，不把 SVG 路径写入控件逻辑；颜色默认继承前景色。
// Renderer 的 vector path / 统一 image 适配属后续版本。
// M6：图标语义 ID 移至 core（几何目录与 Widget/RenderNode 携带；
// token 冻结——名字与值不变，经别名复用）。
using IconId = core::IconId;

// 图标默认几何（渲染能力冻结，token 先行）。strokeWidth 以 16px 基准
// 档定权：1.8 对齐设计稿描边（design/gallery.html 1.7–2.0、
// menu-controls.html 1.8）——1.5 在 16px + AA 下偏细发糊。
struct IconTheme {
    float defaultSize{16.0F};
    float strokeWidth{1.8F};

    bool operator==(const IconTheme&) const = default;
};

}  // namespace lumen::style
