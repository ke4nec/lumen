// Widget Gallery 集成测试（与 settings_integration_tests 同风格）：
// 导航/计数、输入联动（下拉/页签/表单双路径）、弹窗统一规则、主题派生
// 保留（高对比 × 深浅/强调色）、虚拟列表物化窗口、滑杆联动与语义覆盖。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <string>

#include "gallery_app.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/interaction.h"
#include "lumen/core/scrollbar.h"
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

// Scroll the outer gallery-list so the node enters the viewport. The wheel
// at the list center falls inside the nested collection widgets (the inner
// List/Tree/TreeList takes the gesture), so drive the outer controller
// directly (same approach as GalleryApp::showSample).
void scrollIntoView(GalleryApp& app, const std::string& key) {
    const RenderNode* node = findNodeByKey(app.root(), key);
    const RenderNode* viewport = findNodeByKey(app.root(), "gallery-list");
    REQUIRE(node != nullptr);
    REQUIRE(viewport != nullptr);
    app.scroll().updateExtents(viewport->size.height,
                               viewport->size.height +
                                   viewport->scrollExtent);
    const auto position = absoluteOffset(app.root(), key);
    const auto top = absoluteOffset(app.root(), "gallery-list");
    app.scroll().scrollTo(app.scroll().offset() + position.y - top.y -
                          12.0F);
    app.markDirty();
    (void)app.renderFrame();
}

void clickScrolled(GalleryApp& app, const std::string& key) {
    scrollIntoView(app, key);
    click(app, key);
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

TEST_CASE("gallery_collections_showcase_interacts", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-collections");

    // The example setting hides row rings while selection remains usable.
    clickVisible(app, "collection-focus-rings");
    CHECK(app.state().get("collection-focus-rings") == "false");
    // List：单击选中（Extended；current 与 selected 同步）。
    clickVisible(app, "collection-list:item:i1");
    (void)app.renderFrame();
    CHECK(app.collectionList().selection().currentKey() == "i1");
    CHECK(app.collectionList().selection().isSelected("i1"));
    REQUIRE(findNodeByKey(app.root(), "collection-list:item:i1") != nullptr);
    CHECK(findNodeByKey(app.root(), "collection-list:item:i1")->commonStyle().focusWidth == 0.0F);

    // 同行再点击两次（400ms 内第二组 down/up）→ 双击激活回显。
    clickVisible(app, "collection-list:item:i1");
    clickVisible(app, "collection-list:item:i1");
    (void)app.renderFrame();
    CHECK(app.lastActivatedKey() == "i1");

    // Tree：chevron 点击只展开（不选中该行）。树卡在外层视口下方，
    // 经 scrollIntoView 滚入（滚轮中心点会落进内层集合被其消费）。
    clickScrolled(app, "collection-tree:chev:tree:src:widgets");
    CHECK(app.collectionTree().isExpanded("tree:src:widgets"));
    CHECK_FALSE(app.collectionTree().selection().isSelected("tree:src:widgets"));
    // 初始展开两层：src 与 src:core 均展开。
    CHECK(app.collectionTree().isExpanded("tree:src"));

    // TreeList：粘性表头存在；点击 name 表头 → 应用排序（首次升序）。
    const RenderNode* header =
        findNodeByKey(app.root(), "collection-table:header");
    REQUIRE(header != nullptr);
    clickScrolled(app, "collection-table:head:name");
    CHECK(app.lastSortColumn() == "name");
    CHECK_FALSE(app.lastSortDescending());
    // 再次点击 → 方向翻转。
    clickScrolled(app, "collection-table:head:name");
    CHECK(app.lastSortDescending());

    // 三个集合均只物化可见窗口（O(visible)）；TreeList 表头在末子节点
    //（粘性 chrome，盖住滚入的行区）。
    const RenderNode* list = findNodeByKey(app.root(), "collection-list");
    const RenderNode* tree = findNodeByKey(app.root(), "collection-tree");
    const RenderNode* table = findNodeByKey(app.root(), "collection-table");
    REQUIRE(list != nullptr);
    REQUIRE(tree != nullptr);
    REQUIRE(table != nullptr);
    CHECK(list->children.size() < 200);
    CHECK(findNodeByKey(app.root(), "collection-list-states") != nullptr);
    CHECK(findNodeByKey(app.root(), "collection-empty-list:empty") != nullptr);
    CHECK(!tree->children.empty());
    CHECK(!table->children.empty());
    CHECK(table->children.back().key == "collection-table:header");
    CHECK(table->children.size() <= 10);  // 表头 + 9 行数据（全部物化）。
    for (const auto mode : {widgets::SelectionMode::None, widgets::SelectionMode::Single,
                            widgets::SelectionMode::Multiple, widgets::SelectionMode::Extended}) {
        clickScrolled(app, "collection-list-mode");
        CHECK(app.collectionList().selection().mode() == mode);
    }
    // Tree previews remain independent when the live model becomes empty.
    app.collectionTree().setModel(nullptr);
    (void)app.renderFrame();
    CHECK(findNodeByKey(app.root(), "collection-tree:empty") != nullptr);
    CHECK(findNodeByKey(app.root(), "collection-tree-states") != nullptr);
    CHECK(findNodeByKey(app.root(), "collection-empty-tree:empty") != nullptr);
}

// TreeList 表头几何回归（collection-design §8.3/§10.3）：表头不透明、
// 置顶粘性、与行列同口径、右端不越视口（滚动条落在右 pad 空区）。
TEST_CASE("gallery_treelist_header_geometry", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-collections");
    scrollIntoView(app, "collection-table");

    const RenderNode* table = findNodeByKey(app.root(), "collection-table");
    REQUIRE(table != nullptr);
    REQUIRE(!table->children.empty());
    // 表头在末尾（绘制/命中盖住行区）、置顶、不透明 surface 底。
    const RenderNode& header = table->children.back();
    CHECK(header.key == "collection-table:header");
    CHECK(header.offset.y == Catch::Approx(0.0F).margin(0.01F));
    CHECK(header.commonStyle().background == app.theme().colors.surface);
    // 首行紧贴表头下方。
    const RenderNode& row0 = table->children.front();
    CHECK(row0.offset.y ==
          Catch::Approx(header.size.height).margin(0.01F));
    // 表头/行首列同起（左 pad 后），行末盒右端不越视口。
    const Offset tableAbs = absoluteOffset(app.root(), "collection-table");
    const Offset headAbs =
        absoluteOffset(app.root(), "collection-table:head:name");
    CHECK(headAbs.x ==
          Catch::Approx(tableAbs.x + 12.0F).margin(0.01F));
    const RenderNode* row =
        findNodeByKey(app.root(), "collection-table:item:dep-core");
    REQUIRE(row != nullptr);
    REQUIRE(!row->children.empty());
    const RenderNode& rowCells = row->children.front();
    REQUIRE(!rowCells.children.empty());
    const float rowAbsX =
        tableAbs.x + row->offset.x + rowCells.offset.x +
        rowCells.children.front().offset.x;
    CHECK(rowAbsX == Catch::Approx(headAbs.x).margin(0.01F));
    const RenderNode& lastBox = rowCells.children.back();
    const float rightEdge =
        tableAbs.x + row->offset.x + rowCells.offset.x +
        lastBox.offset.x + lastBox.size.width;
    CHECK(rightEdge <= tableAbs.x + table->size.width + 0.01F);

    // 内滚 40px：表头仍置顶置末，行滑入其下（不透明底盖住，无叠字）。
    app.wheel(absoluteOffset(app.root(), "collection-table") +
                  Offset{40.0F, 60.0F},
              Offset{0.0F, 40.0F});
    (void)app.renderFrame();
    table = findNodeByKey(app.root(), "collection-table");
    REQUIRE(table != nullptr);
    REQUIRE(!table->children.empty());
    CHECK(table->children.back().key == "collection-table:header");
    CHECK(table->children.back().offset.y == Catch::Approx(0.0F)
                                                 .margin(0.01F));
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

// 集合控件滚轮回归：框架直接驱动源控制器（不经应用 onWheel 接线），
// 内层集合消费滚轮且偏移落到布局节点。
TEST_CASE("gallery_collections_wheel_scrolls_inner_collection", "[gallery]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-collections");
    scrollIntoView(app, "collection-list");

    const RenderNode* list = findNodeByKey(app.root(), "collection-list");
    REQUIRE(list != nullptr);
    const float before = app.collectionList().scroll().offset();
    const Offset point = absoluteOffset(app.root(), "collection-list") +
                         Offset{40.0F, 20.0F};
    app.wheel(point, Offset{0.0F, 120.0F});
    CHECK(app.collectionList().scroll().offset() == before + 120.0F);
    (void)app.renderFrame();
    list = findNodeByKey(app.root(), "collection-list");
    REQUIRE(list != nullptr);
    CHECK(list->scrollOffset == 120.0F);

    // Tree / TreeList 同路径。
    scrollIntoView(app, "collection-tree");
    const float treeBefore = app.collectionTree().scroll().offset();
    app.wheel(
        absoluteOffset(app.root(), "collection-tree") + Offset{40.0F, 20.0F},
        Offset{0.0F, 120.0F});
    CHECK(app.collectionTree().scroll().offset() == treeBefore + 120.0F);
    (void)app.renderFrame();
    const RenderNode* tree = findNodeByKey(app.root(), "collection-tree");
    REQUIRE(tree != nullptr);
    CHECK(tree->scrollOffset == 120.0F);

    scrollIntoView(app, "collection-table");
    const float tableBefore = app.collectionTable().scroll().offset();
    app.wheel(
        absoluteOffset(app.root(), "collection-table") + Offset{40.0F, 20.0F},
        Offset{0.0F, 120.0F});
    CHECK(app.collectionTable().scroll().offset() == tableBefore + 120.0F);
    (void)app.renderFrame();
    const RenderNode* table = findNodeByKey(app.root(), "collection-table");
    REQUIRE(table != nullptr);
    CHECK(table->scrollOffset == 120.0F);
}

TEST_CASE("gallery_inner_scrollview_owns_wheel_and_thumb_without_moving_page", "[gallery][scrollbar]") {
    GalleryApp app;
    app.setView({1024, 768});
    static_cast<void>(app.renderFrame());
    go(app, "nav-lists");
    scrollIntoView(app, "gallery-scrollview");
    const float outer = app.scroll().offset();
    const auto position = absoluteOffset(app.root(), "gallery-scrollview");
    REQUIRE(findNodeByKey(app.root(), "gallery-scrollview")->scrollExtent >= 30);
    CHECK(app.shell().wheel(position + Offset{20, 20}, {0, 30}));
    static_cast<void>(app.renderFrame());
    CHECK(findNodeByKey(app.root(), "gallery-scrollview")->scrollOffset == Catch::Approx(30));
    CHECK(app.scroll().offset() == outer);
    CHECK(app.shell().wheel(position + Offset{20, 20}, {0, -20}));
    static_cast<void>(app.renderFrame());
    CHECK(findNodeByKey(app.root(), "gallery-scrollview")->scrollOffset == Catch::Approx(10));
    const auto bar = scrollbarGeometry(*findNodeByKey(app.root(), "gallery-scrollview"));
    REQUIRE(bar);
    const auto point = position + bar->thumbHit.origin + Offset{1, bar->thumbHit.size.height * .5F};
    app.shell().pointerMove(point);
    CHECK(app.shell().pointerCursor() == PointerCursor::PointingHand);
    app.shell().pointerDown(point);
    app.shell().pointerMove(point + Offset{0, 8});
    static_cast<void>(app.renderFrame());
    CHECK(findNodeByKey(app.root(), "gallery-scrollview")->scrollOffset > 10);
    CHECK(app.scroll().offset() == outer);
    app.shell().pointerUp(point + Offset{0, 8});
    CHECK(app.scroll().offset() == outer);
}

TEST_CASE("gallery_scrollbar_cancel_does_not_seed_the_next_content_fling", "[gallery][scrollbar][review]") {
    GalleryApp app;
    app.setView({1024, 768});
    static_cast<void>(app.renderFrame());
    go(app, "nav-lists");
    const auto* viewport = findNodeByKey(app.root(), "gallery-list");
    REQUIRE(viewport);
    const auto bar = scrollbarGeometry(*viewport);
    REQUIRE(bar);
    const auto origin = absoluteOffset(app.root(), "gallery-list");
    const auto point = origin + bar->thumbHit.origin + Offset{1, bar->thumbHit.size.height * .5F};
    app.shell().tick(100);
    app.shell().pointerDown(point);
    app.shell().tick(110);
    app.shell().pointerMove(point + Offset{0, 20});
    app.shell().pointerCancel();
    static_cast<void>(app.renderFrame());
    REQUIRE(app.scroll().offset() > 0);
    // One content sample and release in the same tick has no measured velocity.
    app.shell().tick(120);
    const auto content = origin + Offset{4, 100};
    app.shell().pointerDown(content);
    app.shell().pointerMove(content + Offset{0, -12});
    app.shell().pointerUp(content + Offset{0, -12});
    CHECK_FALSE(app.scroll().isFlinging());
}

TEST_CASE("gallery_wheel_over_empty_collections_scrolls_outer_page", "[gallery][wheel-routing]") {
    GalleryApp app;
    app.setView(Size{1024.0F, 768.0F});
    (void)app.renderFrame();
    go(app, "nav-collections");
    for (const auto* key : {"collection-empty-list", "collection-empty-tree"}) {
        CAPTURE(key);
        scrollIntoView(app, key);
        const auto* child = findNodeByKey(app.root(), key);
        REQUIRE(child != nullptr);
        REQUIRE(child->scrollExtent == 0.0F);
        const float before = app.scroll().offset();
        REQUIRE(before >= 30.0F);
        // The Tree card can already be at the page bottom; scroll upward.
        CHECK(app.shell().wheel(centerOf(app.root(), key), {0, -30}));
        (void)app.renderFrame();
        CHECK(app.scroll().offset() == before - 30.0F);
    }
}

// 菜单类控件（menu-controls-design §11.3，对齐 design/gallery.html 增补）：
// chrome 菜单栏打开/命令执行 + 主内容区右键菜单 + Menus 分区演示。
TEST_CASE("gallery_menu_bar_and_context_menu", "[gallery][menu]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();

    // chrome 菜单栏在主树（File/View/Help）。
    REQUIRE(findNodeByKey(app.root(), "menu:bar:file") != nullptr);
    REQUIRE(findNodeByKey(app.root(), "menu:bar:view") != nullptr);

    // 点击 View 栏项：面板锚定栏项下方打开。
    click(app, "menu:bar:view");
    (void)app.renderFrame();
    REQUIRE(app.menuBarOpen());
    REQUIRE(findNodeByKey(*app.shell().overlayRoot(), "menubar:panel:0") !=
            nullptr);

    // 激活 toggle-sidebar（View 首项）：侧栏隐藏 + 命令回显。
    clickOverlayOption(app, "menubar:m0:i0");
    (void)app.renderFrame();
    CHECK(app.lastMenuCommand() == "toggle-sidebar");
    CHECK_FALSE(app.sidebarVisible());
    CHECK(findNodeByKey(app.root(), "gallery-nav") == nullptr);

    // 主内容区右键：指针位置唤起 ContextMenu（全窗 barrier）。
    const Offset content = centerOf(app.root(), "home-hero");
    app.shell().pointerDown(content, kModifierNone, PointerButton::Secondary);
    app.shell().pointerUp(content, PointerButton::Secondary);
    (void)app.renderFrame();
    REQUIRE(app.shell().overlayRoot() != nullptr);
    REQUIRE(findNodeByKey(*app.shell().overlayRoot(), "ctx:panel:0") !=
            nullptr);

    // 激活 Open：命令经统一 onCommand 回显（菜单关闭）。
    clickOverlayOption(app, "ctx:m0:i0");
    (void)app.renderFrame();
    CHECK(app.lastMenuCommand() == "open");
    CHECK_FALSE(app.menuBarOpen());

    // 再开 View 恢复侧栏（checkable 状态经 provider 重读 → 项为未勾选）。
    click(app, "menu:bar:view");
    clickOverlayOption(app, "menubar:m0:i0");
    (void)app.renderFrame();
    CHECK(app.sidebarVisible());
    CHECK(findNodeByKey(app.root(), "gallery-nav") != nullptr);

    // Menus 分区：右键演示行（menu-target: 前缀）行级菜单。
    go(app, "nav-menus");
    REQUIRE(findNodeByKey(app.root(), "menus-title") != nullptr);
    const Offset target = centerOf(app.root(), "menu-target:notes.md");
    app.shell().pointerDown(target, kModifierNone, PointerButton::Secondary);
    (void)app.renderFrame();
    REQUIRE(findNodeByKey(*app.shell().overlayRoot(), "ctx:panel:0") !=
            nullptr);
    clickOverlayOption(app, "ctx:m0:i0");
    (void)app.renderFrame();
    CHECK(app.lastMenuCommand() == "open:notes.md");
}

// Splitter（splitter-design §10.3）：断点跟随（200/168）→ 手动调节后
// KeepOffset → View 菜单复位恢复断点。
TEST_CASE("gallery_splitter_breakpoint_and_user_offset", "[gallery][splitter]") {
    GalleryApp app;
    app.setView(Size{1280.0F, 800.0F});
    (void)app.renderFrame();
    const auto navWidth = [&app] {
        const RenderNode* nav = findNodeByKey(app.root(), "gallery-nav");
        REQUIRE(nav != nullptr);
        return nav->size.width;
    };

    // 未手动调节：跟随响应式断点（>1024 → 200；<1024 → 168）。
    CHECK(navWidth() == 200.0F);
    app.setView(Size{900.0F, 800.0F});
    (void)app.renderFrame();
    CHECK(navWidth() == 168.0F);

    // 手动拖动：用户位置优先，此后窗口变宽不再回断点（KeepOffset）。
    app.sidebarSplitter().dragTo(260.0F);
    app.markDirty();
    (void)app.renderFrame();
    CHECK(navWidth() == 260.0F);
    app.setView(Size{1400.0F, 900.0F});
    (void)app.renderFrame();
    CHECK(navWidth() == 260.0F);

    // View 菜单 Reset pane layout：回断点并恢复跟随。
    click(app, "menu:bar:view");
    clickOverlayOption(app, "menubar:m0:i1");
    (void)app.renderFrame();
    CHECK(app.lastMenuCommand() == "reset-panes");
    CHECK(navWidth() == 200.0F);
    app.setView(Size{900.0F, 800.0F});
    (void)app.renderFrame();
    CHECK(navWidth() == 168.0F);
}
