// M12：其余平台的空 seam（三个原生实现都不参与编译时）。
#include "native_services.h"

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
