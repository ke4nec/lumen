#pragma once

// StatusBar 状态栏（docs/lumen-statusbar-design.md，2026-09）：窗口底部
// 信息 chrome——弹性消息区（瞬态/常驻）+ 右侧常驻项序列（Text/Progress/
// Busy/Toggle/Separator）+ resize grip。零新增 WidgetType（ProgressBar 的
// indeterminate 为唯一 core 缝隙，widget.h packed bool）。
//
// 动效（design §10，全部低幅度、可关闭、不位移）：
// - 消息淡切 statusbarMessageFadeMs=120ms（淡出→替换→淡入，无位移；
//   控制器自持 alpha 相位，menu M14 同"pending → 首 tick 起表"口径）；
// - 瞬态驻留 statusbarMessageTimeoutMs=4000ms 超时回 idle（内容节律，
//   reduceAnimation 保留）；
// - indeterminate 往返段 progressIndeterminateCycleMs=1400ms（相位经
//   scrollOffset 0..1，painter 消费）；determinate 值即状态不补间；
// - busy 弧旋转 statusbarBusyCycleMs=1200ms（IconId::Busy 3/4 弧 +
//   Widget.iconRotation 逐 tick，painter 归一化折线旋转）。
// reduceAnimation：淡切归零（即时切换）、往返/旋转停止（静止形状保留
// "进行中"语义，不靠运动传达唯一信息）。
//
// 交互面：仅 Toggle 项可点击（onItemClicked；行为归应用）。状态栏不抢
// 键盘焦点；grip 为纯视觉件（命中归平台 8 逻辑 px resize 边，
// titlebar-design §4.2），最大化时应用经 setShowResizeGrip 隐藏。

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/geometry.h"
#include "lumen/core/icon_id.h"
#include "lumen/core/widget.h"
#include "lumen/style/theme.h"

namespace lumen::widgets {

enum class StatusItemKind : std::uint8_t {
    Text,       // 文本项（可带 icon 前缀）
    Progress,   // determinate 进度项（内嵌 ProgressBar；120/144×4）
    Busy,       // busy 旋转弧项（12px，indeterminate 的紧凑形态）
    Toggle,     // 可点击项（hover 反馈 + onItemClicked；行为归应用）
    Separator,  // 项间分隔线（1px × 12 垂直；不可交互）
};

struct StatusBarItem {
    std::string id{};          // 行 key = "<key>:item:<id>"
    StatusItemKind kind{StatusItemKind::Text};
    core::IconId icon{core::IconId::None};
    std::string text{};
    bool enabled{true};        // false = disabled.content + 命中拒绝
    float fixedWidth{0.0F};    // 0 = 内容自适应（Toggle 项可显式收窄）

    [[nodiscard]] bool operator==(const StatusBarItem&) const = default;
};

class StatusBarController {
  public:
    explicit StatusBarController(std::string key = {});

    // --- 消息区（design §5.1） ---
    // timeoutMs > 0 = 瞬态（驻留后淡出回 idle）；0 = 常驻。新消息取代
    // 旧消息（不排队）；淡切由 step() 逐 tick 驱动。
    void setMessage(std::string text, std::uint32_t timeoutMs = 0);
    void setIdleMessage(std::string text);
    [[nodiscard]] const std::string& message() const { return message_; }

    // --- 项序列与状态 ---
    void setItems(std::vector<StatusBarItem> items);
    // percent 0..100（determinate，值即状态不补间）；< 0 = indeterminate
    //（进度项转往返段）。
    void setProgress(float percent);
    [[nodiscard]] float progress() const { return progress_; }
    // busy 开关（busy 项显示/隐藏；与进度项独立）。
    void setBusy(bool busy);
    [[nodiscard]] bool busy() const { return busy_; }
    // resize grip 显隐（默认 false；customTitleBar && !maximized 时应用
    // 自行开启——WindowMetrics.maximized 经 RunOptions.onEvent 维护）。
    void setShowResizeGrip(bool show);

    // --- 装配与组合件 ---
    void attach(app::AppShell& shell);
    [[nodiscard]] core::Widget build(const style::Theme& theme) const;

    // 时间驱动步进（应用 ShellConfig.onAnimate 转发）：消息淡切相位、
    // 瞬态超时、busy 旋转、indeterminate 往返相位。值变化 markDirty；
    // 返回 true = 仍有活动动画（继续请求动画帧；等待期不占帧）。
    bool step(app::AppShell& shell, std::uint64_t nowMs);

    // Toggle 项回调（id 为 StatusBarItem.id；行为完全归应用）。
    std::function<void(const std::string& id)> onItemClicked{};

  private:
    [[nodiscard]] std::string msgKey() const;
    [[nodiscard]] std::string itemKey(const std::string& id) const;
    [[nodiscard]] std::string percentText(float percent) const;
    void registerItemHandlers(app::AppShell& shell);
    [[nodiscard]] bool messageAnimating() const;
    // 项折叠（design §5.2：窗口变窄消息先省略，再窄项序列从左往右整
    // 项隐藏、消息保底 120px）。决策读上一帧栏几何（toolbar §15 同
    // 口径），markDirty 二次收敛。
    void recomputeFold(app::AppShell& shell) const;

    std::string key_{};
    std::string idleMessage_{"就绪"};
    std::string message_{"就绪"};
    std::vector<StatusBarItem> items_{};
    app::AppShell* shell_{nullptr};
    float progress_{0.0F};
    bool busy_{false};
    bool showResizeGrip_{false};
    bool attached_{false};
    // 折叠状态（build 内回填；mutable —— build 契约与 MenuBar 相同）。
    mutable std::vector<std::string> foldedIds_{};
    mutable std::map<std::string, float> widthCache_{};

    // 消息淡切状态机（step 驱动；menu M14 同"pending → 首 tick 起表"，
    // 不经 tick 的直驱输出保持终态）。
    enum class MsgPhase : std::uint8_t { Steady, FadingOut, FadingIn };
    mutable float msgAlpha_{1.0F};
    mutable MsgPhase msgPhase_{MsgPhase::Steady};
    mutable std::uint64_t msgPhaseStartMs_{0};
    mutable bool msgAnchorNeeded_{false};
    mutable std::optional<std::string> pendingMessage_{};
    mutable std::uint32_t pendingTimeoutMs_{0};
    mutable std::uint64_t transientDeadlineMs_{0};

    // busy/indeterminate 相位（step 驱动；anchor 于首次活跃 tick）。
    mutable float busyRotation_{0.0F};
    mutable std::uint64_t busyAnchorMs_{0};
    mutable bool busyAnchored_{false};
    mutable float marqueePhase_{0.0F};
    mutable std::uint64_t marqueeAnchorMs_{0};
    mutable bool marqueeAnchored_{false};
};

}  // namespace lumen::widgets
