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
                        core::Size windowSize) {
    // 全屏 barrier：点击关闭；内容卡居中。§8.2：surfaceElevated +
    // radius 12 + 1px borderDefault + L3 阴影；宽度 240–420（窗口可用宽
    // 优先），高度受窗口可用高度约束（边距 ≥16）；整体 padding 24 由
    // helper 统一施加（内容不再自带边距，避免双重 padding）。
    const style::DialogTokens& tokens = theme.dialog;
    const float windowMargin = 16.0F;
    const float maxCardWidth =
        std::max(tokens.minWidth, windowSize.width - 2.0F * windowMargin);
    const float cardWidth =
        std::min(tokens.maxWidth,
                 std::max(tokens.minWidth,
                          std::min(windowSize.width * tokens.widthRatio,
                                   maxCardWidth)));

    core::Widget barrier = core::makeContainerLeaf(
        windowSize.width, windowSize.height, core::EdgeInsets{},
        core::EdgeInsets{}, tokens.scrim);
    barrier.onClick = std::move(onDismiss);
    barrier.semanticsRole = "dialog";
    barrier.semanticsActions = accessibility::kActionDismiss;
    barrier.key = key.empty() ? key : key + "-barrier";

    // 内容统一 padding（tokens.padding = 24）。
    core::Widget padded = core::makeContainer(
        std::move(content), std::nullopt, std::nullopt, tokens.padding);
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
