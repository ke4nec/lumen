// StatusBar 状态栏（docs/lumen-statusbar-design.md）。组合件实现：
// Column[ 顶部 1px 分隔线, Row[ 弹性消息区, 常驻项序列, grip? ] ]。
// 高度 24/28/32（信息 chrome 降一档）；文本 caption 12px secondary；
// 动效全部控制器自持相位（step 逐 tick），不依赖布局动画。

#include "lumen/widgets/statusbar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "lumen/accessibility/semantics.h"

namespace lumen::widgets {
namespace {

// 栏高与部件尺度（design §9.1；随密度三档）。
constexpr float kBarHeight[3] = {24.0F, 28.0F, 32.0F};
constexpr float kProgressWidth[3] = {120.0F, 120.0F, 144.0F};
constexpr float kBusySize[3] = {12.0F, 12.0F, 14.0F};
constexpr float kItemGap = 8.0F;
constexpr float kSeparatorHeight = 12.0F;
constexpr float kGripSize = 12.0F;
constexpr float kMsgMinWidth = 120.0F;  // 消息保底宽（design §5.2）

std::uint8_t densityIndex(const style::Theme& theme) {
    return theme.metrics.baseIndex;
}

}  // namespace

StatusBarController::StatusBarController(std::string key)
    : key_{std::move(key)} {}

std::string StatusBarController::msgKey() const {
    return key_ + ":msg";
}

std::string StatusBarController::itemKey(const std::string& id) const {
    return key_ + ":item:" + id;
}

void StatusBarController::setIdleMessage(std::string text) {
    idleMessage_ = std::move(text);
    if (msgPhase_ == MsgPhase::Steady && !transientDeadlineMs_) {
        message_ = idleMessage_;
        if (shell_ != nullptr) {
            shell_->markDirty();
        }
    }
}

void StatusBarController::setMessage(std::string text,
                                     std::uint32_t timeoutMs) {
    if (shell_ == nullptr || !shell_->motionEnabled() ||
        shell_->theme().motion.statusbarMessageFadeMs == 0) {
        // reduceAnimation / 零时长 / 直驱（无 tick）：即时切换（design §10
        // 与 menu M14"不经 tick 的直驱输出保持终态"同口径）。
        message_ = std::move(text);
        msgAlpha_ = 1.0F;
        msgPhase_ = MsgPhase::Steady;
        pendingMessage_.reset();
        msgAnchorNeeded_ = false;
        pendingTimeoutMs_ = timeoutMs;
        transientDeadlineMs_ = 0;
        if (shell_ != nullptr && timeoutMs > 0) {
            // 驻留为内容节律，reduceAnimation 保留；deadline 由 step 首
            // 拍锚定（anchorNeeded 在此置位，等待下一个 tick 起表）。
            msgAnchorNeeded_ = true;
        }
        if (shell_ != nullptr) {
            shell_->markDirty();
        }
        return;
    }
    // 淡出 → 替换 → 淡入（取代，不排队）；已在切换中则替换目标。
    pendingMessage_ = std::move(text);
    pendingTimeoutMs_ = timeoutMs;
    transientDeadlineMs_ = 0;
    if (msgPhase_ != MsgPhase::FadingOut) {
        msgPhase_ = MsgPhase::FadingOut;
        msgAnchorNeeded_ = true;
    }
    shell_->markDirty();
}

void StatusBarController::setItems(std::vector<StatusBarItem> items) {
    items_ = std::move(items);
    foldedIds_.clear();
    widthCache_.clear();
    if (attached_ && shell_ != nullptr) {
        registerItemHandlers(*shell_);
        shell_->markDirty();
    }
}

void StatusBarController::setProgress(float percent) {
    const bool indeterminate = percent < 0.0F;
    progress_ = indeterminate ? -1.0F : std::clamp(percent, 0.0F, 100.0F);
    if (indeterminate) {
        marqueeAnchored_ = false;  // 相位重新锚定
    }
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

void StatusBarController::setBusy(bool busy) {
    if (busy_ == busy) {
        return;
    }
    busy_ = busy;
    busyAnchored_ = false;  // 相位重新锚定
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

void StatusBarController::setShowResizeGrip(bool show) {
    showResizeGrip_ = show;
    if (shell_ != nullptr) {
        shell_->markDirty();
    }
}

void StatusBarController::registerItemHandlers(app::AppShell& shell) {
    for (const auto& item : items_) {
        if (item.kind != StatusItemKind::Toggle) {
            continue;
        }
        const std::string k = itemKey(item.id);
        shell.handlers()[k] = [this, id = item.id] {
            if (onItemClicked) {
                onItemClicked(id);
            }
        };
    }
}

void StatusBarController::attach(app::AppShell& shell) {
    shell_ = &shell;
    registerItemHandlers(shell);
    attached_ = true;
}

bool StatusBarController::messageAnimating() const {
    return msgPhase_ != MsgPhase::Steady || pendingMessage_.has_value() ||
           msgAnchorNeeded_;
}

void StatusBarController::recomputeFold(app::AppShell& shell) const {
    const style::Theme& theme = shell.theme();
    const std::uint8_t index = densityIndex(theme);
    // 可用宽取自上一帧的栏容器（被父级拉伸；row 自收缩——折叠后变窄会
    // 造成无法回位的死锁，toolbar §15 同教训）。
    const core::RenderNode* bar = core::findNodeByKey(shell.root(), key_);
    if (bar == nullptr) {
        if (!foldedIds_.empty()) {
            foldedIds_.clear();
            shell.markDirty();
        }
        return;
    }
    // 项宽缓存回填（上一帧实测；row 是 bar Column 的次子）。
    if (bar->children.size() >= 2) {
        for (const auto& child : bar->children[1].children) {
            widthCache_[child.key] = child.size.width;
        }
    }

    const float avail =
        std::max(0.0F, bar->size.width - 2.0F * theme.metrics.controlPaddingX[index]);
    const float msgFloor = kMsgMinWidth + 2.0F * kItemGap;  // 保底 + 呼吸

    auto itemsWidth = [&](const std::vector<std::string>& folded) {
        float total = 0.0F;
        std::size_t visible = 0;
        for (const auto& item : items_) {
            if (item.kind == StatusItemKind::Busy && !busy_) {
                continue;  // busy 项出现/消失即项增减
            }
            if (std::find(folded.begin(), folded.end(), item.id) !=
                folded.end()) {
                continue;
            }
            if (item.kind == StatusItemKind::Separator && visible == 0) {
                continue;  // 折空分隔线消失
            }
            if (item.kind == StatusItemKind::Separator) {
                total += 1.0F + 2.0F * kItemGap;
                continue;
            }
            const auto cached = widthCache_.find(itemKey(item.id));
            if (cached != widthCache_.end()) {
                total += cached->second;
            } else if (item.kind == StatusItemKind::Progress) {
                total += item.fixedWidth > 0.0F
                             ? item.fixedWidth
                             : kProgressWidth[index];
            } else if (item.kind == StatusItemKind::Busy) {
                total += kBusySize[index];
            } else {
                // 文本/Toggle 未实测：按字号估宽（下一帧由缓存修正）。
                total += static_cast<float>(item.text.size()) *
                             theme.typography.caption.fontSize * 0.6F +
                         16.0F;
            }
            ++visible;
        }
        return visible > 0
                   ? total + kItemGap * static_cast<float>(visible - 1)
                   : 0.0F;
    };

    std::vector<std::string> folded = foldedIds_;
    // 变宽回位：从尾部逐项放回（声明序从左折叠的逆序）。
    auto unfoldOnce = [&]() -> bool {
        for (std::size_t i = items_.size(); i-- > 0;) {
            const auto& item = items_[i];
            if (item.kind == StatusItemKind::Separator ||
                std::find(folded.begin(), folded.end(), item.id) ==
                    folded.end()) {
                continue;
            }
            std::vector<std::string> trial = folded;
            trial.erase(std::find(trial.begin(), trial.end(), item.id));
            if (msgFloor + itemsWidth(trial) <= avail) {
                folded = trial;
                return true;
            }
            return false;  // 最后折叠的放不回 → 停止
        }
        return false;
    };
    while (unfoldOnce()) {
    }
    // 变窄折叠：从左往右整项隐藏（§5.2/§14.1），消息保底。
    while (msgFloor + itemsWidth(folded) > avail) {
        bool foldedOne = false;
        for (const auto& item : items_) {
            if (item.kind == StatusItemKind::Separator ||
                item.kind == StatusItemKind::Busy ||
                std::find(folded.begin(), folded.end(), item.id) !=
                    folded.end()) {
                continue;
            }
            folded.push_back(item.id);
            foldedOne = true;
            break;
        }
        if (!foldedOne) {
            break;  // 全部可折叠项已隐藏
        }
    }

    if (folded != foldedIds_) {
        foldedIds_ = folded;
        shell.markDirty();  // 二次收敛重建
    }
}

core::Widget StatusBarController::build(const style::Theme& theme) const {
    const std::uint8_t index = densityIndex(theme);
    const float barHeight = kBarHeight[index];
    const float rowHeight = barHeight - 1.0F;  // 顶部 1px 分隔线
    const float paddingX = theme.metrics.controlPaddingX[index];
    if (shell_ != nullptr) {
        recomputeFold(*shell_);
    }

    core::TextStyle caption = theme.typography.caption;
    caption.color = theme.colors.contentSecondary;
    caption.maxLines = 1;
    caption.overflow = core::TextOverflow::Ellipsis;
    if (index == 2) {
        caption.fontSize = 13.0F;  // Touch 档（design §9.1）
    }

    auto foldedOut = [&](const StatusBarItem& item) {
        return std::find(foldedIds_.begin(), foldedIds_.end(), item.id) !=
               foldedIds_.end();
    };

    // 消息区（弹性 + 省略号；淡切 = 节点 transitionAlpha）。8px 为
    // 消息文本内边距（不位移节点原点——布局断言锚定 paddingX）。
    core::Widget msg = core::makeText(message_, caption, core::EdgeInsets{},
                                      1.0F, msgKey(), std::nullopt,
                                      std::nullopt,
                                      core::EdgeInsets::symmetric(8.0F, 0.0F));
    msg.transitionAlpha = msgAlpha_;

    std::vector<core::Widget> children;
    children.push_back(std::move(msg));

    for (const auto& item : items_) {
        if (foldedOut(item)) {
            continue;  // 折叠项整项隐藏（design §5.2）
        }
        switch (item.kind) {
            case StatusItemKind::Separator: {
                // 折空分隔线消失：其后无可见项。
                const bool nextVisible = std::any_of(
                    items_.begin(), items_.end(), [&](const auto& c) {
                        return c.kind != StatusItemKind::Separator &&
                               !foldedOut(c) &&
                               !(c.kind == StatusItemKind::Busy && !busy_);
                    });
                if (!nextVisible) {
                    continue;
                }
                children.push_back(core::makeContainerLeaf(
                    1.0F, kSeparatorHeight, core::EdgeInsets{},
                    core::EdgeInsets::symmetric(kItemGap, 0.0F),
                    theme.colors.borderDefault, itemKey(item.id)));
                break;
            }
            case StatusItemKind::Text: {
                core::Widget text = core::makeText(
                    item.text, caption, core::EdgeInsets{}, 0.0F,
                    itemKey(item.id),
                    item.fixedWidth > 0.0F
                        ? std::optional<float>(item.fixedWidth)
                        : std::nullopt);
                if (!item.enabled) {
                    core::TextStyle dim = caption;
                    dim.color = theme.colors.disabledContent;
                    text.textStyle = dim;
                }
                children.push_back(std::move(text));
                break;
            }
            case StatusItemKind::Progress: {
                // determinate：值即状态（text = 整数百分比，不补间）；
                // indeterminate：往返段（scrollOffset 0..1 相位；-1 哨兵
                // = reduceAnimation 静止中段带，painter §9.3）。
                const bool indet = progress_ < 0.0F;
                core::Widget bar =
                    core::makeProgressBar(indet ? "0" : percentText(progress_),
                                          itemKey(item.id),
                                          item.fixedWidth > 0.0F
                                              ? item.fixedWidth
                                              : kProgressWidth[index],
                                          indet);
                bar.controlSize = core::ControlSize::Small;  // trackHeight 4
                if (indet) {
                    // reduceAnimation（cycleMs == 0）：-1 哨兵 → painter
                    // 画静止中段 40% 半透明带（design §10）。
                    bar.scrollOffset =
                        theme.motion.progressIndeterminateCycleMs == 0
                            ? -1.0F
                            : marqueePhase_;
                    bar.semanticsValue = "indeterminate";
                }
                children.push_back(std::move(bar));
                break;
            }
            case StatusItemKind::Busy: {
                if (!busy_) {
                    continue;  // 隐藏（不占位——出现/消失即项增减）
                }
                core::Widget arc =
                    core::makeIcon(core::IconId::Busy, itemKey(item.id),
                                   kBusySize[index], kBusySize[index]);
                // reduceAnimation：弧静止 + 0.5 透明（design §10——形状
                // 保留"进行中"语义，不靠运动传达唯一信息）。
                arc.styleOverrides.foreground =
                    theme.motion.statusbarBusyCycleMs == 0
                        ? core::scaleColorAlpha(theme.colors.accent, 128)
                        : theme.colors.accent;
                arc.iconRotation = busyRotation_;
                arc.semanticsLabel = "进行中";
                children.push_back(std::move(arc));
                break;
            }
            case StatusItemKind::Toggle: {
                core::Widget toggle = core::makeButton(
                    item.text, caption, core::EdgeInsets{}, 0.0F,
                    itemKey(item.id),
                    item.fixedWidth > 0.0F
                        ? std::optional<float>(item.fixedWidth)
                        : std::nullopt,
                    rowHeight - 8.0F, itemKey(item.id));
                toggle.buttonVariant = core::ButtonVariant::Chrome;
                toggle.controlSize = core::ControlSize::Small;
                toggle.styleOverrides.radius =
                    core::CornerRadius::all(theme.metrics.controlRadius[index]);
                toggle.enabled = item.enabled;
                // hover 表面派生 + 前景提亮 primary、pressed accent 混合
                // （chrome 变体，design §9.3 与稿件一致）。
                children.push_back(std::move(toggle));
                break;
            }
        }
    }

    // resize grip：纯视觉件（命中归平台 resize 边）；右下角贴底。
    if (showResizeGrip_) {
        std::vector<core::Widget> gripColumn;
        gripColumn.push_back(core::makeContainerLeaf(
            kGripSize, std::max(0.0F, rowHeight - kGripSize - 2.0F),
            core::EdgeInsets{}, core::EdgeInsets{}, core::Color::transparent(),
            key_ + ":grip-space"));
        core::Widget grip = core::makeIcon(core::IconId::Grip, key_ + ":grip",
                                           kGripSize, kGripSize);
        grip.styleOverrides.foreground = theme.colors.borderStrong;
        gripColumn.push_back(std::move(grip));
        children.push_back(core::makeColumn(
            std::move(gripColumn), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::End, 0.0F, core::EdgeInsets{},
            core::EdgeInsets::only(0.0F, 0.0F, 2.0F, 0.0F), key_ + ":grip-col",
            kGripSize, rowHeight));
    }

    core::Widget row = core::makeRow(
        std::move(children), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Center, kItemGap,
        core::EdgeInsets{paddingX, 0.0F, paddingX, 0.0F}, core::EdgeInsets{},
        key_ + ":row", std::nullopt, rowHeight);

    core::Widget bar = core::makeColumn(
        {core::makeContainerLeaf(std::nullopt, 1.0F, core::EdgeInsets{},
                                 core::EdgeInsets{},
                                 theme.colors.borderDefault, key_ + ":line"),
         std::move(row)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
        0.0F, core::EdgeInsets{}, core::EdgeInsets{}, key_, std::nullopt,
        barHeight);
    bar.semanticsRole = "statusBar";
    return bar;
}

std::string StatusBarController::percentText(float percent) const {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d",
                  static_cast<int>(std::lround(percent)));
    return buffer;
}

bool StatusBarController::step(app::AppShell& shell, std::uint64_t nowMs) {
    bool changed = false;
    const bool reduced =
        shell.theme().motion.statusbarMessageFadeMs == 0 ||
        !shell.motionEnabled();

    // --- 消息淡切状态机 ---
    if (msgAnchorNeeded_) {
        msgPhaseStartMs_ = nowMs;
        msgAnchorNeeded_ = false;
        changed = true;
    }
    if (msgPhase_ == MsgPhase::Steady && pendingTimeoutMs_ > 0) {
        // 直达路径的瞬态驻留锚定（step 首拍起表）。
        transientDeadlineMs_ = nowMs + pendingTimeoutMs_;
        pendingTimeoutMs_ = 0;
    }
    const double fadeMs = std::max(
        1.0, static_cast<double>(shell.theme().motion.statusbarMessageFadeMs));
    if (msgPhase_ == MsgPhase::FadingOut) {
        const double t = static_cast<double>(nowMs - msgPhaseStartMs_) / fadeMs;
        msgAlpha_ = static_cast<float>(std::clamp(1.0 - t, 0.0, 1.0));
        if (t >= 1.0) {
            message_ = pendingMessage_.value_or(idleMessage_);
            pendingMessage_.reset();
            msgPhase_ = reduced ? MsgPhase::Steady : MsgPhase::FadingIn;
            msgAlpha_ = reduced ? 1.0F : 0.0F;
            msgPhaseStartMs_ = nowMs;
            if (pendingTimeoutMs_ > 0) {
                // 瞬态驻留从替换拍起算（驻留为内容节律，reduceAnimation
                // 同样保留——design §10）。
                transientDeadlineMs_ = nowMs + pendingTimeoutMs_;
                pendingTimeoutMs_ = 0;
            }
        }
        changed = true;
    } else if (msgPhase_ == MsgPhase::FadingIn) {
        const double t = static_cast<double>(nowMs - msgPhaseStartMs_) / fadeMs;
        msgAlpha_ = static_cast<float>(std::clamp(t, 0.0, 1.0));
        if (t >= 1.0) {
            msgPhase_ = MsgPhase::Steady;
            msgAlpha_ = 1.0F;
        }
        changed = true;
    } else if (transientDeadlineMs_ != 0 && nowMs >= transientDeadlineMs_) {
        // 驻留到期：淡出回 idle。
        transientDeadlineMs_ = 0;
        if (reduced) {
            message_ = idleMessage_;
            msgAlpha_ = 1.0F;
        } else {
            msgPhase_ = MsgPhase::FadingOut;
            pendingMessage_ = idleMessage_;
            msgPhaseStartMs_ = nowMs;
        }
        changed = true;
    }

    // --- busy 弧旋转（1200ms/圈 linear；reduceAnimation 停转） ---
    bool busyActive = false;
    if (busy_) {
        const auto cycle =
            static_cast<double>(shell.theme().motion.statusbarBusyCycleMs);
        if (cycle > 0.0 && shell.motionEnabled()) {
            if (!busyAnchored_) {
                busyAnchorMs_ = nowMs;
                busyAnchored_ = true;
            }
            const double elapsed = static_cast<double>(nowMs - busyAnchorMs_);
            busyRotation_ = static_cast<float>(
                std::fmod(2.0 * 3.14159265 * elapsed / cycle, 2.0 * 3.14159265));
            busyActive = true;
            changed = true;
        }
    } else {
        busyAnchored_ = false;
    }

    // --- indeterminate 往返段（1400ms/周期，三角波相位） ---
    bool marqueeActive = false;
    if (progress_ < 0.0F) {
        const auto cycleMs = static_cast<double>(
            shell.theme().motion.progressIndeterminateCycleMs);
        if (cycleMs > 0.0 && shell.motionEnabled()) {
            if (!marqueeAnchored_) {
                marqueeAnchorMs_ = nowMs;
                marqueeAnchored_ = true;
            }
            const double phase = std::fmod(
                static_cast<double>(nowMs - marqueeAnchorMs_), cycleMs * 2.0) /
                cycleMs;
            marqueePhase_ = static_cast<float>(phase < 1.0 ? phase : 2.0 - phase);
            marqueeActive = true;
            changed = true;
        } else {
            marqueeAnchored_ = false;  // reduce：哨兵在 build 按主题判定
        }
    } else {
        marqueeAnchored_ = false;
    }

    if (changed) {
        shell.markDirty();
    }
    return messageAnimating() || busyActive || marqueeActive;
}

}  // namespace lumen::widgets
