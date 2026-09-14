#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "lumen/core/clipboard.h"
#include "lumen/core/geometry.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/core/windowing.h"
#include "lumen/text/editing_history.h"
#include "lumen/text/editing_value.h"
#include "lumen/text/font_manager.h"

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

// M10：视口拖动滚流的阶段（Begin/Update = 拖动中，End = 释放可起惯性，
// Cancel = 取消只停惯性）。AppShell 的同形 sink 别名复用此类型。
enum class ScrollDragPhase { Begin, Update, End, Cancel };

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

    // --- 指针（root = 当前布局树；timestampMs 供双击检测与 M10 拖动
    // 速度采样） ---
    void pointerDown(const RenderNode& root, Offset position,
                     std::uint64_t timestampMs = 0);
    void pointerMove(const RenderNode& root, Offset position,
                     std::uint64_t timestampMs = 0);
    void pointerUp(const RenderNode& root, Offset position,
                   std::uint64_t timestampMs = 0);
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
    // hit 为空表示键盘触发（PageUp/PageDown/方向键），由 sink 决定目标
    // 视口（聚焦节点所在视口或默认视口）。
    using WheelSink = std::function<bool(const RenderNode& root,
                                         const RenderNode* hit,
                                         Offset position, Offset delta)>;
    void setWheelSink(WheelSink sink);
    // 返回 sink 的消费状态（M5 收口：语义滚动回执不再恒成功）。
    [[nodiscard]] bool wheel(const RenderNode& root, Offset position,
                             Offset delta);
    // 键盘滚动（无编辑焦点时的 PageUp/PageDown/Up/Down/Home/End）→
    // wheelSink（hit 为聚焦节点或空）；返回 sink 消费状态。
    [[nodiscard]] bool scrollKey(const RenderNode& root, Key key);

    // --- M10：触摸/指针拖动滚动（视口拖动 → 应用 sink） ---
    // 起点（slop 前）命中滚动视口且不在文本选区路径上的拖动路由到此；
    // 文本字段上的拖动仍走选区扩展。delta 为自上次 Update 的位移。
    // Cancel 时 root/viewport 为空（无释放语义，应用只停止惯性）。
    using ScrollDragSink = std::function<bool(
        const RenderNode* root, const RenderNode* viewport, Offset position,
        Offset delta, ScrollDragPhase phase, std::uint64_t timestampMs)>;
    void setScrollDragSink(ScrollDragSink sink);
    // 当前拖动是否被路由为视口滚动。
    [[nodiscard]] bool isScrollDragging() const { return scrollDragging_; }

    // --- 剪贴板（可选注入；宿主 Clipboard 适配 core::ClipboardProvider） ---
    void setClipboard(ClipboardProvider* clipboard);

    // --- M1 字体事实（可选注入） ---
    // 命中测试/光标定位与布局共享同一份 FontManager（Skia 后端时传入
    // SkiaFontManager；nullptr/未设置 = 占位，与 CPU 布局一致）。生命
    // 周期由调用方（应用）拥有，UI 线程独占。
    void setTextFonts(const text::FontManager* fonts);

    // --- 编辑值（焦点字段） ---
    [[nodiscard]] text::TextEditingValue editingValue() const;
    // 程序设置编辑值（测试/语义 setValue 用）；只读字段拒绝编辑。
    void setEditingValue(const text::TextEditingValue& value);

    // M1：撤销/重做（Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y 与语义 action 共用）。
    // preedit 更新不进栈；IME 提交为单个 Other 项。
    void undo();
    void redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;

    // 语义/键盘焦点请求（plan §3.3 与语义 actions 共用路径）：字段建立
    // 编辑焦点（光标置末尾），其他节点只设置 FocusManager 焦点。
    void focusNode(const RenderNode& node);
    // 语义 setValue：将 Slider 值限制到 0..100 后写回绑定状态。
    bool setSliderValue(const RenderNode& node, const std::string& value);

    // M5：焦点恢复（路由 pop/页面切换）：把焦点给 subtree 内第一个
    // 可聚焦节点（enabled 的字段/按钮/开关；遍历顺序与 Tab 一致）。
    // 无可聚焦节点时清除焦点。返回是否建立了焦点。
    bool focusFirstFocusable(const RenderNode& subtree);

    // Checkbox/Switch 状态切换（bind 值 "true"/"false"）；点击、Enter/
    // Space 与语义 activate 共用。
    void toggleChecked(const RenderNode& node);

    // --- 查询 ---
    // Button key currently held down ("" when none) — pressed visuals.
    [[nodiscard]] const std::string& pressedKey() const { return pressedKey_; }
    [[nodiscard]] const std::string& pressedIdentity() const {
        return pressedIdentity_;
    }
    // 视觉系统（visual-system §5）：hover 状态由指针命中的最深节点承载，
    // 应用汇总进 InteractionStateSnapshot 交给样式解析。
    [[nodiscard]] const std::string& hoveredKey() const {
        return hoveredKey_;
    }
    [[nodiscard]] const std::string& hoveredIdentity() const {
        return hoveredIdentity_;
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
    // 应用新编辑值：写 store、更新 selection/composing、进 undo 栈。
    void commitValue(const text::TextEditingValue& value,
                     text::EditKind kind = text::EditKind::Other);
    // 泛化编辑操作（readOnly 时拒绝；kind 决定 undo 合并/边界）。
    template <typename Fn>
    void applyEdit(Fn&& transform,
                   text::EditKind kind = text::EditKind::Other);
    // 当前字段的历史（按 bind；聚焦时以 store 值种子化）。
    text::EditingHistory& historyFor(const std::string& bind);
    // 点击定位光标：命中字段局部坐标 → grapheme 边界（TextLayout 命中
    // 测试）。extend=true 从选区锚点扩展。
    // M6：Slider 按根坐标位置设值（0..100 整数写 bind）。
    void setSliderByPosition(const RenderNode& root,
                             const RenderNode& node, Offset rootPosition);
    void placeCaretByHit(const RenderNode& field, Offset localPosition,
                         bool extend);
    // 焦点遍历（Tab/Shift-Tab）。返回是否移动了焦点。FocusScope 域内
    // 循环，不越过边界（plan §3.4）。
    bool traverseFocus(const RenderNode& root, bool backward);
    // 激活聚焦的可激活节点（Button/Checkbox/Switch；Enter/Space/语义
    // activate 共用）。
    void activateFocusedButton(const RenderNode& root);

    StateStore& store_;
    const HandlerRegistry& handlers_;
    FocusManager& focus_;
    ClipboardProvider* clipboard_{nullptr};
    WheelSink wheelSink_{};
    ScrollDragSink scrollDragSink_{};

    std::string pressedKey_{};
    std::string pressedIdentity_{};
    // hover：最近一次指针移动/按下的最深命中节点（disabled 除外）。
    std::string hoveredKey_{};
    std::string hoveredIdentity_{};
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
    text::TextSelection selectionBeforeComposition_{};
    std::string composition_{};
    // M1：按字段的 undo/redo 栈（seed 为聚焦时 store 值）。
    std::map<std::string, text::EditingHistory> histories_{};
    // M1：命中测试/光标定位的字体源（nullptr = 占位）。
    const text::FontManager* textFonts_{};

    // Gesture state: press anchor and current pointer while held.
    bool pressActive_{false};
    bool dragging_{false};
    bool selecting_{false};  // 指针拖动扩展选区中
    Offset dragAnchor_{};
    Offset dragCurrent_{};
    // M10：视口拖动滚动（identity 跨重建重定位视口）。
    bool scrollDragging_{false};
    std::string scrollDragIdentity_{};
    Offset scrollLastPoint_{};
    // 双击检测。
    std::uint64_t lastClickMs_{0};
    std::string lastClickIdentity_{};
    bool lastClickWasField_{false};
};

}  // namespace lumen::core
