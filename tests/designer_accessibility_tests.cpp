#include <catch2/catch_test_macros.hpp>

#include "lumen/accessibility/semantics.h"
#include "lumen/dsl/design_preview_frame.h"
#include "lumen/layout/layout.h"

using lumen::accessibility::SemanticsRole;
using lumen::accessibility::buildSemanticsTree;
using lumen::accessibility::kActionActivate;
using lumen::core::Constraints;
using lumen::core::Size;
using lumen::dsl::DesignPreviewFrame;
using lumen::dsl::parseLumenSource;
using lumen::layout::LayoutEngine;

TEST_CASE("designer preview exposes a stable semantic tree for L0 controls",
          "[designer][f6][accessibility]") {
    const auto parsed = parseLumenSource(
        "page editor { Column(key: \"root\") {"
        " Text(\"Title\", key: \"title\")"
        " Button(\"Save\", key: \"save\")"
        " TextField(bind: name, placeholder: \"Name\", key: \"name\")"
        " } }");
    REQUIRE(parsed.ok());

    DesignPreviewFrame frame;
    REQUIRE(frame.update(parsed.document));
    const auto renderTree = LayoutEngine::layout(
        frame.widget(), Constraints::tight(Size{640.0F, 480.0F}));
    const auto semantics = buildSemanticsTree(renderTree);

    const auto* root = semantics.find(semantics.rootId);
    REQUIRE(root != nullptr);
    CHECK(root->role == SemanticsRole::Window);

    const auto* title = semantics.find("/k:root/k:title");
    REQUIRE(title != nullptr);
    CHECK(title->role == SemanticsRole::Text);
    CHECK(title->label == "Title");
    CHECK(title->bounds.size.width > 0.0F);

    const auto* save = semantics.find("/k:root/k:save");
    REQUIRE(save != nullptr);
    CHECK(save->role == SemanticsRole::Button);
    CHECK(save->label == "Save");
    CHECK((save->actions & kActionActivate) != 0);

    const auto* name = semantics.find("/k:root/k:name");
    REQUIRE(name != nullptr);
    CHECK(name->role == SemanticsRole::TextField);
    CHECK(name->label == "Name");

    const auto repeatedTree = LayoutEngine::layout(
        frame.widget(), Constraints::tight(Size{640.0F, 480.0F}));
    const auto repeatedSemantics = buildSemanticsTree(repeatedTree);
    CHECK(semantics.rootId == repeatedSemantics.rootId);
    CHECK(semantics.nodes == repeatedSemantics.nodes);
}
