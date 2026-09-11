#pragma once

// Counter application shared by the windowed example (main.cpp) and the
// headless integration tests. Owns the plan §5.2 frame pipeline:
// events -> state -> reconcile -> layout -> paint. All members are inline so
// both targets compile it directly.
//
// App-layer responsibilities per plan §6.1: state keys, event handlers and
// the frame loop live here, not inside the framework.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "lumen/accessibility/bridge.h"
#include "lumen/core/damage.h"
#include "lumen/core/element.h"
#include "lumen/core/interaction.h"
#include "lumen/core/state.h"
#include "lumen/core/tween.h"
#include "lumen/core/utf8.h"
#include "lumen/dsl/dsl.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/style/state.h"
#include "lumen/text/font_manager.h"
#include "lumen/text/text_layout.h"

namespace lumen::examples {

class CounterApp {
  public:
    CounterApp() : CounterApp(buildUi()) {}

    // Root from the text DSL (`--dsl counter.lumen`, plan §7).
    explicit CounterApp(core::Widget root) {
        initialize(std::move(root));
    }

    CounterApp(const CounterApp&) = delete;
    CounterApp& operator=(const CounterApp&) = delete;
    CounterApp(CounterApp&&) = delete;
    CounterApp& operator=(CounterApp&&) = delete;

    // The declarative UI (plan §6.1 example shape). Static so the DSL golden
    // test can compare it against the parsed `.lumen` document.
    [[nodiscard]] static core::Widget buildUi() {
        using namespace dsl;
        core::Widget page = container(
            column({core::withKey(dsl::text("Count: ", bind("counter")),
                                  "count-text"),
                    core::withKey(button("Increment", onClick("increment")),
                                  "increment-button"),
                    core::withKey(text_field(bind("name"), placeholder("Name")),
                                  "name-field")},
                   core::EdgeInsets::all(24.0F), 12.0F),
            core::Color::fromRGBA(24, 24, 27));
        page.key = "root";
        return page;
    }

    void setView(core::Size size) {
        if (size.width <= 0.0F || size.height <= 0.0F) {
            return;
        }
        view_ = size;
        dirty_ = true;
        // Constraint change invalidates localized damage; full repaint.
        fullRepaintPending_ = true;
    }
    void setDeviceScale(float scale) {
        deviceScale_ = scale;
        cpuRenderer_.setDeviceScale(scale);
        // A pure DPI change alters pixel dimensions; the cache and the
        // preserved previous frame are both stale until a full repaint.
        fullRepaintPending_ = true;
    }

    // Overrides the paint target (plan §7: CPU/Skia switch). Null restores
    // the internal CPU renderer. The external renderer must outlive use.
    void setRenderer(render::Renderer* renderer) {
        if (externalRenderer_ == renderer) {
            return;
        }
        externalRenderer_ = renderer;
        // The two backends do not share framebuffer contents. Invalidate the
        // paint cache whenever the target changes so returning to CPU cannot
        // present pixels produced before the external renderer was used.
        framePainted_ = false;
        fullRepaintPending_ = true;
    }

    // Reconcile + relayout when state or view changed (plan §5.2 steps 2-3).
    // The template (C++ builders or parsed `.lumen`) is copied, binds are
    // resolved against the store, and the Element tree reconciles onto it.
    // The layout diff becomes this frame's dirty rects (plan 阶段6).
    void rebuildIfDirty() {
        if (!dirty_) {
            return;
        }
        core::Widget next = uiTemplate_;
        core::applyBinds(next, state_);
        element_->update(std::move(next));
        // Keep subscriptions in lockstep with the tree: newly bound keys get
        // observers, keys dropped by the rebuild are cleaned up (plan §9).
        syncSubscriptions(core::collectBindKeys(element_->widget()));
        core::RenderNode fresh = layout::LayoutEngine::layout(
            element_->widget(), core::Constraints::tight(view_),
            styleContext());
        // Accumulate damage across rebuilds that share one paint: the screen
        // still shows the last PAINTED tree, so dropping earlier rects here
        // would leave stale pixels. renderFrame clears after painting and
        // re-arms validity (the fresh paint makes screen and tree agree).
        // treeDamageValid_ is sticky-false only between rebuilds that have
        // not been painted yet — never across paints.
        if (hasPreviousRoot_) {
            treeDamageValid_ = treeDamageValid_ &&
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
    }

    // Paint + post-frame bookkeeping; returns the frame hash (plan §9).
    //
    // Damage-aware painting (plan 阶段6: 脏矩形/绘制缓存):
    //  - hover/pressed/focused 等控件状态先进入交互快照，快照变化触发
    //    重建——状态折算进 RenderNode 的 resolved style，diff 产生的
    //    damage 自动覆盖新旧状态（visual-system §5 规则 6）；
    //  - caret/selection/composition 仍是绘制瞬态，只重绘受影响节点；
    //  - 无树/瞬态/动画变化的帧跳过绘制并返回上一哈希（绘制缓存）；
    //  - 内部 CPU 渲染器在保留的上一帧之上只重绘损伤区；结果与全量重
    //    绘逐像素一致（由测试断言）；
    //  - `forceFullRepaint` 绕过两层优化。
    std::uint64_t renderFrame(bool forceFullRepaint = false) {
        // 交互快照变化 → 重建（resolved style 折算状态）。
        syncInteractionSnapshot();
        if (!(interactionSnapshot_ == lastInteraction_)) {
            lastInteraction_ = interactionSnapshot_;
            dirty_ = true;
        }
        rebuildIfDirty();
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
                               optionsChanged;
        if (!needPaint) {
            // Paint cache hit: nothing observable changed since last frame.
            rebuiltThisFrame_ = false;
            return lastFrameHash_;
        }

        std::vector<core::Rect> damage = pendingDamage_;
        if (optionsChanged) {
            // Focus/caret/selection/composition changes only repaint the
            // affected nodes; identities locate them in the current tree.
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
        render::RenderCommandList commands = render::recordScene(root_, options);
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

        // Bookkeeping for the next frame's cache/damage decisions.
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
        // The fresh paint makes screen and tree agree again: damage
        // tracking re-arms even after a full-repaint fallback.
        treeDamageValid_ = true;
        if (externalRenderer_ == nullptr) {
            lastFrameHash_ = render::frameHash(cpuRenderer_.pixels());
            return lastFrameHash_;
        }
        return 0;
    }

    // 可访问性设置变化 → 派生 Theme（font scale/high contrast/reduce
    // animation/density，visual-system §4）。
    void setAccessibilitySettings(
        accessibility::AccessibilitySettings settings) {
        accessibility_ = settings;
        theme_ = style::Theme::fromSettings(accessibility_);
        dirty_ = true;
        fullRepaintPending_ = true;
    }

    // 视觉系统应用入口（visual-system §6.3）：默认暗色 Theme + 交互快照。
    // 快照是成员——StyleContext 只持引用，临时对象会悬空。
    void syncInteractionSnapshot() {
        interactionSnapshot_ = style::InteractionStateSnapshot{
            controller_.hoveredIdentity(), controller_.pressedIdentity(),
            focus_.focusedIdentity()};
    }

    [[nodiscard]] style::StyleContext styleContext() const {
        return style::StyleContext{theme_, interactionSnapshot_,
                                   accessibility_, deviceScale_};
    }

    // Advances time-driven state: the caret blink tween (plan 阶段6).
    // Apps own the clock; tests pass fixed timestamps so animation is
    // deterministic. Unfocused frames keep alpha 1.0 (stable hashes).
    // reduceAnimation 时闪烁时长归零（MotionTokens，visual-system §4）。
    void tick(std::uint64_t nowMs) {
        lastTickMs_ = nowMs;
        if (!controller_.wantsTextInput() ||
            theme_.motion.caretBlinkHalfPeriodMs == 0) {
            blinkAnchored_ = false;
            caretAlpha_ = 1.0F;
            return;
        }
        if (!blinkAnchored_ || nowMs < blinkAnchorMs_) {
            // Anchor (or re-anchor on non-monotonic timestamps) at the
            // phase start so unsigned subtraction cannot wrap.
            blinkAnchored_ = true;
            blinkAnchorMs_ = nowMs;
        }
        const double kHalfPeriodMs =
            static_cast<double>(theme_.motion.caretBlinkHalfPeriodMs);
        const double phase =
            std::fmod(static_cast<double>(nowMs - blinkAnchorMs_),
                      kHalfPeriodMs * 2.0);
        const core::Tween down{1.0, 0.0, kHalfPeriodMs, core::Easing::EaseInOut};
        const core::Tween up{0.0, 1.0, kHalfPeriodMs, core::Easing::EaseInOut};
        caretAlpha_ = static_cast<float>(
            phase < kHalfPeriodMs ? down.sample(phase)
                                  : up.sample(phase - kHalfPeriodMs));
    }

    // Hot reload entry (plan 阶段6): swap the UI template, keep all state;
    // the rebuild diffs the trees so only changed nodes repaint.
    void swapRoot(core::Widget root) {
        uiTemplate_ = std::move(root);
        dirty_ = true;
    }

    // Pointer in logical (root) coordinates; rebuilds first so hits land on
    // the current layout even when events and updates share one batch.
    void pointerDown(core::Offset position) {
        rebuildIfDirty();
        controller_.pointerDown(root_, position, lastTickMs_);
    }

    void pointerMove(core::Offset position) {
        rebuildIfDirty();
        controller_.pointerMove(root_, position);
    }

    void pointerUp(core::Offset position) {
        rebuildIfDirty();
        controller_.pointerUp(root_, position);
    }

    void textInput(const std::string& text) { controller_.textInput(text); }

    void textEditing(const std::string& text) {
        controller_.setComposition(text);
    }

    void cancelComposition() { controller_.cancelComposition(); }

    void keyDown(core::Key key) {
        rebuildIfDirty();
        controller_.keyDown(root_, key);
    }
    void keyDown(core::Key key, core::KeyModifiers modifiers, char keyChar) {
        rebuildIfDirty();
        controller_.keyDown(root_, key, modifiers, keyChar);
    }

    [[nodiscard]] int counterValue() const {
        return std::atoi(state_.get("counter").c_str());
    }
    [[nodiscard]] const core::StateStore& state() const { return state_; }
    [[nodiscard]] const core::RenderNode& root() const { return root_; }
    [[nodiscard]] const core::InteractionController& controller() const {
        return controller_;
    }
    // Stage-6 introspection: how many frames took the damage-scoped partial
    // repaint path. Tests assert on it so the optimization cannot silently
    // degrade into full repaints (which would still look correct).
    [[nodiscard]] std::uint32_t partialRepaintCount() const {
        return partialRepaintCount_;
    }
    [[nodiscard]] bool wantsTextInput() const {
        return controller_.wantsTextInput();
    }
    // Logical rect at the caret for IME candidate positioning
    // (SDL_SetTextInputArea). Empty rect when nothing is focused. Uses the
    // same text::TextLayout as the painter so the candidate window tracks
    // the caret on Linux IBus/Fcitx/Wayland. Query after renderFrame() (as
    // main.cpp does) so the rect tracks the fresh layout.
    [[nodiscard]] core::Rect focusedTextRect() const {
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
        const auto layout = text::TextLayout::layout(
            display, style, 0.0F, text::PlaceholderFontManager::shared());
        std::size_t lineIndex = 0;
        const float x = layout.graphemeToX(controller_.caretGraphemes(),
                                           &lineIndex);
        return core::Rect{
            core::Offset{origin.x + found->commonStyle().padding.left + x,
                         origin.y},
            core::Size{1.0F, found->size.height}};
    }
    // Caret x offset (logical px, relative to the focused field origin) —
    // kept for PlatformWindow implementations that take a separate cursor
    // offset (SDL_SetTextInputArea cursor).
    [[nodiscard]] int focusedCaretOffset() const {
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
        const auto layout = text::TextLayout::layout(
            display, style, 0.0F, text::PlaceholderFontManager::shared());
        return static_cast<int>(found->commonStyle().padding.left +
                                layout.graphemeToX(controller_.caretGraphemes(),
                                                   nullptr));
    }
    // Framebuffer of the internal CPU renderer; the windowed loop presents
    // from here unless an external (Skia) renderer is active.
    [[nodiscard]] const render::PixelBuffer& pixels() const {
        return cpuRenderer_.pixels();
    }
    // 最近一次提交的渲染统计（--diagnostics 输出，v0.2 阶段7E）。
    [[nodiscard]] render::RenderStats stats() {
        return activeRenderer().stats();
    }
    // 当前渲染后端能力（--diagnostics 启动输出）。
    [[nodiscard]] render::RendererCapabilities capabilities() {
        return activeRenderer().capabilities();
    }

  private:
    // Depth-first search by stable identity; `origin` accumulates the
    // root-relative offset of the match.
    static const core::RenderNode* findByIdentity(const core::RenderNode& node,
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

    // Shared focused-field lookup: stable identity first, key fallback.
    // `origin` receives the root-relative offset of the match.
    [[nodiscard]] const core::RenderNode* findFocusedField(
        core::Offset& origin) const {
        const std::string& identity = focus_.focusedIdentity();
        const std::string& key = focus_.focusedKey();
        origin = core::Offset{};
        const core::RenderNode* found = nullptr;
        if (!identity.empty()) {
            found = findByIdentity(root_, identity, origin);
        }
        if (found == nullptr && !key.empty()) {
            if (const core::RenderNode* byKey =
                    core::findNodeByKey(root_, key)) {
                found = byKey;
                origin = core::absoluteOffset(root_, key);
            }
        }
        return found;
    }

    // Stores the root template and wires state/handlers/subscriptions. Both
    // constructors funnel through here.
    void initialize(core::Widget root) {
        uiTemplate_ = std::move(root);
        element_.emplace(uiTemplate_);
        state_.set("counter", "0");
        state_.set("name", "");
        handlers_["increment"] = [this] {
            state_.set("counter", std::to_string(counterValue() + 1));
        };
        syncSubscriptions(core::collectBindKeys(uiTemplate_));
    }

    [[nodiscard]] render::Renderer& activeRenderer() {
        return externalRenderer_ != nullptr ? *externalRenderer_
                                            : cpuRenderer_;
    }

    // Appends the current-tree rect of a node located by identity (key
    // fallback) to the damage list; missing nodes contribute nothing.
    void addNodeRect(std::vector<core::Rect>& damage,
                     const std::string& identity, const std::string& key) {
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

    // Diffs the subscribed bind keys against `keys`: subscribes new ones and
    // unsubscribes keys the latest tree no longer references.
    void syncSubscriptions(const std::set<std::string>& keys) {
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

    core::StateStore state_{};
    core::HandlerRegistry handlers_{};
    std::map<std::string, core::StateStore::ObserverId> subscriptions_{};
    core::FocusManager focus_{};
    core::InteractionController controller_{state_, handlers_, focus_};
    // 视觉系统：默认暗色主题（窗口根 Theme，visual-system §4）。
    style::Theme theme_{style::Theme::dark()};
    accessibility::AccessibilitySettings accessibility_{};
    style::InteractionStateSnapshot interactionSnapshot_{};
    core::Widget uiTemplate_{};
    std::optional<core::Element> element_{};
    core::RenderNode root_{};
    // Damage/paint-cache bookkeeping (plan 阶段6).
    core::RenderNode previousRoot_{};
    bool hasPreviousRoot_{false};
    std::vector<core::Rect> pendingDamage_{};
    // Starts "true" vacuously (screen and tree agree before any frame);
    // the first rebuild (no previous root) sets it false, and every paint
    // re-arms it — so false is sticky only until the next full paint.
    bool treeDamageValid_{true};
    bool rebuiltThisFrame_{false};
    bool framePainted_{false};
    bool fullRepaintPending_{false};
    // 视觉系统：交互快照（hover/press/focus）驱动重建。
    style::InteractionStateSnapshot lastInteraction_{};
    std::string lastFocusedIdentity_{};
    std::size_t lastCaret_{0};
    std::size_t lastSelectionStart_{0};
    std::size_t lastSelectionEnd_{0};
    std::string lastComposition_{};
    float caretAlpha_{1.0F};
    float lastCaretAlpha_{1.0F};
    bool blinkAnchored_{false};
    std::uint64_t blinkAnchorMs_{0};
    std::uint64_t lastFrameHash_{0};
    std::uint32_t partialRepaintCount_{0};
    core::Size view_{800.0F, 600.0F};
    float deviceScale_{1.0F};
    std::uint64_t frameIndex_{0};
    std::uint64_t lastTickMs_{0};
    render::CpuRenderer cpuRenderer_{1.0F};
    render::Renderer* externalRenderer_{nullptr};
    bool dirty_{true};
};

}  // namespace lumen::examples
