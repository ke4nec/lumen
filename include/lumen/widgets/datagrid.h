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
//
// 2026-09-28 对齐增强稿第二批（设计文档 §16）：真实指针修饰键路径
// （Ctrl/Shift 透传）、单元格点击身份（定位列焦点）、提交失败拦截
// （切格/切行/排序/复选框/列显隐与重排失败中止）、编辑器程序化焦点与
// 编辑态 Enter 提交（IME composing 除外）、Tab 提交并移动编辑格（跨行、
// 跳禁用行）、编辑器内点击/双击不打断草稿、排序升→降→清除循环、
// 列 minWidth/显隐/顺序、选择复选框列（表头全选/清空）、数值列右对齐
// + 单行省略、自定义空态。
// 水平虚拟化/双轴滚动协调/RTL 镜像/拖放/列宽拖动手柄/冻结列为后续
// 增量（不破坏本契约）。
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

// 单元格水平对齐（设计文档 §12：数值/日期列 End = 右对齐）。
enum class DataColumnAlign : std::uint8_t { Start, End };

// 列定义（应用装配；width 为固定像素宽，>= minWidth 且 >= 40）。
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
    // 列宽下限（业务列可大于全局 40 下限；resizeColumn 钳制到该值）。
    float minWidth{40.0F};
    // 数值/日期列 End = 单元格文本右对齐（§12）。
    DataColumnAlign align{DataColumnAlign::Start};
    // false 时构建跳过该列（列管理；设计文档 §11.1 列布局状态）。
    bool visible{true};
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
    // 钳制到 [column.minWidth, ∞)（minWidth 自身下限 40）。
    bool resizeColumn(const std::string& columnKey, float width);
    // 当前列宽快照（key → width；含不可见列，供布局持久化）。
    [[nodiscard]] std::vector<std::pair<std::string, float>>
    columnWidths() const;
    // 列显隐（构建跳过不可见列；未知列拒绝）。
    bool setColumnVisible(const std::string& columnKey, bool visible);
    // 列显示顺序（把 columnKey 移到全列向量的 toIndex；钳制到范围内，
    // 未知列拒绝）。列向量的顺序即表头/单元格/TSV 列序。
    bool moveColumn(const std::string& columnKey, std::size_t toIndex);

    // --- 数据装配 ---
    void setRowCount(std::size_t count);
    // 单元格文本（显示与 TSV 复制同源；编辑初值也取自这里）。
    void setCellText(
        std::function<std::string(std::size_t row, const std::string& col)>
            cellText);
    // 行 → stable key（默认 "r<row>"；选择集/焦点/语义身份）。
    void setKeyOf(std::function<std::string(std::size_t)> keyOf);
    // 行禁用查询（不物化屏外行即可回答；禁用行不参与选择/编辑）。
    // 注意：可用行集合有缓存（表头全选态/全选动作用）——enabledness
    // 变化须经本入口或 setRowCount 通知，否则表头勾选显示滞后
    //（全选/行选择/编辑等动作路径始终实时查询）。
    void setRowEnabledOf(std::function<bool(std::size_t)> enabledOf);
    void setEstimatedExtent(float extent);

    // --- 排序/筛选（回调契约；数据重排由应用执行） ---
    // 排序状态由网格维护（表头指示器同源）。点击循环为升序 → 降序 →
    // 清除（§11.2）；清除时回调以空列 key 触发，应用恢复源顺序后重建。
    // 编辑中的草稿先提交，校验失败中止本次排序（§13.1）。
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
    // 文本（state key = owner + ":edit"）。编辑器是树内 text_field；进入
    // 即请求编辑焦点（requestFieldFocus，重建后 focusedBind 生效——文本
    // 输入/IME 直接路由到编辑器，无需先点击）。已有编辑先提交：提交失败
    // 返回 false 并保留旧编辑与草稿（§13.1 失败拦截）；同一格重复进入为
    // no-op 返回 true。
    bool beginEdit(std::size_t row, const std::string& columnKey);
    // 提交：读 state 校验 → 通过则 onCellEdited(row, col, text) 并退出
    // 编辑（焦点回到行节点）；失败保留编辑态（editError() 非空，编辑器
    // 焦点不动）。编辑中 Enter 经 handleKey 走此处（IME composing 期间
    // Enter 留给输入法不触发提交）。
    bool commitEdit();
    void cancelEdit();
    [[nodiscard]] bool editing() const { return editing_.has_value(); }
    [[nodiscard]] std::string editError() const { return editError_; }
    std::function<void(std::size_t row, const std::string& columnKey,
                       const std::string& text)>
        onCellEdited{};

    // --- 组合出口 ---
    // 整网格 Widget：Column[表头行（排序点击/列宽契约见设计文档 §4）,
    // makeList(this)]。表头与每行首列为选择复选框列（宽 44，仅
    // SelectionMode != None；§12 选择辅助列），表头复选框全选/清空当前
    // 可用行。纵向虚拟化复用 List 布局路径；水平方向由应用按需包横向
    // ScrollView（首版契约）。单元格文本单行省略；align=End 的列右对齐。
    [[nodiscard]] core::Widget build() const;

    // 空态内容（数据状态壳由应用组合，§14）：默认 "No rows"；应用可
    // 提供加载骨架/错误重试/无结果等任意子树（listPart 语义由网格补写）。
    void setEmptyBuilder(std::function<core::Widget()> builder);

    // --- shell 接线（构建前一次；ownerKey = build() 根 key） ---
    void attach(app::AppShell& shell, std::string ownerKey);

    // --- 键盘（应用 ShellConfig.onKey 转发；返回 true = 已消费） ---
    // Up/Down/Home/End/PageUp/PageDown/Ctrl+A 与 List 同源；Left/Right
    // 移动列焦点；Enter 进入编辑（或激活；只读列回退 onRowActivated）；
    // Escape 取消编辑；Ctrl+C/Ctrl+V 剪贴板（编辑态不拦截——编辑器自身
    // 消费剪贴板/方向键/文本键；Enter 提交，IME composing 期间除外）。
    // Key 枚举无 F 键，编辑入口契约 = Enter + beginEdit API。
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
    [[nodiscard]] bool columnVisible(const std::string& columnKey) const;
    [[nodiscard]] std::string firstEditableColumn() const;
    [[nodiscard]] std::string firstVisibleColumn() const;
    [[nodiscard]] std::size_t firstVisibleIndex() const;
    [[nodiscard]] std::size_t lastVisibleIndex() const;
    // 视图变化守卫（§13.1）：无编辑或提交成功 = true；失败保留编辑态。
    [[nodiscard]] bool commitPendingEdit();
    // 行/格点击（ctrl/shift 来自指针修饰键；columnKey 定位列焦点）。
    void rowClicked(const std::string& key, bool ctrl, bool shift,
                    const std::string& columnKey = {});
    // 双击/Enter 激活：可编辑列进入编辑，否则回退 onRowActivated。
    // 返回 true = 进入编辑、已中止（提交失败）或触发激活。
    [[nodiscard]] bool activateRow(
        const std::string& rowKey,
        const std::optional<std::string>& columnKey);
    // 编辑态 Tab/Shift+Tab：提交后移动到下一/上一可编辑格（§13.1；跨
    // 行、跳过禁用行、滚动对齐；边界提交后停在当前格）。
    void moveEditor(const std::string& fromRow, const std::string& fromColumn,
                    int direction);
    // 复选框路径：独立切换行选择（不改 current）/ 全选-清空当前可用行。
    void toggleRowSelection(const std::string& key);
    void toggleSelectAll();
    // 当前可用行 key（缓存；数据装配变化时失效，摊销表头全选态的 O(n)）。
    [[nodiscard]] const std::vector<std::string>& selectableKeys() const;
    // "cell:<rowKey>:<colKey>" 引用解析（colKey 末段匹配列集合，行/列
    // key 允许含 ':'）。
    [[nodiscard]] bool parseCellRef(const std::string& ref,
                                    std::string& rowKey,
                                    std::string& columnKey) const;
    // 表头/行首的选择复选框格（44px 整格命中；Checkbox 无 bind，状态由
    // 选择集重建）。
    [[nodiscard]] core::Widget buildHeaderCheckCell() const;
    void requestRebuild();

    core::VirtualListController base_{};
    SelectionModel selection_{};
    std::vector<DataColumn> columns_{};
    std::function<std::string(std::size_t, const std::string&)> cellText_{};
    std::function<std::string(std::size_t)> keyOf_{};
    std::function<bool(std::size_t)> enabledOf_{};
    std::function<core::Widget()> emptyBuilder_{};
    std::unordered_map<std::string, std::function<std::string(
                                        const std::string&)>>
        validators_{};
    // 编辑态（row key + 列 key）；文本经 shell state owner+":edit"。
    std::optional<std::pair<std::string, std::string>> editing_{};
    std::string editError_{};
    std::string sortColumn_{};
    bool sortAscending_{true};
    std::string currentColumn_{};
    // 可用行 key 缓存（表头全选态/全选切换；setRowCount/setKeyOf/
    // setRowEnabledOf 失效）。
    mutable std::vector<std::string> selectableKeysCache_{};
    mutable bool selectableKeysDirty_{true};
    app::AppShell* shell_{nullptr};
    std::string owner_{"grid"};
};

}  // namespace lumen::widgets
