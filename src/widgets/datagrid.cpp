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
// 视觉系统 §3.3（Medium 档）：格水平内边距取 metrics.controlPaddingX
// 同值（collection_common kRowPaddingX = 12）。
constexpr float kCellPaddingX = detail::kRowPaddingX;
// 表头非排序列的辅助文字色（TreeList 表头同口径）。
constexpr core::Color kHeaderSecondary{140, 140, 152, 255};

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

// 单行省略的单元格文本样式（§12：长内容单行省略）。
core::TextStyle cellTextStyle(core::Color color = {}) {
    core::TextStyle style;
    style.maxLines = 1;
    style.overflow = core::TextOverflow::Ellipsis;
    if (color.a != 0) {
        style.color = color;
    }
    return style;
}
}  // namespace

// --- 列模型 ---

void DataGridController::setColumns(std::vector<DataColumn> columns) {
    columns_ = std::move(columns);
    for (auto& column : columns_) {
        // 全局下限 40；业务列 minWidth 只能更高（§11.1）。
        column.minWidth = std::max(column.minWidth, kMinColumnWidth);
        column.width = std::max(column.width, column.minWidth);
    }
    syncColumnSources();
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
    toIndex = std::min(toIndex, columns_.size() - 1);
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
    requestRebuild();
    return true;
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

core::Widget DataGridController::build() const {
    const bool checkboxes = selection_.mode() != SelectionMode::None;
    const float headerExtent = headerExtentPx();
    const float handleWidth = resizeHitWidthPx();
    std::vector<core::Widget> headerCells;
    headerCells.reserve(columns_.size() + 1);
    if (checkboxes) {
        headerCells.push_back(buildHeaderCheckCell());
    }
    for (const auto& column : columns_) {
        if (!column.visible) {
            continue;
        }
        // 表头（TreeList 同口径）：可排序列 = Ghost 按钮（hover chrome +
        // 排序指示图标）；其余 = 辅助色静态文本。可调整列右缘带列宽手柄
        //（§17，splitter 通道）；手柄带宽计入列宽预算（内容 = 列宽 −
        // 手柄带宽，与数据格列边界对齐保持不变式）。数值列内容右对齐待
        // Button 内容对齐扩展（设计文档 §16 已知限制）。
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
            if (sorted) {
                // 多列优先级数字标记待 Button 内容通道扩展（§18 已知
                // 限制）；方向 chevron 与单列同形。
                cell.icon = sortAt->ascending ? core::IconId::ChevronUp
                                              : core::IconId::ChevronDown;
            }
        } else {
            cell = core::makeText(column.header, cellTextStyle(kHeaderSecondary));
            cell.listPart = core::ListPart::Row;  // 表头行样式口径
        }
        cell.width = hasHandle ? column.width - handleWidth : column.width;
        cell.height = headerExtent;
        if (!hasHandle) {
            headerCells.push_back(std::move(cell));
            continue;
        }
        auto box = core::makeRow(
            {std::move(cell), buildResizeHandle(column)});
        box.width = column.width;
        box.key = owner_ + ":headbox:" + column.key;
        headerCells.push_back(std::move(box));
    }
    auto header = core::makeRow(std::move(headerCells));
    header.key = owner_ + ":header";
    header.height = headerExtent;
    header.width = gridWidth();
    header.crossAxis = core::CrossAxisAlignment::Center;

    auto list = core::makeList(this, owner_);
    // 显式宽（横向视口主轴无界）：行被 tight 到内容宽；内容窄于视口时
    // 铺满视口（行背景完整，视口宽经源接缝跟踪收敛）。
    list.width = gridWidth();
    list.flex = 1.0F;
    auto body = core::makeColumn({std::move(header), std::move(list)});
    body.key = owner_ + ":body";
    body.width = gridWidth();

    // 根横向视口（§17 双轴几何）：表头与数据区共享横向 offset；源接缝
    // （hSource_）让交互层直驱 hScroll_（滚轮/拖动/惯性/滚动条/语义），
    // 应用零接线。scrollOffset 每次重建写回（布局期钳制到 scrollExtent）。
    auto view = core::makeScrollView(std::move(body), owner_);
    view.scrollAxis = core::ScrollAxis::Horizontal;
    view.virtualSource = &hSource_;
    view.scrollOffset = hScroll_.offset();
    return view;
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
    const bool ctrl = (modifiers & core::kModifierCtrl) != 0;
    // 剪贴板。
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
        requestRebuild();
        return true;
    }
    return detail::handleCollectionKeys(
        shell_, owner_, selection_, *this,
        [this](std::size_t i) { return keyOf(i); }, key, modifiers, keyChar,
        [this](std::size_t i) { return rowEnabled(i); });
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
    requestRebuild();
}

// --- VirtualListSource（几何委托 base_） ---

std::size_t DataGridController::itemCount() const {
    return base_.itemCount();
}

float DataGridController::estimatedExtent() const {
    return base_.estimatedExtent();
}

float DataGridController::extentOf(std::size_t index) const {
    return base_.extentOf(index);
}

float DataGridController::scrollOffset() const { return base_.scrollOffset(); }

float DataGridController::totalExtent() const { return base_.totalExtent(); }

float DataGridController::offsetOfIndex(std::size_t index) const {
    return base_.offsetOfIndex(index);
}

std::pair<std::size_t, std::size_t> DataGridController::visibleRange(
    float viewportExtent, float cacheExtent) const {
    return base_.visibleRange(viewportExtent, cacheExtent);
}

void DataGridController::noteExtent(std::size_t index, float extent) const {
    base_.noteExtent(index, extent);
}

void DataGridController::updateViewport(float viewportExtent,
                                        float contentPadding) const {
    base_.updateViewport(viewportExtent, contentPadding);
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
        // requestFieldFocus 才能在重建后落地。
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
    cell.onClick = "grid:" + owner_ + ":checkall";
    cell.key = owner_ + ":header-check-cell";
    return cell;
}

core::Widget DataGridController::buildItem(std::size_t index) const {
    const std::string key = keyOf(index);
    core::Widget row;
    detail::applyCollectionRowShell(
        row, owner_, key, selection_.isSelected(key),
        "grid:" + owner_ + ":row:" + key, core::CrossAxisAlignment::Center,
        "listItem");
    row.padding = {};
    row.listPart = index + 1 == itemCount() ? core::ListPart::LastRow
                                            : core::ListPart::Row;
    row.enabled = rowEnabled(index);
    std::vector<core::Widget> cells;
    cells.reserve(columns_.size() + 1);
    if (selection_.mode() != SelectionMode::None) {
        // 行复选框列：格级命中切换该行（不改 current），整格 token 宽命中。
        auto checkbox =
            core::makeCheckbox("", "", owner_ + ":check:" + key,
                               selection_.isSelected(key));
        checkbox.semanticsLabel = "选择 " + key;
        auto cell = core::makeRow({std::move(checkbox)},
                                  core::MainAxisAlignment::Center,
                                  core::CrossAxisAlignment::Center);
        cell.width = selectionColumnWidthPx();
        cell.onClick = "grid:" + owner_ + ":check:" + key;
        cell.key = owner_ + ":check-cell:" + key;
        cells.push_back(std::move(cell));
    }
    for (const auto& column : columns_) {
        if (!column.visible) {
            continue;
        }
        const std::string cellId =
            owner_ + ":cell:" + key + ":" + column.key;
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
            cells.push_back(std::move(editor));
            continue;
        }
        // 单元格：固定列宽 + 水平内边距（§12）+ 单行省略 + 点击身份
        //（定位列焦点；onClick 与节点 key 同串，激活路径按 key 解析）。
        // 统一格式盒（Container，key cellId:box）：当前格（current 行 ×
        // current 列）的内嵌焦点环经盒边框承载 focusRing token（§12）。
        // 盒无条件存在——环的出现不得改变格的结构 identity（双击检测/
        // damage 按身份配对）；编辑中的格由编辑器自身表达焦点，盒仍保留。
        auto cell = core::makeText(cellText(index, column.key),
                                   cellTextStyle());
        cell.onClick = "grid:" + owner_ + ":cell:" + key + ":" + column.key;
        cell.key = cellId;
        if (column.align == DataColumnAlign::End) {
            // 数值/日期右对齐：内容盒内右置（Text 无段内对齐，经 Row
            // 主轴对齐实现；文本仍按剩余宽省略）。
            cell.width = std::max(0.0F, column.width - 2.0F * kCellPaddingX);
            auto inner = core::makeRow(
                {std::move(cell)}, core::MainAxisAlignment::End,
                core::CrossAxisAlignment::Center);
            inner.width = column.width;
            inner.padding = core::EdgeInsets::symmetric(kCellPaddingX, 0.0F);
            inner.key = cellId + ":align";
            cell = std::move(inner);
        } else {
            cell.width = column.width;
            cell.padding = core::EdgeInsets::symmetric(kCellPaddingX, 0.0F);
        }
        auto box = core::makeContainer(std::move(cell), column.width);
        box.key = cellId + ":box";
        if (column.key == currentColumn_ &&
            key == selection_.currentKey() &&
            !(editing_ && editing_->first == key &&
              editing_->second == column.key)) {
            const style::Theme& theme = shell_ != nullptr
                                            ? shell_->theme()
                                            : style::Theme::dark();
            box.styleOverrides.border = theme.colors.focusRing;
            box.styleOverrides.borderWidth = theme.metrics.focusRingWidth;
        }
        cells.push_back(std::move(box));
    }
    auto content = core::makeRow(std::move(cells));
    content.flex = 1.0F;
    if (!row.enabled) {
        const auto disable = [](auto&& self, core::Widget& widget) -> void {
            widget.enabled = false;
            for (auto& child : widget.children) self(self, child);
        };
        disable(disable, content);
    }
    row.children.push_back(std::move(content));
    return row;
}

// --- 第三批（§17）：横向视口源接缝 / 列宽手柄源 / 几何辅助 ---

float DataGridController::headerExtentPx() const {
    return shell_ != nullptr ? shell_->theme().dataGrid.headerExtent
                             : style::DataGridTokens{}.headerExtent;
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

float DataGridController::gridWidth() const {
    float width = selection_.mode() != SelectionMode::None
                      ? selectionColumnWidthPx()
                      : 0.0F;
    for (const auto& column : columns_) {
        if (column.visible) {
            width += column.width;
        }
    }
    // 内容窄于视口时铺满视口（行背景/表头完整；视口宽由源接缝跟踪，
    // 两帧收敛——VirtualList extent 修正同模式）。scrollExtent 两种取法
    // 等价：max(content, viewport) − viewport = max(0, content − viewport)。
    return std::max(width, hViewportWidth_);
}

float DataGridController::columnPrefixWidth(
    const std::string& columnKey) const {
    // 真实内容坐标（不含视口铺满补白）：选择列 + 目标列之前的可见列宽。
    float width = selection_.mode() != SelectionMode::None
                      ? selectionColumnWidthPx()
                      : 0.0F;
    for (const auto& column : columns_) {
        if (column.key == columnKey) {
            break;
        }
        if (column.visible) {
            width += column.width;
        }
    }
    return width;
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
    // 线：rest 1px border、hover/press/focus 3px accent；窄而高 → 拖动
    // 自动判为横向）。onClick 不注册 handler：单击 no-op——非空是 hover
    // 承载与双击复位检测的前提（splitter 分隔条同口径）。
    core::Widget handle;
    handle.type = core::WidgetType::Button;
    handle.buttonVariant = core::ButtonVariant::Ghost;
    handle.key = owner_ + ":hnd:" + column.key;
    handle.onClick = "grid:" + owner_ + ":hnd:" + column.key;
    handle.width = resizeHitWidthPx();
    handle.height = headerExtentPx();
    handle.collectionRow = true;  // Tab 可聚焦（键盘步进通道）
    handle.showFocusRing = true;  // 3px accent 线由 focusWidth>0 驱动
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
    return owner_.gridWidth();
}

core::Widget DataGridController::HorizontalViewportSource::buildItem(
    std::size_t) const {
    // ScrollView 路径不物化 item（几何自测）；退化实现仅为满足接口。
    return {};
}

void DataGridController::HorizontalViewportSource::updateViewport(
    float viewportExtent, float /*contentPadding*/) const {
    owner_.hScroll_.updateExtents(viewportExtent, owner_.gridWidth());
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
               ? owner->columnPrefixWidth(columnKey) + column->width
               : 0.0F;
}

float DataGridController::ColumnResizeSource::minLeading() const {
    const DataColumn* column = owner->findColumn(columnKey);
    return column != nullptr
               ? owner->columnPrefixWidth(columnKey) + column->minWidth
               : 0.0F;
}

float DataGridController::ColumnResizeSource::initialOffset() const {
    return owner->columnPrefixWidth(columnKey) + initialWidth;
}

float DataGridController::ColumnResizeSource::extentPx() const {
    return owner->gridWidth();
}

void DataGridController::ColumnResizeSource::dragTo(float offsetPx) const {
    // 绝对边界位置 → 本列宽（resizeColumn 内部钳 minWidth）。
    (void)owner->resizeColumn(columnKey,
                              offsetPx - owner->columnPrefixWidth(columnKey));
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

}  // namespace lumen::widgets
