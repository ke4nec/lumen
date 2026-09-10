# Lumen

C++20 自绘 GUI 框架（Flutter 式声明式 UI），详见
[`docs/lumen-gui-framework-plan.md`](docs/lumen-gui-framework-plan.md) 与
v0.2 计划
[`docs/lumen-gui-framework-plan-v0.2.md`](docs/lumen-gui-framework-plan-v0.2.md)。

当前进度：阶段 0–6 + v0.2 阶段 7A–7E（命令管线、Skia GPU 后端与 CPU
回退、帧调度、异步资源、基准与 CI 矩阵）。

## 结构

- `include/lumen/`：`core`、`layout`、`render`、`platform`、`dsl` 公共头文件。
- `src/`：与公共模块一一对应的实现（`render` 含 CPU 光栅器、命令管线、
  帧调度器、资源管理器、painter 与可选 Skia 光栅/GPU 适配，`platform` 含 SDL3 后端）。
- `tests/`：Catch2 单测与无窗口集成测试（几何、布局、Element、渲染像素、
  命令回放/序列化、调度、资源、交互、counter frame hash）。
- `benchmarks/`：固定 1080p 场景基准（阶段耗时 p50/p95、堆分配、命令数、frame hash）。
- `examples/counter/`：可交互 counter 示例（窗口模式 + `--headless`）。
- `cmake/`：FetchContent 依赖声明（SDL3、Catch2、stb，均已 pin 版本）。
- `docs/`：架构与分阶段计划。

## 构建

```sh
cmake -S . -B build -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

构建开关：

| 开关 | 默认 | 说明 |
| --- | --- | --- |
| `LUMEN_BUILD_TESTS` | ON | Catch2 单测与集成测试 |
| `LUMEN_BUILD_EXAMPLES` | ON | counter 示例 |
| `LUMEN_BUILD_BENCHMARKS` | OFF | `lumen-scene-bench` 固定场景基准 |
| `LUMEN_ENABLE_SKIA` | OFF | Skia 光栅后端（预编译包自动拉取） |
| `LUMEN_ENABLE_GPU` | OFF | Skia Ganesh GPU 后端（需 `LUMEN_ENABLE_SKIA`） |

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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Windows 可用 VS 自带的 CMake/Ninja，例如：

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
```

示例运行（`build/examples/counter/`，Windows 多一层 `Debug/`）：

```sh
# 窗口模式：Button 点击计数、TextField 输入、窗口缩放自适应
./build/examples/counter/lumen-counter
# 无窗口模式：打印稳定 frame hash（点击/输入/缩放各一帧）
./build/examples/counter/lumen-counter --headless
# 文本 DSL（.lumen）加载 UI（与 C++ DSL 构建同一棵树）
./build/examples/counter/lumen-counter --dsl counter.lumen
```

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
surface resize 均已支持；上下文丢失（`gl-context-lost`）经
`skiaGpuRendererAlive()` 暴露。Graphite 留待后续版本。

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
lumen-scene-bench --frames 300 --json    # 1080p 固定场景基准报告
```

诊断输出包含后端选择、GPU 回退原因、每帧命令数、damage 裁剪数、提交与
GPU 等待耗时、空闲轮询数。基准输出各阶段 p50/p95 耗时、堆分配量、命令数
与 frame hash（同一机器同配置下 hash 必须可重复），CI 归档为 CPU 基线。

### 支持矩阵与故障排查

| 后端 | Windows | Linux |
| --- | --- | --- |
| CPU 光栅（默认） | ✅ | ✅ |
| Skia 光栅 | ✅（仅 Release，静态 CRT） | ✅ |
| Skia GPU（Ganesh+GL） | ✅（WGL，探测失败自动回退） | ✅（GLX/EGL） |

- CI（`.github/workflows/`）：Windows/Linux × {CPU-only, Skia 光栅,
  Skia GPU}；Linux GPU 经 Mesa llvmpipe 软件适配器作为强制门槛，硬件
  GPU 为增强 smoke；Windows runner 无硬件 GL 时验证回退路径。
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
