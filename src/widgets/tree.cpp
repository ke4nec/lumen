// 集合控件：TreeController / TreeListController 实现（见头注释）。

#include "lumen/widgets/tree.h"

#include <algorithm>
#include <cmath>

// 语义层共享实现（与 List 同契约）：键盘导航/滚动对齐/sink 接线/区间序列。
#include "collection_common.h"

namespace lumen::widgets {

namespace {
// 视觉系统 §3.2 / collection-design §10.1：缩进步进与 chevron 命中区。
constexpr float kIndentStep = 20.0F;   // 每层缩进
constexpr float kChevronExtent = 24.0F;  // chevron 命中区
constexpr float kColumnGap = 0.0F;
constexpr float kMinExtent = 1.0F;

// 行首 chevron/占位（collection-design §7.4）：分支行 = 独立 Ghost
// Button（点击只 toggle，不改选择）；叶子行 = 等宽透明占位（列对齐）。
// Tree 与 TreeList 共用（后者把它放进首列盒内，列宽口径一致）。
core::Widget makeLeadWidget(const std::string& owner,
                            const TreeController::VisibleRow& row) {
    if (row.hasChildren) {
        core::Widget chevron = core::makeButton("");
        chevron.icon = row.expanded ? core::IconId::ChevronDown
                                    : core::IconId::ChevronRight;
        chevron.buttonVariant = core::ButtonVariant::Ghost;
        chevron.onClick = "tree:" + owner + ":toggle:" + row.key;
        chevron.key = owner + ":chev:" + row.key;
        chevron.width = kChevronExtent;
        chevron.height = kChevronExtent;
        chevron.semanticsLabel = row.expanded ? "折叠" : "展开";
        chevron.semanticsValue = row.expanded ? "true" : "false";
        return chevron;
    }
    return core::makeContainerLeaf(kChevronExtent, 0.0F);
}
}  // namespace

// --- TreeController ---

void TreeController::setModel(const TreeModel* model) {
    model_ = model;
    invalidateRows();
}

void TreeController::modelChanged() { invalidateRows(); }

void TreeController::invalidateRows() {
    rowsDirty_ = true;
    // 行序变化：前缀偏移随下次读取重建。
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

void TreeController::rebuildRows() const {
    if (!rowsDirty_) {
        return;
    }
    rows_.clear();
    if (model_ != nullptr) {
        // 惰性 DFS：只沿已展开分支下钻；hasChildren 为 true 而未展开
        // 的分支不物化子级（千节点目录首屏只拉可见分支）。
        const auto isExpanded = [this](const std::string& key) {
            return std::find(expanded_.begin(), expanded_.end(), key) !=
                   expanded_.end();
        };
        struct Frame {
            const std::string parent;
            std::size_t depth;
        };
        std::vector<Frame> stack;
        const std::size_t rootCount = model_->childCount("");
        for (std::size_t i = rootCount; i > 0; --i) {
            stack.push_back(Frame{model_->childAt("", i - 1), 0});
        }
        while (!stack.empty()) {
            const Frame frame = stack.back();
            stack.pop_back();
            const bool branch = model_->hasChildren(frame.parent);
            const bool expanded = branch && isExpanded(frame.parent);
            rows_.push_back(
                VisibleRow{frame.parent, frame.depth, branch, expanded});
            if (expanded) {
                const std::size_t count = model_->childCount(frame.parent);
                for (std::size_t i = count; i > 0; --i) {
                    stack.push_back(Frame{model_->childAt(frame.parent, i - 1),
                                          frame.depth + 1});
                }
            }
        }
    }
    rowsDirty_ = false;
    recomputeOffsets();
}

void TreeController::recomputeOffsets() const {
    prefix_.assign(rows_.size() + 1, 0.0F);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        prefix_[i + 1] = prefix_[i] + extentOfKey(rows_[i].key);
    }
}

void TreeController::expand(const std::string& key) {
    applyExpansion(key, true);
}

void TreeController::collapse(const std::string& key) {
    applyExpansion(key, false);
}

void TreeController::toggle(const std::string& key) {
    applyExpansion(key, !isExpanded(key));
}

bool TreeController::isExpanded(const std::string& key) const {
    return std::find(expanded_.begin(), expanded_.end(), key) !=
           expanded_.end();
}

void TreeController::applyExpansion(const std::string& key, bool expanded) {
    const auto it = std::find(expanded_.begin(), expanded_.end(), key);
    if (expanded && it == expanded_.end()) {
        expanded_.push_back(key);
        invalidateRows();
    } else if (!expanded && it != expanded_.end()) {
        expanded_.erase(it);
        invalidateRows();
    }
}

bool TreeController::expandAll(std::size_t maxRows) {
    if (model_ == nullptr) {
        return false;
    }
    // 保守防护：全部展开的可见行数超限时拒绝（防万级目录误触）。
    const auto countAll = [this](const std::string& parent) {
        std::function<std::size_t(const std::string&)> count =
            [&](const std::string& node) -> std::size_t {
            const std::size_t children = model_->childCount(node);
            std::size_t total = children;
            for (std::size_t i = 0; i < children; ++i) {
                total += count(model_->childAt(node, i));
            }
            return total;
        };
        return count(parent);
    };
    // 根可见行数（折叠态）先行展开；超限直接拒绝。递归收集用
    // std::function（MSVC 对自递归泛型 lambda 推导受限）。
    std::vector<std::string> all;
    std::function<void(const std::string&)> collect =
        [&](const std::string& node) {
            if (model_->hasChildren(node)) {
                all.push_back(node);
                const std::size_t n = model_->childCount(node);
                for (std::size_t i = 0; i < n; ++i) {
                    collect(model_->childAt(node, i));
                }
            }
        };
    const std::size_t rootCount = model_->childCount("");
    for (std::size_t i = 0; i < rootCount; ++i) {
        collect(model_->childAt("", i));
    }
    if (countAll("") > maxRows) {
        return false;
    }
    expanded_ = std::move(all);
    invalidateRows();
    return true;
}

void TreeController::collapseAll() {
    if (expanded_.empty()) {
        return;
    }
    expanded_.clear();
    invalidateRows();
}

void TreeController::setSelectionMode(SelectionMode mode) {
    selection_.setMode(mode);
}

void TreeController::attach(app::AppShell& shell, std::string ownerKey) {
    shell_ = &shell;
    owner_ = std::move(ownerKey.empty() ? std::string("tree") : ownerKey);
    estimatedExtent_ = detail::kDefaultRowExtent;
    // 区间选择：行序来自扁平化可见行序列（惰性重建）。
    selection_.setKeySequence(
        [this](const std::string& from, const std::string& to) {
            rebuildRows();
            return detail::closedKeyRange(
                rows_.size(),
                [this](std::size_t i) { return rows_[i].key; }, from, to);
        });
    selection_.onSelectionChanged = [this] { requestRebuild(); };
    selection_.onCurrentChanged = [this](const std::string&) {
        requestRebuild();
    };
    // 行/chevron 点击（collection-design §6.5）：单一 sink 按名字前缀
    // 解析——toggle 前缀先行（它是行前缀的特化），其余为行 key。虚拟化
    // 行不注册常驻 handler（滚动累积）。
    shell.controller().addRowClickSink([this](const std::string& onClick) {
        const std::string togglePrefix = "tree:" + owner_ + ":toggle:";
        if (onClick.rfind(togglePrefix, 0) == 0) {
            const std::string key = onClick.substr(togglePrefix.size());
            if (key.empty()) {
                return false;
            }
            toggle(key);
            return true;
        }
        return detail::dispatchRowClick(
            shell_, onClick, "tree:" + owner_ + ":",
            [this](const std::string& key, bool ctrl, bool shift) {
                rowClicked(key, ctrl, shift);
            });
    });
    shell.controller().addRowActivateSink(detail::makeRowActivateSink(
        owner_, [this](const std::string& key) { activate(key); }));
}

void TreeController::scrollToKey(const std::string& key,
                                 ScrollAlignment align) {
    rebuildRows();
    std::size_t index = 0;
    if (!indexOfKey(key, index)) {
        return;
    }
    detail::scrollToAligned(*this, index, align);
}

void TreeController::moveCurrent(const std::string& key, bool extend) {
    selection_.moveTo(key, extend);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

bool TreeController::handleKey(core::Key key, core::KeyModifiers modifiers,
                               char keyChar) {
    rebuildRows();
    // 共享键位（Ctrl+A / Up / Down / Home / End / PageUp / PageDown）。
    if (detail::handleCollectionKeys(
            shell_, owner_, selection_, *this,
            [this](std::size_t i) { return rows_[i].key; }, key, modifiers,
            keyChar)) {
        return true;
    }
    // --- 树专属键位（collection-design §7.4） ---
    const std::size_t count = rows_.size();
    if (count == 0) {
        return false;  // 与共享导航一致：空树不消费任何键。
    }
    std::size_t current = 0;
    const bool hasCurrent = !selection_.currentKey().empty() &&
                            indexOfKey(selection_.currentKey(), current);
    switch (key) {
        case core::Key::Left:
            // 展开 → 折叠；已折叠 → current 移到父级（Qt 契约）。
            if (hasCurrent) {
                const VisibleRow& row = rows_[current];
                if (row.expanded) {
                    collapse(row.key);
                } else if (row.depth > 0) {
                    // 父级 = 当前行上方最近的 depth-1 行。
                    for (std::size_t i = current; i > 0; --i) {
                        if (rows_[i - 1].depth < row.depth) {
                            moveCurrent(rows_[i - 1].key, false);
                            break;
                        }
                    }
                }
            }
            return true;
        case core::Key::Right:
            // 折叠 → 展开；已展开 → current 移到首个子级。
            if (hasCurrent) {
                const VisibleRow& row = rows_[current];
                if (row.hasChildren && !row.expanded) {
                    expand(row.key);
                } else if (row.expanded && current + 1 < count &&
                           rows_[current + 1].depth == row.depth + 1) {
                    moveCurrent(rows_[current + 1].key, false);
                }
            }
            return true;
        default:
            // Enter 走激活路径（避免双重激活）；Tab 留给焦点遍历。
            return false;
    }
}

bool TreeController::rowOfKey(const std::string& key, VisibleRow& row) const {
    rebuildRows();
    for (const auto& candidate : rows_) {
        if (candidate.key == key) {
            row = candidate;
            return true;
        }
    }
    return false;
}

// --- VirtualListSource ---

std::size_t TreeController::itemCount() const {
    rebuildRows();
    return rows_.size();
}

float TreeController::estimatedExtent() const { return estimatedExtent_; }

float TreeController::extentOfKey(const std::string& key) const {
    const auto it = measured_.find(key);
    return it != measured_.end() ? it->second : estimatedExtent_;
}

float TreeController::extentOf(std::size_t index) const {
    rebuildRows();
    return index < rows_.size() ? extentOfKey(rows_[index].key)
                                : estimatedExtent_;
}

float TreeController::scrollOffset() const { return scroll_.offset(); }

float TreeController::totalExtent() const {
    rebuildRows();
    return prefix_.empty() ? 0.0F : prefix_.back();
}

float TreeController::offsetOfIndex(std::size_t index) const {
    rebuildRows();
    return index < prefix_.size() ? prefix_[index] : 0.0F;
}

std::pair<std::size_t, std::size_t> TreeController::visibleRange(
    float viewportExtent, float cacheExtent) const {
    return visibleRangeAt(scroll_.offset(), viewportExtent, cacheExtent);
}

void TreeController::noteExtent(std::size_t index, float extent) const {
    rebuildRows();
    if (index >= rows_.size() || !std::isfinite(extent)) {
        return;
    }
    const std::string key = rows_[index].key;
    const float clamped = std::max(kMinExtent, extent);
    const auto it = measured_.find(key);
    if (it != measured_.end() && it->second == clamped) {
        return;  // 幂等。
    }
    const float previousExtent = extentOfKey(key);
    const float previousOffset = scroll_.offset();
    const bool aboveViewport =
        offsetOfIndex(index) + previousExtent <= previousOffset;
    measured_[key] = clamped;
    recomputeOffsets();
    // 锚点稳定（同 VirtualListController）：视口上方修正平移 offset。
    scroll_.updateExtents(scroll_.viewportExtent(),
                          totalExtent() + contentPadding_);
    scroll_.scrollTo(previousOffset +
                     (aboveViewport ? clamped - previousExtent : 0.0F));
    extentsChanged_ = true;
}

void TreeController::updateViewport(float viewportExtent,
                                     float contentPadding) const {
    rebuildRows();
    contentPadding_ = std::max(0.0F, contentPadding);
    scroll_.updateExtents(viewportExtent, totalExtent() + contentPadding_);
}

core::Widget TreeController::buildItem(std::size_t index) const {
    rebuildRows();
    if (index >= rows_.size() || model_ == nullptr) {
        return core::Widget{};
    }
    const VisibleRow& row = rows_[index];

    std::vector<core::Widget> cells;
    cells.push_back(makeLeadWidget(owner_, row));
    cells.push_back(buildRowContent(row));

    core::Widget widget;
    detail::applyCollectionRowShell(widget, owner_, row.key,
                                    selection_.isSelected(row.key),
                                    "tree:" + owner_ + ":" + row.key,
                                    core::CrossAxisAlignment::Center,
                                    "treeItem");
    if (row.hasChildren) {
        widget.semanticsValue = row.expanded ? "true" : "false";
    }
    widget.children = std::move(cells);
    return widget;
}

core::Widget TreeController::buildRowContent(const VisibleRow& row) const {
    // 单列树：行内容 + 缩进（在应用 padding 基础上叠加；TreeList 覆盖
    // 为列盒）。
    core::Widget content = model_->buildRow(row.key, row.depth);
    content.padding.left += kIndentStep * static_cast<float>(row.depth);
    return content;
}

void TreeController::rowClicked(const std::string& key, bool ctrl,
                                bool shift) {
    selection_.click(key, ctrl, shift);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

void TreeController::activate(const std::string& key) {
    if (onActivated) {
        onActivated(key);
    }
}

void TreeController::requestRebuild() {
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

bool TreeController::indexOfKey(const std::string& key,
                                std::size_t& index) const {
    rebuildRows();
    return detail::findKeyIndex(
        rows_.size(),
        [this](std::size_t i) { return rows_[i].key; }, key, index);
}

// --- TreeListController ---

void TreeListController::attach(app::AppShell& shell,
                                std::string ownerKey) {
    TreeController::attach(shell, ownerKey);
    // 表头排序点击（collection-design §6.5）：按列名的单一 sink（列集
    // 可动态变化，不注册常驻 handler）。
    shell.controller().addRowClickSink([this](const std::string& onClick) {
        const std::string prefix = "treelist:" + owner_ + ":sort:";
        if (onClick.rfind(prefix, 0) != 0) {
            return false;
        }
        const std::string columnId = onClick.substr(prefix.size());
        if (std::none_of(columns_.begin(), columns_.end(),
                         [&columnId](const TreeListColumn& column) {
                             return column.id == columnId && column.sortable;
                         })) {
            return false;
        }
        // 首次点击/切换列 = 升序；同列再点 = 翻转（Qt QHeaderView）。
        const bool descending =
            columnId == sortColumn_ && !sortDescending_;
        setSortIndicator(columnId, descending);
        if (onHeaderClick) {
            onHeaderClick(columnId, descending);
        }
        return true;
    });
}

void TreeListController::setColumns(std::vector<TreeListColumn> columns) {
    columns_ = std::move(columns);
    widths_.assign(columns_.size(), 0.0F);
    recomputeWidths();
}

void TreeListController::setCellBuilder(
    std::function<core::Widget(const std::string&, const std::string&)>
        builder) {
    cellBuilder_ = std::move(builder);
}

void TreeListController::setSortIndicator(std::string columnId,
                                           bool descending) {
    sortColumn_ = std::move(columnId);
    sortDescending_ = descending;
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

void TreeListController::recomputeWidths() const {
    widths_.assign(columns_.size(), 0.0F);
    if (contentWidth_ <= 0.0F) {
        return;  // 首帧未知视口：固定列先排，弹性列保持 0（重排随回填）。
    }
    float used = 0.0F;
    float totalWeight = 0.0F;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (!columns_[i].visible) {
            continue;
        }
        if (columns_[i].fixedWidth > 0.0F) {
            widths_[i] = columns_[i].fixedWidth;
            used += columns_[i].fixedWidth + kColumnGap;
        } else if (columns_[i].weight > 0.0F) {
            totalWeight += columns_[i].weight;
        }
    }
    const float free = std::max(0.0F, contentWidth_ - used);
    if (totalWeight > 0.0F) {
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            if (!columns_[i].visible || columns_[i].fixedWidth > 0.0F ||
                columns_[i].weight <= 0.0F) {
                continue;
            }
            widths_[i] = std::max(columns_[i].minWidth,
                                  free * columns_[i].weight / totalWeight);
        }
    } else {
        // 无权重列：剩余宽归最后一个可见列（collection-design §8.2）。
        for (std::size_t i = columns_.size(); i > 0; --i) {
            if (columns_[i - 1].visible && columns_[i - 1].fixedWidth <= 0.0F) {
                widths_[i - 1] = std::max(columns_[i - 1].minWidth, free);
                break;
            }
        }
    }
}

void TreeListController::noteContentWidth(float width) const {
    // 列宽预算扣掉行壳/表头两侧对称内边距（detail::kRowPaddingX）：列盒
    // 与表头同起（左 pad 后），同止（右 pad 前）——右侧不再被视口裁剪，
    // overlay 滚动条也落在右 pad 空区。chevron 在首列盒内，不另占宽。
    const float budget =
        std::max(0.0F, width - 2.0F * detail::kRowPaddingX);
    if (budget == contentWidth_) {
        return;
    }
    contentWidth_ = budget;
    recomputeWidths();
}

core::Widget TreeListController::buildItem(std::size_t index) const {
    rebuildRows();
    // rows_/model_ 在基类是 private：经可见行序列访问（model 为空时
    // 序列为空，与基类的 model_ 判空等价）。
    const std::vector<VisibleRow>& rows = visibleRows();
    if (index >= rows.size()) {
        return core::Widget{};
    }
    const VisibleRow& row = rows[index];
    core::Widget rowCells = buildRowContent(row);
    // chevron 进首列盒（与表头同列口径：表头/行列对齐，右侧无裁剪）。
    if (!rowCells.children.empty()) {
        core::Widget box = std::move(rowCells.children.front());
        core::Widget inner;
        inner.type = core::WidgetType::Row;
        inner.crossAxis = core::CrossAxisAlignment::Center;
        core::Widget cell = box.children.empty()
                                ? core::Widget{}
                                : std::move(box.children.front());
        inner.children.push_back(makeLeadWidget(owner_, row));
        inner.children.push_back(std::move(cell));
        box.children.clear();
        box.children.push_back(std::move(inner));
        rowCells.children.front() = std::move(box);
    }

    core::Widget widget;
    detail::applyCollectionRowShell(widget, owner_, row.key,
                                    selection_.isSelected(row.key),
                                    "tree:" + owner_ + ":" + row.key,
                                    core::CrossAxisAlignment::Center,
                                    "treeItem");
    if (row.hasChildren) {
        widget.semanticsValue = row.expanded ? "true" : "false";
    }
    widget.children.push_back(std::move(rowCells));
    return widget;
}

core::Widget TreeListController::buildRowContent(
    const VisibleRow& row) const {
    if (cellBuilder_ == nullptr || columns_.empty()) {
        return TreeController::buildRowContent(row);
    }
    recomputeWidths();
    std::vector<core::Widget> cells;
    bool first = true;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        if (!columns_[i].visible) {
            continue;
        }
        core::Widget cell = cellBuilder_(row.key, columns_[i].id);
        if (first) {
            // 缩进注入首列（chevron 之后；叠加应用 padding）。
            cell.padding.left +=
                kIndentStep * static_cast<float>(row.depth);
            first = false;
        }
        core::Widget box;
        box.type = core::WidgetType::Container;
        box.width = widths_[i];
        box.children.push_back(std::move(cell));
        cells.push_back(std::move(box));
    }
    core::Widget rowCells;
    rowCells.type = core::WidgetType::Row;
    rowCells.crossAxis = core::CrossAxisAlignment::Center;
    rowCells.children = std::move(cells);
    return rowCells;
}

core::Widget TreeListController::buildHeader() const {
    if (columns_.empty()) {
        return core::Widget{};
    }
    recomputeWidths();
    std::vector<core::Widget> cells;
    for (std::size_t i = 0; i < columns_.size(); ++i) {
        const TreeListColumn& column = columns_[i];
        if (!column.visible) {
            continue;
        }
        const bool sorted = column.id == sortColumn_;
        core::Widget cell;
        if (column.sortable) {
            cell = core::makeButton(column.label);
            cell.buttonVariant = core::ButtonVariant::Ghost;
            cell.onClick = "treelist:" + owner_ + ":sort:" + column.id;
            cell.key = owner_ + ":head:" + column.id;
            if (sorted) {
                cell.icon = sortDescending_ ? core::IconId::ChevronDown
                                             : core::IconId::ChevronUp;
            }
        } else {
            cell = core::makeText(column.label);
            cell.textStyle.color = core::Color{140, 140, 152, 255};
        }
        cell.alignContentStart = true;
        cell.width = widths_[i];
        cell.height = detail::kDefaultRowExtent;
        cells.push_back(std::move(cell));
    }
    core::Widget header;
    header.type = core::WidgetType::Row;
    header.key = owner_ + ":header";
    header.crossAxis = core::CrossAxisAlignment::Center;
    header.padding = core::EdgeInsets::symmetric(detail::kRowPaddingX, 0.0F);
    header.children = std::move(cells);
    return header;
}

}  // namespace lumen::widgets
