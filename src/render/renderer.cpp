#include "lumen/render/renderer.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "lumen/render/render_commands.h"

namespace lumen::render {

RendererCapabilities Renderer::capabilities() const {
    RendererCapabilities caps;
    caps.backendName = "adapter";
    return caps;
}

void Renderer::submit(const RenderCommandList& commands, const FrameInfo& info) {
    const auto start = std::chrono::steady_clock::now();
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
            case CommandType::DrawRect:
                drawRect(command.rect, command.color, command.radius);
                break;
            case CommandType::DrawText:
                drawText(command.textRun, command.textStyle);
                break;
            case CommandType::DrawImage:
                drawImage(command.image, command.rect);
                break;
            case CommandType::UploadImage:
            case CommandType::UnloadImage:
                // 资源生命周期命令只有原生 submit 实现（CpuRenderer 等）
                // 消费；纯即时路径后端通过 registerImage 管理资源。
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
