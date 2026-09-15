// M12（自用路线图）：原生平台服务 seam（内部头，不入公共 include）。
//
// SDL 3.2.10 覆盖了窗口/输入/对话框/URL；通知与系统强调色无 API
//（SDL 3.6 才有通知，accent 任何版本都没有）。本 seam 按平台条件编译：
// Windows（Shell_NotifyIcon/DWM，本地真验）、Linux（libdbus 会话总线，
// CI 验证）、macOS（AppKit，CI 验证）；其余平台为空实现。公共契约只
// 用 core/platform 值类型——SDL/Win32/DBus/ObjC 类型不出实现文件。
#pragma once

#include <optional>

#include "lumen/core/geometry.h"
#include "lumen/platform/application_host.h"

namespace lumen::platform::native {

// 系统强调色；nullopt = 平台无查询能力（调用方保持安全默认）。
[[nodiscard]] std::optional<core::Color> systemAccentColor();

// 平台通知可用性（决定 capabilities_.notifications）。
[[nodiscard]] bool notificationsAvailable();

// 投递系统通知；失败为结构化 ServiceResult（无会话/总线等），不抛出。
[[nodiscard]] ServiceResult showNotification(
    const NotificationRequest& request);

}  // namespace lumen::platform::native
