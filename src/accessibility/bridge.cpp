#include "lumen/accessibility/bridge.h"

#include <utility>

// M13：平台 provider 按编译定义分发（CMake LUMEN_ENABLE_ACCESSIBILITY_
// BRIDGE 控制编入；未编入时安全降级，行为与 v0.3 现状一致）。
#if defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)
#include "uia_provider.h"
#endif

namespace lumen::accessibility {

std::unique_ptr<AccessibilityBridge> createPlatformAccessibilityBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics) {
#if defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)
    return uia::createUiaBridge(host, diagnostics);
#else
    // 桌面 provider 未编入本构建（选项关闭或平台无实现）；开启开关也
    // 必须安全降级，避免宣称不存在的能力（plan §2.3）。
    (void)host;
    if (diagnostics != nullptr) {
        *diagnostics =
            "platform accessibility bridge providers are not included in "
            "this build (Recording bridge remains available)";
    }
    return nullptr;
#endif
}

const char* accessibilityProviderName() {
#if defined(_WIN32) && defined(LUMEN_ACCESSIBILITY_PROVIDER_UIA)
    return "uia";
#else
    return "";
#endif
}

const char* accessibilityStageName() { return "8C semantics"; }

}  // namespace lumen::accessibility
