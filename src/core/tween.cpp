#include "lumen/core/tween.h"

#include <algorithm>

namespace lumen::core {

double applyEasing(Easing easing, double t) {
    const double clamped = std::clamp(t, 0.0, 1.0);
    switch (easing) {
        case Easing::Linear:
            return clamped;
        case Easing::EaseIn:
            return clamped * clamped;
        case Easing::EaseOut:
            return 1.0 - (1.0 - clamped) * (1.0 - clamped);
        case Easing::EaseInOut:
            return clamped < 0.5 ? 2.0 * clamped * clamped
                                 : 1.0 -
                                       2.0 * (1.0 - clamped) *
                                           (1.0 - clamped);
    }
    return clamped;
}

double Tween::progress(double elapsedMs) const {
    if (durationMs <= 0.0) {
        return elapsedMs >= 0.0 ? 1.0 : 0.0;
    }
    return std::clamp(elapsedMs / durationMs, 0.0, 1.0);
}

double Tween::sample(double elapsedMs) const {
    const double eased = applyEasing(easing, progress(elapsedMs));
    return from + (to - from) * eased;
}

bool Tween::finished(double elapsedMs) const {
    return progress(elapsedMs) >= 1.0;
}

}  // namespace lumen::core
