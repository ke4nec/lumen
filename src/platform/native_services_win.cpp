// M12：Windows 原生服务（Shell_NotifyIcon 气泡通知 + DWM 强调色）。
// 气泡/Toast 依赖 explorer 会话——无会话环境（headless CI）按结构化
// 失败返回，不崩溃。
#include "native_services.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

namespace lumen::platform::native {
namespace {

// 托盘图标宿主：message-only 窗口（不可见，仅承载通知回调）。
HWND trayOwnerWindow() {
    static HWND owner = [] {
        const wchar_t kClassName[] = L"lumen-tray";
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        RegisterClassW(&wc);
        return CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    }();
    return owner;
}

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        wide.data(), size);
    return wide;
}

void copyTruncated(wchar_t* target, std::size_t capacity,
                   const std::string& utf8) {
    const std::wstring wide = widen(utf8);
    wcsncpy_s(target, capacity, wide.c_str(), _TRUNCATE);
}

constexpr std::uint32_t kTrayIconId = 1;

}  // namespace

std::optional<core::Color> systemAccentColor() {
    DWORD colorization = 0;
    BOOL opaque = FALSE;
    if (FAILED(DwmGetColorizationColor(&colorization, &opaque))) {
        return std::nullopt;
    }
    // 0xAARRGGBB（DWM 着色近似系统强调色； translucent 时按原 alpha）。
    return core::Color{
        static_cast<std::uint8_t>((colorization >> 16) & 0xFF),
        static_cast<std::uint8_t>((colorization >> 8) & 0xFF),
        static_cast<std::uint8_t>(colorization & 0xFF),
        static_cast<std::uint8_t>((colorization >> 24) & 0xFF)};
}

bool notificationsAvailable() { return true; }

ServiceResult showNotification(const NotificationRequest& request) {
    const HWND owner = trayOwnerWindow();
    if (owner == nullptr) {
        return ServiceResult::failed(
            "tray owner window creation failed");
    }
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_INFO;
    nid.uCallbackMessage = WM_APP + 1;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);    nid.dwInfoFlags = NIIF_INFO;
    copyTruncated(nid.szInfoTitle, 64, request.title);
    copyTruncated(nid.szInfo, 256, request.body);
    // 图标常驻进程生命期（删除会连带终止气泡/Toast 展示）。
    if (!Shell_NotifyIconW(NIM_ADD, &nid) &&
        !Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        return ServiceResult::failed(
            "Shell_NotifyIcon rejected the balloon (no shell session?)");
    }
    return ServiceResult::success();
}

}  // namespace lumen::platform::native

#endif  // defined(_WIN32)
