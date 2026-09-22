// M12：Linux 原生服务（libdbus 会话总线：org.freedesktop.Notifications
// 通知 + org.freedesktop.appearance 强调色）。无会话总线（headless CI）
// 结构化 Unavailable；构建缺 libdbus（LUMEN_HAS_DBUS 未定义）为空 seam。
// 本文件仅在 Linux CI 编译验证（本地 Windows 不可编译，符合仓库
// "CI 为事实来源"惯例）。
#include "native_services.h"

#if defined(__linux__)

#if defined(LUMEN_HAS_DBUS)

#include <dbus/dbus.h>

#include <array>
#include <algorithm>
#include <cstdlib>
#include <variant>
#include <string>

namespace lumen::platform::native {
namespace {

struct ConnectionPtr {
    DBusConnection* connection{nullptr};
    ConnectionPtr() {
        DBusError error;
        dbus_error_init(&error);
        connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
        // 引用归 bus 所有（dbus_bus_get 返回共享连接，不 unref）。
        dbus_error_free(&error);
    }
};

// 会话总线上的同步方法调用（appendArgs 负责参数；timeout 毫秒）。
template <typename AppendArgs>
DBusMessage* callMethod(const char* destination, const char* path,
                        const char* interface, const char* member,
                        int timeoutMs, AppendArgs&& appendArgs,
                        DBusError* error) {
    DBusMessage* message =
        dbus_message_new_method_call(destination, path, interface, member);
    if (message == nullptr) {
        return nullptr;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(message, &iter);
    appendArgs(iter);
    DBusPendingCall* pending = nullptr;
    if (!dbus_connection_send_with_reply(ConnectionPtr().connection, message,
                                         &pending, timeoutMs) ||
        pending == nullptr) {
        dbus_message_unref(message);
        return nullptr;
    }
    dbus_pending_call_block(pending);
    DBusMessage* reply = dbus_pending_call_steal_reply(pending);
    dbus_pending_call_unref(pending);
    dbus_message_unref(message);
    if (reply != nullptr && dbus_message_get_type(reply) ==
                                DBUS_MESSAGE_TYPE_ERROR) {
        if (error != nullptr) {
            dbus_set_error_from_message(error, reply);
        }
        dbus_message_unref(reply);
        return nullptr;
    }
    return reply;
}

// "#rrggbb" / "#aarrggbb" → Color。
std::optional<core::Color> parseHexColor(const std::string& text) {
    if (text.size() != 7 && text.size() != 9) {
        return std::nullopt;
    }
    if (text[0] != '#') {
        return std::nullopt;
    }
    const auto channel = [&text](std::size_t at) {
        return static_cast<std::uint8_t>(
            std::strtoul(text.substr(at, 2).c_str(), nullptr, 16));
    };
    if (text.size() == 7) {
        return core::Color{channel(1), channel(3), channel(5), 255};
    }
    return core::Color{channel(3), channel(5), channel(7), channel(1)};
}

using PortalSetting = std::variant<bool, double>;

std::optional<PortalSetting> readPortalSetting(const char* nameSpace,
                                               const char* key) {
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = callMethod(
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", "Read", 500,
        [nameSpace, key](DBusMessageIter& iter) {
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
                                           &nameSpace);
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &key);
        },
        &error);
    if (reply == nullptr) {
        dbus_error_free(&error);
        return std::nullopt;
    }

    std::optional<PortalSetting> result;
    DBusMessageIter args;
    DBusMessageIter variant;
    if (dbus_message_iter_init(reply, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&args, &variant);
        switch (dbus_message_iter_get_arg_type(&variant)) {
            case DBUS_TYPE_BOOLEAN: {
                dbus_bool_t value = FALSE;
                dbus_message_iter_get_basic(&variant, &value);
                result = value != FALSE;
                break;
            }
            case DBUS_TYPE_DOUBLE: {
                double value = 1.0;
                dbus_message_iter_get_basic(&variant, &value);
                result = value;
                break;
            }
            default:
                break;
        }
    }
    dbus_message_unref(reply);
    dbus_error_free(&error);
    return result;
}

}  // namespace

std::optional<core::Color> systemAccentColor() {
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = callMethod(
        "org.freedesktop.appearance", "/org/freedesktop/appearance",
        "org.freedesktop.DBus.Properties", "Get", 500,
        [](DBusMessageIter& iter) {
            const char* interface = "org.freedesktop.appearance";
            const char* property = "accent";
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
                                           &interface);
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
                                           &property);
        },
        &error);
    if (reply == nullptr) {
        dbus_error_free(&error);
        return std::nullopt;
    }
    std::optional<core::Color> result;
    DBusMessageIter args;
    DBusMessageIter variant;
    const char* value = nullptr;
    if (dbus_message_iter_init(reply, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&args, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&variant, &value);
            if (value != nullptr) {
                result = parseHexColor(value);
            }
        }
    }
    dbus_message_unref(reply);
    return result;
}

std::optional<SystemAccessibilityPreferences>
systemAccessibilityPreferences() {
    SystemAccessibilityPreferences preferences;
    bool found = false;
    if (const auto value = readPortalSetting(
            "org.gnome.desktop.a11y.interface", "high-contrast")) {
        if (const auto* highContrast = std::get_if<bool>(&*value)) {
            preferences.highContrast = *highContrast;
            found = true;
        }
    }
    if (const auto value = readPortalSetting("org.gnome.desktop.interface",
                                            "enable-animations")) {
        if (const auto* animations = std::get_if<bool>(&*value)) {
            preferences.reduceAnimation = !*animations;
            found = true;
        }
    }
    if (const auto value = readPortalSetting("org.gnome.desktop.interface",
                                            "text-scaling-factor")) {
        if (const auto* scale = std::get_if<double>(&*value)) {
            preferences.fontScale = std::clamp(
                static_cast<float>(*scale), 0.5F, 3.0F);
            found = true;
        }
    }
    return found ? std::optional{preferences} : std::nullopt;
}

bool notificationsAvailable() { return true; }

ServiceResult showNotification(const NotificationRequest& request) {
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = callMethod(
        "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify", 1000,
        [&request](DBusMessageIter& iter) {
            const char* appName = "lumen";
            const std::string title = request.title;
            const std::string body = request.body;
            const char* icon = "";
            const dbus_uint32_t replacesId = 0;
            const dbus_int32_t timeout = -1;
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &appName);
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32,
                                           &replacesId);
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &icon);
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
                                           title.c_str());
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
                                           body.c_str());
            // actions：空 "as"；hints：空 "a{sv}"。
            DBusMessageIter array;
            dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s",
                                             &array);
            dbus_message_iter_close_container(&iter, &array);
            dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}",
                                             &array);
            dbus_message_iter_close_container(&iter, &array);
            dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &timeout);
        },
        &error);
    if (reply == nullptr) {
        const std::string reason =
            dbus_error_is_set(&error)
                ? std::string("notification daemon: ") + error.message
                : std::string("no reply from notification daemon");
        dbus_error_free(&error);
        return ServiceResult::failed(reason);
    }
    dbus_message_unref(reply);
    return ServiceResult::success();
}

}  // namespace lumen::platform::native

#else  // !LUMEN_HAS_DBUS

namespace lumen::platform::native {

std::optional<core::Color> systemAccentColor() { return std::nullopt; }
std::optional<SystemAccessibilityPreferences>
systemAccessibilityPreferences() {
    return std::nullopt;
}
bool notificationsAvailable() { return false; }
ServiceResult showNotification(const NotificationRequest&) {
    return ServiceResult::unavailable(
        "built without libdbus (LUMEN_HAS_DBUS undefined)");
}

}  // namespace lumen::platform::native

#endif  // LUMEN_HAS_DBUS

#endif  // defined(__linux__)
