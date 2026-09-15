// v0.3 阶段8D settings 示例：滚动列表、表单校验、弹窗、导航、主题与
// 无障碍标签。M2 起窗口主循环由 app::runApp 驱动（ApplicationHost 事件
// 泵 + FrameScheduler + damage/IME 同步）；本文件只保留选项解析与
// headless 冒烟脚本。`--headless` 输出确定性帧哈希。

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "settings_app.h"
#include "lumen/app/app_shell.h"
#include "lumen/platform/sdl3_host.h"
#include "lumen/text/system_font_manager.h"

namespace {

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

    // M3：Grid 页（自适应列宽）与千项 VirtualList 页（可见区物化）。
    // 回滚到顶：入口按钮需在视口内（滚动 240 后已滚出，点击被裁剪拒绝）。
    app.wheel(centerOf("settings-list"), lumen::core::Offset{0.0F, -240.0F});
    (void)app.renderFrame();
    app.pointerDown(centerOf("goto-grid-button"));
    app.pointerUp(centerOf("goto-grid-button"));
    std::printf("route %s\n", app.navigator().current().c_str());
    (void)app.renderFrame();
    const auto* grid = lumen::core::findNodeByKey(app.root(), "tile-grid");
    std::printf("grid %s cells=%zu\n", grid != nullptr ? "yes" : "no",
                grid != nullptr ? grid->children.size() : 0U);
    app.keyDown(lumen::core::Key::Escape);
    (void)app.renderFrame();  // home 树落地后才能取入口按钮坐标。

    app.pointerDown(centerOf("goto-library-button"));
    app.pointerUp(centerOf("goto-library-button"));
    std::printf("route %s\n", app.navigator().current().c_str());
    (void)app.renderFrame();
    const auto* library = lumen::core::findNodeByKey(app.root(),
                                                     "library-list");
    std::printf("library %s visible=%zu\n",
                library != nullptr ? "yes" : "no",
                library != nullptr ? library->children.size() : 0U);
    app.wheel(centerOf("library-list"), lumen::core::Offset{0.0F, 4000.0F});
    (void)app.renderFrame();
    library = lumen::core::findNodeByKey(app.root(), "library-list");
    std::printf("library visible=%zu extent=%.0f\n",
                library != nullptr ? library->children.size() : 0U,
                library != nullptr ? library->scrollExtent : 0.0F);
    app.keyDown(lumen::core::Key::Escape);
    std::printf("route %s\n", app.navigator().current().c_str());

    // M6：控件展示页与主题调试页。
    (void)app.renderFrame();  // home 树落地。
    app.wheel(centerOf("settings-list"), lumen::core::Offset{0.0F, -240.0F});
    (void)app.renderFrame();
    app.pointerDown(centerOf("goto-widgets-button"));
    app.pointerUp(centerOf("goto-widgets-button"));
    std::printf("route %s\n", app.navigator().current().c_str());
    (void)app.renderFrame();
    std::printf("widgets slider=%s progress=%s\n",
                app.state().get("volume").c_str(),
                app.state().get("progress").c_str());
    app.keyDown(lumen::core::Key::Escape);
    (void)app.renderFrame();

    app.pointerDown(centerOf("goto-theme-button"));
    app.pointerUp(centerOf("goto-theme-button"));
    std::printf("route %s\n", app.navigator().current().c_str());
    (void)app.renderFrame();
    app.pointerDown(centerOf("toggle-dark-button"));
    app.pointerUp(centerOf("toggle-dark-button"));
    (void)app.renderFrame();
    std::printf("theme switched ok\n");
    app.keyDown(lumen::core::Key::Escape);
    std::printf("route %s\n", app.navigator().current().c_str());
    return 0;
}

// M2：窗口主循环 = app::runApp（关闭请求策略/IME 同步/调度在应用壳与
// 应用配置钩子里）。M4：平台服务（文件选择/通知/外部链接）经注入装配。
int runWindowed(SettingsApp& app, const Options& options) {
    lumen::platform::Sdl3ApplicationHost host;

    // M4：服务动作注入（能力先行查询；不可用→按钮给出可读诊断）。
    SettingsApp::ServiceActions actions;
    const auto caps = [&host]() {
        // host.initialize 之前的查询不可靠；首次使用时能力已就绪。
        return host.capabilities();
    };
    actions.openFile = [&host, &app, caps]() {
        if (!caps().fileDialogs) {
            app.setPickedFile("open file: dialogs unavailable");
            return false;
        }
        lumen::platform::FileDialogRequest request;
        request.title = "Open file";
        request.allowMultiple = false;
        const auto result = host.requestFileDialog({}, request);
        if (!result.ok) {
            app.setPickedFile("open file: " + result.message);
            return false;
        }
        return true;
    };
    actions.saveFile = [&host, &app, caps]() {
        if (!caps().fileDialogs) {
            app.setPickedFile("save file: dialogs unavailable");
            return false;
        }
        lumen::platform::FileDialogRequest request;
        request.title = "Save file";
        request.forSave = true;
        request.defaultName = "lumen-export.txt";
        const auto result = host.requestFileDialog({}, request);
        if (!result.ok) {
            app.setPickedFile("save file: " + result.message);
            return false;
        }
        return true;
    };
    actions.notify = [&host, &app, caps]() {
        if (!caps().notifications) {
            app.setPickedFile("notify: notifications unavailable");
            return false;
        }
        lumen::platform::NotificationRequest request;
        request.title = "Lumen settings";
        request.body = "Profile saved.";
        return host.postNotification(request).ok;
    };
    actions.openDocs = [&host, &app, caps]() {
        if (!caps().openUrl) {
            app.setPickedFile("open docs: url unavailable");
            return false;
        }
        return host.openUrl("https://www.libsdl.org/").ok;
    };
    app.setServiceActions(std::move(actions));

    lumen::app::RunOptions runOptions;
    runOptions.windowDesc.title = "Lumen Settings - v0.3";
    runOptions.windowDesc.width = 800;
    runOptions.windowDesc.height = 600;
    runOptions.diagnostics = options.diagnostics;

    // 桌面系统字体（与 gallery 同口径：Windows 雅黑优先；失败回退占位）。
    runOptions.fontFactory = []()
        -> std::shared_ptr<lumen::text::FontManager> {
        std::string fontDiagnostics;
        auto fonts =
            lumen::text::createSystemFontManager(&fontDiagnostics);
        if (fonts != nullptr) {
            std::printf("[diag] fonts: %s\n", fontDiagnostics.c_str());
            return std::shared_ptr<lumen::text::FontManager>(
                std::move(fonts));
        }
        std::printf("[diag] fonts: %s — keeping placeholder metrics\n",
                    fontDiagnostics.c_str());
        return {};
    };

    // M4：文件选择完成事件 → 应用状态（UI 线程内同步落地）。
    // M12：系统主题切换 → 注入偏好（开启"跟随系统"时重派生主题）。
    runOptions.onEvent = [&app, &host](lumen::app::AppShell&,
                                       const lumen::core::HostEvent& event) {
        if (event.type ==
            lumen::core::HostEventType::FileDialogCompleted) {
            app.setPickedFile(event.filePaths.empty()
                                  ? event.text.empty()
                                        ? "(cancelled)"
                                        : event.text
                                  : event.filePaths.front());
        } else if (event.type ==
                   lumen::core::HostEventType::SystemThemeChanged) {
            app.setSystemThemePreference(host.capabilities().prefersDarkMode,
                                         host.capabilities().accentColor);
        }
    };

    return lumen::app::runApp(app.shell(), host, runOptions);
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    SettingsApp app;
    try {
        if (options.headless) {
            return runHeadless(app);
        }
        return runWindowed(app, options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
