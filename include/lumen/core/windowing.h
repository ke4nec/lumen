#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"

namespace lumen::core {

// v0.3 阶段8A (plan §3.1): 平台无关的窗口与事件值类型。
//
// 这些值类型属于 lumen-core（plan §6: “core 持有平台无关事件值类型”），
// SDL3、macOS native host 与移动端 host 都只负责填充字段；交互控制器
// 只消费这里的归一化结果。平台 SDK 类型不得出现在本头文件中。

// Platform-agnostic key codes. Platform layers map their native keys onto
// these; printable input arrives separately through text input events.
enum class Key : int {
    None = 0,
    Backspace,
    Tab,
    Enter,
    Escape,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    Delete,
    // v0.3 阶段8B/8D：编辑与滚动键盘支持。
    PageUp,
    PageDown,
    Backtab,  // Shift-Tab（焦点反向遍历）
};

// 稳定窗口标识。事件、资源上传、语义节点与诊断都通过它关联窗口。
struct WindowId {
    std::uint64_t value{0};

    [[nodiscard]] bool valid() const { return value != 0; }
    [[nodiscard]] bool operator==(const WindowId&) const = default;
    [[nodiscard]] bool operator<(const WindowId& other) const {
        return value < other.value;
    }
};

// 窗口指标快照。窗口重建、DPI 变化和应用恢复必须先更新 WindowMetrics，
// 再请求 FrameScheduler（plan §3.1）。
struct WindowMetrics {
    Size logicalSize{};
    Size drawableSize{};
    EdgeInsets safeArea{};
    float deviceScale{1.0F};
    bool visible{true};
    bool minimized{false};
    // 自定义标题栏（lumen-titlebar-design §4.3）：最大化/还原状态（窗口
    // 按钮图标与布局自适应消费；恢复自最小化时为 false）。
    bool maximized{false};

    [[nodiscard]] bool operator==(const WindowMetrics&) const = default;
};

// 应用生命周期（plan §3.1）。桌面近似：启动后 Active，全部窗口最小化/
// 失焦时 Inactive，退出走 Terminating；Background/Suspended 保留给移动端
// host（阶段8E）。
enum class AppLifecycle : std::uint8_t {
    Launching,
    Active,
    Inactive,
    Background,
    Suspended,
    Terminating,
};

[[nodiscard]] const char* appLifecycleName(AppLifecycle lifecycle);

// 键盘修饰键位集。
enum KeyModifierBits : std::uint32_t {
    kModifierNone = 0U,
    kModifierShift = 1U << 0,
    kModifierCtrl = 1U << 1,
    kModifierAlt = 1U << 2,
    kModifierGui = 1U << 3,  // macOS Command / Windows/Linux Win(Super)
};

using KeyModifiers = std::uint32_t;

// 指针设备类型（plan §3.1 pointer 设备类型）。
enum class PointerDevice : std::uint8_t {
    Unknown,
    Mouse,
    Touch,
    Pen,
};

// 主指针按键（PointerDown/Up 携带；-1 表示无按钮信息）。
enum class PointerButton : std::int8_t {
    None = -1,
    Primary = 0,
    Secondary = 1,
    Middle = 2,
};

// 归一化事件类型。相比 v0.2 的 platform::Event，这里补充时间戳、
// WindowId、修饰键、指针设备/pointer id、滚轮增量、指针取消、窗口焦点
// 变化、关闭请求与生命周期/surface 事件（plan §3.1）。
enum class HostEventType : std::uint8_t {
    None = 0,
    Quit,
    // 指针（含触摸）。
    PointerDown,
    PointerUp,
    PointerMove,
    PointerCancel,
    Wheel,
    // 键盘与文本。
    KeyDown,
    KeyUp,
    TextInput,
    TextEditing,  // IME preedit；绝不直接提交进文档
    // 窗口。
    Resize,
    DpiChanged,
    WindowMinimized,
    WindowRestored,
    // 自定义标题栏（lumen-titlebar-design §4.3）：最大化完成（toggle-
    // MaximizeWindow 或系统途径）；还原走 WindowRestored。
    WindowMaximized,
    WindowFocusGained,
    WindowFocusLost,
    WindowCloseRequested,
    // 生命周期与 surface（移动端 host 产生；状态树不得销毁）。
    LifecycleChanged,
    SurfaceDetached,
    SurfaceReattached,
    // M4：平台服务异步完成（文件选择等）。
    FileDialogCompleted,
    // M12：系统主题切换（dark/light；SDL_SYSTEM_THEME_CHANGED 翻译，
    // fake host 可注入）。经 RunOptions.onEvent 转发给应用。
    SystemThemeChanged,
    // 高对比/减少动画/字体缩放变化；宿主先更新 capabilities 再广播。
    SystemAccessibilityChanged,
};

struct HostEvent {
    HostEventType type{HostEventType::None};
    std::uint64_t timestampMs{0};
    WindowId window{};

    // 逻辑坐标（Pointer*/Wheel）。
    Offset position{};
    // 滚轮/触摸板逻辑像素增量；y>0 向下（Wheel）。
    Offset scrollDelta{};

    // 键盘。keyCode 是归一化逻辑键；scanCode 是平台物理键（仅诊断）；
    // keyChar 是无修饰时的可打印字符（Ctrl/Command 快捷键判定用）。
    Key keyCode{Key::None};
    int scanCode{0};
    char keyChar{0};
    KeyModifiers modifiers{kModifierNone};

    // 指针设备信息。
    PointerDevice device{PointerDevice::Unknown};
    PointerButton button{PointerButton::None};
    std::uint32_t pointerId{0};

    // 文本。TextInput 携带 UTF-8 commit 文本；TextEditing 携带 preedit
    // 文本与光标（editCursor/editLength 为平台原始单位，转换只发生在
    // 适配层）。
    std::string text{};
    int editCursor{0};
    int editLength{0};

    // 新 drawable 物理像素尺寸（Resize/DpiChanged）。
    Size pixelSize{};

    // LifecycleChanged 携带新旧状态。
    AppLifecycle lifecycle{AppLifecycle::Launching};
    AppLifecycle previousLifecycle{AppLifecycle::Launching};

    // M4：FileDialogCompleted 携带用户选择的路径（取消为空且无错误；
    // text 复用为失败诊断消息）。
    std::vector<std::string> filePaths{};
};

}  // namespace lumen::core
