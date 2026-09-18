#pragma once

// Splitter 分栏控件 core 契约（docs/lumen-splitter-design.md）。
//
// 分栏位置状态在 widgets 层控制器（SplitterController 实现
// SplitterSource）；Widget/RenderNode 携带裸指针（virtualSource 同模式：
// UI 线程独占，生命周期由应用保证覆盖布局）。布局与交互只经此接口读
// 写位置——core 不依赖 widgets 层。
//
// 位置语义（splitter-design §5.2，GTK position 模式）：offset = 分隔条
// leading 缘到容器 leading 缘的绝对像素（不含 padding/分隔条本身）；
// 比例仅作派生只读，不参与存储。

#include <cstdint>
#include <limits>

namespace lumen::core {

// 组件尺度（splitter-design §9.1/§9.2；SplitterTokens 目标值，先以 core
// 常量冻结——后续视觉 token 扩展时随 scrollbar 同路径入 Theme）。
// 视觉轨道与布局占位固定 6px；交互节点按密度扩大到
// 12/16/24px，以 6px 轨道为中心透明覆盖两窗格，同时作为键盘步进。
inline constexpr float kSplitterTrackThickness = 6.0F;
inline constexpr float kSplitterRestLineWidth = 1.0F;
inline constexpr float kSplitterActiveLineWidth = 3.0F;
[[nodiscard]] inline constexpr float splitterHitExtent(
    std::uint8_t densityBaseIndex) {
    return densityBaseIndex == 0 ? 12.0F
                                 : (densityBaseIndex == 1 ? 16.0F : 24.0F);
}
[[nodiscard]] inline constexpr float splitterStepPx(
    std::uint8_t densityBaseIndex) {
    return splitterHitExtent(densityBaseIndex);
}
// 默认窗格最小尺寸（splitter-design §9.1；应用可经控制器覆盖）。
inline constexpr float kSplitterDefaultMinPane = 48.0F;

class SplitterSource {
  public:
    virtual ~SplitterSource() = default;

    // 期望分隔条位置（px）。首次布局前返回值被忽略（播种走 initial）。
    [[nodiscard]] virtual float offsetPx() const = 0;
    // 两窗格最小尺寸（逻辑 px）。
    [[nodiscard]] virtual float minLeading() const = 0;
    [[nodiscard]] virtual float minTrailing() const = 0;
    // 播种位置与播种状态（首次布局写入控制器后 seeded）。
    [[nodiscard]] virtual float initialOffset() const = 0;
    [[nodiscard]] virtual bool seeded() const = 0;
    // 最近一次布局的可用主轴长（扣除分隔条）。
    [[nodiscard]] virtual float extentPx() const = 0;
    // 布局回填：钳制后的位置与主轴可用长（扣除分隔条；KeepOffset 的
    // resize 钳制依据）。幂等。
    virtual void noteLayout(float clampedOffset, float extent) const = 0;
    // 交互驱动：绝对目标位置（内部按 noteLayout 的 extent 钳制）。
    virtual void dragTo(float offsetPx) const = 0;
    // 键盘步进（delta 有符号）与到边（maxEdge=false → leading 最小）。
    virtual void stepBy(float deltaPx) const = 0;
    virtual void stepToEdge(bool maxEdge) const = 0;
    // 双击复位（回 initialOffset/setResetOffset 目标）。
    virtual void reset() const = 0;
};

}  // namespace lumen::core
