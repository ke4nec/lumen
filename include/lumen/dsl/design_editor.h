#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumen/dsl/design_codec.h"

namespace lumen::dsl {

enum class DesignSelectionMode : std::uint8_t {
    Replace,
    Add,
    Toggle,
};

// Selection is session state. It deliberately contains document ids only;
// runtime widget identities and preview state never enter the document.
struct DesignSelection {
    std::set<DesignNodeId> ids{};
    std::optional<DesignNodeId> primary{};
    std::optional<DesignNodeId> anchor{};
    std::optional<DesignNodeId> captured{};
    std::string focusPanel{};

    bool operator==(const DesignSelection&) const = default;
};

class DesignSelectionModel {
  public:
    [[nodiscard]] const DesignSelection& state() const { return state_; }

    // Selection ids belong to the active design document. Switching document
    // identity clears ids that could otherwise collide with the new tree.
    void setDocument(const DesignDocument& document);

    [[nodiscard]] bool select(DesignNodeId id, DesignSelectionMode mode,
                              const DesignDocument& document);
    [[nodiscard]] bool selectRange(DesignNodeId id,
                                   const DesignDocument& document);
    [[nodiscard]] bool setSelection(
        std::set<DesignNodeId> ids, std::optional<DesignNodeId> primary,
        std::optional<DesignNodeId> anchor, const DesignDocument& document);
    [[nodiscard]] bool capture(DesignNodeId id);
    void releaseCapture() { state_.captured.reset(); }
    void clear();
    void setFocusPanel(std::string panel) { state_.focusPanel = std::move(panel); }

  private:
    [[nodiscard]] static bool contains(const DesignNode& node,
                                       DesignNodeId id);
    [[nodiscard]] static bool validId(DesignNodeId id,
                                      const DesignDocument& document);
    static void collectOrder(const DesignNode& node,
                             std::vector<DesignNodeId>& order);
    void activateDocument(const DesignDocument& document);

    DesignSelection state_{};
    std::string documentId_{};
};

struct DesignDocumentCommand {
    using Operation = std::function<bool(DesignDocument&)>;
    using Precondition = std::function<bool(const DesignDocument&)>;

    std::string label{};
    std::set<DesignNodeId> affectedIds{};
    std::string mergeKey{};
    Precondition precondition{};
    Operation apply{};
    Operation revert{};
};

// A transaction owns a private working document. Failed commands never leak
// partial changes to the caller's document or selection.
class DesignDocumentTransaction {
  public:
    DesignDocumentTransaction(const DesignDocument& document,
                              const DesignSelection& selection,
                              std::uint64_t baseRevision);

    [[nodiscard]] bool apply(DesignDocumentCommand command);
    void setSelectionAfter(DesignSelection selection) {
        selectionAfter_ = std::move(selection);
    }
    void setLabel(std::string label) { label_ = std::move(label); }

    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] bool empty() const { return commands_.empty(); }
    [[nodiscard]] std::uint64_t baseRevision() const { return baseRevision_; }
    [[nodiscard]] const DesignDocument& before() const { return before_; }
    [[nodiscard]] const DesignDocument& after() const { return working_; }
    [[nodiscard]] const DesignSelection& selectionBefore() const {
        return selectionBefore_;
    }
    [[nodiscard]] const DesignSelection& selectionAfter() const {
        return selectionAfter_;
    }
    [[nodiscard]] const std::vector<DesignDocumentCommand>& commands() const {
        return commands_;
    }

  private:
    DesignDocument before_{};
    DesignDocument working_{};
    DesignSelection selectionBefore_{};
    DesignSelection selectionAfter_{};
    std::uint64_t baseRevision_{0};
    std::string label_{};
    std::vector<DesignDocumentCommand> commands_{};
    bool failed_{false};

    friend class DesignDocumentHistory;
};

class DesignDocumentHistory {
  public:
    [[nodiscard]] DesignDocumentTransaction begin(
        const DesignDocument& document,
        const DesignSelection& selection = {}) const;
    [[nodiscard]] bool commit(DesignDocument& document,
                              DesignSelection& selection,
                              DesignDocumentTransaction transaction);
    [[nodiscard]] bool undo(DesignDocument& document,
                            DesignSelection& selection);
    [[nodiscard]] bool redo(DesignDocument& document,
                            DesignSelection& selection);

    void markSaved() { savedRevision_ = documentRevision_; }
    void clear();

    [[nodiscard]] bool canUndo() const { return cursor_ != 0; }
    [[nodiscard]] bool canRedo() const { return cursor_ < entries_.size(); }
    [[nodiscard]] bool dirty() const {
        return documentRevision_ != savedRevision_;
    }
    [[nodiscard]] std::uint64_t documentRevision() const {
        return documentRevision_;
    }
    [[nodiscard]] std::uint64_t savedRevision() const {
        return savedRevision_;
    }
    [[nodiscard]] std::size_t undoSize() const { return cursor_; }
    [[nodiscard]] std::size_t redoSize() const {
        return entries_.size() - cursor_;
    }

  private:
    struct Entry {
        DesignDocument before{};
        DesignDocument after{};
        DesignSelection selectionBefore{};
        DesignSelection selectionAfter{};
        std::uint64_t beforeRevision{0};
        std::uint64_t afterRevision{0};
        std::string label{};
        std::set<DesignNodeId> affectedIds{};
        std::string mergeKey{};
        std::vector<DesignDocumentCommand> commands{};
    };

    std::vector<Entry> entries_{};
    std::size_t cursor_{0};
    std::uint64_t documentRevision_{0};
    std::uint64_t savedRevision_{0};
    std::uint64_t nextRevision_{1};
};

enum class DesignDiagnosticSeverity : std::uint8_t {
    Info,
    Warning,
    Error,
};

enum class DesignDiagnosticStage : std::uint8_t {
    Read,
    Parse,
    Migrate,
    Schema,
    Reference,
    Compile,
    Save,
};

enum class DesignDiagnosticRecoverability : std::uint8_t {
    Continue,
    Placeholder,
    KeepLastFrame,
    BlockSave,
};

struct DesignDiagnosticRelated {
    std::string file{};
    std::optional<DesignSourceSpan> sourceSpan{};
    DesignNodeId nodeId{0};
    std::string label{};

    bool operator==(const DesignDiagnosticRelated&) const = default;
};

struct DesignDiagnostic {
    std::string code{};
    DesignDiagnosticSeverity severity{DesignDiagnosticSeverity::Error};
    DesignDiagnosticStage stage{DesignDiagnosticStage::Compile};
    std::string message{};
    std::string expected{};
    std::string found{};
    std::string file{};
    std::optional<DesignSourceSpan> sourceSpan{};
    std::string documentId{};
    DesignNodeId nodeId{0};
    std::string nodePath{};
    std::string property{};
    std::vector<DesignDiagnosticRelated> related{};
    DesignDiagnosticRecoverability recoverability{
        DesignDiagnosticRecoverability::KeepLastFrame};
    std::size_t occurrences{1};

    [[nodiscard]] std::string key() const;
    [[nodiscard]] static DesignDiagnostic fromError(
        const DesignError& error,
        std::optional<DesignDiagnosticStage> stage = std::nullopt);
    [[nodiscard]] static DesignDiagnostic fromDslError(
        const DslError& error,
        std::string code = "parse.error");
};

// Keeps the first diagnostic's location/message and counts repeated reports.
void appendDesignDiagnostic(std::vector<DesignDiagnostic>& diagnostics,
                            DesignDiagnostic diagnostic);

[[nodiscard]] const char* designDiagnosticStageName(
    DesignDiagnosticStage stage);
[[nodiscard]] const char* designDiagnosticRecoverabilityName(
    DesignDiagnosticRecoverability recoverability);

}  // namespace lumen::dsl
