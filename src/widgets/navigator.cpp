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

core::Widget makeDialog(core::Widget content, const Theme& theme,
                        std::string onDismiss, std::string key,
                        core::Size windowSize) {
    // 全屏 barrier：点击关闭；内容卡居中（宽 60%，最大 420）。
    const float cardWidth =
        std::min(420.0F, std::max(240.0F, windowSize.width * 0.6F));
    const core::Size cardSize{cardWidth, windowSize.height * 0.5F};
    const core::Offset cardOrigin{
        (windowSize.width - cardSize.width) * 0.5F,
        (windowSize.height - cardSize.height) * 0.5F};

    core::Widget barrier = core::makeContainerLeaf(
        windowSize.width, windowSize.height, core::EdgeInsets{},
        core::EdgeInsets{}, theme.barrier);
    barrier.onClick = std::move(onDismiss);
    barrier.semanticsRole = "dialog";
    barrier.semanticsActions = accessibility::kActionDismiss;
    barrier.key = key.empty() ? key : key + "-barrier";

    core::Widget card;
    card.type = core::WidgetType::Container;
    card.color = theme.surfaceElevated;
    card.radius = core::CornerRadius::all(12.0F);
    card.children.push_back(std::move(content));
    card = core::withStackPosition(std::move(card), cardOrigin);
    card.key = key.empty() ? key : key + "-card";

    // FocusScope 包住 dialog：Tab 遍历不逃出（plan §3.4）。
    core::Widget dialog = core::makeStack({std::move(barrier), std::move(card)});
    dialog.key = std::move(key);
    dialog.width = windowSize.width;
    dialog.height = windowSize.height;
    return core::makeFocusScope(std::move(dialog));
}

}  // namespace lumen::widgets
