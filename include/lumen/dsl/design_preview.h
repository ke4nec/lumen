#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "lumen/dsl/design_document.h"

namespace lumen::dsl {

// Runtime and visual values are session data. Neither map is serialized with
// the document or considered by DesignDocumentHistory.
class DesignPreviewState {
  public:
    [[nodiscard]] bool setRuntimeValue(DesignNodeId id, std::string property,
                                       DesignValue value,
                                       const DesignDocument& document);
    [[nodiscard]] bool setVisualOverride(DesignNodeId id, std::string property,
                                          DesignValue value,
                                          const DesignDocument& document);
    [[nodiscard]] bool setBindingSnapshot(std::string name,
                                           DesignValue value);

    [[nodiscard]] std::optional<DesignValue> value(
        DesignNodeId id, std::string_view property,
        const DesignDocument& document) const;
    [[nodiscard]] std::optional<DesignValue> bindingSnapshot(
        std::string_view name) const;

    void clearRuntimeValues() { runtimeValues_.clear(); }
    void clearVisualOverrides() { visualOverrides_.clear(); }
    void clearBindings() { bindingSnapshots_.clear(); }
    void clear() {
        clearRuntimeValues();
        clearVisualOverrides();
        clearBindings();
    }

    [[nodiscard]] bool empty() const {
        return runtimeValues_.empty() && visualOverrides_.empty() &&
               bindingSnapshots_.empty();
    }

  private:
    using NodeValues =
        std::map<DesignNodeId, std::map<std::string, DesignValue>>;

    [[nodiscard]] static const DesignNode* findNode(const DesignNode& node,
                                                     DesignNodeId id);
    [[nodiscard]] static bool setNodeValue(
        NodeValues& values, DesignNodeId id, std::string property,
        DesignValue value, const DesignDocument& document);

    NodeValues runtimeValues_{};
    NodeValues visualOverrides_{};
    std::map<std::string, DesignValue> bindingSnapshots_{};
};

}  // namespace lumen::dsl
