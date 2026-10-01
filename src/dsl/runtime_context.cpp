#include "lumen/dsl/runtime_context.h"

#include "lumen/dsl/design_document.h"

namespace lumen::dsl {

DesignComponentResult MapDesignRuntimeContext::buildComponent(
    const DesignNode& node,
    const DesignComponentContext& componentContext) const {
    const auto found = componentBuilders_.find(node.type);
    if (found == componentBuilders_.end()) {
        return DesignComponentResult{
            std::nullopt, {}, "component.missing",
            "no component builder is registered for node type '" + node.type +
                "'"};
    }
    return found->second(node, componentContext);
}

}  // namespace lumen::dsl
