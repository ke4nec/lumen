#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "designer_app.h"
#include "file_watcher.h"
#include "lumen/accessibility/bridge.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/render_node.h"
#include "lumen/core/scrollbar.h"
#include "stb_image_write.h"
#include "lumen/core/splitter.h"
#include "lumen/core/virtual_list.h"
#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_schema.h"
#include "lumen/dsl/project_store.h"
#include "lumen/style/state.h"
#include "lumen/style/theme.h"

using lumen::accessibility::kActionActivate;
using lumen::accessibility::kActionFocus;
using lumen::accessibility::kActionSetValue;
using lumen::accessibility::SemanticsRole;
using lumen::core::Key;
using lumen::core::Offset;
using lumen::core::Size;
using lumen::core::absoluteOffset;
using lumen::core::findNodeByKey;
using lumen::core::scrollbarGeometry;
using lumen::designer_app::DesignerApp;
using lumen::designer_app::FileWatcher;

void writeRawRgba(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "LUMENRGBA\n2 2\n";
    const std::array<unsigned char, 16> pixels{
        220, 80, 60, 255, 220, 80, 60, 255,
        220, 80, 60, 255, 220, 80, 60, 255};
    file.write(reinterpret_cast<const char*>(pixels.data()),
               static_cast<std::streamsize>(pixels.size()));
}


// B 画布优先：工具箱行在 Toolbox 视图（默认 Toolbox）。需要工具箱
// 按钮的用例在首次渲染后调用此助手确保视图（leftPanelTab_ 为成员态，
// 后续重建保持该视图；默认已是 Toolbox，调用为幂等）。
void showLeftToolbox(lumen::designer_app::DesignerApp& app) {
    app.shell().handlers().at("designer:lefttab-toolbox")();
    (void)app.shell().renderFrame();
}

// 网格点阵存在性探针：点 key 为 col:row 网格坐标，(0,0) 点恒落在标尺
// 条下（遮挡剔除后不再物化），故不断言固定 key，只确认可见区有点。
bool hasCanvasGridDot(lumen::designer_app::DesignerApp& app) {
    const auto* stack = findNodeByKey(app.shell().root(),
                                      "designer-canvas-stack");
    if (stack == nullptr) return false;
    for (const auto& child : stack->children) {
        if (child.key.rfind("designer-canvas-grid-dot:", 0) == 0) return true;
    }
    return false;
}

const lumen::dsl::DesignNode* findDesignNodeById(
    const lumen::dsl::DesignNode& node, lumen::dsl::DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findDesignNodeById(child, id);
            found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findDesignNodeById(child, id);
                found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

const lumen::dsl::DesignPreviewOutlineNode* findOutlineNodeById(
    const lumen::dsl::DesignPreviewOutlineNode& node,
    lumen::dsl::DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findOutlineNodeById(child, id);
            found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

TEST_CASE("designer app loads authorized image resources into both previews",
          "[designer][f6][resource][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-image-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root / "images", error);
    REQUIRE_FALSE(error);
    writeRawRgba(root / "images" / "hero.lumenrgba");

    DesignerApp app;
    app.setResourceRoot(root);
    app.attach();
    lumen::dsl::DesignDocument document;
    document.documentId = "image-preview";
    document.pageName = "image";
    document.root = lumen::dsl::DesignNode{1, "Column"};
    lumen::dsl::DesignNode image{2, "Image"};
    image.properties["key"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{std::string{"hero"}}};
    image.properties["imageSource"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{
            std::string{"project://images/./hero.lumenrgba"}}};
    image.properties["width"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{80.0}};
    image.properties["height"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{40.0}};
    document.root.children.push_back(std::move(image));
    REQUIRE(app.workbench().openDocument(std::move(document)));

    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    const auto* placeholder = findNodeByKey(app.shell().root(), "hero");
    REQUIRE(placeholder != nullptr);
    CHECK(placeholder->imageId == 0);
    CHECK(app.resourceManager()->diagnostics().requested == 1);
    CHECK(app.diagnostics().empty());

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline &&
           app.resourceManager()->diagnostics().loads == 0) {
        (void)app.resourceManager()->pumpCompletions();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    REQUIRE(app.resourceManager()->diagnostics().loads == 1);
    app.shell().markDirty();
    (void)app.shell().renderFrame();
    const auto* loaded = findNodeByKey(app.shell().root(), "hero");
    REQUIRE(loaded != nullptr);
    CHECK(loaded->imageId != 0);
    app.shell().handlers().at("designer:run")();
    app.previewShell().setView(Size{960.0F, 640.0F});
    (void)app.previewShell().renderFrame();
    const auto* previewLoaded =
        findNodeByKey(app.previewShell().root(), "hero");
    REQUIRE(previewLoaded != nullptr);
    CHECK(previewLoaded->imageId != 0);

    std::filesystem::remove_all(root, error);
}

TEST_CASE("designer app reports denied image resources without requesting files",
          "[designer][f6][resource][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-denied-image-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);

    DesignerApp app;
    app.setResourceRoot(root);
    app.attach();
    lumen::dsl::DesignDocument document;
    document.documentId = "denied-image";
    document.pageName = "image";
    document.root = lumen::dsl::DesignNode{1, "Image"};
    document.root.properties["key"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{std::string{"denied"}}};
    document.root.properties["imageSource"] = lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{
            std::string{"https://example.test/hero.png"}}};
    REQUIRE(app.workbench().openDocument(std::move(document)));
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    REQUIRE(app.diagnostics().size() == 1);
    CHECK(app.diagnostics().front().code == "resource.scheme_denied");
    CHECK(app.resourceManager()->diagnostics().requested == 0);
    const auto* denied = findNodeByKey(app.shell().root(), "denied");
    REQUIRE(denied != nullptr);
    CHECK(denied->imageId == 0);

    std::filesystem::remove_all(root, error);
}

TEST_CASE("designer app exposes the D2 shell and semantic controls",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto& root = app.shell().root();
    // B 画布优先外壳（designer-ide.html variant-b）：标题 44 + 主工具 48 +
    // 活动栏 48 + 左右分栏 + 中心 + 状态 28。
    REQUIRE(findNodeByKey(root, "designer-titlebar") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-main-tools") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-rail") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-outline-panel") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-canvas") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-properties-panel") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-diagnostics") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-statusbar") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-document-tabs") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-canvas-controls") != nullptr);
    REQUIRE(findNodeByKey(root, "designer-crumb-bar") != nullptr);
    // B 默认 Toolbox（画布优先先给插入入口），切换后大纲可达。
    REQUIRE(findNodeByKey(root, "designer-toolbox:Container") != nullptr);
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-outline-view") !=
            nullptr);

    const auto semantics = app.shell().buildSemanticsSnapshot();
    bool hasButton = false;
    for (const auto& [id, node] : semantics.nodes) {
        (void)id;
        if (node.role == SemanticsRole::Button && !node.label.empty()) {
            hasButton = true;
            break;
        }
    }
    CHECK(hasButton);
    REQUIRE(app.workbench().selection().primary.has_value());
}

TEST_CASE("designer toolbar groups commands and fits the viewport",
          "[designer][d2][app]") {
    // B 画布优先主工具 48（design/designer-ide.html?variant=b）：保存/撤销/
    // 重做 | 选择/插入/布局 | 提示 | 专注/调试/停止/运行；文件低频保留图标件；
    // 画布工具（主题/密度/缩放/网格）下沉 canvas-controls；1280 内不裁剪。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // 可选取证：默认（Toolbox 视图，B 画布优先）帧导出，与文末 Toolbox
    // 视图导出（LUMEN_TOOLBAR_PNG）互补，供 design/ mockup 并排比对。
    if (const char* leftPath = std::getenv("LUMEN_LEFT_PNG")) {
        const auto& pixels = app.shell().pixels();
        std::printf("left png %s (%dx%d)\n", leftPath, pixels.width,
                    pixels.height);
        if (pixels.width > 0 && pixels.height > 0) {
            (void)stbi_write_png(leftPath, pixels.width, pixels.height, 4,
                                 pixels.rgba.data(),
                                 static_cast<int>(pixels.width) * 4);
        }
    }
    // 可选取证：全布局元素盒导出（与 design/ mockup 逐元素比对的实现侧
    // 数据源；坐标为视口绝对值）。LAYOUT <key> x y w h。
    if (std::getenv("LUMEN_LAYOUT_DUMP") != nullptr) {
        const std::vector<std::string> wanted = {
            "designer-titlebar",       "designer-main-tools",
            "designer-toolbar-row",    "designer-rail",
            "designer-tab-outline",    "designer-tab-toolbox",
            "designer-outline-view",   "designer-project-status",
            "designer-project-new",    "designer-project-open",
            "designer-canvas",         "designer-preview-canvas",
            "designer-document-tabs",  "designer-canvas-controls",
            "designer-crumb-bar",      "designer-center-tabs",
            "designer-properties-panel", "designer-diagnostics",
            "designer-statusbar",      "designer-workspace"};
        for (const auto& key : wanted) {
            const auto* node = findNodeByKey(app.shell().root(), key);
            if (node == nullptr) continue;
            std::printf("LAYOUT %s %.1f %.1f %.1f %.1f\n", key.c_str(),
                        lumen::core::absoluteOffset(app.shell().root(), key).x,
                        lumen::core::absoluteOffset(app.shell().root(), key).y,
                        node->size.width, node->size.height);
        }
        // 工具栏 22 枚按钮 + 大纲树行 + 工具箱行（Toolbox 视图另测）。
        std::function<void(const lumen::core::RenderNode&, float, float)> boxes =
            [&](const lumen::core::RenderNode& node, float x, float y) {
                const float nx = x + node.offset.x;
                const float ny = y + node.offset.y;
                const bool isToolbarButton =
                    node.type == lumen::core::WidgetType::Button &&
                    node.key.rfind("designer-", 0) == 0 &&
                    node.key.rfind("designer-property", 0) != 0 &&
                    node.key.rfind("designer-reference", 0) != 0 &&
                    node.key.rfind("designer-outline:item", 0) != 0 &&
                    node.key.rfind("designer-tip:", 0) != 0 &&
                    node.key.rfind("designer-diagnostic", 0) != 0 &&
                    node.key.rfind("designer-project-", 0) != 0;
                if (isToolbarButton) {
                    std::printf("LAYOUT %s %.1f %.1f %.1f %.1f\n",
                                node.key.c_str(), nx, ny, node.size.width,
                                node.size.height);
                }
                if (node.key.rfind("designer-outline:item:", 0) == 0) {
                    std::printf("LAYOUT %s %.1f %.1f %.1f %.1f\n",
                                node.key.c_str(), nx, ny, node.size.width,
                                node.size.height);
                }
                for (const auto& child : node.children) boxes(child, nx, ny);
            };
        boxes(app.shell().root(), 0.0F, 0.0F);
    }

    // B 左栏 224 默认 Toolbox（画布优先先给插入入口）；切到 Toolbox
    // 视图后整行列表落在面板内容盒内（整行宽 = 内容宽，无长名截断）。
    // 坐标取面板相对（子 offset 链不含面板自身 offset）。
    {
        app.shell().handlers().at("designer:lefttab-toolbox")();
        (void)app.shell().renderFrame();
        const auto* panel =
            findNodeByKey(app.shell().root(), "designer-outline-panel");
        REQUIRE(panel != nullptr);
        std::vector<std::pair<std::string, float>> toolboxButtons;
        std::function<void(const lumen::core::RenderNode&, float)> probe =
            [&](const lumen::core::RenderNode& node, float left) {
                if (node.type == lumen::core::WidgetType::Button &&
                    node.key.rfind("designer-toolbox:", 0) == 0) {
                    toolboxButtons.emplace_back(node.key,
                                                left + node.size.width);
                }
                for (const auto& child : node.children) {
                    probe(child, left + child.offset.x);
                }
            };
        for (const auto& child : panel->children) probe(child, 0.0F);
        REQUIRE(toolboxButtons.size() == 37);  // schema 注册表全量
        const float contentRight =
            panel->size.width - panel->padding.right;
        for (const auto& [key, right] : toolboxButtons) {
            INFO(key << " right=" << right);
            CHECK(right <= Catch::Approx(contentRight).margin(0.5F));
        }
    }

    const auto* toolbar =
        findNodeByKey(app.shell().root(), "designer-main-tools");
    REQUIRE(toolbar != nullptr);
    CHECK(toolbar->size.width == Catch::Approx(1280.0F));
    CHECK(toolbar->size.height == Catch::Approx(48.0F));

    struct ToolRow {
        std::string key;
        std::string label;
        float right;
        float width;
        float fontSize;
        lumen::core::Color background;
    };
    std::vector<ToolRow> rows;
    std::size_t separators = 0;
    std::function<void(const lumen::core::RenderNode&, float, float)> walk =
        [&](const lumen::core::RenderNode& node, float originX,
            float originY) {
            const float x = originX + node.offset.x;
            const float y = originY + node.offset.y;
            if (node.type == lumen::core::WidgetType::Button) {
                const auto* button =
                    std::get_if<lumen::core::ButtonResolvedStyle>(
                        &node.style.component);
                rows.push_back(ToolRow{
                    node.key,
                    node.semanticsLabel.empty() ? node.text
                                                : node.semanticsLabel,
                    x + node.size.width, node.size.width,
                    button != nullptr ? button->common.text.fontSize : 0.0F,
                    button != nullptr
                        ? button->common.background
                        : lumen::core::Color::transparent()});
            }
            if (node.key.rfind("designer-main-tools-separator", 0) == 0)
                ++separators;
            for (const auto& child : node.children) walk(child, x, y);
        };
    walk(*toolbar, 0.0F, 0.0F);

    const float contentRight = toolbar->size.width - toolbar->padding.right;
    float maxRight = 0.0F;
    for (const auto& row : rows) {
        WARN("toolbar " << row.key << " label='" << row.label << "' right="
                        << row.right << " width=" << row.width
                        << " font=" << row.fontSize);
        maxRight = std::max(maxRight, row.right);
    }
    // 运行组完整可见：右缘不越过内容盒（右 padding 16）。
    CHECK(maxRight <= Catch::Approx(contentRight).margin(0.5F));
    // B 主工具两条分隔线：文件|选择/插入/布局|提示（workspace-design §3）。
    CHECK(separators == 2);
    // Run 是唯一 Filled（accent 底）；其余 chrome 件 rest 透明。
    const auto& theme = app.shell().theme();
    std::size_t filled = 0;
    for (const auto& row : rows) {
        if (row.background == theme.colors.accent) ++filled;
    }
    CHECK(filled == 1);
    const auto* runButton =
        findNodeByKey(app.shell().root(), "designer-run");
    REQUIRE(runButton != nullptr);
    const auto* runStyle = std::get_if<lumen::core::ButtonResolvedStyle>(
        &runButton->style.component);
    REQUIRE(runStyle != nullptr);
    CHECK(runStyle->common.background == theme.colors.accent);
    CHECK(runButton->text == "运行预览");

    // M11 tooltip：主工具 tip 常驻 Stack 并完成注册（图标钮/紧凑档的
    // 文字事实来源；画布工具 tip 在 canvas-controls 另测）。
    for (const auto* key : {"designer-tip:designer-run",
                            "designer-tip:designer-save",
                            "designer-tip:designer-undo"}) {
        const auto* tip = findNodeByKey(app.shell().root(), key);
        INFO(key);
        REQUIRE(tip != nullptr);
        CHECK(tip->type == lumen::core::WidgetType::Tooltip);
    }

    // 字号放大（125%）：带文字命令退化为图标钮，工具栏仍不越内容盒。
    app.shell().handlers().at("designer:font-scale")();
    (void)app.shell().renderFrame();
    const auto* compactRun =
        findNodeByKey(app.shell().root(), "designer-run");
    REQUIRE(compactRun != nullptr);
    CHECK(compactRun->text.empty());
    CHECK(compactRun->semanticsLabel == "Run");
    float compactMaxRight = 0.0F;
    std::function<void(const lumen::core::RenderNode&, float)> reprobe =
        [&](const lumen::core::RenderNode& node, float left) {
            const float x = left + node.offset.x;
            if (node.type == lumen::core::WidgetType::Button) {
                compactMaxRight =
                    std::max(compactMaxRight, x + node.size.width);
            }
            for (const auto& child : node.children) reprobe(child, x);
        };
    if (const auto* bar =
            findNodeByKey(app.shell().root(), "designer-main-tools")) {
        reprobe(*bar, 0.0F);
        CHECK(compactMaxRight <=
              Catch::Approx(bar->size.width - bar->padding.right).margin(
                  0.5F));
    }

    // 工具箱行无尾随装饰：拖放按整行认领（onClick 前缀），组头不带
    // 数量、条目不带拖拽点（§8.4 用户裁定移除）。
    if (const auto* toolRow = findNodeByKey(app.shell().root(),
                                            "designer-toolbox:Container")) {
        CHECK(toolRow->icon ==
              static_cast<std::uint8_t>(lumen::core::IconId::None));
    }
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-toolbox-group-count:Layout") == nullptr);

    // 可选视觉取证：LUMEN_TOOLBAR_PNG=path.png 时导出当前帧，供与
    // design/ mockup 并排比对（不设则零开销，不参与断言）。
    if (const char* dumpPath = std::getenv("LUMEN_TOOLBAR_PNG")) {
        const auto& pixels = app.shell().pixels();
        std::printf("toolbar png %s (%dx%d)\n", dumpPath, pixels.width,
                    pixels.height);
        if (pixels.width > 0 && pixels.height > 0) {
            // 帧缓冲为 BGRA 的话在此转换；CpuRenderer 输出 RGBA。
            (void)stbi_write_png(dumpPath, pixels.width, pixels.height, 4,
                                 pixels.rgba.data(),
                                 static_cast<int>(pixels.width) * 4);
        }
    }
}


TEST_CASE("designer toolbox groups cover the schema registry",
          "[designer][d2][app]") {
    // 左面板版本 C：分类法是应用层展示映射——7 组顺序、组计数与
    // schema 注册表互相覆盖（每个类型恰好出现一次，无遗漏无杜撰）。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    app.shell().handlers().at("designer:lefttab-toolbox")();
    (void)app.shell().renderFrame();

    std::vector<std::string> groupLabels;
    std::vector<std::string> typeOrder;
    std::function<void(const lumen::core::RenderNode&)> walk =
        [&](const lumen::core::RenderNode& node) {
            if (node.key.rfind("designer-toolbox-group-label:", 0) == 0) {
                groupLabels.push_back(node.text);
            }
            if (node.type == lumen::core::WidgetType::Button &&
                node.key.rfind("designer-toolbox:", 0) == 0) {
                typeOrder.push_back(node.text);
            }
            for (const auto& child : node.children) walk(child);
        };
    walk(app.shell().root());

    const std::vector<std::string> expectedGroups = {
        "Layout", "Content", "Input", "Collection", "Feedback", "Component",
        "Scope"};
    REQUIRE(groupLabels == expectedGroups);

    // 注册表全量恰好一次。
    std::set<std::string> seen(typeOrder.begin(), typeOrder.end());
    REQUIRE(seen.size() == typeOrder.size());
    for (const auto& schema : lumen::dsl::nodeSchemaRegistry()) {
        INFO(schema.type);
        CHECK(seen.contains(schema.type));
    }
    CHECK(typeOrder.size() == 37);
}

TEST_CASE("designer property fields expose stable semantics and set values",
          "[designer][d3][app][a11y]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Text(\"Original\", key: \"title\") }",
        "a11y-property.lumen"));
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    const auto nodeId = app.workbench().document()->root.id;
    const std::string fieldKey =
        "designer-property-field:" + std::to_string(nodeId) + ":text";
    const auto* field = findNodeByKey(app.shell().root(), fieldKey);
    REQUIRE(field != nullptr);
    const std::string fieldIdentity = field->identity;
    const auto before = app.shell().buildSemanticsSnapshot();
    const auto* beforeNode = before.find(fieldIdentity);
    REQUIRE(beforeNode != nullptr);
    CHECK(beforeNode->role == SemanticsRole::TextField);
    CHECK(beforeNode->label == "text");
    CHECK(beforeNode->value == "Original");

    CHECK(app.shell().performAccessibilityAction(fieldIdentity, kActionFocus) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    CHECK(app.shell().performAccessibilityAction(fieldIdentity, kActionSetValue,
                                                 "Changed") ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();
    const auto after = app.shell().buildSemanticsSnapshot();
    const auto* afterNode = after.find(fieldIdentity);
    REQUIRE(afterNode != nullptr);
    CHECK(afterNode->label == "text");
    CHECK(afterNode->value == "Changed");
    CHECK(app.workbench().dirty());
    REQUIRE(app.undo());
    CHECK_FALSE(app.workbench().dirty());
    (void)app.shell().renderFrame();
    const auto restored = app.shell().buildSemanticsSnapshot();
    REQUIRE(restored.find(fieldIdentity) != nullptr);
    CHECK(restored.find(fieldIdentity)->label == "text");
    CHECK(restored.find(fieldIdentity)->value == "Original");
}

TEST_CASE("designer app selects preview nodes and keeps the last frame on errors",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    // 默认画板 1024×768 触发 fit-to-view：首帧建立画布节点、次帧完成
    // 缩放收敛，之后的几何取点才与交互树一致。
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto* preview = findNodeByKey(app.shell().root(), "title");
    REQUIRE(preview != nullptr);
    // 期望值取被点节点自身（默认样例 §8.1 换为画板结构后不再假设
    // children.front() 即 title）。
    REQUIRE(preview->onClick.rfind("designer:select:", 0) == 0);
    const auto expected = static_cast<lumen::dsl::DesignNodeId>(
        std::strtoull(preview->onClick.c_str() + 16, nullptr, 10));
    const auto origin = absoluteOffset(app.shell().root(), "title");
    const Offset center = origin +
                          Offset{preview->size.width * 0.5F,
                                 preview->size.height * 0.5F};
    app.shell().pointerDown(center);
    app.shell().pointerUp(center);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(*app.workbench().selection().primary == expected);

    const auto generation = app.workbench().frame().generation();
    CHECK_FALSE(app.loadSource("page broken {", "broken.lumen"));
    CHECK(app.workbench().frame().hasFrame());
    CHECK(app.workbench().frame().generation() == generation);
    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().file == "broken.lumen");
}

TEST_CASE("designer app keeps keyboard and semantic activation on one path",
          "[designer][d2][app][a11y]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto* density = findNodeByKey(app.shell().root(), "designer-density");
    REQUIRE(density != nullptr);
    const auto beforeDensity = app.shell().theme().metrics.density;
    CHECK(app.shell().performAccessibilityAction(
              density->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    CHECK(app.shell().theme().metrics.density != beforeDensity);

    app.shell().focus().clearFocus();
    // B 焦点顺序（workspace-design §4.4）：标题栏 → 主工具（Stop 停止态禁用
    // 跳过；缩放组 excludeFromFocus 不参与）→ 活动栏 → 左栏 → 中心 → 右栏。
    // 此处按子序列断言（允许标题栏等新增停靠点穿插），保证命令可达而非
    // 逐键硬编码。
    const std::vector<std::string> toolbarKeys = {
        "designer-menu-file",  "designer-menu-edit",  "designer-menu-view",
        "designer-save",       "designer-open",       "designer-save-as",
        "designer-new-project", "designer-undo",      "designer-redo",
        "designer-insert",     "designer-layout",     "designer-zen",
        "designer-debug",      "designer-run"};
    auto expectSubsequence = [&](const std::vector<std::string>& keys) {
        for (const auto& key : keys) {
            bool found = false;
            for (int step = 0; step < 80; ++step) {
                app.shell().keyDown(Key::Tab);
                if (app.shell().focus().focusedKey() == key) {
                    found = true;
                    break;
                }
            }
            INFO(key);
            CHECK(found);
        }
    };
    expectSubsequence(toolbarKeys);
    // B 默认 Toolbox：主工具之后经活动栏到达左栏页签（控件/结构），
    // 工具箱行在焦点环内；大纲行需切到结构页。
    expectSubsequence({"designer-rail-toolbox", "designer-rail-outline",
                       "designer-tab-toolbox", "designer-tab-outline"});
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();
    app.shell().focus().clearFocus();
    expectSubsequence({"designer-tab-toolbox", "designer-tab-outline"});
    // 结构页内大纲树可达。
    {
        bool found = false;
        for (int step = 0; step < 120; ++step) {
            app.shell().keyDown(Key::Tab);
            if (app.shell().focus().focusedKey().starts_with(
                    "designer-outline:item:")) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    app.shell().keyDown(Key::Down);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(*app.workbench().selection().primary == outline->children.front().id);

    // 切到 Toolbox 视图后，焦点顺序按 7 组分类法（designerToolboxGroups）
    // 逐行推进。
    app.shell().handlers().at("designer:lefttab-toolbox")();
    (void)app.shell().renderFrame();
    app.shell().focus().clearFocus();
    // clearFocus 后遍历从树首重新开始：先过标题栏与主工具，再进左面板。
    expectSubsequence(toolbarKeys);
    const std::vector<std::string> toolboxKeys = {
        "designer-rail-toolbox",  "designer-rail-outline",
        "designer-tab-toolbox",   "designer-tab-outline",
        "designer-toolbox:Container", "designer-toolbox:Row",
        "designer-toolbox:Column",    "designer-toolbox:Stack",
        "designer-toolbox:Grid",      "designer-toolbox:ScrollView",
        "designer-toolbox:Splitter",  "designer-toolbox:Text",
        "designer-toolbox:Image",     "designer-toolbox:Icon",
        "designer-toolbox:Button",    "designer-toolbox:TextField",
        "designer-toolbox:Checkbox",  "designer-toolbox:Switch",
        "designer-toolbox:Radio",     "designer-toolbox:Slider",
        "designer-toolbox:Spin",      "designer-toolbox:ComboBox",
        "designer-toolbox:ColorPicker", "designer-toolbox:Dropdown",
        "designer-toolbox:ListView",  "designer-toolbox:VirtualList",
        "designer-toolbox:List",      "designer-toolbox:Tree",
        "designer-toolbox:TreeList",  "designer-toolbox:DataGrid",
        "designer-toolbox:Tabs",      "designer-toolbox:ProgressBar",
        "designer-toolbox:Tooltip",   "designer-toolbox:Menu",
        "designer-toolbox:ToolBar",   "designer-toolbox:StatusBar",
        "designer-toolbox:DialogHost", "designer-toolbox:Navigator",
        "designer-toolbox:Form",      "designer-toolbox:FocusScope",
        "designer-toolbox:ThemeScope",
        "designer-project-new",       "designer-project-open"};
    expectSubsequence(toolboxKeys);
}

TEST_CASE("designer app mirrors extended outline selection into the canvas",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，大纲操作前切到结构页（workspace-design §3）。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() >= 3);
    const auto firstId = outline->children[0].id;
    const auto secondId = outline->children[1].id;
    const auto thirdId = outline->children[2].id;
    const auto* firstRow = findNodeByKey(
        app.shell().root(),
        "designer-outline:item:" + outline->children[0].path);
    const auto* thirdRow = findNodeByKey(
        app.shell().root(),
        "designer-outline:item:" + outline->children[2].path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(thirdRow != nullptr);

    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto thirdOrigin = absoluteOffset(app.shell().root(), thirdRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset thirdPoint =
        thirdOrigin + Offset{thirdRow->size.width * 0.5F,
                             thirdRow->size.height * 0.5F};

    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId});

    app.shell().pointerDown(thirdPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(thirdPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == thirdId);
    CHECK(app.workbench().selection().ids.contains(firstId));
    CHECK(app.workbench().selection().ids.contains(secondId));
    CHECK(app.workbench().selection().ids.contains(thirdId));

    const auto* firstCanvas = findNodeByKey(
        app.shell().root(), outline->children[0].key);
    const auto* secondCanvas = findNodeByKey(
        app.shell().root(), outline->children[1].key);
    const auto* thirdCanvas = findNodeByKey(
        app.shell().root(), outline->children[2].key);
    REQUIRE(firstCanvas != nullptr);
    REQUIRE(secondCanvas != nullptr);
    REQUIRE(thirdCanvas != nullptr);
    CHECK(firstCanvas->selected);
    CHECK(secondCanvas->selected);
    CHECK(thirdCanvas->selected);

    const auto* secondRow = findNodeByKey(
        app.shell().root(),
        "designer-outline:item:" + outline->children[1].path);
    REQUIRE(secondRow != nullptr);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(secondPoint, lumen::core::kModifierCtrl);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.workbench().selection().ids.contains(secondId));
    CHECK_FALSE(findNodeByKey(app.shell().root(), outline->children[1].key)
                    ->selected);
}

TEST_CASE("designer app keeps selection chrome without idle alignment guides",
          "[designer][d2][app][canvas-alignment]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // The first frame establishes the canvas geometry used to place the
    // non-modal guide layer; the default-on guides then render on the
    // geometry-aware rebuild.
    (void)app.shell().renderFrame();

    REQUIRE(findNodeByKey(app.shell().root(), "designer-canvas-guides") !=
            nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-ruler-top") != nullptr);
    // 点阵存在即可：(0,0) 点在标尺条下，遮挡剔除后不再物化。
    REQUIRE(hasCanvasGridDot(app));
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-guide-frame") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-handle:nw") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-dimensions") != nullptr);
    for (const auto* key : {"designer-canvas-guide-v", "designer-canvas-guide-h",
                            "designer-canvas-guide-chip",
                            "designer-canvas-drag-dash:4:0",
                            "designer-canvas-drag-dash:5:0"}) {
        CHECK(findNodeByKey(app.shell().root(), key) == nullptr);
    }
    bool guideInSemantics = false;
    for (const auto& [id, node] : app.shell().buildSemanticsSnapshot().nodes) {
        (void)node;
        if (id.find("designer-canvas-guide-frame") != std::string::npos ||
            id.find("designer-canvas-guide-v") != std::string::npos ||
            id.find("designer-canvas-guide-h") != std::string::npos ||
            id.find("designer-canvas-handle") != std::string::npos) {
            guideInSemantics = true;
            break;
        }
    }
    CHECK_FALSE(guideInSemantics);

    // Workspace §8.10: alignment visibility is independent of selection chrome.
    app.shell().handlers().at("designer:canvas-guides")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-grid-dot:0:0") == nullptr);
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-guide-frame") != nullptr);
    app.shell().handlers().at("designer:canvas-guides")();
    (void)app.shell().renderFrame();
    CHECK(hasCanvasGridDot(app));
    app.shell().handlers().at("designer:canvas-align")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-guide-v") == nullptr);
    app.shell().handlers().at("designer:canvas-align")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-guide-v") == nullptr);
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-guide-frame") != nullptr);
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-ruler-top") != nullptr);
    CHECK(hasCanvasGridDot(app));
}

TEST_CASE("designer app frame-selects canvas siblings",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    // 自加载极小源：默认样例（§8.1 画板）节点多，框选集合不再恰好两个；
    // 本用例只验证橡皮筋与兄弟命中语义。
    REQUIRE(app.loadSource(
        "page frame { Stack(key: \"root\", width: 300, height: 200) {"
        " Text(\"First\", key: \"first\", width: 80, height: 24, left: 8,"
        " top: 8)"
        " Text(\"Second\", key: \"second\", width: 80, height: 24, left: 8,"
        " top: 96)"
        " } }",
        "frame-select.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() >= 2);
    const auto firstId = outline->children[0].id;
    const auto secondId = outline->children[1].id;
    const auto* second = findNodeByKey(app.shell().root(), "second");
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(second != nullptr);
    REQUIRE(canvas != nullptr);
    const auto canvasOrigin = absoluteOffset(app.shell().root(), canvas->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), second->key);
    const Offset start = canvasOrigin + Offset{4.0F, 4.0F};
    const Offset end = secondOrigin +
                       Offset{std::max(1.0F, second->size.width - 1.0F),
                              std::max(1.0F, second->size.height - 1.0F)};

    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    REQUIRE(app.shell().overlayRoot() != nullptr);
    REQUIRE(findNodeByKey(*app.shell().overlayRoot(),
                          "designer-canvas-selection-rect") != nullptr);
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    CHECK(app.workbench().selection().primary == secondId);
    CHECK(app.shell().overlayRoot() == nullptr);
}

TEST_CASE("designer app transforms canvas preview without editing the document",
          "[designer][d3][app][designer-transform]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"Title\", key: "
        "\"node\", width: 120, height: 30) } }",
        "transform.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const std::string nodeKey = outline->children.front().key;
    REQUIRE_FALSE(nodeKey.empty());
    const auto* before = findNodeByKey(app.shell().root(), nodeKey);
    REQUIRE(before != nullptr);
    const auto beforeSize = before->size;
    const auto beforeOrigin = absoluteOffset(app.shell().root(), nodeKey);
    const auto revision = app.workbench().documentRevision();
    const auto initialGeneration = app.workbench().frame().generation();
    CHECK_FALSE(app.workbench().dirty());

    app.shell().handlers().at("designer:zoom-in")();
    (void)app.shell().renderFrame();
    const auto* zoomed = findNodeByKey(app.shell().root(), nodeKey);
    REQUIRE(zoomed != nullptr);
    CHECK(zoomed->size.width == Catch::Approx(beforeSize.width * 1.25F));
    CHECK(zoomed->size.height == Catch::Approx(beforeSize.height * 1.25F));
    CHECK(app.workbench().documentRevision() == revision);
    CHECK_FALSE(app.workbench().dirty());
    CHECK(app.workbench().frame().generation() > initialGeneration);
    const auto zoomGeneration = app.workbench().frame().generation();

    app.shell().handlers().at("designer:pan-right")();
    (void)app.shell().renderFrame();
    const auto pannedOrigin = absoluteOffset(app.shell().root(), nodeKey);
    // §8.8 锚定缩放后节点可能已随中心锚点左移，只断言平移改变位置。
    CHECK(pannedOrigin.x != beforeOrigin.x);
    CHECK(app.workbench().documentRevision() == revision);
    CHECK_FALSE(app.workbench().dirty());
    CHECK(app.workbench().frame().generation() == zoomGeneration);

    app.shell().handlers().at("designer:zoom-reset")();
    (void)app.shell().renderFrame();
    const auto* reset = findNodeByKey(app.shell().root(), nodeKey);
    REQUIRE(reset != nullptr);
    const auto resetOrigin = absoluteOffset(app.shell().root(), nodeKey);
    CHECK(reset->size.width == Catch::Approx(beforeSize.width));
    CHECK(reset->size.height == Catch::Approx(beforeSize.height));
    CHECK(resetOrigin.x == Catch::Approx(beforeOrigin.x));
    CHECK(resetOrigin.y == Catch::Approx(beforeOrigin.y));
    CHECK(app.workbench().documentRevision() == revision);
    CHECK_FALSE(app.workbench().dirty());
    CHECK(app.workbench().frame().generation() > zoomGeneration);
}

TEST_CASE("designer app selects transformed canvas nodes across dpi and zoom",
          "[designer][d3][app][designer-transform]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"Title\", key: "
        "\"node\", width: 120, height: 30) } }",
        "transform-selection.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto nodeId = outline->children.front().id;
    const auto revision = app.workbench().documentRevision();
    const auto selectTransformedNode = [&] {
        const auto* node = findNodeByKey(app.shell().root(), "node");
        REQUIRE(node != nullptr);
        const auto origin = absoluteOffset(app.shell().root(), "node");
        const Offset center{origin.x + node->size.width * 0.5F,
                            origin.y + node->size.height * 0.5F};
        app.shell().pointerDown(center);
        app.shell().pointerUp(center);
        (void)app.shell().renderFrame();
        REQUIRE(app.workbench().selection().primary.has_value());
        CHECK(*app.workbench().selection().primary == nodeId);
        CHECK(app.workbench().selection().ids.size() == 1);
        CHECK_FALSE(app.workbench().dirty());
    };

    selectTransformedNode();
    app.shell().handlers().at("designer:dpi")();
    (void)app.shell().renderFrame();
    CHECK(app.shell().styleContext().deviceScale == 1.25F);
    app.shell().handlers().at("designer:zoom-in")();
    (void)app.shell().renderFrame();
    selectTransformedNode();

    app.shell().handlers().at("designer:dpi")();
    (void)app.shell().renderFrame();
    CHECK(app.shell().styleContext().deviceScale == 2.0F);
    app.shell().handlers().at("designer:zoom-in")();
    (void)app.shell().renderFrame();
    selectTransformedNode();
    CHECK(app.workbench().documentRevision() == revision);
}

TEST_CASE("designer app resizes a selected node with one snapped transaction",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    // 两帧完成默认画板的 fit 收敛，选中铬件再一帧跟上（缩放后选中框/
    // 手柄按上一帧几何物化，与 zoom 命令同一收敛语义）。
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    // 拖真正未声明 width 的 title 文本（默认样例 §8.1 后 front() 是自带
    // width 的容器）：撤销/取消后 width 属性消失的原语义才成立。
    const auto* titlePreview = findNodeByKey(app.shell().root(), "title");
    REQUIRE(titlePreview != nullptr);
    REQUIRE(titlePreview->onClick.rfind("designer:select:", 0) == 0);
    const auto titleId = static_cast<lumen::dsl::DesignNodeId>(
        std::strtoull(titlePreview->onClick.c_str() + 16, nullptr, 10));
    const auto select = app.shell().handlers().find(
        "designer:select:" + std::to_string(titleId));
    REQUIRE(select != app.shell().handlers().end());
    select->second();
    // 参考线默认开（workspace §8.1）：手柄随选择直接可见。
    (void)app.shell().renderFrame();

    const auto* handle =
        findNodeByKey(app.shell().root(), "designer-canvas-handle:e");
    REQUIRE(handle != nullptr);
    const auto handleOrigin = absoluteOffset(app.shell().root(), handle->key);
    const Offset start =
        handleOrigin + Offset{handle->size.width * 0.5F,
                              handle->size.height * 0.5F};
    const Offset end = start + Offset{17.0F, 0.0F};
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    const auto properties = app.workbench().properties(titleId);
    const auto width = std::find_if(
        properties.begin(), properties.end(),
        [](const auto& property) { return property.name == "width"; });
    REQUIRE(width != properties.end());
    REQUIRE(width->value.has_value());
    const auto* widthValue = std::get_if<double>(&width->value->value);
    REQUIRE(widthValue != nullptr);
    CHECK(*widthValue >= 8.0);
    CHECK(std::fmod(*widthValue, 8.0) == 0.0);
    CHECK(app.workbench().dirty());

    REQUIRE(app.undo());
    const auto restored = app.workbench().properties(titleId);
    CHECK(std::find_if(restored.begin(), restored.end(), [](const auto& property) {
              return property.name == "width";
          }) == restored.end());

    (void)app.shell().renderFrame();
    handle = findNodeByKey(app.shell().root(), "designer-canvas-handle:e");
    REQUIRE(handle != nullptr);
    const auto cancelOrigin = absoluteOffset(app.shell().root(), handle->key);
    const Offset cancelStart =
        cancelOrigin + Offset{handle->size.width * 0.5F,
                              handle->size.height * 0.5F};
    app.shell().pointerDown(cancelStart);
    app.shell().pointerMove(cancelStart + Offset{24.0F, 0.0F});
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.shell().controller().dragSessionActive());
    const auto cancelled = app.workbench().properties(titleId);
    CHECK(std::find_if(cancelled.begin(), cancelled.end(),
                       [](const auto& property) {
                           return property.name == "width";
                       }) == cancelled.end());
}

TEST_CASE("designer app resizes stack children with position in one transaction",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Stack(key: \"root\", width: 300, height: 200) { "
        "Text(\"Title\", key: \"node\", width: 40, height: 30, left: 20, "
        "top: 20) } }",
        "stack-resize.lumen"));
    (void)app.shell().renderFrame();
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto nodeId = outline->children.front().id;
    const auto select = app.shell().handlers().find(
        "designer:select:" + std::to_string(nodeId));
    REQUIRE(select != app.shell().handlers().end());
    select->second();
    // 参考线默认开（§8.1）：手柄/选中框随选择直接可见。
    (void)app.shell().renderFrame();

    const auto* handle =
        findNodeByKey(app.shell().root(), "designer-canvas-handle:nw");
    REQUIRE(handle != nullptr);
    const auto handleOrigin = absoluteOffset(app.shell().root(), handle->key);
    const Offset start =
        handleOrigin + Offset{handle->size.width * 0.5F,
                              handle->size.height * 0.5F};
    const Offset end = start + Offset{-11.0F, -11.0F};
    std::vector<const lumen::core::RenderNode*> hitChain;
    REQUIRE(lumen::core::hitTestChain(app.shell().root(), start, hitChain));
    CHECK(std::any_of(hitChain.begin(), hitChain.end(), [](const auto* node) {
        return node != nullptr && node->key == "designer-canvas-handle:nw";
    }));
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    const auto properties = app.workbench().properties(nodeId);
    const auto hasProperty = [&](std::string_view name) {
        return std::find_if(properties.begin(), properties.end(),
                            [name](const auto& property) {
                                return property.name == name &&
                                       property.value.has_value();
                            }) != properties.end();
    };
    CHECK(hasProperty("width"));
    CHECK(hasProperty("height"));
    CHECK(hasProperty("left"));
    CHECK(hasProperty("top"));
    REQUIRE(app.undo());
    const auto restored = app.workbench().properties(nodeId);
    const auto restoredValue = [&](std::string_view name) {
        const auto found = std::find_if(
            restored.begin(), restored.end(), [name](const auto& property) {
                return property.name == name;
            });
        REQUIRE(found != restored.end());
        REQUIRE(found->value.has_value());
        return std::get<double>(found->value->value);
    };
    CHECK(restoredValue("width") == 40.0);
    CHECK(restoredValue("height") == 30.0);
    CHECK(restoredValue("left") == 20.0);
    CHECK(restoredValue("top") == 20.0);
}

TEST_CASE("designer app previews theme density dpi and accessibility inputs",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    app.shell().setSystemAccessibilitySettings(
        lumen::accessibility::AccessibilitySettings{false, true, 1.0F});
    (void)app.shell().renderFrame();

    const auto activate = [&](const char* key) {
        const auto* node = findNodeByKey(app.shell().root(), key);
        REQUIRE(node != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  node->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
    };

    const bool dark = app.shell().theme().darkMode;
    const auto density = app.shell().theme().metrics.density;
    CHECK(app.shell().styleContext().deviceScale == 1.0F);
    CHECK(app.shell().accessibilitySettings().fontScale == 1.0F);
    CHECK_FALSE(app.shell().accessibilitySettings().highContrast);
    CHECK(app.shell().accessibilitySettings().reduceAnimation);

    activate("designer-theme");
    CHECK(app.shell().theme().darkMode != dark);
    activate("designer-density");
    CHECK(app.shell().theme().metrics.density != density);
    activate("designer-dpi");
    CHECK(app.shell().styleContext().deviceScale == 1.25F);
    activate("designer-font-scale");
    CHECK(app.shell().accessibilitySettings().fontScale == 1.25F);
    activate("designer-contrast");
    CHECK(app.shell().accessibilitySettings().highContrast);
    CHECK(app.shell().accessibilitySettings().reduceAnimation);
    app.shell().setSystemAccessibilitySettings(
        lumen::accessibility::AccessibilitySettings{false, false, 1.0F});
    CHECK_FALSE(app.shell().accessibilitySettings().reduceAnimation);

    (void)app.shell().renderFrame();
    // B 画布工具为图标钮：当前值进 semanticsLabel（无障碍同源；中文标签
    // 对齐 designer-ide.html variant-b）。
    CHECK(findNodeByKey(app.shell().root(), "designer-dpi")->semanticsLabel ==
          "DPI 125%");
    CHECK(findNodeByKey(app.shell().root(), "designer-font-scale")
              ->semanticsLabel == "字号 125%");
    CHECK(findNodeByKey(app.shell().root(), "designer-contrast")
              ->semanticsLabel == "对比开");
}

TEST_CASE("designer app runs and stops the current preview session",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto activate = [&](const char* key) {
        const auto* button = findNodeByKey(app.shell().root(), key);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };

    activate("designer-run");
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Preview running  /  current session");
    CHECK(findNodeByKey(app.shell().root(), "designer-stop")->enabled);

    activate("designer-debug");
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Debug preview running  /  current session");
    CHECK(app.shell().frameDebugSnapshot().nodeCount > 0);

    activate("designer-stop");
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Preview stopped");
    CHECK_FALSE(findNodeByKey(app.shell().root(), "designer-stop")->enabled);
}

TEST_CASE("designer app mirrors running preview into an independent shell",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    app.previewShell().setView(Size{960.0F, 640.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"Preview\", "
        "key: \"title\") } }",
        "independent-preview.lumen"));
    (void)app.shell().renderFrame();
    (void)app.previewShell().renderFrame();
    REQUIRE(findNodeByKey(app.previewShell().root(),
                          "designer-preview-status") != nullptr);

    const auto* run = findNodeByKey(app.shell().root(), "designer-run");
    REQUIRE(run != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              run->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.previewShell().renderFrame();
    CHECK(findNodeByKey(app.previewShell().root(), "title") != nullptr);

    const auto* debug = findNodeByKey(app.shell().root(), "designer-debug");
    REQUIRE(debug != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              debug->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();
    (void)app.previewShell().renderFrame();
    CHECK(app.previewShell().frameDebugSnapshot().nodeCount > 0);

    const auto beforeRejectedOpen = *app.workbench().document();
    auto broken = beforeRejectedOpen;
    broken.root.type = "Unknown";
    CHECK_FALSE(app.workbench().openDocument(std::move(broken)));
    CHECK(app.workbench().document() == beforeRejectedOpen);
    run = findNodeByKey(app.shell().root(), "designer-run");
    REQUIRE(run != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              run->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();
    (void)app.previewShell().renderFrame();
    CHECK(findNodeByKey(app.previewShell().root(), "title") != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Preview running  /  current session");

    const auto* stop = findNodeByKey(app.shell().root(), "designer-stop");
    REQUIRE(stop != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              stop->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.previewShell().renderFrame();
    CHECK(findNodeByKey(app.previewShell().root(),
                        "designer-preview-status") != nullptr);
}

TEST_CASE("designer app cycles keyed interaction state previews",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"Title\", key: \"title\") "
        "Button(\"Save\", key: \"save\", showFocusRing: true) } }",
        "state-preview.lumen"));
    (void)app.shell().renderFrame();
    const auto frameGeneration = app.workbench().frame().generation();

    const auto activatePreview = [&] {
        const auto* button =
            findNodeByKey(app.shell().root(), "designer-preview-state");
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };
    const auto checkState = [&](const char* label, const char* key,
                                lumen::style::WidgetState expected) {
        const auto* button =
            findNodeByKey(app.shell().root(), "designer-preview-state");
        REQUIRE(button != nullptr);
        CHECK(button->semanticsLabel == label);
        const auto context = app.shell().styleContext();
        REQUIRE(context.previewStates != nullptr);
        const auto found = context.previewStates->find(key);
        REQUIRE(found != context.previewStates->end());
        CHECK(found->second == expected);
        CHECK(app.workbench().frame().generation() == frameGeneration);
    };

    const auto* initialPreview =
        findNodeByKey(app.shell().root(), "designer-preview-state");
    REQUIRE(initialPreview != nullptr);
    CHECK(initialPreview->semanticsLabel == "悬停态");
    activatePreview();
    checkState("按压态", "root",
               lumen::style::WidgetState{.hovered = true});

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() >= 2);
    const auto saveId = outline->children[1].id;
    const auto saveHandler =
        app.shell().handlers().find("designer:select:" +
                                    std::to_string(saveId));
    REQUIRE(saveHandler != app.shell().handlers().end());
    saveHandler->second();
    (void)app.shell().renderFrame();
    checkState("按压态", "save",
               lumen::style::WidgetState{.hovered = true});

    activatePreview();
    checkState("聚焦态", "save",
               lumen::style::WidgetState{.pressed = true});
    activatePreview();
    checkState("清除态", "save",
               lumen::style::WidgetState{.focused = true});
    const auto* focusedSave = findNodeByKey(app.shell().root(), "save");
    REQUIRE(focusedSave != nullptr);
    CHECK(focusedSave->commonStyle().focusWidth > 0.0F);
    activatePreview();
    checkState("悬停态", "save", lumen::style::WidgetState{});
}

TEST_CASE("designer app previews interaction state for keyless nodes",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { "
        "Button(\"Save\", showFocusRing: true) } }",
        "keyless-state-preview.lumen"));
    (void)app.shell().renderFrame();

    const auto activatePreview = [&] {
        const auto* button =
            findNodeByKey(app.shell().root(), "designer-preview-state");
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };
    activatePreview();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() == 1);
    const auto keylessId = outline->children.front().id;
    CHECK(outline->children.front().key.empty());
    const auto previewKey = "designer:node:" + std::to_string(keylessId);
    const auto handler = app.shell().handlers().find(
        "designer:select:" + std::to_string(keylessId));
    REQUIRE(handler != app.shell().handlers().end());
    handler->second();
    (void)app.shell().renderFrame();

    const auto context = app.shell().styleContext();
    REQUIRE(context.previewStates != nullptr);
    const auto found = context.previewStates->find(previewKey);
    REQUIRE(found != context.previewStates->end());
    CHECK(found->second == lumen::style::WidgetState{.hovered = true});

    activatePreview();
    const auto pressed = app.shell().styleContext().previewStates->find(previewKey);
    REQUIRE(pressed != app.shell().styleContext().previewStates->end());
    CHECK(pressed->second == lumen::style::WidgetState{.pressed = true});
    activatePreview();
    const auto focused = app.shell().styleContext().previewStates->find(previewKey);
    REQUIRE(focused != app.shell().styleContext().previewStates->end());
    CHECK(focused->second == lumen::style::WidgetState{.focused = true});
    const auto* focusedNode = findNodeByKey(app.shell().root(), previewKey);
    REQUIRE(focusedNode != nullptr);
    CHECK(focusedNode->commonStyle().focusWidth > 0.0F);
}

TEST_CASE("designer app scopes interaction preview to duplicate keyed nodes",
          "[designer][d2][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column { Column { Button(\"First\", key: \"dup\") } "
        "Row { Button(\"Second\", key: \"dup\") } } }",
        "duplicate-key-preview.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() == 2);
    REQUIRE(outline->children[0].children.size() == 1);
    REQUIRE(outline->children[1].children.size() == 1);
    const auto firstId = outline->children[0].children[0].id;
    const auto secondId = outline->children[1].children[0].id;
    const auto firstKey = "designer:node:" + std::to_string(firstId);
    const auto secondKey = "designer:node:" + std::to_string(secondId);
    REQUIRE(findNodeByKey(app.shell().root(), firstKey) != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(), secondKey) != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "dup") == nullptr);

    const auto preview = app.shell().handlers().find("designer:preview-state");
    REQUIRE(preview != app.shell().handlers().end());
    const auto firstSelect = app.shell().handlers().find(
        "designer:select:" + std::to_string(firstId));
    REQUIRE(firstSelect != app.shell().handlers().end());
    firstSelect->second();
    preview->second();
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().styleContext().previewStates != nullptr);
    CHECK(app.shell().styleContext().previewStates->at(firstKey).hovered);
    CHECK(app.shell().styleContext().previewStates->find(secondKey) ==
          app.shell().styleContext().previewStates->end());

    const auto secondSelect = app.shell().handlers().find(
        "designer:select:" + std::to_string(secondId));
    REQUIRE(secondSelect != app.shell().handlers().end());
    secondSelect->second();
    (void)app.shell().renderFrame();
    CHECK(app.shell().styleContext().previewStates->at(firstKey) ==
          lumen::style::WidgetState{});
    CHECK(app.shell().styleContext().previewStates->at(secondKey).hovered);
}

TEST_CASE("designer app edits declaration properties and routes undo redo",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    // 自加载源：默认样例已换成 1024×768 画板（§8.1），本用例只关心声明
    // 编辑的 undo/redo 路由，不与默认样例结构耦合。
    REQUIRE(app.loadSource(
        "page preview { Stack(key: \"root\") {"
        " Text(\"Preview title\", key: \"title\") } }",
        "declaration-edit.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() >= 1);
    const auto titleId = outline->children.front().id;
    const auto handler = app.shell().handlers().find(
        "designer:select:" + std::to_string(titleId));
    REQUIRE(handler != app.shell().handlers().end());
    handler->second();
    (void)app.shell().renderFrame();

    const std::string bind = "designer:property:" + std::to_string(titleId) +
                             ":text";
    REQUIRE(findNodeByKey(
        app.shell().root(),
        "designer-property-field:" + std::to_string(titleId) + ":text"));
    app.shell().state().set(bind, "Edited in panel");
    (void)app.shell().renderFrame();
    auto properties = app.workbench().properties(titleId);
    REQUIRE(properties.size() == 2);
    CHECK(std::get<std::string>(properties.back().value->value) ==
          "Edited in panel");
    CHECK(app.workbench().dirty());

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'z');
    (void)app.shell().renderFrame();
    properties = app.workbench().properties(titleId);
    CHECK(std::get<std::string>(properties.back().value->value) == "Preview title");
    CHECK_FALSE(app.workbench().dirty());

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'y');
    (void)app.shell().renderFrame();
    properties = app.workbench().properties(titleId);
    CHECK(std::get<std::string>(properties.back().value->value) ==
          "Edited in panel");
    CHECK(app.workbench().dirty());
}

// Designer prerequisites section 4.18: IME preview is not a document edit.
TEST_CASE("designer property IME preserves preedit and commits one transaction",
          "[designer][d3][app][ime]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Text(\"Original\", key: \"title\") }",
        "ime-property.lumen"));
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    const auto nodeId = app.workbench().document()->root.id;
    const std::string fieldKey =
        "designer-property-field:" + std::to_string(nodeId) + ":text";
    const auto* field = findNodeByKey(app.shell().root(), fieldKey);
    REQUIRE(field != nullptr);
    const auto point = absoluteOffset(app.shell().root(), fieldKey) +
                       Offset{field->size.width * 0.5F,
                              field->size.height * 0.5F};
    app.shell().pointerDown(point);
    app.shell().pointerUp(point);
    REQUIRE(app.shell().focus().focusedKey() == fieldKey);
    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'a');
    REQUIRE(app.shell().controller().selectionStart() == 0);
    REQUIRE(app.shell().controller().selectionEnd() == 8);
    const auto original =
        lumen::dsl::serializeDesignDocument(*app.workbench().document());
    const auto revision = app.workbench().documentRevision();

    app.shell().textEditing("ni");
    (void)app.shell().renderFrame();
    app.shell().textEditing("nihao");
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().composingActive());
    CHECK(app.shell().controller().composition() == "nihao");
    CHECK(lumen::dsl::serializeDesignDocument(*app.workbench().document()) ==
          original);
    CHECK(app.workbench().documentRevision() == revision);
    CHECK_FALSE(app.workbench().dirty());
    CHECK_FALSE(app.workbench().canUndo());

    SECTION("native cancellation does not create history") {
        app.shell().textEditing("");
    }
    SECTION("Escape cancellation does not create history") {
        app.shell().keyDown(Key::Escape);
    }
    SECTION("commit is atomic and history keys wait for composition") {
        const std::string committed = "\xE4\xBD\xA0\xE5\xA5\xBD";
        app.shell().textInput(committed);
        (void)app.shell().renderFrame();
        CHECK(std::get<std::string>(
                  app.workbench().document()->root.properties.at("text").value) ==
              committed);
        CHECK(app.workbench().documentRevision() == revision + 1);
        CHECK(app.workbench().dirty());
        const auto afterCommit =
            lumen::dsl::serializeDesignDocument(*app.workbench().document());

        app.shell().textEditing("zai");
        for (const char historyKey : {'z', 'y'}) {
            for (const auto modifiers : {lumen::core::kModifierCtrl,
                                         lumen::core::kModifierGui}) {
                app.shell().keyDown(Key::None, modifiers, historyKey);
                (void)app.shell().renderFrame();
                CHECK(lumen::dsl::serializeDesignDocument(
                          *app.workbench().document()) == afterCommit);
                CHECK(app.shell().controller().composition() == "zai");
            }
        }
        app.shell().keyDown(Key::Escape);
        app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'z');
        (void)app.shell().renderFrame();
    }

    CHECK_FALSE(app.shell().controller().composingActive());
    CHECK(app.shell().controller().composition().empty());
    CHECK(lumen::dsl::serializeDesignDocument(*app.workbench().document()) ==
          original);
    CHECK_FALSE(app.workbench().dirty());
    CHECK_FALSE(app.workbench().canUndo());
}

// Prerequisites §4.18: cancelling preedit precedes preview route navigation.
TEST_CASE("designer Escape cancels field composition before navigating the preview",
          "[designer][d3][app][ime][ime-routing]") {
    bool referenceField = false;
    SECTION("declaration property") {}
    SECTION("named reference") { referenceField = true; }
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") {"
        " Text(\"Original\", key: \"title\")"
        " Button(\"Save\", onClick: save, key: \"save\") } }",
        "ime-route.lumen"));
    (void)app.shell().renderFrame();
    app.shell().handlers().at("designer:toolbox:Navigator")();
    (void)app.shell().renderFrame();
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto navigatorId = children.back().id;
    const auto fieldNodeId = children[referenceField ? 1 : 0].id;
    const std::string prefix = "designer:component:Navigator:" +
                               std::to_string(navigatorId) + ":";
    // The route rebuild registers a replacement callback; retain this call's
    // function just as the production handler dispatch does.
    auto routeAction = app.shell().handlers().at(prefix + "route:details");
    routeAction();
    app.shell().handlers().at("designer:select:" + std::to_string(fieldNodeId))();
    app.shell().handlers().at("designer:run")();
    (void)app.shell().renderFrame();
    (void)app.previewShell().renderFrame();
    const std::string fieldKey =
        (referenceField ? "designer-reference-field:" : "designer-property-field:") +
        std::to_string(fieldNodeId) + (referenceField ? ":onClick" : ":text");
    const auto* field = findNodeByKey(app.shell().root(), fieldKey);
    REQUIRE(field != nullptr);
    app.shell().controller().focusNode(*field);
    REQUIRE(app.shell().focus().focusedKey() == fieldKey);
    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'a');
    const auto selectionStart = app.shell().controller().selectionStart();
    const auto selectionEnd = app.shell().controller().selectionEnd();
    REQUIRE(selectionEnd > selectionStart);
    const auto before = *app.workbench().document();
    const auto revision = app.workbench().documentRevision();
    const auto dirty = app.workbench().dirty();
    const auto canUndo = app.workbench().canUndo();
    app.shell().textEditing("nihao");
    REQUIRE(app.shell().controller().composingActive());
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.shell().controller().composingActive());
    CHECK(app.shell().controller().composition().empty());
    CHECK(app.shell().controller().selectionStart() == selectionStart);
    CHECK(app.shell().controller().selectionEnd() == selectionEnd);
    CHECK(app.shell().focus().focusedKey() == fieldKey);
    const auto* route = findNodeByKey(app.shell().root(), prefix + "navigator-route");
    REQUIRE(route != nullptr);
    CHECK(route->text == "Route: details");
    const auto* previewRoute = findNodeByKey(
        app.previewShell().root(), prefix + "navigator-route");
    REQUIRE(previewRoute != nullptr);
    CHECK(previewRoute->text == "Route: details");
    CHECK(app.workbench().document() == before);
    CHECK(app.workbench().documentRevision() == revision);
    CHECK(app.workbench().dirty() == dirty);
    CHECK(app.workbench().canUndo() == canUndo);
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();
    (void)app.previewShell().renderFrame();
    route = findNodeByKey(app.shell().root(), prefix + "navigator-route");
    REQUIRE(route != nullptr);
    CHECK(route->text == "Route: home");
    previewRoute = findNodeByKey(app.previewShell().root(), prefix + "navigator-route");
    REQUIRE(previewRoute != nullptr);
    CHECK(previewRoute->text == "Route: home");
    CHECK(app.workbench().document() == before);
    CHECK(app.workbench().documentRevision() == revision);
}

TEST_CASE("designer app edits a shared multi-selection property in one transaction",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"A\", key: \"a\") "
        "Text(\"B\", key: \"b\") Button(\"C\", key: \"c\") } }",
        "multi-property.lumen"));
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，大纲操作前切到结构页。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto firstId = children[0].id;
    const auto secondId = children[1].id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().ids ==
            std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    const std::string bind =
        "designer:property:" + std::to_string(secondId) + ":text";
    REQUIRE(findNodeByKey(
        app.shell().root(),
        "designer-property-field:" + std::to_string(secondId) + ":text"));
    app.shell().state().set(bind, "Shared");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(std::get<std::string>(
              app.workbench().document()->root.children[0].properties.at("text").value) ==
          "Shared");
    CHECK(std::get<std::string>(
              app.workbench().document()->root.children[1].properties.at("text").value) ==
          "Shared");
    CHECK(app.workbench().canUndo());
    REQUIRE(app.undo());
    CHECK(std::get<std::string>(
              app.workbench().document()->root.children[0].properties.at("text").value) ==
          "A");
    CHECK(std::get<std::string>(
              app.workbench().document()->root.children[1].properties.at("text").value) ==
          "B");
    CHECK_FALSE(app.workbench().dirty());
}

TEST_CASE("designer app routes L0 structure commands",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto initialChildren = initial->children.size();
    const std::vector<std::string> toolboxTypes = {
        "Container", "Row",       "Column",    "Stack",
        "Text",      "Button",    "TextField", "ScrollView",
        "ListView",  "Checkbox",  "Switch",    "FocusScope",
        "Grid",      "Image",      "Icon",      "Slider",
        "ProgressBar", "Radio",    "Tooltip",   "Dropdown",
        "Tabs",      "ThemeScope", "VirtualList", "List",
        "Tree",      "TreeList",   "Splitter", "ComboBox", "ColorPicker",
        "Spin",      "ToolBar",    "StatusBar", "Menu", "DialogHost",
        "Navigator",
        "Form",      "DataGrid"};

    const auto activate = [&](const char* key) {
        const auto* button = findNodeByKey(app.shell().root(), key);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };

    for (const auto& type : toolboxTypes) {
        REQUIRE(findNodeByKey(app.shell().root(), "designer-toolbox:" + type));
    }

    activate("designer-add-text");
    auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() == initialChildren + 1);
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto insertedId = *app.workbench().selection().primary;
    CHECK(insertedId != rootId);

    activate("designer-duplicate");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.size() == initialChildren + 2);
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto duplicateId = *app.workbench().selection().primary;
    CHECK(duplicateId != insertedId);

    activate("designer-move-up");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.back().id != duplicateId);

    activate("designer-move-down");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.back().id == duplicateId);

    activate("designer-remove");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.size() == initialChildren + 1);
    CHECK(app.workbench().selection().primary == rootId);

    CHECK(app.undo());
    CHECK(app.workbench().selection().primary == duplicateId);
    CHECK(app.workbench().outline()->children.size() == initialChildren + 2);

    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(rootId));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();
    activate("designer-toolbox:FocusScope");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.size() == initialChildren + 3);
    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(outline->children.back().id ==
          *app.workbench().selection().primary);
    CHECK(outline->children.back().type == "FocusScope");
}

TEST_CASE("designer app copies and pastes a selected node as one transaction",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 2);
    // 选中文本节点（默认样例 §8.1 后 front() 是容器）：状态文案与
    // "Copied/Pasted Text" 契约沿用。
    const auto* titleNode = findNodeByKey(app.shell().root(), "title");
    REQUIRE(titleNode != nullptr);
    REQUIRE(titleNode->onClick.rfind("designer:select:", 0) == 0);
    const auto sourceId = static_cast<lumen::dsl::DesignNodeId>(
        std::strtoull(titleNode->onClick.c_str() + 16, nullptr, 10));
    const auto select = app.shell().handlers().find(
        "designer:select:" + std::to_string(sourceId));
    REQUIRE(select != app.shell().handlers().end());
    select->second();
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == sourceId);
    CHECK_FALSE(app.shell().focus().focusedKey().starts_with(
        "designer-property-field:"));

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'c');
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Copied Text");
    CHECK_FALSE(app.shell().focus().focusedKey().starts_with(
        "designer-property-field:"));
    CHECK_FALSE(app.shell().focus().focusedKey().starts_with(
        "designer-reference-field:"));
    CHECK(app.workbench().selection().primary == sourceId);
    CHECK_FALSE(app.workbench().dirty());
    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'v');
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Pasted Text");

    const auto afterPaste = app.workbench().outline();
    REQUIRE(afterPaste.has_value());
    REQUIRE(afterPaste->children.size() == initial->children.size() + 1);
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto pastedId = *app.workbench().selection().primary;
    CHECK(pastedId != sourceId);
    // 粘贴落在源节点之后（默认样例 §8.1 后源不在 index 0）。
    const auto pasted = std::find_if(
        afterPaste->children.begin(), afterPaste->children.end(),
        [pastedId](const auto& child) { return child.id == pastedId; });
    REQUIRE(pasted != afterPaste->children.end());
    CHECK(pasted->type == "Text");
    CHECK(app.workbench().dirty());

    REQUIRE(app.undo());
    (void)app.shell().renderFrame();
    CHECK(app.workbench().outline()->children.size() == initial->children.size());
    CHECK(app.workbench().selection().primary == sourceId);
}

TEST_CASE("designer app copies multi-selection across parents as one transaction",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Row(key: \"source\") { "
        "Text(\"A\", key: \"a\") Button(\"B\", key: \"b\") } "
        "Column(key: \"target\") { Text(\"Existing\", key: \"existing\") } "
        "} }",
        "multi-copy.lumen"));
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，大纲操作前切到结构页。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& root = app.workbench().document()->root;
    REQUIRE(root.children.size() == 2);
    const auto& source = root.children[0];
    const auto& target = root.children[1];
    REQUIRE(source.children.size() == 2);
    REQUIRE(target.children.size() == 1);
    const auto firstId = source.children[0].id;
    const auto secondId = source.children[1].id;
    const auto targetId = target.id;
    const auto targetChildId = target.children.front().id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'c');
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Copied 2 nodes");
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    CHECK_FALSE(app.workbench().dirty());

    const auto targetOutline = findOutlineNodeById(*outline, targetChildId);
    REQUIRE(targetOutline != nullptr);
    const auto* targetRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + targetOutline->path);
    REQUIRE(targetRow != nullptr);
    const auto targetOrigin = absoluteOffset(app.shell().root(), targetRow->key);
    const Offset targetPoint =
        targetOrigin + Offset{targetRow->size.width * 0.5F,
                              targetRow->size.height * 0.5F};
    app.shell().pointerDown(targetPoint);
    app.shell().pointerUp(targetPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == targetChildId);
    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'v');
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Pasted 2 nodes");

    REQUIRE(app.workbench().document().has_value());
    const auto* pastedTarget = findDesignNodeById(
        app.workbench().document()->root, targetId);
    REQUIRE(pastedTarget != nullptr);
    REQUIRE(pastedTarget->children.size() == 3);
    const auto pastedFirstId = pastedTarget->children[1].id;
    const auto pastedSecondId = pastedTarget->children[2].id;
    CHECK(pastedFirstId != firstId);
    CHECK(pastedSecondId != secondId);
    CHECK(pastedTarget->children[1].type == "Text");
    CHECK(pastedTarget->children[2].type == "Button");
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{pastedFirstId, pastedSecondId});
    CHECK(app.workbench().selection().primary == pastedSecondId);
    CHECK(app.workbench().dirty());

    REQUIRE(app.undo());
    REQUIRE(app.workbench().document().has_value());
    pastedTarget = findDesignNodeById(app.workbench().document()->root,
                                      targetId);
    REQUIRE(pastedTarget != nullptr);
    CHECK(pastedTarget->children.size() == 1);
    CHECK(app.workbench().selection().primary == targetChildId);

    REQUIRE(app.redo());
    REQUIRE(app.workbench().document().has_value());
    pastedTarget = findDesignNodeById(app.workbench().document()->root,
                                      targetId);
    REQUIRE(pastedTarget != nullptr);
    CHECK(pastedTarget->children.size() == 3);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{pastedFirstId, pastedSecondId});
}

TEST_CASE("designer app duplicates multi-selection with unique keys",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"A\", key: \"a\") "
        "Button(\"B\", key: \"b\") Text(\"C\", key: \"c\") } }",
        "multi-duplicate.lumen"));
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，大纲操作前切到结构页。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto firstId = children[0].id;
    const auto secondId = children[1].id;
    const auto thirdId = children[2].id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().ids ==
            std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    app.shell().handlers().at("designer:duplicate")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Duplicated 2 nodes");
    REQUIRE(app.workbench().document().has_value());
    const auto& duplicated = app.workbench().document()->root.children;
    REQUIRE(duplicated.size() == 5);
    const auto duplicateFirstId = duplicated[2].id;
    const auto duplicateSecondId = duplicated[3].id;
    CHECK(duplicateFirstId != firstId);
    CHECK(duplicateSecondId != secondId);
    CHECK(std::get<std::string>(
              duplicated[2].properties.at("key").value) == "a-copy");
    CHECK(std::get<std::string>(
              duplicated[3].properties.at("key").value) == "b-copy");
    CHECK(duplicated[4].id == thirdId);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{duplicateFirstId,
                                               duplicateSecondId});

    REQUIRE(app.undo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 3);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    REQUIRE(app.redo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 5);
    CHECK(app.workbench().selection().primary == duplicateSecondId);
}

TEST_CASE("designer app deletes multi-selection as one transaction",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"A\", key: \"a\") "
        "Button(\"B\", key: \"b\") Text(\"C\", key: \"c\") } }",
        "multi-delete.lumen"));
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，大纲操作前切到结构页。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto firstId = children[0].id;
    const auto secondId = children[1].id;
    const auto rootId = app.workbench().document()->root.id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    app.shell().keyDown(Key::Delete);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Removed 2 nodes");
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 1);
    CHECK(app.workbench().document()->root.children.front().id != firstId);
    CHECK(app.workbench().selection().primary == rootId);

    REQUIRE(app.undo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 3);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    REQUIRE(app.redo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.size() == 1);
    CHECK(app.workbench().selection().primary == rootId);
}

TEST_CASE("designer app moves multi-selection as one transaction",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\") { Text(\"A\", key: \"a\") "
        "Button(\"B\", key: \"b\") Text(\"C\", key: \"c\") } }",
        "multi-move.lumen"));
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，大纲操作前切到结构页。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().document().has_value());
    const auto& children = app.workbench().document()->root.children;
    REQUIRE(children.size() == 3);
    const auto firstId = children[0].id;
    const auto secondId = children[1].id;
    const auto thirdId = children[2].id;
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto* firstOutline = findOutlineNodeById(*outline, firstId);
    const auto* secondOutline = findOutlineNodeById(*outline, secondId);
    REQUIRE(firstOutline != nullptr);
    REQUIRE(secondOutline != nullptr);
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + firstOutline->path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + secondOutline->path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.5F};
    app.shell().pointerDown(firstPoint);
    app.shell().pointerUp(firstPoint);
    app.shell().pointerDown(secondPoint, lumen::core::kModifierShift);
    app.shell().pointerUp(secondPoint);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().ids ==
            std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    app.shell().handlers().at("designer:move-down")();
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children[0].id == thirdId);
    CHECK(app.workbench().document()->root.children[1].id == firstId);
    CHECK(app.workbench().document()->root.children[2].id == secondId);
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Moved 2 nodes");
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});

    REQUIRE(app.undo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children[0].id == firstId);
    CHECK(app.workbench().document()->root.children[1].id == secondId);
    CHECK(app.workbench().document()->root.children[2].id == thirdId);
    CHECK(app.workbench().selection().ids ==
          std::set<lumen::dsl::DesignNodeId>{firstId, secondId});
    REQUIRE(app.redo());
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children[0].id == thirdId);
    CHECK(app.workbench().selection().primary == secondId);
    CHECK(app.workbench().selection().anchor == secondId);
}

TEST_CASE("designer gallery covers every registered schema without diagnostics",
          "[designer][d2][gallery]") {
    DesignerApp app;
    app.attach();
    const auto gallery = std::filesystem::path{__FILE__}
                              .parent_path()
                              .parent_path() /
                          "examples/designer/gallery.design";
    REQUIRE(app.loadDesignFile(gallery.string()));
    CHECK(app.diagnostics().empty());

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    std::set<std::string> types;
    std::function<void(const lumen::dsl::DesignPreviewOutlineNode&)> visit =
        [&](const lumen::dsl::DesignPreviewOutlineNode& node) {
            types.insert(node.type);
            for (const auto& child : node.children) visit(child);
        };
    visit(*outline);
    const auto& schemas = lumen::dsl::nodeSchemaRegistry();
    CHECK(types.size() == schemas.size());
    for (const auto& schema : schemas) {
        CHECK(types.contains(schema.type));
    }
}

TEST_CASE("designer app creates L1 and L2 toolbox nodes with schema defaults",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(rootId));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    const auto activate = [&](const char* type) {
        const auto* button = findNodeByKey(
            app.shell().root(), std::string{"designer-toolbox:"} + type);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };

    activate("Image");
    auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto imageId = *app.workbench().selection().primary;
    CHECK(outline->children.back().type == "Image");
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-property-field:" + std::to_string(imageId) +
                    ":imageSource") != nullptr);
    app.shell().state().set(
        "designer:property:" + std::to_string(imageId) + ":imageSource",
        "assets/hero.png");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    const auto& imageNode = app.workbench().document()->root.children.back();
    REQUIRE(imageNode.properties.contains("imageSource"));
    CHECK(std::get<std::string>(imageNode.properties.at("imageSource").value) ==
          "assets/hero.png");

    const auto rootHandlerAgain = app.shell().handlers().find(
        "designer:select:" + std::to_string(rootId));
    REQUIRE(rootHandlerAgain != app.shell().handlers().end());
    rootHandlerAgain->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();
    activate("Splitter");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto splitterId = *app.workbench().selection().primary;
    REQUIRE(outline->children.back().type == "Splitter");
    CHECK(outline->children.back().children.size() == 2);
    CHECK(splitterId != imageId);
    CHECK(findNodeByKey(
              app.shell().root(),
              "designer-reference-field:" + std::to_string(splitterId) +
                  ":splitterSource") != nullptr);
    CHECK(app.workbench().diagnostics().empty());
}

TEST_CASE("designer app exposes L2 source references and offline diagnostics",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(rootId));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    const auto* listButton = findNodeByKey(
        app.shell().root(), "designer-toolbox:List");
    REQUIRE(listButton != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              listButton->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto listId = *app.workbench().selection().primary;
    REQUIRE(outline->children.back().type == "List");
    const std::string bind =
        "designer:reference:" + std::to_string(listId) + ":virtualSource";
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-reference-field:" + std::to_string(listId) +
                    ":virtualSource") != nullptr);

    app.shell().state().set(bind, "rows");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document()->root.children.back().references
                .contains("virtualSource"));
    CHECK(app.workbench().document()->root.children.back().references.at(
              "virtualSource") == "rows");
    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().code == "reference.missing");
    CHECK(app.workbench().diagnostics().front().property == "virtualSource");

    app.shell().state().set(bind, "");
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.workbench().document()->root.children.back().references
                    .contains("virtualSource"));
    CHECK(app.workbench().diagnostics().empty());
}

TEST_CASE("designer app previews deterministic L2 source adapters",
          "[designer][d3][app][designer-l2]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto rootId = outline->id;
    const auto selectRoot = [&] {
        const auto handler = app.shell().handlers().find(
            "designer:select:" + std::to_string(rootId));
        REQUIRE(handler != app.shell().handlers().end());
        handler->second();
        (void)app.shell().renderFrame();
    };
    const auto activate = [&](const char* type) {
        const auto* button = findNodeByKey(
            app.shell().root(), std::string{"designer-toolbox:"} + type);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();
    };

    selectRoot();
    activate("List");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto listId = *app.workbench().selection().primary;
    const std::string listReference =
        "designer:reference:" + std::to_string(listId) + ":virtualSource";
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-reference-field:" + std::to_string(listId) +
                    ":virtualSource") != nullptr);
    app.shell().state().set(listReference, "preview_rows");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.back().references.at(
              "virtualSource") == "preview_rows");
    CHECK(app.workbench().diagnostics().empty());
    const auto* list = findNodeByKey(
        app.shell().root(), "designer:node:" + std::to_string(listId));
    REQUIRE(list != nullptr);
    REQUIRE(list->virtualSource != nullptr);
    CHECK(list->virtualSource->itemCount() == 12);
    CHECK(list->children.size() > 0);
    const auto* listSource =
        dynamic_cast<const lumen::core::VirtualListController*>(list->virtualSource);
    REQUIRE(listSource != nullptr);
    CHECK(listSource->lastMaterializedItems() == list->children.size());
    CHECK(listSource->peakMaterializedItems() >=
          listSource->lastMaterializedItems());
    const auto* dynamicRow = findNodeByKey(
        app.shell().root(), "designer-preview-row:0");
    REQUIRE(dynamicRow != nullptr);
    CHECK(dynamicRow->onClick == "designer:select:" +
                                  std::to_string(listId));
    selectRoot();
    dynamicRow = findNodeByKey(app.shell().root(), "designer-preview-row:0");
    REQUIRE(dynamicRow != nullptr);
    const auto rowOrigin = absoluteOffset(app.shell().root(),
                                           "designer-preview-row:0");
    const Offset rowCenter{rowOrigin.x + dynamicRow->size.width * 0.5F,
                           rowOrigin.y + dynamicRow->size.height * 0.5F};
    app.shell().pointerDown(rowCenter);
    app.shell().pointerUp(rowCenter);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(*app.workbench().selection().primary == listId);
    CHECK(app.workbench().selection().ids.size() == 1);
    REQUIRE(app.workbench().frame().session() != nullptr);
    CHECK(app.workbench().frame().session()->leaseCount() == 1);

    selectRoot();
    activate("Splitter");
    outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto splitterId = *app.workbench().selection().primary;
    const std::string splitterReference =
        "designer:reference:" + std::to_string(splitterId) +
        ":splitterSource";
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-reference-field:" + std::to_string(splitterId) +
                    ":splitterSource") != nullptr);
    app.shell().state().set(splitterReference, "preview_splitter");
    (void)app.shell().renderFrame();
    CHECK(app.workbench().diagnostics().empty());
    const auto* splitter = findNodeByKey(
        app.shell().root(),
        "split:div:designer:node:" + std::to_string(splitterId));
    REQUIRE(splitter != nullptr);
    REQUIRE(splitter->splitterSource != nullptr);
    CHECK(app.workbench().frame().session()->leaseCount() == 2);
    const auto document = lumen::dsl::serializeDesignDocument(
        *app.workbench().document());
    const auto revision = app.workbench().documentRevision();
    const auto oldSession = app.workbench().frame().session();
    splitter->splitterSource->stepBy(16.0F);
    CHECK(lumen::dsl::serializeDesignDocument(*app.workbench().document()) ==
          document);
    CHECK(app.workbench().documentRevision() == revision);

    app.shell().handlers().at("designer:run")();
    (void)app.shell().renderFrame();
    CHECK_FALSE(oldSession->active());
    CHECK(oldSession->leaseCount() == 0);
    CHECK(app.workbench().frame().session()->leaseCount() == 2);
    CHECK(lumen::dsl::serializeDesignDocument(*app.workbench().document()) ==
          document);
    CHECK(app.workbench().documentRevision() == revision);

    app.shell().handlers().at("designer:select:" + std::to_string(listId))();
    (void)app.shell().renderFrame();
    app.shell().handlers().at("designer:duplicate")();
    (void)app.shell().renderFrame();
    std::vector<const lumen::core::VirtualListSource*> sources;
    for (const auto& child : app.workbench().frame().widget().children) {
        if (child.virtualSource != nullptr) sources.push_back(child.virtualSource);
    }
    REQUIRE(sources.size() == 2);
    CHECK(sources[0] != sources[1]);
    CHECK(sources[0]->scrollController() != sources[1]->scrollController());
}

TEST_CASE("designer app previews its first L3 DataGrid component",
          "[designer][d3][app][designer-l3]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(initial->id));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    const auto* dataGridButton = findNodeByKey(
        app.shell().root(), "designer-toolbox:DataGrid");
    REQUIRE(dataGridButton != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              dataGridButton->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    CHECK(outline->children.back().type == "DataGrid");
    CHECK(app.workbench().diagnostics().empty());
    REQUIRE(app.workbench().frame().hasFrame());
    CHECK_FALSE(app.workbench().frame().widget().children.empty());
    CHECK(app.workbench().frame().widget().children.back().key.starts_with(
        "designer:component:DataGrid:"));

    CHECK(app.undo());
    REQUIRE(app.workbench().outline().has_value());
    // 默认样例（§8.1 画板）末子节点是 Text（status）。
    CHECK(app.workbench().outline()->children.back().type == "Text");
    CHECK(app.redo());
    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children.back().type == "DataGrid");
}

TEST_CASE("designer app resolves offline ThemeScope references",
          "[designer][d3][app][theme]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto selectRoot = app.shell().handlers().find(
        "designer:select:" + std::to_string(initial->id));
    REQUIRE(selectRoot != app.shell().handlers().end());
    selectRoot->second();
    (void)app.shell().renderFrame();

    const auto* toolbox = findNodeByKey(
        app.shell().root(), "designer-toolbox:ThemeScope");
    REQUIRE(toolbox != nullptr);
    CHECK(app.shell().performAccessibilityAction(
              toolbox->identity, kActionActivate) ==
          lumen::accessibility::SemanticsActionStatus::Handled);
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto themeId = outline->children.back().id;
    CHECK(outline->children.back().type == "ThemeScope");
    const auto referenceKey =
        "designer:reference:" + std::to_string(themeId) + ":theme";
    REQUIRE(findNodeByKey(
                app.shell().root(),
                "designer-reference-field:" + std::to_string(themeId) +
                    ":theme") != nullptr);

    app.shell().state().set(referenceKey, "light");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.back().references.at(
              "theme") == "light");
    CHECK(app.workbench().diagnostics().empty());
    REQUIRE(app.workbench().frame().hasFrame());
    REQUIRE(!app.workbench().frame().widget().children.empty());
    const auto& preview = app.workbench().frame().widget().children.back();
    CHECK(preview.type == lumen::core::WidgetType::ThemeScope);
    CHECK(preview.themeOverride != nullptr);
    REQUIRE(preview.themeOverride != nullptr);
    const auto lightPageBackground =
        static_cast<const lumen::style::Theme*>(preview.themeOverride)
            ->colors.pageBackground;
    CHECK(lightPageBackground == lumen::style::Theme::light().colors.pageBackground);

    app.shell().state().set(referenceKey, "dark");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.back().references.at(
              "theme") == "dark");
    CHECK(app.workbench().diagnostics().empty());
    REQUIRE(app.workbench().frame().hasFrame());
    REQUIRE(!app.workbench().frame().widget().children.empty());
    const auto& darkPreview = app.workbench().frame().widget().children.back();
    REQUIRE(darkPreview.themeOverride != nullptr);
    const auto darkPageBackground =
        static_cast<const lumen::style::Theme*>(darkPreview.themeOverride)
            ->colors.pageBackground;
    CHECK(darkPageBackground == lumen::style::Theme::dark().colors.pageBackground);
    CHECK(darkPageBackground != lightPageBackground);

    REQUIRE(app.undo());
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.back().references.at(
              "theme") == "light");
    CHECK(app.workbench().diagnostics().empty());
    REQUIRE(app.workbench().frame().hasFrame());
    REQUIRE(!app.workbench().frame().widget().children.empty());
    const auto& undonePreview =
        app.workbench().frame().widget().children.back();
    REQUIRE(undonePreview.themeOverride != nullptr);
    CHECK(static_cast<const lumen::style::Theme*>(undonePreview.themeOverride)
              ->colors.pageBackground == lightPageBackground);

    REQUIRE(app.redo());
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document().has_value());
    CHECK(app.workbench().document()->root.children.back().references.at(
              "theme") == "dark");
    CHECK(app.workbench().diagnostics().empty());
    REQUIRE(app.workbench().frame().hasFrame());
    REQUIRE(!app.workbench().frame().widget().children.empty());
    const auto& redonePreview =
        app.workbench().frame().widget().children.back();
    REQUIRE(redonePreview.themeOverride != nullptr);
    CHECK(static_cast<const lumen::style::Theme*>(redonePreview.themeOverride)
              ->colors.pageBackground == darkPageBackground);

    app.shell().state().set(referenceKey, "missing_theme");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().code == "reference.missing");
}

TEST_CASE("designer app previews toolbar and statusbar components",
          "[designer][d3][app][designer-l3]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(initial->id));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    for (const std::string type : {"ToolBar", "StatusBar"}) {
        const auto* button = findNodeByKey(
            app.shell().root(), "designer-toolbox:" + type);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();

        const auto outline = app.workbench().outline();
        REQUIRE(outline.has_value());
        REQUIRE(!outline->children.empty());
        CHECK(outline->children.back().type == type);
        CHECK(app.workbench().diagnostics().empty());
        REQUIRE(app.workbench().frame().hasFrame());
        REQUIRE(!app.workbench().frame().widget().children.empty());
        CHECK(app.workbench().frame().widget().children.back().key.starts_with(
            "designer:component:" + type + ":"));

        CHECK(app.undo());
        REQUIRE(app.workbench().outline().has_value());
        // 默认样例（§8.1 画板）末子节点是 Text（status）。
        CHECK(app.workbench().outline()->children.back().type == "Text");
    }
}

TEST_CASE("designer app previews input component adapters",
          "[designer][d3][app][designer-l3]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(initial->id));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    for (const std::string type : {"ComboBox", "ColorPicker", "Spin"}) {
        const auto* button = findNodeByKey(
            app.shell().root(), "designer-toolbox:" + type);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();

        const auto outline = app.workbench().outline();
        REQUIRE(outline.has_value());
        REQUIRE(!outline->children.empty());
        CHECK(outline->children.back().type == type);
        CHECK(app.workbench().diagnostics().empty());
        REQUIRE(app.workbench().frame().hasFrame());
        REQUIRE(!app.workbench().frame().widget().children.empty());
        CHECK(app.workbench().frame().widget().children.back().key.starts_with(
            "designer:component:" + type + ":"));

        CHECK(app.undo());
        REQUIRE(app.workbench().outline().has_value());
        // 默认样例（§8.1 画板）末子节点是 Text（status）。
        CHECK(app.workbench().outline()->children.back().type == "Text");
    }
}

TEST_CASE("designer app previews menu navigator and form adapters",
          "[designer][d3][app][designer-l3]") {
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootHandler = app.shell().handlers().find(
        "designer:select:" + std::to_string(initial->id));
    REQUIRE(rootHandler != app.shell().handlers().end());
    rootHandler->second();
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    for (const std::string type : {"Menu", "DialogHost", "Navigator", "Form"}) {
        const auto* button = findNodeByKey(
            app.shell().root(), "designer-toolbox:" + type);
        REQUIRE(button != nullptr);
        CHECK(app.shell().performAccessibilityAction(
                  button->identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
        (void)app.shell().renderFrame();

        const auto outline = app.workbench().outline();
        REQUIRE(outline.has_value());
        REQUIRE(!outline->children.empty());
        const auto id = outline->children.back().id;
        CHECK(outline->children.back().type == type);
        CHECK(app.workbench().diagnostics().empty());
        REQUIRE(app.workbench().frame().hasFrame());
        REQUIRE(!app.workbench().frame().widget().children.empty());
        const std::string prefix =
            "designer:component:" + type + ":" + std::to_string(id) + ":";
        CHECK(app.workbench().frame().widget().children.back().key.starts_with(
            prefix));

        if (type == "DialogHost") {
            const auto* open = findNodeByKey(
                app.shell().root(), prefix + "dialog-open-button");
            REQUIRE(open != nullptr);
            CHECK(app.shell().performAccessibilityAction(
                      open->identity, kActionActivate) ==
                  lumen::accessibility::SemanticsActionStatus::Handled);
            (void)app.shell().renderFrame();
            REQUIRE(findNodeByKey(app.shell().root(), "dialog-host") != nullptr);
            app.shell().keyDown(Key::Escape);
            (void)app.shell().renderFrame();
            CHECK(findNodeByKey(app.shell().root(), "dialog-host") == nullptr);
        } else if (type == "Navigator") {
            const auto* routeButton = findNodeByKey(
                app.shell().root(), prefix + "navigator-button:details");
            REQUIRE(routeButton != nullptr);
            CHECK(app.shell().performAccessibilityAction(
                      routeButton->identity, kActionActivate) ==
                  lumen::accessibility::SemanticsActionStatus::Handled);
            (void)app.shell().renderFrame();
            const auto* routeLabel =
                findNodeByKey(app.shell().root(), prefix + "navigator-route");
            REQUIRE(routeLabel != nullptr);
            CHECK(routeLabel->text == "Route: details");
        } else if (type == "Form") {
            const auto* submit =
                findNodeByKey(app.shell().root(), prefix + "submit-button");
            REQUIRE(submit != nullptr);
            CHECK(app.shell().performAccessibilityAction(
                      submit->identity, kActionActivate) ==
                  lumen::accessibility::SemanticsActionStatus::Handled);
            (void)app.shell().renderFrame();
            const auto* error =
                findNodeByKey(app.shell().root(), prefix + "form-name-error");
            REQUIRE(error != nullptr);
            CHECK(error->text == "Name is required");
        }

        CHECK(app.undo());
        REQUIRE(app.workbench().outline().has_value());
        // 默认样例（§8.1 画板）末子节点是 Text（status）。
        CHECK(app.workbench().outline()->children.back().type == "Text");
    }
}

TEST_CASE("designer app edits named runtime references",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Button(\"Save\", onClick: save, key: \"save\") }",
        "reference-editor.lumen"));
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto buttonId = outline->id;
    const std::string bind =
        "designer:reference:" + std::to_string(buttonId) + ":onClick";
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-reference-field:" +
                              std::to_string(buttonId) + ":onClick"));

    app.shell().state().set(bind, "submit");
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().document()->root.references.contains("onClick"));
    CHECK(app.workbench().document()->root.references.at("onClick") ==
          "submit");
    CHECK(app.workbench().dirty());

    app.shell().state().set(bind, "not-valid");
    (void)app.shell().renderFrame();
    CHECK(app.shell().state().get(bind) == "submit");
    CHECK(app.workbench().document()->root.references.at("onClick") ==
          "submit");

    app.shell().state().set(bind, "");
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.workbench().document()->root.references.contains("onClick"));
    REQUIRE(app.undo());
    (void)app.shell().renderFrame();
    CHECK(app.workbench().document()->root.references.at("onClick") ==
          "submit");
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-reference-field:" +
                            std::to_string(buttonId) + ":onClick"));
}

TEST_CASE("designer app exposes named references in the center panel",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page refs { Button(\"Save\", bind: prefs_value, "
        "onClick: app_save, key: \"save\") }",
        "references.lumen"));
    (void)app.shell().renderFrame();

    REQUIRE(findNodeByKey(app.shell().root(), "designer-center-tabs") !=
            nullptr);
    // B 中心视图：设计/分屏/快照（引用已下沉到底部）。
    const auto sourceTab =
        findNodeByKey(app.shell().root(), "designer-view-source");
    REQUIRE(sourceTab != nullptr);
    CHECK(sourceTab->enabled);

    app.shell().handlers().at("designer:view-source")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-source-panel") !=
            nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-source-line-content:1") != nullptr);
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-source-line-content:1")
              ->text.find("Button") != std::string::npos);

    app.shell().handlers().at("designer:bottom-references")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-references-panel") !=
            nullptr);
    REQUIRE(findNodeByKey(app.shell().root(), "designer-references") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(), "designer-references-status") !=
            nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-references-status")->text ==
          "2 refs  /  0 missing");

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    const auto saveId = outline->id;
    const auto* row = findNodeByKey(
        app.shell().root(),
        "designer-references:item:designer-reference:" +
            std::to_string(saveId) + ":onClick");
    REQUIRE(row != nullptr);
    CHECK(app.shell().controller().activateCollectionRow(*row));
    CHECK(app.workbench().selection().primary == saveId);
}

TEST_CASE("designer app routes structural keyboard commands",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().commands().find("designer.undo") != nullptr);
    REQUIRE(app.shell().commands().find("designer.redo") != nullptr);
    REQUIRE(app.shell().commands().find("designer.save") != nullptr);
    REQUIRE(app.shell().commands().find("designer.run") != nullptr);
    REQUIRE(app.shell().commands().find("designer.move-down") != nullptr);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 2);
    const auto rootId = initial->id;
    const auto titleId = initial->children.front().id;
    const auto saveId = initial->children[1].id;
    const auto selectTitle = app.shell().handlers().find(
        "designer:select:" + std::to_string(titleId));
    REQUIRE(selectTitle != app.shell().handlers().end());
    selectTitle->second();
    (void)app.shell().renderFrame();

    app.shell().keyDown(Key::Down, lumen::core::kModifierCtrl);
    (void)app.shell().renderFrame();
    REQUIRE(app.workbench().selection().primary == titleId);
    REQUIRE(app.workbench().outline()->children[0].id == saveId);
    CHECK(app.workbench().outline()->children[1].id == titleId);

    app.shell().keyDown(Key::Delete);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == rootId);
    CHECK(app.workbench().outline()->children[0].id == saveId);

    CHECK(app.undo());
    (void)app.shell().renderFrame();
    CHECK(app.workbench().selection().primary == titleId);
    CHECK(app.workbench().outline()->children[1].id == titleId);
}

TEST_CASE("designer app clears preview state across structural edits",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 1);
    // 默认样例 §8.1 后 front() 是容器；本用例的预览态键契约为 "title"。
    const auto* titleNode = findNodeByKey(app.shell().root(), "title");
    REQUIRE(titleNode != nullptr);
    REQUIRE(titleNode->onClick.rfind("designer:select:", 0) == 0);
    const auto titleId = static_cast<lumen::dsl::DesignNodeId>(
        std::strtoull(titleNode->onClick.c_str() + 16, nullptr, 10));
    const auto selectTitle = app.shell().handlers().find(
        "designer:select:" + std::to_string(titleId));
    REQUIRE(selectTitle != app.shell().handlers().end());
    selectTitle->second();
    const auto preview = app.shell().handlers().find("designer:preview-state");
    REQUIRE(preview != app.shell().handlers().end());
    preview->second();
    (void)app.shell().renderFrame();

    const auto previewKey = "title";
    REQUIRE(app.shell().styleContext().previewStates != nullptr);
    REQUIRE(app.shell().styleContext().previewStates->contains(previewKey));
    CHECK(app.shell().styleContext().previewStates->at(previewKey).hovered);

    app.shell().keyDown(Key::Delete);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().styleContext().previewStates->contains(previewKey));
    CHECK(app.shell().styleContext().previewStates->at(previewKey) ==
          lumen::style::WidgetState{});

    REQUIRE(app.undo());
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().styleContext().previewStates->contains(previewKey));
    CHECK(app.shell().styleContext().previewStates->at(previewKey) ==
          lumen::style::WidgetState{});
}

TEST_CASE("designer app reorders outline rows by pointer drag",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，大纲操作前切到结构页。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 2);
    const auto rootId = initial->id;
    const auto titleId = initial->children.front().id;
    const auto saveId = initial->children[1].id;
    const auto* titleRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + initial->children.front().path);
    const auto* saveRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + initial->children[1].path);
    REQUIRE(titleRow != nullptr);
    REQUIRE(saveRow != nullptr);
    const auto titleOrigin = absoluteOffset(app.shell().root(), titleRow->key);
    const auto saveOrigin = absoluteOffset(app.shell().root(), saveRow->key);
    const Offset titlePoint =
        titleOrigin + Offset{titleRow->size.width * 0.5F,
                             titleRow->size.height * 0.5F};
    const Offset savePoint =
        saveOrigin + Offset{saveRow->size.width * 0.5F,
                             saveRow->size.height * 0.25F};

    app.shell().pointerDown(titlePoint);
    (void)app.shell().renderFrame();
    app.shell().pointerMove(savePoint);
    (void)app.shell().renderFrame();
    CHECK(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(savePoint);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children[0].id == saveId);
    CHECK(app.workbench().outline()->children[1].id == titleId);
    CHECK(app.workbench().selection().primary == titleId);

    REQUIRE(app.undo());
    CHECK(app.workbench().outline()->children[0].id == titleId);
    CHECK(app.workbench().outline()->children[1].id == saveId);
    CHECK(app.workbench().selection().primary == rootId);
}

TEST_CASE("designer app inserts toolbox nodes by pointer drag",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto* toolbox = findNodeByKey(app.shell().root(),
                                        "designer-toolbox:Text");
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(toolbox != nullptr);
    REQUIRE(canvas != nullptr);
    const auto toolboxOrigin = absoluteOffset(app.shell().root(), toolbox->key);
    const auto canvasOrigin = absoluteOffset(app.shell().root(), canvas->key);
    const Offset toolboxPoint =
        toolboxOrigin + Offset{toolbox->size.width * 0.5F,
                               toolbox->size.height * 0.5F};
    const Offset canvasPoint =
        canvasOrigin + Offset{canvas->size.width * 0.5F,
                              canvas->size.height * 0.5F};

    app.shell().pointerDown(toolboxPoint);
    (void)app.shell().renderFrame();
    app.shell().pointerMove(canvasPoint);
    (void)app.shell().renderFrame();
    CHECK(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(canvasPoint);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children.size() ==
          initial->children.size() + 1);
    REQUIRE(app.workbench().selection().primary.has_value());
    const auto insertedId = *app.workbench().selection().primary;
    CHECK(insertedId != rootId);
    CHECK(app.workbench().outline()->children.back().id == insertedId);
    CHECK(app.workbench().outline()->children.back().type == "Text");
}

TEST_CASE("designer app drags layout toolbox entries onto the canvas",
          "[designer][d3][app][drag]") {
    // 布局组条目与 Text 同一条拖放路径：按住工具箱行越阈值起会话，落
    // 画布释放插入当前选中节点；Splitter 按 schema 补足 2 个 Container
    // 子节点（原子插入）；落画布外释放既不插入也不触发点击插入。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto rootId = initial->id;
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(canvas != nullptr);
    const auto canvasOrigin = absoluteOffset(app.shell().root(), canvas->key);
    const Offset canvasPoint{
        canvasOrigin.x + canvas->size.width * 0.5F,
        canvasOrigin.y + canvas->size.height * 0.5F};

    const auto rootChildCount = [&] {
        return app.workbench().outline()->children.size();
    };
    const auto selectRoot = [&] {
        app.shell().handlers().at("designer:select:" +
                                  std::to_string(rootId))();
        (void)app.shell().renderFrame();
    };
    const auto dragRowTo = [](lumen::app::AppShell& shell, const Offset& from,
                              const Offset& to) {
        shell.pointerDown(from);
        shell.pointerMove(to);
        (void)shell.renderFrame();
        const bool dragging = shell.controller().dragSessionActive();
        shell.pointerUp(to);
        (void)shell.renderFrame();
        return dragging;
    };

    // 布局组逐类拖入（含 ScrollView 自身与需补子的 Splitter）。
    for (const auto* type : {"Column", "Grid", "ScrollView", "Splitter"}) {
        selectRoot();
        const auto before = rootChildCount();
        const auto* row = findNodeByKey(app.shell().root(),
                                        std::string{"designer-toolbox:"} + type);
        REQUIRE(row != nullptr);
        const auto rowOrigin = absoluteOffset(app.shell().root(), row->key);
        const Offset rowPoint{rowOrigin.x + row->size.width * 0.5F,
                              rowOrigin.y + row->size.height * 0.5F};
        CHECK(dragRowTo(app.shell(), rowPoint, canvasPoint));
        REQUIRE(rootChildCount() == before + 1);
        REQUIRE(app.workbench().selection().primary.has_value());
        CHECK(*app.workbench().selection().primary != rootId);
        const auto* inserted = findDesignNodeById(
            app.workbench().document()->root,
            *app.workbench().selection().primary);
        REQUIRE(inserted != nullptr);
        CHECK(inserted->type == type);
        if (std::string_view{type} == "Splitter") {
            REQUIRE(inserted->children.size() == 2);
            CHECK(inserted->children[0].type == "Container");
            CHECK(inserted->children[1].type == "Container");
        }
    }

    // 布局容器成为插入目标：拖入 Column 后选中它，再拖 Text 落画布
    // → Text 插到 Column 内而非根层级。
    selectRoot();
    const auto rootBefore = rootChildCount();
    const auto* columnRow =
        findNodeByKey(app.shell().root(), "designer-toolbox:Column");
    REQUIRE(columnRow != nullptr);
    auto columnOrigin = absoluteOffset(app.shell().root(), columnRow->key);
    Offset columnPoint{columnOrigin.x + columnRow->size.width * 0.5F,
                       columnOrigin.y + columnRow->size.height * 0.5F};
    CHECK(dragRowTo(app.shell(), columnPoint, canvasPoint));
    REQUIRE(rootChildCount() == rootBefore + 1);
    const auto columnId = *app.workbench().selection().primary;
    const auto* textRow =
        findNodeByKey(app.shell().root(), "designer-toolbox:Text");
    REQUIRE(textRow != nullptr);
    const auto textOrigin = absoluteOffset(app.shell().root(), textRow->key);
    const Offset textPoint{textOrigin.x + textRow->size.width * 0.5F,
                           textOrigin.y + textRow->size.height * 0.5F};
    CHECK(dragRowTo(app.shell(), textPoint, canvasPoint));
    CHECK(rootChildCount() == rootBefore + 1);
    const auto* column =
        findDesignNodeById(app.workbench().document()->root, columnId);
    REQUIRE(column != nullptr);
    REQUIRE(column->children.size() == 1);
    CHECK(column->children.back().type == "Text");

    // 落点在画布外（仍悬在左面板）释放：不插入、不点击。
    selectRoot();
    const auto outsideBefore = rootChildCount();
    const auto* stackRow =
        findNodeByKey(app.shell().root(), "designer-toolbox:Stack");
    REQUIRE(stackRow != nullptr);
    const auto stackOrigin = absoluteOffset(app.shell().root(), stackRow->key);
    const Offset stackPoint{stackOrigin.x + stackRow->size.width * 0.5F,
                            stackOrigin.y + stackRow->size.height * 0.5F};
    CHECK(dragRowTo(app.shell(), stackPoint,
                    stackPoint + Offset{0.0F, 40.0F}));
    CHECK(rootChildCount() == outsideBefore);
}

TEST_CASE("designer toolbox drag keeps working after panel scrolling",
          "[designer][d3][app][drag][scroll]") {
    // 拖放认领先于视口滚动拖动（interaction §指针）：工具箱可滚后，从
    // 滚动过的列表里拖布局条目仍然起会话插入，且拖动不改变面板偏移。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    const auto* toolbox =
        findNodeByKey(app.shell().root(), "designer-toolbox-scroll");
    REQUIRE(toolbox != nullptr);
    REQUIRE(toolbox->scrollExtent > 0.0F);
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(canvas != nullptr);
    const auto canvasOrigin = absoluteOffset(app.shell().root(), canvas->key);
    const Offset canvasPoint{
        canvasOrigin.x + canvas->size.width * 0.5F,
        canvasOrigin.y + canvas->size.height * 0.5F};

    // 先滚离顶部（幅度以 Layout 组仍在可视区为限）。
    const auto toolboxOrigin =
        absoluteOffset(app.shell().root(), "designer-toolbox-scroll");
    CHECK(app.shell().wheel(
        toolboxOrigin + Offset{toolbox->size.width * 0.5F,
                               toolbox->size.height * 0.5F},
        {0.0F, 40.0F}));
    (void)app.shell().renderFrame();
    const auto* scrolled =
        findNodeByKey(app.shell().root(), "designer-toolbox-scroll");
    REQUIRE(scrolled != nullptr);
    REQUIRE(scrolled->scrollOffset == 40.0F);

    const auto* columnRow =
        findNodeByKey(app.shell().root(), "designer-toolbox:Column");
    REQUIRE(columnRow != nullptr);
    const auto columnOrigin = absoluteOffset(app.shell().root(), columnRow->key);
    // 前置：目标行滚动后仍完整落在视口裁剪区内（滚出视口的行不可命中）。
    REQUIRE(columnOrigin.y >= toolboxOrigin.y);
    REQUIRE(columnOrigin.y + columnRow->size.height <=
            toolboxOrigin.y + scrolled->size.height);
    const Offset columnPoint{columnOrigin.x + columnRow->size.width * 0.5F,
                             columnOrigin.y + columnRow->size.height * 0.5F};
    app.shell().pointerDown(columnPoint);
    app.shell().pointerMove(canvasPoint);
    (void)app.shell().renderFrame();
    CHECK(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(canvasPoint);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children.size() ==
          initial->children.size() + 1);
    CHECK(app.workbench().outline()->children.back().type == "Column");
    const auto* after =
        findNodeByKey(app.shell().root(), "designer-toolbox-scroll");
    REQUIRE(after != nullptr);
    CHECK(after->scrollOffset == 40.0F);
}

TEST_CASE("designer app moves stack children by canvas drag",
          "[designer][d3][app][drag]") {
    // 画布拖动换位（Stack 子节点，left/top 归设计者）：按住节点本体拖
    // 动即选中并跟手，吸附 + 钳制在父内容区内；释放一次事务写回
    // left/top，undo 一步还原；Esc 取消不留事务。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Stack(key: \"root\", width: 300, height: 200) { "
        "Container(key: \"node\", width: 60, height: 40, left: 24, top: 24) "
        "} }",
        "stack-move.lumen"));
    (void)app.shell().renderFrame();
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto nodeId = outline->children.front().id;

    const auto property = [&](const char* name) -> double {
        const auto properties = app.workbench().properties(nodeId);
        const auto found = std::find_if(
            properties.begin(), properties.end(),
            [name](const auto& entry) { return entry.name == name; });
        if (found == properties.end() || !found->value.has_value()) {
            return -1.0;
        }
        const auto* number = std::get_if<double>(&found->value->value);
        return number != nullptr ? *number : -1.0;
    };

    const auto* node = findNodeByKey(app.shell().root(), "node");
    REQUIRE(node != nullptr);
    const auto nodeOrigin = absoluteOffset(app.shell().root(), node->key);
    const Offset start{nodeOrigin.x + 10.0F, nodeOrigin.y + 10.0F};
    // 48/32 落在 8px 网格上，避免吸附候选歧义。
    const Offset end{start + Offset{48.0F, 32.0F}};
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    CHECK(app.workbench().selection().primary == nodeId);
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    CHECK(property("left") == 72.0);
    CHECK(property("top") == 56.0);
    CHECK(app.workbench().dirty());
    REQUIRE(app.undo());
    CHECK(property("left") == 24.0);
    CHECK(property("top") == 24.0);

    // Esc 取消：会话终止、字段不动、文档不脏。
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.shell().controller().dragSessionActive());
    CHECK(property("left") == 24.0);
    CHECK(property("top") == 24.0);
}

TEST_CASE("designer default sample opens at 100 percent with guides",
          "[designer][d3][app][drag]") {
    // 默认态契约（§8.6）：缩放默认 100%（不再自动 fit，画板超出可视
    // 区由画布裁剪约束）；参考线默认可见且首帧后即完成物化；卡片文本
    // 完整呈现；Save 按钮等元素可直接拖动换位。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->type == "Stack");
    const auto rootProperties = app.workbench().properties(outline->id);
    const auto readNumber = [&](const char* name) -> double {
        const auto found =
            std::find_if(rootProperties.begin(), rootProperties.end(),
                         [name](const auto& entry) {
                             return entry.name == name &&
                                    entry.value.has_value();
                         });
        REQUIRE(found != rootProperties.end());
        return std::get<double>(found->value->value);
    };
    CHECK(readNumber("width") == 352.0);
    CHECK(readNumber("height") == 264.0);

    // 参考线（网格点 + 标尺）默认可见（存在性不断言被剔除的原点）。
    CHECK(hasCanvasGridDot(app));
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-ruler-top") !=
          nullptr);

    // 缩放读数默认 100%；画板按设计尺寸原样呈现（zoom=1）。
    CHECK(findNodeByKey(app.shell().root(), "designer-zoom-label")->text ==
          "100%");
    const auto* previewRoot = findNodeByKey(app.shell().root(), "root");
    REQUIRE(previewRoot != nullptr);
    CHECK(previewRoot->size.width == 352.0F);
    const auto* cardLabel = findNodeByKey(app.shell().root(), "card-a-label");
    REQUIRE(cardLabel != nullptr);
    CHECK(cardLabel->size.height > 8.0F);

    // Save 按钮是 Stack 子节点：拖动换位，undo 一步回原位。
    const auto* save = findNodeByKey(app.shell().root(), "save");
    REQUIRE(save != nullptr);
    REQUIRE(save->onClick.rfind("designer:select:", 0) == 0);
    const auto saveId = static_cast<lumen::dsl::DesignNodeId>(
        std::strtoull(save->onClick.c_str() + std::strlen("designer:select:"),
                      nullptr, 10));
    const auto saveProperty = [&](const char* name) -> double {
        const auto properties = app.workbench().properties(saveId);
        const auto found =
            std::find_if(properties.begin(), properties.end(),
                         [name](const auto& entry) {
                             return entry.name == name &&
                                    entry.value.has_value();
                         });
        REQUIRE(found != properties.end());
        return std::get<double>(found->value->value);
    };
    const auto saveOrigin = absoluteOffset(app.shell().root(), save->key);
    const Offset start{saveOrigin.x + save->size.width * 0.5F,
                       saveOrigin.y + save->size.height * 0.5F};
    const Offset end{start + Offset{-80.0F, -60.0F}};
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    CHECK(app.workbench().selection().primary == saveId);
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();
    CHECK(saveProperty("left") != 240.0);
    CHECK(saveProperty("top") != 208.0);
    REQUIRE(app.undo());
    CHECK(saveProperty("left") == 240.0);
    CHECK(saveProperty("top") == 208.0);
}

TEST_CASE("designer zoom control uses discrete steps with clamped range",
          "[designer][d3][app]") {
    // §8.6 缩放契约：默认 100%；≤200% 步进 25%，>200% 步进 50%；
    // 上限 500%、下限 10% 钳制。读数经 designer-zoom-label 断言。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto zoomText = [&app] {
        const auto* label =
            findNodeByKey(app.shell().root(), "designer-zoom-label");
        REQUIRE(label != nullptr);
        return label->text;
    };
    const auto zoomBy = [&app](int steps) {
        for (int i = 0; i < steps; ++i) {
            app.shell().handlers().at("designer:zoom-in")();
            (void)app.shell().renderFrame();
        }
    };
    const auto zoomOut = [&app](int steps) {
        for (int i = 0; i < steps; ++i) {
            app.shell().handlers().at("designer:zoom-out")();
            (void)app.shell().renderFrame();
        }
    };

    CHECK(zoomText() == "100%");
    zoomBy(1);
    CHECK(zoomText() == "125%");
    zoomBy(1);
    CHECK(zoomText() == "150%");
    zoomBy(2);
    CHECK(zoomText() == "200%");
    zoomBy(1);
    CHECK(zoomText() == "250%");
    zoomBy(5);
    CHECK(zoomText() == "500%");
    zoomBy(1);
    CHECK(zoomText() == "500%");
    zoomOut(1);
    CHECK(zoomText() == "450%");
    zoomOut(13);
    CHECK(zoomText() == "10%");
    zoomOut(1);
    CHECK(zoomText() == "10%");
    // 复位 = 100%。
    app.shell().handlers().at("designer:zoom-reset")();
    (void)app.shell().renderFrame();
    CHECK(zoomText() == "100%");
}



TEST_CASE("designer canvas wheel zooms with ctrl and pans without",
          "[designer][d3][app][zoom]") {
    // §8.8（designer-canvas.html）：Ctrl+滚轮 = 锚定档位缩放；普通滚
    // 轮 = 平移画布。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();

    const auto zoomText = [&app] {
        return findNodeByKey(app.shell().root(), "designer-zoom-label")->text;
    };
    const auto rootOrigin = [&app] {
        return absoluteOffset(app.shell().root(), "root");
    };
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(canvas != nullptr);
    const auto canvasOrigin = absoluteOffset(app.shell().root(), "designer-canvas");
    const Offset overCanvas{canvasOrigin.x + 60.0F, canvasOrigin.y + 60.0F};

    CHECK(zoomText() == "100%");
    // Ctrl+滚轮（下滚 = 缩小方向？上滚 delta.y<0 = 放大）。
    CHECK(app.shell().wheel(overCanvas, {0.0F, -53.0F},
                            lumen::core::kModifierCtrl));
    (void)app.shell().renderFrame();
    CHECK(zoomText() == "125%");
    CHECK(app.shell().wheel(overCanvas, {0.0F, 53.0F},
                            lumen::core::kModifierCtrl));
    (void)app.shell().renderFrame();
    CHECK(zoomText() == "100%");
    // 普通滚轮 = 平移（缩放不变，画板右移）。
    const auto before = rootOrigin();
    CHECK(app.shell().wheel(overCanvas, {40.0F, 0.0F},
                            lumen::core::kModifierNone));
    (void)app.shell().renderFrame();
    CHECK(zoomText() == "100%");
    CHECK(rootOrigin().x > before.x);
}

TEST_CASE("designer canvas space drag pans the view",
          "[designer][d3][app][zoom]") {
    // §8.8：按住 Space 拖动 = 平移画布；释放 Space 后拖动恢复常规语义。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();

    const auto rootOrigin = [&app] {
        return absoluteOffset(app.shell().root(), "root");
    };
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    const auto canvasOrigin =
        absoluteOffset(app.shell().root(), "designer-canvas");
    const Offset start{canvasOrigin.x + 200.0F, canvasOrigin.y + 200.0F};
    const Offset end{start + Offset{60.0F, 40.0F}};

    // 按住 Space：拖动画布中心 → 平移（不选中、不改文档）。
    const auto before = rootOrigin();
    const auto revision = app.workbench().documentRevision();
    app.shell().keyDown(lumen::core::Key::None, lumen::core::kModifierNone,
                        ' ');
    app.shell().pointerDown(start);
    // 越阈启动（Start/Move 同位）后再走增量位移，平移量 = 后续增量。
    app.shell().pointerMove(start + Offset{16.0F, 12.0F});
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();
    CHECK(rootOrigin() != before);
    CHECK(app.workbench().documentRevision() == revision);

    // 释放 Space 后：普通拖动不再平移（恢复换位/橡皮筋语义）。
    app.shell().keyUp(lumen::core::Key::None, lumen::core::kModifierNone,
                      ' ');
    (void)app.shell().renderFrame();
    const auto afterSpace = rootOrigin();
    app.shell().pointerDown(start);
    app.shell().pointerMove(start + Offset{16.0F, 12.0F});
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();
    CHECK(rootOrigin() == afterSpace);
}

TEST_CASE("designer canvas selection chrome matches the mockup chips",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();

    const auto* save = findNodeByKey(app.shell().root(), "save");
    REQUIRE(save != nullptr);
    app.shell().handlers().at(
        "designer:select:" +
        std::to_string(static_cast<lumen::dsl::DesignNodeId>(std::strtoull(
            save->onClick.c_str() + 16, nullptr, 10))))();
    (void)app.shell().renderFrame();

    const auto* nameChip =
        findNodeByKey(app.shell().root(), "designer-canvas-selection-name");
    REQUIRE(nameChip != nullptr);
    CHECK(nameChip->text == "Button · save");
    const auto* sizeChip =
        findNodeByKey(app.shell().root(), "designer-canvas-dimensions");
    REQUIRE(sizeChip != nullptr);
    CHECK(sizeChip->text.find("×") != std::string::npos);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-guide-chip") ==
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-guide-v") ==
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-guide-h") ==
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-ruler-mark-x") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-ruler-mark-y") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-page-caption-name") != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-legend") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-hint") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-ruler-corner") !=
          nullptr);
    // 尺寸读数（画板名义尺寸）。
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-size-label")
              ->text == "桌面表单 · 352 × 264 lp");
}

TEST_CASE("designer canvas bounds show page and parent outlines",
          "[designer][d3][app]") {
    // §8.7 容器边界（designer-canvas.html）：页面虚线边界默认可见；选
    // 中节点时其父容器带虚线轮廓与"类型 · key"标注；独立开关可关。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();

    // 页面边界默认在场。
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-page-dash:0:0") !=
          nullptr);
    // 未选中的父容器为文档根：不重复画父轮廓。
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-bounds-dash:0:0") == nullptr);

    // 选中卡片内文本：父容器 = card-a，轮廓 + 标注在场。
    const auto* label = findNodeByKey(app.shell().root(), "card-a-label");
    REQUIRE(label != nullptr);
    REQUIRE(label->onClick.rfind("designer:select:", 0) == 0);
    app.shell().handlers().at(
        "designer:select:" +
        std::to_string(static_cast<lumen::dsl::DesignNodeId>(std::strtoull(
            label->onClick.c_str() + 16, nullptr, 10))))();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-bounds-dash:0:0") != nullptr);
    const auto* caption =
        findNodeByKey(app.shell().root(), "designer-canvas-bounds-caption");
    REQUIRE(caption != nullptr);
    CHECK(caption->text == "Container · card-a");

    // 独立开关：关闭后边界消失（网格不受影响）。
    app.shell().handlers().at("designer:canvas-bounds")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas-page-dash:0:0") ==
          nullptr);
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-bounds-dash:0:0") == nullptr);
    CHECK(hasCanvasGridDot(app));
}
TEST_CASE("designer fit view scales the artboard into the canvas",
          "[designer][d3][app]") {
    // §8.6/§8.7："适合"命令按画布内容区缩小居中画板（不放大），复位
    // 回 100%。默认样例 352×264 在默认窗口 100% 已完整，故用大画板
    // 自有源验证缩放路径。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1024.0F, 768.0F});
    REQUIRE(app.loadSource(
        "page fit { Stack(key: \"root\", width: 800, height: 600) {"
        " Text(\"Big\", key: \"node\", left: 40, top: 40) } }",
        "fit-view.lumen"));
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();

    CHECK(findNodeByKey(app.shell().root(), "designer-zoom-label")->text ==
          "100%");
    app.shell().handlers().at("designer:zoom-fit")();
    (void)app.shell().renderFrame();
    const auto fittedText =
        findNodeByKey(app.shell().root(), "designer-zoom-label")->text;
    CHECK(fittedText != "100%");
    const auto* fitted = findNodeByKey(app.shell().root(), "root");
    REQUIRE(fitted != nullptr);
    // §8.8 fit 边距 120/100（HTML fit() 同口径）：1024 视口 ≈244。
    CHECK(fitted->size.width < 800.0F);
    CHECK(fitted->size.width > 200.0F);
    app.shell().handlers().at("designer:zoom-reset")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-zoom-label")->text ==
          "100%");
    // 100% 下 800 宽画板被画布约束钳制（单轴视口语义），不为 800。
    const auto* reset = findNodeByKey(app.shell().root(), "root");
    REQUIRE(reset != nullptr);
    CHECK(reset->size.width < 800.0F);
}

TEST_CASE("designer canvas drag snaps to sibling edges with dashed guides",
          "[designer][d3][app][drag]") {
    // §8.3 虚线对齐参考：拖动换位的吸附候选 = 父框 0/中/尾 + 兄弟节点
    // 边/中线；命中阈值（6px/zoom）内吸附并画原位虚线框 + 全幅对齐虚
    // 线；未命中回落 8px 网格。释放后装饰清理。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page guides { Stack(key: \"root\", width: 300, height: 200) {"
        " Container(key: \"node\", width: 60, height: 40, left: 24, top: 24)"
        " Container(key: \"peer\", width: 60, height: 40, left: 150, top: 96)"
        " } }",
        "drag-guides.lumen"));
    (void)app.shell().renderFrame();

    const auto* node = findNodeByKey(app.shell().root(), "node");
    REQUIRE(node != nullptr);
    REQUIRE(node->onClick.rfind("designer:select:", 0) == 0);
    const auto nodeId = static_cast<lumen::dsl::DesignNodeId>(
        std::strtoull(node->onClick.c_str() + 16, nullptr, 10));
    const auto property = [&](const char* name) -> double {
        const auto properties = app.workbench().properties(nodeId);
        const auto found =
            std::find_if(properties.begin(), properties.end(),
                         [name](const auto& entry) {
                             return entry.name == name &&
                                    entry.value.has_value();
                         });
        REQUIRE(found != properties.end());
        return std::get<double>(found->value->value);
    };

    // 拖 121px：原始 x = 145（网格回落 144），兄弟左缘 150 在阈值 6 内
    // → 吸附 150（左对齐 peer 左缘）。
    const auto nodeOrigin = absoluteOffset(app.shell().root(), node->key);
    const Offset start{nodeOrigin.x + 10.0F, nodeOrigin.y + 10.0F};
    const Offset end{start + Offset{121.0F, 0.0F}};
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    // 原位虚线框（0..3）与竖向对齐虚线（4）在场。
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-drag-dash:0:0") != nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-canvas-drag-dash:4:0") != nullptr);
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    CHECK(property("left") == 150.0);
    CHECK(property("top") == 24.0);
    CHECK(findNodeByKey(app.shell().root(),
                        "designer-canvas-drag-dash:0:0") == nullptr);
}


TEST_CASE("designer toolbox insert falls back to parent when leaf selected",
          "[designer][d3][app][drag]") {
    // §4.1 契约补全：所选是叶子（Text 等 maxChildren==0）时插入到其父
    // 容器、紧随其后——此前叶子选中时静默失败（仅落 schema 诊断），工
    // 具箱拖放表现为"拖不进去"。容器已满时给状态行反馈而非静默。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto titleId = [&app] {
        const auto* title = findNodeByKey(app.shell().root(), "title");
        REQUIRE(title != nullptr);
        REQUIRE(title->onClick.rfind("designer:select:", 0) == 0);
        return static_cast<lumen::dsl::DesignNodeId>(
            std::strtoull(title->onClick.c_str() + 16, nullptr, 10));
    }();
    const auto titleIndex = [&] {
        const auto outline = app.workbench().outline();
        REQUIRE(outline.has_value());
        for (std::size_t index = 0; index < outline->children.size();
             ++index) {
            if (outline->children[index].id == titleId) return index;
        }
        FAIL("title not found in outline");
        return std::size_t{0};
    }();

    // 点击 title（叶子选中），拖 Column 到画布。
    const auto* title = findNodeByKey(app.shell().root(), "title");
    const auto titleOrigin = absoluteOffset(app.shell().root(), title->key);
    const Offset titlePoint{titleOrigin.x + 5.0F, titleOrigin.y + 5.0F};
    app.shell().pointerDown(titlePoint);
    app.shell().pointerUp(titlePoint);
    (void)app.shell().renderFrame();

    const auto rootCountBefore = app.workbench().outline()->children.size();
    const auto* row = findNodeByKey(app.shell().root(),
                                    "designer-toolbox:Column");
    REQUIRE(row != nullptr);
    const auto rowOrigin = absoluteOffset(app.shell().root(), row->key);
    const Offset start{rowOrigin.x + 20.0F, rowOrigin.y + 10.0F};
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    const auto canvasOrigin = absoluteOffset(app.shell().root(), canvas->key);
    const Offset end{canvasOrigin.x + canvas->size.width * 0.5F,
                     canvasOrigin.y + canvas->size.height * 0.5F};
    app.shell().pointerDown(start);
    app.shell().pointerMove(end);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(end);
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(outline->children.size() == rootCountBefore + 1);
    // 落在 title 之后（同父紧随其后的兄弟）。
    CHECK(outline->children[titleIndex].type == "Text");
    CHECK(outline->children[titleIndex + 1].type == "Column");
    CHECK(app.diagnostics().empty());

    // 点击插入路径同样回退：叶子选中时激活工具箱命令。
    app.shell().handlers().at("designer:select:" + std::to_string(titleId))();
    (void)app.shell().renderFrame();
    const auto countBeforeCommand = app.workbench().outline()->children.size();
    app.shell().handlers().at("designer:toolbox:Text")();
    (void)app.shell().renderFrame();
    CHECK(app.workbench().outline()->children.size() ==
          countBeforeCommand + 1);
    CHECK(app.diagnostics().empty());

    // 容器已满（card 是单子 Container 且已有 label）：状态行反馈，不动
    // 文档。
    const auto* card = findNodeByKey(app.shell().root(), "card-a");
    REQUIRE(card != nullptr);
    app.shell().handlers().at(
        "designer:select:" +
        std::to_string(static_cast<lumen::dsl::DesignNodeId>(std::strtoull(
            card->onClick.c_str() + 16, nullptr, 10))))();
    (void)app.shell().renderFrame();
    const auto countBeforeFull = app.workbench().outline()->children.size();
    app.shell().handlers().at("designer:toolbox:Text")();
    (void)app.shell().renderFrame();
    CHECK(app.workbench().outline()->children.size() == countBeforeFull);
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Container 已满，无法插入");
    CHECK(app.diagnostics().empty());
}

TEST_CASE("designer toolbox drag with incremental motion inserts",
          "[designer][d3][app][drag]") {
    // 真实鼠标输入近似：多次小步移动（≤8px/步）且步间渲染帧。会话启
    // 动阈值按累计位移（锚点起算），增量移动不得影响认领与落点判定。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto before = app.workbench().outline()->children.size();
    const auto* row = findNodeByKey(app.shell().root(),
                                    "designer-toolbox:Button");
    REQUIRE(row != nullptr);
    const auto rowOrigin = absoluteOffset(app.shell().root(), row->key);
    Offset current{rowOrigin.x + 20.0F, rowOrigin.y + 10.0F};
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    const auto canvasOrigin = absoluteOffset(app.shell().root(), canvas->key);
    const Offset target{canvasOrigin.x + canvas->size.width * 0.6F,
                        canvasOrigin.y + canvas->size.height * 0.6F};

    app.shell().pointerDown(current);
    (void)app.shell().renderFrame();
    // 先抖动两小步（未越阈值不应起会话）。
    current = current + Offset{2.0F, 1.0F};
    app.shell().pointerMove(current);
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.shell().controller().dragSessionActive());
    // 分 40 步走完到画布，每 5 步渲染一帧。
    const Offset step{(target.x - current.x) / 40.0F,
                      (target.y - current.y) / 40.0F};
    for (int i = 0; i < 40; ++i) {
        current = current + step;
        app.shell().pointerMove(current);
        if (i % 5 == 0) (void)app.shell().renderFrame();
    }
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(current);
    (void)app.shell().renderFrame();

    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    CHECK(outline->children.size() == before + 1);
    CHECK(outline->children.back().type == "Button");
    CHECK(app.diagnostics().empty());
}

TEST_CASE("designer canvas refuses to drag flow-layout children",
          "[designer][d3][app][drag]") {
    // Row/Column 子节点位置由布局拥有：按压拖动不起换位会话（也不误
    // 起橡皮筋/滚动），释放仍走点击选中。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page preview { Column(key: \"root\", width: 300, height: 200) { "
        "Text(\"Flow\", key: \"node\", width: 60, height: 40) } }",
        "flow-move.lumen"));
    (void)app.shell().renderFrame();
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto nodeId = outline->children.front().id;
    const auto initialSelection = app.workbench().selection().primary;
    const auto* node = findNodeByKey(app.shell().root(), "node");
    REQUIRE(node != nullptr);
    const auto nodeOrigin = absoluteOffset(app.shell().root(), node->key);
    const Offset start{nodeOrigin.x + 10.0F, nodeOrigin.y + 10.0F};
    app.shell().pointerDown(start);
    app.shell().pointerMove(start + Offset{48.0F, 32.0F});
    (void)app.shell().renderFrame();
    CHECK_FALSE(app.shell().controller().dragSessionActive());
    app.shell().pointerUp(start + Offset{48.0F, 32.0F});
    (void)app.shell().renderFrame();
    // 越 slop 的释放不触发点击：选中保持原状（未选中被拖的节点）。
    CHECK(app.workbench().selection().primary == initialSelection);
    CHECK(app.workbench().selection().primary != nodeId);
}

TEST_CASE("designer app cancels outline drag with Escape",
          "[designer][d3][app][selection]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，大纲操作前切到结构页。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    const auto initial = app.workbench().outline();
    REQUIRE(initial.has_value());
    REQUIRE(initial->children.size() >= 2);
    const auto rootId = initial->id;
    const auto firstId = initial->children[0].id;
    const auto secondId = initial->children[1].id;
    const auto* firstRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + initial->children[0].path);
    const auto* secondRow = findNodeByKey(
        app.shell().root(), "designer-outline:item:" + initial->children[1].path);
    REQUIRE(firstRow != nullptr);
    REQUIRE(secondRow != nullptr);
    const auto firstOrigin = absoluteOffset(app.shell().root(), firstRow->key);
    const auto secondOrigin = absoluteOffset(app.shell().root(), secondRow->key);
    const Offset firstPoint =
        firstOrigin + Offset{firstRow->size.width * 0.5F,
                             firstRow->size.height * 0.5F};
    const Offset secondPoint =
        secondOrigin + Offset{secondRow->size.width * 0.5F,
                              secondRow->size.height * 0.25F};

    app.shell().pointerDown(firstPoint);
    app.shell().pointerMove(secondPoint);
    (void)app.shell().renderFrame();
    REQUIRE(app.shell().controller().dragSessionActive());
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().outline().has_value());
    CHECK(app.workbench().outline()->children[0].id == firstId);
    CHECK(app.workbench().outline()->children[1].id == secondId);
    CHECK(app.workbench().selection().primary == rootId);
}

TEST_CASE("designer app navigates actionable diagnostics to their node",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    auto broken = *app.workbench().document();
    REQUIRE(!broken.root.children.empty());
    const auto childId = broken.root.children.front().id;
    broken.root.children.front().type = "Unknown";
    CHECK_FALSE(app.workbench().openDocument(std::move(broken)));
    app.shell().markDirty();
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().nodeId == childId);
    // B 底栏默认收起（36），诊断定位前先展开（workspace-design §3.1）。
    app.shell().handlers().at("designer:toggle-bottom")();
    (void)app.shell().renderFrame();
    const auto* diagnostic =
        findNodeByKey(app.shell().root(), "designer-diagnostic:0");
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->type == lumen::core::WidgetType::Button);
    CHECK(diagnostic->onClick == "designer:diagnostic:0");
    CHECK(diagnostic->size.width > 0.0F);
    CHECK(diagnostic->size.height > 0.0F);
    const auto origin = absoluteOffset(app.shell().root(), diagnostic->key);
    const Offset center = origin +
                          Offset{diagnostic->size.width * 0.5F,
                                 diagnostic->size.height * 0.5F};
    CAPTURE(origin.x, origin.y, diagnostic->size.width,
            diagnostic->size.height, center.x, center.y);
    std::vector<const lumen::core::RenderNode*> hitChain;
    REQUIRE(lumen::core::hitTestChain(app.shell().root(), center, hitChain));
    REQUIRE(!hitChain.empty());
    CHECK(hitChain.front()->key == "designer-diagnostic:0");
    app.shell().pointerDown(center);
    app.shell().pointerUp(center);
    (void)app.shell().renderFrame();

    REQUIRE(app.workbench().selection().primary.has_value());
    CHECK(*app.workbench().selection().primary == childId);
    CHECK(findNodeByKey(app.shell().root(), "designer-source-panel") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-source-line:1") !=
          nullptr);
}

TEST_CASE("designer app reopens private design files",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto path = std::filesystem::temp_directory_path() /
                      ("lumen-designer-app-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       ".design");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{path};

    REQUIRE(app.saveDesignFile(path.string()));
    const auto savedDocument = app.workbench().document();
    REQUIRE(savedDocument.has_value());
    REQUIRE(app.loadSource("page other { Text(\"Other\") }",
                           "other.lumen"));
    REQUIRE(app.loadDesignFile(path.string()));
    CHECK(app.workbench().document() == savedDocument);
    CHECK(app.workbench().frame().hasFrame());
    CHECK(app.workbench().diagnostics().empty());

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << "{ broken";
    }
    const auto generation = app.workbench().frame().generation();
    CHECK_FALSE(app.loadDesignFile(path.string()));
    CHECK(app.workbench().frame().generation() == generation);
    REQUIRE(app.workbench().diagnostics().size() == 1);
    CHECK(app.workbench().diagnostics().front().file == path.string());
}

TEST_CASE("designer rejected file opens keep the current save destination",
          "[designer][d3][app][load-atomic]") {
    const auto root = std::filesystem::temp_directory_path() /
        ("lumen-app-rejected-open-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    const auto currentPath = (root / "current.design").string();
    DesignerApp app;
    app.attach();
    REQUIRE(app.loadSource("page current { Text(\"Original\") }"));
    REQUIRE(app.saveDesignFile(currentPath));
    REQUIRE(app.workbench().setProperty(app.workbench().document()->root.id,
        "text", lumen::dsl::DesignValue{std::string{"Local"}}));
    const auto before = *app.workbench().document();
    const auto revision = app.workbench().documentRevision();
    const auto generation = app.workbench().frame().generation();
    const std::string invalidSource =
        "page other { Column { Text(\"A\", key: \"dup\")"
        " Text(\"B\", key: \"dup\") } }";
    lumen::dsl::DocumentStore store;
    std::string rejectedPath;
    std::optional<std::uint64_t> rejectedRevision;
    SECTION("private design file") {
        const auto parsed = lumen::dsl::parseLumenSource(invalidSource);
        REQUIRE(parsed.ok());
        rejectedPath = (root / "other.design").string();
        std::vector<lumen::dsl::DesignError> errors;
        REQUIRE(store.save(rejectedPath, parsed.document, errors));
        rejectedRevision = store.revision(rejectedPath);
        CHECK_FALSE(app.loadDesignFile(rejectedPath));
    }
    SECTION("imported lumen file") {
        rejectedPath = (root / "other.lumen").string();
        {
            std::ofstream output(rejectedPath);
            output << invalidSource;
        }
        rejectedRevision = store.revision(rejectedPath);
        CHECK_FALSE(app.loadFile(rejectedPath));
    }
    SECTION("source string") {
        CHECK_FALSE(app.loadSource(invalidSource, "other.design"));
    }
    REQUIRE(app.workbench().document() == before);
    CHECK(app.workbench().documentRevision() == revision);
    CHECK(app.workbench().frame().generation() == generation);
    CHECK(app.workbench().dirty());
    REQUIRE_FALSE(app.workbench().diagnostics().empty());
    CHECK(app.workbench().diagnostics().front().code ==
          "compile.duplicate_runtime_identity");
    REQUIRE(app.shell().invokeCommand("designer.save"));
    CHECK_FALSE(app.workbench().dirty());
    CHECK(store.load(currentPath).document == before);
    if (!rejectedPath.empty()) {
        REQUIRE(rejectedRevision.has_value());
        CHECK(store.revision(rejectedPath) == rejectedRevision);
    }
}

// DP-9 and prerequisites §4.14: pages own independent edit histories and
// selection; saving advances checkpoints without replacing those histories.
TEST_CASE("designer project pages retain their edit sessions across switches and saves",
          "[designer][dp9][d3][app][project-session]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-project-session-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    std::filesystem::create_directories(root);
    lumen::dsl::DocumentStore documents;
    lumen::dsl::ProjectStore projects;
    std::vector<lumen::dsl::DesignError> diagnostics;
    auto home = lumen::dsl::parseLumenSource(
        "page home { Column { Text(\"Home\") Text(\"Detail\") } }",
        "home.lumen");
    auto settings = lumen::dsl::parseLumenSource(
        "page settings { Column { Text(\"Settings\") Text(\"Detail\") } }",
        "settings.lumen");
    REQUIRE(home.ok());
    REQUIRE(settings.ok());
    const auto homeId = home.document.documentId;
    const auto settingsId = settings.document.documentId;
    const auto homeNode = home.document.root.children[0].id;
    const auto settingsNode = settings.document.root.children[1].id;
    const auto homePath = root / "home.design";
    const auto settingsPath = root / "settings.design";
    REQUIRE(documents.save(homePath.string(), home.document, diagnostics));
    REQUIRE(documents.save(settingsPath.string(), settings.document, diagnostics));
    lumen::dsl::DesignProject project;
    project.projectId = "session-project";
    project.name = "Session project";
    project.root = ".";
    project.pages = {{homeId, "home.design", "Home"},
                     {settingsId, "settings.design", "Settings"}};
    const auto manifest = root / "session.lumen-project";
    REQUIRE(projects.save(manifest.string(), project, diagnostics));
    DesignerApp app;
    app.attach();
    REQUIRE(app.loadProjectFile(manifest.string()));
    REQUIRE(app.workbench().selectNode(homeNode));
    REQUIRE(app.workbench().setProperty(homeNode, "text",
        lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
            std::string{"Home edited"}}}));
    const auto homeEdited = *app.workbench().document();
    REQUIRE(app.workbench().setProperty(homeNode, "width",
        lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{180.0}}));
    const auto homeRedo = *app.workbench().document();
    REQUIRE(app.workbench().undo());
    const auto homeRevision = app.workbench().documentRevision();
    const auto homeSelection = app.workbench().selection();

    SECTION("rejected activation keeps the active page and its edit session") {
        auto broken = settings.document;
        for (auto& child : broken.root.children) {
            child.properties["key"] = lumen::dsl::DesignValue{
                lumen::dsl::DesignValue::Variant{std::string{"duplicate"}}};
        }
        REQUIRE(documents.save(settingsPath.string(), broken, diagnostics));
        REQUIRE(app.loadProjectFile(manifest.string()));
        REQUIRE(app.workbench().selectNode(homeNode));
        REQUIRE(app.workbench().setProperty(homeNode, "text",
            lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
                std::string{"Still editing home"}}}));
        const auto before = *app.workbench().document();
        const auto selection = app.workbench().selection();
        const auto revision = app.workbench().documentRevision();
        const auto frame = app.workbench().frame().generation();
        const auto runtime = app.workbench().frame().session();
        REQUIRE_FALSE(app.switchProjectDocument(settingsId));
        CHECK(app.activeProjectDocumentId() == homeId);
        CHECK(app.workbench().document() == before);
        CHECK(app.workbench().selection() == selection);
        CHECK(app.workbench().documentRevision() == revision);
        CHECK(app.workbench().frame().generation() == frame);
        CHECK(app.workbench().frame().session() == runtime);
        CHECK(app.workbench().dirty());
        REQUIRE(app.workbench().undo());
        CHECK(app.workbench().document() == home.document);
        CHECK_FALSE(app.workbench().dirty());
    }

    SECTION("a standalone open leaves the project and changes the save target") {
        const auto standalone = root / "standalone.design";
        auto document = home.document;
        document.documentId = "standalone-page";
        REQUIRE(documents.save(standalone.string(), document, diagnostics));
        const auto before = documents.revision(homePath.string());
        SECTION("design file") {
            REQUIRE(app.loadDesignFile(standalone.string()));
        }
        SECTION("lumen source") {
            REQUIRE(app.loadSource("page standalone { Text(\"Standalone\") }",
                                   (root / "standalone.lumen").string()));
            REQUIRE(app.saveDesignFile(standalone.string()));
        }
        SECTION("lumen file") {
            const auto lumenFile = root / "standalone.lumen";
            {
                std::ofstream source(lumenFile);
                source << "page standalone { Text(\"Standalone\") }";
                REQUIRE(source.good());
            }
            REQUIRE(app.loadFile(lumenFile.string()));
            REQUIRE(app.saveDesignFile(standalone.string()));
        }
        SECTION("single page save as") {
            REQUIRE(app.saveDesignFile(standalone.string()));
        }
        CHECK_FALSE(app.project().has_value());
        CHECK(app.activeProjectDocumentId().empty());
        REQUIRE(app.workbench().setProperty(app.workbench().document()->root.id,
            "width", lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
                240.0}}));
        const auto edited = *app.workbench().document();
        REQUIRE(app.shell().invokeCommand("designer.save"));
        CHECK(documents.load(standalone.string()).document == edited);
        CHECK(documents.revision(homePath.string()) == before);
        CHECK_FALSE(app.switchProjectDocument(homeId));
    }

    SECTION("switches isolate colliding node ids and keep both history branches") {
        const auto generation = app.workbench().frame().generation();
        REQUIRE(app.switchProjectDocument(homeId));
        CHECK(app.workbench().frame().generation() == generation);
        CHECK(app.workbench().dirty());
        CHECK(app.workbench().canUndo());
        CHECK(app.workbench().canRedo());
        REQUIRE(app.switchProjectDocument(settingsId));
        CHECK(app.workbench().selection().ids ==
              std::set<lumen::dsl::DesignNodeId>{settings.document.root.id});
        CHECK_FALSE(app.workbench().dirty());
        CHECK_FALSE(app.workbench().canUndo());
        REQUIRE(app.workbench().selectNode(settingsNode));
        REQUIRE(app.workbench().setProperty(settingsNode, "text",
            lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
                std::string{"Settings edited"}}}));
        const auto settingsEdited = *app.workbench().document();
        const auto settingsSelection = app.workbench().selection();
        REQUIRE(app.switchProjectDocument(homeId));
        CHECK(app.workbench().document() == homeEdited);
        CHECK(app.workbench().selection() == homeSelection);
        CHECK(app.workbench().documentRevision() == homeRevision);
        CHECK(app.workbench().dirty());
        CHECK(app.workbench().canUndo());
        CHECK(app.workbench().canRedo());
        REQUIRE(app.workbench().redo());
        CHECK(app.workbench().document() == homeRedo);
        REQUIRE(app.workbench().undo());
        REQUIRE(app.workbench().undo());
        CHECK(app.workbench().document() == home.document);
        CHECK_FALSE(app.workbench().dirty());
        REQUIRE(app.switchProjectDocument(settingsId));
        CHECK(app.workbench().document() == settingsEdited);
        CHECK(app.workbench().selection() == settingsSelection);
        CHECK(app.workbench().dirty());
        REQUIRE(app.workbench().undo());
        CHECK(app.workbench().document() == settings.document);
        CHECK_FALSE(app.workbench().dirty());
    }

    SECTION("save and save as keep every page's undo redo and saved checkpoint") {
        REQUIRE(app.switchProjectDocument(settingsId));
        REQUIRE(app.workbench().selectNode(settingsNode));
        REQUIRE(app.workbench().setProperty(settingsNode, "text",
            lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
                std::string{"Settings edited"}}}));
        const auto settingsEdited = *app.workbench().document();
        const auto settingsSelection = app.workbench().selection();
        std::string target = manifest.string();
        SECTION("save") {}
        SECTION("save as") {
            target = (root / "copy" / "session.lumen-project").string();
        }
        REQUIRE(app.saveProjectFile(target));
        CHECK_FALSE(app.workbench().dirty());
        CHECK(app.workbench().selection() == settingsSelection);
        REQUIRE(app.workbench().undo());
        CHECK(app.workbench().document() == settings.document);
        CHECK(app.workbench().dirty());
        REQUIRE(app.workbench().redo());
        CHECK(app.workbench().document() == settingsEdited);
        CHECK_FALSE(app.workbench().dirty());
        REQUIRE(app.switchProjectDocument(homeId));
        CHECK(app.workbench().document() == homeEdited);
        CHECK(app.workbench().selection() == homeSelection);
        CHECK(app.workbench().documentRevision() == homeRevision);
        CHECK_FALSE(app.workbench().dirty());
        CHECK(app.workbench().canUndo());
        CHECK(app.workbench().canRedo());
        REQUIRE(app.workbench().redo());
        CHECK(app.workbench().document() == homeRedo);
        CHECK(app.workbench().dirty());
        REQUIRE(app.workbench().undo());
        CHECK_FALSE(app.workbench().dirty());
        REQUIRE(app.workbench().undo());
        CHECK(app.workbench().document() == home.document);
        CHECK(app.workbench().dirty());
        REQUIRE(app.shell().invokeCommand("designer.save"));
        CHECK_FALSE(app.workbench().dirty());
        const auto targetRoot = std::filesystem::path{target}.parent_path();
        CHECK(documents.load((targetRoot / "home.design").string()).document ==
              home.document);
        CHECK(documents.load((targetRoot / "settings.design").string()).document ==
              settingsEdited);
    }
}

TEST_CASE("designer app opens, switches, and saves a multi document project",
          "[designer][dp9][d3][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-project-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    std::filesystem::create_directories(root / "images", error);
    REQUIRE_FALSE(error);
    {
        std::ofstream resource(root / "images" / "logo.png",
                               std::ios::binary | std::ios::trunc);
        REQUIRE(resource.good());
        resource << "designer-logo";
    }
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};

    DesignerApp seed;
    seed.attach();
    REQUIRE(seed.loadSource("page home { Text(\"Home\") }", "home.lumen"));
    REQUIRE(seed.saveDesignFile((root / "home.design").string()));
    const auto homeId = seed.workbench().document()->documentId;
    REQUIRE(seed.loadSource("page settings { Text(\"Settings\") }",
                            "settings.lumen"));
    REQUIRE(seed.saveDesignFile((root / "settings.design").string()));
    const auto settingsId = seed.workbench().document()->documentId;

    lumen::dsl::DesignProject project;
    project.projectId = "designer.project";
    project.name = "Designer project";
    project.root = ".";
    project.pages = {
        {homeId, "home.design", "Home"},
        {settingsId, "settings.design", "Settings"},
    };
    project.resources = {
        {"project://images/logo.png", "images/logo.png", "image"},
    };
    lumen::dsl::ProjectStore projectStore;
    std::vector<lumen::dsl::DesignError> diagnostics;
    const auto manifest = root / "demo.lumen-project";
    REQUIRE(projectStore.save(manifest.string(), project, diagnostics));

    DesignerApp app;
    app.attach();
    REQUIRE(app.loadProjectFile(manifest.string()));
    REQUIRE(app.project().has_value());
    CHECK(app.project()->pages.size() == 2);
    CHECK(app.activeProjectDocumentId() == homeId);
    CHECK(app.workbench().document()->documentId == homeId);
    CHECK(app.projectDiagnostics().empty());

    REQUIRE(app.switchProjectDocument(settingsId));
    CHECK(app.activeProjectDocumentId() == settingsId);
    CHECK(app.workbench().document()->pageName == "settings");
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-project-panel") != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-project-page:" + settingsId) !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-project-resources-heading") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-project-resource:0") !=
          nullptr);

    REQUIRE(app.saveProjectFile(manifest.string()));
    const auto saved = projectStore.load(manifest.string());
    REQUIRE(saved.ok());
    CHECK(saved.project.pages[1].documentId == settingsId);

    REQUIRE(app.switchProjectDocument(homeId));
    REQUIRE(app.workbench().setProperty(
        app.workbench().document()->root.id, "text",
        lumen::dsl::DesignValue{
            lumen::dsl::DesignValue::Variant{std::string{"Edited home"}}}));
    REQUIRE(app.saveProjectFile(manifest.string()));
    CHECK_FALSE(app.workbench().dirty());
    REQUIRE(app.workbench().setProperty(
        app.workbench().document()->root.id, "text",
        lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
            std::string{"Edited home again"}}}));
    const auto homePath = root / "home.design";
    const auto settingsPath = root / "settings.design";
    lumen::dsl::DocumentStore documentStore;
    auto externalSettings = documentStore.load(settingsPath.string());
    REQUIRE(externalSettings.ok());
    externalSettings.document.pageName = "External settings";
    std::vector<lumen::dsl::DesignError> externalDiagnostics;
    REQUIRE(documentStore.save(settingsPath.string(),
                               externalSettings.document, externalDiagnostics));
    const auto homeBeforeConflict = documentStore.load(homePath.string());
    REQUIRE(homeBeforeConflict.ok());
    CHECK_FALSE(app.saveProjectFile(manifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "store.revision_conflict");
    const auto homeAfterConflict = documentStore.load(homePath.string());
    REQUIRE(homeAfterConflict.ok());
    CHECK(homeAfterConflict.document == homeBeforeConflict.document);

    const auto copyRoot = root / "copy";
    std::filesystem::create_directories(copyRoot, error);
    REQUIRE_FALSE(error);
    const auto copyManifest = copyRoot / "copy.lumen-project";
    REQUIRE(app.saveProjectFile(copyManifest.string()));
    CHECK(app.projectDiagnostics().empty());
    CHECK(std::filesystem::exists(copyRoot / "home.design"));
    CHECK(std::filesystem::exists(copyRoot / "settings.design"));
    REQUIRE(std::filesystem::exists(copyRoot / "images" / "logo.png"));
    std::ifstream copiedResource(copyRoot / "images" / "logo.png",
                                 std::ios::binary);
    REQUIRE(copiedResource.good());
    CHECK(std::string{std::istreambuf_iterator<char>{copiedResource},
                      std::istreambuf_iterator<char>()} == "designer-logo");

    DesignerApp copied;
    copied.attach();
    REQUIRE(copied.loadProjectFile(copyManifest.string()));
    CHECK(copied.activeProjectDocumentId() == homeId);
    CHECK(copied.workbench().document()->root.properties.at("text") ==
          lumen::dsl::DesignValue{lumen::dsl::DesignValue::Variant{
              std::string{"Edited home again"}}});

    auto unsafeResourceProject = project;
    unsafeResourceProject.projectId = "unsafe.resource";
    unsafeResourceProject.name = "Unsafe resource";
    unsafeResourceProject.resources.front().path = "../outside.png";
    const auto unsafeManifest = copyRoot / "unsafe-resource.lumen-project";
    std::vector<lumen::dsl::DesignError> unsafeDiagnostics;
    REQUIRE(projectStore.save(unsafeManifest.string(), unsafeResourceProject,
                              unsafeDiagnostics));
    REQUIRE(app.loadProjectFile(unsafeManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "project.resource_path");
    CHECK_FALSE(app.saveProjectFile(unsafeManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "project.resource_path");
    REQUIRE(app.loadProjectFile(copyManifest.string()));

    auto missingResourceProject = project;
    missingResourceProject.projectId = "missing.resource";
    missingResourceProject.name = "Missing resource";
    missingResourceProject.resources.front().path = "images/missing.png";
    const auto missingResourceManifest = copyRoot / "missing-resource.lumen-project";
    std::vector<lumen::dsl::DesignError> missingResourceDiagnostics;
    REQUIRE(projectStore.save(missingResourceManifest.string(),
                              missingResourceProject,
                              missingResourceDiagnostics));
    REQUIRE(app.loadProjectFile(missingResourceManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "project.resource_missing");
    REQUIRE(app.loadProjectFile(copyManifest.string()));

    auto directoryResourceProject = project;
    directoryResourceProject.projectId = "directory.resource";
    directoryResourceProject.name = "Directory resource";
    directoryResourceProject.resources.front().path = "images";
    const auto directoryResourceManifest =
        copyRoot / "directory-resource.lumen-project";
    std::vector<lumen::dsl::DesignError> directoryResourceDiagnostics;
    REQUIRE(projectStore.save(directoryResourceManifest.string(),
                              directoryResourceProject,
                              directoryResourceDiagnostics));
    REQUIRE(app.loadProjectFile(directoryResourceManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().back().code == "project.resource_type");
    CHECK_FALSE(app.saveProjectFile(directoryResourceManifest.string()));
    REQUIRE(app.loadProjectFile(copyManifest.string()));

    const auto badManifest = copyRoot / "missing-pages.lumen-project";
    auto missingProject = project;
    missingProject.projectId = "missing.pages";
    missingProject.name = "Missing pages";
    missingProject.pages.front().path = "missing/home.design";
    missingProject.pages.back().path = "missing/settings.design";
    std::vector<lumen::dsl::DesignError> missingDiagnostics;
    REQUIRE(projectStore.save(badManifest.string(), missingProject,
                              missingDiagnostics));
    CHECK_FALSE(app.loadProjectFile(badManifest.string()));
    REQUIRE_FALSE(app.projectDiagnostics().empty());
    CHECK(app.projectDiagnostics().front().code == "store.read");
    CHECK(app.projectDiagnostics().front().file ==
          (copyRoot / "missing" / "home.design").string());
    REQUIRE(app.project().has_value());
    CHECK(app.project()->projectId == project.projectId);
    CHECK(app.activeProjectDocumentId() == homeId);
    CHECK(app.workbench().document()->documentId == homeId);
    CHECK(app.saveProjectFile(copyManifest.string()));

    auto emptyProject = project;
    emptyProject.projectId = "empty.pages";
    emptyProject.name = "Empty pages";
    emptyProject.pages.clear();
    emptyProject.resources.clear();
    const auto emptyManifest = copyRoot / "empty-pages.lumen-project";
    std::vector<lumen::dsl::DesignError> emptyDiagnostics;
    REQUIRE(projectStore.save(emptyManifest.string(), emptyProject,
                              emptyDiagnostics));
    CHECK_FALSE(app.loadProjectFile(emptyManifest.string()));
    REQUIRE(app.projectDiagnostics().size() == 1);
    CHECK(app.projectDiagnostics().front().code == "project.pages_missing");
    REQUIRE(app.project().has_value());
    CHECK(app.project()->projectId == project.projectId);
    CHECK(app.saveProjectFile(copyManifest.string()));
}

TEST_CASE("designer project paths reject source and destination symlink escapes",
          "[designer][dp9][d3][resource][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-project-symlink-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    const auto sourceRoot = root / "source";
    const auto targetRoot = root / "target";
    const auto outsideRoot = root / "outside";
    std::filesystem::create_directories(sourceRoot / "images");
    std::filesystem::create_directories(targetRoot);
    std::filesystem::create_directories(outsideRoot);
    writeRawRgba(sourceRoot / "images" / "logo.lumenrgba");
    {
        std::ofstream outside(outsideRoot / "logo.lumenrgba");
        REQUIRE(outside.good());
        outside << "outside-data";
    }

    DesignerApp app;
    app.attach();
    REQUIRE(app.loadSource("page home { Text(\"Home\") }", "home.lumen"));
    REQUIRE(app.saveDesignFile((sourceRoot / "home.design").string()));
    lumen::dsl::DesignProject project;
    project.projectId = "symlink.project";
    project.name = "Symlink project";
    project.root = ".";
    project.pages = {
        {app.workbench().document()->documentId, "home.design", "Home"},
    };
    project.resources = {
        {"project://images/logo.lumenrgba", "images/logo.lumenrgba", "image"},
    };
    const auto manifest = sourceRoot / "demo.lumen-project";
    lumen::dsl::ProjectStore store;
    std::vector<lumen::dsl::DesignError> diagnostics;
    REQUIRE(store.save(manifest.string(), project, diagnostics));

    SECTION("same path saves reject an escaped resource source") {
        std::filesystem::remove(sourceRoot / "images" / "logo.lumenrgba");
        std::error_code error;
        std::filesystem::create_symlink(outsideRoot / "logo.lumenrgba",
                                        sourceRoot / "images" / "logo.lumenrgba",
                                        error);
        if (error) {
            SUCCEED("symlinks are unavailable on this platform");
            return;
        }
        REQUIRE(app.loadProjectFile(manifest.string()));
        REQUIRE(app.projectDiagnostics().size() == 1);
        CHECK(app.projectDiagnostics().front().code == "project.resource_path");
        CHECK_FALSE(app.saveProjectFile(manifest.string()));
        CHECK(app.projectDiagnostics().back().code == "project.resource_path");
    }

    SECTION("save as rejects an escaped resource destination directory") {
        REQUIRE(app.loadProjectFile(manifest.string()));
        REQUIRE(app.projectDiagnostics().empty());
        std::error_code error;
        std::filesystem::create_directory_symlink(
            outsideRoot, targetRoot / "images", error);
        if (error) {
            SUCCEED("symlinks are unavailable on this platform");
            return;
        }
        const auto targetManifest = targetRoot / "copy.lumen-project";
        CHECK_FALSE(app.saveProjectFile(targetManifest.string()));
        REQUIRE_FALSE(app.projectDiagnostics().empty());
        CHECK(app.projectDiagnostics().back().code == "project.resource_path");
        CHECK_FALSE(std::filesystem::exists(targetManifest));
        CHECK_FALSE(std::filesystem::exists(targetRoot / "home.design"));
    }

    SECTION("save as rejects an escaped page destination") {
        REQUIRE(app.loadProjectFile(manifest.string()));
        std::error_code error;
        std::filesystem::create_symlink(outsideRoot / "home.design",
                                        targetRoot / "home.design", error);
        if (error) {
            SUCCEED("symlinks are unavailable on this platform");
            return;
        }
        CHECK_FALSE(app.saveProjectFile((targetRoot / "copy.lumen-project").string()));
        REQUIRE_FALSE(app.projectDiagnostics().empty());
        CHECK(app.projectDiagnostics().back().code == "project.page");
        CHECK_FALSE(std::filesystem::exists(outsideRoot / "home.design"));
    }

    std::ifstream outside(outsideRoot / "logo.lumenrgba");
    REQUIRE(outside.good());
    CHECK(std::string{std::istreambuf_iterator<char>{outside},
                      std::istreambuf_iterator<char>()} == "outside-data");
}

TEST_CASE("designer project diagnostics switch to the affected document",
          "[designer][dp9][d3][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-project-diagnostics-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};

    const auto homePath = root / "home.design";
    const auto settingsPath = root / "settings.design";
    DesignerApp seed;
    seed.attach();
    REQUIRE(seed.loadSource("page home { Text(\"Home\") }", "home.lumen"));
    REQUIRE(seed.saveDesignFile(homePath.string()));
    const auto homeId = seed.workbench().document()->documentId;
    REQUIRE(seed.loadSource("page settings { Text(\"Settings\") }",
                            "settings.lumen"));
    REQUIRE(seed.saveDesignFile(settingsPath.string()));
    REQUIRE(seed.saveDesignFile(settingsPath.string()));
    const auto settingsId = seed.workbench().document()->documentId;

    std::ofstream(settingsPath, std::ios::trunc) << "{\"truncated\":";
    lumen::dsl::DesignProject project;
    project.projectId = "diagnostics.project";
    project.name = "Diagnostics project";
    project.root = ".";
    project.pages = {{homeId, "home.design", "Home"},
                     {settingsId, "settings.design", "Settings"}};
    lumen::dsl::ProjectStore projectStore;
    std::vector<lumen::dsl::DesignError> saveDiagnostics;
    const auto manifest = root / "diagnostics.lumen-project";
    REQUIRE(projectStore.save(manifest.string(), project, saveDiagnostics));

    DesignerApp app;
    app.attach();
    REQUIRE(app.loadProjectFile(manifest.string()));
    CHECK(app.activeProjectDocumentId() == homeId);
    REQUIRE(app.projectDiagnostics().size() == 1);
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // B 底栏默认收起，诊断定位前先展开。
    app.shell().handlers().at("designer:toggle-bottom")();
    (void)app.shell().renderFrame();
    const auto diagnostic = findNodeByKey(app.shell().root(), "designer-diagnostic:0");
    REQUIRE(diagnostic != nullptr);
    REQUIRE(app.shell().handlers().contains("designer:diagnostic:0"));
    app.shell().handlers().at("designer:diagnostic:0")();
    CHECK(app.activeProjectDocumentId() == settingsId);
    CHECK(app.workbench().document()->documentId == settingsId);
}

TEST_CASE("designer app routes file dialog commands through one path",
          "[designer][d3][app]") {
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();

    const auto path = std::filesystem::temp_directory_path() /
                      ("lumen-designer-dialog-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       ".design");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + ".bak", error);
        }
    } cleanup{path};

    bool requestedForSave = false;
    std::string requestedDefaultName;
    app.setFileDialogRequester(
        [&](bool forSave, const std::string& defaultName) {
            requestedForSave = forSave;
            requestedDefaultName = defaultName;
            return std::string{};
        });

    app.shell().keyDown(Key::None, lumen::core::kModifierCtrl, 'o');
    CHECK_FALSE(requestedForSave);
    app.handleFileDialogResult({});
    app.shell().keyDown(Key::None,
                        lumen::core::kModifierCtrl | lumen::core::kModifierShift,
                        's');
    CHECK(requestedForSave);
    app.handleFileDialogResult({});

    app.shell().handlers().at("designer:save-as")();
    CHECK(requestedForSave);
    CHECK(requestedDefaultName == "untitled.design");
    app.handleFileDialogResult({path.string()});
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(app.workbench().dirty());

    REQUIRE(app.loadSource("page other { Text(\"Other\") }",
                           "other.lumen"));
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "就绪  /  other.lumen");
    app.shell().handlers().at("designer:open")();
    CHECK_FALSE(requestedForSave);
    app.handleFileDialogResult({path.string()});
    CHECK(app.workbench().diagnostics().empty());
    CHECK(app.workbench().document().has_value());

    app.shell().handlers().at("designer:open")();
    app.handleFileDialogResult({}, "cancelled");
    CHECK(findNodeByKey(app.shell().root(), "designer-status") != nullptr);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "File dialog failed: cancelled");
}

TEST_CASE("designer file conflict actions preserve edits until explicitly resolved",
          "[designer][d3][app][conflict][a11y]") {
    // File conflict design §1-§3, prerequisites §4.14 rule 5.
    const auto root = std::filesystem::temp_directory_path() /
        ("lumen-designer-conflict-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    const auto path = root / "source.design";
    lumen::dsl::DocumentStore store;
    DesignerApp app;
    app.attach();
    lumen::accessibility::RecordingAccessibilityBridge bridge;
    app.shell().setAccessibilityBridge(&bridge);
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource("page source { Text(\"Original\") }"));
    REQUIRE(app.saveDesignFile(path.string()));
    REQUIRE(app.workbench().setProperty(1, "text",
        lumen::dsl::DesignValue{std::string{"Local"}}));
    const auto local = *app.workbench().document();
    const auto selection = app.workbench().selection();
    const auto revision = app.workbench().documentRevision();
    auto external = local;
    external.root.properties["text"] = lumen::dsl::DesignValue{std::string{"External"}};
    std::vector<lumen::dsl::DesignError> errors;
    REQUIRE(store.save(path.string(), external, errors));
    CHECK_FALSE(app.saveDesignFile(path.string()));
    CHECK(*app.workbench().document() == local);
    CHECK(app.workbench().selection() == selection);
    CHECK(app.workbench().documentRevision() == revision);
    CHECK(app.workbench().dirty());
    CHECK(store.load(path.string()).document == external);
    (void)app.shell().renderFrame();
    const auto snapshot = app.shell().buildSemanticsSnapshot();
    for (const auto* key : {"designer-conflict-reload", "designer-conflict-save-as",
                            "designer-conflict-overwrite"}) {
        const auto* button = findNodeByKey(app.shell().root(), key);
        REQUIRE(button != nullptr);
        const auto* semantic = snapshot.find(button->identity);
        REQUIRE(semantic != nullptr);
        CHECK(semantic->role == SemanticsRole::Button);
        CHECK((semantic->actions & kActionActivate) != 0);
        CHECK_FALSE(semantic->label.empty());
    }
    const auto activate = [&](const char* key) {
        (void)app.shell().renderFrame();
        const auto* button = findNodeByKey(app.shell().root(), key);
        REQUIRE(button != nullptr);
        const std::string identity = button->identity;
        CHECK(app.shell().performAccessibilityAction(identity, kActionActivate) ==
              lumen::accessibility::SemanticsActionStatus::Handled);
    };

    SECTION("explicit overwrite retains backup and undo history") {
        activate("designer-conflict-overwrite");
        CHECK(store.load(path.string()).document == local);
        CHECK(store.load(path.string() + ".bak").document == external);
        CHECK_FALSE(app.workbench().dirty());
        CHECK(app.workbench().canUndo());
        (void)app.shell().renderFrame();
        CHECK(findNodeByKey(app.shell().root(), "designer-conflict-notice") == nullptr);
    }
    SECTION("a later external edit needs a fresh overwrite decision") {
        external.pageName = "external-again";
        REQUIRE(store.save(path.string(), external, errors));
        activate("designer-conflict-overwrite");
        CHECK(store.load(path.string()).document == external);
        CHECK(*app.workbench().document() == local);
        CHECK(app.workbench().dirty());
        (void)app.shell().renderFrame();
        CHECK(findNodeByKey(app.shell().root(), "designer-conflict-notice") != nullptr);
        activate("designer-conflict-overwrite");
        CHECK(store.load(path.string()).document == local);
        CHECK_FALSE(app.workbench().dirty());
    }
    SECTION("reload explicitly discards edits and history") {
        activate("designer-conflict-reload");
        CHECK(*app.workbench().document() == external);
        CHECK_FALSE(app.workbench().dirty());
        CHECK_FALSE(app.workbench().canUndo());
    }
    SECTION("failed reload preserves the document and recovery actions") {
        const auto frame = app.workbench().frame().widget();
        std::ofstream(path, std::ios::trunc) << "truncated";
        std::filesystem::remove(path.string() + ".bak");
        activate("designer-conflict-reload");
        CHECK(*app.workbench().document() == local);
        CHECK(app.workbench().dirty());
        CHECK(app.workbench().frame().widget() == frame);
        (void)app.shell().renderFrame();
        CHECK(findNodeByKey(app.shell().root(), "designer-conflict-notice") != nullptr);
    }
    SECTION("save as cancellation keeps conflict and successful copy resolves it") {
        bool requested = false;
        app.setFileDialogRequester([&](bool forSave, const std::string&) {
            requested = forSave;
            return std::string{};
        });
        activate("designer-conflict-save-as");
        CHECK(requested);
        app.handleFileDialogResult({});
        (void)app.shell().renderFrame();
        CHECK(findNodeByKey(app.shell().root(), "designer-conflict-notice") != nullptr);
        activate("designer-conflict-save-as");
        const auto copy = root / "copy.design";
        app.handleFileDialogResult({copy.string()});
        CHECK(store.load(copy.string()).document == local);
        CHECK(store.load(path.string()).document == external);
        CHECK_FALSE(app.workbench().dirty());
        (void)app.shell().renderFrame();
        CHECK(findNodeByKey(app.shell().root(), "designer-conflict-notice") == nullptr);
    }
    SECTION("switching document clears stale overwrite action") {
        REQUIRE(app.loadSource("page other { Text(\"Other\") }"));
        REQUIRE(app.shell().invokeCommand("designer.conflict-overwrite"));
        CHECK(app.workbench().document()->pageName == "other");
        CHECK(store.load(path.string()).document == external);
        (void)app.shell().renderFrame();
        CHECK(findNodeByKey(app.shell().root(), "designer-conflict-notice") == nullptr);
    }
    SECTION("keyboard actions remain visible after font and contrast changes") {
        app.shell().handlers().at("designer:font-scale")();
        app.shell().handlers().at("designer:font-scale")();
        app.shell().handlers().at("designer:contrast")();
        app.shell().setView(Size{900.0F, 900.0F});
        (void)app.shell().renderFrame();
        const auto* actions = findNodeByKey(app.shell().root(), "designer-conflict-actions");
        REQUIRE(actions != nullptr);
        CHECK(actions->type == lumen::core::WidgetType::Grid);
        const auto* panel = findNodeByKey(app.shell().root(), "designer-diagnostics");
        REQUIRE(panel != nullptr);
        const lumen::core::Rect panelRect{
            absoluteOffset(app.shell().root(), panel->key), panel->size};
        for (const auto* key : {"designer-conflict-reload", "designer-conflict-save-as",
                                "designer-conflict-overwrite"}) {
            const auto* action = findNodeByKey(app.shell().root(), key);
            REQUIRE(action != nullptr);
            const lumen::core::Rect buttonRect{
                absoluteOffset(app.shell().root(), key), action->size};
            CHECK(buttonRect.left() >= panelRect.left());
            CHECK(buttonRect.right() <= panelRect.right());
            CHECK(buttonRect.bottom() <= panelRect.bottom());
        }
        const auto* button = findNodeByKey(app.shell().root(), "designer-conflict-reload");
        REQUIRE(button != nullptr);
        app.shell().controller().focusNode(*button);
        app.shell().keyDown(Key::Tab);
        CHECK(app.shell().focus().focusedKey() == "designer-conflict-save-as");
        app.shell().keyDown(Key::Tab);
        CHECK(app.shell().focus().focusedKey() == "designer-conflict-overwrite");
        app.shell().keyDown(Key::Enter);
        CHECK(store.load(path.string()).document == local);
        CHECK_FALSE(app.workbench().dirty());
    }
}

TEST_CASE("designer project overwrite rechecks all observed page revisions",
          "[designer][d3][dp9][app][conflict]") {
    const auto root = std::filesystem::temp_directory_path() /
        ("lumen-project-conflict-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};
    DesignerApp seed;
    seed.attach();
    REQUIRE(seed.loadSource("page first { Text(\"First\") }"));
    REQUIRE(seed.saveDesignFile((root / "first.design").string()));
    const auto firstId = seed.workbench().document()->documentId;
    REQUIRE(seed.loadSource("page second { Text(\"Second\") }"));
    REQUIRE(seed.saveDesignFile((root / "second.design").string()));
    const auto secondId = seed.workbench().document()->documentId;
    lumen::dsl::DesignProject project;
    project.projectId = "conflict-project";
    project.name = "Conflict project";
    project.root = ".";
    project.pages = {{firstId, "first.design", "First"},
                     {secondId, "second.design", "Second"}};
    const auto manifest = root / "demo.lumen-project";
    lumen::dsl::ProjectStore projectStore;
    lumen::dsl::DocumentStore store;
    std::vector<lumen::dsl::DesignError> errors;
    REQUIRE(projectStore.save(manifest.string(), project, errors));
    DesignerApp app;
    app.attach();
    REQUIRE(app.loadProjectFile(manifest.string()));
    REQUIRE(app.workbench().setProperty(1, "text",
        lumen::dsl::DesignValue{std::string{"Local first"}}));
    const auto originalFirst = store.load((root / "first.design").string()).document;
    auto externalSecond = store.load((root / "second.design").string()).document;
    externalSecond.pageName = "external-second";
    REQUIRE(store.save((root / "second.design").string(), externalSecond, errors));
    CHECK_FALSE(app.saveProjectFile(manifest.string()));
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-conflict-notice") != nullptr);

    SECTION("explicit overwrite saves local pages") {
        REQUIRE(app.shell().invokeCommand("designer.conflict-overwrite"));
        CHECK_FALSE(app.workbench().dirty());
        CHECK(store.load((root / "first.design").string()).document.root.properties.at("text") ==
              lumen::dsl::DesignValue{std::string{"Local first"}});
        CHECK(store.load((root / "second.design").string()).document.pageName == "second");
        CHECK(store.load((root / "second.design.bak").string()).document == externalSecond);
    }
    SECTION("a later second page edit does not write the first page") {
        externalSecond.pageName = "external-again";
        REQUIRE(store.save((root / "second.design").string(), externalSecond, errors));
        REQUIRE(app.shell().invokeCommand("designer.conflict-overwrite"));
        CHECK(store.load((root / "first.design").string()).document == originalFirst);
        CHECK(store.load((root / "second.design").string()).document == externalSecond);
        CHECK(app.workbench().dirty());
    }
    SECTION("a later manifest edit does not write either page") {
        project.name = "External manifest";
        REQUIRE(projectStore.save(manifest.string(), project, errors));
        REQUIRE(app.shell().invokeCommand("designer.conflict-overwrite"));
        CHECK(store.load((root / "first.design").string()).document == originalFirst);
        CHECK(store.load((root / "second.design").string()).document == externalSecond);
        CHECK(projectStore.load(manifest.string()).project.name == "External manifest");
        CHECK(app.workbench().dirty());
    }
    SECTION("reload selects the external project pages") {
        REQUIRE(app.shell().invokeCommand("designer.conflict-reload"));
        CHECK_FALSE(app.workbench().dirty());
        CHECK(app.workbench().document()->root.properties.at("text") ==
              lumen::dsl::DesignValue{std::string{"First"}});
        REQUIRE(app.switchProjectDocument(secondId));
        CHECK(app.workbench().document()->pageName == "external-second");
    }
}

TEST_CASE("designer app creates a project from the current document",
          "[designer][dp9][d3][app]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("lumen-designer-new-project-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    REQUIRE_FALSE(error);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{root};

    DesignerApp app;
    app.attach();
    REQUIRE(app.loadSource("page editor { Text(\"Editor\") }",
                           "editor.lumen"));
    const auto documentId = app.workbench().document()->documentId;
    bool requestedForSave = false;
    std::string requestedDefaultName;
    app.setFileDialogRequester(
        [&](bool forSave, const std::string& defaultName) {
            requestedForSave = forSave;
            requestedDefaultName = defaultName;
            return std::string{};
        });

    app.shell().handlers().at("designer:new-project")();
    CHECK(requestedForSave);
    CHECK(requestedDefaultName == "untitled.lumen-project");
    const auto manifest = root / "editor.lumen-project";
    app.handleFileDialogResult({manifest.string()});

    REQUIRE(app.project().has_value());
    REQUIRE(app.project()->pages.size() == 1);
    CHECK(app.project()->pages.front().documentId == documentId);
    CHECK(app.activeProjectDocumentId() == documentId);
    CHECK(app.workbench().document()->documentId == documentId);
    CHECK(std::filesystem::exists(root / "main.design"));
    lumen::dsl::ProjectStore store;
    const auto loaded = store.load(manifest.string());
    REQUIRE(loaded.ok());
    CHECK(loaded.project.pages.front().documentId == documentId);
}

TEST_CASE("designer file watcher reloads valid files and keeps the last frame on errors",
          "[designer][d2][watch]") {
    const auto path = std::filesystem::temp_directory_path() /
                      ("lumen-designer-watch-" +
                       std::to_string(std::chrono::steady_clock::now()
                                          .time_since_epoch()
                                          .count()) +
                       ".lumen");
    const auto removeFile = [&path] {
        std::error_code error;
        std::filesystem::remove(path, error);
    };
    struct Cleanup {
        const std::function<void()> remove;
        ~Cleanup() { remove(); }
    } cleanup{removeFile};

    const auto write = [&path](const std::string& source) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        file << source;
        REQUIRE(file.good());
    };
    const auto stamp = [&path](auto offset) {
        std::error_code error;
        const auto current = std::filesystem::last_write_time(path, error);
        REQUIRE_FALSE(error);
        std::filesystem::last_write_time(path, current + offset, error);
        REQUIRE_FALSE(error);
    };

    write("page watch { Text(\"First\", key: \"title\") }");
    FileWatcher watcher(path.string());
    DesignerApp app;
    app.attach();
    REQUIRE(app.loadFile(path.string()));
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    const auto firstGeneration = app.workbench().frame().generation();
    CHECK_FALSE(watcher.poll());

    write("page watch { Text(\"Second\", key: \"title\") }");
    stamp(std::chrono::seconds{1});
    CHECK(watcher.poll());
    REQUIRE(app.loadFile(path.string()));
    (void)app.shell().renderFrame();
    CHECK(app.workbench().frame().generation() > firstGeneration);

    const auto goodGeneration = app.workbench().frame().generation();
    write("page watch {");
    stamp(std::chrono::seconds{2});
    CHECK(watcher.poll());
    CHECK_FALSE(app.loadFile(path.string()));
    CHECK(app.workbench().frame().generation() == goodGeneration);
    REQUIRE(app.workbench().diagnostics().size() == 1);

    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    REQUIRE_FALSE(removeError);
    CHECK(watcher.poll());
    CHECK_FALSE(app.loadFile(path.string()));
    CHECK(app.workbench().frame().generation() == goodGeneration);
    REQUIRE(app.workbench().diagnostics().size() == 1);

    write("page watch { Text(\"Restored\", key: \"title\") }");
    CHECK(watcher.poll());
    REQUIRE(app.loadFile(path.string()));
    (void)app.shell().renderFrame();
    CHECK(app.workbench().frame().generation() > goodGeneration);
}

TEST_CASE("designer B canvas-first workspace matches the HTML geometry",
          "[designer][b][workspace]") {
    // B 一比一（designer-ide.html?variant=b，workspace-design §3.1）：
    // 标题 44 + 主工具 48 + 工作区（48 rail + 224 left + 6 + center + 6 +
    // 280 right）+ 状态 28；中心文档 40 + 画布工具 48 + 面包屑 32 +
    // 底栏 36（收起）/144（展开）。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // B 默认 Toolbox，先切回结构页以稳定大纲几何（工具箱行高不影响外壳）。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    const auto* titlebar =
        findNodeByKey(app.shell().root(), "designer-titlebar");
    const auto* mainTools =
        findNodeByKey(app.shell().root(), "designer-main-tools");
    const auto* rail = findNodeByKey(app.shell().root(), "designer-rail");
    const auto* left =
        findNodeByKey(app.shell().root(), "designer-outline-panel");
    const auto* centerTabs =
        findNodeByKey(app.shell().root(), "designer-document-tabs");
    const auto* canvasControls =
        findNodeByKey(app.shell().root(), "designer-canvas-controls");
    const auto* crumb = findNodeByKey(app.shell().root(), "designer-crumb-bar");
    const auto* bottom =
        findNodeByKey(app.shell().root(), "designer-diagnostics");
    const auto* status =
        findNodeByKey(app.shell().root(), "designer-statusbar");
    const auto* right =
        findNodeByKey(app.shell().root(), "designer-properties-panel");
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(titlebar != nullptr);
    REQUIRE(mainTools != nullptr);
    REQUIRE(rail != nullptr);
    REQUIRE(left != nullptr);
    REQUIRE(centerTabs != nullptr);
    REQUIRE(canvasControls != nullptr);
    REQUIRE(crumb != nullptr);
    REQUIRE(bottom != nullptr);
    REQUIRE(status != nullptr);
    REQUIRE(right != nullptr);
    REQUIRE(canvas != nullptr);
    CHECK(titlebar->size.height == Catch::Approx(44.0F));
    CHECK(mainTools->size.height == Catch::Approx(48.0F));
    CHECK(rail->size.width == Catch::Approx(48.0F));
    CHECK(left->size.width == Catch::Approx(224.0F));
    CHECK(right->size.width == Catch::Approx(280.0F));
    CHECK(centerTabs->size.height == Catch::Approx(40.0F));
    CHECK(canvasControls->size.height == Catch::Approx(48.0F));
    CHECK(crumb->size.height == Catch::Approx(32.0F));
    CHECK(bottom->size.height == Catch::Approx(36.0F));
    CHECK(status->size.height == Catch::Approx(28.0F));
    // 轨道 6：canvas 左缘 = rail 48 + left 224 + 6；右栏左缘 = canvas 右 + 6。
    const float canvasLeft =
        absoluteOffset(app.shell().root(), "designer-canvas").x;
    const float railLeft =
        absoluteOffset(app.shell().root(), "designer-rail").x;
    CHECK(canvasLeft - (railLeft + 48.0F + 224.0F) ==
          Catch::Approx(6.0F).margin(1.0F));
    const float rightLeft =
        absoluteOffset(app.shell().root(), "designer-properties-panel").x;
    CHECK(rightLeft - (canvasLeft + canvas->size.width) ==
          Catch::Approx(6.0F).margin(1.5F));
    // 工作台 Compact，预览独立 Comfortable（visual-system §3.6）。
    CHECK(app.shell().theme().metrics.density ==
          lumen::style::ControlDensity::Compact);
    // 底栏展开 144，收起回 36（不污染 dirty）。
    const bool wasDirty = app.workbench().dirty();
    app.shell().handlers().at("designer:toggle-bottom")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-diagnostics")
              ->size.height == Catch::Approx(144.0F));
    app.shell().handlers().at("designer:toggle-bottom")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-diagnostics")
              ->size.height == Catch::Approx(36.0F));
    CHECK(app.workbench().dirty() == wasDirty);
}

TEST_CASE("designer B splitters drag keyboard reset and zen collapse",
          "[designer][b][workspace]") {
    // 外壳分栏（splitter §7/§9）：拖动、Compact 12 步进、Home/End 到边、
    // 双击复位；专注隐藏左右栏与底栏，Esc 退出；左折叠隐藏左栏。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();

    const auto dividerCenter = [&](const char* key) {
        app.shell().rebuildIfDirty();
        const auto* divider = findNodeByKey(app.shell().root(), key);
        REQUIRE(divider != nullptr);
        const auto origin = absoluteOffset(app.shell().root(), key);
        return Offset{origin.x + divider->size.width * 0.5F,
                      origin.y + divider->size.height * 0.5F};
    };
    // 左分栏拖动 +40。
    const Offset leftCenter = dividerCenter("split:div:designer-left-splitter");
    const auto* leftBefore =
        findNodeByKey(app.shell().root(), "designer-outline-panel");
    REQUIRE(leftBefore != nullptr);
    const float leftWidthBefore = leftBefore->size.width;
    app.shell().pointerDown(leftCenter);
    app.shell().pointerMove(Offset{leftCenter.x + 40.0F, leftCenter.y});
    app.shell().pointerUp(Offset{leftCenter.x + 40.0F, leftCenter.y});
    (void)app.shell().renderFrame();
    const auto* leftAfterNode =
        findNodeByKey(app.shell().root(), "designer-outline-panel");
    REQUIRE(leftAfterNode != nullptr);
    const float leftWidthAfter = leftAfterNode->size.width;
    CHECK(leftWidthAfter ==
          Catch::Approx(leftWidthBefore + 40.0F).margin(2.0F));
    // 键盘步进（Compact 12）：Tab 聚焦左分隔条后 Right +12。
    app.shell().focus().clearFocus();
    bool focused = false;
    for (int i = 0; i < 40; ++i) {
        app.shell().keyDown(Key::Tab);
        if (app.shell().focus().focusedKey() ==
            "split:div:designer-left-splitter") {
            focused = true;
            break;
        }
    }
    REQUIRE(focused);
    app.shell().keyDown(Key::Right);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-outline-panel")
              ->size.width ==
          Catch::Approx(leftWidthAfter + 12.0F).margin(2.0F));
    // 双击复位回 224。
    app.shell().tick(1000);
    app.shell().pointerDown(dividerCenter("split:div:designer-left-splitter"));
    app.shell().pointerUp(dividerCenter("split:div:designer-left-splitter"));
    app.shell().tick(1200);
    app.shell().pointerDown(dividerCenter("split:div:designer-left-splitter"));
    app.shell().pointerUp(dividerCenter("split:div:designer-left-splitter"));
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-outline-panel")
              ->size.width == Catch::Approx(224.0F).margin(2.0F));
    // 专注：左右栏与底栏隐藏，画布独占；Esc 退出。
    app.shell().handlers().at("designer:zen")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-outline-panel") ==
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-properties-panel") ==
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas") != nullptr);
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-outline-panel") !=
          nullptr);
    // 左折叠：rail 保留，左栏隐藏。
    app.shell().handlers().at("designer:toggle-left")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-rail") != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-outline-panel") ==
          nullptr);
    app.shell().handlers().at("designer:toggle-left")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-outline-panel") !=
          nullptr);
}

TEST_CASE("designer B rail views and center split keep one selection",
          "[designer][b][workspace]") {
    // Rail 切换控件/结构，中心设计/分屏/快照，面包屑回选容器，共用
    // DocumentId 选择（workspace-design §4.1）。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    // Rail 默认 Toolbox，切换到结构再切回。
    REQUIRE(findNodeByKey(app.shell().root(), "designer-toolbox:Text") !=
            nullptr);
    app.shell().handlers().at("designer:rail-outline")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-outline-view") !=
            nullptr);
    app.shell().handlers().at("designer:rail-toolbox")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-toolbox:Text") !=
            nullptr);
    // 中心分屏：画布 + 只读快照同屏；快照只读定位。
    app.shell().handlers().at("designer:view-split")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas") != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-source-panel") !=
          nullptr);
    app.shell().handlers().at("designer:view-source")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-source-panel") !=
          nullptr);
    app.shell().handlers().at("designer:view-design")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-canvas") != nullptr);
    // 面包屑存在且可回选（至少一个 crumb 按钮）。
    bool hasCrumb = false;
    std::function<void(const lumen::core::RenderNode&)> walk =
        [&](const lumen::core::RenderNode& node) {
            if (node.key.rfind("designer-crumb:", 0) == 0) hasCrumb = true;
            for (const auto& child : node.children) walk(child);
        };
    walk(app.shell().root());
    CHECK(hasCrumb);
}

TEST_CASE("designer B title menus open popover and forward commands",
          "[designer][b][workspace]") {
    // B 标题栏文件/编辑/视图 popover（HTML .popover）：低频入口，激活后
    // 关闭并转发原命令，不污染 dirty/undo（workspace-design §3/§4.4）。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-menu-popover") ==
          nullptr);
    app.shell().handlers().at("designer:menu-file")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-menu-popover") !=
            nullptr);
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-menu-item:file:save") != nullptr);
    // Esc 关闭。
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-menu-popover") ==
          nullptr);
    // 编辑菜单复制节点：选中叶节点后经菜单复制，文档变脏且菜单关闭。
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    REQUIRE(!outline->children.empty());
    const auto target = outline->children.front().id;
    // 选中首个叶节点（根为容器，首子为叶）。
    app.shell().handlers().at("designer:select:" + std::to_string(target))();
    (void)app.shell().renderFrame();
    const std::size_t before = app.workbench().outline()->children.size();
    app.shell().handlers().at("designer:menu-edit")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-menu-item:edit:duplicate") != nullptr);
    app.shell().handlers().at("designer:menu:duplicate")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-menu-popover") ==
          nullptr);
    CHECK(app.workbench().outline()->children.size() == before + 1);
    // 视图菜单专注：打开后激活专注，左右栏隐藏。
    app.shell().handlers().at("designer:menu-view")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(),
                          "designer-menu-item:view:zen") != nullptr);
    app.shell().handlers().at("designer:menu:zen")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-outline-panel") ==
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-menu-popover") ==
          nullptr);
}

TEST_CASE("designer B bool properties use checkboxes", "[designer][b][inspector]") {
    // 属性 §4.2：布尔量用 Checkbox（HTML“默认选中”同口径），值通道保持
    // designer-property-field 兼容键。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    REQUIRE(app.loadSource(
        "page check { Column(key: \"root\") { Checkbox(\"Agree\", key: \"agree\", "
        "checked: true) } }",
        "bool-check.lumen"));
    (void)app.shell().renderFrame();
    const auto outline = app.workbench().outline();
    REQUIRE(outline.has_value());
    // 选中 Checkbox 节点（首子）。
    REQUIRE(!outline->children.empty());
    const auto checkId = outline->children.front().id;
    app.shell().handlers().at("designer:select:" + std::to_string(checkId))();
    (void)app.shell().renderFrame();
    const std::string fieldKey =
        "designer-property-field:" + std::to_string(checkId) + ":checked";
    const auto* field = findNodeByKey(app.shell().root(), fieldKey);
    REQUIRE(field != nullptr);
    CHECK(field->type == lumen::core::WidgetType::Checkbox);
    CHECK(field->checked);
}

TEST_CASE("designer B right pane keeps preference on resize",
          "[designer][b][workspace]") {
    // 右栏偏好 280（232–400）：窗口 resize 时中心吃增量，右栏稳定；
    // 拖动回填偏好，下次 resize 恢复偏好。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    const auto* rightBefore =
        findNodeByKey(app.shell().root(), "designer-properties-panel");
    REQUIRE(rightBefore != nullptr);
    CHECK(rightBefore->size.width == Catch::Approx(280.0F).margin(2.0F));
    app.shell().setView(Size{1440.0F, 800.0F});
    (void)app.shell().renderFrame();
    const auto* rightAfter =
        findNodeByKey(app.shell().root(), "designer-properties-panel");
    REQUIRE(rightAfter != nullptr);
    CHECK(rightAfter->size.width == Catch::Approx(280.0F).margin(3.0F));
    const auto* canvasAfter = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(canvasAfter != nullptr);
    const auto* leftAfterResize =
        findNodeByKey(app.shell().root(), "designer-outline-panel");
    REQUIRE(leftAfterResize != nullptr);
    // 中心 716 → 876（+160），右栏稳定。
    CHECK(canvasAfter->size.width == Catch::Approx(876.0F).margin(4.0F));
}

TEST_CASE("designer B run preview overlay appears on demand",
          "[designer][b][preview]") {
    // 运行预览按需出现：停止态无覆盖层，运行/调试打开模态，停止/Esc/
    // 遮罩点击关闭；不再依赖第二个 OS 窗口。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-overlay") ==
          nullptr);

    app.shell().handlers().at("designer:run")();
    (void)app.shell().renderFrame();
    const auto* overlay =
        findNodeByKey(app.shell().root(), "designer-run-preview-overlay");
    REQUIRE(overlay != nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-title") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-close") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-content") !=
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-card")
              ->semanticsRole == "dialog");

    app.shell().handlers().at("designer:stop")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-overlay") ==
          nullptr);

    // 调试会话同样打开，Esc 关闭并停止。
    app.shell().handlers().at("designer:debug")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-run-preview-overlay") !=
            nullptr);
    app.shell().keyDown(Key::Escape);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-overlay") ==
          nullptr);
    CHECK(findNodeByKey(app.shell().root(), "designer-status")->text ==
          "Preview stopped");

    // 遮罩点击关闭：卡片 880 居中，角落 (30,400) 落在遮罩上。
    app.shell().handlers().at("designer:run")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-run-preview-overlay") !=
            nullptr);
    const Offset outside{30.0F, 400.0F};
    app.shell().pointerDown(outside);
    app.shell().pointerUp(outside);
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-overlay") ==
          nullptr);
}

TEST_CASE("designer B structural keys suspended behind run overlay",
          "[designer][b][preview]") {
    // 模态打开时 Delete/排序/大纲键盘 suspend，防止遮挡后文档被意外改动。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    app.shell().handlers().at("designer:lefttab-outline")();
    (void)app.shell().renderFrame();
    const auto before = app.workbench().documentRevision();
    app.shell().handlers().at("designer:run")();
    (void)app.shell().renderFrame();
    REQUIRE(findNodeByKey(app.shell().root(), "designer-run-preview-overlay") !=
            nullptr);
    app.shell().keyDown(Key::Delete);
    (void)app.shell().renderFrame();
    CHECK(app.workbench().documentRevision() == before);
    app.shell().handlers().at("designer:stop")();
    (void)app.shell().renderFrame();
    CHECK(findNodeByKey(app.shell().root(), "designer-run-preview-overlay") ==
          nullptr);
}

TEST_CASE("designer panel scrollviews answer wheel and scrollbar drags",
          "[designer][scroll]") {
    // 左面板工具箱是应用侧 ScrollView（lumen-scroll-design §3.4/§4）：滚轮
    // 与滚动条轨道翻页经 onWheel sink 驱动应用持有的 ScrollController，拇
    // 指拖动经 onScrollDrag；偏移随 build 写回 scrollOffset。此前 onWheel
    // 只认画布平移，整个左/右面板列表无法滚动。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    showLeftToolbox(app);

    const auto toolbox = [&app] {
        const auto* node =
            findNodeByKey(app.shell().root(), "designer-toolbox-scroll");
        REQUIRE(node != nullptr);
        return node;
    };
    // 7 组 37 行在 800 高窗口下超出左栏可视高度，视口处于可滚状态。
    REQUIRE(toolbox()->scrollExtent > 0.0F);
    const auto origin =
        absoluteOffset(app.shell().root(), "designer-toolbox-scroll");
    const Offset contentPoint{
        origin.x + toolbox()->size.width * 0.5F,
        origin.y + toolbox()->size.height * 0.5F};

    // 滚轮向下两拍再回一拍：偏移精确写回（120px/拍，无宿主换算）。
    CHECK(app.shell().wheel(contentPoint, {0.0F, 120.0F}));
    (void)app.shell().renderFrame();
    CHECK(toolbox()->scrollOffset == 120.0F);
    CHECK(app.shell().wheel(contentPoint, {0.0F, 120.0F}));
    (void)app.shell().renderFrame();
    CHECK(toolbox()->scrollOffset == 240.0F);
    CHECK(app.shell().wheel(contentPoint, {0.0F, -120.0F}));
    (void)app.shell().renderFrame();
    CHECK(toolbox()->scrollOffset == 120.0F);

    // 越过大额滚动夹取到 maxScrollOffset（滚动条轨道翻页同路径）。
    CHECK(app.shell().wheel(contentPoint, {0.0F, 100000.0F}));
    (void)app.shell().renderFrame();
    CHECK(toolbox()->scrollOffset ==
          Catch::Approx(toolbox()->scrollExtent).margin(0.5F));

    // 滚动条拇指拖动：从拇指中心按住推到轨道底端，偏移应到底。
    const auto grabGeometry = scrollbarGeometry(*toolbox());
    REQUIRE(grabGeometry.has_value());
    const Offset grabPoint{
        origin.x + grabGeometry->thumbHit.origin.x +
            grabGeometry->thumbHit.size.width * 0.5F,
        origin.y + grabGeometry->thumbHit.origin.y +
            grabGeometry->thumbHit.size.height * 0.5F};
    app.shell().pointerMove(grabPoint);
    app.shell().pointerDown(grabPoint);
    app.shell().pointerMove(
        grabPoint + Offset{0.0F, grabGeometry->travel});
    app.shell().pointerUp(grabPoint + Offset{0.0F, grabGeometry->travel});
    (void)app.shell().renderFrame();
    CHECK(toolbox()->scrollOffset ==
          Catch::Approx(toolbox()->scrollExtent).margin(0.5F));

    // 滚回顶部后点击轨道下段 = 向下翻页（0.9 视口高），不触发被覆盖的
    // 工具箱行（不插入控件）。
    (void)app.shell().wheel(contentPoint, {0.0F, -100000.0F});
    (void)app.shell().renderFrame();
    REQUIRE(toolbox()->scrollOffset == 0.0F);
    const auto trackGeometry = scrollbarGeometry(*toolbox());
    REQUIRE(trackGeometry.has_value());
    const Offset trackPoint{
        origin.x + trackGeometry->track.origin.x +
            trackGeometry->track.size.width * 0.5F,
        origin.y + trackGeometry->track.origin.y +
            trackGeometry->track.size.height * 0.75F};
    const auto revision = app.workbench().documentRevision();
    app.shell().pointerDown(trackPoint);
    app.shell().pointerUp(trackPoint);
    (void)app.shell().renderFrame();
    CHECK(toolbox()->scrollOffset ==
          Catch::Approx(toolbox()->size.height * 0.9F).margin(0.5F));
    CHECK(app.workbench().documentRevision() == revision);
}





TEST_CASE("designer canvas rulers and dashes paint over the board",
          "[designer][d3][app][paint]") {
    // §8.8/§8.9 绘制契约（review 像素实证固化）：标尺条绘于预览之上
    //（strip = surfaceSunken，不再被画板选中态冲刷覆盖）；页面虚线边界
    // 按 rose 4on/3off 落点；舞台内缩 20px 后画板顶边不再贴标尺条。
    DesignerApp app;
    app.attach();
    app.shell().setView(Size{1280.0F, 800.0F});
    (void)app.shell().renderFrame();
    (void)app.shell().renderFrame();
    const auto* canvas = findNodeByKey(app.shell().root(), "designer-canvas");
    REQUIRE(canvas != nullptr);
    const auto origin = absoluteOffset(app.shell().root(), "designer-canvas");
    const auto& p = app.shell().pixels();
    REQUIRE(p.width > 0);
    const auto at = [&](int x, int y) {
        const std::size_t i = (static_cast<std::size_t>(y) * p.width + x) * 4;
        return std::make_tuple(p.rgba[i], p.rgba[i + 1], p.rgba[i + 2]);
    };
    const int cx = static_cast<int>(origin.x + canvas->padding.left);
    const int cy = static_cast<int>(origin.y + canvas->padding.top);
    // 标尺条：surfaceSunken（暗/亮主题都非纯背景色）。
    auto [rr, rg, rb] = at(cx + 10, cy + 5);
    const auto& sunken = app.shell().theme().colors.surfaceSunken;
    CHECK(std::abs(rr - sunken.r) <= 2);
    CHECK(std::abs(rg - sunken.g) <= 2);
    CHECK(std::abs(rb - sunken.b) <= 2);
    const auto* root = findNodeByKey(app.shell().root(), "root");
    REQUIRE(root != nullptr);
    const auto rootOrigin = absoluteOffset(app.shell().root(), "root");
    // 舞台内缩：画板顶边在标尺条（20px）之下。
    CHECK(rootOrigin.y >= cy + 20.0F);
    // 页面边界上边虚线：rose 段 + 间隙交替。
    const int dashY = static_cast<int>(rootOrigin.y) - 8;
    int rose = 0;
    int background = 0;
    for (int x = 0; x < 70; ++x) {
        auto [r, g, b] = at(static_cast<int>(rootOrigin.x) - 8 + x, dashY);
        if (r > 200 && g >= 60 && g <= 130 && b >= 90 && b <= 160) {
            ++rose;
        } else if (r < 45 && g < 45 && b < 50) {
            ++background;
        }
    }
    CHECK(rose >= 30);
    CHECK(background >= 20);
    CHECK(rose + background >= 62);
}
