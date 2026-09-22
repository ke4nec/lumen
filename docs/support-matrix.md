# Lumen 桌面平台支持矩阵

> 状态：2026-09-22 核对；保留 M0 四态定义，当前能力覆盖 M0–M8、M10–M13 provider 实施与按需控件增强；M9 冻结，不计入当前完成范围。三平台屏幕阅读器回环仍需真实桌面验收。
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
| Windows | SDL3（宿主多窗口、resize/DPI、触摸 pointer id；`runApp` 按 `WindowId` 隔离多个 `AppShell`） | SDL3 剪贴板（`platform::Clipboard`） | UTF-8 commit + IME preedit（TSF 经 SDL）；修饰键/逻辑键归一化 | 语义树 + Recording 桥；UIA 已实施，需 `LUMEN_ENABLE_ACCESSIBILITY_BRIDGE=ON`；未编入时能力报告 false | CPU、Skia 光栅、Skia GPU（软件回退见下表） | `windows.yml`：cpu / a11y-bridge / skia-raster / skia-gpu / package / package-skia |
| Linux | SDL3（X11/Wayland） | 同上 | UTF-8 + IBus/Fcitx preedit（候选词锚点经 `SDL_SetTextInputArea`） | 语义树 + Recording 桥；AT-SPI2 provider 已编入（需桌面总线），Orca 回环待验 | 同上 | `linux.yml`：cpu / skia / skia-gpu / package / package-skia / package-skia-gpu；窗口自动化使用 Xvfb + llvmpipe |
| macOS | SDL3（菜单关闭经统一关闭规则） | 同上 | UTF-8 + 输入法 preedit（经 SDL） | 语义树 + Recording 桥；NSAccessibility provider 已编入（需 NSWindow），VoiceOver 回环待验 | 同上 | `macos.yml`：cpu / skia-gpu / package / package-skia |

三平台共用：`ApplicationHost` 契约、归一化 `HostEvent`（时间戳/修饰键/
逻辑与物理键/指针设备/pointer id/滚轮/取消/关闭请求）、语义树与 action
分发、`lumen-text` 编辑模型。counter/settings 示例三平台同源。

### 验证快照（2026-09-22，源码基线 `41cd603831eaed37558ebb32ceddebc8f93cff4f`）

| 环境 | 已核对证据 | 验证边界 |
| --- | --- | --- |
| 本地 Windows / VS 2026 / CPU | Debug 与 Release 构建成功；全量 CTest 各 779/779，通过；日志在 `build-debug/Testing/Temporary/LastTest.log`、`build-release/Testing/Temporary/LastTest.log` | 本次测试使用默认 OFF 的 GPU、Skia 和原生无障碍开关；包含历史移动接缝回归，不代表移动设备验收；测试代码仍有 MSVC 警告 |
| Windows CI | [windows 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116368) 全部 job 成功，含 UIA 开关 ON + live smoke、Skia/GPU 与打包 | UIA 客户端冒烟不替代讲述人/NVDA；GPU 不可用时用例可跳过，不能仅凭绿色 job 认定实际 GPU 提交 |
| Linux CI | [linux 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116406) 全部 job 成功，含 Xvfb/llvmpipe GPU、CPU/Skia 包与 AppImage 构建 | GPU 窗口 smoke 显式断言 `backend=skia-gpu`；不替代 Wayland、真实输入法/触控板/合成器及干净桌面包验收 |
| macOS CI | [macos 运行记录](https://github.com/ke4nec/lumen/actions/runs/35684116325) 全部 job 成功，含 Skia/GPU、CPU `.app` 骨架与 Skia zip | GPU 探测不可用时测试可跳过，窗口命令成功退出本身不排除软件回退；不替代 VoiceOver 与人工窗口验收 |

以上结果仅属于该源码提交，不能作为后续提交自动通过的证据。支持矩阵中的能力、
自动化覆盖和人工验收分别记录：M7 的标准 runner 必须实际提交 GPU 帧，Windows/macOS
工作流尚未像 Linux 一样强制断言这一点。性能 CI 当前检查 CPU 帧哈希重复性并上传报告，
尚未自动执行同后端/同场景 p50/p95 退化不超过 10% 的门槛。

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
| 可访问性设置 | `PlatformCapabilities` 字段与应用 `AccessibilitySettings` | 应用设置可驱动高对比/减少动画/字体缩放；SDL 宿主尚未查询系统对应偏好，能力字段保持 false/false/1.0，不能声明自动跟随系统 |

## 当前能力与已知限制（映射到自用路线图里程碑）

- 双向文本为 UAX#9 确定性子集（强/弱/中性类近似，无显式嵌入控制、镜像
  括号与数字定形）：纯 RTL/LTR 与常见混合段落正确，完整 UBA 属后续版
  本；grapheme 边界仍是唯一编辑索引（M1 已收口，见 M1 完成记录）。
- Skia shaping 为逐 grapheme cluster（无 HarfBuzz 连写/合字）：拉丁/
  CJK/希伯来/阿拉伯基本形正确，复杂脚本合字后续增强。
- VirtualList 已落地（M3：可见区物化/实测 extent 修正/锚点稳定），虚拟化仍为
  纵向；M10 已接入触摸/指针拖动与确定性惯性滚动。ScrollView 水平轴、
  横纵滚动条拖动与嵌套滚轮路由已实施；同一视口双轴联滚、水平虚拟化、
  RTL 镜像和 auto-hide 未实现。Gallery 水平滚动演示区仍待补，详见
  [滚动设计](lumen-scroll-design.md)。
- Grid 为纵向网格（无横向滚动/跨行列合并）；Image 需应用侧资源管理器
  驱动加载（框架不管理异步资源生命周期）。
- 平台原生无障碍 provider 已在 M13 编入三桌面目标并有 headless 回归；Linux
  AT-SPI2、macOS NSAccessibility 和 Windows UIA 的真实屏幕阅读器人工回环
  仍需单独验收，不能由 Xvfb 或 headless CI 代替。
- 应用主循环/damage 管线已收敛到 `lumen-app` 应用壳（M2）：新工具页只需
  提供 build/状态逻辑；`runApp` 支持单窗口兼容入口和多个 `AppWindow` 绑定，
  按 `HostEvent.window` 隔离输入、DPI、IME、renderer、语义桥和帧调度。
  宿主窗口生命周期仍由调用方拥有；最后一个运行时关闭后主循环结束。
- 平台服务已统一接口（M4：文件选择/OpenURL/通知/光标/图标 + 能力报
  告）；M12 已收口原生后端——通知与强调色走 native seam（Win32/
  DBus/AppKit），SDL 系统主题输入经 `SDL_GetSystemTheme` +
  `SystemThemeChanged` 事件接入；当前三平台 CI 证据见上方验证快照。高对比/
  减少动画/字体缩放支持应用手动设置，OS 对应偏好自动查询尚未接通，不能
  宣称主题能力已自动跟随系统。
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
  `lumen-render` 与其余桌面公共静态库，但尚未提供 CMake 导出配置，当前包仍不能
  视作完整的外部开发 SDK。
- 移动方向暂不实现并冻结；M9 只保留状态说明，不作为桌面版本的待完成项。
