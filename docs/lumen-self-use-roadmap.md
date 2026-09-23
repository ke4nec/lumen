# Lumen 自用桌面 GUI 里程碑路线图

> 文档状态：实施路线图（2026-09）
> 目标：在现有 Lumen 核心之上，形成一套可供个人工具类应用长期使用的跨平台桌面 GUI。
> 当前策略（2026-09-14 调整）：只规划 Windows/Linux/macOS 桌面端；Android/iOS 暂不实现并冻结。M9 仅保留历史编号，桌面里程碑完成后不自动进入移动端开发，也不预排移动端版本。
> 桌面自用版 M0–M8 已全部收口；后续实施 M10–M13 桌面增强链和 M14 实战可用收敛阶段（见 §3/§4），M9 不在实施链中。
> M12 之后按用户需求穿插交付按需控件增强（集合控件/菜单/分栏/自定义标题栏），不占里程碑编号（见 §10 对应完成记录）。当前源码与 CI 证据索引见 [`docs/support-matrix.md`](support-matrix.md) 的 2026-09-22 验证快照。

## 1. 目标、范围和完成定义

### 1.1 最终目标

Lumen 的自用版不是通用的 Flutter 替代品，而是一个边界清楚、可维护、可诊断的 C++20 自绘 GUI。它应当支持个人工具类应用的完整使用闭环：

- 多窗口、窗口缩放、DPI、最小化/恢复和退出。
- Button、TextField、Checkbox、Switch、Dialog、Navigator、ScrollView、ListView、Grid 和 VirtualList。
- 中文、emoji、组合字符、RTL、IME preedit/commit、选区、剪贴板和 undo/redo。
- 键盘焦点、鼠标、桌面触屏、滚轮、语义树和键盘可访问操作。
- 由 Theme 驱动的颜色、排版、尺寸、图标、阴影、动效和响应式布局。
- CPU、Skia 光栅和 Skia Ganesh GPU 三条渲染路径；GPU 故障可诊断并安全恢复。
- Windows、Linux、macOS 可运行的便携包、启动说明和故障诊断。

### 1.2 明确不纳入第一版

以下能力不作为第一版自用工具 GUI 的完成条件：

- 数据库、网络请求、同步、账号和插件市场；这些由具体应用实现。
- 完整富文本编辑器、表格控件、3D 和 WebAssembly。
- CSS 或 Flutter API 兼容层。
- 第一版完整 i18n/本地化资源系统；应用可自行管理字符串和区域设置。
- Android/iOS 的平台接入、生命周期、软键盘、移动字体、移动页面、GPU、无障碍和发布；整个移动端方向暂不纳入当前路线图。
- Graphite、Vulkan、Metal、D3D 专用渲染后端。第一阶段统一使用 Skia Ganesh + OpenGL。

桌面触屏、`ControlDensity::Touch`、窄窗口布局和通用 `safeArea` 指标仍可用于
桌面交互与布局，不代表移动平台支持承诺。现有移动实验代码与 headless 记录保留为
历史资产；其存在不构成新增移动功能或模拟器/真机验收任务。

### 1.3 发布完成定义

“自用版完成”必须同时满足以下条件：

1. 三桌面平台在固定支持矩阵上构建、运行 settings 级示例并完成窗口 smoke。
2. Skia 真实文本 shaping 参与桌面布局；CPU headless 仍保持确定性回归。
3. Grid 和固定/估算高度 VirtualList 能支撑至少千项工具数据列表。
4. 语义树、键盘导航、表单校验、弹窗和导航状态在视觉、交互和语义层保持一致。
5. 三桌面 GPU 门槛通过；GPU 运行时失败能记录原因、切换 CPU 并保留应用状态。
6. 生成可直接分发的 Windows zip、Linux tar.gz/AppImage、macOS `.app` zip。
7. 所有阶段的代码、测试、CI、README、支持矩阵和路线图状态一致。

## 2. 当前实现盘点

### 2.1 已完成基线

| 范围 | 当前状态 | 证据 |
| --- | --- | --- |
| 阶段 0–6 | 已完成核心闭环 | core、layout、CPU renderer、SDL3、交互、DSL、Skia 基础适配 |
| v0.2 7A–7E | 已完成主要运行时能力 | 命令录制/回放、damage、FrameScheduler、异步资源、GPU 回退、基准设施 |
| v0.3 8A | 已完成平台无关 host 契约 | `ApplicationHost`、`WindowId`、`WindowMetrics`、`HostEvent`、Fake host |
| v0.3 8B | 已完成编辑模型和基础文本契约 | grapheme、UTF-8、selection/composing、IME 状态机、TextField |
| v0.3 8C | 已完成语义契约 | `SemanticsTree`、identity diff、Recording bridge、语义 action |
| v0.3 8D | 已完成应用基础组件 | ScrollView/ListView、Form、Dialog、Navigator、settings 示例；`runApp` 支持多个 `AppShell` 按 WindowId 隔离运行 |
| v0.3 8E | macOS 桌面接入；另保留历史 SDL-free 实验接缝 | SDL3 桌面 host；`MobileHostSeam` 只代表已有通用状态机，不代表 Android/iOS 支持 |
| 视觉 V1/V2 | 已完成主要状态样式迁移 | token、Theme、StyleResolver、ResolvedStyle、状态与 damage 联动 |
| 自用 M1 | 已完成真实文本与编辑闭环 | SkiaFontManager、shaped TextLayout/RenderCommand、UAX#9 子集、EditingHistory（见 §10 M1 完成记录） |
| 自用 M2 | 已完成应用框架层与 C++ DSL | `lumen-app`（AppShell/runApp）、counter/settings 迁移、C++ builder 补齐；多窗口路由已接入 `AppWindow` 绑定（见 §10 M2 完成记录） |
| 自用 M3 | 已完成布局/Grid/VirtualList/Image | Grid/Image/VirtualList 组件、VirtualListController、virtual-list 基准场景（见 §10 M3 完成记录） |
| 自用 M4 | 已完成三桌面平台服务与窗口能力 | 文件选择/OpenURL/通知/光标/图标契约与 SDL/Fake 实现、能力统一报告、settings 服务区（见 §10 M4 完成记录） |
| 自用 M5 | 已完成语义契约与键盘可用性收口 | invalid/hidden flags、语义桥驱动、focusFirstFocusable 焦点恢复、Recording bridge 回归证据（见 §10 M5 完成记录） |
| 自用 M6 | 已完成视觉系统 V3 与控件库 | 图标/阴影/滚动条 token 路径、六控件、ThemeScope、PlatformThemeAdapter、settings 控件+主题页（见 §10 M6 完成记录） |
| 自用 M7 | 已完成 GPU 门槛与性能 | macOS GPU CI/新基准场景/partialSubmit 评估/文本性能修复/Element move 管道；性能门槛 6/6 达标（见 §10 M7 完成记录） |
| 自用 M8 | 已完成三桌面便携发布 | install/CPack/依赖入包/RPATH/CI package job + 解包冒烟（见 §10 M8 完成记录） |
| 增强链 M10 | 已完成动效与滚动体验 | 动画帧调度/整节点透明度转场/状态色过渡/惯性滚动（见 §10 M10 完成记录） |
| 增强链 M11 | 已完成 v0.4 视觉方向与控件体验 | 四方向 Theme 变体/Tooltip hover 延迟/框架级 overlay/Dropdown 浮动菜单（见 §10 M11 完成记录） |
| 增强链 M12 | 已完成平台服务与发布补全 | 系统主题事件/强调色输入、三平台原生通知、三平台 OS 可访问性偏好查询、AppImage/.app/包变体（见 §10 M12 完成记录） |
| 增强链 M13 | 进行中（原生 provider 已接入三平台） | 契约扩展/runApp 装配/UIA fragment 树、Linux AT-SPI2、macOS NSAccessibility；真实屏幕阅读器回环待平台验收（见 §10 M13 进行中记录） |
| M14 实战可用收敛 | 规划中（M13 provider 验收后的发布硬化阶段） | 真实桌面验收、稳定性能门槛、生产生命周期与诊断、数据密集型控件；按阶段出口推进 |
| 按需控件增强 · 集合控件 | 已完成 List/Tree/TreeList 与共享选择模型 | 四选择模式/树扁平化/源视口滚动框架接管（见 §10 集合控件完成记录） |
| 按需控件增强 · 菜单与分栏 | 已完成 ContextMenu/MenuBar 与 Splitter | Secondary 通道/M11 overlay 菜单面板/分隔条框架接管（见 §10 对应完成记录） |
| 按需控件增强 · 自定义标题栏 | 已完成无边框窗口 chrome | customTitleBar/拖拽区谓词/窗口操作宿主服务（见 §10 标题栏完成记录） |

M7/M8 当时记录的验证基线：Linux CPU Debug 374/374、Skia Release 380/380、GPU Release 387/387（其中 3 个硬件相关用例按环境跳过），另有历史 mobile-core 356/356。这些数量不是当前提交的复测结果，mobile-core 结果也不代表移动设备验证。Windows/macOS 对应门槛由 CI package/GPU job 负责验证。M7/M8 的跨平台 CI 与便携包 job 已纳入工作流。

当前盘点的源码入口（行号随实现变化不作为稳定引用）：应用壳位于 `src/app/app_shell.cpp`，`runApp` 装配位于 `src/app/run_app.cpp`；counter/settings 的 build 与业务状态仍在各自示例头文件中；Widget 类型定义位于 `include/lumen/core/widget.h::WidgetType`；C++ DSL builder 位于 `include/lumen/dsl/dsl.h`；表单内置校验器位于 `include/lumen/widgets/form.h::FormController::nonEmpty/minLength`；GPU `partialSubmit` 当前在 `src/render/skia_gpu_renderer.cpp::SkiaGpuRenderer::capabilities` 固定为 false；移动接缝实现位于 `src/platform/mobile/mobile_host_seam.cpp::MobileHostSeam`。

### 2.2 仍缺少的能力

| 领域 | 当前实现 | 对自用版的影响 | 计划里程碑 |
| --- | --- | --- | --- |
| 文本 | M1 已完成：`SkiaFontManager`（字体族/weight/回退/度量/shaping，pimpl 无 Skia 类型）+ `TextLayoutResult` shaped run/glyph/cluster/baseline；CPU 占位与 Skia 共享同一契约，缺字体明确诊断 | 复杂脚本合字（HarfBuzz 级）属按需评估池（§4）；默认字体栈和 Gallery 不依赖 M9 | M1 已收口 |
| 编辑 | M1 已完成：`EditingHistory` undo/redo 栈、事务边界、连续输入合并、Ctrl+Z/Shift+Z/Y、IME 提交单事务、preedit 不进栈 | 富文本编辑不纳入第一版 | M1 已收口 |
| 方向文本 | M1 已完成：UAX#9 确定性子集（强/弱/中性类 + L2 重排），混合方向命中测试可靠，grapheme 边界为唯一编辑索引 | 显式嵌入控制/镜像括号/数字定形属按需评估池（§4） | M1 已收口 |
| 应用框架层 | M2 已完成：`lumen-app` 目标（`app::AppShell` + `app::runApp`）统一主循环/事件泵/重建/damage/DPI/IME 同步，支持 Fake host、外部 renderer 注入和多个 `AppWindow` 绑定；counter/settings 已迁移 | 真实三桌面多窗口交互仍需单独窗口 smoke；宿主窗口生命周期由调用方拥有 | M2 已收口 |
| DSL | M2 已完成：C++ builder 补齐 stack/checkbox/switch_widget/scroll_view/list_view/focus_scope，与 `.lumen` 冻结节点集对齐（golden 对照测试）；Dialog/Navigator 经 `widgets::makeDialog`/`NavigatorController` 提供 | DSL 可编程性/脚本能力不纳入第一版 | M2 已收口 |
| 控件库 | M6+M11 已完成：Slider/ProgressBar/Radio/Tooltip/Dropdown/Tabs 六控件 + Scrollbar 实绘 + `FormController::compose`；M11 收口 Dropdown 浮动菜单（overlay + 键盘导航）与 Tooltip hover 延迟显隐；集合控件 List/Tree/TreeList 与共享选择模型（2026-09-17）、ContextMenu/MenuBar/Splitter（2026-09-18）随按需控件增强补齐 | — | M6+M11 已收口（控件增强见 §10） |
| 布局 | M3 已完成：Grid（固定列数/最小列宽自适应/行列间距）与约束传播扩展；Image Widget（占位/位图） | 横向网格/跨行列合并留按需评估（§4） | M3 已收口 |
| 滚动 | M3 已完成 VirtualList，M10 已完成触摸拖动与惯性；ScrollView 已支持水平轴、水平/嵌套滚轮路由和双向滚动条，Gallery 已加入超宽卡片演示 | VirtualList/Tree 的源控制器仍为纵向；自动隐藏、RTL 与水平虚拟化仍待补 | M3+M10+P2 已收口 |
| 平台服务 | M4+M12 已完成：文件选择/OpenURL/光标/图标（SDL）+ 通知与强调色（M12 原生 seam：Win32/DBus/AppKit）+ SDL 系统主题事件与 `adaptPlatformTheme` 派生 + 高对比/减少动画/字体缩放查询 | 平台服务不可用时保持安全默认；Linux/macOS 原生服务以 CI 证据为准 | M4+M12 已收口 |
| 无障碍 | M5 已完成：语义契约收口（invalid/hidden flags、Image 可访问名、滚动视口隐藏传播）+ AppShell 语义桥驱动（每帧 identity diff/焦点/action 回执）+ FocusScope/焦点恢复（Tab 域内、Escape/返回、modal 关闭后恢复）| M13 已接入 Windows UIA、Linux AT-SPI2、macOS NSAccessibility（Recording bridge + provider headless 回归）；三平台真实屏幕阅读器回环待验 | M5 已收口；M13 进行中 |
| 视觉 V3 | M6 已完成：IconId/IconTheme 目录化、ElevationTokens→DrawShadow（Skia blur；CPU 采用明确的扁平降级）、ThemeScope 布局期子树覆盖、PlatformThemeAdapter、transitionAlpha 通道 | M10 已收口转场动画驱动；v0.4 视觉方向属 M11 | M6+M10 已收口 |
| GPU | M7 已完成：macOS GPU 纳入 CI、新基准场景归档、partialSubmit 实测评估（1.16× 维持全帧提交）、文本/布局性能修复 + Element move 管道 | Linux GPU job 要求实际 `skia-gpu`；Windows/macOS job 在无硬件环境可明确跳过，不能视为硬件呈现验收 | M7 已收口；硬件覆盖受 runner 约束 |
| 发布 | M8+M12 已完成：install/CPack/RPATH/package job + Linux 桌面集成（desktop/icon）与 linuxdeploy AppImage + macOS Lumen.app 骨架 + package-skia（三平台）与 package-skia-gpu（Linux llvmpipe）变体 | Windows/macOS GPU 包与 CPack Bundle 生成器留后续；新形态以 CI 首跑为事实来源 | M8+M12 已收口 |

移动端不列为桌面自用版的能力缺口；已存在的实验接缝和字体代码见 §4 M9 冻结说明。

## 3. 版本和依赖关系

里程碑按依赖顺序实施，不以日历日期承诺完成时间。每个里程碑完成后必须通过自己的出口条件，再进入下一个阶段。

```text
M0 基线冻结
 ├─ M1 真实文本与编辑 ─────────────┐
 ├─ M2 应用框架层与 C++ DSL ───────┤
 └─ M3 布局、Grid、VirtualList、Image ─┤
                                      ↓
                         M4 平台服务与窗口能力
                                      ↓
                         M5 语义与键盘可用性
                                      ↓
                         M6 视觉系统 V3 与控件库
                                      ↓
                         M7 GPU 与性能门槛
                                      ↓
                         M8 三桌面便携发布
                                      ↓（桌面自用版收口）
                         M10 动效与滚动体验
                                      ↓
                         M11 v0.4 视觉方向与控件体验
                                      ↓
                         M12 平台服务与发布补全
                                      ↓
                         M13 原生无障碍 provider
                                      ↓
                         M14 实战可用收敛
```

M1–M3 可以并行准备，但必须全部达到各自出口条件后才能进入 M4；之后按 M4 → M5 → M6 → M7 → M8 合入。M0–M8 构成桌面自用版，已全部收口。增强链从 M10 起编号按序推进：M10 → M11 → M12 → M13 → M14；M9 已冻结，不在实施链中，M8 完成不触发 M9。M12 与 M11 之间无强依赖，可按实际需要调换顺序；M13 依赖 M11 的高对比主题输出，M14 依赖 M13 的 provider 实现并负责把 headless 能力收敛为真实桌面可用性。

## 4. 里程碑详细计划

### M0：基线冻结与工程入口

**目标**：把当前已完成能力和后续目标分开，建立唯一可追踪的验收入口。

**任务**

- 新增本路线图并链接到 README、支持矩阵和版本计划。
- 为桌面 CPU、Skia、GPU、headless、窗口 smoke 和基准建立统一命令表；现有 mobile-core 命令单列为历史实验配置参考。
- 记录三桌面 CI 的操作系统、编译器、SDL/Skia 版本和依赖安装方式。
- 使用 `benchmarks/lumen-scene-bench` 生成并归档 v0.2 CPU 基线报告；至少固定 viewport、场景规模、warmup/测量帧数、编译配置、frame hash，以及 reconcile/layout/paint 的 p50/p95、分配次数和分配字节数。
- 将 CPU 基线固定保存为 `docs/perf-baselines/v0.2-cpu-scene.json`，同时上传带 commit、平台和配置元数据的 CI artifact；M7 只允许引用该文件和确切生成命令。
- 在基线 JSON 中明确 `backend`、`scenario`、`viewport`、`warmup_frames`、`measured_frames`、`toolchain`、`build_type`、`frame_hash`、各阶段 p50/p95 和分配统计字段，禁止用不同场景或不同后端直接比较。
- 将“接口已存在”“headless 已验证”“真实平台已验证”“可发布”拆成四种状态。
- 为每个后续里程碑增加完成记录模板：变更、测试、平台、已知限制、回滚点。

**出口条件**

- 文档之间不存在相互矛盾的阶段状态。
- 基线测试可在干净构建目录重复运行。
- 后续任务均能指向明确的模块、测试和出口条件。

### M1：桌面真实文本与编辑闭环

**目标**：让桌面正式字体、布局、绘制和编辑状态使用同一套文本事实。

**实现**

- 在 `lumen-text` 增加 Skia-backed `FontManager`，封装字体族查询、weight/style、fallback、glyph metrics 和 shaping。
- 扩展 `TextLayoutResult`，保存 shaped run、glyph id、位置、cluster 映射、baseline、行高和命中测试信息。
- `LayoutEngine` 和 Skia renderer 共享同一份布局结果；CPU 继续使用 `PlaceholderFontManager`，不依赖 Skia。
- 为混合 LTR/RTL 接入完整双向段落处理，保留 grapheme 边界作为编辑索引。
- 为 `TextEditingValue` 增加 undo/redo 栈、事务边界、连续输入合并、快捷键和 IME 提交边界。
- 保持 TextField 的多行滚动、选区、caret、composition underline、密码和只读行为。

**接口约束**

- `FontManager` 公共接口不暴露 Skia 类型。
- shaped run 的生命周期由 `TextLayout` 管理，RenderCommand 只接收可序列化的文本绘制数据。
- 字体不可用时必须返回明确 fallback 状态，不能静默改变编辑索引。
- `TextStyle.lineHeight` 是相邻行基线距离相对字号的倍数，紧行高允许
  行框重叠。`TextLayoutResult.lineHeightPx` 保存该步进，
  `lineBoxHeightPx` 独立容纳可见文本各字体的最大 ascent + 最大 descent；
  段落高度包含最后一行完整行框。省略掉的文本不参与度量，全空文本使用
  默认字体度量。光标、选区和控件居中使用行框，行定位/命中仍按步进。
- Windows 系统字体的 GDI 度量与光栅共用字体创建及实际族校验；GDI
  静默替换请求族时，两条路径均回退已加载的 stb 字体，并通过字体
  管理器诊断报告 `gdi-substitution`，避免默认字体混入布局/位图。
  系统/Skia 字形度量同时包含字体度量和实际绘制上下界，覆盖 hhea 指标不足
  以容纳轮廓的 emoji 等字形。

**验证**

- 单测覆盖 UTF-8 错误、grapheme、CJK、emoji、组合字符、纯 RTL 和混合 RTL。
- headless 覆盖插入、删除、选区、IME preedit/commit/cancel、undo/redo。
- Windows/Linux/macOS 各执行一次真实输入法或录制事件 smoke。
- 固定字体环境下比较文字几何、caret、selection 和 frame hash；不要求不同系统字体产生相同像素。

**出口条件**：桌面 Skia 布局不再使用占位字体度量；编辑操作可撤销/重做；缺少正式字体时仍可启动并显示诊断。

### M2：应用框架层与 C++ DSL

**目标**：把每个应用都要重复编写的生命周期、事件路由、重建、布局、绘制和 damage 管线收敛成可复用的应用壳。

**实现**

- 增加 `lumen-app` 目标，提供 `runApp`/应用壳、UI 线程主循环、事件泵、`rebuildIfDirty`、`renderFrame`、damage 管理和 DPI/IME 状态同步。
- 应用壳通过 `ApplicationHost`、`FrameScheduler`、`Renderer`、`StateStore` 和 `InteractionController` 组合工作，不把 SDL 或 Skia 类型放进公共头文件。
- 将 counter/settings 中重复的主循环和帧管线迁移到应用壳；示例只保留 build 函数、状态和业务 handler。
- 补齐 C++ DSL builder，使基础容器、TextField、Checkbox、Switch、ScrollView/ListView、FocusScope、Dialog、Navigator 和常用样式属性与文本 DSL 的能力对齐。
- 定义应用壳的错误处理、关闭请求、renderer 替换、surface 重建和 host 服务不可用行为。
- 将应用壳拆成可独立链接的 `lumen-app` 目标；示例应用通过依赖注入选择 Fake host、SDL host 或测试 renderer。

**接口约束**

- `runApp` 只接收平台无关的应用描述和 `ApplicationHost`/Renderer 注入点；应用状态仍由 UI 线程拥有。
- build 过程不能直接操作平台窗口或 Renderer；所有绘制都经 `RenderCommandList` 提交。
- 应用壳必须支持 headless Fake host 和外部测试 renderer，保证现有测试不依赖真实窗口。

**验证**

- counter/settings 迁移后主循环样板显著减少，行为和 frame hash 与迁移前一致。
- headless 覆盖事件顺序、dirty 合并、DPI 变化、IME 候选区同步、renderer 替换和关闭请求。
- C++ builder 与 `.lumen` DSL 构建相同控件树时，节点类型、属性、key 和错误诊断一致。

**出口条件**：新增一个工具页面只需提供应用 build/状态逻辑即可运行；应用壳统一负责事件、帧、damage、DPI 和 IME 管线。

### M3：约束布局、Grid、VirtualList 与 Image

**目标**：让工具类应用能处理窄窗口、复杂排列和大数据量列表。

**实现**

- 扩展 LayoutEngine 的约束传播、intrinsic、baseline、wrap、overflow 和 safe-area inset 处理。
- 增加 Grid：固定列数、最小列宽、列间距、行间距、主轴/交叉轴对齐和窗口变化重排。
- 增加 `Image` Widget，把现有 `ResourceManager`/`DrawImage` 能力接入声明式 Widget 树；资源未就绪时显示固定占位并保留可访问名称。
- 增加 VirtualList：
  - `itemCount`：数据项数量。
  - `itemBuilder(index)`：按需构建可见项目。
  - `estimatedExtent`：项目初始估算高度。
  - stable key：项目复用和状态保留依据。
  - viewport cache：可见区前后保留有限缓存。
- ScrollController 统一鼠标滚轮、键盘 PageUp/PageDown/Home/End、触摸拖动和语义滚动。
- 列表项目被回收时保留必要的编辑焦点、语义焦点和应用状态；不允许异步完成复活旧项目。
- 在 settings 示例中加入 Grid 页面和千项 VirtualList 页面。

**接口约束**

- VirtualList 不要求应用把全部 Widget 子树预先放入 `children`。
- stable key 变化必须被视为项目替换，不能复用旧 Element 状态。
- estimated extent 修正后，滚动锚点必须保持稳定，不跳回列表顶部。
- CPU、Skia 和 GPU 消费同一份布局结果和绘制命令。

**验证**

- Grid 在 320px、768px、1080p 和连续 resize 下几何稳定。
- VirtualList 覆盖首屏、快速拖动、键盘定位、项目复用、动态数据变更和焦点保持。
- 1000/10000 项场景报告构建节点数、命令数、分配量和 p50/p95 帧时间。
- 局部 damage 与 forced full repaint 的像素结果一致。

**出口条件**：千项列表不会一次性构建全部子树；滚动、焦点、语义和状态在复用后保持正确；Grid 在窄窗口仍可用。

### M4：三桌面平台服务与窗口能力闭环

**目标**：补齐工具应用所需的系统服务，并把平台差异限制在 host/provider 实现中。

**实现**

- 在 `ApplicationHost` 的平台服务层增加文件选择、外部链接打开和通知接口。
- 增加鼠标系统光标形状和窗口图标接口，SDL 实现隐藏在 platform 目标内，Fake host 提供可断言记录。
- 扩展 `PlatformCapabilities`，统一报告 dark mode、accent color、字体缩放、对比度、touch capability 和服务可用性。
- Windows/Linux/macOS 分别完成真实剪贴板、TextInputSession、DPI、safe area、最小化/恢复和窗口销毁验证。
- 文件选择和通知服务不可用时返回结构化失败原因，不阻塞 UI 线程。
- 应用配置、数据库、网络和同步留在应用层，不进入 Lumen 公共模块。

**验证**

- Fake host 为所有服务提供确定性 fake 实现和失败注入。
- 三桌面窗口 smoke 覆盖多窗口、resize、DPI、剪贴板、文本输入、文件选择和退出。
- 窗口 smoke 覆盖光标形状、窗口图标、菜单关闭和服务不可用诊断。
- 服务失败时检查应用状态、焦点、文本选区、滚动位置和导航栈不丢失。

**出口条件**：同一工具示例能在三桌面完成打开文件、编辑、复制粘贴、通知、缩放和退出；平台服务失败有可读诊断。

### M5：语义契约与键盘可用性收口

**目标**：让没有原生屏幕阅读器的环境也能完成完整的可访问性结构验收。

**实现**

- 固化 Button、TextField、Checkbox、Switch、List、Grid、VirtualList、Dialog、Navigator 的 role、label、value、bounds、flags 和 actions。
- 完成 FocusScope 的 Tab/Shift+Tab 顺序、语义焦点、Escape/返回、modal barrier 和焦点恢复。
- 让 disabled、checked、selected、invalid、focused 同时影响视觉、hit test、键盘和语义。
- Recording bridge 记录树更新、identity diff、焦点变化和 action 结果。
- 该条是 M5 阶段的历史边界：当时 `createPlatformAccessibilityBridge` 报告 provider 未纳入。当前 M13 已增加三桌面原生 provider；真实屏幕阅读器回环仍不阻塞第一版便携发布，但必须在 M13 平台验收记录中单独标记。

**验证**

- headless 验证完整语义树、稳定 identity、顺序、隐藏节点、禁用节点和 action。
- settings 示例验证表单错误、列表滚动、Dialog dismiss、Navigator back 和焦点恢复。
- 键盘路径与语义 action 必须触发相同的 Widget handler。

**出口条件**：语义结构、键盘行为、视觉状态和实际交互没有分叉；Recording bridge 可作为跨平台回归证据。

### M6：自用级视觉系统 V3 与控件库

**目标**：完成工具类应用所需的视觉反馈，而不破坏已有 token 和渲染后端边界。

**实现**

- 实现 `IconId`/`IconTheme` 的统一图标资源和矢量绘制入口；控件只引用语义 ID。
- 扩展 RenderCommand：增加 path/icon/shadow 所需的可序列化命令，明确 bounds 和 damage 规则。
- CPU、Skia 光栅和 GPU 对阴影不支持时，使用 token 指定的边框/表面降级，不散落控件常量。
- 将 `MotionTokens` 接入状态过渡、Dialog 转场和 Navigator 转场；`reduceAnimation` 时持续时间为零。
- 增加局部 `ThemeScope`，定义父主题、子树覆盖和状态解析优先级。
- 实现 `PlatformThemeAdapter`，只转换系统主题输入和能力，不返回平台控件对象。
- 增加工具类常用控件：Dropdown/菜单、Tooltip、Slider、ProgressBar、Radio 和 Tabs；控件状态、焦点和语义沿用现有 StyleResolver/WidgetState。
- 实现 Scrollbar 控件的实际绘制和拖动交互，使已冻结的 `ScrollbarTokens` 进入 RenderCommand/Widget 路径。
- 扩展 FormController 的校验器注册和测试覆盖；邮箱、范围、格式等校验通过应用可组合 Validator 提供，不把业务规则写死在框架中。
- settings 增加控件展示页和主题调试页。

**验证**

- 图标、阴影、动效和主题切换的 CPU/Skia/GPU 命令一致性。
- 状态切换、主题切换和局部 damage 不残留旧颜色或旧阴影。
- high contrast、font scale、touch density、reduced motion 和响应式断点覆盖。

**出口条件**：视觉扩展全部由 token/StyleResolver 驱动；渲染后端不依赖 Theme 对象；主题和动效设置可以在运行中切换。

### M7：三桌面 GPU 生产门槛与性能

**目标**：把当前可选 GPU 路径提升为三桌面支持矩阵中的发布门槛。

**实现**

- 沿用 Skia Ganesh + OpenGL，补齐 macOS 的上下文创建、surface 包装、resize、flush 和交换验证。
- 保持 GPU 探测、初始化失败、上下文丢失、交换失败到 CPU 的安全恢复路径。
- 将 GPU 成功初始化纳入 Windows/Linux/macOS CI；硬件不可用的环境使用明确配置的软件适配器或专用 runner。
- 增加文本密集、Grid、VirtualList、语义 diff、资源上传和局部 damage 基准。
- 记录 CPU 构建、layout、paint、submit、GPU wait、分配量、命令数、缓存命中率和 frame hash。
- 评估并实现 GPU 的 `partialSubmit`/damage-preserve 路径；若驱动或 surface 不支持，能力报告必须明确标记为全帧提交，并保留正确性优先的回退。
- 性能比较按“同一后端 + 同一场景 + 同一工具链”进行：CPU 复用 M0 的 v0.2 基线；Skia/GPU 先各自记录 M7 初始基线；文本密集、Grid、VirtualList、语义 diff 等新增场景以首次归档报告为基线。GPU wait 只在 GPU 基线中比较，不与 CPU 数值互比。

**发布规则**

- 支持矩阵中的标准 runner 必须通过 GPU 初始化和一帧呈现。
- 非支持驱动允许回退 CPU，但必须在 diagnostics 中显示能力和原因。
- GPU 恢复后必须重建 surface/资源，保留 StateStore、Element、文本编辑状态、焦点、滚动和导航栈。

**出口条件**：三桌面 GPU CI 通过；长时间运行、快速 resize、资源压力和故障恢复无崩溃、卡死、泄漏或旧资源复活；GPU 的局部提交能力有实测结果和降级说明；p50/p95 相对 M0 归档的 v0.2 基线恶化不超过 10%。

### M8：三桌面便携发布

**目标**：让自用应用可以脱离开发环境直接复制和运行。

**实现**

- 增加 CMake install 目标和发布脚本，收集可执行文件、运行时依赖、资源和版本信息。
- 生成 Windows zip、Linux tar.gz/AppImage、macOS `.app` zip。
- 包内包含 settings/counter 示例资源、启动说明、诊断开关和已知限制。
- 归档调试符号、构建元数据、编译器版本、Skia/SDL 版本和依赖许可证信息。
- CI 执行构建、打包、解包、headless smoke、窗口 smoke 和版本信息检查。

**出口条件**：三平台包在干净机器或干净容器中可启动；依赖缺失、GPU 不可用和字体缺失时都有清晰提示；发布产物可追溯到 Git commit。

### M9：Android/iOS 原生接入（暂不实现，已冻结）

**状态**：2026-09-14 起冻结。保留编号仅用于解释历史记录，不安排实现任务、
出口条件、移动预览版或后续版本接入时间；M8 完成不触发 M9。

**已有内容的定位**：仓库保留 `MobileHostSeam`、`lumen-mobile-host`、
`LUMEN_BUILD_MOBILE_CORE`、`createMobileFontManager` 和 `FontBackend::Mobile`。
这些是历史实验代码；字体族枚举、默认字体栈或目录查询能力不能证明移动端文本
输入、字形绘制、原生 host 或发布链路已经可用。桌面默认字体和 Gallery 工作按
桌面功能验收，不作为 M9 进度。

**当前约束**：不新增或执行 Android/iOS 工程、软键盘、移动字体管线、移动示例、原生
无障碍或移动 GPU/发布任务；不要求模拟器或真机验证。已有 Linux/macOS
`mobile-core` CI job 仍按现有工作流运行，只验证 SDL-free 通用代码的兼容性，
不作为移动产品验收。本次范围调整不删除历史代码、构建目标、测试或 CI job，但这些
遗留项不得派生新的移动实现任务。

只有用户重新明确提出移动端需求后才重新评估范围和架构；旧方案不作为待执行清单。

### M10：动效与滚动体验

**目标**：让 MotionTokens 从“通道就绪”变为真实驱动 Dialog/Navigator 转场与状态色过渡，并补齐触摸拖动与惯性滚动；静态场景零动画帧、既有哈希不变。

**实现**

- `AppShell::tick` 从 caret 专用泛化为通用动画驱动器：活动 Tween 注册表（转场/状态过渡/fling 共用），存在活动动画时置脏并请求 `FrameReason::Animation` 帧；`runApp` 的 `setAnimationsActive` 从 caret 专用扩为「caret 闪烁或存在活动动画」。
- `transitionAlpha` 从仅图标描边消费升级为整节点透明度：表面/边框/文本/图标/阴影统一乘入，CPU/Skia/GPU 消费同一命令数据；`reduceAnimation` 时零时长直达终态。
- Dialog show/dismiss 按 `dialogTransitionMs` 淡入/淡出，退出动画期间延迟移除（dismissing 状态 + tween 结束再真正关闭），动画期间焦点恢复与语义 diff 保持一致；Navigator push/pop 按 `navigatorTransitionMs` 页面级过渡。
- 状态色过渡：WidgetState 变化时按 `stateTransitionMs` 对 ResolvedStyle 颜色通道插值；动画状态放 Element/控制器侧，不增加 Widget 字段（M7 刚完成体积削减）。
- `ScrollController` 增加拖动速度采样与确定性 fling 衰减（固定步长推进，`isFlinging` 供动画驱动器调用）；`InteractionController` 把命中滚动视口的拖动喂 `applyDrag`（TextField 上拖动仍走选区路径）；wheel/wheelSink 消费状态回传（收口 M5 已知限制“语义滚动 sink 恒返回 true”）。

**接口约束**

- 动画时间源唯一：应用注入时钟（`tick(nowMs)`），测试固定步进保持确定性。
- 静态场景（无状态变化、无对话框、无 fling）不产生动画帧；M0 基线与既有 headless 哈希不变。
- alpha/颜色插值只改变绘制数据，不改变布局几何、hit test 与语义树内容。

**验证**

- headless：tween 调度/完成、alpha 像素消费（中间态与终态）、Dialog/Navigator 进出场状态机（终态后行为/语义/焦点与现状一致）、状态色插值端点、fling 物理确定性、拖动→滚动路由 vs 文本选区、reduceAnimation 零时长路径。
- 动画帧上局部 damage 与全帧像素等价保持。

**出口条件**：三后端转场一致；reduceAnimation 全归零；既有测试与基线 hash 全绿；新增路径全部有 headless 测试。

### M11：v0.4 视觉方向与控件体验

**目标**：把 `design/gallery.html` 四方向设计稿沉淀为可切换的 token/Theme 方向，并补齐控件体验缺口（Tooltip hover 延迟、Dropdown 浮动菜单）。

**实现**

- 评审四方向（Core Dark / Ink & Linen / Aurora Signal / Utility Contrast），选定默认基线；token/Theme 方向变体化（含 Light、High-contrast），遵循 `lumen-visual-system-design.md` 的 semantic color、4px grid、focus ring。
- gallery 示例对齐选定方向，支持方向/主题运行中切换演示。
- Tooltip hover 延迟驱动（常驻节点改为 hover 进入/离开显隐，复用 M10 动画驱动）；Dropdown 浮动菜单层（脱离树内展开，含键盘导航与模态 barrier）。

**出口条件**：视觉扩展全部由 token/StyleResolver 驱动；主题方向可在运行中切换；三后端命令一致；新交互有 headless 覆盖。

### M12：平台服务与发布补全

**目标**：补齐 M4/M8 留下的结构化降级项与发布形态。

**实现**

- 原生通知后端（SDL 3.2.10 无 API 的平台走原生层：Windows Toast/传统通知、Linux DBus org.freedesktop.Notifications、macOS UserNotifications）。
- SDL 宿主在初始化与运行期间查询 `highContrast`/`reduceAnimation`/`fontScale`：Linux 异步读取 portal `ReadAll`，macOS 读取 AppKit display preferences 与 preferred body font，Windows 读取 SystemParametersInfo 与 Accessibility 注册表；变化经 `SystemAccessibilityChanged` 广播，`runApp` 在首帧前及运行期间应用到全部跟随窗口。`AccessibilityOverrides` 逐项优先、清空恢复跟随；服务失败保留默认或上次有效快照。平台键缺失与真实设置面板验收边界见支持矩阵。
- 窗口级光标（Win32 `WM_SETCURSOR` 等，替换进程级 `SDL_SetCursor` 语义）。
- 发布形态：Linux AppImage（linuxdeploy）、macOS 完整 `.app` bundle（CPack Bundle/Info.plist）、CI package job 增加 Skia/GPU 包变体。

**出口条件**：服务失败仍结构化降级且 UI 线程不阻塞；三平台新形态在 CI 解包/启动冒烟通过；能力报告如实反映。

### M13：原生无障碍 provider

**状态**：进行中——Windows UIA、Linux AT-SPI2、macOS NSAccessibility provider 已实施（编译/headless 回归）；讲述人/NVDA、Orca、VoiceOver 真实回环仍待对应桌面平台验收。出口条件以三平台全部收口为准。

**目标**：把语义契约接到三平台原生屏幕阅读器（UIA/AT-SPI/NSAccessibility）。

**实现**

- 按 `AccessibilityBridge` 契约实现三平台 provider，`createPlatformAccessibilityBridge` 工厂按平台分发；消费预留开关 `LUMEN_ENABLE_ACCESSIBILITY_BRIDGE`。
- AT 事件回灌走既有 `performAccessibilityAction` 路径；`PlatformCapabilities.accessibility` 如实置位。
- 依赖 M11 的高对比主题与 M10 的 reduceAnimation 语义输入。

**出口条件**：每平台至少一款屏幕阅读器完成焦点导航/激活/值设置回环；Recording bridge 回归不受影响；未启用开关时行为与现状一致。

### M14：实战可用收敛阶段（规划）

**定位**：把已经具备的框架能力从“接口存在/headless 已验证”推进到
“真实平台已验证/可发布”。本阶段优先解决长期运行、性能证据和应用开发效率，
不扩展 Android/iOS，也不提前进入专用 GPU 后端或完整富文本实现。

**进入条件**：M13 三平台 provider 已完成编译和 headless 回归；真实读屏器回环、
性能稳定性和以下生产硬化项分别建立可追溯的验收记录。Xvfb、dummy video、Fake
host 和共享 runner 的一次性结果只能作为诊断证据，不能直接满足 M14 出口。

**实施顺序**：

1. **M14-A 真实平台验收（P1）**
   - 在登录的 Linux X11、Linux Wayland、macOS Aqua 和 Windows Win32 runner 上运行
     settings/Gallery 多窗口 smoke。
   - 验证真实窗口生命周期、DPI、快速 resize、IME preedit/commit/cancel、候选窗锚点、
     跨应用剪贴板、透明合成和 GPU/present 故障恢复。
   - 完成 Orca、VoiceOver、Narrator、NVDA 的焦点导航、激活、值设置、编辑、弹窗、
     resize 和关闭/重开回环；读屏器版本、桌面环境、GPU/驱动和附件必须归档。
   - 在对应真实会话运行至少一小时的双窗口、资源压力、resize 和恢复测试；记录帧数、
     资源峰值、恢复次数以及文档/焦点/选区/滚动状态是否保持。

   **出口**：四类真实桌面记录均通过；Linux 两种会话、macOS 和 Windows 的人工清单
   全部有证据；一小时测试无崩溃、卡死、泄漏、旧资源复活或状态丢失。

2. **M14-B 性能门槛收口（P1）**
   - 为 CPU、Skia 光栅和 Skia GPU 固定同后端同场景基线，覆盖 text-heavy、Grid、
     VirtualList、card-grid 和 semantics-diff。
   - 在空闲、固定 CPU 亲和性和固定工具链的 runner 上交错运行基线与候选，归档
     frame/UI build/layout/paint/submit/GPU wait 的 p50/p95、分配次数/字节数、命令数、
     节点数、frame hash、二进制摘要和 runner 元数据。
   - p50/p95 及确定性计数继续使用不超过 10% 的门槛。共享 runner 的自对照失败只能
     标记环境不稳定，不能通过放宽阈值、更新基线或选择有利聚合方式消除回退。

   **出口**：三个后端在稳定 runner 上重复通过；同二进制 A/A 对照稳定；性能报告可由
   任意审阅者重放并确认基线提交、场景和工具链一致。

3. **M14-C 生产生命周期与诊断（P1/P2）**
   - 补齐图片/字体等资源的异步加载、缓存、取消、失败状态和生命周期回收，避免
     应用层自行拼接不可观测的资源状态。
   - 明确窗口关闭、最小化/恢复、DPI 变化、renderer 重建和 GPU 降级时的状态保持契约，
     覆盖焦点、编辑值、选区、滚动位置和未完成异步操作。
   - 统一结构化错误、日志和诊断输出，至少能定位平台、窗口、后端、present、字体、
     资源和无障碍 provider 的不可用原因；服务失败不能阻塞 UI 线程。
   - 在干净机器或容器中验证 Windows zip、Linux tar.gz/AppImage、macOS `.app` zip，
     并核对版本、commit、依赖和运行时资源追溯信息。

   **出口**：settings 级应用可连续运行并处理资源/窗口/renderer 异常；故障有结构化
   诊断和可恢复路径；三平台发布包能在干净环境启动并报告缺失能力。

4. **M14-D 数据密集型控件与滚动（P2）**
   - 在 List/Tree/TreeList 和 VirtualList 之上增加 DataGrid 基础契约：多列、列宽调整、
     排序/筛选回调、行选择、键盘导航、复制粘贴、单元格编辑和校验。
   - 将水平虚拟化、双轴滚动协调和稳定 key/状态复用作为同一场景验收；补齐大数据量
     下的快速 resize、焦点保持和语义树可见区更新。
   - auto-hide scrollbar、RTL 镜像、Tree/List 行内编辑和拖放作为可独立验收的增量，
     不得破坏现有纵向 VirtualList 与嵌套滚动契约。

   **出口**：固定规模和千/万项数据场景下，键盘、鼠标、剪贴板、编辑、语义和滚动
   状态一致；横向虚拟化不产生不可控节点增长；CPU/Skia/GPU 命令和性能报告可追踪。

5. **M14-E 国际文本增强（按产品范围启用，P2/P3）**
   - 目标应用需要阿拉伯文、南亚复杂脚本或高保真排版时，再接入 HarfBuzz 级合字、
     完整 UBA、数字定形、镜像括号和语言相关字体 fallback。
   - 中文/英文桌面工具在 M14-A 至 M14-D 通过前，不以该增强阻塞发布；新增 shaping
     依赖必须固定版本、记录许可证并保持 CPU-only 构建可用。

   **出口**：目标语言集合有固定字体和 glyph/cluster/命中测试证据；复杂脚本 shaping
   不改变已有编辑事务、选区和无障碍索引契约。

**M14 总出口**：M14-A、M14-B、M14-C 必须全部通过；M14-D 作为工具类应用默认交付
   能力；M14-E 按目标语言选择。只有达到 M14 总出口，路线图才可把三平台桌面支持从
   “实现并可构建”描述为“实战可用”。

### 后续按需评估池（不预排编号）

以下方向在 M13 之后按实际需要评估，不承诺版本：

- HarfBuzz 级合字与富文本：Skia 预编译包已附带 skshaper/harfbuzz/icu 归档，`FontManager::shapeCluster` 接口已预留多 glyph 返回。
- 多窗口：`runApp` 已按 `event.window` 路由多个 `AppShell` 实例；宿主层多窗口 API
  与 `AppWindow` 绑定均已接入，真实三桌面多窗口 smoke 仍按发布验证补做。
- 完整 UBA：显式嵌入控制、镜像括号、数字定形。
- 桌面专用 GPU 后端（Graphite/Vulkan/Metal/D3D）。
- M14 之外的控件增强：集合控件拖放、Splitter 窗格塌缩/KeepRatio 与 `.lumen` 节点及基准场景、菜单触摸长按/F10-Alt 单键/mnemonic 下划线、标题栏 macOS 交通灯与 borderless 最大化回退策略（见 §10 各完成记录已知限制）。DataGrid、列宽调整、行内编辑和水平虚拟化已提升为 M14-D 优先事项。
- Spin（SpinBox 数值步进）、Toolbar（工具栏）、StatusBar（状态栏）三控件已实施（2026-09：设计稿 `docs/lumen-{spin,toolbar,statusbar}-design.md` §15 实施状态 + 配套 `design/{spin,toolbar,statusbar}.html`；实现 `include/lumen/widgets/{spin,toolbar,statusbar}.h` + `src/widgets/` 同名源文件，零新增 WidgetType，唯一 core 缝隙为 ProgressBar indeterminate bool + iconRotation float（Widget 体积断言 +8B → 840B）。测试 `tests/{spin,toolbar,statusbar}_tests.cpp` 33 用例 + settings/gallery 演示页（Spin/Toolbar/StatusBar 与 Controls 路由）集成冒烟；SemanticsRole 尾部追加 SpinButton/Toolbar/StatusBar（UIA 映射已接），MotionTokens 追加 spin 重复节奏（reduceAnimation 不归零——输入节奏）与 statusbar 消息/进度/busy 四 token。

## 5. 公共接口与模块边界

### 5.1 必须保持的边界

- `lumen-core`：Widget、Element、State、RenderNode、几何和平台无关事件值类型。
- `lumen-layout`：约束、Grid、滚动视口、intrinsic、baseline 和 VirtualList 可见区计算。
- `lumen-style`：Theme、token、StyleResolver、ThemeScope 和平台主题输入转换。
- `lumen-text`：FontManager、TextLayout、编辑模型、grapheme、字体 fallback 和 shaping。
- `lumen-render`：RenderCommand、CPU/Skia/GPU、FrameScheduler、资源和诊断。
- `lumen-accessibility`：SemanticsTree、Recording bridge、平台桥接接口和 settings。
- `lumen-platform`：ApplicationHost、Window、Clipboard、TextInputSession、文件选择、通知和 host adapter。
- `lumen-app`：`runApp`/应用壳、UI 线程主循环、事件路由、dirty/damage 管理、DPI/IME 同步和 host/renderer 注入；公共头文件不含平台 SDK 类型。
- `lumen-widgets`：组件 builder、Grid、VirtualList、Form、Dialog、Navigator 和 Theme 入口。
- `lumen-dsl`：C++ builder、受限 `.lumen` 文本 DSL、属性转换和解析诊断；公共头文件继续禁止引入平台 SDK 类型。

公共头文件禁止引入 SDL、Skia、Objective-C、Java/JNI 或其他平台 SDK 类型。后台线程只能提交不可变数据和完成通知，UI 树、StateStore、布局和交互仍由 UI 线程拥有。

### 5.2 兼容和迁移规则

- 新增 RenderCommand 必须同步更新序列化版本、反序列化校验、CPU/Skia/GPU 回放和 damage bounds。
- `ResolvedStyle` 必须是值类型，不能保存 Theme、Renderer 或平台对象指针。
- VirtualList 的 stable key 是状态复用的唯一依据；key 改变必须触发项目替换。
- CPU placeholder、Recording bridge、普通 ListView 和 GPU→CPU 回退路径必须持续保留。
- 新增 FetchContent 依赖时固定版本、记录许可证，并说明 CPU-only 行为。

## 6. 测试、CI 和证据要求

### 6.1 每个里程碑的固定顺序

1. 单元和 headless 测试。
2. CPU 命令/像素回归。
3. Skia 光栅和 GPU 命令/像素一致性。
4. 三桌面窗口、输入和系统服务 smoke。
5. 基准、长时间运行和故障注入。
6. README、支持矩阵、路线图和 CI 状态更新。

### 6.2 最终 CI 矩阵

| 平台 | CPU | Skia 光栅 | Ganesh GPU | 窗口 smoke | 便携包 |
| --- | --- | --- | --- | --- | --- |
| Windows | 必达 | 必达 | 必达 | 必达 | zip |
| Linux X11/XWayland | 必达 | 必达 | 必达 | 必达 | tar.gz/AppImage |
| macOS | 必达 | 必达 | 必达 | 必达 | `.app` zip |

此表只定义三桌面发布目标。现有 Linux/macOS `mobile-core` job 属历史实验配置
兼容性检查，继续按工作流运行；不等同 Android/iOS 编译、模拟器或真机门槛。

### 6.3 性能和稳定性门槛

- 固定 1080p 场景继续作为基线，并增加文本密集、Grid、VirtualList 和语义 diff 场景。
- p50/p95 总帧时间、UI 构建、layout、paint、submit 和 GPU wait 相对适用的归档基线不得恶化超过 10%；比较结果必须记录基线文件、当前报告、后端、场景参数和不适用指标。CPU 使用 M0 的 v0.2 基线，Skia/GPU 和新增场景使用同后端同场景基线。
- 记录节点数、命令数、局部重绘比例、分配次数/字节数、字体 shaping 命中率和列表复用率。
- 长时间输入、快速 resize、反复 attach/detach、窗口创建销毁、资源压力和 GPU 故障注入不得出现崩溃、卡死、泄漏或旧资源复活。

## 7. 风险与处理顺序

| 风险 | 表现 | 处理顺序 |
| --- | --- | --- |
| 系统字体差异 | 跨机器像素和字形位置变化 | 固定字体做像素门槛，其他平台比较布局几何和 glyph mapping |
| OpenGL/macOS 兼容性 | 上下文或交换失败 | 先做 macOS 专项探测；失败保留 CPU 诊断，但支持 runner 必须有可验证 GPU 环境；记录 OpenGL 弃用风险 |
| VirtualList 状态复用 | 输入焦点或表单值串到其他项目 | stable key、Element identity 和回收测试先于性能优化 |
| IME 时序差异 | preedit/commit 顺序不同 | 先锁定 `TextEditingValue` 状态机，再实现平台事件转译 |
| Skia 构建约束 | Windows 预编译 Skia 使用静态 CRT 且当前只提供 Release 包 | M1/M7 明确 Skia 为桌面可选依赖；CI 固定 Release/MT 配置，CPU-only 仍可独立构建 |
| 视觉命令扩展 | CPU/Skia/GPU 输出不一致 | 先扩展命令模型和序列化，再接入具体控件 |
| 平台服务失败 | 文件选择/通知阻塞或丢状态 | 所有服务返回结构化失败，状态更新只能在 UI 线程完成 |
| 范围膨胀 | 桌面任务被未确认的平台扩展拖慢 | 当前范围为三桌面；M9 已冻结，不因已有移动实验代码而扩展任务 |

## 8. 版本切分和停止条件

- **桌面自用版**：完成 M0–M8（已收口）。M5 的语义契约和 Recording bridge 已完成；原生桌面 accessibility provider 由 M13 追加。
- **后续桌面增强版**：M10–M13 负责动效、视觉方向、平台服务、发布补全和原生无障碍 provider；M14 负责真实平台验收、稳定性能门槛、生产生命周期与诊断，以及面向工具应用的 DataGrid/水平虚拟化。集合控件 List/Tree/TreeList、菜单类控件、Splitter 分栏与自定义标题栏已作为 M13 前的按需增强交付；拖放、窗格塌缩、完整 UBA、HarfBuzz 富文本和专用 GPU 后端按 M14 出口后的实际产品需求评估。业务数据与网络仍由应用层负责。
- **移动端**：暂不实现且不规划版本；M9 仅保留为冻结记录。

任何里程碑若无法满足出口条件，只能修复当前阶段或回退实现，不能通过修改文档把“接口存在”标记为“平台完成”。

## 9. 维护规则

- 每个里程碑完成后，在本文件 §10 补充完成日期、提交号、测试数量和未覆盖平台。
- 代码行为、支持矩阵、README 和 CI 发生变化时，必须在同一变更中更新路线图状态。
- 发现设计文档与实现不一致时，先以源码、构建目标和实际测试为事实来源，再决定是补实现还是降级文档承诺。
- 所有新功能必须有至少一个 headless 测试；涉及窗口、输入、GPU 或系统服务时，再增加对应平台 smoke。
- 能力状态必须使用 `support-matrix.md` 的四态（接口已存在/headless 已验证/
  真实平台已验证/可发布），禁止把“接口存在”标记为“平台完成”。

## 10. 里程碑完成记录（M0 冻结模板）

本节是按阶段追加的历史快照，不替代当前状态盘点。后续记录若与当前源码或 CI 不同，必须保留原始日期并在条目中注明“当时”；当前结论以本文件 §2 和 [`docs/support-matrix.md`](support-matrix.md) 的验证快照为准。

每个里程碑的完成记录必须包含：变更、测试、平台、已知限制、回滚点。
模板：

```text
### Mx 完成记录
- 完成日期：
- 提交号：
- 变更：（模块/接口/行为，指向源码符号）
- 测试：（单测/headless/像素/窗口 smoke/基准数量与命令）
- 平台：（三桌面覆盖与未覆盖平台；如运行了历史实验配置，单独记录）
- 已知限制：（与出口条件的差异）
- 回滚点：（回退到的提交号/行为）
```

### M0 完成记录（基线冻结与工程入口）

- 完成日期：2026-09-11
- 提交号：（本变更提交，见 Git 历史 `chore(m0)`）
- 变更：
  - 新增 `docs/build-commands.md` 统一命令表（CPU/Skia/GPU/mobile-core/
    headless/窗口 smoke/基准 canonical 命令）。
  - 新增 `docs/perf-baselines/v0.2-cpu-scene.json`（`backend=cpu`、
    `scenario=card-grid-6x8-1080p`、viewport 1920x1080、warmup 30、
    measured 300、Release）与 `docs/perf-baselines/README.md` 比较规则。
  - 扩展 `benchmarks/scene_bench.cpp` JSON 字段（`scenario`、`toolchain`、
    `build_type`、`commit`、`platform`），`benchmarks/CMakeLists.txt`
    把 `CMAKE_BUILD_TYPE` 编译进报告；`commit`/`platform` 允许
    `LUMEN_BENCH_COMMIT`/`GITHUB_SHA` 与 `LUMEN_BENCH_PLATFORM`/`RUNNER_OS` 覆盖。
  - `docs/support-matrix.md` 增加四态定义、工具链/依赖基线（Windows
    `windows-2025`/MSVC、Linux `ubuntu-24.04`/GCC、macOS `macos-15`/
    AppleClang；SDL3/Catch2/stb/Skia/zlib pin 版本）与已知限制到 M1–M9 的映射。
- 测试：
  - `lumen-scene-bench --frames 300 --warmup 30 --json` 两次运行
    `frame_hash=d28e364efe1b4aca` 一致（本地 Linux/GCC 15.2.0/Release；
    干净目录 `/tmp/lumen-m0-check` 复现同一 hash）。
  - 干净构建目录可重复（`build-bench` out-of-source，见 `build-commands.md`）。
  - 现有 `ctest` 基线不受影响（本地 Linux CPU Debug `303/303` 通过；
    全二进制直跑的 3 个 SDL 视频设备失败为预期的无会话 quirk，`ctest`
    隔离运行通过；bench 扩展仅增 JSON 字段，不改布局/渲染路径）。
- 平台：本地 Linux 已验证基线生成与可重复性；Windows/macOS 工具链以 CI
  配置为事实来源，未在本地重复生成（未覆盖平台）。
- 已知限制：归档基线为本地 Linux Release 单点；Skia/GPU/新增场景基线待 M7
  按同后端同场景规则首次归档；CI artifact 的 commit/platform 元数据依赖
  bench 环境变量透传（GITHUB_SHA/RUNNER_OS）。
- 回滚点：`2735261 docs(roadmap): 完善自用跨端里程碑路线图`（M0 前）。

### M1 完成记录（桌面真实文本与编辑闭环）

- 完成日期：2026-09-12
- 提交号：（本变更提交，见 Git 历史 `feat(text)`）
- 变更：
  - `lumen-text` 新增 `SkiaFontManager`（`include/lumen/text/skia_font_manager.h`，
    pimpl 封装字体族/weight/style/fallback/glyph metrics/shaping；公共接口
    无 Skia 类型；Windows GDI / Linux FontConfig / macOS CoreText 端口），
    `FontManager` 基类新增 `resolveWithStatus`/`horizontalMetrics`/
    `shapeCluster`/`diagnostic` 默认实现；CPU-only 构建工厂返回 nullptr
    并给出诊断（占位启动不阻塞）。
  - `TextLayoutResult`/`TextLine` 保存 shaped run、glyph id、advance、
    x 偏移、cluster 映射、真实 baseline、行高与命中测试信息；
    `TextLayoutCache` 键增加字体后端；缺字 cluster 回退占位 advance 并
    标记 `usedPlaceholderFallback`，不改变编辑索引。
  - `RenderCommand`（v3）的 `TextRun` 携带 `baselinePx` 与可序列化
    `TextGlyphRun`（family/placeholder/glyphs）；`LayoutEngine`/
    `paintScene`/`recordScene` 增加显式字体源入口；CPU 按占位 xOffsetPx
    定位，Skia 光栅/GPU 按真实 glyph id 绘制（`src/render/skia_text.cpp`
    共享），后端无对应数据时回退旧逐码点路径。
  - 混合 LTR/RTL：`lumen/text/bidi.h`（UAX#9 确定性子集：强/弱/中性
    类近似 + L2 逐层逆序），视觉序/命中测试/光标映射共用
    `graphemeX`，grapheme 边界仍为唯一编辑索引。
  - 编辑撤销：`lumen/text/editing_history.h`（值快照栈、连续单字
    输入/删除合并、事务边界、容量 100、分支丢弃 redo）；
    `InteractionController` 接入 Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y，preedit
    期间拒绝，IME commit 为单个事务，纯选区移动只打断合并；
    命中测试/光标定位经 `setTextFonts` 与布局共享同一份 FontManager
    （默认占位；Skia 应用传入 `SkiaFontManager`，示例的默认接线随 M2
    应用壳落地，本里程碑以测试证明闭环）。
- 测试：
  - 新增单测/headless：bidi 类别/runs/视觉序（含纯 RTL、LTR 段嵌 R、
    RTL 段嵌 L）、混合方向布局保持 grapheme 索引与命中测试、
    shaped run 结构/ellipsis cluster、`EditingHistory` 合并/事务/
    分支/容量、IME 提交单次撤销、只读拒绝、Ctrl+Z/Shift+Z/Y、
    工厂诊断（CPU-only 与 Skia 构建同一用例）、命令携带 shaped 数据
    与 v3 序列化 roundtrip、Skia 真实字体布局+绘制确定性（Skia 构建）。
  - 本地 Linux（M1 文件集；工作树另含 10 个未提交的 M2 用例，
    全量亦通过）：CPU Debug `316/316`，Skia Release `322/322`，
    GPU Release `329/329`，mobile-core Debug `304/304`；
    counter/settings headless 集成测试随 CPU 套件通过；Skia 真实字体
    确定性（同布局双帧同 hash）与 CPU 占位像素路径（M0 基线 hash 不变）
    均已验证。窗口 smoke 与真实输入法验收随 M2 示例接线后补测。
  - 基准：M0 规格命令（`--frames 300 --warmup 30`）在 HEAD 与本变更
    均为 `frame_hash=d28e364efe1b4aca`（与归档基线一致），layout/paint
    p50 与 HEAD 相当。
- 平台：本地 Linux 已验证 CPU/Skia/GPU/mobile-core；Windows/macOS
  以 CI 为事实来源（windows.yml 三 job、macos.yml 两 job 均含本变更
  的 headless 覆盖），真实输入法 smoke 未在本地执行（CI/人工验收项）。
- 已知限制：
  - 双向算法为 UAX#9 子集（无显式嵌入控制、镜像括号、数字定形）；
    Skia shaping 为逐 grapheme cluster，无 HarfBuzz 合字（阿拉伯连写
    基本形可用，合字后续增强）。
  - 性能修复（M4 review 追记）：`TextLayout::layout` 的诊断字符串与
    按码点字体回退原直接打 fontconfig——Skia 路径每次布局
    ~52ms/次。`FontManager::familyCount()`（Skia 缓存计数）+
    `SkiaFontManager` 按字符回退 typeface 缓存（含负缓存）后降至
    ~39µs/次（1333×）；charFaceCache 无上限（工具应用码点量级内存
    可忽略，M7 门槛再评估缓存策略）。
  - CPU 后端消费 Skia 度量数据时（GPU→CPU 回退）文本绘制退回占位
    度量旧路径；caret/选区几何仍出自布局。（2026-09-19 追记：GPU
    运行时失效的回退目标已改为 Skia 软件光栅，该漂移不再被应用触
    达——见 §10「既有能力优化」P3 记录；无 Skia 层的纯 CPU 构建本就
    走 SystemFontManager 连续路径。）
  - 真实 IME（IBus/Fcitx/TSF）下的 undo/redo 与候选框行为待三桌面
    窗口 smoke 人工验收；headless IME 状态机已覆盖。
- 回滚点：`d4dbb4a fix(build): 修复基准配置识别与构建命令`（M1 前）。

### M2 完成记录（应用框架层与 C++ DSL）

- 完成日期：2026-09-13
- 提交号：（本变更提交，见 Git 历史 `feat(app)`）
- 变更：
  - 新增 `lumen-app` 目标：`include/lumen/app/app_shell.h`+
    `src/app/{app_shell,run_app}.cpp`。`app::AppShell` 拥有 StateStore/
    HandlerRegistry/FocusManager/InteractionController、Element reconcile、
    订阅同步、布局 + damage、绘制缓存与脏矩形提交、caret 闪烁（可关）、
    IME 候选框查询与热重载换根；`app::runApp` 驱动 UI 线程主循环
   （ApplicationHost 事件泵 → 分发 → FrameScheduler 决策 → renderFrame
    → 呈现 → 空闲等待），公共头无 SDL/Skia 类型。
  - 应用配置钩子（ShellConfig）：build/onKey（Escape 统一规则）/
    onWheel（滚动 sink）/onCloseRequested（关闭策略）/onRebuilt
    （modal 焦点规则）/caretBlink；装配注入（RunOptions）：rendererFactory
    （含窗口重建）/onRendererFailure（GPU→CPU 回退，可重建窗口）/
    fontFactory/poll（热重载）/maxFrames/diagnostics。
  - `ApplicationHost` 新增 `waitForEvents(timeoutMs)`（默认 no-op，SDL 实
    现为 SDL_WaitEventTimeout）；`WindowDesc` 新增 softwarePresentation
    （GPU 回退窗口的原生软件呈现）。`lumen-core` 显式声明对 `lumen-text`
    的真实依赖（interaction 公共头/实现引用；静态库循环由链接线重复解析），
    修复 lumen-app 介入后 CMake 库排序丢失 core 尾置的问题；GPU 构建把
    OpenGL 移入 Skia 归档组并在消费者尾置 GL 依赖。
  - counter/settings 迁移：两示例的帧管线/主循环收敛到应用壳
    （settings main 253→129 行，counter_app 553→163 行；示例只保留
    build/状态/handler 与装配）；counter 保留 GPU 探测/初始化失败/
    运行时失效三条回退路径（经工厂与回退钩子，软件窗口重建）。
  - C++ DSL builder 补齐：`stack`/`checkbox`/`switch_widget`/
    `scroll_view`/`list_view`/`focus_scope`（`switch` 为关键字）；属性经
    core 层 with* 修饰器叠加，与 `.lumen` 冻结属性同名同义。
  - review 修复（本轮）：AppShell 首帧延迟落地（构造期不再求值 build，
    消除 SettingsApp 未构造完成即读成员的 UB）；settings 关闭请求去重
    （一次关闭只消费一级 modal/路由，不重入按键管线）；runApp 接入宿主
    剪贴板（Ctrl+C/V 可用，不可用保持未注入）；DSL parity 对照修正
    （Stack 包裹/flex/空文本）与 `stack()` key 参数；Skia 光栅字体管理
    器按 renderer 缓存；dialog 身份常量化；测试死码清理。
  - 应用层补齐多窗口编排：新增 `AppWindow` 绑定入口，分别装配窗口指标、
    renderer surface、IME、无障碍桥、光标和 FrameScheduler；事件按
    `HostEvent.window` 路由，生命周期/主题事件按规则广播，Surface
    detach/reattach 会暂停并恢复提交。原有单窗口 `runApp` 保持兼容，宿主
    窗口生命周期仍由调用方拥有。
- 测试：
  - 新增 `tests/app_shell_tests.cpp`（8 用例）：Fake host 驱动 runApp 的
    事件顺序/状态流转、指针+文本进入焦点字段、关闭请求消费/退出、
    IME 会话启停与候选框锚点同步、dirty 合并与绘制缓存命中、视口/
    DPI 变化重建、renderer 替换与缓存失效、渲染器失效钩子回退。
  - 新增 DSL 全节点 golden 对照（冻结节点集 + 常用属性逐字段相等）与
    FocusScope/修饰器覆盖用例。
  - 新增 Fake host 双窗口路由回归：窗口 1 的点击只改变窗口 1 的
    AppShell，窗口 2 保持独立状态；同时验证 runApp 返回后宿主生命周期
    仍由调用方管理。
  - 迁移验收：counter/settings 集成测试全部不变通过，headless 帧哈希
    与迁移前一致（counter `1a32cd5be756e9bf`/`df7487f8a9fb0eb8`/
    `55fb5a6e22f5dfaf`/`96166bdba1c35ff5`；settings `528e7440ed00451b`）。
  - 本地 Linux：CPU Debug 326/326、Skia Release 332/332、GPU Release
    339/339（含三条 GPU 失败注入冒烟）、mobile-core Debug 314/314；
    窗口 smoke（CPU/Skia/GPU llvmpipe）与 M0 基准
    `frame_hash=d28e364efe1b4aca` 保持。
- 平台：本地 Linux 全部验证；Windows/macOS 以 CI 为事实来源（事件泵/
    回退路径的跨平台行为由 ApplicationHost 契约与 Fake host 测试锁定）。
- 已知限制：
  - `runApp` 已按 `HostEvent.window` 路由多个 `AppShell`；空 window 的全局生命周期/主题事件广播到所有活动窗口。
    三桌面真实多窗口交互仍需单独窗口 smoke，Fake host 已覆盖事件隔离。
  - 诊断输出格式沿用 counter 契约（`backend=/frames=/gpu failed`），
    settings 的 host 能力诊断行被应用壳诊断取代。
  - rendererFactory 内窗口重建失败时 runApp 降级为无呈现循环（软件窗口
    创建失败属极端场景，无结构化致命错误通道）。
- 回滚点：`06191a7 feat(text): 桌面真实文本与编辑闭环`（M1，
  M2 文件集）。

### M3 完成记录（约束布局、Grid、VirtualList 与 Image）

- 完成日期：2026-09-14
- 提交号：（本变更提交，见 Git 历史 `feat(layout)`）
- 变更：
  - 新增 `WidgetType::{Grid, Image, VirtualList}` 与配套字段/构建器
    （`makeGrid`/`makeImage`/`makeVirtualList`；`isScrollableWidget` 纳入
    VirtualList）。RenderNode 新增 `imageId`/`imageSource`。
  - Grid 布局（`src/layout/layout.cpp::layoutGrid`）：固定列数或最小
    列宽自适应（列数 = floor((可用宽+列间距)/(最小列宽+列间距))，≥1），
    单元宽紧约束均分，行高 = 行内最大外部高度，行列间距独立；窗口
    变化重排由约束传播自然发生。
  - `core::VirtualListSource` 接口 + `VirtualListController`
    （`include/lumen/core/virtual_list.h`）：itemCount/estimatedExtent/
    extentOf/scrollOffset/totalExtent/offsetOfIndex/visibleRange/buildItem/
    noteExtent；实测 extent 缓存（布局期回填，视口上方修正平移 offset
    保锚点，不跳顶）；可见区含前后 cacheExtent 像素缓存；
    scrollToIndex（最小移动语义）；滚动输入复用 ScrollController
   （滚轮/键盘/触摸拖动/语义）。
  - VirtualList 布局（`layoutVirtualList`）：children 为空——布局期经
    source 物化可见区，子项绝对定位在内容坐标（offsetOfIndex），
    实测修正后同帧补齐可见区（每项每次布局最多构建、测量一次）；stable key
   （item key）作为 identity，key 变化 = 项目替换。
  - Image Widget：imageId（0 = 未就绪固定占位：表面+边框+中心叉，
    语义名称保留）/imageSource；painter 接入既有 DrawImage 命令路径
   （CPU/Skia/GPU 同源）。
  - settings 示例：Grid 页（18 tile、最小列宽 160 自适应）与千项
    VirtualList 页（偶数项更高验证 extent 修正；滚轮/键盘 Home/End
    汇入同一 sink）。
  - 基准：`lumen-scene-bench --scenario virtual-list[-<items>]`
   （默认 card-grid 场景与 M0 基线不变）。
- 测试：
  - 新增 `tests/grid_virtual_tests.cpp`（12 用例）：Grid 在 320/768/
    1080 与连续 resize 几何稳定、最小列宽自适应（窄窗口 320 单列仍
    可用）、行高聚合/固定尺寸；VirtualList 只物化可见窗口（千项首屏
    ~25 项）、快速拖动定位底部窗口、实测 extent 单帧位置修正、锚点
    平移与 itemCount 收缩不跳顶、visibleRange/scrollToIndex 契约、
    焦点 identity 回收往返稳定、局部 damage 与全帧逐像素一致
   （AppShell 真实帧管线）、Image 占位/位图命令、语义角色。
  - settings headless 冒烟扩展：grid/library 页导航、千项首屏物化 17
    项、滚动后 22 项、scrollExtent 43427。
  - 本地 Linux：CPU Debug 338/338、Skia Release 344/344、GPU Release
    351/351、mobile-core Debug 326/326；窗口 smoke 通过。
  - 基准（本地 Linux/GCC 15.2/Release/1080p/120 帧）：
    virtual-list-1000：nodes 90、cmds 359/帧、partial 120/120、layout
    p50/p95 1371/1977us、paint p50/p95 22137/25148us；virtual-list-10000
    与 1000 节点/命令数完全一致（90/359，严格 O(visible)）。card-grid
    基线 hash `d28e364efe1b4aca` 保持。
- 平台：本地 Linux 全部验证；Windows/macOS 以 CI 为事实来源。
- 已知限制：
  - Grid 为纵向网格（无横向滚动/跨行列合并）；单元格紧宽度填充
   （子项不支持列内对齐覆盖，后续版本随 M6 视觉对齐扩展）。
  - VirtualList 仅纵向；estimated extent 首帧后由实测修正（两帧内
    收敛，布局期同帧重算覆盖绝大多数情况）。
  - Image 需应用侧 ResourceManager 驱动加载并回写 imageId（框架不
    管理异步资源生命周期；上传命令沿阶段7D 契约）。
  - 惯性滚动不纳入 M3（默认关闭，桌面后续增强按需评估）。
- 回滚点：M2 合入后的提交（见 M2 完成记录）。

### M4 完成记录（三桌面平台服务与窗口能力闭环）

- 完成日期：2026-09-15
- 提交号：（本变更提交，见 Git 历史 `feat(platform)`）
- 变更：
  - `ApplicationHost` 平台服务契约（`include/lumen/platform/application_host.h`）：
    `ServiceResult`（结构化失败：Unavailable/Cancelled/Failed + 可读消息）、
    `FileDialogRequest/Result`、`NotificationRequest`、`SystemCursor`
   （11 形状）、`WindowIcon`（RGBA8）；新虚方法 `openUrl`/
    `requestFileDialog`/`postNotification`/`setCursor`/`setWindowIcon`
   （默认实现全部结构化 Unavailable）。
  - `core::HostEvent` 新增 `FileDialogCompleted` 类型与 `filePaths` 字段
   （文件选择异步完成事件；取消 = 空路径且无错误）。
  - `PlatformCapabilities` 统一报告：`prefersDarkMode`/`accentColor`/
    `fontScale` + 服务可用性（fileDialogs/notifications/openUrl/
    cursorShape/windowIcon）。
  - `FakeApplicationHost`：服务确定性记录（openUrlCalls/
    notificationCalls/fileDialogCalls/cursorCalls/iconCalls）+ 失败注入
   （setXxxFailure）+ 预置对话框结果（queueFileDialogResult→事件）。
  - `Sdl3ApplicationHost`：SDL_OpenURL；SDL 异步文件对话框
   （回调“可能在另一线程”——PendingDialog 互斥同步；宿主销毁时未完成
    对话框转移进程级孤儿列表避免 userdata 悬空；无效窗口 id 回退挂靠
    首窗口）；pollEvent 转 FileDialogCompleted，不阻塞 UI 线程；光标（SDL_CreateSystemCursor 映射 + 窗口缓存，公共头无
    SDL 类型）；图标（SDL_SetWindowIcon）；通知在 SDL 3.2.10 无 API
    →能力 false + 结构化 Unavailable 降级。
  - `app::RunOptions.onEvent`：应用壳不消费的服务事件转发给应用。
  - settings 示例 Services 区：Open file.../Save file.../Notify/Open
    docs 按钮 + 选中路径/诊断展示（动作由 main 注入，能力先行查询；
    测试可换 fake）。
- 测试：
  - 新增 6 用例：ServiceResult 工厂、fake 文件对话框全语义（空队列
    Unavailable/请求期同步失败/多选完成事件/取消空路径无错误）、
    光标/图标/URL/通知记录与失败注入、能力报告、SDL host 无头降级
    冒烟（通知 Unavailable + 光标 + 非法图标）、runApp onEvent 转发、
    settings 服务区状态流（不可用诊断/fake 成功/失败路径后状态完整）。
  - SDL 对话框 dummy 驱动冒烟（请求安全完成或同步结构化失败）。
  - 本地 Linux：CPU Debug 355/355、Skia Release 360/360、GPU Release
    367/367、mobile-core Debug 337/337；窗口 smoke 通过。
- 平台：本地 Linux 全部验证（含 SDL dummy 驱动降级路径）；Windows/
  macOS 以 CI 为事实来源；真实文件对话框的三桌面交互验证属窗口
  smoke/人工验收（xvfb 无显示环境不能自动开真对话框）。
- 已知限制：
  - 通知在 SDL 3.2.10 无 API：能力关闭 + 结构化降级；真实通知待
    SDL 升级或平台原生后端（后续版本，不阻塞 M5+）。
  - 文件对话框过滤器为 SDL name/pattern 简化映射（"Files"/模式串）。
  - prefersDarkMode/accentColor 在 SDL 3.2 无系统主题查询：安全默认
   （false/固定色），后续随 SDL 能力或平台原生后端接入。
  - 光标为进程级（SDL_SetCursor）；窗口级后端待 SDL 支持。
- 回滚点：M3 合入后的提交（见 M3 完成记录）。

### M5 完成记录（语义契约与键盘可用性收口）

- 完成日期：2026-09-16
- 提交号：（本变更提交，见 Git 历史 `feat(a11y)`）
- 变更：
  - 语义契约固化：`kSemanticsInvalid` flag（TextField 校验失败与视觉/
    hit/键盘一致暴露）；`kSemanticsHidden` 实际生效——滚动视口
   （clipContent）裁剪栈传播，与任一视口不相交的子树标 Hidden（保留
    在树中）；Image 可访问名兜底（覆盖 → imageSource）；Grid 归组
    （Group，子序=阅读顺序）；VirtualList 物化项的缓存区（视口外）
    项标 Hidden。
  - `AccessibilityBridge::noteActionPerformed`（默认 no-op）+
    RecordingAccessibilityBridge::ActionRecord（nodeId/action/status）。
  - `app::AppShell` 语义桥驱动：`setAccessibilityBridge`（下一次绘制
    末尾全量推送）、每帧绘制末尾 `pushSemantics`（构建 → identity diff →
    updateTree → 焦点变化 setFocusedNode；仅注册时执行）；
    `performAccessibilityAction`（与键盘同路径分发 + 结果回执；滚动经
    控制器 wheelSink 的语义/键盘回退视口路径）。
  - `InteractionController::focusFirstFocusable`：路由 pop/页面切换
    后的焦点恢复（候选规则与 Tab 遍历一致；disabled 不建立焦点；无
    候选清焦点）。
  - settings：Escape 返回与 back 按钮、Dialog 关闭（closeDialog）统一
    置焦点恢复请求（onRebuilt 落地——新树首个可聚焦节点）。
- 测试：
  - 新增 7 用例：invalid flag 与视觉状态一致（invalid 仍可交互）、
    滚动视口外子树 Hidden、Image label 兜底/role、Grid 顺序、
    VirtualList 缓存项 Hidden + List role/scroll action、
    focusFirstFocusable 恢复 Tab 顺序、settings RecordingBridge 全契约
   （首帧全量 added、语义 Activate ≡ 键盘同 handler、表单错误 invalid
    进 diff 与树、dialog barrier role+Dismiss、语义 Dismiss 关闭 +
    modal 焦点恢复、Navigator back + Escape 焦点恢复 + bridge
    setFocusedNode 序列）。
  - 本地 Linux：CPU Debug 362/362、Skia Release 368/368、GPU Release
    375/375、mobile-core Debug 344/344；窗口 smoke 与 M0 基准
    `frame_hash=d28e364efe1b4aca` 保持。
- 平台：本地 Linux 全部验证；Windows/macOS 以 CI 为事实来源。
- 已知限制：
  - 该条为 M5 历史边界；原生 provider 由 M13 增强链实现，真实屏幕阅读器回环
    仍按平台验收记录推进。
  - 语义滚动 sink 恒返回 true（wheelSink 消费状态未回传；
    InteractionController::wheel 无返回值——后续版本补）。
  - pushSemantics 与绘制同步（cache-hit 帧不推送；焦点/树变化必然
    触发重绘，语义与像素一致）。
- 回滚点：`68f5f14 feat(platform): 完善平台服务与文件对话框`（M5 前）。

### M6 完成记录（自用级视觉系统 V3 与控件库）

- 完成日期：2026-09-17
- 提交号：（本变更提交，见 Git 历史 `feat(visual)`）
- 变更：
  - 图标系统：`core::IconId`（12 语义 ID，枚举自 style/tokens.h 迁移并
    经 using 复用保持 token 冻结）+ 归一化折线目录
   （`core::iconPolylines`，纯几何属 core）；`Widget.icon`/
    `RenderNode.icon+iconStrokeWidth`（IconTheme 布局期折算）；
    `DrawIcon` 命令（可序列化折线组）；CPU 软件线条光栅（方形笔刷）
    与 Skia SkPath stroke（圆帽）共用命令；Icon 节点 + Button 图标位。
  - 阴影/层级：`Widget.elevation`（值来自 ElevationTokens）→ 布局期折
    算 shadowColor/offset/blur 进 RenderNode（后端不依赖 Theme 对象）；
    `DrawShadow` 命令：Skia 光栅/GPU 用 blur mask filter，CPU 降级为
    token 色的偏移扁平面（命令跨后端一致，像素按能力）。
  - 滚动条：ScrollbarTokens.thickness 布局期折算；painter 在 clipContent
    视口按 scrollOffset/scrollExtent 实绘 thumb（前景半透明派生色）；
    `withScrollbar` 控制。
  - 控件库（六控件 + applyBinds 值绑定）：Slider（点击/拖动/键盘 ±5、
    填充可用宽、语义 value+SetValue action）、ProgressBar（展示，语义
    value）、Radio（与 Checkbox 同 toggle 路径、applyBinds checked、
    圆形指示）、Tooltip（气泡表面+文本，语义 label）、Dropdown（值行
    +ChevronDown 图标+展开选项子树，open 控制）、Tabs（标签行容器，
    selected 指示）。
  - ThemeScope：`WidgetType::ThemeScope` + `shared_ptr<void>` 携带
    Theme 拷贝（核心不接触样式类型）+ `style::ScopedThemeOverride`
   （thread_local，resolveStyle 子树覆盖；交互快照/可访问性优先级不
    变）；`style::makeThemeScopeData`。
  - `style::adaptPlatformTheme`：系统主题输入（dark/accent/fontScale）
    → Theme 派生（不返回平台控件对象；M4 PlatformCapabilities 输入）。
  - `FormController::compose`：组合校验器（按序首错返回）；邮箱/范围/
    格式等业务规则由应用组合提供。
  - RenderCommand 序列化 v4（DrawIcon/DrawShadow：折线组+线宽；损坏
    拒绝）；cull/bounds/drawCount 纳入新命令。
  - settings 新页：Widgets（Slider/ProgressBar/Radio/Dropdown/Tabs/
    Tooltip/Icon 预览，StateStore bind 驱动）与 Theme（深浅切换 +
    light ThemeScope 对比预览）；headless 冒烟覆盖。
- 测试：
  - 新增 `tests/visual_m6_tests.cpp`（11 用例）：图标目录归一化/
    DrawIcon 命令/CPU 光栅像素、Button 图标、序列化 v4 roundtrip+
    损坏拒绝、阴影命令（elevation 折算+命令一致）、滚动条 thumb、
    Slider 点击 75%/键盘 ±5、ProgressBar 语义、Radio 同路径切换+
    checked 语义、Dropdown 展开+值行 chevron+语义值、Tabs 行布局、
    Tooltip 语义。
  - 本地 Linux：CPU Debug 374/374、Skia Release 380/380、GPU Release
    387/387、mobile-core Debug 356/356；窗口 smoke、headless（widgets/
    theme 页导航）与 M0 基准 `frame_hash=d28e364efe1b4aca` 保持。
- 平台：本地 Linux 全部验证；Windows/macOS 以 CI 为事实来源。
- 已知限制：
  - 状态/Dialog/Navigator 完整转场动画驱动未实现（MotionTokens 已接
    入 caret 闪烁与 FrameScheduler reduceAnimation；transitionAlpha
    通道就绪——Icon/Button/滚动条已乘，表面级插值待后续增强版）。
  - Dropdown 为树内展开（无浮动层/模态菜单键盘导航）；Tooltip 为常驻
    节点（显隐由应用 rebuild 控制，无 hover 延迟驱动）。
  - CPU 阴影为扁平面近似（无模糊）；图标光栅为方形笔刷（无圆帽/
    抗锯齿近似）。（2026-09-19 追记：图标 AA/圆帽与 CPU 软阴影
    blur 已随后续按需优化收口，见 §10「既有能力优化」完成记录。）
  - Radio 组互斥由应用写值管理（框架不内置组语义）。
- 回滚点：M5 合入后的提交（见 M5 完成记录）。

### M7 完成记录（三桌面 GPU 生产门槛与性能）

- 完成日期：2026-09-18
- 提交号：（本变更提交，见 Git 历史 `perf(render)`）
- 变更：
  - 基准扩展：`lumen-scene-bench --scenario text-heavy|grid|
    semantics-diff|resource-upload`（M7 新场景，名称规范 <name>-1080p）
    与 `--backend cpu|skia`（离屏光栅 SkiaRenderer；GPU wait 语义在
    窗口路径实测，bench 不适用）。resource-upload 每帧 8 张 120x80
    位图注册/注销循环（UploadImage/UnloadImage 命令路径）；
    semantics-diff 每帧语义树构建计入 paint 相。
  - **性能修复（review 发现的三处严重回归）**：
    1. SkiaRenderer/GPU 逐码点回退路径每次 drawText 重建 FontConfig
       manager + 每码点无缓存查询——1080p 文本密集 **3.2s/帧 → 14ms**
      （字形解析缓存于 Impl；GPU 同修）。
    2. `TextLayoutCache` 存在但从未接线（布局与绘制直调
       TextLayout::layout，静态文本每帧全量 reshape）——layout 相
       **925 → 573µs**（measureTextContent/painter.layoutText 经
       thread_local 缓存）。
    3. Widget 体积 840B（M6 累积）→ 784B：value 字段删除（控件值复用
       text，bind 经 applyBinds 写入）、M6 小字段集中打包消除 padding、
       themeOverride 改裸指针（shared_ptr 保活责任在应用）。
    4. SkiaFontManager 字形缓存携带族名（getFamilyName 每 cluster
       字符串构造消除）；TextLayout cluster glyphs move 化 + 族名
       去重池。
  - partialSubmit 实测评估：preserve 路径（snapshot 上帧 + blit 铺底）
    在 Skia/Ganesh 光栅模型下为全帧 Clear 的 **1.16×**（光栅化便宜，
    snapshot+blit 固定成本超过局部重绘节省）——`capabilities.
    partialSubmit` 维持 false 并如实标注全帧提交（正确性优先回退
    保留；实测命令与探针归档）。
  - macOS GPU：Skia macOS 预编译（m124 universal）FetchContent 分支、
    APPLE 链接（OpenGL/AppKit framework、包根布局）、macos.yml 新增
    skia-gpu job（软件 GL 一帧 + 回退验证）。**代码路径经 SDL_GL 抽象
    已覆盖（CGL 由 SDL 选择）；CI 首跑为事实来源。**
  - 稳定性：GPU 357 帧长跑（llvmpipe）无异常；AppShell resize 风暴
    （1000 次奇偶交替 setView + tick + renderFrame）哈希稳定；
    counter_gpu_smoke 三条故障注入路径既有覆盖。
  - M7 初始基线归档：`docs/perf-baselines/m7-{cpu,skia}-*.json`
   （6 份：4 CPU 新场景 + 2 Skia）；全部两跑同 hash。
- 测试：本地 Linux CPU 374/374、Skia 380/380、GPU 387/387、
  mobile-core 356/356（性能修复后全绿；M0 card-grid hash
  `d28e364efe1b4aca` 全程保持）。
- **性能门槛（出口条件）——6/6 全部达标**（Element move 管道后双跑取优，
  高负载机器上多轮验证）：
  - layout.p50 -4.1%、layout.p95 +8.8% ✓
  - paint.p50 +2.1%、paint.p95 -1.3% ✓
  - reconcile.p50 **-13.4%**、reconcile.p95 -4.7% ✓
  - Element reconcile 优化（回收既有缺口）：update 构造/更新全程
    children 移交（O(1) move）+ 去子化快照（widget() 只保留本节点
    字段——复用判断仅用 type+key；整棵最新树由调用方 build 产出
    持有，AppShell/bench 布局输入改为本地完整树）→ 拷贝复杂度
    O(n·depth) → O(0)（全 move）；bench 相位切分与 M0 同口径
   （reconcile = build 段 + update 段）。
- 已知限制：
  - 三桌面 GPU CI：Windows/Linux 既有 job 绿；macOS job 本变更新增，
    **首次 CI 运行为事实来源**（本地无法验证 macOS，失败则回退该 job
    并记录）。
- 回滚点：M6 合入后的提交（`4cd8181 feat(visual)`）。

### M8 完成记录（三桌面便携发布）

- 完成日期：2026-09-19
- 提交号：（本变更提交，见 Git 历史 `build(package)`）
- 变更：
  - `CMakeLists.txt`：`LUMEN_BUILD_PACKAGE` 选项 + install 规则（全部
    静态库/公共头/示例/README+设计文档）+ 便携依赖（SDL3 共享库入包
    + 平台运行时路径（Linux `$ORIGIN/../lib`、macOS
    `@loader_path/../lib`）——声明于 add_subdirectory 之前确保子目录
    目标捕获；Linux 双实体 soname 拷贝规避 ZIP 归档
    不保留 symlink；Windows DLL 随 bin/）。
  - `cmake/PackageConfig.cmake`：CPack 配置（Linux TGZ / Windows·
    macOS ZIP；版本 = `git describe --always --dirty --tags` 可追溯；
    保留调试符号便于诊断）。
  - CI：三平台 workflow 新增 package job（构建 → CPack → 解包 →
    headless counter/settings 冒烟 → artifact 上传 14 天）。
- 验证：
  - 本地 Linux 完整链：configure → build → cpack TGZ → 干净目录解包 →
    RUNPATH `$ORIGIN/../lib` 生效（readelf 断言）→ counter/settings
    headless 冒烟通过（hash 与开发环境一致）→ 包名携带
    `96ab0d7-dirty` git 版本。
  - 依赖缺失提示：动态链接器原生诊断（libSDL3.so.0 缺失时明确报错；
    入包后解包即用）；`--diagnostics` 开关随示例提供。
  - 回归：四构建 374/380/387/356 全绿；M0 基准 hash 保持。
- 平台：本地 Linux 全链验证；Windows/macOS 包以 CI package job 首跑
  为事实来源（失败则修复或回退对应 job）。
- 已知限制：
  - AppImage 与完整 .app bundle 结构未做（zip 内 bin/ 布局即可启动；
    后续按需增强）。
  - Skia/GPU 构建的包体积与许可归档未含（自用包 CPU-only 闭环；
    GPU 包按需在 CI 追加 `-DLUMEN_ENABLE_SKIA=ON` 变体）。
- 回滚点：M7 合入后的提交。

### M10 完成记录（动效与滚动体验）

- 完成日期：2026-09-14
- 提交号：af13461（增强链规划）/ 97c2678（转场驱动与整节点透明度）/
  4dc85b9（惯性滚动与拖动接线）
- 变更：
  - 动画帧调度：`AppShell::tick` 从 caret 专用泛化为通用动画驱动器
    （caret/转场/状态过渡/`ShellConfig.onAnimate` 共用
    `animationsActive`）；`runApp` 以此驱动 `FrameScheduler`
    （`FrameReason::Animation` deadline 循环，替换原
    `wantsTextInput` 单一来源）。
  - 整节点透明度：painter 的 `transitionAlpha` 升级为整节点通道——
    子树乘法继承（`parentAlpha * node.transitionAlpha`），alpha<1 时
    经 `core::scaleStyleColors`（新增于 core/style.h：公共段 + 各组件
    专有色）统一缩放；阴影同步缩放；全透明子树零命令；CPU/Skia/GPU
    消费同一份命令数据。已知限制：`DrawImage` 无颜色通道，位图不参与。
  - 转场驱动：`TransitionSpec`/`beginTransition` +
    `beginDialogTransition`/`beginRouteTransition`（时长取
    MotionTokens；进场 EaseOut、退场 EaseIn）；key 延迟解析 identity
    （begin 可早于含该子树的首次重建）；终值已应用到绘制帧且完成回调
    已消费后，下一 tick 才退休。活动转场持续参与动画调度，终值等待
    VSync/窗口恢复期间不能因多次 tick 提前消失。
    重建 damage 比较当前渲染树的 alpha/状态色与新树，并累计绘制前
    的多次重建，避免终帧恰逢交互重建时漏刷整页或控件颜色。
  - 状态色过渡：`ShellConfig.motionTransitions`（默认关）+
    `blendFrom/blendTo` 双快照——首次应用时定格目标样式（修复插值
    `to` 端被首拍覆盖的问题）；逐帧 `core::lerpStyleColors` 插值，
    非颜色字段取终态。
  - 惯性滚动：`ScrollController` 新增 `noteDragSample`/`endDrag`/
    `stepFling`/`isFlinging`/`stopFling`（指数衰减 tau=160ms，起滑
    150px/s、停止 50px/s，边界即停，滚轮/键盘/再次拖动立即接管；时间
    截全注入，确定性物理）；`InteractionController` 新增
    `ScrollDragSink`（Begin/Update/End/Cancel；起点命中滚动视口且非
    文本选区路径才路由，identity 跨重建重定位）；pointerMove/Up 携带
    时间戳；wheel/scrollKey 返回 sink 消费状态（收口 M5 已知限制
    “语义滚动 sink 恒返回 true”）。
  - settings/gallery 接入 `onScrollDrag` + `onAnimate` 惯性推进
    （滚轮/键盘/选区路径行为不变）。
- review 修复（同日追记）：
  - **拖动滚动路由不再劫持视口内可拖动 Slider**（回归：settings
    Widgets 页与 gallery 主列表的 Slider 都在 ListView 内，路由会在
    slop 越过后把滑块拖动转成滚动、pointerUp 提前返回，M6 的
    `setSliderByPosition` 拖动释放设值失效）——起点命中 enabled 且带
    bind 的 Slider 时拖动属于滑块；新增回归用例
    `slider_drag_inside_scroll_view_still_sets_value` 并经变异测试验证
    （强制路由开启时用例两条断言均失败）。
  - SettingsApp/GalleryApp 的 wheel 转发显式丢弃消费状态返回值
    （`[[nodiscard]]`，消除 MSVC C4834）。
  - `advanceTransitions` 退休以实际绘制为界，不能只等一个 tick；进场
    终值应用后才可清除，退场 onComplete 移除子树时则在重建后清除
    缺失的 identity。详见视觉实现记录的 Buttons 转场回归修复。
- 测试：新增 `tests/motion_scroll_tests.cpp` 11 用例——整节点 alpha
  命令/子树乘法继承/全透明零命令、Dialog 退场淡出+完成回调+退休、进场
  延迟 identity、Route 时长区分、reduceAnimation 零时长首拍即终态、
  状态色插值（起点/中点/终态逐通道夹逼）、静态场景哈希稳定零动画帧、
  fling 物理（单调减速/确定性重放/慢速不起/滚轮接管）、shell 级拖动
  滚动+惯性推进、TextField 拖动保持选区路径、wheel 消费回执、视口内
  Slider 拖动释放设值（回归）。本地 Windows CPU Debug `395/395`（含
  既有 counter/settings headless 哈希全部不变通过）。
- 平台：本地 Windows 全部验证；Linux/macOS 与 Skia/GPU 构建以 CI 为
  事实来源（转场走共享命令路径，后端无关性由命令同源保证）。
- 已知限制：
  - 转场为淡入淡出（无位移/缩放几何过渡——布局几何不变式）；退出
    动画期间子树仍可交互且语义可见（alpha 只改绘制数据，plan §4 M10
    接口约束）。
  - 状态色过渡与 Dialog/Navigator 转场为框架能力 + 专用测试验证；
    settings/gallery 既有哈希路径保持即时切换（无 tick 推进的直驱
    测试是仓库的确定性验收模式，示例启用转场会破坏首帧断言）——随
    M11 视觉迭代按页评估启用。
  - fling 为矩形欧拉积分（确定性优先，非半隐式欧拉）；无水平滚动与
    嵌套视口惯性。
- 回滚点：`af13461 docs(roadmap): 规划 M10 起桌面增强链`（M10 前）。

### M11 完成记录（v0.4 视觉方向与控件体验）

- 完成日期：2026-09-14
- 提交号：d1889f7（四方向变体化）/ b227873（Tooltip）/ d5d8e3f（overlay）/
  393ecb6（Dropdown）；设计输入 `design/gallery.html`（v0.4 concept
  四方向评审稿）随本里程碑收编入库。
- 变更：
  - **四方向 Theme 变体化**：`style::ThemeDirection`（CoreDark 默认/
    InkLinen/AuroraSignal/UtilityContrast）经 palette 槽位法接入——
    `primitivePaletteFor(direction, darkMode)` 填 PrimitivePalette，
    dark/lightColorScheme 与组件 token 派生零改动；`applyHighContrast`
    的 blue300/700 硬引用改为方向色板（修非蓝方向 HC 错色）；方向级
    Metrics 覆盖（controlRadius/cardRadius/controlBorderWidth，Utility
    =2px 边框）；Theme 携带 direction/darkMode 元数据，gallery/settings
    的 pageBackground 色值反推启发式删除；gallery Theme 页四方向切换
    （fromSettings 保留全部 a11y 派生），ThemeScope 预览跟随方向。
  - **Tooltip hover 延迟驱动**：MotionTokens 新增 tooltipDelayMs{400}/
    tooltipFadeMs{120}（reduceAnimation 归零=立即显示）；
    `AppShell::registerTooltip` 状态机（Hidden→Armed→Visible→Fading，
    复用 M10 转场驱动）；Hidden/Armed 态 alpha 强制 0（覆盖重建后的
    新树，替代 M6 常驻显示）；Tooltip 节点对命中测试透明。
  - **框架级 overlay**：`AppShell::setOverlay/clearOverlay`——overlay
    独立布局、与主树各自拥有 identity 命名空间（主树 identity/焦点/
    语义 id 稳定）；绘制经 `RenderCommandList::extend` 后置叠加；命中/
    键盘/语义 action 换事件树（barrier 全窗模态）；语义经
    `appendSemanticsSubtree` 挂根语义节点；打开全量、期间子树 diff、
    替换走 diff；关闭清焦点与活动指针。
  - **Dropdown 浮动菜单**：makeDropdown 收起叶子化（值行走 Button
    解析获得 hover/焦点态；dropdownOpen 字段删除，选项不再内嵌展开）；
    `widgets::DropdownController`——锚定菜单（下方不足翻上、视口
    钳制、FocusScope 键域）、Up/Down/Enter/Esc 键盘导航（应用 onKey
    modal 优先接线）、onSelected 回调 + 值行焦点恢复；gallery/settings
    迁移。
- 测试：新增/迁移——style 方向变体 5 用例（四方向 token 互异/CoreDark
  与 v0.3 等值/Metrics 覆盖/HC 方向 accent/元数据）；gallery 方向派生
  保持 1 用例；tooltip 2 用例（延迟显隐全时序 + 重建保持隐藏 +
  reduceAnimation）；overlay 2 用例（命中优先/barrier 模态/语义 action/
  主树 identity 稳定 + 开关全量/期间局部）；dropdown 2 用例（收起叶子
  几何/命令 + gallery 集成：点击选择/键盘 Down+Enter/Esc 不改值）。
  本地 Windows CPU Debug `405/405`；gallery headless 冒烟输出方向切换
  与下拉浮动菜单演示。
- 平台：本地 Windows 全部验证；Linux/macOS 与 Skia/GPU 构建以 CI 为
  事实来源（overlay/tooltip 走共享命令与转场路径，后端无关性由命令
  同源保证）。
- 已知限制：
  - Aurora 为扁平近似：rgba 表面取实底；玻璃/渐变/光晕/blur 不做；
    InkLinen 的 serif display 排版不做（跨平台字体确定性优先）。
  - overlay 的 IME 候选框查询与惯性滚动仍走主树（菜单场景无文本
    字段）；Dropdown expanded flag 不进语义契约（M5 冻结）。
  - 状态色过渡与 Dialog/Navigator 转场仍为框架能力 + 测试验证，
    示例哈希路径保持即时切换（M10 限制延续，随应用侧确定性测试
    演进启用）。
- 回滚点：`7978adb docs(roadmap): M10 完成记录与状态收口`（M11 前）。

### M12 完成记录（平台服务与发布补全）

- 完成日期：2026-09-15
- 提交号：c0d232a（系统主题与适配）/ d943eac（原生通知与强调色）/
  971c1cb（发布形态与包变体）
- 变更：
  - **系统主题查询与事件**：capabilities.prefersDarkMode 经
    `SDL_GetSystemTheme` 填充（修正 sdl3_host 过时的"SDL 3.2 无查询"
    注释——3.2.0 起可用，零原生代码）；新 `HostEventType::
    SystemThemeChanged`（SDL 事件翻译 + 能力位刷新 + fake host
    pushSystemThemeChanged 同语义注入），runApp 经 onEvent 转发；
    窗口级光标近似——FOCUS_GAINED 重放该窗口缓存形状（进程级原语
    实现窗口级语义）。
  - **adaptPlatformTheme 重做**：签名携带完整 AccessibilitySettings
    （方向/高对比/fontScale/reduceAnimation 全保留，修旧实现丢弃
    base 派生的缺陷）；accent 覆盖经 button/checkbox/switch token
    重建（token 链派生）。settings/gallery Theme 页"跟随系统"开关
    （默认关，保 headless 确定性）+ main onEvent 消费。
  - **三平台原生服务 seam**（src/platform/native_services.*，公共
    契约无平台类型，按平台条件编译）：Windows（Shell_NotifyIcon 气泡
    + DwmGetColorizationColor，链接 dwmapi）/ Linux（libdbus 会话
    总线：Notifications.Notify + appearance accent，缺 dbus 编译空
    seam）/ macOS（AppKit NSColor.controlAccentColor +
    NSUserNotification，OBJC++）/ 其余平台空 seam。
    postNotification/能力位/accentColor 接线；真发送不进 ctest
    （避免测试弹真实系统通知）。
  - **发布形态**：install 补 Linux 桌面集成（desktop + hicolor 图标，
    perl+zlib 生成简易占位资产）；linux package job 追加 linuxdeploy
    AppImage；macos package job 追加 Lumen.app Contents 骨架
    （Info.plist + bundle 内冒烟）；新 package-skia（三平台）与
    package-skia-gpu（Linux llvmpipe 真验 --renderer gpu）CI job。
- 测试：adaptPlatformTheme 派生保留（方向/密度/HC/fontScale +
  accent token 链 + 无 accent 走方向派生）；fake SystemThemeChanged
  能力刷新+事件；gallery 跟随系统主题开关/偏好重派生/关闭后不受
  影响；native seam 能力位与结构化断言（dummy 驱动）；M4 通知降级
  用例更新为平台条件断言。本地 Windows 真验：DwmGetColorizationColor
  取到本机强调色、通知真实送达（ok）；本地包链全通（configure→
  Release→CPack→解包冒烟哈希与基线一致）。本地 Windows CPU Debug
  `410/410`。
- 平台：本地 Windows 全部真验；Linux/macOS 原生代码与 AppImage/
  .app/包变体以 CI 首跑为事实来源（失败则修复或回退对应 seam）。
- 已知限制：
  - Windows 通知为 Shell_NotifyIcon 气泡（Win10+ 显示为 Toast）；
    托盘图标常驻进程生命期。macOS NSUserNotification 已弃用（未签名
    二进制可能被通知中心拒绝→结构化失败）；正式 Toast 需签名证书后
    迁 UserNotifications 框架。
  - accentColor 近似：Windows 取 DWM 着色、Linux 读 appearance
    accent（规范较新，桌面支持不一）——查询失败保持安全默认。
  - 窗口级光标为获焦重放近似（SDL 无 per-window API；Wayland 原生
    per-surface 光标留后续）。
  - 图标为简易占位资产；Windows/macOS GPU 包与 CPack Bundle 生成器
    留后续；AppImage 仅 CPU 主发布件（Skia 变体 TGZ/ZIP）。
- 回滚点：`6e4d2f0 fix(app): M11 review 修复`（M12 前）。
- review 修复（同日追记）：
  - **Tooltip 等待期空转帧消除**：FrameScheduler 新增一次性
    `setAnimationDeadline`（空闲等待到时刻、到达按 Animation 提交一帧、
    一次性消费）；AppShell 暴露 `animationWakeMs`（Armed tooltip 最早
    到期），runApp 每轮注入；Armed 等待期不再请求连续动画帧（原
    ~400ms 内 60fps 空转）。
  - **语义模态边界统一**：performAccessibilityAction 的 scrollSink 与
    action 派发一致走事件树——overlay 活跃期主树节点激活/滚动均
    NotHandled（集成断言）。
  - 菜单高度估算计入排版行高（fontScale 极端时文本行高超过控件最小
    高度）；DropdownController::open 以渲染值行文本同步 value（消除
    控制器/StateStore 双源漂移）；pointerUp 分发处固化"handler 返回
    后不得解引用 chain/root"约定注释；gallery 注册 noop handler。
  本地 Windows CPU Debug `406/406`（含调度器 deadline 新用例）。

### 集合控件完成记录（List/Tree/TreeList，2026-09-17）

- 完成日期：2026-09-17
- 提交号：1b6ce5c（List/Tree/TreeList 与共享选择模型）/ d13a2ea（源视口
  滚动框架接管）/ 4c96a8d（语义层共享实现单源化）/ 286746d（SDL 滚轮
  方向换算）；设计输入 `docs/lumen-collection-controls-design.md` 与
  `design/collection-controls.html`
- 变更：
  - 新增 `WidgetType::{List, Tree, TreeList}` 与控制器
    （`include/lumen/widgets/list.h`/`tree.h`）：List 一维行序列、Tree
    层级模型扁平化为可见行序列（展开状态集/缩进/chevron toggle）、
    TreeList 树 + 列系统（列定义/列宽固定+权重/粘性表头/列头排序点击
    钩子）；共享 VirtualList 虚拟化引擎，O(visible) 物化。
  - 共享选择模型 `include/lumen/widgets/selection.h`：None/Single/
    Multiple/Extended 四模式，current ≠ selected 两概念分离，
    Ctrl 切换/Shift 区间；键盘导航（Up/Down/Home/End/PageUp/PageDown/
    Ctrl+A）、ScrollAlignment 四向滚动对齐、行单击/双击/激活 sink
    （key 前缀分发，`addRowClickSink` 家族）。
  - 源视口滚动框架接管（d13a2ea）：RenderNode 携带 `virtualSource`，
    `VirtualListSource::scrollController()` 暴露源控制器——滚轮/键盘
    滚动/拖动（含拇指跟手换算）/惯性（AppShell::tick 逐拍推进）由框架
    直接驱动，应用零接线（此前需在 onWheel/onScrollDrag 按 key 逐个
    路由，gallery 三段硬编码路由移除）。
  - List/Tree 键盘导航/滚动对齐/sink/集合行 Row 壳单源化（4c96a8d，
    私有 `src/widgets/collection_common.h`；ScrollAlignment 单一定义
    移入公共 `include/lumen/widgets/collection.h`），零行为变化。
  - SDL 滚轮方向换算（286746d）：框架约定 scrollDelta.y>0 = 内容向下，
    SDL NORMAL 语义取负、FLIPPED 透传——修 Windows 真实窗口滚轮方向
    与原生相反；headless fake_host 不经此层。
  - 语义：List/Tree/TreeItem role 尾部追加，选中/展开状态与视觉/
    键盘/指针四层一致；gallery 增加树+列演示区。
- 测试：新增 `tests/collection_tests.cpp`（30 用例：选择模式/键盘
  导航/滚动对齐/树展开/列系统/虚拟化物化）与 gallery 集成用例；本地
  Windows 全量 ctest `526/526`（Debug/Release 双跑）。
- 平台：本地 Windows 全部验证；Linux/macOS 以 CI 为事实来源（滚轮
  方向换算只影响真实窗口输入路径，语义由 headless 契约测试锁定）。
- 已知限制：无单元格行内编辑、拖拽重排（DnD）、列宽拖拽调整（首版
  固定+权重）、水平虚拟化、橡皮筋框选、type-ahead、排序执行（框架
  只给列头点击回调，排序由应用做）；多列纯表格由 Grid 按需评估。
- 回滚点：`3422b06 feat(gui): Gallery 对齐设计稿尺度优化`（集合链前）。

### List 视觉与交互补齐（2026-09-19）

- 依据：`lumen-collection-controls-design.md` §6/§10、
  `lumen-visual-system-design.md` §3/§5 与 `design/collection-controls.html`。
- `Theme.list` 统一行背景、分隔线、选中指示条和空态；行高/padding/圆角
  跟随三档密度，支持局部主题、字体缩放和高对比。Gallery Collections
  补充图标/徽标/禁用行、四模式切换、七态矩阵与自动空态。
- 修复整行命中、按压状态、虚拟行焦点 identity、Tab 单一停靠点、语义
  Focus 与 current 同步、Activate 接入行回调、禁用项的导航/区间/全选
  过滤及首尾滚动对齐。
  绘制、命中与语义统一使用边框内侧裁剪，滚动时外框保持完整。
- 回归入口：`tests/list_visual_tests.cpp`、Gallery 集成测试，以及
  `gpu_list_readback_preserves_selection_focus_and_scrolled_border`。
- 本地 Windows 验证：CPU Debug `612/612`、Skia/GPU Release `628/628`
  全量 ctest 通过；List 专项 13 用例/159 断言；GPU 像素回读实际通过，
  未跳过。Gallery CPU 窗口 3 帧正常退出，系统字体下核对深色/浅色高对比/
  两倍字体样本。Linux/macOS 未在本地实机验证。
- 集合专用耗时基准仍为规划项；滚动条 hover/drag 外观沿用视觉任务中的
  预留边界。此次修复保持 ListView、VirtualList 与 Tree 的样式契约。

### Tree 视觉与交互补齐（2026-09-19）

- 依据：`lumen-collection-controls-design.md` §7/§9.3/§10、
  `lumen-visual-system-design.md` §3/§5 与 `design/collection-controls.html`。
- 独立 `TreePart` / `Theme.tree` 接入布局前样式解析：整行实色选中、
  左侧标记、独立焦点环、分隔线、边框内裁剪；缩进同时移动箭头与内容，
  叶节点保留等宽箭头槽。行尺度随密度/ControlSize，缩进、箭头和空态
  随字体缩放，支持局部主题和高对比。
- 修复整行命中、Tab 单停靠点、折叠后 current/焦点恢复、箭头点击保留
  选择、屏外导航、禁用项过滤；补齐默认/自定义空态与框架语义展开折叠。
  `expandAll` 使用有界迭代遍历，超限保留原展开集。Gallery 补充目录图标/
  计数、默认选中节点、八态矩阵和空态；状态示例不依赖真实树模型。
- 开发工作区阶段验证：Windows CPU Debug 全量 `629/629`、Skia/GPU Release
  全量 `646/646` ctest 通过；Tree headless 专项 11 用例/206 断言；
  GPU Tree 像素回读 32 断言实际通过（未跳过，含 1×/2× 与滚动裁剪）。
  Gallery CPU 窗口 3 帧正常退出；系统字体导出核对选中+聚焦、八态/空态、
  浅色高对比两倍字体。Linux/macOS 未在本地实机验证。
- 边界：原生 UIA ExpandCollapse pattern 和其他平台原生语义映射未补齐；
  集合专用耗时基准仍为规划项，万项测试验证按需物化与防护上限。

### 焦点环显隐属性（2026-09-19）

- `Widget.showFocusRing` 默认关闭，`withFocusRing(widget, true)` 显式开启；
  List/Tree/TreeList 视口传递到生成行，Tree 同时传递到箭头。实际焦点、
  选择、键盘导航及语义 focused 保留，运行时切换不改变几何。
- 文本 DSL 的现有节点支持 `showFocusRing: true`；集合节点仍由 C++
  构建。Gallery Collections 和 HTML 设计稿均提供显隐对照开关（默认关闭）；
  视觉规范同步默认关闭契约，focus-visible 输入来源策略仍为待办。
- 开发工作区阶段验证：Windows CPU Debug 全量 `633/633`、Skia/GPU Release 全量
  `650/650` 通过；专项覆盖指针/键盘/语义、Tree 折叠恢复、高对比局部主题、
  开关切换与增量帧一致性。GPU Tree 回读 60 断言实际通过，覆盖环开/关、
  1×/2× 与滚动裁剪；系统字体导出确认关闭后的实际外观。

### 无滚动范围子视口的滚轮传递修复（2026-09-19）

- 滚轮沿命中链跳过空内容、内容完全放得下或本次轴分量不匹配的子视口，
  交给最近的可滚动父级。判断使用实际 `scrollExtent`，隐藏滚动条但仍有
  溢出内容的子视口继续正常接收滚轮；有范围但已到端点的策略保持原样。
- 覆盖 ScrollView/ListView/VirtualList/List/Tree/TreeList、应用 sink 和
  源控制器两条路径；模态事件树隔离保持，水平轴投影随 P2 集成。
- `wheel_routing_tests.cpp` 和 Gallery 空 List/Tree 集成覆盖嵌套滚轮传递。
  独立提交快照 Windows CPU Debug 全量 `635/635`、Skia/GPU Release
  全量 `652/652` ctest 通过；Linux/macOS 未在本地实机验证。
- 上述阶段工作区计数包含独立的标题栏/水平滚动改动；最终提交快照单独
  构建并执行全量测试，确认本轮修复不依赖这些尚未提交的改动。

### Splitter 完成记录（2026-09-18）

- 完成日期：2026-09-18
- 提交号：80429bd；设计输入 `docs/lumen-splitter-design.md` 与
  `design/splitter.html`
- 变更：
  - 新增 `WidgetType::Splitter` + `core::SplitterSource` 契约
    （`include/lumen/core/splitter.h`：offset/min/initial/seeded/
    noteLayout/dragTo/stepBy/stepToEdge/reset，常量方法 + mutable
    状态，virtualSource 同模式）+ `widgets::SplitterController`
    （GTK position 模式：绝对 px 存储、KeepOffset resize 钳制、比例
    仅派生只读、`onOffsetChanged` 持久化回调、min 窗格默认 48px）。
  - `layoutSplitter`（src/layout/layout.cpp）：两窗格主轴精确尺寸 +
    分隔条布局期物化（Ghost Button 承载命中/焦点/语义，key 前缀
    `split:div:`）；min 钳制、极端窄窗按最小值比例压缩不重叠、
    嵌套递归（水平套垂直）。
  - 交互接管（`split:div:` 命中即独占，先于滚动/滑块/选区判定）：
    拖动直接跟手（逐拍 setOffset + 请求重建）、方向键/Home/End 步进
    （随密度 12/16/24px）、双击复位；按压建立键盘焦点、松手失焦。
  - 悬停/拖动光标：`core::PointerCursor`（ResizeEW/NS，core 语义层）
    → runApp 映射 `SystemCursor`（枚举新增 ResizeNS/ResizeEW）仅在
    变化时落宿主。
  - 视觉：painter 画居中轨道线，rest 1px borderStrong / active 3px
    focusRing（形状差异，HC 不靠颜色）；轨道 6px，命中区随密度
    12/16/24px 透明扩展。
  - 语义：role=splitter（尾部追加）、value=百分比文本、SetValue
    0..100 经交互层驱动（≡ 键盘方向键，钳制同源）。
- 测试：新增 `tests/splitter_tests.cpp` 17 用例（布局分配/min 钳制/
  窄窗压缩/拖动跟手与抢占/键盘步进与到边/双击复位/resize 保持 offset/
  垂直方向/语义/Widget 体积预算/轨道线绘制/光标报告/runApp 落宿主）；
  本地 Windows CPU Debug 全量 `580/580`（含同批菜单/标题栏材料的
  完整工作树验证，见下一条记录）。
- 平台：本地 Windows 全部验证；Linux/macOS 以 CI 为事实来源
  （Resize 光标形状经 SDL 系统光标映射）。
- 已知限制：无窗格塌缩/展开恢复与 KeepRatio resize 行为（设计 §13
  按需评估）；`.lumen` splitter 节点、C++ DSL builder 与
  `splitter-list` 基准场景未做（设计 P3 留后续）。
- 回滚点：`286746d fix(platform): SDL 滚轮方向换算对齐平台原生手感`。

### 菜单类控件与自定义标题栏完成记录（2026-09-18）

- 完成日期：2026-09-18
- 提交号：（本批变更提交，见 Git 历史；与 80429bd 同批拆分提交）；
  设计输入 `docs/lumen-menu-controls-design.md`/
  `docs/lumen-titlebar-design.md` 与 `design/menu-controls.html`/
  `design/gallery.html`（标题栏四变体）
- 变更：
  - **菜单类控件（零新增 WidgetType/RenderCommand）**：
    `widgets::MenuItem` 值类型（label/icon/checkable+checked（应用
    维护）/shortcut 仅展示/hasSubmenu 懒构建/separator/disabled/
    mnemonic）+ `ContextMenuController`（指针位置唤起 `open` 与锚定
    唤起 `openAnchored`、级联子菜单逐层懒展开、键盘全契约 Up/Down/
    Home/End（跳过 separator/disabled）/Enter/Space、Right 展开/Left
    回父级/Escape 逐级关闭、Tab 关闭全部、Alt+助记字母直接激活、
    滚轮持久滚动位置/菜单外滚轮关闭/面板内拖动保持打开、barrier
    点击关闭与焦点恢复）+ `MenuBarController`（栏 = 主树 Ghost
    Button 行，参与 Tab 遍历与语义；菜单面板经同一 ContextMenu
    路径锚定栏项下方；点击/hover 切换打开、菜单打开期 Left/Right
    切换顶级、Alt+mnemonic 直接打开）。菜单面板 = M11 框架级
    overlay + 集合行（hover/焦点/语义激活复用 collectionRow 路径）。
  - **Secondary 右键通道（框架唯一缝隙）**：`AppShell::pointerDown`/
    `pointerUp` 与 `InteractionController` 签名增加 `PointerButton`
    缺省参数（runApp 从 HostEvent 填充，既有调用与帧哈希零改动）；
    Secondary 按下不进点击/按压/拖动路径；新增
    `addSecondaryPressSink` 咨询链（命中链 + 指针位置，按注册序
    问询、消费即停，命中链不做 enabled 过滤）。
  - **语义**：`SemanticsRole::{Menu, MenuItem}` 尾部追加；菜单项
    Activate ≡ Enter ≡ 单击同 handler 回执；checked flag；分隔线
    不进语义树；模态期主树指针/键盘/语义 NotHandled（M12 统一规则）。
  - **自定义标题栏（无边框窗口 chrome）**：`WindowDesc.
    customTitleBar`（SDL `SDL_WINDOW_BORDERLESS`）；宿主窗口操作
    `minimizeWindow`/`toggleMaximizeWindow`/`requestWindowClose`
    （默认实现安全 no-op；close 与系统 X 同路径合成
    WindowCloseRequested，经 pollEvent 交付；Fake host 可记录/注入）
    与 `setWindowDragRegion` 拖拽区谓词（无效窗口 id 挂靠首窗）；
    `WindowMetrics.maximized` + `HostEventType::WindowMaximized`
    （runApp 经 onEvent 转发，还原走既有 WindowRestored）。
  - **SDL hit-test**（泵线程内回调，读应用谓词无竞争）：边 8 逻辑
    px/角 12×12 resize 带（最大化态禁用 resize 边，caption 谓词
    兜底保证拖动可还原/unsnap），谓词命中返回 SDL_HITTEST_DRAGGABLE。
  - **拖拽区判定**：`Widget.windowDrag`/`withWindowDrag` 布局期物化
    到 RenderNode；`AppShell::isWindowDragPoint`——事件树口径
    （overlay 活跃期命中即不可拖），最深命中为交互控件（onClick
    目标/TextField/Checkbox/Switch/Slider/Dropdown/分隔条）时不可
    拖，链上含 windowDrag 节点可拖；runApp 在 customTitleBar 时
    注册为宿主谓词。
  - `IconId::Restore`（最大化态还原双层方框图标，尾部追加）。
  - **示例**：gallery 48px 自绘标题栏（品牌 + MenuBar 嵌入 + 拖拽
    区 + 窗口控制按钮 44×32、close hover 警示红）+ 侧栏 Splitter
    （未手动调节时跟随 200/168 响应式断点）+ Menus 演示页与
    Overview 新瓷砖；settings 新页 Menus（栏 + 列表行/空白处右键
    场景）与 Splitter（列表|详情主分栏 + 详情区嵌套垂直分栏）。
  - TreeList 几何修正：chevron 进首列盒（与表头同列口径）、列宽
    预算扣除行壳两侧内边距（修右缘裁剪与滚动条压列）。
- 测试：新增 `tests/menu_tests.cpp` 18 用例（Secondary 穿透不触发
  点击/按压 + sink 消费与未消费两路 + 注册序问询、打开定位与项列
  表、键盘跳过 separator/disabled、Enter 激活 + 焦点恢复、barrier
  关闭、checkable 与快捷键仅展示、子菜单级联 Right/Left/Escape、
  子菜单项直接激活、mnemonic、menu/menuitem 语义、滚轮持久滚动、
  面板内拖动不关闭、菜单外滚轮关闭、Secondary 不扰 Primary、
  MenuBar 锚定打开、Alt+mnemonic、hover 切换）；新增
  `tests/titlebar_tests.cpp` 14 用例（windowDrag 物化、gallery
  结构仅 caption 行标记、交互控件排除、模态 overlay 阻断、窗口
  命令记录与转发、空宿主安全、close 统一策略回退、最大化态图标
  切换、fake host 窗口操作记录、无效 id 挂靠首窗、默认实现 no-op、
  拖拽谓词、runApp 注册条件两路）；gallery 集成 +3（菜单栏+右键、
  分栏断点与手动调节、TreeList 表头几何）、settings 集成 +2
  （Menus/Splitter 页）。本地 Windows CPU Debug 全量 `580/580`。
- 平台：本地 Windows 全部验证（headless + 窗口路径）；Linux/macOS
  以 CI 为事实来源——borderless/hit-test 平台行为差异（Wayland 无
  WM_NCHITTEST 对应物，SDL 退化为 xdg-shell 软件模拟拖动）。
- 已知限制：
  - 菜单 P3 项未做：触摸长按唤起、F10/Alt 单键打开（Key 枚举无
    功能键值）、mnemonic 下划线渲染（Alt+字母键盘已生效）；快捷键
    仅展示不执行（分发在应用 onKey）；系统菜单栏原生同步
    （NSMainMenu/HMENU）不做。
  - 标题栏：macOS 交通灯不适配（三端统一右上 min/max/close）；
    Windows 11 Snap Layouts 悬停弹层不出现（拖到屏幕边缘 snap 仍
    可用）；borderless 最大化工作区约束依赖 SDL（异常回退策略未
    实现，按需评估）。
- 回滚点：`80429bd feat(splitter): 两窗格分栏控件与框架分隔条接管`
  （同批 Splitter 已先行合入；回退本批其余部分回到该提交）。

### 菜单动效（按需控件增强，2026-09-18）

- 完成日期：2026-09-18
- 设计输入：`design/menubar-variants.html`（4 版本候选稿；采纳
  版本 A「基线精修」+ 版本 D「瞬时响应」的组合——通道全部现成、
  成本最低；B 锚点滑移列 P2、C 纵深级联待 transform 通道激活）；
  视觉/交互契约沿用 `docs/lumen-menu-controls-design.md` §10。
- 变更：
  - **MotionTokens 扩展**：`menuOpenFadeMs`（120，面板打开淡入，
    即 §10.2 承诺的 `menu.open.fadeMs`——此前只有文档没有实现）与
    `menuHighlightSlideMs`（90，键盘高亮动能矩形滑移）；二者随
    `reduceAnimation()` 归零。
  - **overlay 动效步进通道（app 层唯一接口增量）**：
    `AppShell::setOverlayBuilder` 增加第 4 参数 `AnimateSink`
    （`bool(std::uint64_t nowMs)`），`tick` 内与 `config.onAnimate`
    同拍调用；返回 true 计入 `animationsActive()`（FrameScheduler
    动画帧），`clearOverlay` 一并解除——菜单控制器持钟动画不再需要
    应用侧 onAnimate 接线。
  - **面板打开动效（版本 A）**：整面板 `transitionAlpha` 淡入 +
    位移（顶级上升 6px、子菜单沿级联方向滑入 4px，同一 EaseOut 进
    度）；动效口径 = `shell.motionEnabled()`（应用 opt-in 且已被
    tick 驱动——不经 tick 的直驱测试/无动效应用保持即时终态，帧输
    出与既有行为一致）。
  - **键盘高亮动能矩形（版本 D）**：selection 背景独立矩形化并**常驻
    承载**（动效/无动效两口径统一——与行 selected 背景同色同矩形、
    source-over 复合等价，像素一致；行不再折算 selected，current 语义
    由焦点表达，两配置语义一致）；动效口径下在行间滑移（跨分隔线高度
    变形，Windows 11 marquee 同源）；焦点环仍随焦点行，hover 路径
    不变。矩形与滚动同域（ScrollView 包整个 Stack）、与行 x/宽精确
    对齐（column 内边距偏移同域）。
  - **MenuBar 打开态（§10.4 补齐）**：打开的栏项切 Tonal 变体 +
    底部 2px accent 下划线渐入（透明度等价表达 scaleX 生长——
    transform 通道未接线；常驻 2px 占位保栏高稳定；渐入与面板同
    `motionEnabled()` 口径门控——未开动效的应用直达终态）；关闭即
    回 Ghost；顶级切换 = 重新锚定 + 重放打开动效（版本 A 契约）。
    `MenuBarController::build(theme)` 恢复设计文档 §7.1 签名。
  - **分隔线几何修正（既有缺陷）**：面板高度按 9px/条预留（1px
    线 + 上下 4 呼吸）但分隔线 margin 实为 `symmetric(4,0)` 只占
    1px——行实际位置与 `ensureHighlightVisible`/高度推导差 8px。
    修正为 `symmetric(8,4)`（水平 inset 8、垂直呼吸 4，与
    `design/menu-controls.html` msep 同口径），行位置/滚动推导/
    动能矩形三者一致。
  - **关闭即时（有意取舍）**：关闭不做出场淡出——瞬态命令面板的
    关闭延迟直接吃命令分发延迟；出场动效留按需评估。
  - **barrier 遮罩修正（既有缺陷，含 Dropdown 同源）**：菜单全窗
    barrier 复用了 `theme.dialog.scrim`（黑 132/255），打开时整窗
    压暗、形似模态对话框——改为视觉透明（仅输入模态，Dismiss 语
    义保留；Dropdown 浮动菜单 barrier 同步修正）。
  - **集合行 hover 追踪补全（框架既有缺口，含列表/树/下拉选项行）**：
    `InteractionController` 的 hover 承载谓词只认 Button/TextField/
    Checkbox/Switch——collectionRow 行（菜单/列表/树/下拉选项）从未
    悬停高亮（resolver 的集合行 hover 分支一直存在但不可达）。补入
    `collectionRow + onClick`（与可聚焦谓词同源）；disabled 行与普通
    行不承载；按压/点击武装路径独立（onClick 链扫描）不受影响。
  - **悬停自动展开子菜单（menu-controls §6.4 悬停级联，设计稿
    menu-controls.html 本有级联 hover 演示）**：悬停 hasSubmenu 项
    自动展开——`MotionTokens::menuSubmenuHoverMs` 默认 0 = 立即
    （原生菜单惯例；应用可设 300 之类去抖，去抖期内移走/移到其他
    项不展开，同项抖动不重置）；悬停同级其他项收起级联
    （`Level.sourceIndex` 判定，源行不动）。pointer sink 惰性注册
    一次（菜单行在 overlay 树，sink 自行命中 overlayRoot）；计时经
    overlay animate sink 首拍盖章步进（与打开动效同口径，不经 tick
    的直驱不触发）。
  - **菜单标签单行省略号（§10.1 既有规定未接线）**：超长标签此前按
    默认 TextStyle（maxLines=0 不限）换行、撑变行高；标签与快捷键
    列改 `maxLines=1 + TextOverflow::Ellipsis`（TextLayout 同一布
    局路径，与 painter 既有用法同模式）——行高稳定，动能矩形/滚动
    推导不受超宽内容影响。
  - **图标清晰度（Search 几何缺陷 + 描边权重）**：Search 折线手柄
    与镜圆脱开（≈3px 缝）且镜内有一道多余斜线——16px 下读不出放
    大镜形；重写为 24 段镜圆 + 45° 相连手柄（16 栅格 (7,7) r4.25，
    相接处共享端点）。`IconTheme::strokeWidth` 1.5 → 1.8（对齐全部
    设计稿 1.7–2.0；1.5 在 16px + AA 下偏细发糊，影响所有描边图
    标：chevron/勾选/窗口钮同步变清晰）；visual-system §8 补描边
    权重与圆弧段数契约（≥24 段、多段相接不留缝）。
  - gallery Menus 演示页描述补动效说明。
- 测试：新增 `tests/menu_motion_tests.cpp` 8 用例（打开淡入+上升
  全程采样、动能矩形跨分隔线滑移中段/终态、栏 Tonal/下划线渐入与
  切换重放、reduceAnimation 首拍终态、不经 tick 直驱即时终态回
  归、overlay animate sink 生命周期与重开、悬停延迟展开与掠过
  不展开、悬停同级收起/源行稳定）；中段压一次完整
  renderFrame（painter 整树透明度 + overlay damage 冒烟）；
  menu_tests 补 barrier 透明断言（仅输入模态）与行 hover 高亮端到
  端断言；interaction_tests 补集合行 hover 追踪用例（含 disabled
  行与普通行不承载）。本地 Windows CPU Debug 全量 `594/594`。
- 平台：动效通道平台无关（CPU/Skia/GPU 同路径）；本地 Windows
  全量验证，Linux/macOS 以 CI 为事实来源。
- 已知限制：B 版本（pill 滑移 + 面板跨锚点连续切换）未实现（P2，
  需打磨 overlay 连续重定位 damage 口径）；C 版本（锚点生长/
  caret 旋转）依赖 `RenderCommand.transform` 预留通道激活；激活
  闪烁（版本 D「闪 120ms 后关闭」）有意不做（不延迟命令分发）；
  无动效应用（未开 `motionTransitions`）只有静态打开态（Tonal +
  下划线直达），无渐入；打开动效进行中（≤120ms）以键盘展开子菜
  单时，级联锚取自当前（上升中）行位置，残留 ≤6px 偏差至关闭
  （键盘竞态窗口，指针路径不涉及）。

### Gallery 窗口 chrome 对齐设计稿（2026-09-19）

- 完成日期：2026-09-19
- 设计输入：`design/gallery.html`（titlebar/caption-button/gallery-
  window 圆角与 .is-maximized）；契约见
  `docs/lumen-titlebar-design.md` §5（本批同步更新）。
- 变更：
  - **窗口控制对齐**：caption 总高 48px（border-box：内容行 47 + 1px
    分隔线；`align-items: stretch`、右缘无 padding）；窗口按钮 44px
    宽、通高（原 44×32 居中且在 padding 内——与稿不一致）；min/max
    保持 Ghost，close 切新
    `ButtonVariant::WindowClose`（枚举尾部追加，IconId/SemanticsRole
    先例）：rest 幽灵（透明底 + 次要前景）、hover 实心
    `statusError` + `onError` 反色、pressed 叠压暗、disabled 透明
    （Windows 惯例）。栏项切 Small 档（32px）——Medium 项 + 下划线 +
    padding 会撑到 46px 挤满 caption（观感复核经渲染 PNG 视觉评审 +
    节点几何核对：48/38/28 三层高度与图标居中全部达标）。
  - **按钮外形逐像素对齐（复核第二轮）**：图标盒 16 → 14px
    （`StyleOverrides.iconSize` 覆盖，描边折算 ≈1.6；M7 体积预算内
    float 位）；窗口三字形按稿 SVG 24 栅格逐坐标归一（Close X 原
    偏大 37% → 7..17、Maximize 6..18、Minimize 5..19）；close hover
    红改专用 `ButtonTokens.windowClose`（#c42b1c + 白，系统 chrome
    常量、深浅同值——原 statusError #E05A60 偏亮）。
  - **菜单行焦点环抑制（复核第三轮，渲染 PNG 定位）**：键盘高亮行
    叠 2px 内嵌 focus ring（集合行 current 规范带过来的）在选中底色
    上双指示冗余且环比底色亮、观感突兀——resolver 集合行分支按
    `semanticsRole == "menuItem"` 置零 focusWidth（零新增 Widget 字
    字段，体积预算不动）；current 指示由动能矩形单独承载，聚焦可见
    性由底色块满足（§5 规则 4）；menu-controls-design §10.3 状态
    矩阵与高对比段同步更新。
  - **键盘重度表面焦点环开启（复核第四轮）**：默认关闭后，无选中底色
    的键盘激活目标失去唯一焦点指示——`makeDialog` 对 actions 子树
    显式 `showFocusRing=true`（Enter/Esc 目标，§6.1 对话框条目），
    settings 示例页 radio/switch/checkbox 显式开启（无其他焦点指示，
    正文与按钮维持默认关闭）；DSL 解析测试恢复 `showFocusRing: true`
    显式开启断言（默认值翻转后原断言退化为只测默认值）。复核补漏
    （评审定位）：Dropdown 浮动菜单选项同样显式开启——Up/Down 高亮行
    即 `focusNode` 目标，Ghost 选项 rest 底透明、环是唯一指示
    （dropdown.cpp buildOverlay；visual_regression_tests 补高亮行
    focusWidth 断言）。
  - **框架键盘件统一开环（复核第五轮）**：默认关闭遍历框架自建键盘
    表面收尾——Splitter 分隔条布局期恒开环（painter 的 3px accent 线
    由 focusWidth>0 驱动，关环时键盘聚焦退回 1px rest 线不可见）；
    `makeTabs` 为页签按钮统一开环（§6.8，页签是 Tab 停靠点、聚焦无
    其他指示）；`makeSlider` 开环（§6.5 thumb 环是设计内焦点指示、
    端点恒定预留 r+f）；TreeList chevron 改走 treePart 物化
    （makeLeadWidget styledTree=true，几何 24=chevronHitExtent 不变）
    并把 configureTreeParts 传递扩展到 TreeList（chevron 是独立 Tab
    停靠点，此前不随视口传递、默认关环下聚焦不可见；空态同口径）。
    splitter/visual_m6/style/collection 各补断言。
  - **透明圆角窗口**：`WindowDesc.transparent`（SDL_WINDOW_
    TRANSPARENT，sdl3_window/host 透传）+ `AppShell::setClearColor`
    （runApp 依 transparent 自动把 CPU 清屏转全透明）；gallery 根
    容器四角 + 标题栏顶角 16px 圆角（自绘），最大化归零；
    `makeDialog` 增 `scrimRadius` 参数（gallery 传同半径——全窗
    scrim 不再把压暗色涂进圆角外的透明像素）。main.cpp 开
    `transparent`。
- 测试：style_tests 补 WindowClose 变体三态解析、iconSize 覆盖与描
  边折算；titlebar_tests 补 chrome 对齐用例（48px 行高/按钮通高 44
  宽/右缘贴合视口/图标盒 14/根与标题栏圆角 16/最大化归零）；
  menu_tests 补菜单行焦点环抑制断言、visual_regression_tests 补下拉
  高亮行 focusWidth 断言。本地 Windows CPU Debug 全量 `663/663`
  （含并行滚动条工作的新用例与焦点环统一四条新断言）。
- 已知限制：无合成器的 X11 会话角落退化为黑；Skia/GPU 后端的透明
  clear 未接（按需评估）；close hover 为 `ButtonTokens.windowClose`（#c42b1c，深浅主题
  派生）而非 mock 硬编码 #c42b1c。

### 既有能力优化（2026-09-19，计划见 docs/lumen-optimization-plan.md）

#### P1 CPU 阴影软模糊（2026-09-19）

- 完成日期：2026-09-19
- 提交号：（本变更提交，见 Git 历史 `feat(render)`）
- 变更：
  - `CpuRenderer::drawShadow`（src/render/cpu_renderer.cpp）从
    「(void)blur 偏移扁平面（alpha×0.5）」升级为软阴影：偏移矩形 →
    设备像素 alpha 掩膜（盒式滤波 AA 边界）→ 3 次 H+V 可分离 box
    blur 近似高斯（σ = blur×0.5×deviceScale，与 SkiaRenderer 的
    `kNormal_SkBlurStyle, blur*0.5*scale` 同口径；box 宽 =
    σ×sqrt(5)，域外计 0 的能量守恒卷积）→ 以阴影色逐像素 coverage
    混合（复用 blendCoveragePixel）。掩膜/行缓冲为渲染器成员
    scratch（shadowMask_/shadowScratch_），无逐命令分配。
  - `blur=0` 防御路径保留 M6 扁平面行为；高对比 alpha=0 早退不变。
  - RenderCommand/序列化/token/damage 口径零改动（damage 层本就按
    blur×2+1 外扩）；Skia/GPU 像素零变化。
  - 文档：theme.h ElevationTokens 注释与 visual-system-design §8
    的「CPU 扁平降级」表述更新为软阴影口径；M6 已知限制追记收口。
- 测试：新增 `tests/visual_m6_tests.cpp` 5 用例——边缘单调衰减
  （纵/横两向 + 3σ 外≈0）、能量守恒（±30% 容差带）、绘制范围落在
  damage 外扩 blur×2+1 内（局部 damage 不漏刷的直接像素证明）、
  σ 随 deviceScale 线性（scale=2 衰减距离≈2×）、blur=0 扁平面
  回归（内部均匀半强度 + 硬边）。本地 Windows CPU Debug 全量
  `599/599`（594 既有 + 5 新增；**零既有哈希变化**——headless 哈希
  路径首帧不含 elevation，含阴影场景无帧断言）；bench card-grid
  场景无 elevation，A/B 验证帧 hash 与改动前一致。
- 平台：动效平台无关（CPU 软件路径）；本地 Windows 全量验证，
  Linux/macOS 与 Skia/GPU 构建以 CI 为事实来源（命令同源，后端
  像素各自消费）。
- 已知限制：box blur 为高斯近似（核形状非逐位一致 Skia，σ 口径
  一致）；垂直 pass 为列跨步访存（掩膜面板量级微秒级，未做转置
  优化）。
- 回滚点：`cac1259 fix(icons): Search 几何与描边权重清晰度`（P1 前）。

#### P2 水平滚动轴（2026-09-19）

- 完成日期：2026-09-19（切片一与滚动条交互修复同日合入）
- 提交号：`d6f76de feat(core): ScrollController 轴化与水平滚动设计契约（P2 切片一）`、`a923d7c fix(scroll): 完善滚动条交互并修复横向滚动与取消行为`
- 变更：
  - 新增 `core::ScrollAxis { Vertical, Horizontal }`（`include/lumen/core/scroll.h`），
    `ScrollController` 构造携带轴、全部方法按活动轴解释输入（fling 物理
    常量不变）；`Widget.scrollAxis`/`RenderNode.scrollAxis` 默认 Vertical
    （未声明视口行为与现状逐字节一致，Widget 体积预算测试 +1B 对齐已
    更新：`tests/collection_tests.cpp`）。
  - `layoutScrollView` 按轴选择镜像约束（横向：宽不限、高 ≤ 视口 -
    padding），子 offset 应用到活动轴；纵向默认路径几何零变化。
  - painter 按节点轴画横向 thumb（底边、track = 视口宽、thumb 宽 =
    visibleFraction×track、minLength 同 token）；单轴视口只画一条。
  - `InteractionController` wheel 派发按命中视口轴选分量（纵向吃
    deltaY、横向吃 deltaX）；Shift+纵轮 → 横向分量的桌面惯例在交互层
    换算（FLIPPED 符号随 M12 既有换算）；`wheelSink` 签名扩为携带完整
    `Offset` delta（默认参数兼容）。
  - `ScrollDragSink` 泛化为活动轴分量；横视口内 Slider 拖动仍设值
    （垂直版回归的精确镜像）。
  - 键盘：焦点在横向视口（非 TextField）时 Left/Right 步进、Home/End
    两端、PageUp/PageDown 横向翻页；TextField 内 Left/Right 仍是 caret
    移动（既有优先级不变）。
  - 语义：`AccessibilityBridge`/`AppShell::performAccessibilityAction`/
    Recording 的 scroll 参数族尾部追加 `scrollDeltaX = 0.0F`（默认参数
    兼容扩约）；横向视口的语义 value/scroll action 按活动轴报告。
  - 契约写入 `docs/lumen-scroll-design.md`（轴模型、输入路由、物理、
    键盘/语义契约——后续滚动行为变更以此为准）；`lumen-visual-system-
    design.md` 滚动条节补横向几何。
- 测试：`motion_scroll_tests.cpp` 横向 scrollBy/applyWheel/applyDrag/
  applyKey/semanticScroll/fling 与纵向同参数同行为 + `layoutScrollView`
  镜像约束三用例；`scrollbar_tests.cpp` 两轴滑块几何/命中/状态/键盘
  拒绝异轴箭头；`wheel_routing_tests.cpp` 分量路由 + Shift 投影 +
  FLIPPED 符号 + 嵌套回归；`gpu_smoke_tests.cpp` 双轴 thumb damage
  不变量；`semantics_tests.cpp` scrollDeltaX 回执；`collection_tests.cpp`
  Widget 体积预算。本地 Windows CPU Debug 全量通过；既有 headless 帧
  哈希在默认纵向场景零变化（硬出口）。
- 平台：平台无关（布局/交互/渲染层概念）；本地 Windows 全量验证，
  Linux/macOS 与 Skia/GPU 构建以 CI 为事实来源。
- 已知限制：无双轴联滚、无水平虚拟化、无 RTL（§1 非目标）；
  自动隐藏滚动条未实现；Shift+纵轮投影为框架层约定（宿主无原生横轮
  事件时）。
- 回滚点：`dd6366b feat(examples): GPU 回退改用 Skia 软件光栅保持文本连续`（P3，P2 前）。

#### P3 GPU 回退文本连续性（2026-09-19）

- 完成日期：2026-09-19
- 提交号：（本变更提交，见 Git 历史 `feat(examples)`）
- 变更：
  - counter（GPU 回退样板）`onRendererFailure`（examples/counter/
    main.cpp）：Skia 构建（LUMEN_HAVE_SKIA）下回退目标从「空 setup →
    应用壳内部 CPU 渲染器」改为 Skia 软件光栅——`skiaFallback` +
    `skiaSoftwareSetup` 装配（present 回调/DP 同步，样板与
    `--renderer skia` 模式一致），沿用 GPU 期同一 `SkiaFontManager`，
    shaping/度量/光栅同源，回退后文本与光标/选区几何零漂移。纯 CPU
    构建无此层（`skiaSoftwareSetup` 为空 std::function），回退内部
    CPU 渲染器，行为不变。
  - 启动期 `gpu init failed` 路径有意保持 CPU：工厂在 fontFactory
    咨询之前失败，第 0 帧起即 SystemFontManager + CPU，天然连续
    （run_app.cpp 装配顺序：rendererFactory → fontFactory，后者仅
    启动咨询一次）。
  - 诊断口径：`[diag] gpu failed (…) — falling back to skia
    software raster|cpu`（按构建事实）。
  - 测试期望：`counter_gpu_runtime_fallback`（Linux xvfb + GL 交换
    失败注入）在 Skia 构建下断言 `backend=skia-raster`（原
    `backend=cpu`；examples/counter/CMakeLists.txt 按 LUMEN_ENABLE_
    SKIA 分流）；`counter_gpu_fallback`（探测失败→CPU）与
    `counter_software_present_failure`（"CPU present failed" 诊断
    契约）不变。
- 测试：本地 Windows Skia+GPU 构建真验三路径——`--renderer gpu`
  正常（backend=skia-gpu）、`--renderer skia`（backend=skia-raster）、
  dummy 驱动探测失败（"gpu probe failed"→backend=cpu，与
  counter_gpu_fallback 断言一致）；临时探活补丁强制失效跑通运行时
  回退全链路（`gpu failed → falling back to skia software raster →
  backend=skia-raster → frames=3 继续提交`）后还原补丁重编。CI
  counter_gpu_runtime_fallback 以 Linux 首跑为事实来源。CPU-only 本地
  构建全量不受影响（改动全部在 LUMEN_HAVE_SKIA 守卫内）。
- 平台：本地 Windows（Skia+GPU 构建真验）；Linux 运行时失败注入以
  CI 首跑为事实来源；macOS 同链路随 CI。
- 已知限制：文本同源性由「同一 FontManager + 同一光栅器」构造保证，
  无独立像素断言（CI 冒烟的 backend 断言为装配守卫）；run_app.cpp
  呈现失败诊断字符串 "CPU present failed" 措辞沿用（既有 CI 契约，
  不随回退后端改名）。
- 回滚点：`2df25a6 feat(render)`（P1，P3 前）。

### Gallery 设计稿尺度优化对齐（2026-09-16）

- **设计输入**：`design/gallery.html` 评审板重排为 4px 网格——正文/标签
  14px、辅助说明与代码 12px、hero 32px/600、指标值 28px/650、面板标题
  14px/650；主操作/导航 40px、卡片紧凑样本 32px（Small 档）；面板内边距
  与间距 16px；v1 色槽微调（line-strong #8c8c98、positive #73c991、
  accent-content #a8c5fa、按钮/开关前景纯黑）与既有 Theme token 一致，
  无需改 theme.cpp。
- **实现**（`examples/gallery/gallery_app.h`）：
  - 排版辅助整体切换到新尺度并补 letterSpacing/lineHeight；kicker/状态
    胶囊文字改 accentContent；指标 delta 默认 muted、仅正向变化用
    statusSuccess（设计稿 `.positive`）。
  - 壳层几何：品牌标 28×28（随 fontScale）、顶栏内距 20/12 且 gap 16、
    窗口操作 32×32、侧栏 200px/内距 12/24、分隔线上下 24、主内容
    padding 32、页脚紧凑态内距 16/12。
  - Overview：内容栅格 1.4:1、间距 16；组件卡最小 176px、间距 12；面板
    头到内容 16；token 色板 12×12/radius 3 + line-strong 描边；代码行号
    列宽 20、行距 4；主题色点 24px/间距 8；layout 预览条 32px/圆角 4；
    瓦片内 Checkbox/Switch/ProgressBar 降为 Small（紧凑样本）。
  - 响应式（设计稿容器断点的应用侧映射）：≤1024 侧栏收窄 168、主内边距
    24、隐藏窗口状态（窗口操作保留，此前 <1100 连操作一并裁掉）；<720
    折叠为 header 下拉导航 + 页脚单行（64px 图标栏因框架无导航图标目录
    以此近似）；主内容 <720 上下堆叠、<480 指标单列（窄指标卡为左说明
    右数值两列）且 hero 降 24px。
- **验证**：本地 Windows CPU Debug `ctest -C Debug` `494/494` 全过；
  gallery `--headless` 冒烟全链路输出正常（帧哈希随尺度变化更新）；
  `--dump-frame` 首帧像素核对。

### Gallery 对齐 Core Dark 设计稿（2026-09-15）

- **壳层与首屏对齐 `design/gallery.html` v1「Core Dark」**：
  - 窗口顶栏（品牌标/标题 + Desktop preview 状态胶囊 + 视口尺寸 +
    最小化/最大化/关闭装饰图标）；侧栏 214px（SECTIONS 标签、导航、
    分隔线、Live state 注记）；双侧页脚（渲染器状态 | 路由/主题/下拉）。
  - Overview 首屏改为设计稿仪表盘：kicker/hero/主操作、指标卡三联
    （点击数/物化节点/主题与密度）、双栏面板（Control inventory 六格
    迷你控件预览 + C++ DSL 快照 | Resolved tokens 实况 token 行 +
    Theme controls）。分区卡片统一为面板样式（panel head + 1px 边框）。
  - CoreDark 方向 dark 侧色板对齐设计稿槽位：ink #f1f1f4、muted
    #a1a1aa、line #3b3b45、line-strong #555562（经 coreDarkPalette
    槽位法，映射函数零改动；浅色侧保持 v0.3 基线）。
- **框架侧最小扩展**：StyleOverrides 新增 borderWidth（容器卡片边框，
  painter 双层绘制既有契约）；IconId 新增 Maximize（窗口操作方框）。
- **缺陷修复：CPU 后端 submit 丢失 DrawIcon/DrawShadow 命令**——
  CpuRenderer::submit 的原生命令分发缺两条 case（基类适配路径有、原生
  路径无），图标/阴影仅在 paintScene 直绘路径可见、经 submit 静默丢失；
  补齐分发并新增命令回放回归测试（cpu_submit_replays_icon_commands）。
- **验证**：gallery `--headless --dump-frame` 导出首帧 RGBA（新增调试
  选项），像素采样核对设计稿 token（顶栏/页面底/分隔线/卡片/状态胶囊/
  图标描边）；本地 Windows CPU Debug `412/412` 全过、headless 冒烟
  全链路输出正常。
- 已知限制：侧栏 Live state 注记的虚线边框以实线近似（框架暂无虚线）；
  headless 帧文本仍为占位字体（确定性帧哈希）；窗口 CPU 路径经
  SystemFontManager 绘制真实字形（Windows 优先 GDI 雅黑，其他平台或 GDI
  回退经 stb_truetype，无 Skia 依赖）。

### M13 进行中记录（原生 provider，2026-09-22）

- 完成日期：2026-09-22（provider 实施；三平台真实屏幕阅读器回环仍待平台验收）
- 提交号：（本变更提交，见 Git 历史 `feat(a11y)`）
- 变更：
  - 公共契约（`include/lumen/accessibility/bridge.h`）：`PlatformAccessibilityHost`
    （dispatch 回灌 + `void*` 原生窗口句柄 + deviceScale/applicationName；
    SDK 类型不进公共头）+ 工厂改签名 `createPlatformAccessibilityBridge(host,
    diagnostics)` + `accessibilityProviderName()` 编译事实查询。
  - 平台接缝：`ApplicationHost::nativeWindowHandle`（SDL 宿主 Windows =
    HWND、macOS = NSWindow*、Linux = nullptr；经 SDL_GetPointerProperty）与
    `noteAccessibilityBridgeActive`（SDL/Fake host 如实置位 capabilities）。
  - runApp 装配（`src/app/run_app.cpp`）：`RunOptions.nativeAccessibility`
    （默认 true）时创建原生桥、dispatch 捕获
    `shell.performAccessibilityAction`（与键盘/指针同路径）、能力置位与
    `[diag] a11y=` 诊断；桥为局部变量先于窗口销毁析构。
  - Windows UIA provider（`src/accessibility/uia_provider.*`，选项
    `LUMEN_ENABLE_ACCESSIBILITY_BRIDGE` + Win32 条件编入）：
    `SetWindowSubclass` 应答 WM_GETOBJECT（SDL 消息泵钩子拦不到 SendMessage
    直达的请求）；`UiaRootProvider`（fragment root = 语义根，命中测试/
    GetFocus/宿主元素）+ `UiaNodeProvider`（每节点缓存，Simple/Fragment +
    Invoke/Toggle/Value/RangeValue 同对象按快照门控）；role→ControlType、
    flags→属性（focused 双源 flag+focusedId）、actions→pattern、changed
    字段级属性事件 + 结构整体失效事件 + 焦点事件（UiaEventSink 出口可注
    入）；RuntimeId = identity FNV-1a 双字（跨重建稳定）；桥析构
    UiaDisconnectProvider 断开残留 AT 引用。
  - Linux AT-SPI2 provider（`src/accessibility/atspi_provider.*`）：按
    `org.a11y.atspi.Socket.Embed` 注册根对象，提供 Accessible/Action/
    Component/Value/Properties 接口、稳定 identity 对象路径和结构/属性/
    焦点事件；UI 线程经 `AccessibilityBridge::pump()` 驱动 D-Bus。
  - macOS NSAccessibility provider（`src/accessibility/nsaccessibility_provider.*`）：
    以 ObjC++ `NSAccessibilityElement` 树挂接 SDL `NSWindow` contentView，
    映射 role/value/action/bounds/focus 并发送布局/值/焦点通知。
  - 设计文档 `docs/lumen-accessibility-provider-design.md`（含 AT-SPI §6/
    NSAccessibility §7 的 P2/P3 实施映射规格）。
- 测试：新增 `tests/a11y_provider_tests.cpp`——工厂按编译事实分流（未编入
  安全降级）、Fake host 能力翻转、fragment 导航/属性映射/pattern 回灌
  （含 Toggle 翻转/RangeValue 0..100/SetValue 字符串契约）/事件序列/
  RuntimeId 稳定/dispatch 重入（id 拷贝 + 残留引用安全）/根命中测试；
  真实窗口端到端冒烟（`LUMEN_UIA_LIVE_SMOKE=1` 启用）：Win32 窗口 + UIA
  客户端 API `CUIAutomation::ElementFromHandle` → FindFirst("OK") →
  Invoke → dispatch 回执（完整 WM_GETOBJECT 链路，本地 Windows 全过 16
  断言）。本地 Windows：选项 ON Debug `591/591`、默认 OFF Debug
  `582/582`（未启用行为与现状一致——出口条件之一）。
- 平台：本地 Windows 全部真验（headless + 真实窗口端到端）；windows.yml
  新增 `a11y-bridge` job（选项 ON + live smoke，CI 首跑为事实来源）。
- 已知限制（与出口条件的差异）：
  - AT-SPI/NSAccessibility 在无桌面服务或无原生窗口时能力如实降级；真实
    Orca/VoiceOver 回环和 Windows 讲述人/NVDA 人工验收尚未声称完成。
  - 讲述人/NVDA 人工回环待办（端到端冒烟已覆盖 UIA core 链路，但非真实
    AT 验收）。
  - Text/Scroll/Selection pattern 不做（设计文档 §3 非目标）；结构事件为
    整体失效（细粒度留优化）。
- 回滚点：`286746d fix(platform): SDL 滚轮方向换算对齐平台原生手感`
  （P1 前）。

### 既有能力优化：预乘 alpha 完成记录（2026-09-20）

- 范围：[预乘 alpha 计划](lumen-premultiplied-alpha-rendering-plan.md) P0–P5，按阶段
  review、修复、完整验证并提交；不新增里程碑或后端。P0–P4 分别为 `a28c26f`、
  `ea8a0c3`、`d4993ef`、`a45ebd6`、`316127a`；P5 为本记录所在文档提交。
- 交付：AlphaMode 随帧/资源/命令传递，CPU 累积及图片缓存预乘，Skia 上传/读回准确标记，
  SDL 正常透明帧直接提交；修复旧 CPU 通道回绕、Skia 二次预乘和 Preserve 透明清屏重复叠加。
  API 重编译要求、`pixels()` 与首帧例外、v7 写入/v6 兼容、显式 straight 导出见
  [迁移说明](lumen-alpha-migration.md)。控件几何与视觉 token 未改变。
- 验证：Windows CPU Release/Debug 各 755/755、Skia raster 768/768、GPU 779/779，
  无跳过；P5 重新构建四配置并完成全量 CTest。P3 实窗呈现/resize/最小化恢复 584 断言，
  GPU 三模式图片经过真实上下文销毁重建及 CPU 重上传。62 帧视觉对照 alpha 精确、普通
  RGB 最大差 2，图片与冻结的独立参考精确相等。
- 实测：CPU/Skia 各三组、90 次正式运行。CPU 叠层 submit p50/p95 配对变化中位数
  -41.15%/-43.07%，正常透明转换/复制/scratch 为零，1080p/4K 分别省去约
  7.91/31.64 MiB 格式副本；CPU 边缘 p50 约 +5.74%，256×256 straight 图片接纳
  每次增加约 0.24 ms。Skia headless 无可靠提速结论，GPU 性能未测。
  所有超限及追加对照、内存口径与宿主波动限制见
  [P4 报告](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)，历史 M0/P0 保留。
- 待验/限制：Windows 原生 software 为 XRGB，不支持逐像素透明；Windows texture
  合成器视觉、Linux X11/Wayland/macOS 桌面与 Linux 运行时 GPU/present 故障注入未跑。
  上下文重建不等于运行时故障注入；4K drawable 不等于物理 4K 屏幕。
  固定 SDL 在 Wayland/macOS 缺少严格 native software framebuffer，既有回退限制保留。
  这些不改变三平台发布状态或移动端暂缓范围。
- 回滚基点：`e06f3d9`；按计划 §10 逆序回滚阶段，模式生产/消费和 v7 兼容边界须保持一致。

### 范围调整记录（2026-09-14）

- 当前 UI 只面向 Windows/Linux/macOS；M9 已冻结，不列入待实施里程碑。
- v0.3 8E 和视觉系统 V4 的规划统一为桌面范围，Android/iOS 不再承担版本出口条件。
- 原有 M0–M8 完成记录中的 mobile-core 数字保留为当时的实验配置验证记录，
  不表示移动平台已完成，也不表示本次复测通过。
- 删除与既有 M5 完成记录冲突的“未开始”占位项；已有实现不因本次文档调整回退。
