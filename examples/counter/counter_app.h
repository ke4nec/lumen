#pragma once

// Counter application (plan §7)。M2 起帧管线（事件路由、重建、布局、
// damage、绘制缓存、DPI/IME 同步）收敛到 app::AppShell；本文件只保留
// 应用层职责：build 函数、状态 key 与业务 handler（plan §6.1）。
// 公共 API 保持迁移前签名——headless 集成测试与窗口循环共用。

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

#include "lumen/app/app_shell.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/render/renderer.h"
#include "lumen/text/font_manager.h"

namespace lumen::examples {

class CounterApp {
  public:
    CounterApp() : CounterApp(buildUi()) {}

    // Root from the text DSL (`--dsl counter.lumen`, plan §7).
    explicit CounterApp(core::Widget root) : shell_(makeConfig(std::move(root))) {
        initialize();
    }

    CounterApp(const CounterApp&) = delete;
    CounterApp& operator=(const CounterApp&) = delete;
    CounterApp(CounterApp&&) = delete;
    CounterApp& operator=(CounterApp&&) = delete;

    // The declarative UI (plan §6.1 example shape). Static so the DSL golden
    // test can compare it against the parsed `.lumen` document.
    [[nodiscard]] static core::Widget buildUi() {
        using namespace dsl;
        core::Widget page = container(
            column({core::withKey(dsl::text("Count: ", bind("counter")),
                                  "count-text"),
                    core::withKey(button("Increment", onClick("increment")),
                                  "increment-button"),
                    core::withKey(text_field(bind("name"), placeholder("Name")),
                                  "name-field")},
                   core::EdgeInsets::all(24.0F), 12.0F),
            core::Color::fromRGBA(24, 24, 27));
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
    // the rebuild diffs the trees so only changed nodes repaint.
    void swapRoot(core::Widget root) { shell_.swapRoot(std::move(root)); }

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
    [[nodiscard]] static app::AppShell makeConfig(core::Widget root) {
        app::ShellConfig config;
        config.build = [template_ = std::move(root)]() mutable {
            return template_;
        };
        return app::AppShell{std::move(config)};
    }

    // 状态 key 与业务 handler（应用层唯一职责，plan §6.1）。
    void initialize() {
        core::StateStore& state = shell_.state();
        state.set("counter", "0");
        state.set("name", "");
        shell_.handlers()["increment"] = [this] {
            shell_.state().set("counter", std::to_string(counterValue() + 1));
        };
    }

    app::AppShell shell_;
};

}  // namespace lumen::examples
