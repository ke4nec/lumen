// M14-D：DataGridController 实现（契约见 docs/lumen-datagrid-design.md）。
//
// 2026-09-28 第二批（设计文档 §16）：指针修饰键经 pointerModifiers 透传
// （dispatchRowClick 同源）；单元格携带点击身份（定位列焦点 + 行选择，
// colKey 末段解析——行/列 key 允许含 ':'）；提交失败拦截（切格/切行/
// 排序/复选框/激活先提交，失败中止且不覆盖草稿）；编辑器程序化焦点
// （requestFieldFocus，重建后 focusedBind 生效）+ 编辑态 Enter 提交
// （IME composing 除外）+ 提交/取消后焦点回行；排序升 → 降 → 清除循环；
// 列 minWidth/显隐/顺序；选择复选框列（表头全选/清空当前可用行，
// selectableKeys 缓存摊销 O(n)）；数值列右对齐 + 单行省略；自定义空态。
//
// 2026-09-28 第三批（设计文档 §17）：双轴几何——根 ScrollView 横轴 +
// HorizontalViewportSource 源接缝（布局喂 hScroll_ 视口/内容宽；滚轮/
// 拖动/惯性/滚动条由交互层直驱，应用零接线），表头与数据区共享横向
// offset；表头列宽拖动手柄复用 splitter 交互通道（ColumnResizeSource：
// 拖动跟手/双击复位/键盘步进/ResizeEW 光标）；几何取 Theme.dataGrid
// token（Comfortable 档与第二批常量等值）。

#include "lumen/widgets/datagrid.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "lumen/accessibility/semantics.h"
#include "collection_common.h"

namespace lumen::widgets {
namespace {
constexpr float kMinColumnWidth = 40.0F;

std::string escapeTsvCell(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\t' || c == '\n' || c == '\r') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// 单行省略的单元格文本样式（§12：长内容单行省略）。颜色不设——数据格
// 文字继承集合行前景 token（materializeVirtualRows 的行前景继承）。
core::TextStyle cellTextStyle() {
    core::TextStyle style;
    style.maxLines = 1;
    style.overflow = core::TextOverflow::Ellipsis;
    return style;
}

// 表头文字样式（§12/设计稿 th）：caption 档 + contentSecondary——静态
// 列头与可排序列头同色（token 派生，随主题/高对比），单行省略。
core::TextStyle headerTextStyle(const style::Theme& theme) {
    core::TextStyle style = theme.typography.caption;
    style.color = theme.colors.contentSecondary;
    style.maxLines = 1;
    style.overflow = core::TextOverflow::Ellipsis;
    return style;
}
}  // namespace

// --- 列模型 ---

void DataGridController::setColumns(std::vector<DataColumn> columns) {
    // 列集替换先落编辑态（§13.1 视图变化先提交）：提交成功自然关闭；
    // 校验失败不阻断数据重置，但草稿随失效列一并取消——否则换入不含
    // 编辑列的列集后编辑器不再物化、focusedBind 悬空（后续键入丢失），
    // 之后 commitEdit 还会向应用发出带已删列 key 的 onCellEdited
    //（行侧 indexOfKey 兜底同模式，review：setColumns 缺守卫）。
    if (!commitPendingEdit()) {
        cancelEdit();
    }
    columns_ = std::move(columns);
    for (auto& column : columns_) {
        // 全局下限 40；业务列 minWidth 只能更高（§11.1）。
        column.minWidth = std::max(column.minWidth, kMinColumnWidth);
        column.width = std::max(column.width, column.minWidth);
    }
    syncColumnSources();
    scrollPrefixDirty_ = true;
    if (!columnVisible(currentColumn_)) {
        currentColumn_ = firstVisibleColumn();
    }
    // 列集移除的排序键随之失效（序号自然连续）。
    std::erase_if(sortKeys_, [this](const SortKey& key) {
        return !columnExists(key.columnKey);
    });
    requestRebuild();
}

bool DataGridController::resizeColumn(const std::string& columnKey,
                                      float width) {
    for (auto& column : columns_) {
        if (column.key != columnKey) {
            continue;
        }
        if (!column.resizable) {
            return false;
        }
        // 视图变化先提交（§13.1 切列先提交；手柄拖动逐拍调用，无编辑时
        // 幂等直达）。校验失败中止——列宽不动，草稿留在错误格。
        if (!commitPendingEdit()) {
            return false;
        }
        // 钳制到列 minWidth（自身 >= 40）。
        column.width = std::max(width, column.minWidth);
        scrollPrefixDirty_ = true;
        requestRebuild();
        return true;
    }
    return false;
}

std::vector<std::pair<std::string, float>>
DataGridController::columnWidths() const {
    std::vector<std::pair<std::string, float>> out;
    out.reserve(columns_.size());
    for (const auto& column : columns_) {
        out.emplace_back(column.key, column.width);
    }
    return out;
}

bool DataGridController::setColumnVisible(const std::string& columnKey,
                                          bool visible) {
    for (auto& column : columns_) {
        if (column.key != columnKey) {
            continue;
        }
        if (column.visible == visible) {
            return true;
        }
        // 视图变化先提交（§13.1 切列先提交；失败中止显隐变更）。
        if (!commitPendingEdit()) {
            return false;
        }
        column.visible = visible;
        scrollPrefixDirty_ = true;
        if (!visible && currentColumn_ == columnKey) {
            // 当前列被隐藏：列焦点回退首个可见列。
            currentColumn_ = firstVisibleColumn();
        }
        requestRebuild();
        return true;
    }
    return false;
}

bool DataGridController::moveColumn(const std::string& columnKey,
                                    std::size_t toIndex) {
    std::size_t from = 0;
    bool found = false;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].key == columnKey) {
            from = i;
            found = true;
            break;
        }
    }
    if (!found || columns_.empty()) {
        return false;
    }
    // 组内移动（§19 T5.1 前缀不变式）：pinned 列只在冻结组内移动、非
    // pinned 只在滚动组内移动；跨界经 setColumnPinned 显式表达。
    const bool pinned = columns_[from].pinned;
    std::size_t groupBegin = 0;
    while (groupBegin < columns_.size() &&
           columns_[groupBegin].pinned != pinned) {
        ++groupBegin;
    }
    std::size_t groupEnd = groupBegin;
    while (groupEnd < columns_.size() &&
           columns_[groupEnd].pinned == pinned) {
        ++groupEnd;
    }
    toIndex = std::clamp(toIndex, groupBegin, groupEnd - 1);
    if (from == toIndex) {
        return true;
    }
    // 视图变化先提交（§13.1 切列先提交；失败中止重排）。
    if (!commitPendingEdit()) {
        return false;
    }
    DataColumn moved = columns_[from];
    columns_.erase(columns_.begin() + static_cast<std::ptrdiff_t>(from));
    columns_.insert(columns_.begin() + static_cast<std::ptrdiff_t>(toIndex),
                    std::move(moved));
    scrollPrefixDirty_ = true;
    requestRebuild();
    return true;
}

bool DataGridController::setColumnPinned(const std::string& columnKey,
                                         bool pinned) {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].key != columnKey) {
            continue;
        }
        if (columns_[i].pinned == pinned) {
            return true;
        }
        // 区域调整先提交编辑（§13.1；失败中止）。
        if (!commitPendingEdit()) {
            return false;
        }
        DataColumn moved = columns_[i];
        moved.pinned = pinned;
        columns_.erase(columns_.begin() + static_cast<std::ptrdiff_t>(i));
        // 前缀不变式（§19 T5.1/§20.2）：pin = 移到冻结组尾；unpin = 移到
        // 滚动组首。两分支落点是同一位置——"首个非 pinned 位"既是冻结
        // 组尾也是滚动组首（取消冻结的列出现在分界线右侧第一位，不是
        // 全列末尾）；全 pinned/空表时 at=size/0，仍满足不变式。
        std::size_t at = 0;
        while (at < columns_.size() && columns_[at].pinned) {
            ++at;
        }
        columns_.insert(columns_.begin() + static_cast<std::ptrdiff_t>(at),
                        std::move(moved));
        scrollPrefixDirty_ = true;
        requestRebuild();
        return true;
    }
    return false;
}

// --- 数据装配 ---

void DataGridController::setRowCount(std::size_t count) {
    base_.setItemCount(count);
    selectableKeysDirty_ = true;
    requestRebuild();
}

void DataGridController::setCellText(
    std::function<std::string(std::size_t, const std::string&)> cellText) {
    cellText_ = std::move(cellText);
    requestRebuild();
}

void DataGridController::setKeyOf(
    std::function<std::string(std::size_t)> keyOf) {
    keyOf_ = std::move(keyOf);
    selectableKeysDirty_ = true;
    requestRebuild();
}

void DataGridController::setRowEnabledOf(
    std::function<bool(std::size_t)> enabledOf) {
    enabledOf_ = std::move(enabledOf);
    selectableKeysDirty_ = true;
    requestRebuild();
}

void DataGridController::setEstimatedExtent(float extent) {
    base_.setEstimatedExtent(extent);
}

// --- 排序 ---

void DataGridController::requestSort(const std::string& columnKey,
                                     bool extend) {
    if (!columnExists(columnKey)) {
        return;
    }
    for (const auto& column : columns_) {
        if (column.key == columnKey && !column.sortable) {
            return;
        }
    }
    // 视图变化先提交编辑（§13.1）；校验失败中止本次排序。
    if (!commitPendingEdit()) {
        return;
    }
    // §11.2 多列循环：extend（Shift）= 追加/更新该列为最低优先级，
    // 升序 → 降序 → 移除；普通点击 = 单列循环（该列已是唯一排序列时
    // 延续，否则收敛为单列升序）——单列路径与既有契约逐位一致。
    const auto at = std::find_if(
        sortKeys_.begin(), sortKeys_.end(),
        [&](const SortKey& key) { return key.columnKey == columnKey; });
    if (extend) {
        if (at == sortKeys_.end()) {
            sortKeys_.push_back(SortKey{columnKey, true});
        } else if (at->ascending) {
            at->ascending = false;
        } else {
            sortKeys_.erase(at);
        }
    } else if (at != sortKeys_.end() && sortKeys_.size() == 1) {
        if (at->ascending) {
            at->ascending = false;
        } else {
            sortKeys_.clear();
        }
    } else {
        sortKeys_.clear();
        sortKeys_.push_back(SortKey{columnKey, true});
    }
    if (onSortRequest) {
        onSortRequest(sortColumn(), sortAscending());
    }
    if (onSortRequestMulti) {
        onSortRequestMulti(sortKeys_);
    }
    requestRebuild();
}

// --- 选择 ---

void DataGridController::setSelectionMode(SelectionMode mode) {
    selection_.setMode(mode);
}

// --- 剪贴板（TSV） ---

std::size_t DataGridController::copySelection() {
    if (shell_ == nullptr ||
        shell_->controller().clipboard() == nullptr) {
        return 0;
    }
    const auto& selected = selection_.selectedKeys();
    if (selected.empty()) {
        return 0;
    }
    // 按行序输出（选择集无序；与表头列序一致——含不可见列，首版语义
    // 保留；仅可见列的范围变体是显式新 API，§11.3）。
    std::vector<std::size_t> rows;
    for (std::size_t i = 0; i < base_.itemCount(); ++i) {
        if (selection_.isSelected(keyOf(i))) {
            rows.push_back(i);
        }
    }
    std::ostringstream out;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        if (r != 0) out << '\n';
        for (std::size_t c = 0; c < columns_.size(); ++c) {
            if (c != 0) out << '\t';
            out << escapeTsvCell(cellText(rows[r], columns_[c].key));
        }
    }
    if (!shell_->controller().clipboard()->setText(out.str())) {
        return 0;
    }
    return rows.size();
}

bool DataGridController::pasteRows() {
    if (shell_ == nullptr || shell_->controller().clipboard() == nullptr ||
        onRowsPasted == nullptr) {
        return false;
    }
    if (!commitPendingEdit()) {
        return false;
    }
    auto* clipboard = shell_->controller().clipboard();
    if (!clipboard->hasText()) {
        return false;
    }
    std::vector<std::vector<std::string>> rows;
    std::istringstream input(clipboard->text());
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> cells;
        std::istringstream cellsIn(line);
        std::string cell;
        while (std::getline(cellsIn, cell, '\t')) {
            cells.push_back(cell);
        }
        rows.push_back(std::move(cells));
    }
    if (rows.empty()) {
        return false;
    }
    onRowsPasted(rows);
    requestRebuild();
    return true;
}

// --- 单元格编辑 ---

void DataGridController::setCellValidator(
    const std::string& columnKey,
    std::function<std::string(const std::string&)> validator) {
    if (validator == nullptr) {
        validators_.erase(columnKey);
    } else {
        validators_[columnKey] = std::move(validator);
    }
}

bool DataGridController::beginEdit(std::size_t row,
                                   const std::string& columnKey) {
    if (shell_ == nullptr || row >= base_.itemCount() ||
        !rowEnabled(row) || !columnExists(columnKey)) {
        return false;
    }
    const DataColumn* column = nullptr;
    for (const auto& candidate : columns_) {
        if (candidate.key == columnKey) {
            column = &candidate;
            break;
        }
    }
    // 只读列与不可见列（无格可编辑）都拒绝。
    if (column == nullptr || !column->editable || !column->visible) {
        return false;
    }
    const std::string rowKey = keyOf(row);
    if (editing_) {
        // 同一格重复进入 = no-op；不同格先提交，失败保留旧编辑与草稿
        //（§13.1 失败拦截：不得覆盖无效输入）。
        if (editing_->first == rowKey && editing_->second == columnKey) {
            return true;
        }
        if (!commitEdit()) {
            return false;
        }
    }
    editing_ = std::make_pair(rowKey, columnKey);
    editError_.clear();
    shell_->state().set(owner_ + ":edit", cellText(row, columnKey));
    // 编辑目标列滚入横向视口（§19 P0.2：编辑器物化是 requestFieldFocus
    // 落地与可见性的前提）。
    ensureColumnVisible(columnKey);
    // 焦点意图（环）+ 程序化编辑焦点（重建后 focusedBind 建立——文本
    // 输入/IME 直接路由到编辑器，无需先点击）。
    shell_->focus().setFocus(owner_ + ":editor");
    shell_->controller().requestFieldFocus(owner_ + ":edit");
    requestRebuild();
    return true;
}

bool DataGridController::commitEdit() {
    if (!editing_ || shell_ == nullptr) {
        return false;
    }
    const auto [rowKey, columnKey] = *editing_;
    std::size_t row = 0;
    if (!indexOfKey(rowKey, row)) {
        cancelEdit();
        return false;
    }
    const std::string text = shell_->state().get(owner_ + ":edit");
    if (const auto it = validators_.find(columnKey); it != validators_.end()) {
        const std::string error = it->second(text);
        if (!error.empty()) {
            // 失败保留编辑态与草稿；编辑器保持编辑焦点（指针路径的
            // pointerDown 已清焦——重新请求；错误浮层为后续增量，当前
            // placeholder 同源显示 editError）。
            editError_ = error;
            shell_->focus().setFocus(owner_ + ":editor");
            shell_->controller().requestFieldFocus(owner_ + ":edit");
            requestRebuild();
            return false;
        }
    }
    editError_.clear();
    editing_.reset();
    // 提交后释放编辑焦点（focusedBind 不再指向已卸载的编辑器），
    // 焦点回行节点。
    shell_->controller().releaseFieldFocus();
    if (onCellEdited) {
        onCellEdited(row, columnKey, text);
    }
    shell_->focus().setFocus(owner_ + ":item:" + rowKey);
    requestRebuild();
    return true;
}

void DataGridController::cancelEdit() {
    if (!editing_) {
        return;
    }
    const std::string rowKey = editing_->first;
    editing_.reset();
    editError_.clear();
    if (shell_ != nullptr) {
        shell_->controller().releaseFieldFocus();
        shell_->focus().setFocus(owner_ + ":item:" + rowKey);
    }
    requestRebuild();
}

// --- 组合出口 ---

void DataGridController::setEmptyBuilder(
    std::function<core::Widget()> builder) {
    emptyBuilder_ = std::move(builder);
    requestRebuild();
}

core::Widget DataGridController::buildRegionHeader(bool frozen) const {
    const float headerExtent = headerExtentPx();
    const style::Theme& theme =
        shell_ != nullptr ? shell_->theme() : style::Theme::dark();
    const core::TextStyle headerText = headerTextStyle(theme);
    std::vector<core::Widget> headerCells;
    if (frozen && selection_.mode() != SelectionMode::None) {
        headerCells.push_back(buildHeaderCheckCell());
    }
    // 滚动区表头窗口物化（§19 T6.2）：只构建可见列窗口 + 首列前缀偏移
    //（padding.left），格 x 坐标稳定不变式保持；冻结区不窗口化。
    std::size_t windowFirst = 0;
    std::size_t windowEnd = 0;
    if (!frozen) {
        std::tie(windowFirst, windowEnd) = visibleColumnWindow();
    }
    std::size_t scrollAt = 0;  // 滚动区列序（与 scrollPrefix 对齐）
    for (const auto& column : columns_) {
        if (!column.visible || column.pinned != frozen) {
            continue;
        }
        if (!frozen) {
            // 窗口序号 = 滚动区列序，物化与否都推进——非可调整列不推进
            // 会让后续列的窗口判定错位（review 复测发现）。
            const std::size_t at = scrollAt++;
            if (at < windowFirst || at >= windowEnd) {
                continue;
            }
        }
        // 表头（§12/设计稿 th）：文字统一 caption 档 + contentSecondary
        //（静态列头与可排序列头同色；可排序列 = Ghost 按钮，默认
        // accentContent 前景留给排序指示图标——文字经 styleOverrides.text
        // 覆写）。可调整列右缘叠放列宽手柄（§17 splitter 通道）：Stack
        // TopRight 叠放，文字用满列宽——手柄带宽不再从文字预算扣走
        //（设计稿 resizer 骑缝叠放口径）。
        const bool hasHandle = column.resizable;
        core::Widget cell;
        const auto sortAt = std::find_if(
            sortKeys_.begin(), sortKeys_.end(),
            [&](const SortKey& key) { return key.columnKey == column.key; });
        const bool sorted = sortAt != sortKeys_.end();
        if (column.sortable) {
            cell = core::makeButton(column.header);
            cell.buttonVariant = core::ButtonVariant::Ghost;
            cell.onClick = "grid:" + owner_ + ":sort:" + column.key;
            cell.key = owner_ + ":head:" + column.key;
            cell.alignContentStart = true;
            cell.styleOverrides.text = headerText;
            if (sorted) {
                // 多列优先级数字标记待 Button 内容通道扩展（§18 已知
                // 限制）；方向 chevron 与单列同形。
                cell.icon = sortAt->ascending ? core::IconId::ChevronUp
                                              : core::IconId::ChevronDown;
            }
        } else {
            cell = core::makeText(column.header, headerText);
            cell.key = owner_ + ":head:" + column.key;
            // 静态列头自带格内边距（Button 按钮 token padding 对齐；
            // Text 无 chrome，需显式补——表头文字与数据格文字同缘）。
            cell.padding =
                core::EdgeInsets::symmetric(cellPaddingXPx(), 0.0F);
        }
        cell.width = column.width;
        cell.height = headerExtent;
        if (!hasHandle) {
            headerCells.push_back(std::move(cell));
            continue;
        }
        auto box = core::makeStack(
            {std::move(cell), buildResizeHandle(column)},
            core::StackAlignment::TopRight);
        box.width = column.width;
        box.height = headerExtent;
        box.key = owner_ + ":headbox:" + column.key;
        headerCells.push_back(std::move(box));
    }
    auto header = core::makeRow(std::move(headerCells));
    header.key = frozen ? owner_ + ":frozen-header" : owner_ + ":header";
    header.height = headerExtent;
    header.width = frozen ? frozenContentWidth() : scrollRegionWidth();
    header.crossAxis = core::CrossAxisAlignment::Center;
    // 表头表面（§12/§3.3）：surfaceSunken 填充经 styleOverrides 注入
    //（painter 普通容器分支按 resolved 背景绘制）；1px 下边框由 build()
    // 在表头下缘补 borderDefault 分隔线叶（设计稿 th border-bottom）。
    header.styleOverrides.background = theme.colors.surfaceSunken;
    if (!frozen) {
        const auto& prefix = scrollPrefix();
        if (windowFirst < scrollColumnOrder_.size() && windowEnd > windowFirst) {
            header.padding = core::EdgeInsets{prefix[windowFirst], 0.0F,
                                              0.0F, 0.0F};
        }
    }
    return header;
}

core::Widget DataGridController::build() const {
    const float headerExtent = headerExtentPx();
    const bool hasFrozen = frozenContentWidth() > 0.0F;
    const style::Theme& theme =
        shell_ != nullptr ? shell_->theme() : style::Theme::dark();

    // 滚动区（§17 双轴 + §19 第五批区域拆分）：表头与数据区共享横向
    // offset；源接缝（hSource_）让交互层直驱 hScroll_（滚轮/拖动/惯性
    // /滚动条），应用零接线。scrollOffset 每次重建写回。
    auto scrollHeader = buildRegionHeader(false);
    auto list = core::makeList(this, owner_);
    // 显式宽（横向视口主轴无界）：行被 tight 到滚动区行宽；内容窄于
    // 视口时铺满视口（行背景完整，视口宽经源接缝跟踪收敛）。
    list.width = scrollRegionWidth();
    list.flex = 1.0F;
    // List 视口的分隔线内缩（resolveStyle List 分支 widget.padding +
    // separatorWidth）清零：行壳与表头同缘（表头/数据列边界对齐前提），
    // 行分隔线全宽（设计稿 th/td border-bottom 全宽口径）。
    list.styleOverrides.padding = core::EdgeInsets{};
    // 表头下缘 1px 分隔线（§12：分隔线 borderDefault；设计稿 th 下边框）。
    auto scrollHeaderLine = core::makeContainerLeaf(
        scrollRegionWidth(), std::optional<float>(1.0F), {}, {},
        theme.colors.borderDefault, owner_ + ":header-line");
    auto body = core::makeColumn(
        {std::move(scrollHeader), std::move(scrollHeaderLine), std::move(list)});
    body.key = owner_ + ":body";
    body.width = scrollRegionWidth();
    auto view = core::makeScrollView(std::move(body), owner_ + ":scroll");
    view.scrollAxis = core::ScrollAxis::Horizontal;
    view.virtualSource = &hSource_;
    view.scrollOffset = hScroll_.offset();
    // flex 撑满根 Row 剩余宽（总宽 − 冻结区 − 分界线）。
    view.flex = 1.0F;
    // 横向滚动条（review 发现缺失）：scrollExtent>0 时底缘实绘 thumb
    //（token 驱动、不占布局），拇指拖动经源接缝直驱 hScroll_——双轴
    // 几何的可视 affordance；scrollExtent=0（内容放得下）时 painter
    // 自然不画。
    view.showScrollbar = true;

    if (!hasFrozen) {
        // 无冻结内容（无选择列且无 pinned 列）：结构与第三批等价——根
        // 即横向视口（键 owner_ 保持兼容）。
        view.key = owner_;
        return view;
    }

    // 冻结区（§19 第五批）：冻结表头 + 冻结 List 与滚动区共享纵向几何
    //（FrozenRegionSource 全部委托网格自持的固定行高推导）；横向 offset
    // 只属于右区。分界线 = 1px borderStrong（§12：不依赖阴影）。
    auto frozenHeader = buildRegionHeader(true);
    auto frozenList =
        core::makeList(static_cast<const core::VirtualListSource*>(
                           &frozenSource_),
                       owner_ + ":frozen");
    frozenList.width = frozenContentWidth();
    frozenList.flex = 1.0F;
    // 行壳与滚动区/表头同缘（分隔线内缩清零，同上）。
    frozenList.styleOverrides.padding = core::EdgeInsets{};
    auto frozenHeaderLine = core::makeContainerLeaf(
        frozenContentWidth(), std::optional<float>(1.0F), {}, {},
        theme.colors.borderDefault, owner_ + ":frozen-header-line");
    auto frozenBody = core::makeColumn(
        {std::move(frozenHeader), std::move(frozenHeaderLine),
         std::move(frozenList)});
    frozenBody.key = owner_ + ":frozen-body";
    frozenBody.width = frozenContentWidth();

    // 分界线高度 = 表头块（表头高 + 1px 下边框）+ 内容高兜底（tight
    // 交叉宿主中被 Stretch 拉满）。
    auto freezeLine = core::makeContainerLeaf(
        1.0F, std::optional<float>(headerExtent + 1.0F + totalExtent()), {},
        {}, theme.colors.borderStrong, owner_ + ":freeze-line");

    auto root = core::makeRow(
        {std::move(frozenBody), std::move(freezeLine), std::move(view)});
    root.key = owner_;
    root.crossAxis = core::CrossAxisAlignment::Stretch;
    return root;
}

// --- shell 接线 ---

void DataGridController::attach(app::AppShell& shell, std::string ownerKey) {
    shell_ = &shell;
    owner_ = std::move(ownerKey.empty() ? std::string("grid") : ownerKey);
    selection_.setKeySequence(
        [this](const std::string& from, const std::string& to) {
            return detail::closedKeyRange(
                base_.itemCount(),
                [this](std::size_t i) { return keyOf(i); }, from, to,
                [this](std::size_t i) { return rowEnabled(i); });
        });
    selection_.onSelectionChanged = [this] { requestRebuild(); };
    selection_.onCurrentChanged = [this](const std::string&) {
        requestRebuild();
    };
    // 点击身份统一解析：grid:<owner>:{row|cell|check|checkall|sort}:…
    //（表头排序与行/格/复选框同一 sink；修饰键经 pointerModifiers 读取
    // ——与 List 的 dispatchRowClick 同源，P0 真实指针路径）。
    shell.controller().addRowClickSink([this](const std::string& onClick) {
        const std::string prefix = "grid:" + owner_ + ":";
        if (onClick.rfind(prefix, 0) != 0) {
            return false;
        }
        const std::string rest = onClick.substr(prefix.size());
        const auto modifiers = shell_ != nullptr
                                   ? shell_->controller().pointerModifiers()
                                   : core::kModifierNone;
        const bool ctrl = (modifiers & core::kModifierCtrl) != 0;
        const bool shift = (modifiers & core::kModifierShift) != 0;
        if (const std::string rowPrefix = "row:"; rest.rfind(rowPrefix, 0) == 0) {
            const std::string key = rest.substr(rowPrefix.size());
            if (!key.empty()) {
                rowClicked(key, ctrl, shift);
                return true;
            }
        } else if (const std::string cellPrefix = "cell:";
                   rest.rfind(cellPrefix, 0) == 0) {
            std::string rowKey;
            std::string columnKey;
            if (parseCellRef(rest.substr(cellPrefix.size()), rowKey,
                             columnKey)) {
                rowClicked(rowKey, ctrl, shift, columnKey);
                return true;
            }
        } else if (const std::string checkPrefix = "check:";
                   rest.rfind(checkPrefix, 0) == 0) {
            const std::string key = rest.substr(checkPrefix.size());
            if (!key.empty()) {
                toggleRowSelection(key);
                return true;
            }
        } else if (rest == "checkall") {
            toggleSelectAll();
            return true;
        } else if (const std::string sortPrefix = "sort:";
                   rest.rfind(sortPrefix, 0) == 0) {
            const std::string columnKey = rest.substr(sortPrefix.size());
            if (!columnKey.empty()) {
                // Shift = 多列追加/更新（§11.2，extend 语义）。
                requestSort(columnKey, shift);
                return true;
            }
        }
        return false;
    });
    // 激活（双击/Enter/语义）：行 key 或格 key（格命中直接定位编辑列）。
    shell.controller().addRowActivateSink(
        [this](const std::string& nodeKey, const std::string&, bool) {
            const std::string itemPrefix = owner_ + ":item:";
            const std::string cellPrefix = owner_ + ":cell:";
            if (nodeKey.rfind(itemPrefix, 0) == 0) {
                const std::string key = nodeKey.substr(itemPrefix.size());
                if (!key.empty()) {
                    (void)activateRow(key, std::nullopt);
                    return true;
                }
            } else if (nodeKey.rfind(cellPrefix, 0) == 0) {
                std::string rowKey;
                std::string columnKey;
                if (parseCellRef(nodeKey.substr(cellPrefix.size()), rowKey,
                                 columnKey)) {
                    (void)activateRow(rowKey, columnKey);
                    return true;
                }
            }
            return false;
        });
    shell.controller().addRowFocusSink([this](const std::string& rowKey) {
        const std::string prefix = owner_ + ":item:";
        if (!rowKey.starts_with(prefix)) return false;
        const std::string key = rowKey.substr(prefix.size());
        std::size_t index = 0;
        if (indexOfKey(key, index) && rowEnabled(index)) {
            selection_.setCurrent(key);
        }
        return true;
    });
}

// --- 键盘 ---

bool DataGridController::handleKey(core::Key key, core::KeyModifiers modifiers,
                                   char keyChar) {
    if (shell_ != nullptr) {
        const auto* view = core::findNodeByKey(shell_->root(), owner_);
        if (view != nullptr && !view->enabled) return false;
    }
    // 编辑态：编辑器持有焦点（beginEdit 程序化建立或点击建立）——
    // Escape 取消；Enter 提交（IME composing 期间留给输入法，§13.1）；
    // Tab 提交并移动到下一/上一可编辑格（跨行、跳过禁用行；提交失败
    // 中止移动）；其余键（文本/方向键/剪贴板）由编辑器消费，不抢。
    if (editing_) {
        if (key == core::Key::Escape) {
            cancelEdit();
            return true;
        }
        if (key == core::Key::Enter &&
            (shell_ == nullptr ||
             !shell_->controller().composingActive())) {
            // 成败都消费：失败保留编辑 + editError（错误浮层为后续增量）。
            (void)commitEdit();
            return true;
        }
        if (key == core::Key::Tab) {
            // §13.1：Tab/Shift+Tab 先提交，再移动编辑格；提交失败中止。
            // 先拷贝位置：commitEdit 成功会重置编辑态。
            const std::string fromRow = editing_->first;
            const std::string fromColumn = editing_->second;
            if (!commitEdit()) {
                return true;
            }
            moveEditor(fromRow, fromColumn,
                       (modifiers & core::kModifierShift) != 0 ? -1 : 1);
            return true;
        }
        return false;
    }
    // 焦点在本网格列宽手柄（owner+":hnd:" 前缀，§16/§22.3）：方向键与
    // Home/End 让位给交互层的 splitter 键盘路径（stepBy/stepToEdge）。
    // 应用按 onKey 契约先转发本函数，若在此消费，手柄聚焦时的 Left/
    // Right 会变成移动列焦点、Home 会跳首行——键盘步进永远不可达
    //（review：路由冲突；编辑态不至此——编辑器持有焦点）。
    if (shell_ != nullptr &&
        (key == core::Key::Left || key == core::Key::Right ||
         key == core::Key::Up || key == core::Key::Down ||
         key == core::Key::Home || key == core::Key::End)) {
        if (shell_->focus().focusedKey().starts_with(owner_ + ":hnd:")) {
            return false;
        }
    }
    // 剪贴板（⌘ 同 Ctrl——38b9fca 导航键同口径；macOS 在册目标平台）。
    const bool ctrl =
        (modifiers & (core::kModifierCtrl | core::kModifierGui)) != 0;
    if (ctrl && (keyChar == 'c' || keyChar == 'C')) {
        return copySelection() > 0;
    }
    if (ctrl && (keyChar == 'v' || keyChar == 'V')) {
        return pasteRows();
    }
    // 编辑入口/激活（Key 枚举无 F 键——编辑入口契约仅 Enter，见设计
    // 文档 §6；应用可经 beginEdit API 直接进入）。
    if (key == core::Key::Enter) {
        if (selection_.currentKey().empty()) {
            return false;
        }
        return activateRow(selection_.currentKey(), std::nullopt);
    }
    // 列焦点（编辑目标列；只走可见列）。
    if (key == core::Key::Left || key == core::Key::Right) {
        if (columns_.empty()) return false;
        std::ptrdiff_t at = -1;
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].key == currentColumn_) {
                at = static_cast<std::ptrdiff_t>(i);
                break;
            }
        }
        const std::ptrdiff_t direction =
            key == core::Key::Left ? -1 : 1;
        std::ptrdiff_t next = -1;
        if (at < 0) {
            // 无当前列（未设置/被隐藏）：按方向进入首/末可见列。
            next = direction > 0
                       ? static_cast<std::ptrdiff_t>(firstVisibleIndex())
                       : static_cast<std::ptrdiff_t>(lastVisibleIndex());
        } else {
            next = at + direction;
            while (next >= 0 &&
                   next < static_cast<std::ptrdiff_t>(columns_.size()) &&
                   !columns_[next].visible) {
                next += direction;
            }
        }
        if (next < 0 || next >= static_cast<std::ptrdiff_t>(columns_.size()) ||
            next == at) {
            return false;
        }
        currentColumn_ = columns_[next].key;
        ensureColumnVisible(currentColumn_);
        requestRebuild();
        return true;
    }
    return detail::handleCollectionKeys(
        shell_, owner_, selection_, *this,
        [this](std::size_t i) { return keyOf(i); }, key, modifiers, keyChar,
        [this](std::size_t i) { return rowEnabled(i); },
        /*ctrlMovesCurrentOnly=*/true);
}

// --- 滚动定位 / current ---

void DataGridController::scrollToKey(const std::string& key,
                                     ScrollAlignment align) {
    std::size_t index = 0;
    if (!indexOfKey(key, index)) {
        return;
    }
    detail::scrollToAligned(*this, index, align);
    requestRebuild();
}

void DataGridController::setCurrentKey(const std::string& key, bool extend) {
    std::size_t index = 0;
    if (!indexOfKey(key, index) || !rowEnabled(index)) return;
    selection_.moveTo(key, extend);
    scrollToKey(key, ScrollAlignment::Visible);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

void DataGridController::setCurrentColumn(const std::string& columnKey) {
    // 只接受可见列（隐藏列没有可定位的格）。
    if (!columnVisible(columnKey)) return;
    currentColumn_ = columnKey;
    ensureColumnVisible(columnKey);
    requestRebuild();
}

// --- VirtualListSource（纵向几何自持：固定行高，§19 P0.1） ---
//
// 行高取 Theme.dataGrid.rowExtent（Comfortable 40 与集合行旧实测值等值，
// 像素零变化）。extent/offset/窗口全部按行高直接推导——冻结区与滚动区
// 两个 List 的几何因此严格等值（§19 冻结列前提），noteExtent 实测回填
// 忽略（materialize 循环一轮即稳定）；base_ 只保留 itemCount、纵向
// ScrollController 与键查找。

std::size_t DataGridController::itemCount() const {
    return base_.itemCount();
}

float DataGridController::estimatedExtent() const {
    return rowExtentPx();
}

float DataGridController::extentOf(std::size_t index) const {
    return index < base_.itemCount() ? rowExtentPx() : 0.0F;
}

float DataGridController::scrollOffset() const { return base_.scrollOffset(); }

float DataGridController::totalExtent() const {
    return static_cast<float>(base_.itemCount()) * rowExtentPx();
}

float DataGridController::offsetOfIndex(std::size_t index) const {
    return static_cast<float>(index) * rowExtentPx();
}

std::pair<std::size_t, std::size_t> DataGridController::visibleRange(
    float viewportExtent, float cacheExtent) const {
    // 通用窗口推导（VirtualListSource::visibleRangeAt：extentOf/
    // offsetOfIndex 二分）——固定行高下即区间切分。
    return visibleRangeAt(scrollOffset(), viewportExtent, cacheExtent);
}

void DataGridController::noteExtent(std::size_t index, float extent) const {
    // 固定行高：布局实测回填忽略（幂等 no-op；行壳显式定高一致）。
    (void)index;
    (void)extent;
}

void DataGridController::updateViewport(float viewportExtent,
                                        float contentPadding) const {
    // 纵向 extents 自持（原委托 base_ 的内部 totalExtent 是估算口径，
    // 固定行高下直接按真实内容高钳制纵向滚动范围）。
    base_.scrollController()->updateExtents(viewportExtent,
                                            totalExtent() + contentPadding);
}

bool DataGridController::indexOfKey(const std::string& key,
                                    std::size_t& index) const {
    return detail::findKeyIndex(
        base_.itemCount(),
        [this](std::size_t i) { return keyOf(i); }, key, index);
}

core::Widget DataGridController::buildEmpty() const {
    core::Widget empty;
    if (emptyBuilder_) {
        // 数据状态壳由应用组合（§14：加载骨架/错误重试/无结果等）；
        // 空/EmptyText 语义与节点身份由网格补写。
        empty = emptyBuilder_();
    } else {
        auto text = core::makeText("No rows");
        text.listPart = core::ListPart::EmptyText;
        empty = core::makeColumn({std::move(text)},
            core::MainAxisAlignment::Center, core::CrossAxisAlignment::Center);
    }
    empty.listPart = core::ListPart::Empty;
    empty.key = owner_ + ":empty";
    return empty;
}

std::string DataGridController::tabStopKey() const {
    return selection_.currentKey().empty() ? std::string{}
        : owner_ + ":item:" + selection_.currentKey();
}

// --- 私有 ---

std::string DataGridController::keyOf(std::size_t index) const {
    if (keyOf_) {
        return keyOf_(index);
    }
    return "r" + std::to_string(index);
}

std::string DataGridController::cellText(std::size_t row,
                                         const std::string& columnKey) const {
    if (cellText_) {
        return cellText_(row, columnKey);
    }
    return {};
}

bool DataGridController::rowEnabled(std::size_t index) const {
    if (index >= base_.itemCount()) return false;
    return !enabledOf_ || enabledOf_(index);
}

bool DataGridController::columnExists(const std::string& columnKey) const {
    return std::any_of(columns_.begin(), columns_.end(),
                       [&](const DataColumn& column) {
                           return column.key == columnKey;
                       });
}

bool DataGridController::columnVisible(const std::string& columnKey) const {
    return std::any_of(columns_.begin(), columns_.end(),
                       [&](const DataColumn& column) {
                           return column.key == columnKey && column.visible;
                       });
}

std::string DataGridController::firstEditableColumn() const {
    for (const auto& column : columns_) {
        if (column.editable && column.visible) return column.key;
    }
    return {};
}

std::string DataGridController::firstVisibleColumn() const {
    const std::size_t at = firstVisibleIndex();
    return at < columns_.size() ? columns_[at].key : std::string{};
}

std::size_t DataGridController::firstVisibleIndex() const {
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].visible) return i;
    }
    return columns_.size();
}

std::size_t DataGridController::lastVisibleIndex() const {
    for (std::size_t i = columns_.size(); i > 0; --i) {
        if (columns_[i - 1].visible) return i - 1;
    }
    return columns_.size();
}

bool DataGridController::commitPendingEdit() {
    if (!editing_) {
        return true;
    }
    return commitEdit();
}

void DataGridController::rowClicked(const std::string& key, bool ctrl,
                                    bool shift,
                                    const std::string& columnKey) {
    std::size_t index = 0;
    if (!indexOfKey(key, index) || !rowEnabled(index)) return;
    // 按压落在编辑字段内（光标定位/文本选择，冒泡到行 onClick）：
    // 不打断草稿、不改选择——编辑器内的文本操作由编辑器消费（§13.1）。
    if (editing_ && shell_ != nullptr &&
        shell_->controller().focusedBind() == owner_ + ":edit") {
        return;
    }
    // 编辑态点击其他位置 = 先提交（设计文档 §13.1：点击其他格先提交）；
    // 失败中止——不改选择、不动焦点，草稿保留在错误格。
    if (editing_) {
        const bool sameCell = editing_->first == key &&
                              !columnKey.empty() &&
                              editing_->second == columnKey;
        if (!sameCell && !commitEdit()) return;
    }
    if (!columnKey.empty() && columnVisible(columnKey)) {
        currentColumn_ = columnKey;
    }
    selection_.click(key, ctrl, shift);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

bool DataGridController::activateRow(
    const std::string& rowKey, const std::optional<std::string>& columnKey) {
    std::size_t row = 0;
    if (!indexOfKey(rowKey, row) || !rowEnabled(row)) {
        return false;
    }
    // 编辑中同行/同格的重复激活（编辑器内双击/语义 Activate 冒泡到行）：
    // 不打断草稿（§13.1；HTML 同位——编辑中的格不响应激活）。
    if (editing_ && editing_->first == rowKey &&
        (!columnKey || *columnKey == editing_->second)) {
        return true;
    }
    // 激活前先提交；失败中止（§13.1）。
    if (!commitPendingEdit()) {
        return true;
    }
    std::string column = columnKey.value_or(std::string{});
    if (!columnVisible(column)) {
        column = columnVisible(currentColumn_) ? currentColumn_
                                               : firstEditableColumn();
    }
    if (beginEdit(row, column)) {
        return true;
    }
    // beginEdit 失败但编辑仍在 = 前一格提交失败已中止；否则为只读列，
    // 回退行激活（§6）。
    if (editing_) {
        return true;
    }
    if (onRowActivated) {
        onRowActivated(rowKey);
        return true;
    }
    return false;
}

void DataGridController::moveEditor(const std::string& fromRow,
                                    const std::string& fromColumn,
                                    int direction) {
    // 可编辑格序列 = 可见可编辑列（列序）× 可用行（行序），行主序扁平
    // 推进；禁用行跳过；到边界提交后停在当前格（焦点已由 commitEdit
    // 回到行节点，§13.1）。
    std::vector<std::string> editColumns;
    for (const auto& column : columns_) {
        if (column.editable && column.visible) {
            editColumns.push_back(column.key);
        }
    }
    if (editColumns.empty()) {
        return;
    }
    std::size_t fromRowIndex = 0;
    if (!indexOfKey(fromRow, fromRowIndex)) {
        return;
    }
    std::size_t columnAt = 0;
    for (std::size_t i = 0; i < editColumns.size(); ++i) {
        if (editColumns[i] == fromColumn) {
            columnAt = i;
            break;
        }
    }
    const std::size_t columnCount = editColumns.size();
    const std::ptrdiff_t total = static_cast<std::ptrdiff_t>(
        base_.itemCount() * columnCount);
    std::ptrdiff_t flat = static_cast<std::ptrdiff_t>(
        fromRowIndex * columnCount + columnAt);
    for (flat += direction; flat >= 0 && flat < total; flat += direction) {
        const std::size_t row = static_cast<std::size_t>(flat) / columnCount;
        if (!rowEnabled(row)) {
            continue;
        }
        // 先滚动对齐再进入编辑：行在视口外时编辑器必须被物化，
        // requestFieldFocus 才能在重建后落地（横向对齐由 beginEdit 内
        // 的 ensureColumnVisible 承担，§19 P0.2）。
        scrollToKey(keyOf(row), ScrollAlignment::Visible);
        (void)beginEdit(row, editColumns[static_cast<std::size_t>(flat) %
                                          columnCount]);
        return;
    }
}

void DataGridController::toggleRowSelection(const std::string& key) {
    std::size_t index = 0;
    if (!indexOfKey(key, index) || !rowEnabled(index)) return;
    if (!commitPendingEdit()) return;
    // 复选框独立切换选择，不改 current/焦点（§11.3）。
    selection_.toggle(key);
    requestRebuild();
}

void DataGridController::toggleSelectAll() {
    if (!commitPendingEdit()) return;
    // 动作按实时可用行计算（表头勾选态显示走缓存；enabledness 变化须
    // 经 setRowEnabledOf/setRowCount 通知，见头文件契约）。
    selectableKeysCache_.clear();
    selectableKeysDirty_ = true;
    const auto& keys = selectableKeys();
    bool all = !keys.empty();
    for (const auto& key : keys) {
        if (!selection_.isSelected(key)) {
            all = false;
            break;
        }
    }
    // 表头复选框：全选当前可用行 / 清空（三态视觉为后续增量）。
    if (all) {
        selection_.clear();
    } else {
        selection_.setSelected(keys);
    }
    requestRebuild();
}

const std::vector<std::string>& DataGridController::selectableKeys() const {
    if (selectableKeysDirty_) {
        std::vector<std::string> keys;
        keys.reserve(base_.itemCount());
        for (std::size_t i = 0; i < base_.itemCount(); ++i) {
            if (rowEnabled(i)) keys.push_back(keyOf(i));
        }
        selectableKeysCache_ = std::move(keys);
        selectableKeysDirty_ = false;
    }
    return selectableKeysCache_;
}

bool DataGridController::parseCellRef(const std::string& ref,
                                      std::string& rowKey,
                                      std::string& columnKey) const {
    // ref = "<rowKey>:<colKey>"：列 key 末段匹配（行/列 key 都允许含
    // ':'；从最右冒号回扫，首个命中列集合的后缀即列 key）。
    std::size_t at = ref.rfind(':');
    while (at != std::string::npos) {
        const std::string candidate = ref.substr(at + 1);
        if (columnExists(candidate)) {
            const std::string row = ref.substr(0, at);
            if (!row.empty()) {
                rowKey = row;
                columnKey = candidate;
                return true;
            }
            return false;
        }
        if (at == 0) break;
        at = ref.rfind(':', at - 1);
    }
    return false;
}

void DataGridController::requestRebuild() {
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

core::Widget DataGridController::buildHeaderCheckCell() const {
    const auto& keys = selectableKeys();
    // 三态（§11.3/§18）：全部选中 = 勾选；部分选中 = indeterminate
    //（Widget.indeterminate → accent 填充 + 横线，语义 value="mixed"）；
    // 无可用行 = 未勾选 + 禁用。
    bool all = !keys.empty();
    bool some = false;
    for (const auto& key : keys) {
        if (!selection_.isSelected(key)) {
            all = false;
        } else {
            some = true;
        }
    }
    auto checkbox = core::makeCheckbox("", "", owner_ + ":header-check", all);
    checkbox.indeterminate = !all && some;
    checkbox.semanticsLabel = "全选当前结果中的可用行";
    checkbox.enabled = !keys.empty();
    // 整格命中都走全选（token 宽整格命中区域，§12）；Checkbox 仅作视觉/
    // 语义声明（bind 为空——框架 toggle 不接管，状态由选择集重建）。
    auto cell = core::makeRow({std::move(checkbox)},
                              core::MainAxisAlignment::Center,
                              core::CrossAxisAlignment::Center);
    cell.width = selectionColumnWidthPx();
    cell.height = headerExtentPx();  // 整 token 宽 × 表头高命中（§12）
    cell.onClick = "grid:" + owner_ + ":checkall";
    cell.key = owner_ + ":header-check-cell";
    // 键盘/语义可达（review 收口）：collectionRow 使整格成为 Tab 候选与
    // Enter/Space 激活目标（activateCollectionRow → 行点击 sink），
    // semanticsActions 暴露语义 Activate——此前纯键盘/读屏用户无法触达
    // 全选（Extended 可退而 Ctrl+A，Single/Multiple 无通路）。显式高
    // 走 crossOverride，不触发行最小高钳制（表头高 36 < 档位 40）。
    cell.collectionRow = true;
    cell.semanticsActions =
        accessibility::kActionFocus | accessibility::kActionActivate;
    return cell;
}

core::Widget DataGridController::buildRowCheckCell(
    const std::string& key) const {
    // 行复选框列：格级命中切换该行（不改 current），整格 token 宽命中。
    auto checkbox = core::makeCheckbox(
        "", "", owner_ + ":check:" + key, selection_.isSelected(key));
    checkbox.semanticsLabel = "选择 " + key;
    auto cell = core::makeRow({std::move(checkbox)},
                              core::MainAxisAlignment::Center,
                              core::CrossAxisAlignment::Center);
    cell.width = selectionColumnWidthPx();
    cell.height = rowExtentPx();  // 命中区域至少行高（§12 选择辅助列）
    cell.onClick = "grid:" + owner_ + ":check:" + key;
    cell.key = owner_ + ":check-cell:" + key;
    return cell;
}

core::Widget DataGridController::buildCell(std::size_t index,
                                           const std::string& key,
                                           const DataColumn& column) const {
    const std::string cellId = owner_ + ":cell:" + key + ":" + column.key;
    if (editing_ && editing_->first == key &&
        editing_->second == column.key) {
        // 编辑器：绑定 state（commitEdit 读取）；宽度与列对齐。
        auto editor = core::makeTextField(
            {}, editError_.empty() ? "edit" : editError_);
        editor.bind = owner_ + ":edit";
        editor.width = column.width;
        editor.flex = 0.0F;
        editor.key = owner_ + ":editor";
        editor.invalid = !editError_.empty();
        return editor;
    }
    // 单元格：固定列宽 + 水平内边距（§12：metrics.controlPaddingX 密度
    // 档）+ 单行省略 + 点击身份（定位列焦点；onClick 与节点 key 同串，
    // 激活路径按 key 解析）。
    // 统一格式盒（Row，key cellId:box）：盒即内容行——显式定高 = 行高、
    // crossAxis Center（文本垂直居中）、主轴 Start/End 承载列对齐
    //（Text 无段内对齐）。当前格（current 行 × current 列）的内嵌焦点
    // 环经盒边框承载 focusRing token（§12/设计稿 td:focus：环铺满整格；
    // Row 走 painter 普通容器 paintSurface 路径，边框绘制与 Container
    // 同源）。盒无条件存在——环的出现不得改变格的结构 identity（双击
    // 检测/damage 按身份配对）；编辑中的格由编辑器自身表达焦点，盒仍
    // 保留。
    auto cell = core::makeText(cellText(index, column.key), cellTextStyle());
    cell.onClick = "grid:" + owner_ + ":cell:" + key + ":" + column.key;
    cell.key = cellId;
    const float paddingX = cellPaddingXPx();
    cell.width = std::max(0.0F, column.width - 2.0F * paddingX);
    auto box = core::makeRow(
        {std::move(cell)},
        column.align == DataColumnAlign::End
            ? core::MainAxisAlignment::End
            : core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Center);
    box.width = column.width;
    box.height = rowExtentPx();
    box.padding = core::EdgeInsets::symmetric(paddingX, 0.0F);
    box.key = cellId + ":box";
    if (column.key == currentColumn_ && key == selection_.currentKey() &&
        !(editing_ && editing_->first == key &&
          editing_->second == column.key)) {
        const style::Theme& theme =
            shell_ != nullptr ? shell_->theme() : style::Theme::dark();
        box.styleOverrides.border = theme.colors.focusRing;
        box.styleOverrides.borderWidth = theme.metrics.focusRingWidth;
    }
    return box;
}

namespace {
// 禁用子树（行壳 disabled 时整行禁用，含复选框/编辑器——既有口径）。
void disableSubtree(core::Widget& widget) {
    const auto disable = [](auto&& self, core::Widget& w) -> void {
        w.enabled = false;
        for (auto& child : w.children) self(self, child);
    };
    disable(disable, widget);
}
}  // namespace

core::Widget DataGridController::buildItem(std::size_t index) const {
    const std::string key = keyOf(index);
    core::Widget row;
    detail::applyCollectionRowShell(
        row, owner_, key, selection_.isSelected(key),
        "grid:" + owner_ + ":row:" + key, core::CrossAxisAlignment::Center,
        "listItem");
    // 集合行 token 内边距清零（resolveListPart 会对 listPart=Row 覆写
    // common.padding 为 controlPaddingX/Y；styleOverrides 是唯一有效覆写
    // 通道——直接赋 row.padding 会被 resolver 覆盖，旧写法是死代码）。
    // 网格格内边距由单元格自带（§12 密度档），行壳零内边距——表头/
    // 数据列边界对齐的前提；分隔线/选中面仍按行盒全宽绘制。
    row.styleOverrides.padding = core::EdgeInsets{};
    row.listPart = index + 1 == itemCount() ? core::ListPart::LastRow
                                            : core::ListPart::Row;
    row.enabled = rowEnabled(index);
    // 固定行高（§19 P0.1）：行壳显式定高（Theme.dataGrid.rowExtent）；
    // 格式盒同高（格内容不再影响行高，实测回填已忽略）。
    row.height = rowExtentPx();
    // 选择复选框列常驻冻结区（§20.2/T5.1）——滚动区行不再重复物化
    //（第五批回归：双份复选框格且滚动区数据列起点右移选择列宽，
    // 表头/数据列错位）。
    std::vector<core::Widget> cells;
    cells.reserve(columns_.size());
    // 滚动区行窗口物化（§19 T6.2）：只构建可见列窗口，padding.left =
    // 窗口首列前缀偏移（格 x 坐标稳定；窗口外命中落到行壳 = 行级点击，
    // 已知取舍）。
    const auto [windowFirst, windowEnd] = visibleColumnWindow();
    const auto& prefix = scrollPrefix();
    for (std::size_t at = windowFirst; at < windowEnd; ++at) {
        cells.push_back(buildCell(index, key,
                                  columns_[scrollColumnOrder_[at]]));
    }
    auto content = core::makeRow(std::move(cells));
    content.flex = 1.0F;
    // 内容行垂直居中：格式盒显式定高填满行，其余自然高子项（编辑器）
    // 居中——文本不贴行顶（review 第 4 条）。
    content.crossAxis = core::CrossAxisAlignment::Center;
    if (windowFirst < scrollColumnOrder_.size() && windowEnd > windowFirst) {
        content.padding =
            core::EdgeInsets{prefix[windowFirst], 0.0F, 0.0F, 0.0F};
    }
    if (!row.enabled) {
        disableSubtree(content);
    }
    row.children.push_back(std::move(content));
    return row;
}

core::Widget DataGridController::buildFrozenItem(std::size_t index) const {
    // 冻结区行（§19 T5.3/T5.5）：与滚动区行是同一逻辑行的两个视图——
    // 选择/禁用/行点击语义同串；key 前缀 frow: 避开滚动区 identity。
    // excludeFromFocus：Tab 唯一入口在滚动区行（指针 hover/press/click
    // 仍参与，§20.4）。语义侧不整树排除（§22.1）：行复选框格与 pinned
    // 列格只在此物化，整树排除会让读屏用户读不到它们；行壳不设
    // semanticsRole/actions，以 Group 角色进树承载独占内容——行级
    // listItem/selected 播报仍由滚动区行唯一承载，不重复。
    // collectionRow 与滚动区行一致置位：hover/pressed 状态与分隔线
    // 绘制对齐（§20.4 已知限制收口——此前冻结行无 hover 高亮）。
    const std::string key = keyOf(index);
    core::Widget row;
    row.type = core::WidgetType::Row;
    row.key = owner_ + ":frow:" + key;
    row.collectionRow = true;
    row.selected = selection_.isSelected(key);
    row.onClick = "grid:" + owner_ + ":row:" + key;
    row.crossAxis = core::CrossAxisAlignment::Center;
    // 行壳零内边距（与滚动区行/表头同缘，见 buildItem）。
    row.styleOverrides.padding = core::EdgeInsets{};
    row.listPart = index + 1 == itemCount() ? core::ListPart::LastRow
                                            : core::ListPart::Row;
    row.enabled = rowEnabled(index);
    row.height = rowExtentPx();
    row.excludeFromFocus = true;
    std::vector<core::Widget> cells;
    cells.reserve(columns_.size() + 1);
    if (selection_.mode() != SelectionMode::None) {
        cells.push_back(buildRowCheckCell(key));
    }
    for (const auto& column : columns_) {
        // 冻结区行只构建 pinned 可见列。
        if (!column.visible || !column.pinned) {
            continue;
        }
        cells.push_back(buildCell(index, key, column));
    }
    auto content = core::makeRow(std::move(cells));
    content.flex = 1.0F;
    content.crossAxis = core::CrossAxisAlignment::Center;
    if (!row.enabled) {
        disableSubtree(content);
    }
    row.children.push_back(std::move(content));
    return row;
}

// --- 第三批（§17）：横向视口源接缝 / 列宽手柄源 / 几何辅助 ---

float DataGridController::headerExtentPx() const {
    return shell_ != nullptr ? shell_->theme().dataGrid.headerExtent
                             : style::DataGridTokens{}.headerExtent;
}

float DataGridController::rowExtentPx() const {
    return shell_ != nullptr ? shell_->theme().dataGrid.rowExtent
                             : style::DataGridTokens{}.rowExtent;
}

float DataGridController::selectionColumnWidthPx() const {
    return shell_ != nullptr
               ? shell_->theme().dataGrid.selectionColumnWidth
               : style::DataGridTokens{}.selectionColumnWidth;
}

float DataGridController::resizeHitWidthPx() const {
    return shell_ != nullptr ? shell_->theme().dataGrid.resizeHitWidth
                             : style::DataGridTokens{}.resizeHitWidth;
}

float DataGridController::cellPaddingXPx() const {
    // §12：格内边距 = metrics.controlPaddingX 密度档（8/12/16，
    // fontScale 派生同步）；无 shell 时退回 Medium 档 12。
    if (shell_ != nullptr) {
        const auto& metrics = shell_->theme().metrics;
        return metrics.controlPaddingX[metrics.baseIndex];
    }
    return style::Theme::dark().metrics.controlPaddingX[1];
}

float DataGridController::frozenContentWidth() const {
    // 冻结区内容宽（§19 第五批）：选择复选框列常驻冻结区首列 + pinned
    // 可见列宽和。
    float width = selection_.mode() != SelectionMode::None
                      ? selectionColumnWidthPx()
                      : 0.0F;
    for (const auto& column : columns_) {
        if (column.visible && column.pinned) {
            width += column.width;
        }
    }
    return width;
}

float DataGridController::scrollContentWidth() const {
    // 滚动区内容宽：非 pinned 可见列宽和。
    float width = 0.0F;
    for (const auto& column : columns_) {
        if (column.visible && !column.pinned) {
            width += column.width;
        }
    }
    return width;
}

float DataGridController::scrollRegionWidth() const {
    // 滚动区行宽 = max(滚动内容宽, 滚动视口宽)——内容窄于视口铺满
    //（行背景/表头完整；视口宽由源接缝跟踪，两帧收敛）。scrollExtent
    // 两种取法等价：max(content, viewport) − viewport =
    // max(0, content − viewport)。
    return std::max(scrollContentWidth(), hViewportWidth_);
}

float DataGridController::columnLeftInRegion(
    const std::string& columnKey) const {
    // 列在**所属区域内容坐标**中的左缘（§19 P0.3 区域感知前缀换算）：
    // 冻结列相对冻结区原点（选择列不计——复选框列不属于 DataColumn
    // 序列）、滚动列相对滚动区原点。
    const DataColumn* column = findColumn(columnKey);
    if (column == nullptr) {
        return 0.0F;
    }
    float width = 0.0F;
    for (const auto& candidate : columns_) {
        if (candidate.key == columnKey) {
            break;
        }
        if (candidate.visible && candidate.pinned == column->pinned) {
            width += candidate.width;
        }
    }
    return width;
}

const std::vector<float>& DataGridController::scrollPrefix() const {
    if (scrollPrefixDirty_) {
        scrollPrefix_.assign(1, 0.0F);
        scrollColumnOrder_.clear();
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            const DataColumn& column = columns_[i];
            if (column.visible && !column.pinned) {
                scrollPrefix_.push_back(scrollPrefix_.back() + column.width);
                scrollColumnOrder_.push_back(i);
            }
        }
        scrollPrefixDirty_ = false;
    }
    return scrollPrefix_;
}

std::pair<std::size_t, std::size_t> DataGridController::visibleColumnWindow()
    const {
    // 滚动区可见列窗口（§19 T6.1）：与 [offset − cache, offset + 视口 +
    // cache] 相交的列区间（前缀和二分首列 + 线性尾扫；cache 覆盖窗口缘
    // 手柄，列宽 >= 40 → 尾扫 O(可见列)）。
    const auto& prefix = scrollPrefix();
    const std::size_t count = scrollColumnOrder_.size();
    if (count == 0) {
        return {0, 0};
    }
    const float cache = 120.0F;
    const float left = hScroll_.offset() - cache;
    const float right =
        hScroll_.offset() + std::max(hViewportWidth_, 0.0F) + cache;
    std::size_t low = 0;
    std::size_t high = count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (prefix[middle + 1] <= left) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    std::size_t end = low;
    while (end < count && prefix[end] < right) {
        ++end;
    }
    return {low, end};
}

const DataColumn* DataGridController::findColumn(
    const std::string& columnKey) const {
    for (const auto& column : columns_) {
        if (column.key == columnKey) {
            return &column;
        }
    }
    return nullptr;
}

void DataGridController::syncColumnSources() {
    // 按列 key 建档（键已存在则保留：地址稳定，拖动中的源不因
    // setColumns 失效）；initialWidth 只在首次建档时捕获（reset 目标）。
    for (const auto& column : columns_) {
        auto [it, inserted] =
            columnSources_.try_emplace(column.key, ColumnResizeSource{});
        it->second.owner = this;
        it->second.columnKey = column.key;
        if (inserted) {
            it->second.initialWidth = column.width;
        }
    }
    // 移除列的源条目保留（小对象；永不复用键名换取零悬垂窗口）。
}

const DataGridController::ColumnResizeSource*
DataGridController::resizeSourceFor(const std::string& columnKey) const {
    const auto it = columnSources_.find(columnKey);
    return it != columnSources_.end() ? &it->second : nullptr;
}

core::Widget DataGridController::buildResizeHandle(
    const DataColumn& column) const {
    // 手柄 = splitter 通道节点（painter 按 splitterSource 特判画居中轨道
    // 线：线宽经源覆写——静止 0 不画（§12 默认不加竖线/设计稿 resizer
    // 静止透明）、激活 2px accent；窄而高 → 拖动自动判为横向）。onClick
    // 不注册 handler：单击 no-op——非空是 hover 承载与双击复位检测的
    // 前提（splitter 分隔条同口径）。
    core::Widget handle;
    handle.type = core::WidgetType::Button;
    handle.buttonVariant = core::ButtonVariant::Ghost;
    handle.key = owner_ + ":hnd:" + column.key;
    handle.onClick = "grid:" + owner_ + ":hnd:" + column.key;
    handle.width = resizeHitWidthPx();
    handle.height = headerExtentPx();
    handle.collectionRow = true;  // Tab 可聚焦（键盘步进通道）
    handle.showFocusRing = true;  // accent 线由 focusWidth>0 驱动
    handle.semanticsRole = "splitter";
    handle.semanticsLabel = "调整 " + column.header + " 列宽";
    handle.semanticsValue =
        std::to_string(static_cast<int>(std::lround(column.width)));
    // SetValue（splitter 的百分比语义）对列宽无意义，不暴露；键盘调宽走
    // Left/Right（交互层 splitterSource 步进路径）。
    handle.semanticsActions = accessibility::kActionFocus;
    handle.splitterSource = resizeSourceFor(column.key);
    return handle;
}

float DataGridController::HorizontalViewportSource::scrollOffset() const {
    return owner_.hScroll_.offset();
}

float DataGridController::HorizontalViewportSource::totalExtent() const {
    return owner_.scrollRegionWidth();
}

core::Widget DataGridController::HorizontalViewportSource::buildItem(
    std::size_t) const {
    // ScrollView 路径不物化 item（几何自测）；退化实现仅为满足接口。
    return {};
}

void DataGridController::HorizontalViewportSource::updateViewport(
    float viewportExtent, float /*contentPadding*/) const {
    owner_.hScroll_.updateExtents(viewportExtent,
                                  owner_.scrollRegionWidth());
    // 视口宽跟踪：变化时请求一次重建（内容窄于视口的铺满 / 收窄），
    // 下一帧几何收敛。布局期 markDirty 会被 rebuildIfDirty 末尾清脏吞
    // 掉，走 requestRebuildAfterLayout 挂起通道（VirtualList 同帧重算
    // 不适用：宽度在 build 期决策，不在布局期）。
    if (std::abs(viewportExtent - owner_.hViewportWidth_) > 0.5F) {
        owner_.hViewportWidth_ = viewportExtent;
        if (owner_.shell_ != nullptr) {
            owner_.shell_->requestRebuildAfterLayout();
        }
    }
}

core::ScrollController*
DataGridController::HorizontalViewportSource::scrollController() const {
    return &owner_.hScroll_;
}

float DataGridController::ColumnResizeSource::offsetPx() const {
    const DataColumn* column = owner->findColumn(columnKey);
    return column != nullptr
               ? owner->columnLeftInRegion(columnKey) + column->width
               : 0.0F;
}

float DataGridController::ColumnResizeSource::minLeading() const {
    const DataColumn* column = owner->findColumn(columnKey);
    return column != nullptr
               ? owner->columnLeftInRegion(columnKey) + column->minWidth
               : 0.0F;
}

float DataGridController::ColumnResizeSource::initialOffset() const {
    return owner->columnLeftInRegion(columnKey) + initialWidth;
}

float DataGridController::ColumnResizeSource::extentPx() const {
    // 区域宽度（§19 P0.3）：冻结列相对冻结区、滚动列相对滚动区。
    const DataColumn* column = owner->findColumn(columnKey);
    if (column == nullptr) {
        return 0.0F;
    }
    return column->pinned ? owner->frozenContentWidth()
                          : owner->scrollRegionWidth();
}

void DataGridController::ColumnResizeSource::dragTo(float offsetPx) const {
    // 绝对边界位置（区域坐标）→ 本列宽（resizeColumn 内部钳 minWidth）。
    (void)owner->resizeColumn(
        columnKey, offsetPx - owner->columnLeftInRegion(columnKey));
}

void DataGridController::ColumnResizeSource::stepBy(float deltaPx) const {
    const DataColumn* column = owner->findColumn(columnKey);
    if (column != nullptr) {
        (void)owner->resizeColumn(columnKey, column->width + deltaPx);
    }
}

void DataGridController::ColumnResizeSource::stepToEdge(bool maxEdge) const {
    // Home = 收缩到 minWidth；End 无上界语义（列宽无 max），不动作。
    if (!maxEdge) {
        const DataColumn* column = owner->findColumn(columnKey);
        if (column != nullptr) {
            (void)owner->resizeColumn(columnKey, column->minWidth);
        }
    }
}

void DataGridController::ColumnResizeSource::reset() const {
    (void)owner->resizeColumn(columnKey, initialWidth);
}

// --- 冻结区行源（§19 T5.3）：纵向几何全权委托网格（固定行高自持） ---

std::size_t DataGridController::FrozenRegionSource::itemCount() const {
    return owner_.itemCount();
}

float DataGridController::FrozenRegionSource::estimatedExtent() const {
    return owner_.estimatedExtent();
}

float DataGridController::FrozenRegionSource::extentOf(
    std::size_t index) const {
    return owner_.extentOf(index);
}

float DataGridController::FrozenRegionSource::scrollOffset() const {
    return owner_.scrollOffset();
}

float DataGridController::FrozenRegionSource::totalExtent() const {
    return owner_.totalExtent();
}

float DataGridController::FrozenRegionSource::offsetOfIndex(
    std::size_t index) const {
    return owner_.offsetOfIndex(index);
}

std::pair<std::size_t, std::size_t>
DataGridController::FrozenRegionSource::visibleRange(
    float viewportExtent, float cacheExtent) const {
    return owner_.visibleRange(viewportExtent, cacheExtent);
}

core::Widget DataGridController::FrozenRegionSource::buildItem(
    std::size_t index) const {
    return owner_.buildFrozenItem(index);
}

void DataGridController::FrozenRegionSource::noteExtent(
    std::size_t index, float extent) const {
    owner_.noteExtent(index, extent);
}

void DataGridController::FrozenRegionSource::updateViewport(
    float viewportExtent, float contentPadding) const {
    // 与滚动区 List 等值双调用（两区视口高相同：同一根高减同一表头高）
    // ——幂等，无实测分叉（§19 风险表）。
    owner_.updateViewport(viewportExtent, contentPadding);
}

core::ScrollController*
DataGridController::FrozenRegionSource::scrollController() const {
    // 共享同一纵向控制器：冻结区滚轮/拖动/惯性直驱与滚动区同源（T5.4）。
    return owner_.scrollController();
}

core::Widget DataGridController::FrozenRegionSource::buildEmpty() const {
    // 空态只在滚动区呈现；冻结区留白（表头仍在）。
    return {};
}

std::string DataGridController::FrozenRegionSource::tabStopKey() const {
    // Tab 唯一入口在滚动区行（T5.3）；冻结行 excludeFromFocus，集合行
    // Tab 候选收集跳过（interaction.cpp traverseFocus）。
    return {};
}

// --- 程序化列可见性（§19 P0.2） ---

void DataGridController::ensureColumnVisible(const std::string& columnKey) {
    const DataColumn* column = findColumn(columnKey);
    if (column == nullptr || !column->visible || column->pinned) {
        return;  // pinned 列恒可见；未知/隐藏列无几何可保证。
    }
    const float left = columnLeftInRegion(columnKey);
    const float right = left + column->width;
    const float viewport = hViewportWidth_;
    const float offset = hScroll_.offset();
    float next = offset;
    // 最小移动：右缘越界先对齐右缘，左缘仍越界再对齐左缘。
    if (right > offset + viewport) {
        next = right - viewport;
    }
    if (left < next) {
        next = left;
    }
    next = std::clamp(next, 0.0F, hScroll_.maxScrollOffset());
    if (next != offset) {
        hScroll_.scrollTo(next);
        requestRebuild();
    }
}

}  // namespace lumen::widgets
