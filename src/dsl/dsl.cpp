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

core::Widget stack(std::vector<core::Widget> children,
                   core::StackAlignment alignment, std::string key) {
    core::Widget widget;
    widget.type = core::WidgetType::Stack;
    widget.stackAlignment = alignment;
    widget.key = std::move(key);
    widget.children = std::move(children);
    return widget;
}

core::Widget checkbox(std::string label, BindRef bound, std::string key,
                       bool checked) {
    return core::makeCheckbox(std::move(label), std::move(bound.key),
                              std::move(key), checked);
}

core::Widget switch_widget(std::string label, BindRef bound, std::string key,
                           bool checked) {
    return core::makeSwitch(std::move(label), std::move(bound.key),
                            std::move(key), checked);
}

core::Widget scroll_view(core::Widget child, std::string key,
                         std::optional<float> width,
                         std::optional<float> height) {
    return core::makeScrollView(std::move(child), std::move(key), width,
                                height);
}

core::Widget list_view(core::Widget child, std::string key,
                       std::optional<float> width,
                       std::optional<float> height) {
    return core::makeListView(std::move(child), std::move(key), width,
                              height);
}

core::Widget focus_scope(core::Widget child, std::string key) {
    return core::makeFocusScope(std::move(child), std::move(key));
}

const char* dslStageName() {
    return "stage4";
}

}  // namespace lumen::dsl
