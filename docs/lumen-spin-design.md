# Lumen Spin 控件设计（SpinBox 数值步进）

> 文档状态：已实施（2026-09，P1+P2 全量；实施状态与实现差异见文末 §15）
> 输入：源码现状盘点（`include/lumen/core/widget.h`（WidgetType::TextField/Slider/ProgressBar/Tooltip、makeTextField/makeSlider/makeTooltip 先例）、`include/lumen/core/windowing.h`（Key 枚举 Up/Down/PageUp/PageDown/Home/End 齐备）、`include/lumen/core/interaction.h`（sink 家族/Slider 拖动锁定/WheelSink）、`include/lumen/style/theme.h`（MotionTokens/Metrics/TextFieldTokens）、`include/lumen/widgets/menu.h`（控制器 + 组合件零新增 WidgetType 先例））、`docs/lumen-visual-system-design.md`（token 三层模型 §3.1/尺度表 §3.2/状态规则 §5）、`docs/lumen-splitter-design.md`（"顶住"钳制手感先例）。
> 配套视觉设计稿：`design/spin.html`。
> 定位：桌面自用版控件库增强；表单与设置页的离散数值输入控件（数量、字号、不透明度、边距）。与 Slider 同族（数值通道），但面向**精确键入 + 小步调节**，不是快速扫掠。遵循既有"Widget 不可变声明 + 应用侧控制器 + 既有控件组合"架构。

---

## 1. 背景与问题

数值输入在设置页与工具面板是高频形态。源码核对后当前缺口：

| 能力 | 现状 | 缺口 |
| --- | --- | --- |
| TextField | 单行/多行编辑、选区、IME、invalid 态齐备 | 无数值语义：不解析数字、无 min/max、无步进通道 |
| Slider | 拖动/键盘改值（bind 0..100） | 面向比例扫掠：精确值（如 13px）要拖多次；无键入通道；范围/步进由 bind 隐含 |
| Dropdown | 值选择 | 离散枚举语义，非连续数值 |
| 键盘 | Up/Down/PageUp/PageDown/Home/End 均在 Key 枚举内 | 无控件消费它们做"步进"语义 |

结论：Spin 不是渲染问题（stepper = 两个图标按钮 + 一条分隔线），而是**数值编辑语义层**——一个 widgets 层控制器（值域/步进/提交解析）+ TextField 既有编辑路径的组合件。**零新增 WidgetType、零新增 RenderCommand**（menu-controls 同架构口径）。

## 2. 参考框架调研

| 框架 | 控件 | 值语义 | 键盘 | 对 Lumen 的启示 |
| --- | --- | --- | --- | --- |
| Qt 5/6 | `QSpinBox` | int + `setValue` 钳制；`singleStep`/`pageStep`；`prefix`/`suffix`/`decimals` | Up/Down 步进，PgUp/PgDn 翻页；键入即时校验（`validate` 三态） | prefix/suffix 与 decimals 是常用项；非法输入"边打边标"而非静默改写 |
| GTK4 | `GtkSpinButton` | double；`wrap` 可环绕；`snap-to-ticks` | 同上 + 直接键入，失焦提交 | wrap 是独立开关（默认关）；失焦/Enter 才提交编辑值 |
| Win32 | `UDM` up-down | int；attach 编辑框 | 按住自动重复：**首次延迟约 500ms，随后每 ~62ms 一步** | 自动重复节奏的系统惯例；到界后按钮仍在但无效 |
| macOS | `NSStepper` | double；**无文本框配套时独立成对按钮** | 方向键同源 | stepper 与 field 可拆可合；合体是桌面主流 |
| 浏览器 | `<input type=number>` | double + min/max/step；stepMismatch 处理 | Up/Down；wheel 不改值（防误触） | 滚轮改值在桌面工具是惯例、在表单页是争议——本稿按桌面工具定位开放滚轮 |

**采纳的共同事实**：

1. **值域钳制在 `setValue` 单点收口**：拖动/键盘/滚轮/直接键入四路同源（Splitter `setOffset` 同模式）；到界"顶住"（继续按住无位移、无环绕跳变，除非显式 `wrap`）。
2. **编辑值与提交值分离**：键入文本是候选，Enter/失焦才 parse+clamp+commit；非法文本进 invalid 态不提交，失焦恢复上次已提交值。
3. **步进通道三件套**：单击 ±step；按住自动重复（500ms 延迟 + ~60ms 间隔）；键盘 Up/Down ±step、PgUp/PgDn ±pageStep、Home/End 到界。
4. **stepper 是 field 的附属 chrome**：内嵌右缘、共享边框与底色，不是两个独立按钮。
5. **数值文本不做补间**：连续步进时数字滚动/翻牌动画会模糊读数（与 TextField"文字内容不参与状态过渡"同口径）。

**采纳的 Lumen 本土事实**（决定不做的事）：

1. 不新增 WidgetType：`makeSpin` = TextField + 图标按钮组合件，编辑/选区/IME/caret/invalid 全部复用 TextField 既有路径（menu"面板 = Button 子树"同先例）；自绘 stepper painter 会重复实现文本编辑，明确不采纳。
2. 不引入校验回调协议：首版只做"数字可解析性 + 值域"校验（invalid 态复用 TextField token）；业务校验（如"必须是偶数"）由应用在 `onCommitted` 后自行处理。
3. 本地化数字格式（千分位、小数点本地化）不做：默认 ASCII 解析（`strtod` 口径），本地化留应用层。

## 3. 设计目标与非目标

**目标**

1. widgets 层 `SpinController`：值/值域/步进/小数位/环绕语义，四路输入（按钮/键盘/滚轮/键入）汇入同一 `setValue`。
2. `makeSpin(controller)` 组合件：TextField（编辑区）+ 内嵌 stepper（上/下半高按钮），零新增 WidgetType/RenderCommand。
3. 按住 stepper 自动重复（500ms 延迟 + 60ms 间隔，MotionTokens）；键盘全契约（Up/Down/PgUp/PgDn/Home/End/Enter/Escape）。
4. 键盘、指针、滚轮、语义四层一致（M5 出口条件延伸）。
5. 全部新路径 headless 可测；既有示例与测试帧哈希不变。

**非目标（第一版明确不做）**

- prefix/suffix（单位展示，如 "12 px"）——常用但牵动光标/选区几何，留 §14。
- Slider 式 field 拖动改值（文本选区优先，语义冲突）。
- 浮点环绕 `wrap` 之外的加速步进（按住越久步长越大——Gtk 有、Win32 无；自动重复已覆盖节奏需求）。
- 触摸专有手势（点击/长按与鼠标一致）。
- 日期/时间 spin（值域语义独立，按需另立设计）。
- RTL（同 Splitter §14，随布局镜像能力统一评估）。

## 4. 总体架构

```text
┌───────────────────────────────────────────────────────────┐
│ 应用层：makeSpin(key, &controller)                          │
│   onCommitted → StateStore 写回 → 请求重建                  │
├───────────────────────────────────────────────────────────┤
│ widgets 层（新文件 include/lumen/widgets/spin.h）           │
│   SpinController（value/min/max/step/pageStep/decimals/    │
│     wrap；setValue 钳制 + snap；parse/commit；自动重复计时） │
├───────────────────────────────────────────────────────────┤
│ core 层（零改动）：WidgetType::TextField + Button + Icon    │
│   组合；Key 枚举已含全部所需键位                            │
├───────────────────────────────────────────────────────────┤
│ 交互层：stepper onClick（既有）+ 按住重复（FrameScheduler    │
│   tick，caret blink 同口径）+ wheel（WheelSink 家族既有）    │
└───────────────────────────────────────────────────────────┘
```

核心架构决策：**Spin 是 TextField 的数值语义包装，不是新控件类型**。stepper 按钮是普通 Ghost Button（key 前缀 `spin:up:<key>` / `spin:down:<key>`），自动重复计时由控制器持有、经 FrameScheduler tick 步进（caretBlinkHalfPeriodMs 同驱动源）。

## 5. 值模型与控制器（widgets 层）

```cpp
// include/lumen/widgets/spin.h（新）
class SpinController {
  public:
    // 值域与步进。setValue 内部：钳制到 [min, max]（wrap=true 时环绕）、
    // snap 到 step 网格（min + round((v-min)/step)*step）、按 decimals
    // 舍入到显示精度。四路输入（按钮/键盘/滚轮/键入提交）全部经此单点。
    void setValue(double v);
    [[nodiscard]] double value() const;

    void setRange(double min, double max);     // 默认 [0, 100]
    void setStep(double step);                 // 默认 1
    void setPageStep(double step);             // 默认 10 × step
    void setDecimals(int digits);              // 默认 0（整数）
    void setWrap(bool wrap);                   // 默认 false（到界顶住）

    // 步进通道（按钮/键盘/滚轮共用；自动重复逐拍调用）。
    void stepUp();
    void stepDown();                           // = setValue(value() ± step)
    [[nodiscard]] bool atMin() const;
    [[nodiscard]] bool atMax() const;

    // 编辑提交：field 文本 → parse（ASCII double）→ 成功则 setValue 并
    // 清 invalid；失败则保持 invalid、不提交。Escape 恢复上次已提交值。
    bool commitText(const std::string& text);
    void revert();                             // field 文本回到 format(value())

    // 显示：value → 文本（decimals 精度，无本地化分组）。
    [[nodiscard]] std::string formatValue() const;

    // 回调（UI 线程）。onValueChanged 在每一步进拍触发（自动重复期间
    // 连续触发，重建单帧批量吸收）；onCommitted 仅在提交点触发。
    std::function<void(double value)> onValueChanged{};
    std::function<void(double value)> onCommitted{};
};
```

`makeSpin(controller, key)` 组合结构（示意）：

```text
Row(key=<key>, borderColor/focused/invalid 复用 textfield token)
 ├─ TextField(key=<key>:field)          // 编辑区；bind 不接 StateStore，
 │                                        // 文本由控制器 formatValue 播种
 └─ Column(key=<key>:stepper)           // 内嵌右缘，宽 spin.stepper.width
     ├─ Ghost Button(key=spin:up:<key>)   // 半高；Icon ChevronUp
     └─ Ghost Button(key=spin:down:<key>) // 半高；Icon ChevronDown
```

- **单一 Tab 停靠点** = TextField（caret 即焦点指示，焦点环保持默认关闭——视觉系统 §5 规则 4；stepper 按钮不参与 Tab 遍历，`Tab` 顺序上整个 Spin 是一个停靠点）。
- stepper 与 field 共享一条外边框：外框由 Row 承载（textfield token），cluster 左缘与中缝各 1px `border.default` 分隔线；外缘右侧圆角随 `controlRadius`，内缘直角（Windows/VS Code 惯例）。
- 到界（`atMin/atMax`）：对应 stepper 的 chevron 降为 `disabled.content` 色，按钮仍可命中但步进无效（"顶住"，Splitter §7.1 同手感）；不整体转 disabled（避免热区与语义在边界闪烁）。

## 6. 交互契约

### 6.1 指针

| 动作 | 行为 |
| --- | --- |
| 单击 ▲ / ▼ | `stepUp()` / `stepDown()`（±step，立即） |
| 按住 ▲ / ▼ 不放 | 自动重复：**首次延迟 `spinRepeatDelayMs` = 500ms，随后每 `spinRepeatIntervalMs` = 60ms 一步**；pointerUp/Cancel/移出按钮命中区即停 |
| 点击 field | TextField 既有：光标定位/选区/IME |
| 双击 field | TextField 既有：选中数字文本 |

- 自动重复期间每拍触发 `onValueChanged` 并请求重建（单帧多拍合并为一次重建——FrameScheduler 既有合帧口径）。
- stepper 无拖动语义（与 Slider 区分）；pointerCancel 退出干净、不留按下态。

### 6.2 键盘（焦点在 field 时）

| 键 | 行为 |
| --- | --- |
| Up / Down | `stepUp()` / `stepDown()`（±step） |
| PageUp / PageDown | ±pageStep |
| Home | `setValue(min)`；End = `setValue(max)` |
| Enter | `commitText(field)`；成功 → `onCommitted` |
| Escape | `revert()`（恢复上次已提交值，清 invalid） |
| Tab / Shift+Tab | 焦点离开（既有遍历） |
| 数字/小数点/负号 | TextField 既有编辑；每次变更即时 parse：可解析 → 正常态；不可解析 → invalid 态（边框 `textfield.border.invalid`，不提交） |

- 失焦（Tab 离开或指针点击外部）≡ Enter 提交；非法文本失焦时 `revert()` 而非提交（GTK 惯例：尊重已提交值，不猜用户意图）。
- 键位转发：应用 `ShellConfig.onKey` 以"焦点在 spin field"为条件转发 `SpinController::handleKey`（`MenuBarController::handleKey` 同模式）。方向键在单行 TextField 中无既有消费（多行才移动光标）；若实施核对发现字段层已消费方向键，补一条"focus 在 `spin:` 前缀节点时优先咨询控制器"的键盘缝隙（§14.1）。

### 6.3 滚轮

- 指针悬停在控件上（field 或 stepper）滚轮：每档 ±step（`WheelSink` 家族既有通道，控件命中区消费）。
- 控件不在焦点时同样生效（桌面工具惯例：hover 即调值）；`wrap` 关闭时到界停止。
- 连续滚动的拍间合并同自动重复（重建合帧）。

## 7. 语义契约

| 节点 | role | 状态/动作 |
| --- | --- | --- |
| Spin 容器（Row） | `spinbutton`（新 role，枚举尾部追加，MenuItem/Splitter 先例） | label = 应用 semanticsLabel（如"不透明度"）；value = formatValue() |
| field | 既有 TextField role | 编辑语义保留（选区/caret/IME） |
| stepper 按钮 | 不进语义树 | 纯附属 chrome（Increase/Decrease 由 spinbutton 节点承载） |

- `SetValue` action ≡ 键盘键入+Enter ≡ commitText 到达同一 `setValue`；`Increase`/`Decrease` ≡ Up/Down ≡ 单击 ▲/▼ ≡ 滚轮 +1 档（四层一致断言）。
- `atMin/atMax` 不折算 disabled 语义（action 仍可用、值顶住——与视觉"淡化但可命中"一致）。
- invalid（编辑中文本非法）→ `kSemanticsInvalid` 与视觉同步（M5 既有 flag）。

## 8. Widget / DSL 扩展与体积预算

| 项 | 增量 |
| --- | --- |
| `WidgetType` | **零新增**（TextField + Button + Icon 组合） |
| `Widget` 字段 | **零新增**（值状态在 SpinController，Widget 只承载声明） |
| RenderCommand / 序列化版本 | **零改动**（stepper = 既有表面/图标/分隔线命令） |
| `SemanticsRole` | 尾部追加 `SpinButton`（既有 role 数值不变） |
| `.lumen` DSL | 不加节点（运行时控制器行为，与 menu/splitter 同理由不进冻结节点集） |
| C++ DSL | `makeSpin(const SpinController*, std::string key = {})` 便捷 builder |

Widget 体积静态断言不动（当前 ≤832/≤936，`collection_tests.cpp:710` 口径）。

## 9. 视觉规格（详见 design/spin.html）

视觉契约遵循 `docs/lumen-visual-system-design.md`（token 三层模型 §3.1、尺度表 §3.2、状态规则 §5），只做部件级映射，**不新增颜色槽位**。

### 9.1 尺度（视觉系统 §3.2 对齐）

| 项目 | Small/Compact | Medium/Comfortable | Large/Touch | 依据 |
| --- | ---: | ---: | ---: | --- |
| 控件最小高度 | 32px | 40px | 48px | 视觉系统"控件最小高度"行 |
| 控件最小宽度 | 120px | 148px | 176px | TextField 最小宽（96/120/144）+ stepper 宽 |
| stepper 宽 | 24px | 28px | 32px | 图标 12/14/16 + 两侧 6/7/8 呼吸（4px 网格半格近似，与滚动条 6px 同源） |
| stepper 按钮高（半高） | 16px | 20px | 24px | 控件高的一半 |
| chevron 图标 | 12px | 14px | 16px | 密度派生（inlineIconSize 同层，紧凑档缩一档） |
| cluster 左缘/中缝分隔线 | 1px | 1px | 1px | `color.border.default` |
| 圆角 | 4px | 6px | 8px | 视觉系统"小部件圆角"行（外缘；内缘直角） |
| 水平内边距（field 文本） | 8px | 12px | 16px | 视觉系统"水平内边距"行 |

### 9.2 组件 token（三层模型 §3.1 的 component 层）

```text
spin.field.*                  = textfield.* 全量复用（背景/边框/焦点/invalid/选区/caret）
spin.stepper.width            = 24 / 28 / 32（随 ControlDensity）
spin.stepper.iconSize         = 12 / 14 / 16（随 ControlDensity）
spin.stepper.background       = color.background.surface（与 field 同底）
spin.stepper.background.hover = surface 层 hover 派生（menu.item.background.hover 同源）
spin.stepper.background.pressed = surface/accent 0.32 混合（List pressed 同派生）
spin.stepper.separator        = color.border.default
spin.stepper.chevron          = color.content.secondary（rest）
spin.stepper.chevron.hot      = color.content.primary（hover/pressed/自动重复中）
spin.stepper.chevron.atBound  = color.disabled.content（到界淡化；按钮仍可命中）
motion.spin.repeat.delayMs    = 500
motion.spin.repeat.intervalMs = 60
```

### 9.3 状态矩阵

| 状态组合 | field | stepper | 说明 |
| --- | --- | --- | --- |
| rest | textfield rest | chevron `content.secondary` | — |
| hover（stepper） | 不变 | 背景 hover 派生 + chevron primary | 状态过渡 `stateTransitionMs`=100 |
| pressed / 自动重复中 | 不变 | 背景 pressed 派生 | 自动重复期间保持 pressed 态 |
| 编辑中（focused） | textfield focused 边框 | 不变 | caret 即焦点指示；不叠加焦点环（§5 规则 4 默认关闭） |
| invalid（文本非法） | textfield invalid 边框 | 不变 | 常驻不闪烁；Escape/失焦恢复 |
| atMin / atMax | 不变 | 对应 chevron `disabled.content` 淡化 | 仍可命中、步进顶住；语义 action 不 disabled |
| disabled（整控件） | textfield disabled | chevron/背景 `disabled.*` | 命中拒绝 + 键盘跳过 + 语义一致 |
| 高对比主题 | 边框/invalid 对比提升 | chevron 到界淡化改为**形状差异**（到界按钮 chevron 旋转 90° 平置）+ 边框 1px 包络 | 不得只靠颜色区分（视觉系统 §11） |

## 10. 动效规格

| 动效 | 时长/曲线 | 驱动 | reduceAnimation |
| --- | --- | --- | --- |
| stepper hover/pressed 背景 | `stateTransitionMs` = 100ms | 既有状态过渡通道 | 归零（即时切换） |
| **数值文本** | **无补间** | — | —（连续步进时滚动数字动画会模糊读数，与 TextField"内容不参与过渡"同口径；步进是离散节拍，节奏感来自 repeat 间隔本身） |
| **自动重复节奏** | 延迟 500ms → 间隔 60ms | 控制器计时 + FrameScheduler tick（caret blink 同驱动源） | **不归零**：这是输入行为节奏而非视觉过渡（Win32 up-down 惯例值 500/62，取整网格 500/60） |
| 到界"顶住" | 无动画 | 钳制即状态 | 位移即反馈，同 Splitter §7.1 |
| invalid 进入/退出 | 即时（颜色随状态过渡通道） | — | — |

新增 MotionTokens 两个字段（`theme.h` 尾部追加，既有值不动）：`spinRepeatDelayMs{500}`、`spinRepeatIntervalMs{60}`，注释注明"输入节奏，非视觉过渡；reduceAnimation 不归零"。

## 11. 性能与测试计划

### 11.1 性能口径

- 自动重复 60ms 间隔 → 每秒 ~16 次步进拍；重建合帧后实际布局/绘制 ≤ 显示帧率，无额外动画帧空转（M12 deadline 教训回归：计时只在按下期间武装，release 即撤）。
- 控件为小组件树（field + 2 按钮），不进基准场景。

### 11.2 测试（Catch2，`*_tests.cpp`，行为命名）

1. **值模型**：setValue 钳制/step snap/decimals 舍入；wrap 环绕边界；formatValue 精度。
2. **提交**：commitText 成功/失败两路；invalid 态进入与恢复；Escape/失焦 revert；Enter 提交触发 onCommitted。
3. **按钮**：单击 ±step；按住自动重复（tick 驱动首拍 500ms、后续 60ms）；release/Cancel 停止；到界顶住继续按住无位移无事件。
4. **键盘**：Up/Down/PgUp/PgDn/Home/End/Enter/Escape 全契约；Tab 单停靠点（stepper 不进遍历）。
5. **滚轮**：hover 档位 ±step；到界停止；不抢占滚动容器（控件外滚轮照旧）。
6. **语义**：spinbutton role/value 文本与几何一致；SetValue ≡ 键入+Enter；Increase/Decrease ≡ Up/Down；invalid flag 同步。
7. **视觉派生**：density 三档（stepper 宽/图标）、fontScale、高对比（到界形状差异）、reduceAnimation（状态过渡归零、自动重复保持）。
8. **回归**：既有全部测试与帧哈希不变（零新增 WidgetType 不触旧路径）。

### 11.3 示例与验收

- settings 新页或既有表单页补 Spin 样本（整数/小数/到界三态）。
- gallery 控件清单页补 Spin 预览。
- headless 冒烟：步进脚本逐拍 value/invalid 输出；三桌面窗口 smoke 由 CI 承担。

## 12. 实施分期

| 阶段 | 内容 | 出口条件 |
| --- | --- | --- |
| P1 值模型与按钮 | SpinController + makeSpin 组合件 + 单击/自动重复/滚轮 + 视觉（stepper/到界） + 测试 1–3/7–8 | 按住节奏正确、到界顶住；既有哈希不变 |
| P2 键盘与语义 | handleKey 转发 + 全键盘契约 + invalid/revert + SemanticsRole::SpinButton + 四层一致断言 | 四层一致全绿 |
| P3 按需评估 | prefix/suffix、wrap 之外的加速步进、wheel 开关 token | — |

## 13. 兼容与迁移

1. `makeTextField`/TextField 行为零改动（组合不改字段内部）；`WidgetType` 不动，既有帧哈希不变。
2. `spin:` key 前缀与 `list:`/`menu:` 同命名空间规则（前缀分发先例）。
3. `SemanticsRole` 尾部追加，既有 role 数值不变。
4. 键盘转发依赖"单行 TextField 不消费方向键"的现状；若被证伪，缝隙收敛为 focus 前缀咨询（§14.1），仍不引入全局加速键表。

## 14. 开放问题（实施前需确认）

1. **方向键路由层级**：onKey 应用转发（MenuBar 模式）vs 交互层 `spin:` 前缀咨询（SecondaryPressSink 模式）——实施时核对 TextField 单行路径对 Up/Down 的实际消费再定，两者成本都很低。
2. **prefix/suffix**：Qt 常用（"12 px"）；实现牵动光标偏移/选区/宽度测量。建议 P3：field 侧只读 suffix 槽（非编辑区，右对齐于 stepper 左缘），prefix 不做。
3. **滚轮改值的开关**：桌面工具惯例开放；密集表单页可能需要关闭（`spin.wheelEnabled` token 或 Widget 属性）——首版开放，遇到误触再收。
4. **自动重复的间隔曲线**：固定 60ms vs 长按加速（500ms 后 60ms，3s 后 30ms）——首版固定，节奏感不足再加。
5. **到界指示的高对比形状**：chevron 平置（旋转 90°）vs 描边包络——§9.3 先按平置写，高对比派生实测再定。

## 15. 实施状态（2026-09 追记）

P1（值模型与按钮）与 P2（键盘与语义）已实施：`include/lumen/widgets/spin.h` +
`src/widgets/spin.cpp`（`SpinController`），settings「Spin」页与 gallery
「Controls」页接入。`tests/spin_tests.cpp` 12 用例 + settings 集成冒烟全绿；
全套 ctest（Debug 699 用例）通过，Release 体积断言（+8B → 840B）通过。

与设计稿的实现差异（同提交更新本文档）：

1. **值通道**：field 文本经 StateStore bind（key + ":value"）携带——键入
   即写 store（框架既有编辑路径），控制器即时解析 invalid；Enter/失焦提
   写 `setValue`。§5 草案未明确通道，实现取 StateStore（与 Checkbox 同层）。
2. **stepper 命中**：stepper 按钮携带 onClick（指针点击要求 armed onClick
   节点，interaction.cpp 派发前置条件），因此参与 Tab 遍历——「单一 Tab 停
   靠点」调整为「field → ▲ → ▼」三点（MenuBar 栏项同口径）；§14.1 关闭。
3. **wrap 语义**：直接 `setValue` 一律钳制不环绕（GTK 口径）；wrap 只作
   用于步进通道（min 之下 → max、max 之上 → min 相邻跳转）。§5.2 更新。
4. **滚轮**：经应用 `ShellConfig.onWheel` 转发 `handleWheel`（几何命中检
   查 + |dy| ≥ 20 阈值滤触摸板微抖），零 core 改动——§2"WheelSink 改造"
   的担忧不成立，§12.4 的"键盘缝隙"同样不需要（onKey 转发即可）。
5. **invalid 边框**：画在外框行上（Row 走 textfield token 边框，focused/
   invalid 由控制器按焦点与 store 文本即时折算）；field 自身边框抑制为 0，
   `field.invalid` 只承载语义 flag。§9.3 视觉一致，绘制载体不同。
6. **语义**：`SemanticsRole::SpinButton` 已加（尾部追加，UIA
   Spinner 映射已接）；Increase/Decrease 专用 action 枚举未加——
   kActionSetValue/Focus 已覆盖语义可达，专用 action 留 M13 provider。
7. **MotionTokens**：`spinRepeatDelayMs{500}`/`spinRepeatIntervalMs{60}`
   已入 Theme；reduceAnimation **不归零**（输入节奏，§10 口径，已测试锁定）。

### 15.1 复审对齐（2026-09 第二轮）

- **stepper 视觉语言**：改用新增 `ButtonVariant::Chrome`（resolver 特判，
  WindowClose 先例）——hover 表面派生 + **前景提亮 contentPrimary**、
  pressed = **List pressed**（surface/accent 0.32 混合，`theme.list.pressed`，
  稿件 `.spin-step:active` 同值）。此前 Ghost 变体的 hover/pressed 前景不
  变、pressed 为暗色叠加，与稿件不符。
- **field-sep 分隔线**：field 与 stepper 集群之间的 1px `border.default`
  竖线补齐（§9.1 cluster 左缘；此前缺失）。row 结构 = [field(flex),
  fieldSep(1px), cluster]。
- **disabled（§9.3）**：`setEnabled(bool)` 落地——field（disabledBackground
  底色，背景抑制仅在可用时施加）+ stepper（chrome disabled）+ 键盘/滚轮
  拒绝，视觉/命中/键盘一致。
- **移出命中即停（§6.1）**：按住后指针移出（超过拖动 slop →
  `isDragging()`）自动重复停止、按住状态复位；释放不触发单击（拖动释放
  无 click），不会误步进。
