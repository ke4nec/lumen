#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/virtual_list.h"
#include "lumen/dsl/design_workbench.h"
#include "lumen/widgets/datagrid.h"
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
    [[nodiscard]] const dsl::DesignPreviewWorkbench& workbench() const {
        return workbench_;
    }
    [[nodiscard]] dsl::DesignPreviewWorkbench& workbench() { return workbench_; }

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool loadDesignFile(const std::string& filename);
    [[nodiscard]] bool saveDesignFile(const std::string& filename);
    void setFileDialogRequester(FileDialogRequester requester) {
        fileDialogRequester_ = std::move(requester);
    }
    void handleFileDialogResult(const std::vector<std::string>& paths,
                                const std::string& error = {});

  private:
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
    [[nodiscard]] core::Widget buildUi();
    [[nodiscard]] core::Widget buildToolbar();
    [[nodiscard]] core::Widget buildToolboxPanel();
    [[nodiscard]] core::Widget buildBody();
    [[nodiscard]] core::Widget buildOutlinePanel();
    [[nodiscard]] core::Widget buildPreviewPanel();
    [[nodiscard]] core::Widget buildCanvasPanel();
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
    void cyclePreviewState();
    void applyPreviewState();
    void resetPreviewState();
    void requestOpenFile();
    void requestSaveFile();
    void requestSaveAsFile();
    void insertNodeType(std::string type);
    void insertTextNode();
    void duplicateSelectedNode();
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
    void refreshDocumentUi();
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
        SaveAs,
    };

    enum class CenterTab {
        Canvas,
        Source,
        References,
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
        std::optional<dsl::DesignNodeId> selected) const;
    [[nodiscard]] static std::string formatValue(
        const std::optional<dsl::DesignValue>& value,
        const std::optional<std::string>& reference);
    [[nodiscard]] static std::string formatDiagnostic(
        const dsl::DesignDiagnostic& diagnostic);

    dsl::DesignPreviewWorkbench workbench_{};
    OutlineModel outlineModel_;
    widgets::TreeController outlineController_{};
    widgets::DataGridController referencesController_{};
    core::VirtualListController diagnosticsController_{};
    std::vector<ReferenceEntry> referenceRows_{};
    CenterTab centerTab_{CenterTab::Canvas};
    std::string sourceFile_{"<sample>"};
    std::string statusMessage_{};
    FileDialogRequester fileDialogRequester_{};
    PendingFileDialog pendingFileDialog_{PendingFileDialog::None};
    bool darkMode_{true};
    style::ControlDensity density_{style::ControlDensity::Comfortable};
    float deviceScale_{1.0F};
    float fontScale_{1.0F};
    bool highContrast_{false};
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
    std::map<std::string, core::StateStore::ObserverId> propertyObservers_{};
    bool syncingPropertyState_{false};
    app::AppShell shell_;
};

}  // namespace lumen::designer_app
