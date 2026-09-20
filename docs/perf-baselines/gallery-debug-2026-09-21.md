# Gallery Debug 热路径优化配对记录

日期：2026-09-21。源码基点：`af2cbd617a2378b1b74b237bdc2df868cddd595e`。
Windows / MSVC 19.50，AMD Ryzen 7 H 255，8 核 / 16 线程。
这是本机 Gallery CPU 场景的独立配对记录，不替换 M0 或预乘 alpha 迁移基线。

## 改动与边界

- 圆角裁剪：每条扫描线计算保守内部区间并批量填充；字形、图像等单点采样
  先检查缓存的内部矩形。边缘仍执行原有 SDF 与覆盖率混合，保留真实圆角。
- Stretch 布局：交叉轴和主轴预算已确定时直接使用最终约束，拉伸阶段约束
  相同则复用本次布局。避免嵌套容器重复展开子树，不跨帧缓存或跳过应用构建回调。
- 清屏：复用缓冲区，按预乘 RGBA 像素批量填充全帧或 damage，避免逐通道覆写。
- 帧哈希：窗口循环使用 `paintFrame()`；需要确定性哈希的 `renderFrame()`
  保留原有返回值语义，在提交后按需计算并缓存哈希。

Debug 仍为 `/Zi /Ob0 /Od /RTC1 /MDd`，Release 仍为 `/O2 /Ob2 /DNDEBUG /MD`。
没有关闭断言、运行时检查或 STL 调试检查，也没有将 Debug 切换成优化构建。
Gallery 的两种构建都使用 CPU 后端；此次提速不来自切换 GPU。

## 配对方法

使用真实 `GalleryApp`、系统字体（本机 256 faces，GDI+stb）、透明清屏、
1024×768 逻辑及物理像素、DPI 1.0。无调试器。先绘制初始帧，然后依次测量：

1. `renderFrame(true)` 强制完整重绘已有树，不强制重建应用。
2. 在 `nav-buttons` / `nav-inputs` 的中心交替 `pointerMove`，随后 `renderFrame()`。
3. 同上，但事件前额外 `tick(1000 + i * 17)`，形成确定性的动画压力序列。

每个场景 3 次预热、16 次正式迭代，排序后取下标 8 为每次运行的 p50。
对每种构建分别执行两轮：第一轮 before → after，第二轮 after → before。
before 使用修复前静态链接的探针副本，after 链接修复后的同配置库。
测量期间没有并行构建或测试。总时间含事件和 `renderFrame()` 的哈希，
不含窗口呈现、启动和等待时间；计时区外另算一次哈希以检查输出。
悬停各 19 次均走局部重绘，动画悬停各 19 次中有 9 次局部重绘。

以下“汇总”是两次运行各自 p50 的算术平均，**不是合并全部样本计算的 p50**。
单位均为 ms，所有数值均为相同构建、后端和场景内比较。

| 构建 / 场景 | before 两次 p50 | after 两次 p50 | 汇总 before → after | 提速 |
| --- | --- | --- | --- | --- |
| Debug 完整重绘 | 207.113 / 202.776 | 32.725 / 34.025 | 204.945 → 33.375 | 6.14× |
| Debug 悬停 | 181.616 / 178.081 | 57.110 / 60.154 | 179.849 → 58.632 | 3.07× |
| Debug 动画悬停 | 281.786 / 277.124 | 66.629 / 66.245 | 279.455 → 66.437 | 4.21× |
| Release 完整重绘 | 32.986 / 33.352 | 8.372 / 8.073 | 33.169 → 8.223 | 4.03× |
| Release 悬停 | 21.410 / 19.831 | 7.905 / 8.152 | 20.621 → 8.029 | 2.57× |
| Release 动画悬停 | 32.832 / 34.749 | 9.366 / 10.006 | 33.791 → 9.686 | 3.49× |

强制完整重绘中，CPU submit 两次 p50 的平均值为 Debug **193.999 → 22.298 ms**、
Release **28.940 → 4.115 ms**。另外独立 `markDirty()` + `rebuildIfDirty()` 的同口径
时间为 Debug **78.641 → 35.247 ms**、Release **3.008 → 1.313 ms**。
这些是不同计时范围，不能相加；`stats.cpuBuildMs` 只包含命令录制，不包含布局。

配对探针两侧都调用 `renderFrame()`，因此没有把窗口省去哈希的收益计入上述提速。
当前独立哈希约 Debug 4.5 ms、Release 3.7 ms；正常窗口不再支付该哈希开销。
动画使用注入时间戳，不代表实时 FPS。Debug 悬停仍触发整树重建，本记录不保证
60 FPS，也不外推到其他页面、DPI、平台或 GPU 的性能。

## 正确性验证

新增回归覆盖圆角扫描线与逐像素结果相等、嵌套 Stretch 虚拟项仅布局一次、
damage 外像素保留，以及 `paintFrame()` 后哈希缓存的状态 / 强制重绘 / DPI 更新。

最终源码完整构建后运行：

```powershell
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug -j 4
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release -j 4
cmake --build build-gpu --config Release
ctest --test-dir build-gpu --output-on-failure -C Release -j 4
```

- CPU Debug：759 / 759 通过，无跳过。
- CPU Release：759 / 759 通过，无跳过。
- Skia/GPU Release：783 / 783 通过，无跳过；包含提交和回退测试。
- 六个 Gallery 页面 `home/buttons/inputs/lists/collections/theme` × DPI `1/1.25/2`：
  修复前后 18 份完整 RGBA 导出 SHA-256 全部相同，未扩大容差。
- 实际 CPU Debug / Release Gallery 均以 `--max-frames 5 --diagnostics` 启动并正常
  退出，报告 1024×768、DPI 1、602 条命令。末帧 submit 分别为 26.50 / 4.75 ms；
  这是窗口冒烟的单帧数据，不与上表 p50 混算，也不是人工视觉验收。
- 此次没有运行 Linux/macOS；构建中仍存在仓库已有警告，不声称零警告构建。

像素采集命令（两侧使用相同参数，分别写入独立目录）：

```powershell
build/examples/gallery/Release/lumen-gallery.exe --headless --sample-route home --dpi 1 --system-fonts --dump-frame home-1.rgba
```

本机原始探针源码、CMake 工程、修复前探针二进制、8 份
`final-paired-{Debug,Release}-{1,2}-{before,after}.log`、完整测试日志和
`pixels-comparison-final.json` 保存在被 Git 忽略的
`build/debug-perf-investigation/`。这些本机诊断产物不是已提交的仓库依赖。
