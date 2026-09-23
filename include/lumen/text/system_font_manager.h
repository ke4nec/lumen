#pragma once

// 桌面系统字体后端（自用路线图：Windows 窗口雅黑）。
//
// CPU 光栅默认的占位 5x7 点阵字在窗口里观感差（Gallery 与设计稿差距主
// 因）。Windows 优先经 GDI 的灰度字形 API 光栅化（与桌面控件相同的系
// 统字形路径），并保留 stb_truetype 文件光栅化作为可移植回退：
//   - Windows: %WINDIR%/Fonts（Microsoft YaHei / Segoe UI / SimSun…）
//   - Linux:   /usr/share/fonts 递归子目录（Noto / DejaVu…；Debian 的
//              <format>/<foundry>/ 两层布局同样覆盖）
//   - macOS:   /System/Library/Fonts（PingFang / Helvetica…）
// 度量与回退语义和 Mobile 后端一致（同一 defaultFontStackFor 顺序），
// 另向 CpuRenderer 提供字形覆盖位图（灰度 coverage）。
//
// 线程：只读并发安全（位图缓存内部加锁）；UI 线程独占创建。无可用字
// 体文件时工厂返回 nullptr（调用方回退占位并诊断），窗口以外路径
// （headless/测试）默认仍走占位，保证帧哈希确定性。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lumen/text/font_manager.h"

namespace lumen::text {

// 单字形灰度位图（device 像素；pen 位于基线原点）。
struct GlyphBitmap {
    int width{0};
    int height{0};
    // 位图左上相对 pen（基线原点）的偏移：x + bearingX，y = baseline -
    // bearingTop（bearingTop 为基线上方像素数）。
    int bearingX{0};
    int bearingTop{0};
    // width*height 灰度 coverage（0=透明，255=全盖）。
    std::vector<std::uint8_t> coverage{};

    [[nodiscard]] bool empty() const {
        return width <= 0 || height <= 0 || coverage.empty();
    }
};

// 系统字体管理器：FontManager 度量/回退 + CPU 光栅位图。
class SystemFontManager : public FontManager {
  public:
    ~SystemFontManager() override = default;

    [[nodiscard]] FontBackend backend() const override {
        return FontBackend::System;
    }

    // 字形覆盖位图（pixelHeight = sizePx * deviceScale，>0）。family 为
    // 空按默认栈解析；找不到覆盖字形返回 false（调用方画占位盒）。
    // subPixelShift 为 pen 在设备像素网格内的小数偏移（[0,1)，按 1/4 px
    // 量化入缓存键）：光栅时平移轮廓，保留布局的亚像素 advance 累积，
    // 避免逐字形取整造成的小字号字距抖动。
    [[nodiscard]] virtual bool bitmapFor(char32_t codePoint,
                                         const FontQuery& query,
                                         float deviceScale,
                                         GlyphBitmap* out,
                                         float subPixelShift = 0.0F) const = 0;
};

// 创建系统字体管理器。directories 为空时用平台默认目录（见上；首个
// 存在且含字体文件的目录生效，可指定覆盖用于测试）。失败（无 stb/
// 无字体文件）返回 nullptr 并写 diagnostic。
[[nodiscard]] std::unique_ptr<SystemFontManager> createSystemFontManager(
    std::string* diagnostic = nullptr,
    std::vector<std::string> directories = {});

}  // namespace lumen::text
