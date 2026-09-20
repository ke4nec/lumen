# Lumen StatusBar 控件设计（状态栏）

> 文档状态：已实施（2026-09，P1+P2 全量；实施状态与实现差异见文末 §15）
> 输入：源码现状盘点（`include/lumen/core/widget.h`（WidgetType::ProgressBar 既有"bind/value 0..100 无交互"、WidgetType::Tooltip 常驻树透明度切换先例、makeProgressBar/makeText）、`include/lumen/style/theme.h`（MotionTokens：tooltipFadeMs/stateTransitionMs；Metrics 密度档））、`docs/lumen-titlebar-design.md`（无边框窗口 8 逻辑 px resize 边 + 四角 12px、48px chrome 行先例）、`docs/lumen-menu-controls-design.md`（M14 overlay animate sink tick 口径）、`docs/lumen-toolbar-design.md`（同批 chrome 件，toggle 联动场景）、`docs/lumen-visual-system-design.md`（token 三层模型/尺度表 §3.2/状态规则 §5）。
> 配套视觉设计稿：`design/statusbar.html`。
> 定位：桌面自用版控件库增强；工具类应用窗口底部的信息 chrome：上下文消息、光标位置、编码/模式等常驻项、后台任务进度与 busy 指示、resize grip。与标题栏/菜单栏/工具栏同族（窗口 chrome），遵循"Widget 不可变声明 + 应用侧控制器 + 既有控件组合"架构，**零新增 WidgetType**；唯一 core 缝隙是 ProgressBar 的 indeterminate 能力（§5.3）。

---

## 1. 背景与问题

工具类桌面应用的窗口底部惯例是**状态栏**：编辑器显示行列/编码/语言，IDE 显示构建进度，文件管理器显示选中计数。源码核对后当前缺口：

| 能力 | 现状 | 缺口 |
| --- | --- | --- |
| Text/Row | 文本与行组合齐备 | 无栏级组合：左右分区、项分隔、溢出省略规则 |
| ProgressBar | determinate（0..100）无交互 | **无 indeterminate**（时长未知的后台任务）；无状态栏内嵌尺寸档 |
| Tooltip | 透明度切换动效先例（常驻树） | 无消息区复用场景 |
| 窗口 chrome | 自定义标题栏（titlebar 设计）+ resize 8px 边 | 无底部 grip 视觉（borderless 窗口右下角 resize 的可见性提示） |
| 动效 | M10/M11/M14 token 与 tick 基建 | 无消息切换/indeterminate/busy 的动效契约 |

结论：状态栏是**信息 chrome 组合层**——一个 widgets 层控制器（消息区 + 项序列 + 进度/busy 状态）+ Text/ProgressBar 组合件；indeterminate 是 ProgressBar 控件的既有能力补齐，不是状态栏私有绘制。

## 2. 参考框架调研

| 框架 | 状态栏 | 结构 | 进度/busy | 对 Lumen 的启示 |
| --- | --- | --- | --- | --- |
| Qt 5/6 | `QStatusBar` | `showMessage(text, timeout)` 瞬态消息 + `addWidget`（左）/`addPermanentWidget`（右）分区 | `QProgressBar` 内嵌（`setStyleSheet` 缩小）；busy 无内建 | **左右分区 + 瞬态消息 API** 是最小完备集；permanent 区不受消息挤占 |
| GTK4 | `GtkStatusbar` | 上下文栈 push/pop（`getContext`） | 无内建 | 消息栈对多面板应用有用；自用阶段分区即够 |
| Win32 | 状态栏 common control | 分格（parts）+ `SB_SIMPLE` 模式（单格消息） | 分格内自绘 marquee（`PBS_MARQUEE` 属 ProgressBar） | marquee 归属进度条控件，不归状态栏 |
| VS Code | 底部状态栏 | 左（分支/问题/警告）右（行列/编码/缩放）；项可点击 | 后台任务 spinner + 进度条内嵌 | **项可交互**（点击跳转/弹菜单）是现代惯例；spinner 是等宽任务指示 |
| macOS | `NSStatusBar`（菜单栏额外项，非窗口底栏） | — | — | 命名冲突警示：本稿"状态栏"恒指窗口底栏 |

**采纳的共同事实**：

1. **左右分区**：左侧弹性消息区（瞬态/上下文，超长省略号），右侧常驻项序列（从右往左声明，固定内容不被消息挤占）。
2. **瞬态消息有生命周期**：`setMessage(text, timeout)`——超时自动清空回常驻内容；timeout=0 常驻。消息切换是**内容级动效点**（淡切，不做位移）。
3. **进度与 busy 归进度条控件**：determinate 内嵌 + indeterminate（marquee/往返段）；busy spinner 是独立的小尺寸等价物（12px 旋转弧）。
4. **状态栏不是操作控件**：默认全部项不可聚焦、不进 Tab 遍历；可点击项（VS Code 惯例）是例外且行为归应用。
5. **信息密度低于正文**：高度小于控件档、文本用 secondary 色、12px 字号——chrome 的视觉降级语言。

**采纳的 Lumen 本土事实**（决定不做的事）：

1. 栏 = 普通 Widget 子树（Row + Text + ProgressBar），零新增 WidgetType；不建 GTK 式消息栈（分区 + 瞬态已覆盖自用场景）。
2. indeterminate 进度给 `ProgressBar` 控件（`Widget` packed bool + 循环 token），不在状态栏 painter 私有绘制——菜单/对话框等其他宿主将来直接复用。
3. resize grip 是**纯视觉件**：命中归平台 8 逻辑 px resize 边（titlebar §4.2 已定），grip 不设命中区、不进语义树。
4. 消息 live region（屏幕阅读器自动播报）：SemanticsTree 暂无 live 通道，首版以 Text role + value diff 表达，UIA LiveSetting 映射留 §14。

## 3. 设计目标与非目标

**目标**

1. widgets 层 `StatusBarController`：左侧消息区（瞬态/常驻）+ 右侧项序列（text/icon+text/进度/busy）+ 生命周期回调。
2. `makeStatusBar(controller)` 组合件：单行布局、左右分区、项间分隔、resize grip（可选），零新增 WidgetType。
3. ProgressBar 补 indeterminate 能力（packed bool + 往返段动效 token）——唯一 core 缝隙。
4. 动效三件套：消息淡切（120ms）、indeterminate 往返（1400ms/循环）、busy 旋转（1200ms/圈），全部 reduceAnimation 可关。
5. 全部新路径 headless 可测；既有帧哈希不变（ProgressBar 无 indeterminate 声明时绘制与现状逐像素一致）。

**非目标（第一版明确不做）**

- GTK 式消息上下文栈（push/pop）——分区 + 瞬态足够。
- 多行/可展开状态栏（VS Code 无、桌面工具罕见）。
- 项的框架级行为协议（点击弹菜单等由应用经 onClick 自建；控件只提供可点击项的视觉/hover）。
- 自动隐藏状态栏。
- macOS 菜单栏 `NSStatusBar`（菜单栏额外区，另一能力域）。

## 4. 总体架构

```text
┌───────────────────────────────────────────────────────────┐
│ 应用层：makeStatusBar(key, &controller)                     │
│   setMessage/setProgress/setBusy → StateStore → 请求重建    │
├───────────────────────────────────────────────────────────┤
│ widgets 层（新文件 include/lumen/widgets/statusbar.h）      │
│   StatusBarItem（值类型模型）                                │
│   StatusBarController（消息区 + 项序列 + 瞬态计时 + 进度）    │
├───────────────────────────────────────────────────────────┤
│ core 层（唯一缝隙）：WidgetType::ProgressBar 增加            │
│   packed bool indeterminate（默认 false，既有绘制不变）      │
├───────────────────────────────────────────────────────────┤
│ 交互层（零改动）：可点击项 onClick 既有路径；无焦点/无键盘    │
└───────────────────────────────────────────────────────────┘
```

核心架构决策：**状态栏是信息汇聚的 Row 子树，控制器只管内容与节奏**。消息淡切复用 Tooltip 的"常驻树 + 透明度切换"通道（主树节点 alpha 动画先例）；indeterminate/busy 的逐 tick 重绘经 FrameScheduler（caret blink 同驱动源）。

## 5. 项模型与控制器（widgets 层）

### 5.1 StatusBarItem 与控制器

```cpp
// include/lumen/widgets/statusbar.h（新）
namespace lumen::widgets {

enum class StatusItemKind {
    Text,       // 文本项（可带 icon 前缀）
    Progress,   // determinate 进度项（内嵌 ProgressBar）
    Busy,       // busy 旋转弧项（12px，等价 indeterminate 的紧凑形态）
    Toggle,     // 可点击项（hover 反馈 + onClick；行为归应用）
    Separator,  // 项间分隔线（不可交互，不进语义树）
};

struct StatusBarItem {
    std::string id{};          // 行 key = "sb:item:<id>"
    StatusItemKind kind{StatusItemKind::Text};
    core::IconId icon{core::IconId::None};
    std::string text{};
    bool enabled{true};        // false = disabled.content 色 + 命中拒绝
    float fixedWidth{0.0F};    // 0 = 内容自适应；>0 = 固定宽（对齐用）
};

}  // namespace lumen::widgets

class StatusBarController {
  public:
    // 左侧消息区。timeoutMs > 0 = 瞬态（超时清空回 idleText）；
    // 0 = 常驻（idleText 本体）。瞬态计时经 FrameScheduler tick。
    void setMessage(std::string text, std::uint32_t timeoutMs = 0);
    void setIdleMessage(std::string text);      // 常驻底消息（如"就绪"）
    [[nodiscard]] const std::string& message() const;

    // 右侧项序列（从左往右声明；渲染时右对齐于 grip 左缘）。
    void setItems(std::vector<StatusBarItem> items);

    // 进度：0..100（determinate）；<0 = indeterminate（进度项转 marquee）。
    void setProgress(float percent);
    // busy 开关（busy 项显示/隐藏；与进度项独立——"正在索引…"可无百分比）。
    void setBusy(bool busy);

    // 栏 Widget（应用 build 每帧调用）。
    [[nodiscard]] core::Widget build(const style::Theme& theme) const;

    // resize grip 显示条件（默认 = customTitleBar && !maximized，应用可强开）。
    void setShowResizeGrip(bool show);

    // 可点击项回调（Toggle 项；id 为 StatusBarItem.id）。
    std::function<void(const std::string& id)> onItemClicked{};
};
```

### 5.2 布局规则

```text
Row(高 = statusbar.height，底部无分隔线——顶部 1px border 承担)
 ├─ 消息区（弹性，min 0）：text 溢出省略号；瞬态/常驻共用
 ├─ [弹性 spacer]
 └─ 项序列（内容自适应，右对齐）: item | 1px 分隔 | item | ... | grip?
```

- 消息区与项序列之间至少 16px 呼吸；项与项之间 1px×12px 垂直分隔线 + 两侧 8px gap（工具栏分隔线转置同口径）。
- **项序列不与消息互相挤压**：窗口变窄时消息区先收缩到省略号；再窄则**项序列从左往右折叠**（超出项整项隐藏，消息区保底 120px）——状态栏无 overflow 面板（信息项折叠无交互代价，与工具栏溢出语义不同）。
- grip 12×12 贴右下角（项序列右缘为它让位）；`maximized` 或非 customTitleBar 时 grip 消失（条件见 §5.1）。

### 5.3 ProgressBar indeterminate（core 缝隙）

- `Widget` 增加 packed bool `progressIndeterminate`（默认 false）：true 时忽略 value，绘制往返滑动段——16px 宽、轨道全宽 0.6 透明度 accent 底（determinate 轨道同源）。
- `makeProgressBar` 增加可选参数 `bool indeterminate = false`；`.lumen` DSL 不加节点属性（与 value 同理经 bind/属性时再评估）。
- 体积：packed 区，`sizeof(Widget)` 零增长（静态断言不动）。
- 动效 token：`progressIndeterminateCycleMs{1400}`（一个往返周期，linear）；reduceAnimation → 静止于中段（保留 40% 宽度半透明带，"进行中"形状语义仍在——不靠运动传达唯一信息，§11 口径）。

## 6. 交互契约

状态栏整体**不进 Tab 遍历、不获键盘焦点**（信息 chrome；桌面惯例）。唯一交互面：

| 动作 | 行为 |
| --- | --- |
| hover Toggle 项 | 背景 hover 派生 + 文本 primary（状态过渡 100ms） |
| 单击 Toggle 项 | `onItemClicked(id)`（行为完全归应用：跳转/弹菜单/切模式） |
| hover 其他项/消息区 | 无反馈（PointingHand 不出现——非交互件） |
| grip 区域 | 视觉件；命中归平台 8px resize 边（titlebar §4.2），无指针反馈 |

- disabled 项：`disabled.content` 色 + 命中拒绝（语义同步）。
- 瞬态消息期间新的 `setMessage` 取代旧消息（不排队）；timeout 清空回 `idleMessage`。

## 7. 语义契约

| 节点 | role | 状态/动作 |
| --- | --- | --- |
| 状态栏容器 | `statusbar`（新 role，枚举尾部追加，MenuItem/Splitter 先例） | label = 应用 semanticsLabel（如"状态栏"） |
| 消息区 | 既有 Text role | name = 当前消息文本（value diff 即变化信号；live region 见 §14.3） |
| Text/Progress/Busy 项 | Text / ProgressBar（既有） | busy 项 = 既有 ProgressBar role + value="busy"（或 Text"进行中"——实施时定，见 §14.2） |
| Toggle 项 | 既有 Button role | label = 项文本；Activate ≡ 单击（RecordingBridge 回执） |
| Separator / grip | 不进语义树 | 纯视觉 |

一致性断言：Toggle 单击 ≡ 语义 Activate 同回调；disabled 项两路一致拒绝；消息文本与控制器状态同步（重建后不残留旧消息）。

## 8. Widget / DSL 扩展与体积预算

| 项 | 增量 |
| --- | --- |
| `WidgetType` | **零新增** |
| `Widget` 字段 | packed bool `progressIndeterminate`（ProgressBar 专用；**零字节**，packed 区） |
| RenderCommand / 序列化版本 | **零改动**（往返段 = 既有圆角矩形命令逐 tick 位移） |
| `SemanticsRole` | 尾部追加 `StatusBar`（既有 role 数值不变） |
| `.lumen` DSL | 不加节点 |
| C++ DSL | `makeStatusBar(const StatusBarController*, std::string key = {})` |

## 9. 视觉规格（详见 design/statusbar.html）

### 9.1 尺度

| 项目 | Small/Compact | Medium/Comfortable | Large/Touch | 依据 |
| --- | ---: | ---: | ---: | --- |
| 栏高（border-box） | 24px | 28px | 32px | 信息 chrome 降一档（标题栏 48/控件 32-48 之间的薄条；4px 网格） |
| 文本字号 | 12px | 12px | 13px | 正文 14 降一级；fontScale 随动 |
| 栏水平内边距 | 8px | 12px | 16px | §3.2 水平内边距行 |
| 消息区/项序列最小呼吸 | 16px | 16px | 16px | 4px 网格 ×4 |
| 项间分隔线 | 1px × 12px 垂直，上下居中 | 同 | 同 | 工具栏分隔线转置同口径 |
| 分隔线两侧 gap | 8px | 8px | 8px | 同上 |
| 进度项 | 宽 120 × 高 4 | 同 | 宽 144 × 高 4 | §3.2 TextField 最小宽的缩档；高 = 滚动条可视滑块 8 的降档 |
| busy 弧 | 12px | 12px | 14px | 与文本同高族的紧凑件；3/4 圆弧 stroke 2 |
| resize grip | 12×12，3 条 45° 短线 | 同 | 同 | 平台 resize 角区 12×12（titlebar §4.2）对齐 |

### 9.2 组件 token（三层模型 §3.1 的 component 层）

```text
statusbar.background         = color.background.surface
statusbar.border             = color.border.default（顶部 1px；与 MenuBar/Toolbar 底线同语言）
statusbar.content            = color.content.secondary（默认项与消息）
statusbar.content.idle       = color.content.secondary（idle 消息同色——消息不抢焦点）
statusbar.content.emphasis   = color.content.primary（Toggle hover；瞬态消息不强调）
statusbar.content.disabled   = color.disabled.content
statusbar.item.background.hover = surface 层 hover 派生（Toggle 项；工具栏项同源）
statusbar.item.radius        = controlRadius（Toggle hover 底圆角；S/M/L = 4/6/8）
statusbar.separator          = color.border.default
statusbar.progress.track     = surface 上的既有进度轨道派生（0.6 透明 accent 底 indeterminate 段）
statusbar.progress.fill      = color.accent（determinate 填充）
statusbar.busy.arc           = color.accent（弧线）；reduceAnimation 静止态 0.5 透明
statusbar.grip.line          = color.border.strong（3 条 45° 线，1px）
motion.statusbar.messageFadeMs        = 120（消息切换淡入淡出，对齐 tooltipFadeMs）
motion.statusbar.messageTimeoutMs     = 4000（瞬态消息默认驻留；0 = 常驻）
motion.progress.indeterminateCycleMs  = 1400（往返一周期，linear；ProgressBar 控件 token）
motion.statusbar.busyCycleMs          = 1200（busy 弧一圈，linear）
```

### 9.3 状态矩阵

| 状态组合 | 背景 | 文本/图形 | 说明 |
| --- | --- | --- | --- |
| 常驻（idle 消息 + 项） | surface | `content.secondary` | 信息降级语言，不与内容争焦点 |
| 瞬态消息活跃 | 不变 | 消息文本 `content.secondary`（不强调——瞬态≠警告） | 超时淡出回 idle |
| Toggle hover | hover 派生 | `content.primary` | 状态过渡 100ms |
| Toggle pressed | accent 0.32 混合 | `content.primary` | — |
| disabled 项 | 不变 | `disabled.content` | 命中拒绝 + 语义一致 |
| 进度 determinate | 轨道 + accent 填充 | — | 值变化**不补间**（数据即状态，ProgressBar 既有口径） |
| 进度 indeterminate | 0.6 透明 accent 轨道 + 16px 往返段 | — | reduceAnimation 静止中段带 |
| busy | 不变 | accent 3/4 弧旋转 | reduceAnimation 弧静止 0.5 透明 |
| grip | 不变 | `border.strong` 斜纹 | 最大化时消失 |
| 高对比主题 | 边线/分隔线对比提升 | 文本 primary 化 | indeterminate/busy 静止态必须仍有"进行中"形状（不得只靠运动） |

## 10. 动效规格

| 动效 | 时长/曲线 | 驱动 | reduceAnimation |
| --- | --- | --- | --- |
| **消息切换** | 淡出 120ms → 内容替换 → 淡入 120ms（`statusbar.messageFadeMs`，无位移——消息是信息不是转场） | 主树节点透明度（Tooltip 常驻树切换同通道）；FrameScheduler tick | 即时切换 |
| **瞬态超时清空** | 驻留 `messageTimeoutMs`=4000ms → 淡出 120ms 回 idle | 控制器计时（无 tick 空转：延迟期不武装动画帧，到期才驱动） | 驻留不变（内容节奏），淡出归零 |
| **indeterminate 往返段** | 1400ms/周期 linear（16px 段，左右往返） | ProgressBar 逐 tick 位移重建（caret blink 同驱动源） | 静止中段 40% 半透明带 |
| **busy 弧旋转** | 1200ms/圈 linear | 同上 | 弧静止 + 0.5 透明 |
| determinate 进度 | **无补间** | — | —（值即状态；避免连续更新时的追赶假象） |
| Toggle hover/pressed | `stateTransitionMs`=100ms | 既有状态过渡通道 | 归零 |

新增 MotionTokens 四字段（`theme.h` 尾部追加，既有值不动）：`statusbarMessageFadeMs{120}`、`statusbarMessageTimeoutMs{4000}`、`progressIndeterminateCycleMs{1400}`、`statusbarBusyCycleMs{1200}`。

**动效立场**：状态栏动效全部是"低幅度、可关闭、不位移"的信息节律——消息淡切、进度往返、busy 旋转都不改变布局（宽度固定：进度项定宽、busy 项定尺寸、消息区弹性），窗口 resize 与内容更新期间无布局抖动。

## 11. 性能与测试计划

### 11.1 性能口径

- indeterminate/busy 逐 tick 重绘区域 ≤ 144×4 / 12×12（局部 damage，M3 口径回归）；无动画帧空转（到期/循环 tick 才驱动——M12 deadline 教训）。
- 瞬态计时无 tick 常驻（延迟期纯时间戳比较，tooltipDelay 同口径）。
- 栏为小树（典型 ≤ 10 项），不进基准场景。

### 11.2 测试（Catch2，`*_tests.cpp`，行为命名）

1. **模型**：setMessage/setIdle/setItems/setProgress/setBusy 状态与 build 输出同步；瞬态取代不排队。
2. **瞬态生命周期**：4000ms 驻留后清空回 idle；timeout=0 常驻；新消息重置计时。
3. **布局**：左右分区与最小呼吸；消息省略号；窗口变窄项折叠顺序（从左往右整项隐藏、消息保底 120px）。
4. **Toggle**：hover/pressed 视觉状态；单击 → onItemClicked(id)；disabled 拒绝。
5. **ProgressBar indeterminate**：声明后忽略 value；往返段几何随 tick 周期采样（0/25/50/75% 相位）；默认 false 时与既有 determinate 绘制逐像素一致（回归基线）。
6. **busy**：显示/隐藏与 setBusy 同步；弧相位周期 1200ms 采样；reduceAnimation 静止态。
7. **语义**：statusbar role 树；消息文本 diff；Toggle Activate ≡ 单击；separator/grip 缺席。
8. **视觉派生**：density 三档（24/28/32）、fontScale、高对比（静止进行中形状保留）、reduceAnimation（全部动效停、内容即达）。
9. **回归**：既有全部测试与帧哈希不变（packed bool 默认 false 不触旧路径）。

### 11.3 示例与验收

- settings 新页 `StatusBar`：编辑器场景（行列/编码/缩放 Toggle 项）+ 消息按钮（触发瞬态）+ 进度模拟（determinate → indeterminate → 完成）+ busy。
- gallery Controls 分区样本 + 控件清单瓦片（Grid toggle 联动 busy 弧与消息、determinate 进度项、resize grip；`design/gallery.html` live samples）。
- headless 冒烟：消息/进度/busy 脚本输出；三桌面窗口 smoke 由 CI 承担。

## 12. 实施分期

| 阶段 | 内容 | 出口条件 |
| --- | --- | --- |
| P1 栏与消息 | StatusBarItem/StatusBarController + makeStatusBar（分区/项/分隔/grip）+ 瞬态消息淡切 + Toggle + 测试 1–4/7 | 消息生命周期正确；既有哈希不变 |
| P2 进度与 busy | ProgressBar indeterminate 缝隙 + 进度项 + busy 弧 + 动效 token + 测试 5/6/8 | 动效周期采样全绿；determinate 像素回归通过 |
| P3 按需评估 | 消息 live region（UIA LiveSetting）、busy 项语义定稿（§14.2）、项折叠动画 | — |

## 13. 兼容与迁移

1. `WidgetType::ProgressBar` 既有行为零改动（indeterminate 默认 false；determinate 绘制路径不动，帧哈希不变）。
2. `sb:` key 前缀与 `list:`/`menu:`/`tb:` 同命名空间规则。
3. `SemanticsRole` 尾部追加 `StatusBar`，既有 role 数值不变。
4. 消息淡切复用主树 alpha 通道（Tooltip 先例），不引入 overlay——状态栏不是瞬态面板。
5. 状态栏不含滚动语义（内容溢出走折叠/省略，不滚动）。

## 14. 开放问题（实施前需确认）

1. **项折叠顺序**：从左往右隐藏是"右侧项（行列/编码）优先保住"的直觉；VS Code 保左弃右。建议按"声明序从左隐藏"，settings 示例实测再定。
2. **busy 项语义**：ProgressBar role + value="busy" vs Text role"进行中"——UIA provider（M13 进行中）落地时按平台惯例定稿，首版取前者。
3. **消息 live region**：屏幕阅读器播报瞬态消息需要 SemanticsTree live 通道（`kSemanticsLive` flag + UIA LiveSetting 映射）——M13 provider 成形后按需补，不阻塞本控件。
4. **进度项定宽 120/144**：与 TextField 最小宽缩档对齐是取值理由；若编辑器场景需要更长（编译任务），应用可用 `fixedWidth` 覆盖，token 不再加档。
5. **grip 在非 customTitleBar 窗口**：系统边框自带 resize，grip 默认隐藏（§5.1）；应用强开（`setShowResizeGrip(true)`）时它仍是纯视觉件。

## 15. 实施状态（2026-09 追记）

P1（栏与消息）与 P2（进度与 busy）已实施：`include/lumen/widgets/statusbar.h`
+ `src/widgets/statusbar.cpp`（`StatusBarController`），settings「StatusBar」
页与 gallery「Controls」页接入。`tests/statusbar_tests.cpp` 11 用例 +
settings 集成冒烟全绿；全套 ctest（Debug 699 用例）通过，Release 体积断言
通过。

与设计稿的实现差异（同提交更新本文档）：

1. **indeterminate 相位通道**：往返段相位经 `Widget.scrollOffset` 复用
   （0..1 三角波，逐 tick 写入）——零新增几何字段；§5.3 的 packed bool
   `progressIndeterminate` 已加（声明），相位不复用 bind。determinate 默认
   路径绘制逐像素不变（`statusbar_determinate_ignores_phase` 回归锁定）。
2. **busy 弧**：`IconId::Busy`（3/4 圆弧，24 段折线逼近）+
   `Widget.iconRotation`（弧度；painter 对归一化折线绕盒中心旋转后绘制，
   命令层与三后端零改动）——§10 的"逐 tick 重绘重建"落为图标旋转。
3. **消息淡切**：控制器自持相位机（Steady/FadingOut/FadingIn，M14
   "pending → 首 tick 起表"口径）+ 节点 `transitionAlpha`——不经 tooltip
   副本通道；reduceAnimation/无 tick 直驱 = 即时切换（已测试锁定）。
4. **grip**：`IconId::Grip`（3 条 45° 短线，16 栅格归一）；显隐由应用
   `setShowResizeGrip`（customTitleBar && !maximized 时开启）。
5. **语义**：`SemanticsRole::StatusBar`/`SpinButton`/`Toolbar` 已加（尾部
   追加；UIA StatusBar/Spinner/ToolBar 映射已接）；live region 留 M13 后
   （§14.3 不变）。
6. **MotionTokens**：`statusbarMessageFadeMs{120}`/`statusbarMessageTimeoutMs{4000}`/
   `progressIndeterminateCycleMs{1400}`/`statusbarBusyCycleMs{1200}` 已入
   Theme；reduceAnimation 归零（淡切即时、往返/busy 停转，静止形状保留
   "进行中"语义；驻留为内容节律保留——已测试锁定）。
7. **Toggle 项** = Ghost Button（hover/pressed 状态折算走既有路径；
   caption 12px secondary 文本经 textStyle 覆盖）。

### 15.1 复审对齐（2026-09 第二轮）

- **项折叠（§5.2 补齐）**：窗口变窄时消息先省略（flex + 省略号），再窄
  则项序列**从左往右整项隐藏**（§14.1 建议口径）、消息保底 120px；决策读
  上一帧栏几何 + 项宽缓存（toolbar §15.1 同口径），markDirty 二次收敛；
  折空分隔线消失、busy 项随开关出现/消失。变宽逐项回位（从尾部）。
- **Toggle 视觉语言**：Ghost → 新增 `ButtonVariant::Chrome`（resolver 特
  判）——hover 表面派生 + 前景提亮 primary、pressed = List pressed（稿件
  `.sb-item.is-toggle:hover/:active` 同值）。
- **indeterminate reduceAnimation（§10 补齐）**：静止**中段 40% 宽半透明
  带**落地——控制器在 `progressIndeterminateCycleMs == 0` 时写 scrollOffset
  = -1 哨兵，painter 识别负相位画居中 40% 带（fill 0.6 alpha）。
- **busy reduceAnimation（§10 补齐）**：弧静止 + **0.5 透明**（foreground
  折算 accent 128 alpha；形状保留"进行中"语义）。
- **Touch 档字号**：13px（§9.1；caption 12px 基础上 Touch 覆盖）。
- **语义补齐**：indeterminate 进度 `semanticsValue = "indeterminate"`（原
  值 "0" 误导）；busy 弧 `semanticsLabel = "进行中"`（Icon 带 label 进入语
  义树——§14.2 的定稿仍留 M13，先保证可感知）。
