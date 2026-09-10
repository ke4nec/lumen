// v0.3 阶段8D settings 示例：滚动列表、表单校验、弹窗、导航、主题与
// 无障碍标签。窗口循环使用阶段8A 的 Sdl3ApplicationHost（归一化事件：
// 修饰键、滚轮、指针取消、关闭请求），CPU 后端渲染；`--headless` 输出
// 确定性帧哈希。

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

#include <SDL3/SDL.h>

#include "settings_app.h"
#include "lumen/platform/sdl3_host.h"
#include "lumen/render/frame_scheduler.h"

namespace {

using lumen::core::HostEvent;
using lumen::core::HostEventType;
using lumen::examples::SettingsApp;

struct Options {
    bool headless{false};
    bool diagnostics{false};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--headless") {
            options.headless = true;
        } else if (flag == "--diagnostics") {
            options.diagnostics = true;
        }
    }
    return options;
}

int runHeadless(SettingsApp& app) {
    app.setView(lumen::core::Size{800.0F, 600.0F});
    std::printf("frame0 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    const auto centerOf = [&app](const char* key) {
        const lumen::core::RenderNode* node =
            lumen::core::findNodeByKey(app.root(), key);
        if (node == nullptr) {
            std::fprintf(stderr, "headless: node '%s' not found\n", key);
            std::exit(1);
        }
        return lumen::core::absoluteOffset(app.root(), key) +
               lumen::core::Offset{node->size.width * 0.5F,
                                   node->size.height * 0.5F};
    };

    // 导航到表单 → 校验失败（空字段）→ 填写并保存 → 弹窗 → 关闭 → 返回
    // → 列表滚动。
    app.pointerDown(centerOf("goto-form-button"));
    app.pointerUp(centerOf("goto-form-button"));
    std::printf("route %s\n", app.navigator().current().c_str());
    std::printf("frame1 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    app.pointerDown(centerOf("nickname-field"));
    app.pointerUp(centerOf("nickname-field"));
    app.textInput("Lumen");
    app.pointerDown(centerOf("save-button"));
    app.pointerUp(centerOf("save-button"));
    std::printf("errors %zu dialog=%s\n", app.form().errors().size(),
                app.dialogOpen() ? "yes" : "no");

    app.pointerDown(centerOf("email-field"));
    app.pointerUp(centerOf("email-field"));
    app.textInput("dev@lumen.local");
    app.pointerDown(centerOf("save-button"));
    app.pointerUp(centerOf("save-button"));
    std::printf("dialog=%s\n", app.dialogOpen() ? "yes" : "no");
    (void)app.renderFrame();  // 弹窗树先落地，才能按 key 取中心点。
    app.pointerDown(centerOf("dialog-close"));
    app.pointerUp(centerOf("dialog-close"));
    std::printf("dialog=%s\n", app.dialogOpen() ? "yes" : "no");
    std::printf("frame2 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    app.keyDown(lumen::core::Key::Escape);
    std::printf("route %s\n", app.navigator().current().c_str());

    app.wheel(centerOf("settings-list"), lumen::core::Offset{0.0F, 240.0F});
    std::printf("scroll %.1f\n", app.scroll().offset());
    std::printf("frame3 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    return 0;
}

int runWindowed(SettingsApp& app, const Options& options) {
    lumen::platform::Sdl3ApplicationHost host;
    if (!host.initialize()) {
        return 1;
    }
    lumen::platform::WindowDesc desc;
    desc.title = "Lumen Settings - v0.3";
    desc.width = 800;
    desc.height = 600;
    const auto id = host.createWindow(desc);
    if (!id.has_value()) {
        return 1;
    }
    auto* window = host.platformWindow(*id);
    if (window == nullptr) {
        return 1;
    }

    const auto applyScale = [&host, &app, id]() {
        const auto metrics = host.windowMetrics(*id);
        if (metrics.has_value() && metrics->logicalSize.width > 0.0F) {
            app.setDeviceScale(metrics->deviceScale);
            app.setView(metrics->logicalSize);
        }
    };
    applyScale();

    lumen::render::RealtimeClock clock;
    lumen::render::FrameScheduler::Config schedulerConfig;
    schedulerConfig.targetFps = 60;
    lumen::render::FrameScheduler scheduler{schedulerConfig, &clock};
    scheduler.requestFrame(lumen::render::FrameReason::Explicit, *id);

    if (options.diagnostics) {
        const auto caps = host.capabilities();
        std::printf("[diag] host=%s clipboard=%s ime=%s multiWindow=%s\n",
                    caps.adapterName.c_str(), caps.clipboard ? "yes" : "no",
                    caps.ime ? "yes" : "no",
                    caps.multiWindow ? "yes" : "no");
    }

    bool running = true;
    while (running) {
        HostEvent event;
        while (host.pollEvent(event)) {
            switch (event.type) {
                case HostEventType::Quit:
                    running = false;
                    break;
                case HostEventType::WindowCloseRequested:
                    // 统一关闭规则：modal → 路由栈 → 退出（plan §3.4）。
                    if (app.dialogOpen() ||
                        app.navigator().handleBack(false)) {
                        app.keyDown(lumen::core::Key::Escape);
                    } else {
                        running = false;
                    }
                    break;
                case HostEventType::Resize:
                case HostEventType::DpiChanged: {
                    const auto metrics = host.windowMetrics(*id);
                    if (metrics.has_value()) {
                        app.setView(metrics->logicalSize);
                        app.setDeviceScale(metrics->deviceScale);
                    }
                    scheduler.requestFrame(lumen::render::FrameReason::Resize,
                                           event.window);
                    break;
                }
                case HostEventType::WindowMinimized:
                    scheduler.setWindowVisible(false);
                    break;
                case HostEventType::WindowRestored:
                    scheduler.setWindowVisible(true);
                    scheduler.requestFrame(lumen::render::FrameReason::Resize,
                                           event.window);
                    break;
                case HostEventType::PointerDown:
                    app.pointerDown(event.position);
                    scheduler.requestFrame(lumen::render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerMove:
                    app.pointerMove(event.position);
                    scheduler.requestFrame(lumen::render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerUp:
                    app.pointerUp(event.position);
                    scheduler.requestFrame(lumen::render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::PointerCancel:
                    app.pointerCancel();
                    break;
                case HostEventType::Wheel:
                    app.wheel(event.position, event.scrollDelta);
                    scheduler.requestFrame(lumen::render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::TextInput:
                    app.textInput(event.text);
                    scheduler.requestFrame(lumen::render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::TextEditing:
                    app.textEditing(event.text);
                    scheduler.requestFrame(lumen::render::FrameReason::Input,
                                           event.window);
                    break;
                case HostEventType::KeyDown:
                    app.keyDown(event.keyCode, event.modifiers, event.keyChar);
                    scheduler.requestFrame(lumen::render::FrameReason::Input,
                                           event.window);
                    break;
                default:
                    break;
            }
        }

        app.tick(clock.nowMs());
        window->setTextInputEnabled(app.controller().wantsTextInput());

        if (scheduler.shouldSubmitFrame()) {
            app.renderFrame();
            window->present(app.pixels());
            scheduler.markFrameSubmitted();
        }

        const auto waitMs = scheduler.msUntilNextFrame();
        const std::uint32_t capped =
            std::min<std::uint32_t>(waitMs.value_or(250), 250);
        if (capped > 0) {
            SDL_Delay(capped);
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const Options options = parseOptions(argc, argv);
    try {
        SettingsApp app;
        if (options.headless) {
            return runHeadless(app);
        }
        return runWindowed(app, options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 2;
    }
}
