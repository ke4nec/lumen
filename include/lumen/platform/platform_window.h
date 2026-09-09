#pragma once

#include <string>

#include "lumen/core/geometry.h"
#include "lumen/render/renderer.h"

namespace lumen::platform {

// Stage 1 placeholder: full SDL3 event translation, text input and
// framebuffer presentation land in Stage 2. The interface shape is frozen
// now so later stages do not churn UI/layout code.
//
// Forward-compat note: `pollEvent()` returns `Event{None}` when the queue is
// empty. Stage 2 fills `position`/`keyCode`/`text`/`pixelSize` and switches
// the SDL backend to drain the native queue; the signature stays unchanged.
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
};

struct Event {
    EventType type{EventType::None};
    // Pointer position in logical coordinates (PointerDown/Up/Move).
    core::Offset position{};
    // Platform key code (KeyDown/KeyUp); UTF-8 text (TextInput).
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
};

// Stage identifier for the platform module contract (Stage 1 baseline).
[[nodiscard]] const char* platformStageName();

}  // namespace lumen::platform
