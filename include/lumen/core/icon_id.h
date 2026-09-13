#pragma once

// M6（自用路线图）：图标语义 ID 与矢量目录（core 层）。
//
// 图标由语义 ID 表达，控件只引用 ID；绘制几何为归一化折线（0..1 盒
// 内坐标，绘制时按盒子缩放），颜色默认继承前景、线宽来自 IconTheme
//（style 层 token）。折线目录是纯几何数据，不涉及样式，故属 core；
// style::IconTheme 经 using 复用本枚举（token 冻结不变）。

#include <cstdint>
#include <vector>

#include "lumen/core/geometry.h"

namespace lumen::core {

enum class IconId : std::uint8_t {
    None,
    Check,
    Close,
    ChevronDown,
    ChevronRight,
    Alert,
    // M6 扩充（工具类常用）。
    ChevronLeft,
    ChevronUp,
    Plus,
    Minus,
    Search,
    Info,
};

// 图标的归一化折线组（0..1 坐标；stroke 绘制，无填充）。
// 空 vector = 该 ID 无几何（占位不绘制）。
[[nodiscard]] const std::vector<std::vector<Offset>>& iconPolylines(
    IconId id);

}  // namespace lumen::core
