// Widget Gallery 集成测试（与 settings_integration_tests 同风格）：
// 导航/计数、输入联动（下拉/页签/表单双路径）、弹窗统一规则、主题派生
// 保留（高对比 × 深浅/强调色）、虚拟列表物化窗口、滑杆联动与语义覆盖。

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "gallery_app.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/interaction.h"
#include "lumen/core/state.h"

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
    CHECK(app.theme().colors.accent == Color::fromRGBA(74, 160, 106));
    CHECK(app.theme().button.filled.background ==
          Color::fromRGBA(74, 160, 106));
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
