# Lumen 桌面平台支持矩阵

> 状态：2026-09-30 核对；保留 M0 四态定义，当前能力覆盖 M0–M8、M10–M18 实现批次（M15 拖放、M16 窗口与系统集成、M17 控件细节池、M18 开发者诊断首批），各批次四态见下方「实现批次与真实平台缺口」；M9 冻结，不计入当前完成范围。三平台屏幕阅读器回环仍需真实桌面验收。
> 后续完善顺序、缺口编号（R0–R10）与验收模板以 [`lumen-gui-completion-plan.md`](lumen-gui-completion-plan.md) 为任务入口；待现场验收项逐条登记于 [`platform-acceptance.md`](platform-acceptance.md)。
> 当前产品范围（2026-09-14）：Windows/Linux/macOS 桌面；Android/iOS 暂不实现并冻结，M9 仅保留历史编号。
> 构建命令与系统依赖的单一事实来源是
> [`build-commands.md`](build-commands.md) 与 `.github/workflows/`。
>
> 面向自用工具类应用的后续里程碑见
> [`lumen-self-use-roadmap.md`](lumen-self-use-roadmap.md)。
> 性能基线见 [`perf-baselines/README.md`](perf-baselines/README.md)。

## 能力四态定义（M0 冻结）

后续里程碑必须用以下四种状态描述能力，不得把“接口存在”标记为“平台完成”：

| 状态 | 含义 | 证据要求 |
| --- | --- | --- |
| 接口已存在 | 公共头文件冻结，Fake/Recording 可断言 | 头文件 + headless fake 测试 |
| headless 已验证 | 确定性单测/headless 集成通过 | `ctest` 全量通过（含 Fake host/Recording bridge） |
| 真实平台已验证 | 在目标 OS/驱动/输入法上 smoke 通过 | 窗口 smoke、真实输入法/剪贴板/GPU 呈现记录 |
| 可发布 | 便携包可在干净机器运行并可追溯 | install 产物 + 解包启动 + 版本/commit 追溯 |

## 工具链与依赖基线（M0 冻结，三桌面 CI）

| 平台 | runner | 编译器 | 系统依赖安装方式 |
| --- | --- | --- | --- |
| Windows | `windows-2025` | MSVC（runner 自带工具链，具体版本以构建日志为准；`/utf-8 /W4`，Skia 时强制静态 CRT/MT，Release） | 无额外系统包；Skia 预编译自动拉取 |
| Linux | `ubuntu-24.04` | GCC（系统默认，`-Wall -Wextra -Wpedantic`；Skia Release 经 clang-12 官方构建包链接） | `LUMEN_LINUX_DEPS`（见 `linux.yml` env，与 `build-commands.md` 同源）+ GPU job 追加 `libgl1-mesa-dri mesa-utils` |
| macOS | `macos-15` | AppleClang（Xcode CLT） | 无额外系统包；SDL3 经 FetchContent 编译 |

固定第三方版本（`cmake/dependencies.cmake`）：SDL3 `release-3.2.10`、
Catch2 `v3.8.1`、stb `2c980bb59875b0d32144a71867fbdebb2f77cd20`、
Skia `m124-08a5439a6b`（Windows/Linux/macOS 预编译 Release 包；macOS 按 arm64/x64 选包）、
zlib `v1.3.1`（仅 Windows Skia）。新增 FetchContent 依赖时固定版本、
记录许可证，并说明 CPU-only 行为。

## 桌面平台

| 平台 | 窗口/输入 | 剪贴板 | 文本/IME | 无障碍 | 渲染 | CI 验证 |
| --- | --- | --- | --- | --- | --- | --- |
| Windows | SDL3（宿主多窗口、resize/DPI、触摸 pointer id；`runApp` 按 `WindowId` 隔离多个 `AppShell`） | SDL3 剪贴板（`platform::Clipboard`） | UTF-8 commit + IME preedit（TSF 经 SDL）；修饰键/逻辑键归一化 | 语义树 + Recording 桥；UIA 已实施，需 `LUMEN_ENABLE_ACCESSIBILITY_BRIDGE=ON`；未编入时能力报告 false | CPU、Skia 光栅、Skia GPU（软件回退见下表） | `windows.yml`：cpu / a11y-bridge / skia-raster / skia-gpu / package / package-skia / package-skia-gpu；platform-acceptance 的登录 Win32 runner 负责透明合成、字体冷启动、触摸板和 Narrator/NVDA |
| Linux | SDL3（X11/Wayland） | 同上 | UTF-8 + IBus/Fcitx preedit（候选词锚点经 `SDL_SetTextInputArea`） | 语义树 + Recording 桥；AT-SPI2 provider 已编入（需桌面总线），Orca 回环待验 | 同上 | `linux.yml`：cpu / skia / skia-gpu / package / package-skia / package-skia-gpu；`.github/workflows/platform-acceptance.yml` 的 self-hosted X11/Wayland job 负责真实窗口、IME、剪贴板、GPU/present 故障注入 |
| macOS | SDL3（菜单关闭经统一关闭规则） | 同上 | UTF-8 + 输入法 preedit（经 SDL） | 语义树 + Recording 桥；NSAccessibility provider 已编入（需 NSWindow），VoiceOver 回环待验 | 同上 | `macos.yml`：cpu / skia-gpu / package / package-skia / package-skia-gpu；platform-acceptance 的登录 Aqua runner 负责真实 IME、生命周期、透明窗口和 VoiceOver |

三平台共用：`ApplicationHost` 契约、归一化 `HostEvent`（时间戳/修饰键/
逻辑与物理键/指针设备/pointer id/滚轮/取消/关闭请求）、语义树与 action
分发、`lumen-text` 编辑模型。counter/settings 示例三平台同源。SDL 宿主在
初始化及运行期间约每秒查询 `PlatformCapabilities.highContrast`、
`reduceAnimation`、`fontScale`：Linux 使用异步 portal D-Bus settings，macOS 使用
AppKit display preferences/preferred body font，Windows 使用 SystemParametersInfo
与 Accessibility 注册表；查询不可用时保留默认值或上次有效快照。变化经
`SystemAccessibilityChanged` 广播，`runApp` 首帧前及事件到达时自动应用到全部
跟随窗口，应用逐项显式覆盖优先，详见 [视觉系统 §4.2](lumen-visual-system-design.md#42-系统可访问性偏好与应用覆盖)。

Linux 优先读取 [portal 标准键](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html)
`contrast`/`reduced-motion`，再回退到 GNOME 键；字体缩放使用后端暴露的 GNOME
`text-scaling-factor`。未暴露该键的桌面保留 `1.0`，不宣称覆盖所有桌面设置。
使用 `ReadAll` 避免旧 `Read` 的双层 variant 陷阱，解析仍容忍嵌套 variant；
非法/非有限缩放回退 `1.0`。portal 服务重启可重新采样，会话总线断开后须重新初始化
宿主。macOS preferred body font 需 11.0，减少动态效果查询需 10.12，低版本
保留对应默认值；这不是任意应用字体设置的全局缩放接口。

### 验证快照（行内标注日期与源码基线）

| 环境 | 已核对证据 | 验证边界 |
| --- | --- | --- |
| 本地 Linux / GCC / Debug（2026-09-30，源码基线 `3fede85215551c75552da3f007fa4db7296ba95e`） | 全量 `ctest` 935/935 通过（含 M15–M18 批次与 Gallery DataGrid 工作台用例；默认 OFF 的 GPU/Skia/原生无障碍开关除外） | 本地无桌面会话，不产生真实平台证据；不据此提升任何「真实平台已验证」状态，也不更新支持矩阵三平台行 |
| 本地 Windows / VS 2026 / CPU | Debug 与 Release 构建成功；全量 CTest 各 779/779，通过；日志在 `build-debug/Testing/Temporary/LastTest.log`、`build-release/Testing/Temporary/LastTest.log` | 本次测试使用默认 OFF 的 GPU、Skia 和原生无障碍开关；包含历史移动接缝回归，不代表移动设备验收；测试代码仍有 MSVC 警告 |
| Windows CI | [windows 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116368) 全部 job 成功，含 UIA 开关 ON + live smoke、Skia/GPU 与打包 | UIA 客户端冒烟不替代讲述人/NVDA；GPU 不可用时用例可跳过，不能仅凭绿色 job 认定实际 GPU 提交 |
| Linux CI | [linux 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116406) 全部 job 成功，含 Xvfb/llvmpipe GPU、CPU/Skia 包与 AppImage 构建 | GPU 窗口 smoke 显式断言 `backend=skia-gpu`；不替代 Wayland、真实输入法/触控板/合成器及干净桌面包验收 |
| macOS CI | [macos 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116325) 全部 job 成功，含 Skia/GPU、CPU `.app` 骨架与 Skia zip | GPU 探测不可用时测试可跳过，窗口命令成功退出本身不排除软件回退；不替代 VoiceOver 与人工窗口验收 |

以上结果仅属于该源码提交，不能作为后续提交自动通过的证据。支持矩阵中的能力、
自动化覆盖和人工验收分别记录：M7 的标准 runner 必须实际提交 GPU 帧，Windows/macOS
工作流尚未像 Linux 一样强制断言这一点。当前性能 CI 已检查 CPU/Skia/GPU
五类固定场景的 p50/p95、submit/GPU wait、分配量及命令数，门槛为不超过 10%；
固定基准提交与候选在同一 runner 交替实测，双方三次取中位数并归档完整元数据。
旧 `working-tree` 基线仅保留为历史样本，不参与门槛；headless 耗时不包含 present。

### 实现批次与真实平台缺口（2026-09-30）

按 [`lumen-gui-completion-plan.md`](lumen-gui-completion-plan.md) 的缺口编号
（R0–R10）汇总 2026-09-29/30 M15–M18 实现批次的四态。实现已交付不等于平台
完成；「待现场」条目的平台、原因、降级与后续归属逐条登记于
[`platform-acceptance.md`](platform-acceptance.md) 的待验收登记表。

| 缺口 | 四态（2026-09-30） | 说明 |
| --- | --- | --- |
| R0 三桌面真实验收 | 接口已存在 + headless 已验证 | Linux X11/Wayland 有部分回环与浸泡记录；Windows/macOS 读屏、IME、跨应用剪贴板、透明合成、浸泡待现场 |
| R1 发布与安装收口 | Linux 链条较完整；Windows/macOS 部分 | 三平台 package/package-skia/package-skia-gpu CI 变体可构建；Windows/macOS 干净机器安装、启动、升级/卸载未记录 |
| R2 生产生命周期 | headless 已验证 | 恢复用例/资源重排队/字体异步生命周期有 headless 断言；Linux 1h 浸泡已有，Windows/macOS 浸泡待现场 |
| R3 OS 拖放 | 接口已存在 + headless 已验证 | OS 拖入归一化事件与应用内重排/列拖序 headless 契约通过；三桌面真实拖入 smoke 待现场（`drag_drop_os_receive` 已纳入必检清单）；拖出结构化不可用（SDL 3.2.10） |
| R4 桌面系统集成 | 部分接口已存在 | 全屏/置顶/OS 模态/托盘契约与 SDL 实现已有（headless 已验证）；全局快捷键 Linux X11 后端已交付（XGrabKey 独立连接，Xvfb XTEST 端到端通过；Wayland 结构化不可用；Win32/macOS 后端未实现——能力位 false + 结构化 Unavailable）；macOS 原生菜单栏/交通灯未实现 |
| R5 文本与剪贴板深度 | headless 已验证 | G-3 剪贴板 MIME 数据层/图片与自定义格式/变更广播有 headless 断言；三桌面真实 IME 与跨应用复制粘贴待现场 |
| R6 开发者诊断 | dump 与调试图层已交付（headless 已验证） | `dumpRenderTree`/`dumpSemanticsTree`/`dumpStyleTree` + settings/gallery 三旗标 + 帧读数 HUD + bounds/damage 调试图层（`--bounds-overlay`/`--damage-overlay`；纯绘制零额外帧）2026-09-30 交付；分配量维度未接入（renderer stats 无该维度）；inspector 交互式节点查看未做 |
| R7 控件细节 | 按需池交付中 | 已交付 auto-hide 滚动条、Splitter 塌缩/KeepRatio、DataGrid 筛选接线、可编辑 ComboBox、DialogHost 便利层、ColorPicker、Grid 跨行列、菜单 F10/裸 Alt 单键切换 + 打开态 Alt+mnemonic 顶级切换、List/Tree 行内编辑（2026-09-30）；RTL 镜像、双轴联滚、触摸长按唤起等在池 |
| R8 复杂文本 | 未启动（按需） | 保持 UAX#9 子集 + 逐 grapheme shaping；HarfBuzz/完整 UBA/TextSpan 待产品需求触发 |
| R9 框架使用效率 | 部分交付 | `examples/template` 脚手架与 Gallery 样本已有；页面壳/状态摘要等组合组件未提取 |
| R10 文档与证据同步 | 进行中 | 本表与待验收登记即该缺口的 2026-09-30 批次；后续状态变化须同一变更内更新 |

### 预乘 alpha 联调历史记录（2026-09-20）

2026-09-20 预乘 alpha 联调补充：CPU 帧/缓存与 Skia 读回准确声明
Premultiplied / Opaque；三种图片输入模式可跨后端及设备重上传。
Windows direct3d11 texture 与原生 software 的 resize/最小化恢复已经实测。
但 Windows 原生 software 为 XRGB8888，**不支持逐像素透明**；present 成功
仅代表提交。Windows texture 的合成器视觉、Linux X11/Wayland 和 macOS
桌面透明验收仍待运行；固定 SDL 在 Wayland/macOS 没有严格 native software
framebuffer，当前无 GPU 的软件窗口回退仍有限制。证据与逐路径说明见
[P3 联调记录](perf-baselines/premultiplied-alpha-2026-09-20/P3.md)，不据此提升三平台发布状态。

P4/P5 交付验证：Windows CPU Debug/Release 各 755/755、Skia raster Release 768/768、
GPU Release 779/779，无跳过；GPU 三模式图片经历真实上下文销毁重建及 CPU 重上传。
这不是 Linux 运行时 GPU/present 故障注入，该轮未验该路径。62 个视觉样本在统一预乘表示下
alpha 精确一致，普通 RGB 最大差 2；CPU/Skia 各三组正式配对及收益/代价见
[P4 报告](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)。GPU 性能未测。
公开像素结构需要重编译，`pixels()` 返回实际模式；命令写 v7、读 v6/v7，
Gallery 默认导出保持 straight，见 [迁移说明](lumen-alpha-migration.md)。

同日续验补充 Linux **WSL Ubuntu 24.04 / GCC 13.3 CPU Debug** 构建与 headless/dummy 测试：
754 项中 753 通过、1 项因无系统字体跳过、0 失败；修复非 Apple 构建误编 macOS `.mm`
文件及通知能力的旧测试假设。没有 Linux 桌面会话、Skia/GPU 或合成器新证据，
上述实窗待验项不变。Windows CPU Release 756/756（含一个基准工具 CTest）、
Skia/GPU Release 779/779，无跳过；详见
[续验记录](perf-baselines/premultiplied-alpha-2026-09-20/follow-up.md)。

## 历史移动实验内容（暂不实现，已冻结）

| 平台 | 当前范围 | 已有代码 | 验证边界 |
| --- | --- | --- | --- |
| Android | 暂不实现，已冻结 | 保留 `lumen-mobile-host` 通用状态机；JNI/NativeActivity 胶水不实现 | Linux 的 mobile-core job 只检查 SDL-free 通用代码，不是 NDK 或设备验证，也不是任务入口 |
| iOS | 暂不实现，已冻结 | 保留同一 `MobileHostSeam` 状态机；Objective-C++ 胶水不实现 | macOS 的 mobile-core job 只检查 SDL-free 通用代码，不是 iOS 工程或 Simulator 验证，也不是任务入口 |

原生接入、软键盘、移动字体/绘制管线、移动页面、GPU、无障碍与发布均不在当前
设计和验收范围，不进入当前或后续自动计划。已有 `createMobileFontManager`、
`FontBackend::Mobile` 和默认字体栈分支也不能作为移动端文本可用的证据。

现有源码、构建开关、测试和 Linux/macOS mobile-core CI job 可作为历史兼容性检查保留；
不得由此新增移动实现任务。桌面触屏、Touch density、
窄窗口和通用安全区指标仍属于桌面可用性设计。

## 后端与能力

| 能力 | 提供方 | 降级行为 |
| --- | --- | --- |
| CPU 光栅 | `CpuRenderer`（窗口经系统字体绘制真实字形，headless/无字体时确定性占位） | 无需降级；CPU-only 构建不依赖 SDL 实现库与桌面会话 |
| Skia 光栅 | `SkiaRenderer`（可选 `LUMEN_ENABLE_SKIA`） | 未编入时能力报告 `backendName=cpu`，应用安全运行 |
| Skia GPU | `SkiaGpuRenderer`（可选 `LUMEN_ENABLE_GPU`） | counter 的探测/初始化失败路径回退 CPU；运行时故障在 Skia 构建中改用 Skia 软件光栅以保持文本连续性；回退钩子和诊断见路线图 P3 |
| 文本 shaping | `lumen-text` + `SkiaFontManager`（可选 `LUMEN_ENABLE_SKIA`，封装字体族/回退/度量/shaping；公共接口无 Skia 类型）或 `SystemFontManager`（CPU 窗口默认：Windows 优先 GDI 系统字形，其他平台及 GDI 回退经 stb_truetype 读取系统字体，Windows 雅黑优先） | CPU-only 构建或无系统字体时回退 `PlaceholderFontManager`，布局/编辑照常（`fontDiagnostic`/工厂诊断明确报告）；布局与绘制共享同一份 shaped 结果，光标/选区不跨后端漂移 |
| 编辑撤销 | `text::EditingHistory` + `InteractionController`（Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y；连续单字输入/删除合并，IME 提交为单事务，preedit 不进栈） | 只读字段与 preedit 期间拒绝撤销；栈按字段 bind 隔离，容量 100 |
| 应用壳 | `lumen-app`（`app::AppShell` 帧管线 + `app::runApp` 单/多窗口主循环；M2） | 示例只保留 build/状态/handler；支持 Fake host、外部 renderer 和应用回退钩子；GPU 失效可重建软件窗口 |
| 剪贴板 | `platform::Clipboard` / `core::ClipboardProvider` | runApp 启动时接入宿主剪贴板（Ctrl+C/V；宿主不可用保持未注入）；`setText` 失败返回 false，编辑状态不丢 |
| 语义桥接 | `AccessibilityBridge`（接口 + Recording 桥 + AppShell 每帧 identity diff/焦点/action 回执驱动，M5 收口） | Windows UIA、Linux AT-SPI2、macOS NSAccessibility provider 在可选编译开关开启时编入；无桌面服务时能力如实降级，真实屏幕阅读器回环另行验收 |
| 可访问性设置 | `PlatformCapabilities`、`AccessibilitySettings` 与逐项 `AccessibilityOverrides` | SDL 宿主初始化和运行期间查询；`runApp` 自动跟随所有未覆盖字段，重复快照不重绘；关闭跟随、服务缺失和平台键缺失均有明确降级 |

## 当前能力与已知限制（映射到自用路线图里程碑）

- 双向文本为 UAX#9 确定性子集（强/弱/中性类近似，无显式嵌入控制、镜像
  括号与数字定形）：纯 RTL/LTR 与常见混合段落正确，完整 UBA 属后续版
  本；grapheme 边界仍是唯一编辑索引（M1 已收口，见 M1 完成记录）。
- Skia shaping 为逐 grapheme cluster（无 HarfBuzz 连写/合字）：拉丁/
  CJK/希伯来/阿拉伯基本形正确，复杂脚本合字后续增强。
- VirtualList 已落地（M3：可见区物化/实测 extent 修正/锚点稳定），虚拟化仍为
  纵向；M10 已接入触摸/指针拖动与确定性惯性滚动。ScrollView 水平轴、
  横纵滚动条拖动与嵌套滚轮路由已实施；同一视口双轴联滚、水平虚拟化、
  RTL 镜像和 auto-hide 未实现。Gallery 已有超宽卡片水平滚动演示区，详见
  [滚动设计](lumen-scroll-design.md)。
- Grid 为纵向网格（无横向滚动）；子项可声明跨行列（`withGridSpan`，
  阶段C 2026-09-30——占位流式放置 + 跨行差额入末跨行，span=1 与 M3
  基线逐字节同几何，见
  [Grid 跨行列设计](lumen-grid-span-design.md)）；Image 需应用侧资源管理器
  驱动加载（框架不管理异步资源生命周期）。
- 平台原生无障碍 provider 已在 M13 编入三桌面目标并有 headless 回归；Linux
  AT-SPI2、macOS NSAccessibility 和 Windows UIA 的真实屏幕阅读器人工回环
  仍需单独验收，不能由 Xvfb 或 headless CI 代替。
- 应用主循环/damage 管线已收敛到 `lumen-app` 应用壳（M2）：新工具页只需
  提供 build/状态逻辑；`runApp` 支持单窗口兼容入口和多个 `AppWindow` 绑定，
  按 `HostEvent.window` 隔离输入、DPI、IME、renderer、语义桥和帧调度。
  宿主窗口生命周期仍由调用方拥有；最后一个运行时关闭后主循环结束。
- Splitter 塌缩/KeepRatio（M17，2026-09-30）：`setCollapsible/setCollapsed`
  （塌缩钉 minLeading，移离 min 自动解除）+ Enter/Space 切换（不可塌缩
  回退激活路径）；ResizeBehavior 双行为（KeepRatio 等比缩放）。
- DataGrid 筛选接线（M17，2026-09-30）：`requestFilter`（编辑守卫入口）+
  `setFilterActive`（无结果空态文案切换）；筛选面板 UI 仍由应用提供。
- headless 语义树导出（M18，2026-09-30）：`buildSemanticsSnapshot` +
  `dumpSemanticsTree` + settings `--dump-semantics`（与 --dump-tree 同链）。
- auto-hide 滚动条（M17，2026-09-29）：`withAutoHideScrollbar` 声明式开启；
  滚动活动（滚轮/键盘/拖动/惯性）打开 800ms 可见窗口（
  MotionTokens.scrollbarAutoHideMs），到期隐藏（`scrollbarHidden` 绘制跳过，
  命中区保留——悬停重显）；reduceAnimation 不归零（可发现性行为）。
- headless 树导出（M18 首块，2026-09-29）：`app::dumpRenderTree`（确定性）
  + settings `--dump-tree`；语义 dump/inspector/帧统计 overlay 为后续增量。
- 帧读数 HUD 与样式导出（R6 首批，2026-09-30）：`dumpStyleTree` +
  settings/gallery `--dump-style`（与 --dump-tree 同树同序，`style:` 前缀
  行 + `#rrggbbaa` 颜色）；gallery 补齐 `--dump-tree`/`--dump-semantics`
  平价；`app::makeFrameStatsOverlay` + `RunOptions.frameDebugOverlay`
  （settings/gallery `--frame-overlay`）——reconcile/layout/paint/submit/
  GPU wait、fps、节点/命令数与当前 renderer，读数滞后一帧，全子树排除
  语义与焦点；默认关闭零额外帧、frame hash 不变（单测断言）。分配量
  维度未接入；inspector 与 bounds/damage overlay 仍为后续增量。
- 窗口与系统集成（M16 实现批次 2026-09-29）：全屏（`toggleFullscreen` +
  WindowFullscreenEntered/Exited 事件）、置顶、OS 模态（SDL_SetWindowParent
  + SetWindowModal）契约与三桌面能力位；系统托盘经 SDL_tray（菜单激活回灌
  TrayActivated）；windows/macos 新增 package-skia-gpu 包变体。当前四态 =
  接口已存在 + headless 已验证（Linux）；真实平台 smoke 与 macOS 原生
  菜单栏/交通灯未做。
- 全局快捷键 X11 后端（R4/M16 增量，2026-09-30）：`global_hotkeys_x11`
  内部接缝——独立 X 连接 `XGrabKey`（锁定键 8 组合各 grab、事件匹配剔除
  锁定掩码；BadAccess 临时错误处理器 + XSync 检测冲突并回滚）；事件经
  `pollEvent` 转 `GlobalHotkey`（text=注册 id，window=owner）。Wayland
  会话（XDG_SESSION_TYPE/WAYLAND_DISPLAY 探测）结构化不可用——XWayland
  grab 只覆盖 X11 客户端，如实不宣称系统级。当前四态 = 接口已存在 +
  headless 已验证（映射纯函数单测）+ X11 协议级已验证（Xvfb XTEST 端到
  端 `LUMEN_GLOBAL_HOTKEY_E2E=1`：注册→合成按键→事件送达→注销不泄
  漏）；Win32/macOS 后端未实现（能力位 false + 结构化 Unavailable），
  真实桌面（非 Xvfb）按键验收待现场。
- 拖放（M15 实现批次 2026-09-29）：OS 拖入经 `HostEvent` DragEnter/
  Move/Drop/Leave 归一化交付（SDL DROP_* 翻译；COMPLETE 无负载合成
  DragLeave），`startDrag` 结构化 Unavailable（SDL 3.2.10 无拖出 API，
  `dragDropStart` 能力位如实 false）；应用内 List/DataGrid 行重排与
  DataGrid 列拖拽经 core 会话状态机（arm/阈值/仲裁表），Alt+↑/↓ 与语义
  MoveUp/MoveDown 提供非指针路径。当前四态 = 接口已存在 + headless 已
  验证（契约见 [`lumen-drag-drop-design.md`](lumen-drag-drop-design.md)）；
  三桌面真实 OS 拖入 smoke 未做，拖出待 SDL 升级。
- 平台服务已统一接口（M4：文件选择/OpenURL/通知/光标/图标 + 能力报
  告）；M12 已收口原生后端——通知与强调色走 native seam（Win32/
  DBus/AppKit），SDL 系统主题输入经 `SDL_GetSystemTheme` +
  `SystemThemeChanged` 事件接入；当前三平台 CI 证据见上方验证快照。高对比/
  减少动画/字体缩放已接入独立采样与变化广播，应用逐项覆盖优先；实际 OS
  设置面板的人工切换验收仍需真实桌面，隔离 portal/假宿主测试不代替它。
- M6 视觉系统 V3 已收口（图标/阴影/滚动条 token 路径、六控件、
  ThemeScope、组合校验器）；M10 已收口转场动画驱动（transitionAlpha
  整节点透明度、Dialog/Navigator 淡入淡出、状态色过渡 opt-in）；
  M11 已收口 Tooltip hover 延迟显隐与 Dropdown 浮动菜单
  （框架级 overlay + Up/Down/Enter/Esc 键盘导航）。
- M11 v0.4 视觉方向已收口：ThemeDirection 四方向（CoreDark 默认 /
  InkLinen / AuroraSignal / UtilityContrast），方向与深浅/高对比/字体
  缩放/密度正交组合经 fromSettings 派生；Aurora 为扁平近似（玻璃/
  渐变/光晕不做，见 roadmap M11 已知限制）。
- M7 实现已交付：三平台 GPU CI 已配置；partialSubmit 历史评估为 1.16×，
  当前仍报告全帧提交。Element move 管道当时的性能门槛为 6/6；该历史
  结果不代表当前持续性能门槛，实际 GPU 提交与性能 CI 缺口见验证快照。
- M8+M12 便携发布已收口：install/CPack/CI package job + 解包冒烟 +
  Linux AppImage（linuxdeploy）与 macOS Lumen.app 骨架 + package-skia
  （三平台）/package-skia-gpu（Linux llvmpipe）变体；当前 CI 运行成功，
  Windows/macOS GPU 包与 CPack Bundle 留后续。AppImage 生成和 `.app` 内
  headless 启动不替代干净桌面人工启动验收。当前安装清单已覆盖
  `lumen-render` 与其余桌面公共静态库，并随开发归档安装
  `LumenConfig.cmake`/`LumenTargets.cmake`（包含 SDL3 头文件和运行时目标）；
  因此运行时 AppImage/`.app` 与外部 SDK 的职责保持分离。
- 移动方向暂不实现并冻结；M9 只保留状态说明，不作为桌面版本的待完成项。
