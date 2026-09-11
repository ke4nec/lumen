# Lumen 自用跨端 GUI 里程碑路线图

> 文档状态：实施路线图（2026-09）
> 目标：在现有 Lumen 核心之上，形成一套可供个人工具类应用长期使用的跨端 GUI。
> 当前策略：桌面优先，Windows/Linux/macOS 先形成发布闭环；Android/iOS 在桌面稳定后接入。

## 1. 目标、范围和完成定义

### 1.1 最终目标

Lumen 的自用版不是通用的 Flutter 替代品，而是一个边界清楚、可维护、可诊断的 C++20 自绘 GUI。它应当支持个人工具类应用的完整使用闭环：

- 多窗口、窗口缩放、DPI、最小化/恢复和退出。
- Button、TextField、Checkbox、Switch、Dialog、Navigator、ScrollView、ListView、Grid 和 VirtualList。
- 中文、emoji、组合字符、RTL、IME preedit/commit、选区、剪贴板和 undo/redo。
- 键盘焦点、鼠标/触摸、滚轮、语义树和键盘可访问操作。
- 由 Theme 驱动的颜色、排版、尺寸、图标、阴影、动效和响应式布局。
- CPU、Skia 光栅和 Skia Ganesh GPU 三条渲染路径；GPU 故障可诊断并安全恢复。
- Windows、Linux、macOS 可运行的便携包、启动说明和故障诊断。
- 移动端共享同一套 core/layout/style/widget 语义，平台 glue 只负责 host、生命周期和系统输入。

### 1.2 明确不纳入第一版

以下能力不作为第一版自用工具 GUI 的完成条件：

- 数据库、网络请求、同步、账号和插件市场；这些由具体应用实现。
- 完整富文本编辑器、表格控件、3D 和 WebAssembly。
- CSS 或 Flutter API 兼容层。
- 第一版完整 i18n/本地化资源系统；应用可自行管理字符串和区域设置。
- 移动端商店发布、移动端原生无障碍树和完整移动端控件套件。
- Graphite、Vulkan、Metal、D3D 专用渲染后端。第一阶段统一使用 Skia Ganesh + OpenGL。

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
| v0.3 8D | 已完成应用基础组件 | ScrollView/ListView、Form、Dialog、Navigator、settings 示例 |
| v0.3 8E | 已完成 SDL-free 移动接缝 | `MobileHostSeam`、生命周期、safe area、触摸归一化、返回键 |
| 视觉 V1/V2 | 已完成主要状态样式迁移 | token、Theme、StyleResolver、ResolvedStyle、状态与 damage 联动 |

当前验证基线：Windows CPU Debug 303/303，Skia Release 314/314，SDL-free mobile-core 291/291。`817ad43` 后 Windows 的 CPU/Skia/GPU 三个 job、Linux 的 CPU/Skia/GPU/mobile-core 四个 job、macOS 的 CPU/mobile-core 两个 job 均已纳入 CI；真实 macOS GPU 仍待 M7 纳入门槛。最新基线提交为 `817ad43 fix(platform): 对齐跨平台能力与验证契约`。

关键缺口的源码依据（以文件和符号为准，行号随实现变化不作为稳定引用）：应用主循环和帧管线位于 `examples/counter/counter_app.h::CounterApp::rebuildIfDirty/renderFrame` 与 `examples/settings/settings_app.h::SettingsApp::rebuildIfDirty/renderFrame`；Widget 类型定义位于 `include/lumen/core/widget.h::WidgetType`；C++ DSL builder 位于 `include/lumen/dsl/dsl.h::column/row/container/text/button/text_field`；表单内置校验器位于 `include/lumen/widgets/form.h::FormController::nonEmpty/minLength`；GPU `partialSubmit` 当前在 `src/render/skia_gpu_renderer.cpp::SkiaGpuRenderer::capabilities` 固定为 false；移动接缝实现位于 `src/platform/mobile/mobile_host_seam.cpp::MobileHostSeam`。

### 2.2 仍缺少的能力

| 领域 | 当前实现 | 对自用版的影响 | 计划里程碑 |
| --- | --- | --- | --- |
| 文本 | CPU 光栅的内置 5x7 字体只覆盖 ASCII，非 ASCII 会落到方块占位；布局主要使用 `PlaceholderFontManager`，Skia 绘制虽可使用系统字体但布局度量未完全统一 | 中文、字体 fallback、复杂脚本和光标定位可能跨后端漂移 | M1 |
| 编辑 | selection/composing 已有，undo/redo 未完成 | 长时间编辑体验不完整 | M1 |
| 方向文本 | RTL 是确定性近似，混合方向未完整实现 | 双语工具和混排文本命中测试不可靠 | M1 |
| 应用框架层 | counter/settings 各自手写主循环、rebuild、damage 和事件路由，没有统一 `runApp`/应用壳 | 新应用需要复制大量生命周期样板，容易产生行为分叉 | M2 |
| DSL | `.lumen` 文本 DSL 覆盖组件多于 C++ builder，C++ builder 目前只覆盖基础容器、文本、按钮和 TextField | C++ 声明式 API 与文本 DSL 能力不对称 | M2 |
| 控件库 | 没有 Image Widget、Dropdown/Menu、Tooltip、Slider、ProgressBar、Radio/Tabs；Scrollbar token 已有但无完整绘制控件；FormController 实现只有 nonEmpty/minLength，头文件注释提到的邮箱校验器尚未提供 | 工具应用常见信息展示、选择和表单校验能力不足 | M3/M6 |
| 布局 | Box/Flex 子集，缺少 Grid、真正虚拟化列表 | 页面复杂度和大数据量受到限制 | M3 |
| 滚动 | 确定性滚动，惯性默认关闭 | 触摸端和长列表体验较弱 | M3/M9 |
| 平台服务 | 剪贴板和文本输入已有，文件选择/通知、鼠标光标形状、窗口图标等服务未形成统一接口 | 工具应用无法完成常见系统操作 | M4 |
| 无障碍 | 语义树和 Recording bridge 已有，UIA/AT-SPI/NSAccessibility provider 未实现 | 第一版可做结构验收，无法直接被桌面读屏器消费 | M5 先收口，后续版本再做原生 provider |
| 视觉 V3 | IconId、Elevation、Motion token 已冻结，实际图标/阴影/转场/ThemeScope 未完成 | 复杂应用的视觉一致性和反馈不足 | M6 |
| GPU | Skia Ganesh + OpenGL 已有，GPU `partialSubmit` 固定为 false，macOS GPU 不是当前门槛 | 三桌面发布能力不对称，局部 damage 在 GPU 上退化为全帧提交 | M7 |
| 发布 | 有构建和 smoke，没有正式便携包流水线 | 用户无法脱离开发环境分发 | M8 |
| 移动端 | 只有 SDL-free seam，没有文本输入/软键盘入口；mobile-core 与 Skia 互斥，当前只能 CPU 占位字体 | 不能在模拟器/真机启动可用的文本工具页面 | M9 |

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
                                      ↓
                         M9 Android/iOS 原生接入
```

M1–M3 可以并行准备，但必须全部达到各自出口条件后才能进入 M4；之后按 M4 → M5 → M6 → M7 → M8 合入。M9 不阻塞桌面自用版。

## 4. 里程碑详细计划

### M0：基线冻结与工程入口

**目标**：把当前已完成能力和后续目标分开，建立唯一可追踪的验收入口。

**任务**

- 新增本路线图并链接到 README、支持矩阵和版本计划。
- 为 CPU、Skia、GPU、mobile-core、headless、窗口 smoke 和基准建立统一命令表。
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
- `createPlatformAccessibilityBridge` 继续报告 provider 未纳入；UIA/AT-SPI/NSAccessibility 不阻塞第一版便携发布。

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

### M9：Android/iOS 原生接入

**目标**：在桌面版稳定后，把已验证的 core/layout/style/widget 迁移到真实移动 host。

**实现**

- Android 增加 NDK/CMake 工程、JNI/NativeActivity glue、surface 生命周期和返回键映射。
- iOS 增加 Xcode/Objective-C++ 工程、UIView/CAMetalLayer 或当前选定 surface glue、生命周期和 safe area 映射。
- 复用 `MobileHostSeam` 的 attach/detach、pause/resume、内存告警、触摸 pointer id、逻辑坐标和返回请求。
- 为 `MobileHostSeam` 增加 TextInputSession/软键盘显示、隐藏、候选区和编辑状态转发；原生 glue 只负责把系统事件转换为平台无关事件。
- 在移动目标中确定字体策略：优先使用 Skia/系统字体 shaping；若移动核心继续保持 SDL-free 且不引入 Skia，则必须提供可用的中文/emoji 字体后端，而不是沿用只支持 ASCII 的占位绘制。
- 增加移动版 counter/settings 精简页面，验证旋转、暂停恢复、触摸、文本输入和滚动。
- 移动端正式 GPU、商店发布和原生 accessibility tree 单独建立后续版本，不修改桌面发布门槛。

**出口条件**：Android 模拟器、iOS Simulator 和至少一类真机能启动最小页面；surface 重连和暂停恢复不丢状态；移动核心仍可 SDL-free 构建。

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
| Android | SDL-free core | 由后续移动目标决定 | 后续移动目标 | 模拟器 | 后续 |
| iOS | SDL-free core | 由后续移动目标决定 | 后续移动目标 | Simulator | 后续 |

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
| 移动字体策略 | mobile-core 当前与 Skia 互斥，无法直接复用桌面 shaping | M9 先完成软键盘路径，再决定移动 Skia 接入或独立系统字体后端；决策前不宣称移动中文可用 |
| 视觉命令扩展 | CPU/Skia/GPU 输出不一致 | 先扩展命令模型和序列化，再接入具体控件 |
| 平台服务失败 | 文件选择/通知阻塞或丢状态 | 所有服务返回结构化失败，状态更新只能在 UI 线程完成 |
| 范围膨胀 | 计划被富文本、插件、移动商店拖慢 | M8 即可形成桌面自用版，M9 与后续能力独立排期 |

## 8. 版本切分和停止条件

- **桌面自用版**：完成 M0–M8。M5 的语义契约和 Recording bridge 必须完成；原生桌面 accessibility provider 可以后续追加。
- **移动预览版**：完成 M9 的模拟器和生命周期验证，不承诺商店发布或完整移动无障碍。
- **后续增强版**：再评估 UIA/AT-SPI/NSAccessibility、移动端 Metal/Graphite、原生移动 accessibility、惯性滚动、富文本和数据库/网络辅助库。

任何里程碑若无法满足出口条件，只能修复当前阶段或回退实现，不能通过修改文档把“接口存在”标记为“平台完成”。

## 9. 维护规则

- 每个里程碑完成后，在本文件 §10 补充完成日期、提交号、测试数量和未覆盖平台。
- 代码行为、支持矩阵、README 和 CI 发生变化时，必须在同一变更中更新路线图状态。
- 发现设计文档与实现不一致时，先以源码、构建目标和实际测试为事实来源，再决定是补实现还是降级文档承诺。
- 所有新功能必须有至少一个 headless 测试；涉及窗口、输入、GPU 或系统服务时，再增加对应平台 smoke。
- 能力状态必须使用 `support-matrix.md` 的四态（接口已存在/headless 已验证/
  真实平台已验证/可发布），禁止把“接口存在”标记为“平台完成”。

## 10. 里程碑完成记录（M0 冻结模板）

每个里程碑的完成记录必须包含：变更、测试、平台、已知限制、回滚点。
模板：

```text
### Mx 完成记录
- 完成日期：
- 提交号：
- 变更：（模块/接口/行为，指向源码符号）
- 测试：（单测/headless/像素/窗口 smoke/基准数量与命令）
- 平台：（三桌面/mobile-core 覆盖与未覆盖平台）
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

### M1–M9 完成记录（待实施，占位）

- M1 真实文本与编辑：未开始（出口：桌面 Skia 布局不再使用占位字体度量；
  编辑可撤销/重做；缺字体可启动并诊断）。
- M2 应用框架层与 C++ DSL：未开始（出口：新工具页只需提供 build/状态逻辑；
  应用壳统一事件/帧/damage/DPI/IME）。
- M3 布局/Grid/VirtualList/Image：未开始（出口：千项列表不全量构建子树；
  复用后滚动/焦点/语义/状态正确；窄窗口可用）。
- M4 平台服务与窗口能力：未开始（出口：三桌面完成打开文件/编辑/复制粘贴/
  通知/缩放/退出；服务失败有诊断）。
- M5 语义与键盘可用性：未开始（出口：语义/键盘/视觉/交互无分叉；
  Recording bridge 可作回归证据）。
- M6 视觉 V3 与控件库：未开始（出口：视觉扩展由 token/StyleResolver 驱动；
  后端不依赖 Theme；主题/动效可运行中切换）。
- M7 GPU 与性能门槛：未开始（出口：三桌面 GPU CI 通过；故障恢复无崩溃/
  卡死/泄漏/旧资源复活；partialSubmit 有实测与降级说明；p50/p95 相对
  `v0.2-cpu-scene.json` 恶化 ≤10%，同后端同场景比较）。
- M8 三桌面便携发布：未开始（出口：干净机器可启动；依赖/GPU/字体缺失有提示；
  可追溯到 commit）。
- M9 Android/iOS 原生接入：未开始（出口：模拟器+真机启动最小页；
  surface 重连/暂停恢复不丢状态；mobile-core 仍 SDL-free）。
