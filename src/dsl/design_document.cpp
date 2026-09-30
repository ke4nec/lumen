#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_schema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace lumen::dsl {
namespace {

[[nodiscard]] DesignError errorAt(const std::string& code,
                                  const std::string& file,
                                  const std::string& message,
                                  SourcePos pos = {}) {
    return DesignError{code, file, pos, message, {}, {}, 0, {}, {}};
}

class JsonValue {
  public:
    enum class Kind { Null, Boolean, Number, String, Array, Object };

    Kind kind{Kind::Null};
    bool boolean{false};
    double number{0.0};
    std::string string{};
    std::vector<JsonValue> array{};
    std::map<std::string, JsonValue> object{};
};

class JsonParser {
  public:
    JsonParser(const std::string& source, std::string filename)
        : source_(source), filename_(std::move(filename)) {}

    [[nodiscard]] std::optional<JsonValue> parse() {
        skipWhitespace();
        auto value = parseValue();
        if (!value.has_value()) {
            return std::nullopt;
        }
        skipWhitespace();
        if (index_ != source_.size()) {
            fail("codec.trailing_input");
        }
        return error_.has_value() ? std::nullopt : value;
    }

    [[nodiscard]] const std::optional<DesignError>& error() const {
        return error_;
    }

  private:
    void skipWhitespace() {
        while (index_ < source_.size() &&
               (source_[index_] == ' ' || source_[index_] == '\n' ||
                source_[index_] == '\r' || source_[index_] == '\t')) {
            ++index_;
        }
    }

    void fail(const std::string& code, const std::string& message = {}) {
        if (error_.has_value()) {
            return;
        }
        const std::string detail = message.empty() ? code : message;
        error_ = errorAt(code, filename_, detail);
    }

    [[nodiscard]] bool consume(char expected) {
        skipWhitespace();
        if (index_ >= source_.size() || source_[index_] != expected) {
            fail("codec.expected_token",
                 std::string("expected '") + expected + "'");
            return false;
        }
        ++index_;
        return true;
    }

    [[nodiscard]] std::optional<JsonValue> parseValue() {
        skipWhitespace();
        if (index_ >= source_.size()) {
            fail("codec.unexpected_eof", "expected a JSON value");
            return std::nullopt;
        }
        switch (source_[index_]) {
            case 'n':
                return parseLiteral("null", JsonValue::Kind::Null);
            case 't':
                return parseLiteral("true", JsonValue::Kind::Boolean, true);
            case 'f':
                return parseLiteral("false", JsonValue::Kind::Boolean, false);
            case '"': {
                auto string = parseString();
                if (!string.has_value()) return std::nullopt;
                JsonValue value;
                value.kind = JsonValue::Kind::String;
                value.string = std::move(*string);
                return value;
            }
            case '[':
                return parseArray();
            case '{':
                return parseObject();
            default:
                return parseNumber();
        }
    }

    [[nodiscard]] std::optional<JsonValue> parseLiteral(
        const char* literal, JsonValue::Kind kind, bool boolean = false) {
        const std::string text{literal};
        if (source_.compare(index_, text.size(), text) != 0) {
            fail("codec.invalid_literal");
            return std::nullopt;
        }
        index_ += text.size();
        JsonValue value;
        value.kind = kind;
        value.boolean = boolean;
        return value;
    }

    [[nodiscard]] std::optional<std::string> parseString() {
        if (!consume('"')) return std::nullopt;
        std::string result;
        while (index_ < source_.size()) {
            const char c = source_[index_++];
            if (c == '"') return result;
            if (static_cast<unsigned char>(c) < 0x20U) {
                fail("codec.control_character", "control character in string");
                return std::nullopt;
            }
            if (c != '\\') {
                result += c;
                continue;
            }
            if (index_ >= source_.size()) break;
            const char escaped = source_[index_++];
            switch (escaped) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    if (index_ + 4 > source_.size()) {
                        fail("codec.escape", "short Unicode escape");
                        return std::nullopt;
                    }
                    unsigned int codePoint = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char digit = source_[index_++];
                        codePoint <<= 4;
                        if (digit >= '0' && digit <= '9') {
                            codePoint += static_cast<unsigned int>(digit - '0');
                        } else if (digit >= 'a' && digit <= 'f') {
                            codePoint += static_cast<unsigned int>(digit - 'a' + 10);
                        } else if (digit >= 'A' && digit <= 'F') {
                            codePoint += static_cast<unsigned int>(digit - 'A' + 10);
                        } else {
                            fail("codec.escape", "invalid Unicode escape");
                            return std::nullopt;
                        }
                    }
                    if (codePoint > 0x7fU) {
                        fail("codec.escape",
                             "non-ASCII Unicode escapes are not supported");
                        return std::nullopt;
                    }
                    result += static_cast<char>(codePoint);
                    break;
                }
                default:
                    fail("codec.escape", "unsupported JSON escape");
                    return std::nullopt;
            }
        }
        fail("codec.unterminated_string");
        return std::nullopt;
    }

    [[nodiscard]] std::optional<JsonValue> parseNumber() {
        const std::size_t begin = index_;
        if (index_ < source_.size() && source_[index_] == '-') ++index_;
        while (index_ < source_.size() && source_[index_] >= '0' &&
               source_[index_] <= '9') ++index_;
        if (index_ < source_.size() && source_[index_] == '.') {
            ++index_;
            while (index_ < source_.size() && source_[index_] >= '0' &&
                   source_[index_] <= '9') ++index_;
        }
        if (index_ < source_.size() &&
            (source_[index_] == 'e' || source_[index_] == 'E')) {
            ++index_;
            if (index_ < source_.size() &&
                (source_[index_] == '+' || source_[index_] == '-')) ++index_;
            while (index_ < source_.size() && source_[index_] >= '0' &&
                   source_[index_] <= '9') ++index_;
        }
        if (begin == index_) {
            fail("codec.invalid_number");
            return std::nullopt;
        }
        const std::string text = source_.substr(begin, index_ - begin);
        char* end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size() || !std::isfinite(value)) {
            fail("codec.invalid_number");
            return std::nullopt;
        }
        JsonValue result;
        result.kind = JsonValue::Kind::Number;
        result.number = value;
        return result;
    }

    [[nodiscard]] std::optional<JsonValue> parseArray() {
        if (!consume('[')) return std::nullopt;
        JsonValue result;
        result.kind = JsonValue::Kind::Array;
        skipWhitespace();
        if (index_ < source_.size() && source_[index_] == ']') {
            ++index_;
            return result;
        }
        for (;;) {
            auto value = parseValue();
            if (!value.has_value()) return std::nullopt;
            result.array.push_back(std::move(*value));
            skipWhitespace();
            if (index_ < source_.size() && source_[index_] == ']') {
                ++index_;
                return result;
            }
            if (!consume(',')) return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<JsonValue> parseObject() {
        if (!consume('{')) return std::nullopt;
        JsonValue result;
        result.kind = JsonValue::Kind::Object;
        skipWhitespace();
        if (index_ < source_.size() && source_[index_] == '}') {
            ++index_;
            return result;
        }
        for (;;) {
            auto key = parseString();
            if (!key.has_value()) return std::nullopt;
            if (!consume(':')) return std::nullopt;
            auto value = parseValue();
            if (!value.has_value()) return std::nullopt;
            result.object[std::move(*key)] = std::move(*value);
            skipWhitespace();
            if (index_ < source_.size() && source_[index_] == '}') {
                ++index_;
                return result;
            }
            if (!consume(',')) return std::nullopt;
        }
    }

    const std::string& source_;
    std::string filename_;
    std::size_t index_{0};
    std::optional<DesignError> error_{};
};

[[nodiscard]] const JsonValue* member(const JsonValue& object,
                                      const std::string& name) {
    if (object.kind != JsonValue::Kind::Object) return nullptr;
    const auto found = object.object.find(name);
    return found == object.object.end() ? nullptr : &found->second;
}

[[nodiscard]] bool stringValue(const JsonValue* value, std::string& out) {
    if (value == nullptr || value->kind != JsonValue::Kind::String) return false;
    out = value->string;
    return true;
}

[[nodiscard]] bool numberValue(const JsonValue* value, double& out) {
    if (value == nullptr || value->kind != JsonValue::Kind::Number) return false;
    out = value->number;
    return true;
}

[[nodiscard]] std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20U) {
                    out << "\\u00" << std::hex << std::setw(2)
                        << std::setfill('0') << static_cast<int>(c)
                        << std::dec << std::setfill(' ');
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
    return out.str();
}

[[nodiscard]] std::string jsonNumber(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

[[nodiscard]] std::string jsonValue(const JsonValue& value);

[[nodiscard]] std::string jsonValue(const JsonValue& value) {
    switch (value.kind) {
        case JsonValue::Kind::Null: return "null";
        case JsonValue::Kind::Boolean: return value.boolean ? "true" : "false";
        case JsonValue::Kind::Number: return jsonNumber(value.number);
        case JsonValue::Kind::String: return jsonEscape(value.string);
        case JsonValue::Kind::Array: {
            std::string out = "[";
            for (std::size_t i = 0; i < value.array.size(); ++i) {
                if (i != 0) out += ',';
                out += jsonValue(value.array[i]);
            }
            return out + ']';
        }
        case JsonValue::Kind::Object: {
            std::string out = "{";
            bool first = true;
            for (const auto& [key, child] : value.object) {
                if (!first) out += ',';
                first = false;
                out += jsonEscape(key) + ':' + jsonValue(child);
            }
            return out + '}';
        }
    }
    return "null";
}

[[nodiscard]] JsonValue jsonString(std::string value) {
    JsonValue result;
    result.kind = JsonValue::Kind::String;
    result.string = std::move(value);
    return result;
}

[[nodiscard]] JsonValue jsonRaw(std::string_view raw) {
    const std::string source{raw};
    JsonParser parser(source, "<unknown-field>");
    if (auto value = parser.parse(); value.has_value()) {
        return std::move(*value);
    }
    return jsonString(source);
}

[[nodiscard]] JsonValue jsonNumberValue(double value) {
    JsonValue result;
    result.kind = JsonValue::Kind::Number;
    result.number = value;
    return result;
}

[[nodiscard]] JsonValue jsonObject(std::map<std::string, JsonValue> value) {
    JsonValue result;
    result.kind = JsonValue::Kind::Object;
    result.object = std::move(value);
    return result;
}

[[nodiscard]] JsonValue jsonArray(std::vector<JsonValue> value) {
    JsonValue result;
    result.kind = JsonValue::Kind::Array;
    result.array = std::move(value);
    return result;
}

[[nodiscard]] JsonValue encodeDesignValue(const DesignValue& value) {
    std::map<std::string, JsonValue> object;
    if (std::holds_alternative<std::monostate>(value.value)) {
        object["kind"] = jsonString("null");
    } else if (const auto* boolean = std::get_if<bool>(&value.value)) {
        object["kind"] = jsonString("bool");
        object["value"] = JsonValue{JsonValue::Kind::Boolean, *boolean};
    } else if (const auto* number = std::get_if<double>(&value.value)) {
        object["kind"] = jsonString("number");
        object["value"] = jsonNumberValue(*number);
    } else if (const auto* string = std::get_if<std::string>(&value.value)) {
        object["kind"] = jsonString("string");
        object["value"] = jsonString(*string);
    } else if (const auto* color = std::get_if<core::Color>(&value.value)) {
        object["kind"] = jsonString("color");
        object["a"] = jsonNumberValue(color->a);
        object["b"] = jsonNumberValue(color->b);
        object["g"] = jsonNumberValue(color->g);
        object["r"] = jsonNumberValue(color->r);
    } else if (const auto* enumeration = std::get_if<DesignEnum>(&value.value)) {
        object["kind"] = jsonString("enum");
        object["domain"] = jsonString(enumeration->domain);
        object["value"] = jsonString(enumeration->value);
    }
    return jsonObject(std::move(object));
}

[[nodiscard]] std::optional<DesignValue> decodeDesignValue(
    const JsonValue& value, const std::string& file,
    std::optional<DesignError>& error) {
    const std::string* kind = nullptr;
    std::string kindStorage;
    if (!stringValue(member(value, "kind"), kindStorage)) {
        error = errorAt("codec.value_kind", file, "design value kind is missing");
        return std::nullopt;
    }
    kind = &kindStorage;
    if (*kind == "null") return DesignValue{};
    if (*kind == "bool") {
        const auto* raw = member(value, "value");
        if (raw == nullptr || raw->kind != JsonValue::Kind::Boolean) {
            error = errorAt("codec.value_type", file, "boolean value expected");
            return std::nullopt;
        }
        return DesignValue{DesignValue::Variant{raw->boolean}};
    }
    if (*kind == "number") {
        double number = 0.0;
        if (!numberValue(member(value, "value"), number)) {
            error = errorAt("codec.value_type", file, "number value expected");
            return std::nullopt;
        }
        return DesignValue{DesignValue::Variant{number}};
    }
    if (*kind == "string") {
        std::string string;
        if (!stringValue(member(value, "value"), string)) {
            error = errorAt("codec.value_type", file, "string value expected");
            return std::nullopt;
        }
        return DesignValue{DesignValue::Variant{std::move(string)}};
    }
    if (*kind == "color") {
        double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
        if (!numberValue(member(value, "r"), r) ||
            !numberValue(member(value, "g"), g) ||
            !numberValue(member(value, "b"), b) ||
            !numberValue(member(value, "a"), a) || r < 0 || r > 255 ||
            g < 0 || g > 255 || b < 0 || b > 255 || a < 0 || a > 255) {
            error = errorAt("codec.value_type", file, "valid color expected");
            return std::nullopt;
        }
        if (std::floor(r) != r || std::floor(g) != g ||
            std::floor(b) != b || std::floor(a) != a) {
            error = errorAt("codec.value_type", file,
                            "color components must be integers");
            return std::nullopt;
        }
        return DesignValue{DesignValue::Variant{core::Color::fromRGBA(
            static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
            static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a))}};
    }
    if (*kind == "enum") {
        std::string domain;
        std::string enumeration;
        if (!stringValue(member(value, "domain"), domain) ||
            !stringValue(member(value, "value"), enumeration)) {
            error = errorAt("codec.value_type", file, "enum value expected");
            return std::nullopt;
        }
        return DesignValue{DesignValue::Variant{
            DesignEnum{std::move(domain), std::move(enumeration)}}};
    }
    error = errorAt("codec.value_kind", file, "unknown design value kind");
    return std::nullopt;
}

[[nodiscard]] JsonValue encodeNode(const DesignNode& node) {
    std::map<std::string, JsonValue> object;
    object["children"] = jsonArray([&] {
        std::vector<JsonValue> values;
        values.reserve(node.children.size());
        for (const auto& child : node.children) values.push_back(encodeNode(child));
        return values;
    }());
    object["id"] = jsonString(std::to_string(node.id));
    object["properties"] = jsonObject([&] {
        std::map<std::string, JsonValue> values;
        for (const auto& [name, value] : node.properties) {
            values[name] = encodeDesignValue(value);
        }
        return values;
    }());
    object["propertySources"] = jsonObject([&] {
        std::map<std::string, JsonValue> values;
        for (const auto& [name, span] : node.propertySources) {
            values[name] = jsonObject({
                {"begin", jsonObject({
                    {"column", jsonNumberValue(span.begin.column)},
                    {"line", jsonNumberValue(span.begin.line)}})},
                {"end", jsonObject({
                    {"column", jsonNumberValue(span.end.column)},
                    {"line", jsonNumberValue(span.end.line)}})}});
        }
        return values;
    }());
    object["references"] = jsonObject([&] {
        std::map<std::string, JsonValue> values;
        for (const auto& [name, value] : node.references) {
            values[name] = jsonString(value);
        }
        return values;
    }());
    object["slots"] = jsonObject([&] {
        std::map<std::string, JsonValue> values;
        for (const auto& [name, children] : node.slots) {
            std::vector<JsonValue> encoded;
            for (const auto& child : children) encoded.push_back(encodeNode(child));
            values[name] = jsonArray(std::move(encoded));
        }
        return values;
    }());
    object["type"] = jsonString(node.type);
    object["unknownFields"] = jsonObject([&] {
        std::map<std::string, JsonValue> values;
        for (const auto& [name, value] : node.unknownFields) {
            values[name] = jsonRaw(value);
        }
        return values;
    }());
    if (node.source.has_value()) {
        const auto span = *node.source;
        object["source"] = jsonObject({
            {"begin", jsonObject({
                {"column", jsonNumberValue(span.begin.column)},
                {"line", jsonNumberValue(span.begin.line)}})},
            {"end", jsonObject({
                {"column", jsonNumberValue(span.end.column)},
                {"line", jsonNumberValue(span.end.line)}})}});
    }
    return jsonObject(std::move(object));
}

[[nodiscard]] bool integerInRange(double number, std::uint64_t& out) {
    if (!std::isfinite(number) || number < 0 ||
        number > static_cast<double>(std::numeric_limits<std::uint64_t>::max()) ||
        std::floor(number) != number) {
        return false;
    }
    out = static_cast<std::uint64_t>(number);
    return true;
}

[[nodiscard]] bool readStringMap(const JsonValue* value,
                                 std::map<std::string, std::string>& out) {
    if (value == nullptr || value->kind != JsonValue::Kind::Object) return false;
    for (const auto& [name, child] : value->object) {
        if (child.kind != JsonValue::Kind::String) return false;
        out[name] = child.string;
    }
    return true;
}

[[nodiscard]] bool readRawMap(const JsonValue* value,
                              std::map<std::string, std::string>& out) {
    if (value == nullptr || value->kind != JsonValue::Kind::Object) return false;
    for (const auto& [name, child] : value->object) {
        out[name] = jsonValue(child);
    }
    return true;
}

[[nodiscard]] std::optional<DesignNode> decodeNode(
    const JsonValue& value, const std::string& file,
    std::optional<DesignError>& error) {
    if (value.kind != JsonValue::Kind::Object) {
        error = errorAt("codec.node_type", file, "node object expected");
        return std::nullopt;
    }
    std::string type;
    std::string idText;
    if (!stringValue(member(value, "type"), type) ||
        !stringValue(member(value, "id"), idText)) {
        error = errorAt("codec.node_fields", file, "node id and type required");
        return std::nullopt;
    }
    char* idEnd = nullptr;
    errno = 0;
    const std::uint64_t id = std::strtoull(idText.c_str(), &idEnd, 10);
    if (idText.empty() || errno == ERANGE ||
        idEnd != idText.c_str() + idText.size() || id == 0) {
        error = errorAt("codec.node_id", file, "node id must be a nonzero string");
        return std::nullopt;
    }
    DesignNode node;
    node.id = id;
    node.type = std::move(type);
    const auto* properties = member(value, "properties");
    if (properties == nullptr || properties->kind != JsonValue::Kind::Object) {
        error = errorAt("codec.node_fields", file, "node properties required");
        return std::nullopt;
    }
    for (const auto& [name, raw] : properties->object) {
        auto decoded = decodeDesignValue(raw, file, error);
        if (!decoded.has_value()) return std::nullopt;
        node.properties[name] = std::move(*decoded);
    }
    if (const auto* propertySources = member(value, "propertySources");
        propertySources != nullptr) {
        if (propertySources->kind != JsonValue::Kind::Object) {
            error = errorAt("codec.property_sources", file,
                            "property source map object expected");
            return std::nullopt;
        }
        for (const auto& [name, rawSpan] : propertySources->object) {
            const auto* begin = member(rawSpan, "begin");
            const auto* end = member(rawSpan, "end");
            double beginLine = 0.0;
            double beginColumn = 0.0;
            double endLine = 0.0;
            double endColumn = 0.0;
            if (begin == nullptr || end == nullptr ||
                !numberValue(member(*begin, "line"), beginLine) ||
                !numberValue(member(*begin, "column"), beginColumn) ||
                !numberValue(member(*end, "line"), endLine) ||
                !numberValue(member(*end, "column"), endColumn) ||
                beginLine < 1.0 || beginColumn < 1.0 || endLine < 1.0 ||
                endColumn < 1.0 || std::floor(beginLine) != beginLine ||
                std::floor(beginColumn) != beginColumn ||
                std::floor(endLine) != endLine ||
                std::floor(endColumn) != endColumn || endLine < beginLine ||
                (endLine == beginLine && endColumn < beginColumn)) {
                error = errorAt("codec.property_sources", file,
                                "invalid property source span");
                return std::nullopt;
            }
            node.propertySources[name] = DesignSourceSpan{
                SourcePos{static_cast<std::size_t>(beginLine),
                          static_cast<std::size_t>(beginColumn)},
                SourcePos{static_cast<std::size_t>(endLine),
                          static_cast<std::size_t>(endColumn)}};
        }
    }
    if (const auto* references = member(value, "references");
        references != nullptr && !readStringMap(references, node.references)) {
        error = errorAt("codec.references", file, "reference values must be strings");
        return std::nullopt;
    }
    if (const auto* unknown = member(value, "unknownFields");
        unknown != nullptr && !readRawMap(unknown, node.unknownFields)) {
        error = errorAt("codec.unknown_fields", file,
                        "unknown fields must be a JSON object");
        return std::nullopt;
    }
    if (const auto* children = member(value, "children");
        children != nullptr) {
        if (children->kind != JsonValue::Kind::Array) {
            error = errorAt("codec.children", file, "children array expected");
            return std::nullopt;
        }
        for (const auto& raw : children->array) {
            auto child = decodeNode(raw, file, error);
            if (!child.has_value()) return std::nullopt;
            node.children.push_back(std::move(*child));
        }
    }
    if (const auto* slots = member(value, "slots"); slots != nullptr) {
        if (slots->kind != JsonValue::Kind::Object) {
            error = errorAt("codec.slots", file, "slots object expected");
            return std::nullopt;
        }
        for (const auto& [name, rawChildren] : slots->object) {
            if (rawChildren.kind != JsonValue::Kind::Array) {
                error = errorAt("codec.slots", file, "slot children array expected");
                return std::nullopt;
            }
            auto& destination = node.slots[name];
            for (const auto& raw : rawChildren.array) {
                auto child = decodeNode(raw, file, error);
                if (!child.has_value()) return std::nullopt;
                destination.push_back(std::move(*child));
            }
        }
    }
    if (const auto* source = member(value, "source"); source != nullptr) {
        const auto* begin = member(*source, "begin");
        const auto* end = member(*source, "end");
        double beginLine = 0, beginColumn = 0, endLine = 0, endColumn = 0;
        if (begin == nullptr || end == nullptr ||
            !numberValue(member(*begin, "line"), beginLine) ||
            !numberValue(member(*begin, "column"), beginColumn) ||
            !numberValue(member(*end, "line"), endLine) ||
            !numberValue(member(*end, "column"), endColumn) ||
            beginLine < 1 || beginColumn < 1 || endLine < 1 || endColumn < 1 ||
            std::floor(beginLine) != beginLine ||
            std::floor(beginColumn) != beginColumn ||
            std::floor(endLine) != endLine || std::floor(endColumn) != endColumn ||
            endLine < beginLine ||
            (endLine == beginLine && endColumn < beginColumn)) {
            error = errorAt("codec.source_span", file, "invalid source span");
            return std::nullopt;
        }
        node.source = DesignSourceSpan{
            SourcePos{static_cast<std::size_t>(beginLine),
                      static_cast<std::size_t>(beginColumn)},
            SourcePos{static_cast<std::size_t>(endLine),
                      static_cast<std::size_t>(endColumn)}};
    }
    static const std::set<std::string> knownFields = {
        "children", "id", "properties", "propertySources", "references",
        "slots", "source", "type", "unknownFields"};
    for (const auto& [name, raw] : value.object) {
        if (knownFields.find(name) == knownFields.end()) {
            node.unknownFields[name] = jsonValue(raw);
        }
    }
    return node;
}

[[nodiscard]] bool validateNodeIds(const DesignNode& node,
                                   std::set<DesignNodeId>& ids) {
    if (node.id == 0 || !ids.insert(node.id).second) return false;
    for (const auto& child : node.children) {
        if (!validateNodeIds(child, ids)) return false;
    }
    for (const auto& [name, children] : node.slots) {
        (void)name;
        for (const auto& child : children) {
            if (!validateNodeIds(child, ids)) return false;
        }
    }
    return true;
}

[[nodiscard]] bool isL0Node(const std::string& type) {
    return type == "Container" || type == "Row" || type == "Column" ||
           type == "Stack" || type == "Text" || type == "Button" ||
           type == "TextField" || type == "ScrollView" ||
           type == "ListView" || type == "Checkbox" || type == "Switch" ||
           type == "FocusScope";
}

[[nodiscard]] bool validIdentifier(const std::string& value) {
    if (value.empty()) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (!(std::isalpha(first) != 0 || value.front() == '_')) return false;
    for (const char character : value) {
        const auto c = static_cast<unsigned char>(character);
        if (!(std::isalnum(c) != 0 || character == '_')) return false;
    }
    return true;
}

[[nodiscard]] std::string dslEscape(const std::string& value) {
    std::string result = "\"";
    for (const char c : value) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result + '"';
}

[[nodiscard]] std::string dslColor(core::Color color) {
    std::ostringstream out;
    out << '#' << std::hex << std::setfill('0') << std::setw(2)
        << static_cast<int>(color.r) << std::setw(2)
        << static_cast<int>(color.g) << std::setw(2)
        << static_cast<int>(color.b);
    if (color.a != 255) out << std::setw(2) << static_cast<int>(color.a);
    return out.str();
}

[[nodiscard]] std::optional<std::string> dslLiteral(
    const DesignValue& value, const std::string& property,
    std::optional<DesignError>& error) {
    if (const auto* boolean = std::get_if<bool>(&value.value)) {
        return *boolean ? "true" : "false";
    }
    if (const auto* number = std::get_if<double>(&value.value)) {
        if (!std::isfinite(*number)) {
            error = errorAt("compile.invalid_number", "<design>",
                            "property number must be finite");
            return std::nullopt;
        }
        return jsonNumber(*number);
    }
    if (const auto* string = std::get_if<std::string>(&value.value)) {
        return dslEscape(*string);
    }
    if (const auto* color = std::get_if<core::Color>(&value.value)) {
        return dslColor(*color);
    }
    if (const auto* enumeration = std::get_if<DesignEnum>(&value.value)) {
        if (!validIdentifier(enumeration->value)) {
            error = errorAt("compile.invalid_enum", "<design>",
                            "enum value must be an identifier");
            return std::nullopt;
        }
        return enumeration->value;
    }
    error = errorAt("compile.invalid_value", "<design>",
                    "property '" + property + "' has no value");
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> emitNode(
    const DesignNode& node, std::set<DesignNodeId>& ids, std::string path,
    std::optional<DesignError>& error) {
    if (!isL0Node(node.type)) {
        DesignError diagnostic = errorAt(
            "compile.unknown_node", "<design>",
            "node type '" + node.type + "' is not registered in P1");
        diagnostic.nodeId = node.id;
        diagnostic.nodePath = std::move(path);
        error = std::move(diagnostic);
        return std::nullopt;
    }
    if (node.id == 0 || !ids.insert(node.id).second) {
        DesignError diagnostic = errorAt(
            "compile.duplicate_node_id", "<design>",
            "node id must be unique and nonzero");
        diagnostic.nodeId = node.id;
        diagnostic.nodePath = std::move(path);
        error = std::move(diagnostic);
        return std::nullopt;
    }
    std::string output = node.type + "(";
    bool first = true;
    for (const auto& [name, value] : node.properties) {
        if (name.empty()) continue;
        const auto literal = dslLiteral(value, name, error);
        if (!literal.has_value()) return std::nullopt;
        if (!first) output += ", ";
        first = false;
        output += name + ": ";
        if (name == "bind" || name == "onClick") {
            const auto* string = std::get_if<std::string>(&value.value);
            if (string == nullptr || !validIdentifier(*string)) {
                DesignError diagnostic = errorAt(
                    "compile.reference_name", "<design>",
                    "reference names must be identifiers");
                diagnostic.nodeId = node.id;
                diagnostic.nodePath = path;
                diagnostic.property = name;
                error = std::move(diagnostic);
                return std::nullopt;
            }
            output += *string;
        } else {
            output += *literal;
        }
    }
    for (const auto& [name, reference] : node.references) {
        if (name != "bind" && name != "onClick") continue;
        if (!validIdentifier(reference)) {
            DesignError diagnostic = errorAt(
                "compile.reference_name", "<design>",
                "reference names must be identifiers");
            diagnostic.nodeId = node.id;
            diagnostic.nodePath = path;
            diagnostic.property = name;
            error = std::move(diagnostic);
            return std::nullopt;
        }
        if (!first) output += ", ";
        first = false;
        output += name + ": " + reference;
    }
    output += ")";
    if (!node.children.empty()) {
        output += " {";
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            const auto child = emitNode(node.children[i], ids,
                                        path + ".children[" + std::to_string(i) + "]",
                                        error);
            if (!child.has_value()) return std::nullopt;
            output += ' ' + *child;
        }
        output += " }";
    }
    if (!node.slots.empty()) {
        DesignError diagnostic = errorAt(
            "compile.unsupported_slots", "<design>",
            "named slots require a component schema and are not supported in P1");
        diagnostic.nodeId = node.id;
        diagnostic.nodePath = path;
        error = std::move(diagnostic);
        return std::nullopt;
    }
    return output;
}

void fillTrace(const DesignNode& node, const std::string& path,
               CompileTrace& trace) {
    std::string key;
    if (const auto found = node.properties.find("key");
        found != node.properties.end()) {
        if (const auto* value = std::get_if<std::string>(&found->second.value)) {
            key = *value;
        }
    }
    trace.nodes[node.id] = CompiledNodeRef{
        node.id, key.empty() ? "i:" + path : "k:" + key, key, false};
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        fillTrace(node.children[i], path + ".children[" + std::to_string(i) + "]",
                  trace);
    }
}

[[nodiscard]] std::optional<DesignReferenceKind> referenceKind(
    const std::string& name) {
    if (name == "bind") return DesignReferenceKind::Binding;
    if (name == "onClick") return DesignReferenceKind::Handler;
    if (name == "theme") return DesignReferenceKind::Theme;
    if (name == "image") return DesignReferenceKind::Image;
    if (name == "virtualSource") return DesignReferenceKind::VirtualSource;
    if (name == "splitterSource") return DesignReferenceKind::SplitterSource;
    if (name == "component") return DesignReferenceKind::Component;
    return std::nullopt;
}

void validateRuntimeReferences(
    const DesignNode& node, const std::string& path,
    const DesignRuntimeContext& context, DesignRuntimeSession& session,
    std::set<DesignNodeId>& unresolved,
    std::vector<DesignError>& diagnostics) {
    for (const auto& [name, reference] : node.references) {
        const auto kind = referenceKind(name);
        if (!kind.has_value()) {
            unresolved.insert(node.id);
            DesignError diagnostic = errorAt(
                "compile.reference_kind", "<design>",
                "reference kind '" + name + "' is not registered");
            diagnostic.nodeId = node.id;
            diagnostic.nodePath = path;
            diagnostic.property = name;
            diagnostics.push_back(std::move(diagnostic));
        } else if (*kind != DesignReferenceKind::Binding &&
                   *kind != DesignReferenceKind::Handler) {
            unresolved.insert(node.id);
            DesignError diagnostic = errorAt(
                "reference.unsupported", "<design>",
                "reference kind '" + name + "' needs a component schema");
            diagnostic.nodeId = node.id;
            diagnostic.nodePath = path;
            diagnostic.property = name;
            diagnostics.push_back(std::move(diagnostic));
        } else if (context.validatesReferences()) {
            DesignReference resolved;
            if (!context.resolveReference(*kind, reference, resolved)) {
                unresolved.insert(node.id);
                DesignError diagnostic = errorAt(
                    "reference.missing", "<design>",
                    "reference '" + reference + "' was not found");
                diagnostic.nodeId = node.id;
                diagnostic.nodePath = path;
                diagnostic.property = name;
                diagnostics.push_back(std::move(diagnostic));
            } else if (resolved.kind != *kind || resolved.stableName != reference) {
                unresolved.insert(node.id);
                DesignError diagnostic = errorAt(
                    "reference.type", "<design>",
                    "reference resolver returned the wrong typed handle");
                diagnostic.nodeId = node.id;
                diagnostic.nodePath = path;
                diagnostic.property = name;
                diagnostics.push_back(std::move(diagnostic));
            } else {
                session.retain(std::move(resolved.lifetimeToken));
            }
        }
    }
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        validateRuntimeReferences(
            node.children[i], path + ".children[" + std::to_string(i) + "]",
            context, session, unresolved, diagnostics);
    }
}

void disableUnresolvedReferenceNode(const DesignNode& node, core::Widget& widget,
                                   const std::set<DesignNodeId>& unresolved) {
    if (unresolved.contains(node.id)) {
        // Keep the node type and geometry inspectable, but prevent a failed
        // reference from reaching application handlers or state bindings.
        widget.enabled = false;
        widget.invalid = true;
        widget.bind.clear();
        widget.onClick.clear();
    }
    const std::size_t count =
        std::min(node.children.size(), widget.children.size());
    for (std::size_t i = 0; i < count; ++i) {
        disableUnresolvedReferenceNode(node.children[i], widget.children[i],
                                       unresolved);
    }
}

}  // namespace

std::string DesignError::format() const {
    std::ostringstream out;
    out << (file.empty() ? "<design>" : file) << ':' << pos.line << ':'
        << pos.column << ": " << (code.empty() ? "design.error" : code)
        << ": " << message;
    if (!property.empty()) out << " [property " << property << ']';
    if (nodeId != 0) out << " [node " << nodeId << ']';
    if (!expected.empty() || !found.empty()) {
        out << " (expected " << (expected.empty() ? "?" : expected)
            << ", found " << (found.empty() ? "?" : found) << ')';
    }
    return out.str();
}

std::string serializeDesignDocument(const DesignDocument& document) {
    std::map<std::string, JsonValue> root;
    root["documentId"] = jsonString(document.documentId);
    root["format"] = jsonString("lumen.design");
    root["pageName"] = jsonString(document.pageName);
    root["root"] = encodeNode(document.root);
    root["schemaVersion"] = jsonNumberValue(document.schemaVersion);
    root["unknownFields"] = jsonObject([&] {
        std::map<std::string, JsonValue> values;
        for (const auto& [name, value] : document.unknownFields) {
            values[name] = jsonRaw(value);
        }
        return values;
    }());
    return jsonValue(jsonObject(std::move(root)));
}

DesignReadResult readDesignDocument(const std::string& source,
                                    std::string filename) {
    JsonParser parser(source, filename);
    const auto value = parser.parse();
    if (!value.has_value()) {
        return DesignReadResult{DesignDocument{}, parser.error()};
    }
    if (value->kind != JsonValue::Kind::Object) {
        return DesignReadResult{
            DesignDocument{}, errorAt("codec.document_type", filename,
                                      "design document object expected")};
    }
    std::string format;
    std::string documentId;
    std::string pageName;
    double schema = 0.0;
    if (!stringValue(member(*value, "format"), format) ||
        format != "lumen.design" ||
        !numberValue(member(*value, "schemaVersion"), schema) ||
        !stringValue(member(*value, "documentId"), documentId) ||
        documentId.empty() || !stringValue(member(*value, "pageName"), pageName)) {
        return DesignReadResult{
            DesignDocument{}, errorAt("codec.document_fields", filename,
                                      "format, schemaVersion, documentId and pageName are required")};
    }
    std::uint64_t schemaInteger = 0;
    if (!integerInRange(schema, schemaInteger) || schemaInteger > 1) {
        return DesignReadResult{
            DesignDocument{}, errorAt("codec.schema_version", filename,
                                      "unsupported design document schemaVersion")};
    }
    std::optional<DesignError> error;
    const auto* root = member(*value, "root");
    if (root == nullptr) {
        return DesignReadResult{
            DesignDocument{}, errorAt("codec.root", filename, "root is required")};
    }
    auto decodedRoot = decodeNode(*root, filename, error);
    if (!decodedRoot.has_value()) return DesignReadResult{DesignDocument{}, error};
    std::set<DesignNodeId> ids;
    if (!validateNodeIds(*decodedRoot, ids)) {
        return DesignReadResult{
            DesignDocument{}, errorAt("codec.duplicate_node_id", filename,
                                      "node ids must be unique and nonzero")};
    }
    DesignDocument document;
    document.schemaVersion = static_cast<std::uint32_t>(schemaInteger);
    document.documentId = std::move(documentId);
    document.pageName = std::move(pageName);
    document.root = std::move(*decodedRoot);
    if (const auto* unknown = member(*value, "unknownFields");
        unknown != nullptr && !readRawMap(unknown, document.unknownFields)) {
        return DesignReadResult{
            DesignDocument{}, errorAt("codec.unknown_fields", filename,
                                      "unknown fields must be a JSON object")};
    }
    static const std::set<std::string> knownFields = {
        "documentId", "format", "pageName", "root", "schemaVersion",
        "unknownFields"};
    for (const auto& [name, raw] : value->object) {
        if (knownFields.find(name) == knownFields.end()) {
            document.unknownFields[name] = jsonValue(raw);
        }
    }
    return DesignReadResult{std::move(document), std::nullopt};
}

DesignCompileResult compileDesignDocument(const DesignDocument& document,
                                          DesignRuntimeContext& context) {
    DesignCompileResult result;
    result.sourceMap = DesignSourceMap::fromDocument(document);
    result.session = std::make_shared<DesignRuntimeSession>();
    if (document.schemaVersion != 1) {
        result.diagnostics.push_back(errorAt(
            "compile.schema_version", "<design>",
            "unsupported design document schemaVersion"));
        return result;
    }
    if (document.root.type.empty()) {
        result.diagnostics.push_back(
            errorAt("compile.root", "<design>", "root node type is required"));
        return result;
    }
    for (auto diagnostic : validateDesignDocument(document)) {
        if (diagnostic.code == "schema.unknown_node") {
            diagnostic.code = "compile.unknown_node";
        } else if (diagnostic.code == "schema.unknown_reference" ||
                   diagnostic.code == "schema.invalid_reference") {
            diagnostic.code = "compile.reference_kind";
        }
        result.diagnostics.push_back(std::move(diagnostic));
    }
    if (!result.diagnostics.empty()) return result;
    std::set<DesignNodeId> unresolvedReferences;
    validateRuntimeReferences(document.root, "root", context, *result.session,
                              unresolvedReferences, result.diagnostics);
    std::set<DesignNodeId> ids;
    std::optional<DesignError> error;
    const auto emitted = emitNode(document.root, ids, "root", error);
    if (!emitted.has_value()) {
        result.diagnostics.push_back(std::move(*error));
        return result;
    }
    const std::string page = validIdentifier(document.pageName)
                                 ? document.pageName
                                 : "DesignPreview";
    const auto parsed = parseLumen("page " + page + " { " + *emitted + " }");
    if (!parsed.ok()) {
        DesignError diagnostic{parsed.error->message == "" ? "compile.dsl" :
                                   "compile.dsl",
                               parsed.error->file,
                               parsed.error->pos,
                               parsed.error->message,
                               parsed.error->expected,
                               parsed.error->found,
                               0,
                               {},
                               {}};
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    result.root = parsed.root;
    disableUnresolvedReferenceNode(document.root, result.root,
                                   unresolvedReferences);
    fillTrace(document.root, "root", result.trace);
    return result;
}

DesignCompileResult compileDesignDocument(const DesignDocument& document) {
    DesignRuntimeContext context;
    return compileDesignDocument(document, context);
}

}  // namespace lumen::dsl
