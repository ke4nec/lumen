#include "lumen/dsl/design_schema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace lumen::dsl {
namespace {

using core::ButtonVariant;
using core::Color;
using core::ControlSize;
using core::CrossAxisAlignment;
using core::MainAxisAlignment;
using core::StackAlignment;
using core::TextOverflow;
using core::Widget;
using core::WidgetType;

[[nodiscard]] DesignError errorAt(const std::string& code,
                                  const std::string& file,
                                  const std::string& message) {
    return DesignError{code, file, SourcePos{}, message, {}, {}, 0, {}, {}};
}

[[nodiscard]] DesignValue booleanValue(bool value) {
    return DesignValue{DesignValue::Variant{value}};
}

[[nodiscard]] DesignValue numberValue(double value) {
    return DesignValue{DesignValue::Variant{value}};
}

[[nodiscard]] DesignValue stringValue(std::string value) {
    return DesignValue{DesignValue::Variant{std::move(value)}};
}

[[nodiscard]] DesignValue colorValue(Color value) {
    return DesignValue{DesignValue::Variant{value}};
}

[[nodiscard]] DesignValue enumValue(std::string domain, std::string value) {
    return DesignValue{DesignValue::Variant{
        DesignEnum{std::move(domain), std::move(value)}}};
}

[[nodiscard]] bool isIdentifier(const std::string& value) {
    if (value.empty()) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (std::isalpha(first) == 0 && value.front() != '_') return false;
    for (const char character : value) {
        const auto c = static_cast<unsigned char>(character);
        if (std::isalnum(c) == 0 && character != '_') return false;
    }
    return true;
}

[[nodiscard]] const std::string* stringOf(const DesignValue& value) {
    return std::get_if<std::string>(&value.value);
}

[[nodiscard]] const double* numberOf(const DesignValue& value) {
    return std::get_if<double>(&value.value);
}

[[nodiscard]] const bool* boolOf(const DesignValue& value) {
    return std::get_if<bool>(&value.value);
}

[[nodiscard]] const DesignEnum* enumOf(const DesignValue& value) {
    return std::get_if<DesignEnum>(&value.value);
}

[[nodiscard]] bool isStyled(WidgetType type) {
    return type == WidgetType::Text || type == WidgetType::Button ||
           type == WidgetType::TextField || type == WidgetType::Checkbox ||
           type == WidgetType::Switch;
}

[[nodiscard]] bool isSingleChild(WidgetType type) {
    return type == WidgetType::Container || type == WidgetType::ScrollView ||
           type == WidgetType::ListView || type == WidgetType::FocusScope;
}

[[nodiscard]] bool isLeaf(WidgetType type) {
    return type == WidgetType::Text || type == WidgetType::Button ||
           type == WidgetType::TextField || type == WidgetType::Checkbox ||
           type == WidgetType::Switch;
}

[[nodiscard]] DesignValue readProperty(const Widget& widget,
                                       const std::string& name) {
    if (name == "key") return stringValue(widget.key);
    if (name == "width") {
        return widget.width.has_value() ? numberValue(*widget.width) : DesignValue{};
    }
    if (name == "height") {
        return widget.height.has_value() ? numberValue(*widget.height) : DesignValue{};
    }
    if (name == "flex") return numberValue(widget.flex);
    if (name == "padding") return numberValue(widget.padding.left);
    if (name == "margin") return numberValue(widget.margin.left);
    if (name == "color") {
        return colorValue(isStyled(widget.type) ? widget.textStyle.color
                                                : widget.color);
    }
    if (name == "bind") return stringValue(widget.bind);
    if (name == "onClick") return stringValue(widget.onClick);
    if (name == "left") {
        return numberValue(widget.stackPosition.has_value()
                               ? widget.stackPosition->x
                               : 0.0);
    }
    if (name == "top") {
        return numberValue(widget.stackPosition.has_value()
                               ? widget.stackPosition->y
                               : 0.0);
    }
    if (name == "radius") return numberValue(widget.radius.topLeft);
    if (name == "spacing") return numberValue(widget.spacing);
    if (name == "mainAxis") {
        static const char* names[] = {"start", "center", "end", "spaceBetween",
                                      "spaceAround", "spaceEvenly"};
        return enumValue("mainAxis", names[static_cast<int>(widget.mainAxis)]);
    }
    if (name == "crossAxis") {
        static const char* names[] = {"start", "center", "end", "stretch"};
        return enumValue("crossAxis", names[static_cast<int>(widget.crossAxis)]);
    }
    if (name == "alignment") {
        static const char* names[] = {"topLeft", "topCenter", "topRight",
                                      "centerLeft", "center", "centerRight",
                                      "bottomLeft", "bottomCenter", "bottomRight"};
        return enumValue("alignment", names[static_cast<int>(widget.stackAlignment)]);
    }
    if (name == "text") return stringValue(widget.text);
    if (name == "placeholder") return stringValue(widget.placeholder);
    if (name == "fontSize") return numberValue(widget.textStyle.fontSize);
    if (name == "bold") return booleanValue(widget.textStyle.bold);
    if (name == "family") return stringValue(widget.textStyle.family);
    if (name == "weight") return numberValue(widget.textStyle.weight);
    if (name == "italic") return booleanValue(widget.textStyle.italic);
    if (name == "letterSpacing") return numberValue(widget.textStyle.letterSpacing);
    if (name == "lineHeight") return numberValue(widget.textStyle.lineHeight);
    if (name == "maxLines") return numberValue(widget.textStyle.maxLines);
    if (name == "overflow") {
        static const char* names[] = {"clip", "ellipsis", "fade", "visible"};
        return enumValue("overflow", names[static_cast<int>(widget.textStyle.overflow)]);
    }
    if (name == "obscure") return booleanValue(widget.obscure);
    if (name == "readOnly") return booleanValue(widget.readOnly);
    if (name == "multiline") return booleanValue(widget.multiline);
    if (name == "variant") {
        static const char* names[] = {"filled", "tonal", "outline", "ghost",
                                      "danger", "windowClose", "chrome"};
        return enumValue("variant", names[static_cast<int>(widget.buttonVariant)]);
    }
    if (name == "size") {
        static const char* names[] = {"small", "medium", "large"};
        return enumValue("size", names[static_cast<int>(widget.controlSize)]);
    }
    if (name == "enabled") return booleanValue(widget.enabled);
    if (name == "invalid") return booleanValue(widget.invalid);
    if (name == "selected") return booleanValue(widget.selected);
    if (name == "showFocusRing") return booleanValue(widget.showFocusRing);
    if (name == "checked") return booleanValue(widget.checked);
    if (name == "scrollOffset") return numberValue(widget.scrollOffset);
    return DesignValue{};
}

template <typename T>
[[nodiscard]] bool numberRepresentable(double number) {
    if (!std::isfinite(number)) return false;
    if constexpr (std::is_floating_point_v<T>) {
        const double maxValue =
            static_cast<double>(std::numeric_limits<T>::max());
        return number >= -maxValue && number <= maxValue;
    }
    const double roundedMax =
        static_cast<double>(std::numeric_limits<T>::max());
    const double maxValue = [&] {
        if constexpr (std::numeric_limits<T>::digits >
                      std::numeric_limits<double>::digits) {
            return std::nextafter(roundedMax, 0.0);
        }
        return roundedMax;
    }();
    return number >= 0.0 && number <= maxValue && std::floor(number) == number;
}

template <typename T>
[[nodiscard]] bool assignNumber(const DesignValue& value, T& destination) {
    const auto* number = numberOf(value);
    if (number == nullptr || !numberRepresentable<T>(*number)) return false;
    destination = static_cast<T>(*number);
    return true;
}

[[nodiscard]] bool writeProperty(Widget& widget, const std::string& name,
                                 const DesignValue& value) {
    if (name == "key") {
        const auto* string = stringOf(value);
        if (string == nullptr) return false;
        widget.key = *string;
        return true;
    }
    if (name == "width") {
        if (std::holds_alternative<std::monostate>(value.value)) {
            widget.width.reset();
            return true;
        }
        float number = 0.0F;
        if (!assignNumber(value, number)) return false;
        widget.width = number;
        return true;
    }
    if (name == "height") {
        if (std::holds_alternative<std::monostate>(value.value)) {
            widget.height.reset();
            return true;
        }
        float number = 0.0F;
        if (!assignNumber(value, number)) return false;
        widget.height = number;
        return true;
    }
    if (name == "flex") return assignNumber(value, widget.flex);
    if (name == "padding") return assignNumber(value, widget.padding.left) &&
                                      (widget.padding = core::EdgeInsets::all(widget.padding.left), true);
    if (name == "margin") return assignNumber(value, widget.margin.left) &&
                                     (widget.margin = core::EdgeInsets::all(widget.margin.left), true);
    if (name == "color") {
        const auto* color = std::get_if<Color>(&value.value);
        if (color == nullptr) return false;
        if (isStyled(widget.type)) widget.textStyle.color = *color;
        else widget.color = *color;
        return true;
    }
    if (name == "bind" || name == "onClick") {
        const auto* string = stringOf(value);
        if (string == nullptr) return false;
        (name == "bind" ? widget.bind : widget.onClick) = *string;
        return true;
    }
    if (name == "left" || name == "top") {
        float number = 0.0F;
        if (!assignNumber(value, number)) return false;
        if (!widget.stackPosition.has_value()) widget.stackPosition = core::Offset{};
        (name == "left" ? widget.stackPosition->x : widget.stackPosition->y) = number;
        return true;
    }
    if (name == "radius") return assignNumber(value, widget.radius.topLeft) &&
                                      (widget.radius = core::CornerRadius::all(widget.radius.topLeft), true);
    if (name == "spacing") return assignNumber(value, widget.spacing);
    if (name == "mainAxis") {
        const auto* enumeration = enumOf(value);
        if (enumeration == nullptr) return false;
        static const std::vector<std::string> names = {"start", "center", "end",
            "spaceBetween", "spaceAround", "spaceEvenly"};
        const auto found = std::find(names.begin(), names.end(), enumeration->value);
        if (found == names.end()) return false;
        widget.mainAxis = static_cast<MainAxisAlignment>(found - names.begin());
        return true;
    }
    if (name == "crossAxis") {
        const auto* enumeration = enumOf(value);
        if (enumeration == nullptr) return false;
        static const std::vector<std::string> names = {"start", "center", "end", "stretch"};
        const auto found = std::find(names.begin(), names.end(), enumeration->value);
        if (found == names.end()) return false;
        widget.crossAxis = static_cast<CrossAxisAlignment>(found - names.begin());
        return true;
    }
    if (name == "alignment") {
        const auto* enumeration = enumOf(value);
        if (enumeration == nullptr) return false;
        static const std::vector<std::string> names = {"topLeft", "topCenter", "topRight",
            "centerLeft", "center", "centerRight", "bottomLeft", "bottomCenter", "bottomRight"};
        const auto found = std::find(names.begin(), names.end(), enumeration->value);
        if (found == names.end()) return false;
        widget.stackAlignment = static_cast<StackAlignment>(found - names.begin());
        return true;
    }
    if (name == "text" || name == "placeholder" || name == "family") {
        const auto* string = stringOf(value);
        if (string == nullptr) return false;
        if (name == "text") widget.text = *string;
        else if (name == "placeholder") widget.placeholder = *string;
        else widget.textStyle.family = *string;
        return true;
    }
    if (name == "fontSize") return assignNumber(value, widget.textStyle.fontSize);
    if (name == "bold") return boolOf(value) != nullptr &&
                                     (widget.textStyle.bold = *boolOf(value), true);
    if (name == "weight") return assignNumber(value, widget.textStyle.weight);
    if (name == "italic") return boolOf(value) != nullptr &&
                                       (widget.textStyle.italic = *boolOf(value), true);
    if (name == "letterSpacing") return assignNumber(value, widget.textStyle.letterSpacing);
    if (name == "lineHeight") return assignNumber(value, widget.textStyle.lineHeight);
    if (name == "maxLines") return assignNumber(value, widget.textStyle.maxLines);
    if (name == "overflow") {
        const auto* enumeration = enumOf(value);
        if (enumeration == nullptr) return false;
        static const std::vector<std::string> names = {"clip", "ellipsis", "fade", "visible"};
        const auto found = std::find(names.begin(), names.end(), enumeration->value);
        if (found == names.end()) return false;
        widget.textStyle.overflow = static_cast<TextOverflow>(found - names.begin());
        return true;
    }
    if (name == "obscure" || name == "readOnly" || name == "multiline") {
        const auto* boolean = boolOf(value);
        if (boolean == nullptr) return false;
        if (name == "obscure") widget.obscure = *boolean;
        else if (name == "readOnly") widget.readOnly = *boolean;
        else widget.multiline = *boolean;
        return true;
    }
    if (name == "variant") {
        const auto* enumeration = enumOf(value);
        if (enumeration == nullptr) return false;
        static const std::vector<std::string> names = {"filled", "tonal", "outline", "ghost", "danger", "windowClose", "chrome"};
        const auto found = std::find(names.begin(), names.end(), enumeration->value);
        if (found == names.end()) return false;
        widget.buttonVariant = static_cast<ButtonVariant>(found - names.begin());
        return true;
    }
    if (name == "size") {
        const auto* enumeration = enumOf(value);
        if (enumeration == nullptr) return false;
        static const std::vector<std::string> names = {"small", "medium", "large"};
        const auto found = std::find(names.begin(), names.end(), enumeration->value);
        if (found == names.end()) return false;
        widget.controlSize = static_cast<ControlSize>(found - names.begin());
        return true;
    }
    if (name == "enabled" || name == "invalid" || name == "selected" ||
        name == "showFocusRing" || name == "checked") {
        const auto* boolean = boolOf(value);
        if (boolean == nullptr) return false;
        if (name == "enabled") widget.enabled = *boolean;
        else if (name == "invalid") widget.invalid = *boolean;
        else if (name == "selected") widget.selected = *boolean;
        else if (name == "showFocusRing") widget.showFocusRing = *boolean;
        else widget.checked = *boolean;
        return true;
    }
    if (name == "scrollOffset") return assignNumber(value, widget.scrollOffset);
    return false;
}

[[nodiscard]] PropertySpec spec(
    std::string name, PropertyKind kind, PropertyPersistence persistence,
    DesignValue defaultValue, std::vector<std::string> enums = {}) {
    PropertySpec result;
    result.name = name;
    result.kind = kind;
    result.persistence = persistence;
    result.defaultValue = std::move(defaultValue);
    result.enumValues = std::move(enums);
    const std::string property = result.name;
    result.validate = [kind = result.kind, values = result.enumValues,
                       property](
                          const DesignValue& value) {
        switch (kind) {
            case PropertyKind::Boolean: return boolOf(value) != nullptr;
            case PropertyKind::Number: {
                if (std::holds_alternative<std::monostate>(value.value)) {
                    return property == "width" || property == "height";
                }
                const auto* number = numberOf(value);
                if (number == nullptr || !numberRepresentable<float>(*number)) {
                    return false;
                }
                if (property == "weight") {
                    return *number >= 100 && *number <= 900 &&
                           std::floor(*number) == *number &&
                           std::fmod(*number, 100.0) == 0.0;
                }
                if (property == "maxLines") {
                    return numberRepresentable<std::size_t>(*number);
                }
                if (property == "scrollOffset") {
                    return *number >= 0;
                }
                return true;
            }
            case PropertyKind::String:
            case PropertyKind::Reference: return stringOf(value) != nullptr;
            case PropertyKind::Color:
                return std::get_if<Color>(&value.value) != nullptr;
            case PropertyKind::Enum: {
                const auto* enumeration = enumOf(value);
                return enumeration != nullptr &&
                       enumeration->domain == property &&
                       (values.empty() || std::find(values.begin(), values.end(),
                                                     enumeration->value) != values.end());
            }
            case PropertyKind::Object:
                return !std::holds_alternative<std::monostate>(value.value);
        }
        return false;
    };
    if (persistence == PropertyPersistence::RuntimeReference) {
        // References are stored in DesignNode::references and resolved by
        // RuntimeContext; no Widget field can safely own these names.
        return result;
    }
    result.get = [property](const Widget& widget) {
        return readProperty(widget, property);
    };
    result.set = [property](Widget& widget, const DesignValue& value) {
        return writeProperty(widget, property, value);
    };
    return result;
}

void addCommon(std::vector<PropertySpec>& properties, bool styled) {
    properties.push_back(spec("key", PropertyKind::String,
                               PropertyPersistence::Declaration, stringValue("")));
    properties.push_back(spec("width", PropertyKind::Number,
                               PropertyPersistence::Declaration, DesignValue{}));
    properties.push_back(spec("height", PropertyKind::Number,
                               PropertyPersistence::Declaration, DesignValue{}));
    properties.push_back(spec("flex", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(0)));
    properties.push_back(spec("padding", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(0)));
    properties.push_back(spec("margin", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(0)));
    properties.push_back(spec("color", PropertyKind::Color,
                               PropertyPersistence::Declaration,
                               colorValue(styled ? Color::fromRGBA(0, 0, 0)
                                                 : Color::transparent())));
    properties.push_back(spec("bind", PropertyKind::Reference,
                               PropertyPersistence::RuntimeReference, stringValue("")));
    properties.push_back(spec("onClick", PropertyKind::Reference,
                               PropertyPersistence::RuntimeReference, stringValue("")));
    for (const char* name : {"theme", "image", "virtualSource",
                             "splitterSource", "component"}) {
        properties.push_back(spec(name, PropertyKind::Reference,
                                  PropertyPersistence::RuntimeReference,
                                  stringValue("")));
    }
    properties.push_back(spec("left", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(0)));
    properties.push_back(spec("top", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(0)));
    properties.push_back(spec("size", PropertyKind::Enum,
                               PropertyPersistence::Declaration, enumValue("size", "medium"),
                               {"small", "medium", "large"}));
    properties.push_back(spec("enabled", PropertyKind::Boolean,
                               PropertyPersistence::Declaration, booleanValue(true)));
    properties.push_back(spec("invalid", PropertyKind::Boolean,
                               PropertyPersistence::Declaration, booleanValue(false)));
    properties.push_back(spec("selected", PropertyKind::Boolean,
                               PropertyPersistence::Declaration, booleanValue(false)));
    properties.push_back(spec("showFocusRing", PropertyKind::Boolean,
                               PropertyPersistence::Declaration, booleanValue(false)));
}

void addStyled(std::vector<PropertySpec>& properties) {
    properties.push_back(spec("text", PropertyKind::String,
                               PropertyPersistence::Declaration, stringValue("")));
    properties.push_back(spec("placeholder", PropertyKind::String,
                               PropertyPersistence::Declaration, stringValue("")));
    properties.push_back(spec("fontSize", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(14)));
    properties.push_back(spec("bold", PropertyKind::Boolean,
                               PropertyPersistence::Declaration, booleanValue(false)));
    properties.push_back(spec("family", PropertyKind::String,
                               PropertyPersistence::Declaration, stringValue("")));
    properties.push_back(spec("weight", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(400)));
    properties.push_back(spec("italic", PropertyKind::Boolean,
                               PropertyPersistence::Declaration, booleanValue(false)));
    properties.push_back(spec("letterSpacing", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(0)));
    properties.push_back(spec("lineHeight", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(0)));
    properties.push_back(spec("maxLines", PropertyKind::Number,
                               PropertyPersistence::Declaration, numberValue(0)));
    properties.push_back(spec("overflow", PropertyKind::Enum,
                               PropertyPersistence::Declaration, enumValue("overflow", "clip"),
                               {"clip", "ellipsis", "fade", "visible"}));
}

[[nodiscard]] Widget defaultWidget(const std::string& type) {
    Widget widget;
    static const std::pair<const char*, WidgetType> types[] = {
        {"Container", WidgetType::Container}, {"Row", WidgetType::Row},
        {"Column", WidgetType::Column}, {"Stack", WidgetType::Stack},
        {"Text", WidgetType::Text}, {"Button", WidgetType::Button},
        {"TextField", WidgetType::TextField}, {"ScrollView", WidgetType::ScrollView},
        {"ListView", WidgetType::ListView}, {"Checkbox", WidgetType::Checkbox},
        {"Switch", WidgetType::Switch}, {"FocusScope", WidgetType::FocusScope}};
    for (const auto& [name, widgetType] : types) {
        if (type == name) {
            widget.type = widgetType;
            break;
        }
    }
    return widget;
}

[[nodiscard]] NodeSchema makeSchema(const std::string& type) {
    NodeSchema schema;
    schema.type = type;
    schema.canBeRoot = true;
    schema.makeDefault = [type] { return defaultWidget(type); };
    WidgetType widgetType = defaultWidget(type).type;
    addCommon(schema.properties, isStyled(widgetType));
    if (isStyled(widgetType)) addStyled(schema.properties);
    if (widgetType == WidgetType::Button) {
        schema.properties.push_back(spec(
            "variant", PropertyKind::Enum, PropertyPersistence::Declaration,
            enumValue("variant", "filled"),
            {"filled", "tonal", "outline", "ghost", "danger"}));
    }
    if (widgetType == WidgetType::Checkbox || widgetType == WidgetType::Switch) {
        schema.properties.push_back(spec("checked", PropertyKind::Boolean,
                                         PropertyPersistence::Declaration,
                                         booleanValue(false)));
    }
    if (widgetType == WidgetType::TextField) {
        for (const char* name : {"obscure", "readOnly", "multiline"}) {
            schema.properties.push_back(spec(name, PropertyKind::Boolean,
                                             PropertyPersistence::Declaration,
                                             booleanValue(false)));
        }
    }
    if (!isStyled(widgetType)) {
        schema.properties.push_back(spec("radius", PropertyKind::Number,
                                         PropertyPersistence::Declaration,
                                         numberValue(0)));
    }
    if (widgetType == WidgetType::Row || widgetType == WidgetType::Column) {
        schema.properties.push_back(spec("spacing", PropertyKind::Number,
                                         PropertyPersistence::Declaration,
                                         numberValue(0)));
        schema.properties.push_back(spec(
            "mainAxis", PropertyKind::Enum, PropertyPersistence::Declaration,
            enumValue("mainAxis", "start"),
            {"start", "center", "end", "spaceBetween", "spaceAround", "spaceEvenly"}));
        schema.properties.push_back(spec(
            "crossAxis", PropertyKind::Enum, PropertyPersistence::Declaration,
            enumValue("crossAxis", "start"), {"start", "center", "end", "stretch"}));
    }
    if (widgetType == WidgetType::Stack) {
        schema.properties.push_back(spec(
            "alignment", PropertyKind::Enum, PropertyPersistence::Declaration,
            enumValue("alignment", "topLeft"),
            {"topLeft", "topCenter", "topRight", "centerLeft", "center",
             "centerRight", "bottomLeft", "bottomCenter", "bottomRight"}));
    }
    if (widgetType == WidgetType::ScrollView || widgetType == WidgetType::ListView) {
        schema.properties.push_back(spec("scrollOffset", PropertyKind::Number,
                                         PropertyPersistence::PreviewOnly,
                                         numberValue(0)));
    }
    if (isLeaf(widgetType)) schema.maxChildren = 0;
    else if (isSingleChild(widgetType)) schema.maxChildren = 1;
    return schema;
}

[[nodiscard]] DesignError schemaError(const std::string& code,
                                      const DesignNode& node,
                                      const std::string& message,
                                      std::string path,
                                      std::string property = {}) {
    DesignError error{code, "<design>", node.source.has_value()
                                          ? node.source->begin
                                          : SourcePos{},
                      message, {}, {}, node.id, std::move(path),
                      std::move(property)};
    return error;
}

void validateNode(const DesignNode& node, const std::string& path, bool root,
                  std::set<DesignNodeId>& ids,
                  std::vector<DesignError>& diagnostics) {
    if (node.id == 0) {
        diagnostics.push_back(schemaError(
            "schema.node_id", node, "node id must be nonzero", path));
    } else if (!ids.insert(node.id).second) {
        diagnostics.push_back(schemaError(
            "schema.duplicate_node_id", node,
            "node id must be unique within the document", path));
    }
    const NodeSchema* schema = findNodeSchema(node.type);
    if (schema == nullptr) {
        diagnostics.push_back(schemaError(
            "schema.unknown_node", node,
            "node type '" + node.type + "' is not registered", path));
        return;
    }
    if (root && !schema->canBeRoot) {
        diagnostics.push_back(schemaError("schema.root_type", node,
                                          "node type cannot be a root", path));
    }
    if (node.children.size() < schema->minChildren ||
        (schema->maxChildren.has_value() &&
         node.children.size() > *schema->maxChildren)) {
        diagnostics.push_back(schemaError(
            "schema.children", node,
            "invalid child count for node type '" + node.type + "'", path));
    }
    for (const auto& [name, value] : node.properties) {
        const PropertySpec* property = findPropertySpec(*schema, name);
        if (property == nullptr) {
            diagnostics.push_back(schemaError(
                "schema.unknown_property", node,
                "property '" + name + "' is not registered for '" + node.type + "'",
                path, name));
            continue;
        }
        if (property->persistence == PropertyPersistence::RuntimeReference) {
            diagnostics.push_back(schemaError(
                "schema.reference_location", node,
                "runtime reference '" + name +
                    "' must be stored in the references map",
                path, name));
            continue;
        }
        if (property->persistence == PropertyPersistence::PreviewOnly ||
            property->persistence == PropertyPersistence::Derived) {
            diagnostics.push_back(schemaError(
                "schema.non_persistent_property", node,
                "property '" + name + "' is runtime-only and cannot be stored",
                path, name));
            continue;
        }
        if (property->validate && !property->validate(value)) {
            diagnostics.push_back(schemaError(
                "schema.invalid_property", node,
                "property '" + name + "' has the wrong type or value", path, name));
        }
    }
    for (const auto& [name, value] : node.references) {
        const PropertySpec* property = findPropertySpec(*schema, name);
        if (property == nullptr || property->kind != PropertyKind::Reference) {
            diagnostics.push_back(schemaError(
                "schema.unknown_reference", node,
                "reference '" + name + "' is not registered", path, name));
        } else if (!isIdentifier(value)) {
            diagnostics.push_back(schemaError(
                "schema.invalid_reference", node,
                "reference names must be identifiers", path, name));
        }
    }
    if (!node.slots.empty()) {
        diagnostics.push_back(schemaError(
            "schema.unsupported_slots", node,
            "named slots require a component schema", path));
    }
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        validateNode(node.children[i], path + ".children[" + std::to_string(i) + "]",
                     false, ids, diagnostics);
    }
}

}  // namespace

const WidgetFieldInventory& widgetFieldInventory() {
    static const WidgetFieldInventory inventory = {
        {"type", "", WidgetFieldCategory::Structural},
        {"key", "key", WidgetFieldCategory::Declaration},
        {"width", "width", WidgetFieldCategory::Declaration},
        {"height", "height", WidgetFieldCategory::Declaration},
        {"flex", "flex", WidgetFieldCategory::Declaration},
        {"shrinkWrap", "", WidgetFieldCategory::Declaration},
        {"alignContentStart", "", WidgetFieldCategory::Declaration},
        {"reserveIconSpace", "", WidgetFieldCategory::Declaration},
        {"windowDrag", "", WidgetFieldCategory::Declaration},
        {"scrollAxis", "", WidgetFieldCategory::Declaration},
        {"padding", "padding", WidgetFieldCategory::Declaration},
        {"margin", "margin", WidgetFieldCategory::Declaration},
        {"color", "color", WidgetFieldCategory::Declaration},
        {"radius", "radius", WidgetFieldCategory::Declaration},
        {"mainAxis", "mainAxis", WidgetFieldCategory::Declaration},
        {"crossAxis", "crossAxis", WidgetFieldCategory::Declaration},
        {"spacing", "spacing", WidgetFieldCategory::Declaration},
        {"stackAlignment", "alignment", WidgetFieldCategory::Declaration},
        {"text", "text", WidgetFieldCategory::Declaration},
        {"textStyle", "", WidgetFieldCategory::Declaration},
        {"textStyle.fontSize", "fontSize", WidgetFieldCategory::Declaration},
        {"textStyle.bold", "bold", WidgetFieldCategory::Declaration},
        {"textStyle.family", "family", WidgetFieldCategory::Declaration},
        {"textStyle.weight", "weight", WidgetFieldCategory::Declaration},
        {"textStyle.italic", "italic", WidgetFieldCategory::Declaration},
        {"textStyle.letterSpacing", "letterSpacing", WidgetFieldCategory::Declaration},
        {"textStyle.lineHeight", "lineHeight", WidgetFieldCategory::Declaration},
        {"textStyle.maxLines", "maxLines", WidgetFieldCategory::Declaration},
        {"textStyle.overflow", "overflow", WidgetFieldCategory::Declaration},
        {"placeholder", "placeholder", WidgetFieldCategory::Declaration},
        {"obscure", "obscure", WidgetFieldCategory::Declaration},
        {"readOnly", "readOnly", WidgetFieldCategory::Declaration},
        {"multiline", "multiline", WidgetFieldCategory::Declaration},
        {"semanticsLabel", "", WidgetFieldCategory::Declaration},
        {"semanticsValue", "", WidgetFieldCategory::Declaration},
        {"semanticsRole", "", WidgetFieldCategory::Declaration},
        {"semanticsActions", "", WidgetFieldCategory::Declaration},
        {"checked", "checked", WidgetFieldCategory::Declaration},
        {"indeterminate", "", WidgetFieldCategory::Declaration},
        {"scrollOffset", "scrollOffset", WidgetFieldCategory::PreviewOnly},
        {"gridColumnCount", "", WidgetFieldCategory::Declaration},
        {"gridMinColumnWidth", "", WidgetFieldCategory::Declaration},
        {"gridColumnGap", "", WidgetFieldCategory::Declaration},
        {"gridRowGap", "", WidgetFieldCategory::Declaration},
        {"imageId", "", WidgetFieldCategory::Derived},
        {"imageSource", "image", WidgetFieldCategory::RuntimeReference},
        {"themeOverride", "theme", WidgetFieldCategory::RuntimeReference},
        {"virtualSource", "virtualSource", WidgetFieldCategory::RuntimeReference},
        {"virtualCacheExtent", "", WidgetFieldCategory::Declaration},
        {"collectionSelectionMode", "", WidgetFieldCategory::Declaration},
        {"showFocusRing", "showFocusRing", WidgetFieldCategory::Declaration},
        {"gridColumnSpan", "", WidgetFieldCategory::Declaration},
        {"gridRowSpan", "", WidgetFieldCategory::Declaration},
        {"collectionColumns", "component", WidgetFieldCategory::RuntimeReference},
        {"collectionRow", "", WidgetFieldCategory::Derived},
        {"collectionShowHeader", "", WidgetFieldCategory::Declaration},
        {"listPart", "", WidgetFieldCategory::Derived},
        {"treePart", "", WidgetFieldCategory::Derived},
        {"treeDepth", "", WidgetFieldCategory::Derived},
        {"splitterSource", "splitterSource", WidgetFieldCategory::RuntimeReference},
        {"splitterHorizontal", "", WidgetFieldCategory::Declaration},
        {"buttonVariant", "variant", WidgetFieldCategory::Declaration},
        {"controlSize", "size", WidgetFieldCategory::Declaration},
        {"enabled", "enabled", WidgetFieldCategory::Declaration},
        {"invalid", "invalid", WidgetFieldCategory::Declaration},
        {"selected", "selected", WidgetFieldCategory::Declaration},
        {"showScrollbar", "", WidgetFieldCategory::Declaration},
        {"scrollbarAutoHide", "", WidgetFieldCategory::Declaration},
        {"icon", "", WidgetFieldCategory::Declaration},
        {"elevation", "", WidgetFieldCategory::Declaration},
        {"transitionAlpha", "", WidgetFieldCategory::PreviewOnly},
        {"styleOverrides", "", WidgetFieldCategory::Declaration},
        {"progressIndeterminate", "", WidgetFieldCategory::Declaration},
        {"iconRotation", "", WidgetFieldCategory::PreviewOnly},
        {"iconLeading", "", WidgetFieldCategory::Declaration},
        {"clipRounded", "", WidgetFieldCategory::Declaration},
        {"excludeFromSemantics", "", WidgetFieldCategory::Declaration},
        {"excludeFromFocus", "", WidgetFieldCategory::Declaration},
        {"bind", "bind", WidgetFieldCategory::RuntimeReference},
        {"bindPrefix", "", WidgetFieldCategory::Derived},
        {"onClick", "onClick", WidgetFieldCategory::RuntimeReference},
        {"stackPosition.x", "left", WidgetFieldCategory::Declaration},
        {"stackPosition.y", "top", WidgetFieldCategory::Declaration},
        {"children", "", WidgetFieldCategory::Structural},
    };
    return inventory;
}

const std::vector<NodeSchema>& nodeSchemaRegistry() {
    static const std::vector<NodeSchema> registry = [] {
        std::vector<NodeSchema> result;
        for (const char* type : {"Container", "Row", "Column", "Stack", "Text",
                                 "Button", "TextField", "ScrollView", "ListView",
                                 "Checkbox", "Switch", "FocusScope"}) {
            result.push_back(makeSchema(type));
        }
        return result;
    }();
    return registry;
}

const NodeSchema* findNodeSchema(std::string_view type) {
    const auto& registry = nodeSchemaRegistry();
    const auto found = std::find_if(registry.begin(), registry.end(),
                                    [type](const NodeSchema& schema) {
                                        return schema.type == type;
                                    });
    return found == registry.end() ? nullptr : &*found;
}

const PropertySpec* findPropertySpec(const NodeSchema& schema,
                                     std::string_view name) {
    const auto found = std::find_if(schema.properties.begin(), schema.properties.end(),
                                    [name](const PropertySpec& property) {
                                        return property.name == name;
                                    });
    return found == schema.properties.end() ? nullptr : &*found;
}

std::vector<DesignError> validateDesignDocument(const DesignDocument& document) {
    std::vector<DesignError> diagnostics;
    if (document.schemaVersion != 1) {
        diagnostics.push_back(
            errorAt("schema.version", "<design>", "unsupported schemaVersion"));
        return diagnostics;
    }
    if (document.root.id == 0) {
        diagnostics.push_back(errorAt("schema.root_id", "<design>",
                                      "root node id must be nonzero"));
        return diagnostics;
    }
    std::set<DesignNodeId> ids;
    validateNode(document.root, "root", true, ids, diagnostics);
    return diagnostics;
}

}  // namespace lumen::dsl
