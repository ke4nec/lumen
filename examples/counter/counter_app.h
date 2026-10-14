#pragma once

// Counter application (plan §7)。M2 起帧管线（事件路由、重建、布局、
// damage、绘制缓存、DPI/IME 同步）收敛到 app::AppShell；本文件只保留
// 应用层职责：build 函数、状态 key 与业务 handler（plan §6.1）。
// 公共 API 保持迁移前签名——headless 集成测试与窗口循环共用。

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/render/renderer.h"
#include "lumen/style/theme.h"
#include "lumen/text/font_manager.h"

namespace lumen::examples {

class CounterApp {
  public:
    CounterApp() : CounterApp(buildUi()) {}

    // Root from the text DSL (`--dsl counter.lumen`, plan §7).
    explicit CounterApp(core::Widget root)
        : shell_(configFor(this, std::move(root))) {
        initialize();
    }

    CounterApp(const CounterApp&) = delete;
    CounterApp& operator=(const CounterApp&) = delete;
    CounterApp(CounterApp&&) = delete;
    CounterApp& operator=(CounterApp&&) = delete;

    // The declarative UI (plan §6.1 example shape). Static so the DSL golden
    // test can compare it against the parsed `.lumen` document.
    //
    // 控件不携带任何显式样式：Button/TextField/Text 的填充、边框、圆角、
    // 尺度与排版由 style::resolveStyle 从 AppShell 的 Theme（默认
    // Theme::dark()，Comfortable 密度）解析（视觉系统 §5/§6）。唯一显式
    // 值是页面背景——取 theme.colors.pageBackground token，与 `.lumen`
    // 文档中的 #18181B 同值（golden 测试逐字段相等的前提）。
    [[nodiscard]] static core::Widget buildUi(
        const style::Theme& theme = style::Theme::dark()) {
        using namespace dsl;
        core::Widget page = container(
            column({core::withKey(dsl::text("Count: ", bind("counter")),
                                  "count-text"),
                    core::withKey(button("Increment", onClick("increment")),
                                  "increment-button"),
                    core::withKey(text_field(bind("name"), placeholder("Name")),
                                  "name-field")},
                   core::EdgeInsets::all(24.0F), 12.0F),
            theme.colors.pageBackground);
        page.key = "root";
        return page;
    }

    // --- 帧管线/装配：转发到应用壳（M2 收敛点） ---

    void setView(core::Size size) { shell_.setView(size); }
    void setDeviceScale(float scale) { shell_.setDeviceScale(scale); }

    // M1：注入正式字体事实（Skia 后端激活时由 main 创建；空 = 占位）。
    void setFontManager(std::shared_ptr<const text::FontManager> fonts) {
        shell_.setFontManager(std::move(fonts));
    }

    // Overrides the paint target (plan §7: CPU/Skia switch). Null restores
    // the internal CPU renderer. The external renderer must outlive use.
    void setRenderer(render::Renderer* renderer) {
        shell_.setRenderer(renderer);
    }

    void rebuildIfDirty() { shell_.rebuildIfDirty(); }

    std::uint64_t renderFrame(bool forceFullRepaint = false) {
        return shell_.renderFrame(forceFullRepaint);
    }

    void setAccessibilitySettings(
        accessibility::AccessibilitySettings settings) {
        shell_.setAccessibilitySettings(settings);
    }

    void tick(std::uint64_t nowMs) { shell_.tick(nowMs); }

    // Hot reload entry (plan §5.2.4): swap the UI template, keep all state;
    // the rebuild diffs the trees so only changed nodes repaint. swapRoot 会
    // 永久覆盖 config.build——标题栏在此一并包装，热重载后窗口钮不丢。
    void swapRoot(core::Widget root) {
        shell_.swapRoot(withWindowChrome(std::move(root)));
    }

    // 自定义标题栏（titlebar-design §4.4）：窗口命令经宿主（main.cpp 注入；
    // 空 WindowId = SDL 单窗口便捷路径）。headless 不注入时安全 no-op，
    // requestClose 回退 shell.requestClose()——无宿主下语义可测。
    struct WindowCommands {
        std::function<void()> minimize;
        std::function<void()> toggleMaximize;
        std::function<void()> requestClose;
    };
    void setWindowCommands(WindowCommands commands) {
        windowCommands_ = std::move(commands);
    }

    // 最大化态回显（标题栏最大/还原钮图标切换；onEvent 转发）。
    void noteWindowMaximized(bool maximized) {
        if (maximized_ == maximized) {
            return;
        }
        maximized_ = maximized;
        shell_.markDirty();
    }

    // --- 事件分发 ---

    void pointerDown(core::Offset position) { shell_.pointerDown(position); }
    void pointerMove(core::Offset position) { shell_.pointerMove(position); }
    void pointerUp(core::Offset position) { shell_.pointerUp(position); }
    void textInput(const std::string& text) { shell_.textInput(text); }
    void textEditing(const std::string& text) { shell_.textEditing(text); }
    void cancelComposition() { shell_.cancelComposition(); }

    void keyDown(core::Key key) { shell_.keyDown(key); }
    void keyDown(core::Key key, core::KeyModifiers modifiers, char keyChar) {
        shell_.keyDown(key, modifiers, keyChar);
    }

    // --- 应用状态/查询 ---

    // 应用壳访问（M2：窗口循环经 runApp 驱动；测试/宿主直接驱动 shell）。
    [[nodiscard]] app::AppShell& shell() { return shell_; }
    [[nodiscard]] const app::AppShell& shell() const { return shell_; }

    [[nodiscard]] int counterValue() const {
        return std::atoi(shell_.state().get("counter").c_str());
    }
    [[nodiscard]] const core::StateStore& state() const {
        return shell_.state();
    }
    [[nodiscard]] const core::RenderNode& root() const { return shell_.root(); }
    [[nodiscard]] const core::InteractionController& controller() const {
        return shell_.controller();
    }
    [[nodiscard]] std::uint32_t partialRepaintCount() const {
        return shell_.partialRepaintCount();
    }
    [[nodiscard]] bool wantsTextInput() const {
        return shell_.wantsTextInput();
    }
    // IME 候选框锚点（与 painter 同一 TextLayout，M1）。renderFrame() 后
    // 查询使 rect 跟踪新布局。
    [[nodiscard]] core::Rect focusedTextRect() const {
        return shell_.focusedTextRect();
    }
    [[nodiscard]] int focusedCaretOffset() const {
        return shell_.focusedCaretOffset();
    }
    [[nodiscard]] const render::PixelBuffer& pixels() const {
        return shell_.pixels();
    }
    [[nodiscard]] render::RenderStats stats() { return shell_.stats(); }
    [[nodiscard]] render::RendererCapabilities capabilities() {
        return shell_.capabilities();
    }

  private:
    // 应用壳配置：无应用级键/滚轮/关闭拦截——counter 是最小样例。
    // build 每次重建都重新组合标题栏（读取当前 maximized_ 切换最大/
    // 还原图标）；AppShell 延迟首次 build，build 运行时 shell_ 已构造。
    [[nodiscard]] static app::AppShell configFor(CounterApp* self,
                                                 core::Widget root) {
        app::ShellConfig config;
        config.build = [self, template_ = std::move(root)]() -> core::Widget {
            return self->withWindowChrome(template_);
        };
        return app::AppShell{std::move(config)};
    }

    // 自定义标题栏（titlebar-design §5 最小子集，对齐 lumen-template）：
    // 标题文本 + 弹性拖拽区 + 最小化/最大化/关闭钮（44×32；close 用
    // WindowClose 变体——rest 幽灵、hover 实心警示红）+ 1px 底分隔线，
    // 内容行 flex 占满剩余高度。GNOME Wayland 不提供服务端装饰
    // （libdecor 已因输入区域缺陷禁用），标题栏是窗口唯一的移动/关闭
    // 入口。标题栏不进 golden 树——.lumen 冻结语法无 windowDrag/icon，
    // dsl golden 断言的 buildUi() 仍是纯内容树。
    [[nodiscard]] core::Widget withWindowChrome(
        const core::Widget& content) const {
        const style::Theme& theme = shell_.theme();
        core::Widget title = core::makeText("Lumen Counter",
                                            theme.typography.label);
        title.key = "counter-title";
        core::Widget drag = core::makeRow({}, core::MainAxisAlignment::Start,
                                          core::CrossAxisAlignment::Center);
        drag.key = "counter-titlebar-drag";
        drag.flex = 1.0F;
        const auto windowButton = [&](core::IconId icon, std::string key,
                                      core::ButtonVariant variant) {
            // key 同时是 onClick handler 名（gallery windowButton 模式）：
            // Widget.onClick 非空是拖拽区排除（isWindowDragPoint）与命令
            // 分发的共同依据——缺省时命中被窗口拖动吞掉。
            core::Widget button = core::makeButton(
                "", theme.typography.label, core::EdgeInsets{}, 0.0F,
                key, 44.0F, 32.0F, key);
            button.icon = icon;
            return core::withVariant(std::move(button), variant);
        };
        core::Widget controls = core::makeRow(
            {windowButton(core::IconId::Minus, "window-minimize",
                          core::ButtonVariant::Ghost),
             windowButton(maximized_ ? core::IconId::Restore
                                     : core::IconId::Maximize,
                          "window-maximize", core::ButtonVariant::Ghost),
             windowButton(core::IconId::Close, "window-close",
                          core::ButtonVariant::WindowClose)},
            core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch);
        controls.key = "counter-window-actions";
        core::Widget bar = core::makeRow(
            {std::move(title), std::move(drag), std::move(controls)},
            core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Center, 8.0F,
            core::EdgeInsets::only(12.0F, 0.0F, 0.0F, 0.0F));
        bar.height = 48.0F;
        bar = core::withWindowDrag(std::move(bar));
        bar.key = "counter-titlebar";
        core::Widget divider = core::makeContainerLeaf(
            std::nullopt, 1.0F, core::EdgeInsets{}, core::EdgeInsets{},
            theme.colors.borderDefault, "counter-titlebar-divider");
        core::Widget page = content;
        page.flex = 1.0F;
        core::Widget window = core::makeColumn(
            {std::move(bar), std::move(divider), std::move(page)},
            core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch);
        window.color = theme.colors.pageBackground;
        window.key = "counter-window";
        return window;
    }

    // 状态 key 与业务 handler（应用层唯一职责，plan §6.1）。
    void initialize() {
        core::StateStore& state = shell_.state();
        state.set("counter", "0");
        state.set("name", "");
        shell_.handlers()["increment"] = [this] {
            shell_.state().set("counter", std::to_string(counterValue() + 1));
        };
        // 窗口钮（titlebar-design §4.4）：宿主命令优先；headless 无宿主
        // 时 close 回退 shell.requestClose()（走应用关闭规则）。
        shell_.handlers()["window-minimize"] = [this] {
            if (windowCommands_.minimize) {
                windowCommands_.minimize();
            }
        };
        shell_.handlers()["window-maximize"] = [this] {
            if (windowCommands_.toggleMaximize) {
                windowCommands_.toggleMaximize();
            }
        };
        shell_.handlers()["window-close"] = [this] {
            if (windowCommands_.requestClose) {
                windowCommands_.requestClose();
                return;
            }
            (void)shell_.requestClose();
        };
    }

    WindowCommands windowCommands_{};
    bool maximized_{false};
    app::AppShell shell_;
};

}  // namespace lumen::examples
