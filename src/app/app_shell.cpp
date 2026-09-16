// M2（自用路线图）：应用壳实现。
//
// 帧管线（rebuildIfDirty/renderFrame/damage/绘制缓存/caret 闪烁/IME 查询）
// 自 counter/settings 迁移而来——两个示例的行为与 frame hash 由此保持
// 一致（迁移验收：现有集成测试全部不变通过）。

#include "lumen/app/app_shell.h"

#include "lumen/core/text_field.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <utility>

#include "lumen/accessibility/semantics.h"
#include "lumen/core/damage.h"
#include "lumen/core/tween.h"
#include "lumen/text/system_font_manager.h"

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

bool visibleAnchor(const core::RenderNode& node, const std::string& key,
                   core::Offset parent, core::Rect clip) {
    const auto origin = parent + node.offset;
    const core::Rect bounds{origin, node.size};
    if (node.key == key) return bounds.intersects(clip) && node.transitionAlpha > 0;
    if (node.clipContent) {
        const float left = std::max(bounds.left(), clip.left());
        const float top = std::max(bounds.top(), clip.top());
        clip = core::Rect::fromXYWH(left, top, std::max(0.0F, std::min(bounds.right(), clip.right()) - left),
                                   std::max(0.0F, std::min(bounds.bottom(), clip.bottom()) - top));
    }
    if (clip.size.width <= 0 || clip.size.height <= 0 || node.transitionAlpha <= 0) return false;
    for (const auto& child : node.children) {
        if (visibleAnchor(child, key, origin, clip)) return true;
    }
    return false;
}

}  // namespace

AppShell::AppShell(ShellConfig config) : config_(std::move(config)) {
    view_ = config_.initialView;
    // 首帧延迟落地（Element 树 + 订阅同步在首次 rebuildIfDirty 完成）：
    // 构造期求值 config_.build 会重入尚未构造完成的应用对象（如
    // SettingsApp 的 buildUi 读自身后声明成员），属 UB；dirty_ 初始为
    // true 保证首帧前必重建，行为与 eager 落地一致。
    controller_.setWheelSink([this](const core::RenderNode& root,
                                    const core::RenderNode* hit,
                                    core::Offset position, core::Offset delta) {
        if (overlayRoot_) return overlayWheel_ ? overlayWheel_(root, hit, position, delta) : false;
        return config_.onWheel ? config_.onWheel(root, hit, position, delta) : false;
    });
    controller_.setScrollDragSink(
        [this](const core::RenderNode* root, const core::RenderNode* viewport,
               core::Offset position, core::Offset delta,
               core::ScrollDragPhase phase, std::uint64_t timestampMs) {
            const auto& sink = overlayRoot_ ? overlayDrag_ : config_.onScrollDrag;
            return sink ? sink(root, viewport, position, delta, phase, timestampMs) : false;
        });
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
    // M11：overlay 活跃期 action 在事件树上解析（overlay 节点可达）。
    context.root = &eventTree();
    context.handlers = &handlers_;
    context.focus = &focus_;
    context.controller = &controller_;
    // 滚动 sink：经控制器已注册的 wheelSink（虚拟列表/ScrollView 同
    // 源；hit 为空 = 键盘/语义触发的回退视口路径）。M11 review：overlay
    // 活跃期与 action 派发一致走事件树（模态边界统一，主树滚动/激活
    // 不可达）。
    context.scrollSink = [this](const std::string& nodeId, float deltaX,
                                float deltaY) {
        if (!config_.onWheel) {
            return false;
        }
        core::Offset origin{};
        const core::RenderNode* viewport =
            findByIdentity(eventTree(), nodeId, origin);
        if (viewport == nullptr ||
            !core::isScrollableWidget(viewport->type)) {
            return false;
        }
        const core::Offset center =
            origin + core::Offset{viewport->size.width * 0.5F,
                                  viewport->size.height * 0.5F};
        return config_.onWheel(eventTree(), viewport, center,
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
    if (overlayRoot_.has_value()) {
        // M11：overlay 语义作为主树根语义节点的附加子树。
        accessibility::appendSemanticsSubtree(tree, *overlayRoot_, options);
    }
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
    stateBlends_.clear();
    suppressStateBlend_ = true;
    textFonts_ = std::move(fonts);
    controller_.setTextFonts(textFonts_ ? textFonts_.get() : nullptr);
    // 系统字体同时驱动内部 CPU 光栅的字形位图（排版与绘制同源）；
    // 非系统管理器（占位/Skia）保持原绘制路径。
    cpuRenderer_.setSystemFonts(
        textFonts_ ? std::dynamic_pointer_cast<const text::SystemFontManager>(
                         textFonts_)
                   : nullptr);
    dirty_ = true;
    fullRepaintPending_ = true;
}

void AppShell::setTheme(style::Theme theme, bool forceFullRepaint) {
    stateBlends_.clear();
    suppressStateBlend_ = true;
    theme_ = std::move(theme);
    for (auto& transition : transitions_) {
        transition.tween.durationMs = 0;
        transition.sampledAlpha = static_cast<float>(transition.tween.to);
    }
    transitionsPaintPending_ = !transitions_.empty();
    dirty_ = true;
    fullRepaintPending_ = fullRepaintPending_ || forceFullRepaint;
}

void AppShell::setAccessibilitySettings(
    accessibility::AccessibilitySettings settings,
    std::optional<bool> darkMode) {
    stateBlends_.clear();
    suppressStateBlend_ = true;
    accessibility_ = settings;
    theme_ = style::Theme::fromSettings(
        accessibility_, darkMode.value_or(theme_.darkMode),
        theme_.metrics.density, theme_.direction);
    if (settings.reduceAnimation) {
        for (auto& transition : transitions_) {
            transition.tween.durationMs = 0;
            transition.sampledAlpha = static_cast<float>(transition.tween.to);
            transition.finished = true;
            transition.retire = true;
        }
        transitionsPaintPending_ = !transitions_.empty();
    } else {
        for (auto& transition : transitions_) {
            if (transition.finished || transition.retire ||
                transition.baseDurationMs <= 0.0) {
                continue;
            }
            transition.tween.from = transition.sampledAlpha;
            transition.tween.durationMs = transition.baseDurationMs;
            transition.startMs = lastTickMs_;
        }
    }
    dirty_ = true;
    fullRepaintPending_ = true;
}

// --- M11：框架级 overlay ---

void AppShell::setOverlay(core::Widget overlay) {
    const bool replacing = overlayTemplate_.has_value() ||
                           hasPreviousOverlayRoot_;
    controller_.pointerCancel();
    overlayBuilder_ = {};
    overlayWheel_ = {};
    overlayDrag_ = {};
    overlayTemplate_ = std::move(overlay);
    // 打开（首次）覆盖主树像素：全量重绘；替换已打开的 overlay 走
    // overlay 子树 diff（rebuildIfDirty 汇入 damage）。
    dirty_ = true;
    fullRepaintPending_ = fullRepaintPending_ || !replacing;
}

void AppShell::setOverlayBuilder(
    std::function<std::optional<core::Widget>()> builder, WheelSink wheel, ScrollDragSink drag) {
    overlayBuilder_ = std::move(builder);
    overlayWheel_ = std::move(wheel);
    overlayDrag_ = std::move(drag);
    dirty_ = true;
    fullRepaintPending_ = true;
}

void AppShell::clearOverlay() {
    controller_.pointerCancel();
    overlayBuilder_ = {};
    overlayWheel_ = {};
    overlayDrag_ = {};
    if (!overlayTemplate_.has_value() && !overlayRoot_.has_value()) {
        return;
    }
    overlayTemplate_.reset();
    overlayRoot_.reset();
    hasPreviousOverlayRoot_ = false;
    // 关闭：overlay 区域需要回填主树像素。
    dirty_ = true;
    fullRepaintPending_ = true;
    // 焦点可能滞留在 overlay 命名空间（identity 已无对应节点）：清焦点
    // 与活动指针，由应用决定恢复目标（DropdownController 关闭后
    // focusFirstFocusable）。
    focus_.clearFocus();
    controller_.pointerCancel();
}

// --- 事件分发 ---

void AppShell::pointerDown(core::Offset position) {
    dismissTooltips();
    rebuildIfDirty();
    controller_.pointerDown(eventTree(), position, lastTickMs_);
}

void AppShell::pointerMove(core::Offset position) {
    rebuildIfDirty();
    controller_.pointerMove(eventTree(), position, lastTickMs_);
}

void AppShell::pointerUp(core::Offset position) {
    rebuildIfDirty();
    controller_.pointerUp(eventTree(), position, lastTickMs_);
}

void AppShell::pointerCancel() {
    dismissTooltips();
    controller_.pointerCancel();
}

bool AppShell::wheel(core::Offset position, core::Offset delta) {
    dismissTooltips();
    rebuildIfDirty();
    return controller_.wheel(eventTree(), position, delta);
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
    controller_.keyDown(eventTree(), key, modifiers, keyChar);
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
    retargetStateBlends(fresh);
    root_ = fresh;
    previousRoot_ = std::move(fresh);
    hasPreviousRoot_ = true;
    rebuildTooltipTemplates();
    // M11：overlay 与主树同拍重建（独立布局/独立 identity 命名空间；
    // 打开期间 overlay 子树 diff 汇入 damage，打开首帧走全量）。
    if (overlayBuilder_) {
        overlayTemplate_ = overlayBuilder_();
        if (!overlayTemplate_) {
            controller_.pointerCancel();
            focus_.clearFocus();
            overlayBuilder_ = {};
            overlayWheel_ = {};
            overlayDrag_ = {};
            overlayRoot_.reset();
            hasPreviousOverlayRoot_ = false;
            fullRepaintPending_ = true;
        }
    }
    if (overlayTemplate_.has_value()) {
        core::RenderNode freshOverlay = layout::LayoutEngine::layout(
            *overlayTemplate_, core::Constraints::tight(view_),
            styleContext(), textFontSource());
        if (hasPreviousOverlayRoot_) {
            treeDamageValid_ =
                treeDamageValid_ &&
                core::collectDamage(previousOverlayRoot_, freshOverlay,
                                    pendingDamage_);
        } else {
            treeDamageValid_ = false;
        }
        overlayRoot_ = freshOverlay;
        previousOverlayRoot_ = std::move(freshOverlay);
        hasPreviousOverlayRoot_ = true;
    }
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
    // 采样值由 tick 推进，renderFrame 不自带时钟）。M11：tooltip Hidden
    // 态 alpha 归零（含重建后的新树）。
    std::vector<core::Rect> motionDamage;
    const bool motionPaint = applyTransitions(motionDamage);
    const bool blendPaint = applyStateBlend(motionDamage);
    const bool tooltipPaint = applyTooltipVisibility(motionDamage);
    const bool portalPaint = updateTooltipPaintNodes(motionDamage);
    render::PaintOptions options;
    options.suppressedIdentities = tooltipSourceIdentities_;
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
                           optionsChanged || motionPaint || blendPaint ||
                           tooltipPaint || portalPaint;
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
    if (overlayRoot_.has_value()) {
        // M11：overlay 命令后置叠加（绘制序 = 遮挡序）。
        commands.extend(render::recordScene(*overlayRoot_, options,
                                             textFontSource()));
    }
    options.suppressedIdentities = {};
    for (const auto& tooltip : tooltipPaintNodes_) {
        commands.extend(render::recordScene(tooltip, options, textFontSource()));
    }
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
    hasTicked_ = true;
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
    animating = advanceTooltips(nowMs) || animating;
    if (config_.onAnimate) {
        animating = config_.onAnimate(*this, nowMs) || animating;
    }
    animationsActive_ = animating || !stateBlends_.empty();
}

// --- M10：转场驱动 ---

void AppShell::beginTransition(TransitionSpec spec) {
    if (spec.key.empty()) {
        return;
    }
    // A key owns one alpha channel. Reversal starts at the visible sample;
    // replacing the record also cancels its obsolete completion callback.
    for (const auto& current : transitions_) {
        if (current.key == spec.key) {
            spec.from = current.sampledAlpha;
            break;
        }
    }
    std::erase_if(transitions_, [&](const ActiveTransition& current) {
        return current.key == spec.key;
    });
    ActiveTransition transition;
    transition.baseDurationMs = spec.durationMs;
    if (accessibility_.reduceAnimation) {
        spec.durationMs = 0.0;
    }
    transition.key = spec.key;
    // identity 延迟解析：begin 可能早于含该子树的首次重建（如打开 dialog
    // 后立即 beginFadeIn）。
    if (const core::RenderNode* node = core::findNodeByKey(root_, spec.key)) {
        transition.identity = node->identity;
    }
    transition.tween = core::Tween{spec.from, spec.to, spec.durationMs,
                                   spec.easing};
    transition.sampledAlpha = static_cast<float>(transition.tween.sample(0.0));
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
    spec.easing = core::Easing::EaseInOut;
    spec.onComplete = std::move(onComplete);
    beginTransition(std::move(spec));
}

bool AppShell::advanceTransitions(std::uint64_t nowMs) {
    // 上一拍完成的转场本拍退休：进场终值由此获得一次提交机会；退场的
    // onComplete 通常同拍移除子树（重建后 identity 缺失即清除，终值
    // alpha≈0 与不画等价）。
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

std::optional<std::uint64_t> AppShell::animationWakeMs() const {
    const std::uint64_t delay = theme_.motion.tooltipDelayMs;
    std::optional<std::uint64_t> wake;
    for (const auto& tip : tooltips_) {
        if (tip.phase == TooltipRegistration::Phase::Armed) {
            const std::uint64_t at = tip.armedAtMs + delay;
            wake = wake.has_value() ? std::min(*wake, at) : at;
        }
    }
    return wake;
}

// --- M11：Tooltip hover 延迟驱动 ---

void AppShell::registerTooltip(std::string anchorKey,
                               std::string tooltipKey) {
    if (anchorKey.empty() || tooltipKey.empty()) {
        return;
    }
    // 重复注册幂等（重建装配期多次调用安全）。
    for (const auto& tip : tooltips_) {
        if (tip.tooltipKey == tooltipKey) {
            return;
        }
    }
    tooltips_.push_back(
        TooltipRegistration{std::move(anchorKey), std::move(tooltipKey),
                            TooltipRegistration::Phase::Hidden, 0});
    dirty_ = true;
}

bool AppShell::hasTransitionForKey(const std::string& key) const {
    for (const auto& transition : transitions_) {
        if (transition.key == key) {
            return true;
        }
    }
    return false;
}

void AppShell::rebuildTooltipTemplates() {
    tooltipTemplates_.clear();
    if (!element_) return;
    const auto find = [&](const auto& self, const core::Element& element,
                          const std::string& key) -> const core::Widget* {
        if (element.widget().key == key) return &element.widget();
        for (const auto& child : element.children()) {
            if (const auto* found = self(self, *child, key)) return found;
        }
        return nullptr;
    };
    for (const auto& tip : tooltips_) {
        const auto* widget = find(find, *element_, tip.tooltipKey);
        if (!widget || widget->type != core::WidgetType::Tooltip) continue;
        const auto theme = effectiveThemeForKey(tip.anchorKey);
        const style::StyleContext context{theme, interactionSnapshot_, accessibility_, deviceScale_};
        auto paint = layout::LayoutEngine::layout(*widget,
            core::Constraints::loose({std::max(0.0F, std::min(280.0F, view_.width - 16.0F)),
                                      std::max(0.0F, view_.height - 16.0F)}),
            context, textFontSource());
        tooltipTemplates_.emplace(tip.tooltipKey, std::move(paint));
    }
}

void AppShell::dismissTooltips() {
    for (auto& tip : tooltips_) {
        tip.phase = TooltipRegistration::Phase::Hidden;
        tip.suppressed = true;
        std::erase_if(transitions_, [&](const ActiveTransition& transition) {
            return transition.key == tip.tooltipKey;
        });
    }
}

bool AppShell::updateTooltipPaintNodes(std::vector<core::Rect>& damage) {
    std::vector<core::RenderNode> next;
    tooltipSourceIdentities_.clear();
    for (auto& tip : tooltips_) {
        const auto* source = core::findNodeByKey(root_, tip.tooltipKey);
        const auto* anchor = core::findNodeByKey(root_, tip.anchorKey);
        if (source) tooltipSourceIdentities_.push_back(source->identity);
        if (!source || !anchor || !anchor->enabled || hasOverlay() ||
            !visibleAnchor(root_, tip.anchorKey, {}, core::Rect{{}, view_})) {
            tip.phase = TooltipRegistration::Phase::Hidden;
            std::erase_if(transitions_, [&](const ActiveTransition& t) { return t.key == tip.tooltipKey; });
            continue;
        }
        if (source->transitionAlpha <= 0.0F) continue;
        const auto anchorOrigin = core::absoluteOffset(root_, tip.anchorKey);
        if (!core::Rect{anchorOrigin, anchor->size}.intersects(core::Rect{{}, view_})) continue;
        const auto cached = tooltipTemplates_.find(tip.tooltipKey);
        auto paint = cached == tooltipTemplates_.end() ? *source : cached->second;
        paint.identity = source->identity;
        paint.transitionAlpha = source->transitionAlpha;
        const float margin = 8.0F;
        paint.offset.x = std::clamp(anchorOrigin.x, std::min(margin, view_.width),
            std::max(std::min(margin, view_.width), view_.width - margin - paint.size.width));
        const float below = anchorOrigin.y + anchor->size.height + margin;
        const float y = below + paint.size.height <= view_.height - margin
                            ? below : anchorOrigin.y - margin - paint.size.height;
        paint.offset.y = std::clamp(y, std::min(margin, view_.height),
            std::max(std::min(margin, view_.height), view_.height - margin - paint.size.height));
        next.push_back(std::move(paint));
    }
    if (next == tooltipPaintNodes_) return false;
    // Include shadow extents through the same damage walker as ordinary nodes.
    for (const auto& old : tooltipPaintNodes_) {
        auto empty = old;
        empty.transitionAlpha = 0;
        (void)core::collectDamage(old, empty, damage);
    }
    for (const auto& node : next) {
        auto empty = node;
        empty.transitionAlpha = 0;
        (void)core::collectDamage(empty, node, damage);
    }
    tooltipPaintNodes_ = std::move(next);
    return true;
}

bool AppShell::advanceTooltips(std::uint64_t nowMs) {
    if (tooltips_.empty()) {
        return false;
    }
    const std::uint64_t delay = theme_.motion.tooltipDelayMs;
    const double fade = static_cast<double>(theme_.motion.tooltipFadeMs);
    bool active = false;
    const std::string& hoveredKey = controller_.hoveredKey();
    const auto showTooltip = [&](TooltipRegistration& tip) {
        TransitionSpec spec;
        spec.key = tip.tooltipKey;
        spec.from = 0.0F;
        spec.to = 1.0F;
        spec.durationMs = fade;
        spec.easing = core::Easing::EaseOut;
        beginTransition(std::move(spec));
        tip.phase = TooltipRegistration::Phase::Visible;
    };
    for (auto& tip : tooltips_) {
        const auto* anchor = core::findNodeByKey(root_, tip.anchorKey);
        const bool triggered = anchor && anchor->enabled && !hasOverlay() &&
            visibleAnchor(root_, tip.anchorKey, {}, core::Rect{{}, view_}) &&
            (hoveredKey == tip.anchorKey || focus_.focusedKey() == tip.anchorKey);
        if (!triggered) tip.suppressed = false;
        const bool hovered = triggered && !tip.suppressed;
        switch (tip.phase) {
            case TooltipRegistration::Phase::Hidden:
                if (hovered) {
                    tip.phase = TooltipRegistration::Phase::Armed;
                    tip.armedAtMs = nowMs;
                    // reduceAnimation（延迟归零）：同拍直接显示。
                    if (nowMs - tip.armedAtMs >= delay) {
                        showTooltip(tip);
                        active = true;
                    }
                }
                break;
            case TooltipRegistration::Phase::Armed:
                if (!hovered) {
                    tip.phase = TooltipRegistration::Phase::Hidden;
                } else if (nowMs >= tip.armedAtMs &&
                           nowMs - tip.armedAtMs >= delay) {
                    showTooltip(tip);
                    active = true;
                }
                // 等待期不请求连续帧：由 animationWakeMs 经 FrameScheduler
                // 空闲定时唤醒（M11 review：消除 ~400ms 的空转动画帧）。
                break;
            case TooltipRegistration::Phase::Visible:
                if (!hovered) {
                    TransitionSpec spec;
                    spec.key = tip.tooltipKey;
                    spec.from = 1.0F;
                    spec.to = 0.0F;
                    spec.durationMs = fade;
                    spec.easing = core::Easing::EaseOut;
                    beginTransition(std::move(spec));
                    tip.phase = TooltipRegistration::Phase::Fading;
                }
                break;
            case TooltipRegistration::Phase::Fading:
                if (hovered) {
                    // 淡出中折返：重新淡入。
                    showTooltip(tip);
                } else if (!hasTransitionForKey(tip.tooltipKey)) {
                    // 淡出转场已退休：回到 Hidden（apply 保持 alpha 0）。
                    tip.phase = TooltipRegistration::Phase::Hidden;
                }
                break;
        }
    }
    return active;
}

bool AppShell::applyTooltipVisibility(std::vector<core::Rect>& damage) {
    if (tooltips_.empty()) {
        return false;
    }
    bool changed = false;
    for (const auto& tip : tooltips_) {
        // Visible 态的 alpha=1 由淡入转场落地（转场退休后树保持 1）。
        // Hidden/Armed 强制归零——覆盖重建后的新树（Widget 默认 alpha 1）；
        // Fading 在转场驱动期间不干预。
        if (tip.phase == TooltipRegistration::Phase::Visible) {
            continue;
        }
        if (tip.phase == TooltipRegistration::Phase::Fading &&
            hasTransitionForKey(tip.tooltipKey)) {
            continue;  // 淡出转场正在驱动 alpha
        }
        const core::RenderNode* node =
            core::findNodeByKey(root_, tip.tooltipKey);
        if (node == nullptr) {
            continue;
        }
        if (node->transitionAlpha != 0.0F) {
            core::RenderNode* mutableNode =
                findMutableByIdentity(root_, node->identity);
            if (mutableNode != nullptr) {
                mutableNode->transitionAlpha = 0.0F;
                addNodeRect(damage, node->identity, "");
                changed = true;
            }
        }
    }
    return changed;
}

// --- M10：状态色过渡 ---

void AppShell::retargetStateBlends(const core::RenderNode& fresh) {
    // Scope themes are value snapshots: mutation through the same pointer must
    // invalidate active colors just like replacing the window theme does.
    std::map<const core::Element*, style::Theme> scopes;
    const auto collectScopes = [&](const auto& self, const core::Element& element) -> void {
        const auto& widget = element.widget();
        if (widget.type == core::WidgetType::ThemeScope && widget.themeOverride) {
            scopes.emplace(&element, *static_cast<const style::Theme*>(widget.themeOverride));
        }
        for (const auto& child : element.children()) self(self, *child);
    };
    if (element_) collectScopes(collectScopes, *element_);
    suppressStateBlend_ = suppressStateBlend_ || scopes != scopeThemes_;
    scopeThemes_ = std::move(scopes);
    if (!motionEnabled() || suppressStateBlend_ ||
        theme_.motion.stateTransitionMs == 0) {
        stateBlends_.clear();
        suppressStateBlend_ = false;
        return;
    }
    std::map<std::string, const core::RenderNode*> previous;
    const auto collect = [&](const auto& self, const core::RenderNode& node) -> void {
        previous.emplace(node.identity, &node);
        for (const auto& child : node.children) self(self, child);
    };
    collect(collect, root_);
    std::set<std::string> live;
    const auto visit = [&](const auto& self, const core::RenderNode& node) -> void {
        live.insert(node.identity);
        const auto found = previous.find(node.identity);
        const auto* old = found == previous.end() ? nullptr : found->second;
        auto active = stateBlends_.find(node.identity);
        if (!node.enabled || old == nullptr || old->type != node.type) {
            stateBlends_.erase(node.identity);
        } else {
            const auto& target = active == stateBlends_.end()
                                     ? old->style : active->second.to;
            if (!(target == node.style) &&
                !(core::lerpStyleColors(old->style, node.style, 0.0F) == node.style)) {
                stateBlends_.insert_or_assign(node.identity,
                    StateBlend{old->style, node.style, lastTickMs_});
            } else if (!(target == node.style)) {
                stateBlends_.erase(node.identity);
            }
        }
        for (const auto& child : node.children) self(self, child);
    };
    visit(visit, fresh);
    std::erase_if(stateBlends_, [&](const auto& item) {
        return !live.contains(item.first);
    });
}

bool AppShell::applyStateBlend(std::vector<core::Rect>& damage) {
    bool changed = false;
    for (auto it = stateBlends_.begin(); it != stateBlends_.end();) {
        auto* node = findMutableByIdentity(root_, it->first);
        auto& blend = it->second;
        const auto duration = node && node->type == core::WidgetType::Switch
                                  ? theme_.motion.switchTransitionMs
                                  : theme_.motion.stateTransitionMs;
        const double elapsed = lastTickMs_ >= blend.startMs
                                   ? double(lastTickMs_ - blend.startMs) : 0.0;
        const core::Tween tween{0.0, 1.0, double(duration), core::Easing::EaseOut};
        const float t = float(tween.sample(elapsed));
        if (node) {
            auto style = core::lerpStyleColors(blend.from, blend.to, t);
            if (!(node->style == style)) {
                node->style = std::move(style);
                addNodeRect(damage, node->identity, "");
                changed = true;
            }
        }
        if (!node || tween.finished(elapsed)) it = stateBlends_.erase(it);
        else ++it;
    }
    return changed;
}

void AppShell::swapRoot(core::Widget root) {
    swapTemplate_ = std::move(root);
    dirty_ = true;
}

// --- 查询 ---

style::Theme AppShell::effectiveThemeForKey(const std::string& key) const {
    const style::Theme* result = &theme_;
    const auto visit = [&](const auto& self, const core::Element& element,
                           const style::Theme* inherited) -> bool {
        const auto& widget = element.widget();
        const auto* effective = widget.type == core::WidgetType::ThemeScope && widget.themeOverride
                                    ? static_cast<const style::Theme*>(widget.themeOverride) : inherited;
        if (widget.key == key) { result = effective; return true; }
        for (const auto& child : element.children()) {
            if (self(self, *child, effective)) return true;
        }
        return false;
    };
    if (element_) visit(visit, *element_, &theme_);
    return *result;
}

core::Rect AppShell::focusedTextRect() const {
    core::Offset origin{};
    const core::RenderNode* found = findFocusedField(origin);
    if (found == nullptr) {
        return core::Rect{};
    }
    const auto display = core::textFieldDisplay(
        *found, controller_.composition(), controller_.selectionStart());
    const auto layout = text::TextLayout::layout(
        display.text, core::textFieldLayoutStyle(*found),
        core::textFieldWrapWidth(*found), textFontSource());
    auto caret = core::textFieldCaretRect(
        *found, layout, controller_.caretGraphemes(), true);
    caret.origin = origin + caret.origin;
    return caret;
}

int AppShell::focusedCaretOffset() const {
    core::Offset origin{};
    if (findFocusedField(origin) == nullptr) {
        return 0;
    }
    return static_cast<int>(focusedTextRect().origin.x - origin.x);
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
        core::addPaintDamage(*node, origin, damage);
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
