#pragma once

#if defined(__APPLE__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_NSACCESSIBILITY)

#include <cstddef>
#include <memory>
#include <string>

#include "lumen/accessibility/bridge.h"

namespace lumen::accessibility::nsaccessibility {

class NsAccessibilityBridge final : public AccessibilityBridge {
  public:
    class Impl;

    NsAccessibilityBridge(const PlatformAccessibilityHost& host,
                          std::string* diagnostics);
    ~NsAccessibilityBridge() override;

    std::string bridgeName() const override { return "nsaccessibility"; }
    [[nodiscard]] bool available() const override;
    void updateTree(const SemanticsTree& tree, const SemanticsDiff& diff,
                    const std::string& focusedId) override;
    void setFocusedNode(const std::string& id) override;

    [[nodiscard]] std::size_t nodeCountForTesting() const;

  private:
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<AccessibilityBridge>
createNsAccessibilityBridge(const PlatformAccessibilityHost& host,
                            std::string* diagnostics);

}  // namespace lumen::accessibility::nsaccessibility

#endif  // defined(__APPLE__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_NSACCESSIBILITY)
