#pragma once

#include <cstdint>
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
    // v0.3 阶段8D 应用基础组件（plan §3.4）。
    ScrollView,  // 垂直滚动视口：子内容主轴不限，painter 裁剪
    ListView,   // ScrollView + 稳定 key 列表语义（无虚拟化）
    Checkbox,   // 勾选框：bind 值 "true"/"false"，点击自动切换
    Switch,     // 开关：同 Checkbox 行为，不同绘制
    FocusScope, // 焦点域：Tab 遍历不越过边界（Dialog/Route 用）
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

    // v0.3 阶段8B TextField 最小属性（plan §3.2 密码/只读/多行）。
    bool obscure{false};    // 密码模式：绘制为圆点
    bool readOnly{false};   // 只读：编辑键与 IME 提交被拒绝
    bool multiline{false};  // 多行：Enter 换行而非失焦

    // v0.3 阶段8C 语义覆盖（plan §3.3）：应用可覆盖 label/value/role/
    // actions；空字符串 = 使用 widget 默认值，semanticsActions 与推断值
    // 相或。role 取 SemanticsRole 名称（"button"/"checkbox"/...），由
    // lumen-accessibility 解析——core 不依赖语义枚举。
    std::string semanticsLabel{};
    std::string semanticsValue{};
    std::string semanticsRole{};
    std::uint32_t semanticsActions{0};

    // v0.3 阶段8D（plan §3.4）：
    // Checkbox/Switch 的选中状态（applyBinds 从 bind 值解析）。
    bool checked{false};
    // ScrollView/ListView 的当前滚动偏移（应用侧 ScrollController 持有，
    // 重建时写回）。
    float scrollOffset{0.0F};

    // Stage 3 semantics: `bind` names a StateStore key, `onClick` names a
    // handler in the app's HandlerRegistry. `bindPrefix` preserves the
    // literal part of a bound Text ("Count: " + value) across rebuilds.
    std::string bind{};
    std::string bindPrefix{};
    std::string onClick{};

    // Explicit position inside a Stack parent. When set, it overrides
    // stackAlignment for this child.
    std::optional<Offset> stackPosition{};

    std::vector<Widget> children{};

    // Structural equality over every field; DSL golden tests compare parsed
    // trees against C++-built ones.
    [[nodiscard]] bool operator==(const Widget& other) const = default;
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
                         std::optional<float> height = std::nullopt,
                         std::string onClick = {}) {
    Widget widget;
    widget.type = WidgetType::Button;
    widget.text = std::move(label);
    widget.textStyle = style;
    widget.margin = margin;
    widget.flex = flex;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.onClick = std::move(onClick);
    return widget;
}

inline Widget makeTextField(std::string value = {},
                            std::string placeholder = {}, TextStyle style = {},
                            EdgeInsets margin = {}, float flex = 0.0F,
                            std::string key = {},
                            std::optional<float> width = std::nullopt,
                            std::optional<float> height = std::nullopt,
                            std::string bind = {}) {
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
    widget.bind = std::move(bind);
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

// Attaches a HandlerRegistry event name; bubbles from the hit target upward.
inline Widget withOnClick(Widget child, std::string handler) {
    child.onClick = std::move(handler);
    return child;
}

// Attaches a StateStore key to any widget.
inline Widget withBind(Widget child, std::string key) {
    child.bind = std::move(key);
    return child;
}

// Attaches an identity key; used for reuse, focus tracking and press state.
inline Widget withKey(Widget child, std::string key) {
    child.key = std::move(key);
    return child;
}

// v0.3 阶段8B: TextField 属性修饰。
inline Widget withObscure(Widget child, bool obscure = true) {
    child.obscure = obscure;
    return child;
}
inline Widget withReadOnly(Widget child, bool readOnly = true) {
    child.readOnly = readOnly;
    return child;
}
inline Widget withMultiline(Widget child, bool multiline = true) {
    child.multiline = multiline;
    return child;
}

// v0.3 阶段8D: 滚动与选择组件构建器。
// ScrollView：单子内容（通常为 Column），纵向滚动；viewport 尺寸由
// width/height 或父约束给出，scrollOffset 由应用侧 ScrollController
// 在重建时写回。
inline Widget makeScrollView(Widget child, std::string key = {},
                             std::optional<float> width = std::nullopt,
                             std::optional<float> height = std::nullopt,
                             EdgeInsets padding = {}) {
    Widget widget;
    widget.type = WidgetType::ScrollView;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.padding = padding;
    widget.children.push_back(std::move(child));
    return widget;
}

// ListView：语义上的列表（role=list）；实现同 ScrollView，子节点应为带
// 稳定 key 的列表项（首期不做虚拟化，plan §3.4）。
inline Widget makeListView(Widget child, std::string key = {},
                           std::optional<float> width = std::nullopt,
                           std::optional<float> height = std::nullopt) {
    Widget widget;
    widget.type = WidgetType::ListView;
    widget.key = std::move(key);
    widget.width = width;
    widget.height = height;
    widget.children.push_back(std::move(child));
    return widget;
}

inline Widget withScrollOffset(Widget widget, float offset) {
    widget.scrollOffset = offset;
    return widget;
}

// Checkbox/Switch：bind 值 "true"/"false"（"1"/"0" 亦接受）；点击由框架
// 自动切换（与 TextField 编辑相同的内建行为）；label 用作语义标签。
inline Widget makeCheckbox(std::string label, std::string bind,
                           std::string key = {}, bool checked = false) {
    Widget widget;
    widget.type = WidgetType::Checkbox;
    widget.text = std::move(label);
    widget.bind = std::move(bind);
    widget.key = std::move(key);
    widget.checked = checked;
    return widget;
}

inline Widget makeSwitch(std::string label, std::string bind,
                         std::string key = {}, bool checked = false) {
    Widget widget;
    widget.type = WidgetType::Switch;
    widget.text = std::move(label);
    widget.bind = std::move(bind);
    widget.key = std::move(key);
    widget.checked = checked;
    return widget;
}

// FocusScope：单子容器；Tab 遍历在域内循环，不越过边界（modal
// barrier/Dialog 焦点恢复用）。
inline Widget makeFocusScope(Widget child, std::string key = {}) {
    Widget widget;
    widget.type = WidgetType::FocusScope;
    widget.key = std::move(key);
    widget.children.push_back(std::move(child));
    return widget;
}

[[nodiscard]] bool isLeafWidget(WidgetType type);
[[nodiscard]] bool isFlexContainer(WidgetType type);
// v0.3 阶段8D：滚轮/键盘/语义滚动的命中目标类型。
[[nodiscard]] bool isScrollableWidget(WidgetType type);

}  // namespace lumen::core
