#pragma once

// Spin 数值步进控件（docs/lumen-spin-design.md，2026-09）：
// TextField 的数值语义包装——值域/步进/提交解析 + 内嵌 stepper 集群
//（上/下半高按钮 + 中缝 1px 分隔线）。零新增 WidgetType / RenderCommand
//（menu-controls 同架构口径：widgets 层控制器 + 既有控件组合）。
//
// 组合结构：Row（外框 + 背景，textfield token）[ field（bind 驱动，边框
// 抑制）, Column[ ▲, 1px 分隔线, ▼ ] ]。field 是唯一 Tab 停靠点（caret
// 即焦点指示，焦点环保持默认关闭——visual-system §5 规则 4）。
//
// 值通道：field 文本经 StateStore bind（key+":value"）携带——键入即写
// store（框架既有编辑路径），控制器按 store 文本即时解析 invalid；Enter/
// 失焦提交（commitText → setValue），Escape 恢复上次已提交值。
//
// 输入四路汇入同一 setValue：stepper 单击/按住自动重复（500ms 延迟后
// 每 60ms 一步，motion.spin.repeat.*——输入节奏，reduceAnimation 不归
// 零）、键盘 ↑↓/PgUp/PgDn/Home/End（应用 onKey 转发 handleKey）、滚轮
//（应用 onWheel 转发 handleWheel）、直接键入。到界"顶住"（Splitter §7.1
// 同手感）：对应 stepper chevron 淡化（仍可命中）、步进无位移。

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/geometry.h"
#include "lumen/core/icon_id.h"
#include "lumen/core/widget.h"
#include "lumen/core/windowing.h"
#include "lumen/style/theme.h"

namespace lumen::widgets {

class SpinController {
  public:
    explicit SpinController(double initialValue = 0.0,
                            std::string key = {});

    // --- 值域与步进（design §5.2） ---
    void setRange(double min, double max);   // 默认 [0, 100]
    void setStep(double step);               // 默认 1
    void setPageStep(double step);           // 默认 10 × step
    void setDecimals(int digits);            // 默认 0（整数）
    void setWrap(bool wrap);                 // 默认 false（到界顶住）
    void setControlSize(core::ControlSize size);  // 默认 Medium（随密度）
    void setLabel(std::string label);        // 语义标签（如"不透明度"）
    // 整控件禁用（design §9.3 disabled 行）：field 与 stepper 视觉、
    // 命中、键盘一致拒绝。默认可用。
    void setEnabled(bool enabled);

    // setValue：钳制（或环绕）→ step 网格 snap → decimals 舍入 → 写
    // store + onValueChanged。四路输入全部经此单点。
    void setValue(double v);
    [[nodiscard]] double value() const { return value_; }
    [[nodiscard]] bool atMin() const;
    [[nodiscard]] bool atMax() const;
    [[nodiscard]] std::string formatValue() const;

    // 步进通道（按钮/键盘/滚轮共用；自动重复逐拍调用）。以当前编辑文本
    //（可解析时）为基数——键入候选优先于上次提交值。
    void stepUp();
    void stepDown();

    // --- 编辑提交（design §6.2） ---
    // text → parse → 成功：setValue + onCommitted、清 invalid；失败：
    // 保持 invalid、不提交。
    [[nodiscard]] bool commitText(const std::string& text);
    // field 文本恢复为上次已提交值（Escape / 非法失焦）。
    void revert();
    // 当前 store 文本是否不可解析（编辑中 invalid 态）。
    [[nodiscard]] bool editingInvalid() const;

    // --- 装配与组合件 ---
    // 注册 stepper handler + 播种 store（应用装配期调用一次）。
    void attach(app::AppShell& shell);
    // 组合 Widget（应用 build 每帧调用）。非 const：失焦提交（≡ Enter）
    // 在此检测（design §6.2——焦点变化驱动重建，AppShell 既有口径）。
    [[nodiscard]] core::Widget build(const style::Theme& theme);

    // 键盘契约（应用 ShellConfig.onKey 转发；仅焦点在本 field 时消费）：
    // ↑↓ ±step、PgUp/PgDn ±pageStep、Home/End 到界、Enter 提交、
    // Escape 恢复（有可恢复内容时才消费）。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers mods = core::kModifierNone,
                   char keyChar = 0);
    // 滚轮（应用 onWheel 转发）：指针在控件上每档 ±step（到界停止）。
    [[nodiscard]] bool handleWheel(app::AppShell& shell,
                                   core::Offset position, core::Offset delta);
    // 按住自动重复（应用 ShellConfig.onAnimate 转发；返回 true = 按住中，
    // 继续请求动画帧）。计时只在按下期间活跃，release 即停——无空转。
    bool step(app::AppShell& shell, std::uint64_t nowMs);

    // 回调（UI 线程）。onValueChanged 每步进拍触发（自动重复期间连续，
    // 重建单帧吸收）；onCommitted 仅在提交点（Enter/失焦/commitText）。
    std::function<void(double value)> onValueChanged{};
    std::function<void(double value)> onCommitted{};

  private:
    [[nodiscard]] std::string fieldKey() const;
    [[nodiscard]] std::string bindKey() const;
    [[nodiscard]] std::string upKey() const;
    [[nodiscard]] std::string downKey() const;
    [[nodiscard]] std::optional<double> currentTextValue() const;
    void stepBy(double delta);
    void tapStep(int direction);  // stepper 单击（按住期间已步进则吞并）
    double clampOrWrap(double v) const;
    double roundToDecimals(double v) const;
    void syncStore();

    std::string key_{};
    std::string label_{};
    app::AppShell* shell_{nullptr};
    double value_{0.0};
    double min_{0.0};
    double max_{100.0};
    double step_{1.0};
    double pageStep_{10.0};
    int decimals_{0};
    bool wrap_{false};
    bool enabled_{true};
    core::ControlSize controlSize_{core::ControlSize::Medium};
    bool attached_{false};
    bool wasFocused_{false};
    // 按住自动重复状态（step() 驱动）。
    bool holdActive_{false};
    bool holdStepped_{false};  // 本次按住已有步进（release 单击吞并判定）
    std::uint64_t holdNextMs_{0};
};

}  // namespace lumen::widgets
