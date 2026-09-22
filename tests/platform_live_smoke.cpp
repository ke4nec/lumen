// Real desktop probe: AppShell/runApp, real input, two windows and recovery.
#include <SDL3/SDL.h>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "lumen/app/app_shell.h"
#include "lumen/core/virtual_list.h"
#include "lumen/platform/sdl3_host.h"
#include "lumen/text/system_font_manager.h"

using namespace lumen;

namespace {
struct Options {
    int seconds{10};
    int stressMiB{0};
    bool resize{false}, transparent{false}, recovery{false};
    std::string expectedText, clipboardExpected;
};

int number(const std::string& value, int maximum) {
    int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        result < 1 || result > maximum) throw std::runtime_error("invalid numeric option");
    return result;
}

Options parse(int argc, char** argv) {
    Options out;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--resize-burst") out.resize = true;
        else if (flag == "--transparent") out.transparent = true;
        else if (flag == "--inject-recovery") out.recovery = true;
        else if (i + 1 < argc && flag == "--seconds") out.seconds = number(argv[++i], 86400);
        else if (i + 1 < argc && flag == "--stress-mib") out.stressMiB = number(argv[++i], 256);
        else if (i + 1 < argc && flag == "--expected-text") out.expectedText = argv[++i];
        else if (i + 1 < argc && flag == "--clipboard-expect") out.clipboardExpected = argv[++i];
        else throw std::runtime_error("unknown or incomplete option: " + flag);
    }
    return out;
}

struct Events {
    std::atomic<unsigned> preedit{0}, commits{0}, compositionEnds{0}, resize{0}, wheel{0};
    static bool SDLCALL watch(void* data, SDL_Event* event) {
        auto& counts = *static_cast<Events*>(data);
        if (event->type == SDL_EVENT_TEXT_EDITING) {
            if (event->edit.text && event->edit.text[0]) ++counts.preedit;
            else ++counts.compositionEnds;
        }
        if (event->type == SDL_EVENT_TEXT_INPUT) ++counts.commits;
        if (event->type == SDL_EVENT_WINDOW_RESIZED) ++counts.resize;
        if (event->type == SDL_EVENT_MOUSE_WHEEL) ++counts.wheel;
        return true;
    }
};

struct ProbeWindow {
    core::WindowId id;
    core::VirtualListController list;
    app::AppShell shell;
    unsigned presents{0}, recoveries{0};
    bool seeded{false}, statePreserved{true};
    std::string initialText;
    std::optional<text::TextEditingValue> recoveryValue;
    std::string recoveryFocus;
    float recoveryOffset{0};
    unsigned recoveryFrame{0};

    explicit ProbeWindow(std::string text) : shell(config()), initialText(std::move(text)) {
        list.setItemCount(1000);
        list.setEstimatedExtent(32);
        list.setItemBuilder([](std::size_t i) { return core::makeText("Row " + std::to_string(i)); });
        shell.state().set("document", initialText);
    }
    app::ShellConfig config() {
        app::ShellConfig config;
        config.build = [this] {
            const auto& theme = shell.theme();
            auto field = core::makeTextField({}, "Type with your IME", {}, {}, 0, "editor", 360, {}, "document");
            auto content = core::makeColumn({core::makeText("Edit, switch windows, paste and scroll"),
                std::move(field), core::makeVirtualList(&list, "rows", 400, 180)});
            return core::makeContainer(std::move(content), {}, {}, core::EdgeInsets::all(16), {},
                                       theme.colors.surface, core::CornerRadius::all(theme.metrics.cardRadius));
        };
        config.onRebuilt = [this](app::AppShell& app) {
            if (seeded) return;
            seeded = true;
            if (const auto* editor = core::findNodeByKey(app.root(), "editor")) {
                app.controller().focusNode(*editor);
                app.controller().setEditingValue(text::TextEditingValue(initialText, {1, 4}));
            }
            list.scrollController()->scrollTo(150);
            app.markDirty();
        };
        return config;
    }
};
}

int main(int argc, char** argv) {
    try {
        const auto options = parse(argc, argv);
        platform::Sdl3ApplicationHost host;
        if (!host.initialize()) throw std::runtime_error("host initialize failed");
        const std::string driver = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "unknown";
        if (driver == "dummy" || driver == "offscreen") throw std::runtime_error("a desktop video driver is required");
        auto* clipboard = host.clipboard();
        if (!clipboard) throw std::runtime_error("clipboard unavailable");
        const bool externalClipboard = !options.clipboardExpected.empty();
        if (externalClipboard && clipboard->text() != options.clipboardExpected)
            throw std::runtime_error("cross-application clipboard content mismatch");
        if (!externalClipboard && (!clipboard->setText("lumen-live-clipboard") || clipboard->text() != "lumen-live-clipboard"))
            throw std::runtime_error("same-process clipboard round-trip failed");
        ProbeWindow first("primary document"), second("secondary document");
        Events events;
        if (!SDL_AddEventWatch(Events::watch, &events)) throw std::runtime_error(SDL_GetError());
        struct WatchGuard {
            Events* events;
            ~WatchGuard() { SDL_RemoveEventWatch(Events::watch, events); }
        } watch{&events};
        std::vector<std::uint8_t> pressure;
        unsigned cycles = 0;
        const auto started = std::chrono::steady_clock::now();
        auto nextResize = started, nextPressure = started;
        bool timedOut = false, fontAvailable = true;
        const auto makeOptions = [&](ProbeWindow& probe, const char* title) {
            app::RunOptions run;
            run.windowDesc.title = title;
            run.windowDesc.width = 480;
            run.windowDesc.height = 360;
            run.windowDesc.transparent = options.transparent;
            run.idleWaitMs = 16;
            run.fontFactory = [&] {
                auto fonts = text::createSystemFontManager();
                fontAvailable = fontAvailable && fonts != nullptr;
                return std::shared_ptr<text::FontManager>(std::move(fonts));
            };
            run.rendererFactory = [&probe](platform::ApplicationHost& input, core::WindowId& id) {
                probe.id = id;
                app::RendererSetup setup;
                setup.present = [&probe, &input] {
                    auto* window = input.platformWindow(probe.id);
                    const bool ok = window && window->present(probe.shell.pixels()) == platform::PresentResult::Ok;
                    if (ok) ++probe.presents;
                    return ok;
                };
                return setup;
            };
            if (options.recovery) {
                const auto factory = run.rendererFactory;
                run.rendererFactory = [factory, &probe](platform::ApplicationHost& input, core::WindowId& id) {
                    auto setup = factory(input, id);
                    setup.failed = [&probe] { return probe.presents >= 2 && probe.recoveries == 0; };
                    return setup;
                };
                const auto desc = run.windowDesc;
                run.onRendererFailure = [factory, desc, &probe](platform::ApplicationHost& input, core::WindowId& id)
                    -> std::optional<app::RendererSetup> {
                    probe.recoveryValue = probe.shell.controller().editingValue();
                    probe.recoveryFocus = probe.shell.focus().focusedIdentity();
                    probe.recoveryOffset = probe.list.scrollController()->offset();
                    probe.recoveryFrame = probe.presents;
                    input.destroyWindow(id);
                    const auto replacement = input.createWindow(desc);
                    if (!replacement) return std::nullopt;
                    id = *replacement;
                    ++probe.recoveries;
                    return factory(input, id);
                };
            }
            run.poll = [&, isFirst = &probe == &first](app::AppShell&, std::uint64_t) {
                if (probe.recoveryValue && probe.presents > probe.recoveryFrame) {
                    probe.statePreserved = probe.statePreserved &&
                        probe.shell.controller().editingValue() == *probe.recoveryValue &&
                        probe.shell.focus().focusedIdentity() == probe.recoveryFocus &&
                        probe.list.scrollController()->offset() == probe.recoveryOffset;
                    probe.recoveryValue.reset();
                }
                const auto now = std::chrono::steady_clock::now();
                if (now - started >= std::chrono::seconds(options.seconds)) {
                    timedOut = true;
                    for (auto id : host.windowIds()) host.requestWindowClose(id);
                }
                if (!isFirst) return options.recovery;
                if (options.resize && now >= nextResize) {
                    nextResize = now + std::chrono::milliseconds(80);
                    for (auto id : host.windowIds()) {
                        auto* window = host.platformWindow(id);
                        if (window) SDL_SetWindowSize(static_cast<SDL_Window*>(window->nativeSurface().nativeWindow),
                                                       480 + (cycles % 5) * 24, 360 + (cycles % 3) * 16);
                    }
                    ++cycles;
                }
                if (options.stressMiB && now >= nextPressure) {
                    nextPressure = now + std::chrono::seconds(1);
                    if (pressure.empty()) pressure.assign(static_cast<std::size_t>(options.stressMiB) * 1024 * 1024, 0x5a);
                    else std::vector<std::uint8_t>().swap(pressure);
                }
                return options.recovery;
            };
            return run;
        };
        const int code = app::runApp({{&first.shell, makeOptions(first, "Lumen acceptance: primary")},
                                      {&second.shell, makeOptions(second, "Lumen acceptance: secondary")}}, host);
        const bool ime = !options.expectedText.empty() && events.preedit > 0 && events.commits > 0 &&
            first.shell.state().get("document") == options.expectedText;
        const bool preserved = first.statePreserved && second.statePreserved &&
            !first.recoveryValue && !second.recoveryValue &&
            (events.commits > 0 || (first.shell.state().get("document") == first.initialText &&
                                   second.shell.state().get("document") == second.initialText));
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::printf("{\"driver\":\"%s\",\"seconds\":%.3f,\"windows\":2,\"frames\":%u,"
                    "\"resize_events\":%u,\"preedit_events\":%u,\"commit_events\":%u,"
                    "\"composition_end_events\":%u,\"wheel_events\":%u,\"font_available\":%s,"
                    "\"transparent_requested\":%s,\"external_clipboard\":%s,\"ime_verified\":%s,"
                    "\"stress_mib\":%d,\"simulated_recoveries\":%u,\"state_preserved\":%s}\n",
                    driver.c_str(), seconds, first.presents + second.presents,
                    events.resize.load(), events.preedit.load(), events.commits.load(),
                    events.compositionEnds.load(), events.wheel.load(), fontAvailable ? "true" : "false",
                    options.transparent ? "true" : "false", externalClipboard ? "true" : "false",
                    ime ? "true" : "false", options.stressMiB, first.recoveries + second.recoveries,
                    preserved ? "true" : "false");
        return code != 0 || !timedOut || first.presents == 0 || second.presents == 0 || !preserved ||
            (options.resize && events.resize == 0) || (!options.expectedText.empty() && !ime) ||
            (options.recovery && (first.recoveries != 1 || second.recoveries != 1)) ? 1 : 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "live acceptance failed: %s\n", error.what());
        return 2;
    }
}
