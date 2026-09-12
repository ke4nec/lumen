// M1：Skia 后端共享的 shaped 文本绘制辅助（实现文件，仅 LUMEN_ENABLE_SKIA
// 时编入 lumen-render）。布局与绘制共享同一份 TextLayout 结果：本模块按
// TextRun 携带的 glyph id / xOffsetPx / baseline 直接绘制，不再自行度量，
// 光标/选区/字形位置不跨后端漂移。

#pragma once

#include "include/core/SkCanvas.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRefCnt.h"

#include "lumen/core/geometry.h"
#include "lumen/render/renderer.h"

namespace lumen::render::skia_text {

// 平台字体管理器工厂：Windows GDI / Linux FontConfig / macOS CoreText；
// 不可用或未知平台返回 nullptr（调用方回退占位/旧路径并诊断）。
// 本预编译 Skia 不提供 SkFontMgr::RefDefault，必须显式选平台端口。
[[nodiscard]] sk_sp<SkFontMgr> makePlatformFontMgr();

// 绘制 run.shapedRuns 中的真实 shaping 数据（placeholder run 跳过；调用方
// 应在有占位 run 时整体回退旧逐码点路径）。deviceScale 把逻辑坐标换算到
// 设备像素；fontMgr 用于按族名解析 typeface（与布局期同一查询语义）。
void drawShapedText(SkCanvas* canvas, SkFontMgr* fontMgr,
                    const TextRun& run, const core::TextStyle& style,
                    float deviceScale);

}  // namespace lumen::render::skia_text
