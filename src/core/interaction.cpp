#include "lumen/core/interaction.h"

#include "lumen/core/scroll.h"
#include "lumen/core/splitter.h"
#include "lumen/core/text_field.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
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

// 滚动条拇指矩形（局部坐标；与 render/painter.cpp §7.2 同式）：
// 无可见拇指时返回 nullopt。
std::optional<Rect> scrollbarThumbRect(const RenderNode& node) {
    if (node.scrollbarThickness <= 0.0F || node.scrollExtent <= 0.0F ||
        node.scrollbarColor.a == 0 || node.scrollbarThumbWidth <= 0.0F) {
        return std::nullopt;
    }
    const float inset = node.scrollbarThickness - node.scrollbarThumbWidth;
    const float trackLength =
        std::max(0.0F, node.size.height - 2.0F * inset);
    if (trackLength <= 0.0F) {
        return std::nullopt;
    }
    const float fraction =
        node.size.height / (node.size.height + node.scrollExtent);
    const float thumbHeight = std::min(
        std::max(trackLength * fraction, node.scrollbarMinLength),
        trackLength);
    if (thumbHeight <= 0.0F) {
        return std::nullopt;
    }
    const float scrollable = trackLength - thumbHeight;
    const float progress = node.scrollExtent > 0.0F
                               ? node.scrollOffset / node.scrollExtent
                               : 0.0F;
    const float thumbY =
        inset + std::clamp(progress, 0.0F, 1.0F) * scrollable;
    const float thumbX =
        node.size.width - node.scrollbarThickness +
        (node.scrollbarThickness - node.scrollbarThumbWidth) * 0.5F;
    return Rect{Offset{thumbX, thumbY},
                Size{node.scrollbarThumbWidth, thumbHeight}};
}

// 拇指位移 → 内容偏移的换算比（scrollExtent/可滚轨道长）；无可见拇指
// 或轨道被拇指占满时返回 nullopt（调用方退化为普通内容拖拽）。
std::optional<float> scrollbarThumbRatio(const RenderNode& node) {
    const auto thumb = scrollbarThumbRect(node);
    if (!thumb.has_value()) {
        return std::nullopt;
    }
    const float inset = node.scrollbarThickness - node.scrollbarThumbWidth;
    const float trackLength =
        std::max(0.0F, node.size.height - 2.0F * inset);
    const float scrollable = trackLength - thumb->size.height;
    if (scrollable <= 0.0F) {
        return std::nullopt;
    }
    return node.scrollExtent / scrollable;
}

// 按下点是否落在拇指上：x 取整列宽、y 在拇指上下各放宽 2px（4px 宽的
// 拇指直接点中太苛刻）。
bool pressOnScrollbarThumb(const RenderNode& node, Offset local) {
    const auto thumb = scrollbarThumbRect(node);
    if (!thumb.has_value()) {
        return false;
    }
    return local.x >= node.size.width - node.scrollbarThickness &&
           local.x <= node.size.width &&
           local.y >= thumb->origin.y - 2.0F &&
           local.y <= thumb->origin.y + thumb->size.height + 2.0F;
}

// 命中链上 hover 的承载节点：最深的有效可交互控件（disabled 不承载，
// 普通容器不参与——避免整页 hover 抖动触发无谓重建，visual-system §5）。
// 集合行例外（collection-controls-design §6.2，与可聚焦谓词同源）：
// collectionRow + onClick 的 Row/Container 行承载 hover——菜单/列表/
// 下拉选项行的 hover 高亮（resolver 既有分支，此前追踪门未纳入）。
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
        if (node->collectionRow && !node->onClick.empty()) {
            return node;
        }
    }
    return nullptr;
}

// 悬停期望光标（splitter-design §7）：链上分隔条（splitterSource 节点）
// → ResizeEW/NS（分隔条窄边即主轴：宽 ≤ 高 = 水平分栏，左右调）；
// disabled 分隔条不声明。方向判定与 painter 同口径（节点几何）。
PointerCursor cursorFromChain(const std::vector<const RenderNode*>& chain) {
    for (const RenderNode* node : chain) {
        if (node->splitterSource != nullptr && node->enabled) {
            return node->size.width <= node->size.height
                       ? PointerCursor::ResizeEW
                       : PointerCursor::ResizeNS;
        }
    }
    return PointerCursor::Arrow;
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
    if (node.clipContent && !node.contentClipRect().contains(position)) {
        chain.push_back(&node);
        return &node;
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
                                        std::uint64_t timestampMs,
                                        KeyModifiers modifiers,
                                        PointerButton button) {
    std::vector<const RenderNode*> chain;
    const RenderNode* target = hitTestChain(root, position, chain);
    // hover 状态照常更新（Secondary 亦更新；menu-controls-design §6.2）。
    if (const RenderNode* hover = hoverTargetOf(chain)) {
        hoveredKey_ = hover->key;
        hoveredIdentity_ = hover->identity;
    } else {
        hoveredKey_.clear();
        hoveredIdentity_.clear();
    }
    hoverCursor_ = cursorFromChain(chain);
    // 菜单类控件（menu-controls-design §6.2）：Secondary（右键）不进入
    // 点击/按压/拖动/聚焦路径——右键只属于上下文菜单通道；Middle 同样
    // 无语义。进行中的主键手势状态不受影响（§6.2”不进入 press/armed/
    // drag/slide 路径”）。命中链不做 enabled 过滤（对禁用行弹”属性”类
    // 菜单合法）。
    if (button != PointerButton::Primary) {
        if (button == PointerButton::Secondary) {
            for (const auto& sink : secondaryPressSinks_) {
                if (sink(chain, position)) {
                    break;
                }
            }
        }
        return;
    }
    pointerModifiers_ = modifiers;
    pressedKey_.clear();
    pressedIdentity_.clear();
    armedOnClick_.clear();
    armedKey_.clear();
    armedIdentity_.clear();
    scrollDragging_ = false;
    scrollDragIdentity_.clear();
    scrollDragOnThumb_ = false;
    scrollDragSource_ = nullptr;
    sliderDragIdentity_.clear();
    splitterDragIdentity_.clear();
    splitterDragSource_ = nullptr;
    dragAnchor_ = position;
    dragCurrent_ = position;
    // Gesture anchor: every press can become a drag, clickable or not.
    pressActive_ = true;
    dragging_ = false;
    selecting_ = false;
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
        if (node->enabled && (node->type == WidgetType::Button ||
            (node->collectionRow && !node->onClick.empty()))) {
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
    // M6 Slider 拖拽锁定：起点落在可拖动滑块上时，后续移动每拍按位置
    // 设值（旋钮跟手），不再等释放（释放逻辑不变，仍落一次终值）。
    for (const RenderNode* node : chain) {
        if (node->type == WidgetType::Slider && node->enabled &&
            !node->bind.empty()) {
            sliderDragIdentity_ = node->identity;
            break;
        }
    }
    // Splitter 分隔条拖拽锁定（splitter-design §7.1）：命中即独占（优先
    // 于滚动/滑块——分隔条不属于任何滚动视口内容）；identity 跨重建，
    // 方向按节点宽高比判定（宽扁 = 水平分栏的纵向分隔条）。按压期间建
    // 立键盘焦点（拖住时方向键仍可微调）；释放/取消即清除（松手即失
    // 焦——高亮跟鼠标走，见 pointerUp/pointerCancel）。
    std::string splitterPressKey{};
    for (const RenderNode* node : chain) {
        if (node->splitterSource != nullptr && node->enabled) {
            splitterDragIdentity_ = node->identity;
            splitterDragSource_ = node->splitterSource;
            splitterDragStartOffset_ = node->splitterSource->offsetPx();
            splitterDragHorizontal_ = node->size.width <= node->size.height;
            splitterPressKey = node->key;
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
    // Splitter 分隔条：非字段命中的焦点清理后恢复分隔条键盘焦点。
    if (!splitterDragIdentity_.empty() && !splitterPressKey.empty()) {
        focus_.setFocus(splitterPressKey, splitterDragIdentity_);
    }
}

void InteractionController::placeCaretByHit(const RenderNode& field,
                                            Offset localPosition,
                                            bool extend) {
    const auto display = textFieldDisplay(field);
    const auto layout = text::TextLayout::layout(
        display.text, textFieldLayoutStyle(field), textFieldWrapWidth(field),
        textFonts_ != nullptr ? *textFonts_
                              : text::PlaceholderFontManager::shared());
    const auto* style = std::get_if<TextFieldResolvedStyle>(&field.style.component);
    const Offset origin = textFieldTextOrigin(
        field, layout, caretGraphemes(), style != nullptr && style->focused);
    const std::size_t grapheme = std::min(
        text::graphemeCount(field.text),
        layout.positionToGrapheme(localPosition.x - origin.x,
                                   localPosition.y - origin.y));
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
    // S3（§6.5）：指针位置到值使用与绘制相同的预留轨道区间
    //（trackInset..width-trackInset）；不足以容纳预留时整行居中映射。
    const auto* slider =
        std::get_if<SliderResolvedStyle>(&node.style.component);
    const float inset = slider != nullptr ? slider->trackInset : 0.0F;
    const float usable = node.size.width - 2.0F * inset;
    const float ratio = usable > 0.0F
                            ? (rootPosition.x - origin.x - inset) / usable
                            : 0.5F;
    const float clamped = std::clamp(ratio, 0.0F, 1.0F);
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

bool InteractionController::setSplitterValue(const RenderNode& node,
                                              const std::string& raw) {
    if (node.splitterSource == nullptr || !node.enabled ||
        node.splitterSource->extentPx() <= 0.0F) {
        return false;
    }
    char* end = nullptr;
    const float parsed = std::strtof(raw.c_str(), &end);
    if (end == raw.c_str() || !std::isfinite(parsed)) {
        return false;
    }
    if (*end == '%') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    const float percent = std::clamp(parsed, 0.0F, 100.0F);
    node.splitterSource->dragTo(node.splitterSource->extentPx() *
                                percent / 100.0F);
    requestRebuild();
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
        hoverCursor_ = cursorFromChain(chain);
    }
    if (!pressActive_) {
        return;
    }
    dragCurrent_ = position;
    const Offset delta = dragCurrent_ - dragAnchor_;
    // Splitter 分隔条拖拽（splitter-design §7.1）：直接跟手，无 slop——
    // 命中分隔条即意图明确；位移逐拍写入源（内部钳制），请求重建下一帧
    // 重排；越过 slop 标记 dragging（释放不触发点击/双击）。先于滚动/
    // 滑块路径（命中即独占）。
    if (!splitterDragIdentity_.empty()) {
        if (splitterDragSource_ != nullptr) {
            if (std::abs(delta.x) + std::abs(delta.y) > kDragSlopPx) {
                dragging_ = true;
            }
            splitterDragSource_->dragTo(
                splitterDragStartOffset_ +
                (splitterDragHorizontal_ ? delta.x : delta.y));
            requestRebuild();
        }
        return;
    }
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
                        // 源视口（VirtualList/List/Tree/TreeList）：框架
                        // 直接驱动源 ScrollController；其余视口交应用
                        // sink（ScrollView 等应用侧滚动状态）。
                        scrollDragSource_ =
                            node->virtualSource != nullptr
                                ? node->virtualSource->scrollController()
                                : nullptr;
                        // 起点落在拇指上 → 后续 Update 按拇指映射换算
                        //（跟手 1:1）；否则内容拖拽 1:1。
                        Offset viewportOrigin{};
                        if (absoluteOffsetOf(root, *node, Offset{},
                                             viewportOrigin)) {
                            scrollDragOnThumb_ = pressOnScrollbarThumb(
                                *node, dragAnchor_ - viewportOrigin);
                        } else {
                            scrollDragOnThumb_ = false;
                        }
                        if (scrollDragSource_ == nullptr) {
                            scrollDragSink_(&root, node, dragAnchor_, Offset{},
                                            ScrollDragPhase::Begin, timestampMs);
                        }
                        break;
                    }
                }
            }
        }
    }
    if (scrollDragging_) {
        Offset move = position - scrollLastPoint_;
        scrollLastPoint_ = position;
        if (const RenderNode* viewport =
                findNodeByIdentity(root, scrollDragIdentity_)) {
            if (scrollDragOnThumb_) {
                // 拇指跟手：手指位移即拇指位移，内容按比例反向换算
                //（手指下移 → offset 增大；与内容拖拽反号）。
                // 比例每拍按当前树重算（拖动中重建导致几何变化时仍成立）；
                // 拇指中途消失则退化为普通内容拖拽（不换算不断流）。
                if (const auto ratio = scrollbarThumbRatio(*viewport)) {
                    move.y = -move.y * *ratio;
                    move.x = 0.0F;
                }
            }
            if (scrollDragSource_ != nullptr) {
                // 框架路径：内容 1:1 跟手（拇指换算已在 move 上完成）。
                scrollDragSource_->noteDragSample(move.y, timestampMs);
                if (scrollDragSource_->applyDrag(move.y)) {
                    requestRebuild();
                }
            } else {
                scrollDragSink_(&root, viewport, position, move,
                                ScrollDragPhase::Update, timestampMs);
            }
        }
        return;
    }
    // M6 Slider 拖拽跟手：已锁定目标时每拍按位置设值（store 写回经订
    // 阅置脏，下帧重建即更新旋钮）；目标消失则解锁，不劫持选区路径。
    if (pressActive_ && !sliderDragIdentity_.empty()) {
        const RenderNode* slider =
            findNodeByIdentity(root, sliderDragIdentity_);
        if (slider != nullptr && slider->type == WidgetType::Slider &&
            slider->enabled && !slider->bind.empty()) {
            setSliderByPosition(root, *slider, position);
        } else {
            sliderDragIdentity_.clear();
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
                                      std::uint64_t timestampMs,
                                      PointerButton button) {
    // 非主键释放：无点击/拖动语义，也不影响进行中的主键手势（与
    // pointerDown 的 Secondary 隔离对称；menu-controls-design §6.2）。
    if (button != PointerButton::Primary) {
        return;
    }
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
    scrollDragOnThumb_ = false;
    sliderDragIdentity_.clear();
    // Splitter 分隔条释放：拖动状态解除（无惯性/无终值落点；干净单击
    // 继续走下方 fired 路径参与双击复位检测）。松手即失焦：本次按压建
    // 立的分隔条焦点随释放清除（高亮跟鼠标走；中途 Tab 离开则不动新
    // 焦点；Tab/语义聚焦不经过按压路径，不受影响）。
    const std::string pressSplitterIdentity =
        std::move(splitterDragIdentity_);
    splitterDragIdentity_.clear();
    splitterDragSource_ = nullptr;
    if (!pressSplitterIdentity.empty() &&
        focus_.focusedIdentity() == pressSplitterIdentity) {
        focus_.clearFocus();
    }
    if (wasScrollDragging) {
        // 拖动滚动释放：应用 sink 决定是否起惯性（End 携带释放时间戳）。
        if (scrollDragSource_ != nullptr) {
            // 框架路径：源控制器起滑后登记，由 AppShell::tick 逐拍推进。
            if (scrollDragSource_->endDrag(timestampMs)) {
                sourceFlinging_.push_back(scrollDragSource_);
                requestRebuild();
            }
            scrollDragSource_ = nullptr;
            return;
        }
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
    // 约定：handler 内可能同步触发整树重建（M11 DropdownController::open
    // 经 rebuildIfDirty 落地 overlay）——handler 返回后不得再解引用
    // chain/root 中的节点指针。
    for (const RenderNode* node : chain) {
        if (node->onClick.empty()) {
            continue;
        }
        if (node->onClick == armedOnClick && node->key == armedKey &&
            node->identity == armedIdentity && node->enabled) {
            // 集合行双击检测（collection-controls-design §6.2）：同一
            // identity 的第二次点击在 handler 之后触发激活 sink；key/
            // identity 先行拷贝（handler 可能重建整树）。
            const std::string firedKey = node->key;
            const std::string firedIdentity = node->identity;
            // Splitter 分隔条源先行拷贝（同上；双击复位用）。
            const SplitterSource* firedSplitter = node->splitterSource;
            const bool doubleClick =
                rowClickIdentity_ == firedIdentity &&
                timestampMs >= rowClickMs_ &&
                timestampMs - rowClickMs_ <= kDoubleClickMs;
            rowClickMs_ = timestampMs;
            rowClickIdentity_ = firedIdentity;
            // 行点击 sink 先于 HandlerRegistry：集合行按名字前缀解析
            //（虚拟化行不注册常驻 handler）；未消费再走应用注册表。
            bool clickConsumed = false;
            for (const auto& sink : rowClickSinks_) {
                if (sink(armedOnClick)) {
                    clickConsumed = true;
                    break;
                }
            }
            if (!clickConsumed) {
                const auto handler = handlers_.find(armedOnClick);
                if (handler != handlers_.end()) {
                    handler->second();
                }
            }
            if (doubleClick) {
                // Splitter 分隔条双击复位（splitter-design §7.3）：回
                // initialOffset/setResetOffset 目标（源指针已拷贝）。
                if (firedSplitter != nullptr) {
                    firedSplitter->reset();
                    requestRebuild();
                }
                for (const auto& sink : rowActivateSinks_) {
                    if (sink(firedKey, firedIdentity, /*keyboard=*/false)) {
                        break;
                    }
                }
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
    sliderDragIdentity_.clear();
    const std::string cancelledSplitterIdentity =
        std::move(splitterDragIdentity_);
    splitterDragIdentity_.clear();
    splitterDragSource_ = nullptr;
    // 松手即失焦的取消路径：本次按压建立的分隔条焦点一并清除。
    if (!cancelledSplitterIdentity.empty() &&
        focus_.focusedIdentity() == cancelledSplitterIdentity) {
        focus_.clearFocus();
    }
    hoverCursor_ = PointerCursor::Arrow;
    if (scrollDragging_) {
        scrollDragging_ = false;
        scrollDragIdentity_.clear();
        scrollDragOnThumb_ = false;
        // 取消：停止惯性，不触发 End（无释放速度语义）。框架路径直接
        // 停源控制器；应用路径经 sink。
        if (scrollDragSource_ != nullptr) {
            scrollDragSource_->stopFling();
            scrollDragSource_ = nullptr;
        } else if (scrollDragSink_) {
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
    // Splitter 分隔条键盘（splitter-design §7.2）：聚焦分隔条时方向键
    // 步进（水平 Left/Right、垂直 Up/Down——方向不匹配时消费不滚动）、
    // Home/End 到边；先于滚动键（分隔条不属于滚动内容）。
    if (focusedBind_.empty() && !focus_.focusedIdentity().empty() &&
        (key == Key::Left || key == Key::Right || key == Key::Up ||
         key == Key::Down || key == Key::Home || key == Key::End)) {
        const RenderNode* focused =
            findNodeByIdentity(root, focus_.focusedIdentity());
        if (focused != nullptr && focused->splitterSource != nullptr &&
            focused->enabled) {
            const bool horizontal =
                focused->size.width <= focused->size.height;
            const bool forward =
                key == Key::Right || key == Key::Down;
            if (key == Key::Home || key == Key::End) {
                focused->splitterSource->stepToEdge(key == Key::End);
            } else if ((horizontal &&
                        (key == Key::Left || key == Key::Right)) ||
                       (!horizontal &&
                        (key == Key::Up || key == Key::Down))) {
                const float step = horizontal ? focused->size.width
                                              : focused->size.height;
                focused->splitterSource->stepBy(forward ? step : -step);
            }
            requestRebuild();
            return;
        }
    }
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
        std::string collectionCurrent{};
    };
    std::vector<Candidate> focusables;
    std::function<void(const RenderNode&, const std::string&)> collect =
        [&](const RenderNode& node, const std::string& scope) {
            // List is one Tab stop. Arrow keys navigate its rows; Tab leaves it.
            if (node.type == WidgetType::List && node.virtualSource != nullptr) {
                if (!node.enabled) return;
                const std::string preferred = node.virtualSource->tabStopKey();
                const RenderNode* candidate = nullptr;
                for (const auto& row : node.children) {
                    if (!row.collectionRow || !row.enabled || row.onClick.empty()) continue;
                    const bool focused = row.identity == focus_.focusedIdentity() ||
                                         row.key == focus_.focusedKey();
                    const bool visible = row.offset.y < node.size.height - node.padding.bottom &&
                                         row.offset.y + row.size.height > node.padding.top;
                    if (!visible && !focused) continue;
                    if (candidate == nullptr || row.key == preferred || focused) candidate = &row;
                    if (focused) break;
                }
                if (candidate != nullptr) focusables.push_back(Candidate{candidate, scope, preferred});
                return;
            }
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
                 (node.type == WidgetType::Slider && !node.bind.empty()) ||
                 // 集合行（collection-controls-design §6.2）：Row/Container
                 // 行同样可聚焦（控制器构建，onClick 已注册）。
                 (node.collectionRow && !node.onClick.empty()));
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
            (focusables[i].node->key == focus_.focusedKey() ||
             focusables[i].collectionCurrent == focus_.focusedKey());
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
    // Sinks can request a rebuild; do not access the selected node afterwards.
    if (target->collectionRow) {
        const std::string rowKey = target->key;
        for (const auto& sink : rowFocusSinks_) {
            if (sink && sink(rowKey)) break;
        }
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
    // 集合行（Row/Container）与 Button 共用激活路径。
    const bool activatableRow = node->collectionRow && !node->onClick.empty();
    if ((node->type != WidgetType::Button && !activatableRow) ||
        node->onClick.empty()) {
        return;
    }
    // 集合行键盘激活（collection-controls-design §6.2）：Enter/Space 激活
    // 聚焦行——与行 onClick（选择）同一节点，先选择后激活；key/identity
    // 先行拷贝（handler 可能重建整树，返回后不得再解引用 node）。
    const std::string firedKey = node->key;
    const std::string firedIdentity = node->identity;
    const std::string firedOnClick = node->onClick;
    // 行点击 sink 先于 HandlerRegistry（集合行按名字前缀解析）。
    bool clickConsumed = false;
    for (const auto& sink : rowClickSinks_) {
        if (sink(firedOnClick)) {
            clickConsumed = true;
            break;
        }
    }
    if (!clickConsumed) {
        const auto handler = handlers_.find(firedOnClick);
        if (handler != handlers_.end()) {
            handler->second();
        }
    }
    // 集合行键盘激活：Enter/Space 激活聚焦行（与行 onClick 同一节点，
    // 先选择后激活）；消费即止（返回 true 的 sink 处理该行）。
    for (const auto& sink : rowActivateSinks_) {
        if (sink(firedKey, firedIdentity, /*keyboard=*/true)) {
            break;
        }
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

void InteractionController::addRowActivateSink(RowActivateSink sink) {
    rowActivateSinks_.push_back(std::move(sink));
}

bool InteractionController::activateCollectionRow(const RenderNode& node) {
    if (!node.enabled || !node.collectionRow || node.onClick.empty()) return false;
    const std::string key = node.key;
    const std::string identity = node.identity;
    for (const auto& sink : rowActivateSinks_) {
        if (sink && sink(key, identity, /*keyboard=*/true)) return true;
    }
    return false;
}

void InteractionController::addRowClickSink(RowClickSink sink) {
    rowClickSinks_.push_back(std::move(sink));
}

void InteractionController::addRowFocusSink(RowFocusSink sink) {
    rowFocusSinks_.push_back(std::move(sink));
}

void InteractionController::addSecondaryPressSink(SecondaryPressSink sink) {
    secondaryPressSinks_.push_back(std::move(sink));
}

void InteractionController::addPointerMoveSink(PointerMoveSink sink) {
    pointerMoveSinks_.push_back(std::move(sink));
}

void InteractionController::notifyPointerMove(const RenderNode& root,
                                              Offset position) {
    for (const auto& sink : pointerMoveSinks_) {
        sink(root, position);
    }
}

void InteractionController::setRebuildRequest(std::function<void()> request) {
    rebuildRequest_ = std::move(request);
}

void InteractionController::requestRebuild() {
    if (rebuildRequest_) {
        rebuildRequest_();
    }
}

bool InteractionController::advanceSourceFling(std::uint64_t nowMs) {
    if (sourceFlinging_.empty()) {
        return false;
    }
    bool active = false;
    for (ScrollController* scroller : sourceFlinging_) {
        active = scroller->stepFling(nowMs) || active;
    }
    std::erase_if(sourceFlinging_,
                  [](const ScrollController* scroller) {
                      return !scroller->isFlinging();
                  });
    if (active) {
        requestRebuild();
    }
    return active;
}

// 源视口滚动（wheel/drag/scrollKey 共用）：extents 由布局每帧
// updateViewport 同步（wheel 前必经 rebuildIfDirty），直接消费即可。
// |delta| > 1e8 为端点哨兵（跳到顶/底，测试与 Home/End 同路径）。
bool InteractionController::scrollSourceViewport(ScrollController& scroller,
                                                 float dy) {
    if (std::abs(dy) > 1e8F) {
        scroller.scrollTo(dy > 0.0F ? scroller.maxScrollOffset() : 0.0F);
        requestRebuild();
        return true;
    }
    if (scroller.applyWheel(dy)) {
        requestRebuild();
        return true;
    }
    return false;
}

// 定位 identity 节点并收集其祖先链（自节点到根；供最近滚动视口解析）。
const RenderNode* findIdentityChain(const RenderNode& node,
                                    const std::string& identity,
                                    std::vector<const RenderNode*>& chain) {
    chain.push_back(&node);
    if (node.identity == identity) {
        return &node;
    }
    for (const RenderNode& child : node.children) {
        if (const RenderNode* found =
                findIdentityChain(child, identity, chain)) {
            return found;
        }
    }
    chain.pop_back();
    return nullptr;
}

bool InteractionController::wheel(const RenderNode& root, Offset position,
                                  Offset delta) {
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
    // 源视口：滚动状态在源控制器内，框架直接驱动（应用无需按 key 接
    // 线）；已在边界时不冒泡到外层（与应用 sink 行为一致）。
    if (viewport->virtualSource != nullptr) {
        ScrollController* scroller =
            viewport->virtualSource->scrollController();
        if (scroller != nullptr) {
            return scrollSourceViewport(*scroller, delta.y);
        }
    }
    if (!wheelSink_) {
        return false;
    }
    return wheelSink_(root, viewport, position, delta);
}

bool InteractionController::scrollKey(const RenderNode& root, Key key) {
    // 键盘滚动的目标：聚焦节点；无焦点时交给 sink（默认视口）。
    const RenderNode* focused = nullptr;
    std::vector<const RenderNode*> focusChain;
    if (!focus_.focusedIdentity().empty()) {
        focused = findIdentityChain(root, focus_.focusedIdentity(),
                                    focusChain);
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
    // 聚焦节点的最近源视口祖先由框架直接滚动（集合控件行聚焦时的
    // Home/End/PageUp 语义与滚轮一致）。
    if (focused != nullptr) {
        for (const RenderNode* node : focusChain) {
            if (node->virtualSource == nullptr) {
                continue;
            }
            ScrollController* scroller = node->virtualSource->scrollController();
            if (scroller == nullptr) {
                continue;
            }
            if (scrollSourceViewport(*scroller, dy)) {
                return true;
            }
            return false;
        }
    }
    if (!wheelSink_) {
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
    if (node.collectionRow) {
        const std::string rowKey = node.key;
        for (const auto& sink : rowFocusSinks_) {
            if (sink && sink(rowKey)) break;
        }
    }
}

}  // namespace lumen::core
