#pragma once

namespace lumen::render::detail {

// RGBA8 round-to-nearest product. Inputs are byte channels; intermediates
// deliberately stay unsigned (alpha plan §3.3). Tests use an independent model.
constexpr unsigned mul255(unsigned channel, unsigned alpha) {
    return (channel * alpha + 127U) / 255U;
}

}  // namespace lumen::render::detail
