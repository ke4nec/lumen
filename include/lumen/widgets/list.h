#pragma once

// 集合控件（docs/lumen-collection-controls-design.md §6）：List 控制器。
//
// 一维行序列 + 共享 SelectionModel + 激活语义。几何（extent 缓存/锚点
// 稳定/滚动）全部组合复用 VirtualListController（M3）；本控制器只叠加
// 列表语义层：行包装（focusable 集合行 + onClick 注册）、选择语义、
// 激活（双击/Enter）、键盘导航与 scrollToKey 四种对齐。
//
// 行 = Row 容器（collectionRow=true）：hover/pressed/选中/焦点环由
// StyleResolver 与 painter 的集合行路径驱动（Tabs/Dropdown 等既有
// selected 语义不受影响）。应用经 attach 接线后把 makeList(this) 放入
// 树；行 handler 在 buildItem 时按需注册（幂等，O(visible)）。
// UI 线程独占。

#include <cstddef>
#include <functional>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/widgets/selection.h"

namespace lumen::widgets {

class ListController final : public core::VirtualListSource {
  public:
    enum class ScrollAlignment : std::uint8_t {
        Visible,  // 最小移动使行可见（默认）
        Start,    // 行顶对齐视口顶
        Center,   // 行居中
        End,      // 行底对齐视口底
    };

    // --- 数据装配（UI 线程） ---
    void setItemCount(std::size_t count);
    // 行内容构建器（返回任意 Widget；控制器负责包装为集合行）。
    void setItemBuilder(std::function<core::Widget(std::size_t)> builder);
    // index → stable key（默认 "i<index>"；key 是选择集/焦点/语义身份）。
    void setKeyOf(std::function<std::string(std::size_t)> keyOf);
    // 未测量项的估算行高（默认 40 = 视觉系统 Medium 档）。
    void setEstimatedExtent(float extent);
    // 空态内容（itemCount()==0 时；默认居中 "Empty" 文本）。
    void setEmptyBuilder(std::function<core::Widget()> builder);
    // --- 选择与激活 ---
    void setSelectionMode(SelectionMode mode);
    [[nodiscard]] SelectionModel& selection() { return selection_; }
    // 常量读取（应用/示例在 const build 函数中回显状态）。
    [[nodiscard]] const SelectionModel& selection() const {
        return selection_;
    }
    // 激活（双击 / Enter / 语义 Activate 同路径）。
    std::function<void(const std::string& key)> onActivated{};

    // --- shell 接线 ---
    // 应用持有控制器并在构建前调用一次；ownerKey 必须与 makeList 的
    // key 一致（行 key/handler 名按它命名空间隔离）。控制器生命周期
    // 必须覆盖 shell（激活 sink 经 addRowActivateSink 注册）。
    void attach(app::AppShell& shell, std::string ownerKey);

    // --- 滚动定位 ---
    void scrollToKey(const std::string& key, ScrollAlignment align);
    // 以行 key 移动 current 并按模式随动选择。
    void setCurrentKey(const std::string& key, bool extend);

    // --- 键盘（应用 ShellConfig.onKey 转发；返回 true = 已消费） ---
    bool handleKey(core::Key key, core::KeyModifiers modifiers,
                   char keyChar = 0);

    // --- VirtualListSource（几何委托 base_；行为见 virtual_list.h） ---
    [[nodiscard]] std::size_t itemCount() const override;
    [[nodiscard]] float estimatedExtent() const override;
    [[nodiscard]] float extentOf(std::size_t index) const override;
    [[nodiscard]] float scrollOffset() const override;
    [[nodiscard]] float totalExtent() const override;
    [[nodiscard]] float offsetOfIndex(std::size_t index) const override;
    [[nodiscard]] std::pair<std::size_t, std::size_t> visibleRange(
        float viewportExtent, float cacheExtent) const override;
    [[nodiscard]] core::Widget buildItem(std::size_t index) const override;
    void noteExtent(std::size_t index, float extent) const override;
    void updateViewport(float viewportExtent,
                        float contentPadding) const override;

    // 滚动输入（与 ScrollView 统一路径）。
    [[nodiscard]] core::ScrollController& scroll() { return base_.scroll(); }
    // 布局期回填是否引起内容高度变化（应用据此请求一次重建；消费即清）。
    [[nodiscard]] bool consumeExtentsChanged() {
        return base_.consumeExtentsChanged();
    }

    // 行 key → index（不可见返回 false）。
    [[nodiscard]] bool indexOfKey(const std::string& key,
                                 std::size_t& index) const;
    // 空态 Widget（itemCount()==0 时布局不进 VirtualList 物化路径，
    // 由应用在 makeList 外自行呈现；此处供应用便捷取用）。
    [[nodiscard]] core::Widget buildEmpty() const;

  private:
    [[nodiscard]] std::string keyOf(std::size_t index) const;
    void rowClicked(const std::string& key, bool ctrl, bool shift);
    void activate(const std::string& key);
    void requestRebuild();

    core::VirtualListController base_{};
    SelectionModel selection_{};
    std::function<core::Widget(std::size_t)> itemBuilder_{};
    std::function<std::string(std::size_t)> keyOf_{};
    std::function<core::Widget()> emptyBuilder_{};
    app::AppShell* shell_{nullptr};
    std::string owner_{"list"};
};

}  // namespace lumen::widgets
