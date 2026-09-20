# Lumen 统一构建与验证命令表（M0 基线冻结）

> 状态：M0 基线入口（2026-09）；2026-09-14 统一为三桌面范围。本文件是桌面 CPU、Skia、GPU、
> headless、窗口 smoke 和基准的唯一可追踪验收入口。所有当前里程碑的验证
> 命令必须引用本表，不得使用开发者本地脚本作为门槛依据。
> mobile-core 单列为保留的历史实验配置，不代表 Android/iOS 产品验收。
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
| Linux | `ubuntu-24.04` | GCC（系统默认，`-Wall -Wextra -Wpedantic`） | CPU Debug + 基准/Xvfb / Skia Release / GPU Release（Mesa llvmpipe）/ package；另保留 mobile-core 实验检查 |
| macOS | `macos-15` | AppleClang（Xcode CLT） | CPU Debug / GPU Release / package；另保留 mobile-core 实验检查 |

## 2. 桌面统一命令表

| 场景 | 命令 |
| --- | --- |
| CPU-only 构建+测试（默认门槛） | `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON`<br>`cmake --build build --config Debug`<br>`ctest --test-dir build --output-on-failure -C Debug` |
| CPU-only + 基准 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON -DLUMEN_BUILD_BENCHMARKS=ON`<br>`cmake --build build --config Debug`<br>`ctest --test-dir build --output-on-failure -C Debug` |
| Skia 光栅 | `cmake -S . -B build-skia -DCMAKE_BUILD_TYPE=Release -DLUMEN_ENABLE_SKIA=ON`<br>`cmake --build build-skia --config Release`（Windows 必须 Release）<br>`ctest --test-dir build-skia -C Release`（含 CPU/Skia 一致性）<br>`./build-skia/examples/counter/lumen-counter --renderer skia` |
| Skia GPU（Ganesh+GL） | `cmake -S . -B build-gpu -DCMAKE_BUILD_TYPE=Release -DLUMEN_ENABLE_SKIA=ON -DLUMEN_ENABLE_GPU=ON`<br>`cmake --build build-gpu --config Release`<br>`./build-gpu/examples/counter/lumen-counter --renderer gpu --diagnostics` |
| headless smoke | `./build/examples/counter/lumen-counter --headless`<br>`./build/examples/settings/lumen-settings --headless`<br>`./build/examples/gallery/lumen-gallery --headless` |
| Gallery 固定视觉样本 | `./build/examples/gallery/lumen-gallery --headless --sample-route inputs --sample-key samples-Switch-card --width 600 --height 700 --font-scale 2 --dpi 1.25 --system-fonts --dump-frame build/switch.rgba`（Windows 多配置生成器在可执行文件前增加 `Debug/` 或 `Release/`） |
| 窗口 smoke（Linux） | `xvfb-run -a timeout 5 ./build/examples/counter/lumen-counter \|\| test $? -eq 124`<br>`xvfb-run -a timeout 5 ./build/examples/settings/lumen-settings \|\| test $? -eq 124`<br>`xvfb-run -a timeout 5 ./build/examples/gallery/lumen-gallery \|\| test $? -eq 124` |
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

## 3. 历史 SDL-free 实验配置（保留参考）

Gallery 固定样本参数：`--sample-route` 支持 `home/buttons/inputs/layout/lists/feedback/theme`；可选 `--sample-key` 将该 key 滚到主视口顶部。输入类矩阵 key 为 `samples-TextField-card`、`samples-Checkbox-card`、`samples-Switch-card`、`samples-Radio-card`、`samples-Dropdown-card`、`samples-Tabs-card`；Feedback 使用 `samples-Slider-card`、`samples-ProgressBar-card`、`samples-Tooltip-card`；Buttons 使用 `buttons-matrix-card`。`--direction 0..3` 按 Core Dark / Ink Linen / Aurora Signal / Utility Contrast 排列，`--density 0..2` 为 Compact / Comfortable / Touch，另支持 `--light`、`--high-contrast`、`--reduce-animation`。`--width/--height` 是 logical px，`--font-scale` 与 `--dpi` 分开控制且支持 1–2。`--sample-time` 注入毫秒时钟，仅采样当前场景；过渡的事件触发及 t=0/0.5T/T 由测试驱动。

导出为原始 RGBA8，配套 `<输出文件>.txt` 保存实际像素宽高、逻辑视口、字体来源及样本参数。加 `--system-fonts` 使用本机字体，加载失败返回非零状态；省略时使用确定性测试字体。未传 `--sample-route` 的原有 `--headless` 继续执行全路由交互 smoke。原始帧和转换后的 PNG 应保存在忽略的构建目录。

`LUMEN_BUILD_MOBILE_CORE=ON` 仍存在，Linux/macOS 工作流中的 `mobile-core`
job 也仍会执行。以下命令只验证通用库与 headless 测试，不是 Android NDK/iOS
工程构建，不代表模拟器、真机、移动文本输入或发布通过。M9 暂缓，不新增移动
平台验收任务；本次范围调整不修改构建目标或 CI 行为。

```sh
cmake -S . -B build-mobile -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_MOBILE_CORE=ON
cmake --build build-mobile --config Debug
ctest --test-dir build-mobile --output-on-failure -C Debug
```

## 4. 预乘 alpha 迁移的固定测量入口（P0，2026-09-20）

P0–P5 已交付；调用方格式、ABI 和 v6/v7 迁移见
[迁移说明](lumen-alpha-migration.md)，正式视觉/性能结果及波动调查见
[P4 报告](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)。

P3 窗口生命周期回归默认随 CTest 在 dummy 驱动运行；真实桌面独立运行：
Windows PowerShell 设置 `$env:LUMEN_ALPHA_REAL_WINDOW='1'` 后执行
`./build-alpha-after/tests/Release/lumen-tests.exe platform_alpha_resize_restore_and_rejection_recover`，
随后 `Remove-Item Env:LUMEN_ALPHA_REAL_WINDOW`。Linux/macOS 使用
`LUMEN_ALPHA_REAL_WINDOW=1 ./build/tests/lumen-tests platform_alpha_resize_restore_and_rejection_recover`。
不要设置 `SDL_VIDEODRIVER=dummy`；测试拒绝把 dummy 当作真实窗口。该测试验证呈现、
resize、最小化恢复及模式分派；宿主透明合成仍须按 [P3](perf-baselines/premultiplied-alpha-2026-09-20/P3.md) 的限制单独验收。

格式边界：`PixelBuffer.alphaMode` 默认 `Straight`；读帧应检查实际模式，`Opaque` 是全帧 A=255 的内容保证。公共转换返回 `bool`，失败不改变目标；内容校验用于接纳边界，`PixelValidation::Structure` 仅用于生产者已保证内容的内部帧。CPU 累积/图片缓存与 Skia 读回均为 `Premultiplied` / `Opaque`；读取 CPU 帧不生成直通副本。

Gallery 的 `--dump-frame`（headless 与固定 sample）仍输出直通 RGBA，附带 `alpha_mode=straight`、width/height；PNG 转换可按原有 RGBA 方式读取。以下诊断 benchmark/gallery 工具保存的是实际帧模式，须先按元数据归一化。命令记录现在写 v7（像素字段依次为 width/height/alphaMode/byteCount，均 u32 小端），读 v6/v7；v6 图片默认直通，旧程序拒绝 v7。公开结构布局已变化，使用方须重编译。

启用 `LUMEN_BUILD_BENCHMARKS=ON` 后，桌面构建还提供以下目标。
它们只增加场景、导出和观测，不改变绘制算法。Windows 多配置生成器在
可执行文件前增加 `Release/`，文件扩展名为 `.exe`。

```sh
./build-alpha-before/benchmarks/lumen-alpha-bench --backend cpu --scenario layers --warmup 30 --frames 300
./build-alpha-before/benchmarks/lumen-alpha-bench --backend cpu --scenario probe --present software --height 1080 --warmup 30 --frames 300
./build-alpha-before/benchmarks/lumen-alpha-bench --backend cpu --scenario probe --present texture --height 2160 --warmup 30 --frames 300
./build-alpha-before/benchmarks/lumen-alpha-gallery build-alpha-before/gallery
python benchmarks/run_alpha_baseline.py --build build-alpha-before --revision SOURCE_AND_DIFF_ID --output build-alpha-before/reports --windows
python benchmarks/alpha_frames.py build-alpha-before/reports/before-alpha-edges-1.rgba
```

- `lumen-alpha-bench` 固定 `layers`（八层半透明矩形）、`edges`（圆角、嵌套裁剪、描边、文字、图标和阴影）、`images`（一次上传后六次绘制）、`upload`（每帧重新上传同图）、`probe`（半透明纯红）五种输入。高度只允许 1080/2160，宽高比 16:9；DPI=1。`--opaque` 改用不透明黑清屏和不透明窗口。`--backend skia` 需要 Skia 构建。
- `--present none|software|texture` 分别为 headless、真实软件表面和 SDL renderer；默认为 none。禁止以 dummy 驱动结果代替窗口验收。`vsync_requested=false` 仅表示关闭请求，系统合成器仍可能等待；报告同时记录输入和窗口实际物理尺寸。
- `submit` 包含清屏/命令回放/帧发布；`present_prepare` 只计 Lumen 转换与副本；`host_submit` 为 present 剩余部分，包含 SDL 上传、blit 与合成器等待；`total` 已包含 submit 和整个 present，不可重复相加。命令构造、图片上传、事件泵、诊断读回、哈希与文件输出不在 total 中；上传单列冷启动与稳态耗时。
- `PlatformWindow::setPresentDiagnosticsEnabled(true)` 才启用逐次计时。转换次数、转换字节、Lumen 复制字节、scratch capacity 属于 Lumen 格式准备，不能解释为整个 SDL/进程内存或所有上传复制成本。
- 两个 bench 均支持 `--dump-frame PATH`，输出实际缓冲格式，配套 `.txt` 标明 `alpha_mode`；alpha bench 还输出 `.commands` 作为版本化回放证据。这些是诊断产物，和 Gallery 应用默认直通导出区分。
- `lumen-alpha-gallery` 使用确定性占位字体，固定 Core Dark/light、DPI=1/1.25/2、透明清屏，保存普通、close hover/pressed、最大化、菜单、Controls 和 Buttons 的 42 帧及哈希。不会改变 Gallery UI 或主题数值。
- Python 工具只需 Python 3 标准库。基线驱动顺序运行三组 warmup=30/frames=300 并检查哈希重复性；P4 使用 `--peer-build AFTER_BUILD --peer-revision AFTER_ID` 同机交错运行 before/after。采样时停止构建与其他重负载，不比较 CPU-only 与 Skia 构建。
- canonical `resource-upload` 在 paint 外上传八张 120×80 图片，其现有 JSON 不导出私有 `uploadPhase_`；paint 下降不能代表上传也加速。`lumen-alpha-bench --scenario upload` 单列每帧 256×256 图片接纳成本，total 仍不含 upload。
- `alpha_frames.py` 生成 alpha、黑底、白底、棋盘底 PNG；`--compare BASELINE.rgba` 在共同预乘表示中输出差异空间图和统计，冻结容差为 RGB 最大 4、alpha 精确相等、超差比例 0。此规则用于相同 CPU 几何迁移；不用于 CPU/Skia 不同 AA 算法。
- P0 另发现旧 CPU 直通舍入到 256 后回绕的缺陷（图片叠加竖条）。`python benchmarks/alpha_image_reference.py build-alpha-before/reference-images.rgba --before build-alpha-before/reports/before-alpha-images-1.rgba` 生成独立目标算术参考，并要求 alpha 与旧帧完全相等。P2/P4 的 `images`/`upload` 修复应与该参考逐字节相等（`alpha_frames.py AFTER.rgba --compare build-alpha-before/reference-images.rgba --exact`），不能放大普通场景容差，也不能把旧条纹作为目标画面。最小数值复现和解释见 P0 记录。

完整 before/after 复测需事先保留迁移前的 P0 工具/算法构建，不能用当前源码重建 before。
下列 `BEFORE_ID` / `AFTER_ID` 应填写实际提交与差异标识，目录须使用两套对应的 Release 构建：

```sh
python benchmarks/run_alpha_baseline.py --build build-alpha-before --revision BEFORE_ID --peer-build build-alpha-after --peer-revision AFTER_ID --output build-alpha-after/paired --windows
python benchmarks/compare_alpha_baseline.py build-alpha-after/paired build-alpha-after/paired-summary.json
```

Skia 使用独立 before/after 目录并加 `--backend skia`。`--windows` 表示增加真实窗口场景，
不限定操作系统；桌面必须支持所选路径。驱动记录 executable SHA256、工具链和 CMakeCache。
Windows 每 200 ms 额外观察实际客户端尺寸/最小化状态，报告中的 drawable 必须与请求一致；
无效记录写 `.rejected.json`，不得通过改变报告尺寸纳入比较。4K drawable 不等于物理 4K 屏幕。

中断后使用同一命令加 `--resume`。驱动核对来源、二进制 SHA、配置、完整命令及帧数；
仅保留完整配对，半对或带 `.pending` 标记的配对两端全部重跑。不要手工删除该标记来接受旧/新混合结果。
二进制重建后应使用新输出目录，不能续接原采样；本次 P4 留存报告的来源说明见基线 README。
比较工具要求至少三组完整配对和稳定 hash/模式，保留逐组 p50/p95 与 >10% 标记，
不自动把超限判为噪声，也不替代人工 review。`median_paired_delta_percent` 为组内变化率中位数；
`median_delta_percent` 是两端绝对耗时中位数之比，二者不可混称。

P4 另提供只提交像素、不执行渲染的呈现探针，用于控制宿主提交节奏：

```sh
./build-alpha-after/benchmarks/lumen-alpha-present-probe --host transparent --mode straight --period-ms 40
./build-alpha-after/benchmarks/lumen-alpha-present-probe --host transparent --mode premultiplied --period-ms 40
./build-alpha-after/benchmarks/lumen-alpha-present-probe --host opaque --mode opaque --period-ms 18
```

固定 native software、1920×1080、warmup=30/frames=300；模式可选 straight/premultiplied/opaque，
host 可选 transparent/opaque，间隔为 0–100 ms（0 连续提交，默认 40）。透明 fixture 的 A=128，
因此拒绝 opaque 模式；不透明 host 的三种标记使用相同 A=255 字节。间隔等待不含在 present 计时内，
同时报告实际 frame interval、host/prepare/present 分位数和 overruns。探针拒绝 dummy、尺寸变化、
最小化和长时间采样中断；只能补充宿主成本调查，不能替代正式渲染配对或证明透明合成器视觉。

各阶段证据见 [alpha 计划](lumen-premultiplied-alpha-rendering-plan.md)、
[P0 记录](perf-baselines/premultiplied-alpha-2026-09-20/P0.md) 和
[P4 记录](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)。
