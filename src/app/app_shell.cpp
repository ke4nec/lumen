// M2（自用路线图）：应用壳实现。
//
// 帧管线（rebuildIfDirty/renderFrame/damage/绘制缓存/caret 闪烁/IME 查询）
// 自 counter/settings 迁移而来——两个示例的行为与 frame hash 由此保持
// 一致（迁移验收：现有集成测试全部不变通过）。

#include "lumen/app/app_shell.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <utility>

#include "lumen/accessibility/semantics.h"
#include "lumen/core/damage.h"
#include "lumen/core/tween.h"

namespace lumen::app {
namespace {

// 深度优先按 identity 查找（origin 累计根相对偏移）。
const core::RenderNode* findByIdentity(const core::RenderNode& node,
                                       const std::string& identity,
                                       core::Offset& origin) {
    if (node.identity == identity) {
        return &node;
    }
    for (const auto& child : node.children) {
        core::Offset childOrigin = origin + child.offset;
        if (const core::RenderNode* found =
                findByIdentity(child, identity, childOrigin)) {
            origin = childOrigin;
            return found;
        }
    }
    return nullptr;
}

// M10：可变定位（转场 alpha/状态混合写回 root_）。
core::RenderNode* findMutableByIdentity(core::RenderNode& node,
                                        const std::string& identity) {
    if (node.identity == identity) {
        return &node;
    }
    for (auto& child : node.children) {
        if (core::RenderNode* found =
                findMutableByIdentity(child, identity)) {
            return found;
        }
    }
    return nullptr;
}

// M10：状态色过渡——捕获/应用（identity → 变化前样式）。
void collectStylesByIdentity(
    const core::RenderNode& node,
    std::map<std::string, core::ResolvedStyle>& out) {
    out[node.identity] = node.style;
    for (const auto& child : node.children) {
        collectStylesByIdentity(child, out);
    }
}

void applyBlendWalk(core::RenderNode& node, core::Offset absolute, float t,
                    const std::map<std::string, core::ResolvedStyle>& from,
                    std::map<std::string, core::ResolvedStyle>& to,
                    std::vector<core::Rect>& damage, bool& changed,
                    std::set<std::string>& matched) {
    const core::Offset origin = absolute + node.offset;
    if (const auto it = from.find(node.identity); it != from.end()) {
        matched.insert(node.identity);
        // 目标样式在首次应用时定格（后续帧 node.style 已被插值覆盖，
        // 不能作为 to 端）。
        const auto [target, inserted] = to.try_emplace(node.identity,
                                                       node.style);
        (void)inserted;
        core::ResolvedStyle blended =
            core::lerpStyleColors(it->second, target->second, t);
        if (!(blended == node.style)) {
            node.style = std::move(blended);
            damage.push_back(core::Rect{origin, node.size});
            changed = true;
        }
    }
    for (auto& child : node.children) {
        applyBlendWalk(child, origin, t, from, to, damage, changed, matched);
    }
}

}  // namespace

AppShell::AppShell(ShellConfig config) : config_(std::move(config)) {
    view_ = config_.initialView;
    // 首帧延迟落地（Element 树 + 订阅同步在首次 rebuildIfDirty 完成）：
    // 构造期求值 config_.build 会重入尚未构造完成的应用对象（如
    // SettingsApp 的 buildUi 读自身后声明成员），属 UB；dirty_ 初始为
    // true 保证首帧前必重建，行为与 eager 落地一致。
    if (config_.onWheel) {
        controller_.setWheelSink(
            [sink = config_.onWheel](const core::RenderNode& root,
                                     const core::RenderNode* hit,
                                     core::Offset position,
                                     core::Offset delta) {
                return sink(root, hit, position, delta);
            });
    }
}

// --- 视口/渲染器/字体 ---

void AppShell::setView(core::Size size) {
    if (size.width <= 0.0F || size.height <= 0.0F) {
        return;
    }
    view_ = size;
    dirty_ = true;
    // 约束变化使局部 damage 失效；全量重绘。
    fullRepaintPending_ = true;
}

void AppShell::setDeviceScale(float scale) {
    deviceScale_ = scale;
    cpuRenderer_.setDeviceScale(scale);
    // 纯 DPI 变化改变像素尺寸；缓存与保留帧在下次全量重绘前均失效。
    fullRepaintPending_ = true;
}

void AppShell::setRenderer(render::Renderer* renderer) {
    if (externalRenderer_ == renderer) {
        return;
    }
    externalRenderer_ = renderer;
    // 两个后端不共享 framebuffer 内容；目标切换即失效绘制缓存，回到
    // CPU 时不能呈现外部渲染器使用前的像素。
    framePainted_ = false;
    fullRepaintPending_ = true;
}

void AppShell::setAccessibilityBridge(
    accessibility::AccessibilityBridge* bridge) {
    accessibilityBridge_ = bridge;
    lastSemantics_.reset();
    lastSemanticFocus_.clear();
    semanticsNeedsPush_ = bridge != nullptr;
    // 延迟到下一次绘制后推送，确保首帧语义树已经完成布局。
}

accessibility::SemanticsActionStatus AppShell::performAccessibilityAction(
    const std::string& nodeId, std::uint32_t action, const std::string& value,
    float scrollDeltaY) {
    if (!lastSemantics_.has_value()) {
        pushSemantics();
    }
    if (!lastSemantics_.has_value()) {
        return accessibility::SemanticsActionStatus::NodeMissing;
    }
    accessibility::SemanticsActionContext context;
    context.root = &root_;
    context.handlers = &handlers_;
    context.focus = &focus_;
    context.controller = &controller_;
    // 滚动 sink：经控制器已注册的 wheelSink（虚拟列表/ScrollView 同
    // 源；hit 为空 = 键盘/语义触发的回退视口路径）。
    context.scrollSink = [this](const std::string& nodeId, float deltaX,
                                float deltaY) {
        if (!config_.onWheel) {
            return false;
        }
        core::Offset origin{};
        const core::RenderNode* viewport =
            findByIdentity(root_, nodeId, origin);
        if (viewport == nullptr ||
            !core::isScrollableWidget(viewport->type)) {
            return false;
        }
        const core::Offset center =
            origin + core::Offset{viewport->size.width * 0.5F,
                                  viewport->size.height * 0.5F};
        return config_.onWheel(root_, viewport, center,
                               core::Offset{deltaX, deltaY});
    };
    const auto status = accessibility::performSemanticsAction(
        *lastSemantics_, context, nodeId, action, value, scrollDeltaY);
    if (accessibilityBridge_ != nullptr) {
        accessibilityBridge_->noteActionPerformed(nodeId, action, status);
    }
    return status;
}

void AppShell::pushSemantics() {
    if (accessibilityBridge_ == nullptr) {
        return;
    }
    accessibility::SemanticsBuildOptions options;
    options.focus = &focus_;
    accessibility::SemanticsTree tree =
        accessibility::buildSemanticsTree(root_, options);
    const std::string focusedId = focus_.focusedIdentity();
    accessibility::SemanticsDiff diff;
    if (lastSemantics_.has_value()) {
        diff = accessibility::diffSemanticsTrees(*lastSemantics_, tree,
                                                 lastSemanticFocus_,
                                                 focusedId);
    } else {
        // 首帧：全部为新增。
        for (const auto& [id, node] : tree.nodes) {
            diff.added.push_back(id);
        }
    }
    accessibilityBridge_->updateTree(tree, diff, focusedId);
    if (focusedId != lastSemanticFocus_) {
        accessibilityBridge_->setFocusedNode(focusedId);
    }
    lastSemantics_ = std::move(tree);
    lastSemanticFocus_ = focusedId;
    semanticsNeedsPush_ = false;
}

void AppShell::setFontManager(std::shared_ptr<const text::FontManager> fonts) {
    textFonts_ = std::move(fonts);
    controller_.setTextFonts(textFonts_ ? textFonts_.get() : nullptr);
    dirty_ = true;
    fullRepaintPending_ = true;
}

void AppShell::setTheme(style::Theme theme, bool forceFullRepaint) {
    theme_ = std::move(theme);
    dirty_ = true;
    fullRepaintPending_ = fullRepaintPending_ || forceFullRepaint;
}

void AppShell::setAccessibilitySettings(
    accessibility::AccessibilitySettings settings,
    std::optional<bool> darkMode) {
    accessibility_ = settings;
    theme_ = style::Theme::fromSettings(
        accessibility_, darkMode.value_or(true), theme_.metrics.density);
    dirty_ = true;
    fullRepaintPending_ = true;
}

// --- 事件分发 ---

void AppShell::pointerDown(core::Offset position) {
    rebuildIfDirty();
    controller_.pointerDown(root_, position, lastTickMs_);
}

void AppShell::pointerMove(core::Offset position) {
    rebuildIfDirty();
    controller_.pointerMove(root_, position);
}

void AppShell::pointerUp(core::Offset position) {
    rebuildIfDirty();
    controller_.pointerUp(root_, position);
}

void AppShell::pointerCancel() { controller_.pointerCancel(); }

void AppShell::wheel(core::Offset position, core::Offset delta) {
    rebuildIfDirty();
    controller_.wheel(root_, position, delta);
}

void AppShell::textInput(const std::string& text) {
    controller_.textInput(text);
}

void AppShell::textEditing(const std::string& preedit) {
    controller_.setComposition(preedit);
}

void AppShell::cancelComposition() { controller_.cancelComposition(); }

void AppShell::keyDown(core::Key key, core::KeyModifiers modifiers,
                       char keyChar) {
    rebuildIfDirty();
    // 应用级键拦截（Escape/返回统一规则等）：消费后不进交互层。
    if (config_.onKey && config_.onKey(*this, key, modifiers, keyChar)) {
        return;
    }
    controller_.keyDown(root_, key, modifiers, keyChar);
}

// --- 帧管线 ---

void AppShell::rebuildIfDirty() {
    if (!dirty_) {
        return;
    }
    core::Widget next = swapTemplate_.has_value() ? *swapTemplate_
                                                  : (config_.build
                                                         ? config_.build()
                                                         : core::Widget{});
    core::applyBinds(next, state_);
    // 订阅与树保持同步：新增 bind key 补观察者，重建丢弃的 key 清理
    //（plan §9）。M7：布局输入为本地完整树（Element 快照去子化——
    // widget() 只保留本节点字段，整树由 build 产出本地持有）。
    std::set<std::string> bindKeys = core::collectBindKeys(next);
    core::RenderNode fresh = layout::LayoutEngine::layout(
        next, core::Constraints::tight(view_),
        styleContext(), textFontSource(),
        [this, &bindKeys](core::Widget& item) {
            core::applyBinds(item, state_);
            const auto itemKeys = core::collectBindKeys(item);
            bindKeys.insert(itemKeys.begin(), itemKeys.end());
        });
    // 延迟首建（见构造注释）：Element 在此首次落地（update 全程 move，
    // reconcile 零 Widget 拷贝）。
    if (!element_.has_value()) {
        element_.emplace(std::move(next));
    } else {
        element_->update(std::move(next));
    }
    syncSubscriptions(bindKeys);
    // 同一绘制前累计多次重建的 damage：屏幕仍显示上一次“已绘制”的树，
    // 此处丢弃更早的 rect 会留下残影。renderFrame 绘制后清空并重新武装
    // 有效性。treeDamageValid_ 只在“尚未绘制的重建”之间粘性为 false。
    if (hasPreviousRoot_) {
        treeDamageValid_ =
            treeDamageValid_ &&
            core::collectDamage(previousRoot_, fresh, pendingDamage_);
    } else {
        treeDamageValid_ = false;
        pendingDamage_.clear();
    }
    root_ = fresh;
    previousRoot_ = std::move(fresh);
    hasPreviousRoot_ = true;
    rebuiltThisFrame_ = true;
    dirty_ = false;
    // 应用侧重建后钩子（modal 焦点规则：弹窗打开时把焦点移入 dialog
    // 的 FocusScope，Tab/Enter 在域内处理，plan §3.4 焦点恢复）。
    if (config_.onRebuilt) {
        config_.onRebuilt(*this);
    }
}

std::uint64_t AppShell::renderFrame(bool forceFullRepaint) {
    // 交互快照变化 → 重建（resolved style 折算状态，diff 产生 damage，
    // visual-system §5 规则 6）。M10：变化前捕获旧样式供状态色过渡插值。
    syncInteractionSnapshot();
    if (!(interactionSnapshot_ == lastInteraction_)) {
        captureStateBlend();
        lastInteraction_ = interactionSnapshot_;
        dirty_ = true;
    }
    rebuildIfDirty();
    // onRebuilt 可把焦点移入新弹窗。提交前再解析一次交互样式，确保
    // 首帧焦点环与 PaintOptions/IME 查询看到同一个焦点。
    syncInteractionSnapshot();
    if (!(interactionSnapshot_ == lastInteraction_)) {
        lastInteraction_ = interactionSnapshot_;
        dirty_ = true;
        rebuildIfDirty();
    }
    // M10：转场 alpha 与状态混合写入重建后的树（damage 汇入 motionDamage；
    // 采样值由 tick 推进，renderFrame 不自带时钟）。
    std::vector<core::Rect> motionDamage;
    const bool motionPaint = applyTransitions(motionDamage);
    const bool blendPaint = applyStateBlend(motionDamage);
    render::PaintOptions options;
    options.caretGraphemes = controller_.caretGraphemes();
    options.caretAlpha = caretAlpha_;
    options.selectionStart = controller_.selectionStart();
    options.selectionEnd = controller_.selectionEnd();
    options.hasSelection = controller_.hasSelection();
    options.composition = controller_.composition();

    const std::string& focusedIdentity = focus_.focusedIdentity();
    const bool optionsChanged =
        focusedIdentity != lastFocusedIdentity_ ||
        options.caretGraphemes != lastCaret_ ||
        options.selectionStart != lastSelectionStart_ ||
        options.selectionEnd != lastSelectionEnd_ ||
        options.composition != lastComposition_ ||
        caretAlpha_ != lastCaretAlpha_;
    const bool needPaint = forceFullRepaint || fullRepaintPending_ ||
                           !framePainted_ || rebuiltThisFrame_ ||
                           optionsChanged || motionPaint || blendPaint;
    if (!needPaint) {
        if (semanticsNeedsPush_) {
            pushSemantics();
        }
        // 绘制缓存命中：无可见变化，跳过提交并返回上一哈希（仅内部
        // CPU 后端的哈希有意义；外部后端恒返回 0，避免泄漏切换前的
        // CPU 帧哈希）。
        rebuiltThisFrame_ = false;
        return externalRenderer_ == nullptr ? lastFrameHash_ : 0;
    }

    std::vector<core::Rect> damage = pendingDamage_;
    damage.insert(damage.end(), motionDamage.begin(), motionDamage.end());
    if (optionsChanged) {
        // 焦点/caret/选区/preedit 变化只重绘受影响节点；identity 定位
        // 当前树中的节点。
        if (focusedIdentity != lastFocusedIdentity_ ||
            options.caretGraphemes != lastCaret_ ||
            options.selectionStart != lastSelectionStart_ ||
            options.selectionEnd != lastSelectionEnd_ ||
            options.composition != lastComposition_ ||
            caretAlpha_ != lastCaretAlpha_) {
            addNodeRect(damage, focusedIdentity, focus_.focusedKey());
            addNodeRect(damage, lastFocusedIdentity_, "");
        }
    }

    render::Renderer& renderer = activeRenderer();
    const auto bounds = core::damageBounds(damage, view_);
    const bool partial = !forceFullRepaint && !fullRepaintPending_ &&
                         externalRenderer_ == nullptr && framePainted_ &&
                         treeDamageValid_ && bounds.has_value();
    // v0.2 命令路径（阶段7B）：CPU/Skia 光栅/Skia GPU 消费同一份录制
    // 命令；局部重绘经 damage+preserve 提交，全帧不带 damage。
    const auto buildStart = std::chrono::steady_clock::now();
    render::RenderCommandList commands =
        render::recordScene(root_, options, textFontSource());
    renderer.noteCpuBuildMs(
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - buildStart)
            .count());
    render::FrameInfo info;
    info.viewport = view_;
    info.deviceScale = deviceScale_;
    info.frameIndex = frameIndex_;
    info.timestampMs = lastTickMs_;
    if (partial) {
        info.damage = bounds;
        info.preservePrevious = true;
        ++partialRepaintCount_;
    }
    renderer.submit(commands, info);
    frameIndex_ += 1;
    element_->clearDirtyTree();

    // 下一帧的缓存/damage 决策记账。
    lastFocusedIdentity_ = focusedIdentity;
    lastCaret_ = options.caretGraphemes;
    lastSelectionStart_ = options.selectionStart;
    lastSelectionEnd_ = options.selectionEnd;
    lastComposition_ = options.composition;
    lastCaretAlpha_ = caretAlpha_;
    framePainted_ = true;
    fullRepaintPending_ = false;
    rebuiltThisFrame_ = false;
    pendingDamage_.clear();
    // 新绘制让屏幕与树重新一致：全量回退后 damage 跟踪也重新武装。
    treeDamageValid_ = true;
    // M5：绘制落地后推送语义（仅注册了桥时构建；diff 含焦点变化）。
    pushSemantics();    if (externalRenderer_ == nullptr) {
        lastFrameHash_ = render::frameHash(cpuRenderer_.pixels());
        return lastFrameHash_;
    }
    return 0;
}

void AppShell::tick(std::uint64_t nowMs) {
    lastTickMs_ = nowMs;
    bool animating = false;
    if (config_.caretBlink) {
        // caret 闪烁 tween（plan 阶段6）。应用拥有时钟；测试传固定时间戳保
        // 持确定性。无焦点帧保持 alpha 1.0（稳定 hash）。reduceAnimation 时
        // 闪烁半周期归零（MotionTokens，visual-system §4）。
        if (!controller_.wantsTextInput() ||
            theme_.motion.caretBlinkHalfPeriodMs == 0) {
            blinkAnchored_ = false;
            caretAlpha_ = 1.0F;
        } else {
            animating = true;
            if (!blinkAnchored_ || nowMs < blinkAnchorMs_) {
                // 锚定（或非单调时间戳重新锚定）到相位起点，避免无符号回绕。
                blinkAnchored_ = true;
                blinkAnchorMs_ = nowMs;
            }
            const double kHalfPeriodMs =
                static_cast<double>(theme_.motion.caretBlinkHalfPeriodMs);
            const double phase =
                std::fmod(static_cast<double>(nowMs - blinkAnchorMs_),
                          kHalfPeriodMs * 2.0);
            const core::Tween down{1.0, 0.0, kHalfPeriodMs,
                                   core::Easing::EaseInOut};
            const core::Tween up{0.0, 1.0, kHalfPeriodMs,
                                 core::Easing::EaseInOut};
            caretAlpha_ = static_cast<float>(
                phase < kHalfPeriodMs ? down.sample(phase)
                                      : up.sample(phase - kHalfPeriodMs));
        }
    } else {
        blinkAnchored_ = false;
        caretAlpha_ = 1.0F;
    }
    // M10：转场推进与应用侧动画（惯性滚动等）；存在活动动画时 runApp
    // 请求 FrameScheduler 动画帧。
    animating = advanceTransitions(nowMs) || animating;
    if (config_.onAnimate) {
        animating = config_.onAnimate(*this, nowMs) || animating;
    }
    animationsActive_ = animating;
}

// --- M10：转场驱动 ---

void AppShell::beginTransition(TransitionSpec spec) {
    if (spec.key.empty()) {
        return;
    }
    ActiveTransition transition;
    transition.key = spec.key;
    // identity 延迟解析：begin 可能早于含该子树的首次重建（如打开 dialog
    // 后立即 beginFadeIn）。
    if (const core::RenderNode* node = core::findNodeByKey(root_, spec.key)) {
        transition.identity = node->identity;
    }
    transition.tween = core::Tween{spec.from, spec.to, spec.durationMs,
                                   spec.easing};
    transition.sampledAlpha = spec.from;
    transition.startMs = lastTickMs_;
    transition.onComplete = std::move(spec.onComplete);
    transitions_.push_back(std::move(transition));
    transitionsPaintPending_ = true;
}

void AppShell::beginDialogTransition(
    const std::string& key, bool entering,
    std::function<void(AppShell&)> onComplete) {
    TransitionSpec spec;
    spec.key = key;
    spec.from = entering ? 0.0F : 1.0F;
    spec.to = entering ? 1.0F : 0.0F;
    spec.durationMs = static_cast<double>(theme_.motion.dialogTransitionMs);
    spec.easing = entering ? core::Easing::EaseOut : core::Easing::EaseIn;
    spec.onComplete = std::move(onComplete);
    beginTransition(std::move(spec));
}

void AppShell::beginRouteTransition(
    const std::string& key, bool entering,
    std::function<void(AppShell&)> onComplete) {
    TransitionSpec spec;
    spec.key = key;
    spec.from = entering ? 0.0F : 1.0F;
    spec.to = entering ? 1.0F : 0.0F;
    spec.durationMs = static_cast<double>(theme_.motion.navigatorTransitionMs);
    spec.easing = entering ? core::Easing::EaseOut : core::Easing::EaseIn;
    spec.onComplete = std::move(onComplete);
    beginTransition(std::move(spec));
}

bool AppShell::advanceTransitions(std::uint64_t nowMs) {
    // 上一拍完成的转场本拍退休：终值已经有过一次提交机会。
    std::erase_if(transitions_,
                  [](const ActiveTransition& t) { return t.retire; });
    if (transitions_.empty()) {
        return false;
    }
    std::vector<std::function<void(AppShell&)>> completed;
    for (auto& transition : transitions_) {
        const double elapsed =
            nowMs >= transition.startMs
                ? static_cast<double>(nowMs - transition.startMs)
                : 0.0;
        transition.sampledAlpha =
            static_cast<float>(transition.tween.sample(elapsed));
        if (!transition.paintedOnce ||
            transition.sampledAlpha != transition.lastPaintedAlpha) {
            transitionsPaintPending_ = true;
        }
        if (transition.tween.finished(elapsed)) {
            transition.finished = true;
            transition.retire = true;
            if (transition.onComplete) {
                completed.push_back(std::move(transition.onComplete));
            }
        }
    }
    // 完成回调最后触发（可开始新转场/markDirty；不重入采样循环）。
    for (auto& onComplete : completed) {
        onComplete(*this);
    }
    return !transitions_.empty();
}

bool AppShell::applyTransitions(std::vector<core::Rect>& damage) {
    if (transitions_.empty()) {
        transitionsPaintPending_ = false;
        return false;
    }
    for (auto it = transitions_.begin(); it != transitions_.end();) {
        if (it->identity.empty()) {
            const core::RenderNode* node =
                core::findNodeByKey(root_, it->key);
            if (node == nullptr) {
                // 子树尚未出现（begin 早于重建）；已完成的退出转场直接
                // 退休，避免悬挂。
                if (it->finished) {
                    it = transitions_.erase(it);
                    continue;
                }
                ++it;
                continue;
            }
            it->identity = node->identity;
        }
        core::RenderNode* node = findMutableByIdentity(root_, it->identity);
        if (node == nullptr) {
            // 子树已随重建消失（退出转场完成/应用提前移除）。
            it = transitions_.erase(it);
            continue;
        }
        if (node->transitionAlpha != it->sampledAlpha) {
            node->transitionAlpha = it->sampledAlpha;
            addNodeRect(damage, it->identity, "");
        }
        it->lastPaintedAlpha = it->sampledAlpha;
        it->paintedOnce = true;
        ++it;
    }
    const bool pending = transitionsPaintPending_;
    transitionsPaintPending_ = false;
    return pending;
}

// --- M10：状态色过渡 ---

void AppShell::captureStateBlend() {
    if (!config_.motionTransitions ||
        theme_.motion.stateTransitionMs == 0) {
        stateBlendActive_ = false;
        blendFrom_.clear();
        blendTo_.clear();
        return;
    }
    blendFrom_.clear();
    blendTo_.clear();
    collectStylesByIdentity(root_, blendFrom_);
    blendStartMs_ = lastTickMs_;
    stateBlendActive_ = !blendFrom_.empty();
}

bool AppShell::applyStateBlend(std::vector<core::Rect>& damage) {
    if (!stateBlendActive_ || blendFrom_.empty()) {
        stateBlendActive_ = false;
        return false;
    }
    const std::uint32_t duration = theme_.motion.stateTransitionMs;
    const double elapsed =
        lastTickMs_ >= blendStartMs_
            ? static_cast<double>(lastTickMs_ - blendStartMs_)
            : 0.0;
    const float t =
        duration > 0
            ? static_cast<float>(std::clamp(
                  elapsed / static_cast<double>(duration), 0.0, 1.0))
            : 1.0F;
    bool changed = false;
    std::set<std::string> matched;
    applyBlendWalk(root_, core::Offset{}, t, blendFrom_, blendTo_, damage,
                   changed, matched);
    // 已到终态或子树消失的 identity 不再等待。
    for (auto it = blendFrom_.begin(); it != blendFrom_.end();) {
        if (t >= 1.0F || matched.count(it->first) == 0) {
            it = blendFrom_.erase(it);
        } else {
            ++it;
        }
    }
    if (blendFrom_.empty()) {
        blendTo_.clear();
    }
    stateBlendActive_ = !blendFrom_.empty();
    return changed;
}

void AppShell::swapRoot(core::Widget root) {
    swapTemplate_ = std::move(root);
    dirty_ = true;
}

// --- 查询 ---

core::Rect AppShell::focusedTextRect() const {
    core::Offset origin{};
    const core::RenderNode* found = findFocusedField(origin);
    if (found == nullptr) {
        return core::Rect{};
    }
    const std::string display = controller_.composingActive()
                                    ? controller_.editingValue().text()
                                    : found->text;
    core::TextStyle style = found->textStyle();
    style.maxLines = 0;
    const auto layout =
        text::TextLayout::layout(display, style, 0.0F, textFontSource());
    std::size_t lineIndex = 0;
    const float x =
        layout.graphemeToX(controller_.caretGraphemes(), &lineIndex);
    return core::Rect{
        core::Offset{origin.x + found->commonStyle().padding.left + x,
                     origin.y},
        core::Size{1.0F, found->size.height}};
}

int AppShell::focusedCaretOffset() const {
    core::Offset origin{};
    const core::RenderNode* found = findFocusedField(origin);
    if (found == nullptr) {
        return 0;
    }
    const std::string display = controller_.composingActive()
                                    ? controller_.editingValue().text()
                                    : found->text;
    core::TextStyle style = found->textStyle();
    style.maxLines = 0;
    const auto layout =
        text::TextLayout::layout(display, style, 0.0F, textFontSource());
    return static_cast<int>(found->commonStyle().padding.left +
                            layout.graphemeToX(controller_.caretGraphemes(),
                                               nullptr));
}

// --- 内部 ---

void AppShell::syncInteractionSnapshot() {
    interactionSnapshot_ = style::InteractionStateSnapshot{
        controller_.hoveredIdentity(), controller_.pressedIdentity(),
        focus_.focusedIdentity()};
}

const text::FontManager& AppShell::textFontSource() const {
    return textFonts_ ? *textFonts_
                      : text::PlaceholderFontManager::shared();
}

const core::RenderNode* AppShell::findFocusedField(
    core::Offset& origin) const {
    const std::string& identity = focus_.focusedIdentity();
    const std::string& key = focus_.focusedKey();
    origin = core::Offset{};
    const core::RenderNode* found = nullptr;
    if (!identity.empty()) {
        found = findByIdentity(root_, identity, origin);
    }
    if (found == nullptr && !key.empty()) {
        if (const core::RenderNode* byKey = core::findNodeByKey(root_, key)) {
            found = byKey;
            origin = core::absoluteOffset(root_, key);
        }
    }
    return found;
}

void AppShell::addNodeRect(std::vector<core::Rect>& damage,
                           const std::string& identity,
                           const std::string& key) {
    const core::RenderNode* node = nullptr;
    core::Offset origin{};
    if (!identity.empty()) {
        node = findByIdentity(root_, identity, origin);
    }
    if (node == nullptr && !key.empty()) {
        node = core::findNodeByKey(root_, key);
        if (node != nullptr) {
            origin = core::absoluteOffset(root_, key);
        }
    }
    if (node != nullptr) {
        damage.push_back(core::Rect{origin, node->size});
    }
}

void AppShell::syncSubscriptions(const std::set<std::string>& keys) {
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        if (keys.count(it->first) == 0) {
            state_.unsubscribe(it->second);
            it = subscriptions_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& key : keys) {
        if (subscriptions_.count(key) == 0) {
            subscriptions_.emplace(
                key, state_.subscribe(key, [this] { dirty_ = true; }));
        }
    }
}

}  // namespace lumen::app
