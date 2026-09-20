#pragma once

#include <cstddef>
#include <string>

#include "lumen/core/geometry.h"
#include "lumen/render/renderer.h"

namespace lumen::platform {

// Platform-agnostic window contract (plan §4.4). SDL3 implements it; tests
// and future backends can fake it.
//
// `pollEvent()` returns `Event{None}` when the queue is empty. `keyCode`
// carries a lumen::core::Key value on KeyDown/KeyUp; printable input arrives
// as UTF-8 through `text` on TextInput events.
enum class EventType {
    None,
    Quit,
    PointerDown,
    PointerUp,
    PointerMove,
    KeyDown,
    KeyUp,
    TextInput,
    TextEditing,
    Resize,
    FocusGained,
    FocusLost,
    // v0.2 阶段7C/7D: 窗口状态与 DPI（plan §3.2 — resize、DPI、显示器切换
    // 必须先于下一帧 surface 重建处理）。
    WindowMinimized,
    WindowRestored,
    DpiChanged,
};

struct Event {
    EventType type{EventType::None};
    // Pointer position in logical coordinates (PointerDown/Up/Move).
    core::Offset position{};
    // lumen::core::Key value (KeyDown/KeyUp); UTF-8 text (TextInput).
    // TextEditing carries the in-progress IME composition string; it must
    // not be committed to the document (plan §10 leaves full IME out of
    // scope, but Linux IBus/Fcitx needs the event to be visible).
    int keyCode{0};
    std::string text{};
    // IME composition cursor/selection (TextEditing, may be -1 when unset).
    int editCursor{0};
    int editLength{0};
    // New drawable size in physical pixels (Resize).
    core::Size pixelSize{};
};

// 不透明原生 surface 句柄（v0.2 plan §3.2）。值只在 lumen-platform 与
// Renderer 适配层之间传递/解引用；SDL3 后端以 "sdl3" 系统标识携带
// SDL_Window*，GPU 适配据此创建 GL 上下文。
struct NativeSurfaceHandle {
    void* nativeWindow{nullptr};
    const char* windowSystem{""};
};

// present() 的结果（v0.2 plan §3.2）。
enum class PresentResult {
    Ok,
    // 缓冲区无效或后端未启用（如 OpenGL 窗口的 CPU present 路径）。
    Rejected,
    // 设备/上下文丢失：调用方应触发重建或回退。
    DeviceLost,
};

// Opt-in diagnostics for the most recent present (alpha plan P0/§6).
// prepareMs covers Lumen format preparation; submitMs covers the remaining
// host call, including SDL upload/blit/compositor waits. Neither is GPU time.
struct PresentStats {
    double prepareMs{0.0};
    double submitMs{0.0};
    std::uint64_t alphaConversions{0};
    std::uint64_t convertedBytes{0};
    std::uint64_t copiedBytes{0};
    std::size_t scratchCapacityBytes{0};
};

class PlatformWindow {
  public:
    virtual ~PlatformWindow() = default;
    virtual Event pollEvent() = 0;
    [[nodiscard]] virtual core::Size logicalSize() const = 0;
    [[nodiscard]] virtual core::Size drawableSize() const = 0;
    virtual PresentResult present(const render::PixelBuffer& buffer);
    virtual void setPresentDiagnosticsEnabled(bool /*enabled*/) {}
    [[nodiscard]] virtual PresentStats presentStats() const { return {}; }
    // 不透明原生句柄；默认空（无平台绑定的假实现）。
    [[nodiscard]] virtual NativeSurfaceHandle nativeSurface() const {
        return {};
    }
    // 窗口可见性（最小化暂停用，v0.2 plan §3.2）。
    [[nodiscard]] virtual bool isMinimized() const { return false; }
    [[nodiscard]] virtual bool isVisible() const { return true; }
    // VSync 设置；CPU 呈现路径映射到 SDL_RenderSetVSync，GL 路径由 GPU
    // 适配在交换间隔上实现。默认 no-op。
    virtual void setVSyncEnabled(bool /*enabled*/) {}
    // Enables/disables IME-less text input for this window. Default no-op so
    // stub implementations stay trivial; the SDL3 backend maps it to
    // SDL_StartTextInput/SDL_StopTextInput.
    virtual void setTextInputEnabled(bool /*enabled*/) {}
    // Hints the IME candidate-window anchor (logical coordinates) and the
    // caret offset relative to `area.origin.x`. Linux IBus/Fcitx/Wayland
    // needs this via SDL_SetTextInputArea; default no-op for stubs.
    virtual void setTextInputArea(const core::Rect& /*area*/,
                                  int /*cursor*/) {}
};

// Stage identifier for the platform module contract (Stage 2: SDL3 backend).
[[nodiscard]] const char* platformStageName();

}  // namespace lumen::platform
