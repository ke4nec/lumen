#include "designer_app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

#include "lumen/dsl/design_schema.h"

namespace lumen::designer_app {
namespace {

constexpr char kSampleSource[] =
    "page preview { Column(key: \"root\") {"
    " Text(\"Preview title\", key: \"title\")"
    " Button(\"Save\", key: \"save\")"
    " Row(key: \"status\") { Text(\"Ready\", key: \"message\") }"
    " } }";

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

bool isEditableProperty(const dsl::DesignValue& value) {
    return std::holds_alternative<bool>(value.value) ||
           std::holds_alternative<double>(value.value) ||
           std::holds_alternative<std::string>(value.value) ||
           std::holds_alternative<dsl::DesignEnum>(value.value);
}

}  // namespace

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
    : outlineModel_(this), shell_(configFor(this)) {}

app::ShellConfig DesignerApp::configFor(DesignerApp* self) {
    app::ShellConfig config;
    config.initialView = core::Size{1280.0F, 800.0F};
    config.build = [self] { return self->buildUi(); };
    config.onKey = [self](app::AppShell&, core::Key key,
                          core::KeyModifiers modifiers, char keyChar) {
        const bool ctrlLike =
            (modifiers & (core::kModifierCtrl | core::kModifierGui)) != 0;
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
        }
        if (self->outlineController_.handleKey(key, modifiers, keyChar)) {
            return true;
        }
        return false;
    };
    return config;
}

void DesignerApp::attach() {
    outlineController_.setSelectionMode(widgets::SelectionMode::Single);
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
    shell_.handlers()["designer:theme"] = [this] { toggleTheme(); };
    shell_.handlers()["designer:density"] = [this] { cycleDensity(); };
    shell_.handlers()["designer:dpi"] = [this] { cycleDpi(); };
    shell_.handlers()["designer:font-scale"] = [this] { cycleFontScale(); };
    shell_.handlers()["designer:contrast"] = [this] { toggleHighContrast(); };
    shell_.handlers()["designer:preview-state"] =
        [this] { cyclePreviewState(); };
    shell_.handlers()["designer:add-text"] = [this] { insertTextNode(); };
    shell_.handlers()["designer:duplicate"] =
        [this] { duplicateSelectedNode(); };
    shell_.handlers()["designer:remove"] = [this] { removeSelectedNode(); };
    shell_.handlers()["designer:move-up"] = [this] { moveSelectedNode(-1); };
    shell_.handlers()["designer:move-down"] =
        [this] { moveSelectedNode(1); };
    for (const auto& schema : dsl::nodeSchemaRegistry()) {
        shell_.handlers()["designer:toolbox:" + schema.type] =
            [this, type = schema.type] { insertNodeType(type); };
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

bool DesignerApp::saveDesignFile(const std::string& filename) {
    const bool saved = workbench_.saveDesignFile(filename);
    if (saved) {
        sourceFile_ = filename;
        shell_.markDirty();
    }
    return saved;
}

bool DesignerApp::loadFile(const std::string& filename) {
    sourceFile_ = filename;
    const bool loaded = workbench_.openLumenFile(filename);
    if (loaded) {
        resetPreviewState();
        refreshDocumentUi();
    }
    shell_.markDirty();
    return loaded;
}

bool DesignerApp::loadSource(const std::string& source, std::string filename) {
    sourceFile_ = filename.empty() ? "<memory>" : filename;
    const bool loaded = workbench_.openLumenSource(source, filename);
    if (loaded) {
        resetPreviewState();
        refreshDocumentUi();
    }
    shell_.markDirty();
    return loaded;
}

void DesignerApp::rebuildOutline() {
    outlineModel_.setRoot(workbench_.outline());
    outlineController_.modelChanged();
    if (const auto root = workbench_.outline(); root.has_value()) {
        const auto selected = workbench_.selection().primary;
        std::optional<std::string> selectedPath;
        std::function<void(const dsl::DesignPreviewOutlineNode&)> findPath =
            [&](const dsl::DesignPreviewOutlineNode& item) {
                if (selectedPath.has_value()) return;
                if (selected.has_value() && item.id == *selected) {
                    selectedPath = item.path;
                    return;
                }
                for (const auto& child : item.children) findPath(child);
            };
        findPath(*root);
        if (!selectedPath.has_value()) {
            selectedPath = root->path;
            (void)workbench_.selectNode(root->id);
        }
        outlineController_.collapseAll();
        (void)outlineController_.expandAll();
        outlineController_.selection().setCurrent(*selectedPath);
        outlineController_.selection().setSelected({*selectedPath});
    } else {
        outlineController_.selection().clear();
    }
}

void DesignerApp::registerSelectionHandlers(const dsl::DesignNode& node) {
    const std::string handler = "designer:select:" + std::to_string(node.id);
    shell_.handlers()[handler] = [this, id = node.id] {
        if (!workbench_.selectNode(id)) return;
        applyPreviewState();
        if (const auto outline = workbench_.outline(); outline.has_value()) {
            std::function<std::optional<std::string>(
                const dsl::DesignPreviewOutlineNode&)>
                find = [&](const dsl::DesignPreviewOutlineNode& item)
                -> std::optional<std::string> {
                if (item.id == id) return item.path;
                for (const auto& child : item.children) {
                    if (auto path = find(child); path.has_value()) return path;
                }
                return std::nullopt;
            };
            if (const auto path = find(*outline); path.has_value()) {
                outlineController_.selection().setCurrent(*path);
                outlineController_.selection().setSelected({*path});
            }
        }
        shell_.markDirty();
    };
    for (const auto& child : node.children) registerSelectionHandlers(child);
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) registerSelectionHandlers(child);
    }
}

void DesignerApp::refreshDocumentUi() {
    clearPropertyObservers();
    rebuildOutline();
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

void DesignerApp::syncSelectionFromOutline() {
    const auto key = outlineController_.selection().currentKey();
    if (key.empty()) return;
    const auto id = outlineModel_.idForKey(key);
    if (!id.has_value() || workbench_.selection().primary == id) return;
    (void)workbench_.selectNode(*id);
    applyPreviewState();
}

void DesignerApp::applyEnvironmentTheme() {
    accessibility::AccessibilityOverrides overrides;
    overrides.highContrast = highContrast_;
    overrides.fontScale = fontScale_;
    shell_.setAccessibilityOverrides(overrides);
    const auto settings = shell_.accessibilitySettings();
    shell_.setTheme(
        style::Theme::fromSettings(settings, darkMode_, density_));
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

    propertyObservers_[bind] = shell_.state().subscribe(
        bind, [this, id, name = property.name, bind] {
            if (syncingPropertyState_ || !workbench_.document().has_value()) {
                return;
            }
            const auto* node =
                findDesignNode(workbench_.document()->root, id);
            if (node == nullptr) return;
            const auto propertyIt = node->properties.find(name);
            if (propertyIt == node->properties.end()) return;
            const auto parsed =
                parsePropertyState(propertyIt->second, shell_.state().get(bind));
            if (!parsed.has_value() || !workbench_.setProperty(
                                           id, name, *parsed)) {
                syncingPropertyState_ = true;
                shell_.state().set(bind, propertyStateValue(propertyIt->second));
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
    std::optional<dsl::DesignNodeId> selected) const {
    if (widget.key.empty()) widget.key = previewKeyForNode(node.id);
    widget.onClick = "designer:select:" + std::to_string(node.id);
    if (selected.has_value() && selected == node.id) {
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
    auto title = core::makeText("Lumen Designer  /  D3 Editor",
                                theme.typography.title, {}, 1.0F,
                                "designer-title");
    auto toolbar = core::makeRow(
        {std::move(title), std::move(themeButton), std::move(densityButton),
         std::move(dpiButton), std::move(fontButton),
         std::move(contrastButton), std::move(previewButton),
         std::move(addTextButton), std::move(duplicateButton),
         std::move(removeButton), std::move(moveUpButton),
         std::move(moveDownButton)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
        8.0F, core::EdgeInsets::symmetric(16.0F, 8.0F), {}, "designer-toolbar",
        std::nullopt, 56.0F);
    toolbar.color = theme.colors.surface;
    return toolbar;
}

core::Widget DesignerApp::buildToolboxPanel() {
    const auto& theme = shell_.theme();
    std::vector<core::Widget> rows;
    const auto& schemas = dsl::nodeSchemaRegistry();
    for (std::size_t index = 0; index < schemas.size(); index += 3) {
        std::vector<core::Widget> buttons;
        for (std::size_t offset = 0;
             offset < 3 && index + offset < schemas.size(); ++offset) {
            const auto& schema = schemas[index + offset];
            buttons.push_back(core::makeButton(
                schema.type, theme.typography.caption, {}, 1.0F,
                "designer-toolbox:" + schema.type, std::nullopt,
                std::nullopt, "designer:toolbox:" + schema.type));
        }
        rows.push_back(core::makeRow(
            std::move(buttons), core::MainAxisAlignment::Start,
            core::CrossAxisAlignment::Stretch, 4.0F));
    }
    auto heading = core::makeText("Toolbox", theme.typography.label, {}, 0.0F,
                                  "designer-toolbox-heading");
    return core::makeColumn(
        {std::move(heading),
         core::makeColumn(std::move(rows), core::MainAxisAlignment::Start,
                          core::CrossAxisAlignment::Stretch, 4.0F)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Stretch, 6.0F,
        {}, {}, "designer-toolbox");
}

core::Widget DesignerApp::buildOutlinePanel() {
    const auto& theme = shell_.theme();
    auto heading = core::makeText("Outline", theme.typography.label, {}, 0.0F,
                                  "designer-outline-heading");
    auto tree = core::makeTree(&outlineController_, "designer-outline-view",
                               std::nullopt, std::nullopt);
    tree.flex = 1.0F;
    auto panel = core::makeColumn(
        {buildToolboxPanel(), std::move(heading), std::move(tree)},
        core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-outline-panel", 240.0F);
    panel.color = theme.colors.surfaceSunken;
    return panel;
}

core::Widget DesignerApp::buildPreviewPanel() {
    const auto& theme = shell_.theme();
    core::Widget preview;
    if (workbench_.frame().hasFrame() && workbench_.document().has_value()) {
        preview = decoratePreview(
            workbench_.frame().widget(), workbench_.document()->root,
            workbench_.selection().primary);
    } else {
        preview = core::makeText("No valid preview frame",
                                 theme.typography.body);
    }
    auto canvas = core::makeContainer(
        std::move(preview), std::nullopt, std::nullopt,
        core::EdgeInsets::all(24.0F), {}, theme.colors.pageBackground,
        core::CornerRadius::all(theme.metrics.cardRadius), "designer-canvas");
    canvas.flex = 1.0F;
    auto heading = core::makeText("Canvas", theme.typography.label, {}, 0.0F,
                                  "designer-canvas-heading");
    auto panel = core::makeColumn(
        {std::move(heading), std::move(canvas)}, core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-preview-panel");
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
        for (const auto& property : workbench_.properties(*selected)) {
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
    auto panel = core::makeColumn(
        std::move(rows), core::MainAxisAlignment::Start,
        core::CrossAxisAlignment::Stretch, 8.0F,
        core::EdgeInsets::all(12.0F), {}, "designer-properties-panel", 280.0F);
    panel.color = theme.colors.surfaceSunken;
    return panel;
}

core::Widget DesignerApp::buildDiagnosticsPanel() {
    const auto& theme = shell_.theme();
    std::vector<core::Widget> rows;
    rows.push_back(core::makeText(
        workbench_.diagnostics().empty() ? "Ready  /  " + sourceFile_
                                         : "Diagnostics  /  " + sourceFile_,
        theme.typography.caption, {}, 0.0F, "designer-status"));
    for (const auto& diagnostic : workbench_.diagnostics()) {
        rows.push_back(core::makeText(formatDiagnostic(diagnostic),
                                      theme.typography.caption));
    }
    auto panel = core::makeColumn(
        std::move(rows), core::MainAxisAlignment::Start,
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
    return core::makeColumn({std::move(body), buildDiagnosticsPanel()},
                             core::MainAxisAlignment::Start,
                             core::CrossAxisAlignment::Stretch, 0.0F, {}, {},
                             "designer-content");
}

core::Widget DesignerApp::buildUi() {
    syncSelectionFromOutline();
    auto root = core::makeColumn({buildToolbar(), buildBody()},
                                 core::MainAxisAlignment::Start,
                                 core::CrossAxisAlignment::Stretch, 0.0F, {}, {},
                                 "designer-root");
    root.color = shell_.theme().colors.pageBackground;
    return root;
}

}  // namespace lumen::designer_app
