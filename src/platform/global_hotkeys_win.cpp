// R4/M16：全局快捷键 Win32 后端（global_hotkeys.h 契约实现；仅 Windows
// 编译——windows.yml cpu job 为编译门禁）。RegisterHotKey 绑定一个消
// 息专用窗口（HWND_MESSAGE 父级，不可见不激活）：WM_HOTKEY 经 SDL 事件
// 泵（同线程 PeekMessage/DispatchMessage，SDL 对线程内全部窗口分发）到
// 达本窗口 WndProc，入待发队列，pollEvent 非阻塞消费转 GlobalHotkey 事
// 件（与 SDL_tray 激活同模式）。UI 线程独占（创建/注册/轮询/析构），
// 无跨线程共享。
//
// 验证边界（如实登记，support-matrix R4）：编译级验证 = Windows CI；
// 真实按键验收（消息泵到达到 UI 事件、冲突码 GetLastError）待登录
// Win32 会话现场执行（platform-acceptance 登记）。
//
// MOD_NOREPEAT（Win Vista+）：按住不重复触发——与 X11 后端 grab 语义
// 对齐。键值映射只覆盖 core::Key 枚举既有键（字符键不进全局快捷键，
// 与 X11 后端同口径）。

#include "global_hotkeys.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <deque>
#include <map>
#include <string>
#include <utility>

namespace lumen::platform::hotkeys {
namespace {

// core::Key → Win32 虚拟键（无映射返回 0——None/未覆盖键拒绝注册）。
UINT virtualKeyForLumenKey(core::Key key) {
    switch (key) {
        case core::Key::Backspace: return VK_BACK;
        case core::Key::Tab: return VK_TAB;
        case core::Key::Enter: return VK_RETURN;
        case core::Key::Escape: return VK_ESCAPE;
        case core::Key::Left: return VK_LEFT;
        case core::Key::Right: return VK_RIGHT;
        case core::Key::Up: return VK_UP;
        case core::Key::Down: return VK_DOWN;
        case core::Key::Home: return VK_HOME;
        case core::Key::End: return VK_END;
        case core::Key::Delete: return VK_DELETE;
        case core::Key::PageUp: return VK_PRIOR;
        case core::Key::PageDown: return VK_NEXT;
        case core::Key::F10: return VK_F10;
        default: return 0;
    }
}

UINT modifierMask(std::uint32_t lumenModifiers) {
    UINT mask = 0;
    if ((lumenModifiers & core::kModifierShift) != 0) mask |= MOD_SHIFT;
    if ((lumenModifiers & core::kModifierCtrl) != 0) mask |= MOD_CONTROL;
    if ((lumenModifiers & core::kModifierAlt) != 0) mask |= MOD_ALT;
    if ((lumenModifiers & core::kModifierGui) != 0) mask |= MOD_WIN;
    return mask;
}

constexpr wchar_t kClassName[] = L"LumenGlobalHotkeySink";

// 待发事件（WndProc → poll；同线程，无锁——UI 线程独占）。
struct PendingHotkey {
    core::WindowId owner{};
    std::string id{};
};

class Win32Backend final : public Backend {
  public:
    Win32Backend(HINSTANCE instance, HWND window)
        : instance_(instance), window_(window) {
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(this));
    }

    ~Win32Backend() override {
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        for (const auto& [nativeId, registration] : registrations_) {
            (void)registration;
            UnregisterHotKey(window_, nativeId);
        }
        registrations_.clear();
        pending_.clear();
        if (window_ != nullptr) {
            DestroyWindow(window_);
        }
        UnregisterClassW(kClassName, instance_);
    }

    // 每个后端实例持有独立的消息窗口与注册表（多 host 进程内共存）；
    // WndProc 经 GWLP_USERDATA 找回属主。
    static LRESULT CALLBACK hotkeySinkProc(HWND hwnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Win32Backend*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self == nullptr) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        if (message == WM_HOTKEY) {
            const auto found =
                self->registrations_.find(static_cast<int>(wParam));
            if (found != self->registrations_.end()) {
                self->pending_.push_back(found->second);
            }
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    [[nodiscard]] ServiceResult registerHotkey(
        core::WindowId owner, const GlobalHotkeySpec& spec) override {
        if (spec.id.empty()) {
            return ServiceResult::failed(
                "global hotkey: id must not be empty");
        }
        if (registrations_.size() >= kMaxRegistrations) {
            return ServiceResult::failed(
                "global hotkey: registration budget exhausted");
        }
        for (const auto& [nativeId, registration] : registrations_) {
            (void)nativeId;
            if (registration.id == spec.id) {
                return ServiceResult::failed(
                    "global hotkey: duplicate id '" + spec.id + "'");
            }
        }
        const UINT vk = virtualKeyForLumenKey(spec.key);
        if (vk == 0) {
            return ServiceResult::failed(
                "global hotkey: key not mapped for global scope (id '" +
                spec.id + "')");
        }
        const UINT mods = modifierMask(spec.modifiers);
        if (mods == 0) {
            return ServiceResult::failed(
                "global hotkey: bare key grab refused (id '" + spec.id +
                "')");
        }
        const int nativeId = nextNativeId_++;
        if (RegisterHotKey(window_, nativeId, mods | MOD_NOREPEAT, vk) ==
            FALSE) {
            return ServiceResult::failed(
                "global hotkey: RegisterHotKey failed (id '" + spec.id +
                "', GetLastError=" + std::to_string(GetLastError()) + ")");
        }
        registrations_[nativeId] = PendingHotkey{owner, spec.id};
        return ServiceResult::success();
    }

    [[nodiscard]] ServiceResult unregisterHotkey(
        const std::string& id) override {
        for (auto it = registrations_.begin(); it != registrations_.end();
             ++it) {
            if (it->second.id == id) {
                UnregisterHotKey(window_, it->first);
                registrations_.erase(it);
                return ServiceResult::success();
            }
        }
        return ServiceResult::failed("global hotkey: unknown id '" + id +
                                     "'");
    }

    [[nodiscard]] bool poll(core::WindowId& owner, std::string& id) override {
        // WM_HOTKEY 经 SDL 泵（WIN_PumpEvents 对线程内全部窗口分发）
        // 已进队列；这里只取队首。
        if (pending_.empty()) {
            return false;
        }
        const PendingHotkey pending = std::move(pending_.front());
        pending_.pop_front();
        owner = pending.owner;
        id = pending.id;
        return true;
    }

  private:
    // RegisterHotKey 的 id 参数为任意非零 int；预算上限防泄漏性堆积。
    static constexpr std::size_t kMaxRegistrations = 4096;

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    int nextNativeId_{1};
    std::map<int, PendingHotkey> registrations_{};
    std::deque<PendingHotkey> pending_{};
};

}  // namespace

std::unique_ptr<Backend> createWin32Backend() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (instance == nullptr) {
        return nullptr;
    }
    const WNDCLASSW wc{
        0,                      // style
        &Win32Backend::hotkeySinkProc,  // lpfnWndProc
        0,                      // cbClsExtra
        0,                      // cbWndExtra
        instance,               // hInstance
        nullptr,                // hIcon
        nullptr,                // hCursor
        nullptr,                // hbrBackground
        nullptr,                // lpszMenuName
        kClassName,             // lpszClassName
    };
    if (RegisterClassW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }
    // 消息专用窗口：不可见、不进 z 序、只收消息。
    const HWND window = CreateWindowExW(
        0, kClassName, L"", WS_OVERLAPPED, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
        instance, nullptr);
    if (window == nullptr) {
        UnregisterClassW(kClassName, instance);
        return nullptr;
    }
    return std::make_unique<Win32Backend>(instance, window);
}

}  // namespace lumen::platform::hotkeys

#endif  // _WIN32
