# Lumen 既有能力优化计划：CPU 阴影 / 水平滚动轴 / 回退文本连续性

> 文档状态：核心实现与 Gallery 水平滚动演示区已完成（2026-09）。
> 来源：对已收口里程碑「已知限制」的审查（非新里程碑，不占编号；完成记录按仓库惯例写入 `lumen-self-use-roadmap.md` §10）。
> 范围：三项互相独立、可分别合入的既有能力完善。历史实施顺序为 **P1 → P3 → P2**。
> 前置：菜单动效批次已落账；P2 与其曾共同触及 `src/core/interaction.cpp`，当前实现状态以源码、测试和路线图完成记录为准。

本文下方的“现状与证据”“目标”“任务拆分”保留各项实施前的方案快照；当前完成状态以本节总览、路线图完成记录和支持矩阵验证快照为准。

## 0. 总览

| 编号 | 主题 | 一句话 | 核心证据（符号口径，行号不作为稳定引用） | 改动面 | 预估 |
| --- | --- | --- | --- | --- | --- |
| P1 | CPU 阴影模糊 | 已完成：CPU `DrawShadow` 对 blur>0 使用三次可分离 box blur；blur=0 保留防御性扁平路径 | `src/render/cpu_renderer.cpp::CpuRenderer::drawShadow`；Skia/GPU 命令契约不变 | 已合入；无阴影路径保持兼容 | 已完成 |
| P2 | 水平滚动轴 | 已完成：控制器/布局/滚轮/拖动/惯性/滚动条/键盘/语义与 Gallery 超宽卡片演示区均支持水平轴 | `ScrollAxis`、水平 layout/painter/interaction/语义回归与 Gallery 集成用例；详见 [`lumen-scroll-design.md`](lumen-scroll-design.md) | 自动隐藏/RTL/水平虚拟化不在本项 | 已完成 |
| P3 | 回退文本连续性 | 已完成：Skia 构建的 GPU 探测/初始化失败回退到 CPU，运行时失败优先回退到 Skia 软件光栅以保持文本连续 | `examples/counter/main.cpp` 的 `skiaSoftwareSetup` 与 `renderer_fallback.h`；诊断输出区分 `skia software raster`/`cpu` | 回退链和 counter smoke 已接入；硬件 GPU 仍受 runner 条件约束 | 已完成 |

三项均要求：既有 headless 帧哈希在不涉及新行为的场景保持不变；新行为默认关闭或不改变默认路径；各自独立提交、独立回滚。

---

## 1. P1：CPU 阴影模糊

### 1.1 实施前现状与证据（历史快照）

- `src/render/cpu_renderer.cpp::CpuRenderer::drawShadow` 是唯一的降级消费点：`(void)blur`，画 token 阴影色 alpha×0.5 的偏移矩形。Skia/GPU 路径（`SkiaRenderer::drawShadow`）按 `kNormal_SkBlurStyle`、σ = blur×0.5×deviceScale 做真实模糊。
- 基础设施全部就绪，本次只需填渲染实现：
  - token：`style::ElevationTokens`（L1/L2/L3 的 blur 为 6/12/24，offset/alpha 配套；高对比模式 alpha=0 已短路）。
  - 布局折算：`src/layout/layout.cpp` 已把 `shadowColor/shadowOffset/shadowBlur` 写入 RenderNode。
  - 命令：`RenderCommandList::drawShadow` 已携带 offset（transform.tx/ty）与 blur（strokeWidth 字段复用）——**RenderCommand 与序列化零改动**。
  - damage：`src/core/damage.cpp` 对 elevation 节点的外扩口径已是 `blur×2 + 1` + offset（注释明确「Skia uses sigma = blur / 2; cover its kernel」）——脏矩形模型当年就按真实 blur 设计，CPU 只是没消费。
- 影响面：`Widget.elevation` 的全部消费者 = Dropdown/ContextMenu 菜单面板、Navigator 页面转场、应用侧抬升卡片。便携包为 CPU-only 闭环（M8），即日常自用看到的每个阴影都是硬边色块；文本/圆角矩形/图标折线均已有 coverage 抗锯齿，阴影是 CPU 后端最后一块降级绘制。

### 1.2 目标与非目标

**目标**：CPU 后端对 blur>0 的 `DrawShadow` 命令产出与 Skia 同 σ 口径的软阴影（单调渐变、边缘透明），无阴影场景像素零变化。

**非目标**：不引入新的 RenderCommand/序列化版本；不改 token/Theme；不做彩色阴影与 inner shadow；不追求与 Skia 像素逐位一致（核形状不同，只要求同 σ 的视觉等价与渐变单调性）；Aurora 方向的玻璃/背景模糊不在本项（但本项的 blur 原语是其前置）。

### 1.3 实现方案（任务拆分）

1. **`CpuRenderer::drawShadow` 软阴影光栅**：
   - offset 矩形换算到设备像素，生成 alpha 掩膜（矩形内 1.0，尺寸 = elevatedBox + 2×spread，spread = σ×3 截断）。
   - 可分离 box blur 近似高斯：水平/垂直各 3 pass，box 半径按标准公式由 σ 推导（σ = blur×0.5×deviceScale，与 `SkiaRenderer::drawShadow` 的 `blur * 0.5F * scale` 同口径；公式与 σ 约定以实现内注释锁定）。
   - 掩膜值 × shadowColor 逐像素 `blendCoveragePixel`（复用既有覆盖率混合路径）。
   - 掩膜/行缓冲作为渲染器成员 scratch 复用，避免逐命令分配（对齐 M7 分配纪律）。
2. **短路分支保持**：`color.a == 0`（高对比）现有早退不动；`blur == 0` 且 offset 存在时保留现扁平面路径（无 token 触达，纯防御）。
3. **测试与哈希**：
   - 新增 CPU 像素用例（`tests/visual_m6_tests.cpp` 阴影用例旁）：沿阴影中轴线的 alpha 采样严格单调不增（中心 → 边缘 → 外圈）；总 alpha 能量与扁平面基线在同一容差带（防过糊/欠糊）；deviceScale>1 时 σ 换算正确（模糊宽度按设备像素扩展）。
   - damage 等价性：把「elevation 节点状态变化 → 局部 damage 与 forced full repaint 像素一致」的既有等价测试模板套到含阴影场景（damage 外扩口径已在，此处补回归证明）。
   - 帧哈希：含 elevation 的 headless 场景哈希按预期变化（逐一列出并更新）；M0 card-grid 基线场景无 elevation，`frame_hash` 必须不变（显式断言项）。

### 1.4 接口约束与不变量

- RenderCommand、序列化版本、Theme/token、damage 口径：零改动。
- Skia/GPU 像素输出零变化（命令同源，实现只在 CPU 消费端）。
- 无阴影命令的帧输出逐位不变。
- 性能：菜单面板量级（数百×数百设备像素）单次 blur 为微秒级；`lumen-scene-bench` card-grid 场景 p50/p95 不得恶化超 10%（无阴影场景理论零影响）。

### 1.5 出口条件

CPU/Skia/GPU 三后端同命令；CPU 软阴影渐变单调、能量近似守恒；既有无阴影哈希与 M0 基线 hash 全绿；含阴影哈希更新并说明；全量 ctest 通过。

### 1.6 文档同步

- `include/lumen/style/theme.h` ElevationTokens 注释中「CPU 后端维持扁平降级」改为如实描述软阴影；`docs/lumen-visual-system-design.md` 阴影节的 CPU 降级说明同步。
- `docs/lumen-self-use-roadmap.md` §10：M6 已知限制（CPU 阴影扁平面）追记收口 + 本项完成记录。

---

## 2. P2：水平滚动轴

### 2.1 实施前现状与证据（历史快照）

- `include/lumen/core/scroll.h::ScrollController`：单轴全 Y 语义（`offset_/viewportExtent_/contentExtent_` 各一份；`applyWheel/applyDrag/applyKey/semanticScroll/noteDragSample/stepFling` 全部按纵向解释；fling 物理常量与轴无关，可直接复用）。
- `src/layout/layout.cpp::layoutScrollView`：内容约束「宽 ≤ 视口 - padding，高不限」——正是这个约束使横向溢出内容无法存在；子 offset 以 `-offset` 应用在 Y。
- `src/render/painter.cpp` 滚动条：只画纵向 thumb（track 取视口高，thumbY 按 progress）。
- `src/core/interaction.cpp` wheel 路由只消费 deltaY；`HostEvent.scrollDelta` 是 `core::Offset`（`windowing.h`），**SDL 宿主已翻译 X 分量**（`src/platform/sdl3_host.cpp` 对 wheel.x/y 同一符号换算）——事件管线已就绪，缺的是框架消费。
- 语义契约：`scrollDeltaY` 参数族（bridge/AppShell/Recording）无 X 分量。
- VirtualList/Tree/TreeList：纵向虚拟化；TreeList 列宽固定+权重撑满视口，宽内容无出路。

### 2.2 目标与非目标

**目标**：声明为水平轴的 ScrollView 获得与纵向完全对等的滚动能力——布局约束镜像（高 ≤ 视口、宽不限）、滚轮（deltaX + Shift+纵轮）、指针拖动与惯性、键盘（Left/Right/Home/End/Page 翻页）、水平滚动条、语义 scroll action 的 X 分量。

**非目标**：二维双轴联滚（同一视口同时纵横滚）不做——单视口单活动轴，双轴留按需评估；水平虚拟化（VirtualList/TreeList 横向物化）不做；RTL 镜像不做；TreeList 水平平移（P2b，见 2.5）待 P2a 落地后评估。

### 2.3 设计决策

- **轴模型**：新增 `core::ScrollAxis { Vertical, Horizontal }`（默认 Vertical）。`Widget.scrollAxis` 声明视口活动轴（枚举一字节，入 M7 的字段打包段，Widget 体积预算用测试锁定）；RenderNode 镜像 `scrollAxis`，`scrollOffset/scrollExtent` 语义 = 活动轴上的偏移/范围。
- **控制器**：`ScrollController` 构造携带轴（或 `setAxis`），全部方法按活动轴解释输入；fling 物理常量不改。应用为水平视口单独持有一个横向控制器——与现有「一视口一控制器」模型一致。
- **内容约束**：`layoutScrollView` 按轴选择镜像约束（横向：宽不限、高 ≤ 视口 - padding），子 offset 的 `-offset` 应用到对应轴。
- **滚轮路由**：`InteractionController` 的 wheel 派发按命中视口的轴选分量（纵向视口吃 deltaY、横向视口吃 deltaX）；Shift+纵轮 → 横向分量的桌面惯例在交互层换算（规则写入设计文档：Shift 按下时 dy 投影为 dx，FLIPPED 符号随 M12 既有换算）。`wheelSink` 签名从单 dy 扩为携带完整 `Offset` delta（默认参数兼容，PointerButton 先例）。
- **拖动/惯性**：`ScrollDragSink` 泛化为活动轴分量（横向视口吃 dx）；M10 的「起点命中 enabled+bind Slider 属滑块」规则在横轴复验——水平视口内的 Slider 拖动是垂直版回归的精确镜像，必须新增回归用例（`slider_drag_inside_horizontal_scroll_view_still_sets_value`）。
- **滚动条**：painter 按节点轴画横向 thumb（底边、track = 视口宽、thumb 宽 = visibleFraction×track、minLength 同 token）；单轴视口只画一条，几何与纵向互为镜像。
- **键盘**：焦点在横向视口（非 TextField）时 Left/Right = 步进、Home/End = 两端、PageUp/PageDown = 横向翻页；TextField 内 Left/Right 仍是 caret 移动（既有优先级不变）。
- **语义**：`AccessibilityBridge`/`AppShell::performAccessibilityAction`/Recording 的 scroll 参数族尾部追加 `scrollDeltaX = 0.0F`（默认参数兼容扩约，PointerButton 先例）；横向视口的语义 value/scroll action 按活动轴报告。

### 2.4 任务拆分与验证

1. **core**：`ScrollAxis` + 控制器轴化（单测：横向 scrollBy/applyWheel/applyDrag/applyKey/semanticScroll/fling 物理与纵向同参数同行为）。
2. **layout**：`layoutScrollView` 镜像约束与子 offset（单测：宽内容横向 extent、offset 应用到 X、clip 不变、shrinkWrap 行为、纵向默认路径几何零变化）。
3. **render**：painter 横向 thumb（单测：thumb 几何/progress/minLength；纵向哈希不变）。
4. **interaction**：wheel 分量路由 + Shift 换算 + 拖动轴化 + 滑块冲突回归（单测：横向视口 deltaX 滚动、Shift+dy 投影、FLIPPED 符号、横视口内 Slider 拖动仍设值、TextField 选区路径不受影响）。
5. **语义**：scrollDeltaX 贯通（Recording 断言横向视口 action 回执）。
6. **示例**：gallery 增加水平滚动演示区（超宽卡片行，滚轮/Shift+滚轮/拖动/键盘可达）；集成用例。
7. **不变量**：既有全部 headless 帧哈希不变（默认纵向零行为变化是硬出口）。

### 2.5 P2b（可选后继，不阻塞出口）

TreeList 内容超宽时的水平 clip 平移（无虚拟化，整树平移）——仅当 P2a 落地且真实使用出现宽表痛点时评估；`.lumen` DSL/基准场景同批考虑。

### 2.6 文档同步

- 新建 `docs/lumen-scroll-design.md`（滚动系统规格：轴模型、输入路由、物理、键盘/语义契约——后续所有滚动行为变更以此为准）。
- `docs/lumen-visual-system-design.md` 滚动条节补横向几何；`lumen-collection-controls-design.md` 已知限制中「水平虚拟化」条目保持并引用本项。
- roadmap §10 完成记录；Widget 体积预算测试更新。

---

## 3. P3：GPU→CPU 回退的文本连续性

### 3.1 实施前现状与证据（历史快照）

1. counter 的 GPU 模式：`rendererFactory` 创建 `SkiaGpuRenderer`，`fontFactory` 提供 `SkiaFontManager`（真实 shaping/度量）。
2. GPU 运行时失效：`runOptions.onRendererFailure` 销毁 GPU 资源与窗口，重建 softwarePresentation 窗口，返回**有值的空 setup**——语义为「回退到 AppShell 内部 CPU 渲染器」。
3. `AppShell` 内部渲染器的系统字体注入（`src/app/app_shell.cpp`）：`setSystemFonts(dynamic_pointer_cast<const SystemFontManager>(textFonts_))`——`SkiaFontManager` 不是 `SystemFontManager`，强转失败 → systemFonts = nullptr。
4. `CpuRenderer::drawText` 对非 placeholder 的 shaped runs 且无 systemFonts 时，退回**逐码点占位旧路径**（固定 advance=fontSize×0.6）——而光标/选区几何仍来自 Skia 布局结果，回退瞬间文本形态与 caret 位置漂移。CPU-only 构建不受影响（`SystemFontManager` 注入成功，GDI/stb 真实字形路径连续）；headless 占位路径不受影响。

即：这是「Skia/GPU 构建专属」的回退降级，违背 M7 出口承诺「GPU 恢复/回退后保留编辑状态与视觉一致」的最后一处已知断档。

### 3.2 目标与非目标

**目标**：Skia 构建下，GPU 失效回退改为**回退到 Skia 软件光栅**（`SkiaRenderer` + 既有 softwarePresentation 窗口），沿用同一 `SkiaFontManager`——shaping、度量、光栅与 GPU 期完全同源，文本/光标/选区零漂移。

**非目标**：不给 `CpuRenderer` 实现 Skia glyph 光栅（重复造轮子）；不扩 `RunOptions` 公共 API（回退策略属应用装配，维持 M2 设计；若多个应用重复样板再评估框架便利层）；CPU-only 构建行为不变；不处理字体热切换/DPI 重建的桥刷新（另行按需）。

### 3.3 实现方案（任务拆分）

1. **counter（GPU 回退样板）**：`onRendererFailure` 在 `LUMEN_HAVE_SKIA` 分支改为构造 `SkiaRenderer`（软件光栅）+ present 回调 + `syncDeviceScale`——main.cpp 的 `--renderer skia` 装配是现成样板，逐字段复用；窗口重建沿用 `renderer_fallback.h` 的 softwarePresentation 路径。`fontFactory` 不动（同一 `SkiaFontManager` 天然连续）。
2. **诊断口径**：回退诊断行从 backend=cpu 改为如实报告软件光栅后端；`[diag] gpu failed — falling back to skia (software)` 之类，保持 counter 契约的 `[diag]` 前缀风格。
3. **无 Skia 的 GPU 构建不存在**（GPU 路径本身依赖 Skia），故无需第二分支；CPU-only 构建编译面零改动。
4. **文档化推荐模式**：`docs/build-commands.md` 或 roadmap 完成记录中写明「Skia 应用的回退推荐链 = GPU → Skia 软件光栅 → CPU」，供后续自用应用照抄。

### 3.4 验证

- Skia 构建新增/更新用例：counter GPU 失败注入（既有 `counter_gpu_smoke` 三条路径）后——回退帧的文本绘制与「直接以 Skia 软件光栅渲染同一帧」的期望一致（帧数据/hash 同源断言）；回退后 TextField caret/selection 像素与文本基线对齐（无漂移）。
- 既有用例：`CpuRenderer` 无 systemFonts 时的占位回退用例保留（该路径仍存在，只是不再被 GPU 回退触达）。
- CI：package-skia-gpu job（Linux llvmpipe 失败注入）首跑为事实来源。
- 出口：回退前后文本同源连续；三屏键盘/选区状态保持断言不变绿；CPU-only 与 headless 全量不受影响。

### 3.5 文档同步

- roadmap §10：M1 已知限制「CPU 消费 Skia 度量退占位」追记收口口径（该降级仅在无 Skia 软件回退链的场景存在）；本项完成记录。
- `docs/lumen-gui-framework-plan-v0.3.md` 若有回退链描述则同步（以源码为准）。

---

## 4. 实施顺序、提交切分与回滚

当前收口状态：P1、P3、P2（含 Gallery 超宽卡片演示区）均已完成并写入路线图。自动隐藏滚动条、RTL、水平虚拟化和双轴联滚不属于本轮出口条件。

- **顺序**：P1（单文件、无公共接口改动、可见收益最大）→ P3（应用装配层小改）→ P2（跨层，含新设计文档）。
- **提交**（Conventional Commits，各自独立可回滚）：
  - `feat(render): CPU 阴影软模糊与像素回归`
  - `feat(examples): GPU 回退改用 Skia 软件光栅保持文本连续`
  - `feat(core): 水平滚动轴与输入路由`（可按 core/layout/render/interaction 拆多个提交，但出口条件以整项验收）
- **回滚点**：各自合入前的提交；P1 回滚即恢复扁平面降级（damage/命令无残留）；P2 回滚需同时回滚设计文档与示例页；P3 回滚恢复 CPU 回退（无状态迁移）。
- **完成记录**：每项完成后按 §10 模板补 roadmap 记录（变更/测试/平台/已知限制/回滚点），并更新本文档状态行。
