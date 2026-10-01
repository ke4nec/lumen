#pragma once

#include <cstdint>
#include <functional>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/virtual_list.h"
#include "lumen/dsl/design_resources.h"
#include "lumen/dsl/design_workbench.h"
#include "lumen/dsl/document_store.h"
#include "lumen/dsl/project_store.h"
#include "lumen/render/resource_manager.h"
#include "lumen/widgets/color_picker.h"
#include "lumen/widgets/combo_box.h"
#include "lumen/widgets/datagrid.h"
#include "lumen/widgets/dialog_host.h"
#include "lumen/widgets/form.h"
#include "lumen/widgets/menu.h"
#include "lumen/widgets/navigator.h"
#include "lumen/widgets/spin.h"
#include "lumen/widgets/statusbar.h"
#include "lumen/widgets/toolbar.h"
#include "lumen/widgets/tree.h"

namespace lumen::designer_app {

class DesignerApp {
  public:
    using FileDialogRequester =
        std::function<std::string(bool forSave, const std::string& defaultName)>;

    DesignerApp();

    void attach();
    [[nodiscard]] bool loadFile(const std::string& filename);
    [[nodiscard]] bool loadSource(const std::string& source,
                                  std::string filename = "<memory>");

    [[nodiscard]] app::AppShell& shell() { return shell_; }
    [[nodiscard]] const app::AppShell& shell() const { return shell_; }
    [[nodiscard]] app::AppShell& previewShell() { return previewShell_; }
    [[nodiscard]] const app::AppShell& previewShell() const {
        return previewShell_;
    }
    [[nodiscard]] const dsl::DesignPreviewWorkbench& workbench() const {
        return workbench_;
    }
    [[nodiscard]] dsl::DesignPreviewWorkbench& workbench() { return workbench_; }
    [[nodiscard]] const std::vector<dsl::DesignDiagnostic>& diagnostics() const {
        return diagnostics_;
    }

    // Image resources are restricted to this explicitly authorized project
    // root. The policy is intentionally application-owned so opening a
    // document never grants arbitrary filesystem access.
    void setResourceRoot(std::filesystem::path root);
    [[nodiscard]] const std::shared_ptr<render::ResourceManager>&
    resourceManager() const {
        return resourceManager_;
    }

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool loadDesignFile(const std::string& filename);
    [[nodiscard]] bool saveDesignFile(const std::string& filename);
    [[nodiscard]] bool loadProjectFile(const std::string& filename);
    [[nodiscard]] bool saveProjectFile(const std::string& filename);
    [[nodiscard]] bool createProjectFile(const std::string& filename);
    [[nodiscard]] bool switchProjectDocument(const std::string& documentId);
    [[nodiscard]] const std::optional<dsl::DesignProject>& project() const {
        return project_;
    }
    [[nodiscard]] const std::string& activeProjectDocumentId() const {
        return activeProjectDocumentId_;
    }
    [[nodiscard]] const std::vector<dsl::DesignError>& projectDiagnostics() const {
        return projectDiagnostics_;
    }
    void setFileDialogRequester(FileDialogRequester requester) {
        fileDialogRequester_ = std::move(requester);
    }
    void handleFileDialogResult(const std::vector<std::string>& paths,
                                const std::string& error = {});

  private:
    class OfflineRuntimeContext final : public dsl::DesignRuntimeContext {
      public:
        explicit OfflineRuntimeContext(DesignerApp* owner) : owner_(owner) {}

        [[nodiscard]] bool validatesReferences() const override;
        [[nodiscard]] bool resolveReference(
            dsl::DesignReferenceKind kind, const std::string& name,
            dsl::DesignReference& out) const override;
        [[nodiscard]] dsl::DesignComponentResult buildComponent(
            const dsl::DesignNode& node,
            const dsl::DesignComponentContext& componentContext) const override;

      private:
        DesignerApp* owner_{nullptr};
    };

    class OutlineModel final : public widgets::TreeModel {
      public:
        explicit OutlineModel(DesignerApp* owner) : owner_(owner) {}

        void setRoot(std::optional<dsl::DesignPreviewOutlineNode> root);
        [[nodiscard]] std::optional<dsl::DesignNodeId> idForKey(
            const std::string& key) const;

        [[nodiscard]] std::size_t childCount(
            const std::string& parent) const override;
        [[nodiscard]] std::string childAt(const std::string& parent,
                                           std::size_t index) const override;
        [[nodiscard]] bool hasChildren(
            const std::string& key) const override;
        [[nodiscard]] core::Widget buildRow(
            const std::string& key, std::size_t depth) const override;

      private:
        struct Entry {
            dsl::DesignNodeId id{0};
            std::string label{};
        };

        void visit(const dsl::DesignPreviewOutlineNode& node,
                   const std::string& parent);

        DesignerApp* owner_{nullptr};
        std::map<std::string, Entry> entries_{};
        std::map<std::string, std::vector<std::string>> children_{};
    };

    [[nodiscard]] static app::ShellConfig configFor(DesignerApp* self);
    [[nodiscard]] static app::ShellConfig previewConfigFor(DesignerApp* self);
    [[nodiscard]] core::Widget buildUi();
    [[nodiscard]] core::Widget buildPreviewWindow();
    [[nodiscard]] core::Widget buildToolbar();
    [[nodiscard]] core::Widget buildToolboxPanel();
    [[nodiscard]] core::Widget buildProjectPanel();
    [[nodiscard]] core::Widget buildBody();
    [[nodiscard]] core::Widget buildOutlinePanel();
    [[nodiscard]] core::Widget buildPreviewPanel();
    [[nodiscard]] core::Widget buildCanvasPanel();
    [[nodiscard]] core::Widget buildCanvasStack(core::Widget preview) const;
    [[nodiscard]] core::Widget buildSourcePanel();
    [[nodiscard]] core::Widget buildReferencesPanel();
    [[nodiscard]] core::Widget buildPropertiesPanel();
    [[nodiscard]] core::Widget buildDiagnosticsPanel();
    [[nodiscard]] core::Widget buildDiagnosticRow(std::size_t index);

    void rebuildOutline();
    void rebuildReferences();
    void selectReference(const std::string& key);
    void selectNode(dsl::DesignNodeId id);
    [[nodiscard]] std::optional<dsl::DesignNodeId> diagnosticTarget(
        std::size_t index) const;
    void activateDiagnostic(std::size_t index);
    void registerSelectionHandlers(const dsl::DesignNode& node);
    void syncSelectionFromOutline();
    void applyEnvironmentTheme();
    void toggleTheme();
    void cycleDensity();
    void cycleDpi();
    void cycleFontScale();
    void toggleHighContrast();
    void toggleCanvasGuides();
    void cyclePreviewState();
    void applyPreviewState();
    void resetPreviewState();
    void requestOpenFile();
    void requestNewProjectFile();
    void requestSaveFile();
    void requestSaveAsFile();
    [[nodiscard]] bool startPreview(bool debug);
    void stopPreview();
    void insertNodeType(std::string type);
    void insertTextNode();
    void duplicateSelectedNode();
    void copySelectedNode();
    void pasteCopiedNode();
    void removeSelectedNode();
    void moveSelectedNode(int offset);
    void outlineDragSession(core::DragPhase phase, core::Offset position,
                            const std::string& sourceKey);
    void endOutlineDragSession();
    [[nodiscard]] core::Widget buildOutlineDragOverlay() const;
    void toolboxDragSession(core::DragPhase phase, core::Offset position,
                            const std::string& sourceKey);
    void endToolboxDragSession();
    [[nodiscard]] core::Widget buildToolboxDragOverlay() const;
    void canvasResizeSession(core::DragPhase phase, core::Offset position,
                             const std::string& sourceKey);
    void endCanvasResizeSession();
    [[nodiscard]] static float snapCanvasCoordinate(float value, float maximum,
                                                     float threshold);
    void refreshDocumentUi();
    void syncActiveProjectDocument();
    [[nodiscard]] std::filesystem::path projectRootPath() const;
    [[nodiscard]] std::filesystem::path projectPagePath(
        const dsl::DesignProjectPage& page) const;
    void appendProjectDiagnostic(dsl::DesignError diagnostic,
                                 std::string documentId = {});
    [[nodiscard]] bool diagnosticActionable(std::size_t index) const;
    void clearPropertyObservers();
    void registerPropertyBinding(dsl::DesignNodeId id,
                                  const dsl::DesignPreviewProperty& property);
    void registerReferenceBinding(dsl::DesignNodeId id,
                                  const dsl::DesignPreviewProperty& property);

    enum class PreviewStateMode {
        None,
        Hovered,
        Pressed,
        Focused,
    };

    enum class PendingFileDialog {
        None,
        Open,
        NewProject,
        SaveAs,
    };

    enum class CenterTab {
        Canvas,
        Source,
        References,
    };

    enum class PreviewSessionMode {
        Stopped,
        Running,
        Debugging,
    };

    struct ReferenceEntry {
        dsl::DesignNodeId nodeId{0};
        std::string property{};
        std::string value{};
        std::string kind{};
        std::string status{};
        std::string location{};
        std::string key{};
    };

    [[nodiscard]] core::Widget decoratePreview(
        core::Widget widget, const dsl::DesignNode& node,
        const std::set<dsl::DesignNodeId>& selected) const;
    [[nodiscard]] static std::string formatValue(
        const std::optional<dsl::DesignValue>& value,
        const std::optional<std::string>& reference);
    [[nodiscard]] std::string currentSourceText() const;
    [[nodiscard]] static std::string formatDiagnostic(
        const dsl::DesignDiagnostic& diagnostic);

    void syncImageResources();
    void collectImageResources(const dsl::DesignNode& node,
                               const std::string& path,
                               std::set<std::string>& activeUris);
    void applyImageResources(core::Widget& widget) const;
    [[nodiscard]] static std::string imageUriForSource(
        std::string_view source);

    dsl::DesignPreviewWorkbench workbench_{};
    OfflineRuntimeContext runtimeContext_;
    OutlineModel outlineModel_;
    widgets::TreeController outlineController_{};
    widgets::DataGridController referencesController_{};
    widgets::ComboBoxController comboPreviewController_{
        {widgets::ComboBoxController::Option{"draft", "Draft"},
         widgets::ComboBoxController::Option{"review", "Review"},
         widgets::ComboBoxController::Option{"published", "Published"}},
        "designer-component-combo-value", "designer-component-combo"};
    widgets::ColorPickerController colorPickerPreviewController_{
        "designer-component-color-value", "designer-component-color"};
    widgets::SpinController spinPreviewController_{
        42.0, "designer-component-spin"};
    widgets::MenuBarController menuPreviewController_{};
    widgets::NavigatorController navigatorPreviewController_{"home"};
    widgets::FormController formPreviewController_{};
    widgets::DialogHost dialogPreviewController_{};
    widgets::DataGridController dataGridPreviewController_{};
    widgets::ToolBarController toolBarPreviewController_{
        "designer-component-toolbar"};
    widgets::StatusBarController statusBarPreviewController_{
        "designer-component-statusbar"};
    core::VirtualListController diagnosticsController_{};
    std::vector<ReferenceEntry> referenceRows_{};
    CenterTab centerTab_{CenterTab::Canvas};
    PreviewSessionMode previewSessionMode_{PreviewSessionMode::Stopped};
    std::string sourceSnapshot_{};
    std::size_t sourceFocusLine_{0};
    std::string sourceFile_{"<sample>"};
    std::string statusMessage_{};
    std::vector<dsl::DesignNode> clipboardNodes_{};
    FileDialogRequester fileDialogRequester_{};
    PendingFileDialog pendingFileDialog_{PendingFileDialog::None};
    bool darkMode_{true};
    style::ControlDensity density_{style::ControlDensity::Comfortable};
    float deviceScale_{1.0F};
    float fontScale_{1.0F};
    bool highContrast_{false};
    bool canvasGuidesEnabled_{false};
    PreviewStateMode previewStateMode_{PreviewStateMode::None};
    std::string previewStateKey_{};
    bool outlineDragActive_{false};
    dsl::DesignNodeId outlineDragId_{0};
    dsl::DesignNodeId outlineDragParentId_{0};
    std::size_t outlineDragIndex_{0};
    std::size_t outlineDragInsertIndex_{0};
    std::string outlineDragSlot_{};
    core::Offset outlineDragPointer_{};
    bool toolboxDragActive_{false};
    std::string toolboxDragType_{};
    core::Offset toolboxDragPointer_{};
    struct CanvasResizePreview {
        dsl::DesignNodeId id{0};
        float x{0.0F};
        float y{0.0F};
        float width{0.0F};
        float height{0.0F};
    };
    bool canvasResizeActive_{false};
    dsl::DesignNodeId canvasResizeId_{0};
    std::string canvasResizeHandle_{};
    core::Offset canvasResizePointer_{};
    CanvasResizePreview canvasResizeStart_{};
    CanvasResizePreview canvasResizePreview_{};
    bool canvasResizePositionEditable_{false};
    std::map<std::string, core::StateStore::ObserverId> propertyObservers_{};
    bool syncingPropertyState_{false};
    dsl::DesignResourcePolicy resourcePolicy_{};
    dsl::DesignResourceAuthorizer resourceAuthorizer_{resourcePolicy_};
    std::shared_ptr<render::ResourceManager> resourceManager_{
        std::make_shared<render::ResourceManager>()};
    std::map<std::string, render::ResourceHandle> imageResources_{};
    std::vector<dsl::DesignDiagnostic> diagnostics_{};
    dsl::ProjectStore projectStore_{};
    dsl::DocumentStore projectDocumentStore_{};
    std::optional<dsl::DesignProject> project_{};
    std::string projectFile_{};
    std::uint64_t projectRevision_{0};
    std::map<std::string, dsl::DesignDocument> projectDocuments_{};
    std::map<std::string, std::string> projectDocumentPaths_{};
    std::map<std::string, std::uint64_t> projectDocumentRevisions_{};
    std::vector<dsl::DesignError> projectDiagnostics_{};
    std::vector<std::string> projectDiagnosticDocumentIds_{};
    std::string activeProjectDocumentId_{};
    app::AppShell shell_;
    app::AppShell previewShell_;
};

}  // namespace lumen::designer_app
