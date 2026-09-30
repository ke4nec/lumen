#include "designer_app.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

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
            shell_.markDirty();
        }
    };
    shell_.handlers()["designer:theme"] = [this] { toggleTheme(); };
    shell_.handlers()["designer:density"] = [this] { cycleDensity(); };
    shell_.handlers()["designer:preview-state"] =
        [this] { togglePreviewState(); };
    (void)loadSource(kSampleSource, "sample.lumen");
}

bool DesignerApp::loadFile(const std::string& filename) {
    sourceFile_ = filename;
    const bool loaded = workbench_.openLumenFile(filename);
    if (loaded) {
        rebuildOutline();
        registerSelectionHandlers(workbench_.document()->root);
    }
    shell_.markDirty();
    return loaded;
}

bool DesignerApp::loadSource(const std::string& source, std::string filename) {
    sourceFile_ = filename.empty() ? "<memory>" : filename;
    const bool loaded = workbench_.openLumenSource(source, filename);
    if (loaded) {
        rebuildOutline();
        registerSelectionHandlers(workbench_.document()->root);
    }
    shell_.markDirty();
    return loaded;
}

void DesignerApp::rebuildOutline() {
    outlineModel_.setRoot(workbench_.outline());
    outlineController_.modelChanged();
    if (const auto root = workbench_.outline(); root.has_value()) {
        outlineController_.collapseAll();
        (void)outlineController_.expandAll();
        outlineController_.selection().setCurrent(root->path);
        outlineController_.selection().setSelected({root->path});
        (void)workbench_.selectNode(root->id);
    } else {
        outlineController_.selection().clear();
    }
}

void DesignerApp::registerSelectionHandlers(const dsl::DesignNode& node) {
    const std::string handler = "designer:select:" + std::to_string(node.id);
    shell_.handlers()[handler] = [this, id = node.id] {
        if (!workbench_.selectNode(id)) return;
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

void DesignerApp::syncSelectionFromOutline() {
    const auto key = outlineController_.selection().currentKey();
    if (key.empty()) return;
    const auto id = outlineModel_.idForKey(key);
    if (!id.has_value() || workbench_.selection().primary == id) return;
    (void)workbench_.selectNode(*id);
}

void DesignerApp::toggleTheme() {
    darkMode_ = !darkMode_;
    shell_.setTheme(darkMode_ ? style::Theme::dark(density_)
                              : style::Theme::light(density_));
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
    shell_.setTheme(darkMode_ ? style::Theme::dark(density_)
                              : style::Theme::light(density_));
}

void DesignerApp::togglePreviewState() {
    if (!previewStateIdentity_.empty()) {
        shell_.setVisualPreviewState(previewStateIdentity_, {});
    }
    previewStateIdentity_.clear();
    previewStateActive_ = !previewStateActive_;
    if (!previewStateActive_) return;
    const auto primary = workbench_.selection().primary;
    if (!primary.has_value()) return;
    const auto identity = workbench_.runtimeIdentity(*primary);
    if (!identity.has_value()) return;
    previewStateIdentity_ = *identity;
    shell_.setVisualPreviewState(previewStateIdentity_,
                                 style::WidgetState{.hovered = true});
}

core::Widget DesignerApp::decoratePreview(
    core::Widget widget, const dsl::DesignNode& node,
    std::optional<dsl::DesignNodeId> selected) const {
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
    auto themeButton = core::makeButton(
        darkMode_ ? "Light" : "Dark", theme.typography.label, {}, 0.0F,
        "designer-theme", 88.0F, std::nullopt, "designer:theme");
    auto densityButton = core::makeButton(
        "Density", theme.typography.label, {}, 0.0F, "designer-density",
        96.0F, std::nullopt, "designer:density");
    auto previewButton = core::makeButton(
        previewStateActive_ ? "Clear state" : "Hover state",
        theme.typography.label, {}, 0.0F, "designer-preview-state", 128.0F,
        std::nullopt, "designer:preview-state");
    auto title = core::makeText("Lumen Designer  /  D2 Preview",
                                theme.typography.title, {}, 1.0F,
                                "designer-title");
    auto toolbar = core::makeRow(
        {std::move(title), std::move(themeButton), std::move(densityButton),
         std::move(previewButton)},
        core::MainAxisAlignment::Start, core::CrossAxisAlignment::Center,
        8.0F, core::EdgeInsets::symmetric(16.0F, 8.0F), {}, "designer-toolbar",
        std::nullopt, 56.0F);
    toolbar.color = theme.colors.surface;
    return toolbar;
}

core::Widget DesignerApp::buildOutlinePanel() {
    const auto& theme = shell_.theme();
    auto heading = core::makeText("Outline", theme.typography.label, {}, 0.0F,
                                  "designer-outline-heading");
    auto tree = core::makeTree(&outlineController_, "designer-outline-view",
                               std::nullopt, std::nullopt);
    tree.flex = 1.0F;
    auto panel = core::makeColumn(
        {std::move(heading), std::move(tree)}, core::MainAxisAlignment::Start,
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
            rows.push_back(core::makeText(
                property.name + "  " +
                    formatValue(property.value, property.reference),
                theme.typography.body, {}, 0.0F,
                "designer-property:" + property.name));
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
