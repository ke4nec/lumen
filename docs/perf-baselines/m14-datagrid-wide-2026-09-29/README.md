# datagrid-wide 首跑基线（M14-D 第六批，lumen-datagrid-design §19 T6.5）

- 场景：`datagrid-wide-1000-1080p`（100 列 × 1000 行，列宽 80，滚动内容
  8000px；每帧横向滚动 32px 触底回绕 + 活动行文本变化）。
- 生成命令：`lumen-scene-bench --scenario datagrid-wide --frames 300
  --warmup 30 --json`（本地 Linux / GCC 15.2.0 / Release / CPU 后端 /
  1080p headless）。
- 归档：`datagrid-wide-1000-cpu.json`（300 帧测量）。
- 确定性：两次独立运行 `frame_hash=4d439f0470edd282` 一致。
- 首跑相位（p50）：frame 338.9ms / layout 128.0ms / paint 146.5ms /
  submit 63.8ms / reconcile 1.7ms。layout+paint 主导 = 每帧真实格构建
  与文本 shaping 负载（场景设计目标：列窗口滑动下的物化/回收成本）。
- 物化预算（确定性断言在 `tests/datagrid_tests.cpp`
  `datagrid_wide_materializes_only_visible_column_window`）：物化格数
  O(可见行 × 可见列窗口)，与 100 列总量无关。
- 本基线为新场景首跑归档，无既往比较对象；后续改动按 perf gate 同后端
  同场景 10% 门槛比较。GPU/Skia 后端首跑留待对应 CI 变体。
