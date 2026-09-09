#pragma once

#include <string>
#include <utility>
#include <vector>

#include "lumen/core/widget.h"

namespace lumen::dsl {

// C++ declarative API (plan §6.1). Builders only produce Widget
// descriptions; state resolution (bind) and event wiring (onClick) are
// looked up by name in the application's StateStore/HandlerRegistry.
//
//   auto page = column({
//       text("Count: ", bind("counter")),
//       button("Increment", onClick("increment")),
//       text_field(bind("name"), placeholder("Name")),
//   });

struct BindRef {
    std::string key{};
};

struct OnClickRef {
    std::string name{};
};

struct PlaceholderRef {
    std::string text{};
};

[[nodiscard]] inline BindRef bind(std::string key) {
    return BindRef{std::move(key)};
}

[[nodiscard]] inline OnClickRef onClick(std::string name) {
    return OnClickRef{std::move(name)};
}

[[nodiscard]] inline PlaceholderRef placeholder(std::string text) {
    return PlaceholderRef{std::move(text)};
}

[[nodiscard]] core::Widget column(std::vector<core::Widget> children,
                                  core::EdgeInsets padding = core::EdgeInsets{},
                                  float spacing = 8.0F);

[[nodiscard]] core::Widget row(std::vector<core::Widget> children,
                               core::EdgeInsets padding = core::EdgeInsets{},
                               float spacing = 8.0F);

[[nodiscard]] core::Widget container(core::Widget child, core::Color color,
                                     core::CornerRadius radius = {});

// Bound Text renders `literal + value` once applyBinds resolves the key.
[[nodiscard]] core::Widget text(std::string literal = {}, BindRef bound = {});

[[nodiscard]] core::Widget button(std::string label = {},
                                  OnClickRef handler = {});

[[nodiscard]] core::Widget text_field(BindRef bound,
                                      PlaceholderRef hint = {});
[[nodiscard]] core::Widget text_field(BindRef bound, std::string hint);

// Stage identifier for the DSL module contract (Stage 3: C++ declarative
// API; the text `.lumen` DSL lands in Stage 4).
[[nodiscard]] const char* dslStageName();

}  // namespace lumen::dsl
