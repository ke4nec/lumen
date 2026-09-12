#include "lumen/text/editing_history.h"

namespace lumen::text {
namespace {

// 单 grapheme 插入：新文本 = 旧[0:k] + 单字 + 旧[k:]，且 caret 前进 1。
bool isSingleInsert(const TextEditingValue& prev,
                    const TextEditingValue& next) {
    if (!prev.selection().collapsed() || !next.selection().collapsed()) {
        return false;
    }
    const std::size_t prevCount = prev.graphemeLength();
    const std::size_t nextCount = next.graphemeLength();
    if (nextCount != prevCount + 1) {
        return false;
    }
    if (next.caret() != prev.caret() + 1) {
        return false;
    }
    const std::size_t at = prev.caret();
    if (graphemeSubstring(prev.text(), 0, at) !=
        graphemeSubstring(next.text(), 0, at)) {
        return false;
    }
    if (graphemeSubstring(prev.text(), at, prevCount) !=
        graphemeSubstring(next.text(), at + 1, nextCount)) {
        return false;
    }
    return graphemeCount(graphemeSubstring(next.text(), at, at + 1)) == 1;
}

// 单 grapheme 删除（Backspace：caret 回退 1；Delete：caret 不动）。
bool isSingleDelete(const TextEditingValue& prev,
                    const TextEditingValue& next) {
    if (!prev.selection().collapsed() || !next.selection().collapsed()) {
        return false;
    }
    const std::size_t prevCount = prev.graphemeLength();
    const std::size_t nextCount = next.graphemeLength();
    if (prevCount != nextCount + 1) {
        return false;
    }
    // Backspace：删 caret-1。
    if (next.caret() + 1 == prev.caret()) {
        const std::size_t at = next.caret();
        return graphemeSubstring(prev.text(), 0, at) ==
                   graphemeSubstring(next.text(), 0, at) &&
               graphemeSubstring(prev.text(), at + 1, prevCount) ==
                   graphemeSubstring(next.text(), at, nextCount);
    }
    // Delete：删 caret 处。
    if (next.caret() == prev.caret()) {
        const std::size_t at = prev.caret();
        return graphemeSubstring(prev.text(), 0, at) ==
                   graphemeSubstring(next.text(), 0, at) &&
               graphemeSubstring(prev.text(), at + 1, prevCount) ==
                   graphemeSubstring(next.text(), at, nextCount);
    }
    return false;
}

}  // namespace

void EditingHistory::seed(const TextEditingValue& value) {
    if (hasState_) {
        return;
    }
    states_.push_back(value);
    pos_ = 0;
    hasState_ = true;
    mergeable_ = false;
}

void EditingHistory::push(const TextEditingValue& value, EditKind kind) {
    if (transactionDepth_ > 0) {
        pending_ = value;
        return;
    }
    if (!hasState_) {
        states_.push_back(value);
        pos_ = 0;
        hasState_ = true;
        mergeable_ = false;
        lastKind_ = EditKind::Other;
        return;
    }
    const TextEditingValue& current = states_[pos_];
    if (value.text() == current.text()) {
        // 纯选区/光标移动：不进栈，但打断连续合并。
        mergeable_ = false;
        return;
    }
    // undo 后新编辑：丢弃 redo 分支，且不与撤销前合并。
    const bool branched = pos_ + 1 < states_.size();
    if (branched) {
        states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(pos_ + 1),
                      states_.end());
    }
    if (!branched && mergeable_ && lastKind_ == kind &&
        (kind == EditKind::Insert || kind == EditKind::Delete) &&
        tryMerge(current, value, kind)) {
        states_[pos_] = value;
        return;
    }
    states_.push_back(value);
    pos_ = states_.size() - 1;
    mergeable_ = (kind == EditKind::Insert || kind == EditKind::Delete);
    lastKind_ = kind;
    if (states_.size() > kCapacity + 1) {
        states_.erase(states_.begin());
        pos_ -= 1;
    }
}

void EditingHistory::beginTransaction() {
    ++transactionDepth_;
}

void EditingHistory::endTransaction() {
    if (transactionDepth_ == 0) {
        return;
    }
    --transactionDepth_;
    if (transactionDepth_ == 0 && pending_.has_value()) {
        TextEditingValue value = *pending_;
        pending_.reset();
        // 事务整体作为单个 Other 项（不参与字符级合并）。
        mergeable_ = false;
        push(value, EditKind::Other);
    }
}

std::optional<TextEditingValue> EditingHistory::undo() {
    if (!canUndo()) {
        return std::nullopt;
    }
    --pos_;
    mergeable_ = false;
    return states_[pos_];
}

std::optional<TextEditingValue> EditingHistory::redo() {
    if (!canRedo()) {
        return std::nullopt;
    }
    ++pos_;
    mergeable_ = false;
    return states_[pos_];
}

bool EditingHistory::canUndo() const {
    return hasState_ && pos_ > 0;
}

bool EditingHistory::canRedo() const {
    return hasState_ && pos_ + 1 < states_.size();
}

void EditingHistory::clear() {
    states_.clear();
    pos_ = 0;
    hasState_ = false;
    mergeable_ = false;
    transactionDepth_ = 0;
    pending_.reset();
}

std::size_t EditingHistory::undoSize() const {
    return hasState_ ? pos_ : 0;
}

std::size_t EditingHistory::redoSize() const {
    if (!hasState_ || pos_ + 1 >= states_.size()) {
        return 0;
    }
    return states_.size() - pos_ - 1;
}

bool EditingHistory::tryMerge(const TextEditingValue& prev,
                              const TextEditingValue& next,
                              EditKind kind) const {
    if (kind == EditKind::Insert) {
        return isSingleInsert(prev, next);
    }
    if (kind == EditKind::Delete) {
        return isSingleDelete(prev, next);
    }
    return false;
}

}  // namespace lumen::text
