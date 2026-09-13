// M6：图标归一化折线目录（0..1 坐标，stroke 绘制）。
//
// 几何在 16x16 视觉栅格上设计后归一化；线帽/拐角由渲染后端决定
//（圆帽优先，CPU 软件光栅为方帽近似）。每个图标 = 一组折线。

#include "lumen/core/icon_id.h"

namespace lumen::core {
namespace {

using Line = std::vector<Offset>;
using Catalog = std::vector<Line>;

const std::vector<Catalog>& catalog() {
    static const std::vector<Catalog> kCatalog = [] {
        std::vector<Catalog> table(static_cast<std::size_t>(IconId::Info) + 1);
        table[static_cast<std::size_t>(IconId::Check)] =
            Catalog{Line{{Offset{0.20F, 0.52F}, Offset{0.42F, 0.74F},
                  Offset{0.80F, 0.28F}}}};
        table[static_cast<std::size_t>(IconId::Close)] =
            Catalog{Line{{Offset{0.25F, 0.25F}, Offset{0.75F, 0.75F}}},
                    Line{{Offset{0.75F, 0.25F}, Offset{0.25F, 0.75F}}}};
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
        table[static_cast<std::size_t>(IconId::Minus)] =
            Catalog{Line{{Offset{0.22F, 0.50F}, Offset{0.78F, 0.50F}}}};
        table[static_cast<std::size_t>(IconId::Search)] =
            Catalog{Line{{Offset{0.30F, 0.30F}, Offset{0.44F, 0.44F}}},
                    Line{{Offset{0.68F, 0.68F}, Offset{0.80F, 0.80F}}},
                    Line{{Offset{0.44F, 0.26F}, Offset{0.34F, 0.30F},
                          Offset{0.28F, 0.38F}, Offset{0.26F, 0.48F},
                          Offset{0.30F, 0.58F}, Offset{0.38F, 0.62F},
                          Offset{0.48F, 0.62F}, Offset{0.56F, 0.58F},
                          Offset{0.60F, 0.50F}, Offset{0.60F, 0.40F},
                          Offset{0.56F, 0.32F}, Offset{0.48F, 0.26F},
                          Offset{0.44F, 0.26F}}}};
        table[static_cast<std::size_t>(IconId::Info)] =
            Catalog{Line{{Offset{0.50F, 0.26F}, Offset{0.50F, 0.28F}}},
                    Line{{Offset{0.50F, 0.42F}, Offset{0.50F, 0.78F}}}};
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
