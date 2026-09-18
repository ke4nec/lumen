// v0.3 阶段8C (plan §4 8C / §5.1): 语义树测试。
//
// 覆盖：role/label/value/bounds/actions 推断与覆盖、焦点/启用/隐藏
// 状态、identity diff（added/removed/changed 与焦点保持）、语义 action
// 分发（activate/setValue/focus/scroll）、Recording 桥接协议、平台桥
// 工厂降级、FrameScheduler 减少动画设置。

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "lumen/accessibility/bridge.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/layout/layout.h"
#include "lumen/render/frame_scheduler.h"
#include "lumen/style/resolver.h"
#include "lumen/style/theme.h"

using namespace lumen;
using namespace lumen::accessibility;
using namespace lumen::core;
using namespace lumen::layout;

namespace {

RenderNode layoutOf(const Widget& widget, float width = 400.0F,
                    float height = 300.0F) {
    return LayoutEngine::layout(widget,
                                Constraints::tight(Size{width, height}));
}

// 计数器风格页面：文本 + 递增按钮 + 名称字段。
struct AppHarness {
    StateStore store{};
    HandlerRegistry handlers{};
    FocusManager focus{};
    InteractionController controller{store, handlers, focus};
    int clicks{0};

    AppHarness() {
        store.set("counter", "7");
        store.set("name", "Lumen");
        handlers["increment"] = [this] { ++clicks; };

        Widget ui = makeColumn({
            withKey(makeText("Count: 7"), "count-text"),
            withKey(makeButton("Increment", {}, {}, 0.0F, "inc", std::nullopt,
                               std::nullopt, "increment"),
                    "inc"),
            withKey(makeTextField("Lumen", {}, {}, {}, 0.0F, "name-field"),
                    "name-field"),
        });
        ui.children[2].bind = "name";
        ui.key = "root";
        root = layoutOf(ui);
        // 绑定解析后的字段文本。
        root = LayoutEngine::layout(
            [&] {
                Widget copy = ui;
                copy.children[2].text = store.get("name");
                return copy;
            }(),
            Constraints::tight(Size{400.0F, 300.0F}));
    }

    RenderNode root{};
};

const std::string idOf(const RenderNode& root, const char* key) {
    const RenderNode* node = findNodeByKey(root, key);
    REQUIRE(node != nullptr);
    return node->identity;
}

}  // namespace

TEST_CASE("semantics_roles_labels_values_and_bounds", "[a11y]") {
    AppHarness app;
    SemanticsBuildOptions options;
    options.focus = &app.focus;
    const SemanticsTree tree = buildSemanticsTree(app.root, options);

    // 根节点是 Window；容器是 Group。
    const SemanticsNode* root = tree.find(tree.rootId);
    REQUIRE(root != nullptr);
    CHECK(root->role == SemanticsRole::Window);
    CHECK(root->children.size() == 3);

    const SemanticsNode* text = tree.find(idOf(app.root, "count-text"));
    REQUIRE(text != nullptr);
    CHECK(text->role == SemanticsRole::Text);
    CHECK(text->label == "Count: 7");
    CHECK(text->actions == 0);
    CHECK(text->bounds.size.width > 0.0F);

    const SemanticsNode* button = tree.find(idOf(app.root, "inc"));
    REQUIRE(button != nullptr);
    CHECK(button->role == SemanticsRole::Button);
    CHECK(button->label == "Increment");
    CHECK((button->actions & kActionActivate) != 0);
    CHECK((button->actions & kActionFocus) != 0);

    const SemanticsNode* field = tree.find(idOf(app.root, "name-field"));
    REQUIRE(field != nullptr);
    CHECK(field->role == SemanticsRole::TextField);
    CHECK(field->value == "Lumen");
    CHECK((field->actions & kActionSetValue) != 0);
    // bounds 处于窗口坐标系（Column 内 y 递增）。
    CHECK(field->bounds.origin.y > text->bounds.origin.y);
}

TEST_CASE("semantics_focus_and_readonly_flags", "[a11y]") {
    AppHarness app;
    SemanticsBuildOptions options;
    options.focus = &app.focus;

    SemanticsTree unfocused = buildSemanticsTree(app.root, options);
    const SemanticsNode* field = unfocused.find(idOf(app.root, "name-field"));
    REQUIRE(field != nullptr);
    CHECK((field->flags & kSemanticsFocused) == 0);

    // 字段聚焦后 flags 反映焦点。
    const RenderNode* fieldNode = findNodeByKey(app.root, "name-field");
    app.focus.setFocus(fieldNode->key, fieldNode->identity);
    SemanticsTree focused = buildSemanticsTree(app.root, options);
    field = focused.find(idOf(app.root, "name-field"));
    REQUIRE(field != nullptr);
    CHECK((field->flags & kSemanticsFocused) != 0);
    // 其他节点不受影响。
    const SemanticsNode* button = focused.find(idOf(app.root, "inc"));
    CHECK((button->flags & kSemanticsFocused) == 0);
}

TEST_CASE("semantics_obscured_fields_hide_values", "[a11y]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    Widget ui = makeContainer(
        withObscure(withKey(makeTextField("secret", {}, {}, {}, 0.0F, "pw"),
                            "pw")));
    ui.children[0].bind = "pw";
    const RenderNode root = layoutOf(ui);

    SemanticsBuildOptions options;
    const SemanticsTree tree = buildSemanticsTree(root, options);
    const SemanticsNode* field = tree.find(idOf(root, "pw"));
    REQUIRE(field != nullptr);
    CHECK(field->value.empty());  // 密码不进入语义树
}

TEST_CASE("semantics_overrides_replace_defaults", "[a11y]") {
    Widget button =
        withKey(makeButton("Delete", {}, {}, 0.0F, "del", std::nullopt,
                           std::nullopt, "delete"),
                "del");
    button.semanticsLabel = "删除所选项目";
    button.semanticsRole = "button";
    button.semanticsActions = kActionFocus;
    const RenderNode root = layoutOf(makeContainer(std::move(button)));

    const SemanticsTree tree = buildSemanticsTree(root);
    const SemanticsNode* node = tree.find(idOf(root, "del"));
    REQUIRE(node != nullptr);
    CHECK(node->label == "删除所选项目");
    // actions 与默认值相或。
    CHECK((node->actions & kActionActivate) != 0);
    CHECK((node->actions & kActionFocus) != 0);
}

TEST_CASE("semantics_identity_diff_detects_changes_and_stability", "[a11y]") {
    AppHarness app;
    SemanticsBuildOptions options;
    options.focus = &app.focus;

    const SemanticsTree first = buildSemanticsTree(app.root, options);

    // 同一棵树重建：identity 稳定，无 diff（辅助技术焦点保留）。
    const SemanticsTree same = buildSemanticsTree(app.root, options);
    const SemanticsDiff noChange = diffSemanticsTrees(first, same);
    CHECK(noChange.empty());

    // 文本值变化：只 diff 出该节点。
    app.store.set("name", "Lumen2");
    // 重建树（应用层 rebuild+layout 的最小等价）。
    Widget ui = makeColumn({
        withKey(makeText("Count: 7"), "count-text"),
        withKey(makeButton("Increment", {}, {}, 0.0F, "inc", std::nullopt,
                           std::nullopt, "increment"),
                "inc"),
        withKey(makeTextField("Lumen2", {}, {}, {}, 0.0F, "name-field"),
                "name-field"),
    });
    ui.key = "root";
    const SemanticsTree updated = buildSemanticsTree(layoutOf(ui), options);
    const SemanticsDiff diff = diffSemanticsTrees(first, updated);
    CHECK(diff.added.empty());
    CHECK(diff.removed.empty());
    REQUIRE(diff.changed.size() == 1);
    // 变化节点的 identity 与原字段一致（keyed 节点跨重建稳定）。
    const RenderNode* fieldNode = findNodeByKey(app.root, "name-field");
    CHECK(diff.changed.front() == fieldNode->identity);
    CHECK(updated.find(fieldNode->identity)->value == "Lumen2");
}

TEST_CASE("semantics_diff_reports_added_removed_and_focus_moves", "[a11y]") {
    auto build = [](bool withExtra) {
        Widget ui = makeColumn({
            withKey(makeButton("A", {}, {}, 0.0F, "a"), "a"),
        });
        if (withExtra) {
            ui.children.push_back(
                withKey(makeButton("B", {}, {}, 0.0F, "b"), "b"));
        }
        ui.key = "root";
        return layoutOf(ui);
    };
    const SemanticsTree base = buildSemanticsTree(build(false));
    const SemanticsTree withB = buildSemanticsTree(build(true));

    const SemanticsDiff added = diffSemanticsTrees(base, withB);
    REQUIRE(added.added.size() == 1);
    CHECK(added.added.front().find("k:b") != std::string::npos);

    const SemanticsDiff removed = diffSemanticsTrees(withB, base);
    REQUIRE(removed.removed.size() == 1);

    // 焦点变化记录新旧 id。
    const SemanticsDiff focusDiff =
        diffSemanticsTrees(base, base, "old-focus", "new-focus");
    CHECK(focusDiff.previousFocusedId == "old-focus");
    CHECK(focusDiff.currentFocusedId == "new-focus");
}

TEST_CASE("semantics_actions_dispatch_to_handlers_and_fields", "[a11y]") {
    AppHarness app;
    SemanticsBuildOptions options;
    options.focus = &app.focus;
    const SemanticsTree tree = buildSemanticsTree(app.root, options);

    SemanticsActionContext context;
    context.root = &app.root;
    context.handlers = &app.handlers;
    context.focus = &app.focus;
    context.controller = &app.controller;

    // activate 触发按钮 handler（与 Enter 激活同路径）。
    CHECK(performSemanticsAction(tree, context, idOf(app.root, "inc"),
                                 kActionActivate) ==
          SemanticsActionStatus::Handled);
    CHECK(app.clicks == 1);

    // 未声明 action 的节点返回 NotHandled。
    CHECK(performSemanticsAction(tree, context, idOf(app.root, "count-text"),
                                 kActionActivate) ==
          SemanticsActionStatus::NotHandled);
    // 树中不存在的节点。
    CHECK(performSemanticsAction(tree, context, "/missing",
                                 kActionActivate) ==
          SemanticsActionStatus::NodeMissing);

    // setValue：先 focus 字段再写值。
    CHECK(performSemanticsAction(tree, context, idOf(app.root, "name-field"),
                                 kActionFocus) ==
          SemanticsActionStatus::Handled);
    CHECK(app.controller.focusedBind() == "name");
    CHECK(performSemanticsAction(tree, context, idOf(app.root, "name-field"),
                                 kActionSetValue, "你好") ==
          SemanticsActionStatus::Handled);
    CHECK(app.store.get("name") == "你好");
    CHECK(app.controller.editingValue().caret() == 2);
}

TEST_CASE("semantics_scroll_action_routes_to_sink", "[a11y]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    Widget ui = withKey(makeContainerLeaf(100.0F, 100.0F), "viewport");
    ui.semanticsActions = kActionScroll;
    ui.semanticsRole = "list";
    const RenderNode root = layoutOf(makeContainer(std::move(ui)));
    const SemanticsTree tree = buildSemanticsTree(root);
    const std::string viewportId = idOf(root, "viewport");

    float scrolledY = 0.0F;
    SemanticsActionContext context;
    context.root = &root;
    context.scrollSink = [&](const std::string& id, float, float dy) {
        scrolledY = dy;
        return id == viewportId;
    };

    // 无 scroll action 的树节点未声明时 NotHandled 已由另一测试覆盖；
    // 这里验证 sink 路由。
    CHECK(performSemanticsAction(tree, context, viewportId, kActionScroll, {},
                                 -120.0F) == SemanticsActionStatus::Handled);
    CHECK(scrolledY == -120.0F);
}

TEST_CASE("recording_bridge_protocol_and_platform_factory_fallback",
          "[a11y]") {
    AppHarness app;
    SemanticsBuildOptions options;
    options.focus = &app.focus;
    const SemanticsTree tree = buildSemanticsTree(app.root, options);

    RecordingAccessibilityBridge bridge;
    CHECK(bridge.available());
    CHECK(bridge.bridgeName() == "recording");

    // 首次全量更新。
    SemanticsDiff fullDiff;
    for (const auto& [id, node] : tree.nodes) {
        fullDiff.added.push_back(id);
    }
    bridge.updateTree(tree, fullDiff, tree.rootId);
    bridge.setFocusedNode(tree.rootId);

    // 第二次增量。
    SemanticsDiff incremental;
    incremental.changed.push_back(idOf(app.root, "count-text"));
    bridge.updateTree(tree, incremental, idOf(app.root, "count-text"));

    REQUIRE(bridge.updates.size() == 2);
    CHECK(bridge.updates[0].treeSize == tree.size());
    CHECK(bridge.updates[0].diff.added.size() == tree.size());
    CHECK(bridge.updates[1].diff.changed.size() == 1);
    CHECK(bridge.focusedNodes.size() == 1);

    // 平台桥未编入时工厂安全降级（能力可查询，plan §2.3）；编入时由
    // tests/a11y_provider_tests.cpp 按平台分流断言。
    std::string diagnostics;
    if (std::string(accessibilityProviderName()).empty()) {
        auto platform = createPlatformAccessibilityBridge(
            accessibility::PlatformAccessibilityHost{}, &diagnostics);
        CHECK(platform == nullptr);
        CHECK_FALSE(diagnostics.empty());
    }
}

TEST_CASE("frame_scheduler_honors_reduce_animation_setting", "[a11y][sched]") {
    class ManualClock final : public render::FrameClock {
      public:
        std::uint64_t current{0};
        [[nodiscard]] std::uint64_t nowMs() const override { return current; }
    };
    ManualClock clock;
    clock.current = 1000;
    render::FrameScheduler::Config config;
    render::FrameScheduler scheduler{config, &clock};
    scheduler.setAnimationsActive(true);
    scheduler.markFrameSubmitted();

    clock.current = 1100;
    // 动画活跃：提交帧。
    CHECK(scheduler.evaluateFrame().submit);

    // 减少动画：动画不再驱动提交。
    scheduler.setReduceAnimation(true);
    clock.current = 1200;
    CHECK_FALSE(scheduler.evaluateFrame().submit);
    // 输入原因不受影响。
    scheduler.requestFrame(render::FrameReason::Input);
    clock.current = 1300;
    CHECK(scheduler.evaluateFrame().submit);
}

// --- 视觉系统：disabled/selected 语义（§10.3） ---

TEST_CASE("semantics_disabled_controls_drop_enabled_flag", "[semantics]") {
    Widget ui = makeColumn({
        withKey(withEnabled(
                    makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt,
                               std::nullopt, "go"),
                    /*enabled=*/false),
                "btn"),
        withKey(withEnabled(makeCheckbox("A", "a", "cb"), false), "cb"),
        withKey(withEnabled(makeSwitch("S", "s", "sw"), false), "sw"),
    });
    ui.key = "root";
    const RenderNode root = layoutOf(ui);
    const SemanticsTree tree = buildSemanticsTree(root);

    for (const char* key : {"btn", "cb", "sw"}) {
        const RenderNode* node = findNodeByKey(root, key);
        REQUIRE(node != nullptr);
        const SemanticsNode* semantic = tree.find(node->identity);
        REQUIRE(semantic != nullptr);
        CHECK((semantic->flags & kSemanticsEnabled) == 0);
    }
}

TEST_CASE("semantics_disabled_activate_returns_not_handled", "[semantics]") {
    AppHarness harness;
    Widget ui = makeContainer(withKey(
        withEnabled(makeButton("OK", {}, {}, 0.0F, "btn", std::nullopt,
                               std::nullopt, "increment"),
                    /*enabled=*/false),
        "btn"));
    ui.key = "root";
    const RenderNode root = layoutOf(ui);
    const SemanticsTree tree = buildSemanticsTree(root);
    const RenderNode* button = findNodeByKey(root, "btn");
    REQUIRE(button != nullptr);

    SemanticsActionContext context;
    context.handlers = &harness.handlers;
    context.root = &root;
    // Focus/Activate 对禁用节点一律拒绝（§7.3：视觉、hit test、键盘激活
    // 与 semantics flags 一致）。
    CHECK(performSemanticsAction(tree, context, button->identity,
                                 kActionFocus) ==
          SemanticsActionStatus::NotHandled);
    CHECK(performSemanticsAction(tree, context, button->identity,
                                 kActionActivate) ==
          SemanticsActionStatus::NotHandled);
    CHECK(harness.clicks == 0);
}

TEST_CASE("semantics_selected_flag_from_widget", "[semantics]") {
    Widget ui = makeContainer(withKey(
        withSelected(makeCheckbox("A", "a", "cb"), /*selected=*/true), "cb"));
    ui.key = "root";
    const RenderNode root = layoutOf(ui);
    const SemanticsTree tree = buildSemanticsTree(root);
    const RenderNode* checkbox = findNodeByKey(root, "cb");
    REQUIRE(checkbox != nullptr);
    const SemanticsNode* semantic = tree.find(checkbox->identity);
    REQUIRE(semantic != nullptr);
    CHECK((semantic->flags & kSemanticsSelected) != 0);
}

TEST_CASE("semantics_checked_visual_and_flags_stay_in_sync", "[semantics]") {
    // checked 同时影响视觉（resolved style）与 semantics flags（§7.3）。
    Widget unchecked = makeCheckbox("A", "a", "cb");
    Widget checked = withSelected(makeCheckbox("A", "a", "cb"), true);

    const style::Theme theme = style::Theme::dark();
    const style::InteractionStateSnapshot idle;
    const lumen::accessibility::AccessibilitySettings settings;
    const auto uncheckedStyle = lumen::style::resolveStyle(
        unchecked, style::StyleContext{theme, idle, settings}, "/k:cb");
    const auto checkedStyle = lumen::style::resolveStyle(
        checked, style::StyleContext{theme, idle, settings}, "/k:cb");
    const auto& off = std::get<lumen::core::CheckboxResolvedStyle>(
        uncheckedStyle.component);
    const auto& on = std::get<lumen::core::CheckboxResolvedStyle>(
        checkedStyle.component);
    CHECK(off.checked != on.checked);
    CHECK(off.indicator == on.indicator);  // 未选中色不变，绘制按 checked 取 indicatorChecked

    const RenderNode root = layoutOf(withKey(
        makeContainer(withSelected(makeCheckbox("A", "a", "cb"), true)),
        "root2"));
    const SemanticsTree tree = buildSemanticsTree(root);
    const RenderNode* node = findNodeByKey(root, "cb");
    REQUIRE(node != nullptr);
    const SemanticsNode* semantic = tree.find(node->identity);
    REQUIRE(semantic != nullptr);
    CHECK((semantic->flags & kSemanticsChecked) != 0);
}

// --- M5：语义契约收口 ---

TEST_CASE("semantics_invalid_flag_exposed_with_visual_state", "[a11y][m5]") {
    Widget field = makeTextField("email");
    field.key = "email";
    field.invalid = true;
    Widget page;
    page.key = "root";
    page.children = {std::move(field)};

    const RenderNode root = layoutOf(page);
    const auto tree = buildSemanticsTree(root);
    const auto* node = tree.find(findNodeByKey(root, "email")->identity);
    REQUIRE(node != nullptr);
    CHECK((node->flags & kSemanticsInvalid) != 0);
    CHECK((node->flags & kSemanticsEnabled) != 0);  // invalid 仍可交互。
}

TEST_CASE("semantics_hidden_flags_nodes_outside_scroll_viewport", "[a11y][m5]") {
    // 长列表滚到中部：视口外的项标 Hidden（仍在树中）。
    std::vector<Widget> items;
    for (int i = 0; i < 20; ++i) {
        Widget item = makeText("row");
        item.key = "row-" + std::to_string(i);
        item.height = 40.0F;
        items.push_back(std::move(item));
    }
    Widget column = makeColumn(std::move(items));
    Widget scroll = makeScrollView(std::move(column), "scroll", std::nullopt,
                                   200.0F);
    scroll.scrollOffset = 400.0F;  // 滚到中部：row-0..9 在视口上方。
    Widget page;
    page.key = "root";
    page.children = {std::move(scroll)};

    const RenderNode root = layoutOf(page);
    const auto tree = buildSemanticsTree(root);
    const std::string top =
        findNodeByKey(root, "row-0")->identity;
    const std::string visible =
        findNodeByKey(root, "row-12")->identity;
    const auto* hiddenNode = tree.find(top);
    const auto* visibleNode = tree.find(visible);
    REQUIRE(hiddenNode != nullptr);
    REQUIRE(visibleNode != nullptr);
    CHECK((hiddenNode->flags & kSemanticsHidden) != 0);
    CHECK((visibleNode->flags & kSemanticsHidden) == 0);
}

TEST_CASE("semantics_image_label_falls_back_to_source", "[a11y][m5]") {
    Widget image = makeImage(0, "asset://cover.png", 120.0F, 80.0F, "cover");
    Widget page;
    page.key = "root";
    page.children = {std::move(image)};
    const RenderNode root = layoutOf(page);
    const auto tree = buildSemanticsTree(root);
    const auto* node = tree.find(findNodeByKey(root, "cover")->identity);
    REQUIRE(node != nullptr);
    CHECK(node->role == SemanticsRole::Image);
    CHECK(node->label == "asset://cover.png");  // 可访问名称保留。
}

TEST_CASE("semantics_grid_groups_children_in_reading_order", "[a11y][m5]") {
    std::vector<Widget> cells;
    for (int i = 0; i < 6; ++i) {
        cells.push_back(withKey(makeText("c"), "cell-" + std::to_string(i)));
    }
    Widget grid = makeGrid(std::move(cells), 3, 0.0F, 4.0F, 4.0F, "grid");
    Widget page;
    page.key = "root";
    page.children = {std::move(grid)};
    const RenderNode root = layoutOf(page);
    const auto tree = buildSemanticsTree(root);
    const auto* node = tree.find(findNodeByKey(root, "grid")->identity);
    REQUIRE(node != nullptr);
    CHECK(node->role == SemanticsRole::Group);
    REQUIRE(node->children.size() == 6);
    // 子顺序 = 阅读顺序（布局后的树序）。
    CHECK(node->children[0] == findNodeByKey(root, "cell-0")->identity);
    CHECK(node->children[5] == findNodeByKey(root, "cell-5")->identity);
}

TEST_CASE("virtual_list_cache_items_are_hidden_in_semantics", "[a11y][m5]") {
    // M3 测试内 ListSource：可见区含缓存；缓存区（视口外）物化项标 Hidden。
    struct FixedSource final : VirtualListSource {
        std::size_t count{100};
        float offset{0.0F};
        float viewport{200.0F};
        float itemExtent{40.0F};
        mutable std::vector<std::size_t> built{};
        [[nodiscard]] std::size_t itemCount() const override { return count; }
        [[nodiscard]] float estimatedExtent() const override {
            return itemExtent;
        }
        [[nodiscard]] float extentOf(std::size_t) const override {
            return itemExtent;
        }
        [[nodiscard]] float scrollOffset() const override { return offset; }
        [[nodiscard]] float totalExtent() const override {
            return static_cast<float>(count) * itemExtent;
        }
        [[nodiscard]] float offsetOfIndex(std::size_t i) const override {
            return static_cast<float>(i) * itemExtent;
        }
        [[nodiscard]] std::pair<std::size_t, std::size_t> visibleRange(
            float viewportExtent, float cache) const override {
            const float top = std::max(0.0F, offset - cache);
            const float bottom = offset + viewportExtent + cache;
            return {static_cast<std::size_t>(top / itemExtent),
                    static_cast<std::size_t>(bottom / itemExtent) + 1};
        }
        [[nodiscard]] Widget buildItem(std::size_t i) const override {
            built.push_back(i);
            Widget item = makeText("item");
            item.key = "item-" + std::to_string(i);
            item.height = itemExtent;
            return item;
        }
        void noteExtent(std::size_t, float) const override {}
    };
    FixedSource source;
    source.offset = 400.0F;  // 首个可见项 index 10。

    Widget list = makeVirtualList(&source, "list", std::nullopt, 200.0F,
                                  40.0F /* 缓存 = 1 项 */);
    Widget page;
    page.key = "root";
    page.children = {std::move(list)};
    const RenderNode root = layoutOf(page);
    const auto tree = buildSemanticsTree(root);
    const auto* listNode = tree.find(findNodeByKey(root, "list")->identity);
    REQUIRE(listNode != nullptr);
    CHECK(listNode->role == SemanticsRole::List);
    CHECK((listNode->actions & kActionScroll) != 0);
    // 缓存区项（item-9，视口上方）标 Hidden；首个可见项不标。
    const auto* cached = tree.find(findNodeByKey(root, "item-9")->identity);
    const auto* visible = tree.find(findNodeByKey(root, "item-10")->identity);
    REQUIRE(cached != nullptr);
    REQUIRE(visible != nullptr);
    CHECK((cached->flags & kSemanticsHidden) != 0);
    CHECK((visible->flags & kSemanticsHidden) == 0);
}

TEST_CASE("focus_first_focusable_restores_focus_in_tab_order", "[a11y][m5]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("a", "");
    handlers["go"] = [] {};

    Widget field = makeTextField();
    field.bind = "a";
    field.key = "field";
    Widget button = makeButton("Go");
    button.onClick = "go";
    button.key = "go";
    Widget page = makeColumn({
        withKey(makeText("标题"), "title"),
        std::move(field),
        std::move(button),
    });
    page.key = "root";
    const RenderNode root = layoutOf(page);

    // 焦点在按钮上 → 恢复到首个可聚焦节点（field）。
    const RenderNode* go = findNodeByKey(root, "go");
    controller.focusNode(*go);
    CHECK(focus.focusedKey() == "go");
    CHECK(controller.focusFirstFocusable(root));
    CHECK(focus.focusedKey() == "field");
    CHECK(controller.wantsTextInput());
}
