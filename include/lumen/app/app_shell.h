#pragma once

// M2（自用路线图）：应用壳。
//
// 把 counter/settings 各自手写的生命周期、事件路由、重建、布局、绘制和
// damage 管线收敛为可复用的 UI 线程应用壳：
//   - AppShell 拥有 StateStore/HandlerRegistry/FocusManager/
//     InteractionController、Element reconcile、订阅同步、布局 + damage、
//     绘制缓存与脏矩形提交、caret 闪烁、IME 候选框查询；
//   - runApp 驱动 UI 线程主循环：ApplicationHost 事件泵 → 归一化事件
//     分发 → FrameScheduler 决策 → renderFrame → 呈现 → 空闲等待。
//
// 接口约束（roadmap §4 M2）：
//   - 公共头文件不引入 SDL/Skia/平台 SDK 类型；宿主经
//     platform::ApplicationHost 抽象注入，渲染器经 render::Renderer 注入；
//   - build 过程不接触平台窗口或 Renderer，所有绘制经 RenderCommandList
//     提交；
//   - 支持 headless Fake host 与外部测试 renderer（现有测试不依赖真实
//     窗口）；
//   - 应用状态（StateStore/Element/交互）仍由 UI 线程拥有。

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "lumen/accessibility/bridge.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/core/damage.h"
#include "lumen/core/element.h"
#include "lumen/core/geometry.h"
#include "lumen/core/interaction.h"
#include "lumen/core/render_node.h"
#include "lumen/core/state.h"
#include "lumen/core/tween.h"
#include "lumen/layout/layout.h"
#include "lumen/platform/application_host.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"
#include "lumen/render/renderer.h"
#include "lumen/style/state.h"
#include "lumen/style/theme.h"
#include "lumen/text/font_manager.h"
#include "lumen/text/text_layout.h"

namespace lumen::platform {
class ApplicationHost;
}

namespace lumen::app {
// 滚轮 sink（与 core::InteractionController::WheelSink 同形）：命中视口
// 由交互层解析；返回 true 表示已消费。
using WheelSink = std::function<bool(
    const core::RenderNode& root, const core::RenderNode* hit,
    core::Offset position, core::Offset delta)>;

// M10：视口拖动滚动 sink（与 core::InteractionController::ScrollDragSink
// 同形；Cancel 时 root/viewport 为空）。应用把 Update/End 汇入
// ScrollController（applyDrag/noteDragSample/endDrag）。
using ScrollDragSink = std::function<bool(
    const core::RenderNode* root, const core::RenderNode* viewport,
    core::Offset position, core::Offset delta,
    core::ScrollDragPhase phase, std::uint64_t timestampMs)>;

// M14：overlay 动效步进 sink（overlay 控制器持钟动画——菜单淡入/高亮
// 滑移等）。每 tick 调用；返回 true = 仍有活动动画（继续请求动画帧）。
// sink 内自备 shell 引用（markDirty 触发 overlay builder 重采样）。
using AnimateSink = std::function<bool(std::uint64_t nowMs)>;

// 应用壳配置：build + 应用级钩子（平台无关）。
struct ShellConfig {
    // UI 模板构建：每次重建调用（读当前应用状态返回新 Widget 树）。
    // 不允许接触平台窗口或 Renderer。
    std::function<core::Widget()> build{};
    // 键拦截（Escape/平台返回键的统一规则等）；返回 true = 已消费，
    // 不再进入 InteractionController。
    std::function<bool(class AppShell&, core::Key, core::KeyModifiers, char)>
        onKey{};
    // 滚轮 sink（列表滚动）；为空时滚轮事件被忽略。
    WheelSink onWheel{};
    // M10：视口拖动滚动 sink（触摸/指针拖动 → ScrollController 拖动与
    // 惯性）；为空时拖动不路由滚动（文本选区拖动不受影响）。
    ScrollDragSink onScrollDrag{};
    // 重建后钩子（modal 焦点规则等）：新树已落地，root()/focus() 可查。
    std::function<void(class AppShell&)> onRebuilt{};
    // 关闭请求（窗口 X / WindowCloseRequested）：返回 true = 已消费
    //（modal/路由返回），false = 退出应用。
    std::function<bool(class AppShell&)> onCloseRequested{};
    // 初始视口（首帧前由宿主指标覆盖）。
    core::Size initialView{800.0F, 600.0F};
    // caret 闪烁（输入焦点期间）；关闭时 caret 恒不透明（settings 型应用
    // 的确定性输出）。
    bool caretBlink{true};
    // M10：动画帧回调（惯性滚动等应用侧时间驱动状态）。每次 tick 调用；
    // 返回 true = 仍有活动动画（继续请求动画帧）。UI 线程独占。
    std::function<bool(class AppShell&, std::uint64_t nowMs)> onAnimate{};
    // M10：状态色过渡（hover/pressed/focused/checked 颜色按
    // stateTransitionMs 插值）。时间源为 tick 注入时钟：不经 tick 推进的
    // 直驱测试保持即时终态（既有确定性输出不受影响）。
    bool motionTransitions{false};
};

class AppShell {
  public:
    explicit AppShell(ShellConfig config);

    AppShell(const AppShell&) = delete;
    AppShell& operator=(const AppShell&) = delete;
    AppShell(AppShell&&) = delete;
    AppShell& operator=(AppShell&&) = delete;

    // --- 状态/服务门面（build 与业务 handler 经此读写） ---
    [[nodiscard]] core::StateStore& state() { return state_; }
    [[nodiscard]] const core::StateStore& state() const { return state_; }
    [[nodiscard]] core::HandlerRegistry& handlers() { return handlers_; }
    [[nodiscard]] core::FocusManager& focus() { return focus_; }
    [[nodiscard]] const core::FocusManager& focus() const { return focus_; }
    [[nodiscard]] core::InteractionController& controller() {
        return controller_;
    }
    [[nodiscard]] const core::InteractionController& controller() const {
        return controller_;
    }
    // 当前指针期望光标（分隔条 ResizeEW/NS，splitter-design §7）：宿主
    // 适配层（runApp）映射 SystemCursor 并调 ApplicationHost::setCursor。
    [[nodiscard]] core::PointerCursor pointerCursor() const {
        return controller_.pointerCursor();
    }
    [[nodiscard]] style::Theme& theme() { return theme_; }
    [[nodiscard]] const style::Theme& theme() const { return theme_; }
    // 主题切换：resolved style 变化 → 重建（可选全量重绘）。
    void setTheme(style::Theme theme, bool forceFullRepaint = true);
    // 可访问性设置变化 → 派生 Theme（font scale/high contrast/reduce
    // animation/density，visual-system §4）。显式覆盖全部三项，保持旧 API
    // 语义；仅覆盖某一项或恢复跟随时使用 setAccessibilityOverrides。
    void setAccessibilitySettings(
        accessibility::AccessibilitySettings settings,
        std::optional<bool> darkMode = std::nullopt);
    void setAccessibilityOverrides(accessibility::AccessibilityOverrides overrides);
    [[nodiscard]] const accessibility::AccessibilityOverrides&
    accessibilityOverrides() const { return accessibilityOverrides_; }
    // 宿主输入（UI 线程）；只更新未被应用显式覆盖的字段。
    void setSystemAccessibilitySettings(accessibility::AccessibilitySettings settings);
    [[nodiscard]] const accessibility::AccessibilitySettings&
    accessibilitySettings() const {
        return accessibility_;
    }
    // 显式请求重建（应用侧业务状态不在 StateStore 时）。
    void markDirty() { dirty_ = true; }
    // 待提交的内容变化独立于 animationsActive：一次性回调可以标脏后
    // 直接静止；提前 rebuild 也不代表新树已经绘制。由 runApp 合并请求。
    [[nodiscard]] bool hasPendingFrame() const {
        return dirty_ || rebuiltThisFrame_ || fullRepaintPending_ ||
               !framePainted_;
    }
    void setVisualPreviewState(std::string key, style::WidgetState state) {
        const auto found = previewStates_.find(key);
        if (found == previewStates_.end() || !(found->second == state)) {
            previewStates_.insert_or_assign(std::move(key), state);
            dirty_ = true;
        }
    }
    // 请求全量重绘（模态/路由整树切换等，局部 damage 不可靠时）。
    void requestFullRepaint() { fullRepaintPending_ = true; }
    // 关闭请求（窗口 X / WindowCloseRequested）：true = 已消费（modal/
    // 路由返回），false = 退出循环。等价于路由到 config.onCloseRequested。
    [[nodiscard]] bool requestClose() {
        return config_.onCloseRequested && config_.onCloseRequested(*this);
    }

    // --- 视口/渲染器/字体（宿主与后端装配） ---
    void setView(core::Size size);
    void setDeviceScale(float scale);
    // 透明窗口配套（WindowDesc.transparent，runApp 自动接）：CPU 路径
    // 清屏色改全透明——圆角外的像素不被不透明底填充。外部渲染器
    //（Skia/GPU）的透明 clear 由后端装配负责。
    void setClearColor(core::Color clear) {
        cpuRenderer_.setClearColor(clear);
    }
    // 外部渲染器（Skia 光栅/GPU/测试 renderer）；nullptr 恢复内部 CPU。
    // 后端不共享 framebuffer：切换即失效绘制缓存并全量重绘。
    void setRenderer(render::Renderer* renderer);
    // M1：正式字体事实注入（布局/绘制/命中测试/IME 查询共享）。
    void setFontManager(std::shared_ptr<const text::FontManager> fonts);

    // --- M5：语义桥（可观测回归证据） ---
    // 注册后在下一次绘制末尾构建语义树并推送 identity diff + 焦点变化
    //（RecordingAccessibilityBridge 可作跨平台回归断言）；nullptr 注销。
    void setAccessibilityBridge(accessibility::AccessibilityBridge* bridge);
    // 语义 action 分发（与键盘同路径）+ 结果回执到桥。
    [[nodiscard]] accessibility::SemanticsActionStatus
    performAccessibilityAction(const std::string& nodeId,
                                std::uint32_t action,
                                const std::string& value = {},
                                float scrollDeltaY = 0.0F);

    // --- M11：框架级 overlay（浮动菜单等模态层） ---
    // overlay 树独立布局（紧约束视口），与主树各自拥有 identity 命名空
    // 间（路径拼接，主树 identity 不变——焦点/语义/damage 稳定）。绘制
    // 命令后置叠加；overlay 活跃期命中/键盘换树（barrier 需全窗覆盖保
    // 证模态，同 makeDialog 模式）；语义作为根语义节点的附加子树。打开/
    // 关闭全量重绘；打开期间 overlay 子树独立 damage diff。
    // 已知限制：IME 候选框查询/惯性滚动仍走主树（菜单场景无文本字段）。
    void setOverlay(core::Widget overlay);
    // Re-evaluated after main-tree layout, so anchored menus follow resize/theme.
    // animate（M14）：overlay 控制器的时间驱动步进（菜单动效等）；随
    // clearOverlay 一并解除。
    void setOverlayBuilder(std::function<std::optional<core::Widget>()> builder,
                           WheelSink wheel = {}, ScrollDragSink drag = {},
                           AnimateSink animate = {});
    void clearOverlay();
    [[nodiscard]] bool hasOverlay() const {
        return overlayTemplate_.has_value();
    }
    [[nodiscard]] const core::RenderNode* overlayRoot() const {
        return overlayRoot_.has_value() ? &*overlayRoot_ : nullptr;
    }

    // --- 事件分发（runApp 调用；headless 测试可直接驱动） ---
    // modifiers 为按下时刻修饰键（集合行 Extended 选择语义）；button 为
    // 归一化主键（Secondary 经交互层只咨询 SecondaryPressSink，不产生
    // 点击/拖动——menu-controls-design §6.2；缺省 Primary 保持既有调用
    // 不变）。
    void pointerDown(core::Offset position,
                     core::KeyModifiers modifiers = core::kModifierNone,
                     core::PointerButton button = core::PointerButton::Primary);
    void pointerMove(core::Offset position);
    void pointerUp(core::Offset position,
                   core::PointerButton button = core::PointerButton::Primary);
    void pointerCancel();
    // 返回 sink 消费状态（M5 收口：语义滚动回执同源）。modifiers 参与
    // Shift+纵轮 → 水平视口投影（lumen-scroll-design §4；默认无修饰键）。
    [[nodiscard]] bool wheel(core::Offset position, core::Offset delta,
                             core::KeyModifiers modifiers =
                                 core::kModifierNone);
    // 自定义标题栏（lumen-titlebar-design §4.1）：逻辑点是否窗口拖拽区
    //（caption）。runApp 在 WindowDesc.customTitleBar 时注册给宿主
    // hit-test；判定走事件树命中链（overlay 活跃期 = 模态层不可拖），
    // 最深命中为交互控件（onClick 目标/输入控件）时不拖。
    [[nodiscard]] bool isWindowDragPoint(core::Offset position);
    void textInput(const std::string& text);
    void textEditing(const std::string& preedit);
    void cancelComposition();
    void keyDown(core::Key key,
                 core::KeyModifiers modifiers = core::kModifierNone,
                 char keyChar = 0);

    // --- 帧管线（plan §5.2：events -> state -> reconcile -> layout ->
    // paint；damage/绘制缓存同 counter 迁移前行为） ---
    void rebuildIfDirty();
    [[nodiscard]] std::uint64_t renderFrame(bool forceFullRepaint = false);
    // Runtime rendering without a framebuffer hash. renderFrame() preserves
    // the deterministic headless API and computes its hash only when requested.
    void paintFrame(bool forceFullRepaint = false);
    // 时间驱动状态（caret 闪烁/转场/应用动画 onAnimate）；测试传固定时间
    // 戳保持确定性。
    void tick(std::uint64_t nowMs);

    // --- M10：转场驱动（MotionTokens → transitionAlpha） ---
    // 转场只改绘制数据（整节点透明度），不改布局几何、hit test 与语义
    // 树；reduceAnimation（时长归零）时首拍即达终态。
    struct TransitionSpec {
        std::string key{};  // root_ 中按 key 定位子树（延迟解析：begin 后
                            // 的首次重建生效）
        float from{0.0F};
        float to{1.0F};
        double durationMs{150.0};
        core::Easing easing{core::Easing::EaseOut};
        // 完成回调（UI 线程，tick 内触发）；退出转场在此真正移除子树。
        std::function<void(class AppShell&)> onComplete{};
    };
    void beginTransition(TransitionSpec spec);
    // 便捷入口：Dialog 进/出场（dialogTransitionMs）与路由页过渡
    //（navigatorTransitionMs）；进场 EaseOut、退场 EaseIn。
    void beginDialogTransition(const std::string& key, bool entering,
                               std::function<void(class AppShell&)>
                                   onComplete = {});
    void beginRouteTransition(const std::string& key, bool entering,
                              std::function<void(class AppShell&)>
                                  onComplete = {});
    [[nodiscard]] bool hasActiveTransitions() const {
        return !transitions_.empty();
    }
    // 连续动画是否活跃（caret 闪烁/转场/状态过渡/tooltip 计时/onAnimate）；
    // runApp 据此驱动 FrameScheduler 动画帧。
    [[nodiscard]] bool animationsActive() const {
        return animationsActive_ || !transitions_.empty() ||
               !stateBlends_.empty() || transitionsPaintPending_;
    }
    [[nodiscard]] bool motionEnabled() const {
        return config_.motionTransitions && hasTicked_;
    }
    // M11 review：最早的离散动画唤醒时刻（Armed tooltip 延迟到期；
    // 时钟与 tick 同源）。runApp 注入 FrameScheduler 空闲定时唤醒，
    // 等待期不占用连续动画帧。
    [[nodiscard]] std::optional<std::uint64_t> animationWakeMs() const;

    // --- M11：Tooltip hover 延迟驱动 ---
    // 注册 anchor→tooltip 关联：hover 停留 tooltipDelayMs 后 tooltipKey
    // 子树淡入（tooltipFadeMs），离开淡出；键盘焦点同样触发。按压、
    // 滚动、取消及锚点移除会隐藏。绘制样式继承锚点的 ThemeScope；
    // 纯绘制副本在根后绘制（下方 8px、不足则翻转，窗口边距 8px），
    // 不受内容视口裁剪，不增加输入/语义树。隐藏节点 alpha 强制为 0。
    void registerTooltip(std::string anchorKey, std::string tooltipKey);

    // 热重载：替换 UI 模板（后续重建不再调用 config.build，直到再次
    // swapRoot）；状态/焦点/滚动保留。
    void swapRoot(core::Widget root);

    // --- 查询（测试/宿主/IME） ---
    [[nodiscard]] const core::RenderNode& root() const { return root_; }
    [[nodiscard]] style::Theme effectiveThemeForKey(const std::string& key) const;
    // IME 候选框锚点：光标逻辑矩形（与 painter 同一 TextLayout，M1）。
    [[nodiscard]] core::Rect focusedTextRect() const;
    // 光标 x 偏移（逻辑像素；供分离式 cursor 平台的宿主适配）。
    [[nodiscard]] int focusedCaretOffset() const;
    [[nodiscard]] bool wantsTextInput() const {
        return controller_.wantsTextInput();
    }
    // 内部 CPU 渲染器帧缓冲（呈现路径消费；外部渲染器时无意义）。
    [[nodiscard]] const render::PixelBuffer& pixels() const {
        return cpuRenderer_.pixels();
    }
    [[nodiscard]] render::RenderStats stats() {
        return activeRenderer().stats();
    }
    [[nodiscard]] render::RendererCapabilities capabilities() {
        return activeRenderer().capabilities();
    }
    [[nodiscard]] std::uint32_t partialRepaintCount() const {
        return partialRepaintCount_;
    }
    [[nodiscard]] core::Size view() const { return view_; }
    [[nodiscard]] style::StyleContext styleContext() const {
        return style::StyleContext{theme_, interactionSnapshot_,
                                   accessibility_, deviceScale_, &previewStates_};
    }

  private:
    [[nodiscard]] render::Renderer& activeRenderer() {
        return externalRenderer_ != nullptr ? *externalRenderer_
                                            : cpuRenderer_;
    }
    // M11：overlay 活跃期的事件树（命中/键盘换树）。
    [[nodiscard]] const core::RenderNode& eventTree() const {
        return overlayRoot_.has_value() ? *overlayRoot_ : root_;
    }
    void syncInteractionSnapshot();
    [[nodiscard]] const text::FontManager& textFontSource() const;
    [[nodiscard]] const core::RenderNode* findFocusedField(
        core::Offset& origin) const;
    void addNodeRect(std::vector<core::Rect>& damage,
                     const std::string& identity, const std::string& key);
    void syncSubscriptions(const std::set<std::string>& keys);
    // M5：绘制后语义推送（树构建 + diff + 焦点；仅注册了桥时执行）。
    void pushSemantics();
    // M10：转场推进（tick 内采样；完成项触发 onComplete）与应用
    //（renderFrame 重建后的树上写入 alpha；damage 汇入调用方）。
    struct ActiveTransition {
        std::string key{};
        std::string identity{};  // 首次应用时解析（begin 后可能尚未重建）
        core::Tween tween{};
        double baseDurationMs{0.0};
        float sampledAlpha{0.0F};
        float lastPaintedAlpha{0.0F};
        std::uint64_t startMs{0};
        bool paintedOnce{false};
        bool finished{false};
        bool retire{false};  // 终值已应用到绘制帧，下一 tick 才可清除
        // 完成回调已触发（显式标志，不依赖 moved-from 的 function 为空；
        // 后者是工具链相关假设，AppleClang 下曾出现重复触发与永不退休）。
        bool completionNotified{false};
        std::function<void(AppShell&)> onComplete{};
    };
    bool advanceTransitions(std::uint64_t nowMs);
    bool applyTransitions(std::vector<core::Rect>& damage);
    // M11：tooltip hover 状态机（tick 内推进）与 Hidden 态 alpha 写 0
    //（renderFrame 重建后的树上；damage 汇入调用方）。
    struct TooltipRegistration {
        std::string anchorKey{};
        std::string tooltipKey{};
        enum class Phase : std::uint8_t { Hidden, Armed, Visible, Fading };
        Phase phase{Phase::Hidden};
        std::uint64_t armedAtMs{0};
        bool suppressed{false};
    };
    bool advanceTooltips(std::uint64_t nowMs);
    bool applyTooltipVisibility(std::vector<core::Rect>& damage);
    bool hasTransitionForKey(const std::string& key) const;
    void dismissTooltips();
    bool updateTooltipPaintNodes(std::vector<core::Rect>& damage);
    std::vector<TooltipRegistration> tooltips_{};
    std::vector<core::RenderNode> tooltipPaintNodes_{};
    std::vector<std::string> tooltipSourceIdentities_{};
    std::map<std::string, core::RenderNode> tooltipTemplates_{};
    void rebuildTooltipTemplates();
    // M10：状态色过渡（交互快照变化时捕获旧样式，逐帧向新样式插值）。
    void retargetStateBlends(const core::RenderNode& fresh);
    std::map<const core::Element*, style::Theme> scopeThemes_{};
    bool applyStateBlend(std::vector<core::Rect>& damage);
    std::vector<ActiveTransition> transitions_;
    bool transitionsPaintPending_{false};
    bool animationsActive_{false};
    bool hasTicked_{false};
    struct StateBlend {
        core::ResolvedStyle from;
        core::ResolvedStyle to;
        std::uint64_t startMs;
    };
    std::map<std::string, StateBlend> stateBlends_{};
    bool suppressStateBlend_{false};

    ShellConfig config_{};
    core::StateStore state_{};
    core::HandlerRegistry handlers_{};
    std::map<std::string, core::StateStore::ObserverId> subscriptions_{};
    core::FocusManager focus_{};
    // M1：正式字体事实（声明在 controller_ 之前，析构原序保证原始指针
    // 不悬空）。
    std::shared_ptr<const text::FontManager> textFonts_{};
    core::InteractionController controller_{state_, handlers_, focus_};
    style::Theme theme_{style::Theme::dark()};
    accessibility::AccessibilitySettings accessibility_{};
    accessibility::AccessibilitySettings systemAccessibility_{};
    accessibility::AccessibilityOverrides accessibilityOverrides_{};
    std::optional<core::Color> accessibilityAccent_{};
    void applyAccessibilitySettings(accessibility::AccessibilitySettings settings,
                                    std::optional<bool> darkMode, bool preserveAccent);
    void resolveAccessibilitySettings();
    style::InteractionStateSnapshot interactionSnapshot_{};
    std::map<std::string, style::WidgetState> previewStates_{};
    // 热重载模板覆盖（有值时优先于 config_.build）。
    std::optional<core::Widget> swapTemplate_{};
    std::optional<core::Element> element_{};
    core::RenderNode root_{};
    // M11：框架级 overlay（独立布局/独立 identity 命名空间）。
    std::optional<core::Widget> overlayTemplate_{};
    std::function<std::optional<core::Widget>()> overlayBuilder_{};
    WheelSink overlayWheel_{};
    // 本次 wheel 分发中 controller 是否已把滚轮转发给 overlayWheel_
    //（无滚动视口时 controller 不调 sink，AppShell::wheel 以空 hit 直调
    // 一次兜底——见 wheel 实现）。
    bool overlayWheelForwarded_{false};
    ScrollDragSink overlayDrag_{};
    AnimateSink overlayAnimate_{};
    std::optional<core::RenderNode> overlayRoot_{};
    core::RenderNode previousOverlayRoot_{};
    bool hasPreviousOverlayRoot_{false};
    // Damage/paint-cache bookkeeping（plan 阶段6）。
    bool hasRoot_{false};
    std::vector<core::Rect> pendingDamage_{};
    bool treeDamageValid_{true};
    bool rebuiltThisFrame_{false};
    bool framePainted_{false};
    bool fullRepaintPending_{false};
    // 交互快照（hover/press/focus）驱动重建。
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
    bool frameHashValid_{false};
    std::uint32_t partialRepaintCount_{0};
    core::Size view_{800.0F, 600.0F};
    float deviceScale_{1.0F};
    std::uint64_t frameIndex_{0};
    std::uint64_t lastTickMs_{0};
    render::CpuRenderer cpuRenderer_{1.0F};
    render::Renderer* externalRenderer_{nullptr};
    // M5：语义桥（外部拥有）与上次推送树/焦点。
    accessibility::AccessibilityBridge* accessibilityBridge_{nullptr};
    std::optional<accessibility::SemanticsTree> lastSemantics_{};
    std::string lastSemanticFocus_{};
    bool semanticsNeedsPush_{false};
    bool dirty_{true};
};

// --- runApp：UI 线程主循环（M2） ---

// 渲染器装配（窗口就绪后由 factory 返回；全部字段可空）。
struct RendererSetup {
    // null = 应用壳内部 CPU 渲染器。
    render::Renderer* renderer{nullptr};
    // 帧提交后的呈现回调（CPU/Skia 光栅 → 宿主窗口）；空 = 后端自行
    // 交换（GPU）或宿主无窗口（Fake host）。
    std::function<bool()> present{};
    // DPI 变化同步（外部渲染器的 setDeviceScale 等）。
    std::function<void(float deviceScale)> syncDeviceScale{};
    // 帧提交后探活；true = 失效（GPU 上下文丢失），触发回退策略。
    std::function<bool()> failed{};
};

struct RunOptions {
    // 窗口描述（标题/尺寸/OpenGL 标志；Fake host 忽略大部分字段）。
    platform::WindowDesc windowDesc{};
    std::uint64_t maxFrames{0};  // 窗口 smoke/测量；0 = 无限
    bool diagnostics{false};
    // 事件循环空闲等待上限（热重载轮询粒度）。
    std::uint32_t idleWaitMs{250};
    // 渲染器装配（窗口创建后调用一次；空 = 内部 CPU。id 以引用传出，
    // 装配期可重建窗口——例如 GPU 初始化失败换软件窗口）。
    std::function<RendererSetup(platform::ApplicationHost&,
                                core::WindowId&)>
        rendererFactory{};
    // 渲染器失效后的替换（GPU→CPU 回退；可销毁并重建窗口，id 以引用
    // 传出）；返回 nullopt = 退出码 1。
    std::function<std::optional<RendererSetup>(platform::ApplicationHost&,
                                               core::WindowId&)>
        onRendererFailure{};
    // 字体工厂（Skia 后端时返回 SkiaFontManager；返回空保持占位）。
    std::function<std::shared_ptr<text::FontManager>()> fontFactory{};
    // 窗口图标 provider（渲染器装配后、首帧前调用一次；返回 width<=0
    // 跳过，宿主 windowIcon 能力缺失时不调用。setWindowIcon 失败时诊断
    // 降级，不阻塞启动——exe/桌面图标资源由打包层提供，此处只覆盖
    // 运行时窗口/任务栏位）。
    std::function<platform::WindowIcon()> windowIcon{};
    // 扩展轮询（热重载等；返回 true = 请求重绘）。
    std::function<bool(AppShell&, std::uint64_t nowMs)> poll{};
    // M4：宿主服务事件转发（FileDialogCompleted 等应用壳不消费的
    // 事件类型）；事件泵内同步调用，UI 线程独占。
    std::function<void(AppShell&, const core::HostEvent&)> onEvent{};
    // M13：尝试装配原生无障碍桥（LUMEN_ENABLE_ACCESSIBILITY_BRIDGE 编入
    // 时生效；未编入平台实现安全降级为 nullptr + 诊断）。语义 action 与
    // 键盘/指针同路径回灌 performAccessibilityAction。
    bool nativeAccessibility{true};
    // 默认在首帧前与 SystemAccessibilityChanged 时更新系统偏好；应用
    // 显式覆盖优先。false 仅关闭该窗口的自动跟随，不影响原生语义桥。
    bool followSystemAccessibility{true};
};

// 一个宿主窗口与其应用壳的绑定。每个窗口拥有独立的 RunOptions，因而
// renderer、字体、IME、无障碍桥和帧调度都不会跨窗口共享状态。AppShell
// 仍由调用方拥有，并且必须在 runApp 返回前保持有效。
struct AppWindow {
    AppShell* shell{nullptr};
    RunOptions options{};
};

// 多窗口应用入口。事件按 HostEvent.window 路由到对应 AppWindow；Quit
// 事件结束整个应用，单窗口关闭只停止该窗口的运行时，最后一个窗口
// 关闭才结束主循环；窗口和宿主生命周期仍由调用方拥有。传入空集合或
// 包含空 shell 时返回 1。
int runApp(std::vector<AppWindow> windows, platform::ApplicationHost& host);

// 阻塞运行应用直到关闭请求/maxFrames；返回进程退出码。
// host 生命周期由调用方拥有；Fake host 可用于 headless 冒烟。
// 兼容的单窗口入口转发到多窗口实现。
int runApp(AppShell& shell, platform::ApplicationHost& host,
           RunOptions options = {});

}  // namespace lumen::app
