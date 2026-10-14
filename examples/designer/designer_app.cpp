#include "designer_app.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

#include "lumen/dsl/design_schema.h"
#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/project_store.h"
#include "lumen/core/icon_id.h"
#include "lumen/style/theme.h"
#include "lumen/widgets/splitter.h"

namespace lumen::designer_app {
namespace {

// 默认样例（sample.lumen）：352×264 Stack 画板 + 全定位子节点——开箱
// 即可拖动换位（Stack 子节点的 left/top 归设计者，画布拖动会话认领）。
// 尺寸口径（§8.6/§8.7）：1024×768 默认窗口的画布内容区约 364×275，
// 画板在 100% 下完整呈现（声明尺寸不被约束钳制压扁）；"适合"命令仍
// 可对更大画板缩放全览。
constexpr char kSampleSource[] =
    "page preview { Stack(key: \"root\", width: 352, height: 264) {"
    " Container(key: \"toolbar\", width: 320, height: 20, left: 16, top: 12,"
    " color: #27272A)"
    " Text(\"Lumen Designer\", key: \"title\", left: 16, top: 36)"
    " Container(key: \"card-a\", width: 100, height: 68, left: 16, top: 60,"
    " color: #27272A) { Text(\"A\", key: \"card-a-label\") }"
    " Container(key: \"card-b\", width: 100, height: 68, left: 124, top: 60,"
    " color: #27272A) { Text(\"B\", key: \"card-b-label\") }"
    " Container(key: \"card-c\", width: 100, height: 68, left: 232, top: 60,"
    " color: #27272A) { Text(\"C\", key: \"card-c-label\") }"
    " Button(\"Save\", key: \"save\", left: 240, top: 208)"
    " Text(\"Ready\", key: \"status\", left: 16, top: 222)"
    " } }";

std::vector<const dsl::NodeSchema*> designerToolboxSchemas() {
    std::vector<const dsl::NodeSchema*> result;
    for (const auto& schema : dsl::nodeSchemaRegistry()) {
        // These L3 components have deterministic designer-owned adapters.
        // Component nodes without an adapter stay out so inserting one cannot
        // silently produce a placeholder.
        if (!schema.isComponent || schema.type == "ComboBox" ||
            schema.type == "ColorPicker" || schema.type == "Spin" ||
            schema.type == "DataGrid" || schema.type == "ToolBar" ||
            schema.type == "StatusBar" || schema.type == "Menu" ||
            schema.type == "DialogHost" || schema.type == "Navigator" ||
            schema.type == "Form") {
            result.push_back(&schema);
        }
    }
    return result;
}

// 左面板版本 C（design/designer-leftpanel-redesign.html）：工具箱分 7 组
// 展示，口径对齐 examples/gallery 的 nav taxonomy。分组只是展示映射——
// schema 注册表顺序与 designer:toolbox:type 命令不变；测试断言分类法
// 与注册表互相覆盖（见 designer_app_tests "designer toolbox groups
// cover the schema registry"）。
struct ToolboxGroupSpec {
    const char* label;
    core::IconId icon;
    std::vector<const char*> types;
};

const std::vector<ToolboxGroupSpec>& designerToolboxGroups() {
    static const std::vector<ToolboxGroupSpec> groups = {
        {"Layout", core::IconId::NavLayout,
         {"Container", "Row", "Column", "Stack", "Grid", "ScrollView",
          "Splitter"}},
        {"Content", core::IconId::Content, {"Text", "Image", "Icon"}},
        {"Input", core::IconId::NavInputs,
         {"Button", "TextField", "Checkbox", "Switch", "Radio", "Slider",
          "Spin", "ComboBox", "ColorPicker", "Dropdown"}},
        {"Collection", core::IconId::NavLists,
         {"ListView", "VirtualList", "List", "Tree", "TreeList", "DataGrid",
          "Tabs"}},
        {"Feedback", core::IconId::NavFeedback, {"ProgressBar", "Tooltip"}},
        {"Component", core::IconId::NavMenus,
         {"Menu", "ToolBar", "StatusBar", "DialogHost", "Navigator", "Form"}},
        {"Scope", core::IconId::Scope, {"FocusScope", "ThemeScope"}},
    };
    return groups;
}

// §8.8/§8.9 舞台内缩：内容原点让出内嵌标尺（20px）+ 页面边界外扩
// （8px）与 caption（32px 上方）的裕量，复位/初始视图画板完整落在标
// 尺之外（designer-canvas.html 的 stage 与标尺分格等价）。
constexpr float kCanvasStageInset = 32.0F;

// 应用侧滚动视口（左面板工具箱/工程、源码、属性、运行预览）：偏移由
// DesignerApp 持有的 ScrollController 驱动（lumen-scroll-design §3.4），
// 框架解析到这些视口的 wheel/scroll-drag 经 configFor 的 sink 进
// handlePanelWheel/handlePanelScrollDrag。
constexpr std::string_view kPanelScrollKeys[] = {
    "designer-toolbox-scroll",
    "designer-project-scroll",
    "designer-source-scroll",
    "designer-properties-scroll",
    "designer-run-preview-scroll",
};

void prefixWidgetKeys(core::Widget& widget, std::string_view prefix) {
    if (!widget.key.empty()) widget.key = std::string{prefix} + widget.key;
    for (auto& child : widget.children) prefixWidgetKeys(child, prefix);
}

void scaleEdgeInsets(core::EdgeInsets& insets, float scale) {
    insets.left *= scale;
    insets.top *= scale;
    insets.right *= scale;
    insets.bottom *= scale;
}

void scaleCornerRadius(core::CornerRadius& radius, float scale) {
    radius.topLeft *= scale;
    radius.topRight *= scale;
    radius.bottomLeft *= scale;
    radius.bottomRight *= scale;
}

void scaleTextStyle(core::TextStyle& style, float scale) {
    style.fontSize *= scale;
    style.letterSpacing *= scale;
}

void scaleCanvasWidget(core::Widget& widget, float scale) {
    if (scale == 1.0F) return;
    if (widget.width.has_value()) *widget.width *= scale;
    if (widget.height.has_value()) *widget.height *= scale;
    scaleEdgeInsets(widget.padding, scale);
    scaleEdgeInsets(widget.margin, scale);
    widget.spacing *= scale;
    widget.textStyle.fontSize *= scale;
    widget.textStyle.letterSpacing *= scale;
    if (widget.stackPosition.has_value()) {
        widget.stackPosition->x *= scale;
        widget.stackPosition->y *= scale;
    }
    widget.virtualCacheExtent *= scale;
    widget.gridMinColumnWidth *= scale;
    widget.gridColumnGap *= scale;
    widget.gridRowGap *= scale;
    widget.elevation *= scale;
    widget.styleOverrides.iconSize *= scale;
    if (widget.styleOverrides.padding.has_value()) {
        scaleEdgeInsets(*widget.styleOverrides.padding, scale);
    }
    if (widget.styleOverrides.radius.has_value()) {
        scaleCornerRadius(*widget.styleOverrides.radius, scale);
    }
    if (widget.styleOverrides.borderWidth.has_value()) {
        *widget.styleOverrides.borderWidth *= scale;
    }
    if (widget.styleOverrides.text.has_value()) {
        scaleTextStyle(*widget.styleOverrides.text, scale);
    }
    for (auto& child : widget.children) scaleCanvasWidget(child, scale);
}

bool isL0ToolboxType(std::string_view type) {
    for (const auto* candidate : {"Container", "Row", "Column", "Stack",
                                  "Text", "Button", "TextField", "ScrollView",
                                  "ListView", "Checkbox", "Switch",
                                  "FocusScope"}) {
        if (type == candidate) return true;
    }
    return false;
}

std::string nodeLabel(const dsl::DesignPreviewOutlineNode& node) {
    if (node.key.empty()) return node.type;
    return node.type + "  [" + node.key + "]";
}

std::string previewKeyForNode(dsl::DesignNodeId id) {
    return "designer:node:" + std::to_string(id);
}

std::string propertyBindingKey(dsl::DesignNodeId id,
                               std::string_view property) {
    return "designer:property:" + std::to_string(id) + ":" +
           std::string{property};
}

std::string referenceBindingKey(dsl::DesignNodeId id,
                                std::string_view property) {
    return "designer:reference:" + std::to_string(id) + ":" +
           std::string{property};
}

bool sameProjectPath(const std::string& left, const std::string& right) {
    if (left == right) return true;
    std::error_code leftError;
    std::error_code rightError;
    const auto leftPath =
        std::filesystem::absolute(std::filesystem::path{left}, leftError)
            .lexically_normal();
    const auto rightPath =
        std::filesystem::absolute(std::filesystem::path{right}, rightError)
            .lexically_normal();
    return !leftError && !rightError && leftPath == rightPath;
}

bool isDesignFile(const std::string& filename) {
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension == ".design";
}

bool isProjectFile(const std::string& filename) {
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) {
                       return static_cast<char>(std::tolower(value));
                   });
    return extension == ".lumen-project" || extension == ".lumenproject";
}

std::optional<std::string> readSourceFile(const std::string& filename) {
    std::ifstream input(filename, std::ios::binary);
    if (!input.good()) return std::nullopt;
    std::ostringstream content;
    content << input.rdbuf();
    if (!input.good() && !input.eof()) return std::nullopt;
    return content.str();
}

std::string propertyStateValue(const dsl::DesignValue& value) {
    return std::visit(
        [](const auto& item) -> std::string {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                return item ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream stream;
                stream << item;
                return stream.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return item;
            } else if constexpr (std::is_same_v<T, dsl::DesignEnum>) {
                return item.value;
            } else {
                return {};
            }
        },
        value.value);
}

std::optional<dsl::DesignValue> parsePropertyState(
    const dsl::DesignValue& prototype, std::string_view raw) {
    return std::visit(
        [raw](const auto& item) -> std::optional<dsl::DesignValue> {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                if (raw == "true" || raw == "1") {
                    return dsl::DesignValue{
                        dsl::DesignValue::Variant{true}};
                }
                if (raw == "false" || raw == "0") {
                    return dsl::DesignValue{
                        dsl::DesignValue::Variant{false}};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, double>) {
                std::string text{raw};
                char* end = nullptr;
                const double parsed = std::strtod(text.c_str(), &end);
                if (end == text.c_str() || *end != '\0' ||
                    !std::isfinite(parsed)) {
                    return std::nullopt;
                }
                return dsl::DesignValue{
                    dsl::DesignValue::Variant{parsed}};
            } else if constexpr (std::is_same_v<T, std::string>) {
                return dsl::DesignValue{
                    dsl::DesignValue::Variant{std::string{raw}}};
            } else if constexpr (std::is_same_v<T, dsl::DesignEnum>) {
                return dsl::DesignValue{dsl::DesignValue::Variant{
                    dsl::DesignEnum{item.domain, std::string{raw}}}};
            } else {
                return std::nullopt;
            }
        },
        prototype.value);
}

const dsl::DesignNode* findDesignNode(const dsl::DesignNode& node,
                                      dsl::DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findDesignNode(child, id); found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findDesignNode(child, id);
                found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

void collectDesignKeys(const dsl::DesignNode& node,
                       std::set<std::string>& keys) {
    if (const auto found = node.properties.find("key");
        found != node.properties.end()) {
        if (const auto* key = std::get_if<std::string>(&found->second.value);
            key != nullptr && !key->empty()) {
            keys.insert(*key);
        }
    }
    for (const auto& child : node.children) collectDesignKeys(child, keys);
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) collectDesignKeys(child, keys);
    }
}

std::string declaredPreviewKeyForDesignNode(const dsl::DesignNode& node) {
    const auto found = node.properties.find("key");
    if (found != node.properties.end()) {
        if (const auto* key = std::get_if<std::string>(&found->second.value);
            key != nullptr && !key->empty()) {
            return *key;
        }
    }
    return {};
}

void collectPreviewKeyCounts(const dsl::DesignNode& node,
                             std::map<std::string, std::size_t>& counts) {
    const auto found = node.properties.find("key");
    if (found != node.properties.end()) {
        if (const auto* key = std::get_if<std::string>(&found->second.value);
            key != nullptr && !key->empty()) {
            ++counts[*key];
        }
    }
    for (const auto& child : node.children) {
        collectPreviewKeyCounts(child, counts);
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            collectPreviewKeyCounts(child, counts);
        }
    }
}

void assignPreviewKeys(const dsl::DesignNode& node,
                       const std::map<std::string, std::size_t>& counts,
                       const std::set<std::string>& declaredKeys,
                       std::set<std::string>& usedKeys,
                       std::map<dsl::DesignNodeId, std::string>& keys) {
    const auto declared = declaredPreviewKeyForDesignNode(node);
    const auto count = counts.find(declared);
    const bool needsPrivate = declared.empty() ||
                              (count != counts.end() && count->second > 1) ||
                              usedKeys.contains(declared);
    if (!needsPrivate) {
        keys[node.id] = declared;
        usedKeys.insert(declared);
    } else {
        const std::string base = previewKeyForNode(node.id);
        std::string candidate = base;
        std::size_t suffix = 2;
        while (declaredKeys.contains(candidate) ||
               usedKeys.contains(candidate)) {
            candidate = base + "-" + std::to_string(suffix++);
        }
        keys[node.id] = candidate;
        usedKeys.insert(candidate);
    }
    for (const auto& child : node.children) {
        assignPreviewKeys(child, counts, declaredKeys, usedKeys, keys);
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            assignPreviewKeys(child, counts, declaredKeys, usedKeys, keys);
        }
    }
}

void makePastedKeysUnique(dsl::DesignNode& node,
                          std::set<std::string>& usedKeys) {
    const auto found = node.properties.find("key");
    if (found != node.properties.end()) {
        if (auto* key = std::get_if<std::string>(&found->second.value);
            key != nullptr && !key->empty() &&
            usedKeys.find(*key) != usedKeys.end()) {
            const std::string base = *key + "-copy";
            std::string candidate = base;
            std::size_t suffix = 2;
            while (usedKeys.find(candidate) != usedKeys.end()) {
                candidate = base + "-" + std::to_string(suffix++);
            }
            *key = std::move(candidate);
        }
        if (const auto* key = std::get_if<std::string>(&found->second.value);
            key != nullptr && !key->empty()) {
            usedKeys.insert(*key);
        }
    }
    for (auto& child : node.children) makePastedKeysUnique(child, usedKeys);
    for (auto& [slot, children] : node.slots) {
        (void)slot;
        for (auto& child : children) makePastedKeysUnique(child, usedKeys);
    }
}

void collectClipboardNodes(const dsl::DesignNode& node,
                           const std::set<dsl::DesignNodeId>& selected,
                           bool selectedAncestor,
                           std::vector<dsl::DesignNode>& result) {
    const bool isSelected = selected.contains(node.id);
    if (isSelected && !selectedAncestor) {
        result.push_back(node);
        return;
    }
    for (const auto& child : node.children) {
        collectClipboardNodes(child, selected, selectedAncestor || isSelected,
                              result);
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            collectClipboardNodes(child, selected,
                                  selectedAncestor || isSelected, result);
        }
    }
}

bool isEditableProperty(const dsl::DesignValue& value) {
    return std::holds_alternative<bool>(value.value) ||
           std::holds_alternative<double>(value.value) ||
           std::holds_alternative<std::string>(value.value) ||
           std::holds_alternative<dsl::DesignEnum>(value.value);
}

struct DesignNodeLocation {
    dsl::DesignNodeId parent{0};
    std::size_t index{0};
    std::string slot{};
};

std::optional<DesignNodeLocation> locateDesignNode(
    const dsl::DesignNode& parent, dsl::DesignNodeId id) {
    for (std::size_t index = 0; index < parent.children.size(); ++index) {
        if (parent.children[index].id == id) {
            return DesignNodeLocation{parent.id, index, {}};
        }
    }
    for (const auto& [slot, children] : parent.slots) {
        for (std::size_t index = 0; index < children.size(); ++index) {
            if (children[index].id == id) {
                return DesignNodeLocation{parent.id, index, slot};
            }
        }
    }
    for (const auto& child : parent.children) {
        if (const auto found = locateDesignNode(child, id); found.has_value()) {
            return found;
        }
    }
    for (const auto& [slot, children] : parent.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto found = locateDesignNode(child, id);
                found.has_value()) {
                return found;
            }
        }
    }
    return std::nullopt;
}

// 声明属性的数字读取（缺省 0）：拖动对齐候选按文档 left/top/width/
// height 收集，不依赖渲染树（zoom 无关）。
float designNumber(const dsl::DesignNode& node, const char* name) {
    const auto found = node.properties.find(name);
    if (found == node.properties.end()) return 0.0F;
    const auto* number = std::get_if<double>(&found->second.value);
    return number != nullptr ? static_cast<float>(*number) : 0.0F;
}

const dsl::DesignPreviewOutlineNode* findOutlineNode(    const dsl::DesignPreviewOutlineNode& node, dsl::DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findOutlineNode(child, id); found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

std::optional<std::string> outlinePathForNode(
    const dsl::DesignPreviewOutlineNode& node, dsl::DesignNodeId id) {
    if (node.id == id) return node.path;
    for (const auto& child : node.children) {
        if (auto path = outlinePathForNode(child, id); path.has_value()) {
            return path;
        }
    }
    return std::nullopt;
}

}  // namespace

bool DesignerApp::OfflineRuntimeContext::validatesReferences() const {
    return true;
}

bool DesignerApp::OfflineRuntimeContext::resolveReference(
    dsl::DesignReferenceKind kind, const std::string& name,
    dsl::DesignReference& out) const {
    return resolveReferenceForNode(dsl::DesignNode{}, kind, name, out);
}

bool DesignerApp::OfflineRuntimeContext::resolveReferenceForNode(
    const dsl::DesignNode& node, dsl::DesignReferenceKind kind,
    const std::string& name, dsl::DesignReference& out) const {
    if (name.empty()) return false;
    // The designer can display bindings, handlers, and image names without
    // owning their application objects. The two deterministic source names
    // below keep L2 previews usable offline; other source names remain
    // explicit missing-reference diagnostics.
    switch (kind) {
        case dsl::DesignReferenceKind::Binding:
        case dsl::DesignReferenceKind::Handler:
        case dsl::DesignReferenceKind::Image:
            out = dsl::DesignReference{kind, name, {}, nullptr};
            return true;
        case dsl::DesignReferenceKind::VirtualSource: {
            if (name != "preview_rows") return false;
            const float zoom = owner_ != nullptr
                                   ? owner_->canvasTransform_.zoom()
                                   : 1.0F;
            auto source = std::make_shared<core::VirtualListController>();
            source->setItemCount(12);
            source->setEstimatedExtent(28.0F * zoom);
            const auto onSelect = node.id == 0
                                      ? std::string{}
                                      : "designer:select:" +
                                            std::to_string(node.id);
            source->setItemBuilder([zoom, onSelect](std::size_t index) {
                auto item = core::makeText(
                    "Preview row " + std::to_string(index + 1));
                item.key = "designer-preview-row:" + std::to_string(index);
                item.height = 28.0F * zoom;
                if (!onSelect.empty()) item.onClick = onSelect;
                return item;
            });
            out = dsl::DesignReference{kind, name, source, source.get()};
            return true;
        }
        case dsl::DesignReferenceKind::SplitterSource: {
            if (name != "preview_splitter") return false;
            const float zoom = owner_ != nullptr
                                   ? owner_->canvasTransform_.zoom()
                                   : 1.0F;
            auto source =
                std::make_shared<widgets::SplitterController>(180.0F * zoom);
            out = dsl::DesignReference{kind, name, source, source.get()};
            return true;
        }
        case dsl::DesignReferenceKind::Theme: {
            // ThemeScope references must remain deterministic and offline.
            // Keep the Theme value in the preview session instead of exposing
            // a shell-owned pointer or consulting application state.
            if (name != "light" && name != "dark") return false;
            auto themeData = style::makeThemeScopeData(
                name == "light" ? style::Theme::light()
                                : style::Theme::dark());
            out = dsl::DesignReference{kind, name, themeData, themeData.get()};
            return true;
        }
        case dsl::DesignReferenceKind::Component:
            return false;
    }
    return false;
}

dsl::DesignComponentResult DesignerApp::OfflineRuntimeContext::buildComponent(
    const dsl::DesignNode& node,
    const dsl::DesignComponentContext& componentContext) const {
    (void)componentContext;
    if (owner_ == nullptr ||
        (node.type != "ComboBox" && node.type != "ColorPicker" &&
         node.type != "Spin" && node.type != "DataGrid" &&
         node.type != "ToolBar" && node.type != "StatusBar" &&
         node.type != "Menu" && node.type != "DialogHost" &&
         node.type != "Navigator" && node.type != "Form")) {
        return dsl::DesignComponentResult{
            std::nullopt, {}, "component.missing",
            "no component builder is registered for this node type"};
    }
    dsl::DesignComponentResult result;
    if (node.type == "ComboBox") {
        result.widget = owner_->comboPreviewController_.build(
            owner_->shell_.theme());
    } else if (node.type == "ColorPicker") {
        result.widget = owner_->colorPickerPreviewController_.build(
            owner_->shell_, owner_->shell_.theme());
    } else if (node.type == "Spin") {
        result.widget = owner_->spinPreviewController_.build(
            owner_->shell_.theme());
    } else if (node.type == "DataGrid") {
        result.widget = owner_->dataGridPreviewController_.build();
    } else if (node.type == "ToolBar") {
        result.widget = owner_->toolBarPreviewController_.build(
            owner_->shell_, owner_->shell_.theme());
    } else if (node.type == "StatusBar") {
        result.widget = owner_->statusBarPreviewController_.build(
            owner_->shell_.theme());
    } else if (node.type == "Menu") {
        result.widget = owner_->menuPreviewController_.build(
            owner_->shell_.theme());
    } else if (node.type == "DialogHost") {
        const std::string prefix =
            "designer:component:DialogHost:" + std::to_string(node.id) + ":";
        const auto& theme = owner_->shell_.theme();
        const std::string openHandler = prefix + "open";
        owner_->shell_.handlers()[openHandler] = [owner = owner_] {
            owner->dialogPreviewController_.showConfirm(
                owner->shell_, "Preview dialog", "DialogHost component preview",
                widgets::DialogHost::Buttons{"Accept", "Cancel"},
                [owner](bool accepted) {
                    owner->statusMessage_ =
                        accepted ? "Dialog accepted" : "Dialog cancelled";
                    owner->refreshDocumentUi();
                    owner->shell_.markDirty();
                });
            owner->refreshDocumentUi();
            owner->shell_.markDirty();
        };
        auto open = core::makeButton(
            "Open dialog", theme.typography.body, {}, 0.0F,
            "dialog-open-button", std::nullopt, std::nullopt, openHandler);
        open.buttonVariant = core::ButtonVariant::Tonal;
        auto panel = core::makeColumn(
            {core::makeText("DialogHost", theme.typography.label, {}, 0.0F,
                            "dialog-title"),
             core::makeText("Modal lifecycle preview", theme.typography.body,
                            {}, 0.0F, "dialog-description"),
             std::move(open)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
            8.0F, {}, {}, "dialog-host-preview");
        prefixWidgetKeys(panel, prefix);
        if (const auto dialog = owner_->dialogPreviewController_.build(
                owner_->shell_);
            dialog.has_value()) {
            result.widget = core::makeStack(
                {std::move(panel), std::move(*dialog)},
                core::StackAlignment::TopLeft, {}, {}, prefix + "overlay");
        } else {
            result.widget = std::move(panel);
        }
        return result;
    } else if (node.type == "Navigator") {
        const std::string prefix =
            "designer:component:Navigator:" + std::to_string(node.id) + ":";
        const auto& theme = owner_->shell_.theme();
        std::vector<core::Widget> routes;
        routes.push_back(core::makeText(
            "Route: " + owner_->navigatorPreviewController_.current(),
            theme.typography.label, {}, 0.0F, "navigator-route"));
        const std::array<std::pair<const char*, const char*>, 3> routeItems{{
            {"home", "Home"}, {"details", "Details"},
            {"settings", "Settings"}}};
        for (const auto& [route, label] : routeItems) {
            const std::string handler = prefix + "route:" + route;
            owner_->shell_.handlers()[handler] =
                [owner = owner_, route = std::string{route}] {
                    if (route == "home") {
                        owner->navigatorPreviewController_.popToRoot();
                    } else {
                        owner->navigatorPreviewController_.push(route);
                    }
                    owner->statusMessage_ =
                        "Route: " + owner->navigatorPreviewController_.current();
                    owner->refreshDocumentUi();
                    owner->shell_.markDirty();
                };
            auto button = core::makeButton(
                label, theme.typography.body, {}, 0.0F,
                "navigator-button:" + std::string{route}, std::nullopt,
                std::nullopt, handler);
            button.buttonVariant = core::ButtonVariant::Outline;
            routes.push_back(std::move(button));
        }
        result.widget = core::makeColumn(
            std::move(routes), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 8.0F, {}, {},
            "navigator-preview");
    } else {
        const std::string prefix =
            "designer:component:Form:" + std::to_string(node.id) + ":";
        const auto& theme = owner_->shell_.theme();
        const std::string nameBind = prefix + "name";
        const std::string emailBind = prefix + "email";
        owner_->formPreviewController_.registerField(
            nameBind, widgets::FormController::nonEmpty("Name is required"));
        owner_->formPreviewController_.registerField(
            emailBind,
            widgets::FormController::minLength(
                5, "Email must have at least 5 characters"));
        const auto& errors = owner_->formPreviewController_.errors();
        const auto invalid = [&errors](const std::string& key) {
            return errors.find(key) != errors.end();
        };
        auto nameField = core::makeTextField(
            owner_->shell_.state().get(nameBind), "Name", theme.typography.body,
            {}, 0.0F, "name-field", std::nullopt, std::nullopt,
            nameBind);
        nameField.invalid = invalid(nameBind);
        auto emailField = core::makeTextField(
            owner_->shell_.state().get(emailBind), "name@example.com",
            theme.typography.body, {}, 0.0F, "email-field",
            std::nullopt, std::nullopt, emailBind);
        emailField.invalid = invalid(emailBind);
        const std::string submitHandler = prefix + "submit";
        owner_->shell_.handlers()[submitHandler] =
            [owner = owner_] {
                const bool valid =
                    owner->formPreviewController_.validate(owner->shell_.state());
                owner->statusMessage_ = valid ? "Form valid" : "Form has errors";
                owner->refreshDocumentUi();
                owner->shell_.markDirty();
            };
        auto submit = core::makeButton(
            "Validate", theme.typography.body, {}, 0.0F,
            "submit-button", std::nullopt, std::nullopt,
            submitHandler);
        submit.buttonVariant = core::ButtonVariant::Filled;
        result.widget = core::makeColumn(
            {core::makeText("Profile", theme.typography.label, {}, 0.0F,
                            "form-title"),
             widgets::makeFormField(
                 "Name", std::move(nameField),
                 errors.contains(nameBind) ? errors.at(nameBind) : "", theme,
                 "form-name"),
             widgets::makeFormField(
                 "Email", std::move(emailField),
                 errors.contains(emailBind) ? errors.at(emailBind) : "", theme,
                 "form-email"),
             std::move(submit)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
            8.0F, {}, {}, "form-preview");
    }
    prefixWidgetKeys(*result.widget,
                     "designer:component:" + node.type + ":" +
                         std::to_string(node.id) + ":");
    return result;
}

void DesignerApp::OutlineModel::setRoot(
    std::optional<dsl::DesignPreviewOutlineNode> root) {
    entries_.clear();
    children_.clear();
    if (root.has_value()) visit(*root, {});
}

void DesignerApp::OutlineModel::visit(
    const dsl::DesignPreviewOutlineNode& node, const std::string& parent) {
    entries_[node.path] = Entry{node.id, nodeLabel(node)};
    children_[parent].push_back(node.path);
    for (const auto& child : node.children) visit(child, node.path);
}

std::optional<dsl::DesignNodeId> DesignerApp::OutlineModel::idForKey(
    const std::string& key) const {
    const auto found = entries_.find(key);
    if (found == entries_.end()) return std::nullopt;
    return found->second.id;
}

std::size_t DesignerApp::OutlineModel::childCount(
    const std::string& parent) const {
    const auto found = children_.find(parent);
    return found == children_.end() ? 0 : found->second.size();
}

std::string DesignerApp::OutlineModel::childAt(const std::string& parent,
                                                std::size_t index) const {
    const auto found = children_.find(parent);
    if (found == children_.end() || index >= found->second.size()) return {};
    return found->second[index];
}

bool DesignerApp::OutlineModel::hasChildren(const std::string& key) const {
    return childCount(key) != 0;
}

core::Widget DesignerApp::OutlineModel::buildRow(const std::string& key,
                                                  std::size_t) const {
    const auto found = entries_.find(key);
    if (found == entries_.end()) return core::makeText("Missing node");
    return core::makeText(found->second.label,
                          owner_->shell_.theme().typography.body, {}, 0.0F,
                          "designer:outline:" + key);
}

DesignerApp::DesignerApp()
    : runtimeContext_(this), outlineModel_(this), shell_(configFor(this)),
      previewShell_(previewConfigFor(this)) {
    // B 外壳分栏初始值（workspace-design §3.1）：rail 48 外置，左 224，
    // 右中心 716（1280-48-224-6-6-280），轨道 6，左下限 180/右下限 232，
    // 上限 400（右上限经偏好钳制，见 buildWorkspace）。
    leftSplitter_.setOffset(224.0F);
    leftSplitter_.setResetOffset(224.0F);
    leftSplitter_.setMinLeading(180.0F);
    leftSplitter_.setMinTrailing(498.0F);
    rightSplitter_.setOffset(716.0F);
    rightSplitter_.setResetOffset(716.0F);
    rightSplitter_.setMinLeading(260.0F);
    rightSplitter_.setMinTrailing(232.0F);
    // 右拖动回填偏好（resize/折叠时恢复偏好，中心吃增量）。
    rightSplitter_.onOffsetChanged = [this](float offset) {
        const float extent = rightSplitter_.extentPx();
        if (extent > 0.0F) {
            const float right =
                extent - offset - core::kSplitterTrackThickness;
            if (std::isfinite(right)) {
                rightWidthPreference_ =
                    std::clamp(right, 232.0F, 400.0F);
            }
        }
    };
    leftSplitter_.onOffsetChanged = [this](float offset) {
        (void)offset;
        // 左偏好即 offset 本身（180–400 钳制经控制器 min/布局钳制；
        // 上限 400 在 buildWorkspace 显式钳制）。
    };
    applyEnvironmentTheme();
    resourcePolicy_.allowKind(dsl::DesignResourceKind::Image);
    resourceAuthorizer_ = dsl::DesignResourceAuthorizer{resourcePolicy_};
    shell_.setResourceManager(resourceManager_);
    previewShell_.setResourceManager(resourceManager_);
    workbench_.setEditRuntimeContext(&runtimeContext_);
    dataGridPreviewController_.setColumns({
        widgets::DataColumn{"id", "ID", 88.0F, true, false, false},
        widgets::DataColumn{"name", "Name", 156.0F, true, false, false},
        widgets::DataColumn{"state", "State", 112.0F, false, false, false},
    });
    dataGridPreviewController_.setRowCount(4);
    dataGridPreviewController_.setCellText(
        [](std::size_t row, const std::string& column) {
            if (column == "id") return "D-" + std::to_string(row + 1);
            if (column == "name") {
                return "Preview row " + std::to_string(row + 1);
            }
            if (column == "state") return row == 0 ? std::string{"Ready"}
                                                     : std::string{"Draft"};
            return std::string{};
        });
    spinPreviewController_.setRange(0.0, 100.0);
    spinPreviewController_.setStep(1.0);
    spinPreviewController_.setLabel("Preview value");
    toolBarPreviewController_.setItems({
        widgets::ToolBarItem{.id = "open",
                             .icon = core::IconId::Folder,
                             .label = "Open",
                             .shortcut = "Ctrl+O"},
        widgets::ToolBarItem{.id = "run",
                             .icon = core::IconId::Play,
                             .label = "Run",
                             .shortcut = "Ctrl+R",
                             .checkable = true,
                             .labelMode = true},
        widgets::ToolBarItem{.id = "grid",
                             .icon = core::IconId::Grid,
                             .label = "Grid",
                             .checkable = true},
    });
    toolBarPreviewController_.setSemanticsLabel("Designer component toolbar");
    statusBarPreviewController_.setIdleMessage("Designer ready");
    statusBarPreviewController_.setItems({
        widgets::StatusBarItem{.id = "status",
                               .kind = widgets::StatusItemKind::Text,
                               .text = "Preview"},
        widgets::StatusBarItem{.id = "separator",
                               .kind = widgets::StatusItemKind::Separator},
        widgets::StatusBarItem{.id = "progress",
                               .kind = widgets::StatusItemKind::Progress},
    });
    statusBarPreviewController_.setProgress(68.0F);
    statusBarPreviewController_.setSemanticsLabel("Designer component status");
    menuPreviewController_.setMenus({{"file", "File", 'f'},
                                      {"view", "View", 'v'},
                                      {"help", "Help", 'h'}});
    menuPreviewController_.setMenuProvider([this](const std::string& id) {
        widgets::MenuItems items;
        if (id == "file") {
            items.push_back({.id = "open", .label = "Open",
                             .shortcut = "Ctrl+O"});
            items.push_back({.id = "save", .label = "Save",
                             .shortcut = "Ctrl+S"});
            items.push_back({.id = "sep", .separator = true});
            items.push_back({.id = "close", .label = "Close",
                             .enabled = false});
        } else if (id == "view") {
            items.push_back({.id = "density", .label = "Density",
                             .hasSubmenu = true});
            items.push_back({.id = "guides", .label = "Canvas guides",
                             .checkable = true,
                             .checked = canvasGuidesEnabled_});
        } else if (id == "help") {
            items.push_back({.id = "about", .label = "About Lumen"});
        }
        return items;
    });
    menuPreviewController_.setSubmenuProvider([this](const std::string& id) {
        if (id != "density") return widgets::MenuItems{};
        return widgets::MenuItems{
            {.id = "comfortable", .label = "Comfortable", .checkable = true,
             .checked = density_ == style::ControlDensity::Comfortable},
            {.id = "compact", .label = "Compact", .checkable = true,
             .checked = density_ == style::ControlDensity::Compact},
            {.id = "touch", .label = "Touch", .checkable = true,
             .checked = density_ == style::ControlDensity::Touch}};
    });
    menuPreviewController_.onCommand = [this](const std::string& id) {
        statusMessage_ = "Menu command: " + id;
        if (id == "guides") toggleCanvasGuides();
        shell_.markDirty();
    };
    registerCommands();
}

void DesignerApp::registerCommands() {
    const auto action = [this](const std::string& id,
                               std::function<void()> callback) {
        app::CommandSpec spec;
        spec.id = id;
        spec.label = id;
        spec.invoke = [callback = std::move(callback)](app::AppShell&) {
            callback();
        };
        shell_.commands().registerCommand(std::move(spec));
    };
    const auto chord = [this](const std::string& id, char key,
                              core::KeyModifiers modifiers,
                              std::function<void()> action,
                              std::function<bool()> enabled = {},
                              bool allowWhenTextFieldFocused = false) {
        app::CommandSpec spec;
        spec.id = id;
        spec.binding = app::KeyBinding::chord(key, modifiers);
        spec.invoke = [action = std::move(action)](app::AppShell&) { action(); };
        spec.enabled = std::move(enabled);
        spec.allowWhenTextFieldFocused = allowWhenTextFieldFocused;
        shell_.commands().registerCommand(std::move(spec));
    };
    const auto ctrl = core::kModifierCtrl;
    const auto gui = core::kModifierGui;
    const auto registerBoth = [&](const std::string& id, char key,
                                  core::KeyModifiers modifiers,
                                  std::function<void()> action,
                                  std::function<bool()> enabled = {}) {
        const bool allowWhenTextFieldFocused =
            id == "designer.undo" || id == "designer.redo" ||
            id == "designer.redo-shift";
        chord(id, key, modifiers | ctrl, action, enabled,
              allowWhenTextFieldFocused);
        chord(id + ".gui", key, modifiers | gui, std::move(action),
              std::move(enabled), allowWhenTextFieldFocused);
    };
    const auto plainBoth = [&](const std::string& id, core::Key key,
                               std::function<void()> action) {
        app::CommandSpec ctrlSpec;
        ctrlSpec.id = id;
        ctrlSpec.binding = app::KeyBinding::plain(key, ctrl);
        ctrlSpec.invoke = [action](app::AppShell&) { action(); };
        // 运行预览覆盖层打开时 suspend 结构键（Delete/大纲/排序同理，
        // 见 configFor），避免模态后文档被意外改动。
        ctrlSpec.enabled = [this] {
            return !shell_.controller().wantsTextInput() && !runPreviewOpen_;
        };
        shell_.commands().registerCommand(std::move(ctrlSpec));
        app::CommandSpec guiSpec;
        guiSpec.id = id + ".gui";
        guiSpec.binding = app::KeyBinding::plain(key, gui);
        guiSpec.enabled = [this] {
            return !shell_.controller().wantsTextInput() && !runPreviewOpen_;
        };
        guiSpec.invoke = [action = std::move(action)](app::AppShell&) {
            action();
        };
        shell_.commands().registerCommand(std::move(guiSpec));
    };
    const auto historyEnabled = [this] {
        if (shell_.controller().composingActive()) return false;
        const auto& focused = shell_.focus().focusedKey();
        // Only declaration editors opt into document history. A text field
        // in the canvas owns its preview value and local text undo/redo.
        return !shell_.controller().wantsTextInput() ||
               focused.starts_with("designer-property-field:") ||
               focused.starts_with("designer-reference-field:");
    };
    registerBoth("designer.undo", 'z', core::kModifierNone,
                 [this] { (void)undo(); }, historyEnabled);
    registerBoth("designer.redo", 'y', core::kModifierNone,
                 [this] { (void)redo(); }, historyEnabled);
    registerBoth("designer.redo-shift", 'z', core::kModifierShift,
                 [this] { (void)redo(); }, historyEnabled);
    registerBoth("designer.open", 'o', core::kModifierNone,
                 [this] { requestOpenFile(); });
    registerBoth("designer.new-project", 'n', core::kModifierShift,
                 [this] { requestNewProjectFile(); });
    registerBoth("designer.copy", 'c', core::kModifierNone,
                 [this] { copySelectedNode(); });
    registerBoth("designer.paste", 'v', core::kModifierNone,
                 [this] { pasteCopiedNode(); });
    registerBoth("designer.save", 's', core::kModifierNone,
                 [this] { requestSaveFile(); });
    registerBoth("designer.save-as", 's', core::kModifierShift,
                 [this] { requestSaveAsFile(); });
    registerBoth("designer.run", 'r', core::kModifierNone,
                 [this] { (void)startPreview(false); });
    registerBoth("designer.debug", 'd', core::kModifierNone,
                 [this] { (void)startPreview(true); });
    plainBoth("designer.move-up", core::Key::Up,
              [this] { moveSelectedNode(-1); });
    plainBoth("designer.move-down", core::Key::Down,
              [this] { moveSelectedNode(1); });
    action("designer.add-text", [this] { insertTextNode(); });
    action("designer.duplicate", [this] { duplicateSelectedNode(); });
    action("designer.remove", [this] { removeSelectedNode(); });
    action("designer.stop", [this] { stopPreview(); });
    action("designer.conflict-reload", [this] { resolveSaveConflict(false); });
    action("designer.conflict-overwrite", [this] { resolveSaveConflict(true); });
}

DesignerApp::~DesignerApp() {
    clearImageResources();
}

void DesignerApp::setResourceRoot(std::filesystem::path root) {
    clearImageResources();
    resourcePolicy_ = dsl::DesignResourcePolicy{};
    if (!root.empty()) resourcePolicy_.allowRoot("project", std::move(root));
    resourcePolicy_.allowKind(dsl::DesignResourceKind::Image);
    resourceAuthorizer_ = dsl::DesignResourceAuthorizer{resourcePolicy_};
    syncImageResources();
    shell_.markDirty();
    previewShell_.markDirty();
}

app::ShellConfig DesignerApp::configFor(DesignerApp* self) {
    app::ShellConfig config;
    config.initialView = core::Size{1280.0F, 800.0F};
    config.build = [self] { return self->buildUi(); };
    config.onKey = [self](app::AppShell& shell, core::Key key,
                          core::KeyModifiers modifiers, char keyChar) {
        // Prerequisites §4.18: IME cancellation precedes component Escape
        // handling so it preserves the field's original selection and route.
        if (key == core::Key::Escape && shell.controller().composingActive()) {
            shell.cancelComposition();
            shell.markDirty();
            return true;
        }
        const std::string& focused = shell.focus().focusedKey();
        const bool editingText = shell.controller().wantsTextInput();
        if (self->dialogPreviewController_.handleKey(shell, key, modifiers,
                                                     keyChar)) {
            return true;
        }
        if (self->comboPreviewController_.handleKey(shell, key, modifiers,
                                                     keyChar)) {
            return true;
        }
        if (self->spinPreviewController_.handleKey(shell, key, modifiers,
                                                    keyChar)) {
            return true;
        }
        if (self->menuPreviewController_.handleKey(shell, key, modifiers,
                                                   keyChar)) {
            return true;
        }
        // §8.8：Space 按住进入平移预备（输入框内除外；消费避免触发
        // 焦点控件激活），释放经 onKeyUp 复位。
        if (keyChar == ' ' && !editingText) {
            self->spacePanPressed_ = true;
            return true;
        }
        if (key == core::Key::Escape &&
            (self->outlineDragActive_ || self->toolboxDragActive_ ||
             self->canvasResizeActive_ ||
             shell.controller().dragSessionActive())) {
            shell.pointerCancel();
            return true;
        }
        // B 菜单 popover：Esc 优先关闭（workspace-design §4.4）。
        if (key == core::Key::Escape && self->openMenu_.has_value()) {
            self->openMenu_.reset();
            shell.markDirty();
            return true;
        }
        // B 专注模式：Esc 退出（workspace-design §4.4，HTML Esc 契约）。
        if (key == core::Key::Escape && self->zenMode_) {
            self->zenMode_ = false;
            shell.markDirty();
            return true;
        }
        if (key == core::Key::Escape &&
            self->navigatorPreviewController_.handleBack(false)) {
            self->statusMessage_ =
                "Route: " + self->navigatorPreviewController_.current();
            self->refreshDocumentUi();
            shell.markDirty();
            return true;
        }
        // B 运行预览覆盖层：预览内路由/对话框都不消费 Esc 时关闭
        // （等价停止会话），再交后续路由。
        if (key == core::Key::Escape && self->runPreviewOpen_) {
            self->stopPreview();
            return true;
        }
        if (!editingText && key == core::Key::Delete &&
            (modifiers & core::kModifierAlt) == 0 && !self->runPreviewOpen_) {
            self->removeSelectedNode();
            return true;
        }
        if (!self->runPreviewOpen_ &&
            ((focused == "designer-outline" ||
              focused.starts_with("designer-outline:")) &&
             self->outlineController_.handleKey(key, modifiers, keyChar))) {
            return true;
        }
        return false;
    };
    config.onWheel = [self](const core::RenderNode&,
                            const core::RenderNode* viewport,
                            core::Offset position, core::Offset delta,
                            core::KeyModifiers modifiers) {
        if (self->spinPreviewController_.handleWheel(self->shell_, position,
                                                     delta)) {
            return true;
        }
        // 框架解析出的面板滚动视口先消费（滚轮/滚动条轨道翻页/键盘/
        // 语义同 sink）；画布仅在无应用视口接管时处理。
        if (self->handlePanelWheel(viewport, delta)) {
            return true;
        }
        const auto* canvas =
            core::findNodeByKey(self->shell_.root(), "designer-canvas");
        if (canvas != nullptr) {
            const auto origin =
                core::absoluteOffset(self->shell_.root(), canvas->key);
            if (core::Rect{origin, canvas->size}.contains(position)) {
                // §8.8（designer-canvas.html）：Ctrl+滚轮 = 以指针为锚
                // 的档位缩放；普通滚轮 = 平移画布。
                if ((modifiers & core::kModifierCtrl) != 0U) {
                    self->adjustCanvasZoom(delta.y < 0.0F ? 1.0F : -1.0F,
                                           position);
                } else {
                    self->panCanvas(delta);
                }
                return true;
            }
        }
        return false;
    };
    // 滚动条拖动与触摸内容拖动（scrollbar/scroll-design）：面板 ScrollView
    // 的偏移在应用侧，经 drag sink 驱动；释放起惯性由 onAnimate 推进。
    config.onScrollDrag =
        [self](const core::RenderNode*, const core::RenderNode* viewport,
               core::Offset, core::Offset delta,
               core::ScrollDragPhase phase, std::uint64_t nowMs) {
            return self->handlePanelScrollDrag(viewport, delta, phase, nowMs);
        };
    config.onKeyUp = [self](app::AppShell&, core::Key key,
                            core::KeyModifiers, char keyChar) {
        if (keyChar == ' ' && self->spacePanPressed_) {
            self->spacePanPressed_ = false;
            self->shell_.markDirty();
        }
    };
    config.onAnimate = [self](app::AppShell& shell, std::uint64_t nowMs) {
        const bool spinActive = self->spinPreviewController_.step(shell, nowMs);
        return self->advancePanelScrollFling(shell, nowMs) || spinActive;
    };
    config.onRebuilt = [self](app::AppShell& shell) {
        self->dialogPreviewController_.onRebuilt(shell);
    };
    config.onCloseRequested = [self](app::AppShell& shell) {
        return self->dialogPreviewController_.handleCloseRequested(shell);
    };
    return config;
}

app::ShellConfig DesignerApp::previewConfigFor(DesignerApp* self) {
    app::ShellConfig config;
    config.initialView = core::Size{960.0F, 640.0F};
    config.caretBlink = false;
    config.build = [self] { return self->buildPreviewWindow(); };
    return config;
}

core::ScrollController* DesignerApp::scrollControllerForKey(
    std::string_view key) {
    for (const auto known : kPanelScrollKeys) {
        if (known == key) {
            // 已知视口懒建控制器：跨重建保留偏移，首次命中即接管。
            return &panelScrolls_[std::string{known}];
        }
    }
    return nullptr;
}

core::ScrollController* DesignerApp::scrollControllerFor(
    const core::RenderNode& viewport) {
    return scrollControllerForKey(viewport.key);
}

float DesignerApp::panelScrollOffset(std::string_view key) const {
    const auto found = panelScrolls_.find(std::string{key});
    return found == panelScrolls_.end() ? 0.0F : found->second.offset();
}

bool DesignerApp::handlePanelWheel(const core::RenderNode* viewport,
                                   core::Offset delta) {
    if (viewport == nullptr) return false;
    auto* scroll = scrollControllerFor(*viewport);
    if (scroll == nullptr) return false;
    scroll->updateExtents(viewport->size.height,
                          viewport->size.height + viewport->scrollExtent);
    // |delta| > 1e8 为 Home/End 端点哨兵（键盘/语义路径与 sink 约定一致）。
    if (std::abs(delta.y) > 1e8F) {
        scroll->scrollTo(delta.y > 0.0F ? scroll->maxScrollOffset() : 0.0F);
        shell_.markDirty();
        return true;
    }
    const bool changed = scroll->applyWheel(delta.y);
    if (changed) shell_.markDirty();
    return changed;
}

bool DesignerApp::handlePanelScrollDrag(const core::RenderNode* viewport,
                                        core::Offset delta,
                                        core::ScrollDragPhase phase,
                                        std::uint64_t nowMs) {
    auto* scroll =
        viewport != nullptr ? scrollControllerFor(*viewport) : nullptr;
    if (scroll == nullptr) {
        // 窗口级取消无具体视口：冲洗全部采样，避免陈旧速度污染下次拖动。
        if (phase == core::ScrollDragPhase::Cancel) {
            for (auto& [key, controller] : panelScrolls_) {
                controller.cancelDrag();
            }
        }
        return false;
    }
    switch (phase) {
        case core::ScrollDragPhase::Begin:
            scroll->cancelDrag();
            return true;
        case core::ScrollDragPhase::Update:
            scroll->updateExtents(viewport->size.height,
                                  viewport->size.height +
                                      viewport->scrollExtent);
            scroll->noteDragSample(delta.y, nowMs);
            if (scroll->applyDrag(delta.y)) {
                shell_.markDirty();
                return true;
            }
            return false;
        case core::ScrollDragPhase::End:
            if (scroll->endDrag(nowMs)) {
                shell_.markDirty();
                return true;
            }
            return false;
        case core::ScrollDragPhase::Cancel:
            scroll->cancelDrag();
            return false;
    }
    return false;
}

bool DesignerApp::advancePanelScrollFling(app::AppShell& shell,
                                          std::uint64_t nowMs) {
    // 源视口惯性由框架 advanceSourceFling 推进；这里只管应用侧面板。
    bool active = false;
    for (auto& [key, scroll] : panelScrolls_) {
        if (scroll.isFlinging()) {
            shell.markDirty();
            active = scroll.stepFling(nowMs) || active;
        }
    }
    return active;
}

void DesignerApp::attach() {
    diagnosticsController_.setItemBuilder([this](std::size_t index) {
        return buildDiagnosticRow(index);
    });
    outlineController_.setSelectionMode(widgets::SelectionMode::Extended);
    outlineController_.setModel(&outlineModel_);
    outlineController_.attach(shell_, "designer-outline");
    outlineController_.onActivated = [this](const std::string& key) {
        const auto id = outlineModel_.idForKey(key);
        if (id.has_value()) {
            // Pointer, keyboard and accessibility activation must update the
            // document selection and the outline selection together. Updating
            // only the workbench is overwritten by the next buildUi sync.
            selectNode(*id);
        }
    };
    referencesController_.setSelectionMode(widgets::SelectionMode::None);
    referencesController_.setColumns({
        widgets::DataColumn{"name", "Reference", 156.0F, false, false, false},
        widgets::DataColumn{"kind", "Kind", 92.0F, false, false, false},
        widgets::DataColumn{"status", "State", 92.0F, false, false, false},
        widgets::DataColumn{"value", "Value", 176.0F, false, false, false},
        widgets::DataColumn{"location", "Node", 220.0F, false, false, false},
    });
    referencesController_.setKeyOf(
        [this](std::size_t index) {
            return index < referenceRows_.size() ? referenceRows_[index].key
                                                  : std::string{};
        });
    referencesController_.setCellText(
        [this](std::size_t index, const std::string& column) {
            if (index >= referenceRows_.size()) return std::string{};
            const auto& row = referenceRows_[index];
            if (column == "name") return row.property;
            if (column == "kind") return row.kind;
            if (column == "status") return row.status;
            if (column == "value") return row.value;
            if (column == "location") return row.location;
            return std::string{};
        });
    referencesController_.onRowActivated =
        [this](const std::string& key) { selectReference(key); };
    referencesController_.attach(shell_, "designer-references");
    comboPreviewController_.attach(shell_);
    colorPickerPreviewController_.attach(shell_);
    spinPreviewController_.attach(shell_);
    menuPreviewController_.attach(shell_);
    dataGridPreviewController_.attach(shell_, "designer-component-datagrid");
    toolBarPreviewController_.attach(shell_);
    statusBarPreviewController_.attach(shell_);
    shell_.controller().addDragArmSink(
        [this](const std::vector<const core::RenderNode*>& chain,
               core::PointerDevice, core::DragSourceClaim& claim) {
            // §8.8 Space+拖动 = 平移画布：按住期最优先认领（画布区域），
            // 拖放/换位/橡皮筋全部让位。
            if (spacePanPressed_) {
                for (const auto* node : chain) {
                    if (node != nullptr && node->key == "designer-canvas") {
                        claim.key = "designer:space-pan";
                        claim.identity = "designer:space-pan";
                        claim.touchAllowed = false;
                        return true;
                    }
                }
            }
            constexpr std::string_view kHandlePrefix =
                "designer-canvas-handle:";
            for (const auto* node : chain) {
                if (node == nullptr ||
                    node->onClick.rfind(kHandlePrefix, 0) != 0) {
                    continue;
                }
                claim.key = node->key;
                claim.identity = node->identity;
                claim.touchAllowed = true;
                return true;
            }
            constexpr std::string_view kRowPrefix =
                "tree:designer-outline:";
            for (const auto* node : chain) {
                if (node == nullptr || node->onClick.rfind(kRowPrefix, 0) !=
                                             0) {
                    continue;
                }
                const auto id = outlineModel_.idForKey(
                    node->onClick.substr(kRowPrefix.size()));
                if (!id.has_value() || !workbench_.document().has_value()) {
                    return false;
                }
                claim.key = node->key;
                claim.identity = node->identity;
                claim.touchAllowed = false;
                return locateDesignNode(workbench_.document()->root, *id)
                    .has_value();
            }
            constexpr std::string_view kToolboxPrefix = "designer:toolbox:";
            for (const auto* node : chain) {
                if (node == nullptr ||
                    node->onClick.rfind(kToolboxPrefix, 0) != 0) {
                    continue;
                }
                claim.key = node->key;
                claim.identity = node->identity;
                claim.touchAllowed = false;
                return workbench_.document().has_value();
            }
            // 画布拖动换位：命中最深的 select 目标且其文档父节点为
            // Stack（left/top 归设计者所有）才认领；流式父级（Row/
            // Column 等）的位置由布局拥有，不可拖动。目标 id 编码进
            // 伪手柄 drag:<id>，会话以拖动节点而非当前选中为目标。
            constexpr std::string_view kSelectPrefix = "designer:select:";
            for (const auto* node : chain) {
                if (node == nullptr ||
                    node->onClick.rfind(kSelectPrefix, 0) != 0) {
                    continue;
                }
                if (workbench_.document().has_value()) {
                    const std::string_view idText = node->onClick;
                    std::uint64_t id = 0;
                    const auto parsed =
                        std::from_chars(idText.data() + kSelectPrefix.size(),
                                        idText.data() + idText.size(), id);
                    if (parsed.ec == std::errc{} &&
                        parsed.ptr == idText.data() + idText.size()) {
                        const auto location = locateDesignNode(
                            workbench_.document()->root,
                            static_cast<dsl::DesignNodeId>(id));
                        if (location.has_value()) {
                            const auto* parent = findDesignNode(
                                workbench_.document()->root,
                                location->parent);
                            if (parent != nullptr && parent->type == "Stack") {
                                claim.key = "designer-canvas-handle:drag:" +
                                            std::to_string(id);
                                claim.identity = node->identity;
                                claim.touchAllowed = true;
                                return true;
                            }
                        }
                    }
                }
                return false;
            }
            bool overCanvas = false;
            for (const auto* node : chain) {
                if (node == nullptr) continue;
                if (node->key == "designer-canvas") overCanvas = true;
                if (node->onClick.rfind("designer:select:", 0) == 0) {
                    return false;
                }
            }
            if (overCanvas && workbench_.document().has_value()) {
                claim.key = "designer-canvas-selection";
                claim.identity = "designer-canvas-selection";
                claim.touchAllowed = true;
                return true;
            }
            return false;
        });
    shell_.controller().addDragSessionSink(
        [this](core::DragPhase phase, core::Offset position,
               const std::vector<const core::RenderNode*>&,
               const std::string& sourceKey, const std::string&) {
            if (sourceKey == "designer:space-pan") {
                // §8.8 平移会话：跟手写 pan，Drop/Cancel 即止。
                if (phase == core::DragPhase::Start) {
                    spacePanAnchor_ = position;
                    spacePanStart_ = canvasTransform_.pan();
                    shell_.markDirty();
                } else if (phase == core::DragPhase::Move) {
                    (void)canvasTransform_.setPan(
                        spacePanStart_ + (position - spacePanAnchor_));
                    shell_.markDirty();
                }
                return;
            }
            canvasResizeSession(phase, position, sourceKey);
            outlineDragSession(phase, position, sourceKey);
            canvasSelectionSession(phase, position, sourceKey);
            toolboxDragSession(phase, position, sourceKey);
        });
    shell_.handlers()["designer:theme"] = [this] { toggleTheme(); };
    shell_.handlers()["designer:density"] = [this] { cycleDensity(); };
    shell_.handlers()["designer:dpi"] = [this] { cycleDpi(); };
    shell_.handlers()["designer:zoom-in"] =
        [this] { adjustCanvasZoom(1.0F); };
    shell_.handlers()["designer:zoom-out"] =
        [this] { adjustCanvasZoom(-1.0F); };
    shell_.handlers()["designer:zoom-reset"] =
        [this] { resetCanvasView(); };
    shell_.handlers()["designer:pan-left"] =
        [this] { panCanvas(core::Offset{-40.0F, 0.0F}); };
    shell_.handlers()["designer:pan-right"] =
        [this] { panCanvas(core::Offset{40.0F, 0.0F}); };
    shell_.handlers()["designer:pan-up"] =
        [this] { panCanvas(core::Offset{0.0F, -40.0F}); };
    shell_.handlers()["designer:pan-down"] =
        [this] { panCanvas(core::Offset{0.0F, 40.0F}); };
    shell_.handlers()["designer:font-scale"] = [this] { cycleFontScale(); };
    shell_.handlers()["designer:contrast"] = [this] { toggleHighContrast(); };
    shell_.handlers()["designer:canvas-guides"] =
        [this] { toggleCanvasGuides(); };
    shell_.handlers()["designer:canvas-bounds"] = [this] {
        // §8.7 容器边界独立开关（designer-canvas.html"显示容器边界"）。
        canvasBoundsEnabled_ = !canvasBoundsEnabled_;
        shell_.markDirty();
    };
    shell_.handlers()["designer:zoom-fit"] = [this] { fitCanvasView(); };
    shell_.handlers()["designer:canvas-align"] = [this] {
        // §8.8 对齐参考线（十字线 + 坐标徽标）独立开关。
        canvasAlignEnabled_ = !canvasAlignEnabled_;
        shell_.markDirty();
    };
    shell_.handlers()["designer:preview-state"] =
        [this] { cyclePreviewState(); };
    shell_.handlers()["designer:add-text"] = [this] {
        (void)shell_.invokeCommand("designer.add-text");
    };
    shell_.handlers()["designer:duplicate"] =
        [this] { (void)shell_.invokeCommand("designer.duplicate"); };
    shell_.handlers()["designer:copy"] = [this] {
        (void)shell_.invokeCommand("designer.copy");
    };
    shell_.handlers()["designer:paste"] = [this] {
        (void)shell_.invokeCommand("designer.paste");
    };
    shell_.handlers()["designer:remove"] = [this] {
        (void)shell_.invokeCommand("designer.remove");
    };
    shell_.handlers()["designer:move-up"] = [this] {
        (void)shell_.invokeCommand("designer.move-up");
    };
    shell_.handlers()["designer:move-down"] = [this] {
        (void)shell_.invokeCommand("designer.move-down");
    };
    shell_.handlers()["designer:open"] = [this] {
        (void)shell_.invokeCommand("designer.open");
    };
    shell_.handlers()["designer:new-project"] = [this] {
        (void)shell_.invokeCommand("designer.new-project");
    };
    shell_.handlers()["designer:save"] = [this] {
        (void)shell_.invokeCommand("designer.save");
    };
    shell_.handlers()["designer:save-as"] = [this] {
        (void)shell_.invokeCommand("designer.save-as");
    };
    shell_.handlers()["designer:conflict-reload"] = [this] {
        (void)shell_.invokeCommand("designer.conflict-reload");
    };
    shell_.handlers()["designer:conflict-overwrite"] = [this] {
        (void)shell_.invokeCommand("designer.conflict-overwrite");
    };
    shell_.handlers()["designer:run"] = [this] {
        (void)shell_.invokeCommand("designer.run");
    };
    shell_.handlers()["designer:debug"] = [this] {
        (void)shell_.invokeCommand("designer.debug");
    };
    shell_.handlers()["designer:stop"] = [this] {
        (void)shell_.invokeCommand("designer.stop");
    };
    // 自定义标题栏：窗口命令（lumen-titlebar-design §4.4）——命令
    // 始终记录（headless 断言）；平台回调由 main.cpp 注入，close 无
    // 宿主时回退统一关闭规则（modal 优先消费）。
    shell_.handlers()["window-minimize"] = [this] {
        lastWindowCommand_ = "window-minimize";
        if (windowCommands_.minimize) windowCommands_.minimize();
    };
    shell_.handlers()["window-maximize"] = [this] {
        lastWindowCommand_ = "window-maximize";
        if (windowCommands_.toggleMaximize) windowCommands_.toggleMaximize();
    };
    shell_.handlers()["window-close"] = [this] {
        lastWindowCommand_ = "window-close";
        if (windowCommands_.requestClose) {
            windowCommands_.requestClose();
            return;
        }
        (void)shell_.requestClose();
    };
    shell_.handlers()["designer:tab-canvas"] = [this] {
        centerTab_ = CenterTab::Canvas;
        centerView_ = CenterView::Design;
        shell_.markDirty();
    };
    shell_.handlers()["designer:tab-source"] = [this] {
        centerTab_ = CenterTab::Source;
        centerView_ = CenterView::Source;
        shell_.markDirty();
    };
    shell_.handlers()["designer:tab-references"] = [this] {
        centerTab_ = CenterTab::References;
        bottomTab_ = BottomTab::References;
        bottomOpen_ = true;
        centerView_ = CenterView::Design;
        shell_.markDirty();
    };
    // B 中心视图（设计/分屏/快照）与底部页签（问题/引用）。
    shell_.handlers()["designer:view-design"] = [this] {
        centerView_ = CenterView::Design;
        centerTab_ = CenterTab::Canvas;
        shell_.markDirty();
    };
    shell_.handlers()["designer:view-split"] = [this] {
        centerView_ = CenterView::Split;
        shell_.markDirty();
    };
    shell_.handlers()["designer:view-source"] = [this] {
        centerView_ = CenterView::Source;
        centerTab_ = CenterTab::Source;
        shell_.markDirty();
    };
    shell_.handlers()["designer:bottom-diagnostics"] = [this] {
        bottomTab_ = BottomTab::Diagnostics;
        bottomOpen_ = true;
        shell_.markDirty();
    };
    shell_.handlers()["designer:bottom-references"] = [this] {
        bottomTab_ = BottomTab::References;
        bottomOpen_ = true;
        shell_.markDirty();
    };
    shell_.handlers()["designer:toggle-bottom"] = [this] { toggleBottom(); };
    shell_.handlers()["designer:zen"] = [this] { toggleZen(); };
    shell_.handlers()["designer:toggle-left"] = [this] {
        toggleLeftCollapsed();
    };
    shell_.handlers()["designer:reset-layout"] = [this] { resetLayout(); };
    shell_.handlers()["designer:show-toolbox"] = [this] { showToolbox(); };
    shell_.handlers()["designer:layout-context"] = [this] {
        focusLayoutContext();
    };
    shell_.handlers()["designer:rail-toolbox"] = [this] {
        if (leftPanelTab_ == LeftPanelTab::Toolbox && !leftCollapsed_ &&
            !zenMode_) {
            leftCollapsed_ = true;
        } else {
            leftCollapsed_ = false;
            zenMode_ = false;
            leftPanelTab_ = LeftPanelTab::Toolbox;
        }
        shell_.markDirty();
    };
    shell_.handlers()["designer:rail-outline"] = [this] {
        if (leftPanelTab_ == LeftPanelTab::Outline && !leftCollapsed_ &&
            !zenMode_) {
            leftCollapsed_ = true;
        } else {
            leftCollapsed_ = false;
            zenMode_ = false;
            leftPanelTab_ = LeftPanelTab::Outline;
        }
        shell_.markDirty();
    };
    shell_.handlers()["designer:undo"] = [this] { (void)undo(); };
    shell_.handlers()["designer:redo"] = [this] { (void)redo(); };
    // B 标题栏菜单（文件/编辑/视图 popover）。toggle 互斥，Esc 关闭（configFor），
    // 菜单项关闭后转发原命令（不污染 dirty/undo，见 workspace-design §4.4）。
    shell_.handlers()["designer:menu-file"] = [this] {
        openMenu_ = (openMenu_ == "file" ? std::nullopt
                                         : std::optional<std::string>{"file"});
        shell_.markDirty();
    };
    shell_.handlers()["designer:menu-edit"] = [this] {
        openMenu_ = (openMenu_ == "edit" ? std::nullopt
                                         : std::optional<std::string>{"edit"});
        shell_.markDirty();
    };
    shell_.handlers()["designer:menu-view"] = [this] {
        openMenu_ = (openMenu_ == "view" ? std::nullopt
                                         : std::optional<std::string>{"view"});
        shell_.markDirty();
    };
    shell_.handlers()["designer:menu-close"] = [this] {
        openMenu_.reset();
        shell_.markDirty();
    };
    const auto menuForward = [this](const std::string& handler) {
        openMenu_.reset();
        const auto found = shell_.handlers().find(handler);
        // 转发时避开自身（菜单包装器与原 handler 同名时直接执行动作）。
        if (found != shell_.handlers().end() &&
            found->first != "designer:menu-close") {
            // 原 handler 内部会 markDirty；此处仅保证菜单关闭也触发重建。
        }
        shell_.markDirty();
    };
    (void)menuForward;
    // 菜单项包装器：关闭 + 原动作（复用既有命令/方法，保证单路径）。
    shell_.handlers()["designer:menu:save"] = [this] {
        openMenu_.reset();
        (void)shell_.invokeCommand("designer.save");
    };
    shell_.handlers()["designer:menu:save-as"] = [this] {
        openMenu_.reset();
        (void)shell_.invokeCommand("designer.save-as");
    };
    shell_.handlers()["designer:menu:open"] = [this] {
        openMenu_.reset();
        (void)shell_.invokeCommand("designer.open");
    };
    shell_.handlers()["designer:menu:new-project"] = [this] {
        openMenu_.reset();
        (void)shell_.invokeCommand("designer.new-project");
    };
    shell_.handlers()["designer:menu:undo"] = [this] {
        openMenu_.reset();
        (void)undo();
    };
    shell_.handlers()["designer:menu:redo"] = [this] {
        openMenu_.reset();
        (void)redo();
    };
    shell_.handlers()["designer:menu:duplicate"] = [this] {
        openMenu_.reset();
        (void)shell_.invokeCommand("designer.duplicate");
    };
    shell_.handlers()["designer:menu:remove"] = [this] {
        openMenu_.reset();
        (void)shell_.invokeCommand("designer.remove");
    };
    shell_.handlers()["designer:menu:toggle-left"] = [this] {
        openMenu_.reset();
        toggleLeftCollapsed();
    };
    shell_.handlers()["designer:menu:zen"] = [this] {
        openMenu_.reset();
        toggleZen();
    };
    shell_.handlers()["designer:menu:toggle-bottom"] = [this] {
        openMenu_.reset();
        toggleBottom();
    };
    shell_.handlers()["designer:menu:guides"] = [this] {
        openMenu_.reset();
        toggleCanvasGuides();
    };
    shell_.handlers()["designer:menu:reset-layout"] = [this] {
        openMenu_.reset();
        resetLayout();
    };
    for (const auto* schema : designerToolboxSchemas()) {
        shell_.handlers()["designer:toolbox:" + schema->type] =
            [this, type = schema->type] { insertNodeType(type); };
    }
    (void)loadSource(kSampleSource, "sample.lumen");
}

bool DesignerApp::undo() {
    const bool changed = workbench_.undo();
    if (!changed) return false;
    refreshDocumentUi();
    shell_.markDirty();
    return true;
}

bool DesignerApp::redo() {
    const bool changed = workbench_.redo();
    if (!changed) return false;
    refreshDocumentUi();
    shell_.markDirty();
    return true;
}

void DesignerApp::requestOpenFile() {
    pendingFileDialog_ = PendingFileDialog::Open;
    if (!fileDialogRequester_) {
        pendingFileDialog_ = PendingFileDialog::None;
        statusMessage_ = "Open unavailable: file dialogs are not configured";
        shell_.markDirty();
        return;
    }
    const std::string error = fileDialogRequester_(false, {});
    if (!error.empty()) {
        pendingFileDialog_ = PendingFileDialog::None;
        statusMessage_ = "Open failed: " + error;
    } else {
        statusMessage_ = "Opening document...";
    }
    shell_.markDirty();
}

void DesignerApp::requestNewProjectFile() {
    pendingFileDialog_ = PendingFileDialog::NewProject;
    if (!fileDialogRequester_) {
        pendingFileDialog_ = PendingFileDialog::None;
        statusMessage_ =
            "New project unavailable: file dialogs are not configured";
        shell_.markDirty();
        return;
    }
    const std::string error =
        fileDialogRequester_(true, "untitled.lumen-project");
    if (!error.empty()) {
        pendingFileDialog_ = PendingFileDialog::None;
        statusMessage_ = "New project failed: " + error;
    } else {
        statusMessage_ = "Choosing a project file...";
    }
    shell_.markDirty();
}

bool DesignerApp::startPreview(bool debug) {
    const bool refreshed = workbench_.document().has_value() &&
                           workbench_.refresh(&runtimeContext_);
    syncImageResources();
    if (!refreshed) {
        const bool keepActivePreview =
            previewSessionMode_ != PreviewSessionMode::Stopped &&
            workbench_.frame().hasFrame();
        if (!keepActivePreview) {
            previewSessionMode_ = PreviewSessionMode::Stopped;
            shell_.setFrameStatsCapture(false);
            shell_.setDebugFrameStats(false);
            shell_.setDebugBoundsOverlay(false);
            previewShell_.setFrameStatsCapture(false);
            previewShell_.setDebugFrameStats(false);
            previewShell_.setDebugBoundsOverlay(false);
        }
        statusMessage_ = "Run failed  /  kept previous preview";
        shell_.markDirty();
        previewShell_.markDirty();
        return false;
    }

    previewSessionMode_ = debug ? PreviewSessionMode::Debugging
                                : PreviewSessionMode::Running;
    shell_.setFrameStatsCapture(debug);
    shell_.setDebugFrameStats(debug);
    shell_.setDebugBoundsOverlay(debug);
    previewShell_.setFrameStatsCapture(debug);
    previewShell_.setDebugFrameStats(debug);
    previewShell_.setDebugBoundsOverlay(debug);
    statusMessage_ = debug ? "Debug preview running  /  current session"
                           : "Preview running  /  current session";
    openMenu_.reset();
    runPreviewOpen_ = true;
    // 每次会话从头看起：滚回顶部并停掉上次会话残留的惯性（gallery 弹窗
    // 打开即回顶，同形）。
    if (auto* previewScroll =
            scrollControllerForKey("designer-run-preview-scroll")) {
        previewScroll->stopFling();
        previewScroll->scrollTo(0.0F);
    }
    shell_.markDirty();
    previewShell_.markDirty();
    return true;
}

void DesignerApp::stopPreview() {
    runPreviewOpen_ = false;
    if (previewSessionMode_ == PreviewSessionMode::Stopped) {
        shell_.markDirty();
        return;
    }
    previewSessionMode_ = PreviewSessionMode::Stopped;
    shell_.setFrameStatsCapture(false);
    shell_.setDebugFrameStats(false);
    shell_.setDebugBoundsOverlay(false);
    previewShell_.setFrameStatsCapture(false);
    previewShell_.setDebugFrameStats(false);
    previewShell_.setDebugBoundsOverlay(false);
    statusMessage_ = "Preview stopped";
    shell_.markDirty();
    previewShell_.markDirty();
}

void DesignerApp::requestSaveAsFile() {
    pendingFileDialog_ = PendingFileDialog::SaveAs;
    if (!fileDialogRequester_) {
        pendingFileDialog_ = PendingFileDialog::None;
        statusMessage_ =
            "Save as unavailable: file dialogs are not configured";
        shell_.markDirty();
        return;
    }
    const std::string error = fileDialogRequester_(
        true, project_.has_value() ? "untitled.lumen-project" : "untitled.design");
    if (!error.empty()) {
        pendingFileDialog_ = PendingFileDialog::None;
        statusMessage_ = "Save as failed: " + error;
    } else {
        statusMessage_ = project_.has_value()
                             ? "Choosing a project file..."
                             : "Choosing a design file...";
    }
    shell_.markDirty();
}

void DesignerApp::requestSaveFile() {
    if (project_.has_value() && !projectFile_.empty()) {
        if (saveProjectFile(projectFile_)) {
            statusMessage_ = "Saved project  /  " + projectFile_;
        } else {
            statusMessage_ = "Project save failed  /  " + projectFile_;
        }
        shell_.markDirty();
        return;
    }
    if (workbench_.document().has_value() && !sourceFile_.empty() &&
        sourceFile_.front() != '<' && isDesignFile(sourceFile_)) {
        if (saveDesignFile(sourceFile_)) {
            statusMessage_ = "Saved  /  " + sourceFile_;
        } else {
            statusMessage_ = "Save failed  /  " + sourceFile_;
        }
        shell_.markDirty();
        return;
    }
    requestSaveAsFile();
}

void DesignerApp::handleFileDialogResult(
    const std::vector<std::string>& paths, const std::string& error) {
    const auto pending = pendingFileDialog_;
    pendingFileDialog_ = PendingFileDialog::None;
    if (!error.empty()) {
        statusMessage_ = "File dialog failed: " + error;
        shell_.markDirty();
        return;
    }
    if (paths.empty()) {
        statusMessage_ = "File dialog cancelled";
        shell_.markDirty();
        return;
    }

    const std::string& filename = paths.front();
    if (pending == PendingFileDialog::Open) {
        const bool loaded = isProjectFile(filename)
                                ? loadProjectFile(filename)
                                : (isDesignFile(filename) ? loadDesignFile(filename)
                                                          : loadFile(filename));
        statusMessage_ = loaded ? "Opened  /  " + filename
                                : "Open failed  /  " + filename;
    } else if (pending == PendingFileDialog::NewProject) {
        const bool created = createProjectFile(filename);
        statusMessage_ = created ? "Created project  /  " + filename
                                 : "New project failed  /  " + filename;
    } else if (pending == PendingFileDialog::SaveAs) {
        const bool saved = project_.has_value() && isProjectFile(filename)
                               ? saveProjectFile(filename)
                               : saveDesignFile(filename);
        statusMessage_ = saved ? "Saved  /  " + filename
                               : "Save failed  /  " + filename;
    } else {
        statusMessage_ = "Unexpected file dialog result";
    }
    shell_.markDirty();
}

bool DesignerApp::loadDesignFile(const std::string& filename) {
    statusMessage_.clear();
    const bool loaded = workbench_.openDesignFile(filename, &runtimeContext_);
    if (loaded) {
        clearProjectSession();
        sourceFile_ = filename;
        saveConflict_.reset();
        sourceSnapshot_ = workbench_.document().has_value()
                              ? dsl::serializeDesignDocument(
                                    *workbench_.document())
                              : std::string{};
        sourceFocusLine_ = 0;
        resetPreviewState();
        refreshDocumentUi();
    }
    shell_.markDirty();
    return loaded;
}

bool DesignerApp::saveDesignFile(const std::string& filename) {
    const bool saved = workbench_.saveDesignFile(filename);
    if (saved) {
        clearProjectSession();
        saveConflict_.reset();
        sourceFile_ = filename;
        sourceSnapshot_ = workbench_.document().has_value()
                              ? dsl::serializeDesignDocument(
                                    *workbench_.document())
                              : std::string{};
    } else if (!workbench_.diagnostics().empty() &&
               workbench_.diagnostics().front().code == "store.revision_conflict") {
        captureSaveConflict(filename, false);
    }
    shell_.markDirty();
    return saved;
}

void DesignerApp::captureSaveConflict(const std::string& filename, bool project) {
    SaveConflict conflict;
    conflict.filename = filename;
    conflict.project = project;
    conflict.revision = dsl::DocumentStore::revision(filename);
    conflict.canOverwrite = conflict.revision.has_value();
    if (project && project_.has_value()) {
        for (const auto& page : project_->pages) {
            const auto path = projectPagePath(page, filename);
            const auto revision = path.empty() ? std::nullopt
                : dsl::DocumentStore::revision(path.string());
            if (revision.has_value()) {
                conflict.pageRevisions[page.documentId] = *revision;
            } else {
                conflict.canOverwrite = false;
            }
        }
    }
    saveConflict_ = std::move(conflict);
    shell_.markDirty();
}

void DesignerApp::resolveSaveConflict(bool overwrite) {
    if (!saveConflict_.has_value()) return;
    // Loading/saving can clear or replace the pending conflict. Keep the
    // user's observed revision snapshot alive throughout this action.
    const SaveConflict conflict = *saveConflict_;
    if (overwrite && !conflict.canOverwrite) return;
    bool resolved = false;
    if (!overwrite) {
        resolved = conflict.project ? loadProjectFile(conflict.filename)
                                    : loadDesignFile(conflict.filename);
    } else if (conflict.project) {
        resolved = saveProjectFileAtConflict(conflict.filename, &conflict);
        if (!resolved) captureSaveConflict(conflict.filename, true);
    } else {
        resolved = workbench_.overwriteDesignFile(conflict.filename,
                                                   *conflict.revision);
        if (resolved) {
            sourceFile_ = conflict.filename;
            sourceSnapshot_ = dsl::serializeDesignDocument(*workbench_.document());
        } else {
            captureSaveConflict(conflict.filename, false);
        }
    }
    if (resolved) saveConflict_.reset();
    statusMessage_ = resolved
        ? (overwrite ? "Saved local changes over external changes"
                     : "Reloaded external changes; local edits discarded")
        : "Conflict unresolved; review the file and choose again";
    shell_.markDirty();
}

void DesignerApp::appendProjectDiagnostic(dsl::DesignError diagnostic,
                                          std::string documentId) {
    projectDiagnostics_.push_back(std::move(diagnostic));
    projectDiagnosticDocumentIds_.push_back(std::move(documentId));
}

std::filesystem::path DesignerApp::projectRootPath() const {
    return projectRootPath(projectFile_);
}

std::filesystem::path DesignerApp::projectRootPath(
    const std::string& manifest) const {
    if (!project_.has_value()) return {};
    std::filesystem::path root{project_->root};
    const std::filesystem::path manifestPath{manifest};
    if (root.is_relative()) root = manifestPath.parent_path() / root;
    return root.lexically_normal();
}

std::filesystem::path DesignerApp::projectRelativePath(
    const std::string& relativeText, const std::string& manifest) const {
    const auto root = projectRootPath(manifest);
    if (root.empty()) return {};
    const std::filesystem::path relative{relativeText};
    if (relative.empty() || relative.is_absolute()) return {};
    const auto candidate = (root / relative).lexically_normal();
    const auto within = candidate.lexically_relative(root);
    if (within.empty() || within == ".." ||
        (within.begin() != within.end() && *within.begin() == "..")) {
        return {};
    }
    // weakly_canonical leaves a dangling final symlink unresolved.
    std::error_code linkError;
    if (std::filesystem::is_symlink(candidate, linkError)) {
        std::error_code existsError;
        if (!std::filesystem::exists(candidate, existsError) || existsError) {
            return {};
        }
    }
    std::error_code rootError;
    std::error_code candidateError;
    const auto canonicalRoot =
        std::filesystem::weakly_canonical(root, rootError);
    const auto canonicalCandidate =
        std::filesystem::weakly_canonical(candidate, candidateError);
    const auto canonicalWithin =
        canonicalCandidate.lexically_relative(canonicalRoot);
    if (rootError || candidateError || canonicalWithin.empty() ||
        canonicalWithin == ".." ||
        (canonicalWithin.begin() != canonicalWithin.end() &&
         *canonicalWithin.begin() == "..")) {
        return {};
    }
    return candidate;
}

std::filesystem::path DesignerApp::projectPagePath(
    const dsl::DesignProjectPage& page) const {
    return projectPagePath(page, projectFile_);
}

std::filesystem::path DesignerApp::projectPagePath(
    const dsl::DesignProjectPage& page,
    const std::string& manifest) const {
    return projectRelativePath(page.path, manifest);
}

void DesignerApp::syncActiveProjectDocument() {
    if (activeProjectDocumentId_.empty() || !workbench_.document().has_value() ||
        workbench_.document()->documentId != activeProjectDocumentId_) {
        return;
    }
    projectSessions_.insert_or_assign(activeProjectDocumentId_,
                                      *workbench_.snapshotSession());
}

void DesignerApp::clearProjectSession() {
    if (!project_.has_value()) return;
    project_.reset();
    projectFile_.clear();
    projectRevision_ = 0;
    projectSessions_.clear();
    projectDocumentPaths_.clear();
    projectDocumentRevisions_.clear();
    projectDiagnostics_.clear();
    projectDiagnosticDocumentIds_.clear();
    activeProjectDocumentId_.clear();
    setResourceRoot({});
}

bool DesignerApp::switchProjectDocument(const std::string& documentId) {
    if (!project_.has_value()) return false;
    const auto found = projectSessions_.find(documentId);
    const auto path = projectDocumentPaths_.find(documentId);
    if (found == projectSessions_.end() || path == projectDocumentPaths_.end()) {
        return false;
    }
    if (activeProjectDocumentId_ == documentId && workbench_.document() &&
        workbench_.document()->documentId == documentId) return true;
    syncActiveProjectDocument();
    if (!workbench_.restoreSession(found->second, &runtimeContext_)) return false;
    activeProjectDocumentId_ = documentId;
    saveConflict_.reset();
    sourceFile_ = path->second;
    sourceSnapshot_ = dsl::serializeDesignDocument(found->second.document());
    sourceFocusLine_ = 0;
    resetPreviewState();
    refreshDocumentUi();
    statusMessage_ = "Page  /  " + documentId;
    shell_.markDirty();
    return true;
}

bool DesignerApp::loadProjectFile(const std::string& filename) {
    const auto loaded = projectStore_.load(filename);
    if (!loaded.ok()) {
        projectDiagnostics_ = loaded.diagnostics;
        projectDiagnosticDocumentIds_.assign(projectDiagnostics_.size(), {});
        shell_.markDirty();
        return false;
    }
    const auto previousProject = project_;
    const auto previousProjectFile = projectFile_;
    const auto previousSessions = projectSessions_;
    const auto previousPaths = projectDocumentPaths_;
    const auto previousRevisions = projectDocumentRevisions_;
    const auto previousProjectRevision = projectRevision_;
    const auto previousActive = activeProjectDocumentId_;
    const auto previousSourceFile = sourceFile_;
    const auto previousSourceSnapshot = sourceSnapshot_;
    const auto previousSourceFocusLine = sourceFocusLine_;
    const auto previousResourceRoots = resourcePolicy_.roots();
    const auto restorePreviousProject = [&] {
        project_ = previousProject;
        projectFile_ = previousProjectFile;
        projectRevision_ = previousProjectRevision;
        projectSessions_ = previousSessions;
        projectDocumentPaths_ = previousPaths;
        projectDocumentRevisions_ = previousRevisions;
        // Keep the failed load diagnostics without routing them to old pages.
        projectDiagnosticDocumentIds_.assign(projectDiagnostics_.size(), {});
        activeProjectDocumentId_ = previousActive;
        sourceFile_ = previousSourceFile;
        sourceSnapshot_ = previousSourceSnapshot;
        sourceFocusLine_ = previousSourceFocusLine;
        const auto root = previousResourceRoots.find("project");
        setResourceRoot(root == previousResourceRoots.end()
                            ? std::filesystem::path{}
                            : root->second);
    };
    project_ = loaded.project;
    projectFile_ = filename;
    projectRevision_ = loaded.revision;
    projectDiagnostics_ = loaded.diagnostics;
    projectDiagnosticDocumentIds_.assign(projectDiagnostics_.size(), {});
    projectSessions_.clear();
    projectDocumentPaths_.clear();
    projectDocumentRevisions_.clear();
    // The first switch must not sync the previous workbench into freshly
    // loaded pages when reloading the same project/document ids.
    activeProjectDocumentId_.clear();

    const auto root = projectRootPath();
    if (root.empty()) {
        appendProjectDiagnostic(dsl::DesignError{
            "project.root", filename, {}, "project root is invalid", {}, {}, 0,
            {}, {}});
    } else {
        setResourceRoot(root);
        for (const auto& resource : project_->resources) {
            const auto resourcePath =
                projectRelativePath(resource.path, projectFile_);
            if (resourcePath.empty()) {
                appendProjectDiagnostic(dsl::DesignError{
                    "project.resource_path", filename, {},
                    "resource path must stay inside the project root", {}, {},
                    0, {}, resource.uri});
                continue;
            }
            std::error_code resourceError;
            if (!std::filesystem::exists(resourcePath, resourceError)) {
                appendProjectDiagnostic(dsl::DesignError{
                    "project.resource_missing", resourcePath.string(), {},
                    resourceError ? "unable to inspect project resource"
                                  : "project resource is missing",
                    {}, {}, 0, {}, resource.uri});
                continue;
            }
            if (!std::filesystem::is_regular_file(resourcePath,
                                                   resourceError) ||
                resourceError) {
                appendProjectDiagnostic(dsl::DesignError{
                    "project.resource_type", resourcePath.string(), {},
                    "project resource is not a regular file", {}, {}, 0, {},
                    resource.uri});
                continue;
            }
        }
    }
    for (const auto& page : project_->pages) {
        const auto pagePath = projectPagePath(page);
        if (pagePath.empty()) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.page_path", filename, {},
                "page path must stay inside the project root", {}, {}, 0, {}, {}});
            continue;
        }
        const auto document = projectDocumentStore_.load(pagePath.string());
        for (auto diagnostic : document.diagnostics) {
            appendProjectDiagnostic(std::move(diagnostic), page.documentId);
        }
        if (!document.ok()) continue;
        if (document.document.documentId != page.documentId) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.document_id", pagePath.string(), {},
                "page documentId does not match the project manifest", {}, {}, 0,
                {}, {}},
                page.documentId);
            continue;
        }
        projectSessions_.emplace(page.documentId, dsl::DesignWorkbenchSession{
            document.document, pagePath.string(), document.revision});
        projectDocumentPaths_[page.documentId] = pagePath.string();
        projectDocumentRevisions_[page.documentId] = document.revision;
    }
    if (projectSessions_.empty()) {
        if (projectDiagnostics_.empty()) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.pages_missing", filename, {},
                "project has no loadable pages", {}, {}, 0, {}, {}});
        }
        restorePreviousProject();
        shell_.markDirty();
        return false;
    }
    const auto& first = project_->pages.front().documentId;
    const auto active = projectSessions_.contains(first) ? first
                                                           : projectSessions_.begin()->first;
    if (!switchProjectDocument(active)) {
        restorePreviousProject();
        shell_.markDirty();
        return false;
    }
    shell_.markDirty();
    return true;
}

bool DesignerApp::saveProjectFile(const std::string& filename) {
    const bool saved = saveProjectFileAtConflict(filename, nullptr);
    if (saved) {
        saveConflict_.reset();
    } else if (!projectDiagnostics_.empty() &&
               (projectDiagnostics_.back().code == "project.revision_conflict" ||
                projectDiagnostics_.back().code == "store.revision_conflict")) {
        captureSaveConflict(filename, true);
    }
    shell_.markDirty();
    return saved;
}

bool DesignerApp::saveProjectFileAtConflict(
    const std::string& filename, const SaveConflict* approvedConflict) {
    if (!project_.has_value()) return false;
    syncActiveProjectDocument();
    std::vector<dsl::DesignError> diagnostics;
    const bool samePath = sameProjectPath(projectFile_, filename);
    if (approvedConflict != nullptr &&
        (!samePath || !approvedConflict->project ||
         !approvedConflict->canOverwrite ||
         approvedConflict->filename != filename)) return false;
    const auto targetRoot = projectRootPath(filename);
    if (targetRoot.empty()) {
        appendProjectDiagnostic(dsl::DesignError{
            "project.root", filename, {}, "project root is invalid", {}, {}, 0,
            {}, {}});
        return false;
    }
    {
        std::error_code directoryError;
        std::filesystem::create_directories(targetRoot, directoryError);
        if (directoryError) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.root", filename, {},
                "unable to create project root directory", {}, {}, 0, {}, {}});
            return false;
        }
    }
    dsl::DesignProject projectToSave = *project_;
    std::map<std::string, std::filesystem::path> pagePaths;
    for (const auto& page : projectToSave.pages) {
        const auto document = projectSessions_.find(page.documentId);
        const auto path = projectPagePath(page, filename);
        if (document == projectSessions_.end() || path.empty()) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.page", filename, {}, "project page is not loaded", {}, {},
                0, {}, {}});
            return false;
        }
        pagePaths[page.documentId] = path;
    }
    struct ResourceCopy {
        std::filesystem::path source;
        std::filesystem::path target;
    };
    std::vector<ResourceCopy> resourceCopies;
    for (const auto& resource : projectToSave.resources) {
        const auto source = projectRelativePath(resource.path, projectFile_);
        const auto target = projectRelativePath(resource.path, filename);
        if (source.empty() || target.empty()) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.resource_path", filename, {},
                "resource path must stay inside the project root", {}, {}, 0,
                {}, resource.uri});
            return false;
        }
        std::error_code resourceError;
        const bool exists = std::filesystem::exists(source, resourceError);
        if (resourceError) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.resource_copy", projectFile_, {},
                "unable to inspect project resource", {}, {}, 0, {},
                resource.uri});
            return false;
        }
        if (!exists) continue;
        if (!std::filesystem::is_regular_file(source, resourceError) ||
            resourceError) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.resource_copy", source.string(), {},
                "project resource is not a regular file", {}, {}, 0, {},
                resource.uri});
            return false;
        }
        if (sameProjectPath(source.string(), target.string())) continue;
        resourceCopies.push_back(ResourceCopy{source, target});
    }
    auto nextRevisions = projectDocumentRevisions_;
    const auto expectedProjectRevision = approvedConflict != nullptr
        ? *approvedConflict->revision : projectRevision_;
    if (approvedConflict != nullptr) {
        nextRevisions = approvedConflict->pageRevisions;
    }
    auto nextPaths = projectDocumentPaths_;

    // Check every input revision before writing any page. A later page
    // conflict must not leave earlier pages from the same project partially
    // saved.
    if (samePath) {
        const auto manifestRevision = dsl::DocumentStore::revision(filename);
        if (!manifestRevision.has_value() || *manifestRevision != expectedProjectRevision) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.revision_conflict", filename, {},
                "project manifest changed after it was loaded", {}, {}, 0,
                {}, {}});
            return false;
        }
        for (const auto& page : projectToSave.pages) {
            const auto revision = nextRevisions.find(page.documentId);
            if (revision == nextRevisions.end()) continue;
            const auto current = dsl::DocumentStore::revision(
                pagePaths.at(page.documentId).string());
            if (!current.has_value() || *current != revision->second) {
                appendProjectDiagnostic(dsl::DesignError{
                    "store.revision_conflict",
                    pagePaths.at(page.documentId).string(), {},
                    "project page changed after it was loaded", {}, {}, 0,
                    {}, {}},
                    page.documentId);
                return false;
            }
        }
    }

    for (const auto& resource : resourceCopies) {
        std::error_code directoryError;
        std::filesystem::create_directories(resource.target.parent_path(),
                                            directoryError);
        if (directoryError) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.resource_directory", resource.target.string(), {},
                "unable to create project resource directory", {}, {}, 0,
                {}, {}});
            return false;
        }
        std::error_code copyError;
        if (!std::filesystem::copy_file(
                resource.source, resource.target,
                std::filesystem::copy_options::overwrite_existing,
                copyError) ||
            copyError) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.resource_copy", resource.target.string(), {},
                "unable to copy project resource", {}, {}, 0, {}, {}});
            return false;
        }
    }

    for (auto& page : projectToSave.pages) {
        const auto document = projectSessions_.find(page.documentId);
        const auto path = pagePaths.at(page.documentId);
        const auto revision = nextRevisions.find(page.documentId);
        if (!path.parent_path().empty()) {
            std::error_code directoryError;
            std::filesystem::create_directories(path.parent_path(),
                                                directoryError);
            if (directoryError) {
                appendProjectDiagnostic(dsl::DesignError{
                    "project.page_directory", path.string(), {},
                    "unable to create project page directory", {}, {}, 0,
                    {}, {}},
                    page.documentId);
                return false;
            }
        }
        const auto expectedRevision =
            samePath && revision != nextRevisions.end()
                ? std::optional<std::uint64_t>{revision->second}
                : std::nullopt;
        if (!projectDocumentStore_.save(
                path.string(), document->second.document(), diagnostics,
                expectedRevision)) {
            for (auto diagnostic : diagnostics) {
                appendProjectDiagnostic(std::move(diagnostic), page.documentId);
            }
            return false;
        }
        page.pageName = document->second.document().pageName;
        const auto saved = projectDocumentStore_.load(path.string());
        if (saved.ok()) {
            nextRevisions[page.documentId] = saved.revision;
            nextPaths[page.documentId] = path.string();
        }
    }
    if (!projectStore_.save(filename, projectToSave, diagnostics,
                            samePath
                                ? std::optional<std::uint64_t>{expectedProjectRevision}
                                : std::nullopt)) {
        for (auto diagnostic : diagnostics) {
            appendProjectDiagnostic(std::move(diagnostic));
        }
        return false;
    }
    project_ = std::move(projectToSave);
    projectDocumentPaths_ = std::move(nextPaths);
    projectDocumentRevisions_ = std::move(nextRevisions);
    projectFile_ = filename;
    const auto savedProject = projectStore_.load(filename);
    projectRevision_ = savedProject.revision;
    setResourceRoot(targetRoot);
    for (auto& [id, session] : projectSessions_) {
        session.markSaved(projectDocumentPaths_.at(id),
                          projectDocumentRevisions_.at(id));
    }
    if (!activeProjectDocumentId_.empty()) {
        const auto activePath = projectDocumentPaths_.find(activeProjectDocumentId_);
        if (activePath != projectDocumentPaths_.end()) {
            workbench_.markSaved(activePath->second,
                projectDocumentRevisions_.at(activeProjectDocumentId_));
            sourceFile_ = activePath->second;
            sourceSnapshot_ = dsl::serializeDesignDocument(
                *workbench_.document());
            refreshDocumentUi();
        }
    }
    projectDiagnostics_.clear();
    projectDiagnosticDocumentIds_.clear();
    statusMessage_ = "Project saved  /  " + filename;
    shell_.markDirty();
    return true;
}

bool DesignerApp::createProjectFile(const std::string& filename) {
    if (!workbench_.document().has_value() || filename.empty()) return false;

    const auto manifestPath = std::filesystem::path{filename};
    const auto pagePath = manifestPath.parent_path() / "main.design";
    dsl::DesignProject project;
    project.projectId = manifestPath.stem().string();
    if (project.projectId.empty()) project.projectId = "designer.project";
    project.name = project.projectId;
    project.root = ".";
    project.pages.push_back(dsl::DesignProjectPage{
        workbench_.document()->documentId, "main.design",
        workbench_.document()->pageName});

    std::vector<dsl::DesignError> diagnostics;
    if (!projectDocumentStore_.save(pagePath.string(), *workbench_.document(),
                                    diagnostics)) {
        for (auto diagnostic : diagnostics) {
            appendProjectDiagnostic(std::move(diagnostic),
                                    workbench_.document()->documentId);
        }
        shell_.markDirty();
        return false;
    }
    if (!projectStore_.save(filename, project, diagnostics)) {
        for (auto diagnostic : diagnostics) {
            appendProjectDiagnostic(std::move(diagnostic));
        }
        shell_.markDirty();
        return false;
    }
    return loadProjectFile(filename);
}

bool DesignerApp::loadFile(const std::string& filename) {
    statusMessage_.clear();
    const bool loaded = workbench_.openLumenFile(filename, &runtimeContext_);
    if (loaded) {
        clearProjectSession();
        sourceFile_ = filename;
        saveConflict_.reset();
        sourceSnapshot_ = readSourceFile(filename).value_or(
            workbench_.document().has_value()
                ? dsl::serializeDesignDocument(*workbench_.document())
                : std::string{});
        sourceFocusLine_ = 0;
        resetPreviewState();
        refreshDocumentUi();
    }
    shell_.markDirty();
    return loaded;
}

bool DesignerApp::loadSource(const std::string& source, std::string filename) {
    statusMessage_.clear();
    const bool loaded =
        workbench_.openLumenSource(source, filename, &runtimeContext_);
    if (loaded) {
        clearProjectSession();
        sourceFile_ = filename.empty() ? "<memory>" : filename;
        saveConflict_.reset();
        sourceSnapshot_ = source;
        sourceFocusLine_ = 0;
        resetPreviewState();
        refreshDocumentUi();
    }
    shell_.markDirty();
    return loaded;
}

std::string DesignerApp::currentSourceText() const {
    if (workbench_.dirty() && workbench_.document().has_value()) {
        return dsl::serializeDesignDocument(*workbench_.document());
    }
    return sourceSnapshot_;
}

void DesignerApp::rebuildOutline() {
    outlineModel_.setRoot(workbench_.outline());
    outlineController_.modelChanged();
    if (const auto root = workbench_.outline(); root.has_value()) {
        const auto& selection = workbench_.selection();
        std::vector<std::string> selectedPaths;
        std::optional<std::string> selectedPath;
        std::function<void(const dsl::DesignPreviewOutlineNode&)> findPath =
            [&](const dsl::DesignPreviewOutlineNode& item) {
                if (selection.ids.contains(item.id)) {
                    selectedPaths.push_back(item.path);
                }
                if (selection.primary.has_value() &&
                    item.id == *selection.primary) {
                    selectedPath = item.path;
                }
                for (const auto& child : item.children) findPath(child);
            };
        findPath(*root);
        if (!selectedPath.has_value()) {
            selectedPath = root->path;
            (void)workbench_.selectNode(root->id);
            selectedPaths = {root->path};
        } else if (selectedPaths.empty()) {
            selectedPaths = {*selectedPath};
        }
        outlineController_.collapseAll();
        (void)outlineController_.expandAll();
        outlineController_.selection().setCurrent(*selectedPath);
        outlineController_.selection().setSelected(std::move(selectedPaths));
    } else {
        outlineController_.selection().clear();
    }
}

void DesignerApp::rebuildReferences() {
    referenceRows_.clear();
    const auto& document = workbench_.document();
    if (!document.has_value()) {
        referencesController_.setRowCount(0);
        return;
    }

    const auto statusFor = [this](dsl::DesignNodeId id,
                                  const std::string& property) {
        for (const auto& diagnostic : diagnostics_) {
            if (workbench_.document().has_value() &&
                !diagnostic.documentId.empty() &&
                diagnostic.documentId != workbench_.document()->documentId) {
                continue;
            }
            if (diagnostic.nodeId == id && diagnostic.property == property &&
                (diagnostic.code.starts_with("reference.") ||
                 diagnostic.code == "compile.reference_kind")) {
                return std::string{"Missing"};
            }
        }
        return std::string{"Stub"};
    };
    std::function<void(const dsl::DesignNode&)> visit =
        [&](const dsl::DesignNode& node) {
            for (const auto& [property, value] : node.references) {
                ReferenceEntry row;
                row.nodeId = node.id;
                row.property = property;
                row.value = value;
                if (property == "bind") {
                    row.kind = "Binding";
                } else if (property == "onClick") {
                    row.kind = "Handler";
                } else {
                    row.kind = "Reference";
                }
                row.status = statusFor(node.id, property);
                std::string nodeKey;
                const auto keyIt = node.properties.find("key");
                if (keyIt != node.properties.end()) {
                    if (const auto* value =
                            std::get_if<std::string>(&keyIt->second.value)) {
                        nodeKey = *value;
                    }
                }
                row.location = nodeKey.empty()
                                   ? node.type
                                   : node.type + "  [" + nodeKey + "]";
                row.key = "designer-reference:" + std::to_string(node.id) +
                          ":" + property;
                referenceRows_.push_back(std::move(row));
            }
            for (const auto& child : node.children) visit(child);
            for (const auto& [slot, children] : node.slots) {
                (void)slot;
                for (const auto& child : children) visit(child);
            }
        };
    visit(document->root);
    referencesController_.setRowCount(referenceRows_.size());
}

void DesignerApp::selectReference(const std::string& key) {
    const auto row = std::find_if(
        referenceRows_.begin(), referenceRows_.end(),
        [&key](const ReferenceEntry& item) { return item.key == key; });
    if (row != referenceRows_.end()) selectNode(row->nodeId);
}

void DesignerApp::selectNode(dsl::DesignNodeId id) {
    if (!workbench_.selectNode(id)) return;
    applyPreviewState();
    if (const auto outline = workbench_.outline(); outline.has_value()) {
        if (const auto path = outlinePathForNode(*outline, id); path.has_value()) {
            outlineController_.selection().setCurrent(*path);
            outlineController_.selection().setSelected({*path});
        }
    }
    shell_.markDirty();
}

std::optional<dsl::DesignNodeId> DesignerApp::diagnosticTarget(
    std::size_t index) const {
    const auto& diagnostics = diagnostics_;
    if (index >= diagnostics.size() || !workbench_.document().has_value()) {
        return std::nullopt;
    }
    const auto& diagnostic = diagnostics[index];
    const auto& document = *workbench_.document();
    if (diagnostic.nodeId == 0 ||
        (!diagnostic.documentId.empty() &&
         diagnostic.documentId != document.documentId) ||
        findDesignNode(document.root, diagnostic.nodeId) == nullptr) {
        return std::nullopt;
    }
    return diagnostic.nodeId;
}

void DesignerApp::activateDiagnostic(std::size_t index) {
    if (index >= diagnostics_.size()) return;
    const auto diagnostic = diagnostics_[index];
    bool switchedDocument = false;
    if (project_.has_value() && !diagnostic.documentId.empty() &&
        diagnostic.documentId != activeProjectDocumentId_ &&
        projectSessions_.contains(diagnostic.documentId)) {
        if (!switchProjectDocument(diagnostic.documentId)) return;
        switchedDocument = true;
    }
    if (switchedDocument) {
        if (workbench_.document().has_value() && diagnostic.nodeId != 0 &&
            workbench_.document()->documentId == diagnostic.documentId &&
            findDesignNode(workbench_.document()->root, diagnostic.nodeId) !=
                nullptr) {
            selectNode(diagnostic.nodeId);
        }
    } else if (const auto id = diagnosticTarget(index); id.has_value()) {
        selectNode(*id);
    }
    if (diagnostic.sourceSpan.has_value()) {
        centerTab_ = CenterTab::Source;
        centerView_ = CenterView::Source;
        sourceFocusLine_ = diagnostic.sourceSpan->begin.line;
        shell_.markDirty();
    }
}

bool DesignerApp::diagnosticActionable(std::size_t index) const {
    if (index >= diagnostics_.size()) return false;
    const auto& diagnostic = diagnostics_[index];
    return diagnosticTarget(index).has_value() ||
           (project_.has_value() && !diagnostic.documentId.empty() &&
            diagnostic.documentId != activeProjectDocumentId_ &&
            projectSessions_.contains(diagnostic.documentId));
}

void DesignerApp::registerSelectionHandlers(const dsl::DesignNode& node) {
    const std::string handler = "designer:select:" + std::to_string(node.id);
    shell_.handlers()[handler] = [this, id = node.id] {
        selectNode(id);
    };
    for (const auto& child : node.children) registerSelectionHandlers(child);
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) registerSelectionHandlers(child);
    }
}

std::string DesignerApp::imageUriForSource(std::string_view source) {
    if (source.empty()) return {};
    std::string uri = source.find("://") == std::string_view::npos
                          ? "project://" + std::string{source}
                          : std::string{source};
    const auto separator = uri.find("://");
    if (separator == std::string::npos) return uri;
    std::string scheme = uri.substr(0, separator);
    for (char& character : scheme) {
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    }
    const auto path = std::filesystem::path(uri.substr(separator + 3))
                          .lexically_normal()
                          .generic_string();
    return scheme + "://" + path;
}

void DesignerApp::collectImageResources(
    const dsl::DesignNode& node, const std::string& path,
    std::set<std::string>& activeUris) {
    const auto property = node.properties.find("imageSource");
    if (property != node.properties.end()) {
        if (const auto* source = std::get_if<std::string>(&property->second.value);
            source != nullptr && !source->empty()) {
            const auto uri = imageUriForSource(*source);
            std::vector<dsl::DesignDiagnostic> diagnostics;
            const auto reference = resourceAuthorizer_.authorize(
                dsl::DesignResourceKind::Image, uri,
                dsl::DesignResourceDiagnosticContext{
                    sourceFile_,
                    workbench_.document().has_value()
                        ? workbench_.document()->documentId
                        : std::string{},
                    node.id, path, "imageSource"},
                diagnostics);
            for (auto& diagnostic : diagnostics) {
                dsl::appendDesignDiagnostic(diagnostics_, std::move(diagnostic));
            }
            if (reference.has_value()) {
                activeUris.insert(reference->uri());
                imageResourceAliases_[uri] = reference->uri();
                if (!imageResources_.contains(reference->uri())) {
                    const auto root = resourcePolicy_.roots().find(reference->scheme);
                    if (root != resourcePolicy_.roots().end()) {
                        const auto filename =
                            (root->second / reference->relativePath).string();
                        const auto handle = resourceManager_->requestImage(filename);
                        imageResources_.emplace(reference->uri(),
                            ImageResource{handle, imageToken_});
                        // Closing a compilation cancels pending work without
                        // capturing the application or its borrowed context.
                        workbench_.frame().session()->onClose(
                            [manager = std::weak_ptr{resourceManager_}, handle] {
                                if (const auto resources = manager.lock();
                                    resources && resources->state(handle) ==
                                        render::ResourceState::Loading) {
                                    resources->release(handle);
                                }
                            });
                    }
                }
                const auto resource = imageResources_.find(reference->uri());
                if (resource != imageResources_.end() &&
                    resourceManager_->state(resource->second.handle) ==
                        render::ResourceState::Failed) {
                    auto diagnostic = dsl::DesignDiagnostic::fromError(
                        dsl::DesignError{"resource.load_failed", sourceFile_, {},
                            "image could not be read or decoded", {}, {}, node.id,
                            path, "imageSource"},
                        dsl::DesignDiagnosticStage::Reference);
                    diagnostic.documentId = workbench_.document()->documentId;
                    diagnostic.sourceSpan =
                        workbench_.frame().sourceMap().propertySpan(node.id, "imageSource");
                    diagnostic.recoverability =
                        dsl::DesignDiagnosticRecoverability::Placeholder;
                    dsl::appendDesignDiagnostic(diagnostics_, std::move(diagnostic));
                }
            }
        }
    }
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        collectImageResources(
            node.children[index],
            path + ".children[" + std::to_string(index) + "]", activeUris);
    }
    for (const auto& [slot, children] : node.slots) {
        for (std::size_t index = 0; index < children.size(); ++index) {
            collectImageResources(
                children[index],
                path + ".slots[" + slot + "][" + std::to_string(index) + "]",
                activeUris);
        }
    }
}

void DesignerApp::clearImageResources() {
    for (const auto& [uri, resource] : imageResources_) {
        (void)uri;
        resourceManager_->release(resource.handle);
    }
    imageResources_.clear();
    imageResourceAliases_.clear();
    if (imageGeneration_) imageGeneration_->close();
    imageGeneration_.reset();
    imageToken_ = {};
    imageFrameGeneration_ = 0;
}

void DesignerApp::syncImageResources() {
    diagnostics_ = workbench_.diagnostics();
    std::set<std::string> activeUris;
    const auto& frame = workbench_.frame();
    const auto& session = frame.session();
    const bool active = workbench_.document().has_value() && frame.hasFrame() &&
                        frame.documentId() == workbench_.document()->documentId &&
                        session && session->active();
    if (!active) {
        clearImageResources();
    } else if (!imageGeneration_ || imageFrameGeneration_ != frame.generation()) {
        if (imageGeneration_) imageGeneration_->close();
        imageGeneration_.emplace(frame.documentId(), session->generation());
        imageToken_ = imageGeneration_->beginCompile();
        imageFrameGeneration_ = frame.generation();
        // Ready immutable pixels can be adopted by the new compilation; only
        // a matching URI under the current authorization root will use them.
        for (auto it = imageResources_.begin(); it != imageResources_.end();) {
            if (resourceManager_->ready(it->second.handle)) {
                it->second.token = imageToken_;
                ++it;
            } else {
                resourceManager_->release(it->second.handle);
                it = imageResources_.erase(it);
            }
        }
    }
    imageResourceAliases_.clear();
    if (active) {
        collectImageResources(workbench_.document()->root, "root", activeUris);
    }
    for (std::size_t index = 0; index < projectDiagnostics_.size(); ++index) {
        auto diagnostic = dsl::DesignDiagnostic::fromError(
            projectDiagnostics_[index]);
        if (index < projectDiagnosticDocumentIds_.size() &&
            diagnostic.documentId.empty()) {
            diagnostic.documentId = projectDiagnosticDocumentIds_[index];
        }
        dsl::appendDesignDiagnostic(diagnostics_, std::move(diagnostic));
    }
    for (auto it = imageResources_.begin(); it != imageResources_.end();) {
        if (activeUris.contains(it->first)) {
            ++it;
            continue;
        }
        resourceManager_->release(it->second.handle);
        it = imageResources_.erase(it);
    }
}

void DesignerApp::applyImageResources(core::Widget& widget) const {
    if (widget.type == core::WidgetType::Image &&
        !widget.imageSource.empty()) {
        const auto uri = imageUriForSource(widget.imageSource);
        const auto alias = imageResourceAliases_.find(uri);
        const auto found = alias == imageResourceAliases_.end()
            ? imageResources_.end() : imageResources_.find(alias->second);
        const auto& session = workbench_.frame().session();
        widget.imageId = found != imageResources_.end() && imageGeneration_ &&
                        session && imageGeneration_->accepts(found->second.token, *session) &&
                        resourceManager_->ready(found->second.handle)
            ? resourceManager_->imageId(found->second.handle) : 0;
    }
    for (auto& child : widget.children) applyImageResources(child);
}

void DesignerApp::refreshDocumentUi() {
    if (workbench_.document().has_value()) {
        (void)workbench_.refresh(&runtimeContext_);
    }
    rebuildPreviewKeys();
    syncImageResources();
    resetPreviewState();
    clearPropertyObservers();
    rebuildOutline();
    rebuildReferences();
    if (workbench_.document().has_value()) {
        for (auto it = shell_.handlers().begin();
             it != shell_.handlers().end();) {
            if (it->first.starts_with("designer:select:")) {
                it = shell_.handlers().erase(it);
            } else {
                ++it;
            }
        }
        registerSelectionHandlers(workbench_.document()->root);
    }
    previewShell_.markDirty();
}

void DesignerApp::rebuildPreviewKeys() {
    previewKeys_.clear();
    if (!workbench_.document().has_value()) return;
    std::map<std::string, std::size_t> counts;
    std::set<std::string> declaredKeys;
    std::set<std::string> usedKeys;
    collectPreviewKeyCounts(workbench_.document()->root, counts);
    for (const auto& [key, count] : counts) {
        (void)count;
        declaredKeys.insert(key);
    }
    assignPreviewKeys(workbench_.document()->root, counts, declaredKeys,
                      usedKeys, previewKeys_);
}

std::string DesignerApp::previewKeyForNodeId(dsl::DesignNodeId id) const {
    const auto found = previewKeys_.find(id);
    return found == previewKeys_.end() ? previewKeyForNode(id) : found->second;
}

std::string DesignerApp::previewKeyForDesignNode(
    const dsl::DesignNode& node) const {
    return previewKeyForNodeId(node.id);
}

void DesignerApp::insertNodeType(std::string type) {
    // 插入目标（workspace-design §4.1）：所选容器末尾；所选是叶子
    //（schema maxChildren == 0，含组件）时逐级回退到最近的可插祖先、
    // 紧随所选之后，并保持其槽位归属。容器已满（单子 Container、双格
    // Splitter）或无处可插时给状态行反馈——此前叶子选中时静默失败，
    // 仅落一条 schema 诊断，工具箱拖放表现为"拖不进去"。
    if (!workbench_.document().has_value()) return;
    auto parentId = workbench_.selection().primary;
    if (!parentId.has_value()) return;
    const auto* parent =
        findDesignNode(workbench_.document()->root, *parentId);
    if (parent == nullptr) return;
    std::size_t index = parent->children.size();
    std::string slot;
    const auto capacity = [](const std::string& nodeType) {
        const auto* schema = dsl::findNodeSchema(nodeType);
        return schema != nullptr ? schema->maxChildren
                                 : std::optional<std::size_t>{};
    };
    while (true) {
        const auto limit = capacity(parent->type);
        if (limit.has_value() && *limit == 0) {
            // 叶子/组件：回退到其父容器，插在所选之后（保持槽位）。
            const auto location =
                locateDesignNode(workbench_.document()->root, *parentId);
            const auto* grand =
                location.has_value()
                    ? findDesignNode(workbench_.document()->root,
                                     location->parent)
                    : nullptr;
            if (grand == nullptr) {
                statusMessage_ = "无法插入到 " + parent->type;
                shell_.markDirty();
                return;
            }
            parent = grand;
            parentId = grand->id;
            index = location->index + 1;
            slot = location->slot;
            continue;
        }
        if (limit.has_value() && parent->children.size() >= *limit) {
            statusMessage_ = parent->type + " 已满，无法插入";
            shell_.markDirty();
            return;
        }
        break;
    }
    dsl::DesignNode node;
    node.type = std::move(type);
    if (node.type == "Text") {
        node.properties["text"] = dsl::DesignValue{
            dsl::DesignValue::Variant{std::string{"New text"}}};
    } else if (node.type == "Button") {
        node.properties["text"] = dsl::DesignValue{
            dsl::DesignValue::Variant{std::string{"Button"}}};
    }
    if (const auto* schema = dsl::findNodeSchema(node.type);
        schema != nullptr && schema->minChildren != 0) {
        // Splitter is the only current non-component schema with a required
        // child count. Seed valid structural children so insertion is atomic.
        while (node.children.size() < static_cast<std::size_t>(
                                          schema->minChildren)) {
            dsl::DesignNode child;
            child.type = "Container";
            node.children.push_back(std::move(child));
        }
    }
    if (workbench_.insertNode(*parentId, index, std::move(node), slot)) {
        refreshDocumentUi();
        shell_.markDirty();
    } else {
        statusMessage_ = "插入 " + node.type + " 失败";
        shell_.markDirty();
    }
}

void DesignerApp::insertTextNode() { insertNodeType("Text"); }

void DesignerApp::duplicateSelectedNode() {
    if (!workbench_.document().has_value()) return;
    const auto& selection = workbench_.selection();
    if (selection.ids.empty()) return;

    std::vector<dsl::DesignNode> copies;
    collectClipboardNodes(workbench_.document()->root, selection.ids, false,
                          copies);
    if (copies.empty()) return;

    const auto firstLocation =
        locateDesignNode(workbench_.document()->root, copies.front().id);
    if (!firstLocation.has_value()) return;
    const auto parentId = firstLocation->parent;
    const auto slot = firstLocation->slot;
    std::size_t insertionIndex = firstLocation->index + 1;
    for (const auto& copy : copies) {
        const auto location =
            locateDesignNode(workbench_.document()->root, copy.id);
        if (!location.has_value() || location->parent != parentId ||
            location->slot != slot) {
            statusMessage_ = "Duplicate requires one sibling list";
            shell_.markDirty();
            return;
        }
        insertionIndex = std::max(insertionIndex, location->index + 1);
    }

    std::set<std::string> usedKeys;
    collectDesignKeys(workbench_.document()->root, usedKeys);
    for (auto& copy : copies) makePastedKeysUnique(copy, usedKeys);
    const auto inserted = workbench_.insertNodes(
        parentId, insertionIndex, std::move(copies), slot);
    if (inserted.empty()) return;
    if (inserted.size() == 1) {
        statusMessage_ = "Duplicated node";
    } else {
        statusMessage_ =
            "Duplicated " + std::to_string(inserted.size()) + " nodes";
    }
    refreshDocumentUi();
    shell_.markDirty();
}

void DesignerApp::copySelectedNode() {
    if (!workbench_.document().has_value()) return;
    const auto& selection = workbench_.selection();
    if (selection.ids.empty()) return;
    std::vector<dsl::DesignNode> copied;
    collectClipboardNodes(workbench_.document()->root, selection.ids, false,
                          copied);
    if (copied.empty()) return;
    clipboardNodes_ = std::move(copied);
    if (clipboardNodes_.size() == 1) {
        statusMessage_ = "Copied " + clipboardNodes_.front().type;
    } else {
        statusMessage_ = "Copied " + std::to_string(clipboardNodes_.size()) +
                         " nodes";
    }
    shell_.markDirty();
}

void DesignerApp::pasteCopiedNode() {
    if (clipboardNodes_.empty() || !workbench_.document().has_value()) {
        return;
    }
    const auto selected = workbench_.selection().primary;
    if (!selected) return;

    dsl::DesignNodeId parentId = workbench_.document()->root.id;
    std::size_t index = workbench_.document()->root.children.size();
    std::string slot;
    if (*selected != parentId) {
        const auto location = locateDesignNode(workbench_.document()->root,
                                               *selected);
        if (!location.has_value()) return;
        parentId = location->parent;
        index = location->index + 1;
        slot = location->slot;
    }

    std::vector<dsl::DesignNode> pasted;
    pasted.reserve(clipboardNodes_.size());
    std::set<std::string> usedKeys;
    collectDesignKeys(workbench_.document()->root, usedKeys);
    for (auto node : clipboardNodes_) {
        makePastedKeysUnique(node, usedKeys);
        pasted.push_back(std::move(node));
    }
    const auto pastedIds = workbench_.insertNodes(
        parentId, index, std::move(pasted), std::move(slot));
    if (pastedIds.empty()) return;
    if (pastedIds.size() == 1) {
        statusMessage_ = "Pasted " + clipboardNodes_.front().type;
    } else {
        statusMessage_ = "Pasted " + std::to_string(pastedIds.size()) +
                         " nodes";
    }
    refreshDocumentUi();
    shell_.markDirty();
}

void DesignerApp::removeSelectedNode() {
    const auto& selected = workbench_.selection().ids;
    if (selected.empty()) return;
    const auto selectedCount = selected.size();
    if (!workbench_.removeNodes(
            std::vector<dsl::DesignNodeId>(selected.begin(), selected.end()))) {
        return;
    }
    if (selectedCount > 1) {
        statusMessage_ = "Removed " + std::to_string(selectedCount) +
                         " nodes";
    }
    refreshDocumentUi();
    shell_.markDirty();
}

void DesignerApp::moveSelectedNode(int offset) {
    const auto& selected = workbench_.selection().ids;
    if (selected.empty() ||
        !workbench_.moveNodesRelative(
            std::vector<dsl::DesignNodeId>(selected.begin(), selected.end()),
            offset)) {
        return;
    }
    if (selected.size() > 1) {
        statusMessage_ = "Moved " + std::to_string(selected.size()) + " nodes";
    }
    refreshDocumentUi();
    shell_.markDirty();
}

void DesignerApp::outlineDragSession(core::DragPhase phase,
                                     core::Offset position,
                                     const std::string& sourceKey) {
    constexpr std::string_view kSourcePrefix = "designer-outline:item:";
    if (sourceKey.rfind(kSourcePrefix, 0) != 0) return;
    const auto sourceId = outlineModel_.idForKey(
        sourceKey.substr(kSourcePrefix.size()));
    if (!sourceId.has_value() || !workbench_.document().has_value()) {
        if (phase == core::DragPhase::Cancel || phase == core::DragPhase::Drop) {
            endOutlineDragSession();
        }
        return;
    }

    if (phase == core::DragPhase::Cancel) {
        endOutlineDragSession();
        return;
    }
    if (phase == core::DragPhase::Start) {
        const auto location =
            locateDesignNode(workbench_.document()->root, *sourceId);
        if (!location.has_value()) return;
        outlineDragActive_ = true;
        outlineDragId_ = *sourceId;
        outlineDragParentId_ = location->parent;
        outlineDragIndex_ = location->index;
        outlineDragInsertIndex_ = location->index;
        outlineDragSlot_ = location->slot;
        outlineDragPointer_ = position;
        shell_.setVisualOverlayBuilder([this]()
                                            -> std::optional<core::Widget> {
            if (!outlineDragActive_) return std::nullopt;
            return buildOutlineDragOverlay();
        });
        return;
    }
    if (!outlineDragActive_) return;
    outlineDragPointer_ = position;

    if (phase == core::DragPhase::Move) {
        std::vector<const core::RenderNode*> chain;
        (void)core::hitTestChain(shell_.root(), position, chain);
        constexpr std::string_view kRowPrefix = "tree:designer-outline:";
        for (const auto* node : chain) {
            if (node == nullptr || node->onClick.rfind(kRowPrefix, 0) != 0) {
                continue;
            }
            const auto targetId = outlineModel_.idForKey(
                node->onClick.substr(kRowPrefix.size()));
            if (!targetId.has_value()) break;
            const auto target =
                locateDesignNode(workbench_.document()->root, *targetId);
            if (!target.has_value() ||
                target->parent != outlineDragParentId_ ||
                target->slot != outlineDragSlot_) {
                break;
            }
            if (*targetId == outlineDragId_) {
                outlineDragInsertIndex_ = outlineDragIndex_;
                break;
            }
            const core::Offset origin =
                core::absoluteOffset(shell_.root(), node->key);
            const bool belowMiddle =
                position.y > origin.y + node->size.height * 0.5F;
            outlineDragInsertIndex_ =
                target->index + (belowMiddle ? std::size_t{1} : 0U);
            break;
        }
        shell_.markDirty();
        return;
    }
    if (phase != core::DragPhase::Drop) return;

    const auto id = outlineDragId_;
    const auto parent = outlineDragParentId_;
    const auto index = outlineDragInsertIndex_;
    const auto sourceIndex = outlineDragIndex_;
    const auto slot = outlineDragSlot_;
    endOutlineDragSession();
    if (index == sourceIndex ||
        !workbench_.moveNode(id, parent, index, slot)) {
        return;
    }
    (void)workbench_.selectNode(id);
    refreshDocumentUi();
    shell_.markDirty();
}

void DesignerApp::endOutlineDragSession() {
    outlineDragActive_ = false;
    outlineDragId_ = 0;
    outlineDragParentId_ = 0;
    outlineDragIndex_ = 0;
    outlineDragInsertIndex_ = 0;
    outlineDragSlot_.clear();
    outlineDragPointer_ = {};
    shell_.clearVisualOverlay();
}

void DesignerApp::canvasSelectionSession(core::DragPhase phase,
                                         core::Offset position,
                                         const std::string& sourceKey) {
    if (sourceKey != "designer-canvas-selection") return;
    if (phase == core::DragPhase::Cancel) {
        endCanvasSelectionSession();
        shell_.markDirty();
        return;
    }
    if (phase == core::DragPhase::Start) {
        if (!workbench_.document().has_value()) return;
        canvasSelectionActive_ = true;
        canvasSelectionStart_ = shell_.controller().dragAnchor();
        canvasSelectionPointer_ = position;
        shell_.setVisualOverlayBuilder([this]()
                                            -> std::optional<core::Widget> {
            if (!canvasSelectionActive_) return std::nullopt;
            return buildCanvasSelectionOverlay();
        });
        return;
    }
    if (!canvasSelectionActive_) return;
    canvasSelectionPointer_ = position;
    if (phase == core::DragPhase::Move) {
        shell_.markDirty();
        return;
    }
    if (phase != core::DragPhase::Drop) return;
    const auto start = canvasSelectionStart_;
    const auto end = canvasSelectionPointer_;
    endCanvasSelectionSession();
    selectCanvasNodesInRect(core::Rect::fromXYWH(
        std::min(start.x, end.x), std::min(start.y, end.y),
        std::abs(end.x - start.x), std::abs(end.y - start.y)));
}

void DesignerApp::endCanvasSelectionSession() {
    canvasSelectionActive_ = false;
    canvasSelectionStart_ = {};
    canvasSelectionPointer_ = {};
    shell_.clearVisualOverlay();
}

void DesignerApp::selectCanvasNodesInRect(core::Rect selection) {
    if (!workbench_.document().has_value()) return;
    const auto& root = workbench_.document()->root;
    std::vector<dsl::DesignNodeId> selected;
    const auto consider = [this, &selection, &selected](
                              const dsl::DesignNode& node) {
        const auto key = previewKeyForDesignNode(node);
        const auto* render = core::findNodeByKey(shell_.root(), key);
        if (render == nullptr) return;
        const auto origin = core::absoluteOffset(shell_.root(), key);
        if (selection.intersects(core::Rect{origin, render->size})) {
            selected.push_back(node.id);
        }
    };
    for (const auto& child : root.children) consider(child);
    for (const auto& [slot, children] : root.slots) {
        (void)slot;
        for (const auto& child : children) consider(child);
    }
    if (selected.empty()) {
        statusMessage_ = "Cleared selection";
        workbench_.clearSelection();
        outlineController_.selection().clear();
        resetPreviewState();
        shell_.markDirty();
        return;
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
        (void)workbench_.selectNode(
            selected[index], index == 0 ? dsl::DesignSelectionMode::Replace
                                        : dsl::DesignSelectionMode::Add);
    }
    statusMessage_ = "Selected " + std::to_string(selected.size()) +
                     (selected.size() == 1 ? " node" : " nodes");
    if (const auto outline = workbench_.outline(); outline.has_value()) {
        std::vector<std::string> paths;
        paths.reserve(selected.size());
        for (const auto id : selected) {
            if (const auto path = outlinePathForNode(*outline, id);
                path.has_value()) {
                paths.push_back(*path);
            }
        }
        outlineController_.selection().setSelected(std::move(paths));
        if (const auto primary = workbench_.selection().primary;
            primary.has_value()) {
            if (const auto path = outlinePathForNode(*outline, *primary);
                path.has_value()) {
                outlineController_.selection().setCurrent(*path);
            }
        }
    }
    applyPreviewState();
    shell_.markDirty();
}

core::Widget DesignerApp::buildCanvasSelectionOverlay() const {
    const auto& theme = shell_.theme();
    const auto& tokens = theme.designerCanvas;
    const float left = std::min(canvasSelectionStart_.x,
                                canvasSelectionPointer_.x);
    const float top = std::min(canvasSelectionStart_.y,
                               canvasSelectionPointer_.y);
    const float width = std::max(1.0F, std::abs(canvasSelectionPointer_.x -
                                               canvasSelectionStart_.x));
    const float height = std::max(1.0F, std::abs(canvasSelectionPointer_.y -
                                                canvasSelectionStart_.y));
    auto fill = tokens.guide;
    fill.a = 40;
    auto rectangle = core::makeContainerLeaf(
        width, height, {}, {}, fill, "designer-canvas-selection-rect");
    rectangle.styleOverrides.border = tokens.guide;
    rectangle.styleOverrides.borderWidth = tokens.guideThickness;
    rectangle.excludeFromSemantics = true;
    rectangle.excludeFromFocus = true;
    rectangle = core::withStackPosition(std::move(rectangle),
                                        core::Offset{left, top});
    auto overlay = core::makeStack({std::move(rectangle)},
                                   core::StackAlignment::TopLeft);
    overlay.key = "designer-canvas-selection-overlay";
    overlay.width = shell_.view().width;
    overlay.height = shell_.view().height;
    return overlay;
}

float DesignerApp::snapCanvasCoordinate(float value, float maximum,
                                        float threshold) {
    if (!std::isfinite(value) || maximum <= 0.0F) return value;
    float best = value;
    float distance = threshold;
    const float grid = std::round(value / 8.0F) * 8.0F;
    const float candidates[] = {0.0F, maximum, maximum * 0.5F, grid};
    for (const float candidate : candidates) {
        if (candidate < 0.0F || candidate > maximum) continue;
        const float candidateDistance = std::abs(value - candidate);
        if (candidateDistance <= distance) {
            distance = candidateDistance;
            best = candidate;
        }
    }
    return best;
}

float DesignerApp::snapDragCoordinate(
    float value, const std::vector<float>& snaps,
    const std::vector<float>& guides, float gridMaximum, float threshold,
    std::optional<float>& activeGuide) const {
    activeGuide.reset();
    float best = value;
    float bestDistance = threshold;
    for (std::size_t index = 0; index < snaps.size() && index < guides.size();
         ++index) {
        const float distance = std::abs(snaps[index] - value);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = snaps[index];
            activeGuide = guides[index];
        }
    }
    if (!activeGuide.has_value()) {
        // 未命中对齐候选：回落既有网格吸附（§8.3 之前的行为）。
        return snapCanvasCoordinate(value, gridMaximum, threshold);
    }
    return best;
}

void DesignerApp::canvasResizeSession(core::DragPhase phase,
                                      core::Offset position,
                                      const std::string& sourceKey) {
    constexpr std::string_view kSourcePrefix = "designer-canvas-handle:";
    if (sourceKey.rfind(kSourcePrefix, 0) != 0) return;
    const std::string handle = sourceKey.substr(kSourcePrefix.size());
    if (phase == core::DragPhase::Cancel) {
        endCanvasResizeSession();
        shell_.markDirty();
        return;
    }
    if (phase == core::DragPhase::Start) {
        // drag:<id> = 拖动换位伪手柄（arm 自画布 select 目标认领）：目标
        // 是被拖动的节点本身，而非当前选中。
        std::optional<dsl::DesignNodeId> dragTarget;
        if (handle.rfind("drag:", 0) == 0) {
            const std::string_view text = handle;
            std::uint64_t id = 0;
            const auto parsed = std::from_chars(
                text.data() + 5, text.data() + text.size(), id);
            if (parsed.ec == std::errc{} &&
                parsed.ptr == text.data() + text.size()) {
                dragTarget = static_cast<dsl::DesignNodeId>(id);
            }
        }
        const auto selected = dragTarget.has_value()
                                  ? dragTarget
                                  : workbench_.selection().primary;
        if (!selected.has_value() || !workbench_.document().has_value()) {
            return;
        }
        const auto outline = workbench_.outline();
        if (!outline.has_value()) return;
        const auto* outlineNode = findOutlineNode(*outline, *selected);
        if (outlineNode == nullptr) return;
        const std::string key = previewKeyForNodeId(*selected);
        const auto* renderNode = core::findNodeByKey(shell_.root(), key);
        const auto* canvasNode =
            core::findNodeByKey(shell_.root(), "designer-canvas");
        if (renderNode == nullptr || canvasNode == nullptr) return;
        const core::Offset canvasOrigin =
            core::absoluteOffset(shell_.root(), "designer-canvas");
        const core::Offset contentOrigin =
            canvasOrigin +
            core::Offset{canvasNode->padding.left, canvasNode->padding.top};
        const core::Offset renderOrigin =
            core::absoluteOffset(shell_.root(), key);
        const auto location =
            locateDesignNode(workbench_.document()->root, *selected);
        const auto* parent = location.has_value()
                                 ? findDesignNode(workbench_.document()->root,
                                                  location->parent)
                                 : nullptr;
        const auto parentKey = parent == nullptr ? std::string{}
                                                 : previewKeyForNodeId(parent->id);
        const auto* parentRender = parent == nullptr ? nullptr
            : core::findNodeByKey(shell_.root(), parentKey);
        canvasResizePositionEditable_ =
            parent != nullptr && parent->type == "Stack" && parentRender != nullptr;
        // 拖动换位只对拥有 left/top 的 Stack 子节点有效；流式父级的
        // 位置归布局，起会话即放弃。
        if (dragTarget.has_value() && !canvasResizePositionEditable_) {
            return;
        }
        canvasResizeZoom_ = canvasTransform_.zoom();
        // G-D14 §4.15: resize in the parent's design coordinates, then
        // convert the temporary guide back to canvas logical coordinates.
        auto referenceOrigin = renderOrigin;
        auto referenceSize = core::Size{
            canvasNode->size.width - canvasNode->padding.horizontal(),
            canvasNode->size.height - canvasNode->padding.vertical()};
        if (canvasResizePositionEditable_) {
            float margin = 0.0F;
            if (const auto* declaration =
                    findDesignNode(workbench_.document()->root, *selected);
                declaration != nullptr) {
                const auto value = declaration->properties.find("margin");
                if (value != declaration->properties.end()) {
                    if (const auto* number = std::get_if<double>(&value->second.value)) {
                        margin = static_cast<float>(*number) * canvasResizeZoom_;
                    }
                }
            }
            referenceOrigin = core::absoluteOffset(shell_.root(), parentKey) +
                core::Offset{parentRender->padding.left + margin,
                             parentRender->padding.top + margin};
            referenceSize = core::Size{
                parentRender->size.width - parentRender->padding.horizontal() -
                    margin * 2.0F,
                parentRender->size.height - parentRender->padding.vertical() -
                    margin * 2.0F};
        }
        // Flow layout owns position and may grow with its child. Keep the
        // canvas budget rather than using its current intrinsic size as a cap.
        canvasResizeCoordinateOrigin_ = referenceOrigin - contentOrigin;
        canvasResizeLimits_ = core::Size{
            std::max(0.0F, referenceSize.width / canvasResizeZoom_),
            std::max(0.0F, referenceSize.height / canvasResizeZoom_)};
        canvasResizeActive_ = true;
        canvasResizeId_ = *selected;
        canvasResizeHandle_ = handle;
        canvasResizePointer_ = shell_.controller().dragAnchor();
        canvasResizeDocumentId_ = workbench_.document()->documentId;
        canvasResizeRevision_ = workbench_.documentRevision();
        canvasResizeStart_ = CanvasResizePreview{
            *selected,
            (renderOrigin.x - referenceOrigin.x) / canvasResizeZoom_,
            (renderOrigin.y - referenceOrigin.y) / canvasResizeZoom_,
            renderNode->size.width / canvasResizeZoom_,
            renderNode->size.height / canvasResizeZoom_};
        canvasResizePreview_ = canvasResizeStart_;
        // 拖动换位的对齐候选（§8.3 虚线参考）：父框 0/中/尾 + 同父子节
        // 点的边与中线（拖动节点的左/中/右分别对齐目标），按文档声明收
        // 集，与渲染缩放无关。
        canvasResizeSnapXs_.clear();
        canvasResizeGuideXs_.clear();
        canvasResizeSnapYs_.clear();
        canvasResizeGuideYs_.clear();
        canvasResizeActiveGuideX_.reset();
        canvasResizeActiveGuideY_.reset();
        if (dragTarget.has_value()) {
            const float frameWidth = canvasResizeLimits_.width;
            const float frameHeight = canvasResizeLimits_.height;
            const float width = canvasResizeStart_.width;
            const float height = canvasResizeStart_.height;
            const auto addX = [this](float snap, float guide) {
                canvasResizeSnapXs_.push_back(snap);
                canvasResizeGuideXs_.push_back(guide);
            };
            const auto addY = [this](float snap, float guide) {
                canvasResizeSnapYs_.push_back(snap);
                canvasResizeGuideYs_.push_back(guide);
            };
            addX(0.0F, 0.0F);
            addX((frameWidth - width) * 0.5F, frameWidth * 0.5F);
            addX(frameWidth - width, frameWidth);
            addY(0.0F, 0.0F);
            addY((frameHeight - height) * 0.5F, frameHeight * 0.5F);
            addY(frameHeight - height, frameHeight);
            if (parent != nullptr) {
                for (const auto& child : parent->children) {
                    if (child.id == canvasResizeId_) continue;
                    const float left = designNumber(child, "left");
                    const float top = designNumber(child, "top");
                    const float childWidth = designNumber(child, "width");
                    const float childHeight = designNumber(child, "height");
                    const float center = left + childWidth * 0.5F;
                    const float middle = top + childHeight * 0.5F;
                    addX(left, left);
                    addX(center - width * 0.5F, center);
                    addX(left + childWidth, left + childWidth);
                    addX(left - width, left);
                    addX(left + childWidth - width, left + childWidth);
                    addY(top, top);
                    addY(middle - height * 0.5F, middle);
                    addY(top + childHeight, top + childHeight);
                    addY(top - height, top);
                    addY(top + childHeight - height, top + childHeight);
                }
            }
        }
        // 拖动换位顺带选中目标（点击本会选中；拖动抑制了点击，这里补
        // 上，属性面板随拖动同步）。
        if (dragTarget.has_value() &&
            workbench_.selection().primary != canvasResizeId_) {
            selectNode(canvasResizeId_);
        }
        shell_.markDirty();
        return;
    }
    if (!canvasResizeActive_) return;
    // 换位会话的目标由伪手柄锚定，不要求它同时是当前选中（选中只是
    // 顺带效果）；手柄会话仍要求选中一致（手柄画在选中框上）。
    const bool dragHandle = canvasResizeHandle_.rfind("drag:", 0) == 0;
    if (!workbench_.document().has_value() ||
        workbench_.document()->documentId != canvasResizeDocumentId_ ||
        workbench_.documentRevision() != canvasResizeRevision_ ||
        (!dragHandle &&
         workbench_.selection().primary != canvasResizeId_)) {
        endCanvasResizeSession();
        shell_.markDirty();
        return;
    }
    const float canvasWidth = canvasResizeLimits_.width;
    const float canvasHeight = canvasResizeLimits_.height;
    // The grid stays in design units; the snap radius stays in logical pixels.
    const float threshold = shell_.theme().designerCanvas.snapThreshold /
                            canvasResizeZoom_;
    if (phase == core::DragPhase::Move || phase == core::DragPhase::Drop) {
        const auto logicalDelta = position - canvasResizePointer_;
        const core::Offset delta{logicalDelta.x / canvasResizeZoom_,
                                 logicalDelta.y / canvasResizeZoom_};
        CanvasResizePreview next = canvasResizeStart_;
        const bool moveLeft = canvasResizeHandle_.find('w') != std::string::npos;
        const bool moveRight = canvasResizeHandle_.find('e') != std::string::npos;
        const bool moveTop = canvasResizeHandle_.find('n') != std::string::npos;
        const bool moveBottom = canvasResizeHandle_.find('s') != std::string::npos;
        const float right = canvasResizeStart_.x + canvasResizeStart_.width;
        const float bottom = canvasResizeStart_.y + canvasResizeStart_.height;
        if (dragHandle) {
            // 拖动换位：尺寸不变、位置跟手；先对齐参考候选（父框 + 兄弟
            // 边/中线，命中画全幅虚线），未命中回落 8px 网格；吸附后钳
            // 制在父内容区内（G-D14 §4.15 同口径：设计坐标里运算）。
            float x = canvasResizeStart_.x + delta.x;
            float y = canvasResizeStart_.y + delta.y;
            x = snapDragCoordinate(x, canvasResizeSnapXs_,
                                   canvasResizeGuideXs_, canvasWidth,
                                   threshold, canvasResizeActiveGuideX_);
            y = snapDragCoordinate(y, canvasResizeSnapYs_,
                                   canvasResizeGuideYs_, canvasHeight,
                                   threshold, canvasResizeActiveGuideY_);
            next.x = std::clamp(
                x, 0.0F,
                std::max(0.0F, canvasWidth - canvasResizeStart_.width));
            next.y = std::clamp(
                y, 0.0F,
                std::max(0.0F, canvasHeight - canvasResizeStart_.height));
        } else if (!canvasResizePositionEditable_) {
            if (moveLeft || moveRight) {
                const float width = canvasResizeStart_.width +
                                    (moveLeft ? -delta.x : delta.x);
                next.width = std::clamp(
                    snapCanvasCoordinate(width, canvasWidth, threshold),
                    4.0F, std::max(4.0F, canvasWidth));
            }
            if (moveTop || moveBottom) {
                const float height = canvasResizeStart_.height +
                                     (moveTop ? -delta.y : delta.y);
                next.height = std::clamp(
                    snapCanvasCoordinate(height, canvasHeight, threshold),
                    4.0F, std::max(4.0F, canvasHeight));
            }
        } else {
            if (moveLeft) {
                float edge = snapCanvasCoordinate(
                    canvasResizeStart_.x + delta.x, canvasWidth,
                    threshold);
                edge = std::clamp(edge, 0.0F, std::max(0.0F, right - 4.0F));
                next.x = edge;
                next.width = std::max(4.0F, right - edge);
            } else if (moveRight) {
                float edge = snapCanvasCoordinate(
                    right + delta.x, canvasWidth,
                    threshold);
                edge = std::clamp(edge, next.x + 4.0F,
                                  std::max(next.x + 4.0F, canvasWidth));
                next.width = std::max(4.0F, edge - next.x);
            }
            if (moveTop) {
                float edge = snapCanvasCoordinate(
                    canvasResizeStart_.y + delta.y, canvasHeight,
                    threshold);
                edge = std::clamp(edge, 0.0F, std::max(0.0F, bottom - 4.0F));
                next.y = edge;
                next.height = std::max(4.0F, bottom - edge);
            } else if (moveBottom) {
                float edge = snapCanvasCoordinate(
                    bottom + delta.y, canvasHeight,
                    threshold);
                edge = std::clamp(edge, next.y + 4.0F,
                                  std::max(next.y + 4.0F, canvasHeight));
                next.height = std::max(4.0F, edge - next.y);
            }
        }
        canvasResizePreview_ = next;
        shell_.markDirty();
        if (phase == core::DragPhase::Move) return;
    }
    if (phase != core::DragPhase::Drop) return;
    const auto preview = canvasResizePreview_;
    const std::string handleName = canvasResizeHandle_;
    const bool positionEditable = canvasResizePositionEditable_;
    endCanvasResizeSession();

    std::vector<std::pair<std::string, dsl::DesignValue>> properties;
    const auto number = [](float value) {
        return dsl::DesignValue{dsl::DesignValue::Variant{
            static_cast<double>(std::max(0.0F, value))}};
    };
    if (handleName.find('e') != std::string::npos ||
        handleName.find('w') != std::string::npos) {
        properties.emplace_back("width", number(preview.width));
    }
    if (handleName.find('n') != std::string::npos ||
        handleName.find('s') != std::string::npos) {
        properties.emplace_back("height", number(preview.height));
    }
    if (positionEditable && handleName.find('w') != std::string::npos) {
        properties.emplace_back(
            "left", number(preview.x));
    }
    if (positionEditable && handleName.find('n') != std::string::npos) {
        properties.emplace_back(
            "top", number(preview.y));
    }
    if (positionEditable && handleName.rfind("drag:", 0) == 0) {
        // 拖动换位：一次事务只写 left/top（与 resize 手柄的事务粒度
        // 一致，undo 一步还原）。
        properties.emplace_back(
            "left", number(preview.x));
        properties.emplace_back(
            "top", number(preview.y));
    }
    if (!properties.empty() &&
        workbench_.setProperties(preview.id, std::move(properties))) {
        refreshDocumentUi();
        shell_.markDirty();
    }
}

void DesignerApp::endCanvasResizeSession() {
    canvasResizeActive_ = false;
    canvasResizeId_ = 0;
    canvasResizeHandle_.clear();
    canvasResizePointer_ = {};
    canvasResizeStart_ = {};
    canvasResizePreview_ = {};
    canvasResizeCoordinateOrigin_ = {};
    canvasResizeLimits_ = {};
    canvasResizeZoom_ = 1.0F;
    canvasResizeDocumentId_.clear();
    canvasResizeRevision_ = 0;
    canvasResizePositionEditable_ = false;
    canvasResizeSnapXs_.clear();
    canvasResizeGuideXs_.clear();
    canvasResizeSnapYs_.clear();
    canvasResizeGuideYs_.clear();
    canvasResizeActiveGuideX_.reset();
    canvasResizeActiveGuideY_.reset();
}

core::Widget DesignerApp::buildOutlineDragOverlay() const {
    const auto& theme = shell_.theme();
    const auto& tokens = theme.dragDrop;
    const auto view = shell_.view();
    std::string label = "Move node";
    if (const auto outline = workbench_.outline(); outline.has_value()) {
        if (const auto* node = findOutlineNode(*outline, outlineDragId_);
            node != nullptr) {
            label = nodeLabel(*node);
        }
    }
    auto content = core::makeText(label, theme.typography.caption);
    core::Widget ghost;
    ghost.type = core::WidgetType::Container;
    ghost.color = tokens.ghostSurface;
    ghost.radius = core::CornerRadius::all(theme.metrics.cardRadius);
    ghost.elevation = 2.0F;
    ghost.styleOverrides.border = tokens.ghostBorder;
    ghost.styleOverrides.borderWidth = theme.metrics.controlBorderWidth;
    ghost.padding = core::EdgeInsets::symmetric(8.0F, 4.0F);
    ghost.children.push_back(std::move(content));
    ghost = core::withStackPosition(
        std::move(ghost),
        core::Offset{outlineDragPointer_.x + tokens.ghostGrabOffsetX,
                     outlineDragPointer_.y - tokens.ghostGrabOffsetY});
    ghost.key = "designer:outline-drag-ghost";

    core::Widget indicator;
    indicator.type = core::WidgetType::Container;
    indicator.color = tokens.dropIndicator;
    indicator.height = tokens.indicatorThickness;
    if (const auto* viewport =
            core::findNodeByKey(shell_.root(), "designer-outline-view")) {
        indicator.width = viewport->size.width;
        const auto origin =
            core::absoluteOffset(shell_.root(), "designer-outline-view");
        const auto gap = std::min(outlineDragInsertIndex_,
                                  outlineController_.itemCount());
        const float boundaryY = origin.y + outlineController_.offsetOfIndex(gap) -
                                outlineController_.scrollOffset();
        indicator = core::withStackPosition(
            std::move(indicator), core::Offset{origin.x, boundaryY});
    }
    indicator.key = "designer:outline-drag-indicator";
    auto overlay = core::makeStack(
        {std::move(indicator), std::move(ghost)}, core::StackAlignment::TopLeft);
    overlay.key = "designer:outline-drag-overlay";
    overlay.width = view.width;
    overlay.height = view.height;
    return overlay;
}

void DesignerApp::toolboxDragSession(core::DragPhase phase,
                                     core::Offset position,
                                     const std::string& sourceKey) {
    constexpr std::string_view kSourcePrefix = "designer-toolbox:";
    if (sourceKey.rfind(kSourcePrefix, 0) != 0) return;
    if (!workbench_.document().has_value()) {
        if (phase == core::DragPhase::Cancel || phase == core::DragPhase::Drop) {
            endToolboxDragSession();
        }
        return;
    }
    if (phase == core::DragPhase::Cancel) {
        endToolboxDragSession();
        return;
    }
    if (phase == core::DragPhase::Start) {
        toolboxDragActive_ = true;
        toolboxDragType_ = sourceKey.substr(kSourcePrefix.size());
        toolboxDragPointer_ = position;
        shell_.setVisualOverlayBuilder([this]()
                                            -> std::optional<core::Widget> {
            if (!toolboxDragActive_) return std::nullopt;
            return buildToolboxDragOverlay();
        });
        return;
    }
    if (!toolboxDragActive_) return;
    toolboxDragPointer_ = position;
    if (phase == core::DragPhase::Move) {
        shell_.markDirty();
        return;
    }
    if (phase != core::DragPhase::Drop) return;
    const std::string type = toolboxDragType_;
    std::vector<const core::RenderNode*> chain;
    (void)core::hitTestChain(shell_.root(), position, chain);
    const bool overCanvas = std::any_of(
        chain.begin(), chain.end(), [](const core::RenderNode* node) {
            return node != nullptr && node->key == "designer-canvas";
        });
    endToolboxDragSession();
    if (overCanvas) insertNodeType(type);
}

void DesignerApp::endToolboxDragSession() {
    toolboxDragActive_ = false;
    toolboxDragType_.clear();
    toolboxDragPointer_ = {};
    shell_.clearVisualOverlay();
}

core::Widget DesignerApp::buildToolboxDragOverlay() const {
    const auto& theme = shell_.theme();
    const auto& tokens = theme.dragDrop;
    auto ghost = core::makeContainer(
        core::makeText(toolboxDragType_, theme.typography.caption),
        std::nullopt, std::nullopt, core::EdgeInsets::symmetric(8.0F, 4.0F),
        {}, tokens.ghostSurface,
        core::CornerRadius::all(theme.metrics.cardRadius),
        "designer:toolbox-drag-ghost");
    ghost.elevation = 2.0F;
    ghost.styleOverrides.border = tokens.ghostBorder;
    ghost.styleOverrides.borderWidth = theme.metrics.controlBorderWidth;
    ghost = core::withStackPosition(
        std::move(ghost),
        core::Offset{toolboxDragPointer_.x + tokens.ghostGrabOffsetX,
                     toolboxDragPointer_.y - tokens.ghostGrabOffsetY});
    auto overlay = core::makeStack({std::move(ghost)},
                                   core::StackAlignment::TopLeft);
    overlay.key = "designer:toolbox-drag-overlay";
    overlay.width = shell_.view().width;
    overlay.height = shell_.view().height;
    return overlay;
}

void DesignerApp::syncSelectionFromOutline() {
    const auto& outlineSelection = outlineController_.selection();
    if (outlineSelection.selectedKeys().empty()) return;

    std::vector<dsl::DesignNodeId> ordered;
    std::set<dsl::DesignNodeId> selected;
    for (const auto& key : outlineSelection.selectedKeys()) {
        const auto id = outlineModel_.idForKey(key);
        if (!id.has_value() || selected.contains(*id)) continue;
        selected.insert(*id);
        ordered.push_back(*id);
    }
    if (selected.empty()) return;

    std::optional<dsl::DesignNodeId> primary;
    if (!outlineSelection.currentKey().empty()) {
        primary = outlineModel_.idForKey(outlineSelection.currentKey());
        if (!primary.has_value() || !selected.contains(*primary)) {
            primary.reset();
        }
    }
    if (!primary.has_value()) primary = ordered.back();

    const auto& current = workbench_.selection();
    if (current.ids == selected && current.primary == primary) return;

    std::vector<dsl::DesignNodeId> reordered;
    reordered.reserve(ordered.size());
    for (const auto id : ordered) {
        if (id != *primary) reordered.push_back(id);
    }
    reordered.push_back(*primary);
    for (std::size_t index = 0; index < reordered.size(); ++index) {
        (void)workbench_.selectNode(
            reordered[index], index == 0 ? dsl::DesignSelectionMode::Replace
                                         : dsl::DesignSelectionMode::Add);
    }
    applyPreviewState();
    shell_.markDirty();
}

void DesignerApp::applyEnvironmentTheme() {
    accessibility::AccessibilityOverrides overrides;
    overrides.highContrast = highContrast_;
    overrides.fontScale = fontScale_;
    shell_.setAccessibilityOverrides(overrides);
    previewShell_.setAccessibilityOverrides(overrides);
    const auto settings = shell_.accessibilitySettings();
    // B：工作台 Compact，预览独立 Comfortable（visual-system §3.6，
    // workspace-design §3.1）。两者正交，切换互不污染 dirty/undo。
    shell_.setTheme(
        style::Theme::fromSettings(settings, darkMode_, density_));
    previewShell_.setTheme(style::Theme::fromSettings(
        previewShell_.accessibilitySettings(), darkMode_, previewDensity_));
    previewScopeData_ = style::makeThemeScopeData(
        style::Theme::fromSettings(settings, darkMode_, previewDensity_));
}

void DesignerApp::toggleTheme() {
    darkMode_ = !darkMode_;
    applyEnvironmentTheme();
}

void DesignerApp::cycleDensity() {
    switch (density_) {
        case style::ControlDensity::Compact:
            density_ = style::ControlDensity::Comfortable;
            break;
        case style::ControlDensity::Comfortable:
            density_ = style::ControlDensity::Touch;
            break;
        case style::ControlDensity::Touch:
            density_ = style::ControlDensity::Compact;
            break;
    }
    applyEnvironmentTheme();
}

void DesignerApp::cycleDpi() {
    shell_.pointerCancel();
    constexpr float kScales[] = {1.0F, 1.25F, 2.0F};
    constexpr std::size_t kScaleCount = sizeof(kScales) / sizeof(kScales[0]);
    std::size_t next = 0;
    for (std::size_t index = 0; index < kScaleCount; ++index) {
        if (deviceScale_ == kScales[index]) {
            next = (index + 1) % kScaleCount;
            break;
        }
    }
    deviceScale_ = kScales[next];
    (void)canvasTransform_.setDeviceScale(deviceScale_);
    shell_.setDeviceScale(deviceScale_);
    previewShell_.setDeviceScale(deviceScale_);
    shell_.markDirty();
    previewShell_.markDirty();
}

void DesignerApp::adjustCanvasZoom(
    float factor, std::optional<core::Offset> anchor) {
    // 离散档位（§8.6 用户契约）：默认 100%；≤200% 区间步进 25%，
    // >200% 步进 50%；钳制在 10%–500%。factor 仅取方向（≥1 放大）。
    constexpr float kLevels[] = {0.10F, 0.25F, 0.50F, 0.75F, 1.00F,
                                 1.25F, 1.50F, 1.75F, 2.00F,
                                 2.50F, 3.00F, 3.50F, 4.00F, 4.50F, 5.00F};
    if (!std::isfinite(factor) || factor == 0.0F) return;
    const float current = canvasTransform_.zoom();
    std::size_t nearest = 0;
    for (std::size_t index = 1; index < std::size(kLevels); ++index) {
        if (std::abs(kLevels[index] - current) <
            std::abs(kLevels[nearest] - current)) {
            nearest = index;
        }
    }
    const std::size_t next =
        factor >= 1.0F
            ? std::min(nearest + 1, std::size(kLevels) - 1)
            : (nearest > 0 ? nearest - 1 : std::size_t{0});
    if (kLevels[next] == current) return;
    // 锚定缩放（§8.8）：保持锚点（窗口坐标，默认画布内容中心）下方
    // 的设计坐标不动——pan 随新缩放重解。画布节点未就绪（如首帧前的
    // 程序化调用）时不锚定，仅改缩放。
    std::optional<core::Offset> anchorLocal;
    const auto* canvasNode =
        core::findNodeByKey(shell_.root(), "designer-canvas");
    if (canvasNode != nullptr) {
        const auto origin =
            core::absoluteOffset(shell_.root(), "designer-canvas");
        const core::Offset padding{canvasNode->padding.left,
                                   canvasNode->padding.top};
        if (anchor.has_value()) {
            anchorLocal = *anchor - origin - padding;
        } else {
            anchorLocal =
                origin + padding +
                core::Offset{
                    (canvasNode->size.width -
                     canvasNode->padding.horizontal()) * 0.5F,
                    (canvasNode->size.height -
                     canvasNode->padding.vertical()) * 0.5F} -
                origin - padding;
        }
    }
    const float newZoom = kLevels[next];
    shell_.pointerCancel();
    (void)canvasTransform_.setZoom(newZoom);
    if (anchorLocal.has_value()) {
        const core::Offset world{
            (anchorLocal->x - canvasTransform_.pan().x) / current,
            (anchorLocal->y - canvasTransform_.pan().y) / current};
        (void)canvasTransform_.setPan(
            core::Offset{anchorLocal->x - world.x * newZoom,
                         anchorLocal->y - world.y * newZoom});
    }
    refreshDocumentUi();
    shell_.markDirty();
}

void DesignerApp::panCanvas(core::Offset delta) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) return;
    if (delta == core::Offset{}) return;
    shell_.pointerCancel();
    const auto current = canvasTransform_.pan();
    (void)canvasTransform_.setPan(current + delta);
    shell_.markDirty();
}

void DesignerApp::resetCanvasView() {
    const bool changed = canvasTransform_.zoom() != 1.0F ||
                         canvasTransform_.pan() != core::Offset{};
    if (!changed) return;
    shell_.pointerCancel();
    (void)canvasTransform_.setZoom(1.0F);
    (void)canvasTransform_.setPan({});
    refreshDocumentUi();
    shell_.markDirty();
}


void DesignerApp::fitCanvasView() {
    // 适合视图（design/designer-canvas.html"适合"）：按画布内容区缩小
    // 居中（不放大；画板放得下时 100% 居中）。缩放值为精确适配值，后
    // 续 ± 步进会吸附回离散档位。
    const auto* canvasNode =
        core::findNodeByKey(shell_.root(), "designer-canvas");
    if (canvasNode == nullptr || !workbench_.document().has_value()) return;
    const auto readNumber = [&root = workbench_.document()->root](
                                const char* name) -> float {
        const auto found = root.properties.find(name);
        if (found == root.properties.end()) return -1.0F;
        const auto* number = std::get_if<double>(&found->second.value);
        return number != nullptr ? static_cast<float>(*number) : -1.0F;
    };
    const float designWidth = readNumber("width");
    const float designHeight = readNumber("height");
    const float contentWidth =
        canvasNode->size.width - canvasNode->padding.horizontal();
    const float contentHeight =
        canvasNode->size.height - canvasNode->padding.vertical();
    if (designWidth <= 0.0F || designHeight <= 0.0F ||
        contentWidth <= 0.0F || contentHeight <= 0.0F) {
        return;
    }
    // §8.8（designer-canvas.html fit()）：水平留 120（左右各 60 的呼吸
    // 与角标）、垂直留 100（上方 caption 32 + 下方裕量），允许放大到
    // 1.15 铺满；垂直中心向下让 8px 给页面 caption。
    const float zoom = std::min({(contentWidth - 120.0F) / designWidth,
                                 (contentHeight - 100.0F) / designHeight,
                                 1.15F});
    const core::Offset pan{
        (contentWidth - designWidth * zoom) * 0.5F,
        (contentHeight - designHeight * zoom) * 0.5F + 8.0F};
    if (zoom == canvasTransform_.zoom() && pan == canvasTransform_.pan()) {
        return;
    }
    shell_.pointerCancel();
    (void)canvasTransform_.setZoom(zoom);
    (void)canvasTransform_.setPan(pan);
    refreshDocumentUi();
    shell_.markDirty();
}

void DesignerApp::cycleFontScale() {
    constexpr float kScales[] = {1.0F, 1.25F, 1.5F};
    constexpr std::size_t kScaleCount = sizeof(kScales) / sizeof(kScales[0]);
    std::size_t next = 0;
    for (std::size_t index = 0; index < kScaleCount; ++index) {
        if (fontScale_ == kScales[index]) {
            next = (index + 1) % kScaleCount;
            break;
        }
    }
    fontScale_ = kScales[next];
    applyEnvironmentTheme();
}

void DesignerApp::toggleHighContrast() {
    highContrast_ = !highContrast_;
    applyEnvironmentTheme();
}

void DesignerApp::toggleCanvasGuides() {
    canvasGuidesEnabled_ = !canvasGuidesEnabled_;
    shell_.markDirty();
}

void DesignerApp::toggleZen() {
    // B 专注模式（workspace-design §3/§4.4）：释放完整画布，隐藏左右栏与
    // 底栏；Esc 退出（configFor）。不触碰 dirty/undo。
    zenMode_ = !zenMode_;
    shell_.markDirty();
}

void DesignerApp::toggleLeftCollapsed() {
    leftCollapsed_ = !leftCollapsed_;
    shell_.markDirty();
}

void DesignerApp::toggleBottom() {
    bottomOpen_ = !bottomOpen_;
    shell_.markDirty();
}

void DesignerApp::resetLayout() {
    leftSplitter_.resetToInitial();
    rightSplitter_.resetToInitial();
    rightWidthPreference_ = 280.0F;
    openMenu_.reset();
    zenMode_ = false;
    leftCollapsed_ = false;
    bottomOpen_ = false;
    centerView_ = CenterView::Design;
    centerTab_ = CenterTab::Canvas;
    statusMessage_ = "已恢复画布优先默认工作区";
    shell_.markDirty();
}

void DesignerApp::showToolbox() {
    zenMode_ = false;
    leftCollapsed_ = false;
    leftPanelTab_ = LeftPanelTab::Toolbox;
    shell_.markDirty();
}

void DesignerApp::focusLayoutContext() {
    // B 布局按钮：选中布局容器，在右侧调整间距/主轴（HTML layout-context）。
    if (!workbench_.document().has_value()) return;
    const auto primary = workbench_.selection().primary;
    if (!primary.has_value()) return;
    const auto* node = findDesignNode(workbench_.document()->root, *primary);
    if (node == nullptr) return;
    // 若选中叶节点，回到父容器；容器则保持。
    dsl::DesignNodeId target = *primary;
    const auto* schema = dsl::findNodeSchema(node->type);
    const bool isContainer = (node->type == "Column" || node->type == "Row" ||
                              node->type == "Container" || node->type == "Stack");
    (void)schema;
    if (!isContainer) {
        if (const auto location =
                locateDesignNode(workbench_.document()->root, *primary);
            location.has_value() && location->parent != 0) {
            target = location->parent;
        }
    }
    selectNode(target);
}

void DesignerApp::resetPreviewState() {
    if (!previewStateKey_.empty()) {
        shell_.setVisualPreviewState(previewStateKey_, {});
    }
    previewStateKey_.clear();
    previewStateMode_ = PreviewStateMode::None;
}

void DesignerApp::clearPropertyObservers() {
    for (const auto& [key, observer] : propertyObservers_) {
        (void)key;
        shell_.state().unsubscribe(observer);
    }
    propertyObservers_.clear();
}

void DesignerApp::registerPropertyBinding(
    dsl::DesignNodeId id, const dsl::DesignPreviewProperty& property) {
    if (!property.value.has_value() || !isEditableProperty(*property.value)) {
        return;
    }
    const std::string bind = propertyBindingKey(id, property.name);
    syncingPropertyState_ = true;
    shell_.state().set(bind, propertyStateValue(*property.value));
    syncingPropertyState_ = false;
    if (propertyObservers_.contains(bind)) return;

    const dsl::DesignValue prototype = *property.value;
    const std::vector<dsl::DesignNodeId> selectedIds =
        workbench_.selection().ids.empty()
            ? std::vector<dsl::DesignNodeId>{id}
            : std::vector<dsl::DesignNodeId>{
                  workbench_.selection().ids.begin(),
                  workbench_.selection().ids.end()};
    propertyObservers_[bind] = shell_.state().subscribe(
        bind, [this, id, name = property.name, bind, prototype,
               selectedIds] {
            if (syncingPropertyState_ || !workbench_.document().has_value()) {
                return;
            }
            const auto* node =
                findDesignNode(workbench_.document()->root, id);
            if (node == nullptr) return;
            const auto propertyIt = node->properties.find(name);
            const auto& current = propertyIt == node->properties.end()
                                      ? prototype
                                      : propertyIt->second;
            const auto parsed =
                parsePropertyState(current, shell_.state().get(bind));
            if (!parsed.has_value() ||
                !workbench_.setProperty(selectedIds, name, *parsed)) {
                syncingPropertyState_ = true;
                shell_.state().set(bind, propertyStateValue(current));
                syncingPropertyState_ = false;
                return;
            }
            shell_.markDirty();
        });
}

void DesignerApp::registerReferenceBinding(
    dsl::DesignNodeId id, const dsl::DesignPreviewProperty& property) {
    if (!property.reference.has_value()) return;
    const std::string bind = referenceBindingKey(id, property.name);
    syncingPropertyState_ = true;
    shell_.state().set(bind, *property.reference);
    syncingPropertyState_ = false;
    if (propertyObservers_.contains(bind)) return;

    propertyObservers_[bind] = shell_.state().subscribe(
        bind, [this, id, name = property.name, bind] {
            if (syncingPropertyState_ || !workbench_.document().has_value()) {
                return;
            }
            const auto* node =
                findDesignNode(workbench_.document()->root, id);
            if (node == nullptr) return;
            const auto referenceIt = node->references.find(name);
            const std::string previous =
                referenceIt == node->references.end() ? std::string{}
                                                       : referenceIt->second;
            const std::string value = shell_.state().get(bind);
            bool changed = false;
            if (value.empty()) {
                changed = workbench_.clearReference(id, name);
            } else {
                changed = workbench_.setReference(id, name, value);
            }
            if (!changed) {
                syncingPropertyState_ = true;
                shell_.state().set(bind, previous);
                syncingPropertyState_ = false;
                return;
            }
            (void)workbench_.refresh(&runtimeContext_);
            rebuildReferences();
            shell_.markDirty();
        });
}

void DesignerApp::applyPreviewState() {
    if (!previewStateKey_.empty()) {
        shell_.setVisualPreviewState(previewStateKey_, {});
    }
    previewStateKey_.clear();
    if (previewStateMode_ == PreviewStateMode::None) return;
    const auto primary = workbench_.selection().primary;
    if (!primary.has_value()) {
        previewStateMode_ = PreviewStateMode::None;
        return;
    }
    const auto outline = workbench_.outline();
    if (!outline.has_value()) {
        previewStateMode_ = PreviewStateMode::None;
        return;
    }
    // AppShell resolves overrides by Widget::key. The designer assigns a
    // private stable key to keyless preview nodes in decoratePreview.
    std::function<std::optional<std::string>(
        const dsl::DesignPreviewOutlineNode&)> findKey =
        [&](const dsl::DesignPreviewOutlineNode& node)
        -> std::optional<std::string> {
        if (node.id == *primary) {
            return previewKeyForNodeId(node.id);
        }
        for (const auto& child : node.children) {
            if (const auto key = findKey(child); key.has_value()) return key;
        }
        return std::nullopt;
    };
    const auto previewKey = findKey(*outline);
    if (!previewKey.has_value()) {
        previewStateMode_ = PreviewStateMode::None;
        return;
    }
    previewStateKey_ = *previewKey;
    style::WidgetState state;
    switch (previewStateMode_) {
        case PreviewStateMode::None:
            break;
        case PreviewStateMode::Hovered:
            state.hovered = true;
            break;
        case PreviewStateMode::Pressed:
            state.pressed = true;
            break;
        case PreviewStateMode::Focused:
            state.focused = true;
            break;
    }
    shell_.setVisualPreviewState(previewStateKey_, state);
}

void DesignerApp::cyclePreviewState() {
    switch (previewStateMode_) {
        case PreviewStateMode::None:
            previewStateMode_ = PreviewStateMode::Hovered;
            break;
        case PreviewStateMode::Hovered:
            previewStateMode_ = PreviewStateMode::Pressed;
            break;
        case PreviewStateMode::Pressed:
            previewStateMode_ = PreviewStateMode::Focused;
            break;
        case PreviewStateMode::Focused:
            previewStateMode_ = PreviewStateMode::None;
            break;
    }
    applyPreviewState();
}

core::Widget DesignerApp::decoratePreview(
    core::Widget widget, const dsl::DesignNode& node,
    const std::set<dsl::DesignNodeId>& selected) const {
    widget.key = previewKeyForNodeId(node.id);
    widget.onClick = "designer:select:" + std::to_string(node.id);
    // §8.8 review：文档根不渲染选中装饰——根恒选中（默认选择），整板
    // selectionBackground 冲刷会盖住标尺/网格且与选中铬件语义重复。
    const bool isDocumentRoot =
        workbench_.document().has_value() &&
        node.id == workbench_.document()->root.id;
    if (!isDocumentRoot && selected.contains(node.id)) {
        widget.selected = true;
        widget.styleOverrides.background =
            shell_.theme().colors.selectionBackground;
        widget.styleOverrides.border = shell_.theme().colors.accent;
        widget.styleOverrides.borderWidth = 2.0F;
    }
    const std::size_t count = std::min(widget.children.size(),
                                       node.children.size());
    for (std::size_t index = 0; index < count; ++index) {
        widget.children[index] =
            decoratePreview(std::move(widget.children[index]),
                             node.children[index], selected);
    }
    return widget;
}

std::string DesignerApp::formatValue(
    const std::optional<dsl::DesignValue>& value,
    const std::optional<std::string>& reference) {
    if (reference.has_value()) return "ref: " + *reference;
    if (!value.has_value()) return {};
    return std::visit(
        [](const auto& item) -> std::string {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                return item ? "true" : "false";
            } else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream stream;
                stream << item;
                return stream.str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + item + "\"";
            } else if constexpr (std::is_same_v<T, core::Color>) {
                std::ostringstream stream;
                stream << "#" << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<int>(item.r) << std::setw(2)
                       << static_cast<int>(item.g) << std::setw(2)
                       << static_cast<int>(item.b) << std::setw(2)
                       << static_cast<int>(item.a);
                return stream.str();
            } else {
                return item.domain + ":" + item.value;
            }
        },
        value->value);
}

std::string DesignerApp::formatDiagnostic(
    const dsl::DesignDiagnostic& diagnostic) {
    std::ostringstream stream;
    stream << diagnostic.file;
    if (diagnostic.sourceSpan.has_value()) {
        stream << ":" << diagnostic.sourceSpan->begin.line << ":"
               << diagnostic.sourceSpan->begin.column;
    }
    if (!diagnostic.documentId.empty()) {
        stream << "  [" << diagnostic.documentId << "]";
    }
    stream << "  " << diagnostic.code << "  " << diagnostic.message;
    return stream.str();
}

core::Widget DesignerApp::buildTitleBar() {
    // B 标题栏 44（designer-ide.html variant-b .ide-title）：品牌 + 文件/
    // 编辑/视图菜单 + 居中工程名 + DESIGNER 角标 + 帮助 + 右上窗口控制
    // （最小化/最大化-还原/关闭，titlebar-design §5）。视觉值取 Theme
    // token（visual-system §3.6），不复制 HTML 常量。
    // 自绘 caption：整行 windowDrag，空白可拖动移窗、双击最大化由平台
    // hit-test 原生提供；菜单项/帮助/窗口按钮命中链更深，自然排除拖拽。
    const auto& theme = shell_.theme();
    const auto chrome = [&theme](std::string label, std::string key,
                                 std::string handler) {
        auto button = core::withVariant(
            core::makeButton(std::move(label), theme.typography.caption, {},
                             0.0F, std::move(key), std::nullopt, std::nullopt,
                             std::move(handler)),
            core::ButtonVariant::Chrome);
        button.styleOverrides.padding = core::EdgeInsets::symmetric(8.0F, 6.0F);
        return button;
    };
    const auto windowButton = [&theme](core::IconId icon,
                                       const std::string& id,
                                       std::string semanticsLabel,
                                       core::ButtonVariant variant =
                                           core::ButtonVariant::Ghost) {
        // Gallery 同口径（titlebar-design §5）：44 宽、通高、右缘贴合、
        // 直角无圆角；图标盒 14；close = WindowClose（hover 实心红）。
        // key/handler 沿用 Gallery 裸名（window-minimize/maximize/close，
        // 见 §4.4 与本文档 §8，便于两端共享宿主接线心智），语义标签用
        // 中文（与设计稿 aria-label 一致，keys 保持英文稳定标识）。
        const float scale = theme.typography.body.fontSize / 14.0F;
        core::Widget button = core::makeButton(
            "", core::TextStyle{}, core::EdgeInsets{}, 0.0F, id,
            44.0F * scale, std::nullopt, id);
        button.buttonVariant = variant;
        button.styleOverrides.radius = core::CornerRadius{0.0F, 0.0F, 0.0F, 0.0F};
        button.styleOverrides.iconSize = 14.0F * scale;
        button.semanticsLabel = std::move(semanticsLabel);
        return core::withIcon(std::move(button), icon);
    };
    auto brand = core::makeText("Lumen / Designer", theme.typography.label, {},
                                0.0F, "designer-brand");
    auto fileMenu = chrome("文件", "designer-menu-file", "designer:menu-file");
    auto editMenu = chrome("编辑", "designer-menu-edit", "designer:menu-edit");
    auto viewMenu = chrome("视图", "designer-menu-view", "designer:menu-view");
    auto spacerLeft = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(),
        "designer-titlebar-spacer-left");
    spacerLeft.flex = 1.0F;
    spacerLeft.excludeFromFocus = true;
    spacerLeft.excludeFromSemantics = true;
    auto projectName = core::makeText("Workspace  /  settings.design",
                                      theme.typography.caption, {}, 0.0F,
                                      "designer-project-name");
    auto spacerRight = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(),
        "designer-titlebar-spacer-right");
    spacerRight.flex = 1.0F;
    spacerRight.excludeFromFocus = true;
    spacerRight.excludeFromSemantics = true;
    auto badge =
        core::makeText("DESIGNER", theme.typography.caption, {}, 0.0F,
                       "designer-badge");
    auto help = core::withVariant(
        core::withLeadingIcon(
            core::makeButton({}, theme.typography.caption, {}, 0.0F,
                             "designer-help", std::nullopt, std::nullopt,
                             "designer:toggle-bottom"),
            core::IconId::Info),
        core::ButtonVariant::Chrome);
    help.styleOverrides.padding = core::EdgeInsets::symmetric(8.0F, 6.0F);
    help.semanticsLabel = "Help";
    auto content = core::makeRow(
        {std::move(brand), std::move(fileMenu), std::move(editMenu),
         std::move(viewMenu), std::move(spacerLeft), std::move(projectName),
         std::move(spacerRight), std::move(badge), std::move(help)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 6.0F,
        core::EdgeInsets::only(12.0F, 0.0F, 0.0F, 0.0F), {},
        "designer-titlebar-content");
    content.flex = 1.0F;
    auto actions = core::makeRow(
        {windowButton(core::IconId::Minus, "window-minimize", "最小化窗口"),
         windowButton(windowMaximized_ ? core::IconId::Restore
                                       : core::IconId::Maximize,
                      "window-maximize",
                      windowMaximized_ ? "还原窗口" : "最大化窗口"),
         windowButton(core::IconId::Close, "window-close", "关闭窗口",
                      core::ButtonVariant::WindowClose)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 0.0F);
    actions.key = "designer-window-actions";
    auto row = core::makeRow(
        {std::move(content), std::move(actions)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 0.0F,
        {}, {}, "designer-titlebar-row");
    // 通高 44（titlebar-design §5）：行高定死，窗口控制随 Stretch 贴合
    // 右上通高；内容行内部垂直居中，空白仍可拖。
    row.height = 44.0F;
    row = core::withWindowDrag(std::move(row));
    auto bar = core::makeColumn({std::move(row)},
                                core::MainAxisAlignment::Start,
                                core::CrossAxisAlignment::Stretch, 0.0F, {}, {},
                                "designer-titlebar", std::nullopt, 44.0F);
    bar.color = theme.colors.surface;
    return bar;
}

core::Widget DesignerApp::buildMenuPopover() {
    // B 标题栏菜单 popover（HTML .popover/menu-item）：文件/编辑/视图低频
    // 入口，主工具仅留常用（workspace-design §3）。定位取标题栏下固定偏移
    //（headless 取存在性与命令可达，像素级锚定由宿主验证）。
    if (!openMenu_.has_value()) return core::Widget{};
    const auto& theme = shell_.theme();
    const std::string kind = *openMenu_;
    std::vector<std::pair<std::string, std::string>> items;
    if (kind == "file") {
        items = {{"designer:menu:save", "保存   Ctrl+S"},
                 {"designer:menu:save-as", "另存为   Ctrl+Shift+S"},
                 {"designer:menu:open", "打开   Ctrl+O"},
                 {"designer:menu:new-project", "新建工程   Ctrl+Shift+N"}};
    } else if (kind == "edit") {
        items = {{"designer:menu:undo", "撤销   Ctrl+Z"},
                 {"designer:menu:redo", "重做   Ctrl+Shift+Z"},
                 {"designer:menu:duplicate", "复制节点   Ctrl+D"},
                 {"designer:menu:remove", "删除节点   Delete"}};
    } else {
        items = {{"designer:menu:toggle-left", "显示/隐藏左栏"},
                 {"designer:menu:zen", "专注模式   Esc 退出"},
                 {"designer:menu:toggle-bottom", "显示/隐藏底栏"},
                 {"designer:menu:guides", "显示网格"},
                 {"designer:menu:reset-layout", "恢复默认布局"}};
    }
    std::vector<core::Widget> rows;
    for (const auto& [handler, label] : items) {
        // 菜单项 key 取 handler 后缀，保证 findNodeByKey 可达且唯一。
        const auto pos = handler.rfind(':');
        const std::string key =
            "designer-menu-item:" + kind + ":" +
            (pos == std::string::npos ? handler : handler.substr(pos + 1));
        auto button = core::withVariant(
            core::makeButton(label, theme.typography.caption, {}, 0.0F, key,
                             224.0F, std::nullopt, handler),
            core::ButtonVariant::Chrome);
        button.alignContentStart = true;
        button.styleOverrides.padding = core::EdgeInsets::symmetric(8.0F, 6.0F);
        rows.push_back(std::move(button));
    }
    auto menu = core::makeColumn(std::move(rows),
                                 core::MainAxisAlignment::Start,
                                 core::CrossAxisAlignment::Stretch, 2.0F,
                                 core::EdgeInsets::all(6.0F), {},
                                 "designer-menu-popover");
    menu.color = theme.colors.surfaceElevated;
    const float left = kind == "file" ? 120.0F : (kind == "edit" ? 190.0F : 260.0F);
    return core::withStackPosition(std::move(menu),
                                   core::Offset{left, 44.0F});
}

core::Widget DesignerApp::buildRunPreviewOverlay() {
    // 运行预览模态覆盖层：scrim 全窗 + 居中卡片（标题 + live 内容 +
    // 提示）。仅会话运行中构建；停止/Esc/遮罩点击即隐藏，对应 HTML 稿的
    // 运行预览 modal（原生不再常驻第二个 OS 黑窗口）。
    const auto& theme = shell_.theme();
    const float viewWidth = shell_.view().width;
    const float viewHeight = shell_.view().height;
    const float cardWidth =
        viewWidth > 0.0F ? std::min(viewWidth - 64.0F, 880.0F) : 720.0F;
    const float cardHeight =
        viewHeight > 0.0F ? std::max(240.0F, viewHeight - 96.0F) : 560.0F;

    auto scrim = core::makeContainerLeaf(
        viewWidth > 0.0F ? std::optional<float>{viewWidth} : std::nullopt,
        viewHeight > 0.0F ? std::optional<float>{viewHeight} : std::nullopt,
        {}, {}, theme.colors.scrim, "designer-run-preview-scrim");
    scrim = core::withOnClick(std::move(scrim), "designer:stop");
    scrim.excludeFromFocus = true;
    scrim.excludeFromSemantics = true;

    const std::string sessionLabel =
        previewSessionMode_ == PreviewSessionMode::Debugging ? "调试中"
                                                             : "运行中";
    auto title = core::makeText("运行预览 · " + sessionLabel,
                                theme.typography.label, {}, 0.0F,
                                "designer-run-preview-title");
    auto spacer = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(),
        "designer-run-preview-spacer");
    spacer.flex = 1.0F;
    spacer.excludeFromFocus = true;
    spacer.excludeFromSemantics = true;
    auto close = core::withVariant(
        core::withLeadingIcon(
            core::makeButton({}, theme.typography.caption, {}, 0.0F,
                             "designer-run-preview-close", std::nullopt,
                             std::nullopt, "designer:stop"),
            core::IconId::Close),
        core::ButtonVariant::Chrome);
    close.styleOverrides.padding = core::EdgeInsets::symmetric(8.0F, 6.0F);
    close.semanticsLabel = "关闭预览";
    auto head = core::makeRow(
        {std::move(title), std::move(spacer), std::move(close)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 8.0F,
        {}, {}, "designer-run-preview-head");

    core::Widget preview;
    if (workbench_.frame().hasFrame()) {
        preview = workbench_.frame().widget();
        applyImageResources(preview);
    } else {
        preview = core::makeText("No valid preview frame",
                                 theme.typography.body);
    }
    auto body = core::makeScrollView(
        core::makeContainer(std::move(preview), std::nullopt, std::nullopt,
                            core::EdgeInsets::all(16.0F), {},
                            theme.colors.pageBackground,
                            core::CornerRadius::all(theme.metrics.cardRadius),
                            "designer-run-preview-content"),
        "designer-run-preview-scroll");
    body.scrollAxis = core::ScrollAxis::Vertical;
    body.showScrollbar = true;
    body.scrollOffset = panelScrollOffset("designer-run-preview-scroll");
    body.flex = 1.0F;
    auto hint = core::makeText("运行态修改不会写回设计文档；Esc 或关闭按钮结束预览。",
                               theme.typography.caption, {}, 0.0F,
                               "designer-run-preview-hint");
    hint.styleOverrides.foreground = theme.colors.contentSecondary;
    auto card = core::makeColumn(
        {std::move(head), std::move(body), std::move(hint)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(16.0F), {}, "designer-run-preview-card",
        cardWidth, cardHeight);
    card.color = theme.colors.surface;
    card.semanticsRole = "dialog";
    card.semanticsLabel = "运行预览";

    auto overlay = core::makeStack(
        {std::move(scrim), std::move(card)}, core::StackAlignment::Center, {},
        {}, "designer-run-preview-overlay",
        viewWidth > 0.0F ? std::optional<float>{viewWidth} : std::nullopt,
        viewHeight > 0.0F ? std::optional<float>{viewHeight} : std::nullopt);
    return overlay;
}

core::Widget DesignerApp::buildRail() {
    // B 活动栏 48（variant-b .rail）：控件/结构切换 + 复位。图标-only 36，
    // surface 底（visual-system §3.6）。
    const auto& theme = shell_.theme();
    const auto railButton = [&theme](core::IconId icon, std::string key,
                                     std::string semantics,
                                     std::string handler, bool active) {
        auto button = core::withVariant(
            core::withLeadingIcon(
                core::makeButton({}, theme.typography.caption, {}, 0.0F,
                                 std::move(key), 36.0F, 36.0F,
                                 std::move(handler)),
                icon),
            core::ButtonVariant::Chrome);
        button.semanticsLabel = std::move(semantics);
        button.selected = active;
        return button;
    };
    auto toolbox = railButton(core::IconId::Plus, "designer-rail-toolbox",
                              "控件箱", "designer:rail-toolbox",
                              leftPanelTab_ == LeftPanelTab::Toolbox &&
                                  !leftCollapsed_ && !zenMode_);
    auto outline = railButton(core::IconId::NavLists, "designer-rail-outline",
                              "对象树", "designer:rail-outline",
                              leftPanelTab_ == LeftPanelTab::Outline &&
                                  !leftCollapsed_ && !zenMode_);
    auto spacer = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(),
        "designer-rail-spacer");
    spacer.flex = 1.0F;
    spacer.excludeFromFocus = true;
    spacer.excludeFromSemantics = true;
    auto reset = railButton(core::IconId::NavLayout, "designer-reset-layout",
                            "恢复工作区布局", "designer:reset-layout", false);
    auto rail = core::makeColumn(
        {std::move(toolbox), std::move(outline), std::move(spacer),
         std::move(reset)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 8.0F,
        core::EdgeInsets::symmetric(8.0F, 8.0F), {}, "designer-rail", 48.0F);
    rail.color = theme.colors.surface;
    return rail;
}

core::Widget DesignerApp::buildMainTools() {
    // B 主工具 48（variant-b .main-tools）：保存/撤销/重做 | 选择/插入/
    // 布局 | 提示 | 专注/运行。文件/编辑/视图低频入口在标题栏菜单；画布
    // 工具（缩放/网格/环境）下沉到 canvas-controls（workspace-design §3）。
    // Run 唯一 Filled 主行动点（toolbar §6/§9）。
    const auto& theme = shell_.theme();
    const bool compactLabel = fontScale_ > 1.0F;
    const auto command = [&theme, compactLabel](
                             std::string label, core::IconId icon,
                             std::string key, std::string handler) {
        auto button = core::withVariant(
            core::withLeadingIcon(
                core::makeButton(compactLabel ? std::string{} : std::move(label),
                                 theme.typography.caption, {}, 0.0F,
                                 std::move(key), std::nullopt, std::nullopt,
                                 std::move(handler)),
                icon),
            core::ButtonVariant::Chrome);
        button.styleOverrides.padding = core::EdgeInsets::symmetric(8.0F, 6.0F);
        if (compactLabel) button.semanticsLabel = std::move(label);
        return button;
    };
    const auto iconCommand = [&theme](core::IconId icon, std::string key,
                                      std::string semantics,
                                      std::string handler) {
        auto button = core::withVariant(
            core::withLeadingIcon(
                core::makeButton({}, theme.typography.caption, {}, 0.0F,
                                 std::move(key), std::nullopt, std::nullopt,
                                 std::move(handler)),
                icon),
            core::ButtonVariant::Chrome);
        button.styleOverrides.padding = core::EdgeInsets::symmetric(8.0F, 6.0F);
        button.semanticsLabel = std::move(semantics);
        return button;
    };
    const auto separator = [&theme](std::string key) {
        auto divider = core::makeContainerLeaf(
            1.0F, 20.0F, {}, {}, theme.colors.borderDefault, std::move(key));
        divider.excludeFromFocus = true;
        divider.excludeFromSemantics = true;
        return divider;
    };
    auto spacer = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(),
        "designer-main-tools-spacer");
    spacer.flex = 1.0F;
    spacer.excludeFromFocus = true;
    spacer.excludeFromSemantics = true;

    auto save = command("保存", core::IconId::Save, "designer-save",
                        "designer:save");
    // 旧 Add Text 命令保留为图标件（L0 结构命令可达；B 插入入口为
    // show-toolbox，Add Text 为快捷插入）。
    auto addText = iconCommand(core::IconId::TextAdd, "designer-add-text",
                               "插入文本", "designer:add-text");
    auto undo = iconCommand(core::IconId::Undo, "designer-undo", "撤销 Ctrl+Z",
                            "designer:undo");
    auto redo = iconCommand(core::IconId::Redo, "designer-redo", "重做",
                            "designer:redo");
    // 运行组：Run 主行动点 + Debug/Stop 保留（原生预览会话必需；
    // HTML 仅示 Run，生产保留 Debug/Stop 为次级 Chrome 件）。
    auto debug = command("调试", core::IconId::Bug, "designer-debug",
                         "designer:debug");
    debug.selected = previewSessionMode_ == PreviewSessionMode::Debugging;
    auto stop = iconCommand(core::IconId::Stop, "designer-stop", "停止",
                            "designer:stop");
    stop.enabled = previewSessionMode_ != PreviewSessionMode::Stopped;
    // 文件组低频入口保留为图标件（B 标题栏菜单提供完整入口前，
    // 主工具保留 Open/SaveAs/NewProject 以保命令可达）。
    auto open = iconCommand(core::IconId::Folder, "designer-open", "打开 Ctrl+O",
                            "designer:open");
    auto saveAs = iconCommand(core::IconId::SaveAs, "designer-save-as",
                              "另存为", "designer:save-as");
    auto newProject = iconCommand(core::IconId::FolderPlus,
                                  "designer-new-project", "新建工程",
                                  "designer:new-project");
    // 选择为常驻模式态（HTML span.btn.active），此处以选中态 Chrome 表达。
    auto select = command("选择", core::IconId::Cursor, "designer-select-mode",
                          "designer:layout-context");
    select.selected = true;
    auto insert = command("插入", core::IconId::Plus, "designer-insert",
                          "designer:show-toolbox");
    auto layout = command("布局", core::IconId::NavLayout, "designer-layout",
                          "designer:layout-context");
    auto hint = core::makeText("更少干扰，更大画布", theme.typography.caption,
                               {}, 0.0F, "designer-main-tools-hint");
    auto zen = command("专注", core::IconId::Fit, "designer-zen",
                       "designer:zen");
    zen.selected = zenMode_;
    auto run = core::withLeadingIcon(
        core::makeButton(compactLabel ? std::string{} : std::string{"运行预览"},
                         theme.typography.caption, {}, 0.0F, "designer-run",
                         std::nullopt, std::nullopt, "designer:run"),
        core::IconId::Play);
    run.styleOverrides.padding = core::EdgeInsets::symmetric(8.0F, 6.0F);
    if (compactLabel) run.semanticsLabel = "Run";
    run.selected = previewSessionMode_ != PreviewSessionMode::Stopped;

    auto row = core::makeRow(
        {std::move(save), std::move(open), std::move(saveAs),
         std::move(newProject), std::move(addText), std::move(undo),
         std::move(redo),
         separator("designer-main-tools-separator:0"), std::move(select),
         std::move(insert), std::move(layout),
         separator("designer-main-tools-separator:1"), std::move(hint),
         std::move(spacer), std::move(zen), std::move(debug), std::move(stop),
         std::move(run)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 4.0F,
        core::EdgeInsets::symmetric(12.0F, 0.0F), {},
        "designer-toolbar-row");
    const std::vector<std::pair<const char*, const char*>> tooltipTexts = {
        {"designer-save", "保存   Ctrl+S"},
        {"designer-add-text", "插入文本"},
        {"designer-open", "打开   Ctrl+O"},
        {"designer-save-as", "另存为   Ctrl+Shift+S"},
        {"designer-new-project", "新建工程   Ctrl+Shift+N"},
        {"designer-undo", "撤销   Ctrl+Z"},
        {"designer-redo", "重做   Ctrl+Shift+Z"},
        {"designer-insert", "插入控件   /"},
        {"designer-layout", "布局上下文"},
        {"designer-zen", "专注模式   Esc 退出"},
        {"designer-debug", "调试   Ctrl+D"},
        {"designer-stop", "停止"},
        {"designer-run", "运行预览   Ctrl+R"},
    };
    std::vector<core::Widget> stackChildren;
    stackChildren.push_back(std::move(row));
    for (const auto& [buttonKey, text] : tooltipTexts) {
        const std::string tipKey = std::string{"designer-tip:"} + buttonKey;
        shell_.registerTooltip(buttonKey, tipKey);
        stackChildren.push_back(core::withStackPosition(
            core::makeTooltip(text, tipKey), core::Offset{0.0F, 0.0F}));
    }
    auto stack = core::makeStack(std::move(stackChildren),
                                 core::StackAlignment::TopLeft, {}, {},
                                 "designer-toolbar-stack");
    auto toolbar = core::makeColumn({std::move(stack)},
                                    core::MainAxisAlignment::Start,
                                    core::CrossAxisAlignment::Stretch, 0.0F,
                                    {}, {}, "designer-main-tools", std::nullopt,
                                    48.0F);
    toolbar.color = theme.colors.surface;
    return toolbar;
}

core::Widget DesignerApp::buildToolbar() {
    // 兼容入口：旧五组工具栏已收敛为 B 主工具（workspace-design §6 第二阶段
    // 预设）。直接转调，保持命令 id 与 Run Filled 契约。
    auto tools = buildMainTools();
    tools.key = "designer-toolbar";
    return tools;
}
core::Widget DesignerApp::buildToolboxPanel() {
    // 左面板版本 C 的工具箱视图：7 组全展开的整行列表（组头 = 图标 +
    // 名称；行 = 类型名，Chrome 幽灵 + 左对齐，无尾随装饰——拖放认领
    // 按 onClick 前缀，不依赖拖拽点示意）。整行宽 216px 无长名截断；
    // 点击/拖放仍走 designer:toolbox:type。
    const auto& theme = shell_.theme();
    std::vector<core::Widget> rows;
    for (const auto& group : designerToolboxGroups()) {
        auto groupIcon =
            core::makeIcon(group.icon,
                           "designer-toolbox-group:" + std::string(group.label),
                           14.0F, 14.0F);
        groupIcon.styleOverrides.foreground = theme.colors.contentSecondary;
        groupIcon.excludeFromFocus = true;
        rows.push_back(core::makeRow(
            {std::move(groupIcon),
             core::makeText(group.label, theme.typography.caption, {}, 0.0F,
                            "designer-toolbox-group-label:" +
                                std::string(group.label))},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            6.0F, {}, {}, "designer-toolbox-group-row:" +
                              std::string(group.label)));
        for (const char* type : group.types) {
            auto row = core::withVariant(
                core::makeButton(
                    type, theme.typography.caption, {}, 0.0F,
                    "designer-toolbox:" + std::string(type), std::nullopt,
                    std::nullopt, "designer:toolbox:" + std::string(type)),
                core::ButtonVariant::Chrome);
            row.alignContentStart = true;
            row.styleOverrides.padding = core::EdgeInsets::symmetric(6.0F, 4.0F);
            rows.push_back(std::move(row));
        }
    }
    auto body = core::makeColumn(std::move(rows),
                                 core::MainAxisAlignment::Start,
                                 core::CrossAxisAlignment::Stretch, 2.0F);
    auto scroll = core::makeScrollView(std::move(body),
                                       "designer-toolbox-scroll");
    scroll.scrollAxis = core::ScrollAxis::Vertical;
    scroll.showScrollbar = true;
    scroll.scrollOffset = panelScrollOffset("designer-toolbox-scroll");
    return scroll;
}

core::Widget DesignerApp::buildProjectPanel() {
    // 左面板版本 C：Project 收成底部状态行（图标 + 名称/No project +
    // 新建/打开动作）；加载工程后在状态行下保留紧凑的页面/资源区，
    // designer-project-* 键名不变（测试与语义树兼容）。
    const auto& theme = shell_.theme();
    auto statusIcon = core::makeIcon(core::IconId::Folder,
                                     "designer-project-status-icon", 15.0F,
                                     15.0F);
    statusIcon.styleOverrides.foreground = theme.colors.contentSecondary;
    statusIcon.excludeFromFocus = true;
    const std::string statusText = [&] {
        if (!project_.has_value()) return std::string{"No project"};
        if (!projectFile_.empty()) {
            return std::filesystem::path(projectFile_).filename().string();
        }
        return std::string{"Project"};
    }();
    auto newProject = core::withVariant(
        core::makeButton({}, theme.typography.caption, {}, 0.0F,
                         "designer-project-new", std::nullopt, std::nullopt,
                         "designer:new-project"),
        core::ButtonVariant::Chrome);
    newProject.icon = core::IconId::FolderPlus;
    newProject.styleOverrides.padding = core::EdgeInsets::symmetric(6.0F, 4.0F);
    newProject.semanticsLabel = "New project";
    auto openProject = core::withVariant(
        core::makeButton({}, theme.typography.caption, {}, 0.0F,
                         "designer-project-open", std::nullopt, std::nullopt,
                         "designer:open"),
        core::ButtonVariant::Chrome);
    openProject.icon = core::IconId::Folder;
    openProject.styleOverrides.padding = core::EdgeInsets::symmetric(6.0F, 4.0F);
    openProject.semanticsLabel = "Open";
    auto statusSpacer = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(), {});
    statusSpacer.flex = 1.0F;
    statusSpacer.excludeFromFocus = true;
    statusSpacer.excludeFromSemantics = true;
    auto statusRow = core::makeRow(
        {std::move(statusIcon),
         core::makeText(statusText, theme.typography.caption, {}, 0.0F,
                        "designer-project-status"),
         std::move(statusSpacer), std::move(newProject),
         std::move(openProject)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 4.0F,
        {}, {}, "designer-project-status-row");

    std::vector<core::Widget> sections{std::move(statusRow)};
    if (project_.has_value()) {
        std::vector<core::Widget> rows;
        for (const auto& page : project_->pages) {
            const std::string handler = "designer:project-page:" + page.documentId;
            shell_.handlers()[handler] = [this, id = page.documentId] {
                (void)switchProjectDocument(id);
            };
            auto row = core::withVariant(
                core::makeButton(
                    page.pageName.empty() ? page.documentId : page.pageName,
                    theme.typography.caption, {}, 0.0F,
                    "designer-project-page:" + page.documentId, std::nullopt,
                    std::nullopt, handler),
                core::ButtonVariant::Chrome);
            row.alignContentStart = true;
            row.checked = page.documentId == activeProjectDocumentId_;
            row.styleOverrides.padding =
                core::EdgeInsets::symmetric(6.0F, 4.0F);
            rows.push_back(std::move(row));
        }
        if (!project_->resources.empty()) {
            rows.push_back(core::makeText("Resources", theme.typography.caption,
                                          {}, 0.0F,
                                          "designer-project-resources-heading"));
            for (std::size_t index = 0; index < project_->resources.size();
                 ++index) {
                const auto& resource = project_->resources[index];
                const std::string label =
                    resource.kind.empty() ? "Resource" : resource.kind;
                const std::string path = resource.path.empty()
                                             ? resource.uri
                                             : resource.path;
                rows.push_back(core::makeText(
                    label + "  /  " + path, theme.typography.caption, {}, 0.0F,
                    "designer-project-resource:" + std::to_string(index)));
            }
        }
        auto body = core::makeColumn(std::move(rows),
                                     core::MainAxisAlignment::Start,
                                     core::CrossAxisAlignment::Stretch, 2.0F);
        auto scroll = core::makeScrollView(std::move(body),
                                           "designer-project-scroll",
                                           std::nullopt, 128.0F, {});
        scroll.scrollAxis = core::ScrollAxis::Vertical;
        scroll.showScrollbar = true;
        scroll.scrollOffset =
            panelScrollOffset("designer-project-scroll");
        sections.push_back(std::move(scroll));
    }
    return core::makeColumn(std::move(sections),
                            core::MainAxisAlignment::Start,
                            core::CrossAxisAlignment::Stretch, 4.0F, {}, {},
                            "designer-project-panel");
}

core::Widget DesignerApp::buildOutlinePanel() {
    // B 左栏 224（variant-b dock-tabs 控件/结构按需切换，默认控件；
    // workspace-design §3）。大纲操作（上移/下移/复制/删除）下沉到底部
    // outline-actions（HTML 同名），插入目标常驻 foot（§4.1）。
    const auto& theme = shell_.theme();
    shell_.handlers()["designer:lefttab-outline"] = [this] {
        leftPanelTab_ = LeftPanelTab::Outline;
        shell_.markDirty();
    };
    shell_.handlers()["designer:lefttab-toolbox"] = [this] {
        leftPanelTab_ = LeftPanelTab::Toolbox;
        shell_.markDirty();
    };
    auto outlineTab = core::makeButton(
        "控件", theme.typography.caption, {}, 0.0F, "designer-tab-toolbox",
        std::nullopt, std::nullopt, "designer:lefttab-toolbox");
    outlineTab.flex = 1.0F;
    outlineTab.selected = leftPanelTab_ == LeftPanelTab::Toolbox;
    auto structureTab = core::makeButton(
        "结构", theme.typography.caption, {}, 0.0F, "designer-tab-outline",
        std::nullopt, std::nullopt, "designer:lefttab-outline");
    structureTab.flex = 1.0F;
    structureTab.selected = leftPanelTab_ == LeftPanelTab::Outline;
    auto tabs = core::makeTabs(
        {std::move(outlineTab), std::move(structureTab)}, "designer-left-tabs");

    core::Widget view;
    if (leftPanelTab_ == LeftPanelTab::Toolbox) {
        view = buildToolboxPanel();
    } else {
        view = core::makeTree(&outlineController_, "designer-outline-view",
                              std::nullopt, std::nullopt);
    }
    view.flex = 1.0F;
    // 大纲操作条（HTML .outline-actions）：排序与复制/删除就近放置，
    // 主工具不再重复（B 精简契约）。
    const auto smallCommand = [&theme](core::IconId icon, std::string key,
                                       std::string semantics,
                                       std::string handler) {
        auto button = core::withVariant(
            core::withLeadingIcon(
                core::makeButton({}, theme.typography.caption, {}, 0.0F,
                                 std::move(key), std::nullopt, std::nullopt,
                                 std::move(handler)),
                icon),
            core::ButtonVariant::Chrome);
        button.styleOverrides.padding = core::EdgeInsets::symmetric(6.0F, 4.0F);
        button.semanticsLabel = std::move(semantics);
        return button;
    };
    auto outlineActions = core::makeRow(
        {smallCommand(core::IconId::ArrowUp, "designer-move-up", "上移节点",
                      "designer:move-up"),
         smallCommand(core::IconId::ArrowDown, "designer-move-down", "下移节点",
                      "designer:move-down"),
         smallCommand(core::IconId::Duplicate, "designer-duplicate", "复制节点",
                      "designer:duplicate"),
         smallCommand(core::IconId::Trash, "designer-remove", "删除节点",
                      "designer:remove")},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 4.0F,
        {}, {}, "designer-outline-actions");
    std::string targetName = "root";
    if (workbench_.document().has_value()) {
        const auto primary = workbench_.selection().primary;
        if (primary.has_value()) {
            if (const auto* node = findDesignNode(workbench_.document()->root,
                                                  *primary);
                node != nullptr) {
                const auto keyIt = node->properties.find("key");
                if (keyIt != node->properties.end()) {
                    if (const auto* key =
                            std::get_if<std::string>(&keyIt->second.value);
                        key != nullptr && !key->empty()) {
                        targetName = *key;
                    } else {
                        targetName = node->type;
                    }
                } else {
                    targetName = node->type;
                }
            }
        }
    }
    auto insertionTarget = core::makeText("插入到 " + targetName,
                                          theme.typography.caption, {}, 0.0F,
                                          "designer-insertion-target");
    auto panel = core::makeColumn(
        {std::move(tabs), std::move(view), std::move(outlineActions),
         std::move(insertionTarget), buildProjectPanel()},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 6.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-outline-panel", 224.0F);
    panel.color = theme.colors.surface;
    return panel;
}

core::Widget DesignerApp::buildCanvasStack(core::Widget preview) const {
    const auto& theme = shell_.theme();
    const auto& tokens = theme.designerCanvas;
    std::vector<core::Widget> children;
    std::vector<core::Widget> foregroundDecorations;
    std::optional<float> contentWidth;
    std::optional<float> contentHeight;
    const core::RenderNode* canvasNode =
        core::findNodeByKey(shell_.root(), "designer-canvas");
    if (canvasNode == nullptr) {
        // 首帧无上一帧画布节点：网格/标尺/选中铬件（guides 默认开）要
        // 下一帧才物化——回置一帧让 runApp 帧泵自动补齐，否则窗口启
        // 停在无参考线的首帧，直到第一次输入才刷新（§8.6）。
        shell_.requestRebuildAfterLayout();
    } else {
        contentWidth = std::max(
            0.0F, canvasNode->size.width - canvasNode->padding.horizontal());
        contentHeight = std::max(
            0.0F, canvasNode->size.height - canvasNode->padding.vertical());
    }

    const auto makeDecoration = [](core::Widget widget) {
        widget.excludeFromSemantics = true;
        widget.excludeFromFocus = true;
        // 参考线/网格/标尺等装饰铬件对指针透明：事件穿透到下方内容
        //（交互手柄单独恢复可命中）。
        widget.ignorePointer = true;
        return widget;
    };
    const auto position = [](core::Widget widget, core::Offset offset) {
        return core::withStackPosition(std::move(widget), offset);
    };
    const auto makeBar = [&](std::optional<float> width,
                             std::optional<float> height, core::Color color,
                             std::string key) {
        auto bar = core::makeContainerLeaf(width, height, {}, {}, color,
                                            std::move(key));
        return makeDecoration(std::move(bar));
    };

    // 虚线装饰辅助（§8.3/§8.7）：4px 划/3px 隙的条带，指针透明。键为
    // 前缀:行号:段号（行号由调用方给：矩形 0-3 边、参考线 4/5）。
    const auto dash = [&](float x, float y, float length, bool horizontal,
                          std::string keyPrefix, int lineIndex) {
        constexpr float kDashLength = 4.0F;
        constexpr float kDashGap = 3.0F;
        const float step = kDashLength + kDashGap;
        const auto segments =
            static_cast<int>(std::max(0.0F, length) / step) + 1;
        for (int segment = 0; segment < segments; ++segment) {
            const float segLen = std::min(kDashLength, length - segment * step);
            if (segLen <= 0.0F) break;
            const std::string key = keyPrefix + ":" +
                                    std::to_string(lineIndex) + ":" +
                                    std::to_string(segment);
            auto bar = horizontal
                ? makeBar(segLen, tokens.guideThickness, tokens.guide, key)
                : makeBar(tokens.guideThickness, segLen, tokens.guide, key);
            children.push_back(position(
                std::move(bar),
                horizontal ? core::Offset{x + segment * step, y}
                           : core::Offset{x, y + segment * step}));
        }
    };
    const auto dashRect = [&](float x, float y, float width, float height,
                              const std::string& keyPrefix) {
        const float thickness = tokens.guideThickness;
        dash(x, y, width, true, keyPrefix, 0);
        dash(x, y + height - thickness, width, true, keyPrefix, 1);
        dash(x, y, height, false, keyPrefix, 2);
        dash(x + width - thickness, y, height, false, keyPrefix, 3);
    };

    // §8.7 容器边界（design/designer-canvas.html）：页面（画板）虚线边
    // 界 + 所选父容器虚线轮廓与"类型 · key"标注——与网格开关独立，
    // 默认开。
    if (canvasBoundsEnabled_ && canvasNode != nullptr &&
        workbench_.document().has_value()) {
        const core::Offset canvasOrigin =
            core::absoluteOffset(shell_.root(), "designer-canvas");
        const core::Offset contentOrigin =
            canvasOrigin + core::Offset{canvasNode->padding.left,
                                        canvasNode->padding.top};
        const auto localOf = [&](const std::string& key) {
            return core::absoluteOffset(shell_.root(), key) - contentOrigin;
        };
        // 页面（文档根）虚线边界（外扩 8px，designer-canvas.html
        // .page-boundary inset:-8px）+ 页面 caption（页名 + 预览环境）。
        const std::string rootKey =
            previewKeyForNodeId(workbench_.document()->root.id);
        if (const auto* rootRender =
                core::findNodeByKey(shell_.root(), rootKey)) {
            const auto origin = localOf(rootKey);
            constexpr float kOutset = 8.0F;
            dashRect(origin.x - kOutset, origin.y - kOutset,
                     rootRender->size.width + kOutset * 2.0F,
                     rootRender->size.height + kOutset * 2.0F,
                     "designer-canvas-page-dash");
            const std::string pageName =
                workbench_.document()->pageName.empty()
                    ? std::string{"preview"}
                    : workbench_.document()->pageName;
            const std::string environment =
                std::string{darkMode_ ? "CoreDark" : "CoreLight"} + " · " +
                (previewDensity_ == style::ControlDensity::Compact
                     ? "Compact"
                     : previewDensity_ == style::ControlDensity::Touch
                           ? "Dense"
                           : "Comfortable");
            auto spacer = core::makeContainerLeaf(
                std::nullopt, std::nullopt, {}, {},
                core::Color::transparent(), {});
            spacer.flex = 1.0F;
            spacer.excludeFromFocus = true;
            spacer.excludeFromSemantics = true;
            auto captionLeft = core::makeText(
                pageName, theme.typography.caption, {}, 0.0F,
                "designer-canvas-page-caption-name");
            captionLeft.styleOverrides.foreground =
                theme.colors.accentContent;
            auto captionRight = core::makeText(
                environment, theme.typography.caption, {}, 0.0F,
                "designer-canvas-page-caption-env");
            captionRight.styleOverrides.foreground =
                theme.colors.contentSecondary;
            auto caption = core::makeRow(
                {std::move(captionLeft), std::move(spacer),
                 std::move(captionRight)},
                core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Center, 12.0F, {}, {},
                "designer-canvas-page-caption");
            caption.width = rootRender->size.width;
            children.push_back(position(makeDecoration(std::move(caption)),
                                        core::Offset{origin.x,
                                                     origin.y - 30.0F}));
        }
        // 所选的父容器轮廓 + 标注（父为文档根时页面边界已覆盖，跳过）。
        const auto primary = workbench_.selection().primary;
        if (primary.has_value()) {
            const auto location =
                locateDesignNode(workbench_.document()->root, *primary);
            if (location.has_value() &&
                location->parent != workbench_.document()->root.id) {
                const auto* parent = findDesignNode(
                    workbench_.document()->root, location->parent);
                const std::string parentKey =
                    parent != nullptr
                        ? previewKeyForNodeId(parent->id)
                        : std::string{};
                const auto* parentRender =
                    parent == nullptr
                        ? nullptr
                        : core::findNodeByKey(shell_.root(), parentKey);
                if (parentRender != nullptr) {
                    const auto origin = localOf(parentKey);
                    constexpr float kOutset = 6.0F;
                    dashRect(origin.x - kOutset, origin.y - kOutset,
                             parentRender->size.width + kOutset * 2.0F,
                             parentRender->size.height + kOutset * 2.0F,
                             "designer-canvas-bounds-dash");
                    std::string label = parent->type;
                    if (const auto keyIt = parent->properties.find("key");
                        keyIt != parent->properties.end()) {
                        if (const auto* keyName =
                                std::get_if<std::string>(
                                    &keyIt->second.value);
                            keyName != nullptr && !keyName->empty()) {
                            label += " · " + *keyName;
                        }
                    }
                    auto caption = core::makeText(
                        label, theme.typography.caption, {}, 0.0F,
                        "designer-canvas-bounds-caption", std::nullopt,
                        std::nullopt,
                        core::EdgeInsets::symmetric(6.0F, 3.0F));
                    caption.styleOverrides.background = tokens.guide;
                    caption.styleOverrides.foreground = tokens.onGuide;
                    foregroundDecorations.push_back(position(
                        makeDecoration(std::move(caption)),
                        core::Offset{origin.x, origin.y - 18.0F}));
                }
            }
        }
    }

    // —— §8.8 选中解析（铬件/标尺标线共用）——
    std::string selectedKey;
    if (const auto primary = workbench_.selection().primary;
        primary.has_value()) {
        if (const auto outline = workbench_.outline(); outline.has_value()) {
            if (findOutlineNode(*outline, *primary) != nullptr) {
                selectedKey = previewKeyForNodeId(*primary);
            }
        }
    }
    const auto* selectedNode =
        selectedKey.empty()
            ? nullptr
            : core::findNodeByKey(shell_.root(), selectedKey);
    std::optional<core::Rect> selectedRect;
    if (selectedNode != nullptr && canvasNode != nullptr &&
        contentWidth.has_value() && contentHeight.has_value()) {
        const core::Offset canvasOrigin =
            core::absoluteOffset(shell_.root(), "designer-canvas");
        const core::Offset contentOrigin =
            canvasOrigin +
            core::Offset{canvasNode->padding.left, canvasNode->padding.top};
        const core::Offset selectedOrigin =
            core::absoluteOffset(shell_.root(), selectedKey);
        selectedRect = core::Rect{
            selectedOrigin - contentOrigin,
            core::Size{selectedNode->size.width, selectedNode->size.height}};
    }

    // §8.8 网格点阵（独立开关）+ 图例/提示：预览之下（点阵是舞台背
    // 景，稿中 artboard 覆盖其上；图例/提示在其后单独压预览之上）。
    if (canvasNode != nullptr && contentWidth.has_value() &&
        contentHeight.has_value()) {
        const float width = *contentWidth;
        const float height = *contentHeight;
        if (canvasGuidesEnabled_) {
            const float zoom = canvasTransform_.zoom();
            const core::Offset pan = canvasTransform_.pan();
            auto designStep = 8.0F;
            while (designStep * zoom < 8.0F) designStep *= 2.0F;
            auto dotStep = designStep;
            while (dotStep * zoom < 16.0F) dotStep *= 2.0F;
            const float step = dotStep * zoom;
            const float gridOriginY = pan.y + kCanvasStageInset;
            const float firstY =
                std::ceil(-gridOriginY / step) * step + gridOriginY;
            int row = 0;
            for (float y = firstY; y <= height; y += step, ++row) {
                const float gridOriginX = pan.x + kCanvasStageInset;
                const float firstX =
                    std::ceil(-gridOriginX / step) * step + gridOriginX;
                int column = 0;
                for (float x = firstX; x <= width; x += step, ++column) {
                    children.push_back(position(
                        makeBar(2.0F, 2.0F, tokens.gridDot,
                                "designer-canvas-grid-dot:" +
                                    std::to_string(column) + ":" +
                                    std::to_string(row)),
                        core::Offset{x, y}));
                }
            }
        }

        // §8.8 图例（左下）与操作提示（右下）。
        auto legend = core::makeRow(
            {core::makeText("页面 / 容器", theme.typography.caption, {}, 0.0F,
                            "designer-canvas-legend-bounds"),
             core::makeText("选中", theme.typography.caption, {}, 0.0F,
                            "designer-canvas-legend-selection"),
             core::makeText("对齐", theme.typography.caption, {}, 0.0F,
                            "designer-canvas-legend-align")},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
            14.0F, core::EdgeInsets::symmetric(9.0F, 7.0F), {},
            "designer-canvas-legend");
        legend.styleOverrides.background = theme.colors.surfaceElevated;
        legend.styleOverrides.border = theme.colors.borderDefault;
        legend.styleOverrides.borderWidth = tokens.guideThickness;
        children.push_back(position(makeDecoration(std::move(legend)),
                                    core::Offset{14.0F, height - 34.0F}));
        auto hint = core::makeText(
            "Space + 拖动平移 · Ctrl + 滚轮缩放",
            theme.typography.caption, {}, 0.0F, "designer-canvas-hint");
        hint.styleOverrides.foreground = theme.colors.contentSecondary;
        children.push_back(position(makeDecoration(std::move(hint)),
                                    core::Offset{width - 210.0F,
                                                 height - 26.0F}));
    }


    // Apply the view transform at the canvas boundary. The model remains in
    // design units; only the materialized preview geometry moves and scales.
    scaleCanvasWidget(preview, canvasTransform_.zoom());
    preview = core::withStackPosition(
        std::move(preview),
        canvasTransform_.pan() +
            core::Offset{kCanvasStageInset, kCanvasStageInset});

    // The preview stays above passive guides so the canvas keeps normal hit
    // testing. Handles and the dimensions chip are the intentional interactive
    // chrome, so they are appended after the preview and can claim a drag.
    children.push_back(std::move(preview));
    // —— §8.8 标尺（常显）：刻度/标签按设计坐标随缩放与平移重建；
    // 步距 8 设计 px 起，屏幕间距不足 8px 时逐级翻倍（major/读数同步放
    // 大）；选中中心的 accent 标线跟随（ruler-mark）。——
    if (canvasNode != nullptr && contentWidth.has_value() &&
        contentHeight.has_value()) {
        const float width = *contentWidth;
        const float height = *contentHeight;
        const float zoom = canvasTransform_.zoom();
        const core::Offset pan = canvasTransform_.pan();
        auto designStep = 8.0F;
        while (designStep * zoom < 8.0F) designStep *= 2.0F;
        // Rulers are intentionally ordinary render widgets. They stay
        // outside the semantic/focus tree and the preview remains the
        // last child so selection and drag hit testing keep their order.
        children.push_back(position(
            makeBar(width, 20.0F, tokens.rulerSurface,
                    "designer-canvas-ruler-top"),
            core::Offset{0.0F, 0.0F}));
        children.push_back(position(
            makeBar(20.0F, height, tokens.rulerSurface,
                    "designer-canvas-ruler-left"),
            core::Offset{0.0F, 0.0F}));
        children.push_back(position(
            makeBar(20.0F, 20.0F, tokens.rulerSurface,
                    "designer-canvas-ruler-corner"),
            core::Offset{0.0F, 0.0F}));
        // 角标十字（⌖ 近似：两根短刻线）。
        children.push_back(position(
            makeBar(8.0F, tokens.guideThickness,
                    theme.colors.borderStrong,
                    "designer-canvas-ruler-corner-h"),
            core::Offset{6.0F, 9.5F}));
        children.push_back(position(
            makeBar(tokens.guideThickness, 8.0F,
                    theme.colors.borderStrong,
                    "designer-canvas-ruler-corner-v"),
            core::Offset{9.5F, 6.0F}));
        int tickIndex = 0;
        const float originX = pan.x + kCanvasStageInset;
        const float startX =
            std::floor(-originX / zoom / designStep) * designStep;
        const float endX =
            std::ceil((width - originX) / zoom / designStep) * designStep;
        for (float value = startX; value <= endX;
             value += designStep, ++tickIndex) {
            const float screen = originX + value * zoom;
            if (screen < -2.0F || screen > width + 2.0F) continue;
            const bool major =
                std::fmod(std::abs(value), designStep * 5.0F) < 0.01F;
            children.push_back(position(
                makeBar(tokens.guideThickness, major ? 8.0F : 4.0F,
                        major ? theme.colors.borderStrong : tokens.rulerTick,
                        "designer-canvas-ruler-tick-x:" +
                            std::to_string(tickIndex)),
                core::Offset{screen, 20.0F - (major ? 8.0F : 4.0F)}));
            if (std::fmod(std::abs(value), designStep * 10.0F) < 0.01F) {
                auto label = core::makeText(
                    std::to_string(static_cast<int>(value)),
                    theme.typography.caption, {}, 0.0F,
                    "designer-canvas-ruler-label-x:" +
                        std::to_string(tickIndex));
                label.styleOverrides.foreground = tokens.rulerTick;
                children.push_back(position(makeDecoration(std::move(label)),
                                            core::Offset{screen + 2.0F, 1.0F}));
            }
        }
        tickIndex = 0;
        const float originY = pan.y + kCanvasStageInset;
        const float startY =
            std::floor(-originY / zoom / designStep) * designStep;
        const float endY =
            std::ceil((height - originY) / zoom / designStep) * designStep;
        for (float value = startY; value <= endY;
             value += designStep, ++tickIndex) {
            const float screen = originY + value * zoom;
            if (screen < -2.0F || screen > height + 2.0F) continue;
            const bool major =
                std::fmod(std::abs(value), designStep * 5.0F) < 0.01F;
            children.push_back(position(
                makeBar(major ? 8.0F : 4.0F, tokens.guideThickness,
                        major ? theme.colors.borderStrong : tokens.rulerTick,
                        "designer-canvas-ruler-tick-y:" +
                            std::to_string(tickIndex)),
                core::Offset{20.0F - (major ? 8.0F : 4.0F), screen}));
            if (std::fmod(std::abs(value), designStep * 10.0F) < 0.01F) {
                auto label = core::makeText(
                    std::to_string(static_cast<int>(value)),
                    theme.typography.caption, {}, 0.0F,
                    "designer-canvas-ruler-label-y:" +
                        std::to_string(tickIndex));
                label.styleOverrides.foreground = tokens.rulerTick;
                children.push_back(position(makeDecoration(std::move(label)),
                                            core::Offset{1.0F, screen + 2.0F}));
            }
        }
        // 选中中心的标尺标线（accent，跨标尺全高/全宽）。
        if (selectedRect.has_value()) {
            const float markX =
                selectedRect->origin.x + selectedRect->size.width * 0.5F;
            const float markY =
                selectedRect->origin.y + selectedRect->size.height * 0.5F;
            children.push_back(position(
                makeBar(2.0F, 20.0F, tokens.handleBorder,
                        "designer-canvas-ruler-mark-x"),
                core::Offset{markX - 1.0F, 0.0F}));
            children.push_back(position(
                makeBar(20.0F, 2.0F, tokens.handleBorder,
                        "designer-canvas-ruler-mark-y"),
                core::Offset{0.0F, markY - 1.0F}));
        }
    }

    // —— §8.8 选中铬件（常显，不受开关控制）：中心十字（对齐开关）+
    // 选框 + 手柄 + 名字徽标 + 尺寸徽标 + 坐标徽标 ——
    if (selectedRect.has_value()) {
        float selectedX = selectedRect->origin.x;
        float selectedY = selectedRect->origin.y;
        float selectedWidth = selectedRect->size.width;
        float selectedHeight = selectedRect->size.height;
        if (canvasResizeActive_ &&
            canvasResizePreview_.id ==
                workbench_.selection().primary.value_or(0)) {
            selectedX = canvasResizeCoordinateOrigin_.x +
                        canvasResizePreview_.x * canvasResizeZoom_;
            selectedY = canvasResizeCoordinateOrigin_.y +
                        canvasResizePreview_.y * canvasResizeZoom_;
            selectedWidth = canvasResizePreview_.width * canvasResizeZoom_;
            selectedHeight = canvasResizePreview_.height * canvasResizeZoom_;
        }
        if (canvasAlignEnabled_) {
            auto vertical = makeBar(tokens.guideThickness, *contentHeight,
                                    tokens.guide, "designer-canvas-guide-v");
            children.push_back(position(
                std::move(vertical),
                core::Offset{selectedX + selectedWidth * 0.5F -
                                 tokens.guideThickness * 0.5F,
                             0.0F}));
            auto horizontal = makeBar(*contentWidth, tokens.guideThickness,
                                      tokens.guide, "designer-canvas-guide-h");
            children.push_back(position(
                std::move(horizontal),
                core::Offset{0.0F,
                             selectedY + selectedHeight * 0.5F -
                                 tokens.guideThickness * 0.5F}));
        }
        auto frame = core::makeContainerLeaf(
            selectedWidth + 12.0F, selectedHeight + 12.0F, {}, {},
            core::Color::transparent(), "designer-canvas-guide-frame");
        frame.styleOverrides.border = tokens.handleBorder;
        frame.styleOverrides.borderWidth = 2.0F;
        frame = makeDecoration(std::move(frame));
        children.push_back(position(
            std::move(frame), core::Offset{selectedX - 6.0F, selectedY - 6.0F}));

        const float handleSize = 8.0F;
        const std::vector<std::pair<std::string, core::Offset>> handles = {
            {"nw", {selectedX - 10.0F, selectedY - 10.0F}},
            {"n", {selectedX + selectedWidth * 0.5F - 4.0F,
                   selectedY - 10.0F}},
            {"ne", {selectedX + selectedWidth + 2.0F, selectedY - 10.0F}},
            {"e", {selectedX + selectedWidth + 2.0F,
                   selectedY + selectedHeight * 0.5F - 4.0F}},
            {"se", {selectedX + selectedWidth + 2.0F,
                    selectedY + selectedHeight + 2.0F}},
            {"s", {selectedX + selectedWidth * 0.5F - 4.0F,
                   selectedY + selectedHeight + 2.0F}},
            {"sw", {selectedX - 10.0F, selectedY + selectedHeight + 2.0F}},
            {"w", {selectedX - 10.0F,
                   selectedY + selectedHeight * 0.5F - 4.0F}},
        };
        for (const auto& [name, offset] : handles) {
            auto handle = core::makeContainerLeaf(
                handleSize, handleSize, {}, {}, tokens.handleFill,
                "designer-canvas-handle:" + name);
            handle = core::withOnClick(std::move(handle),
                                       "designer-canvas-handle:" + name);
            handle.styleOverrides.border = tokens.handleBorder;
            handle.styleOverrides.borderWidth = tokens.guideThickness;
            handle.radius = core::CornerRadius::all(2.0F);
            // 交互手柄不吃 makeDecoration 的指针透明：拖拽缩放/
            // 换位的抓点必须可命中。
            handle = makeDecoration(std::move(handle));
            handle.ignorePointer = false;
            foregroundDecorations.push_back(
                position(std::move(handle), offset));
        }

        // 名字徽标（accent 底"类型 · key"，选框上方）。
        std::string selectionLabel = "节点";
        if (workbench_.document().has_value()) {
            const auto primary = workbench_.selection().primary;
            if (primary.has_value()) {
                if (const auto* node = findDesignNode(
                        workbench_.document()->root, *primary);
                    node != nullptr) {
                    selectionLabel = node->type;
                    if (const auto keyIt = node->properties.find("key");
                        keyIt != node->properties.end()) {
                        if (const auto* keyName =
                                std::get_if<std::string>(
                                    &keyIt->second.value);
                            keyName != nullptr && !keyName->empty()) {
                            selectionLabel += " · " + *keyName;
                        }
                    }
                }
            }
        }
        auto nameChip = core::makeText(
            selectionLabel, theme.typography.caption, {}, 0.0F,
            "designer-canvas-selection-name", std::nullopt, std::nullopt,
            core::EdgeInsets::symmetric(6.0F, 3.0F));
        nameChip.styleOverrides.background = tokens.handleBorder;
        nameChip.styleOverrides.foreground = theme.colors.accentContent;
        foregroundDecorations.push_back(position(
            makeDecoration(std::move(nameChip)),
            core::Offset{selectedX - 2.0F, selectedY - 24.0F}));

        // 尺寸徽标（W × H，选框右下外侧）。
        auto dimensions = core::makeText(
            std::to_string(static_cast<int>(selectedWidth)) + " × " +
                std::to_string(static_cast<int>(selectedHeight)),
            theme.typography.caption, {}, 0.0F, "designer-canvas-dimensions",
            std::nullopt, std::nullopt,
            core::EdgeInsets::symmetric(6.0F, 3.0F));
        dimensions.styleOverrides.background = theme.colors.surfaceElevated;
        dimensions.styleOverrides.border = tokens.handleBorder;
        dimensions.styleOverrides.borderWidth = tokens.guideThickness;
        dimensions.styleOverrides.foreground = theme.colors.accentContent;
        foregroundDecorations.push_back(position(
            makeDecoration(std::move(dimensions)),
            core::Offset{selectedX + selectedWidth + 8.0F,
                         selectedY + selectedHeight + 7.0F}));

        // 坐标徽标（X · Y，十字交点右下；对齐开关）。
        if (canvasAlignEnabled_) {
            auto guideChip = core::makeText(
                "X " + std::to_string(static_cast<int>(selectedX)) + " · Y " +
                    std::to_string(static_cast<int>(selectedY)),
                theme.typography.caption, {}, 0.0F,
                "designer-canvas-guide-chip", std::nullopt, std::nullopt,
                core::EdgeInsets::symmetric(6.0F, 3.0F));
            guideChip.styleOverrides.background = tokens.guide;
            guideChip.styleOverrides.foreground = tokens.onGuide;
            foregroundDecorations.push_back(position(
                makeDecoration(std::move(guideChip)),
                core::Offset{selectedX + selectedWidth * 0.5F + 8.0F,
                             selectedY + selectedHeight * 0.5F + 9.0F}));
        }

        // §8.3 拖动换位虚线参考：原位虚线框 + 命中对齐时的全幅虚线
        //（4px 划/3px 隙的装饰条带，指针透明）。
        if (canvasResizeActive_ &&
            canvasResizeHandle_.rfind("drag:", 0) == 0) {
            const float zoom = canvasResizeZoom_;
            const float originX = canvasResizeCoordinateOrigin_.x;
            const float originY = canvasResizeCoordinateOrigin_.y;
            // 原位虚线框（拖动起点的矩形轮廓，四边）。
            const float startX = originX + canvasResizeStart_.x * zoom;
            const float startY = originY + canvasResizeStart_.y * zoom;
            const float startWidth = canvasResizeStart_.width * zoom;
            const float startHeight = canvasResizeStart_.height * zoom;
            dashRect(startX, startY, startWidth, startHeight,
                     "designer-canvas-drag-dash");
            // 对齐参考虚线：命中候选时全幅横/竖线。
            if (canvasResizeActiveGuideX_.has_value()) {
                dash(originX + *canvasResizeActiveGuideX_ * zoom, 0.0F,
                     *contentHeight, false, "designer-canvas-drag-dash", 4);
            }
            if (canvasResizeActiveGuideY_.has_value()) {
                dash(0.0F, originY + *canvasResizeActiveGuideY_ * zoom,
                     *contentWidth, true, "designer-canvas-drag-dash", 5);
            }
        }
    }
    for (auto& decoration : foregroundDecorations) {
        children.push_back(std::move(decoration));
    }
    return core::makeStack(std::move(children), core::StackAlignment::TopLeft,
                           {}, {}, "designer-canvas-stack", contentWidth,
                           contentHeight);
}

core::Widget DesignerApp::buildCanvasPanel() {
    // B 最大画布（variant-b .canvas-content padding 48，可专注；
    // workspace-design §3.1）。参考线（网格/标尺/选中框）默认开，菜单可
    // 关闭（§8.1 追加）。预览独立 Comfortable：ThemeScope 包裹，不随工作
    // 台 Compact 变化。
    const auto& theme = shell_.theme();
    core::Widget preview;
    if (workbench_.frame().hasFrame() && workbench_.document().has_value()) {
        auto previewFrame = workbench_.frame().widget();
        applyImageResources(previewFrame);
        preview = decoratePreview(
            std::move(previewFrame), workbench_.document()->root,
            workbench_.selection().ids);
        if (previewScopeData_ != nullptr) {
            preview = core::makeThemeScope(
                std::move(preview), previewScopeData_.get());
        }
    } else {
        preview = core::makeText("No valid preview frame",
                                 theme.typography.body);
    }
    auto canvas = core::makeContainer(
        buildCanvasStack(std::move(preview)), std::nullopt, std::nullopt,
        core::EdgeInsets::all(48.0F), {}, theme.colors.pageBackground,
        core::CornerRadius::all(theme.metrics.cardRadius), "designer-canvas");
    // 100% 缩放下画板可超出可视区：按画布边界裁剪，不越界遮挡右栏
    //（Widget.clipContent 显式声明，§8.6）。
    canvas.clipContent = true;
    canvas.flex = 1.0F;
    auto panel = core::makeColumn(
        {std::move(canvas)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 0.0F, {}, {},
        "designer-preview-canvas");
    panel.flex = 1.0F;
    panel.color = theme.colors.pageBackground;
    return panel;
}

core::Widget DesignerApp::buildSourcePanel() {
    const auto& theme = shell_.theme();
    const std::string source = currentSourceText();
    std::vector<core::Widget> lines;
    std::size_t lineNumber = 1;
    std::size_t begin = 0;
    while (begin <= source.size()) {
        const std::size_t end = source.find('\n', begin);
        const std::string line = source.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        auto number = core::makeText(std::to_string(lineNumber),
                                     theme.typography.caption, {}, 0.0F,
                                     "designer-source-line-number:" +
                                         std::to_string(lineNumber));
        number.width = 40.0F;
        auto content = core::makeText(
            line.empty() ? " " : line, theme.typography.caption, {}, 1.0F,
            "designer-source-line-content:" + std::to_string(lineNumber));
        auto row = core::makeRow(
            {std::move(number), std::move(content)},
            core::MainAxisAlignment::Start, core::CrossAxisAlignment::Start,
            8.0F, {}, {}, "designer-source-line:" +
                              std::to_string(lineNumber));
        if (sourceFocusLine_ == lineNumber) {
            row.styleOverrides.background = theme.colors.selectionBackground;
        }
        lines.push_back(std::move(row));
        if (end == std::string::npos) break;
        begin = end + 1;
        ++lineNumber;
    }
    if (lines.empty()) {
        lines.push_back(core::makeText("No source snapshot",
                                       theme.typography.body,
                                       {}, 0.0F, "designer-source-empty"));
    }
    auto sourceBody = core::makeColumn(
        std::move(lines), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 2.0F, {}, {},
        "designer-source-lines");
    auto scroll = core::makeScrollView(
        std::move(sourceBody), "designer-source-scroll", std::nullopt,
        std::nullopt, core::EdgeInsets::symmetric(12.0F, 8.0F));
    scroll.scrollAxis = core::ScrollAxis::Vertical;
    scroll.showScrollbar = true;
    scroll.scrollOffset = panelScrollOffset("designer-source-scroll");
    scroll.flex = 1.0F;
    auto heading = core::makeRow(
        {core::makeText("Source", theme.typography.label, {}, 0.0F,
                        "designer-source-heading"),
         core::makeText("read-only  /  " + sourceFile_,
                        theme.typography.caption, {}, 1.0F,
                        "designer-source-status")},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 8.0F);
    auto panel = core::makeColumn(
        {std::move(heading), std::move(scroll)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch,
        8.0F, core::EdgeInsets::all(12.0F), {}, "designer-source-panel");
    panel.flex = 1.0F;
    panel.color = theme.colors.surface;
    return panel;
}

core::Widget DesignerApp::buildReferencesPanel() {
    const auto& theme = shell_.theme();
    std::size_t missing = 0;
    for (const auto& row : referenceRows_) {
        if (row.status == "Missing") ++missing;
    }
    auto heading = core::makeRow(
        {core::makeText("References", theme.typography.label, {}, 0.0F,
                        "designer-references-heading"),
         core::makeText(std::to_string(referenceRows_.size()) + " refs  /  " +
                            std::to_string(missing) + " missing",
                        theme.typography.caption, {}, 1.0F,
                        "designer-references-status")},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 8.0F);
    auto grid = referencesController_.build();
    grid.key = "designer-references";
    grid.flex = 1.0F;
    auto panel = core::makeColumn(
        {std::move(heading), std::move(grid)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-references-panel");
    panel.flex = 1.0F;
    panel.color = theme.colors.surface;
    return panel;
}

core::Widget DesignerApp::buildDocumentTabs() {
    // B 文档页签 40（variant-b .document-tabs）：单文档 settings.design +
    // 右侧视图切换 设计/分屏/快照（HTML view-tabs）。引用已移至底部，
    // 中心不再有 References 页（workspace-design §3）。
    const auto& theme = shell_.theme();
    const bool dirty = workbench_.dirty();
    auto docTab = core::makeRow(
        {core::makeText("settings.design", theme.typography.caption, {}, 0.0F,
                        "designer-document-tab"),
         core::makeText(dirty ? "●" : "", theme.typography.caption, {}, 0.0F,
                        "designer-document-dirty")},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 8.0F,
        {}, {}, "designer-document-tab-row");
    auto designTab = core::makeButton(
        "设计", theme.typography.caption, {}, 0.0F, "designer-view-design",
        std::nullopt, std::nullopt, "designer:view-design");
    designTab.selected = centerView_ == CenterView::Design;
    auto splitTab = core::makeButton(
        "分屏", theme.typography.caption, {}, 0.0F, "designer-view-split",
        std::nullopt, std::nullopt, "designer:view-split");
    splitTab.selected = centerView_ == CenterView::Split;
    auto sourceTab = core::makeButton(
        "快照", theme.typography.caption, {}, 0.0F, "designer-view-source",
        std::nullopt, std::nullopt, "designer:view-source");
    sourceTab.selected = centerView_ == CenterView::Source;
    auto viewTabs = core::makeTabs({std::move(designTab), std::move(splitTab),
                                    std::move(sourceTab)},
                                   "designer-view-tabs");
    // 兼容旧中心页签 key：designer-center-tabs 保留在视图切换上，
    // designer-tab-* 保留为隐藏别名按钮（旧测试迁移期）？此处仅保留
    // designer-center-tabs 作为 view-tabs 的别名 key 语义，旧三页签由
    // designer:tab-* handler 映射到新视图（attach 已兼容）。
    viewTabs.key = "designer-center-tabs";
    auto spacer = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(),
        "designer-document-spacer");
    spacer.flex = 1.0F;
    spacer.excludeFromFocus = true;
    spacer.excludeFromSemantics = true;
    auto bar = core::makeRow(
        {std::move(docTab), std::move(spacer), std::move(viewTabs)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 8.0F,
        core::EdgeInsets::symmetric(12.0F, 0.0F), {}, "designer-document-tabs",
        std::nullopt, 40.0F);
    bar.color = theme.colors.surface;
    return bar;
}

core::Widget DesignerApp::buildCanvasControls() {
    // B 画布工具 48（variant-b .canvas-controls）：尺寸标签 + 网格/缩放/
    // 适合/环境就近放置（workspace-design §3）。缩放/网格 handler 与旧工具
    // 栏同 id，保证命令与快捷键不变。
    const auto& theme = shell_.theme();
    const auto iconCommand = [&theme](core::IconId icon, std::string key,
                                      std::string semantics,
                                      std::string handler) {
        auto button = core::withVariant(
            core::withLeadingIcon(
                core::makeButton({}, theme.typography.caption, {}, 0.0F,
                                 std::move(key), std::nullopt, std::nullopt,
                                 std::move(handler)),
                icon),
            core::ButtonVariant::Chrome);
        button.styleOverrides.padding = core::EdgeInsets::symmetric(6.0F, 4.0F);
        button.semanticsLabel = std::move(semantics);
        return button;
    };
    // §8.8：画板名义尺寸（文档根声明 width/height；缺省回退 720×500
    // 示例口径），单位 lp（逻辑设计像素）。
    std::string docSize = "720 × 500";
    if (workbench_.document().has_value()) {
        const auto readNumber = [&root = workbench_.document()->root](
                                    const char* name) -> float {
            const auto found = root.properties.find(name);
            if (found == root.properties.end()) return -1.0F;
            const auto* number = std::get_if<double>(&found->second.value);
            return number != nullptr ? static_cast<float>(*number) : -1.0F;
        };
        const float w = readNumber("width");
        const float h = readNumber("height");
        if (w > 0.0F && h > 0.0F) {
            docSize = std::to_string(static_cast<int>(w)) + " × " +
                      std::to_string(static_cast<int>(h));
        }
    }
    auto sizeLabel =
        core::makeText("桌面表单 · " + docSize + " lp",
                       theme.typography.caption, {}, 0.0F,
                       "designer-canvas-size-label");
    auto spacer = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(),
        "designer-canvas-controls-spacer");
    spacer.flex = 1.0F;
    spacer.excludeFromFocus = true;
    spacer.excludeFromSemantics = true;
    auto grid = iconCommand(core::IconId::Grid, "designer-canvas-guides",
                            canvasGuidesEnabled_ ? "隐藏网格" : "显示网格",
                            "designer:canvas-guides");
    grid.checked = canvasGuidesEnabled_;
    auto bounds = iconCommand(
        core::IconId::Maximize, "designer-canvas-bounds",
        canvasBoundsEnabled_ ? "隐藏边界" : "显示边界",
        "designer:canvas-bounds");
    bounds.checked = canvasBoundsEnabled_;
    auto align = iconCommand(
        core::IconId::Search, "designer-canvas-align",
        canvasAlignEnabled_ ? "隐藏对齐" : "显示对齐", "designer:canvas-align");
    align.checked = canvasAlignEnabled_;
    auto zoomOut = iconCommand(core::IconId::ZoomOut, "designer-zoom-out",
                               "缩小", "designer:zoom-out");
    zoomOut.excludeFromFocus = true;
    std::ostringstream zoomText;
    zoomText << static_cast<int>(canvasTransform_.zoom() * 100.0F + 0.5F)
             << "%";
    auto zoomLabel = core::makeText(zoomText.str(), theme.typography.caption,
                                    {}, 0.0F, "designer-zoom-label");
    auto zoomIn = iconCommand(core::IconId::ZoomIn, "designer-zoom-in", "放大",
                              "designer:zoom-in");
    zoomIn.excludeFromFocus = true;
    auto fit = iconCommand(core::IconId::Fit, "designer-zoom-fit", "适合",
                           "designer:zoom-fit");
    fit.excludeFromFocus = true;
    auto reset = iconCommand(core::IconId::Restore, "designer-zoom-reset",
                             "复位 100%", "designer:zoom-reset");
    reset.excludeFromFocus = true;
    // 预览环境低频组保留旧 handler（主题/密度/DPI/字号/对比/状态），
    // 就近放入画布工具（B 画布工具就近放缩放契约；命令 id 不变）。
    auto scaleLabel = [](const char* name, float scale) {
        std::ostringstream stream;
        stream << name << " " << static_cast<int>(scale * 100.0F + 0.5F)
               << "%";
        return stream.str();
    };
    auto themeButton = iconCommand(core::IconId::Moon, "designer-theme",
                                   darkMode_ ? "浅色" : "深色",
                                   "designer:theme");
    auto densityButton = iconCommand(core::IconId::Density, "designer-density",
                                     "密度", "designer:density");
    auto dpiButton = iconCommand(core::IconId::DeviceScale, "designer-dpi",
                                 scaleLabel("DPI", deviceScale_),
                                 "designer:dpi");
    auto fontButton =
        iconCommand(core::IconId::Font, "designer-font-scale",
                    scaleLabel("字号", fontScale_), "designer:font-scale");
    auto contrastButton = iconCommand(
        core::IconId::Contrast, "designer-contrast",
        highContrast_ ? "对比开" : "对比关", "designer:contrast");
    auto previewStateLabel = [&] {
        switch (previewStateMode_) {
            case PreviewStateMode::None:
                return "悬停态";
            case PreviewStateMode::Hovered:
                return "按压态";
            case PreviewStateMode::Pressed:
                return "聚焦态";
            case PreviewStateMode::Focused:
                return "清除态";
        }
        return "悬停态";
    }();
    auto previewStateButton = iconCommand(
        core::IconId::Cursor, "designer-preview-state", previewStateLabel,
        "designer:preview-state");
    auto env = iconCommand(core::IconId::Settings, "designer-environment",
                           "预览环境", "designer:preview-state");
    auto bar = core::makeRow(
        {std::move(sizeLabel), std::move(spacer), std::move(themeButton),
         std::move(densityButton), std::move(dpiButton), std::move(fontButton),
         std::move(contrastButton), std::move(previewStateButton),
         std::move(grid), std::move(bounds), std::move(align),
         std::move(zoomOut),
         std::move(zoomLabel), std::move(zoomIn), std::move(fit),
         std::move(reset), std::move(env)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 4.0F,
        core::EdgeInsets::symmetric(12.0F, 0.0F), {},
        "designer-canvas-controls", std::nullopt, 48.0F);
    bar.color = theme.colors.surface;
    return bar;
}

core::Widget DesignerApp::buildCrumbBar() {
    // B 面包屑 32（variant-b .crumb-bar）：共用 DocumentId 选择，
    // 点击返回容器（workspace-design §4.1）。
    const auto& theme = shell_.theme();
    std::vector<core::Widget> crumbs;
    if (workbench_.document().has_value() &&
        workbench_.selection().primary.has_value()) {
        const auto primary = *workbench_.selection().primary;
        // 祖先链：根→…→选中。
        std::vector<dsl::DesignNodeId> chain;
        dsl::DesignNodeId current = primary;
        for (int guard = 0; guard < 64; ++guard) {
            chain.push_back(current);
            if (current == workbench_.document()->root.id) break;
            const auto location =
                locateDesignNode(workbench_.document()->root, current);
            if (!location.has_value() || location->parent == 0) break;
            current = location->parent;
        }
        std::reverse(chain.begin(), chain.end());
        for (std::size_t i = 0; i < chain.size(); ++i) {
            if (i > 0) {
                crumbs.push_back(core::makeText(
                    "›", theme.typography.caption, {}, 0.0F,
                    "designer-crumb-separator:" + std::to_string(i)));
            }
            std::string label = std::to_string(chain[i]);
            if (const auto* node = findDesignNode(workbench_.document()->root,
                                                  chain[i]);
                node != nullptr) {
                const auto keyIt = node->properties.find("key");
                if (keyIt != node->properties.end()) {
                    if (const auto* key =
                            std::get_if<std::string>(&keyIt->second.value);
                        key != nullptr && !key->empty()) {
                        label = *key;
                    } else {
                        label = node->type;
                    }
                } else {
                    label = node->type;
                }
            }
            auto crumb = core::withVariant(
                core::makeButton(label, theme.typography.caption, {}, 0.0F,
                                 "designer-crumb:" + std::to_string(chain[i]),
                                 std::nullopt, std::nullopt,
                                 "designer:select:" + std::to_string(chain[i])),
                core::ButtonVariant::Chrome);
            crumb.styleOverrides.padding =
                core::EdgeInsets::symmetric(4.0F, 2.0F);
            crumbs.push_back(std::move(crumb));
        }
    } else {
        crumbs.push_back(core::makeText("选择一个控件，开始调整。",
                                        theme.typography.caption, {}, 0.0F,
                                        "designer-crumb-empty"));
    }
    auto bar = core::makeRow(
        std::move(crumbs), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Center, 4.0F,
        core::EdgeInsets::symmetric(12.0F, 0.0F), {}, "designer-crumb-bar",
        std::nullopt, 32.0F);
    bar.color = theme.colors.surface;
    return bar;
}

core::Widget DesignerApp::buildBottomDock() {
    // B 底栏（variant-b .bottom-dock）：默认收起 36 头标签，展开 36+108
    //（workspace-design §3.1）。问题/引用共用选择，冲突反馈不被折叠隐藏
    //（file-conflict §2）。
    const auto& theme = shell_.theme();
    for (auto it = shell_.handlers().begin(); it != shell_.handlers().end();) {
        if (it->first.starts_with("designer:diagnostic:")) {
            it = shell_.handlers().erase(it);
        } else {
            ++it;
        }
    }
    for (std::size_t index = 0; index < diagnostics_.size(); ++index) {
        if (diagnosticActionable(index)) {
            const std::string handler =
                "designer:diagnostic:" + std::to_string(index);
            shell_.handlers()[handler] = [this, index] {
                activateDiagnostic(index);
            };
        }
    }
    const auto count = diagnostics_.size();
    if (diagnosticsController_.itemCount() != count) {
        diagnosticsController_.setItemCount(count);
    }
    diagnosticsController_.setEstimatedExtent(
        theme.metrics.minHeight[theme.metrics.baseIndex]);
    std::size_t missingRefs = 0;
    for (const auto& row : referenceRows_) {
        if (row.status == "Missing") ++missingRefs;
    }
    auto diagTab = core::makeButton(
        "问题 " + std::to_string(diagnostics_.size()),
        theme.typography.caption, {}, 0.0F, "designer-bottom-diagnostics",
        std::nullopt, std::nullopt, "designer:bottom-diagnostics");
    diagTab.selected = bottomTab_ == BottomTab::Diagnostics;
    auto refTab = core::makeButton(
        "引用", theme.typography.caption, {}, 0.0F,
        "designer-bottom-references", std::nullopt, std::nullopt,
        "designer:bottom-references");
    refTab.selected = bottomTab_ == BottomTab::References;
    auto spacer = core::makeContainerLeaf(
        std::nullopt, std::nullopt, {}, {}, core::Color::transparent(),
        "designer-bottom-spacer");
    spacer.flex = 1.0F;
    spacer.excludeFromFocus = true;
    spacer.excludeFromSemantics = true;
    auto toggle = core::withVariant(
        core::withLeadingIcon(
            core::makeButton({}, theme.typography.caption, {}, 0.0F,
                             "designer-toggle-bottom", std::nullopt,
                             std::nullopt, "designer:toggle-bottom"),
            bottomOpen_ ? core::IconId::ChevronDown : core::IconId::ChevronUp),
        core::ButtonVariant::Chrome);
    toggle.semanticsLabel = bottomOpen_ ? "收起底部面板" : "展开底部面板";
    toggle.styleOverrides.padding = core::EdgeInsets::symmetric(6.0F, 4.0F);
    auto head = core::makeRow(
        {std::move(diagTab), std::move(refTab), std::move(spacer),
         std::move(toggle)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 4.0F,
        core::EdgeInsets::symmetric(8.0F, 0.0F), {}, "designer-bottom-head",
        std::nullopt, 36.0F);
    head.color = theme.colors.surface;
    std::vector<core::Widget> columnChildren;
    columnChildren.push_back(std::move(head));
    float dockHeight = 36.0F;
    if (bottomOpen_) {
        core::Widget body;
        if (bottomTab_ == BottomTab::References) {
            auto grid = referencesController_.build();
            grid.key = "designer-references";
            grid.flex = 1.0F;
            auto status = core::makeText(
                std::to_string(referenceRows_.size()) + " refs  /  " +
                    std::to_string(missingRefs) + " missing",
                theme.typography.caption, {}, 0.0F,
                "designer-references-status");
            body = core::makeColumn({std::move(status), std::move(grid)},
                                    core::MainAxisAlignment::Start,
                                    core::CrossAxisAlignment::Stretch, 4.0F,
                                    core::EdgeInsets::symmetric(12.0F, 8.0F),
                                    {}, "designer-references-panel");
            body.flex = 1.0F;
        } else {
            auto list = core::makeVirtualList(&diagnosticsController_,
                                              "designer-diagnostic-list");
            list.flex = 1.0F;
            std::vector<core::Widget> rows;
            if (saveConflict_.has_value()) {
                rows.push_back(core::makeText(
                    saveConflict_->project
                        ? "Project changed. Reload discards local edits; "
                          "overwrite replaces the manifest and every page."
                        : "External file changed. Reload discards local edits; "
                          "overwrite replaces external edits.",
                    theme.typography.caption, {}, 0.0F,
                    "designer-conflict-notice"));
                // 冲突操作（file-conflict §2）：重载/另存/覆盖常驻底栏，
                // 与折叠状态无关（收起时也在 head 下保留一行操作）。
                auto reload = core::withFocusRing(core::makeButton(
                    saveConflict_->project ? "Reload external project"
                                           : "Reload external file",
                    theme.typography.caption, {}, 0.0F,
                    "designer-conflict-reload", {}, {},
                    "designer:conflict-reload"));
                auto saveAs = core::withFocusRing(core::makeButton(
                    "Save as", theme.typography.caption, {}, 0.0F,
                    "designer-conflict-save-as", {}, {}, "designer:save-as"));
                auto overwrite = core::withFocusRing(core::withVariant(
                    core::makeButton(
                        saveConflict_->project ? "Overwrite external project"
                                               : "Overwrite external file",
                        theme.typography.caption, {}, 0.0F,
                        "designer-conflict-overwrite", {}, {},
                        "designer:conflict-overwrite"),
                    core::ButtonVariant::Danger));
                overwrite.enabled = saveConflict_->canOverwrite;
                {
                    const float gap = theme.metrics.controlGap[theme.metrics.baseIndex];
                    const float available = std::max(0.0F, shell_.view().width -
                        theme.metrics.controlPaddingX[theme.metrics.baseIndex] * 2.0F);
                    const float minColumnWidth =
                        theme.metrics.textFieldMinWidth[theme.metrics.baseIndex] * 2.0F;
                    const int columns = std::clamp(
                        static_cast<int>((available + gap) / (minColumnWidth + gap)), 1, 3);
                    rows.push_back(core::makeGrid(
                        {std::move(reload), std::move(saveAs),
                         std::move(overwrite)},
                        columns, minColumnWidth, gap, gap,
                        "designer-conflict-actions"));
                }
            }
            rows.push_back(std::move(list));
            body = core::makeColumn(
                std::move(rows), core::MainAxisAlignment::Start,
                core::CrossAxisAlignment::Stretch, 4.0F,
                core::EdgeInsets::symmetric(12.0F, 8.0F), {},
                "designer-bottom-body-inner");
            body.flex = 1.0F;
        }
        body.color = theme.colors.surface;
        columnChildren.push_back(std::move(body));
        dockHeight = 144.0F;
    } else if (saveConflict_.has_value()) {
        // 冲突反馈不能被普通折叠隐藏（file-conflict 契约）：收起时仍保留
        // 提示 + 三操作（重载/另存/覆盖），高度 60→96。
        auto notice = core::makeText(
            "文件冲突：展开底部面板处理重载/另存/覆盖。",
            theme.typography.caption, {}, 0.0F, "designer-conflict-notice");
        columnChildren.push_back(std::move(notice));
        auto reload = core::withFocusRing(core::makeButton(
            saveConflict_->project ? "Reload external project"
                                   : "Reload external file",
            theme.typography.caption, {}, 0.0F, "designer-conflict-reload", {},
            {}, "designer:conflict-reload"));
        auto saveAs = core::withFocusRing(core::makeButton(
            "Save as", theme.typography.caption, {}, 0.0F,
            "designer-conflict-save-as", {}, {}, "designer:save-as"));
        auto overwrite = core::withFocusRing(core::withVariant(
            core::makeButton(
                saveConflict_->project ? "Overwrite external project"
                                       : "Overwrite external file",
                theme.typography.caption, {}, 0.0F,
                "designer-conflict-overwrite", {}, {},
                "designer:conflict-overwrite"),
            core::ButtonVariant::Danger));
        overwrite.enabled = saveConflict_->canOverwrite;
        {
            const float gap = theme.metrics.controlGap[theme.metrics.baseIndex];
            const float available = std::max(0.0F, shell_.view().width -
                theme.metrics.controlPaddingX[theme.metrics.baseIndex] * 2.0F);
            const float minColumnWidth =
                theme.metrics.textFieldMinWidth[theme.metrics.baseIndex] * 2.0F;
            const int columns = std::clamp(
                static_cast<int>((available + gap) / (minColumnWidth + gap)), 1, 3);
            columnChildren.push_back(core::makeGrid(
                {std::move(reload), std::move(saveAs), std::move(overwrite)},
                columns, minColumnWidth, gap, gap, "designer-conflict-actions"));
        }
        dockHeight = 144.0F;
    }
    auto dock = core::makeColumn(std::move(columnChildren),
                                 core::MainAxisAlignment::Start,
                                 core::CrossAxisAlignment::Stretch, 0.0F, {},
                                 {}, "designer-diagnostics", std::nullopt,
                                 dockHeight);
    dock.color = theme.colors.surface;
    return dock;
}

core::Widget DesignerApp::buildStatusBar() {
    // B 状态栏 28（variant-b .statusbar，statusbar-design §6/§9）：保存/
    // 选择/反馈统一显示，不混入临时预览值（workspace-design §3）。
    const auto& theme = shell_.theme();
    const std::string saveState =
        workbench_.dirty() ? "未保存修改" : "未修改";
    const std::string nodeStat =
        std::to_string(workbench_.document().has_value()
                            ? workbench_.document()->root.children.size() + 1
                            : 0) +
        " 个节点";
    const std::string message = statusMessage_.empty()
                                  ? ("就绪  /  " + sourceFile_)
                                  : statusMessage_;
    auto ok = core::makeText("就绪", theme.typography.caption, {}, 0.0F,
                             "designer-status-ok");
    auto msg = core::makeText(message, theme.typography.caption, {}, 0.0F,
                              "designer-status");
    msg.flex = 1.0F;
    auto nodes = core::makeText(nodeStat, theme.typography.caption, {}, 0.0F,
                                "designer-node-stat");
    auto saved = core::makeText(saveState, theme.typography.caption, {}, 0.0F,
                                "designer-save-state");
    auto utf8 = core::makeText("UTF-8", theme.typography.caption, {}, 0.0F,
                               "designer-encoding");
    auto densityLabel = core::makeText("Compact", theme.typography.caption, {},
                                       0.0F, "designer-density-label");
    auto bar = core::makeRow(
        {std::move(ok), std::move(msg), std::move(nodes), std::move(saved),
         std::move(utf8), std::move(densityLabel)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 12.0F,
        core::EdgeInsets::symmetric(12.0F, 0.0F), {}, "designer-statusbar",
        std::nullopt, 28.0F);
    bar.color = theme.colors.surface;
    return bar;
}

core::Widget DesignerApp::buildCenterPanel() {
    // B 中心列（workspace-design §3）：文档页签 40 + 画布工具 48 +
    // 编辑区（画布/分屏/快照）+ 面包屑 32 + 底栏（36/144）。
    core::Widget editor;
    if (centerView_ == CenterView::Source) {
        editor = buildSourcePanel();
        editor.flex = 1.0F;
    } else if (centerView_ == CenterView::Split) {
        auto canvas = buildCanvasPanel();
        canvas.flex = 1.0F;
        auto source = buildSourcePanel();
        source.flex = 1.0F;
        editor = core::makeRow({std::move(canvas), std::move(source)},
                               core::MainAxisAlignment::Start,
                               core::CrossAxisAlignment::Stretch, 0.0F, {}, {},
                               "designer-editor-surface");
        editor.flex = 1.0F;
    } else {
        editor = buildCanvasPanel();
        editor.flex = 1.0F;
    }
    auto panel = core::makeColumn(
        {buildDocumentTabs(), buildCanvasControls(), std::move(editor),
         buildCrumbBar(), buildBottomDock()},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 0.0F,
        {}, {}, "designer-preview-panel");
    panel.flex = 1.0F;
    panel.color = shell_.theme().colors.pageBackground;
    return panel;
}

core::Widget DesignerApp::buildWorkspace() {
    // B 工作区网格（workspace-design §3.1）：48 rail（固定外置）+ 224 left +
    // 6 + center + 6 + 280 right；两个嵌套 Splitter（splitter §7/§9），左下限
    // 180、右下限 232、上限 400，Compact 12 步进，双击复位。专注/折叠经 bool
    // 条件构建（§4.4），不触碰 dirty/undo。Rail 置于分栏之外，拖动直接改变
    // 左栏（tight 约束覆盖固定宽），避免 Row 内固定子吞掉增量。
    if (zenMode_) {
        auto center = buildCenterPanel();
        auto workspace = core::makeRow({std::move(center)},
                                       core::MainAxisAlignment::Start,
                                       core::CrossAxisAlignment::Stretch, 0.0F,
                                       {}, {}, "designer-workspace");
        workspace.flex = 1.0F;
        return workspace;
    }
    auto rail = buildRail();
    // 右栏固定偏好、中心吃增量（B 画布优先）：按当前视口与左栏推导内部分栏
    // 可用长，恢复右偏好宽（默认 280，拖动回填 232–400）。左上限 400 显式钳制。
    const float viewWidth = shell_.view().width;
    if (viewWidth > 0.0F) {
        if (leftSplitter_.offset() > 400.0F) {
            leftSplitter_.setOffset(400.0F);
        } else if (leftSplitter_.offset() < 180.0F) {
            leftSplitter_.setOffset(180.0F);
        }
        rightWidthPreference_ =
            std::clamp(rightWidthPreference_, 232.0F, 400.0F);
        constexpr float kRail = 48.0F;
        constexpr float kTrack = 6.0F;
        const float leftWidth =
            leftCollapsed_ ? 0.0F
                           : (leftSplitter_.seeded() ? leftSplitter_.offset()
                                                     : 224.0F);
        const float tracks = leftCollapsed_ ? kTrack : 2.0F * kTrack;
        const float innerMainMax =
            std::max(0.0F, viewWidth - kRail - leftWidth - tracks - kTrack);
        // 内部分栏 mainMax = 内宽 - 轨道；期望中心 = mainMax - 右偏好。
        const float innerWidth = leftCollapsed_
                                     ? std::max(0.0F, viewWidth - kRail)
                                     : std::max(
                                           0.0F, viewWidth - kRail - leftWidth -
                                                     kTrack);
        const float mainMax = std::max(0.0F, innerWidth - kTrack);
        (void)innerMainMax;
        const float desiredCenter = mainMax - rightWidthPreference_;
        if (std::isfinite(desiredCenter)) {
            // 程序恢复偏好时屏蔽回填（extent 为旧值，回填会污染偏好；
            // 用户拖动经交互通道回填，不受影响）。同时放宽 minTrailing
            // 以容纳增长（setOffset 按旧 extent 钳制，resize 放大会被旧
            // max 误杀；布局期再按新 extent 钳制回合法域）。
            auto callback = std::move(rightSplitter_.onOffsetChanged);
            rightSplitter_.onOffsetChanged = nullptr;
            rightSplitter_.setMinTrailing(0.0F);
            rightSplitter_.setOffset(desiredCenter);
            rightSplitter_.setMinTrailing(232.0F);
            rightSplitter_.onOffsetChanged = std::move(callback);
        }
    }
    auto center = buildCenterPanel();
    auto right = buildPropertiesPanel();
    auto inner =
        core::makeSplitter(&rightSplitter_, std::move(center), std::move(right),
                           true, "designer-right-splitter");
    inner.flex = 1.0F;
    core::Widget main;
    if (leftCollapsed_) {
        main = std::move(inner);
    } else {
        auto leftdock = buildOutlinePanel();
        main = core::makeSplitter(&leftSplitter_, std::move(leftdock),
                                  std::move(inner), true,
                                  "designer-left-splitter");
        main.flex = 1.0F;
    }
    auto workspace = core::makeRow(
        {std::move(rail), std::move(main)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 0.0F, {}, {}, "designer-workspace");
    workspace.flex = 1.0F;
    return workspace;
}

core::Widget DesignerApp::buildPreviewPanel() {
    // 兼容入口：旧 Canvas/Source/References 中心页已收敛为 B 中心
    // （文档页签 + 视图 Design/Split/Source，引用下沉到底部）。
    return buildCenterPanel();
}

core::Widget DesignerApp::buildPropertiesPanel() {
    // B 右栏 280 检查器（variant-b .inspector）：属性头 + 选中摘要 +
    // 基本/布局/外观分组（workspace-design §4.2）。类型化输入（Spin/
    // ComboBox/Checkbox）为后续应用层接线项，此版先分组呈现，值通道保持
    // TextField 兼容（designer-property-field key 不变）。
    const auto& theme = shell_.theme();
    std::vector<core::Widget> rows;
    auto head = core::makeRow(
        {core::makeText("属性", theme.typography.label, {}, 0.0F,
                        "designer-properties-heading"),
         core::makeText("当前选择", theme.typography.caption, {}, 0.0F,
                        "designer-properties-count")},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center, 8.0F,
        {}, {}, "designer-properties-head");
    rows.push_back(std::move(head));
    if (workbench_.selection().primary.has_value() &&
        workbench_.document().has_value()) {
        const auto sel = *workbench_.selection().primary;
        if (const auto* selNode =
                findDesignNode(workbench_.document()->root, sel);
            selNode != nullptr) {
            std::string keyLabel = selNode->type;
            if (const auto keyIt = selNode->properties.find("key");
                keyIt != selNode->properties.end()) {
                if (const auto* key =
                        std::get_if<std::string>(&keyIt->second.value);
                    key != nullptr && !key->empty()) {
                    keyLabel = *key;
                }
            }
            auto icon = core::makeIcon(core::IconId::NavControls,
                                       "designer-selected-icon", 18.0F,
                                       18.0F);
            auto summary = core::makeRow(
                {std::move(icon),
                 core::makeColumn(
                     {core::makeText(selNode->type, theme.typography.label, {},
                                     0.0F, "designer-selected-type"),
                      core::makeText(keyLabel + " · #" + std::to_string(sel),
                                     theme.typography.caption, {}, 0.0F,
                                     "designer-selected-key")},
                     core::MainAxisAlignment::Start,
                     core::CrossAxisAlignment::Start, 2.0F, {}, {},
                     "designer-selected-text")},
                core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
                12.0F, {}, {}, "designer-selected-summary");
            rows.push_back(std::move(summary));
        }
    }
    const auto selected = workbench_.selection().primary;
    if (selected.has_value()) {
        auto properties = workbench_.properties(*selected);
        if (workbench_.document().has_value()) {
            const auto* node = findDesignNode(workbench_.document()->root,
                                               *selected);
            const auto* schema = node == nullptr
                                     ? nullptr
                                     : dsl::findNodeSchema(node->type);
            if (schema != nullptr && !schema->isComponent &&
                !isL0ToolboxType(node->type)) {
                // L1/L2 nodes can be created from the schema registry. Show
                // their declaration defaults even before the user overrides
                // a field, while keeping the L0 property API unchanged.
                for (const auto& spec : schema->properties) {
                    if (spec.persistence !=
                            dsl::PropertyPersistence::Declaration ||
                        !isEditableProperty(spec.defaultValue) ||
                        std::any_of(properties.begin(), properties.end(),
                                    [&spec](const auto& property) {
                                        return property.name == spec.name;
                                    })) {
                        continue;
                    }
                    properties.push_back(
                        dsl::DesignPreviewProperty{spec.name, spec.defaultValue,
                                                    std::nullopt});
                }

                const auto addSourceReference =
                    [&properties, schema](std::string_view name) {
                        const auto* spec =
                            dsl::findPropertySpec(*schema, name);
                        if (spec == nullptr ||
                            spec->persistence !=
                                dsl::PropertyPersistence::RuntimeReference ||
                            std::any_of(
                                properties.begin(), properties.end(),
                                [name](const auto& property) {
                                    return property.name == name;
                                })) {
                            return;
                        }
                        properties.push_back(dsl::DesignPreviewProperty{
                            std::string{name}, std::nullopt, std::string{}});
                    };
                if (node->type == "VirtualList" || node->type == "List" ||
                    node->type == "Tree" || node->type == "TreeList") {
                    addSourceReference("virtualSource");
                } else if (node->type == "Splitter") {
                    addSourceReference("splitterSource");
                } else if (node->type == "ThemeScope") {
                    addSourceReference("theme");
                }
            }
        }
        if (workbench_.selection().ids.size() > 1) {
            const auto selectedIds = workbench_.selection().ids;
            properties.erase(
                std::remove_if(
                    properties.begin(), properties.end(),
                    [this, &selectedIds](
                        const dsl::DesignPreviewProperty& property) {
                        if (!property.value.has_value() ||
                            !isEditableProperty(*property.value)) {
                            return true;
                        }
                        for (const auto id : selectedIds) {
                            const auto candidates = workbench_.properties(id);
                            const auto found = std::find_if(
                                candidates.begin(), candidates.end(),
                                [&property](
                                    const dsl::DesignPreviewProperty& candidate) {
                                    return candidate.name == property.name &&
                                           candidate.value.has_value() &&
                                           isEditableProperty(*candidate.value) &&
                                           candidate.value->value.index() ==
                                               property.value->value.index();
                                });
                            if (found == candidates.end()) return true;
                        }
                        return false;
                    }),
                properties.end());
        }
        std::vector<core::Widget> basicRows;
        std::vector<core::Widget> layoutRows;
        std::vector<core::Widget> appearanceRows;
        const auto isLayoutProp = [](std::string_view name) {
            return name == "width" || name == "height" || name == "spacing" ||
                   name == "padding" || name == "mainAxis" ||
                   name == "crossAxis" || name == "alignment";
        };
        for (const auto& property : properties) {
            std::vector<core::Widget>* target = &basicRows;
            if (property.reference.has_value()) {
                target = &appearanceRows;
            } else if (isLayoutProp(property.name)) {
                target = &layoutRows;
            }
            if (property.reference.has_value()) {
                registerReferenceBinding(*selected, property);
                const std::string bind =
                    referenceBindingKey(*selected, property.name);
                auto field = core::makeTextField(
                    {}, "reference", theme.typography.body, {}, 0.0F,
                    "designer-reference-field:" +
                        std::to_string(*selected) + ":" + property.name,
                    168.0F, std::nullopt, bind);
                field.semanticsLabel = property.name + " reference";
                field.flex = 1.0F;
                auto row = core::makeRow(
                    {core::makeText(property.name, theme.typography.caption,
                                    {}, 0.0F,
                                    "designer-reference-label:" +
                                        property.name),
                     std::move(field)},
                    core::MainAxisAlignment::Start,
                    core::CrossAxisAlignment::Center, 8.0F, {}, {},
                    "designer-reference-row:" + std::to_string(*selected) +
                        ":" + property.name);
                target->push_back(std::move(row));
            } else if (property.value.has_value() &&
                isEditableProperty(*property.value)) {
                registerPropertyBinding(*selected, property);
                const std::string bind =
                    propertyBindingKey(*selected, property.name);
                // 布尔量用 Checkbox 表达（workspace-design §4.2；HTML 基本组
                // “默认选中”同口径），其余标量保持 TextField 兼容通道。
                if (std::holds_alternative<bool>(property.value->value)) {
                    const bool checked =
                        std::get<bool>(property.value->value);
                    auto box = core::makeCheckbox(
                        property.name, bind,
                        "designer-property-field:" +
                            std::to_string(*selected) + ":" + property.name,
                        checked);
                    box.semanticsLabel = property.name;
                    auto row = core::makeRow(
                        {std::move(box)}, core::MainAxisAlignment::Start,
                        core::CrossAxisAlignment::Center, 8.0F, {}, {},
                        "designer-property-row:" + std::to_string(*selected) +
                            ":" + property.name);
                    target->push_back(std::move(row));
                } else {
                    auto field = core::makeTextField(
                        {}, "value", theme.typography.body, {}, 0.0F,
                        "designer-property-field:" +
                            std::to_string(*selected) + ":" + property.name,
                        168.0F, std::nullopt, bind);
                    field.semanticsLabel = property.name;
                    field.flex = 1.0F;
                    auto row = core::makeRow(
                        {core::makeText(property.name,
                                        theme.typography.caption, {}, 0.0F,
                                        "designer-property-label:" +
                                            property.name),
                         std::move(field)},
                        core::MainAxisAlignment::Start,
                        core::CrossAxisAlignment::Center, 8.0F, {}, {},
                        "designer-property-row:" + std::to_string(*selected) +
                            ":" + property.name);
                    target->push_back(std::move(row));
                }
            } else {
                target->push_back(core::makeText(
                    property.name + "  " +
                        formatValue(property.value, property.reference),
                    theme.typography.body, {}, 0.0F,
                    "designer-property:" + property.name));
            }
        }
        const auto groupTitle = [&theme](std::string text, std::string key) {
            return core::makeText(std::move(text), theme.typography.caption, {},
                                  0.0F, std::move(key));
        };
        if (!basicRows.empty()) {
            rows.push_back(groupTitle("基本", "designer-prop-group-basic"));
            for (auto &r : basicRows) rows.push_back(std::move(r));
        }
        if (!layoutRows.empty()) {
            rows.push_back(groupTitle("布局", "designer-prop-group-layout"));
            for (auto &r : layoutRows) rows.push_back(std::move(r));
        }
        if (!appearanceRows.empty()) {
            rows.push_back(
                groupTitle("外观与引用", "designer-prop-group-appearance"));
            for (auto &r : appearanceRows) rows.push_back(std::move(r));
        }
        if (basicRows.empty() && layoutRows.empty() && appearanceRows.empty()) {
            rows.push_back(groupTitle("基本", "designer-prop-group-basic"));
        }
    } else {
        rows.push_back(core::makeText("Select a node", theme.typography.body));
    }
    auto propertyBody = core::makeColumn(
        std::move(rows), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F);
    auto propertyScroll = core::makeScrollView(
        std::move(propertyBody), "designer-properties-scroll", std::nullopt,
        std::nullopt, {});
    propertyScroll.scrollAxis = core::ScrollAxis::Vertical;
    propertyScroll.showScrollbar = true;
    propertyScroll.scrollOffset =
        panelScrollOffset("designer-properties-scroll");
    propertyScroll.flex = 1.0F;
    auto panel = core::makeColumn(
        {std::move(propertyScroll)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-properties-panel", 280.0F);
    panel.color = theme.colors.surface;
    return panel;
}

core::Widget DesignerApp::buildDiagnosticRow(std::size_t index) {
    const auto& theme = shell_.theme();
    const auto& diagnostic = diagnostics_.at(index);
    const std::string key = "designer-diagnostic:" + std::to_string(index);
    if (!diagnosticActionable(index)) {
        return core::makeText(formatDiagnostic(diagnostic),
                              theme.typography.caption, {}, 0.0F, key);
    }
    auto row = core::makeButton(
        formatDiagnostic(diagnostic), theme.typography.caption, {}, 0.0F,
        key, std::nullopt, std::nullopt,
        "designer:diagnostic:" + std::to_string(index));
    row = core::withVariant(std::move(row), core::ButtonVariant::Ghost);
    row = core::withFocusRing(std::move(row));
    return row;
}

core::Widget DesignerApp::buildDiagnosticsPanel() {
    // 兼容入口：旧全局诊断条已收敛为 B 中心底栏（36/144 可折叠）。
    return buildBottomDock();
}

core::Widget DesignerApp::buildBody() {
    // B 工作区主体：Splitter 外壳（workspace-design §3.1）。
    return buildWorkspace();
}

core::Widget DesignerApp::buildUi() {
    rebuildPreviewKeys();
    syncImageResources();
    syncSelectionFromOutline();
    // B 整壳（designer-ide.html variant-b）：标题 44 + 主工具 48 +
    // 工作区 + 状态 28。视觉值取 Theme token（visual-system §3.6）。
    // 菜单 popover / 运行预览以 Stack 覆盖层承载（HTML .popover/modal 同位，
    // Esc 关闭；运行预览仅会话中出现）。进 Stack 时根列显式取视口尺寸，
    // 避免松约束下收缩（Stack 子按内容测量）。
    auto workspace = buildWorkspace();
    auto rootColumn = core::makeColumn(
        {buildTitleBar(), buildMainTools(), std::move(workspace),
         buildStatusBar()},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 0.0F,
        {}, {}, "designer-root-column");
    rootColumn.color = shell_.theme().colors.pageBackground;
    std::vector<core::Widget> overlays;
    if (openMenu_.has_value()) overlays.push_back(buildMenuPopover());
    if (runPreviewOpen_) overlays.push_back(buildRunPreviewOverlay());
    if (overlays.empty()) {
        rootColumn.key = "designer-root";
        return rootColumn;
    }
    const float viewWidth = shell_.view().width;
    const float viewHeight = shell_.view().height;
    if (viewWidth > 0.0F) rootColumn.width = viewWidth;
    if (viewHeight > 0.0F) rootColumn.height = viewHeight;
    std::vector<core::Widget> stackKids;
    stackKids.push_back(std::move(rootColumn));
    for (auto& overlay : overlays) stackKids.push_back(std::move(overlay));
    auto root = core::makeStack(std::move(stackKids),
                                core::StackAlignment::TopLeft, {}, {},
                                "designer-root");
    return root;
}

core::Widget DesignerApp::buildPreviewWindow() {
    syncImageResources();
    if (previewSessionMode_ == PreviewSessionMode::Stopped ||
        !workbench_.frame().hasFrame()) {
        return core::makeContainer(
            core::makeText(statusMessage_.empty() ? "Preview stopped"
                                                    : statusMessage_,
                           {}, {}, 0.0F, "designer-preview-status"),
            std::nullopt, std::nullopt, core::EdgeInsets::all(32.0F), {},
            core::Color::transparent(), core::CornerRadius::zero(),
            "designer-preview-window");
    }
    auto preview = workbench_.frame().widget();
    applyImageResources(preview);
    if (preview.key.empty()) preview.key = "designer-preview-root";
    return preview;
}

}  // namespace lumen::designer_app
