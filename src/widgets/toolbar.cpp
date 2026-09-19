// ToolBar 工具栏（docs/lumen-toolbar-design.md）。组合件实现：
// Column[ Row(项序列, surface 底), 1px 底分隔线 ]；项 = Ghost 图标按钮
//（方形通高，min 尺寸天然方形——resolver icon-only 口径）；分隔线 =
// 1px 容器（上下 inset 8）；溢出面板 = ContextMenuController anchored。

#include "lumen/widgets/toolbar.h"

#include <algorithm>
#include <cstdio>

#include "lumen/accessibility/semantics.h"

namespace lumen::widgets {
namespace {

// 栏与项尺度（toolbar-design §9.1；随密度三档）。
constexpr float kBarItemSize[3] = {32.0F, 40.0F, 48.0F};
constexpr float kBarIcon[3] = {16.0F, 16.0F, 20.0F};
constexpr float kBarGap[3] = {2.0F, 4.0F, 4.0F};
constexpr float kBarPaddingX[3] = {4.0F, 8.0F, 8.0F};

std::uint8_t sizeIndexFor(const style::Metrics& metrics,
                          core::ControlSize size) {
    const int offset = static_cast<int>(size) - 1;
    const int index = static_cast<int>(metrics.baseIndex) + offset;
    return static_cast<std::uint8_t>(std::clamp(index, 0, 2));
}

}  // namespace

ToolBarController::ToolBarController(std::string key) : key_{std::move(key)} {
    menu_.setFocusRestoreKey(overflowKey());
}

std::string ToolBarController::barKey() const { return key_ + ":bar"; }
std::string ToolBarController::rowKey() const { return key_ + ":row"; }
std::string ToolBarController::itemKey(const std::string& id) const {
    return key_ + ":item:" + id;
}
std::string ToolBarController::tipKey(const std::string& id) const {
    return key_ + ":tip:" + id;
}
std::string ToolBarController::overflowKey() const {
    return key_ + ":overflow";
}

void ToolBarController::setItems(std::vector<ToolBarItem> items) {
    items_ = std::move(items);
    overflowIds_.clear();
    widthCache_.clear();
    if (attached_ && shell_ != nullptr) {
        registerHandlers(*shell_);
        registerTooltips(*shell_);
        shell_->markDirty();
    }
}

void ToolBarController::setChecked(const std::string& id, bool checked) {
    for (auto& item : items_) {
        if (item.id == id && item.checkable) {
            item.checked = checked;
            return;
        }
    }
}

void ToolBarController::registerHandlers(app::AppShell& shell) const {
    for (const auto& item : items_) {
        if (item.separator) {
            continue;
        }
        shell.handlers()[itemKey(item.id)] = [this, id = item.id] {
            if (onCommand) {
                onCommand(id);
            }
        };
    }
    shell.handlers()[overflowKey()] = [this, &shell] {
        if (!menu_.isOpen()) {
            openOverflow(shell);
        }
    };
}

void ToolBarController::registerTooltips(app::AppShell& shell) const {
    // M11 tooltip 关联（幂等 by tooltipKey）：icon-only 项的文本事实来源。
    for (const auto& item : items_) {
        if (item.separator || item.label.empty()) {
            continue;
        }
        shell.registerTooltip(itemKey(item.id), tipKey(item.id));
    }
}

void ToolBarController::attach(app::AppShell& shell) {
    if (attached_) {
        return;  // sink/handler 注册幂等守卫（MenuBar 同口径）
    }
    shell_ = &shell;
    menu_.onCommand = [this](const std::string& id) {
        if (onCommand) {
            onCommand(id);
        }
    };
    registerHandlers(shell);
    registerTooltips(shell);
    attached_ = true;
}

float ToolBarController::itemWidth(const ToolBarItem& item,
                                   const style::Theme& theme) const {
    if (item.separator) {
        return 1.0F + 16.0F;  // 线宽 + 两侧 8px margin
    }
    const std::uint8_t index =
        sizeIndexFor(theme.metrics, core::ControlSize::Medium);
    const float box = kBarItemSize[index];
    if (item.labelMode) {
        // 上一帧实测优先；未见过（首帧/新项）= icon + gap + 文本估算
        //（CJK 按 1.0、其余按 0.55 × 字号；下一帧由实测修正——二次收敛）。
        const auto cached = widthCache_.find(itemKey(item.id));
        if (cached != widthCache_.end()) {
            return cached->second;
        }
        float textWidth = 0.0F;
        for (const char c : item.label) {
            textWidth += (static_cast<unsigned char>(c) >= 0x80)
                             ? theme.typography.label.fontSize
                             : theme.typography.label.fontSize * 0.55F;
        }
        return box + theme.metrics.controlGap[index] + textWidth +
               2.0F * theme.metrics.controlPaddingX[index];
    }
    const auto cached = widthCache_.find(itemKey(item.id));
    return cached != widthCache_.end() ? cached->second : box;
}

void ToolBarController::recomputeOverflow(app::AppShell& shell) const {
    const style::Theme& theme = shell.theme();
    const std::uint8_t index =
        sizeIndexFor(theme.metrics, core::ControlSize::Medium);
    // 可用宽取自上一帧的栏容器（被父级拉伸；row 自收缩——折叠后变窄会
    // 造成无法回位的死锁，故不能用 row 宽做决策输入）。
    const core::RenderNode* bar = core::findNodeByKey(shell.root(), barKey());
    if (bar == nullptr) {
        if (!overflowIds_.empty()) {
            overflowIds_.clear();
            shell.markDirty();
        }
        return;
    }
    // 项宽缓存回填（上一帧实测；labelMode 折叠回位决策依赖它）。
    // 栏结构：bar(Column) > stack > row —— row 是 stack 首子。
    if (!bar->children.empty() && !bar->children[0].children.empty()) {
        for (const auto& child : bar->children[0].children[0].children) {
            if (child.type == core::WidgetType::Button) {
                widthCache_[child.key] = child.size.width;
            }
        }
    }

    const float gap = kBarGap[index];
    const float avail =
        std::max(0.0F, bar->size.width - 2.0F * kBarPaddingX[index]);
    const float overflowCost = kBarItemSize[index] + gap;

    // 可见宽度合计（折空分隔线不计——其前无可见项即消失，§5.2）。
    auto totalWidth = [&](const std::vector<std::string>& folded) {
        float total = 0.0F;
        std::size_t visibleCount = 0;
        for (const auto& item : items_) {
            if (std::find(folded.begin(), folded.end(), item.id) !=
                folded.end()) {
                continue;
            }
            if (item.separator && visibleCount == 0) {
                continue;
            }
            total += itemWidth(item, theme);
            if (!item.separator) {
                ++visibleCount;
            }
        }
        return visibleCount > 0
                   ? total + gap * static_cast<float>(visibleCount - 1)
                   : 0.0F;
    };

    std::vector<std::string> folded = overflowIds_;
    // 变宽回位：从头部逐项放回（保持"尾部优先折叠"的逆序）。
    auto unfoldOnce = [&]() -> bool {
        for (const auto& item : items_) {
            if (item.separator ||
                std::find(folded.begin(), folded.end(), item.id) ==
                    folded.end()) {
                continue;
            }
            // labelMode 后折 → 先回。
            std::vector<std::string> trial = folded;
            trial.erase(std::find(trial.begin(), trial.end(), item.id));
            const float extra =
                trial.empty() ? 0.0F : overflowCost;
            if (totalWidth(trial) + extra <= avail) {
                folded = trial;
                return true;
            }
            return false;  // 首个（最先折叠的）放不回 → 停止
        }
        return false;
    };
    while (unfoldOnce()) {
    }

    // 变窄折叠：从尾部、labelMode 优先，至少保留首项。
    auto foldOnce = [&]() -> bool {
        const std::size_t visible = items_.size() - folded.size();
        if (visible <= 1) {
            return false;
        }
        // 候选：逆序遍历，labelMode 优先。
        const ToolBarItem* target = nullptr;
        for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
            if (it->separator ||
                std::find(folded.begin(), folded.end(), it->id) !=
                    folded.end()) {
                continue;
            }
            if (target == nullptr || it->labelMode) {
                target = &*it;
                if (it->labelMode) {
                    break;
                }
            }
        }
        if (target == nullptr) {
            return false;
        }
        folded.push_back(target->id);
        return true;
    };
    const bool initialFits = totalWidth(folded) +
                                     (folded.empty() ? 0.0F : overflowCost) <=
                                 avail;
    if (!initialFits) {
        while (totalWidth(folded) + overflowCost > avail && foldOnce()) {
        }
        if (totalWidth(folded) + overflowCost > avail) {
            // 极窄：折叠到只剩首项仍放不下——保底折叠全部可折叠项。
            while (foldOnce()) {
            }
        }
    }

    if (folded != overflowIds_) {
        overflowIds_ = folded;
        shell.markDirty();  // 二次收敛重建（Splitter 同口径）
    }
}

std::vector<MenuItem> ToolBarController::panelItems() const {
    std::vector<MenuItem> result;
    for (const auto& item : items_) {
        if (std::find(overflowIds_.begin(), overflowIds_.end(), item.id) ==
            overflowIds_.end()) {
            continue;
        }
        if (item.separator) {
            continue;  // 折空分隔线不进面板（toolbar-design §6.3）
        }
        MenuItem entry;
        entry.id = item.id;
        entry.label = item.label.empty() ? item.id : item.label;
        entry.icon = item.icon;
        entry.checkable = item.checkable;
        entry.checked = item.checked;
        entry.enabled = item.enabled;
        result.push_back(std::move(entry));
    }
    return result;
}

void ToolBarController::openOverflow(app::AppShell& shell) const {
    const auto* node = core::findNodeByKey(shell.root(), overflowKey());
    if (node == nullptr) {
        return;
    }
    core::Rect anchor{core::absoluteOffset(shell.root(), overflowKey()),
                      node->size};
    menu_.openAnchored(shell, anchor, panelItems(), {}, nullptr, key_);
}

core::Widget ToolBarController::build(app::AppShell& shell,
                                      const style::Theme& theme) const {
    const std::uint8_t index =
        sizeIndexFor(theme.metrics, core::ControlSize::Medium);
    const float itemSize = kBarItemSize[index];
    const float iconSize = kBarIcon[index];
    const float gap = kBarGap[index];

    recomputeOverflow(shell);

    std::vector<core::Widget> children;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const ToolBarItem& item = items_[i];
        const bool folded =
            std::find(overflowIds_.begin(), overflowIds_.end(), item.id) !=
            overflowIds_.end();
        if (item.separator) {
            // 折空分隔线消失：其后无可见命令项（§5.2）。
            const bool nextVisible = std::any_of(
                items_.begin() + static_cast<std::ptrdiff_t>(i + 1),
                items_.end(), [&](const ToolBarItem& c) {
                    return !c.separator &&
                           std::find(overflowIds_.begin(), overflowIds_.end(),
                                     c.id) == overflowIds_.end();
                });
            if (!nextVisible) {
                continue;
            }
            children.push_back(core::makeContainerLeaf(
                1.0F, itemSize - 16.0F, core::EdgeInsets{},
                core::EdgeInsets::symmetric(8.0F, 0.0F),
                theme.colors.borderDefault, itemKey(item.id)));
            continue;
        }
        if (folded) {
            continue;
        }
        core::Widget button = core::makeButton(
            item.labelMode ? item.label : std::string{}, core::TextStyle{},
            core::EdgeInsets{}, 0.0F, itemKey(item.id),
            item.labelMode ? std::optional<float>(std::nullopt)
                           : std::optional<float>(itemSize),
            itemSize, itemKey(item.id));
        button.buttonVariant = core::ButtonVariant::Chrome;
        button.checked = item.checkable && item.checked;  // 语义 flag +
        // resolver checked 底（accentContainer，design §9.2/稿件 .is-checked）
        button.icon = item.icon;
        button.alignContentStart = item.labelMode;  // 稿件：icon+label 起始对齐
        button.styleOverrides.iconSize = iconSize;
        button.showFocusRing = true;  // 框架自建键盘件恒开环（§6.1）
        button.enabled = item.enabled;
        // 语义文本 = tooltip label（icon-only 项的文本事实来源，§7）。
        if (!item.label.empty()) {
            button.semanticsLabel = item.label;
        }
        children.push_back(std::move(button));
    }

    if (!overflowIds_.empty()) {
        // 弹性 spacer：溢出按钮右贴栏缘（design/toolbar.html
        // .tb-overflow margin-left:auto），行借 flex 子扩展到容器宽。
        core::Widget spacer = core::makeContainerLeaf(
            0.0F, 0.0F, core::EdgeInsets{}, core::EdgeInsets{},
            core::Color::transparent(), key_ + ":spacer");
        spacer.flex = 1.0F;
        children.push_back(std::move(spacer));
        core::Widget overflow = core::makeButton(
            "", core::TextStyle{}, core::EdgeInsets{}, 0.0F, overflowKey(),
            itemSize, itemSize, overflowKey());
        overflow.buttonVariant = core::ButtonVariant::Chrome;
        overflow.icon = core::IconId::ChevronDown;
        overflow.styleOverrides.iconSize = iconSize;
        overflow.showFocusRing = true;
        overflow.semanticsLabel = "更多命令";
        children.push_back(std::move(overflow));
    }

    core::Widget row = core::makeRow(
        std::move(children), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Center, gap,
        core::EdgeInsets::symmetric(kBarPaddingX[index], 0.0F),
        core::EdgeInsets{}, rowKey(), std::nullopt, itemSize);
    row.color = theme.colors.surface;

    std::vector<core::Widget> tooltipChildren;
    for (const auto& item : items_) {
        if (item.separator || item.label.empty()) {
            continue;
        }
        std::string text = item.label;
        if (!item.shortcut.empty()) {
            text += "   " + item.shortcut;
        }
        tooltipChildren.push_back(core::withStackPosition(
            core::makeTooltip(text, tipKey(item.id)),
            core::Offset{0.0F, 0.0F}));
    }

    // 栏 = 行 + 底部 1px 分隔线；tooltip 常驻子树挂 Stack（隐藏时
    // alpha 0，M11 机制负责显隐/定位/淡切，toolbar-design §6.1）。
    std::vector<core::Widget> stackChildren;
    stackChildren.push_back(std::move(row));
    for (auto& tip : tooltipChildren) {
        stackChildren.push_back(std::move(tip));
    }
    core::Widget stack = core::makeStack(std::move(stackChildren),
                                         core::StackAlignment::TopLeft);
    stack.semanticsRole = "toolbar";
    core::Widget bar = core::makeColumn(
        {std::move(stack),
         core::makeContainerLeaf(std::nullopt, 1.0F, core::EdgeInsets{},
                                 core::EdgeInsets{},
                                 theme.colors.borderDefault,
                                 key_ + ":line")},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
        0.0F, core::EdgeInsets{}, core::EdgeInsets{}, barKey());
    return bar;
}

bool ToolBarController::handleKey(app::AppShell& shell, core::Key key,
                                  core::KeyModifiers mods, char keyChar) {
    if (menu_.isOpen()) {
        return menu_.handleKey(shell, key, mods, keyChar);
    }
    if (!attached_) {
        return false;
    }
    if (key == core::Key::Down) {
        if (shell.focus().focusedKey() == overflowKey()) {
            openOverflow(shell);
            return true;
        }
        return false;
    }
    if (key != core::Key::Left && key != core::Key::Right &&
        key != core::Key::Home && key != core::Key::End) {
        return false;
    }
    // 漫游焦点（toolbar-design §6.2）：可见项 + 溢出按钮；跳过 disabled
    // 与分隔线；到端即停（集合行口径，不环绕）。
    std::vector<const core::RenderNode*> focusable;
    for (const auto& item : items_) {
        if (item.separator) {
            continue;
        }
        const bool folded =
            std::find(overflowIds_.begin(), overflowIds_.end(), item.id) !=
            overflowIds_.end();
        if (folded) {
            continue;
        }
        const auto* node = core::findNodeByKey(shell.root(), itemKey(item.id));
        if (node != nullptr && node->enabled) {
            focusable.push_back(node);
        }
    }
    if (!overflowIds_.empty()) {
        const auto* node = core::findNodeByKey(shell.root(), overflowKey());
        if (node != nullptr) {
            focusable.push_back(node);
        }
    }
    if (focusable.empty()) {
        return false;
    }
    const std::string current = shell.focus().focusedKey();
    std::size_t index = focusable.size();
    for (std::size_t i = 0; i < focusable.size(); ++i) {
        if (focusable[i]->key == current) {
            index = i;
            break;
        }
    }
    std::size_t target = focusable.size();  // 无效 = 无当前焦点
    switch (key) {
        case core::Key::Left:
            if (index == focusable.size()) {
                target = focusable.size() - 1;  // 栏外 Left = 末项
            } else if (index > 0) {
                target = index - 1;
            }
            break;
        case core::Key::Right:
            if (index == focusable.size()) {
                target = 0;  // 栏外 Right = 首项
            } else if (index + 1 < focusable.size()) {
                target = index + 1;
            }
            break;
        case core::Key::Home:
            target = 0;
            break;
        case core::Key::End:
            target = focusable.size() - 1;
            break;
        default:
            break;
    }
    if (target >= focusable.size() || focusable[target]->key == current) {
        return target < focusable.size();  // 到端即停：消费但不迁移
    }
    shell.controller().focusNode(*focusable[target]);
    return true;
}

}  // namespace lumen::widgets
