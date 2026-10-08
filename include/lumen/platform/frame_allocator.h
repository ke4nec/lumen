#pragma once

#include <memory>

#include "lumen/render/renderer.h"

namespace lumen::platform {

// Optional desktop telemetry. Returns nullptr unless a supported native
// profiler is installed and its malloc, C++ and SDL bindings pass validation.
// The returned source and its scopes are UI-thread owned. See
// docs/lumen-frame-allocator-design.md for coverage and measurement semantics.
[[nodiscard]] std::unique_ptr<render::FrameAllocationSource>
makeNativeFrameAllocationSource();

}  // namespace lumen::platform
