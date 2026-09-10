// Tween/easing tests (plan 阶段6: 动画).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "lumen/core/tween.h"

using namespace lumen::core;
using Catch::Matchers::WithinAbs;

TEST_CASE("easing_maps_unit_interval", "[tween]") {
    CHECK_THAT(applyEasing(Easing::Linear, 0.0), WithinAbs(0.0, 1e-9));
    CHECK_THAT(applyEasing(Easing::Linear, 0.5), WithinAbs(0.5, 1e-9));
    CHECK_THAT(applyEasing(Easing::Linear, 1.0), WithinAbs(1.0, 1e-9));

    CHECK_THAT(applyEasing(Easing::EaseIn, 0.5), WithinAbs(0.25, 1e-9));
    CHECK_THAT(applyEasing(Easing::EaseOut, 0.5), WithinAbs(0.75, 1e-9));
    CHECK_THAT(applyEasing(Easing::EaseInOut, 0.5), WithinAbs(0.5, 1e-9));
    // Both ends still hit 0 and 1 exactly.
    for (const Easing easing :
         {Easing::Linear, Easing::EaseIn, Easing::EaseOut, Easing::EaseInOut}) {
        CHECK_THAT(applyEasing(easing, 0.0), WithinAbs(0.0, 1e-9));
        CHECK_THAT(applyEasing(easing, 1.0), WithinAbs(1.0, 1e-9));
    }
}

TEST_CASE("easing_clamps_out_of_range_input", "[tween]") {
    CHECK_THAT(applyEasing(Easing::Linear, -1.0), WithinAbs(0.0, 1e-9));
    CHECK_THAT(applyEasing(Easing::Linear, 2.0), WithinAbs(1.0, 1e-9));
    CHECK_THAT(applyEasing(Easing::EaseIn, -3.0), WithinAbs(0.0, 1e-9));
}

TEST_CASE("tween_samples_endpoints_and_middle", "[tween]") {
    const Tween tween{10.0, 20.0, 1000.0, Easing::Linear};
    CHECK_THAT(tween.sample(0.0), WithinAbs(10.0, 1e-9));
    CHECK_THAT(tween.sample(500.0), WithinAbs(15.0, 1e-9));
    CHECK_THAT(tween.sample(1000.0), WithinAbs(20.0, 1e-9));
    // Clamped past both ends: no overshoot.
    CHECK_THAT(tween.sample(5000.0), WithinAbs(20.0, 1e-9));
    CHECK_THAT(tween.sample(-100.0), WithinAbs(10.0, 1e-9));
}

TEST_CASE("tween_finished_and_progress", "[tween]") {
    const Tween tween{0.0, 1.0, 400.0, Easing::EaseOut};
    CHECK_FALSE(tween.finished(0.0));
    CHECK_FALSE(tween.finished(399.999));
    CHECK(tween.finished(400.0));
    CHECK(tween.finished(10000.0));
    CHECK_THAT(tween.progress(100.0), WithinAbs(0.25, 1e-9));
}

TEST_CASE("tween_zero_duration_completes_immediately", "[tween]") {
    const Tween instant{3.0, 7.0, 0.0, Easing::EaseInOut};
    CHECK_THAT(instant.sample(0.0), WithinAbs(7.0, 1e-9));
    CHECK(instant.finished(0.0));
    CHECK_THAT(instant.sample(-1.0), WithinAbs(3.0, 1e-9));
}

TEST_CASE("tween_backward_ramp", "[tween]") {
    // The caret blink fades 1 -> 0 then back.
    const Tween fade{1.0, 0.0, 530.0, Easing::EaseInOut};
    CHECK_THAT(fade.sample(0.0), WithinAbs(1.0, 1e-9));
    CHECK_THAT(fade.sample(530.0), WithinAbs(0.0, 1e-9));
    CHECK_THAT(fade.sample(265.0), WithinAbs(0.5, 1e-9));
}
