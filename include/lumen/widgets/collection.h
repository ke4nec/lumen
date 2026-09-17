#pragma once

// 集合控件（docs/lumen-collection-controls-design.md §4.1/§6.1）：List 与
// Tree/TreeList 公用的滚动对齐语义。单一定义于此；ListController 与
// TreeController 经类内 using 别名引用，`ListController::ScrollAlignment`
// 等既有限定写法保持兼容。

#include <cstdint>

namespace lumen::widgets {

enum class ScrollAlignment : std::uint8_t {
    Visible,  // 最小移动使行可见（默认）
    Start,    // 行顶对齐视口顶
    Center,   // 行居中
    End,      // 行底对齐视口底
};

}  // namespace lumen::widgets
