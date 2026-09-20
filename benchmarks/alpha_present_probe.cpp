// P4 diagnostic: equal pixels and fixed cadence isolate present from rendering.
// This supplements (never replaces) the frozen before/after benchmark protocol.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <SDL3/SDL.h>
#include "lumen/platform/sdl3_window.h"

int main(int argc, char** argv) {
    using namespace lumen;
    using Clock = std::chrono::steady_clock;
    try {
        int period = 40;
        std::string mode = "premultiplied";
        std::string host = "transparent";
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 == argc) {
                throw std::runtime_error("missing option value");
            }
            const std::string key = argv[i];
            if (key == "--mode") {
                mode = argv[i + 1];
            } else if (key == "--host") {
                host = argv[i + 1];
            } else if (key == "--period-ms") {
                const std::string value = argv[i + 1];
                std::size_t consumed = 0;
                period = std::stoi(value, &consumed);
                if (consumed != value.size()) {
                    throw std::runtime_error("invalid period");
                }
            } else {
                throw std::runtime_error("unknown option");
            }
        }
        if ((mode != "straight" && mode != "premultiplied" && mode != "opaque") ||
            (host != "transparent" && host != "opaque") ||
            (mode == "opaque" && host == "transparent") || period < 0 || period > 100) {
            throw std::runtime_error("invalid mode, host or period");
        }
        platform::Sdl3WindowDesc desc;
        desc.title = "Lumen alpha baseline";
        desc.width = 1920;
        desc.height = 1080;
        desc.customTitleBar = true;
        desc.transparent = host == "transparent";
        desc.softwarePresentation = true;
        auto window = platform::createSdl3Window(desc);
        if (!window || std::string(SDL_GetCurrentVideoDriver()) == "dummy") {
            throw std::runtime_error("real desktop software window required");
        }
        window->setPresentDiagnosticsEnabled(true);
        window->setVSyncEnabled(false);
        render::PixelBuffer pixels{1920,
                                   1080,
                                   {},
                                   mode == "straight" ? render::AlphaMode::Straight
                                   : mode == "opaque" ? render::AlphaMode::Opaque
                                                      : render::AlphaMode::Premultiplied};
        pixels.rgba.resize(1920 * 1080 * 4);
        for (std::size_t i = 0; i < pixels.rgba.size(); i += 4) {
            pixels.rgba[i] = desc.transparent && mode == "straight" ? 255 : 128;
            pixels.rgba[i + 3] = desc.transparent ? 128 : 255;
        }
        std::vector<double> prepare, submits, whole, intervals;
        prepare.reserve(300);
        submits.reserve(300);
        whole.reserve(300);
        intervals.reserve(300);
        auto previous = Clock::now();
        unsigned overruns = 0;
        for (int frame = -30; frame < 300; ++frame) {
            for (;;) {
                const auto event = window->pollEvent().type;
                if (event == platform::EventType::None) {
                    break;
                }
                if (event == platform::EventType::Quit) {
                    throw std::runtime_error("window closed");
                }
                if (event == platform::EventType::WindowMinimized) {
                    throw std::runtime_error("window minimized during sample");
                }
            }
            const auto drawable = window->drawableSize();
            if (drawable.width != 1920 || drawable.height != 1080 || window->isMinimized()) {
                throw std::runtime_error("drawable changed during sample");
            }
            if (period) {
                std::this_thread::sleep_until(previous + std::chrono::milliseconds(period));
            }
            const auto start = Clock::now();
            const double interval =
                std::chrono::duration<double, std::micro>(start - previous).count();
            if (interval > 5'000'000) {
                throw std::runtime_error("sampling interrupted");
            }
            previous = start;
            if (window->present(pixels) != platform::PresentResult::Ok) {
                throw std::runtime_error("present rejected");
            }
            const double total =
                std::chrono::duration<double, std::micro>(Clock::now() - start).count();
            const auto stats = window->presentStats();
            if (frame >= 0) {
                prepare.push_back(stats.prepareMs * 1000);
                submits.push_back(stats.submitMs * 1000);
                whole.push_back(total);
                intervals.push_back(interval);
                overruns += period && total > period * 1000.0;
            }
        }
        const auto stats = window->presentStats();
        std::printf(
            "{\"mode\":\"%s\",\"period_ms\":%d,\"warmup_frames\":30,\"measured_frames\":300,"
            "\"physical_pixels\":[1920,1080],\"presentation\":\"native-software\",\"transparent\":"
            "%s,"
            "\"scope\":\"present only; immutable equal-color input; no rendering; pacing "
            "excluded\","
            "\"alpha_conversions\":%llu,\"scratch_bytes\":%zu,\"overruns\":%u,\"phases\":{",
            mode.c_str(), period, desc.transparent ? "true" : "false",
            static_cast<unsigned long long>(stats.alphaConversions), stats.scratchCapacityBytes,
            overruns);
        bool first = true;
        const auto report = [&](const char* name, std::vector<double>& values) {
            std::sort(values.begin(), values.end());
            std::printf("%s\"%s\":{\"p50_us\":%.3f,\"p95_us\":%.3f}", first ? "" : ",", name,
                        values[149], values[284]);
            first = false;
        };
        report("prepare", prepare);
        report("host", submits);
        report("present", whole);
        report("frame_interval", intervals);
        std::printf("}}\n");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
