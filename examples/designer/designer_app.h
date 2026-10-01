#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/dsl/design_workbench.h"
#include "lumen/widgets/tree.h"

namespace lumen::designer_app {

class DesignerApp {
  public:
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
    [[nodiscard]] bool saveDesignFile(const std::string& filename);

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
    [[nodiscard]] core::Widget buildBody();
    [[nodiscard]] core::Widget buildOutlinePanel();
    [[nodiscard]] core::Widget buildPreviewPanel();
    [[nodiscard]] core::Widget buildPropertiesPanel();
    [[nodiscard]] core::Widget buildDiagnosticsPanel();

    void rebuildOutline();
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
    void insertTextNode();
    void duplicateSelectedNode();
    void removeSelectedNode();
    void moveSelectedNode(int offset);
    void refreshDocumentUi();
    void clearPropertyObservers();
    void registerPropertyBinding(dsl::DesignNodeId id,
                                  const dsl::DesignPreviewProperty& property);

    enum class PreviewStateMode {
        None,
        Hovered,
        Pressed,
        Focused,
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
    std::string sourceFile_{"<sample>"};
    bool darkMode_{true};
    style::ControlDensity density_{style::ControlDensity::Comfortable};
    float deviceScale_{1.0F};
    float fontScale_{1.0F};
    bool highContrast_{false};
    PreviewStateMode previewStateMode_{PreviewStateMode::None};
    std::string previewStateKey_{};
    std::map<std::string, core::StateStore::ObserverId> propertyObservers_{};
    bool syncingPropertyState_{false};
    app::AppShell shell_;
};

}  // namespace lumen::designer_app
