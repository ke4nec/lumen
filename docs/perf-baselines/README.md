# Lumen 性能基线说明（M0 冻结）

> 状态：M0 基线入口。`v0.2-cpu-scene.json` 是 v0.2 CPU 固定场景的唯一
> 归档基线；M7 的 CPU 对比只允许引用该文件和确切生成命令。

## 1. 基线文件

- `v0.2-cpu-scene.json`：CPU 后端、固定 1080p 卡片网格场景的归档基线。
- `ci/`：M7 CI 门槛基线，按 CPU、Skia 光栅和 Skia Ganesh GPU 分目录命名，
  覆盖 card-grid、text-heavy、Grid、VirtualList、semantic diff 五个固定场景。
- 生成命令（canonical，参数、viewport、场景、后端、构建类型共同定义基线）：

```sh
cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DLUMEN_BUILD_BENCHMARKS=ON
cmake --build build-bench --config Release
./build-bench/benchmarks/lumen-scene-bench --frames 300 --warmup 30 --json > docs/perf-baselines/v0.2-cpu-scene.json
```

- 固定项：`viewport=[1920,1080]`、`scenario=card-grid-6x8-1080p`
  （6x8 卡片网格，约 250 RenderNodes，每帧 1 张卡片变更）、
  `warmup_frames=30`、`measured_frames=300`、`backend=cpu`、
  `build_type=Release`。
- JSON 字段：`backend`、`scenario`、`viewport`、`warmup_frames`、
  `measured_frames`、`toolchain`、`build_type`、`frame_hash`、
  `phases.{frame,reconcile,layout,paint,submit,gpu_wait}.{p50_us,p95_us,mean_us,allocs_p50,alloc_bytes_p50}`，
  以及 `commit`、`platform`、`nodes`、`commands_per_frame`、
  `culled_commands`、`partial_repaint_frames` 元数据。
- CI artifact：Linux `cpu`、`skia`、`skia-gpu` job 对每个固定场景运行三次，
  报告与 `frame_hash` 一致性门槛一起上传（`bench-*.json`），并调用
  [`check_perf_regression.py`](../../benchmarks/check_perf_regression.py) 比较
  归档基线。报告附带 `commit`（`GITHUB_SHA`）、`platform`、配置和工具链元数据；
  归档基线本身的元数据由生成时环境写入。

## 2. 比较规则（强制）

- 性能比较按“同一后端 + 同一场景 + 同一 viewport”进行；构建类型和工具链
  作为报告元数据保留，跨工具链的报告不得覆盖归档基线。
- 当前门槛由 `ci/reference.json` 固定基准源码提交，同一 runner 交替测量
  基准与候选，双方三次取中位数；报告同时校验工具链、采样参数、机器身份、
  场景/依赖源码哈希和二进制哈希。旧最大值聚合的 `working-tree` 报告仅供历史参考。
- CPU、Skia、GPU 分别归档本次同机测量的基线；文本密集、Grid、VirtualList、
  语义 diff 等场景均进入固定门槛。
- `GPU wait` 只在 GPU 基线中比较，不与 CPU 数值互比。
- 禁止用不同场景（不同 viewport/卡片规模/warmup/测量帧数）或不同后端
  直接比较 p50/p95；比较结果必须记录基线文件、当前报告、后端、场景参数
  和不适用指标。
- p50/p95 的 frame、reconcile、layout、paint、submit、GPU wait，以及 p50 分配量和
  `commands_per_frame` 相对适用的归档基线不得恶化超过 10%（M7 门槛）；节点数
  必须保持一致，固定场景 `frame_hash` 不变表示像素路径未漂移。

## 3. 可重复性

- 同一机器同配置下，两次运行的 `frame_hash` 必须一致（CI repeatability gate）。
- 基线测试可在干净构建目录重复运行（见 `../build-commands.md` 统一命令表）。

## 4. 预乘 alpha 迁移的独立配对记录

[`premultiplied-alpha-2026-09-20/P0.md`](premultiplied-alpha-2026-09-20/P0.md)
记录迁移前源码事实、数值规则、真实窗口转换成本与工具链。
该目录独立保留每组三次结果及后续 before/after 配对，不覆盖上述 M0 历史基线。
新报告的 `alpha_mode` 与帧哈希一起解释；`frameHash` 仍只计算宽高和字节。
Windows/MSVC 本机配对不能和旧 Linux/GCC 报告直接计算性能回归率。
计时范围、scratch 与转换计数及固定命令见 `../build-commands.md` §4。

[P4 最终报告](premultiplied-alpha-2026-09-20/P4.md) 归档 CPU/Skia 各 90 次正式运行、
62 帧模式/哈希映射及宿主/同二进制对照；`P4-cpu.json`、`P4-skia.json` 保留每组 p50/p95
和全部 >10% 标记，`P4-controls.json` 记录追加调查，`P4-frames.json` 记录视觉比较对象及原因。
CPU 透明叠层 submit p50 的三组配对变化中位数为 -41.15%，正常透明呈现转换和 scratch 为零；
CPU 图片接纳增加约 0.24 ms/256×256、边缘场景约 +6%，Skia 无可靠 headless 提速结论。
这些是固定场景的 Windows 实测，不能扩展为整应用、GPU 或其他操作系统的速度保证。

| 归档中的来源标识 | 对应源码/用途 |
| --- | --- |
| `e06f3d9+P0-instrumentation` | P0 提交 `a28c26f` 的 before 算法和测量工具；冻结目录不重建 |
| `a45ebd6` | P3 Skia raster 被测二进制；原 SHA 与 `archived_path` 指向的归档核对一致 |
| `a45ebd6+P4-inline` | P4 提交 `316127a` 中的 CPU 内联修复；正式 CPU after 来源 |

P5 只修改文档与注释，并重建执行正确性测试，未重新采样性能。该重建会改变本机构建目录里的
after 可执行文件 SHA；JSON 保存的是**测量时**的 SHA 与路径，不是对后来同路径文件的声明。
不要用重建后的二进制续跑 P4 的 `--resume`；保留原始报告，新的复测使用独立输出目录。
CPU 初轮 P3 和正式 Skia P3 的诊断归档仍各自保留，不作为 CPU 最终内联版的替代。

历史 M0 Linux/GCC 的哈希与本机 P0 已不同；本次只解释本机迁移前后变化，不改写 M0。
普通 CPU 视觉容差、图片独立参考和未运行平台事项均按 P4 保留；迁移使用方式见
[调用方说明](../lumen-alpha-migration.md)。

后续整体 review 补上汇总器的 manifest 来源绑定与跨组计时阶段一致性检查；原 P4 的 CPU/Skia
各 90 份报告全部通过加强后的检查，逐组数据和汇总与归档精确相同，没有重采样或改写基线。
回归与 Linux headless 补验记录见 [续验记录](premultiplied-alpha-2026-09-20/follow-up.md)。

## 5. Gallery Debug 热路径优化

[2026-09-21 配对记录](gallery-debug-2026-09-21.md) 保留同一 Windows/MSVC 主机上
CPU Debug / Release 的完整重绘、悬停和动画悬停 before/after，以及像素与测试验证。
该记录使用 1024×768 Gallery 系统字体场景，独立于上述固定场景基线；Debug 调试
编译开关保持不变，性能收益来自圆角内部批量填充、减少重复布局与清屏等算法改动。
