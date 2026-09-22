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
#include <chrono>
#include <cmath>
#include <map>
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
        if (connection) dbus_connection_set_exit_on_disconnect(connection, FALSE);
        dbus_error_free(&error);
    }
    ~ConnectionPtr() { if (connection) dbus_connection_unref(connection); }
    ConnectionPtr(const ConnectionPtr&) = delete;
    ConnectionPtr& operator=(const ConnectionPtr&) = delete;
};

// 会话总线上的同步方法调用（appendArgs 负责参数；timeout 毫秒）。
template <typename AppendArgs>
DBusMessage* callMethod(const char* destination, const char* path,
                        const char* interface, const char* member,
                        int timeoutMs, AppendArgs&& appendArgs,
                        DBusError* error) {
    ConnectionPtr bus;
    if (!bus.connection) return nullptr;
    DBusMessage* message =
        dbus_message_new_method_call(destination, path, interface, member);
    if (message == nullptr) {
        return nullptr;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(message, &iter);
    appendArgs(iter);
    DBusPendingCall* pending = nullptr;
    if (!dbus_connection_send_with_reply(bus.connection, message,
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

using PortalSetting = std::variant<bool, double, dbus_uint32_t>;

std::optional<PortalSetting> portalValue(DBusMessageIter value) {
    // ReadAll/ReadOne use one variant layer; tolerate older nested variants.
    while (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_VARIANT) {
        DBusMessageIter inner;
        dbus_message_iter_recurse(&value, &inner);
        value = inner;
    }
    switch (dbus_message_iter_get_arg_type(&value)) {
        case DBUS_TYPE_BOOLEAN: {
            dbus_bool_t v = FALSE;
            dbus_message_iter_get_basic(&value, &v);
            return v != FALSE;
        }
        case DBUS_TYPE_DOUBLE: {
            double v = 1.0;
            dbus_message_iter_get_basic(&value, &v);
            return v;
        }
        case DBUS_TYPE_UINT32: {
            dbus_uint32_t v = 0;
            dbus_message_iter_get_basic(&value, &v);
            return v;
        }
        default: return std::nullopt;
    }
}

void appendSettingsNamespaces(DBusMessageIter& iter) {
    DBusMessageIter names;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &names);
    for (const char* name : {"org.freedesktop.appearance", "org.gnome.desktop.*"}) {
        dbus_message_iter_append_basic(&names, DBUS_TYPE_STRING, &name);
    }
    dbus_message_iter_close_container(&iter, &names);
}

std::optional<SystemAccessibilityPreferences> parseSettings(DBusMessage* reply) {
    if (!reply || dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN ||
        !dbus_message_has_signature(reply, "a{sa{sv}}")) return std::nullopt;
    std::map<std::pair<std::string, std::string>, PortalSetting> values;
    DBusMessageIter args, namespaces;
    dbus_message_iter_init(reply, &args);
    dbus_message_iter_recurse(&args, &namespaces);
    while (dbus_message_iter_get_arg_type(&namespaces) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, settings;
        dbus_message_iter_recurse(&namespaces, &entry);
        const char* name = nullptr;
        dbus_message_iter_get_basic(&entry, &name);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &settings);
        while (dbus_message_iter_get_arg_type(&settings) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter setting;
            dbus_message_iter_recurse(&settings, &setting);
            const char* key = nullptr;
            dbus_message_iter_get_basic(&setting, &key);
            dbus_message_iter_next(&setting);
            if (const auto value = portalValue(setting)) values[{name, key}] = *value;
            dbus_message_iter_next(&settings);
        }
        dbus_message_iter_next(&namespaces);
    }
    const auto get = [&]<typename T>(const char* ns, const char* key) -> const T* {
        const auto it = values.find({ns, key});
        return it == values.end() ? nullptr : std::get_if<T>(&it->second);
    };
    SystemAccessibilityPreferences preferences;
    bool found = false;
    // Standard portal values take precedence over desktop-specific fallbacks.
    if (const auto* v = get.operator()<dbus_uint32_t>("org.freedesktop.appearance", "contrast")) {
        preferences.highContrast = *v == 1;
        found = true;
    } else if (const auto* v = get.operator()<bool>("org.gnome.desktop.a11y.interface", "high-contrast")) {
        preferences.highContrast = *v;
        found = true;
    }
    if (const auto* v = get.operator()<dbus_uint32_t>("org.freedesktop.appearance", "reduced-motion")) {
        preferences.reduceAnimation = *v == 1;
        found = true;
    } else if (const auto* v = get.operator()<bool>("org.gnome.desktop.interface", "enable-animations")) {
        preferences.reduceAnimation = !*v;
        found = true;
    }
    if (const auto* v = get.operator()<double>("org.gnome.desktop.interface", "text-scaling-factor");
        v && std::isfinite(*v) && *v > 0.0) {
        preferences.fontScale = static_cast<float>(std::clamp(*v, 0.5, 3.0));
        found = true;
    }
    return found ? std::optional{preferences} : std::nullopt;
}

class PortalPreferenceMonitor final : public AccessibilityPreferenceMonitor {
  public:
    PortalPreferenceMonitor() {
        // Connection/authentication belongs to host initialization, never to
        // the UI event pump. Portal service restarts keep this bus connection.
        DBusError error;
        dbus_error_init(&error);
        connection_ = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
        dbus_error_free(&error);
        if (connection_) dbus_connection_set_exit_on_disconnect(connection_, FALSE);
    }
    ~PortalPreferenceMonitor() override { disconnect(); }
    std::optional<SystemAccessibilityPreferences> poll() override {
        const auto now = std::chrono::steady_clock::now();
        if (!connection_) return std::nullopt;
        // A private connection avoids consuming another subsystem's messages.
        // Zero timeout: replies may take seconds, but event pumping never waits.
        dbus_connection_read_write_dispatch(connection_, 0);
        if (!dbus_connection_get_is_connected(connection_)) {
            disconnect();
            return std::nullopt;
        }
        if (pending_ && dbus_pending_call_get_completed(pending_)) {
            DBusMessage* reply = dbus_pending_call_steal_reply(pending_);
            dbus_pending_call_unref(pending_);
            pending_ = nullptr;
            const auto result = parseSettings(reply);
            if (reply) dbus_message_unref(reply);
            return result;
        }
        if (!pending_ && now >= next_) {
            next_ = now + std::chrono::seconds(1);
            DBusMessage* request = dbus_message_new_method_call(
                "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                "org.freedesktop.portal.Settings", "ReadAll");
            if (!request) return std::nullopt;
            DBusMessageIter iter;
            dbus_message_iter_init_append(request, &iter);
            appendSettingsNamespaces(iter);
            dbus_connection_send_with_reply(connection_, request, &pending_, 1000);
            dbus_message_unref(request);
        }
        return std::nullopt;
    }
  private:
    void disconnect() {
        if (pending_) {
            dbus_pending_call_cancel(pending_);
            dbus_pending_call_unref(pending_);
            pending_ = nullptr;
        }
        if (connection_) {
            dbus_connection_close(connection_);
            dbus_connection_unref(connection_);
            connection_ = nullptr;
        }
    }
    DBusConnection* connection_{nullptr};
    DBusPendingCall* pending_{nullptr};
    std::chrono::steady_clock::time_point next_{};
};

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
    DBusMessage* reply = callMethod(
        "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Settings", "ReadAll", 500,
        appendSettingsNamespaces, nullptr);
    const auto result = parseSettings(reply);
    if (reply) dbus_message_unref(reply);
    return result;
}

std::unique_ptr<AccessibilityPreferenceMonitor> createAccessibilityPreferenceMonitor() {
    return std::make_unique<PortalPreferenceMonitor>();
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
std::unique_ptr<AccessibilityPreferenceMonitor> createAccessibilityPreferenceMonitor() {
    return nullptr;
}
bool notificationsAvailable() { return false; }
ServiceResult showNotification(const NotificationRequest&) {
    return ServiceResult::unavailable(
        "built without libdbus (LUMEN_HAS_DBUS undefined)");
}

}  // namespace lumen::platform::native

#endif  // LUMEN_HAS_DBUS

#endif  // defined(__linux__)
