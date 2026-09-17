#pragma once

// 集合控件（docs/lumen-collection-controls-design.md §5）：共享选择模型。
//
// current（焦点行）与 selected（选中集）是两个概念（Qt QItemSelection
// Model / Win32 LVIS_FOCUSED|LVIS_SELECTED 一致语义）：键盘导航移动
// current，选择集独立维护。选择集按 stable key 存储——index 随数据增删
// 漂移，key 是 Element identity 的既有唯一依据。区间选择需要行序，由持
// 有行序列的控制器经 KeySequence 回调提供（模型不假设数据结构）。
// UI 线程独占。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lumen::widgets {

enum class SelectionMode : std::uint8_t {
    None,      // 不可选择（仅激活/浏览）
    Single,    // 单选：单击选中并重置
    Multiple,  // 多选：单击即切换（checkbox 语义）
    Extended,  // 单击重置；Ctrl+单击切换；Shift+单击区间（桌面默认）
};

class SelectionModel {
  public:
    void setMode(SelectionMode mode);
    [[nodiscard]] SelectionMode mode() const { return mode_; }

    // current（焦点行）：独立于选择集；"" = 无 current。
    void setCurrent(const std::string& key);
    [[nodiscard]] const std::string& currentKey() const { return current_; }

    // selected 集：按 stable key。
    void setSelected(std::vector<std::string> keys);  // 批量替换（清空同路）
    bool toggle(const std::string& key);
    [[nodiscard]] bool isSelected(const std::string& key) const;
    [[nodiscard]] const std::vector<std::string>& selectedKeys() const {
        return selected_;
    }
    [[nodiscard]] std::size_t selectedCount() const { return selected_.size(); }
    void clear();

    // 区间回调：控制器注入 [from, to] 闭区间的行序（方向无关）。
    using KeySequence = std::function<std::vector<std::string>(
        const std::string& from, const std::string& to)>;
    void setKeySequence(KeySequence sequence);

    // 修饰键语义（Extended 模式；控制器把指针/键盘事件翻译到这里）。
    void click(const std::string& key, bool ctrl, bool shift);
    void moveTo(const std::string& key, bool extend);  // 键盘移动 current

    // 回调（UI 线程；控制器订阅后驱动重建/语义）。
    std::function<void()> onSelectionChanged{};
    std::function<void(const std::string&)> onCurrentChanged{};

  private:
    void notifySelectionChanged();
    void notifyCurrentChanged();
    void selectRange(const std::string& anchor, const std::string& key);

    SelectionMode mode_{SelectionMode::Extended};
    std::string current_{};
    std::string anchor_{};  // Shift 区间锚（Extended）
    std::vector<std::string> selected_{};
    KeySequence sequence_{};
};

}  // namespace lumen::widgets
