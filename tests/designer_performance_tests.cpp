#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_schema.h"
#include "lumen/dsl/design_workbench.h"
#include "lumen/core/virtual_list.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/render/renderer.h"

using lumen::core::Constraints;
using lumen::core::Size;
using lumen::dsl::DesignDocument;
using lumen::dsl::DesignNode;
using lumen::dsl::DesignPreviewOutlineNode;
using lumen::dsl::DesignPreviewWorkbench;
using lumen::dsl::DesignValue;
using lumen::dsl::compileDesignDocument;
using lumen::dsl::parseLumenSource;
using lumen::dsl::readDesignDocument;
using lumen::dsl::serializeDesignDocument;
using lumen::dsl::validateDesignDocument;

namespace {

using Clock = std::chrono::steady_clock;

struct FixtureDocument {
    DesignDocument document{};
    std::string encoded{};
    std::chrono::microseconds encodeTime{};
    std::chrono::microseconds readTime{};
    std::chrono::microseconds schemaTime{};
    std::chrono::microseconds compileTime{};
    std::chrono::microseconds layoutTime{};
    std::chrono::microseconds paintTime{};
    lumen::core::RenderNode renderNode{};
    std::uint64_t frameHash{0};
};

DesignValue stringValue(std::string value) {
    return DesignValue{DesignValue::Variant{std::move(value)}};
}

DesignDocument makeDocument(std::size_t nodeCount) {
    REQUIRE(nodeCount >= 1);

    DesignDocument document;
    document.documentId = "designer-performance";
    document.pageName = "baseline";
    document.root.id = 1;
    document.root.type = "Column";
    document.root.properties["key"] = stringValue("root");
    document.root.children.reserve(nodeCount - 1);
    for (std::size_t index = 1; index < nodeCount; ++index) {
        DesignNode child;
        child.id = static_cast<std::uint64_t>(index + 1);
        child.type = "Text";
        child.properties["key"] =
            stringValue("node-" + std::to_string(index));
        child.properties["text"] =
            stringValue("Designer baseline node " + std::to_string(index));
        document.root.children.push_back(std::move(child));
    }
    return document;
}

std::string sourceForNodeCount(std::size_t nodeCount) {
    REQUIRE(nodeCount >= 1);
    std::ostringstream source;
    source << "page baseline { Column(key: \"root\") {";
    for (std::size_t index = 1; index < nodeCount; ++index) {
        source << " Text(\"Designer baseline node " << index
               << "\", key: \"node-" << index << "\")";
    }
    source << " } }";
    return source.str();
}

std::size_t nodeCount(const DesignNode& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) count += nodeCount(child);
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) count += nodeCount(child);
    }
    return count;
}

std::size_t nodeCount(const lumen::core::RenderNode& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) count += nodeCount(child);
    return count;
}

std::size_t outlineNodeCount(const DesignPreviewOutlineNode& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) {
        count += outlineNodeCount(child);
    }
    return count;
}

FixtureDocument runFixture(DesignDocument document) {
    FixtureDocument fixture;
    fixture.document = std::move(document);

    const auto encodeBegin = Clock::now();
    fixture.encoded = serializeDesignDocument(fixture.document);
    fixture.encodeTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - encodeBegin);

    const auto readBegin = Clock::now();
    const auto read = readDesignDocument(fixture.encoded, "baseline.design");
    fixture.readTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - readBegin);
    REQUIRE(read.ok());
    REQUIRE(read.document == fixture.document);

    const auto schemaBegin = Clock::now();
    const auto schemaDiagnostics = validateDesignDocument(fixture.document);
    fixture.schemaTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - schemaBegin);
    REQUIRE(schemaDiagnostics.empty());

    const auto compileBegin = Clock::now();
    const auto compiled = compileDesignDocument(fixture.document);
    fixture.compileTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - compileBegin);
    REQUIRE(compiled.ok());
    REQUIRE(compiled.trace.nodes.size() == nodeCount(fixture.document.root));

    const auto layoutBegin = Clock::now();
    fixture.renderNode = lumen::layout::LayoutEngine::layout(
        compiled.root, Constraints::tight(Size{800.0F, 600.0F}));
    fixture.layoutTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - layoutBegin);
    REQUIRE(nodeCount(fixture.renderNode) == nodeCount(fixture.document.root));

    lumen::render::CpuRenderer renderer;
    const auto paintBegin = Clock::now();
    renderer.beginFrame(Size{800.0F, 600.0F});
    lumen::render::paintScene(renderer, fixture.renderNode);
    renderer.endFrame();
    fixture.paintTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - paintBegin);
    fixture.frameHash = lumen::render::frameHash(renderer.pixels());
    return fixture;
}

std::string timingSummary(const std::string& label,
                          const FixtureDocument& fixture) {
    std::ostringstream summary;
    summary << label << " encode_us=" << fixture.encodeTime.count()
            << " read_us=" << fixture.readTime.count()
            << " schema_us=" << fixture.schemaTime.count()
            << " compile_us=" << fixture.compileTime.count()
            << " layout_us=" << fixture.layoutTime.count()
            << " paint_us=" << fixture.paintTime.count();
    return summary.str();
}

}  // namespace

TEST_CASE("designer performance fixtures are deterministic across the L0 sizes",
          "[designer][f6][performance]") {
    const std::vector<std::size_t> sizes = {12, 100, 1000};
    for (const auto size : sizes) {
        const auto first = runFixture(makeDocument(size));
        const auto second = runFixture(makeDocument(size));
        INFO(timingSummary(std::to_string(size) + " nodes", first));
        INFO(timingSummary(std::to_string(size) + " nodes repeat", second));

        CHECK(first.document == second.document);
        CHECK(first.encoded == second.encoded);
        CHECK(first.renderNode == second.renderNode);
        CHECK(first.frameHash == second.frameHash);
    }
}

TEST_CASE("designer performance baseline imports the twelve node L0 source",
          "[designer][f6][performance]") {
    const auto begin = Clock::now();
    const auto parsed = parseLumenSource(sourceForNodeCount(12),
                                         "baseline.lumen");
    const auto parseTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - begin);
    REQUIRE(parsed.ok());
    REQUIRE(nodeCount(parsed.document.root) == 12);

    const auto fixture = runFixture(parsed.document);
    INFO("12 node source parse_us=" << parseTime.count());
    INFO(timingSummary("12 node source", fixture));
    CHECK(fixture.frameHash != 0);
}

TEST_CASE("designer performance covers edit and outline fixtures",
          "[designer][f6][performance]") {
    DesignPreviewWorkbench editor;
    REQUIRE(editor.openDocument(makeDocument(100)));
    const auto beforeRevision = editor.documentRevision();
    const auto editBegin = Clock::now();
    REQUIRE(editor.setProperty(
        2, "text", stringValue("Designer edited fixture node")));
    const auto editTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - editBegin);
    REQUIRE(editor.document().has_value());
    CHECK(editor.document()->root.children.size() == 99);
    CHECK(editor.dirty());
    CHECK(editor.documentRevision() > beforeRevision);
    CHECK(editor.frame().hasFrame());
    REQUIRE(editor.undo());
    CHECK_FALSE(editor.dirty());
    CHECK(editor.documentRevision() == beforeRevision);

    DesignPreviewWorkbench outlineWorkbench;
    REQUIRE(outlineWorkbench.openDocument(makeDocument(1000)));
    const auto outlineBegin = Clock::now();
    const auto firstOutline = outlineWorkbench.outline();
    const auto outlineTime =
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now() - outlineBegin);
    REQUIRE(firstOutline.has_value());
    REQUIRE(outlineNodeCount(*firstOutline) == 1000);
    const auto secondOutline = outlineWorkbench.outline();
    REQUIRE(secondOutline.has_value());
    CHECK(*firstOutline == *secondOutline);
    INFO("100 node edit_us=" << editTime.count());
    INFO("1000 node outline_us=" << outlineTime.count());
}

TEST_CASE("designer runtime preview virtualizes a large list deterministically",
          "[designer][f6][performance]") {
    lumen::core::VirtualListController source;
    source.setItemCount(1000);
    source.setEstimatedExtent(32.0F);
    source.setItemBuilder([](std::size_t index) {
        auto item = lumen::core::makeText("Preview item " +
                                          std::to_string(index));
        item.key = "item-" + std::to_string(index);
        item.height = 32.0F;
        return item;
    });
    const auto widget = lumen::core::makeVirtualList(
        &source, "preview-list", std::nullopt, 600.0F, 200.0F);

    const auto layoutBegin = Clock::now();
    const auto first = lumen::layout::LayoutEngine::layout(
        widget, Constraints::tight(Size{800.0F, 600.0F}));
    const auto layoutTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - layoutBegin);
    REQUIRE(first.children.size() > 0);
    CHECK(first.children.size() < 50);
    CHECK(first.children.size() < source.itemCount());

    lumen::render::CpuRenderer renderer;
    const auto paintBegin = Clock::now();
    renderer.beginFrame(Size{800.0F, 600.0F});
    lumen::render::paintScene(renderer, first);
    renderer.endFrame();
    const auto paintTime = std::chrono::duration_cast<std::chrono::microseconds>(
        Clock::now() - paintBegin);
    const auto firstHash = lumen::render::frameHash(renderer.pixels());

    const auto second = lumen::layout::LayoutEngine::layout(
        widget, Constraints::tight(Size{800.0F, 600.0F}));
    lumen::render::CpuRenderer repeatedRenderer;
    repeatedRenderer.beginFrame(Size{800.0F, 600.0F});
    lumen::render::paintScene(repeatedRenderer, second);
    repeatedRenderer.endFrame();
    CHECK(first == second);
    CHECK(lumen::render::frameHash(repeatedRenderer.pixels()) == firstHash);
    CHECK(firstHash != 0);
    INFO("virtual-list layout_us=" << layoutTime.count()
         << " paint_us=" << paintTime.count()
         << " materialized=" << first.children.size());
}
