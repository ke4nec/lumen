// Widget Gallery 集成测试（与 settings_integration_tests 同风格）：
// 导航/计数、输入联动（下拉/页签/表单双路径）、弹窗统一规则、主题派生
// 保留（高对比 × 深浅/强调色）、虚拟列表物化窗口、滑杆联动与语义覆盖。

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <string>

#include "gallery_app.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/render/frame_scheduler.h"

using namespace lumen;
using namespace lumen::core;
using namespace lumen::examples;

namespace {

Offset centerOf(const RenderNode& root, const std::string& key) {
    const RenderNode* node = findNodeByKey(root, key);
    REQUIRE(node != nullptr);
    return absoluteOffset(root, key) +
           Offset{node->size.width * 0.5F, node->size.height * 0.5F};
}

void click(GalleryApp& app, const std::string& key) {
    const Offset point = centerOf(app.root(), key);
    app.pointerDown(point);
    app.pointerUp(point);
}

// 内容节点点击：滚入主列表视口后再命中（被裁剪节点不命中）。
void clickVisible(GalleryApp& app, const std::string& key) {
    for (int i = 0; i < 16; ++i) {
        (void)app.renderFrame();
        const RenderNode* node = findNodeByKey(app.root(), key);
        const RenderNode* list = findNodeByKey(app.root(), "gallery-list");
        REQUIRE(node != nullptr);
        REQUIRE(list != nullptr);
        const Offset nodeOff = absoluteOffset(app.root(), key);
        const Offset listOff = absoluteOffset(app.root(), "gallery-list");
        const float top = listOff.y;
        const float bottom = listOff.y + list->size.height;
        if (node->size.height > 0.0F && nodeOff.y >= top &&
            nodeOff.y + node->size.height <= bottom) {
            const Offset point =
                nodeOff + Offset{node->size.width * 0.5F,
                                 node->size.height * 0.5F};
            app.pointerDown(point);
            app.pointerUp(point);
            return;
        }
        const float deltaY = nodeOff.y < top ? nodeOff.y - top - 8.0F
                                             : nodeOff.y + node->size.height -
                                                   bottom + 8.0F;
        app.wheel(centerOf(app.root(), "gallery-list"),
                  Offset{0.0F, deltaY});
    }
    FAIL("node '" << key << "' never became visible");
}

void go(GalleryApp& app, const std::string& navKey) {
    click(app, navKey);
    (void)app.renderFrame();
}

// M11：点击 overlay 菜单选项（坐标相对 overlay 根 = 窗口坐标）。
void clickOverlayOption(GalleryApp& app, const std::string& key) {
    const RenderNode* overlay = app.shell().overlayRoot();
    REQUIRE(overlay != nullptr);
    const RenderNode* node = findNodeByKey(*overlay, key);
    REQUIRE(node != nullptr);
    const Offset point = absoluteOffset(*overlay, key) +
        Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    app.pointerDown(point);
    app.pointerUp(point);
}

}  // namespace

TEST_CASE("gallery_hover_animation_partial_frames_match_full_repaint", "[gallery]") {
    for (const float scale : {1.0F, 1.5F, 2.0F}) {
        CAPTURE(scale);
        GalleryApp app;
        GalleryApp reference;
        app.setView({1024, 768});
        app.setDeviceScale(scale);
        reference.setView({1024, 768});
        reference.setDeviceScale(scale);
        std::uint64_t now = 1000;
        app.shell().tick(now);
        reference.shell().tick(now);
        (void)app.renderFrame();
        (void)reference.renderFrame(true);
        for (const char* key : {"nav-buttons", "show-dialog-button", "nav-inputs"}) {
            CAPTURE(key);
            const auto* node = findNodeByKey(app.root(), key);
            const auto origin = absoluteOffset(app.root(), key);
            INFO("node x=" << origin.x << " y=" << origin.y
                 << " w=" << node->size.width << " h=" << node->size.height);
            app.shell().pointerMove(centerOf(app.root(), key));
            reference.pointerMove(centerOf(reference.root(), key));
            for (int i = 0; i < 3; ++i) {
                CAPTURE(i);
                now += 40;
                app.shell().tick(now);
                reference.shell().tick(now);
                const auto count = app.shell().partialRepaintCount();
                const auto partialHash = app.renderFrame();
                REQUIRE(app.shell().partialRepaintCount() > count);
                const auto partial = app.pixels();
                // Keep the tested renderer on consecutive partial frames;
                // a full repaint here would repair and hide stale pixels.
                const auto fullHash = reference.renderFrame(true);
                if (partialHash != fullHash) {
                    const auto first = std::mismatch(partial.rgba.begin(),
                        partial.rgba.end(), reference.pixels().rgba.begin()).first;
                    const auto byte = first - partial.rgba.begin();
                    INFO("first mismatch x=" << (byte / 4) % partial.width
                         << " y=" << (byte / 4) / partial.width
                         << " channel=" << byte % 4
                         << " partial=" << int(*first)
                         << " full=" << int(reference.pixels().rgba[byte]));
                    REQUIRE(partialHash == fullHash);
                }
                REQUIRE(partialHash == fullHash);
            }
        }
    }
}

TEST_CASE("gallery_navigation_and_button_counter", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    CHECK(app.navigator().current() == "home");

    // Home 网格进入 Buttons。
    click(app, "goto-buttons-button");
    (void)app.renderFrame();
    CHECK(app.navigator().current() == "buttons");

    clickVisible(app, "btn-variant-Filled");
    CHECK(app.state().get("button-clicks") == "1");
    clickVisible(app, "btn-size-Large");
    CHECK(app.state().get("button-clicks") == "2");
    (void)app.renderFrame();

    // Back 按钮返回 home（"back" handler 覆盖）。
    clickVisible(app, "back-button");
    (void)app.renderFrame();
    CHECK(app.navigator().current() == "home");
}

TEST_CASE("gallery_buttons_transition_finishes_without_more_pointer_events",
          "[gallery][motion]") {
    const auto paintMs = GENERATE(7U, 250U);
    CAPTURE(paintMs);
    struct Clock final : render::FrameClock {
        std::uint64_t time{0};
        std::uint64_t nowMs() const override { return time; }
    } clock;
    render::FrameScheduler scheduler{{}, &clock};
    GalleryApp app;
    app.setView({1024, 768});
    app.shell().tick(0);
    (void)app.renderFrame();
    scheduler.markFrameSubmitted();

    clock.time = 20;
    click(app, "nav-buttons");
    scheduler.requestFrame(render::FrameReason::Input);
    // Mirror runApp: tick can run several times between scheduled frames;
    // rendering/presentation consumes time before markFrameSubmitted.
    for (; clock.time < 600; ++clock.time) {
        app.shell().tick(clock.time);
        scheduler.setAnimationsActive(app.shell().animationsActive());
        if (scheduler.shouldSubmitFrame()) {
            (void)app.renderFrame();
            scheduler.setAnimationsActive(app.shell().animationsActive());
            clock.time += paintMs;
            scheduler.markFrameSubmitted();
        }
    }
    REQUIRE(app.navigator().current() == "buttons");
    const auto* page = findNodeByKey(app.root(), "gallery-list");
    REQUIRE(page != nullptr);
    CHECK(page->transitionAlpha == 1.0F);
    CHECK_FALSE(app.shell().hasActiveTransitions());
    CHECK_FALSE(app.shell().animationsActive());
    const auto lastFrame = app.renderFrame();
    CHECK(lastFrame == app.renderFrame(true));
}

TEST_CASE("gallery_route_terminal_frame_repaints_after_pointer_rebuild",
          "[gallery][motion]") {
    const bool paintIntermediate = GENERATE(false, true);
    CAPTURE(paintIntermediate);
    GalleryApp app;
    app.setView({1024, 768});
    app.shell().tick(1000);
    (void)app.renderFrame();
    app.shell().tick(1020);
    click(app, "nav-buttons");
    (void)app.renderFrame();
    REQUIRE(findNodeByKey(app.root(), "gallery-list")->transitionAlpha == 0.0F);
    const auto duration = app.theme().motion.navigatorTransitionMs;
    if (paintIntermediate) {
        app.shell().tick(1020 + duration / 2);
        (void)app.renderFrame();
    }

    // Moving the pointer rebuilds the tree with its default alpha of 1 on the
    // same frame that the route fade finishes. Damage must include the entire
    // page that is still transparent in the last submitted framebuffer.
    app.shell().tick(1020 + duration + 1);
    app.pointerMove(centerOf(app.root(), "nav-inputs"));
    const auto partial = app.renderFrame();
    CHECK(findNodeByKey(app.root(), "gallery-list")->transitionAlpha == 1.0F);
    CHECK(partial == app.renderFrame(true));
}

TEST_CASE("gallery_state_blend_terminal_frame_repaints_after_state_rebuild",
          "[gallery][motion]") {
    GalleryApp app;
    app.setView({1024, 768});
    app.shell().tick(1000);
    (void)app.renderFrame();
    app.pointerMove(centerOf(app.root(), "nav-buttons"));
    (void)app.renderFrame();
    const auto duration = app.theme().motion.stateTransitionMs;
    app.shell().tick(1000 + duration / 2);
    (void)app.renderFrame();

    // An unrelated live-state label rebuilds the tree while the hover color
    // reaches its target. The button's last painted intermediate color still
    // needs damage even though the layout target already has the final color.
    app.shell().state().set("button-clicks", "1");
    app.shell().markDirty();
    app.shell().tick(1000 + duration + 1);
    const auto partial = app.renderFrame();
    CHECK(partial == app.renderFrame(true));
}

TEST_CASE("gallery_inputs_dropdown_tabs_and_form", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-inputs");
    CHECK(app.navigator().current() == "inputs");

    clickVisible(app, "username-field");
    app.textInput("Lumen");
    CHECK(app.state().get("username") == "Lumen");
    clickVisible(app, "autosave-checkbox");
    CHECK(app.state().get("autosave") == "true");

    // M11：下拉浮动菜单——值行点击打开 overlay（barrier+锚定菜单）。
    REQUIRE_FALSE(app.dropdownOpen());
    clickVisible(app, "color-dropdown");
    (void)app.renderFrame();
    REQUIRE(app.dropdownOpen());
    REQUIRE(app.shell().overlayRoot() != nullptr);
    // 选项在 overlay 树中（主树不含选项子树）。
    CHECK(findNodeByKey(*app.shell().overlayRoot(),
                        "color-dropdown-opt-1") != nullptr);
    clickOverlayOption(app, "color-dropdown-opt-1");  // Green
    (void)app.renderFrame();
    CHECK(app.state().get("color") == "Green");
    CHECK_FALSE(app.dropdownOpen());
    CHECK(app.shell().overlayRoot() == nullptr);

    // 键盘路径：值行聚焦 + Enter 打开 → Down/Enter 选中（与指针同
    // handler 路径）。
    clickVisible(app, "color-dropdown");
    (void)app.renderFrame();
    REQUIRE(app.dropdownOpen());
    app.keyDown(Key::Down);
    app.keyDown(Key::Enter);
    (void)app.renderFrame();
    CHECK_FALSE(app.dropdownOpen());
    CHECK(app.state().get("color") == "Blue");  // Red→Down→Blue

    // Esc 关闭不选值。
    clickVisible(app, "color-dropdown");
    (void)app.renderFrame();
    REQUIRE(app.dropdownOpen());
    app.keyDown(Key::Escape);
    (void)app.renderFrame();
    CHECK_FALSE(app.dropdownOpen());
    CHECK(app.state().get("color") == "Blue");

    clickVisible(app, "tab-More");
    (void)app.renderFrame();
    CHECK(app.state().get("gallery-tab") == "More");

    // 空提交：校验失败路径（错误节点出现，不弹窗）。
    clickVisible(app, "save-button");
    (void)app.renderFrame();
    CHECK(app.form().errors().size() == 2);
    CHECK(findNodeByKey(app.root(), "nickname-error") != nullptr);
    CHECK_FALSE(app.dialogOpen());

    // 有效提交：弹窗打开，关闭后回到页面。
    clickVisible(app, "nickname-field");
    app.textInput("Lumen");
    clickVisible(app, "email-field");
    app.textInput("dev@lumen.local");
    clickVisible(app, "save-button");
    (void)app.renderFrame();
    CHECK(app.dialogOpen());
    CHECK(findNodeByKey(app.root(), "gallery-dialog") != nullptr);
    click(app, "dialog-close");
    (void)app.renderFrame();
    CHECK_FALSE(app.dialogOpen());
}

TEST_CASE("gallery_dialog_escape_keeps_route", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();

    // 弹窗优先于路由：Escape 关闭弹窗，路由不变。
    click(app, "show-dialog-button");
    (void)app.renderFrame();
    REQUIRE(app.dialogOpen());
    app.keyDown(Key::Escape);
    (void)app.renderFrame();
    CHECK_FALSE(app.dialogOpen());
    CHECK(app.navigator().current() == "home");
}

TEST_CASE("gallery_direction_switch_preserves_derivation", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-theme");

    // 方向切换保留高对比/密度；方向内深浅切换保留方向（M11）。
    clickVisible(app, "toggle-contrast-button");
    (void)app.renderFrame();
    REQUIRE(app.accessibilitySettings().highContrast);

    clickVisible(app, "direction-ink-button");
    (void)app.renderFrame();
    CHECK(app.direction() == style::ThemeDirection::InkLinen);
    CHECK(app.theme().direction == style::ThemeDirection::InkLinen);
    CHECK(app.accessibilitySettings().highContrast);
    CHECK(app.theme().metrics.controlRadius[1] == 8.0F);

    clickVisible(app, "toggle-dark-button-theme");
    (void)app.renderFrame();
    CHECK(app.direction() == style::ThemeDirection::InkLinen);
    CHECK(app.accessibilitySettings().highContrast);

    clickVisible(app, "direction-aurora-button");
    (void)app.renderFrame();
    CHECK(app.direction() == style::ThemeDirection::AuroraSignal);
    CHECK(app.accessibilitySettings().highContrast);
    CHECK(app.renderFrame() != 0);

    clickVisible(app, "direction-core-button");
    (void)app.renderFrame();
    CHECK(app.direction() == style::ThemeDirection::CoreDark);
    CHECK(app.renderFrame() != 0);
}

TEST_CASE("gallery_theme_derivation_preserved", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-theme");

    // 高对比开启后，深浅切换与强调色切换都不得丢弃派生。
    clickVisible(app, "toggle-contrast-button");
    (void)app.renderFrame();
    REQUIRE(app.accessibilitySettings().highContrast);

    clickVisible(app, "accent-green-button");
    (void)app.renderFrame();
    CHECK(app.theme().colors.accent.g > app.theme().colors.accent.r);
    CHECK(app.theme().colors.accent.g > app.theme().colors.accent.b);
    CHECK(app.theme().button.filled.background == app.theme().colors.accent);
    CHECK(app.accessibilitySettings().highContrast);

    clickVisible(app, "toggle-dark-button-theme");
    (void)app.renderFrame();
    CHECK(app.accessibilitySettings().highContrast);

    clickVisible(app, "cycle-density-button");
    (void)app.renderFrame();
    CHECK(app.accessibilitySettings().highContrast);
    CHECK(app.renderFrame() != 0);
}

TEST_CASE("gallery_lists_materialize_visible_window", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-lists");

    // ScrollView 视口裁剪且内容可滚。
    const RenderNode* scrollView =
        findNodeByKey(app.root(), "gallery-scrollview");
    REQUIRE(scrollView != nullptr);
    CHECK(scrollView->clipContent);

    // 千项虚拟列表只物化可见窗口。
    app.wheel(centerOf(app.root(), "gallery-library"), Offset{0.0F, 4000.0F});
    (void)app.renderFrame();
    const RenderNode* library =
        findNodeByKey(app.root(), "gallery-library");
    REQUIRE(library != nullptr);
    CHECK(library->scrollExtent > 0.0F);
    CHECK(!library->children.empty());
    CHECK(library->children.size() < 1000);
}

TEST_CASE("gallery_slider_drives_progress", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-feedback");

    // 滑杆中心点击 → 50；进度条同 bind 联动。
    clickVisible(app, "progress-slider");
    (void)app.renderFrame();
    CHECK(app.state().get("demo-progress") == "50");
}

TEST_CASE("gallery_semantics_covers_controls", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();

    const auto tree = app.semantics();
    CHECK(tree.size() > 0);
    const RenderNode* button =
        findNodeByKey(app.root(), "goto-buttons-button");
    REQUIRE(button != nullptr);
    const auto* node = tree.find(button->identity);
    REQUIRE(node != nullptr);
    CHECK(node->role == accessibility::SemanticsRole::Button);

    go(app, "nav-inputs");
    const auto inputsTree = app.semantics();
    const RenderNode* checkbox =
        findNodeByKey(app.root(), "autosave-checkbox");
    REQUIRE(checkbox != nullptr);
    const auto* checkNode = inputsTree.find(checkbox->identity);
    REQUIRE(checkNode != nullptr);
    CHECK(checkNode->role == accessibility::SemanticsRole::Checkbox);
    CHECK(checkNode->label == "Autosave drafts");
}

// M12：跟随系统主题——开关启用后按偏好重派生（方向/对比保留）。
TEST_CASE("gallery_follow_system_theme_rederives", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-theme");

    // 开启跟随：偏好为浅色（默认 false）→ 立即重派生为浅色。
    clickVisible(app, "follow-system-button");
    (void)app.renderFrame();
    CHECK(app.followSystemTheme());
    CHECK_FALSE(app.darkMode());

    // 系统切深色：事件注入 → 重派生回深色。
    app.setSystemThemePreference(true);
    (void)app.renderFrame();
    CHECK(app.darkMode());

    // 关闭跟随后偏好变化不再影响主题。
    clickVisible(app, "follow-system-button");
    (void)app.renderFrame();
    CHECK_FALSE(app.followSystemTheme());
    const auto pageBefore = app.theme().colors.pageBackground;
    app.setSystemThemePreference(false);
    (void)app.renderFrame();
    CHECK(app.theme().colors.pageBackground == pageBefore);
}

// --- S5（gui-control-visual-system-task §10.2）：强制状态矩阵样本 ---

TEST_CASE("gallery_buttons_state_matrix_forces_previews", "[gallery]") {
    GalleryApp app;
    (void)app.renderFrame();
    click(app, "goto-buttons-button");
    (void)app.renderFrame();

    const style::Theme& theme = app.theme();
    // 矩阵落地：五变体 × 六列。
    const RenderNode* matrix = findNodeByKey(app.root(), "buttons-matrix");
    REQUIRE(matrix != nullptr);
    REQUIRE(matrix->children.size() == 5);  // 每个变体内按可用宽度换行
    for (const char* variant : {"Filled", "Tonal", "Outline", "Ghost",
                                "Danger"}) {
        REQUIRE(findNodeByKey(app.root(),
                              std::string("matrix-") + variant + "-Normal") !=
                nullptr);
    }

    // Hover 快照 = blendOver(accent, hoverOverlay)（不触发业务回调的
    // 预览：单元格 disabled，颜色来自 resolved 覆盖）。
    const RenderNode* hover =
        findNodeByKey(app.root(), "matrix-Filled-Hover");
    REQUIRE(hover != nullptr);
    CHECK(hover->commonStyle().background ==
          style::blendOver(theme.colors.accent, theme.colors.hoverOverlay));
    CHECK_FALSE(hover->enabled);

    // Press 快照只有 pressed 叠加（不累计 hover，§5.2）。
    const RenderNode* press =
        findNodeByKey(app.root(), "matrix-Filled-Press");
    REQUIRE(press != nullptr);
    CHECK(press->commonStyle().background ==
          style::blendOver(theme.colors.accent, theme.colors.pressedOverlay));

    // Focus uses the real ring channel; the filled button border remains transparent.
    const RenderNode* focus =
        findNodeByKey(app.root(), "matrix-Filled-Focus");
    REQUIRE(focus != nullptr);
    CHECK(focus->commonStyle().focusRing == theme.colors.focusRing);
    CHECK(focus->commonStyle().focusWidth == theme.metrics.focusRingWidth);
    const RenderNode* focusPress =
        findNodeByKey(app.root(), "matrix-Filled-Foc+Prs");
    REQUIRE(focusPress != nullptr);
    CHECK(focusPress->commonStyle().focusRing == theme.colors.focusRing);
    CHECK(focusPress->commonStyle().focusWidth == theme.metrics.focusRingWidth);
    CHECK(focusPress->commonStyle().background ==
          style::blendOver(theme.colors.accent, theme.colors.pressedOverlay));

    // Disabled 列为真实禁用控件（非覆盖近似）。
    const RenderNode* disabled =
        findNodeByKey(app.root(), "matrix-Filled-Disabled");
    REQUIRE(disabled != nullptr);
    CHECK_FALSE(disabled->enabled);
    CHECK(disabled->commonStyle().background ==
          theme.colors.disabledBackground);

    // 长标签样本存在且宽度受限（单行省略路径）。
    const RenderNode* longLabel =
        findNodeByKey(app.root(), "btn-long-label");
    REQUIRE(longLabel != nullptr);
    CHECK(longLabel->size.width <= 200.0F + 0.01F);
}
