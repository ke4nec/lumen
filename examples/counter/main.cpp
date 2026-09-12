// Counter sample (plan §7): interactive UI over the C++ declarative DSL or a
// `.lumen` document, rendered with the CPU backend (default), the optional
// Skia raster backend, or the Skia Ganesh GPU backend with automatic CPU
// fallback (v0.2 阶段7C)。
//
// M2（自用路线图）：窗口主循环收敛到 app::runApp ——本文件只保留应用
// 装配：后端选择/GPU 探测与回退策略、Skia 字体注入、热重载轮询与
// headless 冒烟脚本。`--headless` 输出稳定 frame hash（plan §9）。

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "counter_app.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/windowing.h"
#include "lumen/dsl/text_dsl.h"
#include "lumen/platform/sdl3_host.h"
#include "lumen/text/skia_font_manager.h"

#ifdef LUMEN_HAVE_SKIA
#include "lumen/render/skia_renderer.h"
#endif
#ifdef LUMEN_HAVE_GPU
#include "lumen/render/skia_gpu_renderer.h"
#endif

namespace {

using lumen::core::Offset;
using lumen::core::Widget;
using lumen::examples::CounterApp;

struct Options {
    bool headless{false};
    bool watch{false};
    bool diagnostics{false};
    // Windowed smoke/measurement: present this many frames, then exit.
    std::uint64_t maxFrames{0};
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
        } else if (flag == "--frames") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "--frames requires a positive integer\n");
                std::exit(2);
            }
            const std::string value = argv[++i];
            const auto result = std::from_chars(
                value.data(), value.data() + value.size(), options.maxFrames);
            if (result.ec != std::errc{} ||
                result.ptr != value.data() + value.size() ||
                options.maxFrames == 0) {
                std::fprintf(stderr, "--frames requires a positive integer\n");
                std::exit(2);
            }
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
    bool poll(lumen::app::AppShell& shell) {
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
        shell.swapRoot(parsed.root);
        return true;
    }

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

// M2：窗口主循环 = app::runApp + 应用装配（后端选择/GPU 回退/字体/
// 热重载）。所有平台交互经 ApplicationHost，本文件无窗口 API。
int runWindowed(CounterApp& app, const Options& options) {
    lumen::platform::Sdl3ApplicationHost host;

    lumen::app::RunOptions runOptions;
    runOptions.windowDesc.title = "Lumen Counter - v0.2";
    runOptions.windowDesc.width = 800;
    runOptions.windowDesc.height = 600;
    runOptions.maxFrames = options.maxFrames;
    runOptions.diagnostics = options.diagnostics;

    // Skia 度量激活标志（GPU 探测/初始化成功或 skia 后端）。
#if defined(LUMEN_HAVE_SKIA) || defined(LUMEN_HAVE_GPU)
    bool skiaMetricsActive = false;
#endif

#ifdef LUMEN_HAVE_GPU
    const bool wantGpu = options.renderer == "gpu";
    bool probeGpu = false;
    std::string gpuDiagnostics;
    if (wantGpu) {
        probeGpu = lumen::render::probeSkiaGpuAvailable(&gpuDiagnostics);
        if (!probeGpu) {
            std::printf("[diag] gpu probe failed (%s) — falling back to cpu\n",
                        gpuDiagnostics.c_str());
        }
    }
    runOptions.windowDesc.opengl = wantGpu && probeGpu;
    std::unique_ptr<lumen::render::Renderer> gpuRenderer;
    if (wantGpu && probeGpu) {
        runOptions.rendererFactory =
            [&gpuRenderer, &gpuDiagnostics,
             &skiaMetricsActive](lumen::platform::ApplicationHost& host,
                                 lumen::core::WindowId& id)
            -> lumen::app::RendererSetup {
            lumen::platform::PlatformWindow* window = host.platformWindow(id);
            if (window == nullptr) {
                return {};
            }
            const auto surface = window->nativeSurface();
            lumen::render::SkiaGpuRendererDesc gpuDesc;
            gpuDesc.sdlWindow = surface.nativeWindow;
            gpuDesc.windowSystem = surface.windowSystem;
            gpuDesc.widthPixels =
                static_cast<int>(window->drawableSize().width);
            gpuDesc.heightPixels =
                static_cast<int>(window->drawableSize().height);
            gpuDesc.deviceScale = window->drawableSize().width /
                                  std::max(1.0F, window->logicalSize().width);
            gpuRenderer = lumen::render::createSkiaGpuRenderer(gpuDesc,
                                                               &gpuDiagnostics);
            if (gpuRenderer == nullptr) {
                // 探测通过但正式窗口初始化失败：销毁 OpenGL 窗口，重建
                // 软件呈现窗口 + CPU（GPU 资源先于窗口销毁）。
                std::printf("[diag] gpu init failed (%s) — falling back to cpu\n",
                            gpuDiagnostics.c_str());
                const lumen::core::Size logical = window->logicalSize();
                gpuRenderer.reset();
                host.destroyWindow(id);
                lumen::platform::WindowDesc desc;
                desc.title = "Lumen Counter - v0.2";
                desc.softwarePresentation = true;
                desc.width =
                    std::max(1, static_cast<int>(logical.width));
                desc.height =
                    std::max(1, static_cast<int>(logical.height));
                const auto fallbackWindow = host.createWindow(desc);
                if (!fallbackWindow.has_value()) {
                    id = lumen::core::WindowId{};
                    return {};
                }
                id = *fallbackWindow;
                return {};
            }
            skiaMetricsActive = true;
            lumen::app::RendererSetup setup;
            setup.renderer = gpuRenderer.get();
            // GPU 交换在 submit/endFrame 内完成，无 CPU 呈现回调。
            setup.failed = [&gpuRenderer] {
                return !lumen::render::skiaGpuRendererAlive(*gpuRenderer);
            };
            return setup;
        };
        // GPU 运行时失败（上下文丢失/GL 交换失败）：销毁 GPU 资源与
        // OpenGL 窗口，重建软件窗口回退 CPU（状态/焦点/选区保留）。
        runOptions.onRendererFailure =
            [&app, &gpuRenderer,
             &runOptions](lumen::platform::ApplicationHost& host,
                          lumen::core::WindowId& id)
            -> std::optional<lumen::app::RendererSetup> {
            std::printf("[diag] gpu failed (%s) — falling back to cpu\n",
                        gpuRenderer->stats().fallbackReason.c_str());
            const auto metrics = host.windowMetrics(id);
            const lumen::core::Size logical =
                metrics.has_value() ? metrics->logicalSize
                                    : lumen::core::Size{800.0F, 600.0F};
            app.shell().cancelComposition();
            app.shell().setRenderer(nullptr);
            // GPU 资源必须先于窗口/视频子系统销毁。
            gpuRenderer.reset();
            host.destroyWindow(id);
            lumen::platform::WindowDesc desc = runOptions.windowDesc;
            desc.opengl = false;
            // 原生软件呈现：回退路径不得依赖 SDL 渲染器重建。
            desc.softwarePresentation = true;
            desc.width = std::max(1, static_cast<int>(logical.width));
            desc.height = std::max(1, static_cast<int>(logical.height));
            const auto replacement = host.createWindow(desc);
            if (!replacement.has_value()) {
                return std::nullopt;
            }
            id = *replacement;
            // 有值的空 setup = 回退到应用壳内部 CPU 渲染器（注意不是
            // 空 optional——那会表示回退失败）。
            return lumen::app::RendererSetup{};
        };
    }
#else
    if (options.renderer == "gpu") {
        std::printf("[diag] gpu backend not built in "
                    "(rebuild with -DLUMEN_ENABLE_GPU=ON) — using cpu\n");
    }
#endif

#ifdef LUMEN_HAVE_SKIA
    std::optional<lumen::render::SkiaRenderer> skia;
    if (options.renderer == "skia") {
        runOptions.rendererFactory =
            [&skia, &skiaMetricsActive](lumen::platform::ApplicationHost& host,
                                        lumen::core::WindowId id)
            -> lumen::app::RendererSetup {
            skia.emplace(1.0F);
            skiaMetricsActive = true;
            lumen::app::RendererSetup setup;
            setup.renderer = &*skia;
            setup.present = [&host, id, &skia]() -> bool {
                lumen::platform::PlatformWindow* window =
                    host.platformWindow(id);
                return window != nullptr &&
                       window->present(skia->pixels()) ==
                           lumen::platform::PresentResult::Ok;
            };
            setup.syncDeviceScale = [&skia](float scale) {
                skia->setDeviceScale(scale);
            };
            return setup;
        };
    }
#else
    if (options.renderer == "skia") {
        std::fprintf(stderr,
                     "skia backend not built in; rebuild with "
                     "-DLUMEN_ENABLE_SKIA=ON (using cpu)\n");
    }
#endif

    // M1：Skia 度量激活时注入正式字体；工厂失败保持占位并诊断。
#if defined(LUMEN_HAVE_SKIA) || defined(LUMEN_HAVE_GPU)
    runOptions.fontFactory = [&skiaMetricsActive]()
        -> std::shared_ptr<lumen::text::FontManager> {
        if (!skiaMetricsActive) {
            return {};
        }
        std::string fontDiagnostics;
        auto fonts =
            lumen::text::createSkiaFontManager(&fontDiagnostics);
        if (fonts != nullptr) {
            std::printf("[diag] fonts: %s\n", fontDiagnostics.c_str());
            return std::shared_ptr<lumen::text::FontManager>(
                std::move(fonts));
        }
        std::printf("[diag] fonts: %s — keeping placeholder metrics\n",
                    fontDiagnostics.c_str());
        return {};
    };
#endif

    // 热重载：文档 mtime 变化 → swapRoot（plan 阶段6）。
    std::optional<HotReloader> reloader;
    if (options.watch && options.dslPath.has_value()) {
        reloader.emplace(*options.dslPath);
        std::printf("watching %s for changes\n",
                    options.dslPath->c_str());
        runOptions.poll = [&reloader](lumen::app::AppShell& shell,
                                      std::uint64_t /*nowMs*/) {
            if (reloader->poll(shell)) {
                std::printf("ui reloaded\n");
                return true;
            }
            return false;
        };
    }

    return lumen::app::runApp(app.shell(), host, runOptions);
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    Widget root = loadRoot(options);
    CounterApp app(std::move(root));
    if (options.headless) {
        return runHeadless(app);
    }
    return runWindowed(app, options);
}
