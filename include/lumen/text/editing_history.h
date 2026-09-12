#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lumen/text/editing_value.h"

namespace lumen::text {

// M1：TextEditingValue 的 undo/redo 栈。
//
// - 值语义快照（文本 + 选区 + composing 已落盘部分；preedit 更新不进栈）。
// - 连续单字输入/单字删除合并为一个 undo 项；选区移动、IME 提交、
//   粘贴/剪切/程序设置均为事务边界。
// - UI 线程独占；每个字段一个实例（由 InteractionController 按 bind 持有）。
enum class EditKind : std::uint8_t {
    Insert,
    Delete,
    Other,
    Selection,
};

class EditingHistory {
  public:
    // 初始种子（聚焦时以当前 store 值调用；空栈时首次 push 自动种子化）。
    void seed(const TextEditingValue& value);
    // 提交一次编辑结果。Selection 且文本不变时只打断合并，不进栈。
    void push(const TextEditingValue& value, EditKind kind);
    // 事务边界：begin/end 之间的多次 push 合并为单个 Other 项。
    void beginTransaction();
    void endTransaction();

    [[nodiscard]] std::optional<TextEditingValue> undo();
    [[nodiscard]] std::optional<TextEditingValue> redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    void clear();

    [[nodiscard]] std::size_t undoSize() const;
    [[nodiscard]] std::size_t redoSize() const;

    static constexpr std::size_t kCapacity = 100;

  private:
    [[nodiscard]] bool tryMerge(const TextEditingValue& prev,
                                const TextEditingValue& next,
                                EditKind kind) const;

    std::vector<TextEditingValue> states_{};
    std::size_t pos_{0};
    bool hasState_{false};
    // 上一项是否可被合并（Insert/Delete 连续序列中）。
    bool mergeable_{false};
    EditKind lastKind_{EditKind::Other};
    // 事务嵌套深度与挂起值。
    std::size_t transactionDepth_{0};
    std::optional<TextEditingValue> pending_{};
};

}  // namespace lumen::text
