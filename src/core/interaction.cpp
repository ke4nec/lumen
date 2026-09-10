#include "lumen/core/interaction.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "lumen/text/font_manager.h"
#include "lumen/text/text_layout.h"
#include "lumen/text/grapheme.h"

namespace lumen::core {
namespace {

// Pointer displacement (Manhattan distance) beyond which a press becomes a
// drag instead of a tap. 4 logical px matches common touch slop.
constexpr float kDragSlopPx = 4.0F;
// 双击判定窗口（毫秒）。
constexpr std::uint64_t kDoubleClickMs = 400;
// painter 的字段文本左内边距（与 drawText 起点一致）。
constexpr float kFieldTextPadding = 8.0F;

// 树内节点的根相对（绝对）原点；target 以指针比较。
bool absoluteOffsetOf(const RenderNode& tree, const RenderNode& target,
                      Offset acc, Offset& out) {
    acc = acc + tree.offset;
    if (&tree == &target) {
        out = acc;
        return true;
    }
    for (const auto& child : tree.children) {
        if (absoluteOffsetOf(child, target, acc, out)) {
            return true;
        }
    }
    return false;
}

}  // namespace

const RenderNode* hitTestChain(const RenderNode& node, Offset position,
                               std::vector<const RenderNode*>& chain) {
    if (!Rect{Offset{}, node.size}.contains(position)) {
        return nullptr;
    }
    // Reverse paint order: later children are on top, so they hit first.
    for (auto it = node.children.rbegin(); it != node.children.rend(); ++it) {
        if (const RenderNode* hit =
                hitTestChain(*it, position - it->offset, chain)) {
            chain.push_back(&node);
            return hit;
        }
    }
    chain.push_back(&node);
    return &node;
}

void FocusManager::setFocus(std::string key) {
    setFocus(std::move(key), {});
}

void FocusManager::setFocus(std::string key, std::string identity) {
    focusedKey_ = std::move(key);
    focusedIdentity_ = identity.empty() ? focusedKey_ : std::move(identity);
}

void FocusManager::clearFocus() {
    focusedKey_.clear();
    focusedIdentity_.clear();
}

const std::string& FocusManager::focusedKey() const { return focusedKey_; }

const std::string& FocusManager::focusedIdentity() const {
    return focusedIdentity_;
}

bool FocusManager::hasFocus(const std::string& key) const {
    return !focusedKey_.empty() && focusedKey_ == key;
}

InteractionController::InteractionController(StateStore& store,
                                             const HandlerRegistry& handlers,
                                             FocusManager& focus)
    : store_(store), handlers_(handlers), focus_(focus) {}

// --- 编辑值构建/提交 ---

text::TextEditingValue InteractionController::buildValue() const {
    text::TextEditingValue value{store_.get(focusedBind_)};
    value.restore(selection_, composingActive_, composing_);
    return value;
}

void InteractionController::commitValue(
    const text::TextEditingValue& value) {
    if (focusedBind_.empty()) {
        return;
    }
    store_.set(focusedBind_, value.text());
    selection_ = value.selection();
    composingActive_ = value.composingActive();
    composing_ = value.composing();
    if (!composingActive_) {
        composition_.clear();
    }
}

template <typename Fn>
void InteractionController::applyEdit(Fn&& transform) {
    if (focusedBind_.empty() || focusedReadOnly_) {
        return;
    }
    commitValue(transform(buildValue()));
}

// --- 指针 ---

void InteractionController::pointerDown(const RenderNode& root,
                                        Offset position,
                                        std::uint64_t timestampMs) {
    pressedKey_.clear();
    pressedIdentity_.clear();
    armedOnClick_.clear();
    armedKey_.clear();
    armedIdentity_.clear();
    // Gesture anchor: every press can become a drag, clickable or not.
    pressActive_ = true;
    dragging_ = false;
    selecting_ = false;
    dragAnchor_ = position;
    dragCurrent_ = position;
    std::vector<const RenderNode*> chain;
    const RenderNode* target = hitTestChain(root, position, chain);
    if (target == nullptr) {
        focus_.clearFocus();
        focusedBind_.clear();
        composition_.clear();
        selection_ = {};
        composingActive_ = false;
        return;
    }

    // Nearest button in the target chain gets the pressed state; its key
    // (required) stays stable across the rebuild between down and up.
    for (const RenderNode* node : chain) {
        if (node->type == WidgetType::Button) {
            pressedKey_ = node->key;
            pressedIdentity_ = node->identity;
            break;
        }
    }
    // Arm the nearest click target (first onClick walking target->root);
    // releasing over the same target fires it, anything else cancels.
    for (const RenderNode* node : chain) {
        if (!node->onClick.empty()) {
            armedOnClick_ = node->onClick;
            armedKey_ = node->key;
            armedIdentity_ = node->identity;
            break;
        }
    }

    // Nearest TextField grabs focus; clicking anywhere else releases it.
    const RenderNode* field = nullptr;
    for (const RenderNode* node : chain) {
        if (node->type == WidgetType::TextField) {
            field = node;
            break;
        }
    }
    if (field != nullptr && !field->bind.empty()) {
        const bool refocus = focus_.focusedIdentity().empty() ||
                             focus_.focusedIdentity() != field->identity;
        focus_.setFocus(field->key.empty() ? field->bind : field->key,
                        field->identity);
        focusedBind_ = field->bind;
        focusedReadOnly_ = field->readOnly;
        focusedMultiline_ = field->multiline;
        composition_.clear();
        composingActive_ = false;
        composing_ = {};
        // 双击选词：同一字段且时间窗内。
        const bool doubleClick = !refocus && lastClickWasField_ &&
                                 lastClickIdentity_ == field->identity &&
                                 timestampMs >= lastClickMs_ &&
                                 timestampMs - lastClickMs_ <= kDoubleClickMs;
        // 命中点的绝对原点 = chain 各节点 offset 之和。
        Offset fieldOrigin{};
        for (const RenderNode* node : chain) {
            fieldOrigin = fieldOrigin + node->offset;
        }
        if (doubleClick) {
            // 光标先定位再扩词。
            placeCaretByHit(*field, position - fieldOrigin, false);
            const auto value =
                buildValue().selectWord(selection_.extent);
            commitValue(value);
        } else {
            placeCaretByHit(*field, position - fieldOrigin, false);
        }
        lastClickMs_ = timestampMs;
        lastClickIdentity_ = field->identity;
        lastClickWasField_ = true;
        selecting_ = true;
    } else {
        focus_.clearFocus();
        focusedBind_.clear();
        composition_.clear();
        selection_ = {};
        composingActive_ = false;
        lastClickWasField_ = false;
    }
}

void InteractionController::placeCaretByHit(const RenderNode& field,
                                            Offset localPosition,
                                            bool extend) {
    const std::string& content = field.text.empty() && !field.placeholder.empty()
                                     ? field.placeholder
                                     : field.text;
    const float availableWidth =
        std::max(0.0F, field.size.width - 2.0F * kFieldTextPadding);
    core::TextStyle style = field.textStyle;
    style.maxLines = 0;  // 命中测试按自然行
    const text::TextLayoutResult layout = text::TextLayout::layout(
        content, style, field.multiline ? availableWidth : 0.0F,
        text::PlaceholderFontManager::shared());
    const float xInText = localPosition.x - kFieldTextPadding;
    const std::size_t grapheme =
        layout.positionToGrapheme(xInText, localPosition.y);
    if (extend) {
        selection_ = text::TextSelection{selection_.base, grapheme};
    } else {
        selection_ = text::TextSelection{grapheme, grapheme};
    }
}

void InteractionController::pointerMove(const RenderNode& root,
                                        Offset position) {
    if (!pressActive_) {
        return;
    }
    dragCurrent_ = position;
    const Offset delta = dragCurrent_ - dragAnchor_;
    if (!dragging_ && std::abs(delta.x) + std::abs(delta.y) > kDragSlopPx) {
        dragging_ = true;
    }
    // 拖动扩展选区：焦点字段内移动更新 selection.extent。
    if (selecting_ && !focusedBind_.empty()) {
        const RenderNode* field =
            findNodeByIdentity(root, focus_.focusedIdentity());
        if (field == nullptr && !focus_.focusedKey().empty()) {
            field = findNodeByKey(root, focus_.focusedKey());
        }
        if (field != nullptr) {
            Offset fieldOrigin{};
            if (absoluteOffsetOf(root, *field, Offset{}, fieldOrigin)) {
                placeCaretByHit(*field, position - fieldOrigin, true);
            }
        }
    }
}

void InteractionController::pointerUp(const RenderNode& root,
                                      Offset position) {
    const std::string armedOnClick = std::move(armedOnClick_);
    const std::string armedKey = std::move(armedKey_);
    const std::string armedIdentity = std::move(armedIdentity_);
    const bool wasDragging = dragging_;
    pressedKey_.clear();
    pressedIdentity_.clear();
    armedOnClick_.clear();
    armedKey_.clear();
    armedIdentity_.clear();
    pressActive_ = false;
    dragging_ = false;
    selecting_ = false;
    if (armedOnClick.empty() || wasDragging) {
        // No click target, or the press turned into a drag: a drag release
        // never fires a click (basic gesture discrimination).
        return;
    }
    std::vector<const RenderNode*> chain;
    const RenderNode* target = hitTestChain(root, position, chain);
    if (target == nullptr) {
        return;
    }
    // The first onClick node on the release chain decides: firing only when
    // it is the same node armed at pointer down (plan §5.3: the event goes
    // to the target first, then bubbles to the nearest ancestor handler).
    for (const RenderNode* node : chain) {
        if (node->onClick.empty()) {
            continue;
        }
        if (node->onClick == armedOnClick && node->key == armedKey &&
            node->identity == armedIdentity) {
            const auto handler = handlers_.find(armedOnClick);
            if (handler != handlers_.end()) {
                handler->second();
            }
        }
        return;
    }
}

void InteractionController::pointerCancel() {
    // 取消：解除按压/拖动/选区拖动；已建立的选区保留（plan §3.1 取消
    // 语义），点击绝不触发。
    pressedKey_.clear();
    pressedIdentity_.clear();
    armedOnClick_.clear();
    armedKey_.clear();
    armedIdentity_.clear();
    pressActive_ = false;
    dragging_ = false;
    selecting_ = false;
}

// --- 文本输入与 IME ---

void InteractionController::textInput(const std::string& text) {
    if (focusedBind_.empty() || text.empty() || focusedReadOnly_) {
        return;
    }
    if (composingActive_) {
        // IME 提交紧随 preedit：先落 preedit 再插入 commit 文本。
        commitComposition(text);
        return;
    }
    applyEdit([&text](const text::TextEditingValue& value) {
        return value.insertText(text);
    });
}

void InteractionController::setComposition(const std::string& preedit) {
    // Composition previews the IME string without touching the document;
    // committing still arrives as textInput(). Ignore preedit while unfocused
    // so composition_ never lingers without a focused field.
    if (focusedBind_.empty()) {
        composition_.clear();
        return;
    }
    if (focusedReadOnly_) {
        return;
    }
    const text::TextEditingValue next = buildValue().compose(preedit);
    composition_ = preedit;
    composingActive_ = next.composingActive();
    composing_ = next.composing();
    selection_ = next.selection();
}

void InteractionController::commitComposition(const std::string& text) {
    if (focusedBind_.empty()) {
        return;
    }
    if (focusedReadOnly_) {
        composition_.clear();
        composingActive_ = false;
        return;
    }
    const text::TextEditingValue next =
        buildValue().commitComposition(text);
    commitValue(next);
    composition_.clear();
}

void InteractionController::cancelComposition() {
    if (focusedBind_.empty()) {
        composition_.clear();
        return;
    }
    const text::TextEditingValue next = buildValue().cancelComposition();
    commitValue(next);
    composition_.clear();
}

// --- 键盘 ---

void InteractionController::keyDown(Key key) {
    keyDown(key, kModifierNone, 0);
}

void InteractionController::keyDown(Key key, KeyModifiers modifiers,
                                    char keyChar) {
    if (focusedBind_.empty()) {
        return;
    }
    const bool ctrlLike =
        (modifiers & (kModifierCtrl | kModifierGui)) != 0;
    const bool shift = (modifiers & kModifierShift) != 0;

    // Ctrl/Gui 快捷键（plan §3.2：Ctrl/Command 快捷键）。
    if (ctrlLike && keyChar != 0) {
        switch (keyChar) {
            case 'a':
            case 'A': {
                const auto next = buildValue().selectAll();
                selection_ = next.selection();
                return;
            }
            case 'c':
            case 'C':
                if (clipboard_ != nullptr && hasSelection()) {
                    (void)clipboard_->setText(
                        buildValue().selectedText());
                }
                return;
            case 'x':
            case 'X':
                if (clipboard_ != nullptr && hasSelection()) {
                    (void)clipboard_->setText(buildValue().selectedText());
                }
                applyEdit([](const text::TextEditingValue& value) {
                    return value.insertText("");
                });
                return;
            case 'v':
            case 'V':
                if (clipboard_ != nullptr && clipboard_->hasText()) {
                    const std::string pasted = clipboard_->text();
                    applyEdit([&pasted](
                                  const text::TextEditingValue& value) {
                        return value.insertText(pasted);
                    });
                }
                return;
            default:
                break;
        }
    }

    switch (key) {
        case Key::Backspace:
            applyEdit([](const text::TextEditingValue& value) {
                return value.deleteBackward();
            });
            return;
        case Key::Delete:
            applyEdit([](const text::TextEditingValue& value) {
                return value.deleteForward();
            });
            return;
        case Key::Left:
            if (ctrlLike) {
                applyEdit([shift](const text::TextEditingValue& value) {
                    return value.moveWordLeft(shift);
                });
            } else {
                applyEdit([shift](const text::TextEditingValue& value) {
                    return value.moveCaretLeft(shift);
                });
            }
            return;
        case Key::Right:
            if (ctrlLike) {
                applyEdit([shift](const text::TextEditingValue& value) {
                    return value.moveWordRight(shift);
                });
            } else {
                applyEdit([shift](const text::TextEditingValue& value) {
                    return value.moveCaretRight(shift);
                });
            }
            return;
        case Key::Home:
            applyEdit([shift](const text::TextEditingValue& value) {
                return value.moveCaretToStart(shift);
            });
            return;
        case Key::End:
            applyEdit([shift](const text::TextEditingValue& value) {
                return value.moveCaretToEnd(shift);
            });
            return;
        case Key::Enter:
            if (focusedMultiline_) {
                applyEdit([](const text::TextEditingValue& value) {
                    return value.insertText("\n");
                });
                return;
            }
            focus_.clearFocus();
            focusedBind_.clear();
            composition_.clear();
            selection_ = {};
            composingActive_ = false;
            return;
        case Key::Escape:
            if (composingActive_) {
                cancelComposition();
                return;
            }
            focus_.clearFocus();
            focusedBind_.clear();
            composition_.clear();
            selection_ = {};
            composingActive_ = false;
            return;
        case Key::None:
        case Key::Tab:
        case Key::Backtab:
        case Key::Up:
        case Key::Down:
        case Key::PageUp:
        case Key::PageDown:
            return;
    }
}

void InteractionController::keyDown(const RenderNode& root, Key key,
                                    KeyModifiers modifiers, char keyChar) {
    // Tab/Shift-Tab：焦点遍历（plan §3.2 键盘焦点遍历）。
    if (key == Key::Tab || key == Key::Backtab) {
        const bool backward =
            key == Key::Backtab || (modifiers & kModifierShift) != 0;
        if (traverseFocus(root, backward)) {
            return;
        }
    }
    // Enter/Space 激活聚焦 Button（与语义 activate 共用，阶段8C）。
    if (key == Key::Enter || keyChar == ' ') {
        if (focusedBind_.empty()) {
            activateFocusedButton(root);
            return;
        }
    }
    keyDown(key, modifiers, keyChar);
}

bool InteractionController::traverseFocus(const RenderNode& root,
                                          bool backward) {
    std::vector<const RenderNode*> focusables;
    std::function<void(const RenderNode&)> collect =
        [&](const RenderNode& node) {
            const bool editable =
                (node.type == WidgetType::TextField && !node.bind.empty());
            const bool activatable =
                node.type == WidgetType::Button && !node.onClick.empty();
            if (editable || activatable) {
                focusables.push_back(&node);
            }
            for (const auto& child : node.children) {
                collect(child);
            }
        };
    collect(root);
    if (focusables.empty()) {
        return false;
    }
    // 当前焦点位置（按 identity，其次 key）。
    std::ptrdiff_t index = -1;
    for (std::size_t i = 0; i < focusables.size(); ++i) {
        if (!focus_.focusedIdentity().empty() &&
            focusables[i]->identity == focus_.focusedIdentity()) {
            index = static_cast<std::ptrdiff_t>(i);
            break;
        }
        if (!focus_.focusedKey().empty() &&
            focusables[i]->key == focus_.focusedKey()) {
            index = static_cast<std::ptrdiff_t>(i);
        }
    }
    std::ptrdiff_t next;
    if (index < 0) {
        next = backward ? static_cast<std::ptrdiff_t>(focusables.size()) - 1
                        : 0;
    } else {
        const auto count = static_cast<std::ptrdiff_t>(focusables.size());
        next = backward ? (index - 1 + count) % count : (index + 1) % count;
    }
    const RenderNode* target = focusables[static_cast<std::size_t>(next)];
    if (target->type == WidgetType::TextField) {
        focus_.setFocus(target->key.empty() ? target->bind : target->key,
                        target->identity);
        focusedBind_ = target->bind;
        focusedReadOnly_ = target->readOnly;
        focusedMultiline_ = target->multiline;
        composition_.clear();
        composingActive_ = false;
        composing_ = {};
        selection_ = text::TextSelection{0, 0};
    } else {
        // Button 焦点：字段编辑状态释放。
        focus_.setFocus(target->key, target->identity);
        focusedBind_.clear();
        composition_.clear();
        selection_ = {};
        composingActive_ = false;
    }
    return true;
}

void InteractionController::activateFocusedButton(const RenderNode& root) {
    if (focus_.focusedKey().empty() || !focusedBind_.empty()) {
        return;
    }
    const RenderNode* button =
        findNodeByIdentity(root, focus_.focusedIdentity());
    if (button == nullptr) {
        button = findNodeByKey(root, focus_.focusedKey());
    }
    if (button == nullptr || button->type != WidgetType::Button ||
        button->onClick.empty()) {
        return;
    }
    const auto handler = handlers_.find(button->onClick);
    if (handler != handlers_.end()) {
        handler->second();
    }
}

// --- 滚轮 ---

void InteractionController::setWheelSink(WheelSink sink) {
    wheelSink_ = std::move(sink);
}

void InteractionController::wheel(const RenderNode& root, Offset position,
                                  Offset delta) {
    if (!wheelSink_) {
        return;
    }
    std::vector<const RenderNode*> chain;
    const RenderNode* hit = hitTestChain(root, position, chain);
    if (hit == nullptr) {
        return;
    }
    (void)wheelSink_(root, *hit, position, delta);
}

// --- 剪贴板/编辑值 ---

void InteractionController::setClipboard(ClipboardProvider* clipboard) {
    clipboard_ = clipboard;
}

text::TextEditingValue InteractionController::editingValue() const {
    return buildValue();
}

void InteractionController::setEditingValue(
    const text::TextEditingValue& value) {
    if (focusedBind_.empty()) {
        return;
    }
    commitValue(value);
}

void InteractionController::focusNode(const RenderNode& node) {
    const bool editable =
        node.type == WidgetType::TextField && !node.bind.empty();
    if (editable) {
        focus_.setFocus(node.key.empty() ? node.bind : node.key, node.identity);
        focusedBind_ = node.bind;
        focusedReadOnly_ = node.readOnly;
        focusedMultiline_ = node.multiline;
        composition_.clear();
        composingActive_ = false;
        composing_ = {};
        // 光标置于文本末尾。
        selection_ = text::TextSelection{text::graphemeCount(store_.get(node.bind)),
                                         text::graphemeCount(store_.get(node.bind))};
    } else {
        focus_.setFocus(node.key, node.identity);
        focusedBind_.clear();
        composition_.clear();
        selection_ = {};
        composingActive_ = false;
    }
}

}  // namespace lumen::core
