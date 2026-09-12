#pragma once

// M3（自用路线图）：VirtualList 控制器。
//
// 应用拥有的数据源适配器：实现 core::VirtualListSource 供布局读取
//（itemCount/estimatedExtent/extentOf/scrollOffset/buildItem），同时
// 提供滚动输入（复用 ScrollController：滚轮/键盘/触摸拖动/语义）、
// 可见区计算与实测 extent 缓存。
//
// 锚点稳定：视口上方项目的实测修正会同步平移 offset（视觉锚点不跳），
// itemCount 收缩时 offset 夹取到新 [0, max]（不跳回顶部）。
// UI 线程独占；extent 缓存经布局期 noteExtent 回填（幂等）。

#include <cstddef>
#include <functional>
#include <map>
#include <utility>

#include "lumen/core/scroll.h"
#include "lumen/core/widget.h"

namespace lumen::core {

class VirtualListController final : public VirtualListSource {
  public:
    VirtualListController() = default;

    // --- 数据装配（UI 线程） ---
    // 数据项数量变化：清理超界缓存并立即夹取 offset。
    void setItemCount(std::size_t count);
    // 初始估算高度（未测量项）。
    void setEstimatedExtent(float extent);
    // 项目构建器：必须返回携带含 index 稳定 key 的 Widget。
    void setItemBuilder(std::function<Widget(std::size_t)> builder);

    // --- VirtualListSource（布局读取） ---
    [[nodiscard]] std::size_t itemCount() const override;
    [[nodiscard]] float estimatedExtent() const override;
    [[nodiscard]] float extentOf(std::size_t index) const override;
    [[nodiscard]] float scrollOffset() const override;
    [[nodiscard]] float totalExtent() const override;
    [[nodiscard]] float offsetOfIndex(std::size_t index) const override;
    [[nodiscard]] std::pair<std::size_t, std::size_t> visibleRange(
        float viewportExtent, float cacheExtent) const override;
    [[nodiscard]] Widget buildItem(std::size_t index) const override;
    void noteExtent(std::size_t index, float extent) const override;
    void updateViewport(float viewportExtent, float contentPadding) const override;

    // --- 滚动输入（与 ScrollView 相同的统一路径） ---
    [[nodiscard]] ScrollController& scroll() { return scroll_; }
    [[nodiscard]] const ScrollController& scroll() const { return scroll_; }

    // 键盘/语义定位：以最小移动使项 index 可见。
    void scrollToIndex(std::size_t index, float viewportExtent);

    // 每帧布局后由应用调用：布局期回填是否引起内容高度变化（应用据此
    // 请求一次重建）；消费即清除。
    [[nodiscard]] bool consumeExtentsChanged();

  private:
    // offset 持有于 ScrollController；mutable：布局期 const noteExtent
    // 的锚点平移经 scrollBy 修改（UI 线程独占）。
    mutable ScrollController scroll_{};
    mutable float contentPadding_{0.0F};
    std::size_t itemCount_{0};
    float estimatedExtent_{44.0F};
    std::function<Widget(std::size_t)> itemBuilder_{};
    // index → 实测高度（超界项在 setItemCount 时清理）。
    // mutable：布局期经 const noteExtent 回填（幂等缓存）。
    mutable std::map<std::size_t, float> measured_{};
    mutable bool extentsChanged_{false};
};

}  // namespace lumen::core
