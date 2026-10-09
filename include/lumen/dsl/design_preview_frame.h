#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "lumen/dsl/design_codec.h"
#include "lumen/dsl/design_editor.h"

namespace lumen::dsl {

// Holds the last valid preview compilation independently from the document
// being edited. Failed compiles expose diagnostics while keeping that frame
// only when the document identity is unchanged.
class DesignPreviewFrame {
  public:
    [[nodiscard]] bool update(const DesignDocument& document,
                              DesignRuntimeContext& context);
    [[nodiscard]] bool update(const DesignDocument& document);

    // Attempts an open without clearing the active document's frame on a
    // fatal compile failure. A traced reference placeholder can still replace
    // it; the caller must adopt that candidate document along with the frame.
    [[nodiscard]] bool tryReplaceDocument(const DesignDocument& document,
                                          DesignRuntimeContext& context);
    [[nodiscard]] bool tryReplaceDocument(const DesignDocument& document);
    // Compile a candidate once without publishing it. Preparation records a
    // rebuild attempt; publishing that result must not compile or count again.
    [[nodiscard]] DesignCompileResult prepareDocument(
        const DesignDocument& document, DesignRuntimeContext& context);
    [[nodiscard]] DesignCompileResult prepareDocument(
        const DesignDocument& document);
    [[nodiscard]] bool tryReplaceDocument(const DesignDocument& document,
                                          DesignCompileResult prepared);

    void clear();

    [[nodiscard]] bool hasFrame() const { return hasFrame_; }
    [[nodiscard]] std::uint64_t generation() const { return generation_; }
    // Number of preview compilation attempts, including recoverable failures.
    // Unlike generation(), this also exposes failed rebuild work to the
    // designer's performance diagnostics.
    [[nodiscard]] std::uint64_t rebuildCount() const {
        return rebuildCount_;
    }
    [[nodiscard]] const std::string& documentId() const { return documentId_; }
    [[nodiscard]] const core::Widget& widget() const { return widget_; }
    [[nodiscard]] const CompileTrace& trace() const { return trace_; }
    [[nodiscard]] const DesignSourceMap& sourceMap() const {
        return sourceMap_;
    }
    [[nodiscard]] const std::vector<DesignDiagnostic>& diagnostics() const {
        return diagnostics_;
    }
    [[nodiscard]] const std::shared_ptr<DesignRuntimeSession>& session() const {
        return session_;
    }

  private:
    [[nodiscard]] bool updateInternal(const DesignDocument& document,
                                      DesignRuntimeContext& context,
                                      bool preserveOnFailure);
    [[nodiscard]] bool publishPrepared(std::string_view documentId,
                                       DesignCompileResult prepared,
                                       bool preserveOnFailure);
    [[nodiscard]] bool sameDocument(std::string_view documentId) const;
    void setDiagnostics(const std::vector<DesignError>& errors,
                        std::string_view documentId);

    core::Widget widget_{};
    CompileTrace trace_{};
    DesignSourceMap sourceMap_{};
    std::shared_ptr<DesignRuntimeSession> session_{};
    std::vector<DesignDiagnostic> diagnostics_{};
    std::string documentId_{};
    std::uint64_t generation_{0};
    std::uint64_t rebuildCount_{0};
    bool hasFrame_{false};
};

}  // namespace lumen::dsl
