#pragma once

// R6（completion-plan 阶段D / m15-roadmap M18）：帧阶段读数 HUD。
//
// FrameDebugSnapshot 由 AppShell 在帧管线内采样（setFrameStatsCapture
// 默认关闭：不采样、不改变帧节奏与 frame hash；开启后 paintFrame 记录
// reconcile/layout/paint 阶段耗时、fps 环与节点计数）。本头文件只做两
// 件事：快照值类型 + 把快照组合成非模态视觉 overlay（左上角面板，
// Stack 承载、排除语义与焦点——诊断图层不进入读屏树）。
//
// 语义边界：overlay builder 在重建期求值，读到的是上一帧的统计（读数
// 滞后一帧）；分配量统计未接入（renderer stats 无该维度，见
// support-matrix R6 登记）。文本含真实时间读数，因此开启 HUD 的帧不做
// 确定性 hash 对照。

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/style/theme.h"

namespace lumen::app {

// 一帧的阶段统计（AppShell::frameDebugSnapshot 组装；时间单位 ms）。
struct FrameDebugSnapshot {
    std::uint64_t frameIndex{0};
    double reconcileMs{0.0};  // element reconcile（rebuild 总耗时 − layout）
    double layoutMs{0.0};     // LayoutEngine（主树 + overlay，帧内累计）
    double paintMs{0.0};      // 命令录制（renderer stats cpuBuildMs）
    double submitMs{0.0};
    double gpuWaitMs{0.0};
    double fps{0.0};  // 最近 1s 完成帧数；不足两个样本为 0
    std::uint64_t nodeCount{0};       // 主树 + overlay RenderNode 计数
    std::uint64_t commandCount{0};    // renderer stats
    std::uint64_t culledCommands{0};  // renderer stats
    bool fullFrameFallback{false};
    std::string fallbackReason{};
    std::string backendName{};  // renderer capabilities
};

// HUD 配色（Theme token 派生；不硬编码色值）。
struct FrameOverlayStyle {
    core::Color panel{};
    core::Color text{};
    core::Color accentText{};

    // surfaceElevated（面板底，82% 不透明度——覆盖应用内容仍可读）/
    // contentPrimary（读数）/ accentContent（标题与回退告警）。
    [[nodiscard]] static FrameOverlayStyle fromTheme(const style::Theme& theme) {
        return FrameOverlayStyle{
            core::scaleColorAlpha(theme.colors.surfaceElevated, 0.82F),
            theme.colors.contentPrimary, theme.colors.accentContent};
    }
};

namespace frame_debug_detail {

// 组装一行读数 Text（11px；诊断图层小字号，密度无关）。
[[nodiscard]] inline core::Widget statLine(std::string content,
                                           core::Color color, std::string key) {
    core::TextStyle style;
    style.fontSize = 11.0F;
    style.color = color;
    return core::withKey(core::makeText(std::move(content), style),
                         std::move(key));
}

}  // namespace frame_debug_detail

// 帧读数 HUD（非模态视觉 overlay 根）：Stack 铺满视口，左上角半透明
// 面板承载阶段/计数读数。全子树 excludeFromSemantics + excludeFromFocus
//（诊断图层不进入读屏树与 Tab 序）。fallbackReason 非空时追加告警行。
[[nodiscard]] inline core::Widget makeFrameStatsOverlay(
    const FrameDebugSnapshot& snapshot, const FrameOverlayStyle& style) {
    using namespace frame_debug_detail;

    char line[160];
    std::vector<core::Widget> lines;
    lines.reserve(5);

    std::snprintf(line, sizeof(line), "lumen frame #%llu %s",
                  static_cast<unsigned long long>(snapshot.frameIndex),
                  snapshot.backendName.c_str());
    lines.push_back(statLine(line, style.accentText, "frame-stats-title"));

    std::snprintf(line, sizeof(line),
                  "build %.2fms  layout %.2fms  paint %.2fms",
                  snapshot.reconcileMs, snapshot.layoutMs, snapshot.paintMs);
    lines.push_back(statLine(line, style.text, "frame-stats-stages"));

    std::snprintf(line, sizeof(line),
                  "submit %.2fms  gpu wait %.2fms  fps %.1f",
                  snapshot.submitMs, snapshot.gpuWaitMs, snapshot.fps);
    lines.push_back(statLine(line, style.text, "frame-stats-submit"));

    std::snprintf(line, sizeof(line), "nodes %llu  cmds %llu  culled %llu",
                  static_cast<unsigned long long>(snapshot.nodeCount),
                  static_cast<unsigned long long>(snapshot.commandCount),
                  static_cast<unsigned long long>(snapshot.culledCommands));
    lines.push_back(statLine(line, style.text, "frame-stats-counts"));

    if (snapshot.fullFrameFallback || !snapshot.fallbackReason.empty()) {
        std::snprintf(line, sizeof(line), "fallback: %s",
                      snapshot.fallbackReason.empty()
                          ? "full-frame"
                          : snapshot.fallbackReason.c_str());
        lines.push_back(statLine(line, style.accentText, "frame-stats-fallback"));
    }

    core::Widget panel = core::makeContainer(
        core::makeColumn(std::move(lines), core::MainAxisAlignment::Start,
                         core::CrossAxisAlignment::Start, 2.0F),
        std::nullopt, std::nullopt,
        core::EdgeInsets{8.0F, 6.0F, 8.0F, 6.0F},
        core::EdgeInsets{8.0F, 8.0F, 8.0F, 8.0F}, style.panel,
        core::CornerRadius::all(6.0F), "frame-stats-panel");
    panel.excludeFromSemantics = true;
    panel.excludeFromFocus = true;

    core::Widget root = core::makeStack({std::move(panel)},
                                        core::StackAlignment::TopLeft, {}, {},
                                        "frame-stats-overlay");
    root.excludeFromSemantics = true;
    root.excludeFromFocus = true;
    return root;
}

}  // namespace lumen::app
