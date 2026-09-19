#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "lumen/core/geometry.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"

namespace lumen::accessibility {

// v0.3 阶段8C (plan §3.3): 平台无关语义树。
//
// 描述用户能感知和操作的对象，不暴露绘制命令。节点 id 复用 RenderNode
// 的稳定 identity，重建时按 identity diff 并保留辅助技术的焦点。
// 平台桥接（Windows UIA / Linux AT-SPI / macOS NSAccessibility）是可选
// 目标，桥接失败只关闭对应能力，不影响绘制和输入。

enum class SemanticsRole : std::uint8_t {
    Window,
    Group,
    Text,
    Button,
    TextField,
    Checkbox,
    Switch,
    List,
    ListItem,
    Dialog,
    Image,
    Slider,
    ProgressBar,
    Radio,
    // 集合控件（collection-controls-design §9.3）：树语义。追加在尾部，
    // 保持既有 role 枚举值不变。
    Tree,
    TreeItem,
    // 菜单类控件（menu-controls-design §8.1）与 Splitter（splitter-
    // design §8）：追加在尾部，同上约束。
    Menu,
    MenuItem,
    Splitter,
};

[[nodiscard]] const char* semanticsRoleName(SemanticsRole role);
// 名称 → role；未知返回 false（Widget.semanticsRole 解析用）。
[[nodiscard]] bool semanticsRoleFromName(const std::string& name,
                                         SemanticsRole* out);

enum SemanticsFlags : std::uint32_t {
    kSemanticsEnabled = 1U << 0,
    kSemanticsFocused = 1U << 1,
    kSemanticsSelected = 1U << 2,
    kSemanticsChecked = 1U << 3,
    kSemanticsHidden = 1U << 4,
    // M5：校验失败状态（与视觉 invalid/hit/键盘一致暴露）。
    kSemanticsInvalid = 1U << 5,
};

enum SemanticsActions : std::uint32_t {
    kActionFocus = 1U << 0,
    kActionActivate = 1U << 1,
    kActionSetValue = 1U << 2,
    kActionScroll = 1U << 3,
    kActionDismiss = 1U << 4,
    kActionExpand = 1U << 5,
    kActionCollapse = 1U << 6,
};

[[nodiscard]] std::string semanticsActionsName(std::uint32_t actions);

struct SemanticsNode {
    // 稳定 identity（与 RenderNode.identity 一致）。
    std::string id{};
    SemanticsRole role{SemanticsRole::Group};
    std::string label{};
    std::string value{};
    // 根（窗口）坐标的逻辑边界。
    core::Rect bounds{};
    std::uint32_t flags{kSemanticsEnabled};
    std::uint32_t actions{0};
    std::vector<std::string> children{};

    [[nodiscard]] bool operator==(const SemanticsNode&) const = default;
};

// id → 节点；rootId 指向根（Window）。
struct SemanticsTree {
    std::string rootId{};
    std::map<std::string, SemanticsNode> nodes{};

    [[nodiscard]] const SemanticsNode* find(const std::string& id) const;
    [[nodiscard]] std::size_t size() const { return nodes.size(); }
};

// 语义树构建输入：渲染树（布局结果）+ 焦点状态。TextField 的 value 取
// 绑定后的节点文本；Button 的 label 取按钮文本；Widget 的语义覆盖
//（label/value/role/actions）优先。
struct SemanticsBuildOptions {
    const core::FocusManager* focus{nullptr};
    // TextField 密码字段不暴露文本内容。
    bool hideObscuredValues{true};
};

[[nodiscard]] SemanticsTree buildSemanticsTree(const core::RenderNode& root,
                                               const SemanticsBuildOptions& options = {});

// M11：把 overlay 子树（浮动菜单等模态层）追加为主树根语义节点的附加
// 子树。overlay 独立布局，identity 命名空间独立——identity diff 只增删
// overlay 节点，主树语义 id 稳定。
void appendSemanticsSubtree(SemanticsTree& tree,
                            const core::RenderNode& subtree,
                            const SemanticsBuildOptions& options = {});

// identity diff：added/removed/changed（label/value/bounds/flags/actions/
// 子节点顺序任一变化即 changed）。
struct SemanticsDiff {
    std::vector<std::string> added{};
    std::vector<std::string> removed{};
    std::vector<std::string> changed{};
    // 焦点节点变化时给出新旧 id（空 = 无焦点）。
    std::string previousFocusedId{};
    std::string currentFocusedId{};

    [[nodiscard]] bool empty() const {
        return added.empty() && removed.empty() && changed.empty();
    }
};

[[nodiscard]] SemanticsDiff diffSemanticsTrees(const SemanticsTree& previous,
                                               const SemanticsTree& current,
                                               const std::string& previousFocusedId = {},
                                               const std::string& currentFocusedId = {});

// 语义 action 分发结果。
enum class SemanticsActionStatus : std::uint8_t {
    Handled,
    NotHandled,   // 节点未声明该 action
    NodeMissing,  // 树中找不到节点
};

// 语义 action 执行（plan §3.3：键盘激活与语义 actions 共用
// FocusManager/handler 路径）。
//
//   Focus     → FocusManager 聚焦该节点（字段可编辑时建立编辑焦点）
//   Activate  → 触发节点 onClick handler（Button/Dialog barrier 等）
//   SetValue  → 经 InteractionController 写入字段/Slider/Splitter；
//               只读字段拒绝，数值控件使用 0..100 百分比
//   Dismiss   → 等价 Activate（modal barrier/Dialog 关闭）
//   Scroll    → 转发给 sink（8D ScrollController 注册；未注册则未处理）
using SemanticsScrollSink =
    std::function<bool(const std::string& nodeId, float deltaX,
                       float deltaY)>;

struct SemanticsActionContext {
    const core::RenderNode* root{nullptr};
    const core::HandlerRegistry* handlers{nullptr};
    core::FocusManager* focus{nullptr};
    core::InteractionController* controller{nullptr};
    SemanticsScrollSink scrollSink{};
};

[[nodiscard]] SemanticsActionStatus performSemanticsAction(
    const SemanticsTree& tree, const SemanticsActionContext& context,
    const std::string& nodeId, std::uint32_t action,
    const std::string& value = {}, float scrollDeltaY = 0.0F);

}  // namespace lumen::accessibility
