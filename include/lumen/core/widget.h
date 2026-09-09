#pragma once

#include <optional>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"

namespace lumen::core {

enum class WidgetType {
    Container,
    Row,
    Column,
    Stack,
    Text,
    Button,
    TextField,
};

enum class MainAxisAlignment {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class CrossAxisAlignment {
    Start,
    Center,
    End,
    Stretch,
};

enum class StackAlignment {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// Immutable UI description. Aggregates are intentionally copyable so tests and
// the C++ DSL can build trees by value; runtime state lives in Element.
//
// Box model: `width`/`height` are border-box overrides (include `padding`,
// exclude `margin`) and are honored by every widget type, including the child
// constraints derived from them. `margin` is consumed by the parent when
// positioning; `padding` insets the content box.
//
// Container is single-child: only `children[0]` participates in layout; extra
// children are a programming error (asserted in debug builds).
struct Widget {
    WidgetType type{WidgetType::Container};
    std::string key{};

    // Box model shared by every widget.
    std::optional<float> width{};
    std::optional<float> height{};
    // Flex factor when this widget is a direct child of Row/Column.
    // 0 means inflexible; >0 participates in free-space distribution.
    float flex{0.0F};
    EdgeInsets padding{};
    EdgeInsets margin{};

    // Container styling.
    Color color{Color::transparent()};
    CornerRadius radius{CornerRadius::zero()};

    // Row/Column arrangement.
    MainAxisAlignment mainAxis{MainAxisAlignment::Start};
    CrossAxisAlignment crossAxis{CrossAxisAlignment::Start};
    float spacing{0.0F};

    // Stack arrangement.
    StackAlignment stackAlignment{StackAlignment::TopLeft};

    // Leaf content for Text/Button/TextField.
    std::string text{};
    TextStyle textStyle{};
    std::string placeholder{};

    // Explicit position inside a Stack parent. When set, it overrides
    // stackAlignment for this child.
    std::optional<Offset> stackPosition{};

    std::vector<Widget> children{};
};

// --- C++ declarative builders (Stage 1 subset, extended in Stage 3) ---

inline Widget makeContainer(Widget child,
                            std::optional<float> width = std::nullopt,
                            std::optional<float> height = std::nullopt,
                            EdgeInsets padding = {}, EdgeInsets margin = {},
                            Color color = Color::transparent(),
                            CornerRadius radius = CornerRadius::zero(),
                            std::string key = {}) {
    Widget widget;
    widget.type = WidgetType::Container;
    widget.width = width;
    widget.height = height;
    widget.padding = padding;
    widget.margin = margin;
    widget.color = color;
    widget.radius = radius;
    widget.key = std::move(key);
    widget.children.push_back(std::move(child));
    return widget;
}

inline Widget makeContainerLeaf(std::optional<float> width = std::nullopt,
                                std::optional<float> height = std::nullopt,
                                EdgeInsets padding = {}, EdgeInsets margin = {},
                                Color color = Color::transparent(),
                                std::string key = {}) {
    Widget widget;
    widget.type = WidgetType::Container;
    widget.width = width;
    widget.height = height;
    widget.padding = padding;
    widget.margin = margin;
    widget.color = color;
    widget.key = std::move(key);
    return widget;
}

inline Widget makeRow(std::vector<Widget> children,
                      MainAxisAlignment mainAxis = MainAxisAlignment::Start,
                      CrossAxisAlignment crossAxis = CrossAxisAlignment::Start,
                      float spacing = 0.0F, EdgeInsets padding = {},
                      EdgeInsets margin = {}, std::string key = {},
                      std::optional<float> width = std::nullopt,
                      std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Row;
    widget.mainAxis = mainAxis;
    widget.crossAxis = crossAxis;
    widget.spacing = spacing;
    widget.padding = padding;
    widget.margin = margin;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children = std::move(children);
    return widget;
}

inline Widget makeColumn(std::vector<Widget> children,
                         MainAxisAlignment mainAxis = MainAxisAlignment::Start,
                         CrossAxisAlignment crossAxis = CrossAxisAlignment::Start,
                         float spacing = 0.0F, EdgeInsets padding = {},
                         EdgeInsets margin = {}, std::string key = {},
                         std::optional<float> width = std::nullopt,
                         std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Column;
    widget.mainAxis = mainAxis;
    widget.crossAxis = crossAxis;
    widget.spacing = spacing;
    widget.padding = padding;
    widget.margin = margin;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children = std::move(children);
    return widget;
}

inline Widget makeStack(std::vector<Widget> children,
                        StackAlignment alignment = StackAlignment::TopLeft,
                        EdgeInsets padding = {}, EdgeInsets margin = {},
                        std::string key = {},
                        std::optional<float> width = std::nullopt,
                        std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Stack;
    widget.stackAlignment = alignment;
    widget.padding = padding;
    widget.margin = margin;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children = std::move(children);
    return widget;
}

inline Widget makeText(std::string content, TextStyle style = {},
                       EdgeInsets margin = {}, float flex = 0.0F,
                       std::string key = {},
                       std::optional<float> width = std::nullopt,
                       std::optional<float> height = std::nullopt,
                       EdgeInsets padding = {}) {
    Widget widget;
    widget.type = WidgetType::Text;
    widget.text = std::move(content);
    widget.textStyle = style;
    widget.margin = margin;
    widget.padding = padding;
    widget.flex = flex;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    return widget;
}

inline Widget makeButton(std::string label, TextStyle style = {},
                         EdgeInsets margin = {}, float flex = 0.0F,
                         std::string key = {},
                         std::optional<float> width = std::nullopt,
                         std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::Button;
    widget.text = std::move(label);
    widget.textStyle = style;
    widget.margin = margin;
    widget.flex = flex;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    return widget;
}

inline Widget makeTextField(std::string value = {},
                            std::string placeholder = {}, TextStyle style = {},
                            EdgeInsets margin = {}, float flex = 0.0F,
                            std::string key = {},
                            std::optional<float> width = std::nullopt,
                            std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::TextField;
    widget.text = std::move(value);
    widget.placeholder = std::move(placeholder);
    widget.textStyle = style;
    widget.margin = margin;
    widget.flex = flex;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    return widget;
}

// Marks a child as flexible inside Row/Column.
inline Widget withFlex(Widget child, float flex) {
    child.flex = flex;
    return child;
}

// Sets an explicit offset for a Stack child.
inline Widget withStackPosition(Widget child, Offset position) {
    child.stackPosition = position;
    return child;
}

[[nodiscard]] bool isLeafWidget(WidgetType type);
[[nodiscard]] bool isFlexContainer(WidgetType type);

}  // namespace lumen::core
