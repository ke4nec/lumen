#pragma once

#include <memory>
#include <string>

#include "lumen/render/render_commands.h"
#include "lumen/render/renderer.h"

namespace lumen::render {

// v0.2 阶段7C (plan §3.1/7C): Skia Ganesh GPU 首期后端。
//
// 通过 SDL3 在应用窗口上创建 GL 上下文，命令经 Renderer::submit 回放到
// FBO 0 包装出的 SkSurface；endFrame flush 后交换。纹理、裁剪、透明度、
// 文字与 surface resize 全部支持；Graphite 留待后续版本。
//
// 创建/设备初始化失败、上下文丢失时工厂返回 nullptr 或 isAlive() 变为
// false，应用保持状态切回 CPU 后端（UI 树与状态不丢失，plan §2.1）。

struct SkiaGpuRendererDesc {
    // PlatformWindow::nativeSurface() 的不透明句柄；当前实现要求
    // windowSystem == "sdl3"（SDL_Window*，窗口需带 OpenGL 标志创建）。
    void* sdlWindow{nullptr};
    const char* windowSystem{"sdl3"};
    int widthPixels{0};
    int heightPixels{0};
    float deviceScale{1.0F};
    bool vsync{true};
    // 隐藏/离屏窗口（测试、无头 GPU 验证）不能依赖交换——部分驱动在
    // 隐藏窗口上 SwapWindow 会阻塞。false 时提交止步于 flush。
    bool allowSwap{true};
    core::Color clear{core::Color::fromRGBA(24, 24, 27)};
};

// 探测 + 创建。任何一步失败（GL 库/上下文、Ganesh 初始化、surface 包装）
// 都返回 nullptr 并把原因写进 diagnostics（可空）；绝不抛出、不留半初始化
// 状态（GL 库/上下文即时清理）。
[[nodiscard]] std::unique_ptr<Renderer> createSkiaGpuRenderer(
    const SkiaGpuRendererDesc& desc, std::string* diagnostics = nullptr);

// 无副作用地探测 GPU 路径是否可用（隐藏窗口上建 GL 上下文并初始化
// Ganesh，随即销毁）。供应用在创建正式窗口前决定窗口标志。
[[nodiscard]] bool probeSkiaGpuAvailable(std::string* diagnostics = nullptr);

// 运行中上下文健康查询（device lost 检测的轻量入口）。
[[nodiscard]] bool skiaGpuRendererAlive(const Renderer& renderer);

}  // namespace lumen::render
