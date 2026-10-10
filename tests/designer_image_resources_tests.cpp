#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <thread>
#include <utility>

#include "designer_app.h"
#include "lumen/core/render_node.h"
#include "lumen/dsl/design_codec.h"

using lumen::designer_app::DesignerApp;

namespace {

struct ImageFixture {
    ImageFixture() {
        root = std::filesystem::temp_directory_path() /
            ("lumen-designer-image-session-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "images");
    }

    ~ImageFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write(const std::string& name) const {
        std::ofstream stream(root / "images" / name, std::ios::binary);
        stream << "LUMENRGBA\n2 2\n";
        const std::array<unsigned char, 16> pixels{
            220, 80, 60, 255, 220, 80, 60, 255,
            220, 80, 60, 255, 220, 80, 60, 255};
        stream.write(reinterpret_cast<const char*>(pixels.data()),
                     static_cast<std::streamsize>(pixels.size()));
        REQUIRE(stream.good());
    }

    std::string page(std::initializer_list<std::pair<std::string, std::string>> images,
                     std::string documentId = "image-fixture") const {
        lumen::dsl::DesignDocument document;
        document.documentId = std::move(documentId);
        document.pageName = "images";
        document.root = {1, "Column"};
        lumen::dsl::DesignNodeId next = 2;
        for (const auto& [key, source] : images) {
            lumen::dsl::DesignNode node{next++, "Image"};
            node.properties["key"] = {{key}};
            node.properties["imageSource"] = {{source}};
            node.properties["width"] = {{80.0}};
            node.properties["height"] = {{40.0}};
            document.root.children.push_back(std::move(node));
        }
        const auto filename = (root / "images.design").string();
        std::ofstream stream(filename);
        stream << lumen::dsl::serializeDesignDocument(document);
        REQUIRE(stream.good());
        return filename;
    }

    std::filesystem::path root;
};

void render(DesignerApp& app) {
    app.shell().markDirty();
    (void)app.shell().renderFrame();
}

void pumpUntil(DesignerApp& app, std::uint64_t completed) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    const auto finished = [&] {
        const auto& diagnostics = app.resourceManager()->diagnostics();
        return diagnostics.loads + diagnostics.failures + diagnostics.staleCompletions;
    };
    while (finished() < completed && std::chrono::steady_clock::now() < deadline) {
        (void)app.resourceManager()->pumpCompletions();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(finished() >= completed);
    render(app);
}

std::uint64_t imageId(DesignerApp& app, const std::string& key) {
    const auto* node = lumen::core::findNodeByKey(app.shell().root(), key);
    REQUIRE(node != nullptr);
    return node->imageId;
}

void checkImagePixel(lumen::app::AppShell& shell, const std::string& key) {
    const auto* node = lumen::core::findNodeByKey(shell.root(), key);
    REQUIRE(node != nullptr);
    const auto origin = lumen::core::absoluteOffset(shell.root(), key);
    const int x = static_cast<int>(origin.x + node->size.width * 0.5F);
    const int y = static_cast<int>(origin.y + node->size.height * 0.5F);
    const auto& pixels = shell.pixels();
    REQUIRE(x >= 0);
    REQUIRE(y >= 0);
    REQUIRE(x < pixels.width);
    REQUIRE(y < pixels.height);
    const auto offset = (static_cast<std::size_t>(y) * pixels.width + x) * 4;
    CHECK(pixels.rgba[offset] == 220);
    CHECK(pixels.rgba[offset + 1] == 80);
    CHECK(pixels.rgba[offset + 2] == 60);
    CHECK(pixels.rgba[offset + 3] == 255);
}

}  // namespace

// G-D16 / prerequisites §4.17: authorizer aliases, load failures and sessions.
TEST_CASE("designer image aliases inside an authorized root render in both previews",
          "[designer][resource][app][image-session]") {
    ImageFixture fixture;
    fixture.write("hero.lumenrgba");
    std::string aliasSource = "PROJECT://images/./hero.lumenrgba";
    SECTION("lexical aliases render without symlink privileges") {}
    SECTION("an authorized symlink shares the canonical image") {
        std::error_code error;
        std::filesystem::create_symlink(fixture.root / "images" / "hero.lumenrgba",
                                        fixture.root / "images" / "alias.lumenrgba", error);
        if (error) {
            SUCCEED("symlinks are unavailable on this platform");
            return;
        }
        aliasSource = "PROJECT://images/alias.lumenrgba";
    }
    DesignerApp app;
    app.attach();
    app.setResourceRoot(fixture.root);
    app.shell().setView({1280.0F, 800.0F});
    REQUIRE(app.loadDesignFile(fixture.page({
        {"alias", aliasSource},
        {"real", "project://images/hero.lumenrgba"}})));
    const auto before = lumen::dsl::serializeDesignDocument(*app.workbench().document());
    render(app);
    CHECK(app.resourceManager()->diagnostics().requested == 1);
    pumpUntil(app, 1);
    CHECK(imageId(app, "alias") != 0);
    CHECK(imageId(app, "alias") == imageId(app, "real"));
    checkImagePixel(app.shell(), "alias");
    checkImagePixel(app.shell(), "real");
    CHECK(app.diagnostics().empty());
    app.shell().handlers().at("designer:run")();
    pumpUntil(app, app.resourceManager()->diagnostics().requested);
    app.previewShell().setView({960.0F, 640.0F});
    (void)app.previewShell().renderFrame();
    const auto* alias = lumen::core::findNodeByKey(app.previewShell().root(), "alias");
    const auto* real = lumen::core::findNodeByKey(app.previewShell().root(), "real");
    REQUIRE(alias != nullptr);
    REQUIRE(real != nullptr);
    CHECK(alias->imageId != 0);
    CHECK(alias->imageId == real->imageId);
    checkImagePixel(app.previewShell(), "alias");
    checkImagePixel(app.previewShell(), "real");
    CHECK(lumen::dsl::serializeDesignDocument(*app.workbench().document()) == before);
    CHECK_FALSE(app.workbench().dirty());
}

TEST_CASE("designer image load failures have stable node diagnostics and placeholders",
          "[designer][resource][app][image-session]") {
    ImageFixture fixture;
    std::ofstream(fixture.root / "images" / "broken.lumenrgba") << "broken image";
    for (const std::string name : {"missing.lumenrgba", "broken.lumenrgba"}) {
        INFO("image " << name);
        DesignerApp app;
        app.attach();
        app.setResourceRoot(fixture.root);
        app.shell().setView({1280.0F, 800.0F});
        const std::string source = "project://images/" + name;
        const auto filename = fixture.page({{"first", source}, {"second", source}});
        REQUIRE(app.loadDesignFile(filename));
        const auto before = *app.workbench().document();
        const auto revision = app.workbench().documentRevision();
        render(app);
        pumpUntil(app, 1);
        CHECK(app.resourceManager()->diagnostics().requested == 1);
        CHECK(imageId(app, "first") == 0);
        CHECK(imageId(app, "second") == 0);
        REQUIRE(app.diagnostics().size() == 2);
        for (std::size_t index = 0; index != 2; ++index) {
            const auto& diagnostic = app.diagnostics()[index];
            CHECK(diagnostic.code == "resource.load_failed");
            CHECK(diagnostic.stage == lumen::dsl::DesignDiagnosticStage::Reference);
            CHECK(diagnostic.recoverability == lumen::dsl::DesignDiagnosticRecoverability::Placeholder);
            CHECK(diagnostic.file == filename);
            CHECK(diagnostic.documentId == before.documentId);
            CHECK(diagnostic.nodeId == before.root.children[index].id);
            CHECK(diagnostic.property == "imageSource");
            CHECK(diagnostic.nodePath == "root.children[" + std::to_string(index) + "]");
            CHECK(diagnostic.occurrences == 1);
        }
        const auto output = lumen::dsl::serializeDesignDiagnostics(app.diagnostics());
        CHECK(output.find((fixture.root / "images" / name).string()) == std::string::npos);
        render(app);
        CHECK(lumen::dsl::serializeDesignDiagnostics(app.diagnostics()) == output);
        CHECK(app.workbench().document() == before);
        CHECK(app.workbench().documentRevision() == revision);
        CHECK_FALSE(app.workbench().dirty());
    }
}

TEST_CASE("designer image requests belong to a live document compilation",
          "[designer][resource][app][image-session]") {
    ImageFixture fixture;
    fixture.write("hero.lumenrgba");
    DesignerApp app;
    app.attach();
    app.setResourceRoot(fixture.root);
    app.shell().setView({1280.0F, 800.0F});
    REQUIRE(app.loadDesignFile(fixture.page({
        {"hero", "project://images/hero.lumenrgba"}})));
    render(app);
    const auto before = *app.workbench().document();
    const auto session = app.workbench().frame().session();
    REQUIRE(session != nullptr);
    CHECK(app.resourceManager()->diagnostics().requested == 1);
    SECTION("a later compilation replaces the outstanding request") {
        REQUIRE(app.workbench().refresh());
        CHECK_FALSE(session->active());
        render(app);
        CHECK(app.resourceManager()->diagnostics().requested == 2);
        pumpUntil(app, 2);
        CHECK(app.resourceManager()->diagnostics().staleCompletions == 1);
        CHECK(app.resourceManager()->diagnostics().loads == 1);
        CHECK(imageId(app, "hero") != 0);
    }
    SECTION("closing the runtime session cancels outstanding work") {
        session->close();
        CHECK(app.resourceManager()->liveCount() == 0);
        render(app);
        CHECK(app.resourceManager()->diagnostics().requested == 1);
        pumpUntil(app, 1);
        CHECK(app.resourceManager()->diagnostics().staleCompletions == 1);
        CHECK(app.resourceManager()->diagnostics().loads == 0);
        CHECK(imageId(app, "hero") == 0);
    }
    SECTION("a different document cannot receive an old completion") {
        REQUIRE(app.loadDesignFile(fixture.page({
            {"hero", "project://images/hero.lumenrgba"}}, "replacement-image")));
        CHECK_FALSE(session->active());
        CHECK(app.resourceManager()->diagnostics().requested == 2);
        pumpUntil(app, 2);
        CHECK(app.resourceManager()->diagnostics().staleCompletions == 1);
        CHECK(app.resourceManager()->diagnostics().loads == 1);
        CHECK(imageId(app, "hero") != 0);
        CHECK(app.workbench().document()->documentId == "replacement-image");
        CHECK_FALSE(app.workbench().dirty());
        return;
    }
    CHECK(app.workbench().document() == before);
    CHECK_FALSE(app.workbench().dirty());
}

TEST_CASE("designer ready image reuse respects root revocation and restores without document edits",
          "[designer][resource][app][image-session]") {
    ImageFixture fixture;
    fixture.write("hero.lumenrgba");
    DesignerApp app;
    app.attach();
    app.setResourceRoot(fixture.root);
    app.shell().setView({1280.0F, 800.0F});
    REQUIRE(app.loadDesignFile(fixture.page({
        {"hero", "project://images/hero.lumenrgba"}})));
    const auto before = *app.workbench().document();
    pumpUntil(app, 1);
    const auto ready = imageId(app, "hero");
    REQUIRE(ready != 0);
    REQUIRE(app.workbench().refresh());
    render(app);
    CHECK(app.resourceManager()->diagnostics().requested == 1);
    CHECK(imageId(app, "hero") == ready);
    app.setResourceRoot({});
    render(app);
    CHECK(imageId(app, "hero") == 0);
    REQUIRE(app.diagnostics().size() == 1);
    CHECK(app.diagnostics().front().code == "resource.scheme_denied");
    CHECK(app.resourceManager()->liveCount() == 0);
    app.setResourceRoot(fixture.root);
    pumpUntil(app, 2);
    CHECK(imageId(app, "hero") != 0);
    CHECK(imageId(app, "hero") != ready);
    CHECK(app.diagnostics().empty());
    CHECK(app.workbench().document() == before);
    CHECK_FALSE(app.workbench().dirty());
    CHECK_FALSE(app.workbench().canUndo());
}

TEST_CASE("designer destruction cancels late image work without retaining its context",
          "[designer][resource][app][image-session]") {
    bool readyBeforeClose = false;
    SECTION("outstanding work is cancelled") {}
    SECTION("ready pixels are released") { readyBeforeClose = true; }
    ImageFixture fixture;
    fixture.write("hero.lumenrgba");
    std::shared_ptr<lumen::render::ResourceManager> manager;
    {
        DesignerApp app;
        app.attach();
        app.setResourceRoot(fixture.root);
        REQUIRE(app.loadDesignFile(fixture.page({
            {"hero", "project://images/hero.lumenrgba"}})));
        manager = app.resourceManager();
        CHECK(manager->diagnostics().requested == 1);
        if (readyBeforeClose) {
            pumpUntil(app, 1);
            CHECK(manager->liveCount() == 1);
        }
    }
    CHECK(manager->liveCount() == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!readyBeforeClose && manager->diagnostics().staleCompletions == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        (void)manager->pumpCompletions();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    CHECK(manager->diagnostics().staleCompletions == (readyBeforeClose ? 0 : 1));
    CHECK(manager->diagnostics().loads == (readyBeforeClose ? 1 : 0));
    CHECK(manager->liveCount() == 0);
}
