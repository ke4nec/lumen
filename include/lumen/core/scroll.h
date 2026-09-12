#pragma once

#include "lumen/core/geometry.h"
#include "lumen/core/interaction.h"

namespace lumen::core {

// v0.3 阶段8D (plan §3.4): 滚动控制器。
//
// 应用为每个滚动视口持有一个 ScrollController：布局后喂入视口/内容尺
// 寸（updateExtents），滚轮、触摸拖动、键盘和语义 scroll action 统一汇
// 聚到 scrollBy/applyKey/semanticScroll；偏移变化后应用重建 Widget 并
// 经 withScrollOffset 写回。确定性：默认无惯性（plan §3.4 首期）。
class ScrollController {
  public:
    // 视口/内容主轴尺寸（像素，纵向）。每帧布局后调用。
    void updateExtents(float viewportExtent, float contentExtent);

    // 像素级滚动：正 = 内容向下滚（offset 增大）。返回偏移是否变化。
    bool scrollBy(float delta);
    // 绝对定位（夹取到 [0, maxScrollOffset]）。
    void scrollTo(float offset);

    // 滚轮（HostEvent.scrollDelta.y，正 = 向下）。
    bool applyWheel(float deltaY);
    // 触摸/指针拖动：内容跟随手指（offset -= delta）。
    bool applyDrag(float deltaY);
    // 键盘滚动：PageUp/PageDown/Home/End/Up/Down。
    bool applyKey(Key key, float viewportExtent);
    // 语义 scroll action（plan §3.3）。
    bool semanticScroll(float deltaY);

    [[nodiscard]] float offset() const { return offset_; }
    [[nodiscard]] float viewportExtent() const { return viewportExtent_; }
    [[nodiscard]] float maxScrollOffset() const { return maxOffset_; }
    [[nodiscard]] bool canScroll() const { return maxOffset_ > 0.0F; }
    // 视口可见内容比例（滚动条/语义 value 用）。
    [[nodiscard]] float visibleFraction() const;

    // 拖动灵敏度（内容跟随指针 1:1）；滚轮行高换算在宿主层完成。
    static constexpr float kDragRatio = 1.0F;

  private:
    float clampOffset(float value) const;

    float offset_{0.0F};
    float maxOffset_{0.0F};
    float viewportExtent_{0.0F};
    float contentExtent_{0.0F};
};

}  // namespace lumen::core
