#include "lumen/dsl/text_dsl.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace lumen::dsl {
namespace {

using core::Color;
using core::CornerRadius;
using core::CrossAxisAlignment;
using core::EdgeInsets;
using core::MainAxisAlignment;
using core::Offset;
using core::StackAlignment;
using core::TextOverflow;
using core::TextStyle;
using core::Widget;
using core::WidgetType;

// --- Lexer ---------------------------------------------------------------

enum class Tok {
    End,
    Ident,
    Number,
    String,
    Color,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Colon,
    Comma,
};

struct Token {
    Tok type{Tok::End};
    std::string text{};  // identifier, decoded string, or raw symbol
    double number{0.0};
    Color color{};
    SourcePos pos{};
};

[[nodiscard]] std::string describe(Tok type) {
    switch (type) {
        case Tok::End:
            return "end of file";
        case Tok::Ident:
            return "identifier";
        case Tok::Number:
            return "number";
        case Tok::String:
            return "string";
        case Tok::Color:
            return "color";
        case Tok::LParen:
            return "'('";
        case Tok::RParen:
            return "')'";
        case Tok::LBrace:
            return "'{'";
        case Tok::RBrace:
            return "'}'";
        case Tok::Colon:
            return "':'";
        case Tok::Comma:
            return "','";
    }
    return "token";
}

[[nodiscard]] std::string describeToken(const Token& token) {
    if (token.type == Tok::End) {
        return "end of file";
    }
    if (token.type == Tok::String) {
        return "\"" + token.text + "\"";
    }
    return "'" + token.text + "'";
}

[[nodiscard]] bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] bool isIdentPart(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] bool isHexDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

[[nodiscard]] std::uint8_t hexByte(const std::string& hex, std::size_t at) {
    return static_cast<std::uint8_t>(
        std::stoi(hex.substr(at, 2), nullptr, 16));
}

class Lexer {
  public:
    Lexer(const std::string& source, std::string file)
        : source_(source), file_(std::move(file)) {}

    // Lexes the whole document; stops at the first error.
    [[nodiscard]] std::optional<DslError> run(std::vector<Token>& out) {
        for (;;) {
            Token token = next();
            // Error tokens default to End, so check the error first —
            // otherwise a lexer failure would masquerade as clean EOF.
            if (error_.has_value()) {
                return error_;
            }
            if (token.type == Tok::End) {
                out.push_back(token);
                return std::nullopt;
            }
            out.push_back(std::move(token));
        }
    }

  private:
    [[nodiscard]] char peek(std::size_t ahead = 0) const {
        return index_ + ahead < source_.size() ? source_[index_ + ahead] : '\0';
    }

    void advance() {
        if (index_ < source_.size()) {
            const char c = source_[index_];
            if (c == '\n') {
                ++line_;
                column_ = 1;
            } else if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
                // UTF-8 continuation bytes share their column with the code
                // point's lead byte so columns count characters, not bytes.
                ++column_;
            }
            ++index_;
        }
    }

    void skipTrivia() {
        for (;;) {
            while (peek() != '\0' &&
                   std::isspace(static_cast<unsigned char>(peek())) != 0) {
                advance();
            }
            if (peek() == '/' && peek(1) == '/') {
                while (peek() != '\0' && peek() != '\n') {
                    advance();
                }
                continue;
            }
            return;
        }
    }

    [[nodiscard]] Token error(const std::string& message,
                              const std::string& expected,
                              const std::string& found) {
        error_ = DslError{file_, SourcePos{line_, column_}, message, expected,
                          found};
        return Token{};
    }

    Token next() {
        skipTrivia();
        Token token;
        token.pos = SourcePos{line_, column_};
        const char c = peek();
        if (c == '\0') {
            token.type = Tok::End;
            return token;
        }
        switch (c) {
            case '(':
                return symbol(Tok::LParen);
            case ')':
                return symbol(Tok::RParen);
            case '{':
                return symbol(Tok::LBrace);
            case '}':
                return symbol(Tok::RBrace);
            case ':':
                return symbol(Tok::Colon);
            case ',':
                return symbol(Tok::Comma);
            case '"':
                return lexString(token);
            case '#':
                return lexColor(token);
            default:
                break;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
            ((c == '+' || c == '-') &&
             std::isdigit(static_cast<unsigned char>(peek(1))) != 0)) {
            return lexNumber(token);
        }
        if (isIdentStart(c)) {
            token.type = Tok::Ident;
            while (isIdentPart(peek())) {
                token.text += peek();
                advance();
            }
            return token;
        }
        return error(std::string("unexpected character '") + c + "'",
                     "token", std::string("'") + c + "'");
    }

    [[nodiscard]] Token symbol(Tok type) {
        Token token;
        token.pos = SourcePos{line_, column_};
        token.type = type;
        token.text = std::string(1, peek());
        advance();
        return token;
    }

    Token lexString(Token token) {
        advance();  // opening quote
        while (peek() != '\0' && peek() != '"' && peek() != '\n') {
            if (peek() == '\\' && peek(1) != '\0') {
                advance();  // the backslash
                switch (peek()) {
                    case 'n':
                        token.text += '\n';
                        break;
                    case 't':
                        token.text += '\t';
                        break;
                    case 'r':
                        token.text += '\r';
                        break;
                    case '\\':
                        token.text += '\\';
                        break;
                    case '"':
                        token.text += '"';
                        break;
                    default:
                        return error(std::string("unknown escape sequence '\\") +
                                         peek() + "'",
                                     "\\n, \\t, \\r, \\\\, \\\"",
                                     std::string("'\\") + peek() + "'");
                }
                advance();
                continue;
            }
            token.text += peek();
            advance();
        }
        if (peek() != '"') {
            return error("unterminated string", "closing '\"'",
                         std::string("'") + token.text + "'");
        }
        advance();  // closing quote
        token.type = Tok::String;
        return token;
    }

    Token lexColor(Token token) {
        advance();  // '#'
        std::string hex;
        while (isHexDigit(peek())) {
            hex += peek();
            advance();
        }
        if (hex.size() != 6 && hex.size() != 8) {
            return error("invalid color literal", "#RRGGBB or #RRGGBBAA",
                         "#" + hex);
        }
        token.type = Tok::Color;
        token.text = "#" + hex;
        token.color = Color::fromRGBA(hexByte(hex, 0), hexByte(hex, 2),
                                      hexByte(hex, 4),
                                      hex.size() == 8 ? hexByte(hex, 6) : 255);
        return token;
    }

    Token lexNumber(Token token) {
        if (peek() == '+' || peek() == '-') {
            token.text += peek();
            advance();
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            token.text += peek();
            advance();
        }
        if (peek() == '.' &&
            std::isdigit(static_cast<unsigned char>(peek(1))) != 0) {
            token.text += peek();
            advance();
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                token.text += peek();
                advance();
            }
        }
        // Range-checked parse: std::stod would throw out_of_range on huge
        // literals and crash the loader instead of reporting a diagnostic.
        errno = 0;
        char* parseEnd = nullptr;
        const double value = std::strtod(token.text.c_str(), &parseEnd);
        if (errno == ERANGE ||
            parseEnd != token.text.c_str() + token.text.size()) {
            return error("number out of range", "finite number", token.text);
        }
        token.type = Tok::Number;
        token.number = value;
        return token;
    }

    const std::string& source_;
    std::string file_;
    std::size_t index_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    std::optional<DslError> error_{};
};

// --- AST ------------------------------------------------------------------

struct AstAttr {
    std::string name{};
    SourcePos pos{};
    Token value{};
};

struct AstNode {
    std::string name{};
    SourcePos pos{};
    std::optional<Token> positional{};
    std::vector<AstAttr> attrs{};
    std::vector<AstNode> children{};
};

// --- Parser ---------------------------------------------------------------

class Parser {
  public:
    Parser(std::vector<Token> tokens, std::string file)
        : tokens_(std::move(tokens)), file_(std::move(file)) {}

    [[nodiscard]] std::optional<DslError> parseDocument(AstNode& out) {
        if (const auto failure = expectKeyword("page", out.pos)) {
            return failure;
        }
        const Token& pageName = cur();
        if (pageName.type != Tok::Ident) {
            return makeError(pageName.pos, "missing page name", "identifier",
                             describeToken(pageName));
        }
        advance();
        if (const auto failure = expect(Tok::LBrace)) {
            return failure;
        }
        std::vector<AstNode> nodes;
        if (const auto failure = parseNodeList(nodes)) {
            return failure;
        }
        if (const auto failure = expect(Tok::RBrace)) {
            return failure;
        }
        if (cur().type != Tok::End) {
            return makeError(cur().pos, "trailing tokens after page",
                             "end of file", describeToken(cur()));
        }
        if (nodes.size() != 1) {
            return makeError(out.pos,
                             nodes.empty()
                                 ? "page body must contain a root widget"
                                 : "page must contain exactly one root widget",
                             "one root widget",
                             std::to_string(nodes.size()) + " root widgets");
        }
        out = std::move(nodes.front());
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DslError> parseNodeList(
        std::vector<AstNode>& out) {
        while (cur().type == Tok::Ident) {
            out.emplace_back();
            if (const auto failure = parseNode(out.back())) {
                return failure;
            }
        }
        return std::nullopt;
    }

  private:
    [[nodiscard]] const Token& cur() const { return tokens_[index_]; }

    void advance() {
        if (index_ + 1 < tokens_.size()) {
            ++index_;
        }
    }

    [[nodiscard]] std::optional<DslError> makeError(
        const SourcePos& pos, const std::string& message,
        const std::string& expected, const std::string& found) {
        return DslError{file_, pos, message, expected, found};
    }

    [[nodiscard]] std::optional<DslError> expect(Tok type) {
        if (cur().type == type) {
            advance();
            return std::nullopt;
        }
        return makeError(cur().pos,
                         "unexpected " + describe(cur().type),
                         describe(type), describeToken(cur()));
    }

    [[nodiscard]] std::optional<DslError> expectKeyword(
        const std::string& keyword, SourcePos& pos) {
        if (cur().type == Tok::Ident && cur().text == keyword) {
            pos = cur().pos;
            advance();
            return std::nullopt;
        }
        return makeError(cur().pos, "document must start with 'page'",
                         "'" + keyword + "'", describeToken(cur()));
    }

    [[nodiscard]] std::optional<DslError> parseNode(AstNode& out) {
        out.pos = cur().pos;
        out.name = cur().text;
        advance();
        if (cur().type == Tok::LParen) {
            advance();
            if (const auto failure = parseAttrs(out)) {
                return failure;
            }
        }
        if (cur().type == Tok::LBrace) {
            advance();
            if (const auto failure = parseNodeList(out.children)) {
                return failure;
            }
            if (const auto failure = expect(Tok::RBrace)) {
                return failure;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DslError> parseAttrs(AstNode& out) {
        if (cur().type == Tok::RParen) {
            advance();
            return std::nullopt;
        }
        // Leading positional content value (Text/Button label).
        if (cur().type == Tok::String) {
            out.positional = cur();
            advance();
            if (cur().type == Tok::Comma) {
                advance();
            } else if (cur().type == Tok::RParen) {
                advance();
                return std::nullopt;
            } else {
                return makeError(cur().pos, "expected ',' or ')' after value",
                                 "',' or ')'", describeToken(cur()));
            }
        }
        for (;;) {
            if (cur().type == Tok::RParen) {
                advance();
                return std::nullopt;
            }
            if (cur().type != Tok::Ident) {
                return makeError(cur().pos, "expected attribute name",
                                 "attribute name", describeToken(cur()));
            }
            AstAttr attr;
            attr.name = cur().text;
            attr.pos = cur().pos;
            advance();
            if (const auto failure = expect(Tok::Colon)) {
                return failure;
            }
            if (cur().type != Tok::Number && cur().type != Tok::String &&
                cur().type != Tok::Color && cur().type != Tok::Ident) {
                return makeError(cur().pos, "expected attribute value",
                                 "attribute value", describeToken(cur()));
            }
            attr.value = cur();
            advance();
            out.attrs.push_back(std::move(attr));
            if (cur().type == Tok::Comma) {
                advance();
                continue;  // trailing comma allowed before ')'
            }
            return expect(Tok::RParen);
        }
    }

    std::vector<Token> tokens_;
    std::string file_;
    std::size_t index_{0};
};

// --- AST -> Widget conversion ----------------------------------------------

[[nodiscard]] bool widgetTypeOf(const std::string& name, WidgetType& out) {
    if (name == "Container") {
        out = WidgetType::Container;
    } else if (name == "Row") {
        out = WidgetType::Row;
    } else if (name == "Column") {
        out = WidgetType::Column;
    } else if (name == "Stack") {
        out = WidgetType::Stack;
    } else if (name == "Text") {
        out = WidgetType::Text;
    } else if (name == "Button") {
        out = WidgetType::Button;
    } else if (name == "TextField") {
        out = WidgetType::TextField;
    } else if (name == "ScrollView") {
        // v0.3 阶段8D 冻结节点名。
        out = WidgetType::ScrollView;
    } else if (name == "ListView") {
        out = WidgetType::ListView;
    } else if (name == "Checkbox") {
        out = WidgetType::Checkbox;
    } else if (name == "Switch") {
        out = WidgetType::Switch;
    } else if (name == "FocusScope") {
        out = WidgetType::FocusScope;
    } else {
        return false;
    }
    return true;
}

class Converter {
  public:
    explicit Converter(std::string file) : file_(std::move(file)) {}

    [[nodiscard]] std::optional<DslError> convert(const AstNode& ast,
                                                  Widget& out) {
        out = Widget{};
        WidgetType type = WidgetType::Container;
        if (!widgetTypeOf(ast.name, type)) {
            return DslError{file_, ast.pos,
                            "unknown widget '" + ast.name + "'",
                            "Container|Row|Column|Stack|Text|Button|TextField|"
                            "ScrollView|ListView|Checkbox|Switch|FocusScope",
                            "'" + ast.name + "'"};
        }
        out.type = type;
        if (ast.positional.has_value()) {
            if (const auto failure = applyPositional(*ast.positional, out)) {
                return failure;
            }
        }
        for (const auto& attr : ast.attrs) {
            if (const auto failure = applyAttr(attr, out)) {
                return failure;
            }
        }
        // Normalized after all attrs so semantics never depend on attribute
        // order: a Text's literal prefix is its final text, exactly like the
        // C++ builder (dsl::text).
        if (out.type == WidgetType::Text) {
            out.bindPrefix = out.text;
        }
        return convertChildren(ast, out);
    }

  private:
    [[nodiscard]] std::optional<DslError> makeError(
        const SourcePos& pos, const std::string& message,
        const std::string& expected, const std::string& found) {
        return DslError{file_, pos, message, expected, found};
    }

    [[nodiscard]] static bool isLeaf(WidgetType type) {
        return type == WidgetType::Text || type == WidgetType::Button ||
               type == WidgetType::TextField ||
               type == WidgetType::Checkbox || type == WidgetType::Switch;
    }

    // 单子容器（v0.3 阶段8D：ScrollView/ListView/FocusScope 同 Container）。
    [[nodiscard]] static bool isSingleChild(WidgetType type) {
        return type == WidgetType::Container ||
               type == WidgetType::ScrollView ||
               type == WidgetType::ListView ||
               type == WidgetType::FocusScope;
    }

    [[nodiscard]] static bool isScrollable(WidgetType type) {
        return type == WidgetType::ScrollView ||
               type == WidgetType::ListView;
    }

    [[nodiscard]] std::optional<DslError> applyPositional(const Token& value,
                                                          Widget& out) {
        if (out.type == WidgetType::Text || out.type == WidgetType::Button ||
            out.type == WidgetType::Checkbox ||
            out.type == WidgetType::Switch) {
            if (value.type != Tok::String) {
                return makeError(value.pos, "content must be a string",
                                 "string", describeToken(value));
            }
            out.text = value.text;
            return std::nullopt;
        }
        return makeError(value.pos,
                         "'" + widgetName(out.type) +
                             "' does not take positional content",
                         "named attributes", describeToken(value));
    }

    [[nodiscard]] static std::string widgetName(WidgetType type) {
        switch (type) {
            case WidgetType::Container:
                return "Container";
            case WidgetType::Row:
                return "Row";
            case WidgetType::Column:
                return "Column";
            case WidgetType::Stack:
                return "Stack";
            case WidgetType::Text:
                return "Text";
            case WidgetType::Button:
                return "Button";
            case WidgetType::TextField:
                return "TextField";
            case WidgetType::ScrollView:
                return "ScrollView";
            case WidgetType::ListView:
                return "ListView";
            case WidgetType::Checkbox:
                return "Checkbox";
            case WidgetType::Switch:
                return "Switch";
            case WidgetType::FocusScope:
                return "FocusScope";
        }
        return "?";
    }

    [[nodiscard]] std::optional<DslError> typeError(const AstAttr& attr,
                                                    const std::string& wanted) {
        return makeError(attr.value.pos,
                         "attribute '" + attr.name + "' expects " + wanted,
                         wanted, describeToken(attr.value));
    }

    [[nodiscard]] std::optional<DslError> applyAttr(const AstAttr& attr,
                                                    Widget& out) {
        const Token& v = attr.value;
        const bool styled =
            out.type == WidgetType::Text || out.type == WidgetType::Button ||
            out.type == WidgetType::TextField ||
            out.type == WidgetType::Checkbox || out.type == WidgetType::Switch;

        // Attributes accepted by every widget.
        if (attr.name == "key") {
            return expectString(attr, out.key);
        }
        if (attr.name == "width" || attr.name == "height") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            (attr.name == "width" ? out.width : out.height) =
                static_cast<float>(v.number);
            return std::nullopt;
        }
        if (attr.name == "flex") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            out.flex = static_cast<float>(v.number);
            return std::nullopt;
        }
        if (attr.name == "padding" || attr.name == "margin") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            (attr.name == "padding" ? out.padding : out.margin) =
                EdgeInsets::all(static_cast<float>(v.number));
            return std::nullopt;
        }
        if (attr.name == "bind") {
            if (v.type != Tok::Ident) {
                return typeError(attr, "a state key");
            }
            out.bind = v.text;
            return std::nullopt;
        }
        if (attr.name == "left" || attr.name == "top") {
            // Stack relative positioning applies to any child (plan §5.1).
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            if (!out.stackPosition.has_value()) {
                out.stackPosition = Offset{};
            }
            (attr.name == "left" ? out.stackPosition->x
                                 : out.stackPosition->y) =
                static_cast<float>(v.number);
            return std::nullopt;
        }
        if (attr.name == "onClick") {
            if (v.type != Tok::Ident) {
                return typeError(attr, "a handler name");
            }
            out.onClick = v.text;
            return std::nullopt;
        }
        if (attr.name == "color") {
            if (v.type != Tok::Color) {
                return typeError(attr, "a color");
            }
            if (styled) {
                out.textStyle.color = v.color;
            } else {
                out.color = v.color;
            }
            return std::nullopt;
        }
        if (attr.name == "radius") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            if (styled) {
                return makeError(attr.pos,
                                 "'radius' is not valid on " +
                                     widgetName(out.type),
                                 "no attribute", "'" + attr.name + "'");
            }
            out.radius = CornerRadius::all(static_cast<float>(v.number));
            return std::nullopt;
        }
        // v0.3 阶段8D 冻结属性（容器/叶子通用段）：Checkbox/Switch 选中与
        // 滚动偏移。
        if (attr.name == "checked") {
            if (out.type != WidgetType::Checkbox &&
                out.type != WidgetType::Switch) {
                return makeError(attr.pos,
                                 "'checked' is only valid on Checkbox/Switch",
                                 "no attribute", "'" + attr.name + "'");
            }
            const auto flag = boolValue(attr);
            if (!flag.has_value()) {
                return typeError(attr, "true or false");
            }
            out.checked = *flag;
            return std::nullopt;
        }
        if (attr.name == "scrollOffset") {
            if (!isScrollable(out.type)) {
                return makeError(
                    attr.pos,
                    "'scrollOffset' is only valid on ScrollView/ListView",
                    "no attribute", "'" + attr.name + "'");
            }
            if (v.type != Tok::Number || v.number < 0) {
                return typeError(attr, "a number >= 0");
            }
            out.scrollOffset = static_cast<float>(v.number);
            return std::nullopt;
        }
        if (styled) {
            return applyStyledAttr(attr, out);
        }
        return applyContainerAttr(attr, out);
    }

    [[nodiscard]] std::optional<DslError> applyStyledAttr(const AstAttr& attr,
                                                          Widget& out) {
        const Token& v = attr.value;
        if (attr.name == "text") {
            return expectString(attr, out.text);
        }
        if (attr.name == "placeholder") {
            return expectString(attr, out.placeholder);
        }
        if (attr.name == "fontSize") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            out.textStyle.fontSize = static_cast<float>(v.number);
            return std::nullopt;
        }
        if (attr.name == "bold") {
            const auto flag = boolValue(attr);
            if (!flag.has_value()) {
                return typeError(attr, "true or false");
            }
            out.textStyle.bold = *flag;
            return std::nullopt;
        }
        // v0.3 阶段8B 冻结属性（plan §3.2）：只增加属性，不引入脚本能力。
        if (attr.name == "family") {
            return expectString(attr, out.textStyle.family);
        }
        if (attr.name == "weight") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            const int weight = static_cast<int>(v.number);
            if (weight < 100 || weight > 900 || weight % 100 != 0) {
                return makeError(attr.value.pos,
                                 "'weight' expects 100..900 in steps of 100",
                                 "100|200|...|900",
                                 "'" + v.text + "'");
            }
            out.textStyle.weight = weight;
            return std::nullopt;
        }
        if (attr.name == "italic") {
            const auto flag = boolValue(attr);
            if (!flag.has_value()) {
                return typeError(attr, "true or false");
            }
            out.textStyle.italic = *flag;
            return std::nullopt;
        }
        if (attr.name == "letterSpacing") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            out.textStyle.letterSpacing = static_cast<float>(v.number);
            return std::nullopt;
        }
        if (attr.name == "lineHeight") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            out.textStyle.lineHeight = static_cast<float>(v.number);
            return std::nullopt;
        }
        if (attr.name == "maxLines") {
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            if (v.number < 0) {
                return makeError(attr.value.pos,
                                 "'maxLines' expects >= 0", ">= 0",
                                 "'" + v.text + "'");
            }
            out.textStyle.maxLines = static_cast<std::size_t>(v.number);
            return std::nullopt;
        }
        if (attr.name == "overflow") {
            const std::string& text = v.text;
            if (v.type != Tok::Ident) {
                return typeError(attr, "clip|ellipsis|fade|visible");
            }
            if (text == "clip") {
                out.textStyle.overflow = TextOverflow::Clip;
            } else if (text == "ellipsis") {
                out.textStyle.overflow = TextOverflow::Ellipsis;
            } else if (text == "fade") {
                out.textStyle.overflow = TextOverflow::Fade;
            } else if (text == "visible") {
                out.textStyle.overflow = TextOverflow::Visible;
            } else {
                return enumError(attr, "clip|ellipsis|fade|visible");
            }
            return std::nullopt;
        }
        if (attr.name == "obscure" || attr.name == "readOnly" ||
            attr.name == "multiline") {
            if (out.type != WidgetType::TextField) {
                return makeError(attr.pos,
                                 "'" + attr.name +
                                     "' is only valid on TextField",
                                 "no attribute", "'" + attr.name + "'");
            }
            const auto flag = boolValue(attr);
            if (!flag.has_value()) {
                return typeError(attr, "true or false");
            }
            if (attr.name == "obscure") {
                out.obscure = *flag;
            } else if (attr.name == "readOnly") {
                out.readOnly = *flag;
            } else {
                out.multiline = *flag;
            }
            return std::nullopt;
        }
        return makeError(attr.pos,
                         "unknown attribute '" + attr.name + "' for " +
                             widgetName(out.type),
                         "known attribute", "'" + attr.name + "'");
    }

    [[nodiscard]] std::optional<DslError> applyContainerAttr(const AstAttr& attr,
                                                             Widget& out) {
        const Token& v = attr.value;
        if (attr.name == "spacing") {
            if (out.type != WidgetType::Row && out.type != WidgetType::Column) {
                return makeError(attr.pos,
                                 "'spacing' is only valid on Row/Column",
                                 "no attribute", "'" + attr.name + "'");
            }
            if (v.type != Tok::Number) {
                return typeError(attr, "a number");
            }
            out.spacing = static_cast<float>(v.number);
            return std::nullopt;
        }
        if (attr.name == "mainAxis") {
            const auto value = mainAxisValue(attr);
            if (!value.has_value()) {
                return enumError(attr, "start|center|end|spaceBetween|"
                                       "spaceAround|spaceEvenly");
            }
            out.mainAxis = *value;
            return std::nullopt;
        }
        if (attr.name == "crossAxis") {
            const auto value = crossAxisValue(attr);
            if (!value.has_value()) {
                return enumError(attr, "start|center|end|stretch");
            }
            out.crossAxis = *value;
            return std::nullopt;
        }
        if (attr.name == "alignment") {
            const auto value = stackAlignmentValue(attr);
            if (!value.has_value()) {
                return enumError(attr,
                                 "topLeft|topCenter|topRight|centerLeft|center|"
                                 "centerRight|bottomLeft|bottomCenter|"
                                 "bottomRight");
            }
            out.stackAlignment = *value;
            return std::nullopt;
        }
        return makeError(attr.pos,
                         "unknown attribute '" + attr.name + "' for " +
                             widgetName(out.type),
                         "known attribute", "'" + attr.name + "'");
    }

    [[nodiscard]] std::optional<DslError> expectString(const AstAttr& attr,
                                                       std::string& out) {
        if (attr.value.type != Tok::String) {
            return typeError(attr, "a string");
        }
        out = attr.value.text;
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<bool> boolValue(const AstAttr& attr) {
        if (attr.value.type == Tok::Ident && attr.value.text == "true") {
            return true;
        }
        if (attr.value.type == Tok::Ident && attr.value.text == "false") {
            return false;
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<MainAxisAlignment> mainAxisValue(
        const AstAttr& attr) {
        if (attr.value.type != Tok::Ident) {
            return std::nullopt;
        }
        const std::string& v = attr.value.text;
        if (v == "start") {
            return MainAxisAlignment::Start;
        }
        if (v == "center") {
            return MainAxisAlignment::Center;
        }
        if (v == "end") {
            return MainAxisAlignment::End;
        }
        if (v == "spaceBetween") {
            return MainAxisAlignment::SpaceBetween;
        }
        if (v == "spaceAround") {
            return MainAxisAlignment::SpaceAround;
        }
        if (v == "spaceEvenly") {
            return MainAxisAlignment::SpaceEvenly;
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<CrossAxisAlignment> crossAxisValue(
        const AstAttr& attr) {
        if (attr.value.type != Tok::Ident) {
            return std::nullopt;
        }
        const std::string& v = attr.value.text;
        if (v == "start") {
            return CrossAxisAlignment::Start;
        }
        if (v == "center") {
            return CrossAxisAlignment::Center;
        }
        if (v == "end") {
            return CrossAxisAlignment::End;
        }
        if (v == "stretch") {
            return CrossAxisAlignment::Stretch;
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<StackAlignment> stackAlignmentValue(
        const AstAttr& attr) {
        if (attr.value.type != Tok::Ident) {
            return std::nullopt;
        }
        const std::string& v = attr.value.text;
        if (v == "topLeft") {
            return StackAlignment::TopLeft;
        }
        if (v == "topCenter") {
            return StackAlignment::TopCenter;
        }
        if (v == "topRight") {
            return StackAlignment::TopRight;
        }
        if (v == "centerLeft") {
            return StackAlignment::CenterLeft;
        }
        if (v == "center") {
            return StackAlignment::Center;
        }
        if (v == "centerRight") {
            return StackAlignment::CenterRight;
        }
        if (v == "bottomLeft") {
            return StackAlignment::BottomLeft;
        }
        if (v == "bottomCenter") {
            return StackAlignment::BottomCenter;
        }
        if (v == "bottomRight") {
            return StackAlignment::BottomRight;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<DslError> enumError(const AstAttr& attr,
                                                    const std::string& values) {
        return makeError(attr.value.pos,
                         "invalid value for '" + attr.name + "'", values,
                         describeToken(attr.value));
    }

    [[nodiscard]] std::optional<DslError> convertChildren(const AstNode& ast,
                                                          Widget& out) {
        if (ast.children.empty()) {
            return std::nullopt;
        }
        if (isLeaf(out.type)) {
            return DslError{file_, ast.children.front().pos,
                            "'" + ast.name + "' cannot contain children",
                            "no children", "'{'"};
        }
        if (isSingleChild(out.type) && ast.children.size() > 1) {
            return DslError{file_, ast.children[1].pos,
                            "'" + ast.name + "' accepts at most one child",
                            "at most one child",
                            std::to_string(ast.children.size()) + " children"};
        }
        for (const auto& child : ast.children) {
            Widget converted;
            if (const auto failure = convert(child, converted)) {
                return failure;
            }
            out.children.push_back(std::move(converted));
        }
        return std::nullopt;
    }

    std::string file_;
};

}  // namespace

std::string DslError::format() const {
    std::ostringstream out;
    out << file << ':' << pos.line << ':' << pos.column << ": " << message;
    if (!expected.empty() || !found.empty()) {
        out << " (expected " << (expected.empty() ? "?" : expected)
            << ", found " << (found.empty() ? "?" : found) << ")";
    }
    return out.str();
}

DslParseResult parseLumen(const std::string& source, std::string filename) {
    std::vector<Token> tokens;
    Lexer lexer(source, filename);
    if (const auto failure = lexer.run(tokens)) {
        return DslParseResult{Widget{}, failure};
    }
    Parser parser(std::move(tokens), filename);
    AstNode root;
    if (const auto failure = parser.parseDocument(root)) {
        return DslParseResult{Widget{}, failure};
    }
    Converter converter(std::move(filename));
    Widget widget;
    if (const auto failure = converter.convert(root, widget)) {
        return DslParseResult{Widget{}, failure};
    }
    return DslParseResult{std::move(widget), std::nullopt};
}

DslParseResult parseLumenFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        DslParseResult result{};
        result.error = DslError{path, SourcePos{1, 1}, "cannot read file", "",
                                ""};
        return result;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseLumen(buffer.str(), path);
}

namespace {

[[nodiscard]] std::uint64_t contentHash(const std::string& source) {
    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    std::uint64_t hash = kFnvOffsetBasis;
    for (const char byte : source) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= kFnvPrime;
    }
    return hash;
}

}  // namespace

DslParseResult DslCache::parse(const std::string& source,
                               std::string filename) {
    const std::uint64_t key = contentHash(source);
    const auto cached = entries_.find(key);
    if (cached != entries_.end()) {
        ++stats_.hits;
        return DslParseResult{cached->second, std::nullopt};
    }
    ++stats_.misses;
    DslParseResult parsed = parseLumen(source, std::move(filename));
    if (parsed.ok()) {
        entries_.emplace(key, parsed.root);
    }
    return parsed;
}

void DslCache::clear() {
    entries_.clear();
}

}  // namespace lumen::dsl
