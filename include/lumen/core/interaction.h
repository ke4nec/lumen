#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "lumen/core/clipboard.h"
#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/core/windowing.h"
#include "lumen/text/editing_value.h"

namespace lumen::core {

// Key 枚举与修饰键定义在 windowing.h（平台无关事件值类型）。

// Hit testing walks the render tree in reverse paint order: the last child is
// on top and wins (plan §5.3). On success `chain` receives the nodes from the
// hit target up to the root — the bubbling order. `position` is in root
// (window-logical) coordinates; child offsets are resolved recursively.
[[nodiscard]] const RenderNode* hitTestChain(const RenderNode& node,
                                             Offset position,
                                             std::vector<const RenderNode*>& chain);

// Tracks which widget holds keyboard focus. Keys are widget keys; the
// interaction controller keeps them stable across rebuilds.
class FocusManager {
  public:
    void setFocus(std::string key);
    void setFocus(std::string key, std::string identity);
    void clearFocus();
    [[nodiscard]] const std::string& focusedKey() const;
    [[nodiscard]] const std::string& focusedIdentity() const;
    [[nodiscard]] bool hasFocus(const std::string& key) const;

  private:
    std::string focusedKey_{};
    std::string focusedIdentity_{};
};

// v0.3 阶段8B (plan §3.2): TextField 编辑模型升级为 selection/composing。
// 命中定位用 TextLayout（布局与绘制同一份），光标/删除按 grapheme
// cluster，Shift 扩展选区，Ctrl/Gui 快捷键（A/C/X/V），双击选词，拖动
// 扩展选区，剪贴板读写，密码/只读/多行最小属性。
//
// 事件先归一化（HostEvent）再进入这里；平台层不得直接调用 Widget 回调
//（plan §2.3 不变量）。
class InteractionController {
  public:
    InteractionController(StateStore& store, const HandlerRegistry& handlers,
                          FocusManager& focus);

    // --- 指针（root = 当前布局树；timestampMs 供双击检测） ---
    void pointerDown(const RenderNode& root, Offset position,
                     std::uint64_t timestampMs = 0);
    void pointerMove(const RenderNode& root, Offset position);
    void pointerUp(const RenderNode& root, Offset position);
    // 取消活动指针（触摸取消/窗口失焦）：解除按压与拖动，不触发点击，
    // 选区保留。
    void pointerCancel();

    // --- 文本输入与 IME ---
    void textInput(const std::string& text);
    // preedit 更新（TextEditing 事件）；绝不写入文档。
    void setComposition(const std::string& preedit);
    // 显式提交 preedit（无 preedit 时等价 insertText）。
    void commitComposition(const std::string& text);
    // 取消 preedit：文档与选区恢复。
    void cancelComposition();

    // --- 键盘 ---
    // 兼容入口：无修饰键/树上下文的编辑键处理。
    void keyDown(Key key);
    // 修饰键感知的编辑处理（Ctrl/Gui + A/C/X/V、Shift 选区、Ctrl 词移）。
    void keyDown(Key key, KeyModifiers modifiers, char keyChar = 0);
    // 树上下文键处理：Tab/Shift-Tab 焦点遍历、Enter/Space 激活聚焦
    // Button（与语义 actions 共用路径，阶段8C）。
    void keyDown(const RenderNode& root, Key key,
                 KeyModifiers modifiers = kModifierNone, char keyChar = 0);

    // --- 滚轮转发（8D ScrollController 消费；返回 true 表示已处理） ---
    using WheelSink = std::function<bool(const RenderNode& root,
                                         const RenderNode& hit, Offset position,
                                         Offset delta)>;
    void setWheelSink(WheelSink sink);
    void wheel(const RenderNode& root, Offset position, Offset delta);

    // --- 剪贴板（可选注入；宿主 Clipboard 适配 core::ClipboardProvider） ---
    void setClipboard(ClipboardProvider* clipboard);

    // --- 编辑值（焦点字段） ---
    [[nodiscard]] text::TextEditingValue editingValue() const;
    // 程序设置编辑值（测试/语义 setValue 用）；只读字段拒绝编辑。
    void setEditingValue(const text::TextEditingValue& value);

    // --- 查询 ---
    // Button key currently held down ("" when none) — pressed visuals.
    [[nodiscard]] const std::string& pressedKey() const { return pressedKey_; }
    [[nodiscard]] const std::string& pressedIdentity() const {
        return pressedIdentity_;
    }
    // True while the pressed pointer moved beyond the drag slop; a drag
    // release never fires a click.
    [[nodiscard]] bool isDragging() const { return dragging_; }
    // Drag displacement from the press anchor to the latest move.
    [[nodiscard]] Offset dragDelta() const {
        return dragCurrent_ - dragAnchor_;
    }
    // 光标（grapheme cluster 索引）——焦点字段 selection.extent。
    [[nodiscard]] std::size_t caretGraphemes() const {
        return selection_.extent;
    }
    // 兼容别名（v0.2 按 code point 计数；语义已是 grapheme）。
    [[nodiscard]] std::size_t caretCodePoints() const {
        return caretGraphemes();
    }
    // 当前选区（grapheme 范围，相对焦点字段文本）。
    [[nodiscard]] std::size_t selectionStart() const {
        return selection_.start();
    }
    [[nodiscard]] std::size_t selectionEnd() const { return selection_.end(); }
    [[nodiscard]] bool hasSelection() const { return !selection_.collapsed(); }
    // IME composing 是否活跃（preedit 显示）。
    [[nodiscard]] bool composingActive() const { return composingActive_; }
    // preedit 字符串（绘制/诊断用；绝不进入 StateStore）。
    [[nodiscard]] const std::string& composition() const {
        return composition_;
    }
    // 焦点字段的 bind key（"" 表示无编辑焦点）。
    [[nodiscard]] const std::string& focusedBind() const {
        return focusedBind_;
    }
    [[nodiscard]] bool focusedReadOnly() const { return focusedReadOnly_; }
    // True while a bound TextField is focused; drives platform text input.
    [[nodiscard]] bool wantsTextInput() const { return !focusedBind_.empty(); }
    [[nodiscard]] const FocusManager& focus() const { return focus_; }

  private:
    // 焦点字段的编辑值（store 文本 + 本地 selection/composing）。
    [[nodiscard]] text::TextEditingValue buildValue() const;
    // 应用新编辑值：写 store、更新 selection/composing。
    void commitValue(const text::TextEditingValue& value);
    // 泛化编辑操作（readOnly 时拒绝）。
    template <typename Fn>
    void applyEdit(Fn&& transform);
    // 点击定位光标：命中字段局部坐标 → grapheme 边界（TextLayout 命中
    // 测试）。extend=true 从选区锚点扩展。
    void placeCaretByHit(const RenderNode& field, Offset localPosition,
                         bool extend);
    // 焦点遍历（Tab/Shift-Tab）。返回是否移动了焦点。
    bool traverseFocus(const RenderNode& root, bool backward);
    // 激活聚焦 Button（Enter/Space/语义 activate 共用）。
    void activateFocusedButton(const RenderNode& root);

    StateStore& store_;
    const HandlerRegistry& handlers_;
    FocusManager& focus_;
    ClipboardProvider* clipboard_{nullptr};
    WheelSink wheelSink_{};

    std::string pressedKey_{};
    std::string pressedIdentity_{};
    // Click target armed at pointer down: nearest onClick node identity.
    std::string armedOnClick_{};
    std::string armedKey_{};
    std::string armedIdentity_{};

    std::string focusedBind_{};
    bool focusedReadOnly_{false};
    bool focusedMultiline_{false};
    // grapheme 选区与 composing 状态（相对焦点字段文本）。
    text::TextSelection selection_{};
    bool composingActive_{false};
    text::TextSelection composing_{};
    std::string composition_{};

    // Gesture state: press anchor and current pointer while held.
    bool pressActive_{false};
    bool dragging_{false};
    bool selecting_{false};  // 指针拖动扩展选区中
    Offset dragAnchor_{};
    Offset dragCurrent_{};
    // 双击检测。
    std::uint64_t lastClickMs_{0};
    std::string lastClickIdentity_{};
    bool lastClickWasField_{false};
};

}  // namespace lumen::core
