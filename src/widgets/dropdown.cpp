#include "lumen/widgets/dropdown.h"

#include <algorithm>

#include "lumen/accessibility/semantics.h"

namespace lumen::widgets {

namespace {

// 菜单与锚点的间隙 / 窗口安全边距 / 菜单最大高度（§6.7）。
constexpr float kMenuGapPx = 4.0F;
constexpr float kWindowMarginPx = 8.0F;
constexpr float kMenuMaxHeightPx = 320.0F;
constexpr float kMenuInnerPaddingPx = 4.0F;

// 选项行高：metrics 最小高度与排版行高的最大值（fontScale 极端时文本
// 行高可超过控件最小高度；M11 review 保留）。
float optionRowHeight(const style::Theme& theme) {
    const float minHeight = theme.metrics.minHeight[theme.metrics.baseIndex];
    const float textRow = theme.typography.label.lineHeight > 0.0F
                             ? theme.typography.label.fontSize *
                                   theme.typography.label.lineHeight
                             : theme.typography.label.fontSize * 1.2F;
    return std::max(minHeight, textRow + 2.0F * theme.metrics.controlPaddingY[theme.metrics.baseIndex]);
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
                              const std::string& dropdownKey,
                              const style::Theme* anchorTheme) {
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
    if (!row->text.empty()) {
        for (const auto& option : options_) {
            if (option.value == row->text || option.label == row->text) {
                setValue(option.value);
                break;
            }
        }
    }
    open_ = true;
    explicitTheme_ = anchorTheme != nullptr;
    overlayTheme_ =
        anchorTheme != nullptr
            ? std::optional<style::Theme>(*anchorTheme)
            : std::nullopt;
    registerHandlers(shell);
    scrollOffset_.reset();
    const auto scroll = [this, &shell](const core::RenderNode& root,
                                       const core::RenderNode* hit, float deltaY) {
        if (!hit || hit->key != dropdownKey_ + "-menu-scroll") return false;
        const auto* viewport = core::findNodeByKey(root, hit->key);
        if (!viewport) return false;
        const float next = std::clamp(viewport->scrollOffset + deltaY,
                                     0.0F, viewport->scrollExtent);
        if (next == viewport->scrollOffset) return false;
        scrollOffset_ = next;
        shell.markDirty();
        return true;
    };
    shell.setOverlayBuilder([this, &shell]() -> std::optional<core::Widget> {
        const auto* current = core::findNodeByKey(shell.root(), dropdownKey_);
        if (!open_ || !current || !current->enabled) {
            open_ = false;
            shell.handlers().erase(dropdownKey_ + "-dismiss");
            for (std::size_t i = 0; i < options_.size(); ++i) {
                shell.handlers().erase(optionKey(i));
            }
            return std::nullopt;
        }
        anchor_ = {core::absoluteOffset(shell.root(), dropdownKey_), current->size};
        if (!explicitTheme_) overlayTheme_ = shell.effectiveThemeForKey(dropdownKey_);
        return buildOverlay(overlayTheme_ ? *overlayTheme_ : shell.theme(), shell.view());
    }, [scroll](const core::RenderNode& root, const core::RenderNode* hit,
                core::Offset, core::Offset delta) {
        return scroll(root, hit, delta.y);
    }, [scroll](const core::RenderNode* root, const core::RenderNode* viewport,
                core::Offset, core::Offset delta, core::ScrollDragPhase phase,
                std::uint64_t) {
        return phase == core::ScrollDragPhase::Update && root && viewport
                   ? scroll(*root, viewport, -delta.y) : false;
    });
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
    scrollOffset_.reset();
    shell.markDirty();
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
    // 全窗 barrier：仅输入模态（点击关闭 + 事件/语义模态边界），视觉
    // 透明——浮动菜单不压暗内容（Dialog scrim 只属于 Dialog；与菜单类
    // 控件同口径，menu-controls-design §6.4）。
    core::Widget barrier = core::makeContainerLeaf(
        view.width, view.height, core::EdgeInsets{}, core::EdgeInsets{},
        core::Color::transparent());
    barrier.onClick = dropdownKey_ + "-dismiss";
    barrier.semanticsRole = "dialog";
    barrier.semanticsActions = accessibility::kActionDismiss;
    barrier.key = dropdownKey_ + "-menu-barrier";

    // 菜单定位（§6.7）：值行下方、间隔 4；窗口安全边距 8 内钳制；下方
    // 不足翻向上方。宽度至少覆盖值行且不超过窗口可用宽。
    const float rowHeight = optionRowHeight(theme);
    const float menuWidth = std::clamp(
        std::max(anchor_.size.width, 120.0F), 0.0F,
        std::max(0.0F, view.width - 2.0F * kWindowMarginPx));
    const float contentHeight =
        static_cast<float>(options_.size()) * rowHeight;
    const float below = std::max(0.0F, view.height - kWindowMarginPx -
        anchor_.origin.y - anchor_.size.height - kMenuGapPx);
    const float above = std::max(0.0F, anchor_.origin.y - kMenuGapPx - kWindowMarginPx);
    const float wanted = std::min(contentHeight + 2.0F * kMenuInnerPaddingPx, kMenuMaxHeightPx);
    const bool placeBelow = below >= wanted || below >= above;
    const float menuHeight = std::min(wanted, placeBelow ? below : above);
    core::Offset menuOrigin{anchor_.origin.x,
        placeBelow ? anchor_.origin.y + anchor_.size.height + kMenuGapPx
                   : std::max(kWindowMarginPx, anchor_.origin.y - kMenuGapPx - menuHeight)};
    menuOrigin.x = std::clamp(menuOrigin.x, kWindowMarginPx,
                              std::max(kWindowMarginPx,
                                       view.width - kWindowMarginPx -
                                           menuWidth));

    // 选项（§6.7）：全部 Ghost（hover 状态面/键盘焦点环由交互态表达）；
    // 当前值 Tonal + 尾随 Check（16px 勾选列）。移动高亮不立即改值。
    // Up/Down 的高亮行即 focusNode 目标（refreshOverlay），Ghost rest 底
    // 透明、环是唯一焦点指示——键盘重度表面显式开环（visual-system
    // §6.1，与 makeDialog actions 同口径；showFocusRing 默认关闭）。
    std::vector<core::Widget> buttons;
    buttons.reserve(options_.size());
    for (std::size_t i = 0; i < options_.size(); ++i) {
        const bool isCurrent = options_[i].value == value_;
        core::Widget option = core::makeButton(options_[i].label);
        option.showFocusRing = true;
        option.buttonVariant = isCurrent
                                   ? core::ButtonVariant::Tonal
                                   : core::ButtonVariant::Ghost;
        if (isCurrent) {
            option.icon = core::IconId::Check;
        }
        option.onClick = optionKey(i);
        option.key = optionKey(i);
        option.width = std::max(0.0F, menuWidth - 2.0F * kMenuInnerPaddingPx);
        option.height = rowHeight;
        option.alignContentStart = true;
        option.reserveIconSpace = true;
        buttons.push_back(std::move(option));
    }
    core::Widget menuColumn = core::makeColumn(
        std::move(buttons), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 0.0F,
        core::EdgeInsets::all(kMenuInnerPaddingPx));
    // 项目过多：菜单高度封顶，内容进入垂直滚动；键盘高亮项滚入可见区
    //（§6.7——按行高推导目标偏移，行高统一使偏移确定）。
    const bool needsScroll =
        contentHeight + 2.0F * kMenuInnerPaddingPx > menuHeight + 0.5F;
    if (needsScroll) {
        const float highlightTop =
            static_cast<float>(highlight_) * rowHeight;
        const float maxOffset =
            std::max(0.0F, contentHeight + 2.0F * kMenuInnerPaddingPx -
                              menuHeight);
        const float scrollTarget = std::clamp(
            highlightTop - kMenuInnerPaddingPx, 0.0F, maxOffset);
        menuColumn = core::withScrollOffset(
            core::makeScrollView(std::move(menuColumn),
                                 dropdownKey_ + "-menu-scroll",
                                 std::nullopt, menuHeight),
            scrollOffset_.value_or(scrollTarget));
        menuColumn.showScrollbar = true;
    }
    core::Widget menu;
    menu.type = core::WidgetType::Container;
    menu.color = theme.colors.surfaceElevated;
    menu.radius = core::CornerRadius::all(theme.metrics.cardRadius);
    menu.elevation = 2.0F;  // §4.5：L2（Dropdown/Tooltip）
    // 1px borderDefault 轮廓。
    menu.styleOverrides.border = theme.colors.borderDefault;
    menu.styleOverrides.borderWidth = theme.metrics.controlBorderWidth;
    menu.width = menuWidth;
    if (!needsScroll) {
        menu.height = menuHeight;
    }
    menu.children.push_back(std::move(menuColumn));
    menu = core::withStackPosition(std::move(menu), menuOrigin);
    menu.key = dropdownKey_ + "-menu";

    core::Widget overlay = core::makeStack({std::move(barrier),
                                            std::move(menu)});
    overlay.key = dropdownKey_ + "-menu-root";
    overlay.width = view.width;
    overlay.height = view.height;
    // FocusScope：Tab 遍历不逃出菜单（plan §3.4 modal 键域）。
    if (overlayTheme_.has_value()) {
        overlay = core::makeThemeScope(std::move(overlay), &*overlayTheme_);
    }
    return core::makeFocusScope(std::move(overlay));
}

}  // namespace lumen::widgets
