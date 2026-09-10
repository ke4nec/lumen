# Lumen

C++20 自绘 GUI 框架（Flutter 式声明式 UI），详见
[`docs/lumen-gui-framework-plan.md`](docs/lumen-gui-framework-plan.md)。

当前进度：阶段 0–6（工程骨架、核心树和布局、CPU 渲染与 SDL3 平台层、交互与 C++ DSL、文本 DSL、Skia 适配、框架完善）。

## 结构

- `include/lumen/`：`core`、`layout`、`render`、`platform`、`dsl` 公共头文件。
- `src/`：与公共模块一一对应的实现（`render` 含 CPU 光栅器与 painter，`platform` 含 SDL3 后端）。
- `tests/`：Catch2 单测与无窗口集成测试（几何、布局、Element、渲染像素、交互、counter frame hash）。
- `examples/counter/`：可交互 counter 示例（窗口模式 + `--headless`）。
- `cmake/`：FetchContent 依赖声明（SDL3、Catch2，均已 pin 版本）。
- `docs/`：架构与分阶段计划。

## 构建

```sh
cmake -S . -B build -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

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
