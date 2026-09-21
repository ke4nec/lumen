# Lumen：C++20 自绘 GUI 框架 v0.2 桌面 GPU 与性能工程计划

> 后续面向自用工具类应用的桌面路线图见
> [`lumen-self-use-roadmap.md`](lumen-self-use-roadmap.md)。
> 当前平台范围（2026-09-14）：Windows/Linux/macOS；Android/iOS 暂缓，不预排后续版本。

## 1. 版本定位与当前基线

v0.2 建立在 `docs/lumen-gui-framework-plan.md` 的阶段 0–6 之上。阶段 6
已经覆盖核心树、Box/Flex 布局、CPU/Skia 光栅、SDL3 Windows/Linux 平台、
交互、文本 DSL、脏矩形、基础动画、手势、图片生命周期和 DSL 热重载；这些
能力在进入 v0.2 实现前必须先完成现有工作区改动的构建、测试和提交冻结。

v0.2 的目标是把“能运行的自绘框架”升级为“可测量、可回退、可发布的桌面
渲染运行时”。v0.2 首期只承诺 Windows 与 Linux 桌面，保持单 UI 线程；macOS
桌面在 v0.3 扩展。Android/iOS 暂不规划，跨线程 UI 树仍不在当前范围。

## 2. 目标、非目标与不变量

### 2.1 目标

- 让 CPU、Skia 光栅和 Skia GPU 消费同一份绘制命令。
- 在窗口空闲时停止重复提交，在输入、动画、resize 和资源完成时合并帧请求。
- 通过异步资源读取与解码降低 UI 线程阻塞，并在 GPU 设备重建后重新上传资源。
- 在 GPU 不可用、初始化失败或 device lost 时自动回退 CPU，应用状态和 UI 树不丢失。
- 提供可重复的 headless 基准、运行时诊断和 Windows/Linux CI 验收。

### 2.2 非目标

- v0.2 不增加新的上层控件集合，不实现完整无障碍语义、复杂富文本排版、
  多窗口、脚本化 DSL 或移动端生命周期。
- 不同时实现 SDL3 GPU 和 Skia GPU 两套首期后端。
- 不把 Widget、Element、StateStore 或 DSL 迁移到后台线程。

### 2.3 必须保持的不变量

- `lumen-core` 的公共头文件不包含 SDL、Skia 或其他平台类型。
- 现有 CPU-only 构建、确定性 frame hash、阶段 0–6 测试和 counter 行为保持兼容。
- 所有 GPU 资源由代际安全句柄引用；异步任务完成顺序不能复活已释放资源。
- 每次局部重绘必须能证明命令和缓存覆盖完整；否则退回全帧绘制。

## 3. 公共接口与模块边界

### 3.1 绘制命令

在 `lumen-render` 增加值类型 `RenderCommandList` 和 `FrameInfo`。Painter 只
负责把 `RenderNode` 转换为命令；Renderer 负责验证、回放和提交。命令至少
覆盖现有矩形、圆角、裁剪、文本、图片，并预留变换和透明度字段。

`Renderer` 保留现有生命周期入口，同时增加以下能力：

- `capabilities()`：报告后端是否支持 GPU、局部提交、文字和图片。
- `submit(const RenderCommandList&, const FrameInfo&)`：提交一帧命令。
- `stats()`：返回本帧 CPU 构建、提交、GPU 等待、缓存命中和资源上传数据。
- `resetSurface(RenderSurfaceDesc)`：在 drawable size、DPI 或设备重建后重建目标。

旧的 `beginFrame/save/restore/draw*/endFrame` 路径通过适配器继续可用，保证
现有 `CpuRenderer` 测试无需改写。

### 3.2 平台窗口与帧调度

`PlatformWindow` 增加不透明的 `NativeSurfaceHandle`、可见性/最小化状态、
VSync 设置和 present 结果；句柄只在 `lumen-platform` 与 Renderer 适配层使用。
窗口事件仍转换成平台无关事件，resize、DPI、显示器切换和关闭事件必须先于
下一帧 surface 重建处理。

新增 `FrameScheduler`，由应用主循环拥有：

1. 平台事件入队并合并 invalidate 原因。
2. UI 线程在一次 turn 内完成状态回调、build/reconcile、layout 和命令录制。
3. Scheduler 按 VSync/目标帧率决定是否提交；无 dirty、无动画、无资源完成时不提交。
4. Renderer 提交完成后清除已消费的原因；新事件只能请求下一帧，不能重入当前帧。

Scheduler 必须支持输入优先、动画 deadline、resize 防抖、窗口最小化暂停和
显式 `requestFrame(Reason)`；时间源可注入，以便 headless 测试确定性运行。
动画由活跃转静止的转换必须补交一帧终拍（2026-09-21）：动画驱动的终值样本
（tween 终拍标记的终态树）在动画态不再驱动提交后没有其他提交通道，不补交
则屏幕停留在最后一个中间样本——慢帧率（Debug）下可见，如菜单淡入卡在半透
明直到下一次输入帧。从开始就启用 `reduceAnimation` 的动画不驱动连续提交；
运行中开启该设置会截断既有动画，仍必须保留一次终态提交。

应用壳的待绘制内容与连续动画状态独立：`markDirty`、已重建尚未绘制的树、
显式全量重绘都必须由主循环合并成提交请求；一次性回调即使返回“无后续动画”
也不能丢帧。绘制期间 `onRebuilt` 再次标脏须在等待前挂起下一帧；已有 Resize
原因继续使用原有防抖。离散 deadline 在帧预算内到期时，等待剩余预算而非无限
空闲。`shouldSubmitFrame()` 成功至 `markFrameSubmitted()` 之间禁止重入，
期间到达的请求/终拍保留到下一帧（包括没有 pending 位的纯动画帧）。

### 3.3 资源管理

新增 `ResourceManager` 与 `ResourceHandle`。后台 worker 只执行受限文件读取、
图片/字体解码和校验，产出不可变 CPU 数据；UI 线程提交 upload/unload 命令，
GPU 对象只在 Renderer 所属线程创建和销毁。

资源状态至少包括 `Loading`、`Ready`、`Failed`、`Cancelled` 和 `Evicted`；
未就绪资源绘制固定占位内容，失败信息进入诊断统计但不能阻塞事件循环。资源
缓存必须有字节上限、取消语义、设备重建后的重新上传和析构时的队列清理。

## 4. 分阶段实施路线

### 阶段 7A：冻结基线与测量设施

- 合并并冻结阶段 6 的工作区改动，记录 CPU-only、Skia 光栅和现有 headless
  counter 的行为基线。
- 建立固定 1080p 场景，输出每帧阶段耗时、分配量、绘制命令数和 frame hash。
- 增加 `LUMEN_ENABLE_GPU=OFF` 默认开关，以及独立的 `LUMEN_BUILD_BENCHMARKS`。

出口条件：阶段 0–6 全部测试通过，CPU 基线可重复，基准报告可在 CI 保存。

### 阶段 7B：命令录制与 CPU 兼容适配

- 实现 `RenderCommandList`、序列化/回放测试和 `RendererCapabilities`。
- 让 Painter 只生成命令；CPU Renderer 先完成命令回放，像素结果与现有测试一致。
- 将 damage bounds、preserve 模式和 paint cache 接到命令范围；无法局部重放时
  明确记录 full-frame fallback 原因。

出口条件：旧 Renderer 调用方可编译，CPU 全量/局部重绘像素一致，命令回放
在不同后端之间不改变节点顺序和裁剪语义。

### 阶段 7C：Skia GPU 首期后端

- 只实现 Skia Ganesh GPU 路径；Graphite 不作为 v0.2 依赖，待后续版本评估。
- 在 Windows 使用可用的 D3D/OpenGL 上下文，在 Linux 使用 EGL/GL 或 Vulkan
  中经过探测后选择的上下文；平台差异封装在 `lumen-platform`。
- 实现纹理、裁剪、透明度、文字和 surface resize；Skia 光栅继续作为独立后端。
- 设备初始化失败、上下文丢失、shader/纹理上传失败时切换 CPU，并保留一次
  可控的重新探测入口，避免每帧反复重试。

出口条件：GPU 可用和不可用两种环境都能启动 counter；点击、输入、resize、
热重载和退出行为一致；GPU 诊断日志包含选择结果和回退原因。

### 阶段 7D：调度、异步资源与恢复

- 接入 `FrameScheduler`，替换示例中的固定 `SDL_Delay(16)` 循环。
- 增加资源 worker、上传队列、代际句柄、缓存预算和取消/失败测试。
- 覆盖最小化恢复、连续 resize、显示器 DPI 变化、GPU 重建和热重载期间的资源
  一致性；所有 UI 状态更新仍在 UI 线程完成。

出口条件：空闲窗口不提交重复帧；输入和动画按 deadline 调度；资源加载不会
阻塞事件处理；设备恢复后旧句柄不会绘制错误内容或泄漏。

### 阶段 7E：桌面发布门槛

- 为 Windows/Linux 增加 CPU-only、Skia 光栅、Skia GPU/软件 GPU 四类 CI 矩阵；
  硬件 GPU 只作为增强 smoke，软件适配器是强制门槛。
- 打包 SDL、Skia 运行库和符号/诊断文件，明确 Debug、Release 和 GPU 开关组合。
- 输出运行时后端、设备、DPI、帧耗时和回退事件；文档补充故障排查和支持矩阵。

## 5. 测试与验收标准

- **命令与后端**：命令顺序、嵌套裁剪、透明度、图片和文字回放；CPU 与 Skia
  光栅保持现有像素断言，GPU 使用容差快照和关键像素/几何断言。
- **调度**：invalidate 合并、空闲不绘制、动画 deadline、resize 防抖、最小化
  暂停、VSync 开关和时间源注入。
- **资源**：并发加载、取消、失败、淘汰、代际句柄、设备重建重新上传和析构清理。
- **集成**：counter 在四种后端/回退模式完成点击、输入、拖动、resize、热重载。
- **稳定性**：重复创建销毁窗口、连续 DPI 切换、上下文丢失模拟、长时间空闲
  和资源压力运行无崩溃、无泄漏、无卡死。
- **性能**：以阶段 7A 记录的 CPU 基线为比较对象；固定场景的 p50/p95 总帧时间、
  UI 构建时间和提交时间不得比基线恶化超过 10%，GPU 路径必须在同一场景提供
  可解释的统计数据。硬件相关数值只作报告，不作为跨机器的唯一通过条件。

每个阶段都必须先通过对应 headless/单元测试，再进入窗口 smoke；完整门槛仍为：

```sh
cmake -S . -B build -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

## 6. 交付物、风险与后续版本

v0.2 交付 `RenderCommandList`、Skia Ganesh GPU 后端、`FrameScheduler`、
`ResourceManager`、CPU 回退/设备恢复、性能基准、运行时诊断、CI 矩阵和更新后的
README/平台支持说明。任何新增依赖必须在 CMake 中固定版本并在文档中说明。

主要风险是 Skia 预编译包的 GPU 构建能力、Linux 驱动差异和跨线程资源析构时序。
风险控制顺序固定为：先 CPU 命令适配，再 GPU 最小路径，再资源异步化；任一阶段
失败都不能破坏已验证的 CPU 路径。

v0.3 再评估 macOS 桌面、无障碍语义、复杂文本排版、多窗口、导航/滚动
组件和更完整的插件式后端。任何这些需求不得在 v0.2 实现中通过隐式平台分支提前
侵入 `lumen-core`。
