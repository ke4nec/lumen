#include "lumen/render/renderer.h"

#include <algorithm>
#include <limits>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "lumen/render/render_commands.h"
#include "pixel_math.h"

namespace lumen::render {

const char* alphaModeName(AlphaMode mode) {
    switch (mode) {
        case AlphaMode::Straight: return "straight";
        case AlphaMode::Premultiplied: return "premultiplied";
        case AlphaMode::Opaque: return "opaque";
    }
    return "invalid";
}

bool validatePixelBuffer(const PixelBuffer& buffer, PixelValidation validation) {
    if (buffer.width <= 0 || buffer.height <= 0 ||
        (buffer.alphaMode != AlphaMode::Straight &&
         buffer.alphaMode != AlphaMode::Premultiplied && buffer.alphaMode != AlphaMode::Opaque)) {
        return false;
    }
    const auto width = static_cast<std::size_t>(buffer.width);
    const auto height = static_cast<std::size_t>(buffer.height);
    if (width > std::numeric_limits<std::size_t>::max() / 4U / height ||
        buffer.rgba.size() != width * height * 4U) {
        return false;
    }
    if (validation == PixelValidation::Content && buffer.alphaMode != AlphaMode::Straight) {
        for (std::size_t i = 0; i < buffer.rgba.size(); i += 4) {
            const auto a = buffer.rgba[i + 3];
            if ((buffer.alphaMode == AlphaMode::Opaque && a != 255) ||
                buffer.rgba[i] > a || buffer.rgba[i + 1] > a || buffer.rgba[i + 2] > a) {
                return false;
            }
        }
    }
    return true;
}

namespace {
bool convertRgba(PixelBuffer& destination, const PixelBuffer& source, AlphaMode target,
                 PixelValidation validation) {
    // Validate before mutating, including for aliases and already-converted data.
    if (!validatePixelBuffer(source, validation)) {
        return false;
    }
    if (&destination != &source) {
        destination.rgba = source.rgba;
        destination.width = source.width;
        destination.height = source.height;
        destination.alphaMode = source.alphaMode;
    }
    if (destination.alphaMode == target || destination.alphaMode == AlphaMode::Opaque) {
        return true;
    }
    for (std::size_t i = 0; i < destination.rgba.size(); i += 4) {
        const unsigned a = destination.rgba[i + 3];
        for (std::size_t c = 0; c < 3; ++c) {
            auto& channel = destination.rgba[i + c];
            channel = static_cast<std::uint8_t>(target == AlphaMode::Premultiplied
                ? detail::mul255(channel, a)
                : a == 0 ? 0 : std::min(255U, (channel * 255U + a / 2U) / a));
        }
    }
    destination.alphaMode = target;
    return true;
}
}  // namespace

bool premultiplyRgbaInPlace(PixelBuffer& buffer, PixelValidation validation) {
    return convertRgba(buffer, buffer, AlphaMode::Premultiplied, validation);
}
bool premultiplyRgbaInto(PixelBuffer& destination, const PixelBuffer& source,
                        PixelValidation validation) {
    return convertRgba(destination, source, AlphaMode::Premultiplied, validation);
}
bool unpremultiplyRgbaInPlace(PixelBuffer& buffer, PixelValidation validation) {
    return convertRgba(buffer, buffer, AlphaMode::Straight, validation);
}
bool unpremultiplyRgbaInto(PixelBuffer& destination, const PixelBuffer& source,
                          PixelValidation validation) {
    return convertRgba(destination, source, AlphaMode::Straight, validation);
}

RendererCapabilities Renderer::capabilities() const {
    RendererCapabilities caps;
    caps.backendName = "adapter";
    return caps;
}

void Renderer::submit(const RenderCommandList& commands, const FrameInfo& info) {
    const auto start = std::chrono::steady_clock::now();
    // 默认适配器无法在 beginFrame 携带 deviceScale：经 setter 注入，使
    // 即时路径后端（Skia 光栅等）与原生 submit 后端（CPU/GPU）同尺度。
    if (info.deviceScale > 0.0F) {
        setDeviceScale(info.deviceScale);
    }
    // 局部提交的前提是命令与缓存能证明覆盖完整（plan §2.3）；默认适配
    // 无法保留上一帧，preserve 请求只能退回全帧并记录原因。
    const bool wantsPartial = info.damage.has_value() && info.preservePrevious;
    const bool canPartial = capabilities().partialSubmit;
    bool clipToDamage = false;
    bool fullFrameFallback = false;
    std::string fallbackReason{};
    if (wantsPartial && !canPartial) {
        fullFrameFallback = true;
        fallbackReason = "backend-has-no-partial-submit";
    } else if (info.damage.has_value()) {
        clipToDamage = true;
    }

    stats_.uploads = 0;
    beginFrame(info.viewport);
    if (clipToDamage) {
        save();
        clipRect(*info.damage);
    }
    for (const auto& command : commands.commands()) {
        switch (command.type) {
            case CommandType::Save:
                save();
                break;
            case CommandType::Restore:
                restore();
                break;
            case CommandType::ClipRect:
                clipRect(command.rect);
                break;
            case CommandType::ClipRounded:
                clipRounded(command.rect, command.radius);
                break;
            case CommandType::DrawRect:
                drawRect(command.rect, command.color, command.radius);
                break;
            case CommandType::DrawRectStroke:
                drawRectStroke(command.rect, command.color, command.radius,
                               command.strokeWidth);
                break;
            case CommandType::DrawText:
                drawText(command.textRun, command.textStyle);
                break;
            case CommandType::DrawImage:
                drawImage(command.image, command.rect);
                break;
            case CommandType::DrawIcon:
                drawIcon(command.polylines, command.rect, command.color,
                         command.strokeWidth);
                break;
            case CommandType::DrawShadow:
                drawShadow(command.rect, command.color,
                           core::Offset{command.transform.tx,
                                        command.transform.ty},
                           command.strokeWidth);
                break;
            case CommandType::UploadImage:
                onUploadImage(command.image, command.pixels);
                break;
            case CommandType::UnloadImage:
                onUnloadImage(command.image);
                break;
        }
    }
    if (clipToDamage) {
        restore();
    }
    endFrame();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
            .count();
    noteAdaptedSubmit(commands, elapsedMs, 0, fullFrameFallback,
                      std::move(fallbackReason));
}

RenderStats Renderer::stats() const { return stats_; }

void Renderer::noteCpuBuildMs(double milliseconds) {
    stats_.cpuBuildMs = milliseconds;
}

void Renderer::resetSurface(const RenderSurfaceDesc& /*desc*/) {}

void Renderer::noteAdaptedSubmit(const RenderCommandList& commands,
                                 double submitMs, std::uint64_t culledCommands,
                                 bool fullFrameFallback,
                                 std::string fallbackReason) {
    stats_.framesSubmitted += 1;
    stats_.submitMs = submitMs;
    stats_.commandCount = commands.size();
    stats_.culledCommands = culledCommands;
    stats_.fullFrameFallback = fullFrameFallback;
    stats_.fallbackReason = std::move(fallbackReason);
}

std::uint64_t frameHash(const PixelBuffer& buffer) {
    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    std::uint64_t hash = kFnvOffsetBasis;
    const auto mix = [&hash](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= static_cast<std::uint8_t>(value & 0xFFU);
            hash *= kFnvPrime;
            value >>= 8;
        }
    };
    mix(static_cast<std::uint64_t>(buffer.width));
    mix(static_cast<std::uint64_t>(buffer.height));
    for (const std::uint8_t byte : buffer.rgba) {
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

const char* rendererStageName() {
    // Honest capability label: stage 5 (Skia adapter) only exists when the
    // build actually compiles skia_renderer.cpp into this library.
#ifdef LUMEN_HAS_SKIA_BACKEND
    return "stage5";
#else
    return "stage2";
#endif
}

}  // namespace lumen::render
