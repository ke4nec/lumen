// M12：其余平台的空 seam（三个原生实现都不参与编译时）。
#include "native_services.h"

#include <chrono>

#if !defined(__linux__)
namespace lumen::platform::native {
namespace {
class PollingPreferenceMonitor final : public AccessibilityPreferenceMonitor {
  public:
    std::optional<SystemAccessibilityPreferences> poll() override {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_) return std::nullopt;
        next_ = now + std::chrono::seconds(1);
        return systemAccessibilityPreferences();
    }
  private:
    std::chrono::steady_clock::time_point next_{
        std::chrono::steady_clock::now() + std::chrono::seconds(1)};
};
}
std::unique_ptr<AccessibilityPreferenceMonitor> createAccessibilityPreferenceMonitor() {
    return std::make_unique<PollingPreferenceMonitor>();
}
}  // namespace lumen::platform::native
#endif

#if !defined(_WIN32) && !defined(__linux__) && !defined(__APPLE__)

namespace lumen::platform::native {

std::optional<core::Color> systemAccentColor() { return std::nullopt; }
std::optional<SystemAccessibilityPreferences>
systemAccessibilityPreferences() {
    return std::nullopt;
}
bool notificationsAvailable() { return false; }
ServiceResult showNotification(const NotificationRequest&) {
    return ServiceResult::unavailable("no native service backend");
}

}  // namespace lumen::platform::native

#endif
