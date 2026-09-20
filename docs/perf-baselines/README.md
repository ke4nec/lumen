# Lumen 性能基线说明（M0 冻结）

> 状态：M0 基线入口。`v0.2-cpu-scene.json` 是 v0.2 CPU 固定场景的唯一
> 归档基线；M7 的 CPU 对比只允许引用该文件和确切生成命令。

## 1. 基线文件

- `v0.2-cpu-scene.json`：CPU 后端、固定 1080p 卡片网格场景的归档基线。
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
  `phases.{reconcile,layout,paint}.{p50_us,p95_us,mean_us,allocs_p50,alloc_bytes_p50}`，
  以及 `commit`、`platform`、`nodes`、`commands_per_frame`、
  `culled_commands`、`partial_repaint_frames` 元数据。
- CI artifact：在 Linux `cpu` 门槛 job 中，两次
  `--frames 120 --warmup 10 --json` 运行的报告与 `frame_hash` 一致性门槛
  一起上传（`bench-*.json`），附带 `commit`（`GITHUB_SHA`）、`platform`
  （`RUNNER_OS`）、配置（Debug/CPU）元数据；归档基线本身的 `commit`/
  `platform`/`toolchain` 由生成时环境写入。

## 2. 比较规则（强制）

- 性能比较按“同一后端 + 同一场景 + 同一工具链 + 同一构建类型”进行。
- CPU 复用本目录的 v0.2 基线；Skia/GPU 先各自记录 M7 初始基线；文本密集、
  Grid、VirtualList、语义 diff 等新增场景以首次归档报告为基线。
- `GPU wait` 只在 GPU 基线中比较，不与 CPU 数值互比。
- 禁止用不同场景（不同 viewport/卡片规模/warmup/测量帧数）或不同后端
  直接比较 p50/p95；比较结果必须记录基线文件、当前报告、后端、场景参数
  和不适用指标。
- p50/p95 总帧时间、UI 构建、layout、paint、submit 相对适用的归档基线
  不得恶化超过 10%（M7 门槛）；固定场景 `frame_hash` 不变表示像素路径未漂移。

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
