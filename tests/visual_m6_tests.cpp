// M6（自用路线图）测试：视觉系统 V3 与控件库。
//
// 覆盖：图标折线目录与 DrawIcon 命令（CPU 光栅/Skia 路径共用命令）、
// 序列化 v4 roundtrip、阴影命令与 CPU 降级、滚动条绘制、六控件
//（Slider 拖动/键盘、ProgressBar 值、Radio 切换、Dropdown 展开、
// Tabs 容器）、语义 role/value 固化。

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "lumen/core/icon_id.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/app/app_shell.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/render/render_commands.h"
#include "lumen/style/theme.h"
#include "lumen/widgets/dropdown.h"
#include "lumen/widgets/navigator.h"

using namespace lumen;
using namespace lumen::core;
using Catch::Approx;
using lumen::layout::LayoutEngine;

namespace {

Constraints tightView(float width, float height) {
    return Constraints::tight(Size{width, height});
}

RenderNode layoutOf(const Widget& widget, float width = 400.0F,
                    float height = 300.0F) {
    return LayoutEngine::layout(widget, tightView(width, height));
}

Offset centerOf(const RenderNode& root, const std::string& key) {
    const RenderNode* node = findNodeByKey(root, key);
    REQUIRE(node != nullptr);
    return absoluteOffset(root, key) +
           Offset{node->size.width * 0.5F, node->size.height * 0.5F};
}

}  // namespace

// --- 图标目录与命令 ---

TEST_CASE("icon_catalog_provides_normalized_polylines", "[visual][m6]") {
    for (IconId id :
         {IconId::Check, IconId::Close, IconId::ChevronDown,
          IconId::ChevronRight, IconId::ChevronLeft, IconId::ChevronUp,
          IconId::Alert, IconId::Plus, IconId::Minus, IconId::Search,
          IconId::Info, IconId::Maximize, IconId::GalleryLogo}) {
        const auto& polylines = iconPolylines(id);
        REQUIRE_FALSE(polylines.empty());
        for (const auto& polyline : polylines) {
            REQUIRE(polyline.size() >= 2);
            for (const Offset& point : polyline) {
                CHECK(point.x >= 0.0F);
                CHECK(point.x <= 1.0F);
                CHECK(point.y >= 0.0F);
                CHECK(point.y <= 1.0F);
            }
        }
    }
    // None：无几何。
    CHECK(iconPolylines(IconId::None).empty());
}

TEST_CASE("icon_node_records_draw_icon_command", "[visual][m6]") {
    const RenderNode root =
        layoutOf(withKey(makeIcon(IconId::Check, "tick"), "tick"));
    const auto commands = render::recordScene(root);
    bool sawIcon = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawIcon) {
            sawIcon = true;
            REQUIRE_FALSE(command.polylines.empty());
            CHECK(command.strokeWidth > 0.0F);
            CHECK(command.color.a > 0);
        }
    }
    CHECK(sawIcon);

    // CPU 光栅：图标区域产生非背景像素。
    render::CpuRenderer renderer;
    renderer.beginFrame(Size{400.0F, 300.0F});
    render::paintScene(renderer, root);
    renderer.endFrame();
    const auto* node = findNodeByKey(root, "tick");
    REQUIRE(node != nullptr);
    const auto origin = absoluteOffset(root, "tick");
    int painted = 0;
    for (int y = static_cast<int>(origin.y);
         y < static_cast<int>(origin.y + node->size.height); ++y) {
        for (int x = static_cast<int>(origin.x);
             x < static_cast<int>(origin.x + node->size.width); ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * 400 + x) * 4;
            if (renderer.pixels().rgba[offset + 3] > 0) {
                ++painted;
            }
        }
    }
    CHECK(painted > 4);
}

TEST_CASE("button_with_icon_records_icon_beside_label", "[visual][m6]") {
    Widget button = withIcon(makeButton("Add"), IconId::Plus);
    button.onClick = "add";
    button.key = "add";
    const RenderNode root = layoutOf(button);
    const auto commands = render::recordScene(root);
    std::size_t iconCommands = 0;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawIcon) {
            ++iconCommands;
        }
    }
    CHECK(iconCommands == 1);
}

TEST_CASE("command_serialization_v4_round_trips_icons_and_shadows",
          "[render][commands][m6]") {
    render::RenderCommandList list;
    list.drawIcon(
        std::vector<std::vector<Offset>>{
            {{0.1F, 0.2F}, {0.8F, 0.9F}},
            {{0.5F, 0.0F}, {0.5F, 1.0F}}},
        Rect{Offset{10.0F, 20.0F}, Size{30.0F, 30.0F}},
        Color::fromRGBA(1, 2, 3), 2.5F);
    list.drawShadow(Rect{Offset{5.0F, 6.0F}, Size{100.0F, 50.0F}},
                    Color::fromRGBA(0, 0, 0, 96), Offset{0.0F, 4.0F},
                    12.0F);

    const std::string blob = render::serializeCommands(list);
    render::RenderCommandList parsed;
    REQUIRE(render::deserializeCommands(blob, parsed));
    CHECK(parsed == list);

    // 损坏拒绝。
    render::RenderCommandList out;
    CHECK_FALSE(render::deserializeCommands(blob.substr(0, blob.size() - 8),
                                            out));
}

// --- 阴影（Skia blur / CPU 降级） ---

TEST_CASE("elevated_node_records_shadow_command", "[visual][m6]") {
    Widget card = makeContainer(makeText("Elevated"));
    card.elevation = 3.0F;
    card.key = "card";
    Widget page;
    page.key = "root";
    page.children = {std::move(card)};

    const RenderNode root = layoutOf(page);
    const RenderNode* cardNode = findNodeByKey(root, "card");
    REQUIRE(cardNode != nullptr);
    CHECK(cardNode->elevation == Catch::Approx(3.0F));
    CHECK(cardNode->shadowColor.a > 0);
    CHECK(cardNode->shadowBlur > 0.0F);

    const auto commands = render::recordScene(root);
    bool sawShadow = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawShadow) {
            sawShadow = true;
            // 命令一致性：CPU/Skia/GPU 收到并消费同一份数据。
            CHECK(command.color.a > 0);
        }
    }
    CHECK(sawShadow);
}

// --- 阴影软模糊（CPU 3-pass box blur 近似，σ 与 Skia 同口径） ---

namespace {

// 白底 + 纯黑阴影：像素强度 = 1 - r/255（红色通道测量 bg 全 255），
// 即 coverage × color.a 的合成结果。
float shadowStrengthAt(const render::CpuRenderer& renderer, int width,
                       int x, int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) *
        4;
    return 1.0F - static_cast<float>(renderer.pixels().rgba[offset]) /
                      255.0F;
}

}  // namespace

TEST_CASE("cpu_soft_shadow_blurs_monotonically_beyond_edges",
          "[visual][m6]") {
    render::CpuRenderer renderer(1.0F, Color{255, 255, 255});
    renderer.beginFrame(Size{400.0F, 300.0F});
    renderer.drawShadow(Rect{Offset{50.0F, 50.0F}, Size{200.0F, 100.0F}},
                        Color{0, 0, 0, 255}, Offset{0.0F, 4.0F}, 12.0F);
    renderer.endFrame();

    // 偏移后矩形 y∈[54,154]、x∈[50,250]；σ = blur*0.5*scale = 6。
    // 深处覆盖≈1；跨过边缘单调衰减；3σ 之外≈0。
    CHECK(shadowStrengthAt(renderer, 400, 150, 100) > 0.95F);
    float previous = shadowStrengthAt(renderer, 400, 150, 150);
    for (int y = 151; y <= 180; ++y) {
        const float strength = shadowStrengthAt(renderer, 400, 150, y);
        CHECK(strength <= previous + 0.01F);
        previous = strength;
    }
    CHECK(shadowStrengthAt(renderer, 400, 150, 175) < 0.02F);
    previous = shadowStrengthAt(renderer, 400, 245, 100);
    for (int x = 246; x <= 275; ++x) {
        const float strength = shadowStrengthAt(renderer, 400, x, 100);
        CHECK(strength <= previous + 0.01F);
        previous = strength;
    }
    CHECK(shadowStrengthAt(renderer, 400, 275, 100) < 0.02F);
}

TEST_CASE("cpu_soft_shadow_conserves_energy_within_tolerance",
          "[visual][m6]") {
    render::CpuRenderer renderer(1.0F, Color{255, 255, 255});
    renderer.beginFrame(Size{400.0F, 300.0F});
    renderer.drawShadow(Rect{Offset{50.0F, 50.0F}, Size{200.0F, 100.0F}},
                        Color{0, 0, 0, 255}, Offset{0.0F, 4.0F}, 12.0F);
    renderer.endFrame();
    // box blur 卷积守恒（域外计 0）：总强度 ≈ 矩形面积；±30% 容差带
    // 防过糊/欠糊回归。
    double total = 0.0;
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 400; ++x) {
            total += shadowStrengthAt(renderer, 400, x, y);
        }
    }
    CHECK(total == Approx(200.0 * 100.0).epsilon(0.30));
}

TEST_CASE("cpu_soft_shadow_extent_stays_within_damage_outset",
          "[visual][m6]") {
    render::CpuRenderer renderer(1.0F, Color{255, 255, 255});
    renderer.beginFrame(Size{400.0F, 300.0F});
    // blur=24：damage 口径外扩 blur*2+1 = 49（core/damage.cpp）；可见
    // 模糊尾（≈2.5σ）必须落在该外扩内，局部 damage 才不漏刷。
    const Rect box{Offset{60.0F, 40.0F}, Size{180.0F, 120.0F}};
    const Offset shadowOffset{0.0F, 8.0F};
    renderer.drawShadow(box, Color{0, 0, 0, 255}, shadowOffset, 24.0F);
    renderer.endFrame();
    const float outset = 24.0F * 2.0F + 1.0F;
    const Rect painted{box.origin + shadowOffset, box.size};
    int minX = 400;
    int minY = 300;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < 300; ++y) {
        for (int x = 0; x < 400; ++x) {
            if (shadowStrengthAt(renderer, 400, x, y) > 0.0F) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    REQUIRE(maxX >= minX);
    CHECK(static_cast<float>(minX) >= painted.left() - outset);
    CHECK(static_cast<float>(minY) >= painted.top() - outset);
    CHECK(static_cast<float>(maxX) < painted.right() + outset);
    CHECK(static_cast<float>(maxY) < painted.bottom() + outset);
    // 模糊确实发生了：绘制范围超出矩形本体。
    CHECK(minY < static_cast<int>(painted.top()));
    CHECK(maxY >= static_cast<int>(painted.bottom()));
}

TEST_CASE("cpu_soft_shadow_sigma_scales_with_device_scale",
          "[visual][m6]") {
    const auto edgeFalloffDistance = [](float scale) {
        render::CpuRenderer renderer(scale, Color{255, 255, 255});
        renderer.beginFrame(Size{400.0F, 300.0F});
        renderer.drawShadow(
            Rect{Offset{50.0F, 50.0F}, Size{200.0F, 100.0F}},
            Color{0, 0, 0, 255}, Offset{0.0F, 4.0F}, 12.0F);
        renderer.endFrame();
        const int deviceWidth = static_cast<int>(400.0F * scale);
        const float rectBottomDevice = (50.0F + 4.0F + 100.0F) * scale;
        const int sampleX = static_cast<int>(150.0F * scale);
        int last = -1;
        const int yLimit = static_cast<int>(300.0F * scale);
        for (int y = static_cast<int>(rectBottomDevice); y < yLimit; ++y) {
            if (shadowStrengthAt(renderer, deviceWidth, sampleX, y) >=
                0.05F) {
                last = y;
            }
        }
        REQUIRE(last >= 0);
        return static_cast<float>(last) - rectBottomDevice + 1.0F;
    };
    const float d1 = edgeFalloffDistance(1.0F);
    const float d2 = edgeFalloffDistance(2.0F);
    // σ = blur*0.5*scale：scale=2 的衰减距离约 2×（±25% 像素量化容差）。
    CHECK(d2 == Approx(2.0F * d1).epsilon(0.25));
}

TEST_CASE("cpu_shadow_without_blur_keeps_flat_plane_fallback",
          "[visual][m6]") {
    render::CpuRenderer renderer(1.0F, Color{255, 255, 255});
    renderer.beginFrame(Size{400.0F, 300.0F});
    renderer.drawShadow(Rect{Offset{50.0F, 50.0F}, Size{200.0F, 100.0F}},
                        Color{0, 0, 0, 200}, Offset{0.0F, 4.0F}, 0.0F);
    renderer.endFrame();
    // blur=0 防御路径：内部均匀半强度扁平面，边界 1px 内硬切。
    const float half = 200.0F / 255.0F * 0.5F;
    CHECK(shadowStrengthAt(renderer, 400, 150, 100) ==
          Approx(half).epsilon(0.02));
    CHECK(shadowStrengthAt(renderer, 400, 249, 153) ==
          Approx(half).epsilon(0.02));
    CHECK(shadowStrengthAt(renderer, 400, 251, 100) == 0.0F);
    CHECK(shadowStrengthAt(renderer, 400, 150, 155) == 0.0F);
}

// --- 滚动条 ---

TEST_CASE("scrollbar_draws_thumb_for_scrollable_viewport", "[visual][m6]") {
    std::vector<Widget> rows;
    for (int i = 0; i < 30; ++i) {
        Widget row = makeText("row");
        row.key = "row-" + std::to_string(i);
        row.height = 30.0F;
        rows.push_back(std::move(row));
    }
    Widget scroll = withScrollbar(
        makeScrollView(makeColumn(std::move(rows)), "scroll", std::nullopt,
                       200.0F),
        true);
    scroll.scrollOffset = 300.0F;
    Widget page;
    page.key = "root";
    page.children = {std::move(scroll)};

    const RenderNode root = layoutOf(page);
    const RenderNode* viewport = findNodeByKey(root, "scroll");
    REQUIRE(viewport != nullptr);
    CHECK(viewport->scrollbarThickness > 0.0F);
    CHECK(viewport->scrollExtent > 0.0F);

    const auto commands = render::recordScene(root);
    // thumb 以 DrawRect 呈现（右缘窄条）。
    bool sawThumb = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawRect &&
            command.rect.size.width <= viewport->scrollbarThickness) {
            sawThumb = true;
        }
    }
    CHECK(sawThumb);
}

// --- 控件库 ---

TEST_CASE("slider_pointer_and_keyboard_set_value", "[controls][m6]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("volume", "20");

    Widget slider = makeSlider("volume", "volume");
    Widget page = makeColumn({std::move(slider)});
    page.key = "root";
    const RenderNode root = layoutOf(page, 400.0F, 100.0F);

    // 点击轨道 75% 处。
    const RenderNode* node = findNodeByKey(root, "volume");
    REQUIRE(node != nullptr);
    const auto origin = absoluteOffset(root, "volume");
    const Offset at75{origin.x + node->size.width * 0.75F,
                      origin.y + node->size.height * 0.5F};
    controller.pointerDown(root, at75);
    controller.pointerUp(root, at75);
    CHECK(std::atoi(store.get("volume").c_str()) ==
          Catch::Approx(75).margin(2));

    const auto tree = accessibility::buildSemanticsTree(root);
    const auto* semantic = tree.find(node->identity);
    REQUIRE(semantic != nullptr);
    CHECK(semantic->role == accessibility::SemanticsRole::Slider);
    accessibility::SemanticsActionContext context;
    context.root = &root;
    context.focus = &focus;
    context.controller = &controller;
    controller.focusNode(*node);
    CHECK(accessibility::performSemanticsAction(
              tree, context, node->identity, accessibility::kActionSetValue,
              "91") == accessibility::SemanticsActionStatus::Handled);
    CHECK(store.get("volume") == "91");

    // 键盘：聚焦 Slider 后 Left/Right ±5。
    controller.keyDown(root, Key::Right);
    CHECK(std::atoi(store.get("volume").c_str()) ==
          Catch::Approx(96).margin(0));
    controller.keyDown(root, Key::Left);
    controller.keyDown(root, Key::Left);
    CHECK(std::atoi(store.get("volume").c_str()) ==
          Catch::Approx(86).margin(0));
}

TEST_CASE("progress_bar_records_value_semantics", "[controls][m6]") {
    Widget bar = makeProgressBar("64", "load");
    Widget page;
    page.key = "root";
    page.children = {std::move(bar)};
    const RenderNode root = layoutOf(page, 400.0F, 60.0F);
    const auto tree = accessibility::buildSemanticsTree(root);
    const auto* node = tree.find(findNodeByKey(root, "load")->identity);
    REQUIRE(node != nullptr);
    CHECK(node->role == accessibility::SemanticsRole::ProgressBar);
    CHECK(node->value == "64");
    CHECK(node->actions == 0);  // 展示控件：无交互。
}

TEST_CASE("radio_toggles_through_same_path_as_checkbox", "[controls][m6]") {
    StateStore store;
    HandlerRegistry handlers;
    FocusManager focus;
    InteractionController controller(store, handlers, focus);
    store.set("mode", "false");

    Widget radio = makeRadio("Compact", "mode", "mode-radio");
    Widget page = makeColumn({std::move(radio)});
    page.key = "root";
    const RenderNode root = layoutOf(page, 300.0F, 80.0F);

    controller.pointerDown(root, centerOf(root, "mode-radio"));
    controller.pointerUp(root, centerOf(root, "mode-radio"));
    CHECK(store.get("mode") == "true");
    // 语义：Checkbox role + checked flag（重建后 bind 解析为 checked）。
    Widget again = makeRadio("Compact", "mode", "mode-radio");
    Widget page2 = makeColumn({std::move(again)});
    page2.key = "root";
    core::applyBinds(page2, store);
    const RenderNode root2 = layoutOf(page2, 300.0F, 80.0F);
    const auto tree = accessibility::buildSemanticsTree(root2);
    const auto* node =
        tree.find(findNodeByKey(root2, "mode-radio")->identity);
    REQUIRE(node != nullptr);
    CHECK(node->role == accessibility::SemanticsRole::Radio);
    CHECK((node->flags & accessibility::kSemanticsChecked) != 0);
}

TEST_CASE("dropdown_collapsed_leaf_and_floating_menu_flow", "[controls][m11]") {
    // M11：值行收起叶子（无选项子树、不挤压父布局）+ 浮动菜单流。
    Widget dropdown = makeDropdown("Green", "open-menu", "color");
    Widget page;
    page.key = "root";
    page.children = {std::move(dropdown)};
    const RenderNode root = layoutOf(page, 300.0F, 240.0F);

    const RenderNode* dropdownNode = findNodeByKey(root, "color");
    REQUIRE(dropdownNode != nullptr);
    CHECK(dropdownNode->children.empty());
    CHECK(dropdownNode->onClick == "open-menu");
    // 收起高度 = 单行控件（远小于三选项高度）。
    CHECK(dropdownNode->size.height < 60.0F);

    // 值行绘制带 ChevronDown 图标。
    const auto commands = render::recordScene(root);
    bool sawChevron = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawIcon) {
            sawChevron = true;
        }
    }
    CHECK(sawChevron);
    // 语义值为当前值。
    const auto tree = accessibility::buildSemanticsTree(root);
    const auto* node = tree.find(dropdownNode->identity);
    REQUIRE(node != nullptr);
    CHECK(node->value == "Green");
}

TEST_CASE("tabs_layout_as_row_and_carry_children", "[controls][m6]") {
    std::vector<Widget> tabs;
    for (const char* label : {"General", "Network", "About"}) {
        Widget tab = makeButton(label);
        tab.onClick = "tab";
        tab.key = std::string("tab-") + label;
        tabs.push_back(std::move(tab));
    }
    Widget tabsWidget = makeTabs(std::move(tabs), "tabs");
    Widget page;
    page.key = "root";
    page.children = {std::move(tabsWidget)};
    const RenderNode root = layoutOf(page, 500.0F, 60.0F);

    const RenderNode* tabsNode = findNodeByKey(root, "tabs");
    REQUIRE(tabsNode != nullptr);
    REQUIRE(tabsNode->children.size() == 3);
    // 行布局：第二个标签在第一个右侧。
    CHECK(tabsNode->children[1].offset.x >
          tabsNode->children[0].offset.x);
}

TEST_CASE("tabs_builder_opts_tab_buttons_into_focus_ring", "[controls][m6]") {
    // §6.8：页签是 Tab 停靠点、聚焦态无其他指示（底部指示条只随选中），
    // makeTabs 上下文为子按钮统一开环（§6.1 键盘表面 opt-in；普通按钮
    // 默认关闭不受影响）。
    Widget tabs = makeTabs({makeButton("General"), makeButton("About")}, "tabs");
    REQUIRE(tabs.children.size() == 2);
    CHECK(tabs.children[0].showFocusRing);
    CHECK(tabs.children[1].showFocusRing);
    CHECK_FALSE(makeButton("plain").showFocusRing);
}

TEST_CASE("tooltip_paints_text_and_surface", "[controls][m6]") {
    Widget tip = makeTooltip("Save changes (Ctrl+S)", "tip");
    Widget page;
    page.key = "root";
    page.children = {std::move(tip)};
    const RenderNode root = layoutOf(page, 400.0F, 60.0F);
    const RenderNode* tipNode = findNodeByKey(root, "tip");
    REQUIRE(tipNode != nullptr);
    CHECK(tipNode->size.width > 20.0F);
    const auto tree = accessibility::buildSemanticsTree(root);
    const auto* node = tree.find(tipNode->identity);
    REQUIRE(node != nullptr);
    CHECK(node->label == "Save changes (Ctrl+S)");
}

// --- S2（gui-control-visual-system-task §6.4）：指示器几何与部件 ---

TEST_CASE("checkbox_checked_draws_check_icon_not_inner_block", "[visual][s2]") {
    // 选中 = accent 填充 + IconId::Check 折线勾号（旧实现是内方块）。
    const RenderNode checkedRoot =
        layoutOf(makeCheckbox("A", "a", "cb", true));
    const auto checkedCommands = render::recordScene(checkedRoot);
    bool sawCheckIcon = false;
    for (const auto& command : checkedCommands.commands()) {
        if (command.type == render::CommandType::DrawIcon) {
            sawCheckIcon = command.polylines == iconPolylines(IconId::Check);
        }
    }
    CHECK(sawCheckIcon);

    // 未选 = surfaceSunken 内部 + borderStrong 描边轮廓（空心框）。
    const RenderNode offRoot = layoutOf(makeCheckbox("A", "a", "cb", false));
    const auto offCommands = render::recordScene(offRoot);
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    bool sawOutlineStroke = false;
    bool sawSunkenInterior = false;
    for (const auto& command : offCommands.commands()) {
        if (command.type == render::CommandType::DrawRectStroke &&
            command.color == theme.colors.borderStrong) {
            sawOutlineStroke = true;
        }
        if (command.type == render::CommandType::DrawRect &&
            command.color == theme.colors.surfaceSunken) {
            sawSunkenInterior = true;
        }
    }
    CHECK(sawOutlineStroke);
    CHECK(sawSunkenInterior);
}

TEST_CASE("checkbox_slot_reserves_focus_ring_space", "[visual][s2]") {
    // §4.4：槽位在指示器两侧预留焦点环 + 1px；标签从槽位边缘起算。
    const RenderNode root = layoutOf(makeCheckbox("Label", "a", "cb"));
    const RenderNode* node = findNodeByKey(root, "cb");
    REQUIRE(node != nullptr);
    const auto* checkbox =
        std::get_if<CheckboxResolvedStyle>(&node->style.component);
    REQUIRE(checkbox != nullptr);
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    const float ring = theme.metrics.focusRingWidth + 1.0F;
    CHECK(checkbox->slotSize ==
          checkbox->indicatorSize + 2.0F * ring);
    // 固有宽度 = 槽位 + labelGap + 标签宽（不再是裸指示器宽）。
    CHECK(node->size.width > checkbox->slotSize + checkbox->labelGap);
}

TEST_CASE("radio_resolves_dedicated_style_with_hollow_ring", "[visual][s2]") {
    // §6.4：Radio 专用解析——不再回落通用容器；外环为描边（空心）+
    // surfaceSunken 内部 + 独立 accent 内点。
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    const RenderNode checkedRoot =
        layoutOf(makeRadio("R", "r", "radio", true));
    const RenderNode* node = findNodeByKey(checkedRoot, "radio");
    REQUIRE(node != nullptr);
    const auto* radio =
        std::get_if<RadioResolvedStyle>(&node->style.component);
    REQUIRE(radio != nullptr);
    CHECK(radio->checked);
    CHECK(radio->indicator == theme.colors.surfaceSunken);
    CHECK(radio->indicatorChecked == theme.colors.accent);
    CHECK(radio->dot == theme.colors.accent);
    CHECK(radio->dotRatio == 0.45F);

    // 命令层：内点前有环描边（DrawRectStroke）与环内填充。
    const auto commands = render::recordScene(checkedRoot);
    bool sawRingStroke = false;
    bool sawInterior = false;
    bool sawDot = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawRectStroke &&
            command.color == theme.colors.accent) {
            sawRingStroke = true;
        }
        if (command.type == render::CommandType::DrawRect &&
            command.color == theme.colors.surfaceSunken) {
            sawInterior = true;
        }
        // 内点：以 accent 填充的小圆（宽 = 外径 × 0.45）。
        if (command.type == render::CommandType::DrawRect &&
            command.color == theme.colors.accent &&
            command.rect.size.width < 12.0F) {
            sawDot = true;
        }
    }
    CHECK(sawRingStroke);
    CHECK(sawInterior);
    CHECK(sawDot);

    // 像素层：勾选态环与内点之间的空隙 = surfaceSunken（非 accent 实心）。
    // 布局视口与帧缓冲一致（200×60），采样才落在缓冲内。
    const RenderNode pixelRoot =
        layoutOf(makeRadio("R", "r", "radio", true), 200.0F, 60.0F);
    render::CpuRenderer renderer;
    renderer.beginFrame(Size{200.0F, 60.0F});
    render::paintScene(renderer, pixelRoot);
    renderer.endFrame();
    const auto origin = absoluteOffset(pixelRoot, "radio");
    const auto* radioNode = findNodeByKey(pixelRoot, "radio");
    const auto& radioStyle =
        std::get<RadioResolvedStyle>(radioNode->style.component);
    const float cx =
        origin.x + (radioStyle.slotSize - radioStyle.indicatorSize) * 0.5F +
        radioStyle.indicatorSize * 0.5F;
    const float cy = origin.y + radioNode->size.height * 0.5F;
    const auto pixel = [&](float x, float y) {
        const int px = static_cast<int>(x);
        const int py = static_cast<int>(y);
        REQUIRE(px >= 0);
        REQUIRE(px < 200);
        REQUIRE(py >= 0);
        REQUIRE(py < 60);
        const std::size_t offset =
            (static_cast<std::size_t>(py) * 200 + static_cast<std::size_t>(px)) *
            4;
        return Color::fromRGBA(renderer.pixels().rgba[offset],
                               renderer.pixels().rgba[offset + 1],
                               renderer.pixels().rgba[offset + 2],
                               renderer.pixels().rgba[offset + 3]);
    };
    // 内点中心 = accent；环内空隙（约 0.35 外径处）= surfaceSunken。
    CHECK(pixel(cx, cy) == theme.colors.accent);
    const float gap = radioStyle.indicatorSize * 0.35F;
    CHECK(pixel(cx + gap, cy) == theme.colors.surfaceSunken);
}

TEST_CASE("switch_derives_knob_inset_and_two_knob_colors", "[visual][s2]") {
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    const RenderNode offRoot = layoutOf(makeSwitch("S", "s", "sw", false));
    const RenderNode* off = findNodeByKey(offRoot, "sw");
    REQUIRE(off != nullptr);
    const auto* offStyle =
        std::get_if<SwitchResolvedStyle>(&off->style.component);
    REQUIRE(offStyle != nullptr);
    // 左右内距 = (trackHeight - knobSize) / 2（§6.4，非固定 3px）。
    CHECK(offStyle->knobInset ==
          (offStyle->trackHeight - offStyle->knobSize) * 0.5F);
    CHECK(offStyle->knobOff == theme.colors.contentPrimary);
    CHECK(offStyle->knobOn == theme.colors.onAccent);
    // 档位 2（Touch）：trackHeight 24、knob 16 → 内距 4（≠3）。
    Widget touch = withControlSize(makeSwitch("S", "s", "sw2", false),
                                   ControlSize::Large);
    const RenderNode touchRoot = layoutOf(std::move(touch));
    const RenderNode* touchNode = findNodeByKey(touchRoot, "sw2");
    REQUIRE(touchNode != nullptr);
    const auto* touchStyle =
        std::get_if<SwitchResolvedStyle>(&touchNode->style.component);
    REQUIRE(touchStyle != nullptr);
    CHECK(touchStyle->knobInset == 4.0F);
    // 轨道轮廓描边存在。
    const auto commands = render::recordScene(offRoot);
    bool sawTrackOutline = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawRectStroke &&
            command.color == theme.colors.borderStrong) {
            sawTrackOutline = true;
        }
    }
    CHECK(sawTrackOutline);
}

TEST_CASE("button_icon_uses_inline_tier_and_scales_stroke", "[visual][s2]") {
    // §6.1/§4.4：图标盒取 inlineIconSize 档位（16/16/20），线宽按 16px→
    // 1.5 基准比例。
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    const RenderNode mediumRoot =
        layoutOf(withIcon(makeButton("Add", {}, {}, 0, "b"), IconId::Plus));
    const RenderNode* medium = findNodeByKey(mediumRoot, "b");
    REQUIRE(medium != nullptr);
    const auto* mediumStyle =
        std::get_if<ButtonResolvedStyle>(&medium->style.component);
    REQUIRE(mediumStyle != nullptr);
    CHECK(mediumStyle->iconSize == theme.metrics.inlineIconSize[1]);
    CHECK(mediumStyle->iconGap == theme.metrics.controlGap[1]);
    CHECK(mediumStyle->iconStroke == 1.8F);  // IconTheme 基准档（visual-system §8）

    Widget large = withControlSize(
        withIcon(makeButton("Add", {}, {}, 0, "b2"), IconId::Plus),
        ControlSize::Large);
    const RenderNode largeRoot = layoutOf(std::move(large));
    const RenderNode* largeNode = findNodeByKey(largeRoot, "b2");
    REQUIRE(largeNode != nullptr);
    const auto* largeStyle =
        std::get_if<ButtonResolvedStyle>(&largeNode->style.component);
    REQUIRE(largeStyle != nullptr);
    CHECK(largeStyle->iconSize == theme.metrics.inlineIconSize[2]);
    CHECK(largeStyle->iconStroke == Approx(2.25F));  // 1.8 × 20/16
}

// --- S3（gui-control-visual-system-task §6.5–§6.8、§7.2） ---

TEST_CASE("slider_reserves_endpoints_and_maps_pointer_to_same_interval",
          "[visual][s3]") {
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    const RenderNode root =
        layoutOf(withKey(makeSlider("v", "sl"), "sl"), 200.0F, 40.0F);
    const RenderNode* node = findNodeByKey(root, "sl");
    REQUIRE(node != nullptr);
    const auto* slider =
        std::get_if<SliderResolvedStyle>(&node->style.component);
    REQUIRE(slider != nullptr);
    // 端点预留 r + focusRingWidth + 1（§6.5），聚焦与否不重定位。
    const float expectInset = slider->thumbDiameter * 0.5F +
                              theme.metrics.focusRingWidth + 1.0F;
    CHECK(slider->trackInset == Approx(expectInset));
    // 行高 = 档位 minHeight。
    CHECK(node->size.height == Approx(theme.metrics.minHeight[1]));

    // 指针映射与绘制共用同一区间：中心点击 → 50；两端 → 0/100。
    lumen::core::StateStore store;
    store.set("v", "0");
    lumen::core::HandlerRegistry handlers;
    lumen::core::FocusManager focus;
    lumen::core::InteractionController controller{store, handlers, focus};
    controller.pointerDown(root, centerOf(root, "sl"));
    controller.pointerUp(root, centerOf(root, "sl"));
    CHECK(store.get("v") == "50");
    controller.pointerDown(root, Offset{2.0F, 20.0F});
    controller.pointerUp(root, Offset{2.0F, 20.0F});
    CHECK(store.get("v") == "0");
    controller.pointerDown(root, Offset{198.0F, 20.0F});
    controller.pointerUp(root, Offset{198.0F, 20.0F});
    CHECK(store.get("v") == "100");

    // 命令层：Thumb 描边（accent）在节点内（值 100 不越界）。
    store.set("v", "100");
    const RenderNode full = LayoutEngine::layout(
        withBind(withKey(makeSlider("v", "sl"), "sl"), "v"),
        tightView(200.0F, 40.0F));
    const auto commands = render::recordScene(full);
    const auto* fullNode = findNodeByKey(full, "sl");
    REQUIRE(fullNode != nullptr);
    bool sawThumbStroke = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawRectStroke &&
            command.color == theme.colors.accent) {
            sawThumbStroke = true;
            // Thumb 环带整体落在节点矩形内。
            CHECK(command.rect.right() <=
                  fullNode->offset.x + fullNode->size.width + 0.01F);
            CHECK(command.rect.left() >= fullNode->offset.x - 0.01F);
        }
    }
    CHECK(sawThumbStroke);
}

TEST_CASE("progress_bar_uses_tier_height_and_clamped_fill", "[visual][s3]") {
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    Widget bar = makeProgressBar("100", "pb");
    bar.width = 120.0F;
    const RenderNode root = layoutOf(bar, 200.0F, 40.0F);
    const RenderNode* node = findNodeByKey(root, "pb");
    REQUIRE(node != nullptr);
    const auto* barStyle =
        std::get_if<ProgressBarResolvedStyle>(&node->style.component);
    REQUIRE(barStyle != nullptr);
    CHECK(barStyle->trackHeight == theme.progressBar.trackHeight[1]);
    CHECK(barStyle->track == theme.colors.borderDefault);
    CHECK(barStyle->fill == theme.colors.accent);

    const auto commands = render::recordScene(root);
    bool sawTrack = false;
    bool sawFill = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawRect) {
            if (command.color == theme.colors.borderDefault &&
                command.rect.size.height == barStyle->trackHeight) {
                sawTrack = true;
            }
            if (command.color == theme.colors.accent &&
                command.rect.size.height == barStyle->trackHeight) {
                sawFill = true;
                // 100% 填充封顶在轨道宽内（tight 视口下显式宽被夹取）。
                CHECK(command.rect.size.width <=
                      node->size.width + 0.01F);
            }
        }
    }
    CHECK(sawTrack);
    CHECK(sawFill);
}

TEST_CASE("tabs_paint_separator_and_selected_indicator", "[visual][s3]") {
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    Widget tabA = withKey(makeButton("General"), "tab-a");
    tabA.selected = true;
    Widget tabB = withKey(makeButton("More"), "tab-b");
    const RenderNode root = layoutOf(
        withKey(makeTabs({tabA, tabB}, "tabs"), "tabs"), 400.0F, 60.0F);
    const RenderNode* tabs = findNodeByKey(root, "tabs");
    REQUIRE(tabs != nullptr);

    const auto commands = render::recordScene(root);
    bool sawSeparator = false;
    bool sawIndicator = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawRect &&
            command.color == theme.tabs.separator &&
            command.rect.size.height == theme.tabs.separatorHeight) {
            sawSeparator = true;
        }
        if (command.type == render::CommandType::DrawRect &&
            command.color == theme.tabs.indicator &&
            command.rect.size.height == theme.tabs.indicatorHeight) {
            sawIndicator = true;
            // 指示条归属选中项：宽度 = 选中子项宽。
            const RenderNode* selected = findNodeByKey(root, "tab-a");
            REQUIRE(selected != nullptr);
            CHECK(command.rect.size.width ==
                  Approx(selected->size.width));
        }
    }
    CHECK(sawSeparator);
    CHECK(sawIndicator);

    // 子按钮由 Tabs 上下文解析：选中 accentContent、未选 contentSecondary
    //（不是 Gallery 手写的颜色）。
    const RenderNode* a = findNodeByKey(root, "tab-a");
    const RenderNode* b = findNodeByKey(root, "tab-b");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->textStyle().color == theme.tabs.selectedContent);
    CHECK(b->textStyle().color == theme.tabs.unselectedContent);
}

TEST_CASE("dropdown_value_row_shares_field_chrome", "[visual][s3]") {
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    const RenderNode root =
        layoutOf(makeDropdown("Blue", "open", "dd", 160.0F), 200.0F, 60.0F);
    const RenderNode* node = findNodeByKey(root, "dd");
    REQUIRE(node != nullptr);
    const auto& common = node->commonStyle();
    // §6.7：与字段同源——surfaceSunken 底 + borderStrong 轮廓 + 同高。
    CHECK(common.background == theme.colors.surfaceSunken);
    CHECK(common.border == theme.colors.borderStrong);
    CHECK(common.borderWidth == theme.metrics.controlBorderWidth);
    CHECK(node->size.height >= theme.metrics.minHeight[1] - 0.01F);
    // 尾随 Chevron 来自 inlineIconSize 档位。
    const auto* dropdown =
        std::get_if<ButtonResolvedStyle>(&node->style.component);
    REQUIRE(dropdown != nullptr);
    CHECK(dropdown->iconSize == theme.metrics.inlineIconSize[1]);
}

TEST_CASE("scrollbar_consumes_borderstrong_token_without_alpha_fudge",
          "[visual][s3]") {
    // §7.2：rest 取 borderStrong 实色经 RenderNode 传递——不再对前景乘
    // alpha；长度在 [minLength, trackLength] 夹取（短视口不越界）。
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    Widget column;
    column.type = WidgetType::Column;
    for (int i = 0; i < 40; ++i) {
        Widget row = makeText("row");
        row.height = 20.0F;
        column.children.push_back(row);
    }
    Widget viewport =
        withScrollbar(withKey(makeScrollView(column, "list"), "list"));
    viewport.height = 100.0F;
    viewport.width = 120.0F;
    const RenderNode root = layoutOf(viewport, 200.0F, 100.0F);
    const auto commands = render::recordScene(root);
    const RenderNode* list = findNodeByKey(root, "list");
    REQUIRE(list != nullptr);
    bool sawThumb = false;
    for (const auto& command : commands.commands()) {
        if (command.type == render::CommandType::DrawRect &&
            command.color == theme.scrollbar.rest &&
            command.color.a == 255) {
            sawThumb = true;
            CHECK(command.rect.size.width ==
                  Approx(theme.scrollbar.thumbWidth));
            CHECK(command.rect.size.height >=
                  theme.scrollbar.minLength - 0.01F);
            // Thumb 不越出视口。
            CHECK(command.rect.bottom() <=
                  list->offset.y + list->size.height + 0.01F);
        }
    }
    CHECK(sawThumb);
}

TEST_CASE("dropdown_long_menu_scrolls_and_stays_inside_window",
          "[controls][s3]") {
    // §6.7：项目过多时菜单最大高度 = min(320, 可用高度)，内容进入垂直
    // 滚动；键盘高亮项滚入可见区；菜单整体留在窗口安全边距 8 内。
    app::ShellConfig config;
    config.caretBlink = false;
    config.build = [] {
        Widget page;
        page.key = "page";
        page.children = {makeDropdown("Option 0", "open", "dd", 160.0F)};
        return page;
    };
    app::AppShell shell{config};
    shell.setView(Size{300.0F, 240.0F});
    (void)shell.renderFrame();

    std::vector<lumen::widgets::DropdownController::Option> options;
    for (int i = 0; i < 40; ++i) {
        options.push_back({"Option " + std::to_string(i),
                           "Option " + std::to_string(i)});
    }
    lumen::widgets::DropdownController controller{std::move(options),
                                                  "Option 0"};
    controller.open(shell, "dd");
    REQUIRE(controller.isOpen());
    shell.rebuildIfDirty();
    REQUIRE(shell.overlayRoot() != nullptr);

    const RenderNode* menu = findNodeByKey(*shell.overlayRoot(), "dd-menu");
    REQUIRE(menu != nullptr);
    // 高度封顶：min(320, 240 - 2×8) = 224；窗口安全边距内。
    CHECK(menu->size.height <= 224.0F + 0.01F);
    const Offset menuOrigin = absoluteOffset(*shell.overlayRoot(), "dd-menu");
    CHECK(menu->size.height >= 100.0F);  // 内容多时不是塌缩
    CHECK(menuOrigin.y >= 8.0F - 0.01F);
    CHECK(menuOrigin.y + menu->size.height <= 240.0F - 8.0F + 0.01F);
    // 超长内容进入垂直滚动。
    REQUIRE(findNodeByKey(*shell.overlayRoot(), "dd-menu-scroll") !=
            nullptr);

    // 键盘移到最后一项：高亮项滚入可见区（选项在菜单矩形内）。
    for (int i = 0; i < 39; ++i) {
        REQUIRE(controller.handleKey(shell, Key::Down));
    }
    const RenderNode* last = findNodeByKey(*shell.overlayRoot(),
                                           "dd-opt-39");
    REQUIRE(last != nullptr);
    const Offset lastOrigin =
        absoluteOffset(*shell.overlayRoot(), "dd-opt-39");
    CHECK(lastOrigin.y >= menuOrigin.y - 0.01F);
    CHECK(lastOrigin.y + last->size.height <=
          menuOrigin.y + menu->size.height + 0.01F);

    // Escape 关闭并恢复值行焦点。
    REQUIRE(controller.handleKey(shell, Key::Escape));
    CHECK_FALSE(controller.isOpen());
    shell.rebuildIfDirty();
    const RenderNode* row = findNodeByKey(shell.root(), "dd");
    REQUIRE(row != nullptr);
    CHECK(shell.focus().focusedIdentity() == row->identity);
}

// --- S4（gui-control-visual-system-task §6.9/§8.2/§7.3） ---

TEST_CASE("tooltip_resolves_compact_surface_and_wraps_text", "[visual][s4]") {
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    // 长文本：换行受 maxWidth 280 限制（§6.9）。loose 约束让 intrinsic
    // 生效（tight 会把叶子拉满视口）。
    const std::string longText =
        "A rather long tooltip line that should wrap instead of running "
        "past the maximum width of two hundred eighty logical pixels.";
    const RenderNode root = LayoutEngine::layout(
        makeContainer(makeTooltip(longText, "tip")),
        Constraints::loose(Size{300.0F, 200.0F}));
    const RenderNode* node = findNodeByKey(root, "tip");
    REQUIRE(node != nullptr);
    const auto& common = node->commonStyle();
    CHECK(common.background == theme.colors.surfaceElevated);
    CHECK(common.border == theme.colors.borderDefault);
    CHECK(common.borderWidth == theme.metrics.controlBorderWidth);
    CHECK(common.text.fontSize == theme.typography.caption.fontSize);
    // 最大宽度包含表面 padding。
    CHECK(node->size.width <= theme.tooltip.maxWidth + 0.01F);
    CHECK(node->size.width > theme.tooltip.maxWidth * 0.5F);
    // 高度 > 单行（发生了换行）。
    CHECK(node->size.height > theme.typography.caption.fontSize * 2.0F);
}

TEST_CASE("dialog_card_uses_border_elevation_and_padding", "[visual][s4]") {
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    Widget body = makeText("Body");
    const Widget dialog = lumen::widgets::makeDialog(
        std::move(body), theme, "dismiss", "dlg", Size{400.0F, 300.0F});
    // 经布局落地后核对卡片 chrome（§8.2 真实 RenderNode，不是只看 token）。
    const RenderNode root = layoutOf(dialog, 400.0F, 300.0F);
    const RenderNode* card = findNodeByKey(root, "dlg-card");
    REQUIRE(card != nullptr);
    CHECK(card->commonStyle().background == theme.colors.surfaceElevated);
    CHECK(card->commonStyle().border == theme.colors.borderDefault);
    CHECK(card->elevation ==
          Approx(theme.dialog.elevation));
    // 宽度落在 240–420 且不越窗口边距 16。
    CHECK(card->size.width <= 420.0F + 0.01F);
    CHECK(card->size.width >= 240.0F - 0.01F);
    const Offset cardOrigin = absoluteOffset(root, "dlg-card");
    CHECK(cardOrigin.x >= 16.0F - 0.01F);
    CHECK(cardOrigin.y >= 16.0F - 0.01F);
    CHECK(cardOrigin.y + card->size.height <= 300.0F - 16.0F + 0.01F);
    // 整体 padding 24 由 helper 施加（内容容器）。
    REQUIRE(card->children.size() == 1);
    CHECK(card->children[0].padding.left ==
          Approx(theme.dialog.padding.left));
}

TEST_CASE("dialog_actions_opt_in_focus_ring", "[visual][s4]") {
    // 键盘重度表面（visual-system §6.1）：默认焦点环关闭后，对话框动作
    // 按钮是 Enter/Esc 的键盘激活目标且无其他焦点指示，makeDialog 对
    // actions 子树显式开启；正文内容保持控件自身设置（默认不画环）。
    const lumen::style::Theme theme = lumen::style::Theme::dark();
    Widget body = makeText("Body");
    Widget actions = withKey(makeButton("Close"), "dlg-close");
    actions.onClick = "dismiss-dialog";
    const Widget dialog = lumen::widgets::makeDialog(
        std::move(body), std::move(actions), theme, "dismiss", "dlg",
        Size{400.0F, 300.0F});
    const auto findWidget = [](auto&& self, const Widget& widget,
                               const std::string& key) -> const Widget* {
        if (widget.key == key) return &widget;
        for (const auto& child : widget.children) {
            if (const Widget* found = self(self, child, key)) return found;
        }
        return nullptr;
    };
    const Widget* close = findWidget(findWidget, dialog, "dlg-close");
    REQUIRE(close != nullptr);
    CHECK(close->showFocusRing);
    const Widget* bodyText = findWidget(findWidget, dialog, "dlg-body-scroll");
    REQUIRE(bodyText != nullptr);
    CHECK_FALSE(bodyText->showFocusRing);
}

TEST_CASE("dropdown_overlay_inherits_anchor_scope_theme", "[visual][s4]") {
    // §7.3：浮层继承触发器有效主题（ThemeScope 内的浅色主题），不回落
    // 窗口根主题（shell 为深色）。
    app::ShellConfig config;
    config.caretBlink = false;
    config.build = [] {
        Widget page;
        page.key = "page";
        page.children = {makeDropdown("A", "open", "dd", 160.0F)};
        return page;
    };
    app::AppShell shell{config};
    shell.setView(Size{300.0F, 200.0F});
    (void)shell.renderFrame();
    REQUIRE(shell.theme().darkMode);

    const style::Theme lightScope = style::Theme::light();
    lumen::widgets::DropdownController controller{
        {{"A", "A"}, {"B", "B"}}, "A"};
    controller.open(shell, "dd", &lightScope);
    shell.rebuildIfDirty();
    const RenderNode* menu =
        findNodeByKey(*shell.overlayRoot(), "dd-menu");
    REQUIRE(menu != nullptr);
    CHECK(menu->commonStyle().background == lightScope.colors.surfaceElevated);
    controller.close(shell);
}
