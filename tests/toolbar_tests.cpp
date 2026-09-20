// ToolBar 工具栏（docs/lumen-toolbar-design.md）测试：命令激活（单击 ≡
// 面板项激活 ≡ Enter）、toggle Chrome 变体 + checked 持久底、disabled 拒
// 绝、键盘漫游（跳过分隔线与禁用项/Home/End）、溢出折叠与回位（上一帧几何
// 二次收敛）、溢出面板打开/键盘/焦点恢复、tooltip 子树常驻、语义角色与焦
// 点环、尺度档覆盖。
//
// 命名遵循项目测试规范（行为命名）。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/widgets/toolbar.h"

using namespace lumen;
using namespace lumen::core;
using lumen::widgets::ToolBarController;
using lumen::widgets::ToolBarItem;

namespace {

std::vector<ToolBarItem> sampleItems() {
    return std::vector<ToolBarItem>{
        ToolBarItem{.id = "new", .icon = IconId::Plus, .label = "新建",
                    .shortcut = "Ctrl+N"},
        ToolBarItem{.id = "open", .icon = IconId::Folder, .label = "打开"},
        ToolBarItem{.id = "sep", .separator = true},
        ToolBarItem{.id = "undo", .icon = IconId::Undo, .label = "撤销",
                    .enabled = false},
        ToolBarItem{.id = "run", .icon = IconId::Play, .label = "运行",
                    .checkable = true, .labelMode = true},
        ToolBarItem{.id = "grid", .icon = IconId::Grid, .label = "网格",
                    .checkable = true},
    };
}

class ToolApp {
  public:
    app::AppShell shell{makeConfig(this)};
    ToolBarController bar{"tb"};
    std::vector<std::string> commands;

    static app::ShellConfig makeConfig(ToolApp* self) {
        app::ShellConfig config;
        config.build = [self] {
            // 根列 Stretch：栏容器拉伸到视口宽（溢出决策的约束输入）。
            return makeColumn(
                {self->bar.build(self->shell, self->shell.theme())},
                MainAxisAlignment::Start, CrossAxisAlignment::Stretch);
        };
        config.onKey = [self](app::AppShell& shell, Key key,
                              KeyModifiers mods, char keyChar) {
            return self->bar.handleKey(shell, key, mods, keyChar);
        };
        return config;
    }

    explicit ToolApp(std::vector<ToolBarItem> items = sampleItems()) : bar{"tb"} {
        shell.setView(Size{800.0F, 200.0F});
        bar.setItems(std::move(items));
        bar.onCommand = [this](const std::string& id) {
            commands.push_back(id);
        };
        bar.attach(shell);
        settle();
    }

    // 溢出决策读上一帧几何：三帧收敛（布局 → 折叠 → 稳定）。
    void settle() {
        for (int i = 0; i < 3; ++i) {
            shell.markDirty();
            shell.rebuildIfDirty();
        }
    }

    void resize(float width) {
        shell.setView(Size{width, 200.0F});
        settle();
    }

    [[nodiscard]] Offset centerOf(const std::string& key) const {
        const RenderNode* node = findNodeByKey(shell.root(), key);
        REQUIRE(node != nullptr);
        return absoluteOffset(shell.root(), key) +
               Offset{node->size.width * 0.5F, node->size.height * 0.5F};
    }

    void click(const std::string& key) {
        const Offset center = centerOf(key);
        shell.pointerDown(center);
        shell.pointerUp(center);
    }

    void focusItem(const std::string& key) {
        const RenderNode* node = findNodeByKey(shell.root(), key);
        REQUIRE(node != nullptr);
        shell.controller().focusNode(*node);
        shell.markDirty();
        // renderFrame 同步交互快照（焦点进样式解析）后再重建。
        (void)shell.renderFrame();
    }
};

}  // namespace

TEST_CASE("toolbar_item_click_fires_command", "[widgets][toolbar]") {
    ToolApp app;
    app.click("tb:item:new");
    REQUIRE(app.commands.size() == 1);
    CHECK(app.commands.front() == "new");
}

TEST_CASE("toolbar_disabled_item_rejects_click", "[widgets][toolbar]") {
    ToolApp app;
    app.click("tb:item:undo");
    CHECK(app.commands.empty());
}

TEST_CASE("toolbar_toggle_checked_renders_tonal_surface",
          "[widgets][toolbar]") {
    ToolApp app;
    app.click("tb:item:grid");
    REQUIRE(app.commands.size() == 1);
    // checked 由应用维护：翻转后重建反映 Tonal 底（accentContainer）。
    auto items = sampleItems();
    for (auto& item : items) {
        if (item.id == "grid") {
            item.checked = true;
        }
    }
    app.bar.setItems(std::move(items));
    app.settle();
    const RenderNode* grid = findNodeByKey(app.shell.root(), "tb:item:grid");
    REQUIRE(grid != nullptr);
    CHECK(grid->commonStyle().background ==
          app.shell.theme().colors.accentContainer);
    // 未选中项保持 Ghost 透明底。
    const RenderNode* open = findNodeByKey(app.shell.root(), "tb:item:open");
    REQUIRE(open != nullptr);
    CHECK(open->commonStyle().background.a == 0);
}

TEST_CASE("toolbar_roving_focus_skips_separator_and_disabled",
          "[widgets][toolbar]") {
    ToolApp app;
    app.focusItem("tb:item:new");
    app.shell.keyDown(Key::Right);
    CHECK(app.shell.focus().focusedKey() == "tb:item:open");
    app.shell.keyDown(Key::Right);  // 跳过分隔线与 disabled undo
    CHECK(app.shell.focus().focusedKey() == "tb:item:run");
    app.shell.keyDown(Key::Left);
    CHECK(app.shell.focus().focusedKey() == "tb:item:open");
    app.shell.keyDown(Key::Home);
    CHECK(app.shell.focus().focusedKey() == "tb:item:new");
    app.shell.keyDown(Key::End);
    CHECK(app.shell.focus().focusedKey() == "tb:item:grid");
    // 到端即停（不环绕）。
    app.shell.keyDown(Key::Right);
    CHECK(app.shell.focus().focusedKey() == "tb:item:grid");
}

TEST_CASE("toolbar_focus_ring_always_on_for_items", "[widgets][toolbar]") {
    ToolApp app;
    const RenderNode* item = findNodeByKey(app.shell.root(), "tb:item:new");
    REQUIRE(item != nullptr);
    CHECK(item->commonStyle().focusWidth == 0.0F);  // 未聚焦
    app.focusItem("tb:item:new");
    const RenderNode* focused = findNodeByKey(app.shell.root(), "tb:item:new");
    REQUIRE(focused != nullptr);
    CHECK(focused->commonStyle().focusWidth ==
          Catch::Approx(app.shell.theme().metrics.focusRingWidth).margin(0.01F));
}

TEST_CASE("toolbar_overflow_folds_tail_and_unfolds_on_grow",
          "[widgets][toolbar]") {
    // 8 个 icon-only 项：800px 全量；300px 折叠尾部 3 项；回 800 回位。
    std::vector<ToolBarItem> items;
    for (int i = 0; i < 8; ++i) {
        items.push_back(ToolBarItem{.id = "cmd" + std::to_string(i),
                                    .icon = IconId::Grid,
                                    .label = "命令" + std::to_string(i)});
    }
    ToolApp app{items};
    CHECK(app.bar.overflowedIds().empty());
    CHECK(findNodeByKey(app.shell.root(), "tb:overflow") == nullptr);

    app.resize(300.0F);
    // avail = 300 - 16 = 284；40×N + 4×(N-1) + 44(溢出钮) ≤ 284 → 保留 5 项。
    REQUIRE(app.bar.overflowedIds().size() == 3);
    CHECK(app.bar.overflowedIds().front() == "cmd7");   // 尾部先折
    CHECK(findNodeByKey(app.shell.root(), "tb:overflow") != nullptr);
    CHECK(findNodeByKey(app.shell.root(), "tb:item:cmd7") == nullptr);
    CHECK(findNodeByKey(app.shell.root(), "tb:item:cmd0") != nullptr);  // 首项保留

    app.resize(800.0F);
    CHECK(app.bar.overflowedIds().empty());
    CHECK(findNodeByKey(app.shell.root(), "tb:overflow") == nullptr);
    CHECK(findNodeByKey(app.shell.root(), "tb:item:cmd7") != nullptr);
}

TEST_CASE("toolbar_overflow_extreme_keeps_first_item", "[widgets][toolbar]") {
    std::vector<ToolBarItem> items;
    for (int i = 0; i < 4; ++i) {
        items.push_back(ToolBarItem{.id = "cmd" + std::to_string(i),
                                    .icon = IconId::Grid, .label = "c"});
    }
    ToolApp app{items};
    app.resize(60.0F);  // 只容得下溢出按钮 + 首项（保底）
    CHECK(app.bar.overflowedIds().size() == 3);
    CHECK(findNodeByKey(app.shell.root(), "tb:item:cmd0") != nullptr);
}

TEST_CASE("toolbar_label_mode_folds_first", "[widgets][toolbar]") {
    std::vector<ToolBarItem> items;
    for (int i = 0; i < 5; ++i) {
        items.push_back(ToolBarItem{.id = "cmd" + std::to_string(i),
                                    .icon = IconId::Grid, .label = "c"});
    }
    items.push_back(ToolBarItem{.id = "run", .icon = IconId::Play,
                                .label = "运行项目", .labelMode = true});
    ToolApp app{items};
    app.resize(300.0F);
    // labelMode 项最宽（估宽 128）——折它一项即放得下（§5.2 优先级）。
    REQUIRE(app.bar.overflowedIds().size() == 1);
    CHECK(app.bar.overflowedIds().front() == "run");
    CHECK(findNodeByKey(app.shell.root(), "tb:item:run") == nullptr);
    CHECK(findNodeByKey(app.shell.root(), "tb:item:cmd4") != nullptr);
}

TEST_CASE("toolbar_overflow_panel_activates_same_command",
          "[widgets][toolbar]") {
    std::vector<ToolBarItem> items;
    for (int i = 0; i < 8; ++i) {
        items.push_back(ToolBarItem{.id = "cmd" + std::to_string(i),
                                    .icon = IconId::Grid,
                                    .label = "命令" + std::to_string(i)});
    }
    ToolApp app{items};
    app.resize(300.0F);
    REQUIRE(app.bar.overflowedIds().size() == 3);

    // 打开面板（Down 于溢出按钮）。
    app.focusItem("tb:overflow");
    app.shell.keyDown(Key::Down);
    CHECK(app.shell.hasOverlay());
    // 面板行按折叠原序（M14 面板路径，owner 前缀 = "tb"）。
    const RenderNode* overlay = app.shell.overlayRoot();
    REQUIRE(overlay != nullptr);
    const RenderNode* row = findNodeByKey(*overlay, "tb:m0:i0");
    REQUIRE(row != nullptr);
    // Enter 激活高亮项 ≡ 单击栏内项（同 onCommand）；面板项按原序
    //（§6.3）→ 首项 = cmd5。
    CHECK(app.bar.handleKey(app.shell, Key::Enter));
    REQUIRE(app.commands.size() == 1);
    CHECK(app.commands.front() == "cmd5");
    CHECK_FALSE(app.shell.hasOverlay());
}

TEST_CASE("toolbar_overflow_panel_escape_restores_focus",
          "[widgets][toolbar]") {
    std::vector<ToolBarItem> items;
    for (int i = 0; i < 8; ++i) {
        items.push_back(ToolBarItem{.id = "cmd" + std::to_string(i),
                                    .icon = IconId::Grid, .label = "c"});
    }
    ToolApp app{items};
    app.resize(300.0F);
    app.focusItem("tb:overflow");
    app.shell.keyDown(Key::Down);
    REQUIRE(app.shell.hasOverlay());
    app.shell.keyDown(Key::Escape);
    CHECK_FALSE(app.shell.hasOverlay());
    CHECK(app.shell.focus().focusedKey() == "tb:overflow");
}

TEST_CASE("toolbar_tooltip_widgets_persist_for_hover", "[widgets][toolbar]") {
    ToolApp app;
    // M11 tooltip 常驻子树（显隐由壳层延迟驱动；隐藏 alpha 0）。
    CHECK(findNodeByKey(app.shell.root(), "tb:tip:new") != nullptr);
    CHECK(findNodeByKey(app.shell.root(), "tb:tip:run") != nullptr);
}

TEST_CASE("toolbar_semantics_role_and_icon_only_label",
          "[widgets][toolbar]") {
    ToolApp app;
    // 语义树：栏容器 role = toolbar；icon-only 项 label = tooltip 文本
    //（semanticsLabel 覆盖）。
    const auto tree = accessibility::buildSemanticsTree(app.shell.root());
    bool sawToolbar = false;
    bool sawNewLabel = false;
    for (const auto& [id, node] : tree.nodes) {
        if (node.role == accessibility::SemanticsRole::Toolbar) {
            sawToolbar = true;
        }
        if (node.role == accessibility::SemanticsRole::Button &&
            node.label == "新建") {
            sawNewLabel = true;
        }
    }
    CHECK(sawToolbar);
    CHECK(sawNewLabel);
}

// 栏容器 label 由应用提供（design §7："label = 应用 semanticsLabel"）——
// 此前只有 role，读屏落到工具栏时没有名称。
TEST_CASE("toolbar_semantics_label_comes_from_application",
          "[widgets][toolbar]") {
    ToolApp app;
    app.bar.setSemanticsLabel("主工具栏");
    app.settle();
    const auto tree = accessibility::buildSemanticsTree(app.shell.root());
    bool labelled = false;
    for (const auto& [id, node] : tree.nodes) {
        if (node.role == accessibility::SemanticsRole::Toolbar) {
            labelled = node.label == "主工具栏";
        }
    }
    CHECK(labelled);
}

// 尺度档覆盖（design §9.1）：Comfortable 密度下 Small = 相对下移一档
//（项 32×32、栏 33 含 1px 底线、内边距 4）——清单瓦片紧凑样本的口径，
// 与 Spin/StatusBar 的 setControlSize 同规则。
TEST_CASE("toolbar_control_size_override_selects_compact_tier",
          "[widgets][toolbar]") {
    ToolApp app;
    const RenderNode* rest = findNodeByKey(app.shell.root(), "tb:item:new");
    REQUIRE(rest != nullptr);
    CHECK(rest->size.width == Catch::Approx(40.0F).margin(0.01F));

    app.bar.setControlSize(ControlSize::Small);
    app.settle();
    const RenderNode* item = findNodeByKey(app.shell.root(), "tb:item:new");
    REQUIRE(item != nullptr);
    CHECK(item->size.width == Catch::Approx(32.0F).margin(0.01F));
    CHECK(item->size.height == Catch::Approx(32.0F).margin(0.01F));
    const RenderNode* bar = findNodeByKey(app.shell.root(), "tb:bar");
    REQUIRE(bar != nullptr);
    CHECK(bar->size.height == Catch::Approx(33.0F).margin(0.01F));
    // 首项左缘 = 栏水平内边距 Small 档 4（§9.1）。
    CHECK(item->offset.x == Catch::Approx(4.0F).margin(0.01F));
}

TEST_CASE("toolbar_item_states_match_chrome_language", "[widgets][toolbar]") {
    ToolApp app;
    // hover：表面派生 + 前景提亮 primary（design §9.3/稿件 .tb-item:hover）。
    app.shell.setVisualPreviewState("tb:item:new",
                                    style::WidgetState{.hovered = true});
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* hovered = findNodeByKey(app.shell.root(), "tb:item:new");
    REQUIRE(hovered != nullptr);
    CHECK(hovered->commonStyle().foreground ==
          app.shell.theme().colors.contentPrimary);

    // pressed：List pressed（surface/accent 0.32 混合，§9.2）。
    app.shell.setVisualPreviewState("tb:item:new",
                                    style::WidgetState{.pressed = true});
    app.shell.markDirty();
    app.shell.rebuildIfDirty();
    const RenderNode* pressed = findNodeByKey(app.shell.root(), "tb:item:new");
    REQUIRE(pressed != nullptr);
    CHECK(pressed->commonStyle().background == app.shell.theme().list.pressed);

    // checked（toggle）：accentContainer 持久底 + primary 前景 + 语义 flag。
    auto items = sampleItems();
    for (auto& item : items) {
        if (item.id == "grid") {
            item.checked = true;
        }
    }
    app.bar.setItems(std::move(items));
    app.settle();
    const RenderNode* grid = findNodeByKey(app.shell.root(), "tb:item:grid");
    REQUIRE(grid != nullptr);
    CHECK(grid->commonStyle().background ==
          app.shell.theme().colors.accentContainer);
    CHECK(grid->checked);

    // disabled：disabled.content 前景 + 透明底（稿件 .is-disabled；命中拒绝
    // 与键盘跳过分别见 toolbar_disabled_item_rejects_click 与漫游用例）。
    const RenderNode* undo = findNodeByKey(app.shell.root(), "tb:item:undo");
    REQUIRE(undo != nullptr);
    CHECK(undo->commonStyle().foreground ==
          app.shell.theme().colors.disabledContent);
    CHECK(undo->commonStyle().background == Color::transparent());
    CHECK_FALSE(undo->enabled);
}

// 栏底满幅（§9.2 toolbar.bar.background = surface）：底色记在栏容器上——
// 行按内容自收缩，栏宽大于内容时右段不再露出父级背景。
TEST_CASE("toolbar_bar_surface_spans_full_bar_width", "[widgets][toolbar]") {
    ToolApp app;
    const RenderNode* bar = findNodeByKey(app.shell.root(), "tb:bar");
    REQUIRE(bar != nullptr);
    CHECK(bar->commonStyle().background == app.shell.theme().colors.surface);
    const RenderNode* row = findNodeByKey(app.shell.root(), "tb:row");
    REQUIRE(row != nullptr);
    CHECK(row->size.width < bar->size.width);
}

TEST_CASE("toolbar_overflow_button_aligns_right", "[widgets][toolbar]") {
    std::vector<ToolBarItem> items;
    for (int i = 0; i < 8; ++i) {
        items.push_back(ToolBarItem{.id = "cmd" + std::to_string(i),
                                    .icon = IconId::Grid, .label = "c"});
    }
    ToolApp app{items};
    app.resize(300.0F);
    // 溢出按钮右贴栏缘（design/toolbar.html .tb-overflow margin-left:auto）。
    const RenderNode* row = findNodeByKey(app.shell.root(), "tb:row");
    REQUIRE(row != nullptr);
    const RenderNode* overflow =
        findNodeByKey(app.shell.root(), "tb:overflow");
    REQUIRE(overflow != nullptr);
    CHECK(overflow->offset.x + overflow->size.width ==
          Catch::Approx(row->size.width - 8.0F).margin(0.5F));
}
