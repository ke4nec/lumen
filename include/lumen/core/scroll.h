#pragma once

#include <cstdint>

#include "lumen/core/geometry.h"
#include "lumen/core/interaction.h"

namespace lumen::core {

// v0.3 阶段8D (plan §3.4): 滚动控制器。
//
// 应用为每个滚动视口持有一个 ScrollController：布局后喂入视口/内容尺
// 寸（updateExtents），滚轮、触摸拖动、键盘和语义 scroll action 统一汇
// 聚到 scrollBy/applyKey/semanticScroll；偏移变化后应用重建 Widget 并
// 经 withScrollOffset 写回。M10：拖动结束可起惯性 fling（确定性指数
// 衰减，时间戳全部由调用方注入）。
class ScrollController {
  public:
    // 视口/内容主轴尺寸（像素，纵向）。每帧布局后调用。
    void updateExtents(float viewportExtent, float contentExtent);

    // 像素级滚动：正 = 内容向下滚（offset 增大）。返回偏移是否变化。
    bool scrollBy(float delta);
    // 绝对定位（夹取到 [0, maxScrollOffset]）。
    void scrollTo(float offset);

    // 滚轮（HostEvent.scrollDelta.y，正 = 向下）。接管时停止惯性。
    bool applyWheel(float deltaY);
    // 触摸/指针拖动：内容跟随手指（offset -= delta）；拖动接管惯性。
    bool applyDrag(float deltaY);
    // 键盘滚动：PageUp/PageDown/Home/End/Up/Down。
    bool applyKey(Key key, float viewportExtent);
    // 语义 scroll action。
    bool semanticScroll(float deltaY);

    // --- M10：触摸拖动惯性（fling） ---
    // 拖动速度采样（每次 pointer move；内容跟随由 applyDrag 完成）。
    // 同一时间戳内的多次移动累积，dt>0 才更新速度。
    void noteDragSample(float deltaYPixels, std::uint64_t timestampMs);
    // 拖动结束（pointer up）：冲洗采样并按阈值起 fling；返回是否开始。
    bool endDrag(std::uint64_t timestampMs);
    // 推进一拍（tick 驱动；指数衰减）；返回 true = 仍在惯性中。
    // 到达边界或速度低于停止阈值时结束。
    bool stepFling(std::uint64_t nowMs);
    [[nodiscard]] bool isFlinging() const {
        return flingVelocityPxMs_ != 0.0F;
    }
    // 新输入接管（滚轮/键盘/再次拖动）：立即停止惯性。
    void stopFling();

    [[nodiscard]] float offset() const { return offset_; }
    [[nodiscard]] float viewportExtent() const { return viewportExtent_; }
    [[nodiscard]] float maxScrollOffset() const { return maxOffset_; }
    [[nodiscard]] bool canScroll() const { return maxOffset_ > 0.0F; }
    // 视口可见内容比例（滚动条/语义 value 用）。
    [[nodiscard]] float visibleFraction() const;

    // 拖动灵敏度（内容跟随指针 1:1）；滚轮行高换算在宿主层完成。
    static constexpr float kDragRatio = 1.0F;
    // fling 物理常量（确定性：只依赖注入时间戳，无随机/真实时钟）。
    static constexpr float kFlingStartPxPerMs = 0.15F;  // 起滑 ~150px/s
    static constexpr float kFlingStopPxPerMs = 0.05F;   // 停止 ~50px/s
    static constexpr double kFlingTauMs = 160.0;        // 衰减时间常数

  private:
    float clampOffset(float value) const;

    float offset_{0.0F};
    float maxOffset_{0.0F};
    float viewportExtent_{0.0F};
    float contentExtent_{0.0F};
    // 拖动速度采样（手指位移向下为正）。
    bool dragSampled_{false};
    std::uint64_t lastDragSampleMs_{0};
    float pendingDragDelta_{0.0F};
    float dragVelocityPxMs_{0.0F};
    // fling 速度（offset 方向；= -手指速度）与上次推进时间戳。
    float flingVelocityPxMs_{0.0F};
    std::uint64_t flingLastMs_{0};
};

}  // namespace lumen::core
