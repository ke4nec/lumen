#pragma once

// M9（自用路线图）：移动字体后端。
//
// mobile-core 与 Skia 互斥（8E 约束），不能沿用只覆盖 ASCII 的占位
// 绘制——本实现经 stb_truetype 直接光栅化系统字体文件：
//   - Android: /system/fonts（Noto Sans / NotoSansCJK / NotoColorEmoji）
//   - iOS:     /System/Library/Fonts
//   - Linux（headless 验证）: /usr/share/fonts
// 中文/emoji 有真实 advance/度量；彩色 emoji（CBDT 位图）取 mono 度量，
// 绘制仍为占位盒（后续版本接位图 emoji）。无 stb 可用时工厂返回
// nullptr（调用方回退占位并诊断）。

#include <memory>
#include <string>
#include <vector>

#include "lumen/text/font_manager.h"

namespace lumen::text {

// 创建移动字体管理器。directories 为空时用平台默认目录（Android/iOS/
// Linux 按上述顺序探测，首个存在者生效；均可指定覆盖用于测试）。
// 失败（无 stb/无字体文件）返回 nullptr 并写 diagnostic。
[[nodiscard]] std::unique_ptr<FontManager> createMobileFontManager(
    std::string* diagnostic = nullptr,
    std::vector<std::string> directories = {});

}  // namespace lumen::text
