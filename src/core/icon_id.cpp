// M6：图标归一化折线目录（0..1 坐标，stroke 绘制）。
//
// 几何在 16x16 视觉栅格上设计后归一化；线帽/拐角由渲染后端决定
//（圆帽优先，CPU 软件光栅为方帽近似）。每个图标 = 一组折线。

#include <cmath>

#include "lumen/core/icon_id.h"

namespace lumen::core {
namespace {

using Line = std::vector<Offset>;
using Catalog = std::vector<Line>;

const std::vector<Catalog>& catalog() {
    static const std::vector<Catalog> kCatalog = [] {
        std::vector<Catalog> table(
            static_cast<std::size_t>(IconId::Folder) + 1);
        table[static_cast<std::size_t>(IconId::Check)] =
            Catalog{Line{{Offset{0.20F, 0.52F}, Offset{0.42F, 0.74F},
                  Offset{0.80F, 0.28F}}}};
        // 窗口控制三枚字形按 design/gallery.html caption-button 的 SVG
        // 24 栅格逐坐标归一（14px 盒渲染时与稿逐像素一致；共享方共享
        // 同一 24 栅格设计语言）。Close：m7 7 10 10 / m17 7 7 17。
        table[static_cast<std::size_t>(IconId::Close)] =
            Catalog{Line{{Offset{0.2917F, 0.2917F}, Offset{0.7083F, 0.7083F}}},
                    Line{{Offset{0.7083F, 0.2917F}, Offset{0.2917F, 0.7083F}}}};
        table[static_cast<std::size_t>(IconId::ChevronDown)] =
            Catalog{Line{{Offset{0.28F, 0.38F}, Offset{0.50F, 0.62F},
                  Offset{0.72F, 0.38F}}}};
        table[static_cast<std::size_t>(IconId::ChevronUp)] =
            Catalog{Line{{Offset{0.28F, 0.62F}, Offset{0.50F, 0.38F},
                  Offset{0.72F, 0.62F}}}};
        table[static_cast<std::size_t>(IconId::ChevronLeft)] =
            Catalog{Line{{Offset{0.62F, 0.28F}, Offset{0.38F, 0.50F},
                  Offset{0.62F, 0.72F}}}};
        table[static_cast<std::size_t>(IconId::ChevronRight)] =
            Catalog{Line{{Offset{0.38F, 0.28F}, Offset{0.62F, 0.50F},
                  Offset{0.38F, 0.72F}}}};
        table[static_cast<std::size_t>(IconId::Alert)] =
            Catalog{Line{{Offset{0.50F, 0.20F}, Offset{0.50F, 0.62F}}},
                    Line{{Offset{0.50F, 0.78F}, Offset{0.50F, 0.80F}}}};
        table[static_cast<std::size_t>(IconId::Plus)] =
            Catalog{Line{{Offset{0.50F, 0.22F}, Offset{0.50F, 0.78F}}},
                    Line{{Offset{0.22F, 0.50F}, Offset{0.78F, 0.50F}}}};
        // Minus：M5 12 H19 → 5..19 水平线（24 栅格）。
        table[static_cast<std::size_t>(IconId::Minus)] =
            Catalog{Line{{Offset{0.2083F, 0.5F}, Offset{0.7917F, 0.5F}}}};
        // Search：镜圆（24 段折线逼近）+ 45° 手柄——手柄起点取圆缘
        // （相连无缝），无镜内斜线（旧目录的脱开手柄 + 内部斜线是
        // 16px 下发糊读不清的根因）。几何按 16 栅格 (7,7) r4.25 设计。
        {
            Catalog search;
            Line circle;
            constexpr float kCx = 0.4375F;
            constexpr float kCy = 0.4375F;
            constexpr float kRadius = 0.2656F;
            constexpr int kSegments = 24;
            for (int i = 0; i <= kSegments; ++i) {
                const float angle =
                    6.2831853F * static_cast<float>(i) / kSegments;
                circle.push_back(Offset{kCx + kRadius * std::cos(angle),
                                        kCy + kRadius * std::sin(angle)});
            }
            search.push_back(std::move(circle));
            const float edge = kRadius * 0.7071068F;
            search.push_back(Line{{Offset{kCx + edge, kCy + edge},
                                   Offset{0.8125F, 0.8125F}}});
            table[static_cast<std::size_t>(IconId::Search)] =
                std::move(search);
        }
        table[static_cast<std::size_t>(IconId::Info)] =
            Catalog{Line{{Offset{0.50F, 0.26F}, Offset{0.50F, 0.28F}}},
                    Line{{Offset{0.50F, 0.42F}, Offset{0.50F, 0.78F}}}};
        // Maximize：rect(6,6,12,12,rx1) → 6..18 方框（24 栅格；rx 圆角
        // 由描边圆角连接自然形成）。
        table[static_cast<std::size_t>(IconId::Maximize)] =
            Catalog{Line{{Offset{0.25F, 0.25F}, Offset{0.75F, 0.25F},
                          Offset{0.75F, 0.75F}, Offset{0.25F, 0.75F},
                          Offset{0.25F, 0.25F}}}};
        // S4（§6.10）：图片占位——外框 + 山形折线（描边风格与目录一致）。
        table[static_cast<std::size_t>(IconId::Image)] =
            Catalog{Line{{Offset{0.18F, 0.22F}, Offset{0.82F, 0.22F},
                          Offset{0.82F, 0.78F}, Offset{0.18F, 0.78F},
                          Offset{0.18F, 0.22F}}},
                    Line{{Offset{0.18F, 0.68F}, Offset{0.38F, 0.46F},
                          Offset{0.52F, 0.60F}, Offset{0.66F, 0.42F},
                          Offset{0.82F, 0.58F}}}};
        // 自定义标题栏（lumen-titlebar-design §4.3）：还原 = 前后两个
        // 方框（后框只画被遮挡以外的两边，24 栅格 9,5→19,5→19,15 +
        // 5,9..15,19 前框）。
        table[static_cast<std::size_t>(IconId::Restore)] =
            Catalog{Line{{Offset{0.38F, 0.21F}, Offset{0.79F, 0.21F},
                          Offset{0.79F, 0.62F}}},
                    Line{{Offset{0.21F, 0.38F}, Offset{0.62F, 0.38F},
                          Offset{0.62F, 0.79F}, Offset{0.21F, 0.79F},
                          Offset{0.21F, 0.38F}}}};
        table[static_cast<std::size_t>(IconId::Document)] =
            Catalog{Line{{Offset{0.25F, 0.12F}, Offset{0.60F, 0.12F},
                          Offset{0.80F, 0.32F}, Offset{0.80F, 0.88F},
                          Offset{0.25F, 0.88F}, Offset{0.25F, 0.12F}}},
                    Line{{Offset{0.60F, 0.12F}, Offset{0.60F, 0.32F},
                          Offset{0.80F, 0.32F}}}};
        table[static_cast<std::size_t>(IconId::Folder)] =
            Catalog{Line{{Offset{0.12F, 0.25F}, Offset{0.40F, 0.25F},
                          Offset{0.50F, 0.38F}, Offset{0.88F, 0.38F},
                          Offset{0.88F, 0.78F}, Offset{0.12F, 0.78F},
                          Offset{0.12F, 0.25F}}}};
        return table;
    }();
    return kCatalog;
}

}  // namespace

const std::vector<std::vector<Offset>>& iconPolylines(IconId id) {
    static const Catalog kEmpty{};
    const auto index = static_cast<std::size_t>(id);
    const std::vector<Catalog>& all = catalog();
    return index < all.size() ? all[index] : kEmpty;
}

}  // namespace lumen::core
