#pragma once

#include <cstddef>
#include <string>

#include "lumen/text/grapheme.h"

namespace lumen::text {

// v0.3 阶段8B (plan §3.2/§7): 编辑值状态机。
//
// text + selection + composing，全部以 grapheme cluster 索引表示；平台
// 字节偏移转换只发生在适配层。所有编辑操作都是纯函数（值语义），便于
// 事件转译测试与撤销边界判定；IME 的 preedit 只进入 composing 区间，
// 绝不直接写入文档。

struct TextSelection {
    // base = 选区锚点，extent = 活动端（即光标）。
    std::size_t base{0};
    std::size_t extent{0};

    [[nodiscard]] std::size_t start() const {
        return base < extent ? base : extent;
    }
    [[nodiscard]] std::size_t end() const { return base < extent ? extent : base; }
    [[nodiscard]] bool collapsed() const { return base == extent; }
    [[nodiscard]] bool operator==(const TextSelection&) const = default;
};

class TextEditingValue {
  public:
    TextEditingValue() = default;
    explicit TextEditingValue(std::string text)
        : text_(std::move(text)) {}
    TextEditingValue(std::string text, TextSelection selection)
        : text_(std::move(text)), selection_(selection) {
        clamp();
    }

    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] const TextSelection& selection() const {
        return selection_;
    }
    [[nodiscard]] const TextSelection& composing() const {
        return composing_;
    }
    [[nodiscard]] bool composingActive() const { return composingActive_; }
    [[nodiscard]] std::size_t caret() const { return selection_.extent; }
    [[nodiscard]] bool hasSelection() const { return !selection_.collapsed(); }
    [[nodiscard]] std::string selectedText() const {
        return graphemeSubstring(text_, selection_.start(), selection_.end());
    }
    [[nodiscard]] std::size_t graphemeLength() const {
        return graphemeCount(text_);
    }

    bool operator==(const TextEditingValue&) const = default;

    // --- 纯编辑操作（返回新值；图串粒度） ---

    // 替换选区（或光标处）插入文本；清除 composing。
    [[nodiscard]] TextEditingValue insertText(const std::string& insertion) const;
    // 删除光标前/后的一个 cluster；有选区时先删选区。
    [[nodiscard]] TextEditingValue deleteBackward() const;
    [[nodiscard]] TextEditingValue deleteForward() const;
    // 移动光标（extend=true 扩展选区，即 Shift+方向键）。
    [[nodiscard]] TextEditingValue moveCaretLeft(bool extend) const;
    [[nodiscard]] TextEditingValue moveCaretRight(bool extend) const;
    [[nodiscard]] TextEditingValue moveCaretToStart(bool extend) const;
    [[nodiscard]] TextEditingValue moveCaretToEnd(bool extend) const;
    // 词级移动（Ctrl/Option+方向）。
    [[nodiscard]] TextEditingValue moveWordLeft(bool extend) const;
    [[nodiscard]] TextEditingValue moveWordRight(bool extend) const;
    // 双击选词：选中包含 graphemeIndex 的词。
    [[nodiscard]] TextEditingValue selectWord(std::size_t graphemeIndex) const;
    [[nodiscard]] TextEditingValue selectAll() const;
    // 全删/清空。
    [[nodiscard]] TextEditingValue clear() const;
    // 程序设置文本与光标。
    [[nodiscard]] TextEditingValue replaceAll(const std::string& text,
                                              std::size_t caretGrapheme) const;

    // --- IME 状态机 ---

    // 进入/更新 preedit：替换当前 composing（首次进入时占据选区位置）。
    [[nodiscard]] TextEditingValue compose(const std::string& preedit) const;
    // 提交 composing（用 committed 替换 preedit，光标落在其后）。
    [[nodiscard]] TextEditingValue commitComposition(
        const std::string& committed) const;
    // 取消 composing：preedit 消失，文档与选区回到进入前状态。
    [[nodiscard]] TextEditingValue cancelComposition() const;

    // 夹取 selection/composing 到合法 grapheme 范围。
    void clamp();

    // 恢复编辑状态（交互层从 store 文本重建值时用；夹取后生效）。
    TextEditingValue& restore(TextSelection selection, bool composingActive,
                              TextSelection composing);

  private:
    std::string text_{};
    TextSelection selection_{};
    TextSelection composing_{};
    bool composingActive_{false};
    // preedit 的 grapheme 数（composing 坐标空间的限界）。
    std::size_t composingGraphemes_{0};
    // 进入 composing 前的选区（cancelComposition 恢复）。
    TextSelection savedSelection_{};
};

}  // namespace lumen::text
