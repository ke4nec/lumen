// 系统字体测试共享夹具：同一进程内复用已加载的 SystemFontManager。
//
// 背景：Windows runner 携带数百个系统字体首选面（雅黑/宋体/Segoe 等），
// createSystemFontManager 每次调用都全量扫描字体目录并把全部文件读入
// 内存。system_font_tests / font_metrics_tests 的每个用例都单独加载一次，
// 冷缓存下首个光栅用例的 I/O 成本足以吃掉 ctest 默认 120s 超时
//（windows-2025 上 system_fonts_cover_latin_and_cjk 超时、
// cpu_renderer_system_fonts_draw_real_glyphs 跑满 106s）。
//
// 管理器加载后只读（位图/GDI 缓存内部自带互斥），用例间共享安全；
// 无系统字体时缓存 nullptr，各用例保持原有的 SKIP/SUCCEED 行为。
// 显式目录的用例（如缺失目录失败路径、fixture 隔离目录）不受影响，
// 仍走 createSystemFontManager 直调。

#pragma once

#include <memory>
#include <string>

#include "lumen/text/system_font_manager.h"

namespace lumen::test {

struct SharedSystemFonts {
    std::shared_ptr<text::SystemFontManager> manager;
    std::string diagnostic;
};

// 进程内单例：默认字体目录只加载一次；失败时 manager 为 nullptr。
inline const SharedSystemFonts& sharedSystemFontsCached() {
    static const SharedSystemFonts cached = [] {
        SharedSystemFonts result;
        result.manager = std::shared_ptr<text::SystemFontManager>(
            text::createSystemFontManager(&result.diagnostic));
        return result;
    }();
    return cached;
}

inline std::shared_ptr<text::SystemFontManager> sharedSystemFonts() {
    return sharedSystemFontsCached().manager;
}

inline const std::string& sharedSystemFontsDiagnostic() {
    return sharedSystemFontsCached().diagnostic;
}

}  // namespace lumen::test
