# Lumen Splitter 控件设计（Splitter 分栏调节）

> 文档状态：已实施（高级特性按需评估）（2026-09）
> 输入：源码现状盘点（`include/lumen/core/widget.h`（WidgetType/mainAxis/virtualSource 先例）、`include/lumen/core/interaction.h`（Slider 拖动锁定/ScrollDragSink/源视口框架滚动）、`src/layout/layout.cpp`（Row/Column/GridLayout））、`docs/lumen-collection-controls-design.md`（RenderNode 源指针 + 框架接管交互的先例）、`docs/lumen-self-use-roadmap.md` M3/M7/M10 完成记录、`docs/lumen-visual-system-design.md`（token 三层模型/尺度表 §3.2/状态规则 §5）。
> 配套视觉设计稿：`design/splitter.html`。
> 定位：桌面自用版控件库增强；工具类应用"树|列表|预览"双栏布局的用户可调分栏。遵循既有"Widget 不可变声明 + 应用侧控制器 + 布局期物化"架构。

---

## 1. 背景与问题

工具类桌面应用的第三高频形态是**用户可调分栏**：文件管理器（树|列表）、资产浏览器（列表|预览）、编辑器类（侧栏|主区|检查器）。源码核对后当前缺口：

| 能力 | 现状 | 缺口 |
| --- | --- | --- |
| Row/Column flex | 比例/flex 分配，窗口 resize 自动重排 | 分配是**静态声明**的——运行期无用户调节通道；无分隔条 chrome |
| Grid | 固定列数/最小列宽网格 | 同上，布局参数不可交互调节 |
| 拖动交互基建 | Slider 拖动锁定（M6）、滚动条拇指拖动（M6）、源视口拖动滚动（M10/collection d13a2ea）、`ScrollDragSink` | 拖动模式齐备但只服务既有控件；无"拖动改布局参数"的通道 |
| 状态归属 | `VirtualListController`/`ScrollController`（应用侧持有滚动状态） | 无控件持有"用户设定的分栏位置" |

结论：分栏不是渲染问题（分隔条 = 一条 1px 线 + 命中区），而是**布局参数的用户调节通道**——一个新 WidgetType（两窗格布局）+ 一个应用侧控制器（位置状态）+ 框架拖动接管（源指针先例）。

## 2. 参考框架调研

| 框架 | 控件 | 位置语义 | 最小/塌缩 | 键盘 | 对 Lumen 的启示 |
| --- | --- | --- | --- | --- | --- |
| Qt 5/6 | `QSplitter` | 尺寸列表 + stretch 因子；opaque/transparent resize | `setMinimumSize`/`setCollapsible`（可塌缩到 0） | 无内建（focus policy 可开） | 塌缩与恢复是独立能力；saveState 持久化属应用 |
| GTK4 | `GtkPaned`（双窗格）/`GtkPaned` 嵌套 | `position`（px，从首缘起算）；resize 默认保持 position | `min-position`/`max-position`；`shrink` 允许低于子项最小请求 | focusable handle，方向键 ±像素，Home/End | **位置存 px 不存比例**；handle 可聚焦（a11y 必备） |
| wxWidgets | `wxSplitterWindow` | `SplitVertically(winA, winB, sashPosition)` | min pane size + live update 开关 | 无内建 | "sash"（窗框）命名；live update 对自绘框架天然 |
| JUCE | `StretchableLayoutManager` + `StretchableObjectResizer` | **权重**（itemsSizes 按权重分摊） | 最小像素 | 无 | 权重模型的弹性；但用户感知仍是像素位置 |
| Windows 惯例 | Explorer 等 | 保持一侧固定宽度、另一侧吃掉 resize 增量 | 窗格最小可读宽度 | 无统一 | keep-offset 是文件管理器直觉；双击分隔条复位 |

**采纳的共同事实**：

1. **位置存绝对像素（从首缘起算），不存比例**：比例在 min 钳制与 resize 下漂移不可控；GTK `position`、wx `sashPosition`、Windows 惯例一致。派生比例仅作只读查询。
2. **每个窗格有最小尺寸**：低于最小即"到头"（分隔条顶住）；默认值保证窗格可用（不可读的 0px 窗格没有意义）。
3. **分隔条是可交互、可聚焦的独立部件**：拖动（直接跟手）、双击复位（Windows 惯例）、键盘方向键微调（a11y；GTK 先例）。
4. **两窗格为原子单元，多窗格用嵌套组合**：GtkPaned/QSplitter 之外的通用结论——嵌套天然表达"树|列表|预览"且每层状态独立。
5. **持久化是应用职责**：Qt saveState、IDE 布局记忆都由宿主应用做；框架只提供可读写位置。

**采纳的 Lumen 本土事实**（决定不做的事）：

1. 拖动接管走**框架路径**（RenderNode 源指针 + `InteractionController` 识别），不走应用 `onScrollDrag` 转发——collection（d13a2ea）刚把源视口滚动收口为框架路径，遗漏路由的教训已付过学费。
2. 自绘框架布局便宜，**直接跟手重排**（live resize）——Qt 的 transparent/preview 模式是为原生控件重排贵而设计，不适用。
3. 状态在应用侧控制器（与 `VirtualListController`/`ScrollController` 同层），Widget 只携带声明——M7 体积预算不因运行态膨胀。

## 3. 设计目标与非目标

**目标**

1. 新 WidgetType `Splitter`：两窗格 + 内建分隔条 chrome，水平（左右）/垂直（上下）两方向；嵌套组合多窗格。
2. 应用侧 `SplitterController`：持有位置 px 与两窗格最小尺寸；拖动/键盘/双击三种调节通道汇入同一 `setOffset`。
3. 框架级拖动接管：命中分隔条 → 拖动直接改控制器位置 → 请求重建，单帧跟手；无应用侧路由样板。
4. 分隔条可聚焦、可键盘调节、语义可达（M5 出口条件延伸：视觉/键盘/指针/语义四层一致）。
5. 窗口 resize 后位置语义稳定（keep-offset 钳制）；全部新路径 headless 可测。

**非目标（第一版明确不做）**

- 窗格塌缩到 0 与展开恢复（chevron/箭头 grip）——独立能力，留按需评估（§14）。
- 三窗格以上单控件（嵌套组合表达）；Qt 式 stretch 权重组调节。
- 位置持久化策略（应用读写控制器；无框架级布局记忆文件）。
- 分隔条拖动的预览/ghost 模式（直接跟手）；自动隐藏窗格。
- 水平+垂直混合的单控件（两个方向各一个 Splitter，嵌套即可）。
- 触摸专有手势（拖动语义与鼠标一致，桌面触摸直接可用）。

## 4. 总体架构

```text
┌───────────────────────────────────────────────────────────┐
│ 应用层：makeSplitter(key, &controller, childA, childB)     │
│   持有 SplitterController；按需持久化 offset               │
├───────────────────────────────────────────────────────────┤
│ widgets 层（新文件 include/lumen/widgets/splitter.h）      │
│   SplitterController（位置 px / 最小尺寸 / 复位目标 /      │
│     resize 行为；setOffset 钳制 + 请求重建）                │
├───────────────────────────────────────────────────────────┤
│ core 层：WidgetType::Splitter + splitterSource 指针        │
│   （virtualSource 同模式；RenderNode 复制携带）             │
│   InteractionController：分隔条拖动/键盘接管（框架路径）    │
├───────────────────────────────────────────────────────────┤
│ layout 层：layoutSplitter（新，~60 行）                    │
│   两窗格 + 分隔条布局；min 钳制；嵌套递归                   │
└───────────────────────────────────────────────────────────┘
```

核心架构决策：**分隔条不是应用构建的子 Widget，而是布局期物化的框架 chrome**（与 TreeList 粘性表头、集合行 Button 包装同一先例）——分隔条节点由 `layoutSplitter` 生成，key 固定前缀 `split:div:<ownerKey>`，交互层按前缀 + 源指针识别。应用只提供两个窗格子树。

## 5. Widget 声明与控制器

### 5.1 Widget / 构建器

```cpp
enum class WidgetType { ..., Splitter };  // 新增一个值

// 两窗格分栏：children 恰好 2 个（首 = leading，次 = trailing）。
// source = SplitterController 指针（应用持有生命周期；virtualSource
// 同契约）。orientation 经 packed bool splitterHorizontal 表达。
inline core::Widget makeSplitter(const void* source,
                                 core::Widget leading,
                                 core::Widget trailing,
                                 bool horizontal = true,
                                 std::string key = {},
                                 float initialOffsetPx = 240.0F);
```

- `initialOffsetPx` 仅在控制器首次布局时播种（控制器已有值时忽略）；复位（双击/`reset()`）回到该值。
- `isScrollableWidget` 不纳入 Splitter（不滚动，窗格自身可为任意滚动控件）。

### 5.2 SplitterController（widgets 层）

```cpp
class SplitterController {
  public:
    // 位置：分隔条 leading 缘到容器 leading 缘的距离（逻辑 px）。
    // setOffset 内部钳制到 [minLeading, extent - thickness - minTrailing]。
    void setOffset(float px);
    [[nodiscard]] float offset() const;
    [[nodiscard]] float ratio() const;          // 派生只读：offset / 可用宽

    // 窗格最小尺寸（逻辑 px；默认 48 = 默认密度下可读最小值）。
    void setMinLeading(float px);               // 默认 48
    void setMinTrailing(float px);              // 默认 48

    // 复位：回到 makeSplitter 声明的 initialOffsetPx。
    void reset();
    void setResetOffset(float px);              // 应用动态改复位目标

    // 布局期回填（框架调用；应用只读）。
    [[nodiscard]] float lastExtent() const;     // 最近一次布局可用长度

    // 窗口 resize 行为（首版只做 KeepOffset）。
    enum class ResizeBehavior { KeepOffset, KeepRatio };
    void setResizeBehavior(ResizeBehavior behavior);   // KeepRatio 留 §14

    // 位置变化回调（拖动逐拍/键盘步进/复位都会触发；UI 线程）。
    std::function<void(float offsetPx)> onOffsetChanged{};
};
```

**位置语义（GTK position 模式）**：

- 存储：绝对 px，不随窗口宽度重算（KeepOffset）；窗口变窄时钳制到合法区间，变宽时保持原值（trailing 吃掉增量——文件管理器直觉）。
- 播种：首次布局用 `initialOffsetPx`；此后 `setOffset` 与布局回填驱动。
- 派生：`ratio()` 只作展示（如语义 value 百分比），不参与存储。

### 5.3 语义与体积预算

| 字段 | 类型 | 预算 |
| --- | --- | --- |
| `splitterSource` | `const void*`（应用持有；themeOverride 同模式） | +8B |
| `splitterHorizontal` | packed bool | 0（packed 区） |

Release `sizeof(Widget)` 816 → **824**（M7 口径；Debug 分档同 §9.3 集合控件规则）。体积静态断言同步 bump。

## 6. 布局算法（layout 层）

```text
layoutSplitter(node, constraints):
  t   = SplitterTokens.thickness            // 分隔条轨道厚（§9.2：6px）
  L   = clamp(controller.offset(), minLeading, W - t - minTrailing)
  // W = 主轴可用长；不足防护：W - t < minLeading + minTrailing 时
  // 两窗格按最小值比例压缩（极端窄窗不崩、不重叠）。
  leading  = tight 交叉轴 × 精确 L（主轴）
  divider  = 分隔条节点（key = "split:div:<ownerKey>"，
             t × 交叉轴满，携带源指针回链）
  trailing = tight 交叉轴 × 精确 (W - t - L)
  // 嵌套：窗格是任意 Widget（含另一个 Splitter / 集合控件 /
  // ScrollView），约束递归传播自然发生。
```

- 两窗格主轴**精确尺寸**（非 loose）：拖动逐拍重排时窗格内容按新约束重排——窗格内 VirtualList/List 保持 O(visible)（M3 口径），无全量重建。
- 布局末尾回填 `controller.lastExtent` 与钳制后的 `offset`（同帧一致，二次布局收敛）。
- 交叉轴两窗格 tight 满高（splitter 即视口语义）；窗格内容自身用对齐属性处理富余。

## 7. 交互契约

### 7.1 拖动（框架接管）

- **命中**：`InteractionController::pointerDown` 命中链含 `split:div:` 前缀节点且其 `splitterSource` 非空 → 进入分隔条拖动模式（Slider 拖动锁定同层；不与滚动拖动/文本选区/Slider 抢占——命中即独占）。
- **逐拍**：pointerMove → 主轴坐标 − 拖动起点 + 拖动起点 offset → `controller.setOffset`（内部钳制）→ `setRebuildRequest` 请求重建 → 下一帧重排（单帧延迟，自绘布局实测 ≤ 1ms/百节点，直接跟手）。
- **结束**：pointerUp/Cancel 退出拖动模式；边界处继续钳制（分隔条"顶住"手感）。
- 无 slop 阈值（分隔条命中即意图明确，与 Slider 一致）；`reduceAnimation` 无关（无过渡，位置即状态）。

### 7.2 键盘（分隔条拥有焦点时）

| 键 | 行为 |
| --- | --- |
| Left / Up（水平/垂直） | offset −16px（钳制到 minLeading） |
| Right / Down | offset +16px（钳制到 max） |
| Home | offset = minLeading（trailing 最大） |
| End | offset = max（trailing = minTrailing） |
| Enter / Space | 无操作（非命令控件；预留塌缩切换 §14） |
| Tab / Shift+Tab | 焦点离开（FocusManager 既有遍历；分隔条是普通可聚焦节点） |

- 步进 16px（4px 网格 ×4；Medium 档）；Small/Large 密度各 12/24px（§9.2 尺度表）。
- 方向键始终按**视觉方向**移动分隔条（水平 Splitter 用 Left/Right，垂直用 Up/Down；不按内容语义翻转——RTL 布局留 §14）。

### 7.3 双击复位

分隔条节点注册双击检测（`DoubleClickSink`，集合控件 onActivated 同通道）：双击 → `controller.reset()`。Windows 惯例；复位目标 = `initialOffsetPx`/`setResetOffset`。

### 7.4 指针状态与命中区

- 命中区 = 轨道厚 `t`（6px）与 `hitExtent`（§9.2：12/16/24px）取大——命中区透明扩展，视觉线永远 1–3px。
- hover 显示抓握示意（线加粗 + 颜色提升）；拖动中保持加粗；光标形状经既有 `SystemCursor`（水平 ↔ ResizeEW / 垂直 ↕ ResizeNS——M4 光标契约已含箭头族；缺的形状走能力降级）。
- 焦点环内嵌绘制（V2 damage 不变量：不越出节点矩形）。

## 8. 语义契约

| 节点 | role | 状态/动作 |
| --- | --- | --- |
| Splitter 容器 | 既有 `Group` 复用（或追加 `Splitter` role，§14 开放问题） | 子序 = leading、divider、trailing |
| 分隔条 | `slider` 语义近似 or 新 role `separator` | label = "分栏调节"（应用可覆盖 semanticsLabel）；value = 百分比文本（如 "42%"）；SetValue action ≡ 键盘方向键（钳制同源） |

四层一致性断言（headless 验收）：拖动 ≡ 键盘 ±16px ≡ 语义 SetValue 到达同一 `setOffset`；min/max 钳制在指针/键盘/语义三路同值；value 文本与几何一致。

## 9. 视觉规格（详见 design/splitter.html）

视觉契约遵循 `docs/lumen-visual-system-design.md`（token 三层模型 §3.1、尺度表 §3.2、状态规则 §5），只做部件级映射，**不新增颜色槽位**。

### 9.1 尺度（视觉系统 §3.2 对齐）

| 项目 | Small/Compact | Medium/Comfortable | Large/Touch | 依据 |
| --- | --- | --- | --- | --- |
| 轨道厚度 t | 6px | 6px | 6px | 视觉线 + 呼吸间隙；4px 网格半格对齐（0.5 例外说明：6 = 4 + 2，与滚动条 thumb 同源） |
| 命中区 hitExtent | 12px | 16px | 24px | chevron 命中区（collection §10.1）同档；指针精度随密度 |
| 视觉线宽 rest | 1px | 1px | 1px | `color.border.strong`；与分隔线语言一致 |
| 视觉线宽 hover/drag | 3px | 3px | 3px | accent 色提升 + 加粗（形状差异，HC 不靠颜色 §11） |
| 键盘步进 | 12px | 16px | 24px | 与 hitExtent 同值（手感一致） |
| 默认窗格最小 | 48px | 48px | 48px | 可读下限；应用可调（controller） |

### 9.2 组件 token（三层模型 §3.1 的 component 层）

```text
splitter.track.thickness    = 6
splitter.hitExtent          = 12 / 16 / 24（随 ControlDensity）
splitter.line.color         = color.border.strong（rest）
splitter.line.color.hover   = color.accent（hover/drag/focused 同色）
splitter.line.width         = 1（rest）/ 3（hover/drag/focused）
splitter.divider.focusRing  = color.focus.ring + focusWidth（内嵌）
```

### 9.3 状态矩阵

| 状态 | 视觉 | 命中区/焦点 |
| --- | --- | --- |
| rest | 1px `border.strong` 线居中于轨道 | 命中区 = max(t, hitExtent) |
| hover | 3px accent 线 | 光标 ResizeEW/NS（能力可用时） |
| drag | 3px accent 线 + 两窗格实时重排 | 逐拍 setOffset + 重建 |
| focused（键盘） | 3px accent 线 + 内嵌 focus ring（分隔条为框架物化 chrome，布局期恒 `showFocusRing=true`——3px 线本身由 focusWidth>0 驱动，关环会让键盘聚焦退回 1px rest 线不可见；visual-system §6.1 键盘表面口径） | 方向键 ±步进；Home/End |
| 窗格低于最小 | 分隔条顶住（钳制） | 继续拖动无位移 |
| 高对比主题 | 线宽提升 + 形状差异（1→3px 变化保留） | 不得只靠颜色（§11） |

## 10. 性能与测试计划

### 10.1 性能口径

- 布局增量：`layoutSplitter` O(1) + 两窗格递归（与一次普通重排同数量级）。
- 拖动逐拍 = 重建 + 布局 + damage 帧率要求：窗格内千项 VirtualList 嵌套场景 p95 相对静止帧恶化 ≤ 10%（O(visible) 物化不因窗格变宽退化）。
- 基准场景：`splitter-list`（Splitter(List 千项, 预览面板) 拖动 120 帧录制）进 `lumen-scene-bench`（对齐 M7 口径归档）。

### 10.2 测试（Catch2，`*_tests.cpp`，行为命名）

1. **布局**：offset 精确分配两窗格、min 钳制（两侧）、极端窄窗比例压缩不重叠、嵌套（水平套垂直）几何、窗口 resize 后 keep-offset 钳制、initialOffset 播种只发生一次。
2. **拖动**：命中 `split:div:` 进入拖动、逐拍 setOffset + 单帧重排、边界顶住、pointerCancel 复位干净、不抢占 Slider/滚动拖动/文本选区（既有回归）。
3. **键盘**：±16px 步进、Home/End、Tab 聚焦时 3px accent 线可见（focusWidth>0 驱动）、Tab 离开。
4. **双击**：reset 回 initialOffset；双击不与单击路径冲突。
5. **语义**：value 百分比文本随几何同步、SetValue ≡ 键盘、四层一致性断言。
6. **回归**：既有全部测试与帧哈希不变（新 WidgetType 不触旧路径）；Widget 体积静态断言 bump 到 824。
7. **视觉派生**：density 三档（12/16/24 命中/步进）、fontScale、高对比（HC 线宽/形状差异）、reduceAnimation 无动画帧。

### 10.3 示例与验收

- settings 新页 `Splitter`：树|列表（ListController 真数据）+ 垂直嵌套（列表|详情），演示拖动/键盘/双击复位。
- gallery Overview 布局预览区补 Splitter 样本。
- headless 冒烟：拖动脚本逐拍 offset/几何输出；三桌面窗口 smoke 由 CI 承担。

## 11. 实施分期

| 阶段 | 内容 | 出口条件 |
| --- | --- | --- |
| P1 布局与拖动 | WidgetType::Splitter + layoutSplitter + SplitterController + 框架拖动接管 + 视觉（线/hover/drag）+ 布局/拖动测试 + settings 演示页 | 拖动跟手、min 钳制、嵌套正确；既有哈希不变 |
| P2 键盘与语义 | 分隔条可聚焦 + 方向键/Home/End + 双击复位 + 语义 value/SetValue + 光标形状 | 四层一致断言全绿；体积断言 824 |
| P3 收口 | `.lumen` `splitter` 节点（source C++ 侧装配，与 VirtualList 同规）+ DSL builder + 基准场景 `splitter-list` 归档 | 全部测试绿；基线归档；路线图状态更新 |

## 12. 兼容与迁移

1. Row/Column/Grid 行为零改动（新 WidgetType 独立路径）；`isScrollableWidget` 不含 Splitter。
2. `splitterSource` 裸指针生命周期由应用保证（themeOverride/virtualSource 同契约；M7 已确立裸指针 + 应用保活模式）。
3. 拖动接管优先级：`split:div:` 命中即独占，先于源视口滚动/Slider/选区判定（命中目标互斥，无仲裁歧义）；M10 拖动回归用例（`slider_drag_inside_scroll_view_still_sets_value`）必须保持绿。
4. 新增 RenderCommand：无（1px/3px 线 = 既有表面/边框命令）；序列化版本不动。
5. `SystemCursor` 无 ResizeEW/NS 形状的平台按既有能力降级（默认箭头，能力报告如实）。

## 13. 开放问题（实施前需确认）

1. **Splitter 容器 role**：Group 复用（最小改动）vs 追加 `Splitter` role（语义更准，枚举尾部追加零风险）——建议追加。
2. **KeepRatio resize 行为**：默认 KeepOffset 已定；KeepRatio（窗口按比例分摊）是否值得双行为维护——建议 P1 只做 KeepOffset，KeepRatio 留按需。
3. **窗格塌缩**：双击窗格内 chevron 或分隔条拖过 min 即塌缩（Qt collapsible 模式）与"顶住"互斥；塌缩后键盘/语义如何展开——建议独立后续增强，首版顶住。
4. **RTL 布局**：Right/Left 键是否随阅读方向翻转（UAX#9 子集已入 M1，但布局镜像未做）——首版不翻转，随 RTL 布局能力统一评估。
5. **offset 持久化约定**：是否提供 `serializeState()`/`loadState()` 便捷（纯值拷贝，无 IO）——低成本可选项，实施时定。
