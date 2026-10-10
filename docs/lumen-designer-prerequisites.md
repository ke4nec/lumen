# Lumen 设计器前置条件与缺口分析

> 文档状态：决策记录与实施基线（2026-10-02）
> 定位：登记「可视化设计器」的能力基线、结构性缺口、前置工程与待决策项，作为设计器立项前的评审入口。
> 边界：本文不改变任何既有排除项口径（含「DSL 可编程化」，见 §4.3）。用户已于 2026-10-01 采用本文推荐的 DP-1–DP-9 方案；D3 按已决定的格式、覆盖、运行态、排期、保真度、兼容、权限、性能和工程容器边界实施。
> 证据基线：2026-09-30 源码核对（HEAD `e2c61ef`）；后续提交的实现证据以本文 §10.3 和支持矩阵为准。[`support-matrix.md`](support-matrix.md) 记录的 `3fede85` / 935 项与 [`M15+ 路线图`](lumen-m15-roadmap.md) R6 首批的 940 项是不同历史批次。规划 API 不计入已实现能力。

## 1. 背景与形态定义

### 1.1 盘点结论（2026-09-30）

对照「可视化设计器」所需能力逐项核对源码后结论如下：

- 框架层（控件库、离屏渲染、命中测试、主题/状态预览、命令分发、多窗口/IME/DPI）已经具备承载**只读预览工作台**的主要地基；运行时 Inspector、bounds/damage overlay 和帧统计已有 headless 证据，三桌面现场证据仍待补齐；
- **可编辑并保存的双向设计器**此前被三类结构性问题阻塞：声明式文档模型与双向转换缺失（G-D1）、属性/节点 schema 缺失（G-D2）、声明式文档与运行时 Widget/controller 的边界和格式治理未冻结（G-D3、G-D8）；P1–P5 已完成，DP-1–DP-9 已冻结，D3 L0 应用出口已有 headless 证据，剩余工作主要是平台证据和覆盖矩阵增量。
- 稳定编辑身份、源位置映射、版本迁移、绑定/资源注入和文档级事务也没有现成契约。这些不是单纯的 UI 面板工作，而是 D3 的前置条件。

本文将上述缺口登记为可评审、可验收的前置工程；DP-1–DP-9 的结论和日期记录于 §8，后续只保留实现验收和平台证据。

### 1.2 设计器三形态

后文所有缺口与阶段均以下表三形态表述。「设计器」一词单独出现时指三者总称。

| 形态 | 定义 | 对 UI 文档的权限 | 前置条件 |
| --- | --- | --- | --- |
| D1 检查器 | 运行中应用的只读诊断：树可视化、bounds/damage overlay、帧统计 | 无（观测运行时树） | tree/style dump、Inspector、bounds/damage overlay 和帧统计已有；三桌面现场证据仍待补齐 |
| D2 预览工作台 | 打开 `.lumen`/应用提供的示例，画布渲染 + 结构浏览 + 环境/状态预览 | 只读 | 基础运行时预览可独立交付；文档定位版需要 P1 读取/编译子集、P3 预览适配与 P4 映射 |
| D3 可编辑设计器 | 文档编辑：工具箱拖入、属性就地编辑、结构重排、保存/导出 | 读写 | DP-1–DP-9 已决定 + P1–P5 + 编辑命令栈 |

与既有规划的关系：D1 的范围与 R6（开发者诊断）的已交付调试层重合，属
[`lumen-gui-completion-plan.md`](lumen-gui-completion-plan.md) 阶段 D 的收口，不需要新编号；
D2/D3 已立项，完成计划缺口矩阵和支持矩阵分别以 R11 登记 D3 L0 的四态；DP-2、DP-5–DP-8 已冻结方案，按实现证据和平台范围管理。

### 1.3 阅读约定与分析方法

本文把“已经能调用”与“已经可以交付”分开记录。能力状态沿用完成计划的四态：

| 状态 | 这里能证明什么 | 不能据此推出什么 |
| --- | --- | --- |
| 接口已存在 | 公共契约、Fake/Recording 路径和线程归属已经明确 | 真实窗口、系统服务或发布包可用 |
| headless 已验证 | 无窗口行为、错误路径和确定性输出有自动化证据 | 三桌面输入、字体、合成器和辅助技术已经通过 |
| 真实平台已验证 | 指定桌面上的窗口、输入、资源或系统服务有现场证据 | 其他桌面或干净机器发布链条通过 |
| 可发布 | 干净机器可安装、启动、使用并能追溯版本 | 设计器已覆盖所有控件或支持任意 C++ builder |

“G-Dx”表示缺口，“Px”表示前置工程，“DP-x”表示必须由产品/架构负责人明确的
决策。缺口只登记设计器需要的契约；已有框架能力不因为设计器出现而重复立项。本文的
盘点顺序是：先核对源码和公共头文件，再核对测试、完成计划和支持矩阵，最后把结论写成
可验收的接口、失败路径和出口条件。源码证据只能证明当前提交的事实，不能替代三桌面
真实平台验收。

本文的范围继续受仓库顶层 `AGENTS.md` 约束：目标是 Windows/Linux/macOS 桌面，移动端
保持冻结；设计器文档格式属于数据序列化，不能引入脚本、变量、条件、循环或其他 DSL
可编程能力。任何改变冻结节点集或移动端范围的提议都必须另行获得明确决策并同步相关
设计文档。

### 1.4 设计器的最小用户任务与成功定义

三种形态应以用户任务而不是面板数量验收。下表中的“成功”同时要求正常路径和错误路径
可观察、可恢复，并且不把运行时状态误写回文档。

| 任务 | 适用形态 | 成功定义 |
| --- | --- | --- |
| 定位运行时问题 | D1 | 从树或画布选中节点，看到 bounds/damage/frame 读数，并能回到对应的运行时节点 |
| 比较声明与视觉结果 | D2 | 打开文档，在主题、密度、DPI、字体缩放和交互状态之间切换；错误时仍保留上一份有效画面 |
| 修改一个属性 | D3 | 属性校验失败不产生半提交状态；成功修改可撤销、重做、保存并重开一致 |
| 修改结构 | D3 | 插入、删除、复制、重排后选择、诊断和源位置仍指向正确的 `DocumentId` |
| 使用应用数据预览 | D2/D3 | 业务数据源可以被替身或快照取代；缺失引用显示诊断，不启动网络、数据库或任意业务代码 |
| 交付文档 | D3 | 文档格式、版本、资源引用和迁移结果可追溯；保存中断不会破坏最后一份有效文件 |

### 1.5 总体数据流与所有权边界

设计器的核心边界是“DOM 是编辑源，Widget 是编译产物，RenderNode 是观测结果”。下图
把现有能力和待建能力放在同一条链上；虚线表示会话预览输入，所有新名称均为候选设计。

```mermaid
flowchart LR
    A["文档文件<br/>.lumen 或设计器格式"] --> B["导入器 / DocumentStore<br/>读取/校验/迁移"]
    B --> C["DesignDocument<br/>DOM + DocumentId"]
    E["编辑命令<br/>事务/撤销/重做"] --> C
    C --> D["Schema validator<br/>属性与结构约束"]
    D --> F["编译器<br/>DOM → Widget"]
    R["RuntimeContext<br/>资源/source/组合件适配"] --> F
    F --> G["AppShell<br/>swapRoot + applyBinds"]
    G --> H["Element/Layout<br/>RenderNode"]
    H --> I[画布/树/样式/语义 dump]
    F --> J["SourceMap/CompileTrace<br/>选择/错误/overlay"]
    J --> I
    K["Preview overrides<br/>主题/DPI/hover/focus"] -.-> G
    L["独立预览状态与 handler 替身"] -.-> G
    C --> S["codec → DocumentStore<br/>保存声明数据"]
```

所有权规则如下：`DocumentStore` 只拥有文件读写；`DesignDocument` 只拥有声明数据；
`RuntimeContext` 提供资源和 controller 的解析结果；预览会话必须为整个运行树寿命保活这些
对象（§4.11），`AppShell` 拥有状态、handler 注册表和 Widget/Element 生命周期；设计器 overlay
绘制辅助信息，输入路由由会话显式管理（§5）。任何跨边界的裸指针、
业务回调、布局结果或动态物化行都不能成为文档持久化字段。

## 2. 已具备的能力基线（不重新立项）

以下能力已实现并有测试/文档证据，设计器直接消费；只在发现回归时补验。

| 能力 | 设计器用途 | 证据 |
| --- | --- | --- |
| 离屏像素画布 | 设计器画布直接取 RGBA 帧；确定性 hash 支撑快照对照 | `include/lumen/render/cpu_renderer.h`（`pixels()`，纯内存 `PixelBuffer`）；`AppShell::renderFrame`/`paintFrame`（[`include/lumen/app/app_shell.h`](../include/lumen/app/app_shell.h)） |
| 命中链选择 | 画布点选 = 最深命中节点 + 祖先链 | `core::hitTestChain`（`include/lumen/core/interaction.h`） |
| 树遍历与定位 | 大纲面板、滚动到节点、overlay 锚定 | `findNodeByIdentity`/`findNodeByKey`/`absoluteOffset`（`include/lumen/core/render_node.h`）；`AppShell::root()` |
| 结构文本导出 | 大纲/对照工具链地基 | `app::dumpRenderTree`/`dumpStyleTree`/`dumpSemanticsTree`（`include/lumen/app/tree_dump.h`）；`AppShell::buildSemanticsSnapshot()`；settings/Gallery 三个 dump 入口 |
| 交互状态预览 | 按 key 预览 hover/press/focus 视觉；DesignerApp 为无 key、重复 key 和私有 key 碰撞节点生成确定且唯一的会话 key | `AppShell::setVisualPreviewState(key, state)`；`src/style/resolver.cpp` 的 `stateOf` |
| 环境模拟 | 主题方向/明暗、密度、字体缩放、高对比、减少动画、DPI 的实时切换预览 | `style::Theme`（`dark()`/`light()`/`fromSettings()`、`ControlDensity`）；Gallery CLI 开关（`--dpi/--density/--light/--high-contrast/--font-scale` 等） |
| 面板控件地基 | 属性面板（TextField/Spin/ComboBox/ColorPicker/Slider/Form）、大纲（Tree/TreeList）、工具箱/属性表（DataGrid/ToolBar/StatusBar）、布局（Splitter） | `include/lumen/widgets/*.h`；这些是设计器可复用的面板部件，不等于已经存在 Widget 属性反射 |
| overlay 机制 | 高亮、ghost 和辅助线的基础；两类 overlay 共用一个槽位，命中也会切树 | `setOverlayBuilder` / `setVisualOverlayBuilder`（`app_shell.h`）；限制与绕行见 §5 |
| 命令与快捷键分发 | 设计器快捷键单一数据源 | `include/lumen/app/command_registry.h` |
| 撤销栈先例 | 文档级 undo/redo 的事务模型参考 | `include/lumen/text/editing_history.h`（TextField 编辑事务） |
| 持久化 | 设计器设置与临时文件替换的参考；文档耐久保存另建契约 | `include/lumen/core/preferences.h`；`src/core/preferences.cpp`（没有文件/目录同步或迁移链） |
| 热重载联动 | 编辑文本 → 画布即时刷新、错误保持旧 UI | `dsl::DslCache` + counter `--watch` + `AppShell::swapRoot` |
| 多窗口与输入 | 独立预览窗口、IME 编辑属性值、DPI 正确 | `runApp` 多窗口（每窗口独立 shell/renderer/IME） |
| 应用内拖拽 | 工具箱拖入画布、结构重排 | M15 拖放会话状态机（`lumen-drag-drop-design.md`） |
| 帧统计 HUD | D1 性能读数 | `include/lumen/app/frame_debug.h` + `RunOptions.frameDebugOverlay`；默认关闭、无额外帧 |

### 2.1 已有能力的状态边界

“设计器直接消费”不等于“设计器功能已经交付”。当前证据按设计器最关心的消费方式重新
归类如下；日期和测试数字以 [`support-matrix.md`](support-matrix.md) 的记录为准，本文不
复制一份会随每次提交漂移的完整计数。

| 消费能力 | 当前状态 | D1/D2/D3 可直接依赖的部分 | 仍需补的证据或契约 |
| --- | --- | --- | --- |
| Render/style/semantics dump | 接口已存在 + headless 已验证 | 只读树、样式和语义对照、CI 诊断 | 三桌面 inspector smoke、平台现场辅助技术验收 |
| CPU 离屏画布与 frame hash | headless 已验证 | 像素输出、固定环境的 golden 对照、失败保留旧帧、文档映射和 zoom/DPI 交互 | 三桌面 renderer/alpha 现场验收 |
| 命中测试与 overlay | 接口已存在 + headless 已验证 | 最深命中链、Inspector 钉住、bounds/damage 调试层、动态行回选源节点、zoom/pan 与 DPI 变换、非模态辅助层、拖拽 ghost；画布根节点框选和大纲多选已接通 | 三桌面指针/DPI 现场验收 |
| Theme/状态/DPI 预览 | 接口已存在 + headless 已验证 | 环境切换、视觉状态覆盖、预览值与声明值隔离、dirty/undo 不污染 | 三桌面高对比、字体缩放和辅助技术现场验收 |
| 拖放与命令分发 | 接口已存在 + headless 已验证 | 应用内工具箱拖入和结构重排、原子事务、失败回滚和多选语义 | 三桌面 OS 拖入和触摸边界现场验收 |
| StateStore/HandlerRegistry | 接口已存在 + headless 已验证 | 应用运行时绑定、事件名称解析、preview context、引用类型校验和缺失引用占位 | 真实业务适配器不属于离线设计器前置范围 |
| Preferences/原子写先例 | headless 已验证 | DocumentStore 的 schema 版本、迁移、未知字段、原子保存和恢复副本 | 三桌面文件系统/安装链现场验收 |
| 真实平台窗口/IME/辅助技术 | 部分真实平台证据 | 设计器 UI 可沿用 AppShell/IME/语义接口 | 三桌面窗口 smoke、屏幕阅读器和高 DPI 现场验收 |

因此，D1 随 R6 调试层实现收口；D2 的文档定位版需 P1/P4 才能把选择指回文档；D3 必须等 P1–P5
和文档命令事务完成。这个划分避免把已有的 `dump` 或 HUD 误写成设计器已经可用。

## 3. 缺口总览

| ID | 缺口 | 阻塞形态 | 定性 | 处理 |
| --- | --- | --- | --- | --- |
| G-D1 | 文档表示与序列化双向缺失 | D3 | 结构性（阻塞） | 前置工程 P1（§4.1） |
| G-D2 | 属性元数据/反射缺失 | D3（D2 可降级） | 结构性（阻塞） | 前置工程 P2（§4.2） |
| G-D3 | 文档格式选型触及「冻结节点集 + DSL 可编程化排除」口径 | D3 决策前置 | 治理（决策） | 决策点 DP-1（§4.3） |
| G-D8 | 声明式文档与运行时 Widget/controller/resource 边界未定义 | D2/D3 | 结构性（阻塞） | 前置工程 P3（§4.4） |
| G-D9 | 设计节点没有独立的稳定身份和源码映射 | D2/D3 | 结构性（阻塞） | 前置工程 P4（§4.5） |
| G-D10 | 文档版本、迁移、未知字段和损坏恢复契约缺失 | D3 | 结构性（阻塞） | 前置工程 P5（§4.6） |
| G-D11 | 绑定、命令、主题、资源引用没有设计器侧解析/注入协议 | D2/D3 | 结构性（阻塞） | P3 + 应用适配层（§4.4） |
| G-D12 | 预览状态和文档声明状态没有隔离协议 | D2/D3 | 行为风险 | §4.7；禁止把运行时状态写回文档 |
| G-D13 | 文档编辑命令的事务、合并、撤销和保存状态未定义 | D3 | 设计器结构性（阻塞） | §4.14；设计器应用工程，不新增框架核心工作项 |
| G-D14 | 多选、坐标变换和动态节点选择语义未定义 | D2/D3 | 交互契约（阻塞） | §4.15；基于现有 hit test/overlay 扩展 |
| G-D15 | 解析、schema、引用、编译和保存错误没有统一诊断协议 | D2/D3 文档流程 | 质量契约（阻塞） | §4.16；适配 `DslError`，不阻塞 D1 的运行时诊断 |
| G-D16 | 外部资源、文档信任边界和异步生命周期未定义 | D2/D3 | 生命周期/安全风险 | §4.17；RuntimeContext 与 DocumentStore 共同约束 |
| G-D4 | 无通用绝对定位容器（仅 Stack 子级 `stackPosition`） | D3 自由画布 | 可绕过 | §5 |
| G-D5 | 无自绘光标（仅 11 种系统形状） | D3 编辑体验 | 可降级 | §5 |
| G-D6 | OS 拖出结构化不可用（SDL 3.2.10） | 边缘场景 | 已登记，维持 | §5 |
| G-D7 | 无 Canvas 类自绘面板控件，overlay 共槽位 | D1/D2/D3 辅助层 | 可绕过或立项 | §5 |

另有一类**设计器自建能力**（非框架缺口，框架已有地基）：文档级编辑命令栈
（undo/redo，基于 CommandRegistry + 编辑事务先例）、编辑选择模型（单选/多选/框选，
框架 `SelectionModel` 是集合控件语义，不直接复用）、吸附/对齐规则引擎，以及设计器
自己的诊断面板和资源适配层。它们属于设计器应用实现，登记于 §6 阶段范围，不开框架工作项；
但 G-D13–G-D16 仍是 D3 的进入条件，必须建立在 P1–P5 定义的文档事务、节点身份和引用
解析契约之上。

## 4. 核心缺口

### 4.1 G-D1：文档表示与序列化

#### 现状与证据

1. **解析是单向的，导出完全缺失。** `.lumen` 只能 `parseLumen/parseLumenFile →
   DslParseResult{Widget root, optional<DslError>}`（`include/lumen/dsl/text_dsl.h`）；
   `src/dsl/` 与 `include/lumen/dsl/` 中不存在任何 Widget→文本的序列化函数。私有
   `AstNode` 的属性顺序、注释、空白和源位置在转换成 `Widget` 后也无法恢复。
2. **文本 DSL 节点集冻结在 12 个基础节点。** `src/dsl/text_dsl.cpp` 的 `widgetTypeOf`
   只认：`Container`、`Row`、`Column`、`Stack`、`Text`、`Button`、`TextField`、
   `ScrollView`、`ListView`、`Checkbox`、`Switch`、`FocusScope`。这是当前 parser 的
   allowlist，不是整个框架的控件总数。`WidgetType` 枚举目前有 27 个条目；其余包括
   `Grid`、`Image`、`VirtualList`、`Icon`、`Slider`、`ProgressBar`、`Radio`、
   `Tooltip`、`Dropdown`、`Tabs`、`ThemeScope`、`List`、`Tree`、`TreeList`、
   `Splitter`，都没有对应的文本节点转换路径。
3. **Widget 不是纯声明式文档。** 除了布局、文本、样式、绑定和 handler 名称，Widget
   还携带运行时状态、资源句柄和应用拥有的引用：
   - 指针引用：`themeOverride`、`virtualSource`、`collectionColumns`、`splitterSource`
     （主题、列表/树/网格、分隔器 controller 的裸指针，不能把指针值写入文件）；
   - 运行时资源 id：`imageId`（重启或资源代际变化后失效，应保存 `imageSource` 或稳定资源名）；
   - 动态子树：List/Tree/TreeList/VirtualList 的行由 source 在布局期物化，不是文档 children；
   - 当前状态：`scrollOffset`、`selected`、`checked`、`transitionAlpha`、焦点/交互状态等，
     必须区分声明初值和预览/运行时快照；
   - `bind`/`onClick` 是字符串引用，**可以**序列化，但加载侧必须经 StateStore/
     HandlerRegistry 或设计器应用适配层解析，不能假设任意 C++ lambda 可逆向生成。
   `semanticsActions`、语义覆盖和 `StyleOverrides` 可序列化，但必须在 schema 中标记为
   声明属性或运行时派生属性，不能靠“遍历 Widget 全字段”自动决定。
4. **控件类型和组合控件不是一一对应。** ComboBox、ColorPicker、Spin、ToolBar、
   StatusBar、Menu、DialogHost、Navigator、Form 和 DataGrid 主要由 widgets 层 controller
   组合出 Widget 子树；即使设计器能观察到运行时树，也不能从展开后的 Button/Text/Row
   子树唯一还原原始 controller 配置。设计器必须保存组合件节点或明确的应用适配引用。

#### 影响

D3 的「保存/导出」无路可走；即便增加一个 `Widget -> .lumen` printer，也只能对可表达的
静态子集生成规范化文本，无法恢复注释/格式、controller 构造意图和运行时引用。先定义
设计器文档模型，再决定是否把它投影到 `.lumen`，是 DP-1 的实质。

#### 前置工程 P1：文档对象模型与双向转换

设计器内部维护**可编辑文档对象模型（DOM）**，与 `Widget` 树是两个实体：
DOM --编译--> Widget 树（渲染/预览用）；DOM --序列化--> 文档文件；文档文件 --解析--> DOM。
运行时 Widget 树永不直接回写 DOM；用户确认的编辑命令才修改 DOM。这样可以区分声明属性、
预览状态和 controller/source 注入，也避免把布局结果、焦点和虚拟列表行写回文档。

DOM 的最小节点契约应包含：稳定 `id`、节点类型、typed properties、子节点/插槽、绑定和
命名引用、schema 版本，以及可选的源 span/扩展字段。`Widget` 编译器接收独立的
`RuntimeContext`（StateStore、HandlerRegistry、资源表、主题表、VirtualList/Splitter
source 注册表），未解析引用必须产生结构化诊断而不是写入空指针。

| 方案 | 内容 | 优点 | 代价 |
| --- | --- | --- | --- |
| A：扩展 `.lumen` | 解冻/扩充节点集覆盖全部控件；为 DSL 编写双向序列化器（printer） | 单一格式，与手写热重载工作流直接互通 | 触动冻结口径（多份设计文档「已知限制」锚定该策略，需成套更新 parser/诊断/golden）；控制器类控件（DataGrid 列定义等）需节点化设计，DSL 复杂度上升 |
| B：设计器文档格式 | 另定结构化格式（建议 JSON + schema 版本号），运行时编译为 Widget 树；可选提供 `.lumen` → 文档格式的**单向导入** | 不动冻结口径；schema 可版本化演进；序列化与手写 parser 解耦、测试性好；可表达 C++ builder 全集 | 双格式并存（手写 `.lumen` vs 工具文档），需在文档中明确边界；需要 JSON 解析（见依赖注） |

**推荐 B 起步**：不触碰 G-D3 口径、与 P2 共用一套字段注册表（见 §4.2）、
`.lumen` 单向导入保留与手写工作流的互通；A 作为后续可选升级（若用户希望工具与手写
共用一种格式再评估）。

关键设计点（两方案通用）：

- **指针字段的引用机制**：文档格式以命名引用表达（如 `source: "@list:orders"`、
  `theme: "@theme:dark"`），加载时由 RuntimeContext 的引用表解析到应用注册的实例。
  未解析引用是带 file/line/node id/name 的诊断，不得静默变成空 source 或崩溃。
- **三层往返不变量**：同一 codec 的 `read(serialize(dom)) == dom` 逐字段成立；`.lumen`
  单向导入另测 `parseLumenSource → DOM`，不能把两个格式的 parser 混成一个隐式入口；DOM
  编译出的 Widget 树与手写 C++ builder 的等价声明树做 golden 对照。若要求保留注释、
  属性顺序和原始空白，则必须保留 CST/trivia，不能只从 Widget 反向打印。
- **声明状态与运行时状态分离**：`checked` 的初始值可以存，实际 StateStore 值不存；
  `scrollOffset`、hover/press/focus、虚拟列表窗口、`transitionAlpha` 默认不存；
  `imageId` 存 `imageSource`/稳定资源名，加载时重新注册资源。
- **未知字段和未知节点策略**：设计器文档必须选择“保留并原样写回”或“拒绝保存”之一；
  不能在打开后静默丢失未来版本字段。当前 codec 对显式 `unknownFields` 容器与同级
  扩展字段的同名冲突直接拒绝，避免两个输入字段合并时静默覆盖。
- **源代码边界**：设计器只能逆向 DOM/Widget 声明树，不能从任意 C++ builder、lambda、
  controller 闭包或已展开的 RenderNode 可靠生成原始应用代码。
- **依赖注（仅 B）**：引入 JSON 解析需走 FetchContent 并 pin 修订（AGENTS.md 规则），
  在变更说明中显式登记；若不愿引入依赖，可先自写极简 JSON reader（设计器文档是
  工具产出、非手写，语法面可收窄）或复用 `.lumen` 语法骨架扩展属性表。

**验收**（四态，对齐完成计划 §1）：

- 接口已存在：DOM/序列化/解析/编译四 API + RuntimeContext 引用表 + schema 版本号 +
  声明/运行时属性分类；
- headless 已验证：DOM 往返 golden、Widget 编译 golden、未知引用/未知字段/损坏文件错误
  注入、动态 source 缺失、`.lumen` 单向导入对照；
- 真实平台已验证：属 D3 出口（设计器内打开→编辑→保存→重开一致性）；
- 可发布：不适用（框架层交付物）。

### 4.2 G-D2：属性元数据注册表

#### 现状与证据

全库无反射/属性表机制；`Widget` 是编译期 struct（字段即属性，`include/lumen/core/widget.h`），
全字段 `operator==` 可作变更检测。属性面板、校验、序列化、工具箱提示目前都需要
各自手写字段映射——三处映射必然漂移。

#### 前置工程 P2：PropertyMetadata 注册表

表格驱动（非宏反射），每控件类型登记一份属性描述：

```text
PropertySpec {
    名称（文档格式与面板共用）
    类型：bool / number / string / color / enum / EdgeInsets / TextStyle / …
    取值约束：枚举值列表或数值范围（如 flex >= 0、elevation 0..N）
    默认值（与 Widget 默认成员初始化一致）
    适用控件集（ButtonVariant 仅 Button/Dropdown 等）
    分组：几何 / 布局 / 外观 / 状态 / 语义 / 高级
    读写访问器：对 Widget& 的 get/set（或字段偏移表）
}
```

- **与 P1 协同**：方案 B 的 schema 字段集 = 注册表子集，三处消费（属性面板 UI、
  序列化字段选择、toolbox 提示/校验）共用一套真相。
- **防漂移（关键风险）**：守护测试遍历注册表，逐属性「写→读回→断言」，
  并对 `Widget` 做字段计数对照——新增 Widget 字段未登记元数据时测试失败，
  强制同步。
- 覆盖范围以 `widget.h` 当前 27 种 `WidgetType` 和 widgets 层组合件为准；先覆盖 D3
  首版选定的控件集（见 DP-3），但注册表接口必须能扩展到其余类型和组合件。
- 元数据不能只描述字段，还要描述节点可否作为根、允许的父节点、子节点数量/插槽、默认
  工厂、可见性条件、是否声明属性、是否由运行时派生，以及拖入工具箱的创建规则。
- `imageId`、controller 指针、生成行、布局 bounds 和交互快照不应被误登记为普通可编辑
  属性；它们要么映射为资源/命名引用，要么在属性面板中只读显示。

**验收**：headless——注册表覆盖测试（逐属性读写往返 + 字段计数守护）；
面板冒烟（每类型属性面板构建/修改/撤销一个属性）。

### 4.3 G-D3：治理口径——冻结节点集与「可编程化」排除项

#### 现状与证据

- 「DSL 可编程化/脚本能力」在 [`lumen-gui-completion-plan.md`](lumen-gui-completion-plan.md) §6
  与 [`lumen-m15-roadmap.md`](lumen-m15-roadmap.md) §1.2 均列为排除项；
- `.lumen` 冻结节点集策略锚定在 [`lumen-gui-framework-plan.md`](lumen-gui-framework-plan.md) §6.2
  （受限文本 DSL、面向手写）与多份控件设计文档的「已知限制」；
- AGENTS.md：偏离设计文档需在同一变更内更新文档。

#### 定性澄清

「工具可读写的数据格式」≠「DSL 可编程化」。可编程化指脚本、变量、条件、循环
（`text_dsl.h` 注释明示 `.lumen` 不含这些）；P1 方案 B 是**数据序列化**，
不引入任何可编程性。但该边界目前只存在于排除项的反面表述中，没有被正面写过——
选 B 时必须在 framework-plan §6.2 或本文后续修订中显式登记这一定性，避免被误读为
变相重开排除项。方案 A 则直接修改冻结策略，成套文档更新是交付物的一部分。

#### 决策

DP-1 已于 2026-10-01 选择 **B：设计器私有格式 + `.lumen` 单向导入**。这只增加工具数据
序列化，不引入脚本、变量、条件或循环，不改变 `.lumen` 的冻结节点集和「DSL 可编程化」排除项。

### 4.4 G-D8/G-D11：声明式文档、运行时桥接与引用注入

#### 现状与证据

`AppShell` 可以接收一个已构建的 `Widget` 并通过 `swapRoot` 重建运行时树；`StateStore`、
`HandlerRegistry` 和 `applyBinds` 已有应用运行时接线。但这些接口没有一个统一的“设计器
文档引用表”契约。`Widget` 中的 `virtualSource`、`collectionColumns`、`splitterSource`
和 `themeOverride` 是应用所有权对象的裸指针，设计器不能直接持久化或跨文档复制。

组合控件进一步放大这个边界：DataGrid 的列/数据源、Tree/List 的行 source、ComboBox 的
选项/绑定、Dialog/Navigator 的路由和 Menu/ToolBar 的命令都可能在 Widget 树中只留下展开
后的视觉子树。运行时树可用于预览和检查，但不是可逆的文档模型。

#### 前置工程 P3：RuntimeContext 与可诊断的引用协议

定义设计器编译 DOM 时的运行时适配接口，至少包含：

- `StateStore`/绑定值读取策略（预览固定值、应用真实值、未绑定占位）；
- handler/command 名称到 `HandlerRegistry`/`CommandRegistry` 的解析；
- theme、image、font/resource 的稳定名称到运行时资源的解析；
- VirtualList/List/Tree/TreeList/DataGrid/Splitter source 和组合件 controller 的注入；
- 引用缺失、类型不匹配、生命周期已结束时的结构化错误和可视化降级。

设计器预览必须能使用假的 RuntimeContext，不应为了打开文档启动业务网络、数据库或
真实数据源。应用运行时可提供真实适配器，但文档格式只保存稳定引用和声明初值。

**验收**：headless 覆盖成功解析、缺失引用、错误类型引用、预览替身、真实应用注入；
打开文档时不能解引用未验证的裸指针；同一 DOM 在空 context 下仍能生成可检查的占位树。

### 4.5 G-D9：稳定节点身份、源码映射与编辑选择

#### 现状与证据

当前 Element 复用主要依据 `type + key`；无 key 节点使用位置身份，RenderNode 的 identity
由布局路径生成。已有 `findNodeByIdentity`/`findNodeByKey` 可支持运行时定位，但这不等于
设计文档拥有独立的、跨重排稳定的节点 ID。工具箱插入、兄弟节点重排和复制粘贴会改变
位置身份，直接用运行时 identity 保存选择会导致属性面板跳到错误节点。

当前文本解析错误有 file/line/column，但成功解析结果没有向 Widget 暴露节点 source span；
因此不能可靠地从画布选中节点跳回文本位置，也不能在保存时对原文件做局部 patch。

#### 前置工程 P4：DocumentId 与 SourceMap

- 每个 DOM 节点分配持久 `DocumentId`，复制时生成新 ID，重排时保持原 ID；`key` 继续作为
  应用语义/状态复用键，不能替代编辑器 ID。
- 编译时维护 `DocumentId -> Widget/Element/RenderNode identity` 映射；重建失败时保留
  上一份可运行树并把错误绑定到 DOM 节点。
- `.lumen` 导入至少记录 node/attribute 的 source span；若选择保留注释和格式，则升级
  为 CST/trivia。私有设计器格式可只保存结构化 source map。
- 画布选择、大纲选择、属性面板和错误列表都以 `DocumentId` 为主键，key/identity 只作
  运行时查询辅助。

**验收**：插入、删除、复制、重排、撤销/重做后选择仍指向同一 DocumentId；文本错误能定位
到节点和属性；Widget 重建后映射不会把一个节点的 bounds 显示到兄弟节点上。

### 4.6 G-D10：格式版本、迁移、未知字段与损坏恢复

#### 现状与证据

`Preferences` 有版本和原子写的先例，但 `.lumen` 没有 schema 版本、迁移入口、未知属性
保留策略或设计器文档的临时恢复文件。直接使用 `Widget` 默认值反序列化会把“字段缺失”、
“旧版本字段”和“用户有意设置为默认值”混在一起。

#### 前置工程 P5：可演进的文档存储协议

文档必须带格式版本和应用/工具元数据；加载流程固定为“读取 → 校验 → 迁移 → schema
验证 → 编译”，迁移不可修改原文件。保存采用临时文件、fsync/替换或等价原子策略，并
在失败时保留原文件和可恢复副本。

必须明确：

- 缺失字段使用 schema 默认值；显式默认值仍可 round-trip；
- 未知节点/字段是保留写回还是拒绝保存；
- 旧 schema 如何迁移、迁移失败如何显示和回滚；
- 文档损坏、引用缺失、编译错误时继续显示上一份有效预览；
- 设计器自身的布局/最近文件设置与被编辑文档分开存储。

**验收**：旧版本 fixture 迁移 golden、未知字段 round-trip、截断/非法类型恢复、保存
中断不破坏原文件、编译失败保留旧画布；错误必须可定位到文档版本或节点。

### 4.7 G-D12：预览状态、声明状态和运行时状态隔离

`setVisualPreviewState` 能在运行时覆盖 hover/pressed/focused 等视觉状态，StateStore
和控制器还会产生 checked/selected/scroll 等动态值。若属性面板直接读取经过 `applyBinds`
的 Widget，保存操作就可能把用户当前焦点、滚动位置或业务值误写回设计文档。

设计器需要三份明确的数据：DOM 声明初值、RuntimeContext 提供的业务快照、仅用于画布
预览的 visual preview override。属性面板必须标明“声明值”或“当前预览值”，保存只提交
文档事务；预览状态切换不能产生文档 dirty 标记。DP-4 已于 2026-10-01 选择独立
preview/session：运行时状态不写回设计文档；若以后需要持久化预览状态，也必须使用独立
session 文件并与文档 revision 分离。

画布内预览文本框的 Delete 和 Ctrl/Cmd+Z/Y/Shift+Z 只编辑预览值与文本历史，
不会删除节点、撤销文档编辑或清除文档 redo 分支。属性/命名引用字段仍采用文档级
撤销/重做；Ctrl/Cmd+Up/Down 的结构重排在任何文本字段聚焦时禁用，字段保留光标/
选区处理。字段失焦后文档结构命令恢复，文件保存和运行等非文本命令仍可分发。

**验收**：切换 hover/press/focus、主题、DPI、字体缩放、绑定值和滚动位置后保存，重开
文档仍得到相同声明初值；切换预览状态不进入 undo 栈；运行时业务状态更新不覆盖未保存的
设计器编辑。

### 4.8 控件覆盖策略：12 个 DSL 节点不是完整设计器目标

“扩充到所有控件”需要区分两个问题：文本 `.lumen` 的兼容范围，以及设计器文档能够编辑的
控件范围。两者不必同步扩大。

| 层级 | 覆盖对象 | 编辑能力 | 说明 |
| --- | --- | --- | --- |
| L0：MVP | 当前 parser 接受的 12 个节点 | 可创建、可改属性、可重排、可保存并 round-trip | 先验证工具箱 → 画布 → 属性面板 → 保存 → 重开闭环 |
| L1：静态 WidgetType | `Grid`、`Image`、`Icon`、`Slider`、`ProgressBar`、`Radio`、`Tooltip`、`Dropdown`、`Tabs`、`ThemeScope` 等其余可声明类型 | 设计器格式中应逐步可编辑；`.lumen` 是否支持由 DP-1 决定 | 资源、主题和选项通过稳定名称或引用表达，不能写入运行时指针/句柄 |
| L2：动态/集合 WidgetType | `VirtualList`、`List`、`Tree`、`TreeList`、`Splitter`、DataGrid 数据/列模型 | 需要专用 schema、RuntimeContext source 注入和缺失引用降级 | 运行时物化行/分隔条不是文档 children，不能从展开后的 Widget 树反推 |
| L3：widgets 层组合件 | ComboBox、ColorPicker、Spin、ToolBar、StatusBar、Menu、DialogHost、Navigator、Form 等 | 保存组合件语义和配置，预览时编译成 Widget 子树 | 必须保留组合件节点或命名 controller 引用，否则只剩不可逆的视觉子树 |

因此，**完整设计器的最终目标应覆盖当前所有有稳定声明语义的 WidgetType 和组合件**，但
不要求第一阶段把它们全部加入文本 DSL。12 个节点适合作为 L0 交付范围，不应继续被描述
为“全部控件”。设计器格式的覆盖矩阵至少要记录：节点名称、编辑级别（可编辑/只读预览/
不支持）、属性 schema、运行时引用、创建工厂、序列化状态和测试状态。

DP-3 已于 2026-10-01 选择 **L0 优先**：首批只对当前稳定的 12 个节点闭环，L1–L3
按覆盖矩阵逐批加入；注册表接口仍须保留向后扩展能力。

若产品要求“`.lumen` 文本与完整设计器双向互通”，则必须选择方案 A，逐层扩充 parser、
printer、诊断、属性 schema 和 golden 测试；若采用方案 B，`.lumen` 可以继续保持 12 个
手写基础节点，完整覆盖放在设计器自己的 DesignDocument 中。

### 4.9 P1 实施级细化与可行性验证（第一阶段）

本节把 P1 从“架构建议”收敛为可以先实现的语义 DOM。第一阶段不引入 JSON 依赖、不修改
`Widget` 布局/渲染契约，也不承诺一次覆盖 27 个 `WidgetType`。

#### P1.1 模块边界与文件落点

设计文档模型放在 `lumen-dsl`，不放进 `lumen-core`：它依赖 `Widget` 和 DSL 的源位置，
但不属于渲染、布局或平台基础层。建议落点如下：

| 文件 | 第一阶段职责 |
| --- | --- |
| `include/lumen/dsl/design_document.h` | `DesignDocument`、`DesignNode`、typed value、节点 ID、source span、结构化错误和解析/编译结果类型 |
| `src/dsl/design_document.cpp` | 默认值、节点复制/遍历、文档校验和通用辅助函数 |
| `src/dsl/text_dsl.cpp` | 保留 lexer/grammar；将现有私有 `AstNode` 转换路径拆成 `AST -> DesignDocument -> Widget` |
| `include/lumen/dsl/design_codec.h` | `parseLumenSource`、设计器 codec、`compileDesignDocument`、规范化 `serializeDesignDocument` 的公共入口 |
| `tests/designer_document_tests.cpp` | 12 节点 DOM round-trip、Widget 编译、错误和稳定 ID 回归 |
| `src/dsl/CMakeLists.txt` | 新增源文件；继续只链接 `lumen-core`，不引入 AppShell 或平台库 |

现有 `parseLumen()` 保持 ABI/行为兼容，继续返回 `DslParseResult{Widget, DslError}`。它成为
兼容包装：`parseLumenSource` 解析 `.lumen` 语义文档，`compileDesignDocument` 生成 Widget，
再由包装函数返回旧结果。现有 counter/settings 和已有 DSL 测试不需要迁移。

#### P1.2 最小数据结构

第一阶段的结构可以用 C++20 值类型实现，不需要反射或后台线程：

```cpp
using DesignNodeId = std::uint64_t;

struct DesignSourceSpan {
    SourcePos begin{};
    SourcePos end{};
};

struct DesignEnum {
    std::string domain{};
    std::string value{};
};

struct DesignValue {
    // 第一阶段覆盖 parser 已有的 bool/number/string/color/enum；
    // EdgeInsets/TextStyle 等复合属性由 schema 层展开为对象字段。
    std::variant<std::monostate, bool, double, std::string, core::Color,
                 DesignEnum> value{};
};

struct DesignNode {
    DesignNodeId id{0};
    std::string type{};  // 文档名，不直接依赖 enum 数值
    std::map<std::string, DesignValue> properties{};
    std::map<std::string, std::string> references{};
    std::vector<DesignNode> children{};
    std::map<std::string, std::vector<DesignNode>> slots{};
    // 采用“保留写回”策略时保存 codec 的规范化原始片段；拒绝保存时为空。
    std::map<std::string, std::string> unknownFields{};
    std::optional<DesignSourceSpan> source{};
};

struct DesignDocument {
    std::uint32_t schemaVersion{1};
    std::string documentId{};  // 文档身份；跨另存为/复制的规则由 DocumentStore 定义
    std::string pageName{};
    DesignNode root{};
    std::map<std::string, std::string> unknownFields{};
};
```

实现时还必须保留一个扩展字段容器或采用“未知字段拒绝保存”策略；不能因为第一阶段只
支持 12 个节点，就把未来节点静默丢弃。`type` 使用字符串而不是 `static_cast<int>`，
避免枚举追加导致文件不可读。`DesignNodeId` 只服务编辑器，不写入 `Widget.key`，也不改变
Element 当前的 `type + key` 复用规则。

若方案 B 使用 JSON 或其他可能被 JavaScript 工具读取的 codec，wire format 中的
`documentId`/`DesignNodeId` 必须是带引号的不透明字符串（例如十六进制或 UUID），内部再
转换成 `uint64_t` 或其他值类型；不能把 64 位 ID 写成可能丢精度的 JSON number。复制节点
生成新 ID，打开/保存/迁移保持已有 ID；另存为是否生成新的 `documentId` 由 DocumentStore
固定并写入迁移测试。下面的结构只表达语义，不提前决定 JSON 键名或最终扩展名：

```text
document(schemaVersion=1, documentId="doc:…") {
  root id="node:1" type="Column" {
    property padding = 16
    property background = #20242c
    child id="node:2" type="Text" {
      property text = "Count: "
      reference bind = "counter"
    }
  }
}
```

#### P1.2a：解析入口和存储 codec 的命名约束

“解析 `.lumen`”和“读取设计器文件”必须是两个可辨认的入口。否则一个含糊的
`parseLumenDocument` 名称容易被误解为已经承诺了 DP-1 的方案 B 私有格式。建议按下面的语义命名，并在真正实现时
保持入口职责单一：

| 入口 | 输入/输出 | 允许的副作用 | 说明 |
| --- | --- | --- | --- |
| `parseLumenSource` | 受限 `.lumen` 文本 → `DesignDocument` | 无 | 复用 lexer/AST；只做语义导入，不保留运行时对象 |
| `readDesignDocument` | 设计器格式文件 → `DesignDocument` | 无 | 负责格式版本、未知字段和迁移前校验 |
| `serializeDesignDocument` | `DesignDocument` → 规范化文本 | 无 | 应绑定明确的 codec；不能同时含糊地表示 A 和 B |
| `compileDesignDocument` | `DesignDocument` + `RuntimeContext` → `Widget`/诊断 | 只读借用 context | 不改 DOM，不写入业务 StateStore |

若为了兼容现有 ABI 保留 `parseLumen()`，它只能作为“解析 `.lumen` 并编译 Widget”的旧包装；
新的设计器入口应返回 DOM 和诊断。方案 B 的设计器格式使用独立 magic/format 名称、schema
版本和 codec 版本，不能仅凭文件扩展名猜测格式。`.lumen` 继续作为受限手写 DSL 的单向
导入入口；设计器保存不回写 `.lumen`，从而不把接口名称变成隐含的格式承诺。

#### P1.3 公共转换接口

候选接口保持同步、值语义和 headless 可调用（以下为契约草图，类型在 P3/P4 落地时统一）：

```cpp
struct DesignParseResult {
    DesignDocument document{};
    std::optional<DesignError> error{};
};

struct DesignCompileResult {
    core::Widget root{};
    CompileTrace trace{};
    std::vector<DesignError> diagnostics{};
    std::shared_ptr<DesignRuntimeSession> session{};
};

[[nodiscard]] DesignParseResult parseLumenSource(
    const std::string& source, std::string filename = "<memory>");

[[nodiscard]] DesignCompileResult compileDesignDocument(
    const DesignDocument& document, DesignRuntimeContext& context);

[[nodiscard]] std::string serializeDesignDocument(
    const DesignDocument& document);
```

`compileDesignDocument` 当前编译 L0 的 12 个节点、L1 静态节点 `Grid`、`Image`、`Icon`、
`Slider`、`ProgressBar`、`Radio`、`Tooltip`、`Dropdown`、`Tabs`、`ThemeScope`，L2
动态节点 `VirtualList`、`List`、`Tree`、`TreeList`、`Splitter`，以及首批 L3 组合件
`ComboBox`、`ColorPicker`、`Spin`、`ToolBar`、`StatusBar`、`Menu`、`DialogHost`、
`Navigator`、`Form`、`DataGrid`；L2 的 `virtualSource`/`splitterSource` 通过
`DesignRuntimeContext` 解析到带 lease 的类型化句柄，缺失句柄时保留可检查节点并禁用交互。
L3 节点由按类型注册的 `buildComponent(node, componentContext)` 创建 Widget 子树；缺失
builder 时保留禁用的 `Container` 占位和节点级诊断，成功结果及其 lease 进入同一个
`DesignRuntimeSession`。绑定、handler、theme、image 命名引用仍只把稳定名称写入 DOM，
不写入裸指针。上例中的
`CompileTrace` 和 `DesignRuntimeSession` 是 P4/P3 的候选返回值，若分阶段实现，也必须
通过等价的 sidecar 结果保留映射和 lease，不能丢回普通 `Widget`。

编译步骤固定为：

1. 校验 schemaVersion、root、节点类型、父子数量和属性类型；
2. 按节点类型创建默认 `Widget`；
3. 使用 schema/属性转换器写入声明属性；
4. 递归编译 children，并校验单子容器/叶子规则；
5. 生成 `Widget`，不调用 `applyBinds`，不写入布局 bounds 或交互快照。

AppShell 已有 `swapRoot(core::Widget)`，并在重建阶段统一执行 `applyBinds`；因此 P1 的
编译结果可以直接接入现有画布，不需要修改 AppShell 的状态流。`DslCache` 继续缓存旧的
Widget 结果，DOM cache 在需要时单独增加，避免改变现有热重载行为。

#### P1.4 可行性检查结果

| 检查项 | 现有证据 | 结论 |
| --- | --- | --- |
| 依赖边界 | `lumen-dsl` 目前只链接 `lumen-core` | 可行；新增 DOM 不会引入 app/platform 环 |
| 解析复用 | lexer、递归 parser、`AstNode` 和 `SourcePos` 已存在 | 可行；需要拆分 AST→Widget，不需要重写词法层 |
| 编译接入 | `Widget` 是可移动值类型；`AppShell::swapRoot` 已存在 | 可行；首阶段可不改布局/渲染代码 |
| 绑定接线 | AppShell 重建阶段调用 `applyBinds`，StateStore/HandlerRegistry 已存在 | 可行；P1 不应提前复制业务状态 |
| 错误模型 | 现有 `DslError` 有 file/line/column/expected/found | 可行；DesignError 可复用字段并增加 node id/path |
| 源文本保真 | 当前 lexer/AST 不保存注释和 trivia | 语义 round-trip 可行；DP-5 已选择规范化输出 + SourceMap，不做 CST 局部 patch |
| 全控件编译 | 运行时 source/controller 是裸指针且组合件由 widgets 层构建 | P1 不可一次完成；必须按 P2/P3 分阶段扩展 |

#### P1.5 第一阶段出口

- 同一 codec 的 `read(serializeDesignDocument(document))` 在 12 节点范围内逐字段相等，
  `.lumen` 另以 `parseLumenSource` 做单向导入 golden；
- `compileDesignDocument(document, context).root` 与现有 C++ builder 的声明树 golden 相等；
- 旧 `parseLumen`/`DslCache` 测试全部保持通过；
- 未知节点、错误属性、非法子节点、损坏输入都有确定性诊断；
- 复制/重排节点只改变设计器选择顺序，不改变未复制节点的 `DesignNodeId`；
- 明确记录“规范化文本 round-trip 已支持，注释/空白保真尚未支持”。

本阶段的静态架构核对结论为**可行**。对现有边界的回归验证已完成：`build-debug` 中
Element identity、DSL 全部用例和 counter 热重载的历史边界回归共 43 项通过（2026-09-30）；这证明
P1 可以沿现有 `lumen-dsl -> lumen-core -> AppShell` 路径接入，且不需要先改布局/渲染层。
这不是新 DOM 的实现证据；在开始 P2 前，仍必须以 `designer_document_tests.cpp` 和一次
CPU headless 构建把上述 P1 接口变成编译证据。若任一出口失败，应先修正 P1，而不是继续
扩展控件覆盖。

### 4.10 P2 实施级细化：节点与属性 schema

#### P2.1 文件落点和注册方式

P2 继续放在 `lumen-dsl`，因为 schema 同时服务文档解析、Widget 编译和设计器属性面板，
不应把编辑器 UI 依赖倒灌进 `lumen-core`。建议新增：

| 文件 | 职责 |
| --- | --- |
| `include/lumen/dsl/design_schema.h` | `PropertySpec`、`NodeSchema`、约束、编辑器 hint、schema registry 接口 |
| `src/dsl/design_schema.cpp` | L0、L1 静态、L2 动态和首批 L3 组合件注册表、通用属性转换和校验；后续组合件按批追加 |
| `tests/designer_schema_tests.cpp` | 注册表覆盖、默认值、读写往返、非法值和节点结构约束 |

注册表使用显式静态表，不使用宏反射或字段偏移。字段类型有 `bool`、`number`、`string`、
`color`、`enum`、`EdgeInsets`、`CornerRadius`、`TextStyle` 和 reference；访问器使用
`get(const Widget&)` / `set(Widget&, const DesignValue&)` 函数，避免 `Widget` 中的 optional、
枚举和条件属性被错误地按内存布局访问。

候选结构：

```cpp
enum class PropertyKind { Boolean, Number, String, Color, Enum, Object, Reference };
enum class PropertyPersistence { Declaration, RuntimeReference, PreviewOnly, Derived };

struct PropertySpec {
    std::string name{};
    PropertyKind kind{PropertyKind::String};
    PropertyPersistence persistence{PropertyPersistence::Declaration};
    DesignValue defaultValue{};
    std::vector<std::string> enumValues{};
    std::function<bool(const DesignValue&)> validate{};
    std::function<DesignValue(const core::Widget&)> get{};
    std::function<bool(core::Widget&, const DesignValue&)> set{};
};

struct NodeSchema {
    std::string type{};
    bool canBeRoot{false};
    std::size_t minChildren{0};
    std::optional<std::size_t> maxChildren{};
    std::vector<std::string> allowedParents{};
    std::vector<PropertySpec> properties{};
    std::function<core::Widget()> makeDefault{};
};
```

实际实现可以把 `DesignValue` 的复合对象拆成命名属性，不要求第一阶段支持任意嵌套对象。
每个属性必须带持久化分类：`Declaration` 进入文档，`RuntimeReference` 写稳定名称，
`PreviewOnly` 不落盘，`Derived` 只读显示。这样 `imageId`、bounds、焦点和 controller
指针不会因为注册表遍历而被误保存。

#### P2.2 与现有 parser 的迁移顺序

当前 `Converter::applyAttr` 是一组手写 `if` 分支。不要一次删除它，迁移顺序为：

1. 先把 L0 节点的属性默认值、枚举和叶子/单子规则登记到 registry；
2. 新的 `validateAndSetProperty` 在测试中与旧 `applyAttr` 并行运行，比较 Widget 字段；
3. L0 golden 全部一致后，才让 `compileDesignDocument` 使用 registry；
4. 保留 `parseLumen` 的旧错误格式适配，避免已有诊断测试失效；
5. 每加入一个 L1–L3 节点，同时加入 `NodeSchema`、创建工厂、运行时引用策略和 golden。

#### P2.3 可行性结论和出口

现有 Widget 是公开的值类型，默认成员值和 `make*` builder 已存在；因此表驱动访问器可以
在 `lumen-dsl` 实现，不需要修改 Widget 布局或增加反射运行时。可行性验证必须覆盖：

- 每个注册属性写入后从 Widget 读回，默认值和显式默认值可区分；
- 适用控件错误、枚举错误、范围错误和复合值错误返回稳定诊断；
- registry 中所有 L0 属性都能被 `serializeDesignDocument` 消费；
- 新增 Widget 字段未登记时守护测试失败。C++20 没有标准字段反射，不能把“字段计数对照”
  写成自动发现；`widgetFieldInventory()` 维护明确的 `WidgetFieldInventory`（把结构、声明、
  运行时引用、预览和派生字段分栏），测试逐项核对注册表的文档别名与持久化分类。
  测试另以显式列出的 74 个 Widget 成员执行 C++20 structured binding，成员新增/删除
  即编译失败，包括利用已有 padding 而不改变 sizeof 的布尔字段；这只守护聚合成员数量，
  不自动推导字段名、文档别名或持久化策略，更新成员时仍须人工同步清单。若采用
  clang 工具生成清单，生成器版本和生成文件也必须纳入构建证据；
- 现有 43 项 P1 边界回归和完整 DSL 测试保持通过。

P2 未完成前，属性面板只能使用只读 `RenderNode` 摘要，不能宣称支持通用属性编辑。

### 4.11 P3 实施级细化：RuntimeContext 与引用注入

#### P3.1 接口形状

P3 的目标不是让 DSL 持有 controller，而是让 DOM 中的稳定名称在编译时解析到当前应用
上下文。建议新增 `include/lumen/dsl/runtime_context.h`，接口返回**类型化解析结果**，
而不是暴露通用 `void*`：

```text
DesignRuntimeContext {
    readBinding(name, mode) -> PreviewValue | Diagnostic
    resolveHandler(name) -> HandlerRef | Diagnostic
    resolveReference(kind, name) -> RuntimeRef | Diagnostic
    buildComponent(node, componentContext) -> WidgetFragment | Diagnostic
    placeholder(node, diagnostics) -> WidgetFragment
}

RuntimeRef {
    kind                 // theme / image / font / virtual-source / splitter-source / …
    stableName
    lifetimeToken        // 编译结果使用期间必须有效
}
```

`RuntimeRef` 的实现可以在设计器应用层持有 `shared_ptr`、arena lease 或其他 RAII 保活对象；
`core::Widget` 现有的 `themeOverride`、`virtualSource` 和 `splitterSource` 裸指针只能指向
这些 lease 所拥有或保证寿命的对象。候选公共接口应返回 `DesignRuntimeSession`（编译出的
Widget、诊断和保活 lease 的所有权），不能只返回一个带悬空指针的 `Widget`。若不修改
`AppShell`，设计器至少要把 session 与当前 preview shell 同寿命保存，并在 `swapRoot`、
资源热替换和窗口关闭时先撤销异步任务再释放 lease。

组合件使用 `buildComponent`，由应用适配层根据节点类型和配置创建 ComboBox/DataGrid/
Dialog 等 Widget 子树；适配器必须返回片段的语义根、生成节点的 trace 信息和引用 lease。
编译器返回引用诊断并支持 `Placeholder` 策略，不能在失败时生成可交互的半初始化控件。

#### P3.2 与 AppShell 的接入

- 设计器预览使用独立的 preview `StateStore`，不读取或修改业务 AppShell 的真实状态；
- 应用运行时仍由 `AppShell::rebuildIfDirty` 调用 `applyBinds`，P3 不复制绑定机制；
- handler resolver 只解析名称，实际回调仍由应用注册到 `HandlerRegistry`；
- source/controller 的生命周期由应用拥有，RuntimeContext 只能在 UI 线程编译期间借用；
  编译产物使用期由 `DesignRuntimeSession` 明确延长，不得把“编译调用返回”当成借用结束；
- context 销毁或引用失效时，先取消旧 session 的异步工作，再让文档保持有效、画布显示诊断
  占位；任何旧 Widget/RenderNode 都不能继续持有悬空指针。

#### P3.3 可行性验证

现有 `VirtualListSource`/`SplitterSource` 是 core 前置声明，StateStore/HandlerRegistry 已
提供名称接线，`AppShell::swapRoot` 可消费编译结果；因此 P3 不需要修改平台或渲染算法，
但需要在 app/preview 接缝保存 session lease。验证 fixture 必须覆盖：空 context、成功绑定、
缺失 handler、错误 source 类型、资源失效、组合件 builder 抛出诊断、session 关闭时的异步
回调和预览/真实 context 切换；每个 fixture 都要检查无崩溃、无悬空引用和可重复输出。

### 4.12 P4 实施级细化：DocumentId、编译追踪和 SourceMap

#### P4.1 不修改 Widget 的映射方案

Element 当前以 `type + key` 复用，LayoutEngine 当前按 `k:<key>` 或 `i:<index>` 生成路径
identity。设计器 ID 不应塞入 Widget 新字段，也不应覆盖应用 `key`；编译器额外返回追踪表：

```cpp
struct CompiledNodeRef {
    DesignNodeId documentId{0};
    std::string runtimeIdentity{};
    std::string key{};
    bool runtimeOnly{false};
};

struct CompileTrace {
    std::map<DesignNodeId, CompiledNodeRef> nodes{};
};
```

静态 DOM 节点在编译时按同一 child traversal 顺序登记，布局完成后用 root identity 和
`findNodeByIdentity` 关联 bounds/style。虚拟列表行、Splitter 分隔条和集合空态标为
`runtimeOnly`，不伪造 DocumentId。重排 keyless 节点会改变 runtime identity，但不会改变
DocumentId；编辑器在下一次编译后重新建立映射。若重复 `key` 或其他路径组合使两个静态
节点得到相同 runtime identity，编译器返回 `compile.duplicate_runtime_identity` 并绑定到
后出现节点的 `DocumentId`/`key` source span，不生成不可信的 bounds 映射。

#### P4.2 SourceMap 范围

第一阶段只保存节点和属性的起始/结束 `SourcePos`，足以实现错误列表、属性面板跳转和
“定位到源”。当前 lexer 已维护 UTF-8 字符列，AST 已携带 token position；需要补充 token
结束位置和注释 trivia 的工作不进入设计器持久化契约。DP-5 选择规范化输出 + SourceMap，
SourceMap 只作为导入会话数据，不承诺对原文件做局部 patch。

#### P4.3 可行性验证

P4 可以完全在设计器侧实现，不需要变更 Element/LayoutEngine。测试必须验证插入、复制、
删除、兄弟重排、keyless → keyed、undo/redo 后 DocumentId 和运行时 bounds 映射；动态
source 生成的节点只能出现在运行时树，不得进入文档选择集合。

### 4.13 P5 实施级细化：文档存储、迁移和恢复

#### P5.1 存储 API 与格式边界

新增 `DocumentStore` 位于设计器/DSL 工具层，不复用 `Preferences` 的扁平 key/value 作为
文档格式。候选接口：

```cpp
struct DocumentLoadResult {
    DesignDocument document{};
    std::vector<DesignError> diagnostics{};
    bool recovered{false};
};

class DocumentStore {
  public:
    DocumentLoadResult load(const std::string& path) const;
    bool save(const std::string& path, const DesignDocument& document,
              std::vector<DesignError>& diagnostics) const;
};
```

保存顺序固定为 serialize → 写入同目录的进程/序列号唯一临时文件 → flush/close → atomic rename；失败时
原文件保持不变。若产品把断电后的耐久性也列为“已保存”，还必须在平台抽象层补文件和父目录
同步，并在支持矩阵中单独记录；`Preferences` 现有 tmp+rename 先例本身不能证明 fsync。
多窗口同时保存时要有文档锁或 revision 检测，并且并发进程不得复用固定临时文件名而互相覆盖。
加载顺序固定为 read → format/version check → migration chain → schema validate → compile。
迁移函数接收值对象并返回新对象，不直接修改磁盘文件。

DP-1 已选择 B，`DocumentStore` 使用 JSON/等价结构化设计器 codec；`.lumen` 仅作为单向
导入，不提供设计器保存 printer。DP-6 要求扩展字段原样保留；无法保留时拒绝保存，不得静默丢弃。schemaVersion 和恢复行为必须一致。已有
`Preferences::save` 的 tmp+rename 可作为原子写实现参考，但不能直接复用其行格式承载嵌套
节点和未知字段。

#### P5.2 可行性验证

存储层不进入 AppShell 或渲染线程，可以使用现有文件 I/O 和 Preferences 的原子替换模式。
测试必须覆盖旧 fixture 迁移、未知字段保留/拒绝、截断文件、非法类型、保存中断、外部
revision 冲突、编译失败保留上一棵有效画布和恢复副本；验证失败不能修改内存中的当前文档。
工作台先尝试编译候选文档，再发布 DOM、选择和源路径；schema/编译失败保留当前
文档、撤销/重做、dirty、加载 revision 和预览 session。候选错误仍展示其文件及
documentId，不能把其他文档的错误对象变成当前编辑源。可检查的离线引用占位属于
成功打开：与候选 DOM 一起发布，并建立新的干净历史及文件 revision。

### 4.14 G-D13：编辑命令、事务与 dirty 代数

#### 现状与边界

`CommandRegistry` 负责名称到动作的分发，`text::EditingHistory` 负责 TextField 的编辑
事务；两者都不能直接充当设计器的文档 undo/redo。设计器命令还必须知道节点身份、结构
引用、选择变化和保存状态。若属性面板直接改写 `DesignNode`，一次拖拽或多字段粘贴会被
拆成许多无法回滚的半成品操作。

#### 前置契约：DocumentCommand 与 DocumentTransaction

设计器应在应用层维护独立的命令栈。候选最小字段如下，具体命名可以随实现调整：

```text
DocumentCommand {
    label                 // 撤销菜单和日志使用的稳定文案
    affectedIds            // 受影响的 DocumentId 集合
    precondition(document) // 版本、父节点、属性类型和引用仍匹配
    apply(document)
    revert(document)
    mergeKey               // 连续键入/拖动是否可合并；空值表示不合并
}

DocumentTransaction {
    baseRevision
    commands[]
    selectionBefore/After
    dirtyRevisionAfter
}
```

事务规则必须固定下来：

1. 命令先在副本或可回滚草稿上校验，全部成功后一次提交 DOM；失败不改变文档、选择或
   dirty 状态。
   属性/结构候选还先准备一次预览编译，schema 合法但编译失败的候选不能进入历史或
   清除已有 redo 分支。准备阶段只增加 rebuildCount，发布成功才更新 frame generation；
   非法候选保留当前 session，报告具体编译 code、节点和属性，而不是先提交再 undo。
2. 一次用户意图只有一个 undo 单元：一次拖动、一次多选属性修改、一次结构重排和一次
   粘贴分别形成一个事务；连续文本输入/拖动是否合并由 `mergeKey` 和时间窗口决定。
   `DesignDocumentHistory` 使用单调时钟，以相邻成功提交之间不超过 750ms 的空闲间隔
   判断连续操作；合并后的提交会刷新这个窗口。事务内所有命令必须使用相同的非空
   `mergeKey`，且相邻事务的受影响节点集合、文档快照和选择状态连续。含空 key 或不同
   key 的事务保持独立；保存、成功 undo/redo、新建 redo 分支和选择/目标变化均打断合并。
   时钟回退也不合并，测试可注入时钟，不依赖实际等待。
3. undo/redo 只重放 DOM 命令，并重新编译预览；不能重放业务回调、网络请求、controller
   内部滚动状态或画布 overlay。
4. `documentRevision` 与 `savedRevision` 分离。预览状态、诊断刷新和运行时绑定更新不使
   `documentRevision` 增长；保存成功后才推进 `savedRevision`，另存为应记录新的路径。
   DP-9 工程中的每页分别保存 DOM、选择、命令历史、保存检查点和文件 revision；
   切页不能重置 dirty 或丢失 undo/redo，重新选择当前页不重建会话。工程保存/另存
   成功后更新全部页面的保存检查点与路径，仍能撤销回保存前、重做到保存后的状态。
   编辑快照不持有运行时 frame、controller 或借用 context；切页在活动 context 中
   编译新的预览，失败保留原页面和 session。成功打开独立 `.lumen`/`.design`，或
   将单页保存为 `.design`，进入独立文档会话并退出工程；失败不改变工程或保存目标。
5. 外部文件在编辑期间发生变化时，保存必须检测文件身份/修改代数，提供重新载入、另存
   和覆盖三个明确动作；不能静默覆盖外部修改。
   Designer 的诊断面板已接通这三个动作；明确覆盖仅接受冲突出现时观察到的外部
   revision，观察后再次变化仍拒绝。工程覆盖同时预检 manifest 与全部页面，重载不再
   把旧工作台写入刚加载的同 ID 页面。交互、视觉与失败边界见
   [`文件冲突恢复规范`](lumen-designer-file-conflict-design.md)。

**验收**：属性校验失败、引用失效、父子约束失败、重排索引过期时事务整体回滚；连续
undo/redo 后 DOM、选择、SourceMap 和编译结果一致；保存后重新打开与 `savedRevision` 对齐；
业务 StateStore 变化不进入命令栈。

### 4.15 G-D14：编辑选择、坐标变换与动态节点策略

#### 选择模型

设计器需要自己的 `DesignSelection`，不能直接复用集合控件的 `SelectionModel`。最小状态
包括：选中 `DocumentId` 集合、主节点、框选锚点、选择模式（替换/追加/反选）、拖拽捕获
节点和焦点面板。选择状态属于会话，不写入文档；复制、重排和 undo/redo 的选择恢复由
事务显式记录。

命中结果按以下优先级解释：静态 DOM 节点优先于仅用于绘制的 overlay；动态物化行可以
显示运行时诊断，但没有 `DocumentId` 时只能选择其所属的源节点；不可编辑的派生节点应
显示只读摘要并提供“定位来源”动作。画布点击得到的最深命中链仍来自
`core::hitTestChain`，最终选择必须经过 `CompileTrace` 过滤，不能把 runtime identity
直接放入选择集合。

#### 坐标链

画布交互至少经过四个坐标空间：窗口物理像素、宿主逻辑坐标、带 zoom/pan 的设计坐标、
节点局部坐标。候选变换为 `logical = pixels / deviceScale`、
`design = inverse(pan + zoom * canvasLogical)`、`local = design - nodeAbsoluteOffset`。
每次 pointer、框选、overlay 绘制和吸附计算都应记录使用的空间；DPI 或 zoom 改变时只更新
变换，不改变 DOM 的数值属性。窗口缩放、滚动容器裁剪、Stack 的 `stackPosition` 和
RTL/方向设置不能通过“看起来相同”的屏幕像素值回写文档。

**验收**：在 100%、125% 和 200% DPI 以及至少两个 zoom 值下，点选、框选、移动和 overlay
边界仍指向同一个 `DocumentId`；剪贴板复制、跨父节点粘贴和动态行选择不会生成重复或
伪造的文档 ID；Escape 取消拖拽后选择和 DOM 均恢复到事务开始状态。

当前 D3 应用出口已将多选剪贴板实现为会话内节点副本：按文档遍历顺序复制顶层选中节点，
选区中的后代不会重复复制；粘贴到另一父节点时批量插入并只产生一个 undo 事务，重新分配
节点 ID、规范化冲突 key，并在 undo/redo 中恢复整组选区。多选 Delete 同样以一个事务
删除顶层选中节点；按兄弟列表上移/下移也以一个事务保持整组选区和相对顺序。跨父节点、
跨 slot 或选区包含过期 ID 时整体拒绝，不会部分修改文档。属性面板对共有声明属性执行
批量写入并只产生一个事务，属性类型或 schema 不兼容时整体拒绝。多选 Duplicate 在同一
兄弟列表中批量插入并规范化 key；跨父节点/slot 的 Duplicate 明确拒绝。Escape 会通过统一的
`pointerCancel` 路径取消大纲、工具箱或画布手柄拖拽，保留事务开始时的 DOM 和选择状态。
画布空白区域支持拖拽框选根节点的直接子项和直接 slot 子项，使用同一逻辑坐标空间中的
`RenderNode` bounds 相交测试；Drop 以一次选择更新同步 `DesignSelection` 和大纲，空框清空
选区。动态物化节点仍按上面的 `CompileTrace` 规则过滤，不进入文档选择集合；虚拟行点击会
回到其所属 source 节点的 `DocumentId`，不会把行索引伪造成文档节点。DesignerApp
现已增加画布 zoom/pan 视图状态：Zoom +/-、Reset view、画布滚轮和方向命令只改变
`DesignCoordinateTransform` 的视图参数，预览 Widget 在画布边界按 zoom 缩放、按 pan
平移；实际命中、框选和手柄仍读取缩放后的 RenderNode bounds，文档数值、dirty、revision
和 undo 栈不受视图操作影响。动态 source adapter 在视图重建时按 zoom 生成独立的行/分隔条
几何，避免把运行时物化尺寸写回 DesignDocument。应用级 headless 测试已在 100%、125% 和
200% DPI 以及两个 zoom 值下按变换后的 RenderNode 中心点击，验证选择仍回到同一个
DocumentId，且视图操作不产生文档 revision。

手柄会话在按下点建立锚点，越过拖拽阈值后仍计算完整位移；Move 仅更新临时辅助层，
Drop 使用实际松开位置，以一次属性事务提交。计算使用父容器的设计坐标，扣除父
padding 和子 margin，再按 zoom 换算；嵌套容器偏移和画布 pan 不进入 Stack 子节点
的 left/top。流式节点和根节点按自身尺寸吸附，布局对齐偏移和 pan 不参与尺寸
网格，也不写入位置属性；向西/北拖动同样调整尺寸，位置仍由布局管理。网格保持
8 个设计单位，吸附半径沿用 Theme 的逻辑像素 token 并除以 zoom；临时框和手柄
反向换算到画布逻辑坐标。
会话中切换 zoom/pan/DPI 或重置视图先取消拖拽；文档身份、revision 或主选区变化
使旧会话失效，不能在松开时覆盖新状态。100%/125%/200% DPI 与 1/1.21 zoom 的
交叉组合已有框选、辅助框、根/嵌套 Stack 拖拽和取消/undo/redo 的 headless 回归；
另覆盖八个方向手柄、父 padding/子 margin 和没有父容器的根节点尺寸调整。

### 4.16 G-D15：统一诊断和恢复模型

#### 诊断结构

现有 `DslError` 已有文件、行、列、期望和实际 token；设计器需要扩展为跨阶段稳定的
`DesignDiagnostic`，至少包含：

```text
code            // 稳定机器码，例如 parse.unexpected_token、ref.missing
severity        // info / warning / error
stage           // read / parse / migrate / schema / reference / compile / save
message         // 面向用户的本地化前文本或默认文本
file + sourceSpan
documentId + nodePath + property
related[]       // 相关节点、引用定义或上一条诊断
recoverability  // continue / placeholder / keep-last-frame / block-save
```

错误码而不是展示文案作为测试和 CI 的稳定断言；行列、节点 ID 和属性名用于编辑器跳转。
多个相同引用错误仅在 `(code, file, documentId, nodeId, nodePath, property, sourceSpan)`
完全相同时去重，同时保留发生次数；不同节点或源位置必须各自保留。警告
不能让文档静默丢字段，错误不能被“替换为空值”掩盖。

#### 恢复矩阵

| 阶段 | 可继续的结果 | 必须阻止的动作 |
| --- | --- | --- |
| 读取/解析 | 展示上一次有效 DOM/画布和错误位置 | 覆盖原文件、把半解析树当可保存文档 |
| 迁移/schema | 保留旧对象供查看，允许另存为修复文件 | 以未知字段已丢失的对象覆盖源文件 |
| 引用解析 | 替身/占位节点、引用面板显示缺失项 | 解引用空指针或调用未授权业务服务 |
| 编译/布局 | 保留上一棵有效运行树，错误节点可高亮 | 把失败编译结果写入 undo 栈或保存 |
| 保存 | 原文件保持不变，保留临时/恢复副本 | 删除唯一有效副本或报告“成功” |

命令行 dump、测试 fixture 和 GUI 错误面板应消费同一结构化诊断；格式化为文本只是展示
层。这样 D1 的运行时诊断和 D2/D3 的文档诊断可以共享过滤、复制和定位行为。

`read.io`、`store.read`、codec 读取错误和 `project.read`/`project.codec.*` 归属 read；
迁移失败归属 migrate，版本/身份/schema 校验归属 schema；文件写入、备份、替换和
外部 revision 冲突归属 save。读取入口不能把 `store.*` 全部覆盖为 save；保存时的
校验仍保留实际阶段，但 recoverability 标记为 block-save。存储诊断保留实际文件来源，
主文件和 `.bak` 的迁移/schema 错误分别定位，不用 `<design>`/`<project>` 代替路径。
去重 key 按字段长度编码，文件或 documentId 含换行也不会与另一组位置字段碰撞。

`lumen-designer --headless --dump-diagnostics --file <path>` 输出一行
`diagnostics_json [...]`，使用 `serializeDesignDiagnostics` 序列化 GUI 聚合后的同一份
诊断（包括工程错误），保留上述所有字段、expected/found、nodeId 和 occurrences。
JSON 转义换行、引号、反斜线和控制字符，保留输入排序；同一输入重复输出一致。
窗口模式也支持该选项，`--watch` 每次重载后输出当前诊断；正常运行不默认导出 JSON。

### 4.17 G-D16：资源信任边界与异步生命周期

设计器打开的文件可能来自项目目录之外，文档中的资源路径、字体、图片、主题和 controller
引用不能默认为可信。文档格式是数据，不是可执行脚本；打开文档时不得通过字符串隐式
加载 C++ lambda、动态库、网络 URL 或数据库查询。RuntimeContext 应提供显式能力策略：

- 资源 URI 只允许约定的 scheme 和项目/工作区根目录，保存时优先写相对稳定路径或资源
  名；路径规范化、大小写规则和符号链接策略要有测试。工程页面和资源在读取、同路径保存、
  另存为时均校验符号链接解析后的路径，包括尚未存在但父目录已存在的目标；源和目标均须
  保持在对应 manifest 的工程根内。
- 主题、图片、字体、数据源和组合件 controller 通过类型化命名引用解析；引用表按文档
  会话隔离，不能复用已销毁窗口或另一份文档的借用指针。
- 预览默认使用固定值、快照或占位 source，网络/数据库/文件观察器等副作用能力必须由
  应用明确开启并可撤销；加载失败只产生诊断。
- 异步资源加载带有文档/编译代数 token。旧编译完成后到达的图片或字体结果只能丢弃或
  更新仍匹配的预览代数，不能把旧资源写入新文档节点。
- 保存文档时不写入 `imageId`、地址、窗口句柄、线程 ID、临时文件路径等会话值；导出日志
  和诊断也应避免泄漏未授权的绝对路径。

**验收**：离线空 context 可以打开并生成可检查的占位树；非法 scheme、越界路径、错误
引用类型和过期异步结果均有稳定诊断；关闭预览窗口后没有回调触及已销毁的 context；同一
文档在不同会话中只因资源表不同而产生可解释的预览差异，DOM 序列化保持一致。

图片应用出口按声明 URI 保存授权后的规范 URI 映射；授权根内的符号链接别名与真实
路径共用句柄，编辑画布和独立预览使用同一映射。读取/解码失败产生每个引用节点的
`resource.load_failed`（reference / placeholder），保留设计文件、documentId、nodeId、
nodePath、imageSource 和可用的 source span；不泄漏资源的宿主绝对路径。重建按当前
结果重新聚合诊断，不按帧数累加 occurrences。待完成请求带文档/session/编译 token，
编译关闭以只捕获 weak ResourceManager 和句柄的回调取消工作；过期结果由资源句柄
代数丢弃。已就绪的不可变像素可以由仍授权、URI 匹配的新编译接收，新 token 验证后
才写入预览。撤销授权或销毁应用释放其所有句柄，不改变声明、dirty 或历史。

两个窗口共享 CPU 资源缓存，但 renderer 的上传状态独立。每个 AppShell 持有
`ResourceUploadCursor`，独立接收 upload/unload；替换 renderer 或设备恢复会重新上传。
无指定窗口的完成事件通知所有共享该 manager 的活动窗口；有 WindowId 的事件只通知
该活动窗口，已关闭目标不回投另一窗口。多个 manager 每轮分别 pump 一次。

### 4.18 设计器自身的可访问性、键盘和性能前置

这部分不是框架核心缺口，但若不先写成验收条件，D3 很容易只对鼠标和单一 DPI 可用。

- 工具箱、树、大纲、属性表、错误列表和状态栏必须生成有稳定 label/role/value 的语义树；
  选中节点、错误节点、不可编辑属性和预览模式不能只用颜色表达。高对比、字体缩放和
  `reduceAnimation` 复用现有 Theme/Accessibility 设置。
- 画布提供键盘等价路径：聚焦节点、方向键移动/重排、Shift 多选、Delete、Escape 取消、
  Ctrl/Cmd+Z/Y、复制/粘贴和属性面板导航。命令名称与 `CommandRegistry` 保持单一数据源，
  IME 文本输入走现有编辑事务，不把 preedit 写入 DOM。
  大纲键盘导航只在大纲焦点域内消费，属性和引用字段的全选、光标移动仍走文本编辑路径。
  大纲行的辅助技术/键盘激活与普通节点定位共用选择路径，同步 DocumentId 选区与
  大纲 current/selected；随后重建不能被旧大纲选区覆盖。属性字段随所选节点更新，
  选择/定位不改 DOM、revision、dirty 或文档历史。
  组合输入期间 Ctrl/Cmd+Z/Y 不改文档历史；提交仅替换进入组合输入前的原文选区，
  不能用 preedit 长度裁切原文。取消恢复该选区，提交只产生一个可撤销的文档事务。
  Escape 在有组合输入时先取消输入并保留字段焦点，不执行预览组件的关闭/路由返回；
  组合输入结束后的返回动作与按钮使用同一预览刷新路径，同步编辑器和独立预览，
  不改变声明、文档 revision 或历史。
- headless 每个用户流程都要有确定性 DOM/diagnostic 输出；预览 frame hash 排除时间、
  指针地址和异步到达顺序等非声明因素。`--frame-overlay` 这类实时读数只能显式开启，
  关闭态不得改变既有 frame hash 或性能基线。
- 性能门槛先用代表性 fixture 建立基线：L0 12 节点、100 节点编辑页、1000 节点大纲和
  含虚拟列表/组合件的预览各一份，分别记录 parse/schema/compile/layout/paint 总耗时、
  峰值内存和重建次数。DP-8 选择四类 fixture 的相对基线门禁，不冻结跨机器的绝对毫秒数。
  当前先交付 headless 的 12/100/1000 L0 fixture，覆盖规范化
  serialize/read、schema、compile、layout 和 CPU paint；它记录阶段耗时并断言重复运行的
  DOM、RenderNode 和 frame hash 一致。当前另有运行时 Widget 虚拟列表 fixture，验证大列表
  只物化可见窗口；L2 DesignDocument 虚拟列表/组合件 schema 已登记。独立进程
  `lumen-designer-memory-probe` 已为预览打开、属性编辑重建和 VirtualList layout/paint
  记录 scoped global `new/delete` 的真实 live/peak heap bytes；这些读数只作为同一环境
  的相对基线，不冻结跨机器绝对字节阈值，也不替代 R6 生产整帧 allocator 契约。
  100 节点文档事务和 1000 节点大纲投影的 headless 基线已具备；
  `DesignPreviewFrame::rebuildCount()` 现已记录每次预览编译尝试（含可恢复失败），并由 D3
  测试验证成功、占位失败和保留上一帧失败的计数；`VirtualListController` 另记录当前/峰值
  物化项数并由 L2 fixture 验证可见窗口约束；运行时 VirtualList fixture 与 L2
  `DesignDocument` VirtualList 的 compile/layout/paint fixture 的 scoped heap 读数已由上述探针
  补齐，L2 组合件（如 DataGrid）的真实预览容器仍按应用适配层接入。
  四类 fixture 的性能门禁仍只比较同一环境下的相对基线，不冻结跨机器绝对阈值。
  F6 采集器现位于 `benchmarks/designer_bench.cpp`：无参数仍提供原内存探针，
  `--benchmark --warmup N --iterations N` 导出 `l0_12`、`edit_100`、`outline_1000`、
  `virtual_list_1000` 的阶段 p50/p95、C++ 分配量、scoped heap 峰值、重建数和 frame hash。
  编辑 fixture 绘制编辑后的预览；重复采样检查文档/渲染节点数、虚拟物化项数和 frame hash。
  构建类型由 CMake 编译进报告，未指定类型如实报告 `Unspecified`。
  `run_designer_perf_gate.py` 已接入 Linux CPU CI：干净的 `a1e012e` 基线与候选在同一逻辑
  CPU 交错采样五次，仅接受 Release；逐项比较四类 fixture 的计时、分配/堆峰值、重建数
  和虚拟化范围，并归档原始报告及门禁结果。10% 相对阈值与 50 微秒计时噪声下限沿用
  现有 scene 门禁，详见 [`Designer 性能基线`](perf-baselines/designer/README.md)。
  `580ea50` 与固定基线的干净检出已按此协议完成本地 Linux 对比，minimum 通过、median
  保留一项 `l0_12` paint p95 +12.21% 回退，联合门禁通过；完整源码/产物身份和限制见
  [`本地性能证据`](platform-evidence/designer-perf-linux-2026-10-08.json)。该结果不替代真实 CI。

**验收**：键盘和辅助技术可以完成 D2 的选择/定位与 D3 的属性编辑/保存；高 DPI、高对比
和字体缩放下布局、命中和语义仍一致；相同输入重复运行产生相同规范化文档和诊断排序。

## 5. 次级缺口与绕行

| ID | 现状 | 影响 | 绕行 | 升级为正式工程的条件 |
| --- | --- | --- | --- | --- |
| G-D4 绝对定位 | 仅 Stack 子级 `stackPosition`（`withStackPosition`），无通用任意坐标容器 | 自由画布无法直接摆放 | 设计器约定「画布根 = Stack，子项一律 `stackPosition` 定位」；嵌套布局容器内仍走正常布局流 | 若需要画布内混合布局/绝对模式切换的容器语义，按新控件流程立项（设计文档 + mockup） |
| G-D5 自绘光标 | 平台层仅 11 种系统形状光标（SDL `SDL_CreateSystemCursor`） | 精确编辑光标（十字/旋转/斜切）缺位 | 系统形状 + overlay 模拟（框架非模态视觉 overlay 现成） | 编辑体验明确不够用且 overlay 模拟有闪烁/延迟问题时，平台层扩展自绘光标 |
| G-D6 OS 拖出 | `PlatformCapabilities.dragDropStart` 恒 false（SDL 3.2.10 无 API，结构化不可用，`include/lumen/platform/application_host.h`） | 无法把控件拖到外部工具 | 无需绕行：设计器核心交互是**应用内**拖入画布（M15 状态机现成）+ OS 拖入（可用） | SDL 升级后随 M15 重评，不占设计器工作项 |
| G-D7 Canvas 控件 | 无通用自绘面板控件（自绘走 Icon 折线或自定 painter 路径） | 画布标尺、参考线、吸附提示、选框手柄需要密集自绘 | 首版用非模态视觉 overlay（普通 RenderCommand，三后端一致）+ Icon 折线承载 | overlay 表达不了（如大量动态几何/命中交互）时，按新控件流程立项 |

## 6. 分阶段路线与依赖

```text
R6 开发者诊断（dump / HUD / inspector / bounds·damage overlay，headless 已验证）
        ↓                                      ←—— D1：并入 R6 执行，无需前置工程
D1 检查器（只读诊断）
        ↓（选择/大纲/overlay 地基）
D2 预览工作台（只读）                          ←—— P1/P3/P4；P2 可选（属性只读有降级路径）
        ↓ DP-1–DP-9 + P1–P5 + G-D13–G-D16
D3 可编辑设计器
```

### 6.0 P1–P5 的逐步实施顺序

每一阶段都必须先完成“接口已存在”和 headless 验收，再进入下一阶段；不得先铺开工具箱
控件数量而把文档契约留到最后。

| 阶段 | 交付 | 必须先验证 | 失败时的处理 |
| --- | --- | --- | --- |
| F0 | 现有边界核对 | 当前 DSL/Element/热重载历史边界回归通过；完整 `ctest` 结果另记 §10.3 | 修正事实或范围，不写新 API |
| F1 / P1 | L0 `DesignDocument`、解析/编译/规范化输出 | DOM round-trip、C++ builder golden、旧 DSL 测试不变 | 停在 L0，不进入属性全量登记 |
| F2 / P2 | L0–L3 `NodeSchema`/`PropertySpec` | 逐属性读写、默认值、非法值和字段登记守护 | 保留旧 converter，修正 schema 漂移 |
| F3 / P3 | RuntimeContext、preview session、引用诊断 | 缺失/错误引用、source lease 生命周期、组合件替身和副作用禁用 | 禁止保存运行时指针，回退占位树 |
| F4 / P4 | DocumentId、CompileTrace、SourceMap、坐标变换 | 插入/复制/重排/撤销后的选择、bounds 和源码定位 | 禁止用 runtime identity 充当文档 ID |
| F5 / P5 | DocumentStore、版本迁移、原子保存和恢复 | 旧 fixture、损坏输入、保存中断、旧画布保留、未知字段策略 | 原文件不变，报告结构化错误 |
| F6 / D3 | 工具箱、属性面板、结构编辑、undo/redo、资源授权 | P1–P5 + G-D13–G-D16 全部通过后再做真实编辑流程 | 退回只读 D2，不宣称双向完成 |

F0 是源码边界核对；F1/P1 已完成 L0 语义 DOM、`.lumen` 导入、规范化设计文档 codec 和
DOM→Widget 编译入口；F2/P2 已完成 L0、L1 静态、L2 动态和首批 L3 组合件节点（37 个 schema，含
`Grid`/`Image`/`Icon`/`Slider`/`ProgressBar`/`Radio`/`Tooltip`/`Dropdown`/`Tabs`/`ThemeScope`/
`VirtualList`/`List`/`Tree`/`TreeList`/`Splitter`/`ComboBox`/`ColorPicker`/`Spin`/`ToolBar`/
`StatusBar`/`Menu`/`DialogHost`/`Navigator`/`Form`/`DataGrid`）的
`NodeSchema`/`PropertySpec` 显式注册表，并由编译
入口执行类型、枚举、范围、子节点、引用存储位置和不可持久化预览属性约束，并以
`widgetFieldInventory()` 守护 Widget 字段分类；F3/P3 已完成类型化 `DesignRuntimeContext`、
离线 map context、引用诊断和带代数、可取消关闭回调及引用 lease 保活的
`DesignRuntimeSession`，并可把 VirtualList/Splitter source 注入编译 Widget；引用缺失或类型错误时保留节点类型/几何用于检查，同时禁用该节点并
清除 `bind`/`onClick`，避免未解析引用触发业务行为。
`designer_document_tests.cpp`
覆盖 DOM round-trip、C++ builder golden、未知节点/损坏输入、未知字段保留、重复 ID 和
P1 运行时引用拒绝；`designer_schema_tests.cpp` 覆盖 37 节点注册（含 L1 静态、L2 动态和首批 L3 组合件）、访问器、非法属性和
结构诊断、引用错误存储位置和 `PreviewOnly` 字段拒绝；`designer_runtime_context_tests.cpp` 覆盖成功解析、缺失引用、错误类型引用、frame 保留、
session 关闭回调（含 session 析构时的 RAII 关闭）、组合件 builder 占位/异常诊断和组件 lease 保活。F4/P4 已完成 `DesignSourceMap`、`DesignCoordinateTransform` 和值语义的
`DesignDocumentEditor`：节点/属性 source span 会随 codec 往返保留，编辑操作覆盖普通子节点
和命名 slot，并在插入/复制时清除外来 source span、分配新 ID、在重排时保持原 ID；编译结果
同时返回 SourceMap 和 CompileTrace。P2 registry 已覆盖 L0、L1、L2 与首批 L3 节点；P3 已接通 source handle 注入、组合件 builder 和 lease 保活，DesignerApp 已注册首批离线
`preview_rows`/`preview_splitter` source adapter，未知 source 名称仍显示缺失诊断；其余真实
widgets controller 适配和资源裸指针仍由应用层按需注册；F5 的
DocumentStore、0→1 迁移、未知字段保留、原子保存和 `.bak` 恢复已完成；主文件的读取、迁移或
schema 失败均尝试有效恢复副本。F6 已增加设计器
应用层的 `DesignDocumentTransaction`/`DesignDocumentHistory`、会话级 `DesignSelectionModel`
（含基于稳定文档遍历的范围选择）
和统一 `DesignDiagnostic` 基础契约；事务覆盖副本提交、失败回滚、mergeKey 与 750ms
空闲窗口（保存、undo/redo 和目标变化会阻断合并）、undo/redo、选择恢复及
document/saved revision，诊断覆盖阶段、source
span、恢复策略和稳定去重。F6
同时通过 `DesignDocumentEditor` 提供 schema 门控的声明属性/运行时引用编辑，编辑成功或清除时
移除失效的属性 source span，拒绝 `PreviewOnly` 和错误引用值。
同一文档事务还保持根节点的 `DesignNodeId` 不变，防止在未切换 `documentId` 时替换选择和
SourceMap 的根锚点。
又增加了默认拒绝的 `DesignResourcePolicy`/`DesignResourceAuthorizer` 以及文档、session、
compile 三元代数校验；F6 资源阶段现已把图片声明接入设计器应用的显式 `project://` 授权根、
异步 `ResourceManager` 和占位替换，资源句柄随文档资源集合变化释放，未授权 URI 进入可定位诊断；
真实 D3 应用出口当前已接通 L0 属性面板编辑、命名引用管理、文档级撤销/重做、结构插入/复制/删除/重排、
由 27 个非组件 schema 驱动的 L0–L2 工具箱，并增加首批 L3 `DataGrid`/`ToolBar`/`StatusBar` 专用入口、第二批 `ComboBox`/`ColorPicker`/`Spin` 专用入口（组件预览、稳定 key 前缀、Spin 键盘/滚轮/自动步进接线）和第三批 `Menu`/`DialogHost`/`Navigator`/`Form` 专用入口（菜单栏键盘路径、对话框生命周期、路由返回、表单校验），打开/保存/另存为命令和 `.design`/`.lumen` 文件对话框路径；L1/L2/L3 schema 已可被私有设计文档编译，L3 组合件均由应用注册 builder，离线 RuntimeContext 已提供 `light`/`dark` ThemeScope 引用，其他资源类型仍按组件逐步补齐。G-D12 已增加独立的 `DesignPreviewState`，runtime snapshot 和
visual override 只在预览会话中覆盖读取值，不改变 DOM、dirty 或设计文档序列化；schema 会拒绝
`PreviewOnly`/`Derived` 属性进入可保存文档。
引用面板现已接入中心页签：DataGrid 展示当前文档的命名引用、引用类型、节点位置和离线 `Stub`/
`Missing` 状态，激活行可定位大纲节点；该面板不改变运行时引用解析或保存格式。
D2 Source 页签现已提供只读源快照、行号和诊断行定位；文档编辑后展示当前规范化
`DesignDocument` 输出，未改变 DP-5 的持久化决策。
运行/调试/停止命令现已接通当前 `DesignerApp` 会话的编译刷新与 R6 帧 HUD/bounds
调试层；编译失败保留上一份有效画面并报告状态。`DesignerApp` 现在向 `runApp` 注册独立的
预览 `AppShell`，运行/调试/停止会同步其最后一次成功编译的 Widget、状态页和调试层，编辑器
窗口与预览窗口的壳状态互不共享。
画布 Guides 开关现已接通 Theme 令牌驱动的标尺、中心参考线、位置/尺寸标注、选择框和八个
手柄；辅助节点排除语义与焦点，预览节点仍保持最上层命中。首帧先建立画布几何，再启用
Guides 即可生成定位层。手柄拖动使用独立拖放源，会话中只更新辅助层预览，释放时以一个
文档事务提交 `width`/`height`，Stack 子项的可表达位置同时提交 `left`/`top`；尺寸边缘按
8px 网格、画布边缘和中心线在 6px 阈值内吸附。普通流式父布局不会伪造绝对位置写回。
画布视图还提供 50%–200% 的缩放、滚轮/方向平移和重置命令；这些操作只重建视图，不产生
文档事务。缩放后的 source adapter 仍通过 session lease 保持生命周期，命中链与 overlay
使用同一份实际 RenderNode 几何。

### 6.1 D1：检查器（并入 R6，无前置工程）

- **范围**：运行时 Widget/Element 树可视化（悬停/钉住 → bounds/damage overlay）；
  `--dump-tree`/`--dump-style`/`--dump-semantics`、Inspector 和帧统计 HUD 已交付，作为定位后端。
  约束沿用 M18：默认关闭零开销
  （关闭态 frame hash 与性能基线不变，CI 断言）。
- **不依赖**：P1/P2/任何文档格式。
- **出口条件**：R6 在支持矩阵的四态推进；Linux/Xvfb headless 已验证，inspector 三桌面窗口现场 smoke 仍待补齐。

### 6.2 D2：预览工作台（只读）

- **范围**：headless 画布 + 点选高亮 + 大纲面板（Tree）+ 只读属性面板 +
  交互状态预览（`setVisualPreviewState`）+ 环境/主题/密度切换 +
  `.lumen` 错误面板（file/line/column）与 `--watch` 热重载联动。基础 D2 可直接消费
  已构建的 Widget；文档定位版必须使用 P1 的 DOM 和 P4 的 `DocumentId`，不能直接使用
  无 key 节点的运行时路径 identity。
- **降级路径**：P2 未做时，只读属性可用 `dumpRenderTree`/`RenderNode`
  公开字段（offset/size/style 摘要）承载；P3 引用缺失时使用离线 placeholder；这两种
  降级都不能宣称支持文档编辑。
- **出口条件**：打开 counter/gallery 级别样本无功能缺失；错误定位可复核；编译失败保留
  上一份有效画面；三桌面窗口 smoke。

当前已交付 D2 的基础出口：`DesignPreviewWorkbench` 串联 `.lumen`/设计文档读取、预览帧
恢复、DocumentId 选择、CompileTrace 定位、大纲和只读属性投影；`lumen-designer` 示例已
接入真实 `AppShell`，提供只读画布、大纲树、属性/诊断面板、主题/密度/DPI/字体缩放/
高对比/交互状态预览、`--watch` 热重载、`--headless` 冒烟和 Linux/Xvfb 窗口 smoke；
counter、覆盖全部 L0 节点的 `gallery.lumen` 和覆盖全部注册 schema 的 `gallery.design` fixture
已纳入同一组验证。self-hosted platform acceptance 现在在 Linux X11、Linux Wayland、macOS
和 Windows 会话中直接启动 `lumen-designer --file gallery.design --max-frames 3`，并要求
进程成功退出后日志出现唯一完整行 `designer_window_smoke pass`，且证据记录的
`designer_window_smoke` 为 `pass`；人工附件与本次 Designer/allocator 日志须通过哈希匹配，
旧日志不能替代本次运行。本工作区只取得 Linux Wayland 的一次短 smoke，
不能替代四个平台的完整 acceptance 记录。

D2 工作台的 UI 设计稿见 [`design/designer.html`](../design/designer.html)（2026-10-01，v2）：四带三栏
简洁布局（工具栏 + 大纲/画布/属性 + 诊断带 + 状态栏），面板控件全部映射到既有 widgets 层
（`ToolBarController`/`TreeController`/`DataGridController`/`StatusBarController`/`SplitterController`/
`ComboBoxController`/`SpinController`，视觉值取视觉系统语义 token）。v2 追加三块体验：
画布对齐辅助（标尺、智能对齐参考线、距离标注、选择手柄——overlay 承载的目标态与可交互
拖动演示，视觉 token 冻结在 visual-system §3.5，吸附引擎属设计器自建）；工具栏
运行/调试/停止组（编译当前文档 → `runApp` 多窗口独立 preview-session +
`RunOptions.frameDebugOverlay` 帧 HUD，编译失败保留上一帧）；以及中心「画布/源码」页签
（`makeTabs`：D2 源码只读 + 诊断行定位跳转，「编辑 → 编译并运行」为 D3 目标态交互演示，
管线复用 FileWatcher/DslCache/swapRoot）。框架缺失项（G-D7 Canvas 自绘、G-D4 绝对定位
容器、G-D5 自绘光标、P2 属性元数据 L1–L3 覆盖、专用缩放件、Stop/Debug/Magnet 命令图标）
在稿内以「缺失 · 需补充」显式登记并给出绕行与立项路径。该稿是 D1/D2/D3 共用外壳的
UI 对照基线；D3 L0 的实现授权以本文 §8 决策和 §6.3 范围为准，稿内 D3 交互演示不改变
§6 阶段前置。2026-10-01 v3 再落「设计工程」目标态：左栏「工程」页（页面 / 资源树——
manifest 视图；文档切换按 documentId 隔离、诊断跨文档聚合并可跳回出错文档）与新建 /
导入 / 保存工程命令菜单（ContextMenuController / CommandRegistry 先例，图标复用
IconId::Plus / Document / Folder / Image）；工程容器（清单格式、工程身份、跨文档引用
协议）已按 DP-9 方案 A 落地为 D3 后独立范围：`DesignProject` 清单保存页面集、资源表、工程根、schemaVersion、工程身份和跨文档引用，`ProjectStore` 提供迁移、原子保存、revision 冲突与 `.bak` 恢复；`DesignerApp` 已接入多文档会话、按 `documentId` 切换、工程级新建/打开/保存、资源根授权和工程页列表。v4 再落「引用面板」
目标态：中心第三页签（DataGrid）呈现当前文档全部命名引用（bind / handler / 资源名）与
RuntimeContext 解析状态（替身 / 已解析 / 缺失），`ref.missing` 以警告行进诊断带并可定位
引用节点——这是设计器与代码实现的关联面（文档写稳定名称，行为在 C++ 注册表解析）；
数据侧复用 P3 类型化解析结果，面板 UI 属设计器应用实现，随 D3 落地，无框架缺口。

### 6.3 D3：可编辑设计器

- **前置**：DP-1–DP-8 已于 2026-10-01 决定（私有规范化格式/L0/独立 preview-session、与阶段 A 并行、SourceMap、扩展字段保留、离线授权预览、相对性能基线）+ P1（文档格式与双向转换）+ P2（属性/节点元数据）+
  P3（RuntimeContext/引用注入）+ P4（DocumentId/SourceMap）+ P5（版本迁移/恢复）+
  G-D13（命令事务）+ G-D14（选择/坐标）+ G-D15（诊断）+ G-D16（资源生命周期）+
  编辑命令栈（undo/redo：CommandRegistry 分发 + 文档事务模型，参考
  `editing_history.h` 先例）+ 文档持久化（Preferences 原子写模式或独立文件写入）。
- **范围**：首批闭环仍是 L0 的 12 个节点；当前按覆盖矩阵增量开放 L1/L2 的 schema 驱动工具箱和声明属性默认值，
  L3 组合件保留给有应用 builder 的专用入口。控件工具箱拖入（应用内拖拽）、属性就地编辑（P2 驱动）、
  结构拖拽重排、DocumentId 选择保持、命名引用管理、保存/导出、可选 `.lumen` 导入；
  运行时 Widget 只作为预览编译结果，不作为保存源。
- **出口条件**：P1–P5 与 G-D13–G-D16 验收全绿；设计器自身作为 Lumen 应用自举运行
  （dogfooding 即回归证据）；三桌面真实平台验收按四态登记；DP-8 的性能相对基线无回退。

## 7. 风险与对策

| 风险 | 表现 | 对策 |
| --- | --- | --- |
| 元数据/格式与 `Widget` 字段漂移 | 新增控件字段未进注册表，面板/序列化静默丢字段 | P2 字段计数守护测试强制同步（§4.2） |
| 把运行时 Widget 当文档源 | 保存了 bounds、焦点、controller 指针或展开后的动态行，重开后无法还原 | DOM 作为唯一编辑源；P3 明确引用注入和声明/运行时字段分类 |
| 节点身份漂移 | 重排或复制后无 key 节点的选中项、错误和属性面板指向错误节点 | P4 独立 DocumentId + SourceMap；运行时 identity 只作映射结果 |
| 文档演进失败 | 新版本字段静默丢失，保存损坏文件覆盖原文件 | P5 版本迁移、未知字段策略、原子替换和恢复副本 |
| 编辑事务不完整 | 多字段编辑、拖动或粘贴留下半提交 DOM，undo 后选择/诊断漂移 | G-D13 的 precondition、单意图事务、mergeKey 和 document/saved revision 分离 |
| 绑定/数据源不可预览 | 打开文档依赖业务 StateStore、网络或 controller，设计器无法离线渲染 | RuntimeContext 注入替身；缺失引用显示诊断和占位树 |
| session lease 过早释放 | RenderNode 仍持有 source/theme 指针，异步资源回调触及已关闭窗口 | P3 的 `DesignRuntimeSession` 保活、关闭时取消异步任务、代数 token |
| 运行时选择误写文档 | 多选/框选或动态行使用 runtime identity，重排后属性面板指错节点 | G-D14 的 DesignSelection、CompileTrace 过滤和明确坐标空间 |
| 诊断不可操作 | 错误只能读字符串，无法跳转、去重、决定是否保留旧画面 | G-D15 稳定错误码、source span、recoverability 和统一展示层 |
| 外部文件和资源越权 | 文档路径/引用触发任意业务副作用，日志泄漏绝对路径 | G-D16 的 scheme/根目录策略、离线默认、类型化引用和授权会话 |
| 双格式认知负担（方案 B） | 使用者混淆 `.lumen` 与设计器文档 | 文档明确边界：`.lumen` 手写受限 DSL；设计器文档是工具数据；单向导入是桥 |
| 自举风险 | 设计器依赖框架最新行为，框架改动即破坏设计器 | 反向价值：设计器入 CI 即自举回归；headless 画布对照保证确定性 |
| 与收敛计划阶段 A 抢占优先级 | 三桌面真实验收（R0/R1）是当前阻塞项，设计器插队推迟「实战可用」宣称 | DP-2 显式取舍；D1 因属 R6 可随阶段 D 穿插，不受此冲突影响 |
| 新依赖引入（JSON，方案 B） | 供应链与构建面扩大 | FetchContent pin 修订 + 变更说明登记（AGENTS.md）；或自写极简 reader 规避 |
| 画布实时重排性能 | 编辑期高频重建 | damage/局部重绘与 FrameScheduler 现成；Gallery（约 4700 行应用）为先例；性能门槛照常执行 |
| 设计器自身不可访问 | 只能鼠标操作、颜色表达状态或字体缩放后面板溢出 | §4.18 的语义、键盘、高对比和字体缩放验收；三桌面读屏证据单独登记 |

## 8. 待决策清单

| ID | 决策 | 状态与选项 | 影响范围 |
| --- | --- | --- | --- |
| DP-1 | 设计器文档格式 | **已决定（2026-10-01）：B，设计器私有格式 + `.lumen` 单向导入**。 | P1 使用独立 magic/schema/codec；不扩展 `.lumen` 的写回语义。 |
| DP-2 | 排期取舍 | **已决定（2026-10-01）：与阶段 A 并行推进；三桌面真实验收仍是发布门槛，设计器 headless 通过不替代平台验收**。 | 设计器可继续完成前置和 D3 应用出口；“实战可用”宣称仍受阶段 A 证据约束。 |
| DP-3 | D3 首版控件覆盖 | **已决定（2026-10-01）：L0 的 12 个节点先闭环，L1–L3 按覆盖矩阵逐批加入**。 | 最终目标仍是所有有稳定声明语义的类型和组合件。 |
| DP-4 | 运行时状态保存策略 | **已决定（2026-10-01）：只保存声明初值，运行时状态独立于设计文档，归 preview/session 管理**。 | P3/P5/undo/redo 均按文档 revision 与运行态隔离实现。 |
| DP-5 | 源文本保真度 | **已决定（2026-10-01）：规范化输出 + SourceMap，不做 CST 局部 patch**。 | `.lumen` 作为导入源；设计器文档保存规范化结构，SourceMap 负责会话内定位。 |
| DP-6 | 未知节点/字段策略 | **已决定（2026-10-01）：保留扩展字段并原样写回；无法保留时拒绝保存**。 | P1/P5 codec、迁移安全和未来版本兼容；禁止静默丢字段。 |
| DP-7 | 预览资源与副作用权限 | **已决定（2026-10-01）：离线固定值/快照/占位为默认，应用适配器显式授权**。 | P3 session、G-D16、可重复 frame hash 和安全边界。 |
| DP-8 | 设计器性能门槛 | **已决定（2026-10-01）：先建立四类 fixture 基线，要求相对基线不回退，不冻结跨机器绝对毫秒/内存目标**。 | §4.18、F6 出口和 CI 资源预算。 |
| DP-9 | 设计工程容器 | **已决定（2026-10-01）：A，工程 manifest + 多文档会话**。`DesignProject` 清单保存页面集 / 资源表 / 工程根 / schemaVersion / 工程身份 / 跨文档引用；`ProjectStore` 负责迁移、原子保存、revision 冲突与 `.bak` 恢复；设计器按 `documentId` 隔离页面、选择与预览，并聚合工程诊断。 | P5/G-D16 工程存储和资源授权已接入；最近文件 Preferences 仍作为后续便利功能，不替代工程容器 |

## 9. 决策后的文档同步动作

任一 DP 决策后，同一变更内完成（AGENTS.md 规则）：

1. 本文：对应 DP 状态从「待决策」改为决策结论与日期；本次已同步 DP-1–DP-9，并更新 P1–P5、G-D13–G-D16 和四态证据；
2. [`lumen-gui-completion-plan.md`](lumen-gui-completion-plan.md)：D2/D3 已立项，缺口矩阵以
   R11 登记；方案 B 需在 §6 排除项处补「数据序列化 ≠ 可编程化」
   的边界注记（或确认无需改写）；R6 保留调试层已交付记录和三桌面现场 smoke 待验收项；
3. [`lumen-gui-framework-plan.md`](lumen-gui-framework-plan.md) §6.2：方案 B 登记
   设计器文档格式的存在与边界；方案 A 则改写冻结策略并同步各控件设计文档「已知限制」；
4. [`support-matrix.md`](support-matrix.md)：P1–P5/D1–D3 各交付批次按四态登记，区分
  headless dump/HUD 已交付与真实平台 Inspector smoke；
5. 涉及新视觉（overlay 高亮、吸附辅助线等）：同步
  [`lumen-visual-system-design.md`](lumen-visual-system-design.md) 与 `design/*.html`。
6. 若决策引入 JSON/其他第三方 codec、资源沙箱或生成式字段清单：同步 CMake pin、许可证、
   构建选项、发布包和安全审查记录；若只改变本文的规划，不修改源码或构建配置。

后续每个工作项的变更说明沿用完成计划 §5 的任务验收模板。

## 10. 评审证据与实施前检查表

### 10.1 源码事实索引

下表只列出本轮判断直接依赖的事实。路径是查证入口，不表示这些文件已经提供设计器
契约。

| 事实 | 查证入口 | 对设计器的结论 |
| --- | --- | --- |
| `.lumen` 解析返回 Widget，错误带 file/line/column | `include/lumen/dsl/text_dsl.h`、`src/dsl/text_dsl.cpp` | 需要 DOM 解析入口和兼容包装；不能把 Widget 当编辑源 |
| parser 允许列表与 WidgetType 不同 | `widgetTypeOf`、`WidgetType` 枚举 | L0 只代表文本 DSL 子集；覆盖矩阵必须独立维护 |
| Widget 携带 source/theme/controller 指针 | `include/lumen/core/widget.h` | 文档只保存稳定命名引用；编译结果必须有 session lease |
| StateStore 绑定和 AppShell 重建在运行时解析 | `include/lumen/core/state.h`、`src/app/app_shell.cpp` | 设计器需要 preview StateStore；不能复制 `applyBinds` 或保存业务值 |
| RenderNode identity 由 key/位置生成 | `src/layout/layout.cpp`、`include/lumen/core/render_node.h` | runtime identity 只能做 CompileTrace 结果，不能当 DocumentId |
| visual preview state 以 key 查找 | `include/lumen/app/app_shell.h`、`src/style/resolver.cpp`、`examples/designer/designer_app.cpp` | DesignerApp 按文档会话建立 `DesignNodeId -> Widget::key` 映射；无 key、重复 key 和私有 key 碰撞均回退到确定的唯一会话 key，状态覆盖保持逐节点 |
| overlay 安装会影响事件树，模态/视觉 overlay 共槽位 | `include/lumen/app/app_shell.h`、`docs/lumen-drag-drop-design.md` | 高亮层、拖拽层和菜单必须有互斥及输入路由契约 |
| Preferences 使用 `.tmp` + rename | `src/core/preferences.cpp` | 可复用原子替换思路，不能复用扁平格式或假定已 fsync |
| SDL host 能接收 OS 拖入，拖出能力为 false | `include/lumen/platform/application_host.h`、`src/platform/sdl3_host.cpp` | 应用内拖入属于设计器范围；OS 拖出继续登记为降级 |

### 10.2 各阶段必须具备的测试 fixture

测试名称可以调整，但每个行为都必须有确定的断言和失败诊断；只测成功路径不能作为
阶段出口。

| Fixture | 覆盖行为 | 最低断言 |
| --- | --- | --- |
| `l0_document_roundtrip` | 12 节点、显式默认值、绑定、源 span、扩展字段 | 同一 codec 逐字段相等；未知字段按 DP-6 处理 |
| `l0_compile_golden` | DOM 编译与 C++ builder | Widget 声明字段相等；没有 bounds/焦点/业务快照 |
| `schema_registry_guard` | 属性读写、默认值、范围、适用控件 | 写入后读回；未登记字段和非法类型有稳定错误码 |
| `runtime_context_fixtures` | 空 context、替身 source、组合件、lease 关闭 | 无悬空指针；缺失引用为 placeholder；副作用未启动 |
| `document_id_trace` | 插入、复制、重排、删除、keyless/keyed、动态行 | 静态节点 ID 保持/复制新 ID；动态节点不进入文档选择 |
| `selection_transform` | DPI、zoom/pan、滚动、框选、Escape | 同一 DocumentId；overlay 和 bounds 在同一坐标变换下对齐 |
| `diagnostic_recovery` | 解析/迁移/schema/ref/compile/save 错误 | code/stage/span/recoverability 稳定；旧画面和原文件按矩阵保留 |
| `document_transaction` | 属性、多选、拖动、粘贴、undo/redo、mergeKey | 一次用户意图一个事务；失败全回滚；dirty/saved revision 正确 |
| `document_store_faults` | 截断、未知字段、保存中断、外部修改 | 原文件不变；恢复副本可识别；禁止静默覆盖 |
| `resource_policy_lifecycle` | scheme/根目录授权、符号链接、异步代数、session 关闭 | 非法资源有稳定诊断；绝对路径不进入文档；过期结果被丢弃 |
| `preview_state_isolation` | 声明值、绑定快照、visual override、保存/undo | 预览切换不改 DOM、不产生 dirty、不写回 codec |
| `designer_accessibility` | 语义树、键盘、IME、高对比、字体缩放 | 无颜色唯一信息；键盘完成同等流程；preedit 不进 DOM |
| `preview_determinism` | 固定 context、连续编译、异步结果乱序 | DOM/诊断/frame hash 按声明稳定；旧代数结果被丢弃 |

### 10.3 本轮核对记录（2026-10-02）

- 规划基线：`e2c61ef`；F1/P1 实现新增 `design_document.h`、`design_codec.h`、
  `design_document.cpp`、`designer_document_tests.cpp`，F2/P2 追加 `design_schema.h`、
  `design_schema.cpp`、`designer_schema_tests.cpp`，F3/P3 追加 `runtime_context.h`、
  `designer_runtime_context_tests.cpp`，F4/P4 追加 `design_mapping.h`、
  `design_mapping.cpp`、`designer_mapping_tests.cpp`，F5 追加 `document_store.h`、
  `document_store.cpp`、`document_store_tests.cpp`，F6 基础契约追加 `design_editor.h`、
  `design_editor.cpp`、`designer_editor_tests.cpp`，以及资源边界的 `design_resources.h`、
  `design_resources.cpp`、`designer_resources_tests.cpp`，以及预览隔离的 `design_preview.h`、
  `design_preview.cpp`、`designer_preview_tests.cpp`，D2 headless 地基追加
  `design_workbench.h`、`design_workbench.cpp`、`designer_workbench_tests.cpp`，D2 应用出口追加
  `examples/designer/CMakeLists.txt`、`designer_app.h`、`designer_app.cpp`、`main.cpp`、
  `gallery.lumen`/`gallery.design`、`tests/designer_app_tests.cpp`、`tests/designer_cli_smoke.cmake`、
  `tests/package_designer_smoke.py`；工作区另有既存的平台 host 修改，
  未把它们作为设计器证据。DP-9 A 方案追加 `project_store.h`、`project_store.cpp`、
  `project_store_tests.cpp`，并把 manifest、多文档会话、工程页列表接入 `DesignerApp`。
- 执行命令：`cmake -S . -B build-debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON`、
  `cmake --build build-debug --config Debug --target lumen-tests`、
  `ctest --test-dir build-debug --output-on-failure -C Debug`；Release 使用独立
  `build-release` 配置执行相同构建和 CTest 命令。
- R6 增量提交 `342a508` 建立 `render::FrameAllocationSource` 的调用方拥有、UI 线程整帧
  scope 契约；`9881360` 将 `frame_allocator_source` 加入三桌面平台验收必检项，fake/headless
  证据不能通过真实 allocator source 验收。
- P1 专属筛选 `build-debug/tests/lumen-tests "[designer][p1]"` 为 `106` 个断言、
  `9` 个测试用例通过，覆盖 12 个 L0 节点 round-trip、codec 扩展字段、独立 C++ builder
  Widget golden、codec 字符串边界、非法 UTF-8 和损坏诊断。
- P2 专属筛选 `build-debug/tests/lumen-tests "[designer][p2]"` 为 `7596` 个断言、
  `8` 个测试用例通过，覆盖 Widget 字段清单、访问器回读、registry 驱动编译、可选默认值、
  枚举 domain 和引用存储位置约束。
- P3 专属筛选 `build-debug/tests/lumen-tests "[designer][p3]"` 为 `93` 个断言、
  `13` 个测试用例通过，覆盖类型化解析、错误引用隔离、组合件 builder/占位诊断、解析异常诊断、session 关闭回调和 lease 生命周期。
- P4 专属筛选 `build-debug/tests/lumen-tests "[designer][p4]"` 为 `69` 个断言、
  `6` 个测试用例通过，覆盖 source map、DPI/zoom/pan 变换、稳定 ID 结构编辑、布局 trace 定位
  和重复 runtime identity 诊断。
- P5 专属筛选 `build-debug/tests/lumen-tests "[designer][p5]"` 覆盖进程/序列号临时文件名、
  残留临时文件避让、迁移、迁移身份校验、迁移异常诊断、恢复副本、恢复后保存、revision 冲突、重复/缺失文档 ID、非法保存、非法未知字段诊断定位和恢复副本失败清理，为 `604` 个断言、`9` 个测试用例通过。
- D2 专属筛选 `build-debug/tests/lumen-tests "[designer][d2]"` 为 `337` 个断言、
  `16` 个测试用例通过，覆盖大纲/只读属性/CompileTrace 选择、设计器应用语义壳、画布点选、
  键盘/语义激活、主题/密度/DPI/字体缩放/高对比环境预览、按 key 或私有稳定 key 的
  hover/press/focus 状态循环与选中节点重绑定、解析/编译/读文件错误保留上一帧、文件 watcher
  有效重载和错误恢复、预览切换保持工作台 frame generation、运行/调试/停止命令、独立预览
  AppShell 状态镜像、画布辅助层开关以及显式会话清理。
- D3 当前 headless 切片筛选 `build-debug/tests/lumen-tests "[designer][d3]"` 为 `956` 个断言、
  `43` 个测试用例通过，覆盖 L0 声明属性编辑、L1/L2 schema 工具箱插入与默认值面板、命名引用编辑、引用面板、schema 拒绝运行时属性、事务 dirty/revision、
  undo/redo、私有设计文档保存/重开和文件 revision 基线；应用层覆盖属性面板 TextField
  绑定、Ctrl/Cmd+Z、Ctrl/Cmd+Y、Ctrl/Cmd+C/V 结构化节点复制粘贴（含多选跨父节点批量粘贴），多选 Delete/上移/下移，L0–L2 工具箱按钮，以及插入/复制/删除/上移/下移命令的
  语义激活、键盘等价路径、Shift 区间/Ctrl-Cmd 切换多选、画布多选标记、选择恢复、动态虚拟行回选 source 节点、结构编辑后的预览态清理、大纲指针拖拽重排、工具箱拖入画布、诊断导航、私有 `.design` 重开、文件对话框结果回传和从当前文档新建工程；另覆盖画布 zoom/pan/reset 的 RenderNode 几何变换、预览 frame generation 与其对 dirty/revision/undo 的隔离。
- DP-9 专属筛选 `build-debug/tests/lumen-tests "[designer][dp9]"` 为 `174` 个断言、`8` 个测试用例通过，
  覆盖 manifest round-trip、未知字段、schema 迁移、revision 冲突、跨文档引用端点、损坏恢复，以及
  `DesignerApp` 的工程新建/打开、页面切换、工程页列表、资源清单、工程保存和工程诊断跨文档跳转。
- D2 示例 smoke 在 Debug/Release 均通过 `designer_headless`、`designer_gallery_headless`、
  `designer_gallery_design_headless`、`designer_window_smoke`、`designer_gallery_window_smoke`
  和 `designer_gallery_design_window_smoke` 六项（`6/6`）；另有
  `designer_cli_rejects_invalid_max_frames` 校验 CLI 拒绝非法帧数、未知选项、缺失 `--file`
  值和不存在的 `--file`。
  headless CLI 对指定文件的读取/解析失败返回非零并保留诊断输出；窗口模式对启动时指定文件
  读取/解析失败返回非零，watch 热重载期间仍保留上一份有效画面；文件删除与重新出现也会
  进入同一失败恢复和有效重载路径。
  窗口用例经 `xvfb-run`
  驱动 SDL 窗口并以 `--max-frames 3` 确定性退出，gallery.lumen 覆盖冻结的 12 个 L0
  节点类型，gallery.design 覆盖全部注册 schema，且均无业务引用诊断。
- 便携包验收：启用 `LUMEN_BUILD_PACKAGE=ON` 的 Linux install/CPack 配置与构建通过，安装前缀和
  TGZ 同时包含 `lumen-designer`、`gallery.lumen`、`gallery.design` 及导出所需的
  `lumen-diagnostics`；安装后的 `gallery.design --headless`、命令行失败路径和重定位 SDK
  consumer smoke 均通过。包内失败路径覆盖未知选项、缺失 `--file` 值和选项误作文件名，均要求
  非零退出与稳定 `usage error`。
  包内 Designer smoke 同时校验 `frame0`、`document 1` 和 `diagnostics 0`，不会把带诊断的可恢复预览当作干净 fixture。
- 2026-10-09 桌面 CI 回归复核：包内 smoke 在切换工作目录前规范化包路径，实际安装树的
  相对路径调用从 `FileNotFoundError` 修复为通过，绝对路径调用和三种 CLI 错误路径同时通过。
  ComboBox 键盘用例在菜单关闭重建后重新查找字段，避免使用失效的 `RenderNode` 指针；
  macOS allocator 用例改为在构建回调中验证采样激活，并继续验证退出解绑、异常恢复和重新获取。
  Apple SDK 交叉编译检查通过；本机完整 Debug `1128/1128`、Release `1135/1135` 通过。
  GPU 构建修复固定 Skia 版本不支持的 `MakeTrans`，线段先旋转再平移，保持图标中心位置；
  原生 Windows/macOS 运行与现场验收仍需对应平台证据，交叉编译不代表原生运行通过。
- 同日 Windows MSVC CI 给出的编译错误已据实修复：工程存储关闭 `windows.h` 的
  `min/max` 宏，诊断模块在 Windows 使用 `<io.h>` 与 `_write`，终止处理器显式包含
  标准信号及异常声明。两份实现的 Windows 交叉编译通过，完整 Debug `1128/1128`、
  Release `1135/1135` 通过；MSVC 原生结果由后续独立验收分支 CI 确认。
- 同日旧版 OpenGL 图标回归：对实际桌面 GL 3.0 以下上下文关闭固定 Skia 版本的
  tessellation/atlas 路径，保留胶囊并集和单次 alpha 混合。完整 GPU CTest
  `1156/1156`、默认 GL 与强制 OpenGL 2.1 的视觉读回各 `106` 个断言、`4` 个用例
  通过；macOS GPU 实现的 Apple SDK 编译检查通过。Linux CI 已加入旧 GL 视觉回归。
- 2026-10-09 键盘/IME 评审：属性字段 Ctrl/Cmd+A 和方向键不再被大纲消费；
  组合输入期间文档撤销/重做等待提交或取消。共享编辑模型和交互层保留进入组合输入前的
  原文选区，修复 preedit 长度变化导致提交残留选中文字或删除尾文的问题。模型、字段交互
  和实际 DesignerApp 回归覆盖更新/取消/原子提交/撤销；`[ime]` 五个用例、92 个断言通过。
  全量本地默认配置 CTest `1132/1132`；原生系统输入法和候选框仍须现场验收。
- DesignerApp 的文档级撤销/重做、打开/保存、预览运行/调试和结构移动快捷键已注册到
  `AppShell::CommandRegistry`；Ctrl/Cmd 双平台绑定共享同一动作，字段内建编辑和弦由
  registry 的字段保护规则处理。组合输入期间历史命令动态禁用，提交或取消后恢复。
- 后续增量提交 `322af90` 将上述三条命令行失败路径加入包内 smoke；`35af740` 让
  `FileWatcher` 把文件删除和重新出现视为可观察变化，并在删除时走统一的旧帧保留诊断、
  恢复后重新加载路径。Debug/Release Designer 相关 CTest 均为 `142/142`，watch fixture
  为 `29` 个断言全通过；工作区仍只有 Linux Wayland 的真实窗口短 smoke，未把该证据扩展为
  四平台验收。
- 当前增量修复还收紧了 D3 另存为的 revision 边界：只有覆盖当前源路径时才携带
  `expectedRevision`，另存为已有目标不会复用源文件 revision，而对目标文件后续外部修改
  仍返回 `store.revision_conflict`。`designer save as does not reuse the source revision`
  fixture 为 `14` 个断言通过。
- 当前增量修复还在工程保存前预检 manifest 与全部页面的 revision，避免后续页面发生外部修改时
  先前页面已被部分写入；保存成功后重新打开活动页面以同步工作台的 loaded revision，连续保存不再
  因旧 revision 失败。`designer app opens, switches, and saves a multi document project` fixture
  增至 `61` 个断言，并覆盖“连续保存后保持干净”和“页面冲突不写入其他页面”。
- 工程另存为现按目标 manifest 重新解析相对工程根，复制全部页面并更新页面路径/revision 映射；
  目标目录会先创建，复制后的 manifest 可重新打开并保留当前页面内容，源工程 revision 不会被带入目标。
  同一多文档 fixture 增至 `69` 个断言。
- 工程打开失败现在恢复完整会话状态：旧工程、manifest/page revision、活动文档、源快照和资源授权根
  一起回滚；全部页面缺失时，旧工程仍可继续保存。多文档 fixture 增至 `76` 个断言，并保留“部分页面
  有效时展示工程诊断”的既有降级行为。
  回滚会保留本次失败诊断，并解除与旧页面的导航关联；空页面清单返回 `project.pages_missing`，
  避免工程打开失败后没有错误原因。
- 工程另存为同时复制工程根下存在的声明资源，并拒绝绝对路径、越界路径、越界符号链接和非普通文件；
  工程打开时也会诊断缺失资源、非普通文件和越界符号链接，但仍保留有效页面供继续检查；缺失资源
  保留为可诊断的声明，不因另存为静默改写 URI。多文档 fixture 增加资源内容复制、越界路径和缺失资源
  断言；`[designer][dp9][d3][app]` 为 `145` 个断言、`4` 个测试用例通过。
- 工程路径解析统一检查符号链接后的根目录归属，同路径保存也会预检资源类型；另存为拒绝
  资源目标目录和页面目标文件的越界符号链接，失败前不修改根目录外的资源或写入目标页面。
- 结果：设计器专属筛选 `build-debug/tests/lumen-tests "[designer][f6]"` 为 `402` 个断言、
  `31` 个测试用例通过；标准 `ctest -R 'designer|document store'` 为 `145/145` 通过，
  另有 D2 headless/窗口示例 smoke 通过。
  帧 allocator scope 生命周期修复后的完整
  `ctest --test-dir build-debug --output-on-failure -C Debug` 为 `1120/1120`，
  原生分配批次的 Release 配置启用 `LUMEN_ENABLE_FRAME_ALLOCATOR=ON` 后为
  `1127/1127`（含五组独立进程原生回归）；`[app][r6]` 为 `184` 个断言、`14` 个用例通过。
  当前本地 `build-debug` 没有设置 `CMAKE_BUILD_TYPE`，采集器
  如实报告 `Unspecified`。移动端 seam 和三桌面真实平台 smoke 仍按支持矩阵单独验收。
- P1 语义边界：`.lumen` 仍是 12 个冻结节点的单向导入；设计文档 codec 使用
  `lumen.design` magic、schemaVersion=1、字符串化节点 ID 和未知字段保留；L1 静态、L2 动态与首批 L3 组合件节点已登记私有
  设计 schema，并覆盖 Grid 列/间距、Image 稳定 imageSource、IconId 枚举、控件默认值、
  Tabs/ThemeScope 子树、集合声明属性、Splitter 双子节点约束和 L3 builder/占位路径的编译 golden；真实图片资源和全部 L3 组合件
  已由设计器应用适配层接入，组件交互、资源授权和异步预览均有 headless 覆盖。
- 静态核对：确认 parser allowlist 为 12 个节点；确认 `Widget` 的 source/theme/controller
  字段是运行时指针；确认 RenderNode identity 使用 key/位置路径；确认 R6 dump/HUD、Inspector
  节点命中、bounds/damage overlay 已有代码入口并由 headless 测试覆盖；P1 编译按 L0 schema
  直接创建 Widget，不写入这些运行时字段；P4 的 SourceMap 只保存节点/属性 span，坐标
  变换只在画布交互层使用，编辑器操作不把 runtime identity 写回文档；P5 保存先校验 DOM
  和 expected revision，再写唯一临时文件，
  并在写入及恢复副本生成后再次检查 revision，保留 `.bak` 后原子替换；主文件损坏时只从
  有效恢复副本返回文档。
- F6 性能基线：`designer_performance_tests.cpp` 覆盖 12、100、1000 节点的规范化读回、
  schema、compile、layout、CPU paint 和重复运行确定性；100 节点额外验证文档属性事务/
  undo，1000 节点额外验证大纲投影重复输出；另有 12 节点 `.lumen` 导入 fixture 和
  1000 项运行时 VirtualList 窗口物化与 frame hash，并记录当前/峰值物化项数。独立进程
  `lumen-designer-memory-probe` 还对预览打开、属性编辑重建、运行时 VirtualList
  layout/paint，以及带 `virtualSource` 引用的 L2 `DesignDocument` VirtualList
  compile/layout/paint 做 scoped global `new/delete` 记账，输出 allocation count、累计 allocated
  bytes、live bytes 和 peak bytes；CTest 名称为 `designer_memory_peak`。这些数值只作为同一环境
  下的相对基线，不冻结跨机器的绝对毫秒或字节门槛，且不等同于 R6 的生产 HUD/RSS 或整帧
  allocator 契约。L2 设计文档节点已登记 schema；探针通过 `MapDesignRuntimeContext`
  注册独立 source lease，真实 controller 资源和具体 widgets controller builder 仍由应用
  适配层按需接入；headless 组合件占位和 lease 路径已接入。
- F6 四类采集器：`lumen-designer-bench --benchmark` 输出带构建类型的机器可读 JSON，
  覆盖 12 节点导入、100 节点编辑、1000 节点大纲和 L2 虚拟列表预览。独立进程
  `designer_benchmark_report_integrity` 的 4 项 Python 测试验证阶段读数、堆峰值、重建数、
  虚拟化范围、重复 frame hash、旧内存探针兼容和 CLI 错误路径；两个本地配置均通过。
  相对门禁已由 `run_designer_perf_gate.py` 接入 Linux CPU CI；`a1e012e` 为独立的固定
  Designer 基线，每方五轮、每轮 10 次 warmup 和 300 次测量。原始报告记录源码/二进制
  摘要、构建 flags、同机 CPU affinity 和测量会话；门禁拒绝缺失指标、异常读数、非 Release、
  不可比环境和 dirty 基线。`designer_perf_gate_integrity` 的 12 项 Python 测试覆盖相对阈值
  边界、零基线、堆/重建/虚拟化回退、身份校验、CLI 返回码和交错采样归档。
  2026-10-08 本地 QEMU/Release 的旧 20 次采样协议及同二进制对照均触发 p95 门禁；
  提高到 300 次采样后，干净二进制对照的两种比较均通过；基线/候选配对仅 minimum 通过，
  median 的 L0 paint p50 超限（+25.68%），按现有双重比较规则整体 PASS，阈值未放宽。
  候选报告如实标记 dirty，原始报告和失败记录见 Designer 基线说明；此结果
  不代替真实 CI 或三桌面现场验收。
  2026-10-09 独立验收分支的干净提交 `cbd2025` 已通过
  [Linux CPU CI](https://github.com/ke4nec/lumen/actions/runs/37873377729/job/113636250291)：
  固定 Designer 基线 checkout、同工具链构建、四类 fixture 相对门禁及报告归档步骤均成功。
  这是实际 hosted CI 门禁证据；逐项指标以 `designer-benchmark-report` 归档为准，
  不把 C++ scoped heap 采集扩大为生产整帧 allocator 或人工桌面验收。
  `designer_performance_tests.cpp` 的 L2 VirtualList fixture 还重复编译同一文档，验证 source
  lease、可见窗口物化数量、RenderNode 和 frame hash 的确定性。R6 已冻结
  `render::FrameAllocationSource` 注入契约：一次 scope 从应用 rebuild/layout/paint 覆盖到
  `Renderer::submit`，没有平台 allocator source 时 HUD 显示 unavailable；命令流容量和进程
  RSS 不得冒充整帧读数。`app_shell_tests.cpp` 的 fake source 已覆盖提交帧、无绘制帧取消、
  重新标脏和 HUD 快照。
  帧 scope 现以 RAII 捕获开始时的 source；重建、录制/提交或报告抛异常时取消同一实例，
  帧内切换 source 只作用于下一帧。`cancelFrame()` 为幂等 `noexcept` 清理；`beginFrame()`
  失败必须保持非活动状态，失败尝试可复用 frameIndex，source 不得把它当作唯一分配批次标识。
  这是 source 接口变更：调用方的自定义实现须将 `cancelFrame()` override 改为
  `noexcept`，并允许重复取消及报告失败后取消。
  headless 回归覆盖 build/submit/report 失败后重试及重建回调内切换 source；这些生命周期
  证据仍不替代三桌面真实 allocator 接入与现场验收。
  后续原生批次新增 Linux/glibc 可选 profiler（`LUMEN_ENABLE_FRAME_ALLOCATOR=ON`，
  启动时 `LD_PRELOAD` 显式安装），以原生 malloc-family 对象账本统计请求量、scope
  live/peak bytes；`runApp` 可拥有工厂 source，显式 source 优先。原生 C/C++/SDL、
  跨线程释放、fork、容量耗尽恢复及工厂拒绝不完整绑定均有独立进程回归；详见
  [`lumen-frame-allocator-design.md`](lumen-frame-allocator-design.md)。这些是实际 glibc
  分配的 headless 证据；另在本机 GNOME/Mutter Wayland 和 Xwayland 两窗口分别采集
  `183` / `175` 个有效 allocator 帧，状态保持且 source 校验通过。短 smoke 的原始
  指标、源码/二进制摘要见 `platform-evidence/frame-allocator-linux-2026-10-08.json`，
  不替代独立 X11 桌面、Windows/macOS 后端、完整现场 record 或实际性能 CI 结果。
  后续平台门禁要求 `frame_allocator_source=pass` 附唯一的原生探针日志，校验来源、
  会话、两窗口样本、指标与成功标记，并保持完整人工记录/哈希检查。Linux self-hosted
  工作流已接入 profiler、日志采集和解析；检查器为 `10` 个 Python 回归用例通过。
  本机新版 Wayland 日志（`185` 个有效帧）解析通过；不 preload 时探针返回失败、
  来源 unavailable 且没有成功标记，解析器拒绝。完整 CTest 仍为默认配置
  `1120/1120`、启用 profiler 的 Release `1127/1127`；没有据此宣称实际 workflow 已通过。
  macOS 接入批次新增可选 `DYLD_INSERT_LIBRARIES` profiler 和 `libmalloc/malloc` 来源，
  覆盖标准/typed/zone/batch 分配、zone 销毁、原生失败语义和启动期绑定验证；独立
  原生进程测试、Cocoa 日志检查与 CPU/现场 CI 已配置。Linux/macOS 共用无堆对象账本，
  删除槽后的重复地址、容量/数值溢出、zone 退休及 scope token 为 `25` 个断言、
  `5` 个用例通过。SDK 14.5/26.1 的 arm64/x86_64 后端交叉编译通过，SDK 14.5 的
  两架构 dylib 严格链接通过；SDK/client/native 测试编译检查不等于 macOS 原生运行。
  本批完整 CTest 为默认配置 `1125/1125`、启用 profiler 的 Release `1132/1132`；
  验收解析器 `11` 个 Python 用例通过，本机 Wayland 两窗口回归采集 `133` 个完整帧。
  macOS 原生进程/现场、Windows 后端、完整人工记录与真实 CI 性能结果仍待补齐。
  Windows 接入批次新增 `ntdll/heap` 可选后端，以
  `LUMEN_FRAME_ALLOCATOR_DLL` 显式加载并固定 DLL，默认仍不安装钩子。固定提交的
  MinHook 在 x86/x64 的 Rtl heap 层覆盖静态/动态 CRT、SDL 和跨 DLL 请求；账本
  识别 CRT 前缀后的用户指针，heap 创建/销毁的内部管理请求受递归保护，SEH
  退出释放锁与 TLS 标记。CPU Debug、Skia Release、可选包与 Win32 现场门禁已配置。
  本地 Linux 默认全量 CTest `1126/1126`、启用 profiler 的 Release `1133/1133`；
  共用账本 `38` 个断言、`6` 个用例，验收解析器 `12` 个 Python 用例通过。
  Linux Clang 21.1.8 + MinGW 13.0/13.2 的 Windows x64 DLL/client/独立原生测试
  程序交叉构建通过；Wine 10.0 运行五组测试为 `111` 个断言、`14` 个用例通过。
  这些是交叉构建和兼容层 headless 证据，不是 MSVC 静态 CRT、原生 Windows
  运行或登录桌面验收结果；Windows/macOS 原生 CI 与现场、独立 X11、完整人工
  记录和真实 CI 性能结果仍待补齐。另已修复 Windows 构建误用 POSIX 单实例源码
  与测试 process id 的问题，单实例按既有 Windows `Unavailable` 契约降级。
- F6 预览恢复：`DesignPreviewFrame` 保留同一文档最后一次成功编译的 Widget、Trace、
  SourceMap 和 session；引用缺失时接收带 trace 的占位帧并保留 Placeholder 诊断，schema/
  compile 失败则保留旧帧；替换成功编译会关闭旧 session，文档身份变化时清除旧帧。
  诊断会补齐当前 `documentId`，并按节点 ID、路径和 source span 保留不同错误位置供错误列表
  定位；`designer_preview_frame_tests.cpp` 覆盖这些恢复、聚合和代数边界。`DesignPreviewState`
  的 runtime snapshot 与 visual override 按 `documentId` 隔离，避免不同文档复用节点 ID 时
  串用会话值；`designer_preview_tests.cpp` 覆盖跨文档同 ID 回归。
- F6 无障碍基线：`designer_accessibility_tests.cpp` 从 L0 设计文档编译并布局语义树，验证
  稳定的 role/label/value、bounds、Button 激活动作和重复布局确定性；三桌面读屏和真正的
  设计器面板键盘流程仍属于 D2/D3 应用出口。
- D3 属性面板的可编辑字段现在以实际属性名提供稳定语义 label，引用字段明确标记为
  reference；DesignerApp 回归通过语义聚焦、SetValue、文档 undo 和重建后 label/value
  保持不变（18 个断言）。
- F6 选择隔离：`DesignSelectionModel` 将会话选择绑定到当前 `documentId`；切换文档时清除
  旧节点 ID、主节点、锚点和拖拽捕获，避免不同文档复用节点 ID 导致选择串用。
- 决策记录：DP-1=B、DP-2=与阶段 A 并行但平台验收仍为门槛、DP-3=L0、DP-4=独立 preview/session、DP-5=规范化输出 + SourceMap、DP-6=扩展字段保留且无法保留时拒绝保存、DP-7=离线默认 + 显式授权、DP-8=四类 fixture 相对基线、DP-9=A（工程 manifest + 多文档会话），均于 2026-10-01 决定。现有完整 CTest 结果仍不等同于设计器真实平台验收；D3 应用的
  round-trip、schema、DocumentId、RuntimeContext、完整资源加载和三桌面窗口验收仍须按阶段补齐。

### 10.4 实施期间的评审问题

以下问题已在对应实现阶段记录证据；F6 相对性能门禁已通过 Linux hosted CI，剩余
未闭环项是三桌面现场验收；Windows/macOS allocator 的原生 CTest 证据已补齐。2026-10-09 的
[macOS CI](https://github.com/ke4nec/lumen/actions/runs/37875132300)（`cebe883`）
已全部通过，包括原生 allocator 独立进程 CTest、GPU 和三种安装包；
[Linux CI](https://github.com/ke4nec/lumen/actions/runs/37875132290) 同批全部通过。
Windows 同批定位到文档存储测试的 `windows.h` min/max 宏污染，已补 `NOMINMAX`，
交叉对象编译通过。后续 `55dc741` 为 build-tree Designer 复制 `SDL3.dll`，修复
Windows headless/window smoke 的加载失败；`df57528` 补齐默认系统字体用例的
600s 冷扫描标签，并将漏同步的 M17 Debug Widget 预算由 952B 更新为 960B
（Release 仍为 856B）。该提交的
[Windows CI](https://github.com/ke4nec/lumen/actions/runs/37926334886) 七个 job 全部通过，
包含 CPU、UIA、Skia raster/GPU、三个安装包和 Designer headless/window smoke；
[macOS CI](https://github.com/ke4nec/lumen/actions/runs/37926334943) 全部通过。
[Linux CPU CI](https://github.com/ke4nec/lumen/actions/runs/37926334927/job/113806065498)
包含 CTest 和 Designer 相对性能门禁通过；同批 Linux Skia 的 CTest 通过，通用
scene 性能门禁失败，不能据此宣称该次 Linux 全矩阵通过。
后续 `85314c0` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/37928701758)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/37928701730) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/37928701716) 全部 job 通过，
Linux scene 门禁在该批通过；这三个结果均归属 `85314c0`，不自动覆盖后续提交。
历史批次中的“待补”按下述最新结果更新，人工窗口及读屏结果仍须单独记录：

1. **已验证**：DP-1 的私有格式、magic、codec、单向导入边界和未知字段策略由正式 schema 与 fixture 固定。
2. **已验证**：`DesignRuntimeSession` 由预览编译结果持有，lease 覆盖 Widget、RenderNode、交互和异步资源，并在替换/清理时关闭。
3. **已验证**：失败编译保留同文档上一份有效树，诊断更新，placeholder/keep-last-frame recoverability 和 dirty/save 隔离均有测试。
4. **已验证**：画布通过 DPI/zoom/pan 逆变换命中 `DocumentId`；动态物化行只回选 source node，不进入文档选择集合。
5. **已验证**：资源采用显式 scheme/root 授权，空或离线 context 使用固定替身/placeholder，不启动业务副作用。
6. **已验证**：P1–P5、G-D13–G-D16 的 fixture 已注册 CMake/CTest；真实窗口 smoke 仍按平台矩阵单独登记。
7. **已验证**：视觉 token 以 `lumen-visual-system-design.md` 为基线，语义/键盘/性能证据分别在 designer 测试和 §10.3 记录；`designer_memory_peak` 已补齐预览打开、属性编辑、运行时 VirtualList 和 L2 `DesignDocument` VirtualList fixture 的真实 scoped heap 峰值读数。

其余问题是实现验收条件，不撤销已记录的 D3 L0 实施授权；预览 fixture 的 scoped heap 读数已补齐，
但命令流存储分配读数和该探针都不能替代 R6 的生产整帧堆分配统计；当前已具备可注入的
`FrameAllocationSource` 契约和 unavailable 降级，Linux/glibc 原生后端及本地
Wayland/Xwayland 单项窗口 smoke 已验证。macOS/Windows 后端已实现并通过交叉构建，
Windows 另有 Wine headless 回归；Windows/macOS 已有原生 CPU CI 回归，Linux hosted CI 已有
实际性能门禁通过记录。Windows/macOS 现场、独立 X11 桌面和完整现场结果
仍按已冻结方案补齐。

2026-10-09 按 §6.1、§6.3、§4.18 出口复查发现，现场检查器仅强制 Designer 三帧
启动，尚未要求实际 Inspector 操作、编辑/保存/重开、DPI/主题和 Designer 自身的读屏
流程。现已把这三个平台项及两个逐读屏器项接入必检集合，并同步现场操作说明及支持
矩阵；完整 CLI 验收还必须提供一小时浸泡报告，省略报告或平台检查项不能归档为通过。
这是验收完整性的修复，未把这些人工项改为已验证。
本机 GNOME/Wayland 已用 `85314c0` 的运行时源码执行 `gallery.design` 双窗口三帧
smoke，退出 0 且成功标记唯一；[日志及身份摘要](platform-evidence/designer-wayland-2026-10-09.json)
保留工作树改动、二进制/文档/日志哈希及 EGL 警告。该单项结果不替代 Inspector 操作、
编辑闭环、输入法、读屏、高 DPI、GPU 或完整 self-hosted 现场 record。
本轮检查器回归 `python3 -B tests/platform_acceptance_tests.py` 为 17/17，通过四平台
遗漏/待验拒绝、逐读屏器 Designer 流程、缺失浸泡报告及当前日志匹配检查；默认本地
完整 `ctest --test-dir build-debug --output-on-failure -C Debug` 为 1133/1133 通过。
P2 字段守护复查补上 Widget 聚合成员数的编译期检查；在临时头文件中新增未登记布尔
字段后，sizeof 仍为 856B，但真实 schema 测试编译因 74 个 binding 对应 75 个成员而失败。
正常源码的 P2 切片为 8 个测试、7596 个断言通过，完整本地 CTest 1133/1133 通过。
保存冲突复查还补齐了 §4.14 第 5 条的应用出口：单文档/工程均显示重载、另存、明确
覆盖；取消另存、重载失败和再次外部修改保留本地文档与冲突入口，覆盖沿用合法备份及
单文件原子替换。修复同工程重载时旧工作台误写新页面的加载顺序，新增 headless fixture
覆盖三动作、语义 Activate、Tab/Enter、字体/高对比后的动作布局、后页和 manifest 的
二次冲突不先写前页；不把这些自动化结果标记为三桌面人工验收。
本轮 `[conflict]` 为 3 个用例、318 个断言通过，完整默认 CTest 1136/1136 通过；
Release `[designer]` 为 145 个用例、10614 个断言通过。新增冲突恢复规范和独立 HTML
对照稿，后者已用 Chrome headless 渲染并检查。此前 `a342661` 的
[Windows](https://github.com/ke4nec/lumen/actions/runs/37932612284)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/37932612330) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/37932612291) 常规 CI 全部通过；
该结果归属此前验收检查器批次，不作为本轮冲突恢复代码的 CI 通过证据。
G-D13 合并边界复查补齐了 §4.14 第 2 条：相同 `mergeKey` 现在还受 750ms 空闲窗口、
相同目标与连续选择约束，混合 key/空 key 的多命令事务不合并，保存和成功 undo/redo
打断连续操作。三个可控时钟回归用例为 109 个断言通过；临时将相同用例链接到未修复的
history 实现时，3/3 用例失败（27 个断言失败），证明确能检出旧行为。完整默认 CTest
1139/1139 通过，Release `[designer]` 为 148 个用例、10723 个断言通过；未改变空 key
的属性提交、结构编辑或一次拖动的事务粒度，也未生成新的人工平台证据。
P5/G-D15 加载路径复查修正了编译前发布候选 DOM 的顺序；另外修正应用在失败打开时
提前修改普通保存目标的问题。新 fixture 在旧实现上分别复现同 ID 无效 DOM、其他
文档及 schema 合法但运行时 key 碰撞文件改变当前编辑源，修复后覆盖全部会话状态、
旧 session 保活、原撤销/重做及源文件的外部 revision 检测；三种应用打开入口失败后
普通保存仍写回原文件，未改变被拒绝的文件。离线引用占位成功打开并清理旧历史有
正向断言。`[load-atomic]` 为 3 个用例、169 个断言通过，完整默认 CTest 1142/1142，
Release `[designer]` 151 个用例、10893 个断言通过。此前应用用例依赖无效 DOM 被加载
来制造 Run 失败，现按拒绝加载契约验证 Run 继续使用有效文档；未隐藏加载诊断。
G-D13/G-D15 编辑路径复查另复现了“失败编辑替换已有 redo 分支”：临时回归在旧实现中
redo 返回 false 且 DOM 不等于原分支，两个断言失败。现在候选编译准备与发布分离，
编译失败不提交历史；成功提交直接发布已准备的结果，不二次编译或重复计数。失败保留
frame、session、DOM、选择、documentRevision、dirty、undo/redo 和原分支，并保留
`compile.duplicate_runtime_identity` 的节点/属性诊断。`[edit-atomic]` 为 2 个用例、
94 个断言通过；完整默认 CTest 1144/1144，Release `[designer]` 为 153 个用例、
10987 个断言通过。

`74a568c` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/37937789284)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/37937789425) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/37937789233) 常规 CI 全部通过；
Windows 七个 job 均通过。该提交包含字段守护、冲突恢复和合并边界修复；之后的
加载/编辑发布修复需要其自己的 CI 结果。`4443908` 的 Windows 运行被新提交的并发
规则取消，未登记为成功；该批 Linux/macOS 均成功。常规 CI 仍不替代现场编辑/读屏。

`09705d9` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/37941712087)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/37941712200) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/37941712074) 常规 CI 全部通过。
该结果已覆盖加载/编辑发布修复，Windows 七个 job 全部成功；没有新增人工平台记录。

DP-9/§4.14 工程会话复查复现了切页和保存重置撤销栈的问题。现以
`DesignWorkbenchSession` 按 documentId 保存声明、选择、历史、保存检查点与文件
revision；未访问页面从工程加载快照建立干净会话，同页切换直接保留当前状态。
切页只在活动 context 中重编译预览，快照不延长旧 runtime session 的寿命；失败切页
保留原页面、历史和 frame。工程保存/另存成功后更新所有页面的检查点，保留原
undo/redo 分支；另存后的普通保存写回新工程根。成功打开独立源/设计文件或单页另存
则退出工程，避免旧活动 documentId 接收独立页面或普通保存误写旧工程；失败不退出。
三个 `[project-session]` 回归为 300 个断言通过，默认完整 CTest 1147/1147、Release
`[designer]` 为 156 个用例、11287 个断言通过。`a592118` 的
[Windows](https://github.com/ke4nec/lumen/actions/runs/37946377217)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/37946377159) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/37946377270) 常规 CI 全部通过，
Windows 七个 job 全部成功；未增加人工现场验收记录。

G-D15 诊断出口复查复现了读取/迁移错误被统一标为 save、`.lumen` 读取失败落入
compile 的问题；阶段回归在旧实现上有 22 个失败断言。现按错误来源保留 read /
migrate / schema / reference / compile / save，保存拒绝标记 block-save，主文件和
备份的迁移/schema 诊断分别保留实际路径。位置去重也改为长度编码，避免换行字段
碰撞。CLI 的显式 `--dump-diagnostics` 使用 GUI 同一份聚合诊断并输出完整 JSON，
工程读取失败不再只打印工作台的零条诊断。六个 `[diagnostic-stage]` 用例、163 个
断言通过；CLI 检查覆盖缺失文件、解析失败、未来工程版本、正常 gallery、JSON 字段
完整性和重复输入的输出一致性。默认完整 CTest 1154/1154、Release `[designer]`
162 个用例、11453 个断言通过；这些仍是 headless 证据，源码需另行取得三平台 CI。

`f930129` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/37950109487)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/37950109507) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/37950109511) 常规 CI 全部通过，
覆盖上述诊断修复；同提交被并发规则取消的重复运行未记为成功。现场记录仍待补齐。

G-D14 手柄坐标复查在旧实现的 12 组根/嵌套 Stack 与 DPI/zoom 组合中复现 48 个
失败断言：首段拖动位移丢失，嵌套父容器偏移误写 left/top，缩放后的逻辑坐标参与
设计网格吸附。现保留完整按下/松开位移，在设计坐标中计算并反向绘制临时辅助层。
追加居中 Column 八个方向和根节点回归；流式父节点的自然尺寸不能作为子尺寸增长
的硬上限，尺寸吸附不带布局偏移。新增五个应用用例，覆盖六组 DPI/zoom 的点选、
框选、辅助框、手柄预览与提交、padding/margin、取消、undo/redo 和旧 revision 拒绝。
新增用例与原流式手柄回归共 6 个用例、901 个断言通过；默认完整 CTest 1159/1159
（110.23s），Release `[designer]` 为 167 个用例、12337 个断言通过。三平台 CI
需按本批提交另行确认；未增加人工 DPI、编辑、输入法或读屏验收记录。

`f209ed2` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/37955899483)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/37955899724) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/37955899492) 常规 CI 全部通过，
覆盖上述手柄坐标修复；完整人工现场记录仍待补齐。

G-D16 图片出口复查在旧实现上复现别名占位、失败诊断缺失与旧会话请求保留，
三个回归共 11 个失败断言。授权后的规范 URI 现通过别名映射回填节点；失败读取/
解码按引用节点聚合稳定诊断。待完成请求随编译 session 关闭取消，替换文档不能接收
旧结果；已就绪且仍获授权的不可变像素可由新编译接收。撤销资源根或销毁应用均释放
句柄，晚到回调只持有 weak manager 和句柄，不持有应用或 context。
补充 CPU 像素验证又复现独立预览的 6 个失败断言：共享 manager 的全局 upload 队列
被第一个窗口耗尽。现每个 renderer 使用独立上传游标，完成事件按指定窗口路由或
通知全部共享窗口，并分别 pump 不同 manager；覆盖双窗口实际像素、定向通知、设备
恢复、释放、授权撤销、ready 复用和销毁后的结果丢弃，DOM、dirty 与历史不变。
别名用例包含不需要符号链接权限的分支，Windows 也必检两个预览的实际像素。
默认配置完整 CTest 1166/1166，启用 allocator 的 Release 完整 CTest 1173/1173，
Release `[designer],[resource-consumers]` 174 个用例、12655 个断言通过；本批源码仍需
自己的三平台 CI，未增加人工编辑、输入法、读屏或完整现场记录。

`379275f` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/38012556962)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/38012556969) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/38012556957) 常规 CI 全部通过，
覆盖图片会话与双窗口上传修复；Windows 七个 job 全部成功。
Linux CPU 的 Designer 相对性能门禁、通用 CPU/Skia/GPU 性能门禁及三种包内
Designer fixture smoke 步骤均成功；这些仍是 hosted 证据。

§4.18 键盘出口复查在属性和命名引用字段中复现 10 个失败断言：Navigator 已进入
子路由时，Escape 被返回动作提前消费，组合输入和原文选区未恢复，返回后的路由
标签也继续显示旧值。现组合输入的 Escape 优先取消并保持字段焦点与路由；结束后
再次返回走按钮共用的预览刷新路径，同步编辑器和独立预览。新增两字段分支的应用
回归为 50 个断言通过，Release `[designer][ime]` 两用例、112 个断言通过；默认
配置完整 CTest 1167/1167（34.91s）、启用 allocator 的 Release 完整 CTest
1174/1174（13.43s）。上述结果属于本批 headless 验证，真实输入法/读屏与现场仍待验。

`55ed4d4` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/38014011564)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/38014011555) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/38014011590) 常规 CI 全部通过，
Windows 七个 job 全通过，覆盖上述 Escape/输入法返回修复；这些仍是 hosted 证据。

G-D12 / §4.18 焦点出口复查在旧实现复现 17 个失败断言：画布内预览 TextField 的
Delete 删除了设计节点，Ctrl/Cmd+Z/Y/Shift+Z 改动文档历史而非预览文本，
Ctrl/Cmd+Up/Down 在字段编辑时重排文档。现任何文本字段聚焦时均阻断结构重排和
节点删除；画布字段使用自身文本历史，只有属性/命名引用字段接收文档撤销/重做。
应用回归分别验证 Ctrl 与 Command、已有文档 redo 分支、文本删除/undo/redo、
属性/引用字段的声明撤销及未变化的 DOM、revision、选择与历史。两个用例、448 个
断言通过；连同输入法回归的 Release 筛选四用例、560 个断言通过。默认配置完整
CTest 1169/1169（43.03s）、启用 allocator 的 Release 完整 CTest 1176/1176
（19.20s）；真实桌面键盘/输入法证据仍待补齐。

`ab43fec` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/38015411485)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/38015411609) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/38015411570) 常规 CI 全部通过，
Windows 七个 job 全成功，三平台各三个 Designer fixture smoke 与 Linux Designer
相对性能门禁明确成功；完整提交/job/关键步骤结果见
[`CI 摘要`](platform-evidence/designer-ci-2026-10-10.json)。
同源码的本机 GNOME/Wayland Release 原生 Designer 三帧 smoke 与
`glibc/malloc` 双窗口短探针也退出 0；后者 3.024s、102 次呈现、101 个有效
allocator 帧，状态保持。日志、产物身份与构建开关见
[`原生单项证据`](platform-evidence/designer-wayland-2026-10-10.json)。
原生桥/Skia/GPU 开关关闭，未覆盖真实 IME、读屏、编辑/保存/重开、高 DPI 或一小时浸泡。
F0–F6、13 类 fixture 与四个桌面会话的当前出口复核见
[`阶段验收对照`](lumen-designer-prerequisites-acceptance.md)；完整现场记录仍缺，目标未完成。

§4.18 原生 AT-SPI 出口复查在旧实现复现：大纲行 `activate` 回执成功，但下一次
UI 重建把 DocumentId 选择恢复为旧大纲选区，所激活节点的属性字段不可达。
两个节点分支的应用回归在旧实现共 8 个失败断言。现大纲 `onActivated` 调用已有
`selectNode` 路径，同步 workbench 与大纲 current/selected，避免重建回退。
新增应用用例 52 个断言通过，覆盖选中状态、属性焦点/编辑及 undo/redo，选择动作
保持 DOM/revision/dirty/history；连同键盘与 IME 回归，Release 五用例、612 个断言通过。
默认配置完整 CTest 1170/1170（54.47s）、启用 allocator 的 Release 1177/1177
（30.48s）、原生无障碍桥构建 1171/1171（54.30s）。本机 Wayland 隔离总线的
AT-SPI 重测已确认大纲 selected、对应 text 属性字段和 FOCUSED 状态；协议动作与
语义反馈的单项检查不替代 Orca 实际播报或正式现场签署。

`dc57970` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/38019639083)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/38019639226) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/38019639093) 常规 CI 全部通过，
Windows 七个 job 全成功；修复后的三平台提交归属与关键步骤已重新验证。

Windows Designer 门禁随后补上了真实 UIA 客户端到应用语义树的专用回环：在
`LUMEN_UIA_LIVE_SMOKE=1` 的桌面测试中创建真实 HWND，查找大纲的 `Text  [title]`
TreeItem 并执行 Invoke，随后查找选中节点的 `text` Edit 控件，读回原值并通过
ValuePattern `SetValue` 验证 Designer 文档事务已提交且 dirty 状态改变。该用例位于
`tests/a11y_provider_tests.cpp` 的 `[a11y][live][designer]`，与通用 UIA provider
测试共用 WM_GETOBJECT/语义 action 链；未设置 live 变量时保持 headless 安全跳过。
本地 Linux `[a11y]` 回归为 25 个用例、459 个断言通过；Windows 原生编译和桌面回环
`f021fe8` 的 [Windows](https://github.com/ke4nec/lumen/actions/runs/38025281805)、
[Linux](https://github.com/ke4nec/lumen/actions/runs/38025281836) 和
[macOS](https://github.com/ke4nec/lumen/actions/runs/38025281766) CI 均成功，Windows
`a11y-bridge` 的 live UIA 测试步骤也成功；完整 job/提交归属见
[`CI 证据`](platform-evidence/designer-ci-2026-10-10-f021fe8.json)。这仍不等同
Narrator/NVDA 人工播报验收。

§4.18 的 Linux Designer 原生协议门禁新增专用 libatspi 客户端回环。复查发现生产
AT-SPI provider 原先只暴露数值 Value 接口，属性 TextField 无法从原生客户端读取或
写入文本。现按无障碍 provider 设计 §6 实现最小 Text/EditableText 值接口：Unicode
字符范围与计数、Focus → SetValue 事务、boolean 写入回执和文本删除/插入事件。
`tests/designer_atspi_live_tests.py` 驱动生产 D-Bus provider 与 DesignerApp 大纲/属性，
并由应用端验证 DocumentId 选择、dirty、undo/redo、临时文件保存/重开后的 ID 与值；
覆盖空串、中文、emoji、组合字符、非法范围以及静态标签/禁用字段拒绝写入。
Linux CPU workflow 明确运行此项；fixture 不创建 SDL 窗口，自动化协议证据不替代
Orca 与 D1/D2/D3 的正式桌面现场出口。
review 时用 `1840bbe` 的旧 provider 与同一个新 fixture 临时重链接，回归明确在
缺少 Text 接口处失败；新实现的通用与 Designer AT-SPI 回归顺序通过。启用原生桥
的本机 Debug 完整 CTest 1171/1171（118.82s）通过。
`afc2134` 推送后的 Windows、Linux、macOS CI 均通过；Linux CPU 的通用和 Designer
AT-SPI live 步骤、Windows `a11y-bridge` 的 UIA Designer live 步骤均有本次提交归属，
摘要见 [`afc2134 CI 证据`](platform-evidence/designer-ci-2026-10-10-afc2134.json)。
随后 `70b8944` 修正 Windows UIA live 回环的 Unicode 输入后，Windows、Linux、macOS
再次全部通过；Windows `a11y-bridge` live UIA Designer、Linux 两个 AT-SPI live 步骤
均有明确成功结果，摘要见 [`70b8944 CI 证据`](platform-evidence/designer-ci-2026-10-11-70b8944.json)。

`8684d9a` 继续收紧 UIA ValuePattern 的事务契约：TextField 的 `SetValue` 自动先执行
Focus，禁用、节点失效和未处理事务返回明确 UIA 错误码；Windows live UIA Designer、Linux
和 macOS CI 均通过，摘要见 [`8684d9a CI 证据`](platform-evidence/designer-ci-2026-10-11-8684d9a.json)。

`be32bbf` 将同一回执契约扩展到 Windows UIA `Invoke/Toggle` 与 `SetFocus`：禁用、未处理
和节点失效分别返回对应 HRESULT，避免屏幕阅读器把被拒绝的 Designer 操作播报为成功。
首轮 Windows 回归发现旧的重入测试仍期待已删除节点为 `S_OK`；`32090be` 已修正该断言，
并以同一提交通过 Windows `a11y-bridge`、Linux、macOS 全部 job。当前提交本地 Debug
CTest 为 1170/1170，`[a11y]` 为 25 个用例、459 个断言，平台验收脚本 17/17，Designer
benchmark 4/4；结构化归属见 [`32090be CI 证据`](platform-evidence/designer-ci-2026-10-11-32090be.json)。
这些 hosted/协议结果仍不替代 Narrator/NVDA、IME、剪贴板、高 DPI、干净机器和完整人工
现场记录。

NSAccessibility 的 Designer 属性写入也已统一为跨平台事务：`TextField` 的 `AXValue`
写入先建立 Focus，再提交 SetValue；焦点事务拒绝时不提交值，数值控件仍走直接 SetValue。
macOS provider 回归覆盖成功顺序与焦点失败短路；真实 VoiceOver/桌面现场仍按平台出口待验。

`ec96c8b` 的三平台 hosted CI 已全部通过；macOS CPU job 实际编译并执行
NSAccessibility provider 回归，Windows `a11y-bridge` 也完成 live UIA smoke。完整运行与本机
1170/1170 CTest、25 个无障碍用例/459 个断言、平台检查器 17/17、Designer benchmark 4/4
结果见 [`ec96c8b CI 证据`](platform-evidence/designer-ci-2026-10-11-ec96c8b.json)。
这些结果仍不替代 VoiceOver/Narrator/NVDA、IME、剪贴板、高 DPI、干净机器和完整人工现场记录。

平台读屏回环脚本现也覆盖文本编辑：Windows 使用 Edit `ValuePattern.SetValue`，macOS
使用 `AXTextField` 的 `AXValue` 写入并回读；这两条协议路径与 Designer 属性事务保持一致。
脚本仍需在登录的 Win32/Aqua 会话中运行，进程探活和协议成功不替代人工语音记录。
`3e3d413` 的三平台 hosted CI 全部通过，运行记录见 [`3e3d413 CI 证据`](platform-evidence/designer-ci-2026-10-11-3e3d413.json)。
