#include "lumen/dsl/design_preview_frame.h"

#include <utility>

namespace lumen::dsl {

bool DesignPreviewFrame::sameDocument(std::string_view documentId) const {
    // Empty IDs are still identities: two imported documents without a store
    // identity share the empty identity, while an explicit ID must not reuse
    // an unrelated imported frame.
    return !hasFrame_ || documentId_ == documentId;
}

void DesignPreviewFrame::setDiagnostics(
    const std::vector<DesignError>& errors, std::string_view documentId) {
    diagnostics_.clear();
    for (const auto& error : errors) {
        auto diagnostic = DesignDiagnostic::fromError(error);
        diagnostic.documentId = std::string{documentId};
        appendDesignDiagnostic(diagnostics_, std::move(diagnostic));
    }
}

bool DesignPreviewFrame::update(const DesignDocument& document,
                                DesignRuntimeContext& context) {
    ++rebuildCount_;
    const auto compiled = compileDesignDocument(document, context);
    setDiagnostics(compiled.diagnostics, document.documentId);
    if (!compiled.ok()) {
        // Reference failures still produce a traced placeholder Widget. Keep
        // it as the current frame so an offline preview remains inspectable.
        if (!compiled.trace.nodes.empty()) {
            if (session_) session_->close();
            widget_ = compiled.root;
            trace_ = compiled.trace;
            sourceMap_ = compiled.sourceMap;
            session_ = compiled.session;
            documentId_ = document.documentId;
            hasFrame_ = true;
            ++generation_;
            return false;
        }
        if (!sameDocument(document.documentId)) {
            const auto errors = compiled.diagnostics;
            clear();
            setDiagnostics(errors, document.documentId);
        }
        if (compiled.session) compiled.session->close();
        return false;
    }

    if (session_) session_->close();
    widget_ = compiled.root;
    trace_ = compiled.trace;
    sourceMap_ = compiled.sourceMap;
    session_ = compiled.session;
    documentId_ = document.documentId;
    hasFrame_ = true;
    ++generation_;
    return true;
}

bool DesignPreviewFrame::update(const DesignDocument& document) {
    DesignRuntimeContext context;
    return update(document, context);
}

void DesignPreviewFrame::clear() {
    if (session_) session_->close();
    widget_ = core::Widget{};
    trace_ = CompileTrace{};
    sourceMap_ = DesignSourceMap{};
    session_.reset();
    diagnostics_.clear();
    documentId_.clear();
    hasFrame_ = false;
    ++generation_;
}

}  // namespace lumen::dsl
