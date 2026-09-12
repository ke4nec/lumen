#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"

namespace lumen::accessibility {

// v0.3 阶段8C (plan §3.3): 平台无障碍桥接契约。
//
// 桌面桥接（Windows UIA / Linux AT-SPI / macOS NSAccessibility）是可选
// 编译目标：SDK 或运行环境不可用时工厂返回 nullptr，能力查询报 false，
// 桥接失败只关闭该能力，不影响绘制与输入（plan §2.3 不变量）。
// headless 环境用 RecordingAccessibilityBridge 验证桥接协议本身。
// 移动端语义桥接的公共契约在 v0.3 冻结；原生 accessibility tree 属
// v0.4（plan §3.3）。

// 可访问性设置只读查询（plan 8C：高对比、减少动画、系统字体缩放）。
struct AccessibilitySettings {
    bool highContrast{false};
    bool reduceAnimation{false};
    float fontScale{1.0F};
};

class AccessibilityBridge {
  public:
    virtual ~AccessibilityBridge() = default;

    [[nodiscard]] virtual std::string bridgeName() const = 0;
    // 桥接可用（SDK 编入且平台服务可達）。
    [[nodiscard]] virtual bool available() const = 0;

    // 全量/增量更新：首次传整个树（diff.added = 全部节点），之后传
    // identity diff；focusedId 为当前语义焦点（空 = 无）。
    virtual void updateTree(const SemanticsTree& tree,
                            const SemanticsDiff& diff,
                            const std::string& focusedId) = 0;
    // 辅助技术焦点跟踪。
    virtual void setFocusedNode(const std::string& id) = 0;

    // M5：语义 action 结果记录（应用壳分发后回执；默认 no-op）。
    virtual void noteActionPerformed(const std::string& nodeId,
                                     std::uint32_t action,
                                     SemanticsActionStatus status) {
        (void)nodeId;
        (void)action;
        (void)status;
    }
};

// headless/测试桥：记录 updateTree/setFocusedNode 事件序列，断言桥接
// 协议与 identity 稳定性（plan §5.1 语义测试）。
class RecordingAccessibilityBridge final : public AccessibilityBridge {
  public:
    struct UpdateRecord {
        std::size_t treeSize{0};
        SemanticsDiff diff{};
        std::string focusedId{};
    };

    std::string bridgeName() const override { return "recording"; }
    bool available() const override { return true; }

    void updateTree(const SemanticsTree& tree, const SemanticsDiff& diff,
                    const std::string& focusedId) override {
        UpdateRecord record;
        record.treeSize = tree.size();
        record.diff = diff;
        record.focusedId = focusedId;
        updates.push_back(std::move(record));
    }

    void setFocusedNode(const std::string& id) override {
        focusedNodes.push_back(id);
    }

    std::vector<UpdateRecord> updates{};
    std::vector<std::string> focusedNodes{};

    struct ActionRecord {
        std::string nodeId{};
        std::uint32_t action{0};
        SemanticsActionStatus status{SemanticsActionStatus::NotHandled};
        bool operator==(const ActionRecord&) const = default;
    };
    std::vector<ActionRecord> actions{};

    void noteActionPerformed(const std::string& nodeId, std::uint32_t action,
                             SemanticsActionStatus status) override {
        actions.push_back(ActionRecord{nodeId, action, status});
    }
};

// 平台桥接工厂（预留接口；当前仓库未包含桌面 provider，因此返回 nullptr
// 并把原因写入 *diagnostics）。RecordingAccessibilityBridge 用于验证协议。
[[nodiscard]] std::unique_ptr<AccessibilityBridge>
createPlatformAccessibilityBridge(std::string* diagnostics = nullptr);

// 阶段标识。
[[nodiscard]] const char* accessibilityStageName();

}  // namespace lumen::accessibility
