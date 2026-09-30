#include "lumen/dsl/design_workbench.h"

#include <map>
#include <utility>

namespace lumen::dsl {

bool DesignPreviewWorkbench::openLumenSource(
    const std::string& source, std::string filename,
    DesignRuntimeContext* context) {
    const auto sourceFile = filename;
    const auto parsed = parseLumenSource(source, std::move(filename));
    if (!parsed.ok()) {
        setError(*parsed.error);
        return false;
    }
    return openDocumentInternal(std::move(parsed.document), context,
                                sourceFile);
}

bool DesignPreviewWorkbench::openDesignSource(
    const std::string& source, std::string filename,
    DesignRuntimeContext* context) {
    const auto sourceFile = filename;
    const auto read = readDesignDocument(source, std::move(filename));
    if (!read.ok()) {
        setError(*read.error);
        return false;
    }
    return openDocumentInternal(std::move(read.document), context, sourceFile);
}

bool DesignPreviewWorkbench::openDocument(DesignDocument document,
                                          DesignRuntimeContext* context) {
    return openDocumentInternal(std::move(document), context, "<design>");
}

bool DesignPreviewWorkbench::openDocumentInternal(
    DesignDocument document, DesignRuntimeContext* context,
    std::string sourceFile) {
    document_ = std::move(document);
    sourceFile_ = sourceFile.empty() ? "<design>" : std::move(sourceFile);
    selection_.setDocument(*document_);
    return updateFrame(context);
}

bool DesignPreviewWorkbench::refresh(DesignRuntimeContext* context) {
    if (!document_.has_value()) {
        diagnostics_.clear();
        return false;
    }
    return updateFrame(context);
}

void DesignPreviewWorkbench::clear() {
    frame_.clear();
    selection_.clear();
    document_.reset();
    diagnostics_.clear();
    sourceFile_ = "<design>";
}

bool DesignPreviewWorkbench::updateFrame(DesignRuntimeContext* context) {
    const bool compiled = context == nullptr
                              ? frame_.update(*document_)
                              : frame_.update(*document_, *context);
    diagnostics_ = frame_.diagnostics();
    for (auto& diagnostic : diagnostics_) {
        if (diagnostic.file.empty() || diagnostic.file == "<design>") {
            diagnostic.file = sourceFile_;
        }
    }
    return compiled;
}

void DesignPreviewWorkbench::setError(const DesignError& error) {
    diagnostics_.clear();
    auto diagnostic = DesignDiagnostic::fromError(error);
    appendDesignDiagnostic(diagnostics_, std::move(diagnostic));
}

const DesignNode* DesignPreviewWorkbench::findNode(const DesignNode& node,
                                                   DesignNodeId id) {
    if (node.id == id) return &node;
    for (const auto& child : node.children) {
        if (const auto* found = findNode(child, id); found != nullptr) {
            return found;
        }
    }
    for (const auto& [slot, children] : node.slots) {
        (void)slot;
        for (const auto& child : children) {
            if (const auto* found = findNode(child, id); found != nullptr) {
                return found;
            }
        }
    }
    return nullptr;
}

std::string DesignPreviewWorkbench::nodeKey(const DesignNode& node) {
    const auto found = node.properties.find("key");
    if (found == node.properties.end()) return {};
    const auto* value = std::get_if<std::string>(&found->second.value);
    return value == nullptr ? std::string{} : *value;
}

DesignPreviewOutlineNode DesignPreviewWorkbench::makeOutline(
    const DesignNode& node, std::string path) {
    DesignPreviewOutlineNode result{node.id, node.type, nodeKey(node),
                                    std::move(path), {}};
    result.children.reserve(node.children.size() + node.slots.size());
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        result.children.push_back(makeOutline(
            node.children[index],
            result.path + ".children[" + std::to_string(index) + "]"));
    }
    for (const auto& [slot, children] : node.slots) {
        for (std::size_t index = 0; index < children.size(); ++index) {
            result.children.push_back(makeOutline(
                children[index], result.path + ".slots[" + slot + "][" +
                                     std::to_string(index) + "]"));
        }
    }
    return result;
}

bool DesignPreviewWorkbench::selectNode(DesignNodeId id,
                                        DesignSelectionMode mode) {
    if (!document_.has_value()) return false;
    return selection_.select(id, mode, *document_);
}

bool DesignPreviewWorkbench::selectRuntimeIdentity(
    std::string_view identity, DesignSelectionMode mode) {
    const auto id = nodeForRuntimeIdentity(identity);
    return id.has_value() && selectNode(*id, mode);
}

std::optional<DesignNodeId> DesignPreviewWorkbench::nodeForRuntimeIdentity(
    std::string_view identity) const {
    for (const auto& [id, reference] : frame_.trace().nodes) {
        if (!reference.runtimeOnly && reference.runtimeIdentity == identity) {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<std::string> DesignPreviewWorkbench::runtimeIdentity(
    DesignNodeId id) const {
    const auto found = frame_.trace().nodes.find(id);
    if (found == frame_.trace().nodes.end() || found->second.runtimeOnly) {
        return std::nullopt;
    }
    return found->second.runtimeIdentity;
}

std::optional<DesignPreviewOutlineNode> DesignPreviewWorkbench::outline() const {
    if (!document_.has_value()) return std::nullopt;
    return makeOutline(document_->root, "root");
}

std::vector<DesignPreviewProperty> DesignPreviewWorkbench::properties(
    DesignNodeId id) const {
    std::vector<DesignPreviewProperty> result;
    if (!document_.has_value()) return result;
    const auto* node = findNode(document_->root, id);
    if (node == nullptr) return result;

    std::map<std::string, DesignPreviewProperty> byName;
    for (const auto& [name, value] : node->properties) {
        byName[name] = DesignPreviewProperty{name, value, std::nullopt};
    }
    for (const auto& [name, reference] : node->references) {
        auto& property = byName[name];
        property.name = name;
        property.reference = reference;
    }
    result.reserve(byName.size());
    for (auto& [name, property] : byName) {
        (void)name;
        result.push_back(std::move(property));
    }
    return result;
}

}  // namespace lumen::dsl
