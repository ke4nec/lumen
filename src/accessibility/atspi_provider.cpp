#include "atspi_provider.h"

#if defined(__linux__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_ATSPI)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(LUMEN_HAS_DBUS)
#include <dbus/dbus.h>
#endif

namespace lumen::accessibility::atspi {
namespace {

constexpr char kRootPath[] = "/org/a11y/atspi/accessible/root";
constexpr char kNullPath[] = "/org/a11y/atspi/null";
constexpr char kRegistryName[] = "org.a11y.atspi.Registry";
constexpr char kRegistryPath[] = "/org/a11y/atspi/accessible/root";
constexpr char kRegistryInterface[] = "org.a11y.atspi.Socket";

std::uint64_t identityHash(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hexHash(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}

[[maybe_unused]] int roleValue(SemanticsRole role) {
    // Values are AtspiRole constants.  Keep this table local to the provider;
    // the public semantics contract deliberately has no AT-SPI dependency.
    switch (role) {
        case SemanticsRole::Button:
            return 41;  // ATSPI_ROLE_PUSH_BUTTON
        case SemanticsRole::Checkbox:
        case SemanticsRole::Switch:
            return 7;   // ATSPI_ROLE_CHECK_BOX
        case SemanticsRole::Radio:
            return 42;  // ATSPI_ROLE_RADIO_BUTTON
        case SemanticsRole::TextField:
            return 79;  // ATSPI_ROLE_ENTRY
        case SemanticsRole::Text:
            return 61;  // ATSPI_ROLE_TEXT
        case SemanticsRole::Image:
            return 27;  // ATSPI_ROLE_IMAGE
        case SemanticsRole::Slider:
        case SemanticsRole::Splitter:
            return 51;  // ATSPI_ROLE_SLIDER
        case SemanticsRole::ProgressBar:
            return 38;  // ATSPI_ROLE_PROGRESS_BAR
        case SemanticsRole::List:
            return 35;  // ATSPI_ROLE_LIST
        case SemanticsRole::ListItem:
            return 36;  // ATSPI_ROLE_LIST_ITEM
        case SemanticsRole::Tree:
            return 65;  // ATSPI_ROLE_TREE
        case SemanticsRole::TreeItem:
            return 66;  // ATSPI_ROLE_TREE_ITEM
        case SemanticsRole::Menu:
            return 32;  // ATSPI_ROLE_MENU
        case SemanticsRole::MenuItem:
            return 34;  // ATSPI_ROLE_MENU_ITEM
        case SemanticsRole::Dialog:
            return 16;  // ATSPI_ROLE_DIALOG
        case SemanticsRole::Window:
            return 69;  // ATSPI_ROLE_WINDOW
        case SemanticsRole::Toolbar:
            return 63;  // ATSPI_ROLE_TOOL_BAR
        case SemanticsRole::StatusBar:
            return 54;  // ATSPI_ROLE_STATUS_BAR
        case SemanticsRole::SpinButton:
            return 52;  // ATSPI_ROLE_SPIN_BUTTON
        case SemanticsRole::Group:
            return 42 + 1;  // ATSPI_ROLE_PANEL (43)
    }
    return 0;
}

#if defined(LUMEN_HAS_DBUS)

void appendString(DBusMessageIter* iter, const std::string& value) {
    const char* text = value.c_str();
    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &text);
}

void appendObjectReference(DBusMessageIter* iter, const char* busName,
                           const char* path) {
    DBusMessageIter tuple;
    dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, nullptr, &tuple);
    const char* bus = busName != nullptr ? busName : "";
    dbus_message_iter_append_basic(&tuple, DBUS_TYPE_STRING, &bus);
    dbus_message_iter_append_basic(&tuple, DBUS_TYPE_OBJECT_PATH, &path);
    dbus_message_iter_close_container(iter, &tuple);
}

DBusHandlerResult replyError(DBusConnection* connection, DBusMessage* request,
                             const char* name, const char* text) {
    DBusMessage* reply = dbus_message_new_error(request, name, text);
    if (reply == nullptr) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_connection_send(connection, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

DBusHandlerResult replyEmpty(DBusConnection* connection, DBusMessage* request) {
    DBusMessage* reply = dbus_message_new_method_return(request);
    if (reply == nullptr) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_connection_send(connection, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

template <typename Append>
DBusHandlerResult replyValue(DBusConnection* connection, DBusMessage* request,
                             Append&& append) {
    DBusMessage* reply = dbus_message_new_method_return(request);
    if (reply == nullptr) {
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    DBusMessageIter iter;
    dbus_message_iter_init_append(reply, &iter);
    append(&iter);
    dbus_connection_send(connection, reply, nullptr);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

std::string readBusAddress() {
    if (const char* value = std::getenv("AT_SPI_BUS_ADDRESS"); value != nullptr &&
        *value != '\0') {
        return value;
    }
    if (const char* value = std::getenv("AT_SPI_BUS"); value != nullptr &&
        *value != '\0') {
        return value;
    }
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr) {
        std::ifstream file(std::string(runtime) + "/at-spi/bus");
        std::string line;
        if (file && std::getline(file, line) && !line.empty()) {
            return line;
        }
    }
    return {};
}

DBusMessage* callGetAddress(DBusConnection* session) {
    DBusMessage* request = dbus_message_new_method_call(
        "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    if (request == nullptr) {
        return nullptr;
    }
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
        session, request, 500, &error);
    dbus_message_unref(request);
    dbus_error_free(&error);
    return reply;
}

DBusConnection* openAccessibilityBus(std::string* diagnostics) {
    DBusError error;
    dbus_error_init(&error);
    DBusConnection* session = dbus_bus_get(DBUS_BUS_SESSION, &error);
    dbus_error_free(&error);
    std::string address = readBusAddress();
    if (address.empty() && session != nullptr) {
        DBusMessage* reply = callGetAddress(session);
        if (reply != nullptr) {
            DBusMessageIter iter;
            const char* value = nullptr;
            if (dbus_message_iter_init(reply, &iter) &&
                dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
                dbus_message_iter_get_basic(&iter, &value);
                if (value != nullptr) {
                    address = value;
                }
            }
            dbus_message_unref(reply);
        }
    }
    if (session != nullptr) dbus_connection_unref(session);
    if (address.empty()) {
        if (diagnostics != nullptr) {
            *diagnostics = "AT-SPI accessibility bus address is unavailable";
        }
        return nullptr;
    }
    DBusConnection* connection = dbus_connection_open_private(address.c_str(),
                                                                &error);
    if (connection == nullptr || dbus_error_is_set(&error)) {
        if (diagnostics != nullptr) {
            *diagnostics = dbus_error_is_set(&error)
                               ? std::string("AT-SPI bus connect failed: ") +
                                     error.message
                               : "AT-SPI bus connect failed";
        }
        dbus_error_free(&error);
        return nullptr;
    }
    dbus_error_free(&error);
    dbus_connection_set_exit_on_disconnect(connection, false);
    if (!dbus_bus_register(connection, &error)) {
        if (diagnostics != nullptr) {
            *diagnostics = "AT-SPI bus registration failed";
        }
        dbus_error_free(&error);
        dbus_connection_close(connection);
        dbus_connection_unref(connection);
        return nullptr;
    }
    dbus_error_free(&error);
    return connection;
}

#endif  // LUMEN_HAS_DBUS

}  // namespace

class AtspiAccessibilityBridge::Impl {
  public:
    explicit Impl(const PlatformAccessibilityHost& input) : host(input) {}

    PlatformAccessibilityHost host;
    SemanticsTree tree;
    std::string focusedId;
    std::unordered_map<std::string, std::string> pathToId;
    std::unordered_map<std::string, std::string> idToPath;
    bool connected{false};
#if defined(LUMEN_HAS_DBUS)
    DBusConnection* connection{nullptr};
    std::string uniqueName;
    std::int32_t applicationId{0};
#endif

    std::string pathFor(const std::string& id) const {
        if (id == tree.rootId) {
            return kRootPath;
        }
        if (const auto it = idToPath.find(id); it != idToPath.end()) {
            return it->second;
        }
        return std::string("/org/a11y/atspi/accessible/") +
               hexHash(identityHash(id));
    }

    const SemanticsNode* nodeAt(const char* path) const {
        if (path == nullptr) {
            return nullptr;
        }
        if (std::strcmp(path, kRootPath) == 0) {
            return tree.find(tree.rootId);
        }
        const auto pathIt = pathToId.find(path);
        if (pathIt == pathToId.end()) {
            return nullptr;
        }
        return tree.find(pathIt->second);
    }

    std::string idAt(const char* path) const {
        if (path != nullptr && std::strcmp(path, kRootPath) == 0) {
            return tree.rootId;
        }
        if (path == nullptr) {
            return {};
        }
        const auto it = pathToId.find(path);
        return it == pathToId.end() ? std::string{} : it->second;
    }

    std::string parentPathFor(const std::string& id) const {
        if (id.empty() || id == tree.rootId) {
            return kNullPath;
        }
        for (const auto& [candidateId, candidate] : tree.nodes) {
            if (std::find(candidate.children.begin(), candidate.children.end(), id) !=
                candidate.children.end()) {
                return pathFor(candidateId);
            }
        }
        return kNullPath;
    }
};

#if defined(LUMEN_HAS_DBUS)

namespace {

constexpr char kIntrospectionXml[] =
    "<node>"
    "<interface name='org.freedesktop.DBus.Introspectable'>"
    "<method name='Introspect'><arg direction='out' type='s'/></method>"
    "</interface>"
    "<interface name='org.freedesktop.DBus.Properties'>"
    "<method name='Get'><arg direction='in' type='s'/><arg direction='in' type='s'/><arg direction='out' type='v'/></method>"
    "<method name='Set'><arg direction='in' type='s'/><arg direction='in' type='s'/><arg direction='in' type='v'/></method>"
    "<method name='GetAll'><arg direction='in' type='s'/><arg direction='out' type='a{sv}'/></method>"
    "</interface>"
    "<interface name='org.a11y.atspi.Accessible'/>"
    "<interface name='org.a11y.atspi.Action'/>"
    "<interface name='org.a11y.atspi.Component'/>"
    "<interface name='org.a11y.atspi.Value'/>"
    "<interface name='org.a11y.atspi.Application'/>"
    "</node>";

void appendVariantString(DBusMessageIter* parent, const std::string& value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "s", &variant);
    appendString(&variant, value);
    dbus_message_iter_close_container(parent, &variant);
}

void appendVariantDouble(DBusMessageIter* parent, double value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "d", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &value);
    dbus_message_iter_close_container(parent, &variant);
}

void appendVariantUint32(DBusMessageIter* parent, std::uint32_t value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "u", &variant);
    dbus_uint32_t number = value;
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &number);
    dbus_message_iter_close_container(parent, &variant);
}

void appendVariantInt32(DBusMessageIter* parent, std::int32_t value) {
    DBusMessageIter variant;
    dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "i", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &value);
    dbus_message_iter_close_container(parent, &variant);
}

void appendStateArray(DBusMessageIter* parent, const SemanticsNode& node,
                      const std::string& focused) {
    DBusMessageIter array;
    dbus_message_iter_open_container(parent, DBUS_TYPE_ARRAY, "u", &array);
    // AT-SPI GetState returns two 32-bit bitsets, not a list of enum values.
    std::uint32_t bits[2]{};
    const auto append = [&bits](std::uint32_t state) {
        bits[state / 32] |= std::uint32_t{1} << (state % 32);
    };
    if ((node.flags & kSemanticsEnabled) != 0) append(8);   // ENABLED
    if ((node.actions & kActionFocus) != 0 &&
        (node.flags & kSemanticsEnabled) != 0) append(11);  // FOCUSABLE
    if (node.id == focused || (node.flags & kSemanticsFocused) != 0) append(12);
    if ((node.flags & kSemanticsChecked) != 0) append(4);
    if ((node.flags & kSemanticsSelected) != 0) append(23);
    if ((node.flags & kSemanticsHidden) == 0) {
        append(30);  // VISIBLE
        append(25);  // SHOWING
    }
    if ((node.flags & kSemanticsInvalid) != 0) append(36); // INVALID_ENTRY
    for (auto word : bits) dbus_message_iter_append_basic(&array, DBUS_TYPE_UINT32, &word);
    dbus_message_iter_close_container(parent, &array);
}

DBusHandlerResult handleMessage(DBusConnection* connection, DBusMessage* request,
                                void* userData) {
    auto* impl = static_cast<AtspiAccessibilityBridge::Impl*>(userData);
    const char* path = dbus_message_get_path(request);
    const char* interface = dbus_message_get_interface(request);
    const char* member = dbus_message_get_member(request);
    if (interface == nullptr || member == nullptr) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (std::strcmp(interface, "org.a11y.atspi.Cache") == 0 &&
        std::strcmp(member, "GetItems") == 0) {
        // Optional bulk cache: clients query live objects on demand.
        return replyValue(connection, request, [](DBusMessageIter* out) {
            DBusMessageIter array;
            dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "((so)(so)(so)iiassusau)", &array);
            dbus_message_iter_close_container(out, &array);
        });
    }
    const SemanticsNode* node = impl->nodeAt(path);
    const bool isRoot = path != nullptr && std::strcmp(path, kRootPath) == 0;
    const bool isIntrospection =
        interface != nullptr &&
        std::strcmp(interface, "org.freedesktop.DBus.Introspectable") == 0;
    const bool isApplicationProperties =
        interface != nullptr &&
        std::strcmp(interface, "org.freedesktop.DBus.Properties") == 0 &&
        isRoot;
    if (node == nullptr && !isIntrospection && !isApplicationProperties) {
        return replyError(connection, request, DBUS_ERROR_UNKNOWN_OBJECT,
                          "semantic node is not present");
    }
    if (interface == nullptr || member == nullptr) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (std::strcmp(interface, "org.freedesktop.DBus.Introspectable") == 0 &&
        std::strcmp(member, "Introspect") == 0) {
        return replyValue(connection, request, [](DBusMessageIter* iter) {
            appendString(iter, kIntrospectionXml);
        });
    }
    if (std::strcmp(interface, "org.freedesktop.DBus.Properties") == 0) {
        DBusMessageIter args;
        if (!dbus_message_iter_init(request, &args) ||
            dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
            return replyError(connection, request, DBUS_ERROR_INVALID_ARGS,
                              "invalid property arguments");
        }
        const char* propertyInterface = nullptr;
        dbus_message_iter_get_basic(&args, &propertyInterface);
        dbus_message_iter_next(&args);
        const char* property = nullptr;
        if (dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&args, &property);
        }
        const std::string id = impl->idAt(path);
        const SemanticsNode* current = impl->tree.find(id);
        if (isRoot && propertyInterface != nullptr &&
            std::strcmp(propertyInterface, "org.a11y.atspi.Application") == 0) {
            if (std::strcmp(member, "Get") == 0 && property != nullptr &&
                std::strcmp(property, "Id") == 0) {
                return replyValue(connection, request, [impl](DBusMessageIter* out) {
                    appendVariantInt32(out, impl->applicationId);
                });
            }
            if (std::strcmp(member, "Set") == 0 && property != nullptr &&
                std::strcmp(property, "Id") == 0) {
                dbus_message_iter_next(&args);
                if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT) {
                    return replyError(connection, request, DBUS_ERROR_INVALID_ARGS,
                                      "Application.Id must be a variant");
                }
                DBusMessageIter variant;
                dbus_message_iter_recurse(&args, &variant);
                if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_INT32) {
                    return replyError(connection, request, DBUS_ERROR_INVALID_ARGS,
                                      "Application.Id must contain an int32");
                }
                dbus_int32_t applicationId = 0;
                dbus_message_iter_get_basic(&variant, &applicationId);
                impl->applicationId = applicationId;
                return replyEmpty(connection, request);
            }
        }
        if (std::strcmp(member, "Get") == 0 && current != nullptr &&
            property != nullptr) {
            return replyValue(connection, request, [&, current](DBusMessageIter* out) {
                if (std::strcmp(property, "Name") == 0) {
                    appendVariantString(out, current->label);
                } else if (std::strcmp(property, "Description") == 0) {
                    appendVariantString(out, "");
                } else if (std::strcmp(property, "Role") == 0) {
                    appendVariantUint32(out, static_cast<std::uint32_t>(roleValue(current->role)));
                } else if (std::strcmp(property, "ChildCount") == 0) {
                    DBusMessageIter variant;
                    dbus_message_iter_open_container(out, DBUS_TYPE_VARIANT, "i", &variant);
                    const dbus_int32_t count = static_cast<dbus_int32_t>(current->children.size());
                    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT32, &count);
                    dbus_message_iter_close_container(out, &variant);
                } else if (std::strcmp(property, "CurrentValue") == 0) {
                    double value = 0.0;
                    try { value = std::stod(current->value); } catch (...) {}
                    appendVariantDouble(out, value);
                } else if (std::strcmp(property, "MinimumValue") == 0) {
                    appendVariantDouble(out, 0.0);
                } else if (std::strcmp(property, "MaximumValue") == 0) {
                    appendVariantDouble(out, 100.0);
                } else if (std::strcmp(property, "MinimumIncrement") == 0) {
                    appendVariantDouble(out, 1.0);
                } else if (std::strcmp(property, "NActions") == 0) {
                    appendVariantInt32(out, ((current->actions & kActionActivate) != 0) +
                                            ((current->actions & kActionFocus) != 0));
                } else if (std::strcmp(property, "Parent") == 0) {
                    const std::string parentPath = impl->parentPathFor(id);
                    DBusMessageIter variant;
                    dbus_message_iter_open_container(out, DBUS_TYPE_VARIANT, "(so)", &variant);
                    appendObjectReference(&variant, parentPath == kNullPath ? "" : impl->uniqueName.c_str(), parentPath.c_str());
                    dbus_message_iter_close_container(out, &variant);
                } else {
                    appendVariantString(out, "");
                }
            });
        }
        if (std::strcmp(member, "Set") == 0 && current != nullptr &&
            property != nullptr && std::strcmp(property, "CurrentValue") == 0) {
            dbus_message_iter_next(&args);
            if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT) {
                return replyError(connection, request, DBUS_ERROR_INVALID_ARGS,
                                  "CurrentValue must be a variant");
            }
            DBusMessageIter variant;
            dbus_message_iter_recurse(&args, &variant);
            double value = 0.0;
            if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_DOUBLE) {
                dbus_message_iter_get_basic(&variant, &value);
            } else {
                return replyError(connection, request, DBUS_ERROR_INVALID_ARGS,
                                  "CurrentValue must contain a double");
            }
            const auto status = impl->host.dispatch
                                    ? impl->host.dispatch(id, kActionSetValue,
                                                          std::to_string(value), 0.0F)
                                    : SemanticsActionStatus::NotHandled;
            return status == SemanticsActionStatus::Handled
                       ? replyEmpty(connection, request)
                       : replyError(connection, request, DBUS_ERROR_FAILED,
                                    "semantic SetValue was not handled");
        }
        if (std::strcmp(member, "GetAll") == 0) {
            return replyValue(connection, request, [&, current](DBusMessageIter* out) {
                DBusMessageIter array;
                dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "{sv}", &array);
                if (isRoot && propertyInterface != nullptr &&
                    std::strcmp(propertyInterface, "org.a11y.atspi.Application") == 0) {
                    DBusMessageIter entry;
                    dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
                    appendString(&entry, "Id");
                    appendVariantInt32(&entry, impl->applicationId);
                    dbus_message_iter_close_container(&array, &entry);
                }
                if (current != nullptr) {
                    DBusMessageIter entry;
                    dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
                    appendString(&entry, "Name");
                    appendVariantString(&entry, current->label);
                    dbus_message_iter_close_container(&array, &entry);
                }
                dbus_message_iter_close_container(out, &array);
            });
        }
        return replyError(connection, request, DBUS_ERROR_UNKNOWN_METHOD,
                          "unsupported property");
    }

    if (std::strcmp(interface, "org.a11y.atspi.Accessible") == 0) {
        if (std::strcmp(member, "GetName") == 0) {
            return replyValue(connection, request, [node](DBusMessageIter* out) {
                appendString(out, node->label);
            });
        }
        if (std::strcmp(member, "GetDescription") == 0) {
            return replyValue(connection, request, [](DBusMessageIter* out) {
                appendString(out, {});
            });
        }
        if (std::strcmp(member, "GetParent") == 0) {
            const std::string parentPath = impl->parentPathFor(impl->idAt(path));
            const char* parentBus = parentPath == kNullPath ? "" : impl->uniqueName.c_str();
            return replyValue(connection, request, [parentBus, parentPath](DBusMessageIter* out) {
                appendObjectReference(out, parentBus, parentPath.c_str());
            });
        }
        if (std::strcmp(member, "GetRole") == 0) {
            return replyValue(connection, request, [node](DBusMessageIter* out) {
                dbus_uint32_t value = static_cast<dbus_uint32_t>(roleValue(node->role));
                dbus_message_iter_append_basic(out, DBUS_TYPE_UINT32, &value);
            });
        }
        if (std::strcmp(member, "GetRoleName") == 0 ||
            std::strcmp(member, "GetLocalizedRoleName") == 0) {
            return replyValue(connection, request, [node](DBusMessageIter* out) {
                appendString(out, semanticsRoleName(node->role));
            });
        }
        if (std::strcmp(member, "GetChildAtIndex") == 0) {
            DBusMessageIter args;
            dbus_message_iter_init(request, &args);
            dbus_int32_t index = -1;
            dbus_message_iter_get_basic(&args, &index);
            if (index < 0 || static_cast<std::size_t>(index) >= node->children.size()) {
                return replyError(connection, request, DBUS_ERROR_INVALID_ARGS,
                                  "child index out of range");
            }
            const std::string childPath = impl->pathFor(node->children[index]);
            return replyValue(connection, request, [&, childPath](DBusMessageIter* out) {
                appendObjectReference(out, impl->uniqueName.c_str(), childPath.c_str());
            });
        }
        if (std::strcmp(member, "GetChildren") == 0) {
            return replyValue(connection, request, [&, node](DBusMessageIter* out) {
                DBusMessageIter array;
                dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "(so)", &array);
                for (const auto& child : node->children) {
                    const std::string childPath = impl->pathFor(child);
                    appendObjectReference(&array, impl->uniqueName.c_str(), childPath.c_str());
                }
                dbus_message_iter_close_container(out, &array);
            });
        }
        if (std::strcmp(member, "GetIndexInParent") == 0) {
            int index = -1;
            const auto parent = std::find_if(impl->tree.nodes.begin(), impl->tree.nodes.end(),
                                             [id = impl->idAt(path)](const auto& pair) {
                                                 return std::find(pair.second.children.begin(),
                                                                  pair.second.children.end(), id) != pair.second.children.end();
                                             });
            if (parent != impl->tree.nodes.end()) {
                index = static_cast<int>(std::distance(parent->second.children.begin(),
                    std::find(parent->second.children.begin(), parent->second.children.end(), impl->idAt(path))));
            }
            return replyValue(connection, request, [index](DBusMessageIter* out) {
                const dbus_int32_t value = index;
                dbus_message_iter_append_basic(out, DBUS_TYPE_INT32, &value);
            });
        }
        if (std::strcmp(member, "GetState") == 0) {
            return replyValue(connection, request, [&, node](DBusMessageIter* out) {
                appendStateArray(out, *node, impl->focusedId);
            });
        }
        if (std::strcmp(member, "GetApplication") == 0) {
            return replyValue(connection, request, [&, outName = impl->uniqueName](DBusMessageIter* out) {
                appendObjectReference(out, outName.c_str(), kRootPath);
            });
        }
        if (std::strcmp(member, "GetInterfaces") == 0) {
            return replyValue(connection, request, [node, isRoot](DBusMessageIter* out) {
                DBusMessageIter array;
                dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "s", &array);
                appendString(&array, "org.a11y.atspi.Accessible");
                if (isRoot) appendString(&array, "org.a11y.atspi.Application");
                if (node->actions != 0) appendString(&array, "org.a11y.atspi.Action");
                if (node->role == SemanticsRole::Slider || node->role == SemanticsRole::ProgressBar || node->role == SemanticsRole::Splitter) appendString(&array, "org.a11y.atspi.Value");
                appendString(&array, "org.a11y.atspi.Component");
                dbus_message_iter_close_container(out, &array);
            });
        }
    }
    if (std::strcmp(interface, "org.a11y.atspi.Action") == 0) {
        std::vector<std::pair<const char*, std::uint32_t>> actions;
        if ((node->actions & kActionActivate) != 0) actions.emplace_back("activate", kActionActivate);
        if ((node->actions & kActionFocus) != 0) actions.emplace_back("focus", kActionFocus);
        if (std::strcmp(member, "GetName") == 0 || std::strcmp(member, "GetDescription") == 0 ||
            std::strcmp(member, "GetKeyBinding") == 0) {
            dbus_int32_t index = -1;
            if (!dbus_message_get_args(request, nullptr, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID) ||
                index < 0 || static_cast<std::size_t>(index) >= actions.size()) {
                return replyError(connection, request, DBUS_ERROR_INVALID_ARGS, "invalid action index");
            }
            return replyValue(connection, request, [&](DBusMessageIter* out) {
                appendString(out, std::strcmp(member, "GetKeyBinding") == 0 ? "" : actions[index].first);
            });
        }
        if (std::strcmp(member, "GetActions") == 0) {
            return replyValue(connection, request, [actions](DBusMessageIter* out) {
                DBusMessageIter array;
                dbus_message_iter_open_container(out, DBUS_TYPE_ARRAY, "(sss)", &array);
                for (const auto& [name, action] : actions) {
                    (void)action;
                    DBusMessageIter tuple;
                    dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, nullptr, &tuple);
                    appendString(&tuple, name);
                    appendString(&tuple, name);
                    appendString(&tuple, "");
                    dbus_message_iter_close_container(&array, &tuple);
                }
                dbus_message_iter_close_container(out, &array);
            });
        }
        if (std::strcmp(member, "DoAction") == 0) {
            DBusMessageIter args;
            dbus_message_iter_init(request, &args);
            dbus_int32_t index = -1;
            dbus_message_iter_get_basic(&args, &index);
            const bool valid = index >= 0 && static_cast<std::size_t>(index) < actions.size();
            SemanticsActionStatus status = SemanticsActionStatus::NotHandled;
            if (valid && (node->flags & kSemanticsEnabled) != 0 && impl->host.dispatch) {
                status = impl->host.dispatch(impl->idAt(path), actions[index].second, {}, 0.0F);
            }
            return replyValue(connection, request, [status](DBusMessageIter* out) {
                dbus_bool_t value = status == SemanticsActionStatus::Handled ? TRUE : FALSE;
                dbus_message_iter_append_basic(out, DBUS_TYPE_BOOLEAN, &value);
            });
        }
    }
    if (std::strcmp(interface, "org.a11y.atspi.Component") == 0) {
        if (std::strcmp(member, "GetExtents") == 0) {
            return replyValue(connection, request, [&, node](DBusMessageIter* out) {
                DBusMessageIter tuple;
                dbus_message_iter_open_container(out, DBUS_TYPE_STRUCT, nullptr, &tuple);
                const auto number = [](float value) { return static_cast<dbus_int32_t>(std::lround(value)); };
                dbus_int32_t x = number(node->bounds.origin.x * impl->host.deviceScale);
                dbus_int32_t y = number(node->bounds.origin.y * impl->host.deviceScale);
                dbus_int32_t w = number(node->bounds.size.width * impl->host.deviceScale);
                dbus_int32_t h = number(node->bounds.size.height * impl->host.deviceScale);
                dbus_message_iter_append_basic(&tuple, DBUS_TYPE_INT32, &x);
                dbus_message_iter_append_basic(&tuple, DBUS_TYPE_INT32, &y);
                dbus_message_iter_append_basic(&tuple, DBUS_TYPE_INT32, &w);
                dbus_message_iter_append_basic(&tuple, DBUS_TYPE_INT32, &h);
                dbus_message_iter_close_container(out, &tuple);
            });
        }
        if (std::strcmp(member, "GrabFocus") == 0) {
            const auto status = impl->host.dispatch
                                    ? impl->host.dispatch(impl->idAt(path), kActionFocus, {}, 0.0F)
                                    : SemanticsActionStatus::NotHandled;
            return replyValue(connection, request, [status](DBusMessageIter* out) {
                dbus_bool_t value = status == SemanticsActionStatus::Handled ? TRUE : FALSE;
                dbus_message_iter_append_basic(out, DBUS_TYPE_BOOLEAN, &value);
            });
        }
    }
    if (std::strcmp(interface, "org.a11y.atspi.Value") == 0 &&
        std::strcmp(member, "GetCurrentValue") == 0) {
        double value = 0.0;
        try { value = std::stod(node->value); } catch (...) {}
        return replyValue(connection, request, [value](DBusMessageIter* out) {
            dbus_message_iter_append_basic(out, DBUS_TYPE_DOUBLE, &value);
        });
    }
    return replyError(connection, request, DBUS_ERROR_UNKNOWN_METHOD,
                      "unsupported AT-SPI method");
}

DBusHandlerResult objectPathMessage(DBusConnection* connection,
                                     DBusMessage* request, void* userData) {
    return handleMessage(connection, request, userData);
}

const DBusObjectPathVTable kObjectPathVtable = {nullptr, objectPathMessage,
                                                nullptr, nullptr, nullptr,
                                                nullptr};

void emitSignal(AtspiAccessibilityBridge::Impl* impl, const char* path,
                const char* interface, const char* member,
                const char* signature, const std::string& property,
                const std::string& value = {}, dbus_int32_t detail1 = 0) {
    if (!impl->connected || impl->connection == nullptr) return;
    DBusMessage* signal = dbus_message_new_signal(path, interface, member);
    if (signal == nullptr) return;
    DBusMessageIter args;
    dbus_message_iter_init_append(signal, &args);
    if (std::strcmp(signature, "property") == 0) {
        appendString(&args, property);
        dbus_int32_t detail2 = 0;
        dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &detail1);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &detail2);
        if (property == "accessible-value") {
            appendVariantDouble(&args, std::strtod(value.c_str(), nullptr));
        } else appendVariantString(&args, value);
        DBusMessageIter dict;
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
        dbus_message_iter_close_container(&args, &dict);
    } else {
        appendString(&args, property);
        dbus_int32_t detail2 = 0;
        dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &detail1);
        dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &detail2);
        if (std::strcmp(signature, "children") == 0) {
            DBusMessageIter variant;
            dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "(so)", &variant);
            appendObjectReference(&variant, impl->uniqueName.c_str(), value.c_str());
            dbus_message_iter_close_container(&args, &variant);
        } else appendVariantString(&args, "");
        DBusMessageIter dict;
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
        dbus_message_iter_close_container(&args, &dict);
    }
    dbus_connection_send(impl->connection, signal, nullptr);
    dbus_message_unref(signal);
}

}  // namespace

#endif  // LUMEN_HAS_DBUS

AtspiAccessibilityBridge::AtspiAccessibilityBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics)
    : impl_(std::make_unique<Impl>(host)) {
#if defined(LUMEN_HAS_DBUS)
    impl_->connection = openAccessibilityBus(diagnostics);
    if (impl_->connection == nullptr) {
        return;
    }
    const char* unique = dbus_bus_get_unique_name(impl_->connection);
    impl_->uniqueName = unique != nullptr ? unique : "";
    if (!dbus_connection_register_fallback(impl_->connection,
                                           "/org/a11y/atspi",
                                           &kObjectPathVtable, impl_.get())) {
        if (diagnostics != nullptr) *diagnostics = "AT-SPI object registration failed";
        dbus_connection_close(impl_->connection);
        dbus_connection_unref(impl_->connection);
        impl_->connection = nullptr;
        return;
    }
    DBusMessage* embed = dbus_message_new_method_call(
        kRegistryName, kRegistryPath, kRegistryInterface, "Embed");
    if (embed == nullptr) return;
    DBusMessageIter args;
    dbus_message_iter_init_append(embed, &args);
    appendObjectReference(&args, impl_->uniqueName.c_str(), kRootPath);
    DBusError error;
    dbus_error_init(&error);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
        impl_->connection, embed, 500, &error);
    dbus_message_unref(embed);
    if (reply == nullptr) {
        if (diagnostics != nullptr) {
            *diagnostics = dbus_error_is_set(&error)
                               ? std::string("AT-SPI registry unavailable: ") + error.message
                               : "AT-SPI registry did not accept the application";
        }
        dbus_error_free(&error);
        return;
    }
    dbus_message_unref(reply);
    dbus_error_free(&error);
    impl_->connected = true;
#else
    if (diagnostics != nullptr) {
        *diagnostics = "built without libdbus (LUMEN_HAS_DBUS undefined)";
    }
#endif
}

AtspiAccessibilityBridge::~AtspiAccessibilityBridge() {
#if defined(LUMEN_HAS_DBUS)
    if (impl_->connection != nullptr) {
        dbus_connection_flush(impl_->connection);
        dbus_connection_close(impl_->connection);
        dbus_connection_unref(impl_->connection);
        impl_->connection = nullptr;
    }
#endif
}

bool AtspiAccessibilityBridge::available() const { return impl_->connected; }

void AtspiAccessibilityBridge::updateTree(const SemanticsTree& tree,
                                          const SemanticsDiff& diff,
                                          const std::string& focusedId) {
    const SemanticsTree previous = std::move(impl_->tree);
    impl_->tree = tree;
    impl_->pathToId.clear();
    impl_->idToPath.clear();
    for (const auto& [id, node] : tree.nodes) {
        (void)node;
        const std::string path = impl_->pathFor(id);
        impl_->idToPath[id] = path;
        impl_->pathToId[path] = id;
    }
#if defined(LUMEN_HAS_DBUS)
    if (impl_->connected) {
        const auto emitEdges = [&](const SemanticsTree& from, const SemanticsTree& to,
                                   const char* operation) {
            for (const auto& [id, parent] : to.nodes) {
                const auto* old = from.find(id);
                for (std::size_t index = 0; index < parent.children.size(); ++index) {
                    const auto& child = parent.children[index];
                    if (old && std::find(old->children.begin(), old->children.end(), child) != old->children.end()) continue;
                    const auto path = impl_->pathFor(id);
                    emitSignal(impl_.get(), path.c_str(), "org.a11y.atspi.Event.Object",
                               "ChildrenChanged", "children", operation,
                               impl_->pathFor(child), static_cast<dbus_int32_t>(index));
                }
            }
        };
        emitEdges(tree, previous, "remove");
        emitEdges(previous, tree, "add");
        for (const auto& id : diff.changed) {
            const auto* node = tree.find(id);
            if (node != nullptr) {
                const std::string path = impl_->pathFor(id);
                emitSignal(impl_.get(), path.c_str(),
                           "org.a11y.atspi.Event.Object", "PropertyChange",
                           "property", "accessible-name", node->label);
                emitSignal(impl_.get(), path.c_str(),
                           "org.a11y.atspi.Event.Object", "PropertyChange",
                           "property", "accessible-value", node->value);
            }
        }
    }
#else
    (void)diff;
#endif
    setFocusedNode(focusedId);
}

void AtspiAccessibilityBridge::setFocusedNode(const std::string& id) {
    const std::string previous = impl_->focusedId;
    if (previous == id) return;
    impl_->focusedId = id;
#if defined(LUMEN_HAS_DBUS)
    if (impl_->connected && !previous.empty() && previous != id) {
        const auto path = impl_->pathFor(previous);
        emitSignal(impl_.get(), path.c_str(), "org.a11y.atspi.Event.Object",
                   "StateChanged", "state", "focused", {}, 0);
    }
    if (impl_->connected && !id.empty()) {
        const std::string path = impl_->pathFor(id);
        emitSignal(impl_.get(), path.c_str(), "org.a11y.atspi.Event.Object",
                   "StateChanged", "state", "focused", {}, 1);
    }
#endif
}

void AtspiAccessibilityBridge::pump() {
#if defined(LUMEN_HAS_DBUS)
    if (!impl_->connected || impl_->connection == nullptr) return;
    dbus_connection_read_write(impl_->connection, 0);
    while (dbus_connection_dispatch(impl_->connection) == DBUS_DISPATCH_DATA_REMAINS) {
    }
#endif
}

std::size_t AtspiAccessibilityBridge::nodeCountForTesting() const {
    return impl_->tree.nodes.size();
}

std::string AtspiAccessibilityBridge::objectPathForTesting(
    const std::string& identity) const {
    return impl_->pathFor(identity);
}

std::unique_ptr<AccessibilityBridge> createAtspiBridge(
    const PlatformAccessibilityHost& host, std::string* diagnostics) {
    auto bridge = std::make_unique<AtspiAccessibilityBridge>(host, diagnostics);
    return bridge->available() || host.nativeWindow == nullptr
               ? std::unique_ptr<AccessibilityBridge>(std::move(bridge))
               : nullptr;
}

}  // namespace lumen::accessibility::atspi

#endif  // defined(__linux__) && defined(LUMEN_ACCESSIBILITY_PROVIDER_ATSPI)
