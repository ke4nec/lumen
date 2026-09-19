#include "lumen/widgets/navigator.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "lumen/accessibility/semantics.h"

namespace lumen::widgets {

NavigatorController::NavigatorController(std::string initialRoute) {
    stack_.push_back(std::move(initialRoute));
}

void NavigatorController::push(const std::string& route) {
    if (stack_.empty() || stack_.back() != route) {
        stack_.push_back(route);
    }
}

bool NavigatorController::pop() {
    if (stack_.size() <= 1) {
        return false;
    }
    stack_.pop_back();
    return true;
}

void NavigatorController::popToRoot() {
    if (stack_.empty()) {
        return;
    }
    stack_.resize(1);
}

const std::string& NavigatorController::current() const {
    static const std::string kEmpty;
    return stack_.empty() ? kEmpty : stack_.back();
}

const std::vector<std::string>& NavigatorController::stack() const {
    return stack_;
}

bool NavigatorController::handleBack(bool modalOpen) {
    // 统一规则（plan §3.4）：modal 最优先；其后路由栈 pop；根路由交给
    // 应用/平台（返回 false = 未消费，如退出应用）。
    if (modalOpen) {
        return true;
    }
    return pop();
}

core::Widget makeDialog(core::Widget content, const style::Theme& theme,
                        std::string onDismiss, std::string key,
                        core::Size windowSize, float scrimRadius) {
    return makeDialog(std::move(content), core::Widget{}, theme,
                      std::move(onDismiss), std::move(key), windowSize, 0.0F, scrimRadius);
}

core::Widget makeDialog(core::Widget body, core::Widget actions,
                        const style::Theme& theme, std::string onDismiss,
                        std::string key, core::Size windowSize,
                        float bodyScrollOffset, float scrimRadius) {
    // 全屏 barrier：点击关闭；内容卡居中。§8.2：surfaceElevated +
    // radius 12 + 1px borderDefault + L3 阴影；宽度 240–420（窗口可用宽
    // 优先），高度受窗口可用高度约束（边距 ≥16）；整体 padding 24 由
    // helper 统一施加（内容不再自带边距，避免双重 padding）。
    const style::DialogTokens& tokens = theme.dialog;
    const float windowMargin = 16.0F;
    const float availableWidth = std::max(0.0F, windowSize.width - 2.0F * windowMargin);
    const float cardWidth = std::min(availableWidth,
        std::clamp(windowSize.width * tokens.widthRatio, tokens.minWidth, tokens.maxWidth));

    core::Widget barrier = core::makeContainerLeaf(
        windowSize.width, windowSize.height, core::EdgeInsets{},
        core::EdgeInsets{}, tokens.scrim);
    barrier.onClick = std::move(onDismiss);
    barrier.radius = core::CornerRadius::all(std::max(0.0F, scrimRadius));
    barrier.semanticsRole = "dialog";
    barrier.semanticsActions = accessibility::kActionDismiss;
    barrier.key = key.empty() ? key : key + "-barrier";

    core::Widget scroll = core::makeScrollView(std::move(body), key + "-body-scroll");
    scroll.scrollOffset = bodyScrollOffset;
    scroll.showScrollbar = true;
    scroll.flex = 1.0F;
    scroll.shrinkWrap = true;
    std::vector<core::Widget> sections;
    sections.push_back(std::move(scroll));
    const bool hasActions = actions.type != core::WidgetType::Container ||
                            !actions.children.empty() || !actions.text.empty() ||
                            !actions.key.empty();
    if (hasActions) {
        // 对话框是键盘重度表面（visual-system §6.1）：动作按钮是 Enter/Esc
        // 的键盘激活目标，默认关闭的焦点环在这里显式开启；正文内容保持
        // 控件自身设置。
        const auto enableFocusRing = [](auto&& self, core::Widget& widget) -> void {
            widget.showFocusRing = true;
            for (auto& child : widget.children) self(self, child);
        };
        enableFocusRing(enableFocusRing, actions);
        sections.push_back(std::move(actions));
    }
    core::Widget padded = core::makeColumn(std::move(sections),
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
        24.0F, tokens.padding);
    core::Widget card;
    card.type = core::WidgetType::Container;
    card.color = tokens.surface;
    card.radius = core::CornerRadius::all(tokens.radius);
    card.styleOverrides.border = theme.colors.borderDefault;
    card.styleOverrides.borderWidth = theme.metrics.controlBorderWidth;
    card.elevation = tokens.elevation;
    // 宽度显式（240–420 / 窗口可用宽优先）；高度内容驱动（Stack 居中
    // 按实际高度对齐，超长内容受根约束压缩）。
    card.width = cardWidth;
    card.margin = core::EdgeInsets::all(windowMargin);
    card.children.push_back(std::move(padded));
    card.key = key.empty() ? key : key + "-card";

    // FocusScope 包住 dialog：Tab 遍历不逃出（plan §3.4）。卡片经
    // StackAlignment::Center 居中（布局期真实居中，不再按估算尺寸定位）。
    core::Widget dialog = core::makeStack({std::move(barrier), std::move(card)},
                                          core::StackAlignment::Center);
    dialog.key = std::move(key);
    dialog.width = windowSize.width;
    dialog.height = windowSize.height;
    return core::makeFocusScope(std::move(dialog));
}

}  // namespace lumen::widgets
