#include "skia_text.h"

#include <algorithm>
#include <vector>

#include "include/core/SkFont.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#elif defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#elif defined(__APPLE__)
#include <CoreText/CoreText.h>
#include "include/ports/SkFontMgr_mac_ct.h"
#endif

namespace lumen::render::skia_text {

sk_sp<SkFontMgr> makePlatformFontMgr() {
#ifdef _WIN32
    return SkFontMgr_New_GDI();
#elif defined(__linux__)
    return SkFontMgr_New_FontConfig(nullptr);
#elif defined(__APPLE__)
    return SkFontMgr_New_CoreText(nullptr);
#else
    return nullptr;
#endif
}

namespace {

SkColor toSkColorLocal(core::Color color) {
    return SkColorSetARGB(static_cast<U8CPU>(color.a),
                          static_cast<U8CPU>(color.r),
                          static_cast<U8CPU>(color.g),
                          static_cast<U8CPU>(color.b));
}

// 与 SkiaFontManager 的查询语义一致：weight clamp 到 Skia 范围，
// italic → slant，宽度固定 normal。
SkFontStyle toSkStyle(const core::TextStyle& style) {
    const int weight =
        std::clamp(style.bold ? std::max(style.weight, 700) : style.weight,
                   100, 900);
    const SkFontStyle::Slant slant =
        style.italic ? SkFontStyle::kItalic_Slant
                     : SkFontStyle::kUpright_Slant;
    return SkFontStyle(weight, SkFontStyle::kNormal_Width, slant);
}

}  // namespace

void drawShapedText(SkCanvas* canvas, SkFontMgr* fontMgr,
                    const TextRun& run, const core::TextStyle& style,
                    float deviceScale) {
    if (canvas == nullptr || fontMgr == nullptr || run.shapedRuns.empty() ||
        style.color.a == 0) {
        return;
    }
    const float fontSize = style.fontSize > 0.0F ? style.fontSize : 14.0F;
    const float scale = deviceScale;
    // baseline 出自布局（真实 ascent）；缺省回退 fontSize（旧行为）。
    const float baseline =
        (run.origin.y + (run.baselinePx > 0.0F ? run.baselinePx : fontSize)) *
        scale;

    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setAntiAlias(true);
    paint.setColor(toSkColorLocal(style.color));

    const SkFontStyle fontStyle = toSkStyle(style);
    for (const TextGlyphRun& glyphRun : run.shapedRuns) {
        if (glyphRun.placeholder || glyphRun.glyphs.empty()) {
            continue;
        }
        sk_sp<SkTypeface> face =
            fontMgr->matchFamilyStyle(glyphRun.family.c_str(), fontStyle);
        if (face == nullptr) {
            // 族名不可解析（字体在布局后被卸载）：跳过该 run，几何仍以
            // 布局为准；调用方诊断可报告 fallback。
            continue;
        }
        SkFont font(face, fontSize * scale);
        font.setEdging(SkFont::Edging::kAntiAlias);
        std::vector<SkGlyphID> ids;
        std::vector<SkPoint> positions;
        ids.reserve(glyphRun.glyphs.size());
        positions.reserve(glyphRun.glyphs.size());
        for (const text::ShapedGlyph& glyph : glyphRun.glyphs) {
            ids.push_back(static_cast<SkGlyphID>(glyph.glyphId));
            positions.push_back(SkPoint::Make(
                (run.origin.x + glyph.xOffsetPx) * scale, baseline));
        }
        canvas->drawGlyphs(static_cast<int>(ids.size()), ids.data(),
                           positions.data(), SkPoint::Make(0.0F, 0.0F), font,
                           paint);
    }
}

}  // namespace lumen::render::skia_text
