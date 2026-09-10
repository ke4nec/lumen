// Counter sample (plan §7): interactive UI over the C++ declarative DSL or a
// `.lumen` document, rendered with the CPU backend (default) or the optional
// Skia backend. `--headless` runs the same pipeline without a window and
// prints stable frame hashes (plan §9). `--watch` (implies `--dsl`) reloads
// the document when its mtime changes (hot reload, plan 阶段6).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <SDL3/SDL.h>

#include "counter_app.h"
#include "lumen/dsl/text_dsl.h"
#include "lumen/platform/sdl3_window.h"

#ifdef LUMEN_HAVE_SKIA
#include "lumen/render/skia_renderer.h"
#endif

namespace {

using lumen::core::Key;
using lumen::core::Offset;
using lumen::core::Widget;
using lumen::examples::CounterApp;

struct Options {
    bool headless{false};
    bool watch{false};
    std::optional<std::string> dslPath{};
    std::string renderer{"cpu"};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--headless") {
            options.headless = true;
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

int runWindowed(CounterApp& app, const std::string& rendererName,
                const Options& options) {
    lumen::platform::Sdl3WindowDesc desc;
    desc.title = "Lumen Counter - Stage 6";
    desc.width = 800;
    desc.height = 600;
    auto window = lumen::platform::createSdl3Window(desc);
    if (window == nullptr) {
        return 1;
    }

#ifdef LUMEN_HAVE_SKIA
    std::optional<lumen::render::SkiaRenderer> skia;
    if (rendererName == "skia") {
        skia.emplace(1.0F);
        app.setRenderer(&*skia);
    }
#else
    if (rendererName == "skia") {
        std::fprintf(stderr,
                     "skia backend not built in; rebuild with "
                     "-DLUMEN_ENABLE_SKIA=ON (using cpu)\n");
    }
#endif

    // Query the device scale up front; resize events refresh it later.
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
    Uint64 lastWatchPoll = SDL_GetTicks();
    bool running = true;
    while (running) {
        for (auto event = window->pollEvent();
             event.type != lumen::platform::EventType::None;
             event = window->pollEvent()) {
            switch (event.type) {
                case lumen::platform::EventType::Quit:
                    running = false;
                    break;
                case lumen::platform::EventType::Resize:
                    // Root constraints track the new logical size (plan §5.3).
                    app.setView(window->logicalSize());
                    applyScale();
                    break;
                case lumen::platform::EventType::PointerDown:
                    app.pointerDown(event.position);
                    break;
                case lumen::platform::EventType::PointerMove:
                    // Feeds tap/drag gesture discrimination (plan 阶段6).
                    app.pointerMove(event.position);
                    break;
                case lumen::platform::EventType::PointerUp:
                    app.pointerUp(event.position);
                    break;
                case lumen::platform::EventType::TextInput:
                    app.textInput(event.text);
                    break;
                case lumen::platform::EventType::TextEditing:
                    app.textEditing(event.text);
                    break;
                case lumen::platform::EventType::KeyDown:
                    app.keyDown(static_cast<Key>(event.keyCode));
                    break;
                default:
                    break;
            }
        }
        if (reloader.has_value() &&
            SDL_GetTicks() - lastWatchPoll >= 250) {
            lastWatchPoll = SDL_GetTicks();
            if (reloader->poll(app)) {
                std::printf("ui reloaded\n");
            }
        }
        // Time-driven state (caret blink) rides the frame loop.
        app.tick(SDL_GetTicks());
        window->setTextInputEnabled(app.wantsTextInput());
        app.renderFrame();
        if (app.wantsTextInput()) {
            // Keep the IME candidate window anchored to the focused field
            // (queried after renderFrame so the rect tracks the fresh
            // layout). Required on Linux (IBus/Fcitx under X11/Wayland);
            // harmless elsewhere.
            window->setTextInputArea(app.focusedTextRect(),
                                     0);
        }
#ifdef LUMEN_HAVE_SKIA
        window->present(skia.has_value() ? skia->pixels() : app.pixels());
#else
        window->present(app.pixels());
#endif
        SDL_Delay(16);
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
