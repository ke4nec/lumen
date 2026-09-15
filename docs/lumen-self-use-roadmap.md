# Lumen 自用桌面 GUI 里程碑路线图

> 文档状态：实施路线图（2026-09）
> 目标：在现有 Lumen 核心之上，形成一套可供个人工具类应用长期使用的跨平台桌面 GUI。
> 当前策略（2026-09-14 调整）：只规划 Windows/Linux/macOS 桌面端；暂不考虑 Android/iOS。M9 已暂缓，桌面里程碑完成后不自动进入移动端开发，也不预排移动端版本。
> 桌面自用版 M0–M8 已全部收口；后续实施 M10–M13 桌面增强链（见 §3/§4），M9 编号保留为暂缓历史记录。

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
| v0.3 8D | 已完成应用基础组件 | ScrollView/ListView、Form、Dialog、Navigator、settings 示例 |
| v0.3 8E | macOS 桌面接入；另保留历史 SDL-free 实验接缝 | SDL3 桌面 host；`MobileHostSeam` 只代表已有通用状态机，不代表 Android/iOS 支持 |
| 视觉 V1/V2 | 已完成主要状态样式迁移 | token、Theme、StyleResolver、ResolvedStyle、状态与 damage 联动 |
| 自用 M1 | 已完成真实文本与编辑闭环 | SkiaFontManager、shaped TextLayout/RenderCommand、UAX#9 子集、EditingHistory（见 §10 M1 完成记录） |
| 自用 M2 | 已完成应用框架层与 C++ DSL | `lumen-app`（AppShell/runApp）、counter/settings 迁移、C++ builder 补齐（见 §10 M2 完成记录） |
| 自用 M3 | 已完成布局/Grid/VirtualList/Image | Grid/Image/VirtualList 组件、VirtualListController、virtual-list 基准场景（见 §10 M3 完成记录） |
| 自用 M4 | 已完成三桌面平台服务与窗口能力 | 文件选择/OpenURL/通知/光标/图标契约与 SDL/Fake 实现、能力统一报告、settings 服务区（见 §10 M4 完成记录） |
| 自用 M5 | 已完成语义契约与键盘可用性收口 | invalid/hidden flags、语义桥驱动、focusFirstFocusable 焦点恢复、Recording bridge 回归证据（见 §10 M5 完成记录） |
| 自用 M6 | 已完成视觉系统 V3 与控件库 | 图标/阴影/滚动条 token 路径、六控件、ThemeScope、PlatformThemeAdapter、settings 控件+主题页（见 §10 M6 完成记录） |
| 自用 M7 | 已完成 GPU 门槛与性能 | macOS GPU CI/新基准场景/partialSubmit 评估/文本性能修复/Element move 管道；性能门槛 6/6 达标（见 §10 M7 完成记录） |
| 自用 M8 | 已完成三桌面便携发布 | install/CPack/依赖入包/RPATH/CI package job + 解包冒烟（见 §10 M8 完成记录） |
| 增强链 M10 | 已完成动效与滚动体验 | 动画帧调度/整节点透明度转场/状态色过渡/惯性滚动（见 §10 M10 完成记录） |
| 增强链 M11 | 已完成 v0.4 视觉方向与控件体验 | 四方向 Theme 变体/Tooltip hover 延迟/框架级 overlay/Dropdown 浮动菜单（见 §10 M11 完成记录） |
| 增强链 M12 | 已完成平台服务与发布补全 | 系统主题查询/事件、三平台原生通知与强调色、AppImage/.app/包变体（见 §10 M12 完成记录） |
| 增强链 M13 | 已规划未开始 | 原生无障碍 provider（UIA/AT-SPI/NSAccessibility，见 §4） |

M7/M8 当时记录的验证基线：Linux CPU Debug 374/374、Skia Release 380/380、GPU Release 387/387（其中 3 个硬件相关用例按环境跳过），另有历史 mobile-core 356/356。这些数量不是当前提交的复测结果，mobile-core 结果也不代表移动设备验证。Windows/macOS 对应门槛由 CI package/GPU job 负责验证。M7/M8 的跨平台 CI 与便携包 job 已纳入工作流。

关键缺口的源码依据（以文件和符号为准，行号随实现变化不作为稳定引用）：应用主循环和帧管线位于 `examples/counter/counter_app.h::CounterApp::rebuildIfDirty/renderFrame` 与 `examples/settings/settings_app.h::SettingsApp::rebuildIfDirty/renderFrame`；Widget 类型定义位于 `include/lumen/core/widget.h::WidgetType`；C++ DSL builder 位于 `include/lumen/dsl/dsl.h::column/row/container/text/button/text_field`；表单内置校验器位于 `include/lumen/widgets/form.h::FormController::nonEmpty/minLength`；GPU `partialSubmit` 当前在 `src/render/skia_gpu_renderer.cpp::SkiaGpuRenderer::capabilities` 固定为 false；移动接缝实现位于 `src/platform/mobile/mobile_host_seam.cpp::MobileHostSeam`。

### 2.2 仍缺少的能力

| 领域 | 当前实现 | 对自用版的影响 | 计划里程碑 |
| --- | --- | --- | --- |
| 文本 | M1 已完成：`SkiaFontManager`（字体族/weight/回退/度量/shaping，pimpl 无 Skia 类型）+ `TextLayoutResult` shaped run/glyph/cluster/baseline；CPU 占位与 Skia 共享同一契约，缺字体明确诊断 | 复杂脚本合字（HarfBuzz 级）属按需评估池（§4）；默认字体栈和 Gallery 不依赖 M9 | M1 已收口 |
| 编辑 | M1 已完成：`EditingHistory` undo/redo 栈、事务边界、连续输入合并、Ctrl+Z/Shift+Z/Y、IME 提交单事务、preedit 不进栈 | 富文本编辑不纳入第一版 | M1 已收口 |
| 方向文本 | M1 已完成：UAX#9 确定性子集（强/弱/中性类 + L2 重排），混合方向命中测试可靠，grapheme 边界为唯一编辑索引 | 显式嵌入控制/镜像括号/数字定形属按需评估池（§4） | M1 已收口 |
| 应用框架层 | M2 已完成：`lumen-app` 目标（`app::AppShell` + `app::runApp`）统一主循环/事件泵/重建/damage/DPI/IME 同步，支持 Fake host 与外部 renderer 注入；counter/settings 已迁移（示例只保留 build/状态/handler） | — | M2 已收口 |
| DSL | M2 已完成：C++ builder 补齐 stack/checkbox/switch_widget/scroll_view/list_view/focus_scope，与 `.lumen` 冻结节点集对齐（golden 对照测试）；Dialog/Navigator 经 `widgets::makeDialog`/`NavigatorController` 提供 | DSL 可编程性/脚本能力不纳入第一版 | M2 已收口 |
| 控件库 | M6+M11 已完成：Slider/ProgressBar/Radio/Tooltip/Dropdown/Tabs 六控件 + Scrollbar 实绘 + `FormController::compose`；M11 收口 Dropdown 浮动菜单（overlay + 键盘导航）与 Tooltip hover 延迟显隐 | — | M6+M11 已收口 |
| 布局 | M3 已完成：Grid（固定列数/最小列宽自适应/行列间距）与约束传播扩展；Image Widget（占位/位图） | 横向网格/跨行列合并留按需评估（§4） | M3 已收口 |
| 滚动 | M3 已完成：VirtualList（itemCount/itemBuilder/estimatedExtent/stable key/viewport cache；实测 extent 修正与锚点稳定）统一汇入 ScrollController | M10 已收口触摸拖动接线与惯性滚动；水平/嵌套滚动留按需评估 | M3+M10 已收口 |
| 平台服务 | M4+M12 已完成：文件选择/OpenURL/光标/图标（SDL）+ 通知与强调色（M12 原生 seam：Win32/DBus/AppKit）+ 系统主题查询（SDL_GetSystemTheme）与 SystemThemeChanged 事件 + adaptPlatformTheme 保留派生 | Linux/macOS 原生代码以 CI 首跑为事实来源 | M4+M12 已收口 |
| 无障碍 | M5 已完成：语义契约收口（invalid/hidden flags、Image 可访问名、滚动视口隐藏传播）+ AppShell 语义桥驱动（每帧 identity diff/焦点/action 回执）+ FocusScope/焦点恢复（Tab 域内、Escape/返回、modal 关闭后恢复）| UIA/AT-SPI/NSAccessibility 原生 provider 属 M13（Recording bridge 作跨平台回归证据） | M5 已收口，provider M13 |
| 视觉 V3 | M6 已完成：IconId/IconTheme 目录化、ElevationTokens→DrawShadow（Skia blur/CPU 扁平面降级）、ThemeScope 布局期子树覆盖、PlatformThemeAdapter、transitionAlpha 通道 | M10 已收口转场动画驱动（整节点透明度/Dialog/Navigator 过渡/状态色过渡 opt-in）；v0.4 视觉方向属 M11 | M6+M10 已收口 |
| GPU | M7 已完成：macOS GPU 纳入 CI、新基准场景归档、partialSubmit 实测评估（1.16× 维持全帧提交）、文本/布局性能修复 + Element move 管道（快照去子化，reconcile O(n·depth)→O(n)） | 三桌面 GPU CI 首跑为事实来源（macOS job 为本变更新增） | M7 已收口 |
| 发布 | M8+M12 已完成：install/CPack/RPATH/package job + Linux 桌面集成（desktop/icon）与 linuxdeploy AppImage + macOS Lumen.app 骨架 + package-skia（三平台）与 package-skia-gpu（Linux llvmpipe）变体 | Windows/macOS GPU 包与 CPack Bundle 生成器留后续；新形态以 CI 首跑为事实来源 | M8+M12 已收口 |

移动端不列为桌面自用版的能力缺口；已存在的实验接缝和字体代码见 §4 M9 暂缓说明。

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
```

M1–M3 可以并行准备，但必须全部达到各自出口条件后才能进入 M4；之后按 M4 → M5 → M6 → M7 → M8 合入。M0–M8 构成桌面自用版，已全部收口。增强链从 M10 起编号按序推进：M10 → M11 → M12 → M13；M9 编号保留为暂缓历史记录，不在实施链中，M8 完成不触发 M9。M12 与 M11 之间无强依赖，可按实际需要调换顺序；M13 依赖 M11 的高对比主题输出。

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

### M9：Android/iOS 原生接入（暂缓，不在当前范围）

**状态**：2026-09-14 调整为暂缓。保留编号用于解释历史记录，不安排实现任务、
出口条件、移动预览版或后续版本接入时间；M8 完成不触发 M9。

**已有内容的定位**：仓库保留 `MobileHostSeam`、`lumen-mobile-host`、
`LUMEN_BUILD_MOBILE_CORE`、`createMobileFontManager` 和 `FontBackend::Mobile`。
这些是历史实验代码；字体族枚举、默认字体栈或目录查询能力不能证明移动端文本
输入、字形绘制、原生 host 或发布链路已经可用。桌面默认字体和 Gallery 工作按
桌面功能验收，不作为 M9 进度。

**当前约束**：不新增 Android/iOS 工程、软键盘、移动字体管线、移动示例、原生
无障碍或移动 GPU/发布任务；不要求模拟器或真机验证。已有 Linux/macOS
`mobile-core` CI job 仍按现有工作流运行，只验证 SDL-free 通用代码的兼容性，
不作为移动产品验收。本次范围调整不删除代码、构建目标、测试或 CI job。

只有用户重新提出移动端需求后才重新评估范围和架构；旧方案不作为待执行清单。

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
- `prefersDarkMode`/`accentColor` 系统查询（Win32 注册表/SystemParametersInfo、DBus org.freedesktop.appearance、NSAppearance）接入 `PlatformThemeAdapter`。
- 窗口级光标（Win32 `WM_SETCURSOR` 等，替换进程级 `SDL_SetCursor` 语义）。
- 发布形态：Linux AppImage（linuxdeploy）、macOS 完整 `.app` bundle（CPack Bundle/Info.plist）、CI package job 增加 Skia/GPU 包变体。

**出口条件**：服务失败仍结构化降级且 UI 线程不阻塞；三平台新形态在 CI 解包/启动冒烟通过；能力报告如实反映。

### M13：原生无障碍 provider

**目标**：把语义契约接到三平台原生屏幕阅读器（UIA/AT-SPI/NSAccessibility）。

**实现**

- 按 `AccessibilityBridge` 契约实现三平台 provider，`createPlatformAccessibilityBridge` 工厂按平台分发；消费预留开关 `LUMEN_ENABLE_ACCESSIBILITY_BRIDGE`。
- AT 事件回灌走既有 `performAccessibilityAction` 路径；`PlatformCapabilities.accessibility` 如实置位。
- 依赖 M11 的高对比主题与 M10 的 reduceAnimation 语义输入。

**出口条件**：每平台至少一款屏幕阅读器完成焦点导航/激活/值设置回环；Recording bridge 回归不受影响；未启用开关时行为与现状一致。

### 后续按需评估池（不预排编号）

以下方向在 M13 之后按实际需要评估，不承诺版本：

- HarfBuzz 级合字与富文本：Skia 预编译包已附带 skshaper/harfbuzz/icu 归档，`FontManager::shapeCluster` 接口已预留多 glyph 返回。
- 多窗口：`runApp` 按 `event.window` 路由多个 AppShell 实例；宿主层多窗口 API 已齐备。
- 完整 UBA：显式嵌入控制、镜像括号、数字定形。
- 桌面专用 GPU 后端（Graphite/Vulkan/Metal/D3D）。

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
| 范围膨胀 | 桌面任务被未确认的平台扩展拖慢 | 当前范围为三桌面；M9 暂缓，不因已有移动实验代码而扩展任务 |

## 8. 版本切分和停止条件

- **桌面自用版**：完成 M0–M8（已收口）。M5 的语义契约和 Recording bridge 已完成；原生桌面 accessibility provider 由 M13 追加。
- **后续桌面增强版**：按 M10–M13 增强链实施——M10 动效与滚动体验、M11 v0.4 视觉方向与控件体验、M12 平台服务与发布补全、M13 原生无障碍 provider；HarfBuzz 富文本、多窗口、完整 UBA、专用 GPU 后端留在按需评估池（§4）。业务数据与网络仍由应用层负责。
- **移动端**：暂不规划版本；M9 保留为暂缓记录。

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
    度量旧路径；caret/选区几何仍出自布局。
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
- 测试：
  - 新增 `tests/app_shell_tests.cpp`（8 用例）：Fake host 驱动 runApp 的
    事件顺序/状态流转、指针+文本进入焦点字段、关闭请求消费/退出、
    IME 会话启停与候选框锚点同步、dirty 合并与绘制缓存命中、视口/
    DPI 变化重建、renderer 替换与缓存失效、渲染器失效钩子回退。
  - 新增 DSL 全节点 golden 对照（冻结节点集 + 常用属性逐字段相等）与
    FocusScope/修饰器覆盖用例。
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
  - runApp 为单窗口主循环（多窗口属后续里程碑；事件不按 windowId 过滤）。
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
  - UIA/AT-SPI/NSAccessibility 原生 provider 未实现（工厂继续返回
    nullptr + 诊断；第一版便携发布不阻塞，后续版本）。
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
    抗锯齿近似）。
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
    （begin 可早于含该子树的首次重建）；完成回调后一拍退休，保证终值
    有一次提交机会。
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
  - `advanceTransitions` 退休注释精确化：进场终值有一拍提交机会；退场
    onComplete 同拍移除子树（重建后 identity 缺失即清除，终值
    alpha≈0 与不画等价）。
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

### 范围调整记录（2026-09-14）

- 当前 UI 只面向 Windows/Linux/macOS；M9 暂缓，不列入待实施里程碑。
- v0.3 8E 和视觉系统 V4 的规划统一为桌面范围，Android/iOS 不再承担版本出口条件。
- 原有 M0–M8 完成记录中的 mobile-core 数字保留为当时的实验配置验证记录，
  不表示移动平台已完成，也不表示本次复测通过。
- 删除与既有 M5 完成记录冲突的“未开始”占位项；已有实现不因本次文档调整回退。
