# Lumen 平台支持矩阵（v0.3 + M0 基线）

> 状态：随 v0.3 阶段 8A–8E 更新（2026-09），M0 基线冻结补充工具链与四态定义。
> 当前产品范围（2026-09-14）：Windows/Linux/macOS 桌面；Android/iOS 暂不支持，M9 暂缓。
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
| Windows | `windows-2025` | MSVC（VS 2025 自带，`/utf-8 /W4`，Skia 时强制静态 CRT/MT，Release） | 无额外系统包；Skia 预编译自动拉取 |
| Linux | `ubuntu-24.04` | GCC（系统默认，`-Wall -Wextra -Wpedantic`；Skia Release 经 clang-12 官方构建包链接） | `LUMEN_LINUX_DEPS`（见 `linux.yml` env，与 `build-commands.md` 同源）+ GPU job 追加 `libgl1-mesa-dri mesa-utils` |
| macOS | `macos-15` | AppleClang（Xcode CLT） | 无额外系统包；SDL3 经 FetchContent 编译 |

固定第三方版本（`cmake/dependencies.cmake`）：SDL3 `release-3.2.10`、
Catch2 `v3.8.1`、stb `2c980bb59875b0d32144a71867fbdebb2f77cd20`、
Skia `m124-08a5439a6b`（Windows/ Linux 预编译 Release 包）、
zlib `v1.3.1`（仅 Windows Skia）。新增 FetchContent 依赖时固定版本、
记录许可证，并说明 CPU-only 行为。

## 桌面平台

| 平台 | 窗口/输入 | 剪贴板 | 文本/IME | 无障碍 | 渲染 | CI 验证 |
| --- | --- | --- | --- | --- | --- | --- |
| Windows | SDL3（多窗口、resize/DPI、触摸 pointer id） | SDL3 剪贴板（`platform::Clipboard`） | UTF-8 commit + IME preedit（TSF 经 SDL）；修饰键/逻辑键归一化 | 语义树 + Recording 桥；UIA 原生桥为可选目标（未编入时能力报告 false） | CPU、Skia 光栅、Skia GPU（失败回退 CPU） | `windows.yml`：cpu / skia-raster / skia-gpu |
| Linux | SDL3（X11/Wayland） | 同上 | UTF-8 + IBus/Fcitx preedit（候选词锚点经 `SDL_SetTextInputArea`） | 语义树 + Recording 桥；AT-SPI 原生桥为可选目标 | 同上 | `linux.yml`：cpu / skia / skia-gpu（Xvfb + llvmpipe） |
| macOS | SDL3（v0.3 新增桌面支持；菜单关闭经统一关闭规则） | 同上 | UTF-8 + 输入法 preedit（经 SDL） | 语义树 + Recording 桥；NSAccessibility 原生桥为可选目标 | CPU、Skia 光栅、Skia GPU（失败回退 CPU；M7 纳入桌面 GPU 验收） | `macos.yml`：cpu / skia-gpu / package（工作流已配置，运行结果以具体 CI 记录为准） |

三平台共用：`ApplicationHost` 契约、归一化 `HostEvent`（时间戳/修饰键/
逻辑与物理键/指针设备/pointer id/滚轮/取消/关闭请求）、语义树与 action
分发、`lumen-text` 编辑模型。counter/settings 示例三平台同源。

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
这不是 Linux 运行时 GPU/present 故障注入，后者仍待验。62 个视觉样本在统一预乘表示下
alpha 精确一致，普通 RGB 最大差 2；CPU/Skia 各三组正式配对及收益/代价见
[P4 报告](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)。GPU 性能未测。
公开像素结构需要重编译，`pixels()` 返回实际模式；命令写 v7、读 v6/v7，
Gallery 默认导出保持 straight，见 [迁移说明](lumen-alpha-migration.md)。

## 历史移动实验内容（暂缓，不在当前支持范围）

| 平台 | 当前范围 | 已有代码 | 验证边界 |
| --- | --- | --- | --- |
| Android | 暂不支持，未排期 | 保留 `lumen-mobile-host` 通用状态机；JNI/NativeActivity 胶水未纳入 | Linux 的 mobile-core job 只检查 SDL-free 通用代码，不是 NDK 或设备验证 |
| iOS | 暂不支持，未排期 | 保留同一 `MobileHostSeam` 状态机；Objective-C++ 胶水未纳入 | macOS 的 mobile-core job 只检查 SDL-free 通用代码，不是 iOS 工程或 Simulator 验证 |

原生接入、软键盘、移动字体/绘制管线、移动页面、GPU、无障碍与发布均不在当前
设计和验收范围，不预排到 v0.4。已有 `createMobileFontManager`、
`FontBackend::Mobile` 和默认字体栈分支也不能作为移动端文本可用的证据。

现有源码、构建开关、测试和 Linux/macOS mobile-core CI job 保留，继续按工作流
执行兼容性检查；本次文档调整不删除或禁用这些内容。桌面触屏、Touch density、
窄窗口和通用安全区指标仍属于桌面可用性设计。

## 后端与能力

| 能力 | 提供方 | 降级行为 |
| --- | --- | --- |
| CPU 光栅 | `CpuRenderer`（窗口经系统字体绘制真实字形，headless/无字体时确定性占位） | 无需降级；CPU-only 构建不依赖 SDL 实现库与桌面会话 |
| Skia 光栅 | `SkiaRenderer`（可选 `LUMEN_ENABLE_SKIA`） | 未编入时能力报告 `backendName=cpu`，应用安全运行 |
| Skia GPU | `SkiaGpuRenderer`（可选 `LUMEN_ENABLE_GPU`） | 探测/初始化失败自动回退 CPU，诊断记录原因（v0.2 §7C） |
| 文本 shaping | `lumen-text` + `SkiaFontManager`（可选 `LUMEN_ENABLE_SKIA`，封装字体族/回退/度量/shaping；公共接口无 Skia 类型）或 `SystemFontManager`（CPU 窗口默认：Windows 优先 GDI 系统字形，其他平台及 GDI 回退经 stb_truetype 读取系统字体，Windows 雅黑优先） | CPU-only 构建或无系统字体时回退 `PlaceholderFontManager`，布局/编辑照常（`fontDiagnostic`/工厂诊断明确报告）；布局与绘制共享同一份 shaped 结果，光标/选区不跨后端漂移 |
| 编辑撤销 | `text::EditingHistory` + `InteractionController`（Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y；连续单字输入/删除合并，IME 提交为单事务，preedit 不进栈） | 只读字段与 preedit 期间拒绝撤销；栈按字段 bind 隔离，容量 100 |
| 应用壳 | `lumen-app`（`app::AppShell` 帧管线 + `app::runApp` 主循环；M2） | 示例只保留 build/状态/handler；支持 Fake host 与外部测试 renderer 注入；GPU 失效经回退钩子重建软件窗口回到 CPU |
| 剪贴板 | `platform::Clipboard` / `core::ClipboardProvider` | runApp 启动时接入宿主剪贴板（Ctrl+C/V；宿主不可用保持未注入）；`setText` 失败返回 false，编辑状态不丢 |
| 语义桥接 | `AccessibilityBridge`（接口 + Recording 桥 + AppShell 每帧 identity diff/焦点/action 回执驱动，M5 收口） | 平台原生 provider（UIA/AT-SPI/NSAccessibility）未实现：工厂返回 nullptr 并给出原因；Recording 桥作跨平台回归证据 |
| 可访问性设置 | `PlatformCapabilities`（只读查询） | 高对比/减少动画/字体缩放由 `Theme::fromSettings` 与 `FrameScheduler::setReduceAnimation` 消费 |

## 已知限制（v0.3，映射到自用路线图里程碑）

- 双向文本为 UAX#9 确定性子集（强/弱/中性类近似，无显式嵌入控制、镜像
  括号与数字定形）：纯 RTL/LTR 与常见混合段落正确，完整 UBA 属后续版
  本；grapheme 边界仍是唯一编辑索引（M1 已收口，见 M1 完成记录）。
- Skia shaping 为逐 grapheme cluster（无 HarfBuzz 连写/合字）：拉丁/
  CJK/希伯来/阿拉伯基本形正确，复杂脚本合字后续增强。
- VirtualList 已落地（M3：可见区物化/实测 extent 修正/锚点稳定），仅
  纵向；M10 已接入触摸/指针拖动与确定性惯性滚动（fling 物理见 M10
  完成记录），水平/嵌套滚动仍不支持。
- Grid 为纵向网格（无横向滚动/跨行列合并）；Image 需应用侧资源管理器
  驱动加载（框架不管理异步资源生命周期）。
- 平台原生无障碍桥（UIA/AT-SPI/NSAccessibility）的完整 provider 实现
  属后续版本；v0.3 冻结了桥接契约与 headless 验证路径，语义收口见 M5。
- 应用主循环/damage 管线已收敛到 `lumen-app` 应用壳（M2）：新工具页只需
  提供 build/状态逻辑；runApp 为单窗口主循环（多窗口属后续里程碑）。
- 平台服务已统一接口（M4：文件选择/OpenURL/通知/光标/图标 + 能力报
  告）；M12 已收口原生后端——通知与强调色走 native seam（Win32/
  DBus/AppKit），prefersDarkMode 经 SDL_GetSystemTheme +
  SystemThemeChanged 事件；Linux/macOS 以 CI 首跑为事实来源。
- M6 视觉系统 V3 已收口（图标/阴影/滚动条 token 路径、六控件、
  ThemeScope、组合校验器）；M10 已收口转场动画驱动（transitionAlpha
  整节点透明度、Dialog/Navigator 淡入淡出、状态色过渡 opt-in）；
  M11 已收口 Tooltip hover 延迟显隐与 Dropdown 浮动菜单
  （框架级 overlay + Up/Down/Enter/Esc 键盘导航）。
- M11 v0.4 视觉方向已收口：ThemeDirection 四方向（CoreDark 默认 /
  InkLinen / AuroraSignal / UtilityContrast），方向与深浅/高对比/字体
  缩放/密度正交组合经 fromSettings 派生；Aurora 为扁平近似（玻璃/
  渐变/光晕不做，见 roadmap M11 已知限制）。
- M7 已收口：macOS GPU CI 已纳入（首跑为事实来源）；partialSubmit
  实测 1.16× 维持全帧提交（正确性优先）；Element move 管道完成后性能
  门槛 6/6 达标。
- M8+M12 便携发布已收口：install/CPack/CI package job + 解包冒烟 +
  Linux AppImage（linuxdeploy）与 macOS Lumen.app 骨架 + package-skia
  （三平台）/package-skia-gpu（Linux llvmpipe）变体；新形态以 CI 首跑
  为事实来源，Windows/macOS GPU 包与 CPack Bundle 留后续。
- 移动方向暂缓；M9 只保留状态说明，不作为桌面版本的待完成项。
