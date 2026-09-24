# M14-B 性能门槛收口证据（2026-09-24）

本目录归档 M14-B（性能门槛收口）的本地门禁运行判定文件。命令、runner
与复现方式见下；每次运行的完整逐轮报告（含 p50/p95、分配计数、命令数、
节点数、frame hash、二进制摘要、runner 元数据）在运行输出目录
`gate-{cpu,skia,gpu,aa2}/`（本机 `.m14-scratch/`，未入库——判定文件已
含两端 commit/二进制/tolerance 的可追溯锚点）。

## 运行环境

- 本机 Linux 桌面（GNOME Wayland + XWayland），Ubuntu 24.04 级工具链。
- 基线：pinned CI 参考提交 `445bae5`（`docs/perf-baselines/ci/reference.json`），
  干净工作树 `.perf-reference` 分别构建 CPU/Skia/GPU 参考二进制。
- 候选：当前工作树（Release，与参考同 `CMAKE_BUILD_TYPE`）。
- 门禁命令与 CI 一致（`benchmarks/run_perf_gate.py`；交错采样 ×5 轮、
  warmup 30 / measured 300、min+median 双聚合、10% 门槛）。
- GPU 在 llvmpipe（`SDL_VIDEODRIVER=x11` + 真实 XWayland 显示——本机
  Wayland 会话下 SDL 默认 wayland 驱动走 EGL 不可用；CI 的 xvfb 路径
  不受影响）。

## 结果（15/15 全过 + A/A 5/5）

| 后端 | 场景结果 | 判定文件前缀 |
| --- | --- | --- |
| CPU | 5/5 PASS（card-grid/text-heavy/grid/virtual-list-1000/semantics-diff） | `cpu-gate-*.json` |
| Skia 光栅 | 5/5 PASS（同上五场景） | `skia-gate-*.json` |
| Skia GPU（llvmpipe） | 5/5 PASS（同上五场景） | `gpu-gate-*.json` |
| A/A 自对照（CPU，同二进制） | 5/5 PASS | `aa-gate-*.json` |

## 环境不稳定记录（规则执行）

首次 A/A 运行 submit p95 出现 +54% 自回归——运行期间本机正执行 1 小时
双窗口浸泡采样与并行构建（CPU/内存带宽争抢）。按 M14-B 规则（共享
runner 自对照失败只能标记环境不稳定，不得通过放宽阈值/换聚合消除），
该次结果标记为环境噪声；在浸泡结束、构建停止后的静默环境复跑，5/5 全
过（本目录 `aa-gate-*.json` 即复跑判定）。未修改任何阈值或基线。

## 复现

```sh
git worktree add .perf-reference 445bae5   # pinned 基线
# 分别构建 .perf-reference/{build,build-skia,build-gpu} 与候选 bench
python3 -B benchmarks/run_perf_gate.py --backend <cpu|skia|gpu> \
  --baseline-source .perf-reference \
  --baseline-bin .perf-reference/build[-skia|-gpu]/benchmarks/lumen-scene-bench \
  --current-bin build-<cfg>/benchmarks/lumen-scene-bench --output <out>
# A/A：baseline 与 current 指向同一二进制（reference.json 的 commit 换成
# 该二进制的干净源提交，见 benchmarks/run_perf_gate.py 的 manifest 校验）。
```
