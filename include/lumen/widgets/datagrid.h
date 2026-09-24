#pragma once

// M14-D（docs/lumen-datagrid-design.md）：DataGrid 控制器。
//
// 多列数据网格 = 列模型 + 行虚拟化 + 共享 SelectionModel + 单元格编辑。
// 几何全部组合复用 VirtualList（纵向虚拟化/extent 缓存/锚点稳定）；
// 列与表头是普通 Widget 组合（Row of 固定宽单元），零新增 WidgetType、
// 零布局引擎改动。行 = 集合行（collectionRow 语义/选中/焦点环与
// List 一致）。
//
// 契约切片（首版，见设计文档 §8 已知限制）：多列、列宽调整（运行时
// resizeColumn + 持久化读取 columnWidths）、排序回调（onSortRequest，
// 数据重排由应用执行）、行选择（SelectionModel 四模式）、键盘导航
// （Up/Down/Home/End/PageUp/PageDown/Ctrl+A + Left/Right 列焦点）、
// 复制粘贴（TSV；Ctrl+C/Ctrl+V 经宿主剪贴板）、单元格编辑与校验
// （beginEdit/commitEdit/cancelEdit + setCellValidator；Enter 开始、
// Escape 取消、点击其他单元格提交）。
// 水平虚拟化/双轴滚动协调/RTL 镜像/拖放为后续增量（不破坏本契约）。
//
// UI 线程独占；控制器生命周期必须覆盖 shell（sink 注册于 attach）。

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/widgets/collection.h"
#include "lumen/widgets/selection.h"

namespace lumen::widgets {

// 列定义（应用装配；width 为固定像素宽，>= kMinColumnWidth）。
struct DataColumn {
    std::string key{};
    std::string header{};
    float width{120.0F};
    // 列宽调整入口是否接受该列（表头不画调整手柄的可锁列）。
    bool resizable{true};
    // requestSort 是否允许该列（排序执行在应用侧）。
    bool sortable{false};
    // commitEdit 是否允许写入该列（只读列）。
    bool editable{false};
};

class DataGridController final : public core::VirtualListSource {
  public:
    using ScrollAlignment = ::lumen::widgets::ScrollAlignment;

    // --- 列模型 ---
    void setColumns(std::vector<DataColumn> columns);
    [[nodiscard]] const std::vector<DataColumn>& columns() const {
        return columns_;
    }
    // 运行时列宽（应用持久化回填用）；不可调整列拒绝并返回 false。
    bool resizeColumn(const std::string& columnKey, float width);
    // 当前列宽快照（key → width）。
    [[nodiscard]] std::vector<std::pair<std::string, float>>
    columnWidths() const;

    // --- 数据装配 ---
    void setRowCount(std::size_t count);
    // 单元格文本（显示与 TSV 复制同源；编辑初值也取自这里）。
    void setCellText(
        std::function<std::string(std::size_t row, const std::string& col)>
            cellText);
    // 行 → stable key（默认 "r<row>"；选择集/焦点/语义身份）。
    void setKeyOf(std::function<std::string(std::size_t)> keyOf);
    // 行禁用查询（不物化屏外行即可回答；禁用行不参与选择/编辑）。
    void setRowEnabledOf(std::function<bool(std::size_t)> enabledOf);
    void setEstimatedExtent(float extent);

    // --- 排序/筛选（回调契约；数据重排由应用执行） ---
    // 排序状态由网格维护（表头指示器同源）；requestSort 切换方向并触发
    // 回调。应用重排数据后调用 setRowCount/refresh 重建。
    std::function<void(const std::string& columnKey, bool ascending)>
        onSortRequest{};
    [[nodiscard]] const std::string& sortColumn() const {
        return sortColumn_;
    }
    [[nodiscard]] bool sortAscending() const { return sortAscending_; }
    void requestSort(const std::string& columnKey);
    // 筛选回调契约：应用提供过滤入口（工具栏/表头上下文均可），网格只
    // 约定回调与刷新路径，不内置过滤 UI。
    std::function<void()> onFilterRequest{};

    // --- 选择（共享 SelectionModel；语义同 List §6.5） ---
    void setSelectionMode(SelectionMode mode);
    [[nodiscard]] SelectionModel& selection() { return selection_; }
    [[nodiscard]] const SelectionModel& selection() const {
        return selection_;
    }
    // 行激活（无可编辑列时 Enter 的语义出口）。
    std::function<void(const std::string& rowKey)> onRowActivated{};

    // --- 剪贴板（TSV） ---
    // 选中行按行序拼 TSV 写入宿主剪贴板；返回写入行数（0 = 无剪贴板/
    // 无选中）。列按 columns() 顺序，单元格文本经 cellText（转义 \t\n）。
    std::size_t copySelection();
    // 读宿主剪贴板 TSV → onRowsPasted（行×列，应用校验/应用数据后
    // setRowCount + 重建）；返回是否已分发。
    bool pasteRows();
    std::function<void(const std::vector<std::vector<std::string>>&)>
        onRowsPasted{};

    // --- 单元格编辑 ---
    // 校验器：返回空串 = 通过；否则错误文案（编辑态保留 + editError）。
    void setCellValidator(
        const std::string& columnKey,
        std::function<std::string(const std::string&)> validator);
    // 进入编辑：current 列必须在 editable 列内；编辑器初值 = 当前单元格
    // 文本（state key = owner + ":edit"）。编辑器是树内 text_field，焦点
    // 在字段内——Enter/点击其他单元格提交（见 commitEdit）。
    bool beginEdit(std::size_t row, const std::string& columnKey);
    // 提交：读 state 校验 → 通过则 onCellEdited(row, col, text) 并退出
    // 编辑；失败保留编辑态（editError() 非空）。
    bool commitEdit();
    void cancelEdit();
    [[nodiscard]] bool editing() const { return editing_.has_value(); }
    [[nodiscard]] std::string editError() const { return editError_; }
    std::function<void(std::size_t row, const std::string& columnKey,
                       const std::string& text)>
        onCellEdited{};

    // --- 组合出口 ---
    // 整网格 Widget：Column[表头行（排序点击/列宽契约见设计文档 §4）,
    // makeList(this)]。纵向虚拟化复用 List 布局路径；水平方向由应用按
    // 需包横向 ScrollView（首版契约）。
    [[nodiscard]] core::Widget build() const;

    // --- shell 接线（构建前一次；ownerKey = build() 根 key） ---
    void attach(app::AppShell& shell, std::string ownerKey);

    // --- 键盘（应用 ShellConfig.onKey 转发；返回 true = 已消费） ---
    // Up/Down/Home/End/PageUp/PageDown/Ctrl+A 与 List 同源；Left/Right
    // 移动列焦点；Enter 进入编辑（或激活；Key 枚举无 F 键，编辑入口契
    // 约 = Enter + beginEdit API）；Escape 取消编辑；Ctrl+C/Ctrl+V 剪贴板。
    bool handleKey(core::Key key, core::KeyModifiers modifiers,
                   char keyChar = 0);

    // --- 滚动定位 / current ---
    void scrollToKey(const std::string& key, ScrollAlignment align);
    void setCurrentKey(const std::string& key, bool extend);
    // 当前列焦点（编辑目标列；默认第一列）。
    [[nodiscard]] const std::string& currentColumn() const {
        return currentColumn_;
    }
    void setCurrentColumn(const std::string& columnKey);

    // --- VirtualListSource（几何委托 base_） ---
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
    [[nodiscard]] core::ScrollController* scrollController() const override {
        return base_.scrollController();
    }
    [[nodiscard]] core::ScrollController& scroll() { return base_.scroll(); }
    [[nodiscard]] bool consumeExtentsChanged() {
        return base_.consumeExtentsChanged();
    }
    [[nodiscard]] bool indexOfKey(const std::string& key,
                                  std::size_t& index) const;
    [[nodiscard]] core::Widget buildEmpty() const override;
    [[nodiscard]] std::string tabStopKey() const override;

  private:
    [[nodiscard]] std::string keyOf(std::size_t index) const;
    [[nodiscard]] std::string cellText(std::size_t row,
                                       const std::string& columnKey) const;
    [[nodiscard]] bool rowEnabled(std::size_t index) const;
    [[nodiscard]] bool columnExists(const std::string& columnKey) const;
    [[nodiscard]] std::string firstEditableColumn() const;
    void rowClicked(const std::string& key, bool ctrl, bool shift);
    void requestRebuild();

    core::VirtualListController base_{};
    SelectionModel selection_{};
    std::vector<DataColumn> columns_{};
    std::function<std::string(std::size_t, const std::string&)> cellText_{};
    std::function<std::string(std::size_t)> keyOf_{};
    std::function<bool(std::size_t)> enabledOf_{};
    std::unordered_map<std::string, std::function<std::string(
                                        const std::string&)>>
        validators_{};
    // 编辑态（row key + 列 key）；文本经 shell state owner+":edit"。
    std::optional<std::pair<std::string, std::string>> editing_{};
    std::string editError_{};
    std::string sortColumn_{};
    bool sortAscending_{true};
    std::string currentColumn_{};
    app::AppShell* shell_{nullptr};
    std::string owner_{"grid"};
};

}  // namespace lumen::widgets
