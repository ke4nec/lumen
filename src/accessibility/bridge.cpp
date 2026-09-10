#include "lumen/accessibility/bridge.h"

#include <utility>

namespace lumen::accessibility {

std::unique_ptr<AccessibilityBridge> createPlatformAccessibilityBridge(
    std::string* diagnostics) {
    // 桌面桥接（Windows UIA / Linux AT-SPI / macOS NSAccessibility）按
    // LUMEN_ENABLE_ACCESSIBILITY_BRIDGE 可选编译；未启用时报告原因并安
    // 全降级（plan §3.3：桥接失败只关闭对应能力）。
    if (diagnostics != nullptr) {
        *diagnostics =
            "platform accessibility bridge not compiled in "
            "(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE=OFF)";
    }
    return nullptr;
}

const char* accessibilityStageName() { return "8C semantics"; }

}  // namespace lumen::accessibility
