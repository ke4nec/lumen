#pragma once

// 集合控件语义层共享实现（docs/lumen-collection-controls-design.md §4.1）：
// List 与 Tree 控制器中语义相同的部分——键盘导航（§6.3/§7.4 共享键位）、
// ScrollAlignment 四向滚动对齐、激活/行点击 sink 接线与 Shift 区间 key
// 序列——在此单源实现。全部 inline 自由函数，属私有实现细节，不进入
// 公共 API。Tree 专属键位（Left/Right）、chevron toggle 与扁平化/按 key
// 的 extent 缓存仍在 TreeController。UI 线程独占。

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/windowing.h"
#include "lumen/widgets/collection.h"
#include "lumen/widgets/selection.h"

namespace lumen::widgets::detail {

// 视觉系统 §3.2 / collection-design §10.1：Medium 档默认行高与行水平
// 内边距（8/12/16 中的 12）。
inline constexpr float kDefaultRowExtent = 40.0F;
inline constexpr float kRowPaddingX = 12.0F;

// 行 key 访问器（List：keyOf(index)；Tree：扁平化 rows_[index].key）。
using KeyAt = std::function<std::string(std::size_t)>;

// 行点击接收器（key + Ctrl/Shift 修饰）。
using RowClick =
    std::function<void(const std::string& key, bool ctrl, bool shift)>;

// key → 行号（线性扫描；不在序列返回 false）。
inline bool findKeyIndex(std::size_t count, const KeyAt& keyAt,
                         const std::string& key, std::size_t& index) {
    for (std::size_t i = 0; i < count; ++i) {
        if (keyAt(i) == key) {
            index = i;
            return true;
        }
    }
    return false;
}

// [from, to] 闭区间的行 key 序列（方向无关；任一 key 不在序列返回空）。
// SelectionModel 的 KeySequence 回调由此实现（List/Tree 各自注入行序）。
inline std::vector<std::string> closedKeyRange(std::size_t count,
                                               const KeyAt& keyAt,
                                               const std::string& from,
                                               const std::string& to,
                                               const std::function<bool(std::size_t)>& enabled = {}) {
    std::vector<std::string> keys;
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!findKeyIndex(count, keyAt, from, begin) ||
        !findKeyIndex(count, keyAt, to, end)) {
        return keys;
    }
    if (begin > end) {
        std::swap(begin, end);
    }
    for (std::size_t i = begin; i <= end && i < count; ++i) {
        if (!enabled || enabled(i)) keys.push_back(keyAt(i));
    }
    return keys;
}

// 行点击分发（collection-design §6.5）：onClick 名以 rowPrefix 开头则
// 解析出行 key 并带修饰键回调，返回 true（已消费）。单一 sink 恒 O(1)，
// 虚拟化行不注册常驻 handler（滚动累积）。
inline bool dispatchRowClick(app::AppShell* shell,
                             const std::string& onClick,
                             const std::string& rowPrefix,
                             const RowClick& onRowClick) {
    if (onClick.rfind(rowPrefix, 0) != 0) {
        return false;
    }
    const std::string key = onClick.substr(rowPrefix.size());
    if (key.empty()) {
        return false;
    }
    const auto modifiers = shell != nullptr
                               ? shell->controller().pointerModifiers()
                               : core::kModifierNone;
    onRowClick(key, (modifiers & core::kModifierCtrl) != 0,
               (modifiers & core::kModifierShift) != 0);
    return true;
}

// 激活 sink：owner+":item:" 前缀命中 → activate(key)（双击/Enter/语义
// Activate 同路径；多集合共存，各 sink 各自比对前缀）。
inline std::function<bool(const std::string&, const std::string&, bool)>
makeRowActivateSink(const std::string& owner,
                    std::function<void(const std::string&)> activate) {
    const std::string prefix = owner + ":item:";
    return [prefix, activate = std::move(activate)](
               const std::string& rowKey, const std::string&, bool) {
        if (rowKey.rfind(prefix, 0) != 0) {
            return false;
        }
        activate(rowKey.substr(prefix.size()));
        return true;
    };
}

// 四向滚动对齐（collection-design §6.1）：Visible=最小移动（已可见不
// 动）；Start/Center/End=显式对齐。目标先夹取到内容范围，再经 scrollTo
// 的视口范围夹取（双重 clamp 与原 List/Tree 实现一致；等价于 M3
// VirtualListController::scrollToIndex 的最小移动语义）。
inline void scrollToAligned(const core::VirtualListSource& source,
                            std::size_t index, ScrollAlignment align) {
    core::ScrollController* scroll = source.scrollController();
    if (scroll == nullptr) {
        return;
    }
    const float viewport = scroll->viewportExtent();
    if (viewport <= 0.0F) {
        return;
    }
    const float top = source.offsetOfIndex(index);
    const float extent = source.extentOf(index);
    float target = 0.0F;
    switch (align) {
        case ScrollAlignment::Visible: {
            const float offset = scroll->offset();
            if (top < offset) {
                target = top;
            } else if (top + extent > offset + viewport) {
                target = top + extent - viewport;
            } else {
                return;  // 已可见。
            }
            break;
        }
        case ScrollAlignment::Start:
            target = top;
            break;
        case ScrollAlignment::Center:
            target = top - (viewport - extent) * 0.5F;
            break;
        case ScrollAlignment::End:
            target = top + extent - viewport;
            break;
    }
    const float maxOffset =
        std::max(0.0F, source.totalExtent() - viewport);
    scroll->scrollTo(std::clamp(target, 0.0F, maxOffset));
}

// 共享键盘导航（collection-design §6.3/§7.4）：Ctrl+A 全选、
// Up/Down/Home/End/PageUp/PageDown 移动 current（Extended 随动选择）并按
// 对齐滚动。Left/Right 与 Enter/Tab 不在此处理（前者 Tree 专属，后者走
// 激活路径/焦点遍历）。返回 false = 未消费。
//
// 滚动一律按 key 在选择移动之后重新解析行号：应用回调（onCurrentChanged
// 等）可能 modelChanged 失效行缓存，先取的 index 会指向漂移后的行。
inline bool handleCollectionKeys(app::AppShell* shell,
                                 const std::string& owner,
                                 SelectionModel& selection,
                                 const core::VirtualListSource& source,
                                 const KeyAt& keyAt, core::Key key,
                                 core::KeyModifiers modifiers, char keyChar,
                                 const std::function<bool(std::size_t)>& enabled = {}) {
    const std::size_t count = source.itemCount();
    if (count == 0) {
        return false;
    }
    std::size_t current = 0;
    const bool hasCurrent =
        !selection.currentKey().empty() &&
        findKeyIndex(count, keyAt, selection.currentKey(), current);
    const bool ctrl = (modifiers & core::kModifierCtrl) != 0;
    const bool shift = (modifiers & core::kModifierShift) != 0;

    // Ctrl+A：Extended 全选；Single 选中 current（collection-design §6.3）。
    if (ctrl && (keyChar == 'a' || keyChar == 'A')) {
        if (selection.mode() == SelectionMode::Extended) {
            std::vector<std::string> all;
            all.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                if (!enabled || enabled(i)) all.push_back(keyAt(i));
            }
            selection.setSelected(std::move(all));
        } else if (selection.mode() == SelectionMode::Single && hasCurrent &&
                   (!enabled || enabled(current))) {
            selection.setSelected({keyAt(current)});
        }
        if (shell != nullptr) {
            shell->markDirty();
        }
        return true;
    }

    const auto scrollToKeyAligned = [&](const std::string& key,
                                        ScrollAlignment align) {
        std::size_t index = 0;
        if (findKeyIndex(source.itemCount(), keyAt, key, index)) {
            scrollToAligned(source, index, align);
        }
    };
    const auto move = [&](std::size_t target, bool backward = false) {
        while (enabled && !enabled(target)) {
            if (backward ? target == 0 : target + 1 >= count) return;
            if (backward) --target;
            else ++target;
        }
        // 按值拷贝 key：选择回调可能失效行缓存，引用会随 clear 悬垂。
        const std::string nextKey = keyAt(target);
        selection.moveTo(nextKey, shift);
        if (shell != nullptr) {
            shell->focus().setFocus(owner + ":item:" + nextKey);
        }
        scrollToKeyAligned(nextKey, ScrollAlignment::Visible);
        if (shell != nullptr) {
            shell->markDirty();
        }
    };

    switch (key) {
        case core::Key::Up:
            move(hasCurrent && current > 0 ? current - 1 : 0, true);
            return true;
        case core::Key::Down:
            move(hasCurrent ? std::min(current + 1, count - 1) : 0);
            return true;
        case core::Key::Home: {
            move(0);
            scrollToKeyAligned(selection.currentKey(), ScrollAlignment::Start);
            return true;
        }
        case core::Key::End: {
            move(count - 1, true);
            scrollToKeyAligned(selection.currentKey(), ScrollAlignment::End);
            return true;
        }
        case core::Key::PageUp:
        case core::Key::PageDown: {
            const float extent =
                std::max(1.0F, source.extentOf(hasCurrent ? current : 0));
            const std::size_t step = std::max(
                1U, static_cast<unsigned>(
                        source.scrollController()->viewportExtent() / extent));
            if (key == core::Key::PageUp) {
                move(hasCurrent && current > step ? current - step : 0, true);
            } else {
                move(hasCurrent ? std::min(current + step, count - 1)
                                : std::min(step, count - 1));
            }
            return true;
        }
        default:
            // Enter/Tab/其余键不在此消费：Enter 走激活路径（避免双重激
            // 活），Tab 留给焦点遍历。
            return false;
    }
}

// 集合行 Row 壳：key/collectionRow/selected/onClick/水平内边距/语义
// role + actions（hover/pressed/选中/焦点环由 StyleResolver 与 painter
// 的集合行路径驱动）。crossAxis 与 role 由控件语义决定：List=Center/
// listItem，Tree=Center/treeItem（Tree 的 semanticsValue 由调用方补写）。
// onClick 是行身份（attach 的 sink 按前缀解析；空 = 不可聚焦/激活）。
inline void applyCollectionRowShell(core::Widget& row,
                                    const std::string& owner,
                                    const std::string& key, bool selected,
                                    const std::string& onClick,
                                    core::CrossAxisAlignment crossAxis,
                                    const char* semanticsRole) {
    row.type = core::WidgetType::Row;
    row.key = owner + ":item:" + key;
    row.collectionRow = true;
    row.selected = selected;
    row.onClick = onClick;
    row.crossAxis = crossAxis;
    row.padding = core::EdgeInsets::symmetric(kRowPaddingX, 0.0F);
    row.semanticsRole = semanticsRole;
    row.semanticsActions =
        accessibility::kActionFocus | accessibility::kActionActivate;
}

}  // namespace lumen::widgets::detail
