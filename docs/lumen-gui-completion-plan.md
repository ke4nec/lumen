# Lumen 桌面 GUI 完善与收敛计划

> 文档状态：执行基线（2026-10-02）
> 适用范围：Windows / Linux / macOS 桌面 GUI   
> 目的：记录当前真实缺口、实施顺序和验收证据，作为后续功能开发的任务入口。

本文是当前源码的收敛计划，不替代阶段设计文档。里程碑细节仍以
[`lumen-self-use-roadmap.md`](lumen-self-use-roadmap.md) 和
[`lumen-m15-roadmap.md`](lumen-m15-roadmap.md) 为准；平台能力以
[`support-matrix.md`](support-matrix.md) 为准。本文解决的是一个更实际的问题：
下一步应该完善什么，完成到什么程度才算完成。

## 1. 判断标准

每项能力使用支持矩阵的四态描述，不能把接口存在或 headless 测试通过写成平台完成：

| 状态 | 含义 | 最低证据 |
| --- | --- | --- |
| 接口已存在 | 公共契约和 Fake/Recording 路径已建立 | 头文件、Fake host 测试 |
| headless 已验证 | 无窗口路径行为稳定 | Catch2/集成测试、确定性输出 |
| 真实平台已验证 | 目标桌面输入、窗口、字体或系统服务真实工作 | Windows/Linux/macOS smoke 或人工记录 |
| 可发布 | 干净机器可安装、启动、使用并可追溯 | 安装包、解包启动、版本与 commit 记录 |

每个任务必须同时满足四个条件：

1. 公共 API、应用组合边界和线程归属清楚。
2. headless 测试先于窗口 smoke，失败路径也有断言。
3. 视觉变化同步更新视觉系统文档和 `design/*.html`。
4. 支持矩阵、路线图、README 和构建命令与源码状态同步。

## 2. 当前基线

### 2.1 已具备的基础能力

以下能力不应重新立项，只在发现回归或平台缺口时补验：

- C++20 声明式 Widget、Element identity、约束布局、Row/Column/Grid、ScrollView、ListView、VirtualList。
- CPU、Skia 光栅和可选 Skia Ganesh GPU 渲染路径，damage、帧调度、命令记录/回放和资源生命周期。
- UTF-8、grapheme、编辑选区、IME 状态机、TextField、剪贴板文本、undo/redo 和系统字体回退。
- Theme token、四种 ThemeDirection、深浅色、高对比、字体缩放、密度、减少动画和局部 ThemeScope。
- AppShell、Navigator、DialogHost、Form、FocusScope、命令注册表、Preferences、运行时诊断和多窗口隔离。
- Button、Checkbox、Switch、Radio、Dropdown、ComboBox、ColorPicker、Slider、ProgressBar、Tooltip、Spin、ToolBar、StatusBar、Splitter、List/Tree/TreeList、DataGrid。
- 语义树、Recording bridge、Windows UIA、Linux AT-SPI2、macOS NSAccessibility provider 的实现和 headless 回归。
- M15 拖放会话、List/DataGrid 行重排、DataGrid 列拖序和 pinch 手势的 headless 契约。
- `examples/template`、`examples/settings`、`examples/gallery` 以及静态 render tree/semantics dump。

2026-09-30 本地 Linux Debug 基线为 `ctest` 929/929 通过；Gallery 为 39 个测试、
695 个断言。这个结果证明源码和无窗口契约稳定，不代表 Windows/macOS 真实平台验收完成。

### 2.2 当前最重要的事实差距

当前最大的差距是“实现已经存在，但平台证据还不完整”。具体包括：

- Windows Narrator/NVDA、macOS VoiceOver 的真实读屏回环尚未完成；Linux Orca 有回环记录，但支持矩阵需要更新到最新状态。
- Windows/macOS 真实 IME preedit/commit/cancel、候选框定位、跨应用剪贴板和透明合成仍需现场验收。
- Windows/macOS GPU 包、CPack Bundle、干净机器启动和长时间运行证据不完整。
- M15 OS 拖入尚未完成三桌面真实 smoke；SDL 3.2.10 没有拖出 API，拖出能力必须保持明确的不可用状态。
- M16 全局快捷键目前只有公共契约，没有 Win32/X11/macOS 平台后端；macOS 原生菜单栏和交通灯仍未收口。

## 3. 缺口矩阵

优先级含义：P0 阻塞“桌面可发布”宣称，P1 影响长期使用，P2 是按产品需求或效率收益交付。

| ID | 优先级 | 工作项 | 当前状态 | 完成标志 |
| --- | --- | --- | --- | --- |
| R0 | P0 | 三桌面真实验收 | 接口和部分 headless 已有 | 三桌面窗口、输入、IME、读屏、剪贴板、GPU/透明清单有记录 |
| R1 | P0 | 发布与安装收口 | Linux 发布链较完整，Windows/macOS GPU 包与 Bundle 仍有缺口 | 三平台干净机器安装、启动、升级/卸载路径可复现 |
| R2 | P1 | 生产生命周期 | 运行时诊断已接入，人工浸泡和部分重建场景不足 | 多窗口、DPI、最小化/恢复、renderer 重建、GPU 失败、异步资源在长时间运行中状态不丢 |
| R3 | P1 | OS 拖放 | headless 会话和应用内重排已实现 | 三桌面文件/文本拖入真实验证；拖出能力如实报告；取消、自动滚动、触摸边界明确 |
| R4 | P1 | 桌面系统集成 | 全屏、置顶、OS 模态、托盘契约已有 | 全局快捷键平台后端、macOS 菜单栏/交通灯、任务栏进度和托盘细节完成或明确降级 |
| R5 | P1 | 文本与剪贴板深度 | 基础编辑和文本剪贴板完成 | 三桌面真实 IME；图片/自定义格式剪贴板能力明确，跨平台不支持时有降级 |
| R6 | P1 | 开发者诊断 | render tree、semantics dump、Inspector、bounds/damage overlay、帧阶段统计、命令/节点计数和命令流存储分配读数已交付，headless 已验证；`FrameAllocationSource` 已冻结整帧 scope 契约并在无 source 时显示 unavailable | 三桌面 Inspector smoke 和稳定的跨平台生产 allocator source；命令流读数不能替代整帧堆统计，也不得用进程 RSS 冒充 |
| R7 | P2 | 控件细节 | auto-hide scrollbar、Splitter 塌缩/KeepRatio 等已实现，仍有清单 | 双轴联滚/RTL/菜单 mnemonic/Tree/List 行内编辑/DataGrid 条件筛选等按需逐项交付 |
| R8 | P2 | 复杂文本 | 当前为确定性 UAX#9 子集和逐 grapheme shaping | 只有在产品需要时实现 HarfBuzz、完整 UBA、TextSpan 富文本和目标语言字体策略 |
| R9 | P2 | 框架使用效率 | 模板已存在，Gallery/Settings 仍较大 | 页面壳、工具栏、状态摘要、表单和常用对话框组合方式有稳定示例与文档 |
| R10 | P2 | 文档与证据同步 | 多份文档存在历史快照 | 支持矩阵日期、CI 链接、完成状态、已知限制与源码保持同一版本事实 |
| R11 | P1 | D3 L0 可编辑设计器 | DP-1/DP-3/DP-4 已决定；P1–P5 与编辑基础契约已有 headless 证据，DesignerApp 已交付 12 类型工具箱、属性/命名引用/结构编辑、undo/redo，以及设计文件和工程文件的打开/保存/另存为/新建路径；F6 已增加独立预览 scoped heap 峰值 fixture；platform-acceptance 已接入四平台 `designer_window_smoke` 真实会话门槛，但现场证据未完成 | 12 个 L0 节点完成工具箱、属性编辑、结构重排、undo/redo、保存重开和独立 preview/session 隔离；文件对话框、工程容器和 F6 memory probe headless 已验证，三桌面证据另记 |

## 4. 执行顺序

### 阶段 A：M14 出口和发布阻塞项

先完成 R0、R1、R2。新控件开发不应替代这些验收工作。

1. 建立 Windows/Linux/macOS 真实验收清单，记录窗口、DPI、输入、IME、剪贴板、读屏、透明合成、GPU/present 和多窗口结果。
2. 运行 UIA/NVDA/Narrator、AT-SPI/Orca、NSAccessibility/VoiceOver 回环；将“provider 已编入”和“读屏器真实消费”分开记录。
3. 完成 Windows/macOS `package-skia-gpu`、CPack Bundle 和干净机器启动；保存安装产物、commit、后端和字体诊断。
4. 做 Windows/macOS 一小时双窗口浸泡，覆盖 resize、DPI、最小化/恢复、renderer 重建、窗口关闭和异步字体/图片资源。

阶段 A 的出口是：三平台真实证据齐全，或者每个未覆盖项都有明确平台、原因、降级行为和后续负责人；不能只凭 Linux CTest 通过宣称完成。

### 阶段 B：拖放与桌面集成

阶段 A 可以与阶段 B 并行，但 R3/R4 的平台实现要在真实 smoke 前冻结契约。

- R3：OS 文件/文本拖入、应用内行列重排、键盘等价、语义 MoveUp/MoveDown、取消和落点反馈；触摸拖拽让位滚动的规则要保留。
- R4：全局快捷键按平台能力实现；Wayland 无标准能力时返回结构化不可用；macOS 原生菜单栏与自绘菜单使用同一命令模型；补任务栏进度和托盘菜单 separator 等平台差异。
- 所有平台服务继续通过 `PlatformCapabilities` 报告能力，不在应用层猜测平台支持。

### 阶段 C：控件和布局增量

按实际应用需求拆成独立小项，每项都需要设计文档、mockup、headless 测试和 Gallery/Settings 样本：

1. RTL 布局镜像：`start/end`、滚动条、chevron、进度方向、DataGrid 冻结列和分隔线。
2. ScrollView 同视口双轴联滚，以及 Grid 的 rowspan/colspan。
3. 菜单 mnemonic、Alt/F10、触摸长按；Tree/List 行内编辑。
4. DataGrid 条件筛选面板、条件模型、异步提交失败状态、表头多列优先级角标和专属网格/单元格语义。
5. Splitter 继续补齐 `.lumen` 节点和基准场景；已实现的塌缩/KeepRatio 保持回归覆盖。

### 阶段 D：诊断和开发效率

R6、R9、R10 可以穿插实现，但应在新增复杂控件前提供足够的定位工具：

- Inspector 默认关闭，开启后显示 Widget/Element 类型、key、identity、bounds、ResolvedStyle、damage 和语义节点。
- Frame overlay 显示 build/layout/paint/submit/GPU wait、fps、命令数、节点数、命令流存储分配和当前 renderer；整帧 heap 通过注入的 `FrameAllocationSource` 显示真实来源，未接入 source 时明确显示 unavailable，不能用进程 RSS 代替。
- `--dump-tree`、`--dump-style`、`--dump-semantics` 使用同一套稳定文本格式，纳入 golden 和构建命令文档。
- 默认关闭状态必须保持零额外帧、frame hash 和性能基线不变。
- 将 Settings/Gallery 中反复出现的页面壳和状态摘要提取成小型示例组件，保持模板仍然简短。

### 阶段 E：按需复杂文本

R8 不作为中文/英文桌面工具的发布阻塞项。只有出现明确产品需求时才启动：

- TextSpan 树、局部样式继承、富文本命中/选区/编辑历史。
- HarfBuzz/Skia shaping、完整 UBA、镜像括号、数字定形和复杂脚本字体 fallback。
- CPU-only 构建继续可用，依赖版本和许可证固定，既有 grapheme 编辑索引不改变。

## 5. 任务验收模板

每个后续功能的变更说明至少包含以下内容：

```text
工作项：R?/M?/设计文档章节
用户场景：哪个桌面工具工作流因此完成
公共契约：新增/修改的头文件、能力位和事件
实现边界：core / layout / render / platform / widgets / app
headless：Catch2 用例、失败注入、确定性输出
真实平台：Windows / Linux / macOS 的 smoke 或人工清单
视觉：Theme token、设计文档、design/*.html、截图或 RGBA 导出
性能：场景、基线、p50/p95、命令数/分配量变化
降级：能力不可用时的行为和诊断文本
状态：接口已存在 / headless 已验证 / 真实平台已验证 / 可发布
```

## 6. 明确不纳入本计划

以下内容继续保持应用层或冻结状态，不因为本计划重新打开范围：

- Android/iOS 平台接入、移动页面、软键盘、移动 GPU、移动无障碍和移动发布；M9 继续冻结。
- 数据库、网络请求、同步、账号、插件市场和服务端数据模型。
- CSS/Flutter API 兼容层、3D、WebAssembly、DSL 可编程化。
- 框架级完整 i18n 资源系统；应用自行管理字符串和区域设置。
- Graphite、Vulkan、Metal、D3D 等专用 GPU 后端，除非新的产品需求明确开启。
- DatePicker 等没有当前产品需求支撑的控件；先保留为按需池。

## 7. 推荐的下一批任务

按风险和收益，下一批应按以下顺序推进：

1. 更新 [`support-matrix.md`](support-matrix.md) 和 [`platform-acceptance.md`](platform-acceptance.md)，把 2026-09-30 的 M15–M18 实现批次与真实平台缺口分开登记。
2. 完成 R0 的 Windows/macOS 读屏、IME、剪贴板、透明合成和一小时浸泡验收，形成可复查证据。
3. 完成 R1 的 Windows/macOS GPU 包、CPack Bundle 和干净机器安装启动。
4. 完成 R3 的三桌面拖入 smoke，并记录拖出不可用的结构化降级。
5. 保持 R6 Inspector/Frame overlay 的默认关闭和零开销回归；命令流存储读数和 `FrameAllocationSource` unavailable/提交 scope 已加入 headless 证据，三桌面生产 allocator source 仍不能以命令流或 RSS 代替。

完成以上五项后，再根据实际工具需求选择 R4、R5 或 R7 的具体子项；不要把未完成的真实平台出口用新增控件数量掩盖。
