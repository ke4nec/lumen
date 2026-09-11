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

// 命中链上 hover 的承载节点：最深的有效可交互控件（disabled 不承载，
// 容器不参与——避免整页 hover 抖动触发无谓重建，visual-system §5）。
const RenderNode* hoverTargetOf(const std::vector<const RenderNode*>& chain) {
    for (const RenderNode* node : chain) {
        if (!node->enabled) {
            continue;
        }
        switch (node->type) {
            case WidgetType::Button:
            case WidgetType::TextField:
            case WidgetType::Checkbox:
            case WidgetType::Switch:
                return node;
            default:
                break;
        }
    }
    return nullptr;
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
    if (const RenderNode* hover = hoverTargetOf(chain)) {
        hoveredKey_ = hover->key;
        hoveredIdentity_ = hover->identity;
    } else {
        hoveredKey_.clear();
        hoveredIdentity_.clear();
    }
    if (target == nullptr) {
        focus_.clearFocus();
        focusedBind_.clear();
        composition_.clear();
        selection_ = {};
        composingActive_ = false;
        return;
    }

    // Nearest enabled button in the target chain gets the pressed state;
    // disabled 控件不响应指针（visual-system §7.3）。
    for (const RenderNode* node : chain) {
        if (node->type == WidgetType::Button && node->enabled) {
            pressedKey_ = node->key;
            pressedIdentity_ = node->identity;
            break;
        }
    }
    // Arm the nearest enabled click target (first onClick walking
    // target->root); releasing over the same target fires it, anything else
    // cancels.
    for (const RenderNode* node : chain) {
        if (!node->onClick.empty() && node->enabled) {
            armedOnClick_ = node->onClick;
            armedKey_ = node->key;
            armedIdentity_ = node->identity;
            break;
        }
    }

    // Nearest enabled TextField grabs focus; clicking anywhere else (含
    // disabled 字段) releases it.
    const RenderNode* field = nullptr;
    for (const RenderNode* node : chain) {
        if (node->type == WidgetType::TextField && node->enabled) {
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
    const CommonResolvedStyle& common = field.commonStyle();
    const float padX = common.padding.left;
    const float availableWidth =
        std::max(0.0F, field.size.width - common.padding.horizontal());
    core::TextStyle style = common.text;
    style.maxLines = 0;  // 命中测试按自然行
    const text::TextLayoutResult layout = text::TextLayout::layout(
        content, style, field.multiline ? availableWidth : 0.0F,
        text::PlaceholderFontManager::shared());
    const float xInText = localPosition.x - padX;
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
    // hover 跟踪与按压状态独立：未按下时也更新命中（visual-system §5）。
    {
        std::vector<const RenderNode*> chain;
        (void)hitTestChain(root, position, chain);
        if (const RenderNode* hover = hoverTargetOf(chain)) {
            hoveredKey_ = hover->key;
            hoveredIdentity_ = hover->identity;
        } else {
            hoveredKey_.clear();
            hoveredIdentity_.clear();
        }
    }
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
    std::vector<const RenderNode*> chain;
    const RenderNode* target = hitTestChain(root, position, chain);
    if (target == nullptr) {
        return;
    }
    // Checkbox/Switch：命中（含祖先）即由框架切换状态（plan §3.4，与
    // TextField 编辑一致的内建行为）；拖动释放与 disabled 不切换。
    if (!wasDragging) {
        for (const RenderNode* node : chain) {
            if ((node->type == WidgetType::Checkbox ||
                 node->type == WidgetType::Switch) &&
                !node->bind.empty() && node->enabled) {
                toggleChecked(*node);
                return;
            }
        }
    }
    if (armedOnClick.empty() || wasDragging) {
        // No click target, or the press turned into a drag: a drag release
        // never fires a click (basic gesture discrimination).
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
            node->identity == armedIdentity && node->enabled) {
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
    if (preedit.empty()) {
        cancelComposition();
        return;
    }
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
    if (!composingActive_) {
        selectionBeforeComposition_ = selection_;
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
    if (composingActive_ && !focusedBind_.empty()) {
        // buildValue() reconstructs the editing value for each event, so its
        // internal saved selection does not survive a preedit update.
        commitValue(text::TextEditingValue{store_.get(focusedBind_),
                                           selectionBeforeComposition_});
    }
    composingActive_ = false;
    composing_ = {};
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
    // Enter/Space 激活聚焦的可激活节点（Button/Checkbox/Switch；与语义
    // activate 共用，阶段8C）。
    if (key == Key::Enter || keyChar == ' ') {
        if (focusedBind_.empty()) {
            activateFocusedButton(root);
            return;
        }
    }
    // 无编辑焦点时的滚动键：PageUp/PageDown/Up/Down/Home/End → wheelSink
    //（plan §3.4 键盘滚动）。
    if (focusedBind_.empty() &&
        (key == Key::PageUp || key == Key::PageDown || key == Key::Up ||
         key == Key::Down || key == Key::Home || key == Key::End)) {
        scrollKey(root, key);
        return;
    }
    keyDown(key, modifiers, keyChar);
}

bool InteractionController::traverseFocus(const RenderNode& root,
                                          bool backward) {
    // 收集焦点候选与其 FocusScope 域（最近的 FocusScope 祖先；根域 = ""）。
    struct Candidate {
        const RenderNode* node{};
        std::string scope{};
    };
    std::vector<Candidate> focusables;
    std::function<void(const RenderNode&, const std::string&)> collect =
        [&](const RenderNode& node, const std::string& scope) {
            const bool editable =
                (node.type == WidgetType::TextField && !node.bind.empty() &&
                 node.enabled);
            const bool activatable =
                node.enabled &&
                ((node.type == WidgetType::Button &&
                  !node.onClick.empty()) ||
                 ((node.type == WidgetType::Checkbox ||
                   node.type == WidgetType::Switch) &&
                  !node.bind.empty()));
            if (editable || activatable) {
                focusables.push_back(Candidate{&node, scope});
            }
            const std::string childScope =
                node.type == WidgetType::FocusScope ? node.identity : scope;
            for (const auto& child : node.children) {
                collect(child, childScope);
            }
        };
    collect(root, "");
    if (focusables.empty()) {
        return false;
    }
    // 当前焦点位置与其域。
    std::ptrdiff_t index = -1;
    std::string currentScope = "";
    for (std::size_t i = 0; i < focusables.size(); ++i) {
        const bool identityMatch =
            !focus_.focusedIdentity().empty() &&
            focusables[i].node->identity == focus_.focusedIdentity();
        const bool keyMatch = !focus_.focusedKey().empty() &&
                              focusables[i].node->key == focus_.focusedKey();
        if (identityMatch || keyMatch) {
            index = static_cast<std::ptrdiff_t>(i);
            currentScope = focusables[i].scope;
            if (identityMatch) {
                break;
            }
        }
    }
    // 域内循环（plan §3.4 FocusScope：Tab 不越过边界）。
    std::vector<std::size_t> inScope;
    for (std::size_t i = 0; i < focusables.size(); ++i) {
        if (focusables[i].scope == currentScope) {
            inScope.push_back(i);
        }
    }
    if (inScope.empty()) {
        return false;
    }
    std::size_t positionInScope = 0;
    if (index >= 0) {
        bool found = false;
        for (std::size_t i = 0; i < inScope.size(); ++i) {
            if (static_cast<std::ptrdiff_t>(inScope[i]) == index) {
                positionInScope = i;
                found = true;
                break;
            }
        }
        if (!found) {
            // 焦点在其他域：Tab 不跨界（modal barrier 下的默认）。
            return false;
        }
    }
    const auto count = inScope.size();
    std::size_t next;
    if (index < 0) {
        // 无焦点：正向取域内第一个，反向取最后一个。
        next = backward ? count - 1 : 0;
    } else {
        next = backward ? (positionInScope + count - 1) % count
                        : (positionInScope + 1) % count;
    }
    const RenderNode* target = focusables[inScope[next]].node;
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
        // Button/Checkbox/Switch 焦点：字段编辑状态释放。
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
    const RenderNode* node = findNodeByIdentity(root, focus_.focusedIdentity());
    if (node == nullptr) {
        node = findNodeByKey(root, focus_.focusedKey());
    }
    if (node == nullptr || !node->enabled) {
        return;
    }
    if (node->type == WidgetType::Checkbox ||
        node->type == WidgetType::Switch) {
        toggleChecked(*node);
        return;
    }
    if (node->type != WidgetType::Button || node->onClick.empty()) {
        return;
    }
    const auto handler = handlers_.find(node->onClick);
    if (handler != handlers_.end()) {
        handler->second();
    }
}

void InteractionController::toggleChecked(const RenderNode& node) {
    if (node.bind.empty() || !node.enabled ||
        (node.type != WidgetType::Checkbox &&
         node.type != WidgetType::Switch)) {
        return;
    }
    // bind 值往返 "true"/"false"（applyBinds 解析时同样宽容 "1"/"0"）。
    const std::string& value = store_.get(node.bind);
    const bool checked = value == "true" || value == "1" || value == "on";
    store_.set(node.bind, checked ? "false" : "true");
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
    // 命中链上最近的滚动视口承担滚动（plan §3.4 统一手势/焦点状态机）。
    const RenderNode* viewport = nullptr;
    for (const RenderNode* node : chain) {
        if (isScrollableWidget(node->type)) {
            viewport = node;
            break;
        }
    }
    if (viewport == nullptr) {
        return;
    }
    (void)wheelSink_(root, viewport, position, delta);
}

void InteractionController::scrollKey(const RenderNode& root, Key key) {
    if (!wheelSink_) {
        return;
    }
    // 键盘滚动的目标：聚焦节点；无焦点时交给 sink（默认视口）。
    const RenderNode* focused = nullptr;
    if (!focus_.focusedIdentity().empty()) {
        focused = findNodeByIdentity(root, focus_.focusedIdentity());
    }
    float dy = 0.0F;
    switch (key) {
        case Key::PageDown:
        case Key::Down:
            dy = 120.0F;
            break;
        case Key::PageUp:
        case Key::Up:
            dy = -120.0F;
            break;
        case Key::Home:
            dy = -1e9F;
            break;
        case Key::End:
            dy = 1e9F;
            break;
        default:
            return;
    }
    (void)wheelSink_(root, focused, Offset{}, Offset{0.0F, dy});
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
    // disabled 节点不建立焦点（键盘/语义 activate 与 focus 共用路径）。
    if (!node.enabled) {
        return;
    }
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
