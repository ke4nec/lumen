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
    if (name.empty()) return false;
    // The designer can display bindings, handlers, and image names without
    // owning their application objects. Typed source handles remain missing
    // until the embedding application registers a real adapter.
    switch (kind) {
        case dsl::DesignReferenceKind::Binding:
        case dsl::DesignReferenceKind::Handler:
        case dsl::DesignReferenceKind::Image:
            out = dsl::DesignReference{kind, name, {}, nullptr};
            return true;
        case dsl::DesignReferenceKind::Theme:
        case dsl::DesignReferenceKind::VirtualSource:
        case dsl::DesignReferenceKind::SplitterSource:
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
}

void DesignerApp::setResourceRoot(std::filesystem::path root) {
    for (const auto& [uri, handle] : imageResources_) {
        (void)uri;
        resourceManager_->release(handle);
    }
    imageResources_.clear();
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
        const bool ctrlLike =
            (modifiers & (core::kModifierCtrl | core::kModifierGui)) != 0;
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
            self->navigatorPreviewController_.handleBack(false)) {
            self->statusMessage_ =
                "Route: " + self->navigatorPreviewController_.current();
            shell.markDirty();
            return true;
        }
        if (ctrlLike && (modifiers & core::kModifierAlt) == 0) {
            const char lower = static_cast<char>(
                std::tolower(static_cast<unsigned char>(keyChar)));
            if (lower == 'z') {
                (void)self->undo();
                return true;
            }
            if (lower == 'y') {
                (void)self->redo();
                return true;
            }
            if (lower == 'o') {
                self->requestOpenFile();
                return true;
            }
            if (lower == 'n' && (modifiers & core::kModifierShift) != 0) {
                self->requestNewProjectFile();
                return true;
            }
            if (!editingProperty && lower == 'c') {
                self->copySelectedNode();
                return true;
            }
            if (!editingProperty && lower == 'v') {
                self->pasteCopiedNode();
                return true;
            }
            if (lower == 's') {
                if ((modifiers & core::kModifierShift) != 0) {
                    self->requestSaveAsFile();
                } else {
                    self->requestSaveFile();
                }
                return true;
            }
            if (!editingProperty && lower == 'r') {
                (void)self->startPreview(false);
                return true;
            }
            if (!editingProperty && lower == 'd') {
                (void)self->startPreview(true);
                return true;
            }
            if (!editingProperty && key == core::Key::Up) {
                self->moveSelectedNode(-1);
                return true;
            }
            if (!editingProperty && key == core::Key::Down) {
                self->moveSelectedNode(1);
                return true;
            }
        }
        if (!editingProperty && key == core::Key::Delete &&
            (modifiers & core::kModifierAlt) == 0) {
            self->removeSelectedNode();
            return true;
        }
        if (self->outlineController_.handleKey(key, modifiers, keyChar)) {
            return true;
        }
        return false;
    };
    config.onWheel = [self](const core::RenderNode&, const core::RenderNode*,
                            core::Offset position, core::Offset delta) {
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
            return false;
        });
    shell_.controller().addDragSessionSink(
        [this](core::DragPhase phase, core::Offset position,
               const std::vector<const core::RenderNode*>&,
               const std::string& sourceKey, const std::string&) {
            canvasResizeSession(phase, position, sourceKey);
            outlineDragSession(phase, position, sourceKey);
            toolboxDragSession(phase, position, sourceKey);
        });
    shell_.handlers()["designer:theme"] = [this] { toggleTheme(); };
    shell_.handlers()["designer:density"] = [this] { cycleDensity(); };
    shell_.handlers()["designer:dpi"] = [this] { cycleDpi(); };
    shell_.handlers()["designer:font-scale"] = [this] { cycleFontScale(); };
    shell_.handlers()["designer:contrast"] = [this] { toggleHighContrast(); };
    shell_.handlers()["designer:canvas-guides"] =
        [this] { toggleCanvasGuides(); };
    shell_.handlers()["designer:preview-state"] =
        [this] { cyclePreviewState(); };
    shell_.handlers()["designer:add-text"] = [this] { insertTextNode(); };
    shell_.handlers()["designer:duplicate"] =
        [this] { duplicateSelectedNode(); };
    shell_.handlers()["designer:copy"] = [this] { copySelectedNode(); };
    shell_.handlers()["designer:paste"] = [this] { pasteCopiedNode(); };
    shell_.handlers()["designer:remove"] = [this] { removeSelectedNode(); };
    shell_.handlers()["designer:move-up"] = [this] { moveSelectedNode(-1); };
    shell_.handlers()["designer:move-down"] =
        [this] { moveSelectedNode(1); };
    shell_.handlers()["designer:open"] = [this] { requestOpenFile(); };
    shell_.handlers()["designer:new-project"] =
        [this] { requestNewProjectFile(); };
    shell_.handlers()["designer:save"] = [this] { requestSaveFile(); };
    shell_.handlers()["designer:save-as"] =
        [this] { requestSaveAsFile(); };
    shell_.handlers()["designer:run"] = [this] {
        (void)startPreview(false);
    };
    shell_.handlers()["designer:debug"] = [this] {
        (void)startPreview(true);
    };
    shell_.handlers()["designer:stop"] = [this] { stopPreview(); };
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
    sourceFile_ = filename;
    const bool loaded = workbench_.openDesignFile(filename);
    if (loaded) {
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
        sourceFile_ = filename;
        sourceSnapshot_ = workbench_.document().has_value()
                              ? dsl::serializeDesignDocument(
                                    *workbench_.document())
                              : std::string{};
        shell_.markDirty();
    }
    return saved;
}

void DesignerApp::appendProjectDiagnostic(dsl::DesignError diagnostic,
                                          std::string documentId) {
    projectDiagnostics_.push_back(std::move(diagnostic));
    projectDiagnosticDocumentIds_.push_back(std::move(documentId));
}

std::filesystem::path DesignerApp::projectRootPath() const {
    if (!project_.has_value()) return {};
    std::filesystem::path root{project_->root};
    const std::filesystem::path manifest{projectFile_};
    if (root.is_relative()) root = manifest.parent_path() / root;
    return root.lexically_normal();
}

std::filesystem::path DesignerApp::projectPagePath(
    const dsl::DesignProjectPage& page) const {
    const auto root = projectRootPath();
    if (root.empty()) return {};
    const auto relative = std::filesystem::path{page.path};
    if (relative.empty() || relative.is_absolute()) return {};
    const auto candidate = (root / relative).lexically_normal();
    const auto within = candidate.lexically_relative(root);
    if (within.empty() || within == ".." ||
        (within.begin() != within.end() && *within.begin() == "..")) {
        return {};
    }
    return candidate;
}

void DesignerApp::syncActiveProjectDocument() {
    if (activeProjectDocumentId_.empty() || !workbench_.document().has_value()) {
        return;
    }
    projectDocuments_[activeProjectDocumentId_] = *workbench_.document();
}

bool DesignerApp::switchProjectDocument(const std::string& documentId) {
    if (!project_.has_value()) return false;
    const auto found = projectDocuments_.find(documentId);
    const auto path = projectDocumentPaths_.find(documentId);
    if (found == projectDocuments_.end() || path == projectDocumentPaths_.end()) {
        return false;
    }
    syncActiveProjectDocument();
    if (!workbench_.openDocument(found->second, &runtimeContext_)) return false;
    activeProjectDocumentId_ = documentId;
    sourceFile_ = path->second;
    sourceSnapshot_ = dsl::serializeDesignDocument(found->second);
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
    const auto previousDocuments = projectDocuments_;
    const auto previousPaths = projectDocumentPaths_;
    const auto previousRevisions = projectDocumentRevisions_;
    const auto previousDiagnostics = projectDiagnostics_;
    const auto previousDiagnosticDocumentIds = projectDiagnosticDocumentIds_;
    const auto previousActive = activeProjectDocumentId_;
    project_ = loaded.project;
    projectFile_ = filename;
    projectRevision_ = loaded.revision;
    projectDiagnostics_ = loaded.diagnostics;
    projectDiagnosticDocumentIds_.assign(projectDiagnostics_.size(), {});
    projectDocuments_.clear();
    projectDocumentPaths_.clear();
    projectDocumentRevisions_.clear();

    const auto root = projectRootPath();
    if (root.empty()) {
        appendProjectDiagnostic(dsl::DesignError{
            "project.root", filename, {}, "project root is invalid", {}, {}, 0,
            {}, {}});
    } else {
        setResourceRoot(root);
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
        projectDocuments_[page.documentId] = document.document;
        projectDocumentPaths_[page.documentId] = pagePath.string();
        projectDocumentRevisions_[page.documentId] = document.revision;
    }
    if (projectDocuments_.empty()) {
        project_ = previousProject;
        projectFile_ = previousProjectFile;
        projectDocuments_ = previousDocuments;
        projectDocumentPaths_ = previousPaths;
        projectDocumentRevisions_ = previousRevisions;
        projectDiagnostics_ = previousDiagnostics;
        projectDiagnosticDocumentIds_ = previousDiagnosticDocumentIds;
        activeProjectDocumentId_ = previousActive;
        shell_.markDirty();
        return false;
    }
    const auto& first = project_->pages.front().documentId;
    const auto active = projectDocuments_.contains(first) ? first
                                                           : projectDocuments_.begin()->first;
    if (!switchProjectDocument(active)) return false;
    shell_.markDirty();
    return true;
}

bool DesignerApp::saveProjectFile(const std::string& filename) {
    if (!project_.has_value()) return false;
    syncActiveProjectDocument();
    std::vector<dsl::DesignError> diagnostics;
    for (auto& page : project_->pages) {
        const auto document = projectDocuments_.find(page.documentId);
        const auto path = projectPagePath(page);
        if (document == projectDocuments_.end() || path.empty()) {
            appendProjectDiagnostic(dsl::DesignError{
                "project.page", filename, {}, "project page is not loaded", {}, {},
                0, {}, {}});
            return false;
        }
        const auto revision = projectDocumentRevisions_.find(page.documentId);
        if (!projectDocumentStore_.save(
                path.string(), document->second, diagnostics,
                revision == projectDocumentRevisions_.end()
                    ? std::nullopt
                    : std::optional<std::uint64_t>{revision->second})) {
            for (auto diagnostic : diagnostics) {
                appendProjectDiagnostic(std::move(diagnostic), page.documentId);
            }
            return false;
        }
        page.pageName = document->second.pageName;
        const auto saved = projectDocumentStore_.load(path.string());
        if (saved.ok()) projectDocumentRevisions_[page.documentId] = saved.revision;
    }
    if (!projectStore_.save(filename, *project_, diagnostics,
                            projectFile_ == filename
                                ? std::optional<std::uint64_t>{projectRevision_}
                                : std::nullopt)) {
        for (auto diagnostic : diagnostics) {
            appendProjectDiagnostic(std::move(diagnostic));
        }
        return false;
    }
    projectFile_ = filename;
    const auto savedProject = projectStore_.load(filename);
    projectRevision_ = savedProject.revision;
    if (!activeProjectDocumentId_.empty()) {
        const auto activePath = projectDocumentPaths_.find(activeProjectDocumentId_);
        if (activePath != projectDocumentPaths_.end()) {
            (void)workbench_.saveDesignFile(activePath->second);
            sourceSnapshot_ = workbench_.document().has_value()
                                  ? dsl::serializeDesignDocument(*workbench_.document())
                                  : std::string{};
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
    sourceFile_ = filename;
    const bool loaded = workbench_.openLumenFile(filename);
    if (loaded) {
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
    sourceFile_ = filename.empty() ? "<memory>" : filename;
    const bool loaded = workbench_.openLumenSource(source, filename);
    if (loaded) {
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
        projectDocuments_.contains(diagnostic.documentId)) {
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
            projectDocuments_.contains(diagnostic.documentId));
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
                if (!imageResources_.contains(reference->uri())) {
                    const auto root = resourcePolicy_.roots().find(reference->scheme);
                    if (root != resourcePolicy_.roots().end()) {
                        const auto filename =
                            (root->second / reference->relativePath).string();
                        imageResources_.emplace(
                            reference->uri(),
                            resourceManager_->requestImage(filename));
                    }
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

void DesignerApp::syncImageResources() {
    diagnostics_ = workbench_.diagnostics();
    std::set<std::string> activeUris;
    if (workbench_.document().has_value()) {
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
        resourceManager_->release(it->second);
        it = imageResources_.erase(it);
    }
}

void DesignerApp::applyImageResources(core::Widget& widget) const {
    if (widget.type == core::WidgetType::Image &&
        !widget.imageSource.empty()) {
        const auto uri = imageUriForSource(widget.imageSource);
        const auto found = imageResources_.find(uri);
        widget.imageId = found != imageResources_.end() &&
                                 resourceManager_->ready(found->second)
                             ? resourceManager_->imageId(found->second)
                             : 0;
    }
    for (auto& child : widget.children) applyImageResources(child);
}

void DesignerApp::refreshDocumentUi() {
    if (workbench_.document().has_value()) {
        (void)workbench_.refresh(&runtimeContext_);
    }
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
    const auto selected = workbench_.selection().primary;
    if (!selected || !workbench_.duplicateNode(*selected)) return;
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
    const auto selected = workbench_.selection().primary;
    if (!selected || !workbench_.removeNode(*selected)) return;
    refreshDocumentUi();
    shell_.markDirty();
}

void DesignerApp::moveSelectedNode(int offset) {
    const auto selected = workbench_.selection().primary;
    if (!selected || !workbench_.moveNodeRelative(*selected, offset)) return;
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
        (void)workbench_.selectNode(*sourceId);
        const std::string path = sourceKey.substr(kSourcePrefix.size());
        outlineController_.selection().setCurrent(path);
        outlineController_.selection().setSelected({path});
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
        const std::string key = outlineNode->key.empty()
                                    ? previewKeyForNode(*selected)
                                    : outlineNode->key;
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
        canvasResizePositionEditable_ =
            parent != nullptr && parent->type == "Stack";
        canvasResizeActive_ = true;
        canvasResizeId_ = *selected;
        canvasResizeHandle_ = handle;
        canvasResizePointer_ = position;
        canvasResizeStart_ = CanvasResizePreview{
            *selected,
            renderOrigin.x - contentOrigin.x,
            renderOrigin.y - contentOrigin.y,
            renderNode->size.width,
            renderNode->size.height};
        canvasResizePreview_ = canvasResizeStart_;
        shell_.markDirty();
        return;
    }
    if (!canvasResizeActive_) return;
    const auto* canvasNode =
        core::findNodeByKey(shell_.root(), "designer-canvas");
    if (canvasNode == nullptr) return;
    const float canvasWidth =
        std::max(0.0F, canvasNode->size.width - canvasNode->padding.horizontal());
    const float canvasHeight =
        std::max(0.0F, canvasNode->size.height - canvasNode->padding.vertical());
    if (phase == core::DragPhase::Move) {
        const core::Offset delta = position - canvasResizePointer_;
        CanvasResizePreview next = canvasResizeStart_;
        const bool moveLeft = canvasResizeHandle_.find('w') != std::string::npos;
        const bool moveRight = canvasResizeHandle_.find('e') != std::string::npos;
        const bool moveTop = canvasResizeHandle_.find('n') != std::string::npos;
        const bool moveBottom = canvasResizeHandle_.find('s') != std::string::npos;
        const float right = canvasResizeStart_.x + canvasResizeStart_.width;
        const float bottom = canvasResizeStart_.y + canvasResizeStart_.height;
        if (moveLeft) {
            float edge = snapCanvasCoordinate(
                canvasResizeStart_.x + delta.x, canvasWidth,
                shell_.theme().designerCanvas.snapThreshold);
            edge = std::clamp(edge, 0.0F, right - 4.0F);
            next.x = canvasResizePositionEditable_ ? edge : canvasResizeStart_.x;
            next.width = std::max(4.0F, right - edge);
        } else if (moveRight) {
            float edge = snapCanvasCoordinate(
                right + delta.x, canvasWidth,
                shell_.theme().designerCanvas.snapThreshold);
            edge = std::clamp(edge, next.x + 4.0F, canvasWidth);
            next.width = std::max(4.0F, edge - next.x);
        }
        if (moveTop) {
            float edge = snapCanvasCoordinate(
                canvasResizeStart_.y + delta.y, canvasHeight,
                shell_.theme().designerCanvas.snapThreshold);
            edge = std::clamp(edge, 0.0F, bottom - 4.0F);
            next.y = canvasResizePositionEditable_ ? edge : canvasResizeStart_.y;
            next.height = std::max(4.0F, bottom - edge);
        } else if (moveBottom) {
            float edge = snapCanvasCoordinate(
                bottom + delta.y, canvasHeight,
                shell_.theme().designerCanvas.snapThreshold);
            edge = std::clamp(edge, next.y + 4.0F, canvasHeight);
            next.height = std::max(4.0F, edge - next.y);
        }
        canvasResizePreview_ = next;
        shell_.markDirty();
        return;
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
        properties.emplace_back("left", number(preview.x));
    }
    if (positionEditable && handleName.find('n') != std::string::npos) {
        properties.emplace_back("top", number(preview.y));
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
    shell_.setDeviceScale(deviceScale_);
    previewShell_.setDeviceScale(deviceScale_);
    shell_.markDirty();
    previewShell_.markDirty();
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
    propertyObservers_[bind] = shell_.state().subscribe(
        bind, [this, id, name = property.name, bind, prototype] {
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
            if (!parsed.has_value() || !workbench_.setProperty(
                                           id, name, *parsed)) {
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
            return node.key.empty() ? previewKeyForNode(node.id) : node.key;
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
    if (widget.key.empty()) widget.key = previewKeyForNode(node.id);
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
         std::move(saveButton), std::move(saveAsButton)},
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
                    selectedKey = selected->key.empty()
                                      ? previewKeyForNode(*primary)
                                      : selected->key;
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
                selectedX = canvasResizePreview_.x;
                selectedY = canvasResizePreview_.y;
                selectedWidth = canvasResizePreview_.width;
                selectedHeight = canvasResizePreview_.height;
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
    auto panel = core::makeColumn(
        {std::move(status), std::move(list)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 4.0F,
        core::EdgeInsets::symmetric(12.0F, 8.0F), {}, "designer-diagnostics",
        std::nullopt, 96.0F);
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
