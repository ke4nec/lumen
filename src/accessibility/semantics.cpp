#include "lumen/accessibility/semantics.h"

#include <algorithm>
#include <utility>

namespace lumen::accessibility {
namespace {

using core::RenderNode;
using core::WidgetType;

SemanticsRole defaultRoleFor(const RenderNode& node, bool isRoot) {
    switch (node.type) {
        case WidgetType::Button:
            return SemanticsRole::Button;
        case WidgetType::TextField:
            return SemanticsRole::TextField;
        case WidgetType::Text:
            return SemanticsRole::Text;
        case WidgetType::Checkbox:
            return SemanticsRole::Checkbox;
        case WidgetType::Radio:
            return SemanticsRole::Radio;
        case WidgetType::Switch:
            return SemanticsRole::Switch;
        case WidgetType::ListView:
            return SemanticsRole::List;
        case WidgetType::ScrollView:
        case WidgetType::VirtualList:  // M3：虚拟列表同列表语义
            return SemanticsRole::List;
        case WidgetType::Image:  // M3：图像（label/value 保留可访问名）
            return SemanticsRole::Image;
        case WidgetType::Slider:
            return SemanticsRole::Slider;
        case WidgetType::ProgressBar:
            return SemanticsRole::ProgressBar;
        case WidgetType::Tooltip:
            return SemanticsRole::Text;
        case WidgetType::Dropdown:  // M6：展开选择
        case WidgetType::Tabs:
        case WidgetType::Icon:
        case WidgetType::ThemeScope:
            return SemanticsRole::Group;
        case WidgetType::Grid:  // M3：网格归组语义
        case WidgetType::Container:
        case WidgetType::Row:
        case WidgetType::Column:
        case WidgetType::Stack:
        case WidgetType::FocusScope:
            return isRoot ? SemanticsRole::Window : SemanticsRole::Group;
    }
    return SemanticsRole::Group;
}

std::uint32_t defaultActionsFor(const RenderNode& node) {
    switch (node.type) {
        case WidgetType::Button:
            return kActionFocus | kActionActivate;
        case WidgetType::TextField:
            return kActionFocus | kActionSetValue;
        case WidgetType::Checkbox:
        case WidgetType::Switch:
        case WidgetType::Radio:
            return kActionFocus | kActionActivate;
        case WidgetType::Slider:
            return kActionFocus | kActionSetValue;
        case WidgetType::ScrollView:
        case WidgetType::ListView:
        case WidgetType::VirtualList:
            return kActionScroll;
        case WidgetType::Image:
            return kActionFocus;  // 可聚焦/可访问（无激活语义）
        default:
            return 0;
    }
}

bool intersectsAll(const core::Rect& bounds,
                   const std::vector<core::Rect>& viewports) {
    for (const auto& viewport : viewports) {
        if (!bounds.intersects(viewport)) {
            return false;
        }
    }
    return true;
}

// 递归收集；bounds 以根坐标累积。
void collectNodes(const RenderNode& node, core::Offset absolute, bool isRoot,
                  const SemanticsBuildOptions& options,
                  const std::vector<core::Rect>& clipViewports,
                  SemanticsTree& tree) {
    const core::Offset origin = absolute + node.offset;
    SemanticsNode semantic;
    semantic.id = node.identity;
    semantic.role = defaultRoleFor(node, isRoot);
    semantic.bounds = core::Rect{origin, node.size};
    semantic.actions = defaultActionsFor(node) | node.semanticsActions;
    // 滚动视口外（与任一裁剪视口不相交）→ 隐藏。
    if (!clipViewports.empty() &&
        !intersectsAll(semantic.bounds, clipViewports)) {
        semantic.flags |= kSemanticsHidden;
    }

    // 默认 label/value。
    switch (node.type) {
        case WidgetType::Button:
            semantic.label = node.text;
            break;
        case WidgetType::Text:
            semantic.label = node.text;
            break;
        case WidgetType::Checkbox:
        case WidgetType::Switch:
        case WidgetType::Radio:  // M6：单选同选中语义（组由应用管理）
            // 视觉系统：selected 与 checked 同样折算（resolver 已把
            // selected 计入选中视觉，语义保持一致，§7.3）。
            semantic.label = node.text;
            semantic.value =
                (node.checked || node.selected) ? "true" : "false";
            if (node.checked || node.selected) {
                semantic.flags |= kSemanticsChecked;
            }
            break;
        case WidgetType::Image:
            // M5：可访问名称保留（覆盖 → 资源路径）。
            semantic.label = node.imageSource;
            break;
        case WidgetType::Slider:
        case WidgetType::ProgressBar:
            // 值（0..100）为语义 value；label 依覆盖。
            semantic.value =
                node.bind.empty() ? node.value : node.text;
            break;
        case WidgetType::Dropdown:
            semantic.value = node.bind.empty() ? node.value : node.text;
            break;
        case WidgetType::Tooltip:
            semantic.label = node.text;
            break;
        case WidgetType::TextField:
            semantic.label = node.placeholder.empty() ? node.bind
                                                      : node.placeholder;
            // 密码字段不暴露文本（plan §3.3 隐私最小语义）。
            if (!(node.obscure && options.hideObscuredValues)) {
                semantic.value = node.text;
            }
            if (node.readOnly) {
                semantic.flags &= ~kSemanticsEnabled;
            }
            // M5：校验失败状态与视觉/hit/键盘一致暴露。
            if (node.invalid) {
                semantic.flags |= kSemanticsInvalid;
            }
            break;
        default:
            break;
    }
    // 视觉系统（visual-system §7.3）：disabled 同时影响视觉、命中、键盘
    // 与语义 flags；selected 暴露列表/工具栏选中语义。
    if (!node.enabled) {
        semantic.flags &= ~kSemanticsEnabled;
    }
    if (node.selected) {
        semantic.flags |= kSemanticsSelected;
    }
    // 应用覆盖。
    if (!node.semanticsLabel.empty()) {
        semantic.label = node.semanticsLabel;
    }
    if (!node.semanticsValue.empty()) {
        semantic.value = node.semanticsValue;
    }
    if (!node.semanticsRole.empty()) {
        SemanticsRole role = semantic.role;
        if (semanticsRoleFromName(node.semanticsRole, &role)) {
            semantic.role = role;
        }
    }
    // 焦点 flag。
    if (options.focus != nullptr &&
        !options.focus->focusedIdentity().empty() &&
        options.focus->focusedIdentity() == node.identity) {
        semantic.flags |= kSemanticsFocused;
    }

    for (const auto& child : node.children) {
        semantic.children.push_back(child.identity);
    }
    if (isRoot) {
        tree.rootId = semantic.id;
    }
    tree.nodes.emplace(semantic.id, std::move(semantic));
    // 裁剪视口传播：clipContent（滚动视口）节点的盒子加入子树裁剪栈。
    std::vector<core::Rect> childViewports = clipViewports;
    if (node.clipContent) {
        childViewports.push_back(core::Rect{origin, node.size});
    }
    for (const auto& child : node.children) {
        collectNodes(child, origin, false, options, childViewports, tree);
    }
}

}  // namespace

const char* semanticsRoleName(SemanticsRole role) {
    switch (role) {
        case SemanticsRole::Window:
            return "window";
        case SemanticsRole::Group:
            return "group";
        case SemanticsRole::Text:
            return "text";
        case SemanticsRole::Button:
            return "button";
        case SemanticsRole::TextField:
            return "textField";
        case SemanticsRole::Checkbox:
            return "checkbox";
        case SemanticsRole::Switch:
            return "switch";
        case SemanticsRole::List:
            return "list";
        case SemanticsRole::ListItem:
            return "listItem";
        case SemanticsRole::Dialog:
            return "dialog";
        case SemanticsRole::Image:
            return "image";
        case SemanticsRole::Slider:
            return "slider";
        case SemanticsRole::ProgressBar:
            return "progressBar";
        case SemanticsRole::Radio:
            return "radio";
    }
    return "unknown";
}

bool semanticsRoleFromName(const std::string& name, SemanticsRole* out) {
    const std::pair<const char*, SemanticsRole> kTable[] = {
        {"window", SemanticsRole::Window},
        {"group", SemanticsRole::Group},
        {"text", SemanticsRole::Text},
        {"button", SemanticsRole::Button},
        {"textField", SemanticsRole::TextField},
        {"text_field", SemanticsRole::TextField},
        {"checkbox", SemanticsRole::Checkbox},
        {"switch", SemanticsRole::Switch},
        {"list", SemanticsRole::List},
        {"listItem", SemanticsRole::ListItem},
        {"list_item", SemanticsRole::ListItem},
        {"dialog", SemanticsRole::Dialog},
        {"image", SemanticsRole::Image},
        {"slider", SemanticsRole::Slider},
        {"progressBar", SemanticsRole::ProgressBar},
        {"progress_bar", SemanticsRole::ProgressBar},
        {"radio", SemanticsRole::Radio},
    };
    for (const auto& [key, role] : kTable) {
        if (name == key) {
            if (out != nullptr) {
                *out = role;
            }
            return true;
        }
    }
    return false;
}

std::string semanticsActionsName(std::uint32_t actions) {
    std::string result;
    const auto append = [&result](const char* name) {
        if (!result.empty()) {
            result += "|";
        }
        result += name;
    };
    if ((actions & kActionFocus) != 0) {
        append("focus");
    }
    if ((actions & kActionActivate) != 0) {
        append("activate");
    }
    if ((actions & kActionSetValue) != 0) {
        append("setValue");
    }
    if ((actions & kActionScroll) != 0) {
        append("scroll");
    }
    if ((actions & kActionDismiss) != 0) {
        append("dismiss");
    }
    return result;
}

const SemanticsNode* SemanticsTree::find(const std::string& id) const {
    const auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

SemanticsTree buildSemanticsTree(const core::RenderNode& root,
                                 const SemanticsBuildOptions& options) {
    SemanticsTree tree;
    collectNodes(root, core::Offset{}, true, options, {}, tree);
    return tree;
}

SemanticsDiff diffSemanticsTrees(const SemanticsTree& previous,
                                 const SemanticsTree& current,
                                 const std::string& previousFocusedId,
                                 const std::string& currentFocusedId) {
    SemanticsDiff diff;
    diff.previousFocusedId = previousFocusedId;
    diff.currentFocusedId = currentFocusedId;

    // added / changed。
    for (const auto& [id, node] : current.nodes) {
        const auto it = previous.nodes.find(id);
        if (it == previous.nodes.end()) {
            diff.added.push_back(id);
        } else if (!(it->second == node)) {
            diff.changed.push_back(id);
        }
    }
    // removed。
    for (const auto& [id, node] : previous.nodes) {
        if (current.nodes.find(id) == current.nodes.end()) {
            diff.removed.push_back(id);
        }
    }
    return diff;
}

SemanticsActionStatus performSemanticsAction(
    const SemanticsTree& tree, const SemanticsActionContext& context,
    const std::string& nodeId, std::uint32_t action, const std::string& value,
    float scrollDeltaY) {
    const SemanticsNode* node = tree.find(nodeId);
    if (node == nullptr) {
        return SemanticsActionStatus::NodeMissing;
    }
    if ((node->actions & action) != action) {
        return SemanticsActionStatus::NotHandled;
    }
    // disabled 控件不响应语义 action（visual-system §10.3）；Scroll 与
    // Dismiss（modal barrier）不受影响。
    if ((node->flags & kSemanticsEnabled) == 0 && action != kActionScroll &&
        action != kActionDismiss) {
        return SemanticsActionStatus::NotHandled;
    }
    const core::RenderNode* renderNode =
        context.root != nullptr
            ? core::findNodeByIdentity(*context.root, nodeId)
            : nullptr;

    if (action == kActionFocus) {
        if (context.focus == nullptr || renderNode == nullptr) {
            return SemanticsActionStatus::NotHandled;
        }
        // 与键盘遍历共用路径：字段建立编辑焦点，其余节点设置焦点。
        if (context.controller != nullptr) {
            context.controller->focusNode(*renderNode);
        } else {
            context.focus->setFocus(
                renderNode->key.empty() ? renderNode->identity
                                        : renderNode->key,
                renderNode->identity);
        }
        return SemanticsActionStatus::Handled;
    }

    if (action == kActionActivate || action == kActionDismiss) {
        // Checkbox/Switch：语义 activate 直接切换状态。
        if (context.controller != nullptr && renderNode != nullptr &&
            (renderNode->type == core::WidgetType::Checkbox ||
             renderNode->type == core::WidgetType::Switch)) {
            context.controller->toggleChecked(*renderNode);
            return SemanticsActionStatus::Handled;
        }
        if (context.handlers == nullptr || renderNode == nullptr ||
            renderNode->onClick.empty()) {
            return SemanticsActionStatus::NotHandled;
        }
        const auto handler = context.handlers->find(renderNode->onClick);
        if (handler == context.handlers->end()) {
            return SemanticsActionStatus::NotHandled;
        }
        handler->second();
        return SemanticsActionStatus::Handled;
    }

    if (action == kActionSetValue) {
        if (context.controller != nullptr && renderNode != nullptr &&
            renderNode->type == core::WidgetType::Slider) {
            return context.controller->setSliderValue(*renderNode, value)
                       ? SemanticsActionStatus::Handled
                       : SemanticsActionStatus::NotHandled;
        }
        if (context.controller == nullptr || renderNode == nullptr ||
            renderNode->type != core::WidgetType::TextField ||
            renderNode->bind.empty()) {
            return SemanticsActionStatus::NotHandled;
        }
        // 字段必须先持有编辑焦点才能写入。
        if (context.controller->focusedBind() != renderNode->bind) {
            return SemanticsActionStatus::NotHandled;
        }
        text::TextEditingValue next =
            context.controller->editingValue().replaceAll(
                value, text::graphemeCount(value));
        context.controller->setEditingValue(next);
        return SemanticsActionStatus::Handled;
    }

    if (action == kActionScroll) {
        if (!context.scrollSink) {
            return SemanticsActionStatus::NotHandled;
        }
        return context.scrollSink(nodeId, 0.0F, scrollDeltaY)
                   ? SemanticsActionStatus::Handled
                   : SemanticsActionStatus::NotHandled;
    }

    return SemanticsActionStatus::NotHandled;
}

}  // namespace lumen::accessibility
