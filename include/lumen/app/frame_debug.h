#pragma once

// R6（completion-plan 阶段D / m15-roadmap M18）：帧阶段读数 HUD。
//
// FrameDebugSnapshot 由 AppShell 在帧管线内采样（setFrameStatsCapture
// 默认关闭：不采样、不改变帧节奏与 frame hash；开启后 paintFrame 记录
// reconcile/layout/paint 阶段耗时、fps 环与节点计数）。本头文件只做两
// 件事：快照值类型 + 把快照组合成非模态视觉 overlay（左上角面板，
// Stack 承载、排除语义与焦点——诊断图层不进入读屏树）。
//
// 语义边界：图层在提交前绘制，读数来自上一帧的采样（滞后一帧）；命令
// 存储分配只覆盖 RenderCommandList 的 vector 容量增长，不是整帧堆分配。
// 文本含真实时间读数，因此开启 HUD 的帧不做确定性 hash 对照。

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "lumen/core/render_node.h"
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
    std::uint64_t commandStorageAllocationCount{0};
    std::uint64_t commandStorageAllocatedBytes{0};
    std::uint64_t commandStoragePeakBytes{0};
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

[[nodiscard]] inline std::uint64_t ceilKiB(std::uint64_t bytes) {
    return bytes / 1024U + (bytes % 1024U == 0 ? 0U : 1U);
}

// 合成一行读数 Text 节点（11px；诊断图层小字号，密度无关；offset 为
// 面板内绝对数据——本层不经布局）。
[[nodiscard]] inline core::RenderNode statLine(core::Offset offset,
                                               float width,
                                               const std::string& content,
                                               core::Color color) {
    core::RenderNode node;
    node.type = core::WidgetType::Text;
    node.offset = offset;
    node.size = core::Size{width, 15.0F};
    node.text = content;
    core::CommonResolvedStyle& common =
        core::commonStyle(node.style.component);
    common.text.fontSize = 11.0F;
    common.text.color = color;
    return node;
}

}  // namespace frame_debug_detail

// 帧读数 HUD 图层（纯绘制 RenderNode 合成树——与 bounds/damage 调试
// 层同架构）：左上角半透明面板 + 固定步进行。R6 修复（2026-09-30）：
// 早前经视觉 overlay 槽位承载会随 eventTree 切换吞掉应用输入——本层
// 在主场景命令后录制，不参与命中/焦点/语义，输入零影响。
// fallbackReason 非空时追加告警行。面板尺寸随行数推导（确定性）。
[[nodiscard]] inline core::RenderNode makeFrameStatsLayer(
    const FrameDebugSnapshot& snapshot, const FrameOverlayStyle& style) {
    using namespace frame_debug_detail;

    char line[160];
    struct Row {
        std::string text{};
        bool accent{false};
    };
    std::vector<Row> rows;
    rows.reserve(6);

    std::snprintf(line, sizeof(line), "lumen frame #%llu %s",
                  static_cast<unsigned long long>(snapshot.frameIndex),
                  snapshot.backendName.c_str());
    rows.push_back(Row{line, true});

    std::snprintf(line, sizeof(line), "build %.2fms  layout %.2fms  paint %.2fms",
                  snapshot.reconcileMs, snapshot.layoutMs, snapshot.paintMs);
    rows.push_back(Row{line, false});

    std::snprintf(line, sizeof(line),
                  "cmd storage %llu alloc / %llu KiB / %llu KiB peak",
                  static_cast<unsigned long long>(
                      snapshot.commandStorageAllocationCount),
                  static_cast<unsigned long long>(
                      ceilKiB(snapshot.commandStorageAllocatedBytes)),
                  static_cast<unsigned long long>(
                      ceilKiB(snapshot.commandStoragePeakBytes)));
    rows.push_back(Row{line, false});

    std::snprintf(line, sizeof(line), "submit %.2fms  gpu wait %.2fms  fps %.1f",
                  snapshot.submitMs, snapshot.gpuWaitMs, snapshot.fps);
    rows.push_back(Row{line, false});

    std::snprintf(line, sizeof(line), "nodes %llu  cmds %llu  culled %llu",
                  static_cast<unsigned long long>(snapshot.nodeCount),
                  static_cast<unsigned long long>(snapshot.commandCount),
                  static_cast<unsigned long long>(snapshot.culledCommands));
    rows.push_back(Row{line, false});

    if (snapshot.fullFrameFallback || !snapshot.fallbackReason.empty()) {
        std::snprintf(line, sizeof(line), "fallback: %s",
                      snapshot.fallbackReason.empty()
                          ? "full-frame"
                          : snapshot.fallbackReason.c_str());
        rows.push_back(Row{line, true});
    }

    constexpr float kMargin = 8.0F;
    constexpr float kPadX = 10.0F;
    constexpr float kPadY = 7.0F;
    constexpr float kLineStep = 15.0F;
    constexpr float kPanelWidth = 280.0F;
    const float panelHeight =
        kPadY * 2.0F + static_cast<float>(rows.size()) * kLineStep;

    core::RenderNode panel;
    panel.type = core::WidgetType::Container;
    panel.offset = core::Offset{kMargin, kMargin};
    panel.size = core::Size{kPanelWidth, panelHeight};
    panel.excludeFromSemantics = true;
    panel.excludeFromFocus = true;
    core::CommonResolvedStyle& common = core::commonStyle(panel.style.component);
    common.background = style.panel;
    common.radius = core::CornerRadius::all(6.0F);

    const float textWidth = kPanelWidth - kPadX * 2.0F;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        panel.children.push_back(statLine(
            core::Offset{kPadX, kPadY + static_cast<float>(i) * kLineStep},
            textWidth, rows[i].text,
            rows[i].accent ? style.accentText : style.text));
    }
    return panel;
}

}  // namespace lumen::app
