// M13（自用路线图）：Windows UIA provider——内部头（不入公共 include/）。
//
// AccessibilityBridge 的 Windows 实现：语义树 → UIA fragment 树。
// WM_GETOBJECT 经 SetWindowSubclass 子类化宿主 HWND 应答（SDL 无窗口
// 过程钩子；WM_GETOBJECT 由 AT SendMessage 直达窗口过程，消息泵钩子
// 拦不到）。AT 调用与 updateTree 同在 UI 线程（单线程拥有，无锁）。
// 映射与线程模型见 docs/lumen-accessibility-provider-design.md。
//
// 仅在 LUMEN_ENABLE_ACCESSIBILITY_BRIDGE=ON 的 Windows 构建编入；
// lumen-tests 以相同编译定义直驱 COM 对象做 headless 断言。
#if defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)

#include <cstdint>
#include <memory>
#include <string>

// windows.h 的 min/max 宏会破坏 core/geometry.h 的 numeric_limits 调用，
// 先于任何 SDK 头定义 NOMINMAX（本头及其包含者都在 SDK 头之后引入）。
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <UIAutomation.h>

#include "lumen/accessibility/bridge.h"

namespace lumen::accessibility::uia {

// 事件出口（可注入）：默认实现调用 UIA（UiaRaise*，仅窗口连接成功且
// 未断开后发出）；测试注入记录器获得确定性断言。
struct UiaEvent {
    enum class Kind : std::uint8_t {
        StructureChanged,  // 子树增删（根整体失效，AT 重取）
        PropertyChanged,   // 单节点属性变化
        FocusChanged,      // 语义焦点变化
    };
    Kind kind{Kind::PropertyChanged};
    std::string nodeId{};  // 空 = 根
    int propertyId{0};     // Kind::PropertyChanged 时的 UIA 属性 id
};

class UiaEventSink {
  public:
    virtual ~UiaEventSink() = default;
    virtual void raise(const UiaEvent& event) = 0;
};

class UiaAccessibilityBridge final : public AccessibilityBridge {
  public:
    UiaAccessibilityBridge(const PlatformAccessibilityHost& host,
                           std::string* diagnostics);
    ~UiaAccessibilityBridge() override;

    std::string bridgeName() const override { return "uia"; }
    [[nodiscard]] bool available() const override;
    void updateTree(const SemanticsTree& tree, const SemanticsDiff& diff,
                    const std::string& focusedId) override;
    void setFocusedNode(const std::string& id) override;

    // --- 测试接线（内部；不影响 UI 线程语义） ---
    void setEventSinkForTesting(UiaEventSink* sink);  // 非拥有；null 恢复默认
    // COM provider 访问（返回已 AddRef 的指针，调用方负责 Release）。
    [[nodiscard]] IRawElementProviderFragmentRoot* rootProviderForTesting()
        const;
    [[nodiscard]] IRawElementProviderFragment* fragmentForTesting(
        const std::string& id) const;
    [[nodiscard]] bool hasNode(const std::string& id) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<AccessibilityBridge> createUiaBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics);

}  // namespace lumen::accessibility::uia

#endif  // defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)
