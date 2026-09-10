// Counter sample (plan §7): interactive UI over the C++ declarative DSL or a
// `.lumen` document, rendered with the CPU backend (default), the optional
// Skia raster backend, or the Skia Ganesh GPU backend with automatic CPU
// fallback (v0.2 阶段7C). The loop is driven by FrameScheduler (阶段7D):
// idle windows stop submitting, input/animation/resize follow deadlines and
// debounce, minimized windows pause. `--headless` runs the same pipeline
// without a window and prints stable frame hashes (plan §9). `--watch`
// (implies `--dsl`) reloads the document when its mtime changes (hot reload,
// plan 阶段6). `--diagnostics` prints backend/device/DPI/frame stats and
// fallback events (阶段7E).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include <SDL3/SDL.h>

#include "counter_app.h"
#include "lumen/dsl/text_dsl.h"
#include "lumen/platform/sdl3_window.h"
#include "lumen/render/frame_scheduler.h"

#ifdef LUMEN_HAVE_SKIA
#include "lumen/render/skia_renderer.h"
#endif
#ifdef LUMEN_HAVE_GPU
#include "lumen/render/skia_gpu_renderer.h"
#endif

namespace {

using lumen::core::Key;
using lumen::core::Offset;
using lumen::core::Widget;
using lumen::examples::CounterApp;

struct Options {
    bool headless{false};
    bool watch{false};
    bool diagnostics{false};
    std::optional<std::string> dslPath{};
    std::string renderer{"cpu"};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--headless") {
            options.headless = true;
        } else if (flag == "--diagnostics") {
            options.diagnostics = true;
        } else if (flag == "--watch") {
            options.watch = true;
            options.dslPath = "counter.lumen";
        } else if (flag == "--dsl") {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.dslPath = argv[++i];
            } else {
                options.dslPath = "counter.lumen";
            }
        } else if (flag == "--renderer" && i + 1 < argc) {
            options.renderer = argv[++i];
        }
    }
    return options;
}

// Loads the root widget: `.lumen` file when requested, C++ builders
// otherwise (plan §7: both must build the same UI).
Widget loadRoot(const Options& options) {
    if (!options.dslPath.has_value()) {
        return CounterApp::buildUi();
    }
    const lumen::dsl::DslParseResult parsed =
        lumen::dsl::parseLumenFile(*options.dslPath);
    if (!parsed.ok()) {
        std::fprintf(stderr, "dsl error: %s\n",
                     parsed.error->format().c_str());
        std::exit(1);
    }
    return parsed.root;
}

int runHeadless(CounterApp& app) {
    app.setView(lumen::core::Size{800.0F, 600.0F});
    std::printf("frame0 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    // Simulate: click Increment, type into the name field, resize.
    const auto centerOf = [&app](const char* key) {
        const lumen::core::RenderNode* node =
            lumen::core::findNodeByKey(app.root(), key);
        return lumen::core::absoluteOffset(app.root(), key) +
               Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    };
    app.pointerDown(centerOf("increment-button"));
    app.pointerUp(centerOf("increment-button"));
    std::printf("frame1 %016llx counter=%d\n",
                static_cast<unsigned long long>(app.renderFrame()),
                app.counterValue());

    app.pointerDown(centerOf("name-field"));
    app.pointerUp(centerOf("name-field"));
    app.textInput("Lumen");
    std::printf("frame2 %016llx name=%s\n",
                static_cast<unsigned long long>(app.renderFrame()),
                app.state().get("name").c_str());

    app.setView(lumen::core::Size{1024.0F, 768.0F});
    std::printf("frame3 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));
    return 0;
}

// Hot reload support (plan 阶段6): polls the document's mtime and swaps the
// UI template on change; re-parses only when the bytes differ (DslCache).
class HotReloader {
  public:
    explicit HotReloader(std::string path) : path_(std::move(path)) {
        refreshStamp();
    }

    // Returns true when the app root was swapped this poll.
    bool poll(CounterApp& app) {
        if (!refreshStamp()) {
            return false;
        }
        const lumen::dsl::DslParseResult parsed =
            cache_.parse(readFile(), path_);
        if (!parsed.ok()) {
            std::fprintf(stderr, "dsl error (kept previous UI): %s\n",
                         parsed.error->format().c_str());
            return false;
        }
        app.swapRoot(parsed.root);
        return true;
    }

    [[nodiscard]] const lumen::dsl::DslCache& cache() const { return cache_; }

  private:
    [[nodiscard]] std::string readFile() const {
        std::ifstream file(path_, std::ios::binary);
        if (!file) {
            return {};
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    bool refreshStamp() {
        std::error_code error;
        const auto stamp = std::filesystem::last_write_time(path_, error);
        if (error) {
            return false;
        }
        if (stamp == stamp_) {
            return false;
        }
        stamp_ = stamp;
        return true;
    }

    std::string path_;
    std::filesystem::file_time_type stamp_{};
    lumen::dsl::DslCache cache_{};
};

// v0.2 运行时诊断（阶段7E, plan §7E）：启动输出后端/设备/DPI，运行中
// 周期输出帧统计与回退事件。
void printStartupDiagnostics(const lumen::render::RendererCapabilities& caps,
                             const lumen::platform::PlatformWindow& window) {
    const auto logical = window.logicalSize();
    const auto drawable = window.drawableSize();
    const float dpi = drawable.width / std::max(1.0F, logical.width);
    std::printf("[diag] backend=%s gpu=%s partial=%s dpi=%.2f pixels=%.0fx%.0f\n",
                caps.backendName, caps.gpu ? "yes" : "no",
                caps.partialSubmit ? "yes" : "no", dpi, drawable.width,
                drawable.height);
}

void printFrameDiagnostics(std::uint64_t frames, std::uint32_t partial,
                           const lumen::render::RenderStats& stats,
                           std::uint64_t skippedIdleTurns) {
    std::printf("[diag] frames=%llu partial=%u idleTurns=%llu cmds=%llu "
                "culled=%llu submitMs=%.2f gpuWaitMs=%.2f buildMs=%.2f "
                "uploads=%llu%s%s\n",
                static_cast<unsigned long long>(frames), partial,
                static_cast<unsigned long long>(skippedIdleTurns),
                static_cast<unsigned long long>(stats.commandCount),
                static_cast<unsigned long long>(stats.culledCommands),
                stats.submitMs, stats.gpuWaitMs, stats.cpuBuildMs,
                static_cast<unsigned long long>(stats.uploads),
                stats.fullFrameFallback ? " fallback=yes" : "",
                stats.fallbackReason.empty()
                    ? ""
                    : (" reason=" + stats.fallbackReason).c_str());
}

int runWindowed(CounterApp& app, const std::string& rendererName,
                const Options& options) {
    // --- 后端选择（阶段7C）：GPU 请求先探测，失败自动回退 CPU ---
    bool wantGpu = rendererName == "gpu";
    bool gpuActive = false;
    std::string gpuDiagnostics;
#ifdef LUMEN_HAVE_GPU
    bool probeGpu = false;
    if (wantGpu) {
        probeGpu = lumen::render::probeSkiaGpuAvailable(&gpuDiagnostics);
        if (!probeGpu) {
            std::printf("[diag] gpu probe failed (%s) — falling back to cpu\n",
                        gpuDiagnostics.c_str());
        }
    }
#else
    if (wantGpu) {
        std::printf("[diag] gpu backend not built in "
                    "(rebuild with -DLUMEN_ENABLE_GPU=ON) — using cpu\n");
    }
#endif

    lumen::platform::Sdl3WindowDesc desc;
    desc.title = "Lumen Counter - v0.2";
    desc.width = 800;
    desc.height = 600;
#ifdef LUMEN_HAVE_GPU
    desc.opengl = wantGpu && probeGpu;
#endif
    auto window = lumen::platform::createSdl3Window(desc);
    if (window == nullptr) {
        return 1;
    }

    std::unique_ptr<lumen::render::Renderer> gpuRenderer;
#ifdef LUMEN_HAVE_GPU
    if (wantGpu && probeGpu) {
        const auto surface = window->nativeSurface();
        lumen::render::SkiaGpuRendererDesc gpuDesc;
        gpuDesc.sdlWindow = surface.nativeWindow;
        gpuDesc.windowSystem = surface.windowSystem;
        gpuDesc.widthPixels = static_cast<int>(window->drawableSize().width);
        gpuDesc.heightPixels = static_cast<int>(window->drawableSize().height);
        gpuDesc.deviceScale = window->drawableSize().width /
                              std::max(1.0F, window->logicalSize().width);
        gpuRenderer = lumen::render::createSkiaGpuRenderer(gpuDesc,
                                                           &gpuDiagnostics);
        if (gpuRenderer != nullptr) {
            app.setRenderer(gpuRenderer.get());
            gpuActive = true;
        } else {
            // 探测通过但正式窗口初始化失败：换普通窗口 + CPU。
            std::printf("[diag] gpu init failed (%s) — falling back to cpu\n",
                        gpuDiagnostics.c_str());
            window.reset();
            desc.opengl = false;
            window = lumen::platform::createSdl3Window(desc);
            if (window == nullptr) {
                return 1;
            }
        }
    }
#endif

#ifdef LUMEN_HAVE_SKIA
    std::optional<lumen::render::SkiaRenderer> skia;
    if (!gpuActive && rendererName == "skia") {
        skia.emplace(1.0F);
        app.setRenderer(&*skia);
    }
#else
    if (!gpuActive && rendererName == "skia") {
        std::fprintf(stderr,
                     "skia backend not built in; rebuild with "
                     "-DLUMEN_ENABLE_SKIA=ON (using cpu)\n");
    }
#endif

    // Query the device scale up front; resize/DPI events refresh it later.
    const auto applyScale = [&window, &app
#ifdef LUMEN_HAVE_SKIA
                             ,
                             &skia
#endif
    ]() {
        const float scale = window->drawableSize().width /
                            std::max(1.0F, window->logicalSize().width);
        app.setDeviceScale(scale);
#ifdef LUMEN_HAVE_SKIA
        if (skia.has_value()) {
            skia->setDeviceScale(scale);
        }
#endif
    };
    applyScale();
    app.setView(window->logicalSize());

    std::optional<HotReloader> reloader;
    if (options.watch && options.dslPath.has_value()) {
        reloader.emplace(*options.dslPath);
        std::printf("watching %s for changes\n", options.dslPath->c_str());
    }

    // --- 调度器（阶段7D）：替换固定 SDL_Delay(16) ---
    lumen::render::RealtimeClock clock;
    lumen::render::FrameScheduler::Config schedulerConfig;
    schedulerConfig.targetFps = 60;
    schedulerConfig.resizeDebounceMs = 16;
    schedulerConfig.pauseWhenHidden = true;
    lumen::render::FrameScheduler scheduler{schedulerConfig, &clock};
    scheduler.requestFrame(lumen::render::FrameReason::Explicit);

    if (options.diagnostics) {
        printStartupDiagnostics(app.capabilities(), *window);
    }

    Uint64 lastWatchPoll = SDL_GetTicks();
    Uint64 lastDiagPrint = SDL_GetTicks();
    std::uint64_t idleTurns = 0;
    bool running = true;
    while (running) {
        bool eventsPumped = false;
        for (auto event = window->pollEvent();
             event.type != lumen::platform::EventType::None;
             event = window->pollEvent()) {
            eventsPumped = true;
            switch (event.type) {
                case lumen::platform::EventType::Quit:
                    running = false;
                    break;
                case lumen::platform::EventType::Resize:
                case lumen::platform::EventType::DpiChanged:
                    // Root constraints track the new logical size; DPI
                    // 变化在下一帧 surface 重建前处理（plan §3.2）。
                    app.setView(window->logicalSize());
                    applyScale();
                    scheduler.requestFrame(lumen::render::FrameReason::Resize);
                    break;
                case lumen::platform::EventType::WindowMinimized:
                    scheduler.setWindowVisible(false);
                    break;
                case lumen::platform::EventType::WindowRestored:
                    scheduler.setWindowVisible(true);
                    scheduler.requestFrame(lumen::render::FrameReason::Resize);
                    break;
                case lumen::platform::EventType::PointerDown:
                    app.pointerDown(event.position);
                    scheduler.requestFrame(lumen::render::FrameReason::Input);
                    break;
                case lumen::platform::EventType::PointerMove:
                    // Feeds tap/drag gesture discrimination (plan 阶段6).
                    app.pointerMove(event.position);
                    scheduler.requestFrame(lumen::render::FrameReason::Input);
                    break;
                case lumen::platform::EventType::PointerUp:
                    app.pointerUp(event.position);
                    scheduler.requestFrame(lumen::render::FrameReason::Input);
                    break;
                case lumen::platform::EventType::TextInput:
                    app.textInput(event.text);
                    scheduler.requestFrame(lumen::render::FrameReason::Input);
                    break;
                case lumen::platform::EventType::TextEditing:
                    app.textEditing(event.text);
                    scheduler.requestFrame(lumen::render::FrameReason::Input);
                    break;
                case lumen::platform::EventType::KeyDown:
                    app.keyDown(static_cast<Key>(event.keyCode));
                    scheduler.requestFrame(lumen::render::FrameReason::Input);
                    break;
                default:
                    break;
            }
        }
        // 空轮询（无事件）也照常走调度决策：空闲时不提交。
        if (!eventsPumped) {
            idleTurns += 1;
        }

        // 输入法/焦点驱动的 caret 闪烁是当前唯一的连续动画源。
        scheduler.setAnimationsActive(app.wantsTextInput());

        if (reloader.has_value() && SDL_GetTicks() - lastWatchPoll >= 250) {
            lastWatchPoll = SDL_GetTicks();
            if (reloader->poll(app)) {
                std::printf("ui reloaded\n");
                scheduler.requestFrame(
                    lumen::render::FrameReason::HotReload);
            }
        }

        // Time-driven state (caret blink) rides the frame loop.
        app.tick(SDL_GetTicks());
        window->setTextInputEnabled(app.wantsTextInput());

        if (scheduler.shouldSubmitFrame()) {
            app.renderFrame();
            if (app.wantsTextInput()) {
                // Keep the IME candidate window anchored to the focused
                // field. Required on Linux (IBus/Fcitx); harmless elsewhere.
                window->setTextInputArea(app.focusedTextRect(), 0);
            }
            if (!gpuActive) {
#ifdef LUMEN_HAVE_SKIA
                window->present(skia.has_value() ? skia->pixels()
                                                 : app.pixels());
#else
                window->present(app.pixels());
#endif
            }
            // GPU 路径的交换已在 submit/endFrame 完成。
            scheduler.markFrameSubmitted();

            if (options.diagnostics &&
                SDL_GetTicks() - lastDiagPrint >= 2000) {
                lastDiagPrint = SDL_GetTicks();
                printFrameDiagnostics(scheduler.submittedFrames(),
                                      app.partialRepaintCount(), app.stats(),
                                      idleTurns);
            }
        }

        // 空闲等待：有 deadline 就等到 deadline，否则最多等一个热重载
        // 轮询周期；任何输入事件立刻唤醒。
        const auto waitMs = scheduler.msUntilNextFrame();
        const std::uint32_t capped =
            std::min<std::uint32_t>(waitMs.value_or(250), 250);
        if (capped > 0) {
            SDL_WaitEventTimeout(nullptr, capped);
        }
    }
    if (options.diagnostics) {
        printFrameDiagnostics(scheduler.submittedFrames(),
                              app.partialRepaintCount(), app.stats(),
                              idleTurns);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    Widget root = loadRoot(options);
    CounterApp app(std::move(root));
    if (options.headless) {
        return runHeadless(app);
    }
    return runWindowed(app, options.renderer, options);
}
