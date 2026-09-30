// R4/M16：全局快捷键平台接缝实现（global_hotkeys.h 契约）。Linux 且
// Xlib 头存在时编入 X11 后端：独立 X 连接 XGrabKey 抓根窗口按键，事件
// 在本连接上排队，pollEvent 非阻塞轮询（UI 线程独占，无跨线程共享）；
// 锁定键（CapsLock/NumLock/ScrollLock）为每种组合各 grab 一次、事件匹
// 配时剔除掩码。其余平台（Win32/macOS/无 Xlib）走文件尾 stub 分支——
// probe/工厂符号在所有平台一致存在（sdl3_host 无条件调用），结构化
// 不可用并带平台真实原因。
//
// 边界：XOpenDisplay 失败（无显示）→ 工厂返回 nullptr（调用方结构化
// 不可用）；keysym 不在当前 keymap（keycode 0）→ 注册 Failed 带原因；
// XGrabKey 的 BadAccess 异步投递——临时错误处理器 + XSync 检测冲突；
// 注册表进程内持有——析构统一 ungrab 并关闭连接。

#include "global_hotkeys.h"

// X11 分支与 CMake 链接范围一致（UNIX 非 Apple；macOS 上的 XQuartz 头
// 不触发编译——无链接对应）。
#if !defined(__APPLE__) && !defined(_WIN32) && __has_include(<X11/Xlib.h>)

#include <X11/Xlib.h>

// Xlib 把 None/True/False 定义为宏——core::Key::None 与语义布尔在此
// 文件内冲突，include 后解除（后续 X 调用不再消费这些标识符）。
#undef None
#undef True
#undef False

#include <cstdlib>
#include <map>
#include <string_view>
#include <utility>

namespace lumen::platform::hotkeys {

// 纯映射（global_hotkeys.h 契约；外部链接供单测直连）。
long keysymForLumenKey(core::Key key) {
    // 与 <X11/keysymdef.h> 同值（XK_*）。
    switch (key) {
        case core::Key::Backspace: return 0xff08;
        case core::Key::Tab: return 0xff09;
        case core::Key::Backtab: return 0xff89;  // ISO_Left_Tab
        case core::Key::Enter: return 0xff0d;    // XK_Return
        case core::Key::Escape: return 0xff1b;
        case core::Key::Left: return 0xff51;
        case core::Key::Up: return 0xff52;
        case core::Key::Right: return 0xff53;
        case core::Key::Down: return 0xff54;
        case core::Key::Home: return 0xff50;
        case core::Key::End: return 0xff57;
        case core::Key::PageUp: return 0xff55;    // XK_Prior
        case core::Key::PageDown: return 0xff56;  // XK_Next
        case core::Key::Delete: return 0xffff;
        default: return 0;
    }
}

int modifierMask(std::uint32_t lumenModifiers) {
    int mask = 0;
    if ((lumenModifiers & core::kModifierShift) != 0) mask |= ShiftMask;
    if ((lumenModifiers & core::kModifierCtrl) != 0) mask |= ControlMask;
    if ((lumenModifiers & core::kModifierAlt) != 0) mask |= Mod1Mask;
    if ((lumenModifiers & core::kModifierGui) != 0) mask |= Mod4Mask;
    return mask;
}

namespace {

// 锁定键组合的 8 个子集（LockMask=Caps、Mod2Mask=Num、Mod5Mask=Scroll）；
// 事件匹配时同样剔除这三个掩码。
constexpr int kLockVariants[8] = {
    0,
    LockMask,
    Mod2Mask,
    LockMask | Mod2Mask,
    Mod5Mask,
    LockMask | Mod5Mask,
    Mod2Mask | Mod5Mask,
    LockMask | Mod2Mask | Mod5Mask};

constexpr int kLockMaskCombo = LockMask | Mod2Mask | Mod5Mask;

// 单条注册（keycode/mask 为 grab 与事件匹配的物理键事实）。
struct HotkeyRecord {
    core::WindowId owner{};
    KeyCode keycode{0};
    int mask{0};
};

// XGrabKey 的 BadAccess 经异步错误处理器投递（请求返回 Status 不携带）
// ——注册窗口内挂临时处理器，XSync 后按标志判定冲突。Xlib 处理器按
// 线程隔离；本类的 grab/ungrab 只发生在 UI 线程（host 契约）。
struct BadAccessGuard {
    Display* display{};
    bool conflict{false};

    static int handler(Display*, XErrorEvent* event) {
        if (event->error_code == BadAccess) {
            active->conflict = true;
        }
        return 0;
    }

    static thread_local BadAccessGuard* active;

    [[nodiscard]] bool grabConflicts() {
        XErrorHandler previous = XSetErrorHandler(&handler);
        active = this;
        XSync(display, 0);  // Bool=False（Xlib 宏已 undef）。
        XSetErrorHandler(previous);
        active = nullptr;
        return conflict;
    }
};

thread_local BadAccessGuard* BadAccessGuard::active = nullptr;

class X11Backend final : public Backend {
  public:
    explicit X11Backend(Display* display) : display_(display) {
        root_ = DefaultRootWindow(display_);
        // 按键事件发给抓取方；根窗口需要选择 KeyPressMask 才投递到本
        // 连接。
        XSelectInput(display_, root_, KeyPressMask);
        XFlush(display_);
    }

    ~X11Backend() override {
        for (const auto& [id, record] : records_) {
            (void)id;
            ungrab(record);
        }
        if (display_ != nullptr) {
            XFlush(display_);
            XCloseDisplay(display_);
        }
    }

    [[nodiscard]] ServiceResult registerHotkey(
        core::WindowId owner, const GlobalHotkeySpec& spec) override {
        if (spec.id.empty()) {
            return ServiceResult::failed(
                "global hotkey: id must not be empty");
        }
        if (spec.key == core::Key::None) {
            return ServiceResult::failed(
                "global hotkey: key must not be None");
        }
        if (records_.find(spec.id) != records_.end()) {
            return ServiceResult::failed("global hotkey: duplicate id '" +
                                         spec.id + "'");
        }
        const long keysym = keysymForLumenKey(spec.key);
        if (keysym == 0) {
            return ServiceResult::failed(
                "global hotkey: key not mapped for global scope (id '" +
                spec.id + "')");
        }
        const KeyCode keycode = XKeysymToKeycode(display_, keysym);
        if (keycode == 0) {
            return ServiceResult::failed(
                "global hotkey: key not available on current keymap (id '" +
                spec.id + "')");
        }
        const int mask = modifierMask(spec.modifiers);
        if (mask == 0) {
            return ServiceResult::failed(
                "global hotkey: bare key grab refused (id '" + spec.id +
                "')");
        }
        for (const int locks : kLockVariants) {
            // owner_events=False：grab 期事件一律按 grab 窗口报告到本
            // 客户端（全局快捷键标准配方；XTEST 实测路径）。
            XGrabKey(display_, keycode, mask | locks, root_, 0,
                     GrabModeAsync, GrabModeAsync);
        }
        BadAccessGuard guard{display_, false};
        if (guard.grabConflicts()) {
            // 冲突（组合被其他客户端占用）：回滚本次全部 grab，保持
            // 无半注册状态。
            for (const int locks : kLockVariants) {
                XUngrabKey(display_, keycode, mask | locks, root_);
            }
            XSync(display_, 0);
            return ServiceResult::failed(
                "global hotkey: key combination already grabbed by another "
                "client (id '" +
                spec.id + "')");
        }
        records_.emplace(spec.id, HotkeyRecord{owner, keycode, mask});
        XFlush(display_);
        return ServiceResult::success();
    }

    [[nodiscard]] ServiceResult unregisterHotkey(
        const std::string& id) override {
        const auto found = records_.find(id);
        if (found == records_.end()) {
            return ServiceResult::failed("global hotkey: unknown id '" + id +
                                         "'");
        }
        ungrab(found->second);
        records_.erase(found);
        XFlush(display_);
        return ServiceResult::success();
    }

    [[nodiscard]] bool poll(core::WindowId& owner, std::string& id) override {
        while (XPending(display_) > 0) {
            XEvent event{};
            XNextEvent(display_, &event);
            if (event.type != KeyPress) {
                continue;
            }
            // 锁定键掩码剔除后匹配注册表（keycode + 修饰位）。
            const int state = event.xkey.state & ~kLockMaskCombo;
            for (const auto& [key, record] : records_) {
                if (record.keycode == event.xkey.keycode &&
                    record.mask == state) {
                    owner = record.owner;
                    id = key;
                    return true;
                }
            }
        }
        return false;
    }

  private:
    void ungrab(const HotkeyRecord& record) {
        for (const int locks : kLockVariants) {
            XUngrabKey(display_, record.keycode, record.mask | locks, root_);
        }
    }

    Display* display_{nullptr};
    Window root_{0};
    std::map<std::string, HotkeyRecord> records_{};
};

}  // namespace

Probe probeGlobalHotkeySession() {
    // Wayland 会话（含 XWayland 混合）：无标准全局快捷键协议；XWayland
    // grab 只覆盖 X11 客户端，结构化不可用（completion-plan R4）。
    const char* session = std::getenv("XDG_SESSION_TYPE");
    if (session != nullptr && std::string_view{session} == "wayland") {
        return Probe{false, "wayland session: no global hotkey protocol"};
    }
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (waylandDisplay != nullptr && session == nullptr) {
        return Probe{false,
                     "wayland session (WAYLAND_DISPLAY set): no global "
                     "hotkey protocol"};
    }
    // 无会话类型声明按 X11 尝试（Xvfb/纯 X 会话）；能否开显示由工厂的
    // XOpenDisplay 结果决定。
    return Probe{true, {}};
}

std::unique_ptr<Backend> createX11Backend(Probe probe) {
    if (!probe.available) {
        return nullptr;
    }
    Display* display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        return nullptr;
    }
    return std::make_unique<X11Backend>(display);
}

}  // namespace lumen::platform::hotkeys


#else  // 无 X11 头（Windows/macOS 或无 Xlib 的 Linux）：stub 分支保持
// 结构化不可用——sdl3_host 的能力探测/注册路径在所有平台一致编译。

namespace lumen::platform::hotkeys {

long keysymForLumenKey(core::Key) { return 0; }
int modifierMask(std::uint32_t) { return 0; }

Probe probeGlobalHotkeySession() {
    // Windows 由 createPlatformBackend 直连 Win32 后端（不经会话探测）；
    // 此处只剩 macOS 与无 Xlib 的 Linux stub。
#if defined(__APPLE__)
    return Probe{false,
                 "macos backend not implemented yet (RegisterEventHotKey)"};
#else
    return Probe{false, "x11 backend not compiled (no Xlib headers)"};
#endif
}

std::unique_ptr<Backend> createX11Backend(Probe probe) {
    (void)probe;
    return nullptr;
}

}  // namespace lumen::platform::hotkeys

#endif


namespace lumen::platform::hotkeys {

std::unique_ptr<Backend> createPlatformBackend(Probe& probe) {
#if defined(_WIN32)
    // Win32：RegisterHotKey 后端（global_hotkeys_win.cpp；编译门禁 =
    // windows.yml）。失败回退结构化不可用（原因如实）。
    auto backend = createWin32Backend();
    if (backend != nullptr) {
        probe.available = true;
        return backend;
    }
    probe.available = false;
    probe.reason = "win32 backend init failed (message window)";
    return nullptr;
#elif defined(__APPLE__)
    probe.available = false;
    probe.reason = "macos backend not implemented yet (RegisterEventHotKey)";
    return nullptr;
#else
    // Linux/BSD：会话探测 + X11（global_hotkeys.cpp X11 分支）。
    probe = probeGlobalHotkeySession();
    return createX11Backend(probe);
#endif
}

}  // namespace lumen::platform::hotkeys
