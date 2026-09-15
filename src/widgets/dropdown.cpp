#include "lumen/widgets/dropdown.h"

#include <algorithm>

#include "lumen/accessibility/semantics.h"

namespace lumen::widgets {

namespace {

// 菜单与锚点的间隙 / 选项行间距。
constexpr float kMenuGapPx = 4.0F;
constexpr float kOptionGapPx = 4.0F;

// 菜单估算高度（向上翻折判断；实际高度由布局决定，钳制到视口内）。
// M11 review：行高取 metrics 与排版字号的最大值——fontScale 极端时
// 文本行高可超过控件最小高度。
float estimateMenuHeight(const style::Theme& theme,
                         std::size_t optionCount) {
    const float minHeight = theme.metrics.minHeight[theme.metrics.baseIndex];
    const float textRow = theme.typography.label.fontSize * 1.2F + 8.0F;
    const float row = std::max(minHeight, textRow) + kOptionGapPx;
    return static_cast<float>(optionCount) * row + kMenuGapPx * 2.0F;
}

}  // namespace

DropdownController::DropdownController(std::vector<Option> options,
                                       std::string defaultValue)
    : options_(std::move(options)), value_(std::move(defaultValue)) {
    setValue(value_);
}

void DropdownController::setValue(std::string value) {
    value_ = std::move(value);
    highlight_ = 0;
    for (std::size_t i = 0; i < options_.size(); ++i) {
        if (options_[i].value == value_) {
            highlight_ = i;
            break;
        }
    }
}

std::string DropdownController::optionKey(std::size_t index) const {
    return dropdownKey_ + "-opt-" + std::to_string(index);
}

void DropdownController::open(app::AppShell& shell,
                              const std::string& dropdownKey) {
    if (options_.empty() || open_) {
        return;
    }
    dropdownKey_ = dropdownKey;
    // 锚定值行（主树根相对矩形；关闭后焦点恢复目标）。
    const core::RenderNode* row =
        core::findNodeByKey(shell.root(), dropdownKey_);
    if (row == nullptr) {
        return;
    }
    anchor_ = core::Rect{core::absoluteOffset(shell.root(), dropdownKey_),
                         row->size};
    // M11 review：值同步以渲染值行文本为准（bind 值经 applyBinds 写入；
    // 控制器内部 value_ 只是打开时的高亮依据，消除双源漂移）。
    setValue(row->text.empty() ? value_ : row->text);
    open_ = true;
    registerHandlers(shell);
    shell.setOverlay(buildOverlay(shell.theme(), shell.view()));
    shell.rebuildIfDirty();  // overlay 布局落地，供焦点定位。
    if (shell.overlayRoot() != nullptr) {
        if (const core::RenderNode* option =
                core::findNodeByKey(*shell.overlayRoot(),
                                    optionKey(highlight_))) {
            shell.controller().focusNode(*option);
        }
    }
}

void DropdownController::close(app::AppShell& shell) {
    if (!open_) {
        return;
    }
    open_ = false;
    // 注入的 handler 随菜单移除（handler 名含 dropdownKey，不与应用冲突）。
    shell.handlers().erase(dropdownKey_ + "-dismiss");
    for (std::size_t i = 0; i < options_.size(); ++i) {
        shell.handlers().erase(optionKey(i));
    }
    shell.clearOverlay();
    // 焦点恢复到值行（消失则交给应用的既有恢复规则）。
    shell.rebuildIfDirty();
    if (const core::RenderNode* row =
            core::findNodeByKey(shell.root(), dropdownKey_)) {
        shell.controller().focusNode(*row);
    }
}

void DropdownController::registerHandlers(app::AppShell& shell) {
    shell.handlers()[dropdownKey_ + "-dismiss"] = [this, &shell]() {
        close(shell);
    };
    for (std::size_t i = 0; i < options_.size(); ++i) {
        shell.handlers()[optionKey(i)] = [this, i, &shell]() {
            select(shell, i);
        };
    }
}

void DropdownController::refreshOverlay(app::AppShell& shell) {
    if (!open_) {
        return;
    }
    shell.setOverlay(buildOverlay(shell.theme(), shell.view()));
    shell.rebuildIfDirty();
    if (shell.overlayRoot() != nullptr) {
        if (const core::RenderNode* option =
                core::findNodeByKey(*shell.overlayRoot(),
                                    optionKey(highlight_))) {
            shell.controller().focusNode(*option);
        }
    }
}

void DropdownController::select(app::AppShell& shell, std::size_t index) {
    if (index >= options_.size()) {
        return;
    }
    const std::string selected = options_[index].value;
    setValue(selected);
    close(shell);
    if (onSelected) {
        onSelected(value_);
    }
}

bool DropdownController::handleKey(app::AppShell& shell, core::Key key) {
    if (!open_ || options_.empty()) {
        return false;
    }
    switch (key) {
        case core::Key::Down:
            highlight_ = (highlight_ + 1) % options_.size();
            refreshOverlay(shell);
            return true;
        case core::Key::Up:
            highlight_ = (highlight_ + options_.size() - 1) % options_.size();
            refreshOverlay(shell);
            return true;
        case core::Key::Enter:
            select(shell, highlight_);
            return true;
        case core::Key::Escape:
            close(shell);
            return true;
        default:
            return false;
    }
}

core::Widget DropdownController::buildOverlay(const style::Theme& theme,
                                              core::Size view) const {
    // 全窗 barrier：点击关闭（同 makeDialog 模态模式）。
    core::Widget barrier = core::makeContainerLeaf(
        view.width, view.height, core::EdgeInsets{}, core::EdgeInsets{},
        theme.dialog.scrim);
    barrier.onClick = dropdownKey_ + "-dismiss";
    barrier.semanticsRole = "dialog";
    barrier.semanticsActions = accessibility::kActionDismiss;
    barrier.key = dropdownKey_ + "-menu-barrier";

    // 菜单定位：值行下方；视口不足翻向上方；水平/垂直钳制在视口内。
    const float menuWidth = std::max(anchor_.size.width, 120.0F);
    const float menuHeight =
        estimateMenuHeight(theme, options_.size());
    core::Offset menuOrigin{anchor_.origin.x, anchor_.origin.y +
                                                   anchor_.size.height +
                                                   kMenuGapPx};
    if (menuOrigin.y + menuHeight > view.height) {
        menuOrigin.y = std::max(
            0.0F, anchor_.origin.y - kMenuGapPx - menuHeight);
    }
    menuOrigin.x =
        std::clamp(menuOrigin.x, 0.0F, std::max(0.0F, view.width - menuWidth));

    std::vector<core::Widget> buttons;
    buttons.reserve(options_.size());
    for (std::size_t i = 0; i < options_.size(); ++i) {
        core::Widget option = core::makeButton(options_[i].label);
        option.buttonVariant = i == highlight_
                                   ? core::ButtonVariant::Filled
                                   : (options_[i].value == value_
                                          ? core::ButtonVariant::Tonal
                                          : core::ButtonVariant::Ghost);
        option.onClick = optionKey(i);
        option.key = optionKey(i);
        option.width = menuWidth;
        buttons.push_back(std::move(option));
    }
    core::Widget menuColumn = core::makeColumn(
        std::move(buttons), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Start, kOptionGapPx,
        core::EdgeInsets{kMenuGapPx, kMenuGapPx, kMenuGapPx, kMenuGapPx});
    core::Widget menu;
    menu.type = core::WidgetType::Container;
    menu.color = theme.colors.surfaceElevated;
    menu.radius = core::CornerRadius::all(theme.metrics.cardRadius);
    menu.elevation = theme.dialog.elevation;
    menu.width = menuWidth;
    menu.children.push_back(std::move(menuColumn));
    menu = core::withStackPosition(std::move(menu), menuOrigin);
    menu.key = dropdownKey_ + "-menu";

    core::Widget overlay = core::makeStack({std::move(barrier),
                                            std::move(menu)});
    overlay.key = dropdownKey_ + "-menu-root";
    overlay.width = view.width;
    overlay.height = view.height;
    // FocusScope：Tab 遍历不逃出菜单（plan §3.4 modal 键域）。
    return core::makeFocusScope(std::move(overlay));
}

}  // namespace lumen::widgets
