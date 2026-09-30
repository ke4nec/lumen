#pragma once

// R4/M16：全局快捷键平台后端内部接缝（不出公共头）。SDL 3.2.10 无系统
// 级快捷键 API——X11 经独立 X 连接 XGrabKey 实现（不干扰 SDL 事件循环，
// pollEvent 非阻塞轮询）；Wayland 无标准全局快捷键协议 → 结构化不可用
//（completion-plan 阶段B：能力经 PlatformCapabilities 如实报告）；Win32
// RegisterHotKey / macOS seam 为后续增量（保持 Unavailable + 命名原因）。
//
// 纯映射函数（keysym/修饰位）独立导出，单测无需 X 连接。

#include <cstdint>
#include <memory>
#include <string>

#include "lumen/platform/application_host.h"

namespace lumen::platform::hotkeys {

// core::Key → X keysym 值（与 <X11/keysymdef.h> 同值；core 头不引入
// X11 类型）。无映射（None/字符键等）返回 0——Key 枚举当前只覆盖编辑
// 与导航键。
[[nodiscard]] long keysymForLumenKey(core::Key key);

// core 修饰位 → X modifier mask（ShiftMask/ControlMask/Mod1Mask=Alt/
// Mod4Mask=Super；显式映射不依赖位序巧合）。
[[nodiscard]] int modifierMask(std::uint32_t lumenModifiers);

// 会话探测结果（结构化；available=false 时 reason 必非空）。
struct Probe {
    bool available{false};
    std::string reason{};
};

// 当前会话能否承载全局快捷键：Wayland 会话（XDG_SESSION_TYPE=wayland，
// 或 WAYLAND_DISPLAY 在场且无 X 会话声明）如实不可用——XWayland grab
// 只能捕获 X11 客户端按键，会误导应用；纯 X11 会话可用。
[[nodiscard]] Probe probeGlobalHotkeySession();

// 平台后端（活跃时由 Sdl3ApplicationHost 持有；全部调用限 UI 线程）。
class Backend {
  public:
    virtual ~Backend() = default;
    [[nodiscard]] virtual ServiceResult registerHotkey(
        core::WindowId owner, const GlobalHotkeySpec& spec) = 0;
    [[nodiscard]] virtual ServiceResult unregisterHotkey(
        const std::string& id) = 0;
    // 非阻塞轮询已捕获的快捷键事件；返回 true 时填 owner/id。
    [[nodiscard]] virtual bool poll(core::WindowId& owner,
                                    std::string& id) = 0;
};

// X11 后端工厂（probe.available=false 时返回 nullptr；实现见
// global_hotkeys.cpp 的 X11 分支，无 Xlib 头时同样 nullptr）。
[[nodiscard]] std::unique_ptr<Backend> createX11Backend(Probe probe);

// Win32 后端工厂（RegisterHotKey + 消息专用窗口；实现 global_hotkeys_win.cpp
// 仅 Windows 编译）。WM_HOTKEY 在 SDL 泵（同线程 DispatchMessage）期到
// 达本窗口的 WndProc，入队后 pollEvent 消费——与托盘同模式。
[[nodiscard]] std::unique_ptr<Backend> createWin32Backend();

// 平台分发（sdl3_host 装配点）：_WIN32 → Win32；__APPLE__ → nullptr
//（macOS 后端未实现，probe 给出原因）；其余 → 会话探测 + X11。
[[nodiscard]] std::unique_ptr<Backend> createPlatformBackend(
    Probe& probe);

}  // namespace lumen::platform::hotkeys
