# Lumen 平台支持矩阵（v0.3 + M0 基线）

> 状态：随 v0.3 阶段 8A–8E 更新（2026-09），M0 基线冻结补充工具链与四态定义。
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
| macOS | SDL3（v0.3 新增桌面支持；菜单关闭经统一关闭规则） | 同上 | UTF-8 + 输入法 preedit（经 SDL） | 语义树 + Recording 桥；NSAccessibility 原生桥为可选目标 | CPU、Skia 光栅；GPU 可选、失败回退 CPU（非门槛） | `macos.yml`：cpu（含 settings 冒烟） |

三平台共用：`ApplicationHost` 契约、归一化 `HostEvent`（时间戳/修饰键/
逻辑与物理键/指针设备/pointer id/滚轮/取消/关闭请求）、语义树与 action
分发、`lumen-text` 编辑模型。counter/settings 示例三平台同源。

## 移动平台（实验性，v0.3 接缝门槛）

| 平台 | v0.3 承诺 | 实现 | 验证 |
| --- | --- | --- | --- |
| Android | SDL-free host 接缝（实验性） | `lumen-mobile-host`：surface attach/detach、pause/resume、安全区、触摸归一化、返回键；Android JNI/NativeActivity 胶水尚未纳入本仓库 | `linux.yml` mobile-core 只验证通用静态库和 headless；NDK/模拟器目标待实现 |
| iOS | SDL-free host 接缝（实验性） | 同一 `MobileHostSeam` 状态机；iOS Objective-C++ 胶水尚未纳入本仓库 | `macos.yml` mobile-core 只验证通用静态库和 headless；Xcode/模拟器目标待实现 |

移动端不承诺（v0.4 再评估）：商店发布、完整移动端控件、后台渲染、原生
accessibility tree、Metal/Graphite。

## 后端与能力

| 能力 | 提供方 | 降级行为 |
| --- | --- | --- |
| CPU 光栅 | `CpuRenderer`（确定性占位字体） | 无需降级；CPU-only 构建不依赖 SDL 实现库与桌面会话 |
| Skia 光栅 | `SkiaRenderer`（可选 `LUMEN_ENABLE_SKIA`） | 未编入时能力报告 `backendName=cpu`，应用安全运行 |
| Skia GPU | `SkiaGpuRenderer`（可选 `LUMEN_ENABLE_GPU`） | 探测/初始化失败自动回退 CPU，诊断记录原因（v0.2 §7C） |
| 文本 shaping | `lumen-text` + `SkiaFontManager`（可选 `LUMEN_ENABLE_SKIA`，封装字体族/回退/度量/shaping；公共接口无 Skia 类型） | CPU-only 构建或无系统字体时回退 `PlaceholderFontManager`，布局/编辑照常（`fontDiagnostic`/工厂诊断明确报告）；布局与绘制共享同一份 shaped 结果，光标/选区不跨后端漂移 |
| 编辑撤销 | `text::EditingHistory` + `InteractionController`（Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y；连续单字输入/删除合并，IME 提交为单事务，preedit 不进栈） | 只读字段与 preedit 期间拒绝撤销；栈按字段 bind 隔离，容量 100 |
| 应用壳 | `lumen-app`（`app::AppShell` 帧管线 + `app::runApp` 主循环；M2） | 示例只保留 build/状态/handler；支持 Fake host 与外部测试 renderer 注入；GPU 失效经回退钩子重建软件窗口回到 CPU |
| 剪贴板 | `platform::Clipboard` / `core::ClipboardProvider` | runApp 启动时接入宿主剪贴板（Ctrl+C/V；宿主不可用保持未注入）；`setText` 失败返回 false，编辑状态不丢 |
| 语义桥接 | `AccessibilityBridge`（接口 + Recording 桥） | 当前只提供平台无关契约与 Recording 桥；平台原生 provider 尚未实现，工厂返回 nullptr 并给出原因 |
| 可访问性设置 | `PlatformCapabilities`（只读查询） | 高对比/减少动画/字体缩放由 `Theme::fromSettings` 与 `FrameScheduler::setReduceAnimation` 消费 |

## 已知限制（v0.3，映射到自用路线图里程碑）

- 双向文本为 UAX#9 确定性子集（强/弱/中性类近似，无显式嵌入控制、镜像
  括号与数字定形）：纯 RTL/LTR 与常见混合段落正确，完整 UBA 属后续版
  本；grapheme 边界仍是唯一编辑索引（M1 已收口，见 M1 完成记录）。
- Skia shaping 为逐 grapheme cluster（无 HarfBuzz 连写/合字）：拉丁/
  CJK/希伯来/阿拉伯基本形正确，复杂脚本合字后续增强。
- VirtualList 已落地（M3：可见区物化/实测 extent 修正/锚点稳定），但仅
  纵向且惯性滚动默认关闭（动量物理属后续版本）。
- Grid 为纵向网格（无横向滚动/跨行列合并）；Image 需应用侧资源管理器
  驱动加载（框架不管理异步资源生命周期）。
- 平台原生无障碍桥（UIA/AT-SPI/NSAccessibility）的完整 provider 实现
  属后续版本；v0.3 冻结了桥接契约与 headless 验证路径，语义收口见 M5。
- 应用主循环/damage 管线已收敛到 `lumen-app` 应用壳（M2）：新工具页只需
  提供 build/状态逻辑；runApp 为单窗口主循环（多窗口属后续里程碑）。
- 平台服务已统一接口（M4：文件选择/OpenURL/通知/光标/图标 + 能力报
  告）；通知在 SDL 3.2.10 无 API（结构化降级，真实通知待 SDL 升级或
  原生后端）；prefersDarkMode/accentColor 为安全默认（SDL 3.2 无查询）。
- Icon/阴影/转场/ThemeScope、Dropdown/Menu/Tooltip/Slider/ProgressBar/
  Radio/Tabs、Scrollbar 完整控件、表单扩展校验器：见 M6。
- GPU `partialSubmit` 固定 false，macOS GPU 非门槛：见 M7。
- 无正式便携包流水线：见 M8；移动软键盘/字体策略：见 M9。
