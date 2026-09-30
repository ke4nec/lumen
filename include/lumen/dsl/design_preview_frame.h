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

    void clear();

    [[nodiscard]] bool hasFrame() const { return hasFrame_; }
    [[nodiscard]] std::uint64_t generation() const { return generation_; }
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
    bool hasFrame_{false};
};

}  // namespace lumen::dsl
