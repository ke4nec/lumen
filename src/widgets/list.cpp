// 集合控件：ListController 实现（见头注释）。

#include "lumen/widgets/list.h"

#include <algorithm>

#include "lumen/accessibility/semantics.h"

namespace lumen::widgets {

namespace {
// 未测量行的估算高度 = 视觉系统 §3.2 Medium 档行最小高。
constexpr float kDefaultRowExtent = 40.0F;
// 行水平内边距 = 视觉系统 §3.2 Medium 档（8/12/16）。
constexpr float kRowPaddingX = 12.0F;
}  // namespace

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
    base_.setEstimatedExtent(kDefaultRowExtent);
    // 区间选择：行序由本控制器的 key 序列给出（方向无关的闭区间）。
    selection_.setKeySequence(
        [this](const std::string& from, const std::string& to) {
            std::vector<std::string> keys;
            std::size_t begin = 0;
            std::size_t end = 0;
            if (!indexOfKey(from, begin) || !indexOfKey(to, end)) {
                return keys;
            }
            if (begin > end) {
                std::swap(begin, end);
            }
            for (std::size_t i = begin; i <= end; ++i) {
                keys.push_back(keyOf(i));
            }
            return keys;
        });
    selection_.onSelectionChanged = [this] { requestRebuild(); };
    selection_.onCurrentChanged = [this](const std::string&) {
        requestRebuild();
    };
    // 行点击（collection-design §6.5）：按名字前缀解析行 key 的单一
    // sink——虚拟化行不注册常驻 handler（滚动累积），恒 O(1) 分发。
    shell.controller().addRowClickSink([this](const std::string& onClick) {
        const std::string prefix = "list:" + owner_ + ":";
        if (onClick.rfind(prefix, 0) != 0) {
            return false;
        }
        const std::string key = onClick.substr(prefix.size());
        const auto modifiers =
            shell_ != nullptr
                ? shell_->controller().pointerModifiers()
                : core::kModifierNone;
        rowClicked(key, (modifiers & core::kModifierCtrl) != 0,
                   (modifiers & core::kModifierShift) != 0);
        return true;
    });
    shell.controller().addRowActivateSink(
        [this](const std::string& rowKey, const std::string&, bool) {
            const std::string prefix = owner_ + ":item:";
            if (rowKey.rfind(prefix, 0) != 0) {
                return false;
            }
            activate(rowKey.substr(prefix.size()));
            return true;
        });
}

void ListController::scrollToKey(const std::string& key,
                                 ScrollAlignment align) {
    std::size_t index = 0;
    if (!indexOfKey(key, index)) {
        return;
    }
    const float viewport = base_.scroll().viewportExtent();
    if (viewport <= 0.0F) {
        return;
    }
    if (align == ScrollAlignment::Visible) {
        base_.scrollToIndex(index, viewport);
        return;
    }
    const float top = base_.offsetOfIndex(index);
    const float extent = base_.extentOf(index);
    float target = 0.0F;
    switch (align) {
        case ScrollAlignment::Start:
            target = top;
            break;
        case ScrollAlignment::Center:
            target = top - (viewport - extent) * 0.5F;
            break;
        case ScrollAlignment::End:
        case ScrollAlignment::Visible:
            target = top + extent - viewport;
            break;
    }
    const float maxOffset = std::max(
        0.0F, base_.totalExtent() - viewport);
    base_.scroll().scrollTo(std::clamp(target, 0.0F, maxOffset));
}

void ListController::setCurrentKey(const std::string& key, bool extend) {
    selection_.moveTo(key, extend);
    shell_->focus().setFocus(owner_ + ":item:" + key);
    requestRebuild();
}

bool ListController::handleKey(core::Key key, core::KeyModifiers modifiers,
                               char keyChar) {
    const std::size_t count = base_.itemCount();
    if (count == 0) {
        return false;
    }
    std::size_t current = 0;
    const bool hasCurrent =
        !selection_.currentKey().empty() &&
        indexOfKey(selection_.currentKey(), current);
    const bool ctrl = (modifiers & core::kModifierCtrl) != 0;
    const bool shift = (modifiers & core::kModifierShift) != 0;

    // Ctrl+A：Extended 全选；Single 选中 current（collection-design §6.3）。
    if (ctrl && (keyChar == 'a' || keyChar == 'A')) {
        if (selection_.mode() == SelectionMode::Extended) {
            std::vector<std::string> all;
            all.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                all.push_back(keyOf(i));
            }
            selection_.setSelected(std::move(all));
        } else if (hasCurrent) {
            selection_.setSelected({keyOf(current)});
        }
        requestRebuild();
        return true;
    }

    const auto move = [this, shift](std::size_t target) {
        const std::string key = keyOf(target);
        selection_.moveTo(key, shift);
        shell_->focus().setFocus(owner_ + ":item:" + key);
        scrollToKey(key, ScrollAlignment::Visible);
        requestRebuild();
    };

    switch (key) {
        case core::Key::Up:
            move(hasCurrent && current > 0 ? current - 1 : 0);
            return true;
        case core::Key::Down:
            move(hasCurrent ? std::min(current + 1, count - 1) : 0);
            return true;
        case core::Key::Home:
            move(0);
            scrollToKey(keyOf(0), ScrollAlignment::Start);
            return true;
        case core::Key::End:
            move(count - 1);
            scrollToKey(keyOf(count - 1), ScrollAlignment::End);
            return true;
        case core::Key::PageUp:
        case core::Key::PageDown: {
            const float extent =
                std::max(1.0F, base_.extentOf(hasCurrent ? current : 0));
            const std::size_t step = std::max(
                1U, static_cast<unsigned>(
                        base_.scroll().viewportExtent() / extent));
            if (key == core::Key::PageUp) {
                move(hasCurrent && current > step ? current - step : 0);
            } else {
                move(hasCurrent ? std::min(current + step, count - 1)
                               : std::min(step, count - 1));
            }
            return true;
        }
        default:
            // Enter/Tab/其余键不在此消费：Enter 走激活路径（避免双重激活），
            // Tab 留给焦点遍历。
            return false;
    }
}

bool ListController::indexOfKey(const std::string& key,
                                std::size_t& index) const {
    const std::size_t count = base_.itemCount();
    for (std::size_t i = 0; i < count; ++i) {
        if (keyOf(i) == key) {
            index = i;
            return true;
        }
    }
    return false;
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
    shell_->focus().setFocus(owner_ + ":item:" + key);
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
    row.type = core::WidgetType::Row;
    row.key = owner_ + ":item:" + key;
    row.collectionRow = true;
    row.selected = selection_.isSelected(key);
    // onClick 是行身份（sink 按前缀解析；空 = 不可聚焦/激活）。
    row.onClick = "list:" + owner_ + ":" + key;
    row.crossAxis = core::CrossAxisAlignment::Stretch;
    row.padding = core::EdgeInsets::symmetric(kRowPaddingX, 0.0F);
    row.semanticsRole = "listItem";
    row.semanticsActions =
        accessibility::kActionFocus | accessibility::kActionActivate;
    row.children.push_back(itemBuilder_(index));
    return row;
}

}  // namespace lumen::widgets
