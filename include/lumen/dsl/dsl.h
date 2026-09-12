#pragma once

#include <optional>
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

[[nodiscard]] core::Widget stack(
    std::vector<core::Widget> children,
    core::StackAlignment alignment = core::StackAlignment::TopLeft,
    std::string key = {});

[[nodiscard]] core::Widget container(core::Widget child, core::Color color,
                                     core::CornerRadius radius = {});

// Bound Text renders `literal + value` once applyBinds resolves the key.
[[nodiscard]] core::Widget text(std::string literal = {}, BindRef bound = {});

[[nodiscard]] core::Widget button(std::string label = {},
                                  OnClickRef handler = {});

[[nodiscard]] core::Widget text_field(BindRef bound,
                                      PlaceholderRef hint = {});
[[nodiscard]] core::Widget text_field(BindRef bound, std::string hint);

// --- M2：与 .lumen 冻结节点集对齐的其余 builder（属性经 core 层
// with* 修饰器叠加：withKey/withFlex/withVariant/withStyleOverrides 等，
// 与文本 DSL 属性同名同义） ---

// Checkbox/Switch：bind 值 "true"/"false"；点击由框架自动切换，
// label 兼作语义标签（同 .lumen checked 属性）。`switch` 是 C++ 关键字，
// 故用 switch_widget。
[[nodiscard]] core::Widget checkbox(std::string label, BindRef bound,
                                   std::string key = {},
                                   bool checked = false);
[[nodiscard]] core::Widget switch_widget(std::string label, BindRef bound,
                                         std::string key = {},
                                         bool checked = false);

// ScrollView/ListView：单子内容纵向滚动；scrollOffset 由应用侧
// ScrollController 写回（withScrollOffset）。
[[nodiscard]] core::Widget scroll_view(
    core::Widget child, std::string key = {},
    std::optional<float> width = std::nullopt,
    std::optional<float> height = std::nullopt);
[[nodiscard]] core::Widget list_view(
    core::Widget child, std::string key = {},
    std::optional<float> width = std::nullopt,
    std::optional<float> height = std::nullopt);

// FocusScope：Tab 遍历不越过域边界（modal/Dialog 焦点恢复用；
// .lumen 冻结语法未包含，仅 C++ builder 提供）。
[[nodiscard]] core::Widget focus_scope(core::Widget child,
                                       std::string key = {});

// Stage identifier for the DSL module contract (Stage 3: C++ declarative
// API; the text `.lumen` DSL lands in Stage 4).
[[nodiscard]] const char* dslStageName();

}  // namespace lumen::dsl
