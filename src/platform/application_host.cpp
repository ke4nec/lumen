#include "lumen/platform/application_host.h"

namespace lumen::platform {

const char* hostStageName() { return "8A application host"; }



// --- M4：默认服务实现（契约 host/未支持平台安全降级） ---

ServiceResult ApplicationHost::openUrl(const std::string&) {
    return ServiceResult::unavailable("openUrl not supported by this host");
}

ServiceResult ApplicationHost::requestFileDialog(core::WindowId,
                                                 const FileDialogRequest&) {
    return ServiceResult::unavailable(
        "file dialogs not supported by this host");
}

ServiceResult ApplicationHost::postNotification(const NotificationRequest&) {
    return ServiceResult::unavailable(
        "notifications not supported by this host");
}

void ApplicationHost::setCursor(core::WindowId, SystemCursor) {}

ServiceResult ApplicationHost::setWindowIcon(core::WindowId,
                                             const WindowIcon&) {
    return ServiceResult::unavailable(
        "window icon not supported by this host");
}

// --- 自定义标题栏（lumen-titlebar-design §4.3）：默认安全 no-op ---

void ApplicationHost::minimizeWindow(core::WindowId) {}

void ApplicationHost::toggleMaximizeWindow(core::WindowId) {}

void ApplicationHost::requestWindowClose(core::WindowId) {}

void ApplicationHost::raiseWindow(core::WindowId) {}

void ApplicationHost::setWindowDragRegion(core::WindowId,
                                          std::function<bool(core::Offset)>) {}

}  // namespace lumen::platform
