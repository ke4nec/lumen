// Headless integration of the counter app (plan §9: 无窗口模式运行 counter
// 并生成稳定 frame hash). Uses the same CounterApp as the windowed example.

#include <catch2/catch_test_macros.hpp>

#include "counter_app.h"

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
            if (pixels.rgba[offset] > 100) {
                ++litPixels;
            }
        }
    }
    // Button background (212) covers most of its rect; label adds texture.
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
    CHECK(rect.size.height == field->size.height);
    CHECK(rect.origin.x == lumen::core::absoluteOffset(app.root(),
                                                       "name-field")
                               .x + 8.0F);
    // Empty text: caret sits after the 8px left padding.
    CHECK(app.focusedCaretOffset() == 8);

    app.textInput("hi");
    app.renderFrame();
    // 2 code points at 0.6em (14px font): 8 + 2*8.4 = 24 (truncated).
    CHECK(app.focusedCaretOffset() == 24);

    // IME preedit never touches the document.
    app.textEditing("ni");
    CHECK(app.state().get("name") == "hi");
    CHECK(app.controller().composition() == "ni");
}
