// M14-D：DataGridController 实现（契约见 docs/lumen-datagrid-design.md）。

#include "lumen/widgets/datagrid.h"

#include <algorithm>
#include <sstream>

#include "collection_common.h"

namespace lumen::widgets {
namespace {
constexpr float kMinColumnWidth = 40.0F;
constexpr float kHeaderExtent = 36.0F;

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
}  // namespace

// --- 列模型 ---

void DataGridController::setColumns(std::vector<DataColumn> columns) {
    columns_ = std::move(columns);
    for (auto& column : columns_) {
        column.width = std::max(column.width, kMinColumnWidth);
    }
    if (!columnExists(currentColumn_)) {
        currentColumn_ = columns_.empty() ? std::string{}
                                          : columns_.front().key;
    }
    if (!sortColumn_.empty() && !columnExists(sortColumn_)) {
        sortColumn_.clear();
    }
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
        column.width = std::max(width, kMinColumnWidth);
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

// --- 数据装配 ---

void DataGridController::setRowCount(std::size_t count) {
    base_.setItemCount(count);
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
    requestRebuild();
}

void DataGridController::setRowEnabledOf(
    std::function<bool(std::size_t)> enabledOf) {
    enabledOf_ = std::move(enabledOf);
    requestRebuild();
}

void DataGridController::setEstimatedExtent(float extent) {
    base_.setEstimatedExtent(extent);
}

// --- 排序 ---

void DataGridController::requestSort(const std::string& columnKey) {
    if (!columnExists(columnKey)) {
        return;
    }
    for (const auto& column : columns_) {
        if (column.key == columnKey && !column.sortable) {
            return;
        }
    }
    if (sortColumn_ == columnKey) {
        sortAscending_ = !sortAscending_;
    } else {
        sortColumn_ = columnKey;
        sortAscending_ = true;
    }
    if (onSortRequest) {
        onSortRequest(sortColumn_, sortAscending_);
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
    // 按行序输出（选择集无序；与表头列序一致）。
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
    for (const auto& column : columns_) {
        if (column.key == columnKey && !column.editable) {
            return false;
        }
    }
    // 已有编辑先提交（点击其他单元格提交语义）。
    if (editing_) {
        (void)commitEdit();
    }
    editing_ = std::make_pair(keyOf(row), columnKey);
    editError_.clear();
    shell_->state().set(owner_ + ":edit", cellText(row, columnKey));
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
            editError_ = error;
            requestRebuild();
            return false;
        }
    }
    editError_.clear();
    editing_.reset();
    if (onCellEdited) {
        onCellEdited(row, columnKey, text);
    }
    requestRebuild();
    return true;
}

void DataGridController::cancelEdit() {
    if (!editing_) {
        return;
    }
    editing_.reset();
    editError_.clear();
    requestRebuild();
}

// --- 组合出口 ---

core::Widget DataGridController::build() const {
    std::vector<core::Widget> headerCells;
    headerCells.reserve(columns_.size());
    for (const auto& column : columns_) {
        // 表头 = 按钮化文本（排序点击走行点击 sink 的 grid 前缀；不可排
        // 序列退化为静态文本）。排序指示器与排序状态同源。
        std::string label = column.header;
        if (column.sortable && column.key == sortColumn_) {
            label += sortAscending_ ? " ▲" : " ▼";
        }
        auto cell = core::makeText(label);
        cell.listPart = core::ListPart::Row;  // 表头行样式口径
        cell.width = column.width;
        if (column.sortable) {
            // onClick 身份经 dispatchRowClick 前缀解析（修饰键忽略）。
            cell.onClick = "grid:" + owner_ + ":sort:" + column.key;
            cell.semanticsRole = "button";
            cell.semanticsActions = accessibility::kActionFocus |
                                    accessibility::kActionActivate;
        }
        headerCells.push_back(std::move(cell));
    }
    auto header = core::makeRow(std::move(headerCells));
    header.key = owner_ + ":header";
    header.height = kHeaderExtent;
    header.crossAxis = core::CrossAxisAlignment::Center;

    auto list = core::makeList(this, owner_);
    list.flex = 1.0F;
    auto grid = core::makeColumn({std::move(header), std::move(list)});
    grid.key = owner_;
    return grid;
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
    // 行点击（含表头排序点击——同一 sink，不同前缀）。
    shell.controller().addRowClickSink([this](const std::string& onClick) {
        const std::string rowPrefix = "grid:" + owner_ + ":row:";
        if (onClick.rfind(rowPrefix, 0) == 0) {
            const std::string key = onClick.substr(rowPrefix.size());
            if (!key.empty()) {
                rowClicked(key, false, false);
                return true;
            }
        }
        const std::string sortPrefix = "grid:" + owner_ + ":sort:";
        if (onClick.rfind(sortPrefix, 0) == 0) {
            const std::string columnKey = onClick.substr(sortPrefix.size());
            if (!columnKey.empty()) {
                requestSort(columnKey);
                return true;
            }
        }
        return false;
    });
    shell.controller().addRowActivateSink(detail::makeRowActivateSink(
        owner_, [this](const std::string& key) {
            if (onRowActivated) {
                std::size_t index = 0;
                if (indexOfKey(key, index) && rowEnabled(index)) {
                    onRowActivated(key);
                }
            }
        }));
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
    const bool ctrl = (modifiers & core::kModifierCtrl) != 0;
    // 剪贴板。
    if (ctrl && (keyChar == 'c' || keyChar == 'C')) {
        return copySelection() > 0;
    }
    if (ctrl && (keyChar == 'v' || keyChar == 'V')) {
        return pasteRows();
    }
    // 编辑态：Escape 取消；导航键不抢（编辑器持有焦点，应用按需转发）。
    if (editing_) {
        if (key == core::Key::Escape) {
            cancelEdit();
            return true;
        }
        return false;
    }
    // 编辑入口/激活（Key 枚举无 F 键——编辑入口契约仅 Enter，见设计
    // 文档 §6；应用可经 beginEdit API 直接进入）。
    if (key == core::Key::Enter) {
        if (!selection_.currentKey().empty()) {
            std::size_t row = 0;
            if (indexOfKey(selection_.currentKey(), row) && rowEnabled(row)) {
                const std::string column =
                    columnExists(currentColumn_) ? currentColumn_
                                                 : firstEditableColumn();
                if (beginEdit(row, column)) {
                    return true;
                }
                if (onRowActivated) {
                    onRowActivated(selection_.currentKey());
                    return true;
                }
            }
        }
        return false;
    }
    // 列焦点（编辑目标列）。
    if (key == core::Key::Left || key == core::Key::Right) {
        if (columns_.empty()) return false;
        std::size_t at = 0;
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].key == currentColumn_) {
                at = i;
                break;
            }
        }
        if (key == core::Key::Left && at > 0) {
            --at;
        } else if (key == core::Key::Right && at + 1 < columns_.size()) {
            ++at;
        } else {
            return false;
        }
        currentColumn_ = columns_[at].key;
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
    if (!columnExists(columnKey)) return;
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
    std::vector<core::Widget> children;
    auto text = core::makeText("No rows");
    text.listPart = core::ListPart::EmptyText;
    children.push_back(std::move(text));
    auto empty = core::makeColumn(std::move(children),
        core::MainAxisAlignment::Center, core::CrossAxisAlignment::Center);
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

std::string DataGridController::firstEditableColumn() const {
    for (const auto& column : columns_) {
        if (column.editable) return column.key;
    }
    return {};
}

void DataGridController::rowClicked(const std::string& key, bool ctrl,
                                    bool shift) {
    std::size_t index = 0;
    if (!indexOfKey(key, index) || !rowEnabled(index)) return;
    // 编辑态点击其他行 = 提交（设计文档 §6）。
    if (editing_ && editing_->first != key) {
        (void)commitEdit();
    }
    selection_.click(key, ctrl, shift);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

void DataGridController::requestRebuild() {
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
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
    cells.reserve(columns_.size());
    for (const auto& column : columns_) {
        if (editing_ && editing_->first == key &&
            editing_->second == column.key) {
            // 编辑器：绑定 state（commitEdit 读取）；宽度与列对齐。
            auto editor = core::makeTextField(
                {}, editError_.empty() ? "edit" : editError_);
            editor.bind = owner_ + ":edit";
            editor.width = column.width;
            editor.flex = 0.0F;
            editor.key = owner_ + ":editor";
            cells.push_back(std::move(editor));
            continue;
        }
        auto cell = core::makeText(cellText(index, column.key));
        cell.width = column.width;
        cells.push_back(std::move(cell));
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

}  // namespace lumen::widgets
