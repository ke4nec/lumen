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

}  // namespace lumen::platform
