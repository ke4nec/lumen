# datagrid-wide 首跑基线（M14-D 第六批，lumen-datagrid-design §19 T6.5）

- 场景：`datagrid-wide-1000-1080p`（100 列 × 1000 行，列宽 80，滚动内容
  8000px；每帧横向滚动 32px 触底回绕 + 活动行文本变化）。
- 生成命令：`lumen-scene-bench --scenario datagrid-wide --frames 300
  --warmup 30 --json`（本地 Linux / GCC 15.2.0 / Release / CPU 后端 /
  1080p headless）。
- 归档：`datagrid-wide-1000-cpu.json`（300 帧测量）。
- 确定性：两次独立运行 `frame_hash=0130592730defdce` 一致。
- 首跑相位（p50）：frame 312.2ms / layout / paint / submit 为主导 =
  每帧真实格构建与文本 shaping 负载（场景设计目标：列窗口滑动下的
  物化/回收成本）。
- **同日重归档注记**：首跑归档（4d439f0470edd282）后 review 发现根
  横向视口缺 `showScrollbar`（横向滚动无可视滚动条/拇指拖动），修复
  后 thumb 绘制进入帧输出 → 帧哈希变化；按 perf gate 流程同场景同
  工具链重跑归档本文件（未改阈值）。
- 物化预算（确定性断言在 `tests/datagrid_tests.cpp`
  `datagrid_wide_materializes_only_visible_column_window`）：物化格数
  O(可见行 × 可见列窗口)，与 100 列总量无关。
- 本基线为新场景首跑归档，无既往比较对象；后续改动按 perf gate 同后端
  同场景 10% 门槛比较。GPU/Skia 后端首跑留待对应 CI 变体。
- **§21 review 收口重归档**：表头表面/列对齐/格式盒 Row 化/手柄静止
  透明等变更后按 perf gate 同场景同工具链重跑归档本文件（未改阈值）：
  frame p50 322.3ms（对前档 +3.2%，10% 门槛内）；nodes 2143 → 2081
  （滚动区去重复选择列）；commands/frame 4166 → 3982（列宽手柄静止
  轨道不再绘制）。两次独立运行 `frame_hash=9de43d88ca5bf861` 一致。
- **§20.4 冻结行 hover 收口重归档**（2026-09-29，冻结区行置
  `collectionRow` 后每条冻结行绘制分隔线）：commands/frame 3982 → 4014
  （+32 ≈ 可见冻结行数，+0.8%）；nodes 2081 不变；
  `frame_hash=9de43d88ca5bf861 → 4c641cd57f2a4f11`。
  **门槛判定**：同配置（无优化构建目录，与原归档一致）同机交错 A/B
  三轮（变更前 `9de43d88` 二进制 vs 当前 `4c641cd5` 二进制，taskset
  固定核）：frame p50 delta 中位 **+0.42%**（layout +0.43% / paint
  +0.58% / submit +0.79%），10% 门槛内——与命令增量量级吻合。
  **环境注记**：本档绝对值（frame p50 ≈371–375ms）较前档（322ms）高
  ~15%，为归档窗口整机后台负载所致（同一前档哈希二进制在当前窗口
  复测同为 ~372ms，A/A 自对照证实环境漂移而非代码回归；M14-B 既有
  同类先例）。三轮 A/B 中前档二进制哈希与原归档完全一致，二进制
  保真。确定性：A/B 三轮 + 独立复跑 `4c641cd57f2a4f11` 全部一致。
  本场景不属 CI perf gate 五场景清单，重归档仅为证据链延续。
- **构建目录口径勘误**：本目录（build-release）CMakeCache 实际
  `CMAKE_BUILD_TYPE` 为空（无优化构建），本文件历史各档均在该配置下
  生成（JSON `build_type` 字段如实记录为 `""`）；README 此前"Release"
  表述指目录名而非优化配置。json `build_type` 字段为事实来源。
