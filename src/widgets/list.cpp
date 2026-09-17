// 集合控件：ListController 实现（见头注释）。

#include "lumen/widgets/list.h"

// 语义层共享实现（与 Tree 同契约）：键盘导航/滚动对齐/sink 接线/区间序列。
#include "collection_common.h"

namespace lumen::widgets {

void ListController::setItemCount(std::size_t count) {
    base_.setItemCount(count);
}

void ListController::setItemBuilder(
    std::function<core::Widget(std::size_t)> builder) {
    itemBuilder_ = std::move(builder);
}

void ListController::setKeyOf(std::function<std::string(std::size_t)> keyOf) {
    keyOf_ = std::move(keyOf);
}

void ListController::setEstimatedExtent(float extent) {
    base_.setEstimatedExtent(extent);
}

void ListController::setEmptyBuilder(std::function<core::Widget()> builder) {
    emptyBuilder_ = std::move(builder);
}

void ListController::setSelectionMode(SelectionMode mode) {
    selection_.setMode(mode);
}

void ListController::attach(app::AppShell& shell, std::string ownerKey) {
    shell_ = &shell;
    owner_ = std::move(ownerKey.empty() ? std::string("list") : ownerKey);
    base_.setEstimatedExtent(detail::kDefaultRowExtent);
    // 区间选择：行序由本控制器的 key 序列给出（方向无关的闭区间）。
    selection_.setKeySequence(
        [this](const std::string& from, const std::string& to) {
            return detail::closedKeyRange(
                base_.itemCount(),
                [this](std::size_t i) { return keyOf(i); }, from, to);
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
}

void ListController::scrollToKey(const std::string& key,
                                 ScrollAlignment align) {
    std::size_t index = 0;
    if (!indexOfKey(key, index)) {
        return;
    }
    detail::scrollToAligned(*this, index, align);
}

void ListController::setCurrentKey(const std::string& key, bool extend) {
    selection_.moveTo(key, extend);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

bool ListController::handleKey(core::Key key, core::KeyModifiers modifiers,
                               char keyChar) {
    return detail::handleCollectionKeys(
        shell_, owner_, selection_, *this,
        [this](std::size_t i) { return keyOf(i); }, key, modifiers, keyChar);
}

bool ListController::indexOfKey(const std::string& key,
                                std::size_t& index) const {
    return detail::findKeyIndex(
        base_.itemCount(),
        [this](std::size_t i) { return keyOf(i); }, key, index);
}

core::Widget ListController::buildEmpty() const {
    if (emptyBuilder_) {
        return emptyBuilder_();
    }
    core::Widget text = core::makeText("Empty");
    text.textStyle.color = core::Color{140, 140, 152, 255};
    return core::makeColumn({std::move(text)}, core::MainAxisAlignment::Center,
                            core::CrossAxisAlignment::Center);
}

std::string ListController::keyOf(std::size_t index) const {
    if (keyOf_) {
        return keyOf_(index);
    }
    return "i" + std::to_string(index);
}

void ListController::rowClicked(const std::string& key, bool ctrl,
                                bool shift) {
    selection_.click(key, ctrl, shift);
    if (shell_ != nullptr) {
        shell_->focus().setFocus(owner_ + ":item:" + key);
    }
    requestRebuild();
}

void ListController::activate(const std::string& key) {
    if (onActivated) {
        onActivated(key);
    }
}

void ListController::requestRebuild() {
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
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
                                    core::CrossAxisAlignment::Stretch,
                                    "listItem");
    row.children.push_back(itemBuilder_(index));
    return row;
}

}  // namespace lumen::widgets
