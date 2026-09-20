// Fixed pre/post migration driver. See alpha rendering plan P0 and §6.
// Scene construction, hashing, readback diagnostics and exports are NOT timed.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include "lumen/platform/sdl3_window.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/render_commands.h"
#ifdef LUMEN_BENCH_HAS_SKIA
#include "lumen/render/skia_renderer.h"
#endif

namespace {
using namespace lumen;
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
struct Options {
    std::string backend{"cpu"}, scenario{"layers"}, present{"none"}, dump{};
    int height{1080}, frames{300}, warmup{30};
    bool opaque{false};
};
int number(const std::string& value) {
    int n{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), n);
    if (error != std::errc{} || end != value.data() + value.size() || n < 0) {
        throw std::runtime_error("invalid nonnegative integer");
    }
    return n;
}
Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--opaque") {
            o.opaque = true;
            continue;
        }
        if (++i == argc) {
            throw std::runtime_error("missing option value");
        }
        const std::string value = argv[i];
        if (key == "--backend") {
            o.backend = value;
        } else if (key == "--scenario") {
            o.scenario = value;
        } else if (key == "--present") {
            o.present = value;
        } else if (key == "--height") {
            o.height = number(value);
        } else if (key == "--frames") {
            o.frames = number(value);
        } else if (key == "--warmup") {
            o.warmup = number(value);
        } else if (key == "--dump-frame") {
            o.dump = value;
        } else {
            throw std::runtime_error("unknown option: " + key);
        }
    }
    if ((o.height != 1080 && o.height != 2160) || o.frames < 1 || o.frames > 100000 ||
        o.warmup > 100000 || (o.backend != "cpu" && o.backend != "skia") ||
        (o.present != "none" && o.present != "texture" && o.present != "software") ||
        (o.scenario != "layers" && o.scenario != "edges" && o.scenario != "images" &&
         o.scenario != "upload" && o.scenario != "probe")) {
        throw std::runtime_error("invalid option value");
    }
    return o;
}
render::PixelBuffer image() {
    render::PixelBuffer result{256, 256, {}};
    result.rgba.reserve(256 * 256 * 4);
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            result.rgba.insert(result.rgba.end(), {255, static_cast<std::uint8_t>(y), 32,
                                                   static_cast<std::uint8_t>(x)});
        }
    }
    return result;
}
render::RenderCommandList scene(const Options& o, render::ImageId id) {
    render::RenderCommandList commands;
    const float scale = o.height / 1080.0F;
    const auto rect = [scale](float x, float y, float w, float h) {
        return core::Rect::fromXYWH(x * scale, y * scale, w * scale, h * scale);
    };
    if (o.scenario == "probe") {
        commands.drawRect(rect(0, 0, 1920, 1080), core::Color::fromRGBA(255, 0, 0, 128));
    } else if (o.scenario == "layers") {
        for (int i = 0; i < 8; ++i) {
            commands.drawRect(rect(40.0F * i, 24.0F * i, 1600, 840),
                              core::Color::fromRGBA(static_cast<std::uint8_t>(30 + i * 27),
                                                    static_cast<std::uint8_t>(240 - i * 23), 160,
                                                    static_cast<std::uint8_t>(24 + i * 17)));
        }
    } else if (o.scenario == "images" || o.scenario == "upload") {
        commands.save();
        commands.clipRounded(rect(60, 60, 1700, 900), core::CornerRadius::all(33 * scale));
        for (int i = 0; i < 6; ++i) {
            commands.drawImage(id, rect(30.0F + 220 * i, 40.0F + 70 * i, 600, 600));
        }
        commands.restore();
    } else {
        commands.drawRect(rect(40, 40, 300, 200), core::Color::fromRGBA(255, 0, 0, 160),
                          core::CornerRadius::all(37 * scale));
        commands.drawRect(rect(380, 40, 300, 200), core::Color::fromRGBA(255, 255, 255, 220),
                          core::CornerRadius::all(37 * scale));
        commands.save();
        commands.clipRounded(rect(40, 300, 1500, 600), core::CornerRadius::all(43 * scale));
        commands.clipRounded(rect(50, 305, 1450, 580), core::CornerRadius::all(51 * scale));
        commands.drawRect(rect(0, 0, 1920, 1080), core::Color::fromRGBA(30, 180, 220, 96));
        commands.drawRectStroke(rect(90, 330, 600, 420), core::Color::fromRGBA(250, 240, 230, 180),
                                core::CornerRadius::all(29 * scale), 2 * scale);
        commands.drawShadow(rect(800, 380, 400, 230), core::Color::fromRGBA(10, 20, 40, 100),
                            {4 * scale, 8 * scale}, 16 * scale);
        core::TextStyle text;
        text.fontSize = 28 * scale;
        text.color = core::Color::fromRGBA(250, 240, 230, 200);
        commands.drawText({"Alpha edges 012345", {100 * scale, 450 * scale}}, text);
        commands.drawIcon({{{0, 0}, {1, 1}}, {{1, 0}, {0, 1}}}, rect(900, 420, 60, 60),
                          core::Color::fromRGBA(255, 255, 255, 160), 2 * scale);
        commands.restore();
    }
    return commands;
}
void phase(const char* name, std::vector<double> values, bool last = false) {
    std::sort(values.begin(), values.end());
    std::printf("    \"%s\": {\"p50_us\": %.3f, \"p95_us\": %.3f}%s\n", name,
                values[(values.size() - 1) * 50 / 100], values[(values.size() - 1) * 95 / 100],
                last ? "" : ",");
}
template <class Backend> int run(const Options& o) {
    Backend renderer(1.0F, core::Color::fromRGBA(0, 0, 0, o.opaque ? 255 : 0));
    const auto source = image();
    const auto coldStart = Clock::now();
    auto id = renderer.registerImage(source);
    const double coldUpload = elapsed(coldStart);
    auto commands = scene(o, id);
    const int width = o.height * 16 / 9;
    render::FrameInfo frame;
    frame.viewport = {static_cast<float>(width), static_cast<float>(o.height)};
    std::unique_ptr<platform::PlatformWindow> window;
    if (o.present != "none") {
        platform::Sdl3WindowDesc desc;
        desc.title = "Lumen alpha baseline";
        desc.width = width;
        desc.height = o.height;
        desc.highPixelDensity = false;
        desc.customTitleBar = true;
        desc.transparent = !o.opaque;
        desc.softwarePresentation = o.present == "software";
        window = platform::createSdl3Window(desc);
        if (!window) {
            throw std::runtime_error("window creation failed");
        }
        window->setVSyncEnabled(false);
        window->setPresentDiagnosticsEnabled(true);
    }
    std::vector<double> submits, prepares, hosts, totals, uploads;
    for (auto* list : {&submits, &prepares, &hosts, &totals, &uploads}) {
        list->reserve(o.frames);
    }
    platform::PresentStats stats;
    std::size_t peakScratch = 0;
    for (int i = -o.warmup; i < o.frames; ++i) {
        if (window) {
            while (window->pollEvent().type != platform::EventType::None) {
            }
        }
        double upload = 0;
        if (o.scenario == "upload") {
            const auto start = Clock::now();
            renderer.unregisterImage(id);
            id = renderer.registerImage(source);
            upload = elapsed(start);
            commands = scene(o, id);
        }
        const auto totalStart = Clock::now();
        const auto start = Clock::now();
        renderer.submit(commands, frame);
        const auto submit = elapsed(start);
        if (window) {
            if (window->present(renderer.pixels()) != platform::PresentResult::Ok) {
                throw std::runtime_error("present failed");
            }
            stats = window->presentStats();
            peakScratch = std::max(peakScratch, stats.scratchCapacityBytes);
        }
        const double total = elapsed(totalStart);
        if (i >= 0) {
            submits.push_back(submit);
            prepares.push_back(stats.prepareMs * 1000);
            hosts.push_back(stats.submitMs * 1000);
            totals.push_back(total);
            uploads.push_back(upload);
        }
    }
    const auto& pixels = renderer.pixels();
    const auto drawable = window ? window->drawableSize() : core::Size{};
    const char* mode = render::alphaModeName(pixels.alphaMode);
    if (!o.dump.empty()) {
        std::ofstream out(o.dump, std::ios::binary);
        out.write(reinterpret_cast<const char*>(pixels.rgba.data()), pixels.rgba.size());
        if (!out) {
            throw std::runtime_error("frame export failed");
        }
        std::ofstream metadata(o.dump + ".txt");
        metadata << "width=" << width << "\nheight=" << o.height << "\nalpha_mode=" << mode << "\n";
        if (!metadata) {
            throw std::runtime_error("metadata export failed");
        }
        render::RenderCommandList recording;
        recording.uploadImage(id, source);
        for (const auto& command : commands.commands()) {
            recording.append(command);
        }
        const auto blob = render::serializeCommands(recording);
        std::ofstream record(o.dump + ".commands", std::ios::binary);
        record.write(blob.data(), blob.size());
        if (!record) {
            throw std::runtime_error("command export failed");
        }
    }
    std::printf(
        "{\n  \"benchmark\": \"lumen-alpha-bench\",\n  \"backend\": \"%s\",\n"
        "  \"scenario\": \"%s\",\n  \"presentation\": \"%s\",\n"
        "  \"build_type\": \"%s\",\n  \"alpha_mode\": \"%s\",\n"
        "  \"physical_pixels\": [%d,%d],\n  \"clear_alpha\": %d,\n"
        "  \"device_scale\": 1,\n  \"vsync_requested\": false,\n"
        "  \"window_drawable_pixels\": [%.0f,%.0f],\n"
        "  \"warmup_frames\": %d,\n  \"measured_frames\": %d,\n"
        "  \"scope\": \"total includes submit and present; excludes upload, commands, events and "
        "diagnostics readback\",\n"
        "  \"cold_upload_us\": %.3f,\n  \"image_bytes\": %zu,\n"
        "  \"alpha_conversions_per_present\": %llu,\n  \"converted_bytes_per_present\": %llu,\n"
        "  \"copied_bytes_per_present\": %llu,\n  \"scratch_capacity_bytes\": %zu,\n"
        "  \"peak_scratch_capacity_bytes\": %zu,\n  \"frame_hash\": \"%016llx\",\n",
        o.backend.c_str(), o.scenario.c_str(), o.present.c_str(), LUMEN_BENCH_BUILD_TYPE, mode,
        width, o.height, o.opaque ? 255 : 0, drawable.width, drawable.height, o.warmup, o.frames,
        coldUpload, source.rgba.size(), static_cast<unsigned long long>(stats.alphaConversions),
        static_cast<unsigned long long>(stats.convertedBytes),
        static_cast<unsigned long long>(stats.copiedBytes), stats.scratchCapacityBytes, peakScratch,
        static_cast<unsigned long long>(render::frameHash(pixels)));
    // Software surface readback is outside measurement. The host may discard alpha;
    // RGB still detects premultiplication twice (red 128 -> 64 in the P0 Skia probe).
    if (window && o.present == "software") {
        auto* native = static_cast<SDL_Window*>(window->nativeSurface().nativeWindow);
        auto* surface = SDL_GetWindowSurface(native);
        Uint8 r{}, g{}, b{}, a{};
        if (!surface || !SDL_ReadSurfacePixel(surface, 0, 0, &r, &g, &b, &a)) {
            throw std::runtime_error("software surface readback failed");
        }
        std::printf("  \"surface_pixel_0\": [%u,%u,%u,%u],\n", r, g, b, a);
        std::printf("  \"surface_format\": \"%s\",\n", SDL_GetPixelFormatName(surface->format));
    }
    if (window) {
        auto* native = static_cast<SDL_Window*>(window->nativeSurface().nativeWindow);
        auto* hostRenderer = SDL_GetRenderer(native);
        std::printf("  \"sdl_video_driver\": \"%s\",\n  \"sdl_renderer\": \"%s\",\n",
                    SDL_GetCurrentVideoDriver(), hostRenderer ? SDL_GetRendererName(hostRenderer) : "surface");
    }
    std::printf("  \"source_pixel_0\": [%u,%u,%u,%u],\n  \"phases\": {\n", pixels.rgba[0],
                pixels.rgba[1], pixels.rgba[2], pixels.rgba[3]);
    phase("submit", submits);
    phase("present_prepare", prepares);
    phase("host_submit", hosts);
    phase("upload", uploads);
    phase("total", totals, true);
    std::printf("  }\n}\n");
    return 0;
}
} // namespace
int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        if (options.backend == "cpu") {
            return run<lumen::render::CpuRenderer>(options);
        }
#ifdef LUMEN_BENCH_HAS_SKIA
        return run<lumen::render::SkiaRenderer>(options);
#else
        throw std::runtime_error("Skia is not enabled");
#endif
    } catch (const std::exception& error) {
        std::fprintf(stderr, "alpha-bench: %s\n", error.what());
        return 1;
    }
}
