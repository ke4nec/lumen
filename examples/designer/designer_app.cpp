#include "designer_app.h"

#include <algorithm>
#include <array>
#include <cctype>
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
#include "lumen/widgets/splitter.h"

namespace lumen::designer_app {
namespace {

constexpr char kSampleSource[] =
    "page preview { Column(key: \"root\") {"
    " Text(\"Preview title\", key: \"title\")"
    " Button(\"Save\", key: \"save\")"
    " Row(key: \"status\") { Text(\"Ready\", key: \"message\") }"
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

const dsl::DesignPreviewOutlineNode* findOutlineNode(
    const dsl::DesignPreviewOutlineNode& node, dsl::DesignNodeId id) {
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
        case dsl::DesignReferenceKind::Theme:
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
        shell_.commands().registerCommand(std::move(ctrlSpec));
        app::CommandSpec guiSpec;
        guiSpec.id = id + ".gui";
        guiSpec.binding = app::KeyBinding::plain(key, gui);
        guiSpec.invoke = [action = std::move(action)](app::AppShell&) {
            action();
        };
        shell_.commands().registerCommand(std::move(guiSpec));
    };
    const auto historyEnabled = [this] {
        return !shell_.controller().composingActive();
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
        const std::string& focused = shell.focus().focusedKey();
        const bool editingProperty =
            focused.starts_with("designer-property-field:") ||
            focused.starts_with("designer-reference-field:");
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
        if (key == core::Key::Escape &&
            (self->outlineDragActive_ || self->toolboxDragActive_ ||
             self->canvasResizeActive_ ||
             shell.controller().dragSessionActive())) {
            shell.pointerCancel();
            return true;
        }
        if (key == core::Key::Escape &&
            self->navigatorPreviewController_.handleBack(false)) {
            self->statusMessage_ =
                "Route: " + self->navigatorPreviewController_.current();
            shell.markDirty();
            return true;
        }
        if (!editingProperty && key == core::Key::Delete &&
            (modifiers & core::kModifierAlt) == 0) {
            self->removeSelectedNode();
            return true;
        }
        if ((focused == "designer-outline" ||
             focused.starts_with("designer-outline:")) &&
            self->outlineController_.handleKey(key, modifiers, keyChar)) {
            return true;
        }
        return false;
    };
    config.onWheel = [self](const core::RenderNode&, const core::RenderNode*,
                            core::Offset position, core::Offset delta) {
        const auto* canvas =
            core::findNodeByKey(self->shell_.root(), "designer-canvas");
        if (canvas != nullptr) {
            const auto origin =
                core::absoluteOffset(self->shell_.root(), canvas->key);
            if (core::Rect{origin, canvas->size}.contains(position)) {
                self->panCanvas(delta);
                return true;
            }
        }
        return self->spinPreviewController_.handleWheel(self->shell_, position,
                                                        delta);
    };
    config.onAnimate = [self](app::AppShell& shell, std::uint64_t nowMs) {
        return self->spinPreviewController_.step(shell, nowMs);
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
            (void)workbench_.selectNode(*id);
            applyPreviewState();
            shell_.markDirty();
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
            canvasResizeSession(phase, position, sourceKey);
            outlineDragSession(phase, position, sourceKey);
            canvasSelectionSession(phase, position, sourceKey);
            toolboxDragSession(phase, position, sourceKey);
        });
    shell_.handlers()["designer:theme"] = [this] { toggleTheme(); };
    shell_.handlers()["designer:density"] = [this] { cycleDensity(); };
    shell_.handlers()["designer:dpi"] = [this] { cycleDpi(); };
    shell_.handlers()["designer:zoom-in"] =
        [this] { adjustCanvasZoom(1.1F); };
    shell_.handlers()["designer:zoom-out"] =
        [this] { adjustCanvasZoom(1.0F / 1.1F); };
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
    shell_.handlers()["designer:tab-canvas"] = [this] {
        centerTab_ = CenterTab::Canvas;
        shell_.markDirty();
    };
    shell_.handlers()["designer:tab-source"] = [this] {
        centerTab_ = CenterTab::Source;
        shell_.markDirty();
    };
    shell_.handlers()["designer:tab-references"] = [this] {
        centerTab_ = CenterTab::References;
        shell_.markDirty();
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
    shell_.markDirty();
    previewShell_.markDirty();
    return true;
}

void DesignerApp::stopPreview() {
    if (previewSessionMode_ == PreviewSessionMode::Stopped) return;
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
    const auto parentId = workbench_.selection().primary;
    if (!parentId || !workbench_.document().has_value()) return;
    const auto* parent = findDesignNode(workbench_.document()->root, *parentId);
    if (parent == nullptr) return;
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
        while (node.children.size() < schema->minChildren) {
            dsl::DesignNode child;
            child.type = "Container";
            node.children.push_back(std::move(child));
        }
    }
    if (workbench_.insertNode(*parentId, parent->children.size(),
                              std::move(node))) {
        refreshDocumentUi();
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
        const auto selected = workbench_.selection().primary;
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
        shell_.markDirty();
        return;
    }
    if (!canvasResizeActive_) return;
    if (!workbench_.document().has_value() ||
        workbench_.document()->documentId != canvasResizeDocumentId_ ||
        workbench_.documentRevision() != canvasResizeRevision_ ||
        workbench_.selection().primary != canvasResizeId_) {
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
        if (!canvasResizePositionEditable_) {
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
    shell_.setTheme(
        style::Theme::fromSettings(settings, darkMode_, density_));
    previewShell_.setTheme(style::Theme::fromSettings(
        previewShell_.accessibilitySettings(), darkMode_, density_));
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

void DesignerApp::adjustCanvasZoom(float factor) {
    if (!std::isfinite(factor) || factor <= 0.0F) return;
    const float next = std::clamp(canvasTransform_.zoom() * factor, 0.5F, 2.0F);
    if (next == canvasTransform_.zoom()) return;
    shell_.pointerCancel();
    (void)canvasTransform_.setZoom(next);
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
    if (selected.contains(node.id)) {
        widget.selected = true;
        widget.styleOverrides.background = shell_.theme().colors.selectionBackground;
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

core::Widget DesignerApp::buildToolbar() {
    const auto& theme = shell_.theme();
    auto scaleLabel = [](const char* name, float scale) {
        std::ostringstream stream;
        stream << name << " " << static_cast<int>(scale * 100.0F + 0.5F)
               << "%";
        return stream.str();
    };
    auto themeButton = core::makeButton(
        darkMode_ ? "Light" : "Dark", theme.typography.label, {}, 0.0F,
        "designer-theme", 88.0F, std::nullopt, "designer:theme");
    auto densityButton = core::makeButton(
        "Density", theme.typography.label, {}, 0.0F, "designer-density",
        96.0F, std::nullopt, "designer:density");
    auto dpiButton = core::makeButton(
        scaleLabel("DPI", deviceScale_), theme.typography.label, {}, 0.0F,
        "designer-dpi", 88.0F, std::nullopt, "designer:dpi");
    auto fontButton = core::makeButton(
        scaleLabel("Font", fontScale_), theme.typography.label, {}, 0.0F,
        "designer-font-scale", 96.0F, std::nullopt, "designer:font-scale");
    auto contrastButton = core::makeButton(
        highContrast_ ? "Contrast on" : "Contrast off", theme.typography.label,
        {}, 0.0F, "designer-contrast", 112.0F, std::nullopt,
        "designer:contrast");
    auto previewButton = core::makeButton(
        [&] {
            switch (previewStateMode_) {
                case PreviewStateMode::None:
                    return "Hover state";
                case PreviewStateMode::Hovered:
                    return "Press state";
                case PreviewStateMode::Pressed:
                    return "Focus state";
                case PreviewStateMode::Focused:
                    return "Clear state";
            }
            return "Hover state";
        }(),
        theme.typography.label, {}, 0.0F, "designer-preview-state", 128.0F,
        std::nullopt, "designer:preview-state");
    auto guidesButton = core::withLeadingIcon(
        core::makeButton(canvasGuidesEnabled_ ? "Guides on" : "Guides off",
                         theme.typography.label, {}, 0.0F,
                         "designer-canvas-guides", 104.0F, std::nullopt,
                         "designer:canvas-guides"),
        core::IconId::Grid);
    guidesButton.selected = canvasGuidesEnabled_;
    auto runButton = core::withLeadingIcon(
        core::makeButton("Run", theme.typography.label, {}, 0.0F,
                         "designer-run", 72.0F, std::nullopt,
                         "designer:run"),
        core::IconId::Play);
    auto debugButton = core::withLeadingIcon(
        core::makeButton("Debug", theme.typography.label, {}, 0.0F,
                         "designer-debug", 88.0F, std::nullopt,
                         "designer:debug"),
        core::IconId::Grid);
    auto stopButton = core::makeButton(
        "Stop", theme.typography.label, {}, 0.0F, "designer-stop", 72.0F,
        std::nullopt, "designer:stop");
    runButton.selected = previewSessionMode_ == PreviewSessionMode::Running;
    debugButton.selected =
        previewSessionMode_ == PreviewSessionMode::Debugging;
    stopButton.enabled = previewSessionMode_ != PreviewSessionMode::Stopped;
    auto addTextButton = core::makeButton(
        "Add Text", theme.typography.label, {}, 0.0F, "designer-add-text",
        96.0F, std::nullopt, "designer:add-text");
    auto duplicateButton = core::makeButton(
        "Duplicate", theme.typography.label, {}, 0.0F,
        "designer-duplicate", 104.0F, std::nullopt, "designer:duplicate");
    auto removeButton = core::makeButton(
        "Delete", theme.typography.label, {}, 0.0F, "designer-remove", 80.0F,
        std::nullopt, "designer:remove");
    auto moveUpButton = core::makeButton(
        "Move up", theme.typography.label, {}, 0.0F, "designer-move-up",
        88.0F, std::nullopt, "designer:move-up");
    auto moveDownButton = core::makeButton(
        "Move down", theme.typography.label, {}, 0.0F, "designer-move-down",
        96.0F, std::nullopt, "designer:move-down");
    auto newProjectButton = core::withLeadingIcon(
        core::makeButton("New project", theme.typography.label, {}, 0.0F,
                         "designer-new-project", 116.0F, std::nullopt,
                         "designer:new-project"),
        core::IconId::Plus);
    auto openButton = core::withLeadingIcon(
        core::makeButton("Open", theme.typography.label, {}, 0.0F,
                         "designer-open", 78.0F, std::nullopt,
                         "designer:open"),
        core::IconId::Folder);
    auto saveButton = core::withLeadingIcon(
        core::makeButton("Save", theme.typography.label, {}, 0.0F,
                         "designer-save", 78.0F, std::nullopt,
                         "designer:save"),
        core::IconId::Document);
    auto saveAsButton = core::withLeadingIcon(
        core::makeButton("Save as", theme.typography.label, {}, 0.0F,
                         "designer-save-as", 92.0F, std::nullopt,
                         "designer:save-as"),
        core::IconId::Document);
    auto zoomOutButton = core::makeButton(
        "Zoom -", theme.typography.label, {}, 0.0F, "designer-zoom-out",
        84.0F, std::nullopt, "designer:zoom-out");
    zoomOutButton.excludeFromFocus = true;
    auto zoomInButton = core::makeButton(
        "Zoom +", theme.typography.label, {}, 0.0F, "designer-zoom-in",
        84.0F, std::nullopt, "designer:zoom-in");
    zoomInButton.excludeFromFocus = true;
    auto zoomResetButton = core::makeButton(
        scaleLabel("Zoom", canvasTransform_.zoom()), theme.typography.label,
        {}, 0.0F, "designer-zoom-reset", 96.0F, std::nullopt,
        "designer:zoom-reset");
    zoomResetButton.excludeFromFocus = true;
    auto title = core::makeText("Lumen Designer  /  D3 Editor",
                                theme.typography.title, {}, 1.0F,
                                "designer-title");
    auto toolbar = core::makeRow(
        {std::move(title), std::move(themeButton), std::move(densityButton),
         std::move(dpiButton), std::move(fontButton),
         std::move(contrastButton), std::move(previewButton),
         std::move(guidesButton),
         std::move(runButton), std::move(debugButton), std::move(stopButton),
         std::move(addTextButton), std::move(duplicateButton),
         std::move(removeButton), std::move(moveUpButton),
         std::move(moveDownButton), std::move(newProjectButton),
         std::move(openButton),
         std::move(saveButton), std::move(saveAsButton),
         std::move(zoomOutButton), std::move(zoomInButton),
         std::move(zoomResetButton)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
        8.0F, core::EdgeInsets::symmetric(16.0F, 8.0F), {}, "designer-toolbar",
        std::nullopt, 56.0F);
    toolbar.color = theme.colors.surface;
    return toolbar;
}

core::Widget DesignerApp::buildToolboxPanel() {
    const auto& theme = shell_.theme();
    const auto schemas = designerToolboxSchemas();
    std::vector<core::Widget> rows;
    for (std::size_t index = 0; index < schemas.size(); index += 3) {
        std::vector<core::Widget> buttons;
        for (std::size_t offset = 0;
             offset < 3 && index + offset < schemas.size();
             ++offset) {
            const auto* schema = schemas[index + offset];
            buttons.push_back(core::makeButton(
                schema->type, theme.typography.caption, {}, 1.0F,
                "designer-toolbox:" + schema->type, std::nullopt,
                std::nullopt, "designer:toolbox:" + schema->type));
        }
        rows.push_back(core::makeRow(
            std::move(buttons), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 4.0F));
    }
    auto heading = core::makeText("Toolbox", theme.typography.label, {}, 0.0F,
                                  "designer-toolbox-heading");
    auto toolboxBody = core::makeColumn(
        std::move(rows), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 4.0F);
    auto toolboxScroll = core::makeScrollView(
        std::move(toolboxBody), "designer-toolbox-scroll", std::nullopt,
        240.0F, {});
    toolboxScroll.scrollAxis = core::ScrollAxis::Vertical;
    toolboxScroll.showScrollbar = true;
    return core::makeColumn(
        {std::move(heading), std::move(toolboxScroll)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 6.0F,
        {}, {}, "designer-toolbox");
}

core::Widget DesignerApp::buildProjectPanel() {
    const auto& theme = shell_.theme();
    auto heading = core::makeText("Project", theme.typography.label, {}, 0.0F,
                                  "designer-project-heading");
    std::vector<core::Widget> rows;
    if (!project_.has_value()) {
        rows.push_back(core::makeText("No project", theme.typography.caption, {},
                                      0.0F, "designer-project-empty"));
    } else {
        for (const auto& page : project_->pages) {
            const std::string handler = "designer:project-page:" + page.documentId;
            shell_.handlers()[handler] = [this, id = page.documentId] {
                (void)switchProjectDocument(id);
            };
            auto row = core::makeButton(
                page.pageName.empty() ? page.documentId : page.pageName,
                theme.typography.caption, {}, 0.0F,
                "designer-project-page:" + page.documentId, std::nullopt,
                std::nullopt, handler);
            row.selected = page.documentId == activeProjectDocumentId_;
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
    }
    auto body = core::makeColumn(std::move(rows), core::MainAxisAlignment::Start,
                                 core::CrossAxisAlignment::Stretch, 4.0F);
    return core::makeColumn({std::move(heading), std::move(body)},
                            core::MainAxisAlignment::Start,
                            core::CrossAxisAlignment::Stretch, 6.0F, {}, {},
                            "designer-project-panel");
}

core::Widget DesignerApp::buildOutlinePanel() {
    const auto& theme = shell_.theme();
    auto heading = core::makeText("Outline", theme.typography.label, {}, 0.0F,
                                  "designer-outline-heading");
    auto tree = core::makeTree(&outlineController_, "designer-outline-view",
                               std::nullopt, std::nullopt);
    tree.flex = 1.0F;
    auto panel = core::makeColumn(
        {buildProjectPanel(), buildToolboxPanel(), std::move(heading),
         std::move(tree)},
        core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-outline-panel", 240.0F);
    panel.color = theme.colors.surfaceSunken;
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
    if (canvasNode != nullptr) {
        contentWidth = std::max(
            0.0F, canvasNode->size.width - canvasNode->padding.horizontal());
        contentHeight = std::max(
            0.0F, canvasNode->size.height - canvasNode->padding.vertical());
    }

    const auto makeDecoration = [](core::Widget widget) {
        widget.excludeFromSemantics = true;
        widget.excludeFromFocus = true;
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

    if (canvasGuidesEnabled_ && canvasNode != nullptr) {
        const float width = contentWidth.value_or(0.0F);
        const float height = contentHeight.value_or(0.0F);
        if (width > 0.0F && height > 0.0F) {
            // Keep the point field sparse enough for a retained widget tree;
            // the 40px sampling still communicates the designer grid while
            // leaving the frozen 8px snap threshold to the future engine.
            for (float y = 0.0F; y <= height; y += 40.0F) {
                for (float x = 0.0F; x <= width; x += 40.0F) {
                    children.push_back(position(
                        makeBar(2.0F, 2.0F, tokens.gridDot,
                                "designer-canvas-grid-dot:" +
                                    std::to_string(static_cast<int>(x)) +
                                    ":" + std::to_string(static_cast<int>(y))),
                        core::Offset{x, y}));
                }
            }
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
            for (float x = 0.0F; x <= width; x += 40.0F) {
                children.push_back(position(
                    makeBar(tokens.guideThickness, 8.0F, tokens.rulerTick,
                            "designer-canvas-ruler-tick-x:" +
                                std::to_string(static_cast<int>(x))),
                    core::Offset{x, 12.0F}));
            }
            for (float y = 0.0F; y <= height; y += 40.0F) {
                children.push_back(position(
                    makeBar(8.0F, tokens.guideThickness, tokens.rulerTick,
                            "designer-canvas-ruler-tick-y:" +
                                std::to_string(static_cast<int>(y))),
                    core::Offset{12.0F, y}));
            }
            for (float x = 0.0F; x <= width; x += 80.0F) {
                auto label = core::makeText(
                    std::to_string(static_cast<int>(x)),
                    theme.typography.caption, {}, 0.0F,
                    "designer-canvas-ruler-label-x:" +
                        std::to_string(static_cast<int>(x)));
                label.styleOverrides.foreground = tokens.rulerTick;
                children.push_back(position(makeDecoration(std::move(label)),
                                            core::Offset{x + 2.0F, 1.0F}));
            }
            for (float y = 40.0F; y <= height; y += 80.0F) {
                auto label = core::makeText(
                    std::to_string(static_cast<int>(y)),
                    theme.typography.caption, {}, 0.0F,
                    "designer-canvas-ruler-label-y:" +
                        std::to_string(static_cast<int>(y)));
                label.styleOverrides.foreground = tokens.rulerTick;
                children.push_back(position(makeDecoration(std::move(label)),
                                            core::Offset{1.0F, y + 2.0F}));
            }
        }

        std::string selectedKey;
        if (const auto primary = workbench_.selection().primary;
            primary.has_value()) {
            if (const auto outline = workbench_.outline(); outline.has_value()) {
                if (const auto* selected =
                        findOutlineNode(*outline, *primary);
                    selected != nullptr) {
                    selectedKey = previewKeyForNodeId(*primary);
                }
            }
        }
        const auto selectedId = workbench_.selection().primary;
        const auto* selectedNode = selectedKey.empty()
                                       ? nullptr
                                       : core::findNodeByKey(shell_.root(),
                                                             selectedKey);
        if (selectedNode != nullptr && canvasNode != nullptr &&
            contentWidth.has_value() && contentHeight.has_value()) {
            const core::Offset canvasOrigin =
                core::absoluteOffset(shell_.root(), "designer-canvas");
            const core::Offset contentOrigin =
                canvasOrigin +
                core::Offset{canvasNode->padding.left,
                             canvasNode->padding.top};
            const core::Offset selectedOrigin =
                core::absoluteOffset(shell_.root(), selectedKey);
            const core::Offset local{selectedOrigin.x - contentOrigin.x,
                                     selectedOrigin.y - contentOrigin.y};
            float selectedX = local.x;
            float selectedY = local.y;
            float selectedWidth = selectedNode->size.width;
            float selectedHeight = selectedNode->size.height;
            if (canvasResizeActive_ && selectedId.has_value() &&
                canvasResizePreview_.id == *selectedId) {
                selectedX = canvasResizeCoordinateOrigin_.x +
                            canvasResizePreview_.x * canvasResizeZoom_;
                selectedY = canvasResizeCoordinateOrigin_.y +
                            canvasResizePreview_.y * canvasResizeZoom_;
                selectedWidth = canvasResizePreview_.width * canvasResizeZoom_;
                selectedHeight = canvasResizePreview_.height * canvasResizeZoom_;
            }
            if (selectedWidth > 0.0F && selectedHeight > 0.0F) {
                auto vertical = makeBar(tokens.guideThickness, *contentHeight,
                                        tokens.guide, "designer-canvas-guide-v");
                children.push_back(position(
                    std::move(vertical),
                    core::Offset{selectedX + selectedWidth * 0.5F -
                                     tokens.guideThickness * 0.5F,
                                 0.0F}));
                auto horizontal = makeBar(*contentWidth, tokens.guideThickness,
                                          tokens.guide,
                                          "designer-canvas-guide-h");
                children.push_back(position(
                    std::move(horizontal),
                    core::Offset{0.0F,
                                 selectedY + selectedHeight * 0.5F -
                                     tokens.guideThickness * 0.5F}));

                auto frame = core::makeContainerLeaf(
                    selectedWidth + 12.0F, selectedHeight + 12.0F, {}, {},
                    core::Color::transparent(),
                    "designer-canvas-guide-frame");
                frame.styleOverrides.border = tokens.guide;
                frame.styleOverrides.borderWidth = tokens.guideThickness;
                frame = makeDecoration(std::move(frame));
                children.push_back(position(
                    std::move(frame),
                    core::Offset{selectedX - 6.0F, selectedY - 6.0F}));

                const float handleSize = 8.0F;
                const std::vector<std::pair<std::string, core::Offset>> handles = {
                    {"nw", {selectedX - 10.0F, selectedY - 10.0F}},
                    {"n", {selectedX + selectedWidth * 0.5F - 4.0F,
                            selectedY - 10.0F}},
                    {"ne", {selectedX + selectedWidth + 2.0F,
                             selectedY - 10.0F}},
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
                    handle = core::withOnClick(
                        std::move(handle), "designer-canvas-handle:" + name);
                    handle.styleOverrides.border = tokens.handleBorder;
                    handle.styleOverrides.borderWidth = tokens.guideThickness;
                    foregroundDecorations.push_back(
                        position(makeDecoration(std::move(handle)), offset));
                }

                auto dimensions = core::makeText(
                    "X " + std::to_string(static_cast<int>(local.x)) +
                        "  Y " + std::to_string(static_cast<int>(local.y)) +
                        "  W " +
                        std::to_string(static_cast<int>(selectedWidth)) +
                        "  H " +
                        std::to_string(static_cast<int>(selectedHeight)),
                    theme.typography.caption, {}, 0.0F,
                    "designer-canvas-dimensions", std::nullopt, std::nullopt,
                    core::EdgeInsets::symmetric(6.0F, 3.0F));
                dimensions.styleOverrides.background = tokens.guide;
                dimensions.styleOverrides.foreground = tokens.onGuide;
                foregroundDecorations.push_back(
                    position(makeDecoration(std::move(dimensions)),
                             core::Offset{selectedX,
                                          selectedY + selectedHeight + 14.0F}));
            }
        }
    }

    // Apply the view transform at the canvas boundary. The model remains in
    // design units; only the materialized preview geometry moves and scales.
    scaleCanvasWidget(preview, canvasTransform_.zoom());
    preview = core::withStackPosition(std::move(preview),
                                      canvasTransform_.pan());

    // The preview stays above passive guides so the canvas keeps normal hit
    // testing. Handles and the dimensions chip are the intentional interactive
    // chrome, so they are appended after the preview and can claim a drag.
    children.push_back(std::move(preview));
    for (auto& decoration : foregroundDecorations) {
        children.push_back(std::move(decoration));
    }
    return core::makeStack(std::move(children), core::StackAlignment::TopLeft,
                           {}, {}, "designer-canvas-stack", contentWidth,
                           contentHeight);
}

core::Widget DesignerApp::buildCanvasPanel() {
    const auto& theme = shell_.theme();
    core::Widget preview;
    if (workbench_.frame().hasFrame() && workbench_.document().has_value()) {
        auto previewFrame = workbench_.frame().widget();
        applyImageResources(previewFrame);
        preview = decoratePreview(
            std::move(previewFrame), workbench_.document()->root,
            workbench_.selection().ids);
    } else {
        preview = core::makeText("No valid preview frame",
                                 theme.typography.body);
    }
    auto canvas = core::makeContainer(
        buildCanvasStack(std::move(preview)), std::nullopt, std::nullopt,
        core::EdgeInsets::all(24.0F), {}, theme.colors.pageBackground,
        core::CornerRadius::all(theme.metrics.cardRadius), "designer-canvas");
    canvas.flex = 1.0F;
    auto heading = core::makeText("Canvas", theme.typography.label, {}, 0.0F,
                                  "designer-canvas-heading");
    auto panel = core::makeColumn(
        {std::move(heading), std::move(canvas)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-preview-canvas");
    panel.flex = 1.0F;
    panel.color = theme.colors.surface;
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

core::Widget DesignerApp::buildPreviewPanel() {
    const auto& theme = shell_.theme();
    auto canvasTab = core::makeButton(
        "Canvas", theme.typography.label, {}, 0.0F, "designer-tab-canvas",
        96.0F, std::nullopt, "designer:tab-canvas");
    canvasTab.selected = centerTab_ == CenterTab::Canvas;
    auto sourceTab = core::makeButton(
        "Source", theme.typography.label, {}, 0.0F, "designer-tab-source",
        96.0F, std::nullopt, "designer:tab-source");
    sourceTab.selected = centerTab_ == CenterTab::Source;
    auto referencesTab = core::makeButton(
        "References", theme.typography.label, {}, 0.0F,
        "designer-tab-references", 112.0F, std::nullopt,
        "designer:tab-references");
    referencesTab.selected = centerTab_ == CenterTab::References;
    auto tabs = core::makeTabs(
        {std::move(canvasTab), std::move(sourceTab), std::move(referencesTab)},
        "designer-center-tabs");

    core::Widget content;
    if (centerTab_ == CenterTab::References) {
        content = buildReferencesPanel();
    } else if (centerTab_ == CenterTab::Source) {
        content = buildSourcePanel();
    } else {
        content = buildCanvasPanel();
    }
    content.flex = 1.0F;
    auto panel = core::makeColumn(
        {std::move(tabs), std::move(content)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 0.0F, {}, {},
        "designer-preview-panel");
    panel.flex = 1.0F;
    panel.color = theme.colors.surface;
    return panel;
}

core::Widget DesignerApp::buildPropertiesPanel() {
    const auto& theme = shell_.theme();
    std::vector<core::Widget> rows;
    rows.push_back(core::makeText("Properties", theme.typography.label, {}, 0.0F,
                                  "designer-properties-heading"));
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
        for (const auto& property : properties) {
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
                rows.push_back(std::move(row));
            } else if (property.value.has_value() &&
                isEditableProperty(*property.value)) {
                registerPropertyBinding(*selected, property);
                const std::string bind =
                    propertyBindingKey(*selected, property.name);
                auto field = core::makeTextField(
                    {}, "value", theme.typography.body, {}, 0.0F,
                    "designer-property-field:" + std::to_string(*selected) +
                        ":" + property.name,
                    168.0F, std::nullopt, bind);
                field.semanticsLabel = property.name;
                field.flex = 1.0F;
                auto row = core::makeRow(
                    {core::makeText(property.name, theme.typography.caption,
                                    {}, 0.0F,
                                    "designer-property-label:" + property.name),
                     std::move(field)},
                    core::MainAxisAlignment::Start,
                    core::CrossAxisAlignment::Center, 8.0F, {}, {},
                    "designer-property-row:" + std::to_string(*selected) +
                        ":" + property.name);
                rows.push_back(std::move(row));
            } else {
                rows.push_back(core::makeText(
                    property.name + "  " +
                        formatValue(property.value, property.reference),
                    theme.typography.body, {}, 0.0F,
                    "designer-property:" + property.name));
            }
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
    propertyScroll.flex = 1.0F;
    auto panel = core::makeColumn(
        {std::move(propertyScroll)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-properties-panel", 280.0F);
    panel.color = theme.colors.surfaceSunken;
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
    const auto& theme = shell_.theme();
    const std::string statusText =
        statusMessage_.empty()
            ? (diagnostics_.empty()
                   ? "Ready  /  " + sourceFile_
                   : "Diagnostics  /  " + sourceFile_)
            : statusMessage_;
    auto status = core::makeText(statusText, theme.typography.caption, {},
                                 0.0F, "designer-status");
    for (auto it = shell_.handlers().begin(); it != shell_.handlers().end();) {
        if (it->first.starts_with("designer:diagnostic:")) {
            it = shell_.handlers().erase(it);
        } else {
            ++it;
        }
    }
    for (std::size_t index = 0; index < diagnostics_.size();
         ++index) {
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
    auto list = core::makeVirtualList(&diagnosticsController_,
                                      "designer-diagnostic-list");
    list.flex = 1.0F;
    std::vector<core::Widget> rows;
    rows.push_back(std::move(status));
    float panelHeight = 96.0F;
    if (saveConflict_.has_value()) {
        // Designer conflict design §2: text states both consequences, while
        // ordinary Theme button variants and focus rings carry the actions.
        const auto index = theme.metrics.baseIndex;
        rows.push_back(core::makeText(
            saveConflict_->project
                ? "Project changed. Reload discards local edits; overwrite replaces the manifest and every page."
                : "External file changed. Reload discards local edits; overwrite replaces external edits.",
            theme.typography.caption, {}, 0.0F, "designer-conflict-notice"));
        auto reload = core::withFocusRing(core::makeButton(
            saveConflict_->project ? "Reload external project" : "Reload external file",
            theme.typography.label, {}, 0.0F,
            "designer-conflict-reload", {}, {}, "designer:conflict-reload"));
        auto saveAs = core::withFocusRing(core::makeButton(
            "Save as", theme.typography.label, {}, 0.0F,
            "designer-conflict-save-as", {}, {}, "designer:save-as"));
        auto overwrite = core::withFocusRing(core::withVariant(core::makeButton(
            saveConflict_->project ? "Overwrite external project" : "Overwrite external file",
            theme.typography.label, {}, 0.0F,
            "designer-conflict-overwrite", {}, {}, "designer:conflict-overwrite"),
            core::ButtonVariant::Danger));
        overwrite.enabled = saveConflict_->canOverwrite;
        const float gap = theme.metrics.controlGap[index];
        const float available = std::max(0.0F, shell_.view().width -
            theme.metrics.controlPaddingX[index] * 2.0F);
        const float minColumnWidth = theme.metrics.textFieldMinWidth[index] * 2.0F;
        const int columns = std::clamp(
            static_cast<int>((available + gap) / (minColumnWidth + gap)), 1, 3);
        rows.push_back(core::makeGrid(
            {std::move(reload), std::move(saveAs), std::move(overwrite)},
            columns, minColumnWidth, gap, gap, "designer-conflict-actions"));
        const int actionRows = (3 + columns - 1) / columns;
        panelHeight += theme.metrics.minHeight[index] * static_cast<float>(actionRows) +
                       theme.typography.caption.fontSize * 2.0F +
                       gap * static_cast<float>(actionRows + 1);
    }
    rows.push_back(std::move(list));
    auto panel = core::makeColumn(
        std::move(rows), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 4.0F,
        core::EdgeInsets::symmetric(12.0F, 8.0F), {}, "designer-diagnostics",
        std::nullopt, panelHeight);
    panel.color = theme.colors.surfaceSunken;
    return panel;
}

core::Widget DesignerApp::buildBody() {
    auto body = core::makeRow(
        {buildOutlinePanel(), buildPreviewPanel(), buildPropertiesPanel()},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 1.0F,
        core::EdgeInsets::all(8.0F), {}, "designer-body");
    body.flex = 1.0F;
    auto content = core::makeColumn({std::move(body), buildDiagnosticsPanel()},
                                    core::MainAxisAlignment::Start,
                                    core::CrossAxisAlignment::Stretch, 0.0F,
                                    {}, {}, "designer-content");
    content.flex = 1.0F;
    return content;
}

core::Widget DesignerApp::buildUi() {
    rebuildPreviewKeys();
    syncImageResources();
    syncSelectionFromOutline();
    auto root = core::makeColumn({buildToolbar(), buildBody()},
                                 core::MainAxisAlignment::Start,
                                 core::CrossAxisAlignment::Stretch, 0.0F, {}, {},
                                 "designer-root");
    root.color = shell_.theme().colors.pageBackground;
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
