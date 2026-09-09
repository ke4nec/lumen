#include "lumen/dsl/dsl.h"

namespace lumen::dsl {

core::Widget column(std::vector<core::Widget> children,
                    core::EdgeInsets padding, float spacing) {
    core::Widget widget;
    widget.type = core::WidgetType::Column;
    widget.padding = padding;
    widget.spacing = spacing;
    widget.children = std::move(children);
    return widget;
}

core::Widget row(std::vector<core::Widget> children, core::EdgeInsets padding,
                 float spacing) {
    core::Widget widget;
    widget.type = core::WidgetType::Row;
    widget.padding = padding;
    widget.spacing = spacing;
    widget.children = std::move(children);
    return widget;
}

core::Widget container(core::Widget child, core::Color color,
                       core::CornerRadius radius) {
    core::Widget widget;
    widget.type = core::WidgetType::Container;
    widget.color = color;
    widget.radius = radius;
    widget.children.push_back(std::move(child));
    return widget;
}

core::Widget text(std::string literal, BindRef bound) {
    core::Widget widget;
    widget.type = core::WidgetType::Text;
    widget.text = literal;
    // bindPrefix preserves the literal across applyBinds re-runs.
    widget.bindPrefix = literal;
    widget.bind = std::move(bound.key);
    return widget;
}

core::Widget button(std::string label, OnClickRef handler) {
    core::Widget widget;
    widget.type = core::WidgetType::Button;
    widget.text = std::move(label);
    widget.onClick = std::move(handler.name);
    return widget;
}

core::Widget text_field(BindRef bound, PlaceholderRef hint) {
    core::Widget widget;
    widget.type = core::WidgetType::TextField;
    widget.bind = std::move(bound.key);
    widget.placeholder = std::move(hint.text);
    return widget;
}

core::Widget text_field(BindRef bound, std::string hint) {
    return text_field(std::move(bound), PlaceholderRef{std::move(hint)});
}

const char* dslStageName() {
    return "stage3";
}

}  // namespace lumen::dsl
