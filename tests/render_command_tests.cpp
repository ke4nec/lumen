// v0.2 阶段7B tests (plan §5 命令与后端): command recording, replay parity,
// damage culling, serialization round-trips, upload/unload lifecycle commands
// and the capability/stats contract.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "lumen/core/geometry.h"
#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/render/render_commands.h"
#include "lumen/render/renderer.h"
#include "lumen/text/font_manager.h"

using lumen::core::Color;
using lumen::core::Constraints;
using lumen::core::CornerRadius;
using lumen::core::Offset;
using lumen::core::Rect;
using lumen::core::Size;
using lumen::core::Widget;
using lumen::layout::LayoutEngine;
using lumen::render::CpuRenderer;
using lumen::render::CommandType;
using lumen::render::FrameInfo;
using lumen::render::ImageId;
using lumen::render::PixelBuffer;
using lumen::render::RenderCommandList;
using lumen::render::frameHash;
using lumen::render::recordScene;

namespace {

// Counter-shaped scene: page background + text + button + field.
Widget sampleScene(std::string buttonText = "Increment") {
    Widget text;
    text.type = lumen::core::WidgetType::Text;
    text.text = "Count: 0";
    Widget button;
    button.type = lumen::core::WidgetType::Button;
    button.text = buttonText;
    Widget field;
    field.type = lumen::core::WidgetType::TextField;
    field.placeholder = "Name";

    Widget column;
    column.type = lumen::core::WidgetType::Column;
    column.padding = lumen::core::EdgeInsets::all(24.0F);
    column.spacing = 12.0F;
    column.children = {text, button, field};

    Widget page;
    page.type = lumen::core::WidgetType::Container;
    page.color = Color::fromRGBA(24, 24, 27);
    page.children = {column};
    return page;
}

lumen::core::RenderNode laidOutScene(const Widget& widget,
                                     Size viewport = Size{400.0F, 300.0F}) {
    return LayoutEngine::layout(widget, Constraints::tight(viewport));
}

PixelBuffer solidImage(int width, int height, Color color) {
    PixelBuffer image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        image.rgba[i] = color.r;
        image.rgba[i + 1] = color.g;
        image.rgba[i + 2] = color.b;
        image.rgba[i + 3] = color.a;
    }
    return image;
}

}  // namespace

TEST_CASE("record_scene_orders_commands_like_the_immediate_path",
          "[render][commands]") {
    const auto root = laidOutScene(sampleScene());
    const RenderCommandList list = recordScene(root);

    REQUIRE_FALSE(list.empty());
    // Page background first, then the column background is transparent
    // (skipped), then per-leaf: button rect + clipped text, field rect +
    // clipped placeholder text. The text widget records its own clip.
    const auto& commands = list.commands();
    CHECK(commands.front().type == CommandType::DrawRect);
    CHECK(commands.front().color == Color::fromRGBA(24, 24, 27));

    // Find the button chrome: a rounded DrawRect followed by Save/ClipRect/
    // DrawText/Restore.
    std::size_t buttonRect = 0;
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].type == CommandType::DrawRect &&
            commands[i].radius.topLeft > 0.0F) {
            buttonRect = i;
            break;
        }
    }
    REQUIRE(buttonRect > 0);
    REQUIRE(buttonRect + 4 < commands.size());
    CHECK(commands[buttonRect + 1].type == CommandType::Save);
    CHECK(commands[buttonRect + 2].type == CommandType::ClipRect);
    CHECK(commands[buttonRect + 3].type == CommandType::DrawText);
    CHECK(commands[buttonRect + 3].textRun.text == "Increment");
    CHECK(commands[buttonRect + 4].type == CommandType::Restore);
    // Text commands carry their clip rect as bounds for damage culling.
    CHECK(commands[buttonRect + 3].hasBounds);
}

TEST_CASE("cpu_submit_matches_immediate_paint_pixels", "[render][commands]") {
    const auto root = laidOutScene(sampleScene());

    CpuRenderer immediate;
    immediate.beginFrame(Size{400.0F, 300.0F});
    lumen::render::paintScene(immediate, root, {});
    immediate.endFrame();

    CpuRenderer submitted;
    FrameInfo info;
    info.viewport = Size{400.0F, 300.0F};
    submitted.submit(recordScene(root), info);

    CHECK(frameHash(immediate.pixels()) == frameHash(submitted.pixels()));
}

TEST_CASE("cpu_submit_replays_icon_commands", "[render][commands]") {
    // 回归：CpuRenderer::submit 的原生命令分发曾漏掉 DrawIcon（DrawShadow
    // 同批补齐）——图标在 paintScene 直绘路径可见、经 submit 静默丢失。
    Widget icon = lumen::core::makeIcon(lumen::core::IconId::Close, "close",
                                        32.0F, 32.0F);
    Widget page;
    page.type = lumen::core::WidgetType::Container;
    page.color = Color::fromRGBA(24, 24, 27);
    page.padding = lumen::core::EdgeInsets::all(16.0F);
    page.children = {icon};

    const auto root = laidOutScene(page);
    const RenderCommandList commands = recordScene(root);
    bool hasIconCommand = false;
    for (const auto& command : commands.commands()) {
        hasIconCommand = hasIconCommand ||
                         command.type == CommandType::DrawIcon;
    }
    REQUIRE(hasIconCommand);

    CpuRenderer immediate;
    immediate.beginFrame(Size{400.0F, 300.0F});
    lumen::render::paintScene(immediate, root, {});
    immediate.endFrame();

    CpuRenderer submitted;
    FrameInfo info;
    info.viewport = Size{400.0F, 300.0F};
    submitted.submit(commands, info);

    CHECK(frameHash(immediate.pixels()) == frameHash(submitted.pixels()));
}

TEST_CASE("cpu_partial_submit_preserves_previous_frame", "[render][commands]") {
    // Frame 1 renders the initial scene; frame 2 changes the button text and
    // submits only the button's damaged area with preserve — pixels must
    // match a full repaint of frame 2 exactly.
    const auto first = laidOutScene(sampleScene("Increment"));
    const auto second = laidOutScene(sampleScene("Decrement..."));

    CpuRenderer partialRenderer;
    FrameInfo full;
    full.viewport = Size{400.0F, 300.0F};
    partialRenderer.submit(recordScene(first), full);

    CpuRenderer fullRenderer;
    fullRenderer.submit(recordScene(first), full);

    // Damage covers only the button row (the sole change): text and field
    // commands fall outside and must be culled while parity holds.
    FrameInfo damaged = full;
    damaged.damage = Rect::fromXYWH(0.0F, 50.0F, 400.0F, 50.0F);
    damaged.preservePrevious = true;
    partialRenderer.submit(recordScene(second), damaged);
    fullRenderer.submit(recordScene(second), full);

    CHECK(frameHash(partialRenderer.pixels()) == frameHash(fullRenderer.pixels()));
    const auto stats = partialRenderer.stats();
    CHECK(stats.framesSubmitted == 2);
    // The first submit was a full frame request (no damage): not a fallback.
    // The second one was partial: culled commands recorded.
    CHECK(stats.culledCommands > 0);
    CHECK_FALSE(stats.fullFrameFallback);
}

TEST_CASE("cpu_partial_without_previous_frame_falls_back_and_reports",
          "[render][commands]") {
    CpuRenderer renderer;
    FrameInfo info;
    info.viewport = Size{100.0F, 80.0F};
    info.damage = Rect::fromXYWH(10.0F, 10.0F, 20.0F, 20.0F);
    info.preservePrevious = true;

    RenderCommandList list;
    list.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 80.0F),
                  Color::fromRGBA(255, 0, 0));
    renderer.submit(list, info);

    const auto stats = renderer.stats();
    CHECK(stats.fullFrameFallback);
    CHECK(stats.fallbackReason == "no-previous-frame");
    CHECK(stats.culledCommands == 0);
}

TEST_CASE("default_submit_adapter_replays_through_legacy_path",
          "[render][commands]") {
    // A renderer that only implements the legacy immediate interface still
    // accepts submit() via the adapter (plan §3.1 旧路径适配器).
    class LegacyOnlyRenderer final : public lumen::render::Renderer {
      public:
        void beginFrame(Size viewport) override {
            renderer.beginFrame(viewport);
        }
        void save() override { ++saves; }
        void restore() override { ++restores; }
        void clipRect(Rect) override { ++clips; }
        void drawRect(Rect, Color, CornerRadius) override { ++rects; }
        void drawText(lumen::render::TextRun, lumen::core::TextStyle) override {
            ++texts;
        }
        void drawImage(ImageId, Rect) override { ++images; }
        void endFrame() override { ++frames; }

        CpuRenderer renderer{};
        int saves = 0;
        int restores = 0;
        int clips = 0;
        int rects = 0;
        int texts = 0;
        int images = 0;
        int frames = 0;
    };

    LegacyOnlyRenderer legacy;
    const auto root = laidOutScene(sampleScene());
    FrameInfo info;
    info.viewport = Size{400.0F, 300.0F};
    info.damage = Rect::fromXYWH(0.0F, 0.0F, 200.0F, 300.0F);
    // preserve 请求 + 无 partialSubmit 能力 -> 适配器退回全帧并记录原因。
    info.preservePrevious = true;
    legacy.submit(recordScene(root), info);

    CHECK(legacy.frames == 1);
    CHECK(legacy.rects > 0);
    CHECK(legacy.texts > 0);
    // Damage clips the whole replay: one extra save/clip/restore pair.
    CHECK(legacy.saves == legacy.restores);
    CHECK(legacy.clips > 0);
    const auto stats = legacy.stats();
    CHECK(stats.commandCount == recordScene(root).size());
    CHECK(stats.framesSubmitted == 1);
    // partialSubmit 能力为假，preserve 请求退回全帧并说明原因。
    CHECK(stats.fullFrameFallback);
    CHECK(stats.fallbackReason == "backend-has-no-partial-submit");
    CHECK(std::string(legacy.capabilities().backendName) == "adapter");
}

TEST_CASE("cull_commands_keeps_state_structure_and_drops_outside_draws",
          "[render][commands]") {
    RenderCommandList list;
    list.save();
    list.clipRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 100.0F));
    list.drawRect(Rect::fromXYWH(10.0F, 10.0F, 20.0F, 20.0F),
                  Color::fromRGBA(255, 0, 0));
    list.drawText({"inside", Offset{15.0F, 15.0F}}, {},
                  Rect::fromXYWH(0.0F, 0.0F, 100.0F, 100.0F));
    list.drawRect(Rect::fromXYWH(500.0F, 500.0F, 50.0F, 50.0F),
                  Color::fromRGBA(0, 255, 0));
    list.restore();

    const RenderCommandList culled =
        lumen::render::cullCommandsOutside(list, Rect::fromXYWH(0.0F, 0.0F, 100.0F, 100.0F));
    CHECK(culled.size() == list.size() - 1);
    bool sawFarRect = false;
    for (const auto& command : culled.commands()) {
        if (command.type == CommandType::DrawRect &&
            command.rect.left() == 500.0F) {
            sawFarRect = true;
        }
    }
    CHECK_FALSE(sawFarRect);
    // Save/ClipRect/DrawRect/DrawText/Restore survive untouched.
    CHECK(culled.commands()[0].type == CommandType::Save);
    CHECK(culled.commands()[1].type == CommandType::ClipRect);
    CHECK(culled.commands()[2].type == CommandType::DrawRect);
    CHECK(culled.commands()[3].type == CommandType::DrawText);
    CHECK(culled.commands().back().type == CommandType::Restore);

    // A draw without bounds is conservatively kept.
    RenderCommandList unbounded;
    unbounded.drawText({"no bounds", Offset{900.0F, 900.0F}}, {});
    CHECK(lumen::render::cullCommandsOutside(
              unbounded, Rect::fromXYWH(0.0F, 0.0F, 10.0F, 10.0F))
              .size() == 1);
}

TEST_CASE("command_draw_bounds_unions_recorded_areas", "[render][commands]") {
    RenderCommandList list;
    list.drawRect(Rect::fromXYWH(10.0F, 10.0F, 20.0F, 20.0F),
                  Color::fromRGBA(255, 0, 0));
    list.drawRect(Rect::fromXYWH(60.0F, 70.0F, 20.0F, 20.0F),
                  Color::fromRGBA(0, 255, 0));
    const auto bounds = lumen::render::commandDrawBounds(list);
    REQUIRE(bounds.has_value());
    CHECK(bounds->left() == 10.0F);
    CHECK(bounds->top() == 10.0F);
    CHECK(bounds->right() == 80.0F);
    CHECK(bounds->bottom() == 90.0F);

    // Any unbounded draw makes coverage unprovable.
    list.drawText({"unbounded", Offset{}}, {});
    CHECK_FALSE(lumen::render::commandDrawBounds(list).has_value());
}

TEST_CASE("command_serialization_round_trips", "[render][commands]") {
    const auto root = laidOutScene(sampleScene());
    RenderCommandList list = recordScene(root);
    list.uploadImage(77, solidImage(2, 2, Color::fromRGBA(1, 2, 3)));
    list.drawImage(77, Rect::fromXYWH(1.0F, 2.0F, 30.0F, 40.0F));
    list.unloadImage(77);

    const std::string blob = lumen::render::serializeCommands(list);
    REQUIRE_FALSE(blob.empty());

    RenderCommandList parsed;
    REQUIRE(lumen::render::deserializeCommands(blob, parsed));
    CHECK(parsed == list);

    SECTION("corrupt magic rejected") {
        std::string bad = blob;
        bad[0] = 'X';
        RenderCommandList out;
        CHECK_FALSE(lumen::render::deserializeCommands(bad, out));
    }
    SECTION("truncated blob rejected") {
        RenderCommandList out;
        CHECK_FALSE(
            lumen::render::deserializeCommands(blob.substr(0, blob.size() / 2), out));
        CHECK_FALSE(
            lumen::render::deserializeCommands(blob.substr(0, 12), out));
    }
}

TEST_CASE("serialized_commands_replay_to_identical_pixels",
          "[render][commands]") {
    const auto root = laidOutScene(sampleScene());
    const std::string blob =
        lumen::render::serializeCommands(recordScene(root));
    RenderCommandList parsed;
    REQUIRE(lumen::render::deserializeCommands(blob, parsed));

    CpuRenderer direct;
    FrameInfo info;
    info.viewport = Size{400.0F, 300.0F};
    direct.submit(recordScene(root), info);

    CpuRenderer replayed;
    replayed.submit(parsed, info);
    CHECK(frameHash(direct.pixels()) == frameHash(replayed.pixels()));
}

TEST_CASE("upload_and_unload_commands_drive_image_lifecycle",
          "[render][commands]") {
    RenderCommandList list;
    list.uploadImage(9, solidImage(4, 4, Color::fromRGBA(200, 10, 10)));
    list.drawImage(9, Rect::fromXYWH(0.0F, 0.0F, 40.0F, 40.0F));

    CpuRenderer renderer;
    FrameInfo info;
    info.viewport = Size{40.0F, 40.0F};
    renderer.submit(list, info);
    CHECK(renderer.stats().uploads == 1);

    CpuRenderer expected;
    expected.beginFrame(Size{40.0F, 40.0F});
    const ImageId id = expected.registerImage(solidImage(4, 4, Color::fromRGBA(200, 10, 10)));
    expected.drawImage(id, Rect::fromXYWH(0.0F, 0.0F, 40.0F, 40.0F));
    expected.endFrame();
    CHECK(frameHash(renderer.pixels()) == frameHash(expected.pixels()));

    // Unload removes the image; later draws are no-ops (clear frame).
    RenderCommandList unload;
    unload.unloadImage(9);
    unload.drawImage(9, Rect::fromXYWH(0.0F, 0.0F, 40.0F, 40.0F));
    renderer.submit(unload, info);
    CpuRenderer cleared;
    cleared.beginFrame(Size{40.0F, 40.0F});
    cleared.endFrame();
    CHECK(frameHash(renderer.pixels()) == frameHash(cleared.pixels()));
}

TEST_CASE("renderer_capabilities_report_backend_features", "[render][commands]") {
    CpuRenderer renderer;
    const auto caps = renderer.capabilities();
    CHECK_FALSE(caps.gpu);
    CHECK(caps.partialSubmit);
    CHECK(caps.text);
    CHECK(caps.images);
    CHECK(std::string(caps.backendName) == "cpu");
}

TEST_CASE("nested_clip_commands_replay_with_same_semantics",
          "[render][commands]") {
    // Two nested clips must intersect: the red rect only paints inside both.
    RenderCommandList list;
    list.save();
    list.clipRect(Rect::fromXYWH(0.0F, 0.0F, 50.0F, 50.0F));
    list.save();
    list.clipRect(Rect::fromXYWH(25.0F, 0.0F, 50.0F, 50.0F));
    list.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 50.0F),
                  Color::fromRGBA(255, 0, 0));
    list.restore();
    list.restore();

    CpuRenderer submitted;
    FrameInfo info;
    info.viewport = Size{100.0F, 50.0F};
    submitted.submit(list, info);

    CpuRenderer immediate;
    immediate.beginFrame(Size{100.0F, 50.0F});
    immediate.save();
    immediate.clipRect(Rect::fromXYWH(0.0F, 0.0F, 50.0F, 50.0F));
    immediate.save();
    immediate.clipRect(Rect::fromXYWH(25.0F, 0.0F, 50.0F, 50.0F));
    immediate.drawRect(Rect::fromXYWH(0.0F, 0.0F, 100.0F, 50.0F),
                       Color::fromRGBA(255, 0, 0));
    immediate.restore();
    immediate.restore();
    immediate.endFrame();

    CHECK(frameHash(submitted.pixels()) == frameHash(immediate.pixels()));
}

// --- M1：shaped 文本数据随命令录制/序列化（布局与绘制共享同一份结果） ---

namespace {

// 确定性“真实后端”字体双：0.5em advance / 0.7em ascent，与占位不同，
// 用于验证布局与命令携带同一份字体事实。
class FixedMetricsFontManager final : public lumen::text::FontManager {
  public:
    [[nodiscard]] lumen::text::FontBackend backend() const override {
        return lumen::text::FontBackend::Skia;
    }
    [[nodiscard]] bool supportsShaping() const override { return true; }
    [[nodiscard]] std::string resolveFamily(
        const lumen::text::FontQuery&, char32_t) const override {
        return "fixed-test";
    }
    [[nodiscard]] bool glyphMetrics(const lumen::text::FontQuery&,
                                    char32_t,
                                    lumen::text::GlyphMetrics* out)
        const override {
        out->advanceEm = 0.5F;
        out->ascentEm = 0.7F;
        out->descentEm = 0.3F;
        return true;
    }
    [[nodiscard]] bool horizontalMetrics(const lumen::text::FontQuery& query,
                                         float* ascentPx,
                                         float* descentPx)
        const override {
        const float size = query.sizePx > 0.0F ? query.sizePx : 14.0F;
        if (ascentPx != nullptr) {
            *ascentPx = size * 0.7F;
        }
        if (descentPx != nullptr) {
            *descentPx = size * 0.3F;
        }
        return true;
    }
    [[nodiscard]] std::vector<std::string> availableFamilies()
        const override {
        return {"fixed-test"};
    }
};

const lumen::render::RenderCommand* firstTextCommand(
    const lumen::render::RenderCommandList& list) {
    for (const auto& command : list.commands()) {
        if (command.type == lumen::render::CommandType::DrawText) {
            return &command;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("text_commands_carry_shaped_runs_from_layout", "[render][commands]") {
    const auto root = laidOutScene(sampleScene());

    // 默认（占位）：命令带占位 shaped runs + 占位 baseline。
    const RenderCommandList placeholderList = recordScene(root);
    const auto* placeholderText = firstTextCommand(placeholderList);
    REQUIRE(placeholderText != nullptr);
    REQUIRE_FALSE(placeholderText->textRun.shapedRuns.empty());
    CHECK(placeholderText->textRun.baselinePx > 0.0F);
    for (const auto& run : placeholderText->textRun.shapedRuns) {
        CHECK(run.placeholder);
        CHECK(run.family == "lumen-latin");
    }

    // 显式字体源：布局与录制共享同一份度量（advance/baseline 均出自
    // FixedMetricsFontManager）。
    FixedMetricsFontManager fonts;
    const auto shapedRoot = LayoutEngine::layout(
        sampleScene(), Constraints::tight(Size{300.0F, 200.0F}), fonts);
    const RenderCommandList shapedList = recordScene(shapedRoot, {}, fonts);
    const auto* shapedText = firstTextCommand(shapedList);
    REQUIRE(shapedText != nullptr);
    REQUIRE_FALSE(shapedText->textRun.shapedRuns.empty());
    CHECK(shapedText->textRun.baselinePx ==
          Catch::Approx(14.0F * 0.7F).margin(0.01F));
    for (const auto& run : shapedText->textRun.shapedRuns) {
        CHECK_FALSE(run.placeholder);
        CHECK(run.family == "fixed-test");
        for (const auto& glyph : run.glyphs) {
            // 0.5em advance，与布局使用的度量一致。
            CHECK(glyph.advancePx ==
                  Catch::Approx(14.0F * 0.5F).margin(0.01F));
        }
    }
    // 文本节点宽度按真实 advance（"Count: 0" = 8 字符 × 0.5em）。
    CHECK(shapedRoot.children.front().children.front().size.width ==
          Catch::Approx(8.0F * 14.0F * 0.5F).margin(0.05F));

    // 序列化保持 shaped 字段逐位相等（v3）。
    const std::string blob =
        lumen::render::serializeCommands(shapedList);
    lumen::render::RenderCommandList parsed;
    REQUIRE(lumen::render::deserializeCommands(blob, parsed));
    CHECK(parsed == shapedList);
}
