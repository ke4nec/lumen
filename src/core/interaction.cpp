#include "lumen/core/interaction.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    // M11：Tooltip 为装饰性悬浮层（无交互/无语义 action），对命中测试
    // 透明——悬浮气泡不遮挡其下方的锚点与控件。
    if (node.type == WidgetType::Tooltip) {
        return nullptr;
    }
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
    const text::TextEditingValue& value, text::EditKind kind) {
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
    // M1：内容变更进 undo 栈（纯选区移动只打断合并，不进栈）。
    historyFor(focusedBind_).push(value, kind);
}

template <typename Fn>
void InteractionController::applyEdit(Fn&& transform, text::EditKind kind) {
    if (focusedBind_.empty() || focusedReadOnly_) {
        return;
    }
    commitValue(transform(buildValue()), kind);
}

text::EditingHistory& InteractionController::historyFor(
    const std::string& bind) {
    auto it = histories_.find(bind);
    if (it != histories_.end()) {
        return it->second;
    }
    text::EditingHistory history;
    history.seed(text::TextEditingValue{store_.get(bind)});
    auto inserted = histories_.emplace(bind, std::move(history));
    return inserted.first->second;
}

void InteractionController::undo() {
    if (focusedBind_.empty() || focusedReadOnly_ || composingActive_) {
        return;
    }
    auto it = histories_.find(focusedBind_);
    if (it == histories_.end()) {
        return;
    }
    if (const auto prev = it->second.undo()) {
        store_.set(focusedBind_, prev->text());
        selection_ = prev->selection();
        composingActive_ = false;
        composing_ = {};
        composition_.clear();
    }
}

void InteractionController::redo() {
    if (focusedBind_.empty() || focusedReadOnly_ || composingActive_) {
        return;
    }
    auto it = histories_.find(focusedBind_);
    if (it == histories_.end()) {
        return;
    }
    if (const auto next = it->second.redo()) {
        store_.set(focusedBind_, next->text());
        selection_ = next->selection();
        composingActive_ = false;
        composing_ = {};
        composition_.clear();
    }
}

bool InteractionController::canUndo() const {
    if (focusedBind_.empty()) {
        return false;
    }
    const auto it = histories_.find(focusedBind_);
    return it != histories_.end() && it->second.canUndo();
}

bool InteractionController::canRedo() const {
    if (focusedBind_.empty()) {
        return false;
    }
    const auto it = histories_.find(focusedBind_);
    return it != histories_.end() && it->second.canRedo();
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
    scrollDragging_ = false;
    scrollDragIdentity_.clear();
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
        // M1：聚焦时以 store 值种子化历史（首个 undo 回到聚焦时状态；
        // 历史按 bind 持久，返回字段不重种子）。
        (void)historyFor(focusedBind_);
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
            commitValue(value, text::EditKind::Selection);
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
        textFonts_ != nullptr ? *textFonts_
                              : text::PlaceholderFontManager::shared());
    const float xInText = localPosition.x - padX;
    const std::size_t grapheme =
        layout.positionToGrapheme(xInText, localPosition.y);
    if (extend) {
        selection_ = text::TextSelection{selection_.base, grapheme};
    } else {
        selection_ = text::TextSelection{grapheme, grapheme};
    }
    // M1：点击/拖动定位只打断输入合并，不进 undo 栈。
    if (!focusedBind_.empty()) {
        historyFor(focusedBind_)
            .push(buildValue(), text::EditKind::Selection);
    }
}

void InteractionController::setSliderByPosition(const RenderNode& root,
                                                const RenderNode& node,
                                                Offset rootPosition) {
    if (node.size.width <= 0.0F) {
        return;
    }
    Offset origin{};
    if (!absoluteOffsetOf(root, node, Offset{}, origin)) {
        return;
    }
    const float clamped = std::clamp(
        (rootPosition.x - origin.x) / node.size.width, 0.0F, 1.0F);
    const int value = static_cast<int>(std::lround(clamped * 100.0F));
    store_.set(node.bind, std::to_string(value));
}

bool InteractionController::setSliderValue(const RenderNode& node,
                                            const std::string& raw) {
    if (node.type != WidgetType::Slider || node.bind.empty() ||
        !node.enabled) {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(raw.c_str(), &end);
    if (end == raw.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    const int value = static_cast<int>(std::lround(std::clamp(parsed, 0.0F,
                                                               100.0F)));
    store_.set(node.bind, std::to_string(value));
    return true;
}

void InteractionController::pointerMove(const RenderNode& root,
                                        Offset position,
                                        std::uint64_t timestampMs) {
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
        // M10：越过 slop 的拖动若不在文本选区路径上，且起点命中滚动
        // 视口 → 路由为视口拖动滚动（视口按 identity 跨重建重定位）。
        // 例外：起点在可拖动的 Slider 上（M6 拖动释放按位置设值），
        // 拖动属于滑块而非滚动——视口内的 Slider 不被滚动劫持。
        if (!selecting_ && scrollDragSink_) {
            std::vector<const RenderNode*> chain;
            (void)hitTestChain(root, dragAnchor_, chain);
            bool overDraggableSlider = false;
            for (const RenderNode* node : chain) {
                if (node->type == WidgetType::Slider && node->enabled &&
                    !node->bind.empty()) {
                    overDraggableSlider = true;
                    break;
                }
            }
            if (!overDraggableSlider) {
                for (const RenderNode* node : chain) {
                    if (isScrollableWidget(node->type)) {
                        scrollDragging_ = true;
                        scrollDragIdentity_ = node->identity;
                        scrollLastPoint_ = dragAnchor_;
                        scrollDragSink_(&root, node, dragAnchor_, Offset{},
                                        ScrollDragPhase::Begin, timestampMs);
                        break;
                    }
                }
            }
        }
    }
    if (scrollDragging_) {
        const Offset move = position - scrollLastPoint_;
        scrollLastPoint_ = position;
        if (const RenderNode* viewport =
                findNodeByIdentity(root, scrollDragIdentity_)) {
            scrollDragSink_(&root, viewport, position, move,
                            ScrollDragPhase::Update, timestampMs);
        }
        return;
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
                                      Offset position,
                                      std::uint64_t timestampMs) {
    const std::string armedOnClick = std::move(armedOnClick_);
    const std::string armedKey = std::move(armedKey_);
    const std::string armedIdentity = std::move(armedIdentity_);
    const bool wasDragging = dragging_;
    const bool wasScrollDragging = scrollDragging_;
    const std::string scrollDragIdentity = std::move(scrollDragIdentity_);
    pressedKey_.clear();
    pressedIdentity_.clear();
    armedOnClick_.clear();
    armedKey_.clear();
    armedIdentity_.clear();
    pressActive_ = false;
    dragging_ = false;
    selecting_ = false;
    scrollDragging_ = false;
    scrollDragIdentity_.clear();
    if (wasScrollDragging) {
        // 拖动滚动释放：应用 sink 决定是否起惯性（End 携带释放时间戳）。
        if (const RenderNode* viewport =
                findNodeByIdentity(root, scrollDragIdentity)) {
            scrollDragSink_(&root, viewport, position, Offset{},
                            ScrollDragPhase::End, timestampMs);
        }
        return;
    }
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
                 node->type == WidgetType::Switch ||
                 node->type == WidgetType::Radio) &&
                !node->bind.empty() && node->enabled) {
                toggleChecked(*node);
                return;
            }
        }
    }
    // M6 Slider：点击/拖动命中轨道即按位置设值（bind 0..100 整数）。
    if (!chain.empty()) {
        for (const RenderNode* node : chain) {
            if (node->type == WidgetType::Slider && !node->bind.empty() &&
                node->enabled && node->size.width > 0.0F) {
                setSliderByPosition(root, *node, position);
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
    if (scrollDragging_) {
        scrollDragging_ = false;
        scrollDragIdentity_.clear();
        // 取消：应用 sink 停止惯性，不触发 End（无释放速度语义）。
        if (scrollDragSink_) {
            scrollDragSink_(nullptr, nullptr, Offset{}, Offset{},
                            ScrollDragPhase::Cancel, 0);
        }
    }
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
    // M1：单字输入合并，批量插入为独立事务。
    const text::EditKind kind =
        text::graphemeCount(text) == 1 ? text::EditKind::Insert
                                       : text::EditKind::Other;
    applyEdit(
        [&text](const text::TextEditingValue& value) {
            return value.insertText(text);
        },
        kind);
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

    // Ctrl/Gui 快捷键（plan §3.2：Ctrl/Command 快捷键；M1：undo/redo）。
    if (ctrlLike && keyChar != 0) {
        switch (keyChar) {
            case 'z':
            case 'Z':
                if (composingActive_) {
                    return;
                }
                if (shift) {
                    redo();
                } else {
                    undo();
                }
                return;
            case 'y':
            case 'Y':
                if (composingActive_) {
                    return;
                }
                redo();
                return;
            case 'a':
            case 'A': {
                const auto next = buildValue().selectAll();
                selection_ = next.selection();
                if (!focusedBind_.empty()) {
                    historyFor(focusedBind_)
                        .push(buildValue(), text::EditKind::Selection);
                }
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
            applyEdit(
                [](const text::TextEditingValue& value) {
                    return value.deleteBackward();
                },
                text::EditKind::Delete);
            return;
        case Key::Delete:
            applyEdit(
                [](const text::TextEditingValue& value) {
                    return value.deleteForward();
                },
                text::EditKind::Delete);
            return;
        case Key::Left:
            if (ctrlLike) {
                applyEdit(
                    [shift](const text::TextEditingValue& value) {
                        return value.moveWordLeft(shift);
                    },
                    text::EditKind::Selection);
            } else {
                applyEdit(
                    [shift](const text::TextEditingValue& value) {
                        return value.moveCaretLeft(shift);
                    },
                    text::EditKind::Selection);
            }
            return;
        case Key::Right:
            if (ctrlLike) {
                applyEdit(
                    [shift](const text::TextEditingValue& value) {
                        return value.moveWordRight(shift);
                    },
                    text::EditKind::Selection);
            } else {
                applyEdit(
                    [shift](const text::TextEditingValue& value) {
                        return value.moveCaretRight(shift);
                    },
                    text::EditKind::Selection);
            }
            return;
        case Key::Home:
            applyEdit(
                [shift](const text::TextEditingValue& value) {
                    return value.moveCaretToStart(shift);
                },
                text::EditKind::Selection);
            return;
        case Key::End:
            applyEdit(
                [shift](const text::TextEditingValue& value) {
                    return value.moveCaretToEnd(shift);
                },
                text::EditKind::Selection);
            return;
        case Key::Enter:
            if (focusedMultiline_) {
                applyEdit(
                    [](const text::TextEditingValue& value) {
                        return value.insertText("\n");
                    },
                    text::EditKind::Insert);
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
    // M6：聚焦 Slider 的 Left/Right 调值（±5，夹取 0..100）。
    if (focusedBind_.empty() &&
        (key == Key::Left || key == Key::Right) &&
        !focus_.focusedIdentity().empty()) {
        const RenderNode* slider =
            findNodeByIdentity(root, focus_.focusedIdentity());
        if (slider != nullptr && slider->type == WidgetType::Slider &&
            !slider->bind.empty() && slider->enabled) {
            const int current =
                std::atoi(store_.get(slider->bind).c_str());
            const int next = std::clamp(
                current + (key == Key::Right ? 5 : -5), 0, 100);
            store_.set(slider->bind, std::to_string(next));
            return;
        }
    }
    if (focusedBind_.empty() &&
        (key == Key::PageUp || key == Key::PageDown || key == Key::Up ||
         key == Key::Down || key == Key::Home || key == Key::End)) {
        (void)scrollKey(root, key);
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
                  !node.bind.empty()) ||
                 (node.type == WidgetType::Radio && !node.bind.empty()) ||
                 (node.type == WidgetType::Slider && !node.bind.empty()));
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
        node->type == WidgetType::Switch || node->type == WidgetType::Radio) {
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
         node.type != WidgetType::Switch &&
         node.type != WidgetType::Radio)) {
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

void InteractionController::setScrollDragSink(ScrollDragSink sink) {
    scrollDragSink_ = std::move(sink);
}

bool InteractionController::wheel(const RenderNode& root, Offset position,
                                  Offset delta) {
    if (!wheelSink_) {
        return false;
    }
    std::vector<const RenderNode*> chain;
    const RenderNode* hit = hitTestChain(root, position, chain);
    if (hit == nullptr) {
        return false;
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
        return false;
    }
    return wheelSink_(root, viewport, position, delta);
}

bool InteractionController::scrollKey(const RenderNode& root, Key key) {
    if (!wheelSink_) {
        return false;
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
            return false;
    }
    return wheelSink_(root, focused, Offset{}, Offset{0.0F, dy});
}

// --- 剪贴板/编辑值 ---

void InteractionController::setClipboard(ClipboardProvider* clipboard) {
    clipboard_ = clipboard;
}

void InteractionController::setTextFonts(const text::FontManager* fonts) {
    textFonts_ = fonts;
}

text::TextEditingValue InteractionController::editingValue() const {
    return buildValue();
}

void InteractionController::setEditingValue(
    const text::TextEditingValue& value) {
    if (focusedBind_.empty()) {
        return;
    }
    commitValue(value, text::EditKind::Other);
}

bool InteractionController::focusFirstFocusable(
    const RenderNode& subtree) {
    // 与 traverseFocus 相同的候选规则（enabled 的字段/可激活控件），
    // 深度优先顺序即 Tab 顺序；disabled 不建立焦点。
    std::function<const RenderNode*(const RenderNode&)> first =
        [&](const RenderNode& node) -> const RenderNode* {
        const bool editable =
            node.type == WidgetType::TextField && !node.bind.empty() &&
            node.enabled;
        const bool activatable =
            node.enabled &&
            ((node.type == WidgetType::Button && !node.onClick.empty()) ||
             ((node.type == WidgetType::Checkbox ||
               node.type == WidgetType::Switch) &&
              !node.bind.empty()));
        if (editable || activatable) {
            return &node;
        }
        for (const auto& child : node.children) {
            if (const RenderNode* found = first(child)) {
                return found;
            }
        }
        return nullptr;
    };
    const RenderNode* target = first(subtree);
    if (target == nullptr) {
        focus_.clearFocus();
        focusedBind_.clear();
        selection_ = {};
        composingActive_ = false;
        composing_ = {};
        composition_.clear();
        return false;
    }
    focusNode(*target);
    return true;
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
        (void)historyFor(focusedBind_);
    } else {
        focus_.setFocus(node.key, node.identity);
        focusedBind_.clear();
        composition_.clear();
        selection_ = {};
        composingActive_ = false;
    }
}

}  // namespace lumen::core
