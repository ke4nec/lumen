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
    // 全屏 barrier：点击关闭；内容卡居中。尺寸/颜色/圆角全部来自
    // DialogTokens（visual-system §7.4；elevation 渲染能力冻结，先用表
    // 面层级表达）。
    const style::DialogTokens& tokens = theme.dialog;
    const float cardWidth =
        std::min(tokens.maxWidth,
                 std::max(tokens.minWidth,
                          windowSize.width * tokens.widthRatio));
    const core::Size cardSize{cardWidth, windowSize.height * tokens.heightRatio};
    const core::Offset cardOrigin{
        (windowSize.width - cardSize.width) * 0.5F,
        (windowSize.height - cardSize.height) * 0.5F};

    core::Widget barrier = core::makeContainerLeaf(
        windowSize.width, windowSize.height, core::EdgeInsets{},
        core::EdgeInsets{}, tokens.scrim);
    barrier.onClick = std::move(onDismiss);
    barrier.semanticsRole = "dialog";
    barrier.semanticsActions = accessibility::kActionDismiss;
    barrier.key = key.empty() ? key : key + "-barrier";

    core::Widget card;
    card.type = core::WidgetType::Container;
    card.color = tokens.surface;
    card.radius = core::CornerRadius::all(tokens.radius);
    // 内容自带内边距（DialogTokens.padding 供应用侧组合使用，避免双重
    // padding）。
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
