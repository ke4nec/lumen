#pragma once

namespace lumen::core {

// Time-driven value animation primitives (plan 阶段6: 动画). Apps own the
// clock: they sample tweens with their own timestamps (SDL_GetTicks in the
// counter, fixed steps in tests), which keeps animation deterministic and
// headless-testable.
enum class Easing {
    Linear,
    EaseIn,     // slow start, fast finish (quadratic)
    EaseOut,    // fast start, slow finish (quadratic)
    EaseInOut,  // smooth at both ends (cubic)
};

// Maps normalized time t (clamped to [0,1]) onto the eased progress [0,1].
[[nodiscard]] double applyEasing(Easing easing, double t);

// One value ramp over a duration. `sample(elapsedMs)` is clamped at both
// ends, so sampling before start returns `from` and after end returns `to`.
struct Tween {
    double from{0.0};
    double to{1.0};
    double durationMs{0.0};
    Easing easing{Easing::Linear};

    // Elapsed time as a 0..1 progress (unclamped input, clamped output).
    [[nodiscard]] double progress(double elapsedMs) const;
    [[nodiscard]] double sample(double elapsedMs) const;
    [[nodiscard]] bool finished(double elapsedMs) const;
};

}  // namespace lumen::core
