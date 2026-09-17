// 集合控件：SelectionModel 实现（见头注释）。

#include "lumen/widgets/selection.h"

#include <algorithm>
#include <string_view>
#include <unordered_set>

namespace lumen::widgets {

void SelectionModel::setMode(SelectionMode mode) {
    if (mode == mode_) {
        return;
    }
    mode_ = mode;
    // 模式收紧时裁剪选择集：Single 仅保留 current（若其已选中），
    // None 清空。
    if (mode_ == SelectionMode::None) {
        clear();
    } else if (mode_ == SelectionMode::Single && selected_.size() > 1) {
        const bool keepCurrent = !current_.empty() && isSelected(current_);
        selected_.clear();
        if (keepCurrent) {
            selected_.push_back(current_);
        }
        notifySelectionChanged();
    }
}

void SelectionModel::setCurrent(const std::string& key) {
    if (current_ == key) {
        return;
    }
    current_ = key;
    notifyCurrentChanged();
}

void SelectionModel::setSelected(std::vector<std::string> keys) {
    // 去重并保序（顺序 = 选择操作顺序；Ctrl+A 全选等批量路径是 O(n)——
    // 千/万级行集的线性去重会卡 UI）。成员判断用哈希集合；计数与
    // isSelected 的线性查找保留（自用规模下选中集通常远小于行数）。
    std::unordered_set<std::string_view> seen;
    seen.reserve(keys.size());
    std::vector<std::string> unique;
    unique.reserve(keys.size());
    for (const auto& key : keys) {
        if (seen.insert(std::string_view(key)).second) {
            unique.push_back(key);
        }
    }
    if (unique == selected_) {
        return;
    }
    selected_ = std::move(unique);
    notifySelectionChanged();
}

bool SelectionModel::toggle(const std::string& key) {
    const auto it = std::find(selected_.begin(), selected_.end(), key);
    if (it != selected_.end()) {
        selected_.erase(it);
        notifySelectionChanged();
        return false;
    }
    selected_.push_back(key);
    notifySelectionChanged();
    return true;
}

bool SelectionModel::isSelected(const std::string& key) const {
    return std::find(selected_.begin(), selected_.end(), key) != selected_.end();
}

void SelectionModel::clear() {
    if (selected_.empty()) {
        return;
    }
    selected_.clear();
    notifySelectionChanged();
}

void SelectionModel::setKeySequence(KeySequence sequence) {
    sequence_ = std::move(sequence);
}

void SelectionModel::click(const std::string& key, bool ctrl, bool shift) {
    setCurrent(key);
    switch (mode_) {
        case SelectionMode::None:
            return;
        case SelectionMode::Single:
            setSelected({key});
            break;
        case SelectionMode::Multiple:
            toggle(key);
            break;
        case SelectionMode::Extended:
            if (shift && !anchor_.empty()) {
                selectRange(anchor_, key);
            } else if (ctrl) {
                toggle(key);
                // Ctrl 修正后锚点跟随（下次 Shift 从该行起算）。
                anchor_ = key;
            } else {
                setSelected({key});
                anchor_ = key;
            }
            break;
    }
}

void SelectionModel::moveTo(const std::string& key, bool extend) {
    setCurrent(key);
    switch (mode_) {
        case SelectionMode::None:
        case SelectionMode::Multiple:
            return;  // 键盘只移 current，不改选择集。
        case SelectionMode::Single:
            setSelected({key});
            break;
        case SelectionMode::Extended:
            if (extend && !anchor_.empty()) {
                selectRange(anchor_, key);
            } else {
                // 移动即随动选中（Qt Extended 键盘导航语义）。
                setSelected({key});
                anchor_ = key;
            }
            break;
    }
}

void SelectionModel::selectRange(const std::string& anchor,
                                 const std::string& key) {
    if (!sequence_) {
        setSelected({key});
        return;
    }
    // 先按值拷贝：区间回调可能重建行序列（树的扁平化），而 anchor/key
    // 可能正引用其元素（如 moveTo(visibleRows()[i].key)）。
    const std::string anchorKey = anchor;
    const std::string targetKey = key;
    const std::vector<std::string> range = sequence_(anchorKey, targetKey);
    setSelected(range);
}

void SelectionModel::notifySelectionChanged() {
    if (onSelectionChanged) {
        onSelectionChanged();
    }
}

void SelectionModel::notifyCurrentChanged() {
    if (onCurrentChanged) {
        onCurrentChanged(current_);
    }
}

}  // namespace lumen::widgets
