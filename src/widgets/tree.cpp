// 集合控件：TreeController / TreeListController 实现（见头注释）。

#include "lumen/widgets/tree.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

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
                            const TreeController::VisibleRow& row, bool styledTree = false) {
    if (row.hasChildren) {
        core::Widget chevron = core::makeButton("");
        chevron.icon = row.expanded ? core::IconId::ChevronDown
                                    : core::IconId::ChevronRight;
        chevron.buttonVariant = core::ButtonVariant::Ghost;
        chevron.onClick = "tree:" + owner + ":toggle:" + row.key;
        chevron.key = owner + ":chev:" + row.key;
        if (styledTree) chevron.treePart = core::TreePart::Chevron;
        else {
            chevron.width = kChevronExtent;
            chevron.height = kChevronExtent;
        }
        chevron.semanticsLabel = row.expanded ? "折叠" : "展开";
        chevron.semanticsValue = row.expanded ? "true" : "false";
        return chevron;
    }
    if (!styledTree) return core::makeContainerLeaf(kChevronExtent, 0.0F);
    auto spacer = core::makeIcon(core::IconId::None);
    spacer.treePart = core::TreePart::Spacer;
    return spacer;
}
}  // namespace

// --- TreeController ---

void TreeController::setModel(const TreeModel* model) {
    model_ = model;
    measured_.clear();
    contentEnabled_.clear();
    invalidateRows();
}

void TreeController::modelChanged() {
    contentEnabled_.clear();
    invalidateRows();
}

void TreeController::setEmptyBuilder(std::function<core::Widget()> builder) {
    emptyBuilder_ = std::move(builder);
    requestRebuild();
}

bool TreeController::itemEnabled(std::size_t index) const {
    rebuildRows();
    if (index >= rows_.size() || model_ == nullptr || !model_->isEnabled(rows_[index].key)) return false;
    const auto found = contentEnabled_.find(rows_[index].key);
    return found == contentEnabled_.end() || found->second;
}

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
        const std::unordered_set<std::string> expandedKeys(expanded_.begin(), expanded_.end());
        std::unordered_set<std::string> visited;
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
            if (frame.parent.empty() || !visited.insert(frame.parent).second) continue;
            const bool branch = model_->hasChildren(frame.parent);
            const bool expanded = branch && expandedKeys.contains(frame.parent);
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
    // Copy keys before rebuilding: callers may pass a reference into rows_.
    const std::string target = key;
    const std::string currentKey = selection_.currentKey();
    std::size_t current = 0, parent = 0;
    bool hidesCurrent = false;
    if (!expanded && indexOfKey(target, parent) && indexOfKey(currentKey, current) && current > parent) {
        hidesCurrent = true;
        for (std::size_t i = parent + 1; i <= current; ++i) {
            if (rows_[i].depth <= rows_[parent].depth) { hidesCurrent = false; break; }
        }
    }
    const auto it = std::find(expanded_.begin(), expanded_.end(), target);
    if (expanded && it == expanded_.end()) {
        expanded_.push_back(target);
        invalidateRows();
    } else if (!expanded && it != expanded_.end()) {
        expanded_.erase(it);
        invalidateRows();
    }
    if (hidesCurrent) {
        selection_.setCurrent(target);
        if (shell_ != nullptr && shell_->focus().focusedKey() == owner_ + ":item:" + currentKey) {
            shell_->focus().setFocus(owner_ + ":item:" + target);
        }
        scrollToKey(target, ScrollAlignment::Visible);
    }
}

bool TreeController::expandAll(std::size_t maxRows) {
    if (model_ == nullptr) {
        return false;
    }
    // Bounded iterative traversal: do not enumerate the rest of a huge model
    // before rejecting; duplicate/cyclic keys cannot recurse indefinitely.
    struct Frame { std::string key; std::size_t next{0}; };
    std::vector<Frame> stack{{"", 0}};
    std::unordered_set<std::string> visited;
    std::vector<std::string> all;
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.next >= model_->childCount(frame.key)) { stack.pop_back(); continue; }
        if (visited.size() >= maxRows) return false;
        const std::string key = model_->childAt(frame.key, frame.next++);
        if (key.empty() || !visited.insert(key).second) continue;
        if (model_->hasChildren(key)) {
            all.push_back(key);
            stack.push_back({key, 0});
        }
    }
    expanded_ = std::move(all);
    invalidateRows();
    return true;
}

void TreeController::collapseAll() {
    if (expanded_.empty()) {
        return;
    }
    std::size_t current = 0;
    if (indexOfKey(selection_.currentKey(), current) && rows_[current].depth > 0) {
        while (current > 0 && rows_[current].depth > 0) --current;
        collapse(rows_[current].key);
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
                [this](std::size_t i) { return rows_[i].key; }, from, to,
                [this](std::size_t i) { return itemEnabled(i); });
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
            std::size_t index = 0;
            if (indexOfKey(key, index) && itemEnabled(index)) {
                toggle(key);
                // Pointer-down releases non-field focus. Restore the current
                // row after toggling, without selecting it or the clicked branch.
                const std::string current = selection_.currentKey();
                if (indexOfKey(current, index) && itemEnabled(index)) {
                    shell_->focus().setFocus(owner_ + ":item:" + current);
                } else {
                    selection_.setCurrent(key);
                    shell_->focus().setFocus(owner_ + ":item:" + key);
                }
            }
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
    shell.controller().addRowFocusSink([this](const std::string& rowKey) {
        const std::string prefix = owner_ + ":item:";
        if (!rowKey.starts_with(prefix)) return false;
        const std::string key = rowKey.substr(prefix.size());
        std::size_t index = 0;
        if (indexOfKey(key, index) && itemEnabled(index)) selection_.setCurrent(key);
        return true;
    });
    shell.controller().addRowExpansionSink([this](const std::string& rowKey, bool expanded) {
        const std::string prefix = owner_ + ":item:";
        if (!rowKey.starts_with(prefix)) return false;
        const std::string key = rowKey.substr(prefix.size());
        std::size_t index = 0;
        if (!indexOfKey(key, index) || !itemEnabled(index) || !rows_[index].hasChildren) return false;
        applyExpansion(key, expanded);
        return true;
    });
}

void TreeController::scrollToKey(const std::string& key,
                                 ScrollAlignment align) {
    rebuildRows();
    std::size_t index = 0;
    if (!indexOfKey(key, index)) {
        return;
    }
    detail::scrollToAligned(*this, index, align);
    requestRebuild();
}

void TreeController::setCurrentKey(const std::string& key, bool extend) {
    std::size_t index = 0;
    if (indexOfKey(key, index) && itemEnabled(index)) moveCurrent(key, extend);
}

void TreeController::moveCurrent(const std::string& key, bool extend) {
    const std::string target = key;
    selection_.moveTo(target, extend);
    scrollToKey(target, ScrollAlignment::Visible);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + target);
    }
    requestRebuild();
}

bool TreeController::handleKey(core::Key key, core::KeyModifiers modifiers,
                               char keyChar) {
    if (shell_ != nullptr) {
        const auto* view = core::findNodeByKey(shell_->root(), owner_);
        if (view != nullptr && !view->enabled) return false;
    }
    rebuildRows();
    // 共享键位（Ctrl+A / Up / Down / Home / End / PageUp / PageDown）。
    if (detail::handleCollectionKeys(
            shell_, owner_, selection_, *this,
            [this](std::size_t i) { return rows_[i].key; }, key, modifiers,
            keyChar, [this](std::size_t i) { return itemEnabled(i); })) {
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
            if (hasCurrent && itemEnabled(current)) {
                const VisibleRow row = rows_[current];
                if (row.expanded) {
                    collapse(row.key);
                } else if (row.depth > 0) {
                    // 父级 = 当前行上方最近的 depth-1 行。
                    for (std::size_t i = current; i > 0; --i) {
                        if (rows_[i - 1].depth < row.depth) {
                            if (itemEnabled(i - 1)) moveCurrent(rows_[i - 1].key, false);
                            break;
                        }
                    }
                }
            }
            return true;
        case core::Key::Right:
            // 折叠 → 展开；已展开 → current 移到首个子级。
            if (hasCurrent && itemEnabled(current)) {
                const VisibleRow row = rows_[current];
                if (row.hasChildren && !row.expanded && itemEnabled(current)) {
                    expand(row.key);
                } else if (row.expanded && current + 1 < count &&
                           rows_[current + 1].depth == row.depth + 1) {
                    for (std::size_t i = current + 1; i < count && rows_[i].depth > row.depth; ++i) {
                        if (itemEnabled(i)) { moveCurrent(rows_[i].key, false); break; }
                    }
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
    cells.push_back(makeLeadWidget(owner_, row, true));
    auto content = buildRowContent(row);
    const auto labelOf = [](auto&& self, const core::Widget& child) -> std::string {
        if (!child.semanticsLabel.empty()) return child.semanticsLabel;
        if (child.type == core::WidgetType::Text) return child.text;
        for (const auto& nested : child.children) {
            auto label = self(self, nested);
            if (!label.empty()) return label;
        }
        return {};
    };
    const std::string label = labelOf(labelOf, content);
    if (content.enabled) contentEnabled_.erase(row.key);
    else contentEnabled_[row.key] = false;
    content.flex = 1.0F;
    cells.push_back(std::move(content));

    core::Widget widget;
    detail::applyCollectionRowShell(widget, owner_, row.key,
                                    selection_.isSelected(row.key),
                                    "tree:" + owner_ + ":" + row.key,
                                    core::CrossAxisAlignment::Center,
                                    "treeItem");
    widget.padding = {};
    widget.treeDepth = static_cast<std::uint32_t>(row.depth);
    widget.treePart = index + 1 == rows_.size() ? core::TreePart::LastRow : core::TreePart::Row;
    widget.enabled = itemEnabled(index);
    widget.semanticsLabel = label;
    if (row.hasChildren) {
        widget.semanticsValue = row.expanded ? "true" : "false";
        widget.semanticsActions |= row.expanded ? accessibility::kActionCollapse : accessibility::kActionExpand;
    }
    widget.children = std::move(cells);
    if (!widget.enabled) {
        const auto disable = [](auto&& self, core::Widget& child) -> void {
            child.enabled = false;
            for (auto& nested : child.children) self(self, nested);
        };
        disable(disable, widget);
    }
    return widget;
}

core::Widget TreeController::buildEmpty() const {
    std::vector<core::Widget> children;
    if (emptyBuilder_) children.push_back(emptyBuilder_());
    else {
        auto icon = core::makeIcon(core::IconId::Folder);
        icon.treePart = core::TreePart::EmptyIcon;
        auto text = core::makeText("No items");
        text.treePart = core::TreePart::EmptyText;
        children.push_back(std::move(icon));
        children.push_back(std::move(text));
    }
    auto empty = core::makeColumn(std::move(children), core::MainAxisAlignment::Center,
                                 core::CrossAxisAlignment::Center);
    empty.treePart = core::TreePart::Empty;
    empty.key = owner_ + ":empty";
    return empty;
}

std::string TreeController::tabStopKey() const {
    return selection_.currentKey().empty() ? std::string{} : owner_ + ":item:" + selection_.currentKey();
}

core::Widget TreeController::buildRowContent(const VisibleRow& row) const {
    // Tree indentation belongs to the resolved row surface; TreeList keeps
    // its existing first-column geometry.
    core::Widget content = model_->buildRow(row.key, row.depth);
    if (isTreeList()) content.padding.left += kIndentStep * static_cast<float>(row.depth);
    return content;
}

void TreeController::rowClicked(const std::string& key, bool ctrl,
                                bool shift) {
    std::size_t index = 0;
    if (!indexOfKey(key, index) || !itemEnabled(index)) return;
    selection_.click(key, ctrl, shift);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

void TreeController::activate(const std::string& key) {
    std::size_t index = 0;
    if (onActivated && indexOfKey(key, index) && itemEnabled(index)) {
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
        // styledTree=true：chevron 带 treePart 标记，materializeVirtualRows
        // 对 TreeList 同样经 configureTreeParts 传递视口 showFocusRing/
        // controlSize（与 Tree 箭头同契约；TreeList chevron 是独立 Tab
        // 停靠点，漏传会在默认关环下聚焦不可见）。
        inner.children.push_back(makeLeadWidget(owner_, row, true));
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
    if (row.hasChildren) {
        widget.semanticsActions |= row.expanded ? accessibility::kActionCollapse : accessibility::kActionExpand;
    }
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
