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
#include "lumen/core/scroll.h"
#include "lumen/core/virtual_list.h"
#include "lumen/dsl/design_resources.h"
#include "lumen/dsl/design_mapping.h"
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
#include "lumen/widgets/splitter.h"
#include "lumen/widgets/statusbar.h"
#include "lumen/widgets/toolbar.h"
#include "lumen/widgets/tree.h"

namespace lumen::designer_app {

class DesignerApp {
  public:
    using FileDialogRequester =
        std::function<std::string(bool forSave, const std::string& defaultName)>;

    DesignerApp();
    ~DesignerApp();

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
    struct SaveConflict {
        std::string filename{};
        bool project{false};
        std::optional<std::uint64_t> revision{};
        std::map<std::string, std::uint64_t> pageRevisions{};
        bool canOverwrite{false};
    };

    class OfflineRuntimeContext final : public dsl::DesignRuntimeContext {
      public:
        explicit OfflineRuntimeContext(DesignerApp* owner) : owner_(owner) {}

        [[nodiscard]] bool validatesReferences() const override;
        [[nodiscard]] bool resolveReference(
            dsl::DesignReferenceKind kind, const std::string& name,
            dsl::DesignReference& out) const override;
        [[nodiscard]] bool resolveReferenceForNode(
            const dsl::DesignNode& node, dsl::DesignReferenceKind kind,
            const std::string& name,
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
    void registerCommands();
    [[nodiscard]] core::Widget buildUi();
    [[nodiscard]] core::Widget buildPreviewWindow();
    // B 画布优先（design/designer-ide.html?variant=b，workspace-design §3/§3.1）：
    // 标题栏 44 + 主工具 48 + 工作区（48 rail + 224 left + 6 + center + 6 + 280
    // right）+ 状态栏 28。旧 buildToolbar/buildBody 保留为兼容入口，内部转调
    // 新的 B 组件，保持 designer-toolbar 等既有 key 可测。
    [[nodiscard]] core::Widget buildTitleBar();
    [[nodiscard]] core::Widget buildMenuPopover();
    // 运行预览模态覆盖层：仅在运行/调试会话中出现，停止/Esc 即隐藏，
    // 不再常驻第二个 OS 窗口（HTML 稿亦为 modal 近似）。
    [[nodiscard]] core::Widget buildRunPreviewOverlay();
    [[nodiscard]] core::Widget buildMainTools();
    [[nodiscard]] core::Widget buildRail();
    [[nodiscard]] core::Widget buildDocumentTabs();
    [[nodiscard]] core::Widget buildCanvasControls();
    [[nodiscard]] core::Widget buildCrumbBar();
    [[nodiscard]] core::Widget buildBottomDock();
    [[nodiscard]] core::Widget buildStatusBar();
    [[nodiscard]] core::Widget buildCenterPanel();
    [[nodiscard]] core::Widget buildWorkspace();
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
    // B 工作区状态（workspace-design §3/§4.4）：专注、左栏折叠、底部折叠、
    // 中心视图（设计/分屏/快照）与底部页签（问题/引用）。切换均不触碰文档
    // dirty/undo（§4.4），仅 markDirty 重建。
    void toggleZen();
    void toggleLeftCollapsed();
    void toggleBottom();
    void resetLayout();
    void showToolbox();
    void focusLayoutContext();
    void requestOpenFile();
    void requestNewProjectFile();
    void requestSaveFile();
    void requestSaveAsFile();
    void captureSaveConflict(const std::string& filename, bool project);
    void resolveSaveConflict(bool overwrite);
    [[nodiscard]] bool saveProjectFileAtConflict(
        const std::string& filename, const SaveConflict* approvedConflict);
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
    void canvasSelectionSession(core::DragPhase phase, core::Offset position,
                                const std::string& sourceKey);
    void endCanvasSelectionSession();
    void selectCanvasNodesInRect(core::Rect selection);
    [[nodiscard]] core::Widget buildCanvasSelectionOverlay() const;
    void toolboxDragSession(core::DragPhase phase, core::Offset position,
                            const std::string& sourceKey);
    void endToolboxDragSession();
    [[nodiscard]] core::Widget buildToolboxDragOverlay() const;
    void canvasResizeSession(core::DragPhase phase, core::Offset position,
                             const std::string& sourceKey);
    void endCanvasResizeSession();
    void adjustCanvasZoom(float factor,
                          std::optional<core::Offset> anchor = std::nullopt);
    void panCanvas(core::Offset delta);
    void resetCanvasView();
    // 适合视图（design/designer-canvas.html）：画板按画布内容区缩小
    // 居中（不放大；放得下时 100% 居中）。
    void fitCanvasView();
    // 应用侧 ScrollView 滚动接线（lumen-scroll-design §3.4/§4）：框架把
    // 滚轮/滚动条/键盘/语义滚动解析到视口节点后经 onWheel/onScrollDrag
    // 交回应用，这里按视口 key 驱动各自的 ScrollController，offset 由
    // 各 build* 写回 scrollOffset。
    [[nodiscard]] core::ScrollController* scrollControllerForKey(
        std::string_view key);
    [[nodiscard]] core::ScrollController* scrollControllerFor(
        const core::RenderNode& viewport);
    [[nodiscard]] float panelScrollOffset(std::string_view key) const;
    bool handlePanelWheel(const core::RenderNode* viewport,
                          core::Offset delta);
    bool handlePanelScrollDrag(const core::RenderNode* viewport,
                               core::Offset delta,
                               core::ScrollDragPhase phase,
                               std::uint64_t nowMs);
    bool advancePanelScrollFling(app::AppShell& shell, std::uint64_t nowMs);
    [[nodiscard]] static float snapCanvasCoordinate(float value, float maximum,
                                                    float threshold);
    // 拖动换位吸附：阈值内取最近候选（并记命中参考线），否则回落
    // snapCanvasCoordinate 的网格吸附。
    [[nodiscard]] float snapDragCoordinate(
        float value, const std::vector<float>& snaps,
        const std::vector<float>& guides, float gridMaximum, float threshold,
        std::optional<float>& activeGuide) const;
    void refreshDocumentUi();
    void clearProjectSession();
    void syncActiveProjectDocument();
    [[nodiscard]] std::filesystem::path projectRootPath() const;
    [[nodiscard]] std::filesystem::path projectRootPath(
        const std::string& manifest) const;
    [[nodiscard]] std::filesystem::path projectRelativePath(
        const std::string& relative,
        const std::string& manifest) const;
    [[nodiscard]] std::filesystem::path projectPagePath(
        const dsl::DesignProjectPage& page) const;
    [[nodiscard]] std::filesystem::path projectPagePath(
        const dsl::DesignProjectPage& page,
        const std::string& manifest) const;
    void appendProjectDiagnostic(dsl::DesignError diagnostic,
                                 std::string documentId = {});
    [[nodiscard]] bool diagnosticActionable(std::size_t index) const;
    void clearPropertyObservers();
    void registerPropertyBinding(dsl::DesignNodeId id,
                                  const dsl::DesignPreviewProperty& property);
    void registerReferenceBinding(dsl::DesignNodeId id,
                                  const dsl::DesignPreviewProperty& property);
    void rebuildPreviewKeys();
    [[nodiscard]] std::string previewKeyForNodeId(
        dsl::DesignNodeId id) const;
    [[nodiscard]] std::string previewKeyForDesignNode(
        const dsl::DesignNode& node) const;

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

    // B 中心视图（designer-ide.html variant-b）：设计（仅画布）、分屏
    // （画布+只读快照）、快照（全宽只读）。引用移至底部，中心不再有
    // References 页（workspace-design §3 表格）。
    enum class CenterView {
        Design,
        Split,
        Source,
    };

    // B 底部页签（workspace-design §3）：问题/引用常驻底栏，C 才有布局工具。
    enum class BottomTab {
        Diagnostics,
        References,
    };

    enum class CenterTab {
        Canvas,
        Source,
        References,
    };

    // 左面板 B（designer-ide.html variant-b dock-tabs）：控件/结构按需切换，
    // 默认控件（与 A 的同时显示不同；workspace-design §3 表格）。
    enum class LeftPanelTab {
        Outline,
        Toolbox,
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
    void clearImageResources();
    void collectImageResources(const dsl::DesignNode& node,
                               const std::string& path,
                               std::set<std::string>& activeUris);
    void applyImageResources(core::Widget& widget) const;
    [[nodiscard]] static std::string imageUriForSource(
        std::string_view source);

    dsl::DesignPreviewWorkbench workbench_{};
    OfflineRuntimeContext runtimeContext_;
    // B 预览独立密度（ThemeScope 包裹画布，工作台 Compact 不影响预览
    // Comfortable；visual-system §3.6）。shared_ptr 存活覆盖布局期。
    std::shared_ptr<void> previewScopeData_{};
    OutlineModel outlineModel_;    widgets::TreeController outlineController_{};
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
    // B 默认：Toolbox（画布优先先给插入入口，大纲按需展开；HTML leftTab
    // variant-b 默认 toolbox）。旧 CenterTab 保留兼容映射到 CenterView。
    LeftPanelTab leftPanelTab_{LeftPanelTab::Toolbox};
    CenterView centerView_{CenterView::Design};
    BottomTab bottomTab_{BottomTab::Diagnostics};
    // B 默认收起（workspace-design §3.1 底栏初始 36；A/B 折叠，C/D 展开）。
    bool bottomOpen_{false};
    bool zenMode_{false};
    bool leftCollapsed_{false};
    // B 外壳分栏（workspace-design §3.1：左 224 + 48 rail，右 280，轨道 6，
    // 左右边界不低于 180/232，上限 400；Compact 12 步进，双击复位）。
    widgets::SplitterController leftSplitter_{224.0F};
    widgets::SplitterController rightSplitter_{716.0F};
    // 右栏偏好宽（默认 280，上限 400；拖动经 onOffsetChanged 回填，resize/
    // 折叠时恢复偏好，中心吃增量）。
    float rightWidthPreference_{280.0F};
    // 标题栏菜单 popover（B 文件/编辑/视图；HTML .popover，Esc 关闭）。
    std::optional<std::string> openMenu_{};
    // 运行预览覆盖层是否打开。仅 startPreview 成功置位，stopPreview/Esc
    // 复位；窗口关闭等价停止会话。
    bool runPreviewOpen_{false};
    PreviewSessionMode previewSessionMode_{PreviewSessionMode::Stopped};
    std::string sourceSnapshot_{};
    std::size_t sourceFocusLine_{0};
    std::string sourceFile_{"<sample>"};
    std::string statusMessage_{};
    std::vector<dsl::DesignNode> clipboardNodes_{};
    FileDialogRequester fileDialogRequester_{};
    PendingFileDialog pendingFileDialog_{PendingFileDialog::None};
    std::optional<SaveConflict> saveConflict_{};
    bool darkMode_{true};
    // B 工作台显式 Compact（visual-system §3.6）；预览独立 Comfortable，
    // 互不影响（workspace-design §3.1/§4.4）。
    style::ControlDensity density_{style::ControlDensity::Compact};
    style::ControlDensity previewDensity_{style::ControlDensity::Comfortable};
    float deviceScale_{1.0F};
    dsl::DesignCoordinateTransform canvasTransform_{};
    float fontScale_{1.0F};
    bool highContrast_{false};
    // 参考线（网格点 + 标尺 + 选中框/手柄）默认可见：对齐辅助是画布的
    // 常驻预期（workspace-design §8.1 追加；原 B 落地为默认关）。
    bool canvasGuidesEnabled_{true};
    // 容器边界（designer-canvas.html，§8.7）：页面虚线边界 + 所选父容
    // 器虚线轮廓与类型标注，独立开关，默认开。
    bool canvasBoundsEnabled_{true};
    // 对齐参考线（十字线 + 坐标徽标，§8.8 独立开关，默认开）。
    bool canvasAlignEnabled_{true};
    // Space 按住态（§8.8：Space + 拖动 = 平移画布；onKeyUp 复位）。
    bool spacePanPressed_{false};
    core::Offset spacePanAnchor_{};
    core::Offset spacePanStart_{};
    PreviewStateMode previewStateMode_{PreviewStateMode::None};
    std::string previewStateKey_{};
    std::map<dsl::DesignNodeId, std::string> previewKeys_{};
    bool outlineDragActive_{false};
    dsl::DesignNodeId outlineDragId_{0};
    dsl::DesignNodeId outlineDragParentId_{0};
    std::size_t outlineDragIndex_{0};
    std::size_t outlineDragInsertIndex_{0};
    std::string outlineDragSlot_{};
    core::Offset outlineDragPointer_{};
    bool canvasSelectionActive_{false};
    core::Offset canvasSelectionStart_{};
    core::Offset canvasSelectionPointer_{};
    bool toolboxDragActive_{false};
    std::string toolboxDragType_{};
    core::Offset toolboxDragPointer_{};
    // 面板 ScrollView（工具箱/工程/源码/属性/运行预览）的应用侧滚动状态；
    // 键 = 视口 key，全部纵向。VirtualList/List/Tree 等源视口由框架直驱，
    // 不进这张表。
    std::map<std::string, core::ScrollController> panelScrolls_{};
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
    core::Offset canvasResizeCoordinateOrigin_{};
    core::Size canvasResizeLimits_{};
    float canvasResizeZoom_{1.0F};
    std::string canvasResizeDocumentId_{};
    std::uint64_t canvasResizeRevision_{0};
    bool canvasResizePositionEditable_{false};
    // 拖动换位对齐参考（§8.3）：Start 时按父框与同父子节点收集吸附候
    // 选（拖动节点左/中/右对齐目标坐标）与对应参考线位置（设计坐标，
    // 成对等长）；命中阈值内吸附并记 ActiveGuide 供画虚线。
    std::vector<float> canvasResizeSnapXs_{};
    std::vector<float> canvasResizeGuideXs_{};
    std::vector<float> canvasResizeSnapYs_{};
    std::vector<float> canvasResizeGuideYs_{};
    std::optional<float> canvasResizeActiveGuideX_{};
    std::optional<float> canvasResizeActiveGuideY_{};
    std::map<std::string, core::StateStore::ObserverId> propertyObservers_{};
    bool syncingPropertyState_{false};
    dsl::DesignResourcePolicy resourcePolicy_{};
    dsl::DesignResourceAuthorizer resourceAuthorizer_{resourcePolicy_};
    std::shared_ptr<render::ResourceManager> resourceManager_{
        std::make_shared<render::ResourceManager>()};
    struct ImageResource {
        render::ResourceHandle handle{};
        dsl::DesignPreviewToken token{};
    };
    std::map<std::string, ImageResource> imageResources_{};
    std::map<std::string, std::string> imageResourceAliases_{};
    std::optional<dsl::DesignPreviewGeneration> imageGeneration_{};
    dsl::DesignPreviewToken imageToken_{};
    std::uint64_t imageFrameGeneration_{0};
    std::vector<dsl::DesignDiagnostic> diagnostics_{};
    dsl::ProjectStore projectStore_{};
    dsl::DocumentStore projectDocumentStore_{};
    std::optional<dsl::DesignProject> project_{};
    std::string projectFile_{};
    std::uint64_t projectRevision_{0};
    std::map<std::string, dsl::DesignWorkbenchSession> projectSessions_{};
    std::map<std::string, std::string> projectDocumentPaths_{};
    std::map<std::string, std::uint64_t> projectDocumentRevisions_{};
    std::vector<dsl::DesignError> projectDiagnostics_{};
    std::vector<std::string> projectDiagnosticDocumentIds_{};
    std::string activeProjectDocumentId_{};
    app::AppShell shell_;
    app::AppShell previewShell_;
};

}  // namespace lumen::designer_app
