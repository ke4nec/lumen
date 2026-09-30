// 集合控件：ListController 实现（见头注释）。

#include "lumen/widgets/list.h"

#include <optional>

// 语义层共享实现（与 Tree 同契约）：键盘导航/滚动对齐/sink 接线/区间序列。
#include "collection_common.h"

namespace lumen::widgets {

ListController::ListController() {
    base_.setEstimatedExtent(detail::kDefaultRowExtent);
}

void ListController::setItemCount(std::size_t count) {
    base_.setItemCount(count);
    contentEnabled_.clear();
    requestRebuild();
}

void ListController::setItemBuilder(
    std::function<core::Widget(std::size_t)> builder) {
    itemBuilder_ = std::move(builder);
    contentEnabled_.clear();
    requestRebuild();
}

void ListController::setKeyOf(std::function<std::string(std::size_t)> keyOf) {
    keyOf_ = std::move(keyOf);
    contentEnabled_.clear();
    requestRebuild();
}

void ListController::setEnabledOf(std::function<bool(std::size_t)> enabledOf) {
    enabledOf_ = std::move(enabledOf);
    contentEnabled_.clear();
    requestRebuild();
}

bool ListController::itemEnabled(std::size_t index) const {
    if (index >= itemCount() || (enabledOf_ && !enabledOf_(index))) return false;
    const auto found = contentEnabled_.find(keyOf(index));
    return found == contentEnabled_.end() || found->second;
}

void ListController::setEstimatedExtent(float extent) {
    base_.setEstimatedExtent(extent);
}

void ListController::setEmptyBuilder(std::function<core::Widget()> builder) {
    emptyBuilder_ = std::move(builder);
    requestRebuild();
}

void ListController::setSelectionMode(SelectionMode mode) {
    selection_.setMode(mode);
}

void ListController::attach(app::AppShell& shell, std::string ownerKey) {
    shell_ = &shell;
    owner_ = std::move(ownerKey.empty() ? std::string("list") : ownerKey);
    // 区间选择：行序由本控制器的 key 序列给出（方向无关的闭区间）。
    selection_.setKeySequence(
        [this](const std::string& from, const std::string& to) {
            return detail::closedKeyRange(
                base_.itemCount(),
                [this](std::size_t i) { return keyOf(i); }, from, to,
                [this](std::size_t i) { return itemEnabled(i); });
        });
    selection_.onSelectionChanged = [this] { requestRebuild(); };
    selection_.onCurrentChanged = [this](const std::string&) {
        requestRebuild();
    };
    // 行点击（collection-design §6.5）：按名字前缀解析行 key 的单一
    // sink——虚拟化行不注册常驻 handler（滚动累积），恒 O(1) 分发。
    shell.controller().addRowClickSink([this](const std::string& onClick) {
        return detail::dispatchRowClick(
            shell_, onClick, "list:" + owner_ + ":",
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
    // M15：行拖拽重排（drag-drop-design §4）。arm 认领行（触摸不认领，
    // 行整体拖拽让位滚动）；会话 sink 按 source key 前缀过滤（多集合
    // 共存时各列表只消费自己的会话）。
    shell.controller().addDragArmSink(
        [this](const std::vector<const core::RenderNode*>& chain,
               core::PointerDevice, core::DragSourceClaim& claim) {
            if (!reorderEnabled_) {
                return false;
            }
            for (const core::RenderNode* node : chain) {
                if (node->onClick.rfind(clickPrefix(), 0) != 0) {
                    continue;
                }
                const std::string key =
                    node->onClick.substr(clickPrefix().size());
                std::size_t index = 0;
                if (!key.empty() && indexOfKey(key, index) &&
                    itemEnabled(index)) {
                    claim.key = node->key;
                    claim.identity = node->identity;
                    claim.touchAllowed = false;
                    return true;
                }
                return false;
            }
            return false;
        });
    shell.controller().addDragSessionSink(
        [this](core::DragPhase phase, core::Offset position,
               const std::vector<const core::RenderNode*>&,  // 命中链按主树重解析
               const std::string& sourceKey, const std::string&) {
            dragSession(phase, position, sourceKey);
        });
}

void ListController::scrollToKey(const std::string& key,
                                 ScrollAlignment align) {
    std::size_t index = 0;
    if (!indexOfKey(key, index)) {
        return;
    }
    detail::scrollToAligned(*this, index, align);
    requestRebuild();
}

void ListController::setCurrentKey(const std::string& key, bool extend) {
    std::size_t index = 0;
    if (!indexOfKey(key, index) || !itemEnabled(index)) return;
    selection_.moveTo(key, extend);
    scrollToKey(key, ScrollAlignment::Visible);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

bool ListController::handleKey(core::Key key, core::KeyModifiers modifiers,
                               char keyChar) {
    if (shell_ != nullptr) {
        const auto* view = core::findNodeByKey(shell_->root(), owner_);
        if (view != nullptr && !view->enabled) return false;
    }
    return detail::handleCollectionKeys(
        shell_, owner_, selection_, *this,
        [this](std::size_t i) { return keyOf(i); }, key, modifiers, keyChar,
        [this](std::size_t i) { return itemEnabled(i); });
}

bool ListController::indexOfKey(const std::string& key,
                                std::size_t& index) const {
    return detail::findKeyIndex(
        base_.itemCount(),
        [this](std::size_t i) { return keyOf(i); }, key, index);
}

core::Widget ListController::buildEmpty() const {
    std::vector<core::Widget> children;
    if (emptyBuilder_) {
        children.push_back(emptyBuilder_());
    } else {
        auto icon = core::makeIcon(core::IconId::Folder);
        icon.listPart = core::ListPart::EmptyIcon;
        auto text = core::makeText("No items");
        text.listPart = core::ListPart::EmptyText;
        children.push_back(std::move(icon));
        children.push_back(std::move(text));
    }
    auto empty = core::makeColumn(std::move(children),
        core::MainAxisAlignment::Center, core::CrossAxisAlignment::Center);
    empty.listPart = core::ListPart::Empty;
    empty.key = owner_ + ":empty";
    return empty;
}

std::string ListController::tabStopKey() const {
    return selection_.currentKey().empty() ? std::string{}
        : owner_ + ":item:" + selection_.currentKey();
}

std::string ListController::keyOf(std::size_t index) const {
    if (keyOf_) {
        return keyOf_(index);
    }
    return "i" + std::to_string(index);
}

void ListController::rowClicked(const std::string& key, bool ctrl,
                                bool shift) {
    std::size_t index = 0;
    if (!indexOfKey(key, index) || !itemEnabled(index)) return;
    selection_.click(key, ctrl, shift);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

void ListController::activate(const std::string& key) {
    std::size_t index = 0;
    if (onActivated && indexOfKey(key, index) && itemEnabled(index)) {
        onActivated(key);
    }
}

void ListController::requestRebuild() {
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

// --- M15：行拖拽重排 ---

void ListController::setReorderable(bool enabled) {
    reorderEnabled_ = enabled;
}

void ListController::dragSession(core::DragPhase phase, core::Offset position,
                                 const std::string& sourceKey) {
    // 会话归属：sourceKey 是行节点 key（owner+":item:"+行 key）。
    const std::string itemPrefix = owner_ + ":item:";
    if (sourceKey.rfind(itemPrefix, 0) != 0) {
        return;
    }
    const std::string rowKey = sourceKey.substr(itemPrefix.size());
    std::size_t fromIndex = 0;
    // M15 review 修复：Cancel 无条件清理（此前源行因数据变更消失时
    // 提前 return，endDragSession 永不执行——ghost overlay 永久滞留）；
    // Drop 同样先清理会话，源行存在才提交。
    if (phase == core::DragPhase::Cancel) {
        endDragSession();
        return;
    }
    if (!indexOfKey(rowKey, fromIndex)) {
        // 会话开始后数据变更使源行消失：Start/Move 不响应；Drop 清理
        // 不提交。
        if (phase == core::DragPhase::Drop) {
            endDragSession();
        }
        return;
    }
    switch (phase) {
        case core::DragPhase::Start:
            dragActive_ = true;
            dragFromIndex_ = fromIndex;
            dragInsertIndex_ = fromIndex;
            dragPointer_ = position;
            if (shell_ != nullptr) {
                // 非模态视觉 overlay：不取消活动指针（会话仍在进行）。
                shell_->setVisualOverlayBuilder(
                    [this]() -> std::optional<core::Widget> {
                        if (!dragActive_) {
                            return std::nullopt;
                        }
                        return buildDragOverlay();
                    });
            }
            requestRebuild();
            break;
        case core::DragPhase::Move: {
            dragPointer_ = position;
            // 会话 sink 收到的命中链来自事件树——ghost overlay 活跃期即
            // overlay 树；落点必须按主树解析。
            if (shell_ == nullptr) {
                break;
            }
            std::vector<const core::RenderNode*> mainChain;
            (void)core::hitTestChain(shell_->root(), position, mainChain);
            for (const core::RenderNode* node : mainChain) {
                if (node->onClick.rfind(clickPrefix(), 0) != 0) {
                    continue;
                }
                const std::string targetKey =
                    node->onClick.substr(clickPrefix().size());
                std::size_t target = 0;
                if (!indexOfKey(targetKey, target)) {
                    break;
                }
                if (target == fromIndex) {
                    dragInsertIndex_ = fromIndex;  // 悬停源行自身：无位移。
                    break;
                }
                // 行下半落点 = 插入其后（间隙 +1）；上边界按绝对原点。
                const core::Offset origin =
                    core::absoluteOffset(shell_->root(), node->key);
                const bool belowMiddle =
                    position.y > origin.y + node->size.height * 0.5F;
                dragInsertIndex_ = belowMiddle ? target + 1 : target;
                break;
            }
            requestRebuild();  // ghost 跟手 + 指示线更新。
            break;
        }
        case core::DragPhase::Drop: {
            const std::size_t gap = dragInsertIndex_;
            endDragSession();
            if (gap == fromIndex) {
                break;
            }
            // 先移除后插入：间隙在源行之后时最终落点行号 -1。
            const std::size_t toIndex = gap > fromIndex ? gap - 1 : gap;
            if (onReorder != nullptr && toIndex != fromIndex) {
                onReorder(fromIndex, toIndex);
            }
            break;
        }
        case core::DragPhase::Cancel:
            endDragSession();
            break;
    }
}

void ListController::endDragSession() {
    dragActive_ = false;
    if (shell_ != nullptr) {
        shell_->clearVisualOverlay();
    }
    requestRebuild();
}

core::Widget ListController::buildDragOverlay() const {
    if (shell_ == nullptr) {
        return core::Widget{};
    }
    const style::Theme& theme = shell_->theme();
    const style::DragDropTokens& tokens = theme.dragDrop;
    const core::Size view = shell_->view();

    // 拖拽 ghost：源行内容 + DragDropTokens 表面/描边，抓取偏移跟随
    // 指针。纯视觉（无 barrier/FocusScope）——拖放会话本身接管输入。
    core::Widget content = itemBuilder_ != nullptr
                               ? itemBuilder_(dragFromIndex_)
                               : core::makeText("");
    content.flex = 0.0F;
    core::Widget ghost;
    ghost.type = core::WidgetType::Container;
    ghost.color = tokens.ghostSurface;
    ghost.radius = core::CornerRadius::all(theme.metrics.cardRadius);
    ghost.elevation = 2.0F;  // L2：与 Dropdown/Tooltip 同层（§4.5）。
    ghost.styleOverrides.border = tokens.ghostBorder;
    ghost.styleOverrides.borderWidth = theme.metrics.controlBorderWidth;
    ghost.padding = core::EdgeInsets::symmetric(8.0F, 4.0F);
    ghost.children.push_back(std::move(content));
    ghost = core::withStackPosition(
        std::move(ghost),
        core::Offset{dragPointer_.x + tokens.ghostGrabOffsetX,
                     dragPointer_.y - tokens.ghostGrabOffsetY});
    ghost.key = owner_ + ":drag-ghost";

    // 插入指示线：目标行上边界（间隙 0..count；count = 内容尾部）。
    // 视口坐标 = 视口原点 + offsetOfIndex(间隙) - 滚动偏移。
    core::Widget indicator;
    indicator.type = core::WidgetType::Container;
    indicator.color = tokens.dropIndicator;
    indicator.height = tokens.indicatorThickness;
    if (const core::RenderNode* viewport =
            core::findNodeByKey(shell_->root(), owner_)) {
        indicator.width = viewport->size.width;
        const core::Offset origin = core::absoluteOffset(shell_->root(), owner_);
        const float scroll = base_.scrollController() != nullptr
                                 ? base_.scrollController()->offset()
                                 : 0.0F;
        const std::size_t gap =
            std::min(dragInsertIndex_, base_.itemCount());
        const float boundaryY =
            origin.y + base_.offsetOfIndex(gap) - scroll;
        indicator = core::withStackPosition(
            std::move(indicator), core::Offset{origin.x, boundaryY});
    }
    indicator.key = owner_ + ":drag-indicator";

    core::Widget overlay = core::makeStack(
        {std::move(indicator), std::move(ghost)},
        core::StackAlignment::TopLeft);
    overlay.key = owner_ + ":drag-overlay";
    overlay.width = view.width;
    overlay.height = view.height;
    return overlay;
}

// --- VirtualListSource：几何全部委托 M3 控制器（extent 缓存/锚点稳定/
// 滚动语义见 virtual_list.cpp） ---

std::size_t ListController::itemCount() const { return base_.itemCount(); }

float ListController::estimatedExtent() const {
    return base_.estimatedExtent();
}

float ListController::extentOf(std::size_t index) const {
    return base_.extentOf(index);
}

float ListController::scrollOffset() const { return base_.scrollOffset(); }

float ListController::totalExtent() const { return base_.totalExtent(); }

float ListController::offsetOfIndex(std::size_t index) const {
    return base_.offsetOfIndex(index);
}

std::pair<std::size_t, std::size_t> ListController::visibleRange(
    float viewportExtent, float cacheExtent) const {
    return base_.visibleRange(viewportExtent, cacheExtent);
}

void ListController::noteExtent(std::size_t index, float extent) const {
    base_.noteExtent(index, extent);
}

void ListController::updateViewport(float viewportExtent,
                                    float contentPadding) const {
    base_.updateViewport(viewportExtent, contentPadding);
}

core::Widget ListController::buildItem(std::size_t index) const {
    if (itemBuilder_ == nullptr) {
        return core::Widget{};
    }
    const std::string key = keyOf(index);
    core::Widget row;
    // onClick 是行身份（attach 的 sink 按前缀解析；空 = 不可聚焦/激活）。
    detail::applyCollectionRowShell(row, owner_, key,
                                    selection_.isSelected(key),
                                    "list:" + owner_ + ":" + key,
                                    core::CrossAxisAlignment::Center,
                                    "listItem");
    row.padding = {};
    row.listPart = index + 1 == itemCount() ? core::ListPart::LastRow : core::ListPart::Row;
    auto content = itemBuilder_(index);
    if (content.enabled) contentEnabled_.erase(key);
    else contentEnabled_[key] = false;
    row.enabled = itemEnabled(index);
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
