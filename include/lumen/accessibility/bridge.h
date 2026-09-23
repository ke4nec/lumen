#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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
    bool operator==(const AccessibilitySettings&) const = default;
};

// Unset fields follow the latest system preference; false/1.0 are explicit
// overrides too. Clearing a field resumes following without restarting.
struct AccessibilityOverrides {
    std::optional<bool> highContrast{};
    std::optional<bool> reduceAnimation{};
    std::optional<float> fontScale{};
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

    // M13：窗口激活状态（宿主 WindowFocusGained/Lost 驱动）。屏幕阅读器
    // （Orca/Narrator/VoiceOver）以窗口激活切换“当前应用”上下文——只发
    // state-changed:focused 不够，AT 侧不会开始播报。默认 no-op（既有
    // provider 未受益于窗口事件时行为不变）。
    virtual void noteWindowActive(bool active) {
        (void)active;
    }

    // Pump platform messages owned by the UI thread.  Providers that use an
    // external event source (AT-SPI on Linux) override this; synchronous
    // providers keep the default no-op implementation.
    virtual void pump() {}

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
    std::vector<bool> windowActiveEvents{};

    void noteWindowActive(bool active) override {
        windowActiveEvents.push_back(active);
    }

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

// M13：AT（屏幕阅读器）请求执行语义 action 时回灌应用——与键盘/指针
// 同走 AppShell::performAccessibilityAction 路径（plan §3.3）。provider
// 在 UI 线程同步调用；重入（dispatch 内触发重建/推送）由应用壳的
// 既有单线程顺序保证。
using SemanticsActionDispatch = std::function<SemanticsActionStatus(
    const std::string& nodeId, std::uint32_t action,
    const std::string& value, float scrollDeltaY)>;

// provider 装配输入：全部为平台无关值。nativeWindow 为原生窗口句柄
//（Windows = HWND、macOS = NSWindow*，由宿主层填入；AT-SPI 走会话总
// 线无窗口句柄需求；headless 测试为 nullptr）。SDK 类型不进公共头。
struct PlatformAccessibilityHost {
    // 语义 action 回灌目标（runApp 接 shell.performAccessibilityAction）。
    SemanticsActionDispatch dispatch{};
    // M13：AT 请求激活/抬升所属窗口（AT-SPI Component.GrabFocus 的平台
    // 惯例——atk_component_grab_focus 会置前所属 toplevel；屏幕阅读器
    // 以窗口激活切换“当前应用”）。缺省空 = 未接线（GrabFocus 仍走语义
    // focus action）。
    std::function<bool()> activateWindow{};
    void* nativeWindow{nullptr};
    // 逻辑坐标 → 物理像素倍率快照（语义 bounds 为窗口逻辑坐标）；
    // 有原生窗口时 provider 以活度量（窗口 DPI）优先。
    float deviceScale{1.0F};
    // 应用名（UIA root 名称 / AT-SPI RegisterApplication 名）。
    std::string applicationName{};
};

// 平台 provider 工厂：LUMEN_ENABLE_ACCESSIBILITY_BRIDGE 开启且平台实现
// 编入时返回原生桥（Windows UIA / Linux AT-SPI / macOS NSAccessibility）；
// 否则返回 nullptr 并把原因写入 *diagnostics（能力如实降级，plan §2.3）。
[[nodiscard]] std::unique_ptr<AccessibilityBridge>
createPlatformAccessibilityBridge(const PlatformAccessibilityHost& host,
                                  std::string* diagnostics = nullptr);

// 编入的 provider 名称（"uia"/"atspi"/"nsaccessibility"；"" = 无）。
// 只反映编译事实，不代表运行时可用（另查 bridge->available()）；测试
// 以此分流断言。
[[nodiscard]] const char* accessibilityProviderName();

// 阶段标识。
[[nodiscard]] const char* accessibilityStageName();

}  // namespace lumen::accessibility
