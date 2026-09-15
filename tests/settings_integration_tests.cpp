// v0.3 阶段8D (plan §4 8D / §5.1): 应用基础组件与 settings 集成测试。
//
// 覆盖：滚动视口约束（内容主轴不限 + clip）、ScrollController（滚轮/
// 键盘/拖动/语义 + clamp）、intrinsic/baseline、Checkbox/Switch 点击切换
// 与语义、FocusScope 域内遍历、局部重绘与 forced full repaint 像素一致、
// 列表 key 复用、表单校验、弹窗 barrier/Escape 统一规则、导航返回与
// 状态保持、Theme 切换、DSL 新节点解析、320px 窄窗口可用性。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lumen/accessibility/semantics.h"
#include "lumen/core/interaction.h"
#include "lumen/core/scroll.h"
#include "lumen/core/state.h"
#include "lumen/dsl/text_dsl.h"
#include "lumen/layout/layout.h"
#include "settings_app.h"

using namespace lumen;
using namespace lumen::core;
using namespace lumen::layout;
using namespace lumen::examples;

namespace {

RenderNode layoutOf(const Widget& widget, float width = 400.0F,
                    float height = 300.0F) {
    return LayoutEngine::layout(
        widget, Constraints::tight(Size{width, height}));
}

Offset centerOf(const RenderNode& root, const std::string& key) {
    const RenderNode* node = findNodeByKey(root, key);
    REQUIRE(node != nullptr);
    return absoluteOffset(root, key) +
           Offset{node->size.width * 0.5F, node->size.height * 0.5F};
}

}  // namespace

// --- 滚动视口布局 ---

TEST_CASE("scroll_viewport_layout_and_clipping", "[widgets][layout]") {
    // 内容高于视口：视口高度固定，子内容按 scrollOffset 上移。外层用 loose
    std::vector<Widget> rows;
    // 约束让显式高度生效（tight 父约束会覆盖显式尺寸）。
    for (int i = 0; i < 20; ++i) {
        rows.push_back(withKey(makeText("row"), "row-" + std::to_string(i)));
    }
    Widget ui = makeContainer(
        withScrollOffset(makeScrollView(makeColumn(std::move(rows)), "sv"),
                         50.0F),
        300.0F, 200.0F);
    const RenderNode root = LayoutEngine::layout(
        ui, Constraints::loose(Size{400.0F, 300.0F}));

    const RenderNode* viewport = findNodeByKey(root, "sv");
    REQUIRE(viewport != nullptr);
    CHECK(viewport->type == WidgetType::ScrollView);
    CHECK(viewport->size.height == 200.0F);
    CHECK(viewport->clipContent);
    CHECK(viewport->scrollExtent > 0.0F);
    CHECK(viewport->scrollOffset == 50.0F);
    // 子内容第一个 row 相对视口上移 50px（减去 Column 自身）。
    const RenderNode* firstRow = findNodeByKey(root, "row-0");
    REQUIRE(firstRow != nullptr);
    const float rowTop = absoluteOffset(root, "row-0").y;
    const float viewportTop = absoluteOffset(root, "sv").y;
    // 未滚时 row 紧贴视口顶部内容区；滚 50 后 row 顶部在视口上方。
    CHECK(rowTop < viewportTop);
}

TEST_CASE("scroll_offset_clamps_to_extent", "[widgets][layout]") {
    std::vector<Widget> rows;
    for (int i = 0; i < 20; ++i) {
        rows.push_back(withKey(makeText("row"), "row"));
    }
    Widget ui = makeContainer(
        withScrollOffset(makeScrollView(makeColumn(std::move(rows)), "sv"),
                         100000.0F),
        300.0F, 200.0F);
    const RenderNode root = layoutOf(ui);
    const RenderNode* viewport = findNodeByKey(root, "sv");
    REQUIRE(viewport != nullptr);
    CHECK(viewport->scrollOffset == viewport->scrollExtent);
}

TEST_CASE("scroll_controller_wheel_keyboard_drag_and_semantics",
          "[widgets]") {
    ScrollController scroll;
    scroll.updateExtents(200.0F, 500.0F);
    CHECK(scroll.maxScrollOffset() == 300.0F);
    CHECK(scroll.visibleFraction() == Catch::Approx(0.4F));

    CHECK(scroll.applyWheel(80.0F));
    CHECK(scroll.offset() == 80.0F);
    // 拖动内容跟随手指：手指下移 → 内容滚回。
    CHECK(scroll.applyDrag(30.0F));
    CHECK(scroll.offset() == 50.0F);
    // 键盘翻页。
    CHECK(scroll.applyKey(Key::PageDown, 200.0F));
    CHECK(scroll.offset() == 230.0F);
    CHECK(scroll.applyKey(Key::End, 200.0F));
    CHECK(scroll.offset() == 300.0F);
    CHECK_FALSE(scroll.applyKey(Key::PageDown, 200.0F));  // 已到底。
    CHECK(scroll.applyKey(Key::Home, 200.0F));
    CHECK(scroll.offset() == 0.0F);
    // 语义滚动（AT 上滚 = 内容向上）。
    CHECK(scroll.semanticScroll(-60.0F));
    CHECK(scroll.offset() == 60.0F);
}

// --- intrinsic / baseline ---

TEST_CASE("layout_engine_intrinsic_size_and_baseline", "[widgets][layout]") {
    const Size intrinsic =
        LayoutEngine::intrinsicSize(makeText("hello"), Constraints::unbounded());
    CHECK(intrinsic.width == 14.0F * 0.6F * 5.0F);
    // S1：body 行高倍数 20/14（§4.5）。
    CHECK(intrinsic.height == 20.0F);

    // Column 内第一个文本提供 baseline。
    Widget ui = makeColumn({
        withKey(makeText("first"), "t1"),
        withKey(makeButton("OK"), "b1"),
    });
    const RenderNode root = layoutOf(ui);
    const float baseline = LayoutEngine::baseline(root);
    CHECK(baseline > 0.0F);
    // 无文本的树返回 -1。
    const RenderNode bare = layoutOf(makeContainerLeaf(10.0F, 10.0F));
    CHECK(LayoutEngine::baseline(bare) == -1.0F);
}

// --- Checkbox / Switch ---

TEST_CASE("checkbox_and_switch_toggle_on_click_and_keyboard", "[widgets]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("autosave", "false");
    store.set("notifications", "true");

    Widget ui = makeColumn({
        withKey(makeCheckbox("Autosave", "autosave", "auto-box"), "auto-box"),
        withKey(makeSwitch("Notifications", "notifications", "notif-switch"),
                "notif-switch"),
    });
    const RenderNode root = layoutOf(ui);

    controller.pointerDown(root, centerOf(root, "auto-box"));
    controller.pointerUp(root, centerOf(root, "auto-box"));
    CHECK(store.get("autosave") == "true");

    controller.pointerDown(root, centerOf(root, "notif-switch"));
    controller.pointerUp(root, centerOf(root, "notif-switch"));
    CHECK(store.get("notifications") == "false");

    // 键盘：Tab 聚焦 checkbox，Space/Enter 切换。
    controller.keyDown(root, Key::Tab);
    controller.keyDown(root, Key::Enter);
    CHECK(store.get("autosave") == "false");
}

TEST_CASE("checkbox_state_resolves_from_bind", "[widgets]") {
    StateStore store;
    Widget ui = makeContainer(
        withKey(makeCheckbox("Autosave", "autosave", "c"), "c"));
    store.set("autosave", "true");
    applyBinds(ui, store);
    REQUIRE(ui.children.size() == 1);
    CHECK(ui.children[0].checked);
}

// --- FocusScope 域内遍历 ---

TEST_CASE("focus_scope_keeps_tab_traversal_inside", "[widgets]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("a", "");
    store.set("d1", "");
    store.set("d2", "");

    // 根域一个字段；FocusScope 内两个按钮。
    Widget ui = makeColumn({
        withKey(makeTextField("", {}, {}, {}, 0.0F, "outside"), "outside"),
        makeFocusScope(
            makeColumn({
                withKey(makeButton("A", {}, {}, 0.0F, "in-a", std::nullopt,
                                   std::nullopt, "h"),
                        "in-a"),
                withKey(makeButton("B", {}, {}, 0.0F, "in-b", std::nullopt,
                                   std::nullopt, "h"),
                        "in-b"),
            }),
            "scope"),
    });
    ui.children[0].bind = "a";
    const RenderNode root = layoutOf(ui);

    // 聚焦域内按钮 A：Tab 只在域内循环 A → B → A。
    controller.focusNode(*findNodeByKey(root, "in-a"));
    controller.keyDown(root, Key::Tab);
    CHECK(focus.focusedKey() == "in-b");
    controller.keyDown(root, Key::Tab);
    CHECK(focus.focusedKey() == "in-a");
    // 域内最后一个再 Tab 不逃逸到 outside。
    controller.keyDown(root, Key::Tab);
    CHECK(focus.focusedKey() == "in-b");
}

// --- DSL 新节点 ---

TEST_CASE("dsl_parses_stage8d_widgets", "[widgets][dsl]") {
    const auto parsed = lumen::dsl::parseLumen(
        "page root {\n"
        "  ScrollView(key: \"sv\", scrollOffset: 40) {\n"
        "    Column {\n"
        "      Checkbox(\"Autosave\", bind: autosave, checked: true)\n"
        "      Switch(\"Notify\", bind: notify)\n"
        "    }\n"
        "  }\n"
        "}");
    REQUIRE(parsed.ok());
    const auto& scrollNode = parsed.root;
    CHECK(scrollNode.type == WidgetType::ScrollView);
    CHECK(scrollNode.scrollOffset == 40.0F);
    REQUIRE(scrollNode.children.size() == 1);
    const auto& columnNode = scrollNode.children[0];
    REQUIRE(columnNode.children.size() == 2);
    CHECK(columnNode.children[0].type == WidgetType::Checkbox);
    CHECK(columnNode.children[0].text == "Autosave");
    CHECK(columnNode.children[0].bind == "autosave");
    CHECK(columnNode.children[0].checked);
    CHECK(columnNode.children[1].type == WidgetType::Switch);
    // 未知/错位属性被拒绝。
    CHECK_FALSE(lumen::dsl::parseLumen("page root { Text(\"x\", checked: true) }").ok());
    CHECK_FALSE(
        lumen::dsl::parseLumen("page root { Column(scrollOffset: 5) {} }").ok());
}

// --- settings 集成（出口条件） ---

TEST_CASE("settings_narrow_window_stays_usable", "[settings]") {
    SettingsApp app;
    app.setView(Size{320.0F, 480.0F});
    const auto hash = app.renderFrame();
    CHECK(hash != 0);
    // 控件落在窗口内且可命中。
    const RenderNode* list = findNodeByKey(app.root(), "settings-list");
    REQUIRE(list != nullptr);
    CHECK(list->size.width <= 320.0F);
    CHECK(list->size.height <= 480.0F);
    const RenderNode* button = findNodeByKey(app.root(), "goto-form-button");
    REQUIRE(button != nullptr);
    CHECK(button->size.width >= 64.0F);  // 最小可点击目标
}

TEST_CASE("settings_scroll_partial_repaint_matches_full", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    app.wheel(centerOf(app.root(), "settings-list"), Offset{0.0F, 200.0F});
    CHECK(app.scroll().offset() == 200.0F);
    const auto partial = app.renderFrame();
    // 滚动后强制全帧重绘：与局部重绘像素一致（plan 8D 出口条件）。
    const auto full = app.renderFrame(/*forceFullRepaint=*/true);
    CHECK(partial == full);
    CHECK(app.partialRepaintCount() > 0);

    // 滚动上限 clamp。
    app.wheel(centerOf(app.root(), "settings-list"), Offset{0.0F, 1e6F});
    app.renderFrame();
    const RenderNode* list = findNodeByKey(app.root(), "settings-list");
    REQUIRE(list != nullptr);
    CHECK(app.scroll().offset() <= list->scrollExtent);
    CHECK(app.scroll().offset() == app.scroll().maxScrollOffset());
}

TEST_CASE("settings_keyboard_scrolls_list", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();
    // 无编辑焦点时 PageDown 触发键盘滚动（wheel sink → ScrollController）。
    app.keyDown(Key::PageDown);
    CHECK(app.scroll().offset() > 0.0F);
}

TEST_CASE("settings_list_keys_reuse_across_rebuilds", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();
    const std::string identityBefore =
        findNodeByKey(app.root(), "about-5")->identity;

    // 切换状态触发重建：keyed 列表项 identity 稳定（plan §3.4）。
    const_cast<StateStore&>(app.state()).set("notifications", "false");
    (void)app.renderFrame();
    const RenderNode* item = findNodeByKey(app.root(), "about-5");
    REQUIRE(item != nullptr);
    CHECK(item->identity == identityBefore);
}

TEST_CASE("settings_form_validation_and_dialog_flow", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    // 进入表单。
    app.pointerDown(centerOf(app.root(), "goto-form-button"));
    app.pointerUp(centerOf(app.root(), "goto-form-button"));
    (void)app.renderFrame();
    CHECK(app.navigator().current() == "form");

    // 空字段提交：校验失败，错误渲染为文本节点。
    app.pointerDown(centerOf(app.root(), "save-button"));
    app.pointerUp(centerOf(app.root(), "save-button"));
    (void)app.renderFrame();
    CHECK(app.form().errors().size() == 2);
    CHECK(findNodeByKey(app.root(), "nickname-error") != nullptr);
    CHECK_FALSE(app.dialogOpen());

    // 填写后提交：弹窗打开。
    app.pointerDown(centerOf(app.root(), "nickname-field"));
    app.pointerUp(centerOf(app.root(), "nickname-field"));
    app.textInput("Lumen");
    app.pointerDown(centerOf(app.root(), "email-field"));
    app.pointerUp(centerOf(app.root(), "email-field"));
    app.textInput("dev@lumen.local");
    app.pointerDown(centerOf(app.root(), "save-button"));
    app.pointerUp(centerOf(app.root(), "save-button"));
    (void)app.renderFrame();
    CHECK(app.dialogOpen());
    CHECK(findNodeByKey(app.root(), "saved-dialog") != nullptr);
    CHECK(app.form().errors().empty());

    // 弹窗内 Tab 不逃逸（FocusScope）：域内 Close 按钮。
    app.keyDown(Key::Tab);
    app.keyDown(Key::Enter);
    CHECK_FALSE(app.dialogOpen());
}

TEST_CASE("settings_escape_rules_modal_before_route", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    app.pointerDown(centerOf(app.root(), "goto-form-button"));
    app.pointerUp(centerOf(app.root(), "goto-form-button"));
    (void)app.renderFrame();

    // 表单内 Escape：pop 路由回 home（统一返回规则，plan §3.4）。
    app.keyDown(Key::Escape);
    (void)app.renderFrame();
    CHECK(app.navigator().current() == "home");
    // home 是根路由：Escape 交给应用（不 pop）。
    CHECK_FALSE(app.navigator().handleBack(false));
}

TEST_CASE("settings_survives_continuous_resize", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    app.pointerDown(centerOf(app.root(), "goto-form-button"));
    app.pointerUp(centerOf(app.root(), "goto-form-button"));
    (void)app.renderFrame();

    app.pointerDown(centerOf(app.root(), "nickname-field"));
    app.pointerUp(centerOf(app.root(), "nickname-field"));
    app.textInput("kept");

    // 连续 resize：状态不丢（plan 8D 出口条件）。
    for (float width = 320.0F; width <= 1024.0F; width += 64.0F) {
        app.setView(Size{width, 480.0F});
        (void)app.renderFrame();
    }
    CHECK(app.state().get("nickname") == "kept");
    CHECK(app.navigator().current() == "form");
    const auto hash = app.renderFrame();
    CHECK(hash != 0);
}

TEST_CASE("settings_toggles_and_semantics", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    // Switch/Checkbox 点击切换 store。
    app.pointerDown(centerOf(app.root(), "notifications-switch"));
    app.pointerUp(centerOf(app.root(), "notifications-switch"));
    CHECK(app.state().get("notifications") == "false");
    app.pointerDown(centerOf(app.root(), "autosave-checkbox"));
    app.pointerUp(centerOf(app.root(), "autosave-checkbox"));
    CHECK(app.state().get("autosave") == "true");
    (void)app.renderFrame();

    // 语义树：list/checkbox/switch/dialog 角色与 scroll action。
    const auto tree = app.semantics();
    const RenderNode* listView = findNodeByKey(app.root(), "settings-list");
    REQUIRE(listView != nullptr);
    const auto* listNode = tree.find(listView->identity);
    REQUIRE(listNode != nullptr);
    CHECK(listNode->role == accessibility::SemanticsRole::List);
    CHECK((listNode->actions & accessibility::kActionScroll) != 0);

    const RenderNode* checkbox = findNodeByKey(app.root(), "autosave-checkbox");
    const auto* checkNode = tree.find(checkbox->identity);
    REQUIRE(checkNode != nullptr);
    CHECK(checkNode->role == accessibility::SemanticsRole::Checkbox);
    CHECK(checkNode->value == "true");
    CHECK((checkNode->flags & accessibility::kSemanticsChecked) != 0);
    CHECK(checkNode->label == "Autosave drafts");  // 语义覆盖生效

    const RenderNode* switchNode = findNodeByKey(app.root(), "notifications-switch");
    const auto* notif = tree.find(switchNode->identity);
    REQUIRE(notif != nullptr);
    CHECK(notif->role == accessibility::SemanticsRole::Switch);

    // 语义 scroll action：经 sink 路由到 ScrollController。
    accessibility::SemanticsActionContext context;
    context.root = &app.root();
    context.scrollSink = [&app](const std::string&, float, float dy) {
        return app.scrollWheelForTests(dy);
    };
    CHECK(accessibility::performSemanticsAction(
              tree, context, listView->identity, accessibility::kActionScroll,
              {}, 100.0F) == accessibility::SemanticsActionStatus::Handled);
    CHECK(app.scroll().offset() == 100.0F);
}

TEST_CASE("settings_theme_switch_changes_appearance", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();
    const auto darkHash = app.renderFrame();

    style::Theme light = style::Theme::light();
    app.setTheme(light);
    const auto lightHash = app.renderFrame();
    CHECK(darkHash != lightHash);
    CHECK(app.theme().colors.pageBackground == light.colors.pageBackground);
}

TEST_CASE("settings_theme_switch_damage_matches_full_repaint", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    // 不强制全量：主题切换只走 diff damage 的局部重绘（§10.2——damage
    // 必须同时覆盖旧颜色与新颜色区域，不残留上一帧控件外观）。
    app.setTheme(style::Theme::light(), /*forceFullRepaint=*/false);
    const auto partial = app.renderFrame();
    CHECK(app.partialRepaintCount() > 0);
    const auto full = app.renderFrame(/*forceFullRepaint=*/true);
    CHECK(partial == full);

    // 切回暗色同样成立。
    app.setTheme(style::Theme::dark(), /*forceFullRepaint=*/false);
    const auto backPartial = app.renderFrame();
    const auto backFull = app.renderFrame(/*forceFullRepaint=*/true);
    CHECK(backPartial == backFull);
}

TEST_CASE("settings_invalid_field_syncs_with_form_errors", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    app.pointerDown(centerOf(app.root(), "goto-form-button"));
    app.pointerUp(centerOf(app.root(), "goto-form-button"));
    (void)app.renderFrame();

    // 空字段提交：错误出现，字段携带 invalid 声明（§10.3）。
    app.pointerDown(centerOf(app.root(), "save-button"));
    app.pointerUp(centerOf(app.root(), "save-button"));
    (void)app.renderFrame();
    const RenderNode* field = findNodeByKey(app.root(), "nickname-field");
    REQUIRE(field != nullptr);
    CHECK(field->invalid);
    CHECK(findNodeByKey(app.root(), "nickname-error") != nullptr);

    // 填写后重新提交：校验通过，错误清除，invalid 复位（§10.3 与
    // FormController 错误信息同步——提交时重算）。
    app.pointerDown(centerOf(app.root(), "nickname-field"));
    app.pointerUp(centerOf(app.root(), "nickname-field"));
    app.textInput("Lumen");
    app.pointerDown(centerOf(app.root(), "email-field"));
    app.pointerUp(centerOf(app.root(), "email-field"));
    app.textInput("dev@lumen.local");
    app.pointerDown(centerOf(app.root(), "save-button"));
    app.pointerUp(centerOf(app.root(), "save-button"));
    (void)app.renderFrame();
    CHECK(app.form().errors().empty());
    field = findNodeByKey(app.root(), "nickname-field");
    REQUIRE(field != nullptr);
    CHECK_FALSE(field->invalid);
}

TEST_CASE("settings_theme_from_accessibility_settings", "[settings]") {
    accessibility::AccessibilitySettings settings;
    settings.fontScale = 1.5F;
    settings.highContrast = true;
    const style::Theme theme =
        style::Theme::fromSettings(settings, /*darkMode=*/true);
    CHECK(theme.typography.body.fontSize ==
          Catch::Approx(14.0F * 1.5F).margin(1e-4F));
    CHECK(theme.colors.contentPrimary == Color{255, 255, 255, 255});
}

TEST_CASE("settings_accessibility_derivation_relayouts_controls",
          "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();
    const float baseHeight =
        findNodeByKey(app.root(), "goto-form-button")->size.height;

    // font scale 1.5 + touch density：控件最小高度同步放大（§4 派生）。
    accessibility::AccessibilitySettings settings;
    settings.fontScale = 1.5F;
    app.setAccessibilitySettings(settings);
    (void)app.renderFrame();
    const float scaledHeight =
        findNodeByKey(app.root(), "goto-form-button")->size.height;
    CHECK(scaledHeight > baseHeight);
}

TEST_CASE("settings_theme_switch_keeps_state_and_selection", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    // 聚焦字段并输入文本。
    app.pointerDown(centerOf(app.root(), "goto-form-button"));
    app.pointerUp(centerOf(app.root(), "goto-form-button"));
    (void)app.renderFrame();
    app.pointerDown(centerOf(app.root(), "nickname-field"));
    app.pointerUp(centerOf(app.root(), "nickname-field"));
    app.textInput("kept");
    (void)app.renderFrame();

    // 主题切换：StateStore/Element/文本/滚动/路由不丢（§10.3）。
    const float scrollBefore = app.scroll().offset();
    app.setTheme(style::Theme::light());
    (void)app.renderFrame();
    CHECK(app.state().get("nickname") == "kept");
    CHECK(app.navigator().current() == "form");
    CHECK(app.scroll().offset() == scrollBefore);
    // 焦点与选区保留（编辑焦点仍在 nickname 字段）。
    CHECK(app.controller().wantsTextInput());
}

TEST_CASE("settings_accessibility_keeps_light_theme", "[settings]") {
    SettingsApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();
    app.setTheme(style::Theme::light());
    (void)app.renderFrame();

    accessibility::AccessibilitySettings settings;
    settings.fontScale = 1.25F;
    app.setAccessibilitySettings(settings);
    (void)app.renderFrame();

    const style::Theme expected =
        style::Theme::fromSettings(settings, /*darkMode=*/false);
    CHECK(app.theme().colors.pageBackground == expected.colors.pageBackground);
    CHECK(app.theme().typography.body.fontSize ==
          Catch::Approx(expected.typography.body.fontSize).margin(1e-4F));
}

TEST_CASE("settings_touch_density_and_high_contrast_pixel_stability",
          "[settings]") {
    SettingsApp app;
    app.setView(Size{320.0F, 480.0F});
    (void)app.renderFrame();

    // 触摸密度：列表仍可用（视口内、控件命中）。
    app.setTheme(style::Theme::dark(lumen::style::ControlDensity::Touch),
                 /*forceFullRepaint=*/false);
    const auto touchPartial = app.renderFrame();
    CHECK(touchPartial == app.renderFrame(true));
    const RenderNode* button = findNodeByKey(app.root(), "goto-form-button");
    REQUIRE(button != nullptr);
    CHECK(button->size.width <= 320.0F);
    CHECK(button->size.height >= 48.0F);  // Touch 档最小高度

    // 高对比度：像素稳定且局部重绘与全量一致。
    accessibility::AccessibilitySettings settings;
    settings.highContrast = true;
    app.setAccessibilitySettings(settings);
    const auto contrast = app.renderFrame();
    CHECK(contrast != 0);
    CHECK(contrast == app.renderFrame(true));
}

// M3：Grid 页与千项 VirtualList 页（可见区物化/滚动/焦点语义保持）。

TEST_CASE("settings_grid_and_virtual_list_pages", "[settings][m3]") {
    SettingsApp app;
    app.setView(core::Size{800.0F, 600.0F});
    (void)app.renderFrame();  // 首帧布局落地。

    // Grid 页：入口 → 18 格自适应列宽 → Escape 返回。
    REQUIRE(core::findNodeByKey(app.root(), "goto-grid-button") != nullptr);
    const auto gridCenter = [] (SettingsApp& a) {
        const auto* node = core::findNodeByKey(a.root(), "goto-grid-button");
        return core::absoluteOffset(a.root(), "goto-grid-button") +
               core::Offset{node->size.width * 0.5F,
                            node->size.height * 0.5F};
    };
    app.pointerDown(gridCenter(app));
    app.pointerUp(gridCenter(app));
    REQUIRE(app.navigator().current() == "grid");
    (void)app.renderFrame();
    const auto* grid = core::findNodeByKey(app.root(), "tile-grid");
    REQUIRE(grid != nullptr);
    CHECK(grid->children.size() == 18);
    CHECK(grid->children.front().size.width ==
          Catch::Approx(grid->children[1].size.width).margin(0.01F));
    app.keyDown(core::Key::Escape);
    CHECK(app.navigator().current() == "home");

    // Library 页：千项只物化可见窗口；滚动改变窗口；Escape 返回。
    (void)app.renderFrame();
    const auto libraryCenter = [](SettingsApp& a) {
        const auto* node =
            core::findNodeByKey(a.root(), "goto-library-button");
        return core::absoluteOffset(a.root(), "goto-library-button") +
               core::Offset{node->size.width * 0.5F,
                            node->size.height * 0.5F};
    };
    app.pointerDown(libraryCenter(app));
    app.pointerUp(libraryCenter(app));
    REQUIRE(app.navigator().current() == "library");
    (void)app.renderFrame();
    const auto* list = core::findNodeByKey(app.root(), "library-list");
    REQUIRE(list != nullptr);
    CHECK(list->children.size() < 60);   // 千项不全量构建。
    CHECK(list->children.size() >= 10);
    CHECK(list->scrollExtent > 10000.0F);
    // 首屏含 item-0；滚动后窗口移动且不构建全部。
    CHECK(core::findNodeByKey(app.root(), "item-0") != nullptr);

    const auto wheelAt = [&app](float deltaY) {
        const auto* node = core::findNodeByKey(app.root(), "library-list");
        const auto pos = core::absoluteOffset(app.root(), "library-list") +
                         core::Offset{node->size.width * 0.5F,
                                      node->size.height * 0.5F};
        app.wheel(pos, core::Offset{0.0F, deltaY});
    };
    wheelAt(4000.0F);
    (void)app.renderFrame();
    CHECK(core::findNodeByKey(app.root(), "item-0") == nullptr);
    CHECK(core::findNodeByKey(app.root(), "item-999") == nullptr);
    const std::size_t afterScroll =
        core::findNodeByKey(app.root(), "library-list")->children.size();
    CHECK(afterScroll < 60);

    app.keyDown(core::Key::Escape);
    CHECK(app.navigator().current() == "home");
}

// M4：平台服务区（文件选择/通知/外部链接；动作注入 + 状态展示）。

TEST_CASE("settings_services_page_actions_and_diagnostics", "[settings][m4]") {
    SettingsApp app;
    app.setView(core::Size{800.0F, 600.0F});

    // 无动作注入：按钮点击给出“服务不可用”诊断（不崩溃、不阻塞）。
    (void)app.renderFrame();
    const auto click = [&app](const char* key) {
        const auto* node = core::findNodeByKey(app.root(), key);
        REQUIRE(node != nullptr);
        const auto pos = core::absoluteOffset(app.root(), key) +
                         core::Offset{node->size.width * 0.5F,
                                      node->size.height * 0.5F};
        app.pointerDown(pos);
        app.pointerUp(pos);
    };
    click("open-file-button");
    (void)app.renderFrame();
    const auto* picked = core::findNodeByKey(app.root(), "picked-file");
    REQUIRE(picked != nullptr);
    CHECK(picked->text.find("unavailable") != std::string::npos);
    // 状态不丢：焦点/路由/导航仍在 home。
    CHECK(app.navigator().current() == "home");

    // 注入 fake 动作：打开成功 → 完成回调更新选中路径。
    SettingsApp::ServiceActions actions;
    actions.openFile = [&app] {
        app.setPickedFile("/tmp/fake-open.txt");
        return true;
    };
    actions.notify = [] { return false; };  // 失败路径 → 诊断。
    app.setServiceActions(std::move(actions));
    click("open-file-button");
    (void)app.renderFrame();
    CHECK(app.pickedFile() == "/tmp/fake-open.txt");
    const auto* updated = core::findNodeByKey(app.root(), "picked-file");
    REQUIRE(updated != nullptr);
    CHECK(updated->text == "/tmp/fake-open.txt");

    click("notify-button");
    (void)app.renderFrame();
    CHECK(app.pickedFile().find("failed") != std::string::npos);
    // 服务失败后应用状态完整：路由/表单/弹窗状态未受影响。
    CHECK(app.navigator().current() == "home");
    CHECK_FALSE(app.dialogOpen());
}

// M5：语义契约与键盘可用性收口（Recording bridge 作回归证据）。

TEST_CASE("settings_semantics_bridge_records_full_contract", "[settings][m5]") {
    using lumen::accessibility::RecordingAccessibilityBridge;
    using lumen::accessibility::SemanticsActionStatus;
    using lumen::accessibility::kActionActivate;
    using lumen::accessibility::kActionDismiss;
    using lumen::accessibility::kActionScroll;
    using lumen::accessibility::kSemanticsInvalid;
    using lumen::accessibility::SemanticsRole;

    SettingsApp app;
    app.setView(core::Size{800.0F, 600.0F});
    RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);

    // 首帧：全量推送（added = 全部节点）。
    (void)app.renderFrame();
    REQUIRE(bridge.updates.size() >= 1);
    CHECK(bridge.updates.front().diff.added.size() ==
          bridge.updates.front().treeSize);

    const auto click = [&app](const char* key) {
        const auto* node = core::findNodeByKey(app.root(), key);
        REQUIRE(node != nullptr);
        const auto pos = core::absoluteOffset(app.root(), key) +
                         core::Offset{node->size.width * 0.5F,
                                      node->size.height * 0.5F};
        app.pointerDown(pos);
        app.pointerUp(pos);
    };
    // 键盘路径与语义 action 触发同一 handler：语义 Activate goto-form。
    const std::string gotoId = core::findNodeByKey(app.root(),
                                                   "goto-form-button")
                                   ->identity;
    const auto status = app.shell().performAccessibilityAction(
        gotoId, kActionActivate);
    CHECK(status == SemanticsActionStatus::Handled);
    (void)app.renderFrame();
    REQUIRE(app.navigator().current() == "form");
    REQUIRE_FALSE(bridge.actions.empty());
    CHECK(bridge.actions.back().nodeId == gotoId);
    CHECK(bridge.actions.back().status == SemanticsActionStatus::Handled);

    // 表单错误 → invalid flag 进入语义 diff（changed 含 nickname-field）。
    click("save-button");
    (void)app.renderFrame();
    REQUIRE(app.form().errors().count("nickname") == 1);
    const auto invalidId =
        core::findNodeByKey(app.root(), "nickname-field")->identity;
    bool sawInvalid = false;
    for (const auto& update : bridge.updates) {
        for (const auto& changed : update.diff.changed) {
            if (changed == invalidId) {
                sawInvalid = true;
            }
        }
    }
    CHECK(sawInvalid);
    // 树上 flag 已暴露。
    const auto semantics = app.semantics();
    const auto* fieldNode = semantics.find(invalidId);
    REQUIRE(fieldNode != nullptr);
    CHECK((fieldNode->flags & kSemanticsInvalid) != 0);

    // 填写并保存 → 弹窗：barrier 语义（Dialog role + Dismiss action）。
    click("nickname-field");
    app.textInput("Lumen");
    click("email-field");
    app.textInput("dev@lumen.local");
    click("save-button");
    (void)app.renderFrame();
    REQUIRE(app.dialogOpen());
    const std::string barrierId = [ &app ] {
        // barrier 是 keyless 的 dialog 根（semanticsRole=dialog）。
        const auto tree = app.semantics();
        for (const auto& [id, node] : tree.nodes) {
            if (node.role == SemanticsRole::Dialog) {
                return id;
            }
        }
        return std::string{};
    }();
    REQUIRE_FALSE(barrierId.empty());
    {
        const auto tree = app.semantics();
        const auto* barrier = tree.find(barrierId);
        REQUIRE(barrier != nullptr);
        CHECK((barrier->actions & kActionDismiss) != 0);
    }

    // 语义 Dismiss ≡ 点击/Escape 关闭（同一 handler 路径）+ 焦点恢复。
    const auto dismissStatus = app.shell().performAccessibilityAction(
        barrierId, kActionDismiss);
    CHECK(dismissStatus == SemanticsActionStatus::Handled);
    (void)app.renderFrame();
    CHECK_FALSE(app.dialogOpen());
    CHECK_FALSE(bridge.actions.empty());
    // modal 焦点恢复：dialog-close 焦点已随树消失，恢复到路由内首个
    // 可聚焦节点（form 页的 nickname-field）。
    CHECK(app.shell().focus().focusedIdentity() ==
          core::findNodeByKey(app.root(), "nickname-field")->identity);

    // Navigator back（Escape）→ 焦点恢复（M5：pop 后首个可聚焦节点）。
    const std::size_t focusedBefore = bridge.focusedNodes.size();
    app.keyDown(core::Key::Escape);
    (void)app.renderFrame();
    CHECK(app.navigator().current() == "home");
    CHECK(bridge.focusedNodes.size() > focusedBefore);
    // 恢复焦点为 home 首个可聚焦节点（goto-form-button）。
    CHECK(app.shell().focus().focusedIdentity() ==
          core::findNodeByKey(app.root(), "goto-form-button")->identity);

    // 语义滚动命中目标视口中心，确保非原点视口也实际滚动。
    const std::string listId =
        core::findNodeByKey(app.root(), "settings-list")->identity;
    const float beforeScroll = app.scroll().offset();
    CHECK(app.shell().performAccessibilityAction(listId, kActionScroll, {},
                                                 100.0F) ==
          SemanticsActionStatus::Handled);
    (void)app.renderFrame();
    CHECK(app.scroll().offset() > beforeScroll);
}
