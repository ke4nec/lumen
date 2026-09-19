// 菜单类控件（docs/lumen-menu-controls-design.md §6-7）：
// ContextMenuController / MenuBarController。面板构建、定位钳制、级联
// 与键盘状态机共享；行 = collectionRow Row（hover/焦点/语义激活复用集
// 合行路径），面板容器/锚定/barrier 复用 M11 DropdownController 验证过
// 的 overlay 结构。

#include "lumen/widgets/menu.h"

#include <algorithm>
#include <cctype>

#include "lumen/accessibility/semantics.h"

namespace lumen::widgets {

namespace {

// 面板几何（menu-controls-design §10.1/§10.2）。
constexpr float kMenuGapPx = 4.0F;          // 锚定菜单与锚的间隙
constexpr float kPointerOffsetPx = 2.0F;    // 指针菜单原点偏移（避压首行）
constexpr float kWindowMarginPx = 8.0F;     // 视口安全边距
constexpr float kMenuInnerPaddingPx = 4.0F; // 面板内边距
constexpr float kMenuMinWidthPx = 160.0F;
constexpr float kMenuMaxWidthPx = 320.0F;
constexpr float kContextMenuWidthPx = 220.0F;
constexpr float kItemPaddingXPx = 12.0F;    // 菜单项水平内边距（Medium 档）
constexpr float kIconSlotPx = 20.0F;        // 图标/勾选槽（16 图标 + 4）
constexpr float kMenuSepHeightPx = 9.0F;    // 分隔线行高（1px + 上下 4）
// M14 打开动效位移（menubar-variants 版本 A）：顶级 = 上升 6px；子菜单
// 级联 = 沿展开方向滑入 4px（同一 openT 进度）。
constexpr float kMenuOpenRisePx = 6.0F;
constexpr float kSubmenuSlidePx = 4.0F;
// 级联深度上限（防递归 submenu 构造异常；应用菜单典型 ≤ 3）。
constexpr std::size_t kMaxCascadeDepth = 8;

// 行高：metrics 最小高度与排版行高的最大值（fontScale 极端时文本行高
// 可超过控件最小高度；DropdownController 同口径）。
float menuRowHeight(const style::Theme& theme) {
    const float minHeight = theme.metrics.minHeight[theme.metrics.baseIndex];
    const float textRow = theme.typography.label.lineHeight > 0.0F
                              ? theme.typography.label.fontSize *
                                    theme.typography.label.lineHeight
                              : theme.typography.label.fontSize * 1.2F;
    return std::max(minHeight,
                    textRow + 2.0F * theme.metrics.controlPaddingY[
                                          theme.metrics.baseIndex]);
}

// 行内容坐标（含面板内边距偏移）：动能矩形与滚动跟随共用同一口径
//（ensureHighlightVisible 同推导）。
void itemContentRect(const MenuItems& items, std::size_t index,
                     const style::Theme& theme, float& top, float& height) {
    const float rowHeight = menuRowHeight(theme);
    top = kMenuInnerPaddingPx;
    for (std::size_t i = 0; i < index && i < items.size(); ++i) {
        top += items[i].separator ? kMenuSepHeightPx : rowHeight;
    }
    height = rowHeight;
}

}  // namespace

// --- ContextMenuController ---

std::string ContextMenuController::itemKey(std::size_t level,
                                           std::size_t index) const {
    return owner_ + ":m" + std::to_string(level) + ":i" +
           std::to_string(index);
}

std::string ContextMenuController::scrollKey(std::size_t level) const {
    return owner_ + ":m" + std::to_string(level) + ":scroll";
}

std::size_t ContextMenuController::firstFocusable(const Level& level) const {
    for (std::size_t i = 0; i < level.items.size(); ++i) {
        if (!level.items[i].separator && level.items[i].enabled) {
            return i;
        }
    }
    return 0;
}

std::size_t ContextMenuController::nextFocusable(const Level& level,
                                                 std::size_t from,
                                                 int step) const {
    // 不环绕：到端即停（与集合行键盘一致）；跳过 separator/disabled。
    if (level.items.empty()) {
        return 0;
    }
    std::size_t i = from;
    for (std::size_t guard = 0; guard < level.items.size(); ++guard) {
        if (step > 0 && i + 1 >= level.items.size()) {
            break;
        }
        if (step < 0 && i == 0) {
            break;
        }
        i = step > 0 ? i + 1 : i - 1;
        if (!level.items[i].separator && level.items[i].enabled) {
            return i;
        }
    }
    return from;
}

void ContextMenuController::setHighlight(Level& level, std::size_t to) {
    if (to == level.highlight) {
        return;
    }
    // M14：高亮迁移记录滑移起点（hlFrom），由 stepMotion 起 表采样；
    // 无动效口径保持原直达行为。
    if (motion_) {
        level.hlFrom = level.highlight;
        level.hlAnim = true;
        level.hlStarted = false;
    }
    level.highlight = to;
}

bool ContextMenuController::stepMotion(std::uint64_t nowMs, bool& active) {
    active = false;
    if (!motion_ || levels_.empty()) {
        return false;
    }
    bool changed = false;
    const auto step = [nowMs, &changed, &active](
                          bool& anim, bool& started,
                          std::uint64_t& startMs, std::uint32_t durationMs,
                          float& value) {
        if (!anim) {
            return;
        }
        if (!started) {
            // pending → 首拍起表：open 后未经 tick 的直驱构建保持终态。
            started = true;
            startMs = nowMs;
        }
        const std::uint64_t elapsed =
            nowMs >= startMs ? nowMs - startMs : 0;
        const core::Tween tween{0.0, 1.0, static_cast<double>(durationMs),
                                core::Easing::EaseOut};
        const float next = static_cast<float>(tween.sample(elapsed));
        if (next != value) {
            value = next;
            changed = true;
        }
        if (tween.finished(elapsed)) {
            anim = false;
            started = false;
        } else {
            active = true;
        }
    };
    for (Level& level : levels_) {
        step(level.openAnim, level.openStarted, level.openStartMs, fadeMs_,
             level.openT);
        step(level.hlAnim, level.hlStarted, level.hlStartMs, slideMs_,
             level.hlT);
    }
    return changed;
}

void ContextMenuController::registerHoverSink(app::AppShell& shell) {
    // 惰性注册一次（sink 家族无注销 API；菜单关闭时 levels_ 空守卫空转，
    // 与 MenuBar 栏切换 sink 同口径）。菜单行在 overlay 树——sink 收到
    // 的是主树 root，这里自行对 overlayRoot 做命中。
    if (hoverSinkRegistered_) {
        return;
    }
    hoverSinkRegistered_ = true;
    shell.controller().addPointerMoveSink(
        [this, &shell](const core::RenderNode&, core::Offset position) {
            if (levels_.empty() || shell.overlayRoot() == nullptr) {
                pending_.armed = false;
                return false;
            }
            std::vector<const core::RenderNode*> chain;
            (void)core::hitTestChain(*shell.overlayRoot(), position, chain);
            for (const core::RenderNode* node : chain) {
                for (std::size_t l = 0; l < levels_.size(); ++l) {
                    for (std::size_t i = 0; i < levels_[l].items.size();
                         ++i) {
                        if (node->key == itemKey(l, i)) {
                            hoverMenuItem(shell, l, i);
                            return false;
                        }
                    }
                }
            }
            // 未命中行（分隔线/面板空白/barrier/栏）：解除待展开，不动
            // 级联（点击外部/Esc 才关闭）。
            pending_.armed = false;
            return false;
        });
}

void ContextMenuController::hoverMenuItem(app::AppShell& shell,
                                          std::size_t level,
                                          std::size_t index) {
    const MenuItem& item = levels_[level].items[index];
    // 祖先层：悬停非当前级联源的项 → 收起到该层（桌面惯例：悬停切换
    // 级联，与键盘 Left 回退互补；源行自身不动）。
    if (level + 1 < levels_.size() &&
        levels_[level + 1].sourceIndex != index) {
        while (levels_.size() > level + 1) {
            popLevel(shell);
        }
    }
    // 收起后该层即最深层：hasSubmenu 项武装自动展开（同项不重置计时
    //——行内指针抖动不清零），其余解除。
    if (level + 1 == levels_.size() && item.hasSubmenu && item.enabled) {
        if (!pending_.armed || pending_.level != level ||
            pending_.index != index) {
            pending_ = PendingSubmenu{};
            pending_.armed = true;
            pending_.level = level;
            pending_.index = index;
        }
        return;
    }
    pending_.armed = false;
}

bool ContextMenuController::stepPendingSubmenu(app::AppShell& shell,
                                                std::uint64_t nowMs) {
    if (!pending_.armed) {
        return false;
    }
    if (!pending_.started) {
        // pending → 首拍起表（pointer sink 无钟；与打开动效同口径）。
        pending_.started = true;
        pending_.armedAtMs = nowMs;
    }
    const std::uint64_t elapsed =
        nowMs >= pending_.armedAtMs ? nowMs - pending_.armedAtMs : 0;
    if (submenuHoverMs_ == 0 || elapsed >= submenuHoverMs_) {
        const std::size_t level = pending_.level;
        const std::size_t index = pending_.index;
        pending_.armed = false;
        // 展开前再验（计时期间层级/项可能已变）。
        if (level < levels_.size() && level + 1 == levels_.size() &&
            index < levels_[level].items.size()) {
            expandSubmenu(shell, level, index);
        }
        return false;
    }
    return true;  // 计时中：保持动画帧粒度 tick（到点即触发）
}

void ContextMenuController::open(app::AppShell& shell,
                                 core::Offset position, MenuItems items,
                                 SubmenuProvider submenu,
                                 const style::Theme* anchorTheme) {
    focusRestoreKey_ = shell.focus().focusedKey();
    openAnchored(shell, core::Rect{position, core::Size{0.0F, 0.0F}},
                 std::move(items), std::move(submenu), anchorTheme, "ctx");
}

void ContextMenuController::openAnchored(app::AppShell& shell,
                                         core::Rect anchor, MenuItems items,
                                         SubmenuProvider submenu,
                                         const style::Theme* anchorTheme,
                                         std::string owner) {
    if (items.empty()) {
        return;
    }
    // 复开：清旧 handler/overlay（不动焦点——focusRestoreKey_ 是本次
    // 唤起方的恢复目标，close 的恢复语义留给真正关闭）。
    levels_.clear();
    eraseHandlers(shell);
    shell.clearOverlay();
    owner_ = std::move(owner);
    submenu_ = std::move(submenu);
    explicitTheme_ = anchorTheme != nullptr;
    overlayTheme_ = anchorTheme != nullptr
                        ? std::optional<style::Theme>(*anchorTheme)
                        : std::nullopt;
    // M14 动效口径（menubar-variants 版本 A+D）：motionEnabled = 应用
    // opt-in 且已被 tick 驱动（直驱测试保持即时终态）；时长快照自唤起
    // 主题（reduceAnimation 归零 = 首拍即达终态）。
    motion_ = shell.motionEnabled();
    const style::Theme& sourceTheme =
        anchorTheme != nullptr ? *anchorTheme : shell.theme();
    fadeMs_ = sourceTheme.motion.menuOpenFadeMs;
    slideMs_ = sourceTheme.motion.menuHighlightSlideMs;
    submenuHoverMs_ = sourceTheme.motion.menuSubmenuHoverMs;
    pending_.armed = false;
    registerHoverSink(shell);
    levels_.push_back(
        Level{std::move(items), 0, anchor});
    levels_.back().highlight = firstFocusable(levels_.back());
    levels_.back().hlFrom = levels_.back().highlight;
    if (motion_) {
        levels_.back().openAnim = true;  // pending：首次 stepMotion 起表
    }
    registerHandlers(shell);
    // 滚轮：命中菜单滚动视口 → 滚动；其余（hit 为空 = 菜单外/无滚动视
    // 口，AppShell::wheel 的 overlay 兜底直调）→ 关闭（menu-controls-
    // design §6.4"滚轮（任意位置）关闭"）。
    const auto scrollOrClose = [this, &shell](const core::RenderNode& root,
                                              const core::RenderNode* hit,
                                              float dy) {
        if (levels_.empty()) {
            return false;
        }
        if (hit != nullptr) {
            for (std::size_t level = 0; level < levels_.size(); ++level) {
                if (hit->key != scrollKey(level)) {
                    continue;
                }
                const auto* viewport =
                    core::findNodeByKey(root, hit->key);
                if (viewport == nullptr) {
                    return false;
                }
                const float next = std::clamp(
                    levels_[level].scrollOffset + dy, 0.0F,
                    viewport->scrollExtent);
                if (next == levels_[level].scrollOffset) {
                    return false;
                }
                levels_[level].scrollOffset = next;
                shell.markDirty();
                return true;
            }
        }
        close(shell);
        return true;
    };
    shell.setOverlayBuilder(
        [this, &shell]() -> std::optional<core::Widget> {
            if (levels_.empty()) {
                eraseHandlers(shell);
                return std::nullopt;
            }
            if (!explicitTheme_) {
                overlayTheme_ = shell.theme();
            }
            return buildOverlay(overlayTheme_ ? *overlayTheme_
                                              : shell.theme(),
                                shell.view());
        },
        [scrollOrClose](const core::RenderNode& root,
                        const core::RenderNode* hit, core::Offset,
                        core::Offset delta) {
            return scrollOrClose(root, hit, delta.y);
        },
        [this, &shell](const core::RenderNode*, const core::RenderNode* viewport,
                       core::Offset, core::Offset,
                       core::ScrollDragPhase phase, std::uint64_t) {
            // 拖动起点在菜单滚动视口内 → 首版无菜单内拖拽滚动，无操作
            //（长菜单面板内拖动不得把菜单拖关）；其余 Begin（防御：overlay
            // 内当前没有其他滚动视口，barrier 拖动不触发滚动拖拽路径）
            // → 关闭。
            if (phase != core::ScrollDragPhase::Begin || levels_.empty()) {
                return false;
            }
            for (std::size_t level = 0; level < levels_.size(); ++level) {
                if (viewport != nullptr &&
                    viewport->key == scrollKey(level)) {
                    return false;
                }
            }
            close(shell);
            return true;
        },
        [this, &shell](std::uint64_t nowMs) {
            // M14：面板动效采样 + 悬停展开计时 + 宿主附加动效链
            //（MenuBar 下划线）；值变化即 markDirty（终拍也重建，避免
            // 0.99 残帧）。pending 计时保持动画帧粒度的 tick。
            bool active = false;
            if (stepMotion(nowMs, active)) {
                shell.markDirty();
            }
            active = stepPendingSubmenu(shell, nowMs) || active;
            if (onAnimate) {
                active = onAnimate(nowMs) || active;
            }
            return active;
        });
    shell.rebuildIfDirty();  // overlay 布局落地，供焦点定位。
    refreshOverlay(shell);
}

void ContextMenuController::close(app::AppShell& shell) {
    if (levels_.empty()) {
        return;
    }
    pending_.armed = false;
    levels_.clear();
    eraseHandlers(shell);
    shell.clearOverlay();
    shell.rebuildIfDirty();
    // 焦点恢复（消失则交给应用既有恢复规则）。
    if (!focusRestoreKey_.empty()) {
        if (const core::RenderNode* target =
                core::findNodeByKey(shell.root(), focusRestoreKey_)) {
            shell.controller().focusNode(*target);
        }
    }
    focusRestoreKey_.clear();
}

void ContextMenuController::registerHandlers(app::AppShell& shell) {
    shell.handlers()[owner_ + "-dismiss"] = [this, &shell]() {
        close(shell);
    };
    registeredHandlers_.push_back(owner_ + "-dismiss");
    for (std::size_t l = 0; l < levels_.size(); ++l) {
        for (std::size_t i = 0; i < levels_[l].items.size(); ++i) {
            const MenuItem& item = levels_[l].items[i];
            if (item.separator) {
                continue;
            }
            const std::string key = itemKey(l, i);
            shell.handlers()[key] = [this, l, i, &shell]() {
                itemClicked(shell, l, i);
            };
            registeredHandlers_.push_back(key);
        }
    }
}

void ContextMenuController::eraseHandlers(app::AppShell& shell) {
    shell.handlers().erase(owner_ + "-dismiss");
    for (const auto& key : registeredHandlers_) {
        shell.handlers().erase(key);
    }
    registeredHandlers_.clear();
}

void ContextMenuController::itemClicked(app::AppShell& shell,
                                        std::size_t level,
                                        std::size_t index) {
    activate(shell, level, index);
}

void ContextMenuController::activate(app::AppShell& shell, std::size_t level,
                                     std::size_t index) {
    if (level >= levels_.size() || index >= levels_[level].items.size()) {
        return;
    }
    const MenuItem& item = levels_[level].items[index];
    if (item.separator || !item.enabled) {
        return;
    }
    if (item.hasSubmenu) {
        expandSubmenu(shell, level, index);
        return;
    }
    const std::string id = item.id;
    close(shell);
    if (onCommand) {
        onCommand(id);
    }
}

void ContextMenuController::expandSubmenu(app::AppShell& shell,
                                          std::size_t level,
                                          std::size_t index) {
    if (!submenu_ || level + 1 != levels_.size()) {
        return;  // 只从最深层展开（保持级联线性）
    }
    const MenuItem& parent = levels_[level].items[index];
    MenuItems items = submenu_(parent.id);
    if (items.empty()) {
        return;
    }
    // 级联锚 = 父行矩形（overlay 树内即视口坐标；父行可见、面板不动，
    // 几何取自当前帧）。
    core::Rect anchor{};
    if (shell.overlayRoot() != nullptr) {
        if (const core::RenderNode* row = core::findNodeByKey(
                *shell.overlayRoot(), itemKey(level, index))) {
            anchor = core::Rect{core::absoluteOffset(*shell.overlayRoot(),
                                                     row->key),
                                row->size};
        }
    }
    levels_.push_back(Level{std::move(items), 0, anchor});
    levels_.back().highlight = firstFocusable(levels_.back());
    levels_.back().hlFrom = levels_.back().highlight;
    levels_.back().sourceIndex = index;
    pending_.armed = false;
    if (motion_) {
        levels_.back().openAnim = true;  // 级联滑入（同一 pending 口径）
    }
    for (std::size_t i = 0; i < levels_.back().items.size(); ++i) {
        const MenuItem& item = levels_.back().items[i];
        if (item.separator) {
            continue;
        }
        const std::size_t l = levels_.size() - 1;
        const std::string key = itemKey(l, i);
        shell.handlers()[key] = [this, l, i, &shell]() {
            itemClicked(shell, l, i);
        };
        registeredHandlers_.push_back(key);
    }
    refreshOverlay(shell);
}

void ContextMenuController::popLevel(app::AppShell& shell) {
    if (levels_.size() <= 1) {
        return;
    }
    pending_.armed = false;
    const std::size_t l = levels_.size() - 1;
    for (std::size_t i = 0; i < levels_[l].items.size(); ++i) {
        shell.handlers().erase(itemKey(l, i));
    }
    levels_.pop_back();
    refreshOverlay(shell);
}

void ContextMenuController::refreshOverlay(app::AppShell& shell) {
    if (levels_.empty()) {
        return;
    }
    const std::size_t l = levels_.size() - 1;
    ensureHighlightVisible(shell, l);
    shell.markDirty();
    shell.rebuildIfDirty();
    if (shell.overlayRoot() != nullptr) {
        if (const core::RenderNode* item = core::findNodeByKey(
                *shell.overlayRoot(), itemKey(l, levels_[l].highlight))) {
            shell.controller().focusNode(*item);
        }
    }
}

void ContextMenuController::ensureHighlightVisible(app::AppShell& shell,
                                                   std::size_t level) {
    if (level >= levels_.size() || shell.overlayRoot() == nullptr) {
        return;
    }
    const core::RenderNode* viewport = core::findNodeByKey(
        *shell.overlayRoot(), scrollKey(level));
    if (viewport == nullptr) {
        levels_[level].scrollOffset = 0.0F;
        return;
    }
    const style::Theme& theme = overlayTheme_ ? *overlayTheme_ : shell.theme();
    const float rowHeight = menuRowHeight(theme);
    float itemTop = kMenuInnerPaddingPx;
    for (std::size_t i = 0; i < levels_[level].highlight; ++i) {
        itemTop += levels_[level].items[i].separator ? kMenuSepHeightPx
                                                     : rowHeight;
    }
    const float itemBottom = itemTop + rowHeight;
    float next = levels_[level].scrollOffset;
    if (itemTop < next) {
        next = itemTop;
    } else if (itemBottom > next + viewport->size.height) {
        next = itemBottom - viewport->size.height;
    }
    levels_[level].scrollOffset =
        std::clamp(next, 0.0F, viewport->scrollExtent);
}

bool ContextMenuController::handleKey(app::AppShell& shell, core::Key key,
                                      core::KeyModifiers mods, char keyChar) {
    if (levels_.empty()) {
        return false;
    }
    Level& deep = levels_.back();
    switch (key) {
        case core::Key::Down:
            setHighlight(deep, nextFocusable(deep, deep.highlight, +1));
            refreshOverlay(shell);
            return true;
        case core::Key::Up:
            setHighlight(deep, nextFocusable(deep, deep.highlight, -1));
            refreshOverlay(shell);
            return true;
        case core::Key::Home:
            setHighlight(deep, firstFocusable(deep));
            refreshOverlay(shell);
            return true;
        case core::Key::End: {
            // 末个可聚焦项（从尾端倒退）。
            std::size_t last = firstFocusable(deep);
            for (std::size_t i = deep.items.size(); i-- > 0;) {
                if (!deep.items[i].separator && deep.items[i].enabled) {
                    last = i;
                    break;
                }
            }
            setHighlight(deep, last);
            refreshOverlay(shell);
            return true;
        }
        case core::Key::Enter:
            activate(shell, levels_.size() - 1, deep.highlight);
            return true;
        case core::Key::Right: {
            const MenuItem& item = deep.items[deep.highlight];
            if (item.hasSubmenu && item.enabled &&
                levels_.size() < kMaxCascadeDepth) {
                expandSubmenu(shell, levels_.size() - 1, deep.highlight);
                return true;
            }
            // 顶级无子菜单动作：交调用方（MenuBar 顶级切换）。
            return levels_.size() > 1;
        }
        case core::Key::Left:
            if (levels_.size() > 1) {
                popLevel(shell);
                return true;
            }
            return false;  // 顶级：MenuBar 切换
        case core::Key::Escape:
            if (levels_.size() > 1) {
                popLevel(shell);
            } else {
                close(shell);
            }
            return true;
        case core::Key::Tab:
        case core::Key::Backtab:
            // 模态不逃逸：关闭全部并恢复焦点（menu-controls-design §6.4）。
            close(shell);
            return true;
        default:
            break;
    }
    // Space（Key::None + keyChar ' '）：激活（Enter 同路径）。
    if (key == core::Key::None && keyChar == ' ') {
        activate(shell, levels_.size() - 1, deep.highlight);
        return true;
    }
    // Alt + 助记字母（大小写不敏感）。
    if ((mods & core::kModifierAlt) != 0 && keyChar != 0) {
        const char want = static_cast<char>(std::tolower(
            static_cast<unsigned char>(keyChar)));
            for (std::size_t i = 0; i < deep.items.size(); ++i) {
                const MenuItem& item = deep.items[i];
                if (item.mnemonic != 0 && item.enabled && !item.separator &&
                    std::tolower(static_cast<unsigned char>(item.mnemonic)) ==
                        want) {
                    setHighlight(deep, i);
                    activate(shell, levels_.size() - 1, i);
                    return true;
                }
            }
    }
    return false;
}

core::Widget ContextMenuController::buildOverlay(const style::Theme& theme,
                                                 core::Size view) const {
    // 全窗 barrier：仅输入模态（点击关闭 + makeDialog 的事件/语义模态
    // 边界），视觉透明——菜单不是对话框，不压暗内容（Dialog scrim 只
    // 属于 Dialog；menu-controls-design §6.4）。
    core::Widget barrier = core::makeContainerLeaf(
        view.width, view.height, core::EdgeInsets{}, core::EdgeInsets{},
        core::Color::transparent());
    barrier.onClick = owner_ + "-dismiss";
    barrier.semanticsRole = "dialog";
    barrier.semanticsActions = accessibility::kActionDismiss;
    barrier.key = owner_ + "-menu-barrier";

    std::vector<core::Widget> layers;
    layers.push_back(std::move(barrier));
    for (std::size_t l = 0; l < levels_.size(); ++l) {
        layers.push_back(buildPanel(theme, levels_[l], l, view));
    }
    core::Widget overlay = core::makeStack(std::move(layers));
    overlay.key = owner_ + "-menu-root";
    overlay.width = view.width;
    overlay.height = view.height;
    if (overlayTheme_.has_value()) {
        overlay = core::makeThemeScope(std::move(overlay), &*overlayTheme_);
    }
    return core::makeFocusScope(std::move(overlay));
}

core::Widget ContextMenuController::buildPanel(
    const style::Theme& theme, const Level& level, std::size_t levelIndex,
    core::Size view) const {
    const float rowHeight = menuRowHeight(theme);
    const bool pointerAnchor =
        levelIndex == 0 && level.anchor.size.width <= 0.0F;
    const float menuWidth = std::clamp(
        pointerAnchor ? kContextMenuWidthPx
                      : std::max(level.anchor.size.width, 200.0F),
        kMenuMinWidthPx,
        std::min(kMenuMaxWidthPx,
                 std::max(0.0F, view.width - 2.0F * kWindowMarginPx)));

    float contentHeight = 0.0F;
    for (const auto& item : level.items) {
        contentHeight += item.separator ? kMenuSepHeightPx : rowHeight;
    }
    const float wanted =
        contentHeight + 2.0F * kMenuInnerPaddingPx;

    // 定位（menu-controls-design §6.3）：指针锚 = 原点 + (2,2)，下/右缘
    // 不足翻上/左；栏锚 = 下方 + 4 间隙，不足翻上；统一 8px 视口钳制。
    float menuHeight = 0.0F;
    core::Offset origin{};
    bool placeRight = true;  // 子菜单级联方向（滑入位移随载荷）
    if (pointerAnchor) {
        const float availBelow =
            view.height - kWindowMarginPx - level.anchor.origin.y;
        const float availAbove =
            level.anchor.origin.y - kWindowMarginPx;
        const bool placeBelow =
            wanted <= availBelow || availBelow >= availAbove;
        menuHeight = std::min(wanted, std::max(0.0F, placeBelow ? availBelow
                                                                : availAbove));
        origin = core::Offset{level.anchor.origin.x + kPointerOffsetPx,
                              placeBelow
                                  ? level.anchor.origin.y + kPointerOffsetPx
                                  : std::max(
                                        kWindowMarginPx,
                                        level.anchor.origin.y -
                                            kPointerOffsetPx - menuHeight)};
    } else if (levelIndex == 0) {
        const float below = std::max(0.0F, view.height - kWindowMarginPx -
            level.anchor.origin.y - level.anchor.size.height - kMenuGapPx);
        const float above = std::max(0.0F, level.anchor.origin.y -
            kMenuGapPx - kWindowMarginPx);
        const bool placeBelow = wanted <= below || below >= above;
        menuHeight = std::min(wanted, placeBelow ? below : above);
        origin = core::Offset{
            level.anchor.origin.x,
            placeBelow
                ? level.anchor.origin.y + level.anchor.size.height +
                      kMenuGapPx
                : std::max(kWindowMarginPx,
                           level.anchor.origin.y - kMenuGapPx - menuHeight)};
    } else {
        // 子菜单级联：父行右缘 + 0、顶对齐；右缘不足翻左。
        const float availRight = view.width - kWindowMarginPx -
                                 level.anchor.origin.x -
                                 level.anchor.size.width;
        const float availLeft =
            level.anchor.origin.x - kWindowMarginPx;
        placeRight = availRight >= kMenuMinWidthPx ||
                                availRight >= availLeft;
        menuHeight = std::min(
            wanted, view.height - 2.0F * kWindowMarginPx);
        origin = core::Offset{
            placeRight
                ? level.anchor.origin.x + level.anchor.size.width
                : std::max(kWindowMarginPx,
                           level.anchor.origin.x - menuWidth),
            std::min(
                std::max(kWindowMarginPx, level.anchor.origin.y),
                std::max(kWindowMarginPx,
                         view.height - kWindowMarginPx - menuHeight))};
    }
    origin.x = std::clamp(origin.x, kWindowMarginPx,
                          std::max(kWindowMarginPx,
                                   view.width - kWindowMarginPx - menuWidth));
    // M14 打开动效（版本 A）：淡入（整面板 transitionAlpha）+ 位移——顶级
    // 上升 6px、子菜单沿级联方向滑入 4px（同一 openT 进度，EaseOut）。
    // 钳制后叠加：终态 = 精确钳位，过程中的瞬时越界随进度收敛。
    if (level.openT < 1.0F) {
        if (levelIndex == 0) {
            origin.y += (1.0F - level.openT) * kMenuOpenRisePx;
        } else {
            origin.x += (1.0F - level.openT) *
                        (placeRight ? kSubmenuSlidePx : -kSubmenuSlidePx);
        }
    }

    // 行（collectionRow：hover/焦点/背景经 WidgetState 解析，与集合行
    // 同路径）。M14：键盘高亮背景统一由动能矩形承载（两口径像素等价
    // ——同色同矩形，source-over 复合一致；current 语义由焦点表达，
    // §8.1 契约本未列 selected），行不再折算 selected。
    std::vector<core::Widget> rows;
    rows.reserve(level.items.size());
    for (std::size_t i = 0; i < level.items.size(); ++i) {
        const MenuItem& item = level.items[i];
        if (item.separator) {
            // 分隔线：1px 线 + 上下 4 呼吸（kMenuSepHeightPx=9）+ 水平
            // inset 8（§10.1；design/menu-controls.html msep 同口径）。
            core::Widget sep = core::makeContainerLeaf(
                std::max(0.0F, menuWidth - 2.0F * kMenuInnerPaddingPx -
                                   2.0F * kItemPaddingXPx),
                1.0F,
                core::EdgeInsets{}, core::EdgeInsets::symmetric(8.0F, 4.0F),
                theme.colors.borderDefault);
            rows.push_back(std::move(sep));
            continue;
        }
        core::Widget row;
        row.type = core::WidgetType::Row;
        row.key = itemKey(levelIndex, i);
        row.onClick = row.key;  // handler 由控制器注册
        row.collectionRow = true;
        row.checked = item.checkable && item.checked;
        row.enabled = item.enabled;
        row.crossAxis = core::CrossAxisAlignment::Center;
        row.spacing = 8.0F;
        row.padding = core::EdgeInsets::symmetric(kItemPaddingXPx, 0.0F);
        row.width = std::max(0.0F, menuWidth - 2.0F * kMenuInnerPaddingPx);
        row.height = rowHeight;
        row.semanticsRole = "menuItem";
        row.semanticsLabel = item.label;
        if (item.hasSubmenu) {
            row.semanticsValue = "hasSubmenu=true";
        }
        row.semanticsActions =
            accessibility::kActionFocus | accessibility::kActionActivate;
        // 图标/勾选槽（20px：checked > icon > 空槽对齐）。
        core::IconId slotIcon = core::IconId::None;
        if (item.checkable && item.checked) {
            slotIcon = core::IconId::Check;
        } else if (item.icon != core::IconId::None) {
            slotIcon = item.icon;
        }
        core::Widget slot = core::makeIcon(slotIcon, {}, 16.0F, 16.0F);
        row.children.push_back(
            core::makeContainer(std::move(slot), kIconSlotPx, std::nullopt));
        // 标签（flex 吃满中段）。§10.1：超宽省略号——单行 + Ellipsis，
        // 不换行（行高稳定；TextStyle 未着色回落角色前景色）。
        core::Widget label = core::makeText(item.label);
        label.flex = 1.0F;
        label.textStyle.maxLines = 1;
        label.textStyle.overflow = core::TextOverflow::Ellipsis;
        row.children.push_back(std::move(label));
        // 快捷键列（仅展示；muted；同样单行省略）。
        if (!item.shortcut.empty()) {
            core::Widget sc = core::makeText(item.shortcut);
            sc.textStyle.color = theme.colors.contentSecondary;
            sc.textStyle.maxLines = 1;
            sc.textStyle.overflow = core::TextOverflow::Ellipsis;
            row.children.push_back(std::move(sc));
        }
        // 子菜单级联指示。
        if (item.hasSubmenu) {
            row.children.push_back(core::makeIcon(
                core::IconId::ChevronRight, {}, 16.0F, 16.0F));
        }
        rows.push_back(std::move(row));
    }
    core::Widget column = core::makeColumn(
        std::move(rows), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 0.0F,
        core::EdgeInsets::all(kMenuInnerPaddingPx));
    core::Widget content = std::move(column);
    const bool highlightFocusable =
        !level.items.empty() && !level.items[level.highlight].separator &&
        level.items[level.highlight].enabled;
    if (highlightFocusable) {
        // M14 高亮动能矩形（版本 D）——常驻承载键盘高亮背景（动效/经典
        // 两口径统一：与行 selected 背景同色同矩形、source-over 复合等
        // 价；语义 current 由焦点表达）。置于行下方（Stack 先序 = 底层，
        // 命中/绘制后序优先，行 hover/焦点环不受影响），动效口径下在行
        // 间滑移（hlFrom → highlight，跨分隔线高度变形）；内容坐标与滚
        // 动同域（ScrollView 包整个 Stack）。x 与行对齐（column 内边距
        // 偏移）。
        float fromTop = 0.0F;
        float fromHeight = 0.0F;
        float toTop = 0.0F;
        float toHeight = 0.0F;
        itemContentRect(level.items, level.hlFrom, theme, fromTop,
                        fromHeight);
        itemContentRect(level.items, level.highlight, theme, toTop,
                        toHeight);
        const float top = fromTop + (toTop - fromTop) * level.hlT;
        const float height =
            fromHeight + (toHeight - fromHeight) * level.hlT;
        core::Widget highlight = core::makeContainerLeaf(
            std::max(0.0F, menuWidth - 2.0F * kMenuInnerPaddingPx), height,
            core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.selectionBackground,
            owner_ + ":hl:" + std::to_string(levelIndex));
        content = core::makeStack(
            {core::withStackPosition(
                 std::move(highlight),
                 core::Offset{kMenuInnerPaddingPx, top}),
             std::move(content)});
    }
    // 超长菜单：封顶滚动（ScrollView 兜底；键盘高亮按行高推导滚入）。
    const bool needsScroll = wanted > menuHeight + 0.5F;
    if (needsScroll) {
        const float maxOffset = std::max(0.0F, wanted - menuHeight);
        content = core::withScrollOffset(
            core::makeScrollView(std::move(content), scrollKey(levelIndex),
                                 std::nullopt, menuHeight),
            std::clamp(level.scrollOffset, 0.0F, maxOffset));
        content.showScrollbar = true;
    }
    core::Widget menu;
    menu.type = core::WidgetType::Container;
    menu.color = theme.colors.surfaceElevated;
    menu.radius = core::CornerRadius::all(theme.metrics.cardRadius);
    menu.elevation = 2.0F;  // §10.2：L2（与 Dropdown 浮动菜单同层）
    menu.styleOverrides.border = theme.colors.borderDefault;
    menu.styleOverrides.borderWidth = theme.metrics.controlBorderWidth;
    menu.width = menuWidth;
    if (!needsScroll) {
        menu.height = menuHeight;
    }
    menu.transitionAlpha = level.openT;  // M14 打开淡入（终态恒 1）
    menu.semanticsRole = "menu";
    menu.children.push_back(std::move(content));
    menu = core::withStackPosition(std::move(menu), origin);
    menu.key = owner_ + ":panel:" + std::to_string(levelIndex);
    return menu;
}

// --- MenuBarController ---

std::string MenuBarController::barKey(std::size_t index) const {
    return "menu:bar:" + (index < menus_.size() ? menus_[index].id
                                                : std::string());
}

void MenuBarController::setMenus(std::vector<TopMenu> menus) {
    menus_ = std::move(menus);
}

void MenuBarController::setMenuProvider(
    std::function<MenuItems(const std::string& id)> provider) {
    provider_ = std::move(provider);
}

void MenuBarController::setSubmenuProvider(SubmenuProvider submenu) {
    submenu_ = std::move(submenu);
}

core::Widget MenuBarController::build(const style::Theme& theme) const {
    std::vector<core::Widget> items;
    items.reserve(menus_.size());
    const bool open = menu_.isOpen();
    for (std::size_t i = 0; i < menus_.size(); ++i) {
        core::Widget item = core::makeButton(menus_[i].title);
        // M14 打开态（版本 A）：打开项 = Tonal 变体（§10.4），其余 Ghost。
        item.buttonVariant =
            open && i == openIndex_ ? core::ButtonVariant::Tonal
                                    : core::ButtonVariant::Ghost;
        // 栏项取 Small 档（32px，design/gallery.html .mb-item）——栏嵌
        // 入 48px caption 时留呼吸；Medium（40px）+ 下划线 + padding 会
        // 撑到 46px 挤满标题栏。
        item.controlSize = core::ControlSize::Small;
        item.alignContentStart = true;
        item.key = barKey(i);
        item.onClick = item.key;  // attach 注册打开/切换
        // 下划线（2px accent）：常驻占位保持栏高稳定（关闭 alpha 0），
        // 打开项随打开动效渐入（无动效口径 = 直达 1）。
        core::Widget underline = core::makeContainerLeaf(
            std::nullopt, 2.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.accent);
        if (open && i == openIndex_) {
            underline.key = barKey(i) + ":ul";
            underline.transitionAlpha = underlineT_;
        } else {
            underline.transitionAlpha = 0.0F;
        }
        core::Widget wrap = core::makeColumn(
            {std::move(item), std::move(underline)},
            core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 0.0F);
        items.push_back(std::move(wrap));
    }
    core::Widget bar = core::makeRow(
        std::move(items), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Center, 2.0F,
        core::EdgeInsets::symmetric(4.0F, 2.0F));
    bar.key = "menu:bar";
    return bar;
}

void MenuBarController::attach(app::AppShell& shell) {
    // 幂等守卫：重复 attach 会叠加 pointer-move sink（sink 无注销 API，
    // 悬停切换单次注册即够）；handler 按 key 覆盖本无副作用，一并挡住。
    if (attached_) {
        return;
    }
    attached_ = true;
    for (std::size_t i = 0; i < menus_.size(); ++i) {
        shell.handlers()[barKey(i)] = [this, i, &shell]() {
            openMenu(shell, i);
        };
    }
    menu_.onCommand = [this](const std::string& id) {
        if (onCommand) {
            onCommand(id);
        }
    };
    // M14：下划线动效挂在菜单 overlay 的 animate 链上（菜单关闭即解除）。
    menu_.onAnimate = [this, &shell](std::uint64_t nowMs) {
        return stepUnderline(shell, nowMs);
    };
    shell.controller().addPointerMoveSink(
        [this, &shell](const core::RenderNode& root, core::Offset position) {
            if (!menu_.isOpen()) {
                return;
            }
            std::vector<const core::RenderNode*> chain;
            (void)core::hitTestChain(root, position, chain);
            for (const core::RenderNode* node : chain) {
                for (std::size_t i = 0; i < menus_.size(); ++i) {
                    if (i != openIndex_ && node->key == barKey(i)) {
                        openMenu(shell, i);
                        return;
                    }
                }
            }
        });
}

void MenuBarController::openMenu(app::AppShell& shell, std::size_t index) {
    if (index >= menus_.size() || !provider_) {
        return;
    }
    MenuItems items = provider_(menus_[index].id);
    if (items.empty()) {
        return;
    }
    const core::RenderNode* barItem =
        core::findNodeByKey(shell.root(), barKey(index));
    if (barItem == nullptr) {
        return;
    }
    const core::Rect anchor{core::absoluteOffset(shell.root(), barKey(index)),
                            barItem->size};
    openIndex_ = index;
    // M14：下划线渐入重新武装（pending：首次 stepUnderline 起表；切换
    // 顶级菜单 = 重放打开动效——版本 A 契约）。与面板同口径门控——
    // 未开 motionTransitions 的应用保持静态打开态（直达 1）。
    underlineFadeMs_ = shell.theme().motion.menuOpenFadeMs;
    underlineAnim_ = shell.motionEnabled();
    underlineStarted_ = false;
    if (!underlineAnim_) {
        underlineT_ = 1.0F;
    }
    menu_.setFocusRestoreKey(barKey(index));
    menu_.openAnchored(shell, anchor, std::move(items), submenu_, nullptr,
                       "menubar");
}

bool MenuBarController::stepUnderline(app::AppShell& shell,
                                       std::uint64_t nowMs) {
    if (!underlineAnim_) {
        return false;
    }
    if (!menu_.isOpen()) {
        underlineAnim_ = false;
        underlineStarted_ = false;
        return false;
    }
    if (!underlineStarted_) {
        // pending → 首拍起表：open 后未经 tick 的直驱构建保持终态。
        underlineStarted_ = true;
        underlineStartMs_ = nowMs;
    }
    const std::uint64_t elapsed =
        nowMs >= underlineStartMs_ ? nowMs - underlineStartMs_ : 0;
    const core::Tween tween{0.0, 1.0,
                            static_cast<double>(underlineFadeMs_),
                            core::Easing::EaseOut};
    const float next = static_cast<float>(tween.sample(elapsed));
    if (next != underlineT_) {
        underlineT_ = next;
        shell.markDirty();  // 栏在主树：值变化即重建采样
    }
    if (tween.finished(elapsed)) {
        underlineAnim_ = false;
        underlineStarted_ = false;
        return false;
    }
    return true;
}

void MenuBarController::close(app::AppShell& shell) {
    underlineAnim_ = false;
    underlineStarted_ = false;
    menu_.close(shell);
}

bool MenuBarController::handleKey(app::AppShell& shell, core::Key key,
                                  core::KeyModifiers mods, char keyChar) {
    if (menu_.isOpen()) {
        // 顶级菜单打开时 Left/Right = 切换顶级（子级级联交菜单面板）。
        if (menu_.levelCount() == 1 &&
            (key == core::Key::Left || key == core::Key::Right)) {
            std::size_t next = openIndex_;
            if (key == core::Key::Right && openIndex_ + 1 < menus_.size()) {
                next = openIndex_ + 1;
            }
            if (key == core::Key::Left && openIndex_ > 0) {
                next = openIndex_ - 1;
            }
            if (next != openIndex_) {
                openMenu(shell, next);
            }
            return true;
        }
        return menu_.handleKey(shell, key, mods, keyChar);
    }
    // Alt + 助记字母：直接打开对应顶级菜单（大小写不敏感）。
    if ((mods & core::kModifierAlt) != 0 && keyChar != 0) {
        const char want = static_cast<char>(std::tolower(
            static_cast<unsigned char>(keyChar)));
        for (std::size_t i = 0; i < menus_.size(); ++i) {
            if (menus_[i].mnemonic != 0 &&
                std::tolower(static_cast<unsigned char>(
                    menus_[i].mnemonic)) == want) {
                openMenu(shell, i);
                return true;
            }
        }
    }
    // 焦点在栏项：Down/Enter 打开该菜单。
    const std::string& focused = shell.focus().focusedKey();
    if (!focused.empty() && focused.rfind("menu:bar:", 0) == 0 &&
        (key == core::Key::Down || key == core::Key::Enter)) {
        for (std::size_t i = 0; i < menus_.size(); ++i) {
            if (focused == barKey(i)) {
                openMenu(shell, i);
                return true;
            }
        }
    }
    return false;
}

}  // namespace lumen::widgets
