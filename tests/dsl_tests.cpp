// Text DSL tests (plan 阶段4): golden parsing, error diagnostics with
// file/line/column/expected token, and runtime bind resolution.

#include <catch2/catch_test_macros.hpp>

#include "counter_app.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/dsl/dsl.h"
#include "lumen/dsl/text_dsl.h"
#include "lumen/layout/layout.h"

using namespace lumen::core;
using namespace lumen::dsl;
using namespace lumen::layout;
using lumen::examples::CounterApp;

namespace {

// Same document as examples/counter/counter.lumen.
constexpr const char* kCounterSource =
    "// Counter UI\n"
    "page Counter {\n"
    "  Container(key: \"root\", color: #18181B) {\n"
    "    Column(padding: 24, spacing: 12) {\n"
    "      Text(\"Count: \", bind: counter, key: \"count-text\")\n"
    "      Button(\"Increment\", onClick: increment, key: \"increment-button\")\n"
    "      TextField(bind: name, placeholder: \"Name\", key: \"name-field\")\n"
    "    }\n"
    "  }\n"
    "}\n";

}  // namespace

TEST_CASE("dsl_golden_counter_document", "[dsl]") {
    const DslParseResult parsed = parseLumen(kCounterSource, "counter.lumen");
    REQUIRE(parsed.ok());
    // The text DSL must reproduce the C++ builder tree field for field
    // (plan §7: C++ DSL 与文本 DSL 都能构建同一 UI).
    CHECK(parsed.root == CounterApp::buildUi());
}

TEST_CASE("dsl_parses_defaults", "[dsl]") {
    const DslParseResult parsed = parseLumen("page P { Text(\"hi\") }");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.root.type == WidgetType::Text);
    CHECK(parsed.root.text == "hi");
    CHECK(parsed.root.textStyle.fontSize == 14.0F);
    CHECK_FALSE(parsed.root.textStyle.bold);
    CHECK(parsed.root.children.empty());
}

TEST_CASE("dsl_parses_number_string_color_bool_attrs", "[dsl]") {
    const DslParseResult parsed = parseLumen(
        "page P {\n"
        "  Container(color: #11223344, radius: 6, padding: 1.5) {\n"
        "    Text(\"hi\", fontSize: 20, bold: true, color: #FF0000)\n"
        "  }\n"
        "}\n");
    REQUIRE(parsed.ok());
    const Widget& container = parsed.root;
    REQUIRE(container.type == WidgetType::Container);
    CHECK(container.color == Color::fromRGBA(0x11, 0x22, 0x33, 0x44));
    CHECK(container.radius == CornerRadius::all(6.0F));
    CHECK(container.padding == EdgeInsets::all(1.5F));
    REQUIRE(container.children.size() == 1);
    const Widget& text = container.children.front();
    CHECK(text.textStyle.fontSize == 20.0F);
    CHECK(text.textStyle.bold);
    CHECK(text.textStyle.color == Color::fromRGBA(255, 0, 0));
}

TEST_CASE("dsl_parses_alignment_enums_and_stack_position", "[dsl]") {
    const DslParseResult parsed = parseLumen(
        "page P {\n"
        "  Stack(alignment: bottomRight) {\n"
        "    Row(mainAxis: spaceBetween, crossAxis: stretch, spacing: 4) {\n"
        "      Text(\"a\")\n"
        "    }\n"
        "    Text(\"b\", left: 10, top: 20)\n"
        "  }\n"
        "}\n");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.root.type == WidgetType::Stack);
    CHECK(parsed.root.stackAlignment == StackAlignment::BottomRight);
    REQUIRE(parsed.root.children.size() == 2);
    const Widget& row = parsed.root.children[0];
    CHECK(row.mainAxis == MainAxisAlignment::SpaceBetween);
    CHECK(row.crossAxis == CrossAxisAlignment::Stretch);
    CHECK(row.spacing == 4.0F);
    const Widget& positioned = parsed.root.children[1];
    REQUIRE(positioned.stackPosition.has_value());
    CHECK(*positioned.stackPosition == Offset{10.0F, 20.0F});
}

TEST_CASE("dsl_parses_signed_numeric_values", "[dsl]") {
    const DslParseResult parsed = parseLumen(
        "page P { Stack { Text(\"x\", left: -10.5, top: +2) } }");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.root.children.size() == 1);
    REQUIRE(parsed.root.children.front().stackPosition.has_value());
    CHECK(parsed.root.children.front().stackPosition->x == -10.5F);
    CHECK(parsed.root.children.front().stackPosition->y == 2.0F);
}

TEST_CASE("dsl_error_reports_file_line_column_and_expected", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P {\n  Text(\"x\" padding: 1)\n}\n", "bad.lumen");
    REQUIRE_FALSE(parsed.ok());
    const std::string formatted = parsed.error->format();
    INFO(formatted);
    CHECK(parsed.error->file == "bad.lumen");
    CHECK(parsed.error->pos.line == 2);
    CHECK(parsed.error->pos.column >= 10);
    CHECK(formatted.find("bad.lumen:2:") == 0);
    CHECK_FALSE(parsed.error->expected.empty());
}

TEST_CASE("dsl_error_missing_page_keyword", "[dsl]") {
    const DslParseResult parsed = parseLumen("Column { }");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->expected == "'page'");
    CHECK(parsed.error->format().find("expected 'page'") !=
          std::string::npos);
}

TEST_CASE("dsl_error_unterminated_string", "[dsl]") {
    const DslParseResult parsed = parseLumen("page P { Text(\"oops) }", "s.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->pos.line == 1);
    CHECK(parsed.error->expected == "closing '\"'");
}

TEST_CASE("dsl_error_invalid_color_literal", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P { Container(color: #12) }", "c.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->expected == "#RRGGBB or #RRGGBBAA");
    CHECK(parsed.error->pos.line == 1);
}

TEST_CASE("dsl_error_unknown_widget", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P {\n  Txt(\"x\")\n}\n", "w.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->message.find("unknown widget 'Txt'") !=
          std::string::npos);
    CHECK(parsed.error->pos.line == 2);
}

TEST_CASE("dsl_error_unknown_attribute", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P {\n  Text(\"x\", foo: 1)\n}\n", "a.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->message.find("unknown attribute 'foo'") !=
          std::string::npos);
    CHECK(parsed.error->pos.line == 2);
}

TEST_CASE("dsl_error_wrong_attribute_type", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P { Text(\"x\", padding: \"wide\") }", "t.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->message.find("expects a number") != std::string::npos);
}

TEST_CASE("dsl_error_invalid_enum_value", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P { Row(mainAxis: sideways) }", "e.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->expected.find("spaceEvenly") != std::string::npos);
}

TEST_CASE("dsl_error_leaf_with_children", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P {\n  Text(\"x\") { Text(\"y\") }\n}\n", "n.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->message.find("cannot contain children") !=
          std::string::npos);
    CHECK(parsed.error->pos.line == 2);
}

TEST_CASE("dsl_error_page_requires_exactly_one_root", "[dsl]") {
    const DslParseResult one = parseLumen("page P { }", "x.l");
    REQUIRE_FALSE(one.ok());
    CHECK(one.error->message.find("root widget") != std::string::npos);

    const DslParseResult two =
        parseLumen("page P { Text(\"a\") Text(\"b\") }", "x.l");
    REQUIRE_FALSE(two.ok());
    CHECK(two.error->message.find("exactly one") != std::string::npos);
}

TEST_CASE("dsl_error_trailing_tokens", "[dsl]") {
    const DslParseResult parsed = parseLumen("page P { Text(\"a\") } extra", "j.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->message.find("trailing") != std::string::npos);
}

TEST_CASE("dsl_runtime_binds_resolve_through_state_store", "[dsl]") {
    const DslParseResult parsed = parseLumen(kCounterSource, "counter.lumen");
    REQUIRE(parsed.ok());
    StateStore store;
    store.set("counter", "42");
    Widget ui = parsed.root;
    applyBinds(ui, store);
    const RenderNode root = LayoutEngine::layout(
        ui, Constraints::tight(Size{800.0F, 600.0F}));
    const RenderNode* label = findNodeByKey(root, "count-text");
    REQUIRE(label != nullptr);
    CHECK(label->text == "Count: 42");
}

TEST_CASE("dsl_file_matches_cpp_ui", "[dsl]") {
    // The asset is copied next to the test binary (tests/CMakeLists.txt).
    const DslParseResult parsed = parseLumenFile("counter.lumen");
    REQUIRE(parsed.ok());
    CHECK(parsed.error == std::nullopt);
    CHECK(parsed.root == CounterApp::buildUi());
}

TEST_CASE("dsl_file_reports_missing_file", "[dsl]") {
    const DslParseResult parsed = parseLumenFile("does-not-exist.lumen");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->message == "cannot read file");
    CHECK(parsed.error->file == "does-not-exist.lumen");
}

TEST_CASE("dsl_comments_and_trailing_commas", "[dsl]") {
    const DslParseResult parsed = parseLumen(
        "// leading comment\n"
        "page P {\n"
        "  Column(padding: 8, // inline comment\n"
        "         spacing: 2,) {\n"
        "    Text(\"a\",)\n"
        "  }\n"
        "}\n");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.root.type == WidgetType::Column);
    CHECK(parsed.root.padding == EdgeInsets::all(8.0F));
    CHECK(parsed.root.spacing == 2.0F);
    CHECK(parsed.root.children.size() == 1);
}

TEST_CASE("dsl_decodes_string_escapes", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P { Text(\"a\\tb\\nc\\rd\\\\e\\\"f\") }");
    REQUIRE(parsed.ok());
    CHECK(parsed.root.text == "a\tb\nc\rd\\e\"f");
}

TEST_CASE("dsl_error_unknown_escape", "[dsl]") {
    const DslParseResult parsed =
        parseLumen("page P {\n  Text(\"a\\qb\")\n}\n", "esc.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->message.find("unknown escape") != std::string::npos);
    CHECK(parsed.error->pos.line == 2);
}

TEST_CASE("dsl_named_attrs_match_positional_form", "[dsl]") {
    // Attribute order must not change semantics (declarative format).
    const DslParseResult positional =
        parseLumen("page P { Text(\"Count: \", bind: counter) }");
    const DslParseResult named =
        parseLumen("page P { Text(bind: counter, text: \"Count: \") }");
    REQUIRE(positional.ok());
    REQUIRE(named.ok());
    CHECK(named.root == positional.root);
    CHECK(named.root.bindPrefix == "Count: ");

    // A Text without bind still carries the literal as bindPrefix, exactly
    // like dsl::text().
    const DslParseResult plain = parseLumen("page P { Text(\"hi\") }");
    REQUIRE(plain.ok());
    CHECK(plain.root == text("hi"));
}

TEST_CASE("dsl_error_number_out_of_range", "[dsl]") {
    // A 400-digit literal must produce a diagnostic, not an exception.
    std::string huge(400, '9');
    const DslParseResult parsed =
        parseLumen("page P { Text(\"a\", fontSize: " + huge + ") }", "big.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->message.find("out of range") != std::string::npos);
}

TEST_CASE("dsl_columns_count_code_points_not_bytes", "[dsl]") {
    // "你" (3 UTF-8 bytes) occupies one column; the faulty token starts at
    // code-point column 14 on line 2 (byte counting would report 20).
    const DslParseResult parsed =
        parseLumen("page P {\n  Text(\"你你你\" pad)\n}\n", "col.l");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error->pos.line == 2);
    CHECK(parsed.error->pos.column == 14);
}

// --- Parse cache (plan 阶段6: DSL 编译缓存). ---

TEST_CASE("dsl_cache_hits_on_identical_content", "[dsl]") {
    DslCache cache;
    const DslParseResult first = cache.parse(kCounterSource, "a.l");
    const DslParseResult second = cache.parse(kCounterSource, "b.l");
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.root == second.root);
    CHECK(cache.stats().misses == 1);
    CHECK(cache.stats().hits == 1);

    (void)cache.parse(kCounterSource);
    CHECK(cache.stats().hits == 2);
}

TEST_CASE("dsl_cache_misses_on_changed_content_and_skips_errors", "[dsl]") {
    DslCache cache;
    REQUIRE(cache.parse("page P { Text(\"a\") }").ok());
    // Errors are never cached: fixing the document reparses immediately.
    REQUIRE_FALSE(cache.parse("page P { Txt(\"x\") }").ok());
    const auto statsAfterError = cache.stats();
    CHECK(statsAfterError.misses == 2);
    CHECK(statsAfterError.hits == 0);
    REQUIRE_FALSE(cache.parse("page P { Txt(\"x\") }").ok());
    CHECK(cache.stats().misses == 3);

    REQUIRE(cache.parse("page P { Text(\"b\") }").ok());
    CHECK(cache.stats().misses == 4);

    cache.clear();
    REQUIRE(cache.parse("page P { Text(\"b\") }").ok());
    CHECK(cache.stats().misses == 5);
    CHECK(cache.stats().hits == 0);
}

// --- 视觉系统：控件声明属性解析（visual-system §6.1 / §10.1） ---

TEST_CASE("dsl_parses_visual_system_control_attributes", "[dsl]") {
    const DslParseResult parsed = parseLumen(
        "page root {\n"
        "  Column {\n"
        "    Button(\"Save\", variant: outline, size: large, onClick: save)\n"
        "    TextField(bind: name, invalid: true, enabled: false)\n"
        "    Checkbox(\"A\", bind: a, selected: true)\n"
        "  }\n"
        "}");
    REQUIRE(parsed.ok());
    // page 的唯一根就是 Column 本身。
    const Widget& column = parsed.root;
    REQUIRE(column.children.size() == 3);

    const Widget& button = column.children[0];
    CHECK(button.buttonVariant == ButtonVariant::Outline);
    CHECK(button.controlSize == ControlSize::Large);
    CHECK(button.enabled);

    const Widget& field = column.children[1];
    CHECK(field.invalid);
    CHECK_FALSE(field.enabled);

    const Widget& checkbox = column.children[2];
    CHECK(checkbox.selected);
}

TEST_CASE("dsl_rejects_invalid_visual_attributes", "[dsl]") {
    // variant 只对 Button 有效；取值必须是五个枚举名之一。
    CHECK_FALSE(parseLumen(
        "page root { Text(\"x\", variant: outline) }").ok());
    CHECK_FALSE(parseLumen(
        "page root { Button(\"x\", variant: round) }").ok());
    CHECK_FALSE(parseLumen(
        "page root { Button(\"x\", size: huge) }").ok());
    CHECK_FALSE(parseLumen(
        "page root { Button(\"x\", enabled: maybe) }").ok());
}
