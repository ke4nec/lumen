// lumen-template 入口：SDL 宿主装配 + 窗口命令注入 + 诊断接线。
// headless 冒烟（--headless）输出确定性帧哈希（CI 防漂移）。

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

#include "lumen/core/preferences.h"
#include "lumen/core/single_instance.h";

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
    // G-8：位置记忆回放（上次窗口位置；无记录 = 系统默认）。
    {
        lumen::core::Preferences prefs;
        const std::string dir = lumen::diagnostics::
            defaultDiagnosticsDirectory("lumen-template");
        if (!dir.empty() &&
            prefs.load((std::filesystem::path(dir) / "window.dat")
                           .string())) {
            runOptions.windowDesc.x =
                static_cast<int>(prefs.getInt("window.x", -1));
            runOptions.windowDesc.y =
                static_cast<int>(prefs.getInt("window.y", -1));
        }
    }
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
    const int exitCode = lumen::app::runApp(app.shell(), host, runOptions);
    // G-8：退出时记录最后窗口位置（下次启动回放）。
    if (!host.windowIds().empty()) {
        const auto metrics = host.windowMetrics(host.windowIds().front());
        if (metrics.has_value() && metrics->positioned) {
            const std::string dir = lumen::diagnostics::
                defaultDiagnosticsDirectory("lumen-template");
            if (!dir.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                lumen::core::Preferences prefs;
                prefs.setInt("window.x", metrics->x);
                prefs.setInt("window.y", metrics->y);
                (void)prefs.save((std::filesystem::path(dir) /
                                  "window.dat")
                                     .string());
            }
        }
    }
    return exitCode;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    TemplateApp app{/*persistent=*/!options.headless};
    app.attach();
    try {
        if (options.headless) {
            // headless 冒烟不走单实例（并行 CI 下第二个实例会被静默
            // 退出、不输出帧哈希——review M-5）；也不进窗口路径。
            return runHeadless(app);
        }
        // G-8：单实例（仅窗口路径）——已有实例时送达激活请求后即刻
        // 退出。
        switch (lumen::core::SingleInstanceGuard::acquire(
            {.appName = "lumen-template",
             .onActivateRequest = [] {
                 // 激活请求在后台线程到达；投递 UI 线程属应用装配
                 //（模板单窗口场景由窗口管理器聚焦）。
             }})) {
            case lumen::core::SingleInstanceGuard::Status::
                SecondaryActivated:
                return 0;
            case lumen::core::SingleInstanceGuard::Status::
                SecondaryNotifyFailed:
            case lumen::core::SingleInstanceGuard::Status::Unavailable:
                // 结构化降级：允许多实例继续运行（不因助手失败丢窗口）。
            case lumen::core::SingleInstanceGuard::Status::Primary:
                break;
        }
        return runWindowed(app);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
