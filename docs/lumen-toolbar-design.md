# Lumen Toolbar 控件设计（工具栏）

> 文档状态：已实施（2026-09，P1+P2 全量；实施状态与实现差异见文末 §15）
> 输入：源码现状盘点（`include/lumen/widgets/menu.h`（MenuItem/ContextMenuController/MenuBarController：命令面板与栏件先例）、`include/lumen/core/widget.h`（WidgetType::Tooltip/M6 Ghost variant、makeButton/makeTooltip）、`include/lumen/core/interaction.h`（sink 家族、hover 状态快照）、`include/lumen/style/theme.h`（MotionTokens：stateTransitionMs/tooltipDelayMs/tooltipFadeMs/menuOpenFadeMs））、`docs/lumen-menu-controls-design.md`（M14 动效实现口径 §10.2）、`docs/lumen-visual-system-design.md`（token 三层模型/尺度表 §3.2/状态规则 §5/§6.1 焦点环口径）。
> 配套视觉设计稿：`design/toolbar.html`。
> 定位：桌面自用版控件库增强；工具类应用"标题栏/菜单栏之下、内容之上"的常驻命令条。与 MenuBar 同族（命令入口），复用菜单的命令语义与浮动面板，不引入 Qt 级 QAction 注册表（menu-controls §2 既定边界）。遵循"Widget 不可变声明 + 应用侧控制器 + 既有控件组合"架构，**零新增 WidgetType、零新增 RenderCommand**。

---

## 1. 背景与问题

工具类桌面应用的第二高频 chrome 是**常驻命令条**：文件操作（新建/打开/保存）、历史（撤销/重做）、模式开关（运行/停止）、快速入口（搜索）。源码核对后当前缺口：

| 能力 | 现状 | 缺口 |
| --- | --- | --- |
| MenuBar | 顶级菜单锚定 + 命令面板 + mnemonic | 命令藏在菜单二层；无**一击直达**的常驻入口 |
| Button（Ghost） | 图标按钮的所有状态路径齐备 | 无栏级组合：分组分隔、toggle 语义、溢出折叠 |
| Tooltip（M6/M11） | hover 延迟显隐 + 淡入淡出 token | 无图标按钮消费场景（icon-only 项需要文字说明） |
| ContextMenuController | M14 面板（淡入/上升/键盘导航） | 无溢出面板复用场景 |
| 布局 | Row intrinsic 测量 | 无"宽度不足折叠尾部项"的栏级行为 |

menu-controls §2 已明确"Lumen 无 Action 抽象、无工具栏"。结论：工具栏是**栏级组合语义层**——一个 widgets 层控制器（项模型 + 分组 + 溢出决策 + 键盘漫游）+ 既有 Ghost Button/Tooltip/浮动面板的组合件。

## 2. 参考框架调研

| 框架 | 控件 | 项模型 | 溢出 | 对 Lumen 的启示 |
| --- | --- | --- | --- | --- |
| Qt 5/6 | `QToolBar` + `QAction` | Action 全局注册，菜单/工具栏/快捷键共享 | `extension` 溢出按钮（» 图标展开面板） | 溢出是工具栏必备；**Action 注册表不必备**（Qt 级全局状态与本仓架构相悖） |
| GTK4 | `GtkActionBar`（底）/自定义 `GtkBox` | `GAction` 声明式 | 无内建 | 顶栏形态靠组合；底栏是独立变体 |
| Win32/COM | `ReBar`/`ToolStrip`（WPF） | Items 集合 + `Overflow` 三态（AsNeeded/Always/Never) | 溢出显示 chevron 面板 | AsNeeded（按需折叠）是默认直觉；项可拖拽重排（不采纳） |
| macOS | `NSToolbar` | item identifier + 可定制面板 | 自定义面板 | 可定制/可拖拽是重量级能力，自用阶段不需要 |
| VS Code | 活动栏/编辑器工具栏 | icon-only + tooltip + **toggle 态**（高亮底色） | 窄窗口折叠到 `...` 菜单 | icon-only + tooltip + toggle 是现代桌面最小完备集 |

**采纳的共同事实**：

1. **项 = 与菜单项平行的命令描述**：id/icon/label/tooltip/checkable/enabled/分隔线。同一命令在菜单和工具栏出现时由**应用**保证 id 一致并联动状态——框架不建注册表（menu §2 本土事实重申）。
2. **icon-only + tooltip 是桌面默认**；label 模式（icon+文本水平排列）留给少数主操作。
3. **溢出折叠按需发生**（AsNeeded）：宽度不足时尾部项进溢出面板，尾部出现溢出按钮（chevron）；折叠与展开即时重排（无布局动画）。
4. **工具栏是单一 Tab 停靠域**：Tab 进入栏、Left/Right 在项间漫游焦点（roving），Enter/Space 激活——不逐项占 Tab 位（Windows/WPF 惯例）。
5. **toggle 项的激活态用持久底色**（accent-soft/selection），不是图标切换——空间所限，与菜单 checkable 的勾选槽区分。

**采纳的 Lumen 本土事实**（决定不做的事）：

1. 栏 = 普通 Widget 子树（Row + Ghost Button），参与语义树；溢出面板 = M11 overlay + M14 菜单面板路径——与 MenuBar"栏在主树、面板在 overlay"同构，零新增渲染能力。
2. 可定制工具栏（拖拽重排/自定义面板/显隐配置）不做——自用阶段无场景（§14）。
3. 底部 `GtkActionBar` 变体不做——栏方向是布局参数（应用把 build 结果放底部即可），不设控件变体。
4. split button（主按钮 + 下拉箭头）首版不做——交互面翻倍，留 §14。

## 3. 设计目标与非目标

**目标**

1. widgets 层 `ToolBarController`：项模型（icon/label/tooltip/checkable/checked/enabled/separator）、命令回调、溢出决策、键盘漫游。
2. `makeToolBar(controller)` 组合件：Ghost Button 行 + 分组分隔线 + 尾部溢出按钮，零新增 WidgetType/RenderCommand。
3. Tooltip 复用：icon-only 项 hover `tooltipDelayMs`=400 后淡入（`tooltipFadeMs`=120），显示 label + 快捷键文本（可选）。
4. 溢出：宽度不足折叠尾部项 → 溢出按钮（ChevronDown）→ 点击弹 M14 面板（icon + label 列表，键盘导航复用）。
5. 键盘、指针、语义三层一致；全部新路径 headless 可测；既有帧哈希不变。

**非目标（第一版明确不做）**

- QAction 式全局命令注册表（应用自管两份描述的联动）。
- 项拖拽重排、显隐自定义面板、NSToolbar 式定制。
- split button（下拉复合按钮）、栏内嵌入式控件（搜索框等——应用可用 Row 自行在栏旁组合，不属控件）。
- 纵向工具栏/停靠（docking）——栏方向由应用布局决定，不设变体。
- 状态栏联动协议（工具栏 toggle 驱动状态栏 busy 属应用逻辑，见 statusbar 设计稿示例场景）。

## 4. 总体架构

```text
┌───────────────────────────────────────────────────────────┐
│ 应用层：makeToolBar(key, &controller)                       │
│   onCommand(id) → StateStore 写回（checked 状态应用维护）    │
├───────────────────────────────────────────────────────────┤
│ widgets 层（新文件 include/lumen/widgets/toolbar.h）        │
│   ToolBarItem（值类型模型）                                  │
│   ToolBarController（项序列/checked 快照/溢出决策/漫游焦点）  │
├───────────────────────────────────────────────────────────┤
│ core 层（零改动）：Ghost Button + Icon + Tooltip + Row      │
│   溢出面板 = M11 overlay + ContextMenuController 面板路径   │
├───────────────────────────────────────────────────────────┤
│ 交互层（零改动）：按钮 onClick/hover/pressed 既有路径；      │
│   布局回填 lastBarWidth（SplitterController 同先例）        │
└───────────────────────────────────────────────────────────┘
```

核心架构决策：**工具栏是 Ghost Button 子树 + 栏级控制器，不是新 WidgetType**。溢出决策需要"本帧实际可用宽度"，走 SplitterController `lastExtent` 同款布局期回填：build 全量声明 → 布局回填宽度 → 控制器重算溢出集合 → 下一帧 build 折叠（二次收敛，splitter §6 同口径）。

## 5. 项模型与控制器（widgets 层）

```cpp
// include/lumen/widgets/toolbar.h（新）
namespace lumen::widgets {

struct ToolBarItem {
    std::string id{};          // 稳定标识：行 key = "tb:item:<id>"；回调参数
    core::IconId icon{core::IconId::None};  // 图标（icon-only 项必填）
    std::string label{};       // tooltip 文本；labelMode 项的栏内文本
    std::string shortcut{};    // tooltip 尾随展示（"Ctrl+S"；仅显示，同菜单口径）
    bool separator{false};     // 分组分隔线（不可聚焦，不进语义树——菜单 msep 同口径）
    bool checkable{false};     // toggle 项：checked 持久底色
    bool checked{false};       // 应用维护（StateStore 事实来源）
    bool labelMode{false};     // icon+文本水平排列（默认 icon-only）
    bool enabled{true};
};

}  // namespace lumen::widgets

class ToolBarController {
  public:
    void setItems(std::vector<ToolBarItem> items);   // 全量声明
    // 项级懒取（动态 checked 快照场景；空 = 用 setItems 的副本）。
    void setItemProvider(std::function<std::vector<ToolBarItem>()> provider);

    // 栏 Widget（应用 build 每帧调用；溢出按钮 key = "tb:overflow:<key>"）。
    // theme 提供 accent/动效口径（MenuBarController::build 同签名模式）。
    [[nodiscard]] core::Widget build(const style::Theme& theme) const;

    // 注册项 handler 与溢出面板；应用装配时调用一次。
    void attach(app::AppShell& shell);

    // 键盘（应用 onKey 转发；栏内漫游焦点时）。Left/Right/Home/End 移动
    // 焦点，Enter/Space 激活，Down 打开该项溢出面板（若有），Esc 归位。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers mods = core::kModifierNone);

    // 命令回调（与 ContextMenuController::onCommand 同语义；checkable 项
    // 激活后应用翻转状态并重建）。
    std::function<void(const std::string& id)> onCommand{};

    // 布局期回填（框架调用；应用只读）——溢出决策输入。
    void noteBarWidth(float widthPx);

    // 溢出集合只读查询（测试/语义用）。
    [[nodiscard]] const std::vector<std::string>& overflowedIds() const;
};
```

设计要点：

1. **checked 由应用维护**（MenuItem §5 同口径）：toggle 激活 → `onCommand(id)` → 应用翻转 → 重建反映新快照。控制器不持状态。
2. **溢出决策**：可用宽度 < 全量 intrinsic 宽度时，**从尾部**逐项折叠（labelMode 项优先折叠，separator 折空后自动消失），直到放得下 + 溢出按钮；至少保留首项（全折叠 = 栏只剩溢出按钮，不出现空栏）。溢出集合按原顺序进面板。
3. **漫游焦点**：控制器记录焦点项 id（不进 StateStore）；Tab 进入时落到记忆项或首项。
4. 项宽：icon-only = 方形通高（§9.1）；labelMode = icon + 8 gap + label + 水平内边距。

## 6. 交互契约

### 6.1 指针

| 动作 | 行为 |
| --- | --- |
| 单击项 | `onCommand(id)`；checkable 项激活后重建反映 checked |
| hover 项 | 背景 hover 派生（状态过渡 100ms） |
| hover 停留 | `tooltipDelayMs`=400ms 后 tooltip 淡入（120ms）——显示 label（+ shortcut 换行尾随）；移出即隐 |
| 单击溢出按钮 | 打开溢出面板（M14 菜单面板路径：icon + label，无 shortcut 列） |
| 右键栏 | 不消费（应用可经 `SecondaryPressSink` 自行挂"自定义工具栏"菜单，不属控件） |

- tooltip 复用 `WidgetType::Tooltip` 既有气泡与 token，锚定项上方居中（栏在窗口顶部时）——锚定方向由应用布局位置决定，首版固定上方。

### 6.2 键盘（栏拥有焦点时）

| 键 | 行为 |
| --- | --- |
| Left / Right | 焦点移到前/后一**可聚焦**项（跳过 separator/disabled；到端即停，不环绕——集合行口径） |
| Home / End | 焦点到首/末可聚焦项（含溢出按钮） |
| Enter / Space | 激活焦点项 ≡ 单击 |
| Down | 焦点在溢出按钮 → 打开面板（面板内 Up/Down/Enter/Esc 为 M14 既有契约） |
| Escape | 面板打开时关闭并焦点回溢出按钮；否则无操作 |
| Tab / Shift+Tab | 焦点离开栏（单一停靠域：Tab 序上整栏一个位） |

- **焦点环统一开启**：`makeToolBar` 为全部项按钮显式 `showFocusRing=true`（框架自建键盘件不走应用 opt-in——`makeTabs` 同口径：栏是 Tab 停靠点且 Left/Right 漫游无其他指示，视觉系统 §6.1）。
- 项不占独立 Tab 位（漫游焦点模型）；焦点记忆在控制器，重建后保持。

### 6.3 溢出行为

- 触发：窗口/容器变窄（布局回填宽度变化）→ 下一帧折叠；变宽 → 逐项回位。**即时重排，无布局动画**（与菜单关闭同口径：布局参数不是视觉状态）。
- 溢出面板项语义与栏内项完全一致（onCommand 同 id）；checkable 项面板内同样显示 checked 底色。
- 溢出按钮出现/消失 = 状态过渡淡入 100ms（按钮自身 alpha，非布局动画）。

## 7. 语义契约

| 节点 | role | 状态/动作 |
| --- | --- | --- |
| 工具栏容器 | `toolbar`（新 role，枚举尾部追加，MenuItem/Splitter 先例） | label = 应用 semanticsLabel（如"主工具栏"） |
| 项 | 既有 Button role | label = 项 label（icon-only 项的文本事实来源）；checkable 项 `kSemanticsChecked`；Activate ≡ Enter ≡ 单击 |
| 溢出按钮 | 既有 Button role | label = "更多命令"（应用可覆盖） |
| 分隔线 | 不进语义树 | 菜单 msep 同口径 |

三层一致性断言（headless 验收）：单击 ≡ Enter ≡ 语义 Activate 触发同一 `onCommand(id)`；checked 视觉（持久底色）≡ 语义 flag ≡ 应用状态；溢出项激活与栏内激活同回调；disabled 四层一致。

## 8. Widget / DSL 扩展与体积预算

| 项 | 增量 |
| --- | --- |
| `WidgetType` | **零新增**（Row + Ghost Button + Icon + Tooltip 组合） |
| `Widget` 字段 | **零新增**（checked 复用既有状态位；labelMode 是声明组合差异，非字段） |
| RenderCommand / 序列化版本 | **零改动**（分隔线/图标/tooltip 全部既有命令） |
| `SemanticsRole` | 尾部追加 `Toolbar`（既有 role 数值不变） |
| `IconId` | 候选新增（枚举追尾部，零几何冲突）：Undo/Redo/Play/Grid/Settings——应用项集用到才扩；Plus/Folder/Document/Search/ChevronDown 既有 |
| `.lumen` DSL | 不加节点（运行时控制器行为，menu/splitter 同理由） |
| C++ DSL | `makeToolBar(const ToolBarController*, std::string key = {})` |

Widget 体积静态断言不动。

## 9. 视觉规格（详见 design/toolbar.html）

视觉契约遵循 `docs/lumen-visual-system-design.md`，只做部件级映射，**不新增颜色槽位**。

### 9.1 尺度（视觉系统 §3.2 对齐）

| 项目 | Small/Compact | Medium/Comfortable | Large/Touch | 依据 |
| --- | ---: | ---: | ---: | --- |
| 栏高（border-box） | 33px | 41px | 49px | 内容行 = 控件最小高度档（32/40/48）+ 1px 底分隔线（design/toolbar.html `--item + 1px`） |
| 项尺寸（icon-only） | 32×32 | 40×40 | 48×48 | 方形通高（caption 按钮通高先例） |
| 图标 | 16px | 16px | 20px | `Metrics.inlineIconSize` 既有档 |
| 栏水平内边距 | 4px | 8px | 8px | §3.2 水平内边距紧凑档 |
| 项间 gap | 2px | 4px | 4px | `controlGap` 既有档（栏件更紧凑，menubar gap 2 同源） |
| 分组分隔线 | 1px 宽，上下 inset 8px | 同 | 同 | 菜单 msep 水平 inset 8 的转置；高度 = 栏高 − 16 |
| 分隔线两侧 gap | 8px | 8px | 8px | msep 呼吸同口径 |
| 项圆角 | 4px | 6px | 8px | 视觉系统"小部件圆角"行 |
| labelMode 项内 icon-text gap | 8px | 8px | 8px | 菜单项 icon 槽 gap 同源 |

### 9.2 组件 token（三层模型 §3.1 的 component 层）

```text
toolbar.bar.background       = color.background.surface
toolbar.bar.border           = color.border.default（底部 1px，与 MenuBar 同）
toolbar.item.background      = 透明（Ghost rest）
toolbar.item.background.hover   = surface 层 hover 派生（menubar 栏项同源）
toolbar.item.background.pressed = surface/accent 0.32 混合（List pressed 同派生）
toolbar.item.background.checked = color.accentContainer（toggle 持久底，稿件 .is-checked 的 --tb-checked；前景 contentPrimary，悬停不变）
toolbar.item.content         = color.content.secondary（rest）→ primary（hover/pressed/checked）
toolbar.item.content.disabled = color.disabled.content
toolbar.item.focusRing       = color.focus.ring + focusWidth（内嵌；makeToolBar 恒开启）
toolbar.separator            = color.border.default
toolbar.overflow.icon        = ChevronDown 16/16/20（IconId 既有）
tooltip.*                    = 既有 tooltip token 全量复用
```

### 9.3 状态矩阵（项）

| 状态组合 | 背景 | 图标/文本 | 指示 |
| --- | --- | --- | --- |
| rest | 透明（栏 surface 底） | `content.secondary` | — |
| hover | surface hover 派生 | `content.primary` | tooltip 延迟武装 |
| pressed | accent 0.32 混合 | `content.primary` | — |
| checked（toggle） | `accentContainer` 持久（§15.1：稿件 `--tb-checked`；悬停不改变 checked 底） | `content.primary`（高对比下加图标 underline 2px accent——形状差异，§11） | 与菜单勾选槽区分：空间所限用底色；`kSemanticsChecked` 同步 |
| hover + checked | checked 覆盖 hover（§5 规则 3 同源） | 同上 | — |
| disabled | 不变 | `disabled.content` | 命中拒绝 + 键盘跳过 + 语义一致 |
| focused（漫游） | 不变 | 不变 | 内嵌焦点环（恒开启） |
| 溢出中的项 | 面板内按菜单项矩阵渲染 | 同 | checked 同底色 |

高对比主题：toggle checked 不能只靠底色——图标下加 2px accent 下划线（menubar 打开态下划线同语言）；焦点环对比提升（既有派生）。

## 10. 动效规格

| 动效 | 时长/曲线 | 驱动 | reduceAnimation |
| --- | --- | --- | --- |
| 项 hover/pressed/checked 背景与前景 | `stateTransitionMs` = 100ms | 既有状态过渡通道 | 归零（即时） |
| **tooltip 显隐** | 延迟 `tooltipDelayMs`=400ms → 淡入 `tooltipFadeMs`=120ms；移出即隐 | M6/M11 既有 Tooltip 通道（常驻树透明度切换） | 延迟归零 = 立即显示（既有规则） |
| **溢出面板打开** | `menuOpenFadeMs`=120ms 淡入 + 上升 6px（EaseOut） | M14 菜单动效路径（overlay animate sink 逐 tick）零新增 | 零时长直达终态（既有规则） |
| 溢出折叠/回位 | **无布局动画**（即时重排） | 布局参数非视觉状态（菜单关闭即时同口径） | —（本就无动画） |
| 溢出按钮出现/消失 | 按钮 alpha 状态过渡 100ms | 既有状态过渡通道 | 归零 |

**零新增 MotionTokens**：全部复用 stateTransitionMs / tooltipDelayMs / tooltipFadeMs / menuOpenFadeMs（M6/M10/M11/M14 既有）。工具栏的动效立场是"栏件不动、状态缓动、面板借用菜单动效"——常驻 chrome 不参与布局动画，避免窗口 resize 期间栏内项目弹跳。

## 11. 性能与测试计划

### 11.1 性能口径

- 栏为小树（典型 ≤ 20 项），溢出决策 O(n) 宽度比较，每帧常数成本；溢出切换帧 = 一次普通重建。
- tooltip 无动画帧空转（M11 既有：延迟期无 tick、显示期 120ms 后静止）。

### 11.2 测试（Catch2，`*_tests.cpp`，行为命名）

1. **模型**：setItems/provider 快照；separator/labelMode/enabled 组合。
2. **激活**：单击 → onCommand(id)；checkable 激活后重建反映 checked；disabled 拒绝。
3. **键盘漫游**：Left/Right 跳过 separator/disabled、Home/End、Enter/Space 激活、Tab 单停靠域进出、焦点记忆跨重建保持。
4. **溢出**：宽度回放序列（宽→窄→宽）折叠/回位顺序正确（尾部优先、labelMode 先折、折空 separator 消失、至少留首项）；溢出面板打开/键盘导航/激活同回调；溢出按钮 alpha 过渡。
5. **tooltip**：400ms 延迟内移出不显示；显示后移出即隐；icon-only 项 label 为语义 label 同源。
6. **语义**：toolbar role 树、checkable flag、Activate ≡ Enter ≡ 单击、溢出项激活等价、separator 缺席。
7. **视觉派生**：density 三档、fontScale、高对比（checked 下划线形状差异）、reduceAnimation（tooltip 即时、面板直达、状态即时）。
8. **回归**：既有全部测试与帧哈希不变。

### 11.3 示例与验收

- settings 新页 `Toolbar`：文件组 + 撤销重做 + toggle（网格开关）+ 搜索入口；窄容器演示溢出。
- gallery 窗口 chrome 补工具栏样本（标题栏下）。
- headless 冒烟：漫游/激活/溢出脚本输出；三桌面窗口 smoke 由 CI 承担。

## 12. 实施分期

| 阶段 | 内容 | 出口条件 |
| --- | --- | --- |
| P1 栏与命令 | ToolBarItem/ToolBarController + makeToolBar（icon-only + 分隔 + toggle）+ 单击/tooltip + 视觉 + 测试 1/2/5 | 命令链路三层一致；既有哈希不变 |
| P2 键盘与溢出 | 漫游焦点 + 焦点环恒开启 + 溢出决策/面板复用 + labelMode + 语义 role + 测试 3/4/6 | 四层一致全绿 |
| P3 按需评估 | split button、右键自定义菜单约定、tooltip 键盘聚焦显示（focus 停留 400ms 同延迟） | — |

## 13. 兼容与迁移

1. Ghost Button/Tooltip/ContextMenuController 行为零改动（组合不改内部）；`WidgetType` 不动，既有帧哈希不变。
2. `tb:` key 前缀与 `list:`/`menu:`/`spin:` 同命名空间规则。
3. `SemanticsRole` 尾部追加 `Toolbar`，既有 role 数值不变。
4. 溢出面板复用 ContextMenuController 时**不带 shortcut 列/mnemonic**（工具栏项无此语义，面板 builder 参数化收窄）。

## 14. 开放问题（实施前需确认）

1. **tooltip 锚定方向**：栏固定在窗口顶部时 tooltip 在项上方还是下方？桌面惯例下方（不遮标题栏），但栏紧贴菜单栏时下方会盖内容——建议下方 + 8px 间距，实测再定。
2. **溢出按钮图标**：ChevronDown（面板向下打开直觉）vs Windows 惯例 »（ChevronRight 双叠）——首版 ChevronDown（IconId 既有，零新增）。
3. **toggle 高对比形状差异**：图标下 2px accent 下划线 vs 图标描边加粗——§9.3 按下划线写（menubar 打开态同语言），高对比实测再定。
4. **labelMode 项的溢出优先级**：labelMode 先折是"信息保 icon"直觉；若实测视觉突兀，改为同序折叠。
5. **溢出面板宽度**：复用菜单面板 160–320px 区间是否够 labelMode 长 label——面板内允许省略号，不放宽。

## 15. 实施状态（2026-09 追记）

P1（栏与命令）与 P2（键盘与溢出）已实施：`include/lumen/widgets/toolbar.h` +
`src/widgets/toolbar.cpp`（`ToolBarController`），settings「Toolbar」页与
gallery「Controls」页接入。`tests/toolbar_tests.cpp` 10 用例 + settings 集
成冒烟全绿；全套 ctest（Debug 699 用例）通过。

与设计稿的实现差异（同提交更新本文档）：

1. **build 签名**：`build(app::AppShell&, const style::Theme&)`——溢出决
   策读上一帧布局几何（bar 容器宽 + 项宽缓存），需要 shell 引用（Splitter
   布局回填的"读上一帧"变体；§5.2 的 noteBarWidth 回调取消）。
2. **avail 取自 bar 容器**：row 自收缩（内容宽），折叠后变窄会造成"无法回
   位"死锁——可用宽必须读被父级拉伸的栏容器（`<key>:bar`），应用需把栏放
   进 Stretch 列（settings/gallery 均如此）。实现教训，§6.3 补记。
3. **溢出决策收敛**：首帧全量（无上一帧几何），次帧折叠/回位，markDirty
   二次收敛（Splitter 同口径）；labelMode 项宽首帧按 CJK 1.0/ASCII 0.55×
   字号估算，实测后入缓存修正。
4. **toggle 激活态**：Tonal 变体（accentContainer 持久底）——非 §9.3 的
   selection 底。原因：`StyleOverrides.background` 在状态折算后应用会压掉
   hover 混合；Tonal 保持 hover/pressed 折算工作，且与 MenuBar 打开态同语
   言。高对比形状差异（图标下划线）留 §14.3。
5. **键盘漫游**：项为普通 Tab 停靠点（MenuBar 栏项口径）——真正的单一停
   靠域需要 tabindex 抑制机制（框架无此缝隙）；Left/Right/Home/End 漫游、
   Down 开溢出面板、焦点环恒开启照稿实施。§6.2 按实现口径更新。
6. **tooltip**：复用 M11 `registerTooltip`（label 非空项注册 anchor→tip
   关联；tooltip 常驻子树挂栏 Stack，隐藏 alpha 0，定位/延迟/淡切全由壳层
   既有机制承担）；shortcut 以"　"拼接进文本。
7. **溢出面板**：复用 `ContextMenuController::openAnchored`（owner = 控件
   key；面板项按原序，与 §6.3 一致）；焦点恢复目标 = 溢出按钮。
8. **图标**：Undo/Redo/Play/Grid/Settings 已入 `IconId` 目录（尾部追加；
   16 栅格设计归一），`Busy`/`Grip` 见 statusbar §15。

### 15.1 复审对齐（2026-09 第二轮）

- **项视觉语言**：改用新增 `ButtonVariant::Chrome`（resolver 特判）——
  hover 表面派生 + **前景提亮 contentPrimary**、pressed = **List pressed**
  （surface/accent 0.32 混合，`theme.list.pressed`），对齐稿件
  `.tb-item:hover/:active`；§15.4 的 Tonal 妥协作废。
- **checked（§9.2 修正）**：toggle 持久底落地为 **accentContainer**（稿件
  `.is-checked` 的 `--tb-checked` #2e3c60；原 §9.2 写 selection.background
  系笔误——selection 为 50% 透明 accent，压在栏 surface 上观感相同但语义
  通道不同），前景 contentPrimary，悬停不改变 checked 底；`Widget.checked`
  直接声明 → `kSemanticsChecked` 语义 flag 同步（此前 Tonal 方案缺失）。
- **溢出按钮右贴**：折叠态下 row 注入 flex spacer，溢出按钮右贴栏缘
  （稿件 `.tb-overflow margin-left:auto`）；行借 flex 子扩展到栏宽。
- **labelMode 起始对齐**：icon+文本内容组 `alignContentStart`（稿件
  `.has-label` 起始排列），icon-only 项保持居中。
- **§10 溢出按钮 alpha 渐入修正**：未实现（出现/消失即布局增减，即时切
  换）——alpha 渐入需要控制器持钟 + onAnimate 链，收益低；留按需评估。
