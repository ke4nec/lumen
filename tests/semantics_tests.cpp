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

    // 平台桥未编入时工厂安全降级（能力可查询，plan §2.3）。
    std::string diagnostics;
    auto platform = createPlatformAccessibilityBridge(&diagnostics);
    CHECK(platform == nullptr);
    CHECK_FALSE(diagnostics.empty());
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
