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
//
// 2026-09-28 第三批（设计文档 §17）：双轴几何（表头与数据区共享横向
// offset——根 ScrollView 横轴 + 源视口接缝，滚轮/拖动/惯性/语义滚动由
// 框架直驱，应用零接线）+ 表头列宽拖动手柄（复用 splitter 交互通道：
// 拖动跟手、双击复位、键盘 Left/Right 步进、ResizeEW 悬停光标）+
// Theme.dataGrid token 组（rowExtent/headerExtent/selectionColumnWidth/
// resizeHitWidth 三档密度 + fontScale 派生，Comfortable 档与第二批
// 常量等值）。2026-09-29 第五/六批（§19/§20）：冻结列（DataColumn.
// pinned 前缀不变式 + 区域拆分 + 共享纵向几何 + 语义排除副本）与水平
// 虚拟化（滚动区列窗口物化，复制/粘贴/排序/列宽 API 始终作用于全列
// 集）。RTL 镜像/拖放仍为后续增量。
//
// UI 线程独占；控制器生命周期必须覆盖 shell（sink 注册于 attach）。

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/scroll.h"
#include "lumen/core/splitter.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/widgets/collection.h"
#include "lumen/widgets/selection.h"

namespace lumen::widgets {

// 单元格水平对齐（设计文档 §12：数值/日期列 End = 右对齐）。
enum class DataColumnAlign : std::uint8_t { Start, End };

// 排序键（多列排序 §11.2）：向量序 = 优先级（front 为主排序）；相等值
// 保持源顺序由应用执行（稳定排序）。
struct SortKey {
    std::string columnKey{};
    bool ascending{true};
    bool operator==(const SortKey&) const = default;
};

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
    // 冻结列（§19 第五批）：true 时该列进左冻结区（列向量维持 pinned
    // 前缀不变式——pinned 列恒在非 pinned 列之前）；区域调整经
    // setColumnPinned 显式表达。
    bool pinned{false};
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
    // 列显示顺序（把 columnKey 移到全列向量的 toIndex；钳制到该列所属
    // 区域内——pinned 列在冻结组内移动、非 pinned 在滚动组内移动，跨界
    // 经 setColumnPinned 显式表达；未知列拒绝）。列向量顺序即表头/单元格
    // /TSV 列序。
    bool moveColumn(const std::string& columnKey, std::size_t toIndex);
    // 冻结区调整（§19 第五批）：true = 移入冻结组尾，false = 移回滚动组
    // 首。先提交编辑（§13.1），失败中止；未知列拒绝。
    bool setColumnPinned(const std::string& columnKey, bool pinned);

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
    // 固定行高改造（§19 P0.1）后行高恒按 Theme.dataGrid.rowExtent 推导，
    // 估算值不再被读取——本入口保留兼容但为空操作；行高经主题密度档
    // 调整。
    void setEstimatedExtent(float extent);

    // --- 排序/筛选（回调契约；数据重排由应用执行） ---
    // 排序状态由网格维护（表头指示器同源）。多列排序（§11.2）：向量序
    // = 优先级；普通点击 = 以单列循环 升序 → 降序 → 清除（该列已是唯一
    // 排序列时延续循环，否则收敛为单列升序）；Shift 点击 = 追加/更新该列
    // 为最低优先级，升序 → 降序 → 移除（移除后序号连续）。清除时回调以
    // 空列 key 触发，应用恢复源顺序后重建。
    // 编辑中的草稿先提交，校验失败中止本次排序（§13.1）。
    // onSortRequest 为单列兼容回调（主排序键；既有接线不变）；
    // onSortRequestMulti 携带完整多列状态（§11.2 相等值稳定排序在应用）。
    std::function<void(const std::string& columnKey, bool ascending)>
        onSortRequest{};
    std::function<void(const std::vector<SortKey>&)> onSortRequestMulti{};
    [[nodiscard]] const std::vector<SortKey>& sortKeys() const {
        return sortKeys_;
    }
    [[nodiscard]] const std::string& sortColumn() const {
        static const std::string kNone;
        return sortKeys_.empty() ? kNone : sortKeys_.front().columnKey;
    }
    [[nodiscard]] bool sortAscending() const {
        return sortKeys_.empty() || sortKeys_.front().ascending;
    }
    // extend = Shift 追加语义（指针路径按修饰键透传）。
    void requestSort(const std::string& columnKey, bool extend = false);
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
    // 焦点在列宽手柄上时方向键/Home/End 不消费——留给交互层 splitter
    // 键盘路径（stepBy/stepToEdge，§22.3）。
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
    // 横向滚动控制器（双轴几何，§17：表头与数据区共享；滚轮/拖动/惯性
    // 由框架经源视口接缝直驱，应用无需接线；此处仅供程序化定位）。
    [[nodiscard]] core::ScrollController& hScroll() { return hScroll_; }
    [[nodiscard]] const core::ScrollController& hScroll() const {
        return hScroll_;
    }
    // 程序化把列滚入横向视口（§19 P0.2）：最小移动——只修被破坏的一侧
    // 边界（右缘越界先对齐右缘，左缘越界再对齐左缘）；pinned 列恒可见
    //（no-op）。setCurrentColumn/beginEdit/moveEditor 已接线。
    void ensureColumnVisible(const std::string& columnKey);

    // --- VirtualListSource（纵向几何自持：固定行高，§19 P0.1） ---
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
    // 横向视口的源接缝（§17 双轴几何）：ScrollView 布局/交互对该源只
    // 消费 scrollController() 与 updateViewport()（几何由 ScrollView
    // 自测内容尺寸；item 系列不在该路径被调用，退化实现安全）。
    // updateViewport 喂 hScroll_ 的视口/内容宽并跟踪视口宽度（内容窄于
    // 视口时行背景铺满视口的收敛重建，VirtualList extent 修正同模式）。
    class HorizontalViewportSource final : public core::VirtualListSource {
      public:
        explicit HorizontalViewportSource(DataGridController& owner)
            : owner_(owner) {}

        [[nodiscard]] std::size_t itemCount() const override { return 0; }
        [[nodiscard]] float estimatedExtent() const override { return 0.0F; }
        [[nodiscard]] float extentOf(std::size_t) const override {
            return 0.0F;
        }
        [[nodiscard]] float scrollOffset() const override;
        [[nodiscard]] float totalExtent() const override;
        [[nodiscard]] float offsetOfIndex(std::size_t) const override {
            return 0.0F;
        }
        [[nodiscard]] std::pair<std::size_t, std::size_t> visibleRange(
            float, float) const override {
            return {0, 0};
        }
        [[nodiscard]] core::Widget buildItem(std::size_t) const override;
        void noteExtent(std::size_t, float) const override {}
        void updateViewport(float viewportExtent,
                            float contentPadding) const override;
        [[nodiscard]] core::ScrollController* scrollController()
            const override;

      private:
        DataGridController& owner_;
    };

    // 冻结区行源（§19 第五批 T5.3）：与滚动区 List 共享同一纵向几何
    //（ScrollController/offset/extent 全部委托网格自持的固定行高推导，
    // 两区可见区严格等值）；buildItem 只构建冻结格（key 前缀 frow:，
    // excludeFromSemantics 防语义重复），空态返回空白（空态只在滚动区
    // 呈现）。
    class FrozenRegionSource final : public core::VirtualListSource {
      public:
        explicit FrozenRegionSource(DataGridController& owner)
            : owner_(owner) {}

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
        [[nodiscard]] core::ScrollController* scrollController()
            const override;
        [[nodiscard]] core::Widget buildEmpty() const override;
        [[nodiscard]] std::string tabStopKey() const override;

      private:
        DataGridController& owner_;
    };

    // 列宽手柄源（复用 core::SplitterSource 交互通道，§17）：offset =
    // 该列右缘边界的**区域内容坐标**（冻结列相对冻结区原点、滚动列相对
    // 滚动区原点；§19 P0.3）；dragTo/stepBy 经 resizeColumn 落地（内部钳
    // minWidth），reset 双击复位到 setColumns 时的初始宽。生命周期：按列
    // key 建档、地址稳定（RenderNode/交互层按指针持有；列集刷新复用同
    // 键源）。
    class ColumnResizeSource final : public core::SplitterSource {
      public:
        DataGridController* owner{};
        std::string columnKey{};
        float initialWidth{120.0F};

        [[nodiscard]] float offsetPx() const override;
        [[nodiscard]] float minLeading() const override;
        [[nodiscard]] float minTrailing() const override { return 0.0F; }
        [[nodiscard]] float initialOffset() const override;
        [[nodiscard]] bool seeded() const override { return true; }
        [[nodiscard]] float extentPx() const override;
        void noteLayout(float, float) const override {}
        void dragTo(float offsetPx) const override;
        void stepBy(float deltaPx) const override;
        void stepToEdge(bool maxEdge) const override;
        void reset() const override;
        // 轨道线覆写（§12/设计稿 resizer）：静止透明（默认不加竖线）、
        // 激活 2px（splitter 分隔条默认 1/3px 不适用于网格手柄）。
        [[nodiscard]] float restTrackWidth() const override { return 0.0F; }
        [[nodiscard]] float activeTrackWidth() const override { return 2.0F; }
    };

    [[nodiscard]] std::string keyOf(std::size_t index) const;
    [[nodiscard]] std::string cellText(std::size_t row,
                                       const std::string& columnKey) const;
    [[nodiscard]] bool rowEnabled(std::size_t index) const;
    [[nodiscard]] bool columnExists(const std::string& columnKey) const;
    [[nodiscard]] bool columnVisible(const std::string& columnKey) const;
    // 区域几何（§19 第五批）：pinned 前缀不变式下的分区宽度与列坐标。
    [[nodiscard]] float frozenContentWidth() const;
    [[nodiscard]] float scrollContentWidth() const;
    // 滚动区行宽 = max(滚动内容宽, 滚动视口宽)——内容窄于视口铺满。
    [[nodiscard]] float scrollRegionWidth() const;
    // 列在**所属区域内容坐标**中的左缘（冻结列相对冻结区原点、滚动列
    // 相对滚动区原点；P0.3 区域感知前缀换算）。
    [[nodiscard]] float columnLeftInRegion(const std::string& columnKey) const;
    // 水平虚拟化（§19 第六批）：滚动区列前缀和缓存（非 pinned 可见列，
    // front = 0）+ 可见列窗口 [first, end)（二分 + cache 边距）。窗口只
    // 影响物化——复制/粘贴/排序/列宽 API 始终作用于全列集。
    [[nodiscard]] const std::vector<float>& scrollPrefix() const;
    [[nodiscard]] std::pair<std::size_t, std::size_t> visibleColumnWindow()
        const;
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
    // 表头/行首的选择复选框格（整格命中；Checkbox 无 bind，状态由
    // 选择集重建）。
    [[nodiscard]] core::Widget buildHeaderCheckCell() const;
    // 表头列宽手柄（可调整列右缘；splitter 交互通道，§17）。
    [[nodiscard]] core::Widget buildResizeHandle(
        const DataColumn& column) const;
    // 区域构建（§19 第五批 T5.2）：表头按区域拆分（frozen = 复选框列 +
    // pinned 列；scroll = 其余可见列），数据行同理（buildItem 构建滚动
    // 区行，buildFrozenItem 构建冻结区行——同一逻辑行的两个视图）。
    [[nodiscard]] core::Widget buildRegionHeader(bool frozen) const;
    [[nodiscard]] core::Widget buildFrozenItem(std::size_t index) const;
    // 行首选择复选框格 / 单元格构建（滚动区与冻结区共用；§19 T5.3）。
    [[nodiscard]] core::Widget buildRowCheckCell(const std::string& key) const;
    [[nodiscard]] core::Widget buildCell(std::size_t index,
                                         const std::string& key,
                                         const DataColumn& column) const;
    // 主题几何（Theme.dataGrid；无 shell 时退回 Comfortable 默认）。
    [[nodiscard]] float rowExtentPx() const;
    [[nodiscard]] float headerExtentPx() const;
    [[nodiscard]] float selectionColumnWidthPx() const;
    [[nodiscard]] float resizeHitWidthPx() const;
    // 格内水平内边距（§12：metrics.controlPaddingX 密度档 8/12/16）。
    [[nodiscard]] float cellPaddingXPx() const;
    [[nodiscard]] const DataColumn* findColumn(
        const std::string& columnKey) const;
    // setColumns 同步手柄源（按列 key 建档；既有键复用——地址稳定，
    // 交互层按指针持有，异步 setColumns 不得使拖动中的源失效。移除列
    // 的源保留不销毁：小对象、永不复用，换取零悬垂窗口）。
    void syncColumnSources();
    [[nodiscard]] const ColumnResizeSource* resizeSourceFor(
        const std::string& columnKey) const;
    void requestRebuild();

    core::VirtualListController base_{};
    // 横向滚动（滚动区表头与数据共享，§17；经右区 ScrollView 的源接缝
    // 由框架直驱滚轮/拖动/惯性）。
    core::ScrollController hScroll_{core::ScrollAxis::Horizontal};
    HorizontalViewportSource hSource_{*this};
    // 冻结区行源（与滚动区 List 共享纵向几何，§19 T5.3）。
    FrozenRegionSource frozenSource_{*this};
    // 手柄源按列 key 建档（map 节点地址稳定；键删除不回收，见
    // syncColumnSources 契约）。
    std::map<std::string, ColumnResizeSource> columnSources_{};
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
    // 多列排序状态（front = 主排序；表头指示器/回调同源）。
    std::vector<SortKey> sortKeys_{};
    std::string currentColumn_{};
    // 可用行 key 缓存（表头全选态/全选切换；setRowCount/setKeyOf/
    // setRowEnabledOf 失效）。
    mutable std::vector<std::string> selectableKeysCache_{};
    mutable bool selectableKeysDirty_{true};
    // 横向视口宽跟踪（源接缝回填，§19 区域拆分后 = 滚动区视口宽；
    // 滚动内容窄于视口时滚动区行铺满的收敛依据）。
    mutable float hViewportWidth_{0.0F};
    // 列窗口缓存（§19 T6.1；列模型变化时失效，首帧 hViewportWidth_=0 时
    // 窗口仅 cache 边距内列，两帧收敛——同铺满口径）。
    mutable std::vector<float> scrollPrefix_{};
    mutable std::vector<std::size_t> scrollColumnOrder_{};
    mutable bool scrollPrefixDirty_{true};
    app::AppShell* shell_{nullptr};
    std::string owner_{"grid"};
};

}  // namespace lumen::widgets
