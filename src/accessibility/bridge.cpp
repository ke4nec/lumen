#include "lumen/accessibility/bridge.h"

#include <utility>

namespace lumen::accessibility {

std::unique_ptr<AccessibilityBridge> createPlatformAccessibilityBridge(
    std::string* diagnostics) {
    // 桌面 provider（Windows UIA / Linux AT-SPI / macOS NSAccessibility）尚
    // 未纳入本仓库；预留开关开启时也必须安全降级，避免宣称不存在的能力。
    if (diagnostics != nullptr) {
        *diagnostics =
            "platform accessibility bridge providers are not included in "
            "this build (Recording bridge remains available)";
    }
    return nullptr;
}

const char* accessibilityStageName() { return "8C semantics"; }

}  // namespace lumen::accessibility
