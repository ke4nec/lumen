#pragma once

#if defined(__linux__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_ATSPI)

#include <cstddef>
#include <memory>
#include <string>

#include "lumen/accessibility/bridge.h"

namespace lumen::accessibility::atspi {

class AtspiAccessibilityBridge final : public AccessibilityBridge {
  public:
    class Impl;

    AtspiAccessibilityBridge(const PlatformAccessibilityHost& host,
                             std::string* diagnostics);
    ~AtspiAccessibilityBridge() override;

    std::string bridgeName() const override { return "atspi"; }
    [[nodiscard]] bool available() const override;
    void updateTree(const SemanticsTree& tree, const SemanticsDiff& diff,
                    const std::string& focusedId) override;
    void setFocusedNode(const std::string& id) override;
    void noteWindowActive(bool active) override;
    void pump() override;

    // Deterministic headless probes used by the provider tests.
    [[nodiscard]] std::size_t nodeCountForTesting() const;
    [[nodiscard]] std::string objectPathForTesting(
        const std::string& identity) const;

  private:
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::unique_ptr<AccessibilityBridge> createAtspiBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics);

}  // namespace lumen::accessibility::atspi

#endif  // defined(__linux__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_ATSPI)
