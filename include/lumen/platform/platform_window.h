#pragma once

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
    Resize,
    FocusGained,
    FocusLost,
};

struct Event {
    EventType type{EventType::None};
    // Pointer position in logical coordinates (PointerDown/Up/Move).
    core::Offset position{};
    // lumen::core::Key value (KeyDown/KeyUp); UTF-8 text (TextInput).
    int keyCode{0};
    std::string text{};
    // New drawable size in physical pixels (Resize).
    core::Size pixelSize{};
};

class PlatformWindow {
  public:
    virtual ~PlatformWindow() = default;
    virtual Event pollEvent() = 0;
    [[nodiscard]] virtual core::Size logicalSize() const = 0;
    [[nodiscard]] virtual core::Size drawableSize() const = 0;
    virtual void present(const render::PixelBuffer& buffer) = 0;
    // Enables/disables IME-less text input for this window. Default no-op so
    // stub implementations stay trivial; the SDL3 backend maps it to
    // SDL_StartTextInput/SDL_StopTextInput.
    virtual void setTextInputEnabled(bool /*enabled*/) {}
};

// Stage identifier for the platform module contract (Stage 2: SDL3 backend).
[[nodiscard]] const char* platformStageName();

}  // namespace lumen::platform
