// Headless integration of the counter app (plan §9: 无窗口模式运行 counter
// 并生成稳定 frame hash). Uses the same CounterApp as the windowed example.

#include <catch2/catch_test_macros.hpp>

#include "counter_app.h"
#include "lumen/dsl/text_dsl.h"

using lumen::core::Offset;
using lumen::core::Size;
using lumen::examples::CounterApp;

namespace {

Offset centerOf(const CounterApp& app, const char* key) {
    const lumen::core::RenderNode* node =
        lumen::core::findNodeByKey(app.root(), key);
    REQUIRE(node != nullptr);
    return lumen::core::absoluteOffset(app.root(), key) +
           Offset{node->size.width * 0.5F, node->size.height * 0.5F};
}

}  // namespace

TEST_CASE("counter_frame_hash_is_stable_across_runs", "[counter]") {
    CounterApp a;
    a.setView(Size{800.0F, 600.0F});
    const auto first = a.renderFrame();

    CounterApp b;
    b.setView(Size{800.0F, 600.0F});
    CHECK(b.renderFrame() == first);
    // A second frame without changes produces the identical hash.
    CHECK(a.renderFrame() == first);
}

TEST_CASE("counter_increment_updates_state_ui_and_frame", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    const auto before = app.renderFrame();
    CHECK(app.counterValue() == 0);

    app.pointerDown(centerOf(app, "increment-button"));
    app.pointerUp(centerOf(app, "increment-button"));
    CHECK(app.counterValue() == 1);

    const auto after = app.renderFrame();
    CHECK(after != before);
    const lumen::core::RenderNode* label =
        lumen::core::findNodeByKey(app.root(), "count-text");
    REQUIRE(label != nullptr);
    CHECK(label->text == "Count: 1");
}

TEST_CASE("counter_text_input_flows_to_state_and_paint", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    const auto before = app.renderFrame();

    app.pointerDown(centerOf(app, "name-field"));
    app.pointerUp(centerOf(app, "name-field"));
    app.textInput("Lumen");
    CHECK(app.state().get("name") == "Lumen");

    const auto after = app.renderFrame();
    CHECK(after != before);
    const lumen::core::RenderNode* field =
        lumen::core::findNodeByKey(app.root(), "name-field");
    REQUIRE(field != nullptr);
    CHECK(field->text == "Lumen");
}

TEST_CASE("counter_utf8_backspace_removes_code_point", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    app.renderFrame();
    app.pointerDown(centerOf(app, "name-field"));
    app.pointerUp(centerOf(app, "name-field"));
    app.textInput("\xe4\xbd\xa0\xe5\xa5\xbd");
    CHECK(app.state().get("name") == "\xe4\xbd\xa0\xe5\xa5\xbd");
    app.keyDown(lumen::core::Key::Backspace);
    CHECK(app.state().get("name") == "\xe4\xbd\xa0");
}

TEST_CASE("counter_resize_adapts_root_layout", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    const auto small = app.renderFrame();
    CHECK(app.root().size.width == 800.0F);

    app.setView(Size{1024.0F, 768.0F});
    const auto large = app.renderFrame();
    CHECK(app.root().size == Size{1024.0F, 768.0F});
    CHECK(large != small);
}

TEST_CASE("counter_frame_paints_content_pixels", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    app.renderFrame();
    const auto& pixels = app.pixels();
    REQUIRE(pixels.width == 800);
    REQUIRE(pixels.height == 600);
    // The button chrome and text are brighter than the dark background.
    const lumen::core::RenderNode* button =
        lumen::core::findNodeByKey(app.root(), "increment-button");
    REQUIRE(button != nullptr);
    const Offset origin = lumen::core::absoluteOffset(app.root(),
                                                      "increment-button");
    int litPixels = 0;
    for (int y = static_cast<int>(origin.y);
         y < static_cast<int>(origin.y + button->size.height); ++y) {
        for (int x = static_cast<int>(origin.x);
             x < static_cast<int>(origin.x + button->size.width); ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * 800 + x) * 4;
            if (pixels.rgba[offset + 2] > 100) {
                ++litPixels;
            }
        }
    }
    // Filled 按钮背景是 accent 蓝（blue 通道 > 100），覆盖大部分区域。
    const int total = static_cast<int>(button->size.width) *
                      static_cast<int>(button->size.height);
    CHECK(litPixels > total * 3 / 4);
}

TEST_CASE("counter_focused_rect_tracks_text_field", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    app.renderFrame();
    CHECK(app.focusedTextRect().size.width == 0.0F);
    CHECK_FALSE(app.wantsTextInput());

    app.pointerDown(centerOf(app, "name-field"));
    app.pointerUp(centerOf(app, "name-field"));
    app.renderFrame();
    REQUIRE(app.wantsTextInput());
    const auto rect = app.focusedTextRect();
    const auto* field =
        lumen::core::findNodeByKey(app.root(), "name-field");
    REQUIRE(field != nullptr);
    CHECK(rect.size.width == 1.0F);
    CHECK(rect.size.height == field->textStyle().fontSize * field->textStyle().lineHeight);
    CHECK(rect.origin.x == lumen::core::absoluteOffset(app.root(),
                                                       "name-field")
                               .x + 12.0F);
    // Empty text: caret sits after the 12px chrome padding.
    CHECK(app.focusedCaretOffset() == 12);

    app.textInput("hi");
    app.renderFrame();
    // 2 code points at 0.6em (14px font): 12 + 2*8.4 = 28 (truncated).
    CHECK(app.focusedCaretOffset() == 28);

    // IME preedit never touches the document.
    app.textEditing("ni");
    CHECK(app.state().get("name") == "hi");
    CHECK(app.controller().composition() == "ni");
}

// --- Stage 6: dirty-rect repaint, paint cache, hot reload. ---

TEST_CASE("counter_partial_repaint_matches_full_repaint", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    // Click Increment: the rebuild damages only the count label.
    app.pointerDown(centerOf(app, "increment-button"));
    app.pointerUp(centerOf(app, "increment-button"));
    const auto partialCountBefore = app.partialRepaintCount();
    const std::uint64_t partial = app.renderFrame();
    // The damage path must actually run; otherwise this test would compare
    // two full repaints and verify nothing.
    CHECK(app.partialRepaintCount() == partialCountBefore + 1);

    // A forced full repaint of the identical tree must be pixel-equal.
    const std::uint64_t full = app.renderFrame(true);
    CHECK(partial == full);
}

TEST_CASE("counter_skips_repaint_when_nothing_changed", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    const auto first = app.renderFrame();
    // No state/option/animation change: the cached frame hash comes back.
    CHECK(app.renderFrame() == first);
    // ...and remains equal to a forced full repaint of the same tree.
    CHECK(app.renderFrame(true) == first);
}

TEST_CASE("counter_caret_blink_ticks_change_only_the_field", "[counter]") {
    CounterApp app;
    app.setView(Size{320.0F, 180.0F});
    (void)app.renderFrame();
    app.pointerDown(centerOf(app, "name-field"));
    app.pointerUp(centerOf(app, "name-field"));
    (void)app.renderFrame();

    // Blink at full phase start (alpha ~1) then at the dark end (alpha ~0):
    // frames differ, and each equals its own forced full repaint.
    app.tick(0);
    const auto visible = app.renderFrame();
    CHECK(visible == app.renderFrame(true));
    app.tick(530);
    const auto dark = app.renderFrame();
    CHECK(dark == app.renderFrame(true));
    INFO("caret " << app.focusedTextRect().origin.x << "," << app.focusedTextRect().origin.y
         << " size " << app.focusedTextRect().size.width << "," << app.focusedTextRect().size.height
         << " focus " << app.controller().wantsTextInput());
    CHECK(dark != visible);
}

TEST_CASE("counter_drag_on_button_does_not_increment", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();
    const Offset at = centerOf(app, "increment-button");
    app.pointerDown(at);
    app.pointerMove(at + Offset{40.0F, 0.0F});
    app.pointerUp(at + Offset{40.0F, 0.0F});
    CHECK(app.counterValue() == 0);
}

TEST_CASE("counter_hot_reload_keeps_state_and_resyncs_binds", "[counter]") {
    const std::string v1 =
        "page P {\n"
        "  Container(key: \"root\") {\n"
        "    Column(padding: 8) {\n"
        "      Text(\"Count: \", bind: counter, key: \"count-text\")\n"
        "      Button(\"Go\", onClick: increment, key: \"increment-button\")\n"
        "      TextField(bind: name, placeholder: \"Name\", key: \"name-field\")\n"
        "    }\n"
        "  }\n"
        "}\n";
    const std::string v2 =
        "page P {\n"
        "  Container(key: \"root\") {\n"
        "    Column(padding: 8) {\n"
        "      Text(\"Total: \", bind: counter, key: \"count-text\")\n"
        "      Button(\"Go\", onClick: increment, key: \"increment-button\")\n"
        "      TextField(bind: name, placeholder: \"Name\", key: \"name-field\")\n"
        "    }\n"
        "  }\n"
        "}\n";
    const auto parsed1 = lumen::dsl::parseLumen(v1);
    const auto parsed2 = lumen::dsl::parseLumen(v2);
    REQUIRE(parsed1.ok());
    REQUIRE(parsed2.ok());

    CounterApp app(parsed1.root);
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();
    app.pointerDown(centerOf(app, "increment-button"));
    app.pointerUp(centerOf(app, "increment-button"));
    REQUIRE(app.counterValue() == 1);

    // Hot swap: new template, state preserved, binds re-resolved.
    app.swapRoot(parsed2.root);
    const auto hashAfter = app.renderFrame();
    CHECK(hashAfter != 0);
    const lumen::core::RenderNode* label =
        lumen::core::findNodeByKey(app.root(), "count-text");
    REQUIRE(label != nullptr);
    CHECK(label->text == "Total: 1");
    CHECK(app.counterValue() == 1);

    // Interaction still works after the swap (subscriptions resynced).
    app.pointerDown(centerOf(app, "name-field"));
    app.pointerUp(centerOf(app, "name-field"));
    app.textInput("hi");
    CHECK(app.state().get("name") == "hi");
}

TEST_CASE("counter_damage_accumulates_across_rebuilds_before_paint", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    // Two state changes with no paint in between: the increment changes the
    // label, the typed text changes the field. Both rects must repaint.
    app.pointerDown(centerOf(app, "increment-button"));
    app.pointerUp(centerOf(app, "increment-button"));  // counter -> 1, dirty
    app.pointerDown(centerOf(app, "name-field"));      // rebuild #1 (label)
    app.textInput("Hi");                               // name -> "Hi", dirty
    const auto accumulatedBefore = app.partialRepaintCount();
    const std::uint64_t partial = app.renderFrame();   // rebuild #2 (field)

    // The damage path must run despite two rebuilds sharing one paint.
    CHECK(app.partialRepaintCount() == accumulatedBefore + 1);
    CHECK(app.counterValue() == 1);
    CHECK(app.state().get("name") == "Hi");
    const std::uint64_t full = app.renderFrame(true);
    CHECK(partial == full);
}

TEST_CASE("counter_renderer_switch_invalidates_cpu_cache", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    const auto initial = app.renderFrame();

    // Render an updated tree through a separate backend. The internal CPU
    // framebuffer is not touched while the external renderer is active.
    lumen::render::CpuRenderer external;
    app.setRenderer(&external);
    app.pointerDown(centerOf(app, "increment-button"));
    app.pointerUp(centerOf(app, "increment-button"));
    CHECK(app.renderFrame() == 0);
    REQUIRE(app.counterValue() == 1);

    // Returning to CPU must repaint the changed tree instead of returning
    // the old cached frame from before the backend switch.
    app.setRenderer(nullptr);
    const auto restored = app.renderFrame();
    const auto forced = app.renderFrame(true);
    CHECK(restored == forced);
    CHECK(restored != initial);
}

// --- 视觉系统：状态变化的局部重绘与全量重绘像素一致（§10.2） ---

TEST_CASE("counter_pressed_state_partial_repaint_matches_full", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    // 按住不放：pressed 进入 resolved style，重建走 diff damage。
    app.pointerDown(centerOf(app, "increment-button"));
    const auto partialCountBefore = app.partialRepaintCount();
    const auto pressedPartial = app.renderFrame();
    CHECK(app.partialRepaintCount() == partialCountBefore + 1);
    CHECK(pressedPartial == app.renderFrame(true));

    // 释放：pressed 消失，同样像素一致。
    app.pointerUp(centerOf(app, "increment-button"));
    (void)app.renderFrame();
    const auto released = app.renderFrame();
    CHECK(released == app.renderFrame(true));
    CHECK(released != pressedPartial);
}

TEST_CASE("counter_hover_state_changes_frame", "[counter]") {
    CounterApp app;
    app.setView(Size{800.0F, 600.0F});
    (void)app.renderFrame();

    // 悬停在按钮上：hover 折算进 resolved style，帧像素变化。
    app.pointerMove(centerOf(app, "increment-button"));
    const auto hovered = app.renderFrame();
    CHECK(hovered == app.renderFrame(true));
    app.pointerMove(Offset{10.0F, 10.0F});
    const auto elsewhere = app.renderFrame();
    CHECK(elsewhere == app.renderFrame(true));
    CHECK(hovered != elsewhere);
}

TEST_CASE("counter_reduce_animation_stops_caret_blink", "[counter]") {
    CounterApp app;
    app.setView(Size{320.0F, 180.0F});
    (void)app.renderFrame();
    app.pointerDown(centerOf(app, "name-field"));
    app.pointerUp(centerOf(app, "name-field"));
    (void)app.renderFrame();

    // reduceAnimation：闪烁时长归零，任何 tick 光标保持可见（§4）。
    lumen::accessibility::AccessibilitySettings settings;
    settings.reduceAnimation = true;
    app.setAccessibilitySettings(settings);
    (void)app.renderFrame();
    app.tick(0);
    const auto atStart = app.renderFrame();
    app.tick(530);
    const auto atDarkPhase = app.renderFrame();
    CHECK(atStart == atDarkPhase);
    CHECK(atStart == app.renderFrame(true));
}

TEST_CASE("counter_high_contrast_changes_visuals", "[counter]") {
    CounterApp app;
    app.setView(Size{320.0F, 180.0F});
    const auto normal = app.renderFrame();
    lumen::accessibility::AccessibilitySettings settings;
    settings.highContrast = true;
    app.setAccessibilitySettings(settings);
    const auto contrast = app.renderFrame();
    CHECK(contrast != normal);
    CHECK(contrast == app.renderFrame(true));
}
