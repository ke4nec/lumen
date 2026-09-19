// Widget Gallery 示例：控件/布局/颜色方案/主题演示。
// 窗口主循环由 app::runApp 驱动（ApplicationHost 事件泵 + FrameScheduler）；
// `--headless` 输出确定性帧哈希并遍历各分区做最小交互冒烟；
// `--dump-frame <path>` 额外把首帧像素写为 RGBA 原始数据（视觉核对用）；
// `--max-frames N` 用于窗口级短跑验证后自动退出。

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "gallery_app.h"
#include "lumen/app/app_shell.h"
#include "lumen/platform/sdl3_host.h"
#include "lumen/text/system_font_manager.h"

namespace {

using lumen::examples::GalleryApp;

struct Options {
    bool headless{false};
    bool diagnostics{false};
    std::string dumpFrame{};
    std::uint64_t maxFrames{0};
    std::string sampleRoute{};
    std::string sampleKey{};
    float width{1024}, height{768}, fontScale{1}, dpi{1};
    int direction{0}, density{1};
    bool light{false}, highContrast{false}, reduceAnimation{false}, systemFonts{false};
    std::uint64_t sampleTime{0};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--headless") {
            options.headless = true;
        } else if (flag == "--diagnostics") {
            options.diagnostics = true;
        } else if (flag == "--dump-frame" && i + 1 < argc) {
            options.dumpFrame = argv[++i];
        } else if (flag == "--sample-route" && i + 1 < argc) {
            options.sampleRoute = argv[++i];
        } else if (flag == "--sample-key" && i + 1 < argc) {
            options.sampleKey = argv[++i];
        } else if (flag == "--width" && i + 1 < argc) {
            options.width = std::strtof(argv[++i], nullptr);
        } else if (flag == "--height" && i + 1 < argc) {
            options.height = std::strtof(argv[++i], nullptr);
        } else if (flag == "--font-scale" && i + 1 < argc) {
            options.fontScale = std::strtof(argv[++i], nullptr);
        } else if (flag == "--dpi" && i + 1 < argc) {
            options.dpi = std::strtof(argv[++i], nullptr);
        } else if (flag == "--direction" && i + 1 < argc) {
            options.direction = std::atoi(argv[++i]);
        } else if (flag == "--density" && i + 1 < argc) {
            options.density = std::atoi(argv[++i]);
        } else if (flag == "--sample-time" && i + 1 < argc) {
            options.sampleTime = std::strtoull(argv[++i], nullptr, 10);
        } else if (flag == "--light") {
            options.light = true;
        } else if (flag == "--high-contrast") {
            options.highContrast = true;
        } else if (flag == "--reduce-animation") {
            options.reduceAnimation = true;
        } else if (flag == "--system-fonts") {
            options.systemFonts = true;
        } else if (flag == "--max-frames" && i + 1 < argc) {
            options.maxFrames = std::strtoull(argv[++i], nullptr, 10);
        }
    }
    return options;
}

lumen::core::Offset centerOf(GalleryApp& app, const char* key) {
    const lumen::core::RenderNode* node =
        lumen::core::findNodeByKey(app.root(), key);
    if (node == nullptr) {
        std::fprintf(stderr, "headless: node '%s' not found\n", key);
        std::exit(1);
    }
    return lumen::core::absoluteOffset(app.root(), key) +
           lumen::core::Offset{node->size.width * 0.5F,
                               node->size.height * 0.5F};
}

void click(GalleryApp& app, const char* key) {
    const auto point = centerOf(app, key);
    app.pointerDown(point);
    app.pointerUp(point);
}

// 内容节点点击：先滚入主列表视口（被裁剪节点不命中，与真实用户一致），
// 再返回中心点。header/nav/弹窗等视口外常驻节点仍用 click。
lumen::core::Offset visibleCenterOf(GalleryApp& app, const char* key) {
    for (int i = 0; i < 16; ++i) {
        (void)app.renderFrame();
        const lumen::core::RenderNode* node =
            lumen::core::findNodeByKey(app.root(), key);
        const lumen::core::RenderNode* list =
            lumen::core::findNodeByKey(app.root(), "gallery-list");
        if (node == nullptr || list == nullptr) {
            std::fprintf(stderr, "headless: node '%s' not found\n", key);
            std::exit(1);
        }
        const auto nodeOff = lumen::core::absoluteOffset(app.root(), key);
        const auto listOff = lumen::core::absoluteOffset(app.root(),
                                                         "gallery-list");
        const float top = listOff.y;
        const float bottom = listOff.y + list->size.height;
        const float nodeTop = nodeOff.y;
        const float nodeBottom = nodeOff.y + node->size.height;
        if (node->size.height > 0.0F && nodeTop >= top &&
            nodeBottom <= bottom) {
            return nodeOff +
                   lumen::core::Offset{node->size.width * 0.5F,
                                       node->size.height * 0.5F};
        }
        const float deltaY =
            nodeTop < top ? nodeTop - top - 8.0F : nodeBottom - bottom + 8.0F;
        app.wheel(centerOf(app, "gallery-list"),
                  lumen::core::Offset{0.0F, deltaY});
    }
    std::fprintf(stderr, "headless: node '%s' never became visible\n", key);
    std::exit(1);
}

void clickVisible(GalleryApp& app, const char* key) {
    const auto point = visibleCenterOf(app, key);
    app.pointerDown(point);
    app.pointerUp(point);
}

int runHeadless(GalleryApp& app, const std::string& dumpFrame) {
    app.setView(lumen::core::Size{1024.0F, 768.0F});
    std::printf("frame0 %016llx route=%s\n",
                static_cast<unsigned long long>(app.renderFrame()),
                app.navigator().current().c_str());
    if (!dumpFrame.empty()) {
        // 首帧原始 RGBA（宽高固定 1024×768；转换 PNG 由外部脚本完成）。
        std::ofstream out(dumpFrame, std::ios::binary);
        if (!out) {
            std::fprintf(stderr, "dump-frame: cannot open '%s' for writing\n",
                         dumpFrame.c_str());
        } else {
            const lumen::render::PixelBuffer& pixels = app.pixels();
            out.write(reinterpret_cast<const char*>(pixels.rgba.data()),
                      static_cast<std::streamsize>(pixels.rgba.size()));
            out.close();
            if (!out) {
                std::fprintf(stderr, "dump-frame: short write to '%s'\n",
                             dumpFrame.c_str());
            } else {
                std::printf("dump %s %dx%d\n", dumpFrame.c_str(),
                            pixels.width, pixels.height);
            }
        }
    }

    // Buttons：点击变体按钮，计数 +1。
    click(app, "goto-buttons-button");
    std::printf("route %s\n", app.navigator().current().c_str());
    std::printf("frame1 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));
    click(app, "btn-variant-Filled");
    std::printf("clicks %s\n", app.state().get("button-clicks").c_str());
    std::printf("frame2 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    // Inputs：文本输入 + 开关切换 + 下拉选择 + 页签切换 + 表单校验
    // （有效/无效双路径）。内容节点经 clickVisible 自动滚入视口。
    click(app, "nav-inputs");
    (void)app.renderFrame();
    std::printf("route %s\n", app.navigator().current().c_str());
    clickVisible(app, "username-field");
    app.textInput("Lumen");
    clickVisible(app, "autosave-checkbox");
    std::printf("username=%s autosave=%s\n",
                app.state().get("username").c_str(),
                app.state().get("autosave").c_str());
    // M11：下拉浮动菜单——值行点击打开 overlay，选项在 overlay 树中。
    clickVisible(app, "color-dropdown");
    (void)app.renderFrame();
    std::printf("dropdown=%s\n", app.dropdownOpen() ? "open" : "closed");
    if (app.shell().overlayRoot() != nullptr) {
        const lumen::core::RenderNode* option = lumen::core::findNodeByKey(
            *app.shell().overlayRoot(), "color-dropdown-opt-1");
        if (option != nullptr) {
            const lumen::core::Offset point =
                lumen::core::absoluteOffset(*app.shell().overlayRoot(),
                                            "color-dropdown-opt-1") +
                lumen::core::Offset{option->size.width * 0.5F,
                                    option->size.height * 0.5F};
            app.pointerDown(point);
            app.pointerUp(point);
        }
    }
    std::printf("color=%s dropdown=%s\n",
                app.state().get("color").c_str(),
                app.dropdownOpen() ? "open" : "closed");
    (void)app.renderFrame();
    clickVisible(app, "tab-More");
    (void)app.renderFrame();
    std::printf("tab=%s\n", app.state().get("gallery-tab").c_str());
    // 空表单提交：校验失败路径（错误文案节点出现，不弹窗）。
    clickVisible(app, "save-button");
    (void)app.renderFrame();
    std::printf("invalid dialog=%s errors=%zu nickname-error=%s\n",
                app.dialogOpen() ? "yes" : "no",
                app.form().errors().size(),
                lumen::core::findNodeByKey(app.root(), "nickname-error") !=
                        nullptr
                    ? "yes"
                    : "no");
    clickVisible(app, "nickname-field");
    app.textInput("Lumen");
    clickVisible(app, "email-field");
    app.textInput("dev@lumen.local");
    clickVisible(app, "save-button");
    std::printf("dialog=%s errors=%zu\n",
                app.dialogOpen() ? "yes" : "no",
                app.form().errors().size());
    (void)app.renderFrame();
    if (app.dialogOpen()) {
        click(app, "dialog-close");
        std::printf("dialog=%s\n", app.dialogOpen() ? "yes" : "no");
    }
    std::printf("frame3 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    // Layout：自适应网格单元数（窗口变化重排由约束传播自然发生），
    // 随后用 Back 按钮（覆盖 "back" handler）返回。
    click(app, "nav-layout");
    (void)app.renderFrame();
    std::printf("route %s\n", app.navigator().current().c_str());
    const auto* grid =
        lumen::core::findNodeByKey(app.root(), "adaptive-grid");
    std::printf("grid %s cells=%zu\n", grid != nullptr ? "yes" : "no",
                grid != nullptr ? grid->children.size() : 0U);
    const auto* image =
        lumen::core::findNodeByKey(app.root(), "demo-image");
    std::printf("image %s\n", image != nullptr ? "yes" : "no");
    std::printf("frame4 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));
    clickVisible(app, "back-button");
    (void)app.renderFrame();
    std::printf("route %s\n", app.navigator().current().c_str());

    // Lists：主列表滚动 + 千项虚拟列表滚动。
    click(app, "nav-lists");
    (void)app.renderFrame();
    app.wheel(centerOf(app, "gallery-list"),
              lumen::core::Offset{0.0F, 240.0F});
    std::printf("scroll %.1f\n", app.scroll().offset());
    (void)app.renderFrame();
    app.wheel(centerOf(app, "gallery-library"),
              lumen::core::Offset{0.0F, 4000.0F});
    (void)app.renderFrame();
    const auto* library =
        lumen::core::findNodeByKey(app.root(), "gallery-library");
    std::printf("library %s visible=%zu\n",
                library != nullptr ? "yes" : "no",
                library != nullptr ? library->children.size() : 0U);
    std::printf("frame5 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    // Feedback：滑杆联动进度条 + 弹窗开合。
    click(app, "nav-feedback");
    (void)app.renderFrame();
    // 滑杆中心点击 → 值 50，进度条同 bind 联动。
    clickVisible(app, "progress-slider");
    (void)app.renderFrame();
    std::printf("slider=%s\n", app.state().get("demo-progress").c_str());
    clickVisible(app, "show-dialog-button-feedback");
    std::printf("dialog=%s\n", app.dialogOpen() ? "yes" : "no");
    (void)app.renderFrame();
    click(app, "dialog-close");
    std::printf("dialog=%s progress=%s\n",
                app.dialogOpen() ? "yes" : "no",
                app.state().get("demo-progress").c_str());
    std::printf("frame6 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    // Theme：深浅/密度/高对比/强调色切换（顺序覆盖派生保留回归）。
    click(app, "nav-theme");
    (void)app.renderFrame();
    clickVisible(app, "toggle-dark-button-theme");
    (void)app.renderFrame();
    std::printf("dark=%s\n", app.darkMode() ? "yes" : "no");
    clickVisible(app, "cycle-density-button");
    (void)app.renderFrame();
    std::printf(
        "density=%d\n",
        static_cast<int>(app.theme().metrics.density));
    // M11：v0.4 方向切换（派生保留：高对比/密度/深浅与方向正交）。
    clickVisible(app, "direction-ink-button");
    (void)app.renderFrame();
    std::printf("direction=%d\n", static_cast<int>(app.direction()));
    clickVisible(app, "toggle-contrast-button");
    (void)app.renderFrame();
    std::printf("contrast=%s\n",
                app.accessibilitySettings().highContrast ? "on" : "off");
    clickVisible(app, "accent-green-button");
    (void)app.renderFrame();
    const auto accent = app.theme().colors.accent;
    std::printf("accent=%d,%d,%d contrast=%s\n", accent.r, accent.g, accent.b,
                app.accessibilitySettings().highContrast ? "on" : "off");
    // 深浅二次切换：高对比必须保留（toggle-dark 派生保留回归）。
    clickVisible(app, "toggle-dark-button-theme");
    (void)app.renderFrame();
    std::printf("dark=%s contrast=%s\n", app.darkMode() ? "yes" : "no",
                app.accessibilitySettings().highContrast ? "on" : "off");
    clickVisible(app, "direction-core-button");
    (void)app.renderFrame();
    std::printf("direction=%d\n", static_cast<int>(app.direction()));
    std::printf("frame7 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));

    // 弹窗 Escape：主操作位于 Overview 内容头，先回根路由再打开弹窗。
    click(app, "nav-home");
    (void)app.renderFrame();
    std::printf("route %s\n", app.navigator().current().c_str());
    click(app, "show-dialog-button");
    (void)app.renderFrame();
    std::printf("dialog=%s\n", app.dialogOpen() ? "yes" : "no");
    app.keyDown(lumen::core::Key::Escape);
    (void)app.renderFrame();
    std::printf("dialog=%s route=%s\n", app.dialogOpen() ? "yes" : "no",
                app.navigator().current().c_str());

    app.keyDown(lumen::core::Key::Escape);
    (void)app.renderFrame();
    std::printf("route %s\n", app.navigator().current().c_str());
    std::printf("frame8 %016llx\n",
                static_cast<unsigned long long>(app.renderFrame()));
    return 0;
}

int runSample(GalleryApp& app, const Options& options) {
    const std::vector<std::string> routes{"home", "buttons", "inputs", "layout", "lists", "collections", "feedback", "theme"};
    if (std::find(routes.begin(), routes.end(), options.sampleRoute) == routes.end()) return 2;
    if (!(options.width >= 200 && options.width <= 4096 &&
          options.height >= 200 && options.height <= 4096 &&
          options.dpi >= 1 && options.dpi <= 2 &&
          options.fontScale >= 1 && options.fontScale <= 2 &&
          options.direction >= 0 && options.direction <= 3 &&
          options.density >= 0 && options.density <= 2)) {
        std::fprintf(stderr, "Invalid sample dimensions, scale, direction or density\n");
        return 2;
    }
    lumen::accessibility::AccessibilitySettings settings;
    settings.fontScale = options.fontScale;
    settings.highContrast = options.highContrast;
    settings.reduceAnimation = options.reduceAnimation;
    app.setAccessibilitySettings(settings);
    app.setTheme(lumen::style::Theme::fromSettings(settings, !options.light,
        static_cast<lumen::style::ControlDensity>(options.density),
        static_cast<lumen::style::ThemeDirection>(options.direction)));
    app.setView({options.width, options.height});
    app.setDeviceScale(options.dpi);
    std::string fonts = "placeholder";
    if (options.systemFonts) {
        auto manager = lumen::text::createSystemFontManager(&fonts);
        if (!manager) return 3; // A requested real-font sample may not silently fall back.
        app.setFontManager(std::shared_ptr<lumen::text::FontManager>(std::move(manager)));
    }
    if (!app.showSample(options.sampleRoute, options.sampleKey)) {
        std::fprintf(stderr, "Sample key not found: %s\n", options.sampleKey.c_str());
        return 4;
    }
    app.shell().tick(options.sampleTime);
    const auto hash = app.renderFrame();
    const auto& pixels = app.pixels();
    if (!options.dumpFrame.empty()) {
        std::ofstream out(options.dumpFrame, std::ios::binary);
        out.write(reinterpret_cast<const char*>(pixels.rgba.data()),
                  static_cast<std::streamsize>(pixels.rgba.size()));
        if (!out) return 5;
        std::ofstream metadata(options.dumpFrame + ".txt");
        metadata << "route=" << options.sampleRoute << "\nkey=" << options.sampleKey
                 << "\nlogical=" << options.width << 'x' << options.height
                 << "\npixels=" << pixels.width << 'x' << pixels.height
                 << "\nfontScale=" << options.fontScale << "\ndpi=" << options.dpi
                 << "\ndirection=" << options.direction << "\nlight=" << options.light
                 << "\ndensity=" << options.density << "\nhighContrast=" << options.highContrast
                 << "\nreduceAnimation=" << options.reduceAnimation << "\ntimeMs=" << options.sampleTime
                 << "\nbackend=CPU\nfonts=" << fonts << '\n';
        if (!metadata) return 5;
    }
    std::printf("sample route=%s key=%s pixels=%dx%d hash=%016llx fonts=%s\n",
        options.sampleRoute.c_str(), options.sampleKey.c_str(), pixels.width, pixels.height,
        static_cast<unsigned long long>(hash), fonts.c_str());
    return 0;
}

int runWindowed(GalleryApp& app, const Options& options) {
    if (options.reduceAnimation) {
        // 窗口模式此前解析了该开关但从不消费（仅采样路径用）：经同一
        // 派生链归零 MotionTokens，路由/状态过渡首拍即终态，便于对比
        // “慢”是动画还是光栅。不传参时默认行为不变。
        lumen::accessibility::AccessibilitySettings settings;
        settings.reduceAnimation = true;
        app.setAccessibilitySettings(settings);
    }
    lumen::platform::Sdl3ApplicationHost host;
    lumen::app::RunOptions runOptions;
    runOptions.windowDesc.title = "Lumen Gallery";
    runOptions.windowDesc.width = static_cast<int>(options.width);
    runOptions.windowDesc.height = static_cast<int>(options.height);
    // 自定义标题栏（lumen-titlebar-design）：无边框窗口 + 自绘 caption
    //（拖拽/resize 边由平台 hit-test 提供；runApp 注册拖拽区谓词）。
    runOptions.windowDesc.customTitleBar = true;
    // 透明窗口（design/gallery.html 圆角主界面）：根/标题栏自绘 16px
    // 圆角（最大化归零），圆角外像素交桌面合成器；runApp 同步将 CPU
    // 清屏色转全透明。
    runOptions.windowDesc.transparent = true;
    runOptions.diagnostics = options.diagnostics;
    runOptions.maxFrames = options.maxFrames;
    // 桌面系统字体：窗口路径注入真实字形（Windows 雅黑优先），CPU 光
    // 栅经同一管理器排版+绘制；失败回退占位并诊断（headless 不注入，
    // 保持帧哈希确定性）。
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
    // M12：系统主题切换 → 注入偏好（开启"跟随系统"时重派生主题）；
    // 自定义标题栏：WindowMaximized/WindowRestored → 最大化图标切换。
    // Restored 需查询实际最大化态（最小化恢复与最大化还原同事件，盲目
    // 置 false 会误清“最大化后最小化再恢复”的图标）。
    runOptions.onEvent = [&app, &host](lumen::app::AppShell&,
                                       const lumen::core::HostEvent& event) {
        if (event.type == lumen::core::HostEventType::SystemThemeChanged) {
            app.setSystemThemePreference(host.capabilities().prefersDarkMode,
                                         host.capabilities().accentColor);
        } else if (event.type ==
                   lumen::core::HostEventType::WindowMaximized) {
            app.noteWindowMaximized(true);
        } else if (event.type == lumen::core::HostEventType::WindowRestored) {
            const auto metrics = host.windowMetrics(event.window);
            app.noteWindowMaximized(metrics.has_value() &&
                                    metrics->maximized);
        }
    };
    // 自定义标题栏：窗口命令经宿主（无效 WindowId 走 SDL host 单窗口
    // 便捷路径）；close 走 WindowCloseRequested 统一拦截规则。
    GalleryApp::WindowCommands windowCommands;
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
    GalleryApp app;
    try {
        if (!options.sampleRoute.empty()) {
            const int result = runSample(app, options);
            if (result != 0 || options.headless) return result;
        }
        if (options.headless) {
            return runHeadless(app, options.dumpFrame);
        }
        return runWindowed(app, options);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
