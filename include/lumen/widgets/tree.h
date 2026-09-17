#pragma once

// 集合控件（docs/lumen-collection-controls-design.md §7-8）：Tree 与
// TreeList 控制器。
//
// Tree = 层级模型 + 展开状态集 + 扁平化可见行序列（复用 M3 虚拟化引
// 擎，布局走 layoutVirtualList）。extent 缓存按节点 key 存储（折叠/再
// 展开保留实测高度；index 缓存在树场景会因展开折叠大面积漂移）。
// TreeList = Tree + 列系统（固定/权重列宽、粘性表头、排序钩子——排序
// 由应用执行，控制器只回调 + 记录指示方向）。
//
// 行 = Row 容器（collectionRow=true）；分支行的 chevron 是行内独立
// Ghost Button（点击只 toggle 展开状态，不改变选择）。UI 线程独占。

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/widgets/selection.h"

namespace lumen::widgets {

// 应用拥有的层级数据适配器（惰性契约：hasChildren 可先行返回 true 而
// 不物化子级——千节点目录场景）。
class TreeModel {
  public:
    virtual ~TreeModel() = default;
    // parent 为空串时表示根的子级查询。
    [[nodiscard]] virtual std::size_t childCount(
        const std::string& parent) const = 0;
    [[nodiscard]] virtual std::string childAt(const std::string& parent,
                                              std::size_t index) const = 0;
    [[nodiscard]] virtual bool hasChildren(const std::string& key) const = 0;
    // 行内容（不含缩进与 chevron，由控制器注入）。
    [[nodiscard]] virtual core::Widget buildRow(const std::string& key,
                                                std::size_t depth) const = 0;
};

class TreeController : public core::VirtualListSource {
  public:
    enum class ScrollAlignment : std::uint8_t {
        Visible, Start, Center, End,
    };
    struct VisibleRow {
        std::string key{};
        std::size_t depth{0};
        bool hasChildren{false};
        bool expanded{false};
    };

    // --- 数据 ---
    void setModel(const TreeModel* model);  // 应用拥有生命周期
    void modelChanged();                    // 数据变更 → 失效扁平缓存 + 重建

    // --- 展开状态（按 key 存储；数据移动/重排不丢失） ---
    void expand(const std::string& key);
    void collapse(const std::string& key);
    void toggle(const std::string& key);
    [[nodiscard]] bool isExpanded(const std::string& key) const;
    // 展开全部（防护上限：超过 maxRows 拒绝并返回 false）。
    bool expandAll(std::size_t maxRows = 10000);
    void collapseAll();

    // --- 选择与激活（同 ListController 契约） ---
    void setSelectionMode(SelectionMode mode);
    [[nodiscard]] SelectionModel& selection() { return selection_; }
    // 常量读取（应用/示例在 const build 函数中回显状态）。
    [[nodiscard]] const SelectionModel& selection() const {
        return selection_;
    }
    std::function<void(const std::string& key)> onActivated{};

    // --- shell 接线（ownerKey 与 makeTree/makeTreeList 的 key 一致） ---
    void attach(app::AppShell& shell, std::string ownerKey);

    // --- 滚动定位 ---
    void scrollToKey(const std::string& key, ScrollAlignment align);

    // --- 键盘（应用 ShellConfig.onKey 转发；树契约见设计文档 §7.4） ---
    bool handleKey(core::Key key, core::KeyModifiers modifiers,
                   char keyChar = 0);

    // --- 可见行查询（键盘导航/语义/测试） ---
    [[nodiscard]] const std::vector<VisibleRow>& visibleRows() const {
        return rows_;
    }
    [[nodiscard]] bool rowOfKey(const std::string& key,
                                VisibleRow& row) const;

    // --- VirtualListSource ---
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

    [[nodiscard]] core::ScrollController& scroll() { return scroll_; }
    [[nodiscard]] bool consumeExtentsChanged() {
        const bool changed = extentsChanged_;
        extentsChanged_ = false;
        return changed;
    }
    void setEstimatedExtent(float extent) { estimatedExtent_ = extent; }

  protected:
    // TreeList 覆盖：行内容构建（列盒/单元格）——列模式下不触
    // TreeModel::buildRow（单元格经 cellBuilder 构建）。
    [[nodiscard]] virtual core::Widget buildRowContent(
        const VisibleRow& row) const;
    [[nodiscard]] virtual bool isTreeList() const { return false; }

    [[nodiscard]] std::string rowKeyPrefix() const { return owner_; }
    void requestRebuild();
    void invalidateRows();
    [[nodiscard]] bool indexOfKey(const std::string& key,
                                 std::size_t& index) const;

    // 扁平化（惰性 DFS；expand/collapse/modelChanged 失效重建）。
    void rebuildRows() const;

    // 行构建与交互。
    void rowClicked(const std::string& key, bool ctrl, bool shift);
    void activate(const std::string& key);
    void applyExpansion(const std::string& key, bool expanded);
    void moveCurrent(const std::string& key, bool extend);

    SelectionModel selection_{};
    app::AppShell* shell_{nullptr};
    std::string owner_{"tree"};

  private:
    [[nodiscard]] float extentOfKey(const std::string& key) const;
    void recomputeOffsets() const;

    const TreeModel* model_{nullptr};
    // 展开状态集（按 key）。
    std::vector<std::string> expanded_{};
    // 扁平化行缓存 + 前缀偏移（mutable：source 契约为 const 读）。
    mutable std::vector<VisibleRow> rows_{};
    mutable std::vector<float> prefix_{};
    mutable bool rowsDirty_{true};
    // key → 实测高度（折叠/再展开保留）。
    mutable std::map<std::string, float> measured_{};
    mutable core::ScrollController scroll_{};
    mutable float contentPadding_{0.0F};
    float estimatedExtent_{40.0F};
    mutable bool extentsChanged_{false};
};

// --- TreeList（§8）：Tree + 列系统 ---

// 应用拥有的列声明（生命周期覆盖布局；Widget 经 makeTreeList 的 columns
// 指针携带）。
struct TreeListColumn {
    std::string id{};
    std::string label{};
    float fixedWidth{0.0F};  // >0：固定像素列宽
    float weight{0.0F};      // >0：按权重分配剩余宽
    float minWidth{48.0F};   // 弹性列下限
    bool visible{true};
    bool sortable{false};    // 表头可点击（排序执行在应用）
};

class TreeListController final : public TreeController {
  public:
    // 单元格构建：列内容与行内容解耦（返回 Widget 由控制器放入列盒）。
    void setColumns(std::vector<TreeListColumn> columns);
    void setCellBuilder(std::function<core::Widget(
        const std::string& rowKey, const std::string& columnId)> builder);
    // 表头点击（sortable 列）：应用执行排序；控制器记录指示方向。
    std::function<void(const std::string& columnId, bool descending)>
        onHeaderClick{};
    void setSortIndicator(std::string columnId, bool descending);

    // shell 接线（同 TreeController；追加表头排序 sink）。
    void attach(app::AppShell& shell, std::string ownerKey);

    [[nodiscard]] const std::vector<TreeListColumn>& columns() const {
        return columns_;
    }
    [[nodiscard]] const std::vector<float>& columnWidths() const {
        return widths_;
    }

  protected:
    [[nodiscard]] core::Widget buildRowContent(
        const VisibleRow& row) const override;
    [[nodiscard]] bool isTreeList() const override { return true; }
    // VirtualListSource 扩展：表头 + 内容宽回填。
    [[nodiscard]] core::Widget buildHeader() const override;
    void noteContentWidth(float width) const override;

  private:
    void recomputeWidths() const;

    std::vector<TreeListColumn> columns_{};
    mutable std::vector<float> widths_{};
    std::function<core::Widget(const std::string&, const std::string&)>
        cellBuilder_{};
    mutable float contentWidth_{0.0F};
    std::string sortColumn_{};
    bool sortDescending_{false};
};

}  // namespace lumen::widgets
