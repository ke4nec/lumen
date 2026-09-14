// Widget Gallery 示例：控件/布局/颜色方案/主题演示。
// 窗口主循环由 app::runApp 驱动（ApplicationHost 事件泵 + FrameScheduler）；
// `--headless` 输出确定性帧哈希并遍历各分区做最小交互冒烟。

#include <cstdio>
#include <cstdlib>
#include <string>

#include "gallery_app.h"
#include "lumen/app/app_shell.h"
#include "lumen/platform/sdl3_host.h"

namespace {

using lumen::examples::GalleryApp;

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

int runHeadless(GalleryApp& app) {
    app.setView(lumen::core::Size{1024.0F, 768.0F});
    std::printf("frame0 %016llx route=%s\n",
                static_cast<unsigned long long>(app.renderFrame()),
                app.navigator().current().c_str());

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
    clickVisible(app, "color-Green");
    std::printf("color=%s dropdown=%s\n",
                app.state().get("color").c_str(),
                app.dropdownOpen() ? "open" : "closed");
    (void)app.renderFrame();
    clickVisible(app, "toggle-dropdown-button");
    std::printf("dropdown=%s\n", app.dropdownOpen() ? "open" : "closed");
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

    // 弹窗 Escape：关闭弹窗但不弹出路由。
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

int runWindowed(GalleryApp& app, const Options& options) {
    lumen::platform::Sdl3ApplicationHost host;
    lumen::app::RunOptions runOptions;
    runOptions.windowDesc.title = "Lumen Gallery";
    runOptions.windowDesc.width = 1024;
    runOptions.windowDesc.height = 768;
    runOptions.diagnostics = options.diagnostics;
    return lumen::app::runApp(app.shell(), host, runOptions);
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    GalleryApp app;
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
