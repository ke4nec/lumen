#include "lumen/text/editing_value.h"

#include <algorithm>
#include <utility>

namespace lumen::text {

void TextEditingValue::clamp() {
    // composing 期间 selection/composing 处于“文档 + preedit”坐标系；
    // 限界 = 文档 grapheme 数 + preedit grapheme 数。
    const std::size_t count = graphemeCount(text_);
    const std::size_t limit = composingActive_ ? count + composingGraphemes_
                                               : count;
    selection_.base = std::min(selection_.base, limit);
    selection_.extent = std::min(selection_.extent, limit);
    composing_.base = std::min(composing_.base, limit);
    composing_.extent = std::min(composing_.extent, limit);
    savedSelection_.base = std::min(savedSelection_.base, count);
    savedSelection_.extent = std::min(savedSelection_.extent, count);
}

TextEditingValue& TextEditingValue::restore(TextSelection selection,
                                            bool composingActive,
                                            TextSelection composing) {
    selection_ = selection;
    composing_ = composing;
    composingActive_ = composingActive;
    clamp();
    return *this;
}

TextEditingValue TextEditingValue::insertText(
    const std::string& insertion) const {
    TextEditingValue next;
    const std::size_t start = selection_.start();
    const std::size_t end = selection_.end();
    std::string text = graphemeSubstring(text_, 0, start) + insertion +
                       graphemeSubstring(text_, end, graphemeCount(text_));
    const std::size_t insertionGraphemes = graphemeCount(insertion);
    next.text_ = std::move(text);
    next.selection_ = TextSelection{start + insertionGraphemes,
                                    start + insertionGraphemes};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::deleteBackward() const {
    if (hasSelection()) {
        return insertText("");
    }
    if (selection_.extent == 0) {
        return *this;
    }
    TextEditingValue next;
    const std::size_t caret = selection_.extent;
    next.text_ = graphemeSubstring(text_, 0, caret - 1) +
                 graphemeSubstring(text_, caret, graphemeCount(text_));
    next.selection_ = TextSelection{caret - 1, caret - 1};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::deleteForward() const {
    if (hasSelection()) {
        return insertText("");
    }
    if (selection_.extent >= graphemeCount(text_)) {
        return *this;
    }
    TextEditingValue next;
    const std::size_t caret = selection_.extent;
    next.text_ = graphemeSubstring(text_, 0, caret) +
                 graphemeSubstring(text_, caret + 1, graphemeCount(text_));
    next.selection_ = TextSelection{caret, caret};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::moveCaretLeft(bool extend) const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    if (!extend && next.hasSelection()) {
        // 折叠到选区起点。
        next.selection_ =
            TextSelection{selection_.start(), selection_.start()};
        return next;
    }
    const std::size_t target =
        selection_.extent > 0 ? selection_.extent - 1 : 0;
    next.selection_ =
        extend ? TextSelection{selection_.base, target}
               : TextSelection{target, target};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::moveCaretRight(bool extend) const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    const std::size_t count = graphemeCount(text_);
    if (!extend && next.hasSelection()) {
        next.selection_ = TextSelection{selection_.end(), selection_.end()};
        return next;
    }
    const std::size_t target =
        selection_.extent < count ? selection_.extent + 1 : count;
    next.selection_ =
        extend ? TextSelection{selection_.base, target}
               : TextSelection{target, target};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::moveCaretToStart(bool extend) const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    next.selection_ = extend ? TextSelection{selection_.base, 0}
                             : TextSelection{0, 0};
    return next;
}

TextEditingValue TextEditingValue::moveCaretToEnd(bool extend) const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    const std::size_t count = graphemeCount(text_);
    next.selection_ = extend ? TextSelection{selection_.base, count}
                             : TextSelection{count, count};
    return next;
}

TextEditingValue TextEditingValue::moveWordLeft(bool extend) const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    next.composingGraphemes_ = 0;
    std::size_t pos = selection_.extent;
    // 先跳过左侧紧邻的空白，再取词起点。
    while (pos > 0) {
        const std::string g = graphemeSubstring(text_, pos - 1, pos);
        if (g != " " && g != "\t" && g != "\n") {
            break;
        }
        --pos;
    }
    const std::size_t target =
        pos > 0 ? wordRangeContaining(text_, pos - 1, nullptr) : 0;
    next.selection_ = extend ? TextSelection{selection_.base, target}
                             : TextSelection{target, target};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::moveWordRight(bool extend) const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    const std::size_t count = graphemeCount(text_);
    const std::size_t pos = selection_.extent;
    // 光标右侧 cluster 所在词的终点即目标；行尾夹取。
    std::size_t target = pos;
    if (pos < count) {
        std::size_t end = pos;
        (void)wordRangeContaining(text_, pos, &end);
        target = std::max(end, pos + 1);
    } else {
        target = count;
    }
    next.selection_ = extend ? TextSelection{selection_.base, target}
                             : TextSelection{target, target};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::selectWord(
    std::size_t graphemeIndex) const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    std::size_t end = graphemeIndex;
    const std::size_t begin = wordRangeContaining(text_, graphemeIndex, &end);
    next.selection_ = TextSelection{begin, end};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::selectAll() const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    next.selection_ = TextSelection{0, graphemeCount(text_)};
    return next;
}

TextEditingValue TextEditingValue::clear() const {
    TextEditingValue next;
    return next;
}

TextEditingValue TextEditingValue::replaceAll(const std::string& text,
                                              std::size_t caretGrapheme) const {
    TextEditingValue next{text};
    next.selection_ = TextSelection{caretGrapheme, caretGrapheme};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::compose(const std::string& preedit) const {
    TextEditingValue next = *this;
    const std::size_t preeditGraphemes = graphemeCount(preedit);
    if (!composingActive_) {
        // 首次进入：preedit 占据当前选区位置，记录进入前选区。
        next.savedSelection_ = selection_;
        const std::size_t at = selection_.start();
        next.composing_ = TextSelection{at, at + preeditGraphemes};
        next.selection_ = next.composing_;
    }
    next.composingActive_ = !preedit.empty();
    next.composingGraphemes_ = preeditGraphemes;
    if (!next.composingActive_) {
        next.composing_ = {};
        next.composingGraphemes_ = 0;
        next.selection_ = savedSelection_;
    } else {
        // preedit 长度变化时同步 composing 区间与光标。
        const std::size_t at = next.composing_.base;
        next.composing_ = TextSelection{at, at + preeditGraphemes};
        next.selection_ =
            TextSelection{at + preeditGraphemes, at + preeditGraphemes};
    }
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::commitComposition(
    const std::string& committed) const {
    if (!composingActive_) {
        return insertText(committed);
    }
    // 用 committed 替换 composing 区间。
    TextEditingValue next;
    const std::size_t start = composing_.base;
    const std::size_t end = composing_.end();
    next.text_ = graphemeSubstring(text_, 0, start) + committed +
                 graphemeSubstring(text_, end, graphemeCount(text_));
    const std::size_t caret = start + graphemeCount(committed);
    next.selection_ = TextSelection{caret, caret};
    next.clamp();
    return next;
}

TextEditingValue TextEditingValue::cancelComposition() const {
    TextEditingValue next = *this;
    next.composingActive_ = false;
    next.composing_ = {};
    next.selection_ = savedSelection_;
    next.clamp();
    return next;
}

}  // namespace lumen::text
