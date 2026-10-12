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
            static_cast<std::size_t>(IconId::Scope) + 1);
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
        // Spin/ToolBar/StatusBar 控件（2026-09）：16 栅格设计后归一。
        // Undo/Redo：左/右弯钩箭头（design/toolbar.html SVG 同形）。
        table[static_cast<std::size_t>(IconId::Undo)] =
            Catalog{Line{{Offset{0.33F, 0.29F}, Offset{0.17F, 0.46F},
                          Offset{0.33F, 0.63F}}},
                    Line{{Offset{0.17F, 0.46F}, Offset{0.58F, 0.46F},
                          Offset{0.79F, 0.46F}, Offset{0.79F, 0.70F},
                          Offset{0.63F, 0.79F}}}};
        table[static_cast<std::size_t>(IconId::Redo)] =
            Catalog{Line{{Offset{0.67F, 0.29F}, Offset{0.83F, 0.46F},
                          Offset{0.67F, 0.63F}}},
                    Line{{Offset{0.83F, 0.46F}, Offset{0.42F, 0.46F},
                          Offset{0.21F, 0.46F}, Offset{0.21F, 0.70F},
                          Offset{0.37F, 0.79F}}}};
        // Play：实心观感的三角（stroke 闭合）。
        table[static_cast<std::size_t>(IconId::Play)] =
            Catalog{Line{{Offset{0.33F, 0.21F}, Offset{0.75F, 0.50F},
                          Offset{0.33F, 0.79F}, Offset{0.33F, 0.21F}}}};
        // Grid：2×2 方框。
        table[static_cast<std::size_t>(IconId::Grid)] =
            Catalog{Line{{Offset{0.17F, 0.17F}, Offset{0.45F, 0.17F},
                          Offset{0.45F, 0.45F}, Offset{0.17F, 0.45F},
                          Offset{0.17F, 0.17F}}},
                    Line{{Offset{0.55F, 0.17F}, Offset{0.83F, 0.17F},
                          Offset{0.83F, 0.45F}, Offset{0.55F, 0.45F},
                          Offset{0.55F, 0.17F}}},
                    Line{{Offset{0.17F, 0.55F}, Offset{0.45F, 0.55F},
                          Offset{0.45F, 0.83F}, Offset{0.17F, 0.83F},
                          Offset{0.17F, 0.55F}}},
                    Line{{Offset{0.55F, 0.55F}, Offset{0.83F, 0.55F},
                          Offset{0.83F, 0.83F}, Offset{0.55F, 0.83F},
                          Offset{0.55F, 0.55F}}}};
        // Settings：中心圆 + 八向辐条（齿轮的扁平近似）。
        table[static_cast<std::size_t>(IconId::Settings)] =
            Catalog{Line{{Offset{0.50F, 0.36F}, Offset{0.59F, 0.41F},
                          Offset{0.59F, 0.59F}, Offset{0.50F, 0.64F},
                          Offset{0.41F, 0.59F}, Offset{0.41F, 0.41F},
                          Offset{0.50F, 0.36F}}},
                    Line{{Offset{0.50F, 0.17F}, Offset{0.50F, 0.30F}}},
                    Line{{Offset{0.50F, 0.70F}, Offset{0.50F, 0.83F}}},
                    Line{{Offset{0.17F, 0.50F}, Offset{0.30F, 0.50F}}},
                    Line{{Offset{0.70F, 0.50F}, Offset{0.83F, 0.50F}}},
                    Line{{Offset{0.27F, 0.27F}, Offset{0.36F, 0.36F}}},
                    Line{{Offset{0.64F, 0.64F}, Offset{0.73F, 0.73F}}},
                    Line{{Offset{0.73F, 0.27F}, Offset{0.64F, 0.36F}}},
                    Line{{Offset{0.36F, 0.64F}, Offset{0.27F, 0.73F}}}};
        // Busy：3/4 圆弧（24 段折线逼近；旋转经 iconRotation 逐 tick 驱动，
        // 静止态保留"进行中"形状——statusbar-design §10）。
        {
            Catalog busy;
            Line arc;
            constexpr float kCx = 0.5F;
            constexpr float kCy = 0.5F;
            constexpr float kRadius = 0.34F;
            constexpr int kSegments = 24;
            // 起点 135°，顺时针 270°（3/4 弧，开口朝右下）。
            constexpr float kStart = 0.75F * 3.1415927F;
            constexpr float kSweep = 1.5F * 3.1415927F;
            for (int i = 0; i <= kSegments; ++i) {
                const float angle =
                    kStart + kSweep * static_cast<float>(i) / kSegments;
                arc.push_back(Offset{kCx + kRadius * std::cos(angle),
                                     kCy + kRadius * std::sin(angle)});
            }
            busy.push_back(std::move(arc));
            table[static_cast<std::size_t>(IconId::Busy)] = std::move(busy);
        }
        // Grip：右下角 resize 斜纹（3 条 45° 短线；纯视觉件，命中归平台
        // resize 边——statusbar-design §9.1）。
        table[static_cast<std::size_t>(IconId::Grip)] =
            Catalog{Line{{Offset{0.58F, 0.92F}, Offset{0.92F, 0.58F}}},
                    Line{{Offset{0.71F, 0.92F}, Offset{0.92F, 0.71F}}},
                    Line{{Offset{0.45F, 0.92F}, Offset{0.92F, 0.45F}}}};
        // Gallery 标题栏品牌标记：与应用图标相同的首字母 L 轮廓，
        // 由 Icon 节点按主题前景色绘制，避免在 UI 中嵌入文字字形。
        table[static_cast<std::size_t>(IconId::GalleryLogo)] =
            Catalog{Line{{Offset{0.25F, 0.18F}, Offset{0.25F, 0.78F}}},
                    Line{{Offset{0.25F, 0.78F}, Offset{0.80F, 0.78F}}}};
        // Gallery 侧栏导航（design/gallery.html sidebar-nav 的 24 栅格
        // SVG 逐坐标归一；18px 盒 + ~1.7 描边渲染，圆角矩形按目录惯例
        // 取直角折线）。NavCollections/NavControls 的小圆节点以 8 段
        // 八边形逼近；NavTheme 的右半填充以三条竖弦近似（目录只描边）。
        table[static_cast<std::size_t>(IconId::NavHome)] =
            Catalog{Line{{Offset{0.125F, 0.4167F}, Offset{0.5F, 0.125F},
                          Offset{0.875F, 0.4167F}}},
                    Line{{Offset{0.2083F, 0.375F}, Offset{0.2083F, 0.8333F},
                          Offset{0.7917F, 0.8333F}, Offset{0.7917F, 0.375F}}},
                    Line{{Offset{0.375F, 0.8333F}, Offset{0.375F, 0.5417F},
                          Offset{0.625F, 0.5417F}, Offset{0.625F, 0.8333F}}}};
        table[static_cast<std::size_t>(IconId::NavButtons)] =
            Catalog{Line{{Offset{0.125F, 0.25F}, Offset{0.875F, 0.25F},
                          Offset{0.875F, 0.75F}, Offset{0.125F, 0.75F},
                          Offset{0.125F, 0.25F}}},
                    Line{{Offset{0.3333F, 0.5F}, Offset{0.6667F, 0.5F}}}};
        table[static_cast<std::size_t>(IconId::NavInputs)] =
            Catalog{Line{{Offset{0.125F, 0.2083F}, Offset{0.875F, 0.2083F},
                          Offset{0.875F, 0.7917F}, Offset{0.125F, 0.7917F},
                          Offset{0.125F, 0.2083F}}},
                    Line{{Offset{0.3333F, 0.3333F}, Offset{0.3333F, 0.6667F}}},
                    Line{{Offset{0.25F, 0.3333F}, Offset{0.4167F, 0.3333F}}},
                    Line{{Offset{0.25F, 0.6667F}, Offset{0.4167F, 0.6667F}}},
                    Line{{Offset{0.5417F, 0.5F}, Offset{0.7083F, 0.5F}}}};
        table[static_cast<std::size_t>(IconId::NavLayout)] =
            Catalog{Line{{Offset{0.125F, 0.125F}, Offset{0.875F, 0.125F},
                          Offset{0.875F, 0.875F}, Offset{0.125F, 0.875F},
                          Offset{0.125F, 0.125F}}},
                    Line{{Offset{0.125F, 0.375F}, Offset{0.875F, 0.375F}}},
                    Line{{Offset{0.4167F, 0.375F}, Offset{0.4167F, 0.875F}}}};
        // NavLists：三条行线 + 左侧圆点（0.04 宽短横线在 18px 盒中仅
        // ~0.75px 不可见——按目录惯例以 8 段八边形逼近 1px 半径实点）。
        table[static_cast<std::size_t>(IconId::NavLists)] =
            Catalog{Line{{Offset{0.375F, 0.25F}, Offset{0.875F, 0.25F}}},
                    Line{{Offset{0.375F, 0.5F}, Offset{0.875F, 0.5F}}},
                    Line{{Offset{0.375F, 0.75F}, Offset{0.875F, 0.75F}}},
                    Line{{Offset{0.1458F, 0.2083F}, Offset{0.1843F, 0.2115F}, Offset{0.1875F, 0.2500F}, Offset{0.1843F, 0.2885F}, Offset{0.1458F, 0.2917F}, Offset{0.1073F, 0.2885F}, Offset{0.1042F, 0.2500F}, Offset{0.1073F, 0.2115F}, Offset{0.1458F, 0.2083F}}},
                    Line{{Offset{0.1458F, 0.4583F}, Offset{0.1843F, 0.4615F}, Offset{0.1875F, 0.5000F}, Offset{0.1843F, 0.5385F}, Offset{0.1458F, 0.5417F}, Offset{0.1073F, 0.5385F}, Offset{0.1042F, 0.5000F}, Offset{0.1073F, 0.4615F}, Offset{0.1458F, 0.4583F}}},
                    Line{{Offset{0.1458F, 0.7083F}, Offset{0.1843F, 0.7115F}, Offset{0.1875F, 0.7500F}, Offset{0.1843F, 0.7885F}, Offset{0.1458F, 0.7917F}, Offset{0.1073F, 0.7885F}, Offset{0.1042F, 0.7500F}, Offset{0.1073F, 0.7115F}, Offset{0.1458F, 0.7083F}}}};
        table[static_cast<std::size_t>(IconId::NavCollections)] =
            Catalog{Line{{Offset{0.5F, 0.125F}, Offset{0.5F, 0.2917F}}},
                    Line{{Offset{0.25F, 0.875F}, Offset{0.25F, 0.7917F},
                          Offset{0.3333F, 0.7083F}, Offset{0.6667F, 0.7083F},
                          Offset{0.75F, 0.625F}, Offset{0.75F, 0.5417F}}},
                    Line{{Offset{0.25F, 0.375F}, Offset{0.3088F, 0.3996F},
                          Offset{0.3333F, 0.4583F}, Offset{0.3088F, 0.5171F},
                          Offset{0.25F, 0.5417F}, Offset{0.1913F, 0.5171F},
                          Offset{0.1667F, 0.4583F}, Offset{0.1913F, 0.3996F},
                          Offset{0.25F, 0.375F}}},
                    Line{{Offset{0.75F, 0.375F}, Offset{0.8088F, 0.3996F},
                          Offset{0.8333F, 0.4583F}, Offset{0.8088F, 0.5171F},
                          Offset{0.75F, 0.5417F}, Offset{0.6913F, 0.5171F},
                          Offset{0.6667F, 0.4583F}, Offset{0.6913F, 0.3996F},
                          Offset{0.75F, 0.375F}}},
                    Line{{Offset{0.25F, 0.5417F}, Offset{0.25F, 0.7917F}}},
                    Line{{Offset{0.75F, 0.5417F}, Offset{0.75F, 0.4583F}}}};
        table[static_cast<std::size_t>(IconId::NavMenus)] =
            Catalog{Line{{Offset{0.1667F, 0.2917F}, Offset{0.8333F, 0.2917F}}},
                    Line{{Offset{0.1667F, 0.5F}, Offset{0.8333F, 0.5F}}},
                    Line{{Offset{0.1667F, 0.7083F}, Offset{0.5833F, 0.7083F}}}};
        table[static_cast<std::size_t>(IconId::NavControls)] =
            Catalog{Line{{Offset{0.1667F, 0.3333F}, Offset{0.4167F, 0.3333F}}},
                    Line{{Offset{0.6667F, 0.3333F}, Offset{0.8333F, 0.3333F}}},
                    Line{{Offset{0.1667F, 0.6667F}, Offset{0.25F, 0.6667F}}},
                    Line{{Offset{0.5F, 0.6667F}, Offset{0.8333F, 0.6667F}}},
                    Line{{Offset{0.5417F, 0.2333F}, Offset{0.6125F, 0.2625F},
                          Offset{0.6417F, 0.3333F}, Offset{0.6125F, 0.4042F},
                          Offset{0.5417F, 0.4333F}, Offset{0.4708F, 0.4042F},
                          Offset{0.4417F, 0.3333F}, Offset{0.4708F, 0.2625F},
                          Offset{0.5417F, 0.2333F}}},
                    Line{{Offset{0.375F, 0.5667F}, Offset{0.4458F, 0.5958F},
                          Offset{0.475F, 0.6667F}, Offset{0.4458F, 0.7375F},
                          Offset{0.375F, 0.7667F}, Offset{0.3042F, 0.7375F},
                          Offset{0.275F, 0.6667F}, Offset{0.3042F, 0.5958F},
                          Offset{0.375F, 0.5667F}}}};
        table[static_cast<std::size_t>(IconId::NavFeedback)] =
            Catalog{Line{{Offset{0.875F, 0.4583F}, Offset{0.7775F, 0.6942F},
                          Offset{0.5417F, 0.7917F}, Offset{0.2917F, 0.7917F},
                          Offset{0.125F, 0.9167F}, Offset{0.125F, 0.2083F},
                          Offset{0.1496F, 0.1496F}, Offset{0.2083F, 0.125F},
                          Offset{0.5417F, 0.125F}, Offset{0.7775F, 0.2225F},
                          Offset{0.875F, 0.4583F}}},
                    Line{{Offset{0.3333F, 0.375F}, Offset{0.6667F, 0.375F}}},
                    Line{{Offset{0.3333F, 0.5417F}, Offset{0.5417F, 0.5417F}}}};
        {
            // NavTheme：整圆（24 段折线逼近）+ 右半填充的三条竖弦
            // 近似（稿为 currentColor 实心半圆，目录只支持描边）。
            Catalog theme;
            Line circle;
            constexpr float kCx = 12.0F;
            constexpr float kCy = 12.0F;
            constexpr float kRadius = 9.0F;
            constexpr int kSegments = 24;
            for (int i = 0; i <= kSegments; ++i) {
                const float angle =
                    2.0F * 3.1415927F * static_cast<float>(i) / kSegments;
                circle.push_back(
                    Offset{(kCx + kRadius * std::cos(angle)) / 24.0F,
                           (kCy + kRadius * std::sin(angle)) / 24.0F});
            }
            theme.push_back(std::move(circle));
            theme.push_back(Line{{Offset{0.625F, 0.1463F},
                                  Offset{0.625F, 0.8538F}}});
            theme.push_back(Line{{Offset{0.7292F, 0.2033F},
                                  Offset{0.7292F, 0.7967F}}});
            theme.push_back(Line{{Offset{0.8333F, 0.3283F},
                                  Offset{0.8333F, 0.6717F}}});
            table[static_cast<std::size_t>(IconId::NavTheme)] = std::move(theme);
        }
        // --- Designer 工具栏（designer-toolbar-redesign.html 版本 A）：
        // 16 栅格设计后 /16 归一；线帽/拐角沿用目录圆帽惯例。 ---
        // TextAdd：T 字 + 右下加号（插入文本节点）。
        table[static_cast<std::size_t>(IconId::TextAdd)] =
            Catalog{Line{{Offset{0.1875F, 0.25F}, Offset{0.6875F, 0.25F}}},
                    Line{{Offset{0.4375F, 0.25F}, Offset{0.4375F, 0.8125F}}},
                    Line{{Offset{0.7625F, 0.65F}, Offset{0.7625F, 0.9F}}},
                    Line{{Offset{0.6375F, 0.775F}, Offset{0.8875F, 0.775F}}}};
        // Duplicate：前框完整 + 后框右下两边（复制节点）。
        table[static_cast<std::size_t>(IconId::Duplicate)] =
            Catalog{Line{{Offset{0.15625F, 0.15625F}, Offset{0.65625F, 0.15625F},
                          Offset{0.65625F, 0.65625F}, Offset{0.15625F, 0.65625F},
                          Offset{0.15625F, 0.15625F}}},
                    Line{{Offset{0.375F, 0.84375F}, Offset{0.76875F, 0.84375F}}},
                    Line{{Offset{0.76875F, 0.84375F}, Offset{0.76875F, 0.4375F}}}};
        // Trash：桶盖 + 收腰桶身 + 两条肋线（删除节点）。
        table[static_cast<std::size_t>(IconId::Trash)] =
            Catalog{Line{{Offset{0.1875F, 0.28125F}, Offset{0.8125F, 0.28125F}}},
                    Line{{Offset{0.4F, 0.28125F}, Offset{0.4F, 0.1875F},
                          Offset{0.6F, 0.1875F}, Offset{0.6F, 0.28125F}}},
                    Line{{Offset{0.275F, 0.28125F}, Offset{0.31875F, 0.84375F},
                          Offset{0.68125F, 0.84375F}, Offset{0.725F, 0.28125F}}},
                    Line{{Offset{0.41875F, 0.46875F}, Offset{0.41875F, 0.675F}}},
                    Line{{Offset{0.58125F, 0.46875F}, Offset{0.58125F, 0.675F}}}};
        // ArrowUp / ArrowDown：单箭头（节点上移/下移）。
        table[static_cast<std::size_t>(IconId::ArrowUp)] =
            Catalog{Line{{Offset{0.5F, 0.8125F}, Offset{0.5F, 0.1875F}}},
                    Line{{Offset{0.28125F, 0.40625F}, Offset{0.5F, 0.1875F},
                          Offset{0.71875F, 0.40625F}}}};
        table[static_cast<std::size_t>(IconId::ArrowDown)] =
            Catalog{Line{{Offset{0.5F, 0.1875F}, Offset{0.5F, 0.8125F}}},
                    Line{{Offset{0.28125F, 0.59375F}, Offset{0.5F, 0.8125F},
                          Offset{0.71875F, 0.59375F}}}};
        // Bug：圆角桶身 + 头线/触角 + 六足（调试预览）。
        table[static_cast<std::size_t>(IconId::Bug)] =
            Catalog{Line{{Offset{0.3375F, 0.3625F}, Offset{0.6625F, 0.3625F},
                          Offset{0.6625F, 0.7625F}, Offset{0.3375F, 0.7625F},
                          Offset{0.3375F, 0.3625F}}},
                    Line{{Offset{0.5F, 0.3625F}, Offset{0.5F, 0.2625F}}},
                    Line{{Offset{0.4F, 0.2625F}, Offset{0.3125F, 0.1625F}}},
                    Line{{Offset{0.6F, 0.2625F}, Offset{0.6875F, 0.1625F}}},
                    Line{{Offset{0.3375F, 0.5F}, Offset{0.1875F, 0.5F}}},
                    Line{{Offset{0.3375F, 0.65F}, Offset{0.1875F, 0.65F}}},
                    Line{{Offset{0.6625F, 0.5F}, Offset{0.8125F, 0.5F}}},
                    Line{{Offset{0.6625F, 0.65F}, Offset{0.8125F, 0.65F}}},
                    Line{{Offset{0.43125F, 0.7625F}, Offset{0.43125F, 0.8625F}}},
                    Line{{Offset{0.56875F, 0.7625F}, Offset{0.56875F, 0.8625F}}}};
        // Stop：实心观感的停止方框（stroke 闭合，同 Play 的处理）。
        table[static_cast<std::size_t>(IconId::Stop)] =
            Catalog{Line{{Offset{0.2625F, 0.2625F}, Offset{0.7375F, 0.2625F},
                          Offset{0.7375F, 0.7375F}, Offset{0.2625F, 0.7375F},
                          Offset{0.2625F, 0.2625F}}}};
        // Save：软盘外框 + 快门 + 标签窗（保存）。
        table[static_cast<std::size_t>(IconId::Save)] =
            Catalog{Line{{Offset{0.1875F, 0.15625F}, Offset{0.6875F, 0.15625F},
                          Offset{0.84375F, 0.3125F}, Offset{0.84375F, 0.84375F},
                          Offset{0.1875F, 0.84375F}, Offset{0.1875F, 0.15625F}}},
                    Line{{Offset{0.34375F, 0.15625F}, Offset{0.34375F, 0.34375F},
                          Offset{0.625F, 0.34375F}, Offset{0.625F, 0.15625F}}},
                    Line{{Offset{0.34375F, 0.84375F}, Offset{0.34375F, 0.59375F},
                          Offset{0.65625F, 0.59375F}, Offset{0.65625F, 0.84375F}}}};
        // SaveAs：软盘 + 右下铅笔（另存为）。
        table[static_cast<std::size_t>(IconId::SaveAs)] =
            Catalog{Line{{Offset{0.1875F, 0.15625F}, Offset{0.6875F, 0.15625F},
                          Offset{0.84375F, 0.3125F}, Offset{0.84375F, 0.84375F},
                          Offset{0.7F, 0.84375F}, Offset{0.1875F, 0.84375F},
                          Offset{0.1875F, 0.15625F}}},
                    Line{{Offset{0.34375F, 0.15625F}, Offset{0.34375F, 0.34375F},
                          Offset{0.625F, 0.34375F}, Offset{0.625F, 0.15625F}}},
                    Line{{Offset{0.7F, 0.56875F}, Offset{0.8875F, 0.75625F}}},
                    Line{{Offset{0.65F, 0.9F}, Offset{0.8875F, 0.75625F}}}};
        // FolderPlus：Folder 轮廓 + 中央加号（新建工程）。
        table[static_cast<std::size_t>(IconId::FolderPlus)] =
            Catalog{Line{{Offset{0.12F, 0.25F}, Offset{0.4F, 0.25F},
                          Offset{0.5F, 0.38F}, Offset{0.88F, 0.38F},
                          Offset{0.88F, 0.78F}, Offset{0.12F, 0.78F},
                          Offset{0.12F, 0.25F}}},
                    Line{{Offset{0.5F, 0.48F}, Offset{0.5F, 0.68F}}},
                    Line{{Offset{0.4F, 0.58F}, Offset{0.6F, 0.58F}}}};
        // Moon：月牙（明暗主题切换；外弧 260° + 内弧回接，24 段折线逼近）。
        {
            Catalog moon;
            Line crescent;
            constexpr float kOuterCx = 0.44F;
            constexpr float kOuterCy = 0.5F;
            constexpr float kOuterR = 0.36F;
            constexpr int kSegments = 24;
            // 外弧：从 -40° 逆时针扫 260° 到 220°（经过左侧）。
            constexpr float kStart = -0.6981317F;   // -40°
            constexpr float kSweep = 4.5378561F;    // 260°
            for (int i = 0; i <= kSegments; ++i) {
                const float angle =
                    kStart + kSweep * static_cast<float>(i) / kSegments;
                crescent.push_back(
                    Offset{kOuterCx + kOuterR * std::cos(angle),
                           kOuterCy + kOuterR * std::sin(angle)});
            }
            // 内弧：以右侧偏心圆从终点扫回起点（开口朝右的月牙）。
            constexpr float kInnerCx = 0.62F;
            constexpr float kInnerR = 0.26F;
            for (int i = kSegments; i >= 0; --i) {
                // 内弧起止角与外弧终点/起点同点（0.62+0.26cosθ 拼接处近似）。
                const float angle =
                    -0.35F + 1.9F * static_cast<float>(i) / kSegments;
                crescent.push_back(
                    Offset{kInnerCx + kInnerR * std::cos(angle),
                           kOuterCy + kInnerR * std::sin(angle)});
            }
            moon.push_back(std::move(crescent));
            table[static_cast<std::size_t>(IconId::Moon)] = std::move(moon);
        }
        // Density：上下两横线 + 中间双向竖箭头（密度切换）。
        table[static_cast<std::size_t>(IconId::Density)] =
            Catalog{Line{{Offset{0.1875F, 0.21875F}, Offset{0.6875F, 0.21875F}}},
                    Line{{Offset{0.1875F, 0.78125F}, Offset{0.6875F, 0.78125F}}},
                    Line{{Offset{0.5F, 0.35625F}, Offset{0.5F, 0.64375F}}},
                    Line{{Offset{0.3875F, 0.46875F}, Offset{0.5F, 0.35625F},
                          Offset{0.6125F, 0.46875F}}},
                    Line{{Offset{0.3875F, 0.53125F}, Offset{0.5F, 0.64375F},
                          Offset{0.6125F, 0.53125F}}}};
        // DeviceScale：屏幕 + 底座 + 屏内扩展箭头（DPI 缩放）。
        table[static_cast<std::size_t>(IconId::DeviceScale)] =
            Catalog{Line{{Offset{0.1375F, 0.1875F}, Offset{0.8625F, 0.1875F},
                          Offset{0.8625F, 0.7125F}, Offset{0.1375F, 0.7125F},
                          Offset{0.1375F, 0.1875F}}},
                    Line{{Offset{0.375F, 0.85F}, Offset{0.625F, 0.85F}}},
                    Line{{Offset{0.5F, 0.7125F}, Offset{0.5F, 0.85F}}},
                    Line{{Offset{0.3375F, 0.575F}, Offset{0.65F, 0.2625F}}},
                    Line{{Offset{0.65F, 0.2625F}, Offset{0.475F, 0.2625F}}},
                    Line{{Offset{0.65F, 0.2625F}, Offset{0.65F, 0.4375F}}}};
        // Font：A 字形 + 横梁（字号缩放）。
        table[static_cast<std::size_t>(IconId::Font)] =
            Catalog{Line{{Offset{0.2625F, 0.7875F}, Offset{0.44375F, 0.2125F},
                          Offset{0.55625F, 0.2125F}, Offset{0.7375F, 0.7875F}}},
                    Line{{Offset{0.33125F, 0.59375F}, Offset{0.66875F, 0.59375F}}}};
        // Contrast：整圆（24 段逼近）+ 右半填充的三条竖弦（高对比）。
        {
            Catalog contrast;
            Line circle;
            constexpr float kCx = 0.5F;
            constexpr float kCy = 0.5F;
            constexpr float kRadius = 0.36F;
            constexpr int kSegments = 24;
            for (int i = 0; i <= kSegments; ++i) {
                const float angle =
                    6.2831853F * static_cast<float>(i) / kSegments;
                circle.push_back(Offset{kCx + kRadius * std::cos(angle),
                                        kCy + kRadius * std::sin(angle)});
            }
            contrast.push_back(std::move(circle));
            contrast.push_back(Line{{Offset{0.64F, 0.2371F},
                                     Offset{0.64F, 0.7629F}}});
            contrast.push_back(Line{{Offset{0.75F, 0.2937F},
                                     Offset{0.75F, 0.7063F}}});
            contrast.push_back(Line{{Offset{0.83F, 0.41F},
                                     Offset{0.83F, 0.59F}}});
            table[static_cast<std::size_t>(IconId::Contrast)] =
                std::move(contrast);
        }
        // Cursor：指针光标轮廓（预览状态循环）。
        table[static_cast<std::size_t>(IconId::Cursor)] =
            Catalog{Line{{Offset{0.25F, 0.1625F}, Offset{0.7875F, 0.5F},
                          Offset{0.58125F, 0.5625F}, Offset{0.70625F, 0.79375F},
                          Offset{0.6F, 0.85F}, Offset{0.475F, 0.61875F},
                          Offset{0.3F, 0.75F}, Offset{0.25F, 0.1625F}}}};
        // ZoomOut / ZoomIn：Search 镜圆 + 镜内减/加号。
        {
            auto searchLines = [] {
                Catalog lines;
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
                lines.push_back(std::move(circle));
                const float edge = kRadius * 0.7071068F;
                lines.push_back(Line{{Offset{kCx + edge, kCy + edge},
                                       Offset{0.8125F, 0.8125F}}});
                return lines;
            };
            Catalog zoomOut = searchLines();
            zoomOut.push_back(Line{{Offset{0.33F, 0.4375F},
                                    Offset{0.545F, 0.4375F}}});
            table[static_cast<std::size_t>(IconId::ZoomOut)] = std::move(zoomOut);
            Catalog zoomIn = searchLines();
            zoomIn.push_back(Line{{Offset{0.4375F, 0.33F},
                                   Offset{0.4375F, 0.545F}}});
            zoomIn.push_back(Line{{Offset{0.33F, 0.4375F},
                                   Offset{0.545F, 0.4375F}}});
            table[static_cast<std::size_t>(IconId::ZoomIn)] = std::move(zoomIn);
        }
        // Fit：四角取景框 + 中心点（缩放重置；点为 8 段八边形逼近）。
        {
            Catalog fit;
            fit.push_back(Line{{Offset{0.15625F, 0.325F}, Offset{0.15625F, 0.15625F},
                                Offset{0.325F, 0.15625F}}});
            fit.push_back(Line{{Offset{0.64375F, 0.15625F}, Offset{0.8125F, 0.15625F},
                                Offset{0.8125F, 0.325F}}});
            fit.push_back(Line{{Offset{0.8125F, 0.64375F}, Offset{0.8125F, 0.8125F},
                                Offset{0.64375F, 0.8125F}}});
            fit.push_back(Line{{Offset{0.325F, 0.8125F}, Offset{0.15625F, 0.8125F},
                                Offset{0.15625F, 0.64375F}}});
            Line dot;
            constexpr float kDotR = 0.1F;
            for (int i = 0; i <= 8; ++i) {
                const float angle =
                    6.2831853F * static_cast<float>(i) / 8;
                dot.push_back(Offset{0.5F + kDotR * std::cos(angle),
                                     0.5F + kDotR * std::sin(angle)});
            }
            fit.push_back(std::move(dot));
            table[static_cast<std::size_t>(IconId::Fit)] = std::move(fit);
        }
        // 左面板工具箱分组头（16 栅格归一）。
        // Content：三条递减横线（Text/Image/Icon 静态内容组）。
        table[static_cast<std::size_t>(IconId::Content)] =
            Catalog{Line{{Offset{0.1875F, 0.28F}, Offset{0.625F, 0.28F}}},
                    Line{{Offset{0.1875F, 0.5F}, Offset{0.8125F, 0.5F}}},
                    Line{{Offset{0.1875F, 0.72F}, Offset{0.5F, 0.72F}}}};
        // Scope：左右方括号（FocusScope/ThemeScope 行为包装组）。
        table[static_cast<std::size_t>(IconId::Scope)] =
            Catalog{Line{{Offset{0.34F, 0.22F}, Offset{0.22F, 0.31F},
                          Offset{0.22F, 0.69F}, Offset{0.34F, 0.78F}}},
                    Line{{Offset{0.66F, 0.22F}, Offset{0.78F, 0.31F},
                          Offset{0.78F, 0.69F}, Offset{0.66F, 0.78F}}}};
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
