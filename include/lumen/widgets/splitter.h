#pragma once

// SplitterController（docs/lumen-splitter-design.md §5.2/§5.3）：分栏位置
// 状态持有者（应用拥有，UI 线程独占）。
//
// 位置 = 分隔条 leading 缘到容器 leading 缘的绝对像素（GTK position 模
// 式；比例仅派生只读）。窗口 resize 默认 KeepOffset：位置保持、钳制在
// [minLeading, extent - minTrailing]，trailing 吃掉增量（文件管理器直
// 觉）；KeepRatio（§5.3）= 两窗格按比例分摊增量（offset 随 extent 等
// 比缩放后钳制）。实现 core::SplitterSource 供布局/交互驱动——状态成员
// mutable（const 接口契约同 VirtualListSource::noteExtent 先例，布局期
// 回填不经回调）。
//
// M17 塌缩（§5.3）：collapsible 开启后 setCollapsed/toggleCollapsed 把
// 分隔条钉到 minLeading（leading 窗格 0 宽），展开恢复塌缩前位置；塌缩
// 中任何把 offset 移离 min 的输入（拖动/步进）自动解除塌缩。

#include <algorithm>
#include <functional>
#include <limits>

#include "lumen/core/splitter.h"

namespace lumen::widgets {

class SplitterController final : public core::SplitterSource {
  public:
    explicit SplitterController(float initialOffsetPx = 240.0F)
        : initial_(initialOffsetPx), offset_(initialOffsetPx) {}

    // --- M17：resize 行为与塌缩（splitter-design §5.3） ---
    enum class ResizeBehavior : std::uint8_t { KeepOffset, KeepRatio };
    void setResizeBehavior(ResizeBehavior behavior) {
        resizeBehavior_ = behavior;
    }
    [[nodiscard]] ResizeBehavior resizeBehavior() const {
        return resizeBehavior_;
    }
    void setCollapsible(bool collapsible) { collapsible_ = collapsible; }
    [[nodiscard]] bool collapsible() const { return collapsible_; }
    // 塌缩/展开（collapse = 钉到 minLeading；expand = 回塌缩前位置钳制）。
    // 已是目标状态时 no-op（返回 false）。onOffsetChanged 随位移触发。
    bool setCollapsed(bool collapsed) const;
    [[nodiscard]] bool collapsed() const override { return collapsed_; }

    // --- 程序读写（应用侧；修改后自行 markDirty 请求重建） ---
    // 钳制按最近一次布局回填的 extent（未知时仅下限）。
    void setOffset(float px) {
        seeded_ = true;
        applyOffset(px);
    }
    [[nodiscard]] float offset() const { return offset_; }
    // 派生只读比例（extent 未知时 0）。
    [[nodiscard]] float ratio() const {
        return lastExtent_ > 0.0F ? offset_ / lastExtent_ : 0.0F;
    }
    void setMinLeading(float px) { minLeading_ = std::max(0.0F, px); }
    void setMinTrailing(float px) { minTrailing_ = std::max(0.0F, px); }
    // 复位目标（首次布局播种同值）。
    void setResetOffset(float px) { initial_ = std::max(0.0F, px); }
    // 程序复位（等价分隔条双击）。
    void resetToInitial() {
        seeded_ = true;
        applyOffset(initial_);
    }

    // 位置变化回调（拖动逐拍/键盘步进/复位触发；UI 线程；持久化用）。
    // 布局期 noteLayout 回填不触发（回调里不得同步重建）。
    std::function<void(float offsetPx)> onOffsetChanged{};

    // --- core::SplitterSource（布局/交互驱动；见 core/splitter.h） ---
    [[nodiscard]] float offsetPx() const override { return offset_; }
    [[nodiscard]] float minLeading() const override { return minLeading_; }
    [[nodiscard]] float minTrailing() const override { return minTrailing_; }
    [[nodiscard]] float initialOffset() const override { return initial_; }
    [[nodiscard]] bool seeded() const override { return seeded_; }
    [[nodiscard]] float extentPx() const override { return lastExtent_; }
    void noteLayout(float clampedOffset, float extent) const override;
    void dragTo(float offsetPx) const override { applyOffset(offsetPx); }
    void stepBy(float deltaPx) const override {
        applyOffset(offset_ + deltaPx);
    }
    void stepToEdge(bool maxEdge) const override {
        applyOffset(maxEdge ? maxOffset() : minLeading_);
    }
    void reset() const override { applyOffset(initial_); }
    // M17：塌缩切换（Enter/Space 聚焦分隔条；不可塌缩返回 false）。
    bool toggleCollapse() const override { return setCollapsed(!collapsed_); }

  private:
    [[nodiscard]] float maxOffset() const {
        return lastExtent_ > 0.0F
                   ? std::max(minLeading_, lastExtent_ - minTrailing_)
                   : std::numeric_limits<float>::max();
    }
    [[nodiscard]] float clampOffset(float px) const {
        return std::clamp(px, minLeading_, maxOffset());
    }
    void applyOffset(float px) const;

    mutable float offset_{};
    mutable bool seeded_{false};
    mutable float lastExtent_{0.0F};
    // M17：塌缩状态与展开恢复点（collapse 时刻的钳制位置）。
    mutable bool collapsed_{false};
    mutable float restore_{0.0F};
    ResizeBehavior resizeBehavior_{ResizeBehavior::KeepOffset};
    bool collapsible_{false};
    float minLeading_{core::kSplitterDefaultMinPane};
    float minTrailing_{core::kSplitterDefaultMinPane};
    float initial_{};
};

}  // namespace lumen::widgets
