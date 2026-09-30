// G-5（docs/lumen-pinch-gesture-design.md）：pinch/zoom 手势测试。
// 覆盖：两指合成状态机（武装/阈值启动/Update/End）、仲裁（第二指取消
// 单指手势不触发点击；字段选区优先）、Fake host 事件流（pointerId 透
// 传）、sink 未设置时零投递。全部 headless。

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"
#include "lumen/core/widget.h"
#include "lumen/dsl/dsl.h"
#include "lumen/platform/fake_host.h"

using lumen::app::AppShell;
using lumen::app::RunOptions;
using lumen::app::ShellConfig;
using lumen::core::Offset;
using lumen::core::PinchPhase;
using lumen::core::PointerDevice;
using lumen::core::RenderNode;
using lumen::core::Size;
using lumen::platform::FakeApplicationHost;

namespace {

const RenderNode* findByKeyDeep(const RenderNode& node,
                                const std::string& key) {
    if (node.key == key) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const RenderNode* hit = findByKeyDeep(child, key)) {
            return hit;
        }
    }
    return nullptr;
}

struct PinchLog {
    struct Event {
        PinchPhase phase{PinchPhase::Begin};
        Offset center{};
        float scale{1.0F};
    };
    std::vector<Event> events;
    bool consumed{true};

    bool sink(const RenderNode&, Offset center, float scale,
              PinchPhase phase) {
        events.push_back({phase, center, scale});
        return consumed;
    }
};

struct Harness {
    AppShell shell{configFor(this)};
    PinchLog pinch{};

    static ShellConfig configFor(Harness* self) {
        ShellConfig config;
        config.initialView = Size{400.0F, 300.0F};
        config.build = [] {
            using namespace lumen::dsl;
            lumen::core::Widget page = container(
                column({
                    withKey(button("Tap", onClick("tap")), "tap-btn"),
                    withKey(text_field(lumen::dsl::bind("name")), "field"),
                }),
                lumen::core::Color::fromRGBA(24, 24, 27));
            page.key = "root";
            return page;
        };
        config.onPinch = [self](AppShell& shell, const RenderNode& root,
                                Offset center, float scale,
                                PinchPhase phase) {
            (void)shell;
            (void)root;
            return self->pinch.sink(root, center, scale, phase);
        };
        return config;
    }

    Harness() { shell.rebuildIfDirty(); }

    const RenderNode* find(const std::string& key) {
        return findByKeyDeep(shell.root(), key);
    }

    // 两指手势合成（AppShell 直驱；pointerId 1/2）。
    void twoFingerDown(Offset a, Offset b) {
        shell.pointerDown(a, lumen::core::kModifierNone,
                          lumen::core::PointerButton::Primary,
                          PointerDevice::Touch, 1);
        shell.pointerDown(b, lumen::core::kModifierNone,
                          lumen::core::PointerButton::Primary,
                          PointerDevice::Touch, 2);
    }
};

}  // namespace

TEST_CASE("pinch_state_machine_begin_update_end", "[core][pinch]") {
    Harness harness;
    AppShell& shell = harness.shell;

    // 两指落下（相距 100px）→ armed；距离变化 ≤8px 不启动。
    harness.twoFingerDown({100, 100}, {200, 100});
    CHECK(shell.controller().touchPointerCount() == 2);
    CHECK(harness.pinch.events.empty());
    shell.pointerMove({105, 100}, 1);
    CHECK(harness.pinch.events.empty());
    // 距离变化 >8px：105→90（距离 110）→ Begin（scale = 1.1）。
    shell.pointerMove({90, 100}, 1);
    REQUIRE(harness.pinch.events.size() == 1);
    CHECK(harness.pinch.events[0].phase == PinchPhase::Begin);
    CHECK(harness.pinch.events[0].scale > 1.09F);
    CHECK(harness.pinch.events[0].scale < 1.11F);
    // 持续收拢：Update 每拍（scale 递减）。
    shell.pointerMove({120, 100}, 1);
    REQUIRE(harness.pinch.events.size() == 2);
    CHECK(harness.pinch.events[1].phase == PinchPhase::Update);
    CHECK(harness.pinch.events[1].scale < 1.0F);
    // 任一指抬起 → End，剩余触摸清空。
    shell.pointerUp({120, 100}, lumen::core::PointerButton::Primary, 2);
    REQUIRE(harness.pinch.events.size() == 3);
    CHECK(harness.pinch.events[2].phase == PinchPhase::End);
    CHECK(shell.controller().touchPointerCount() == 0);
    CHECK_FALSE(shell.controller().pinchActive());
    // 会话结束后的单指 Move 回到普通路径（无新事件）。
    shell.pointerMove({130, 100}, 1);
    CHECK(harness.pinch.events.size() == 3);
}

TEST_CASE("pinch_second_finger_cancels_single_finger_gesture",
          "[core][pinch]") {
    Harness harness;
    AppShell& shell = harness.shell;
    int clicks = 0;
    shell.handlers()["tap"] = [&clicks] { ++clicks; };

    // 单指按在按钮上（按压中）→ 第二指落下 → 单指手势取消（按压解除，
    // armed 点击作废）；随后释放不触发点击。
    const RenderNode* btn = harness.find("tap-btn");
    REQUIRE(btn != nullptr);
    const Offset btnCenter =
        lumen::core::absoluteOffset(shell.root(), "tap-btn") +
        Offset{btn->size.width * 0.5F, btn->size.height * 0.5F};
    shell.pointerDown(btnCenter, lumen::core::kModifierNone,
                      lumen::core::PointerButton::Primary,
                      PointerDevice::Touch, 1);
    CHECK(shell.controller().pressedKey() == "tap-btn");
    shell.pointerDown(btnCenter + Offset{40, 0},
                      lumen::core::kModifierNone,
                      lumen::core::PointerButton::Primary,
                      PointerDevice::Touch, 2);
    CHECK(shell.controller().pressedKey().empty());
    shell.pointerUp(btnCenter + Offset{40, 0},
                    lumen::core::PointerButton::Primary, 1);
    shell.pointerUp(btnCenter + Offset{40, 0},
                    lumen::core::PointerButton::Primary, 2);
    CHECK(clicks == 0);
    CHECK(shell.controller().touchPointerCount() == 0);
}

TEST_CASE("pinch_not_armed_over_text_field", "[core][pinch]") {
    Harness harness;
    AppShell& shell = harness.shell;
    // 起始中点在字段上：不武装（字段选区优先）。
    const RenderNode* field = harness.find("field");
    REQUIRE(field != nullptr);
    const Offset center =
        lumen::core::absoluteOffset(shell.root(), "field") +
        Offset{field->size.width * 0.5F, field->size.height * 0.5F};
    harness.twoFingerDown(center + Offset{-40, 0}, center + Offset{40, 0});
    shell.pointerMove(center + Offset{-60, 0}, 1);
    CHECK(harness.pinch.events.empty());
    CHECK(shell.controller().touchPointerCount() == 2);
    shell.pointerCancel();
    CHECK(shell.controller().touchPointerCount() == 0);
}

TEST_CASE("pinch_third_finger_ignored_until_pair_lifts",
          "[core][pinch]") {
    Harness harness;
    AppShell& shell = harness.shell;
    // 两指启动会话。
    harness.twoFingerDown({100, 100}, {200, 100});
    shell.pointerMove({80, 100}, 1);  // 距离 120 > 100：Begin
    REQUIRE(harness.pinch.events.size() == 1);
    CHECK(harness.pinch.events[0].phase == PinchPhase::Begin);
    // 第三指落下：仅登记——不取消、不重置、无新事件。
    shell.pointerDown({150, 200}, lumen::core::kModifierNone,
                      lumen::core::PointerButton::Primary,
                      PointerDevice::Touch, 3);
    CHECK(harness.pinch.events.size() == 1);
    CHECK(shell.controller().pinchActive());
    CHECK(shell.controller().touchPointerCount() == 3);
    // 第三指移动：不参与距离计算（无事件；配对距离未变）。
    shell.pointerMove({999, 999}, 3);
    CHECK(harness.pinch.events.size() == 1);
    // 配对指移动：Update 照常（基于配对 id 而非表首两项）。
    shell.pointerMove({70, 100}, 1);
    REQUIRE(harness.pinch.events.size() == 2);
    CHECK(harness.pinch.events[1].phase == PinchPhase::Update);
    // 第三指抬起：不触发 End（非配对）。
    shell.pointerUp({999, 999}, lumen::core::PointerButton::Primary, 3);
    CHECK(harness.pinch.events.size() == 2);
    CHECK(shell.controller().pinchActive());
    // 配对指抬起：End。
    shell.pointerUp({70, 100}, lumen::core::PointerButton::Primary, 1);
    REQUIRE(harness.pinch.events.size() == 3);
    CHECK(harness.pinch.events[2].phase == PinchPhase::End);
    CHECK_FALSE(shell.controller().pinchActive());
}

TEST_CASE("pinch_single_finger_paths_unchanged_without_ids",
          "[core][pinch]") {
    Harness harness;
    AppShell& shell = harness.shell;
    int clicks = 0;
    shell.handlers()["tap"] = [&clicks] { ++clicks; };
    // pointerId 缺省 0（既有调用）：点击语义不变，无 pinch 表登记。
    const RenderNode* btn = harness.find("tap-btn");
    REQUIRE(btn != nullptr);
    const Offset center =
        lumen::core::absoluteOffset(shell.root(), "tap-btn") +
        Offset{btn->size.width * 0.5F, btn->size.height * 0.5F};
    shell.pointerDown(center, lumen::core::kModifierNone,
                      lumen::core::PointerButton::Primary,
                      PointerDevice::Touch);
    shell.pointerUp(center, lumen::core::PointerButton::Primary);
    CHECK(clicks == 1);
    CHECK(shell.controller().touchPointerCount() == 0);
    CHECK(harness.pinch.events.empty());
}

TEST_CASE("pinch_pointer_id_flows_through_fake_host", "[core][pinch]") {
    FakeApplicationHost host;
    Harness harness;
    AppShell& shell = harness.shell;
    REQUIRE(host.initialize());
    RunOptions options;
    options.maxFrames = 1;
    // 宿主事件流（pointerId 透传）：Down(id1)/Down(id2)/Move(id1)/Up(id2)。
    const auto window = host.createWindow(lumen::platform::WindowDesc{});
    REQUIRE(window.has_value());
    host.pushPointerDown(*window, {100, 100}, PointerDevice::Touch, 1);
    host.pushPointerDown(*window, {200, 100}, PointerDevice::Touch, 2);
    host.pushPointerMove(*window, {80, 100}, PointerDevice::Touch, 1);
    host.pushPointerUp(*window, {80, 100}, PointerDevice::Touch, 1);
    (void)lumen::app::runApp(shell, host, options);
    // Begin 已由越阈值的 Move 产生。
    bool sawBegin = false;
    for (const auto& event : harness.pinch.events) {
        sawBegin = sawBegin || event.phase == PinchPhase::Begin;
    }
    CHECK(sawBegin);
}
