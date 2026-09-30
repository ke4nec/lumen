// lumen-template 入口：SDL 宿主装配 + 窗口命令注入 + 诊断接线。
// headless 冒烟（--headless）输出确定性帧哈希（CI 防漂移）。

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "lumen/platform/sdl3_host.h"
#include "template_app.h"

namespace {

using lumen::app::RunOptions;
using lumen::template_app::TemplateApp;

namespace app = lumen::app;

struct Options {
    bool headless{false};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            options.headless = true;
        }
    }
    return options;
}

int runHeadless(TemplateApp& app) {
    app.shell().setView(lumen::core::Size{800.0F, 560.0F});
    std::printf("frame0 %016llx\n",
                static_cast<unsigned long long>(app.shell().renderFrame()));
    // 命令路径冒烟：菜单命令注册表 + 状态 + 确认对话框。
    app.shell().invokeCommand("bump");
    app.shell().invokeCommand("bump");
    std::printf("counter %s\n",
                app.shell().state().get("counter").c_str());
    app.shell().invokeCommand("reset");
    std::printf("dialog %d\n", app.dialogsBusy() ? 1 : 0);
    return 0;
}

int runWindowed(TemplateApp& app) {
    lumen::platform::Sdl3ApplicationHost host;
    app::RunOptions runOptions;
    runOptions.windowDesc.title = "Lumen Template";
    runOptions.windowDesc.width = 800;
    runOptions.windowDesc.height = 560;
    // 自定义标题栏：无边框窗口 + 拖拽区谓词（runApp 自动接线）。
    runOptions.windowDesc.customTitleBar = true;
    // G-2：崩溃兜底与持久日志（平台惯例目录）。
    runOptions.diagnosticsDirectory =
        lumen::diagnostics::defaultDiagnosticsDirectory("lumen-template");
    runOptions.diagnosticsAppName = "lumen-template";
    runOptions.onEvent = [&app, &host](app::AppShell&,
                                       const lumen::core::HostEvent& event) {
        if (event.type == lumen::core::HostEventType::WindowMaximized) {
            app.noteWindowMaximized(true);
        } else if (event.type == lumen::core::HostEventType::WindowRestored) {
            const auto metrics = host.windowMetrics(event.window);
            app.noteWindowMaximized(metrics.has_value() &&
                                    metrics->maximized);
        }
    };
    // 自定义标题栏：窗口命令经宿主（空 WindowId = SDL 单窗口便捷路径）。
    TemplateApp::WindowCommands windowCommands;
    windowCommands.minimize = [&host] { host.minimizeWindow({}); };
    windowCommands.toggleMaximize = [&host] {
        host.toggleMaximizeWindow({});
    };
    windowCommands.requestClose = [&host] { host.requestWindowClose({}); };
    app.setWindowCommands(std::move(windowCommands));
    return lumen::app::runApp(app.shell(), host, runOptions);
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    TemplateApp app;
    app.attach();
    try {
        return options.headless ? runHeadless(app) : runWindowed(app);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
