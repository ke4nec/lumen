#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"

namespace lumen::render {

using ImageId = std::uint64_t;

struct TextRun {
    std::string text{};
    core::Offset origin{};
};

struct PixelBuffer {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba{};
};

// Renderer contract shared by CpuRenderer (Stage 2), SkiaRenderer (Stage 5)
// and any future backend. UI/layout layers only program against this.
class Renderer {
  public:
    virtual ~Renderer() = default;
    virtual void beginFrame(core::Size viewport) = 0;
    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void clipRect(core::Rect rect) = 0;
    virtual void drawRect(core::Rect rect, core::Color color,
                          core::CornerRadius radius = {}) = 0;
    virtual void drawText(TextRun run, core::TextStyle style) = 0;
    virtual void drawImage(ImageId id, core::Rect destination) = 0;
    virtual void endFrame() = 0;
};

// Stage identifier for the renderer module contract (Stage 1 baseline).
[[nodiscard]] const char* rendererStageName();

}  // namespace lumen::render
