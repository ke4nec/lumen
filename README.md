# Lumen

C++20 自绘桌面 GUI 框架（Flutter 式声明式 UI），详见
[`docs/lumen-gui-framework-plan.md`](docs/lumen-gui-framework-plan.md)、
v0.2 计划
[`docs/lumen-gui-framework-plan-v0.2.md`](docs/lumen-gui-framework-plan-v0.2.md)
与 v0.3 计划
[`docs/lumen-gui-framework-plan-v0.3.md`](docs/lumen-gui-framework-plan-v0.3.md)，
视觉系统设计
[`docs/lumen-visual-system-design.md`](docs/lumen-visual-system-design.md)。
当前只规划 Windows/Linux/macOS。Android/iOS 暂不实现并冻结，不纳入当前或后续自动任务；M9 仅保留历史编号。只有用户再次明确提出移动端需求后，才另行评估范围与方案。
桌面自用版 M0–M8 已收口，桌面增强链 M10（动效与滚动）、M11（v0.4 视觉
方向与控件体验）、M12（平台服务与发布补全）已完成；M13 原生无障碍
provider 已接入三桌面平台，真实屏幕阅读器回环仍在平台验收中。
面向自用工具类应用的桌面路线图见
[`docs/lumen-self-use-roadmap.md`](docs/lumen-self-use-roadmap.md)。
统一构建/验证命令表见
[`docs/build-commands.md`](docs/build-commands.md)，性能基线见
[`docs/perf-baselines/README.md`](docs/perf-baselines/README.md)。
预乘 alpha 渲染改造 P0–P5 已完成（三平台透明合成真机验证仍待验），
交付记录见
[`docs/lumen-premultiplied-alpha-rendering-plan.md`](docs/lumen-premultiplied-alpha-rendering-plan.md)。

当前进度：M0 基线冻结完成（统一命令表、工具链/依赖基线、四态定义、
`docs/perf-baselines/v0.2-cpu-scene.json` CPU 归档基线）；
阶段 0–6 + v0.2 阶段 7A–7E + v0.3 桌面阶段 8A–8E 的核心契约（跨平台宿主、
文本/IME/编辑模型、语义树与 Recording 桥、滚动/表单/弹窗/导航组件）。历史
SDL-free 移动 host 接缝仍保留，但不代表当前支持移动平台。三桌面便携发布已纳入
M8；M12 已交付 AppImage、CPU `.app` 骨架、三平台 Skia 包和 Linux GPU 包，
Windows/macOS GPU 包与 CPack Bundle 仍待补齐；开发归档和运行时产物职责见
[`docs/lumen-packaging.md`](docs/lumen-packaging.md)；平台原生
无障碍 M13 进行中（三平台原生 provider 已接入，真实屏幕阅读器回环待验）。平台能力详见
[`docs/support-matrix.md`](docs/support-matrix.md)。

## 结构

- `include/lumen/`：`core`、`style`、`layout`、`render`、`text`、`accessibility`、
  `widgets`、`platform`、`dsl` 公共头文件。
- `src/`：与公共模块一一对应的实现（`render` 含 CPU 光栅器、命令管线、
  帧调度器、资源管理器、painter 与可选 Skia 光栅/GPU 适配，`platform` 含
  SDL3 桌面后端与保留的历史 SDL-free 移动实验接缝）。
- `tests/`：Catch2 单测与无窗口集成测试（几何、布局、Element、渲染像素、
  命令回放/序列化、调度、资源、交互、文本/图串、语义树、平台宿主、
  历史移动接缝、counter/settings frame hash、gallery 集成）。
- `benchmarks/`：固定 1080p 场景基准（阶段耗时 p50/p95、堆分配、命令数、frame hash）。
- `examples/counter/`：最小回归示例（窗口模式 + `--headless`）。
- `examples/settings/`：v0.3 应用基础组件示例（滚动列表、表单校验、弹窗、
  导航、主题、无障碍标签；窗口模式 + `--headless`）。
- `examples/gallery/`：控件 Gallery（按钮变体/尺寸/状态、输入控件、布局、
  滚动与虚拟列表、进度/图标/弹窗反馈、颜色方案/排版/密度/ThemeScope）。
  壳层与 Overview 首屏对齐 `design/gallery.html` v1「Core Dark」设计稿
  （窗口顶栏/侧栏 Live state/指标卡/双栏面板；`--headless --dump-frame
  <path>` 可导出首帧 RGBA 供视觉核对）。
- `cmake/`：FetchContent 依赖声明（SDL3、Catch2、stb，均已 pin 版本）。
- `docs/`：架构、分阶段计划与支持矩阵。

## 构建

每配置使用独立构建目录（`build-debug` / `build-release`），避免在同一树内
并行编 Debug+Release（多配置生成器在 `ZERO_CHECK` stamp 上可能竞态）：

```sh
cmake -S . -B build-debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build-debug --config Debug
ctest --test-dir build-debug --output-on-failure -C Debug

cmake -S . -B build-release -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build-release --config Release
ctest --test-dir build-release --output-on-failure -C Release
```

构建开关：

| 开关 | 默认 | 说明 |
| --- | --- | --- |
| `LUMEN_BUILD_TESTS` | ON | Catch2 单测与集成测试 |
| `LUMEN_BUILD_EXAMPLES` | ON | counter / settings / gallery 示例 |
| `LUMEN_BUILD_BENCHMARKS` | OFF | `lumen-scene-bench` 固定场景基准 |
| `LUMEN_ENABLE_SKIA` | OFF | Skia 光栅后端（预编译包自动拉取） |
| `LUMEN_ENABLE_GPU` | OFF | Skia Ganesh GPU 后端（需 `LUMEN_ENABLE_SKIA`） |
| `LUMEN_ENABLE_ACCESSIBILITY_BRIDGE` | OFF | 平台原生无障碍桥开关；Windows UIA、Linux AT-SPI2、macOS NSAccessibility 在对应桌面+开关下编入；无桌面服务时能力如实降级；语义树与 Recording 桥始终可用 |
| `LUMEN_BUILD_MOBILE_CORE` | OFF | 保留的历史 SDL-free 实验配置（跳过 SDL 与桌面示例）；不属于当前产品范围，不代表 Android/iOS 工程或设备验证 |

Linux（Ubuntu 24.04/26.04）先安装系统依赖（SDL3 窗口/输入、Skia
FontConfig、Xvfb 冒烟）：

```sh
sudo apt-get update
sudo apt-get install -y cmake ninja-build pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxfixes-dev libxi-dev libxss-dev libwayland-dev \
  libxkbcommon-dev libdrm-dev libgbm-dev \
  libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
  libdbus-1-dev libibus-1.0-dev libdecor-0-dev \
  libasound2-dev libpulse-dev libaudio-dev libjack-dev \
  libsndio-dev libsamplerate0-dev liburing-dev \
  wayland-protocols libfontconfig1-dev libfreetype6-dev xvfb
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build-debug
ctest --test-dir build-debug --output-on-failure
```

Windows 可用 VS 自带的 CMake/Ninja，例如：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
```

示例运行（`build-debug/examples/{counter,settings}/Debug/`，Ninja 单配置则无
`Debug/` 子目录）：

```sh
# 窗口模式：Button 点击计数、TextField 输入、窗口缩放自适应
./build-debug/examples/counter/Debug/lumen-counter
# 无窗口模式：打印稳定 frame hash（点击/输入/缩放各一帧）
./build-debug/examples/counter/Debug/lumen-counter --headless
# 文本 DSL（.lumen）加载 UI（与 C++ DSL 构建同一棵树）
./build-debug/examples/counter/Debug/lumen-counter --dsl counter.lumen
# v0.3 settings：滚动列表、表单校验、弹窗、导航、主题、无障碍标签
./build-debug/examples/settings/Debug/lumen-settings
./build-debug/examples/settings/Debug/lumen-settings --headless
# Widget Gallery：控件/布局/颜色方案/主题演示
./build-debug/examples/gallery/Debug/lumen-gallery
./build-debug/examples/gallery/Debug/lumen-gallery --headless
```

macOS 构建与 Linux 相同（SDL3 经 FetchContent 编译，Xcode CLT 的
clang + CMake 即可，无需额外系统依赖）；CI 见
`.github/workflows/macos.yml`。

## v0.3 应用基础（阶段 8A–8E）

### 跨平台宿主契约（8A）

`ApplicationHost` 拆分初始化/事件泵/生命周期、窗口管理（稳定
`WindowId` + `WindowMetrics`：逻辑/drawable 尺寸、安全区、deviceScale、
可见性）与平台服务（`Clipboard`、`TextInputSession`、能力查询）。
`HostEvent` 为归一化事件值类型（时间戳、`WindowId`、修饰键、逻辑/物理
键、指针设备与 pointer id、滚轮增量、取消与关闭请求、surface
detach/attach）。`FakeApplicationHost` 提供确定性 headless 测试（fake
clock/clipboard/text-input、可注入事件源、多窗口）；`Sdl3ApplicationHost`
为 Windows/Linux/macOS 桌面实现。`WindowId` 关联帧提交、surface 重建与
资源完成事件。

### 文本、IME 与编辑（8B）

`lumen-text`：grapheme cluster 分段（组合标记/ZWJ emoji/旗帜/肤色/变体
选择符/Hangul）、严格 UTF-8 校验、`FontManager` 回退链（latin/cjk/emoji
确定性占位实现；窗口 CPU 路径另有 `SystemFontManager` 使用系统字形（Windows
优先 GDI 雅黑，其他平台或 GDI 回退直读系统字体，仍经 stb_truetype），
`TextLayout`（换行/ellipsis/maxLines/baseline/字形
位置/命中测试/RTL 视觉逆序/布局缓存）。`TextEditingValue` 状态机以
grapheme 索引承载 text/selection/composing。TextField 支持 Shift 选区、
Ctrl/Gui+A/C/X/V、双击选词、拖动扩选、点击定位、IME preedit
commit/cancel、密码/只读/多行属性与剪贴板。布局与绘制共用同一份
TextLayout，光标/选区/宽度不跨后端漂移。

### 语义树与无障碍（8C）

`lumen-accessibility`：`SemanticsTree`（role/label/value/bounds/flags/
actions，节点 id 复用 RenderNode 稳定 identity）、identity diff（重建时
保留辅助技术焦点）、action 分发（activate/setValue/focus/scroll/dismiss
与键盘路径共用）。`AccessibilityBridge` 契约 + headless Recording 桥；
Windows UIA、Linux AT-SPI2、macOS NSAccessibility provider 均已实现，需启用
`LUMEN_ENABLE_ACCESSIBILITY_BRIDGE`，并在真实桌面会话完成对应屏幕阅读器回环。
应用提供的高对比/减少动画/字体缩放设置可驱动 `Theme::fromSettings` 与
`FrameScheduler::setReduceAnimation`；SDL 宿主在 Linux D-Bus、macOS AppKit
和 Windows Win32 上查询系统对应偏好，不可用时保持安全默认。
UIA 客户端端到端冒烟已有覆盖，三平台屏幕阅读器人工验收按 M13 单独记录。

### 应用组件与 settings 示例（8D）

`ScrollView`/`ListView`（滚动视口：内容主轴不限、clip、`ScrollController`
统一滚轮/拖动/键盘/语义入口，M10 已接入确定性惯性滚动；ScrollView 支持水平轴）、`Checkbox`/`Switch`（bind
状态自动切换）、`FocusScope`（Tab 域内循环）、`FormController` 校验、
`NavigatorController`（push/pop/handleBack 统一 Escape/返回/关闭规则）与
`makeDialog`（modal barrier + FocusScope + 语义 dismiss；视觉来自
`DialogTokens`）。`examples/settings/` 组合以上全部能力；320px 窄窗口、
连续 resize、局部重绘与全帧像素一致（测试断言）。

### 视觉系统（V1–V3 已实施，V4 部分完成）

`lumen-style`：primitive → semantic → component 三层 token、分组
`Theme`（ColorScheme/Typography/Metrics/Elevation/Motion/Icon 与 Button/
TextField/Checkbox/Switch/Dialog/Scrollbar token）、`WidgetState` 与
`InteractionStateSnapshot`、`StyleResolver`（`resolveStyle` 折算 hover/
pressed/focused/disabled/checked/invalid）。`core/style.h` 的
`ResolvedStyle` 写入 RenderNode 并参与 diff/damage；painter 只读
resolved style（无控件硬编码，CPU/Skia/GPU 命令路径不依赖 Theme）。
`Theme::fromSettings` 派生高对比/字体缩放/减少动画/density；焦点环内嵌
绘制且不影响布局尺寸；`StyleOverrides` 提供字段级品牌定制（显式黑/透明
按字面生效）。DSL 支持 `variant/size/enabled/invalid/selected` 声明。
IconTheme/Elevation/Motion、ThemeScope、PlatformThemeAdapter 和四套
ThemeDirection 已接入。完整三桌面人工窗口验收仍待补齐；当前自动化证据与平台限制以
[`docs/support-matrix.md`](docs/support-matrix.md) 为准。

### macOS 桌面与历史实验接缝（8E）

macOS 走与 Windows/Linux 相同的 SDL3 桌面契约。CPU、Skia 光栅和 GPU/CPU
回退的支持状态及发布门槛见支持矩阵和自用路线图 M7/M8。

历史 `MobileHostSeam`（`lumen-mobile-host`，SDL-free）保留 surface 生命周期、
触摸归一化和返回请求状态机。`LUMEN_BUILD_MOBILE_CORE=ON` 与 Linux/macOS
`mobile-core` CI job 仍用于通用实验代码的编译和 headless 兼容性检查，
不证明 Android/iOS 可用，也不是移动端任务入口。本轮只保留这些历史代码和工作流；
不得据此新增、补齐或执行原生 host、软键盘、移动字体管线、移动示例和设备验收任务。

桌面触屏、`ControlDensity::Touch`、窄窗口与通用 `safeArea` 指标仍可服务桌面
布局和输入；默认字体栈与 Gallery 只按桌面功能验收，不作为 M9 或任何移动端进度。

### 支持矩阵与故障排查

平台/后端/能力矩阵与已知限制见 [`docs/support-matrix.md`](docs/support-matrix.md)。
GPU 回退、黑屏与 Skia 链接问题的排查沿用 v0.2 段落；新增：

- **剪贴板不可用**（无桌面会话）：`Clipboard::setText` 返回 false，应用
  状态不受影响；`--diagnostics` 输出能力。
- **无障碍桥未编入**：`createPlatformAccessibilityBridge` 返回 nullptr 并
  给出原因；语义树与键盘导航照常（headless 全量验证）。

## 文本 DSL（阶段 4）

`.lumen` 文件由手写 lexer + 递归下降 parser 解析为与 C++ builder 相同的
Widget 树（见 `examples/counter/counter.lumen`）。支持节点嵌套、数值/字符串/
`#RRGGBB[AA]` 颜色/布尔属性、对齐枚举、`bind`/`onClick`/`placeholder`；
错误信息包含文件名、行号、列号与期望 token。

## Skia 后端（阶段 5，可选）

```sh
cmake -S . -B build-skia -DLUMEN_ENABLE_SKIA=ON
cmake --build build-skia --config Release   # 预编译包为 Release（Windows: skia.lib/MT，Linux: libskia.a）
ctest --test-dir build-skia -C Release      # 含 CPU/Skia 一致性 smoke 测试
./build-skia/examples/counter/lumen-counter --renderer skia
# Windows 多一层 Release/：./build-skia/examples/counter/Release/lumen-counter --renderer skia
```

Windows/Linux 下自动拉取固定版本的预编译 Skia（aseprite/skia
`m124-08a5439a6b`：`Skia-Windows-Release-x64.zip` /
`Skia-Linux-Release-x64.zip`）；其他平台用
`-DLUMEN_SKIA_ROOT=<skia 安装目录>`。默认 `OFF`，CPU-only 构建不引入
Skia 依赖。

Linux 上 Skia 文字经 FontConfig 解析系统字体（DejaVu/Noto/CJK），需已
安装 `libfontconfig1-dev`；X11/Wayland 中文输入（IBus/Fcitx）候选框跟随
聚焦的 TextField（`SDL_SetTextInputArea`），触摸屏经 Finger 事件映射为
Pointer，窗口缩放（含 Wayland 分数缩放）统一转为 Resize。

> Linux CPU-only 与 Skia 构建均已在 Ubuntu 26.04 验证：
> `ctest` 全量通过（含 CPU/Skia 一致性）与 Xvfb 窗口冒烟通过，见
> `.github/workflows/linux.yml`。

## v0.2 桌面运行时（阶段 7A–7E）

### 命令管线（7B）

Painter 只录制命令（`recordScene` → `RenderCommandList`），CPU / Skia 光栅 /
Skia GPU 后端消费同一份命令：`Renderer::submit(list, FrameInfo)` 提交一帧，
`capabilities()` 报告后端能力，`stats()` 返回命令数、提交/GPU 等待耗时、
damage 裁剪数与 full-frame fallback 原因；`resetSurface()` 在尺寸/DPI/设备
重建后重建目标。命令支持二进制序列化（`serializeCommands`）与 damage
范围裁剪（`cullCommandsOutside`）；旧 `beginFrame/draw*/endFrame` 即时路径
经默认适配器继续可用，现有测试无需改写。

### Skia GPU 后端与 CPU 回退（7C）

```sh
cmake -S . -B build-gpu -DLUMEN_ENABLE_SKIA=ON -DLUMEN_ENABLE_GPU=ON
cmake --build build-gpu --config Release
./build-gpu/examples/counter/lumen-counter --renderer gpu --diagnostics
```

`--renderer gpu` 先无副作用探测（隐藏窗口上完整初始化 GL + Ganesh），
成功则以 OpenGL 窗口 + `SkiaGpuRenderer` 运行；探测或初始化失败打印原因
并自动回退 CPU，应用状态与 UI 树不丢失。纹理、裁剪、透明度、文字、
surface resize 均已支持。surface 重建、上下文或交换失败会使
`skiaGpuRendererAlive()` 返回 false；counter 随即释放 GPU 资源、重建 CPU
窗口并通过软件 surface 呈现，保留应用状态、焦点与逻辑窗口大小。
重建会取消旧输入法会话的预编辑并恢复原选区，已提交文本不丢失。
软件窗口更新失败会报告错误并退出。Graphite 留待后续版本。

软件回退需要原生窗口 framebuffer（Windows/X11 支持）。固定版本 SDL
的 Wayland 驱动不提供该能力；Wayland 桌面需要 XWayland，并以
`SDL_VIDEODRIVER=x11` 运行示例，否则回退会明确报错退出。

### 帧调度与异步资源（7D）

主循环由 `FrameScheduler` 驱动（counter 已接入，替换固定延时）：
invalidate 原因按帧合并、空闲不提交（事件等待唤醒）、动画按 deadline、
resize 防抖（输入优先不被拖延）、最小化暂停、VSync/目标帧率节流、turn
内不重入；时间源可注入（headless 测试确定性运行）。

`ResourceManager` 提供异步图片加载：worker 只做受限文件读取与解码
（PNG/JPEG 走固定版本 stb_image，另有 `.lumenrgba` 原始格式），代际句柄
保证异步完成不能复活已释放资源；CPU 缓存按字节上限 LRU 淘汰，upload/
unload 以命令增量进入帧提交，GPU 设备重建后同 ImageId 重新上传。

`.lumenrgba` 原始图片格式：ASCII 头 `LUMENRGBA\n<width> <height>\n` +
`w*h*4` 字节 straight RGBA。

### 运行时诊断与基准（7A/7E）

```sh
./lumen-counter --diagnostics            # 后端/能力/DPI + 周期帧统计
./lumen-counter --renderer gpu --diagnostics --frames 1  # 呈现一帧后正常退出
lumen-scene-bench --frames 300 --json    # 1080p 固定场景基准报告
```

诊断输出包含后端选择、GPU 回退原因、每帧命令数、damage 裁剪数、提交与
GPU 等待耗时、空闲轮询数。基准输出各阶段 p50/p95 耗时、堆分配量、命令数
与 frame hash（同一机器同配置下 hash 必须可重复），CI 归档为 CPU 基线。

### 支持矩阵与故障排查（v0.2）

桌面后端矩阵与平台能力总表见 [`docs/support-matrix.md`](docs/support-matrix.md)。

| 后端 | Windows | Linux | macOS |
| --- | --- | --- | --- |
| CPU 光栅（默认） | ✅ | ✅ | ✅ |
| Skia 光栅 | ✅（仅 Release，静态 CRT） | ✅ | ✅（`LUMEN_SKIA_ROOT`） |
| Skia GPU（Ganesh+GL） | ✅（WGL，探测失败自动回退） | ✅（GLX/EGL） | 可选（非门槛） |

- CI（`.github/workflows/`）：Windows/Linux × {CPU-only, Skia 光栅,
  Skia GPU}；Linux GPU 经 Mesa llvmpipe 软件适配器作为强制门槛，硬件
  GPU 为增强 smoke；Windows/Linux 的 `counter_gpu_fallback` 测试使用 SDL
  dummy 驱动强制探测失败，验证 CPU 呈现成功及正常退出。
  Linux 安装 Xvfb 后还会运行 `counter_gpu_runtime_fallback` 和
  `counter_software_present_failure`，分别注入持续 GL 交换失败和软件
  窗口更新失败，验证回退成功及错误退出。
- **GPU 初始化失败/回退 CPU**：查 `--diagnostics` 的
  `[diag] gpu probe failed (...)` 原因（GL 库加载、上下文创建、Ganesh
  初始化、字体管理器）；确认显卡驱动与 OpenGL ≥ 3.0。
- **窗口黑屏/无交换**：GL 窗口上不会走 CPU present（返回 Rejected）；
  确认 `--renderer gpu` 探测成功而不是回退（诊断行 `backend=skia-gpu`）。
- **隐藏/离屏窗口挂起**：`SkiaGpuRendererDesc::allowSwap=false`（部分
  驱动在隐藏窗口上 SwapWindow 阻塞）。
- **Windows Skia 链接错误**：预编译 skia.lib 是 Release/MT 静态 CRT，
  只能链 Release 配置。
- **性能回归**：以 CI 归档的基准报告为对照（同机器同配置），固定场景
  frame hash 不变表示像素路径未漂移。

## 框架完善（阶段 6）

- **脏矩形与绘制缓存**：重建帧与上一帧按 identity 对齐求差（`core/damage.h`），
  CPU/Skia 后端支持 `FrameMode::Preserve` 局部重绘；无变化帧直接复用缓存哈希。
  局部与全量重绘像素级一致（测试断言）。
- **图片资源生命周期**：`registerImage`/`unregisterImage`/`clearImages`，
  id 不复用，释放后绘制为 no-op。
- **基础动画**：`core/tween.h`（Linear/EaseIn/EaseOut/EaseInOut，端点钳制）；
  counter 窗口模式以光标闪烁接入（`app.tick` 驱动，headless 保持确定性）。
- **基础手势**：`pointerMove` + 4px slop 区分 tap/drag，拖动释放不触发点击。
- **DSL 编译缓存与热重载**：`DslCache` 按内容哈希去重；`lumen-counter --watch`
  监视 `.lumen` 文件 mtime，变更后重新解析并热替换 UI（状态保留，错误时保持旧
  UI）。
- **Impler 后端**：暂不实现——无公开可用的上游库；Renderer 契约的双后端一致性
  （CPU/Skia smoke）即是未来接入点。
