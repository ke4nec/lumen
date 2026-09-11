# Lumen 统一构建与验证命令表（M0 基线冻结）

> 状态：M0 基线入口（2026-09）。本文件是 CPU、Skia、GPU、mobile-core、
> headless、窗口 smoke 和基准的唯一可追踪验收入口。所有里程碑的验证
> 命令必须引用本表，不得使用开发者本地脚本作为门槛依据。
>
> 相关文档：[自用路线图](lumen-self-use-roadmap.md) ·
> [平台支持矩阵](support-matrix.md) ·
> [性能基线说明](perf-baselines/README.md)。

## 1. 前置依赖

| 平台 | 系统依赖安装命令 | 说明 |
| --- | --- | --- |
| Linux Ubuntu 24.04/26.04 | `sudo apt-get update && sudo apt-get install -y cmake ninja-build pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libwayland-dev libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libdecor-0-dev libasound2-dev libpulse-dev libaudio-dev libjack-dev libsndio-dev libsamplerate0-dev liburing-dev wayland-protocols libfontconfig1-dev libfreetype6-dev xvfb` | 与 `.github/workflows/linux.yml` 的 `LUMEN_LINUX_DEPS` 同源；SDL3 窗口/输入、Skia FontConfig、Xvfb 冒烟共用 |
| Windows | Visual Studio 2022+ 自带 CMake/Ninja/MSVC；无需额外系统包 | Skia 预编译包为 Release/MT 静态 CRT，只能链 Release（见根 `CMakeLists.txt` 注释） |
| macOS | Xcode CLT 的 clang + CMake 即可；SDL3 经 FetchContent 编译 | CI 见 `.github/workflows/macos.yml` |

固定第三方版本（`cmake/dependencies.cmake`，禁止浮动到 main）：

| 依赖 | 版本 | 用途 |
| --- | --- | --- |
| SDL3 | `release-3.2.10` | 桌面窗口/输入（mobile-core 跳过） |
| Catch2 | `v3.8.1` | 单测 |
| stb | `2c980bb59875b0d32144a71867fbdebb2f77cd20` | 图片解码 |
| Skia 预编译 | `m124-08a5439a6b`（Windows `Skia-Windows-Release-x64.zip` / Linux `Skia-Linux-Release-x64.zip`，aseprite/skia） | 可选光栅/GPU；其他平台需 `-DLUMEN_SKIA_ROOT=<目录>` |
| zlib | `v1.3.1` | 仅 Windows Skia 构建（补 plain zlib 符号） |

三桌面 CI 基线（`.github/workflows/`）：

| 平台 | runner | 编译器 | 备注 |
| --- | --- | --- | --- |
| Windows | `windows-2025` | MSVC（VS 2025 自带，`/utf-8 /W4`） | CPU Debug / Skia Release / GPU Release |
| Linux | `ubuntu-24.04` | GCC（系统默认，`-Wall -Wextra -Wpedantic`） | CPU Debug + 基准/Xvfb / Skia Release / GPU Release（Mesa llvmpipe）/ mobile-core Debug |
| macOS | `macos-15` | AppleClang（Xcode CLT） | CPU Debug / mobile-core Debug；GPU 非门槛 |

## 2. 统一命令表

| 场景 | 命令 |
| --- | --- |
| CPU-only 构建+测试（默认门槛） | `cmake -S . -B build -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON`<br>`cmake --build build --config Debug`<br>`ctest --test-dir build --output-on-failure -C Debug` |
| CPU-only + 基准 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON -DLUMEN_BUILD_BENCHMARKS=ON`<br>`cmake --build build --config Debug`<br>`ctest --test-dir build --output-on-failure -C Debug` |
| Skia 光栅 | `cmake -S . -B build-skia -DLUMEN_ENABLE_SKIA=ON`<br>`cmake --build build-skia --config Release`（Windows 必须 Release）<br>`ctest --test-dir build-skia -C Release`（含 CPU/Skia 一致性）<br>`./build-skia/examples/counter/lumen-counter --renderer skia` |
| Skia GPU（Ganesh+GL） | `cmake -S . -B build-gpu -DLUMEN_ENABLE_SKIA=ON -DLUMEN_ENABLE_GPU=ON`<br>`cmake --build build-gpu --config Release`<br>`./build-gpu/examples/counter/lumen-counter --renderer gpu --diagnostics` |
| mobile-core（SDL-free） | `cmake -S . -B build-mobile -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_MOBILE_CORE=ON`<br>`cmake --build build-mobile --config Debug`<br>`ctest --test-dir build-mobile --output-on-failure -C Debug` |
| headless smoke | `./build/examples/counter/lumen-counter --headless`<br>`./build/examples/settings/lumen-settings --headless` |
| 窗口 smoke（Linux） | `xvfb-run -a timeout 5 ./build/examples/counter/lumen-counter \|\| test $? -eq 124`<br>`xvfb-run -a timeout 5 ./build/examples/settings/lumen-settings \|\| test $? -eq 124` |
| 窗口 smoke（macOS 无窗口服务器） | `SDL_VIDEODRIVER=dummy ./build/examples/counter/lumen-counter & pid=$!; sleep 10; kill -0 "$pid"`（见 `macos.yml`） |
| 诊断 | `./lumen-counter --diagnostics`<br>`./lumen-counter --renderer gpu --diagnostics --frames 1` |
| 基准（ repeatability 门槛） | `./build/benchmarks/lumen-scene-bench --frames 120 --warmup 10 --json > bench-1.json`<br>`./build/benchmarks/lumen-scene-bench --frames 120 --warmup 10 --json > bench-2.json`（两次 `frame_hash` 必须一致） |
| 基准（M0 归档基线，唯一 canonical） | `cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DLUMEN_BUILD_BENCHMARKS=ON`<br>`cmake --build build-bench --config Release`<br>`./build-bench/benchmarks/lumen-scene-bench --frames 300 --warmup 30 --json > docs/perf-baselines/v0.2-cpu-scene.json` |

说明：

- `build/`、`build-skia/`、`build-gpu/`、`build-mobile/`、`build-bench/` 均为
  out-of-source 构建目录，禁止提交到仓库（见 `.gitignore`）。
- 基准 canonical 命令的参数（`--frames 300 --warmup 30`）、viewport
  （1920x1080）、场景（6x8 卡片网格，见 `scenario=card-grid-6x8-1080p`）、
  后端（`cpu`）、构建类型（Release）共同定义 M0 基线；任何一项不同即为
  不同场景，禁止直接比较数值（见 `perf-baselines/README.md`）。
- M7 只允许引用 `docs/perf-baselines/v0.2-cpu-scene.json` 和上表 canonical
  命令做 CPU 对比；Skia/GPU 和新增场景以各自首次归档报告为基线。
