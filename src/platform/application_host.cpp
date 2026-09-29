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

// --- M15：拖放 ---

ServiceResult ApplicationHost::startDrag(core::WindowId,
                                         const DragOutPayload&) {
    return ServiceResult::unavailable(
        "drag start not supported by this host");
}

// --- M16：窗口能力（默认安全 no-op/结构化降级） ---

void ApplicationHost::toggleFullscreen(core::WindowId) {}

void ApplicationHost::setAlwaysOnTop(core::WindowId, bool) {}

ServiceResult ApplicationHost::setWindowModal(core::WindowId,
                                              core::WindowId) {
    return ServiceResult::unavailable(
        "window modality not supported by this host");
}

// --- M16：系统托盘与全局快捷键（默认结构化降级） ---

ServiceResult ApplicationHost::setTray(core::WindowId,
                                       const TraySetup&) {
    return ServiceResult::unavailable(
        "system tray not supported by this host");
}

void ApplicationHost::removeTray() {}

ServiceResult ApplicationHost::registerGlobalHotkey(
    core::WindowId, const GlobalHotkeySpec&) {
    return ServiceResult::unavailable(
        "global hotkeys not supported by this host");
}

ServiceResult ApplicationHost::unregisterGlobalHotkey(const std::string&) {
    return ServiceResult::unavailable(
        "global hotkeys not supported by this host");
}

// --- 自定义标题栏（lumen-titlebar-design §4.3）：默认安全 no-op ---

void ApplicationHost::minimizeWindow(core::WindowId) {}

void ApplicationHost::toggleMaximizeWindow(core::WindowId) {}

void ApplicationHost::requestWindowClose(core::WindowId) {}

void ApplicationHost::raiseWindow(core::WindowId) {}

void ApplicationHost::setWindowDragRegion(core::WindowId,
                                          std::function<bool(core::Offset)>) {}

}  // namespace lumen::platform
