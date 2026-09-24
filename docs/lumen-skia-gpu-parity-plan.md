# Skia GPU 后端补齐计划:达到 CPU 渲染同等支持程度

- 状态:提案(实现依据文档,照此可完成开发)
- 日期:2026-09-24(基于当日工作区核对,含未提交改动)
- 引用惯例:按仓库路线图惯例,以 `文件::符号` 为稳定引用;括注行号仅为当
  日核对快照,随实现漂移不作为稳定依据。
- 关联文档:`docs/lumen-self-use-roadmap.md`(§M7、命令一致性契约)、
  `docs/lumen-gui-framework-plan.md`(§3.1 渲染器契约)、
  `docs/lumen-visual-system-design.md`(阴影/图标 token)。

## 1. 目标与范围

**目标定义**:Skia GPU 后端(`src/render/skia_gpu_renderer.cpp`)在
`Renderer` 契约的命令覆盖与视觉语义上,达到 `CpuRenderer` 当前的同等程度:
painter 发出的每一条绘制命令,三个后端(CPU / Skia 光栅 / Skia GPU)都必须
产生语义一致的可见输出。这正是路线图的既有契约——"图标、阴影、动效和主题
切换的 CPU/Skia/GPU 命令一致性"(roadmap §8)。

**必做(缺口修复)**:

1. GPU 命令回放补齐 `DrawIcon` / `DrawShadow` 两个被静默丢弃的命令(§3.1/§3.2)。
2. 即时路径补齐 `drawIcon` / `drawShadow` / `clipRounded` 覆写(§3.3)。
3. GPU smoke 测试补图标与阴影用例——当前缺口之所以未被察觉,就是因为
   `tests/gpu_smoke_tests.cpp` 没有任何用例画过图标或阴影(§5)。

**明确不在本次范围**(与"达到 CPU 程度"无冲突,均为框架级已记录决策):

- `partialSubmit`:CPU 为 `true`,GPU 维持 `false`。M7 已实测 Ganesh 上
  preserve 快照路径为全帧 Clear 的 1.16×,维持全帧提交是记录在案的决策
  (roadmap §10 M7)。如未来重启,见 §4.3 方案草案。
- 文本 shaping(HarfBuzz 合字/上下文形)、完整 UBA、COLR 彩色 emoji:
  三后端同限,属 M14 后评估项(roadmap §9)。
- 渐变、图层/透明度组、自由变换:`RenderCommand` 的 `alpha`/`transform`
  为预留字段,三后端一致忽略;CPU 也没有,不构成差距。

**实现完成后必须同步的文档/注释**(AGENTS.md 义务):见 §6。

## 2. 基准:三后端命令覆盖现状

`RenderCommandList` 共 12 种命令(`include/lumen/render/render_commands.h`
`CommandType`)。三后端覆盖:

| 命令 | CPU | Skia 光栅 | Skia GPU | 备注 |
|---|---|---|---|---|
| Save / Restore / ClipRect | ✅ | ✅ | ✅ | |
| ClipRounded | ✅ SDF 覆盖率 | ✅ clipRRect | ✅(仅命令路径) | 即时路径未覆写,§3.3 |
| DrawRect / DrawRectStroke | ✅ | ✅ | ✅ | 几何已同式(内缩 width/2) |
| DrawText | ✅ 占位字形 | ✅ 真实字体 | ✅ 真实字体 | shaping 三端同限 |
| DrawImage / Upload / Unload | ✅ | ✅ | ✅ | 采样差异见 §4.2 |
| **DrawIcon** | ✅ AA 距离场描边 | ✅ SkPath 描边 | ❌ **静默丢弃** | §3.1 |
| **DrawShadow** | ✅ 3-pass box blur | ✅ Skia blur | ❌ **静默丢弃** | §3.2 |

能力报告差异:`CpuRenderer::capabilities().partialSubmit == true`,两个
Skia 后端均为 `false`(§4.3)。

受影响 UI 面(painter 实际发射点,`src/render/painter.cpp`):

- 阴影:所有 `node.elevation > 0 && node.shadowColor.a > 0` 的节点表面
  (painter `paintNode` 开头,快照 :431-433)。
- 图标:Image 未就绪占位(:497-510)、下拉/树展开 chevron(:775-821,
  含 `iconRotation` 旋转)、按钮图标与新增的 `iconLeading` 前导图标
  (:877-927,当前未提交改动正在**加大** drawIcon 用量)、复选框勾
  (:1003-1014)、滚动条箭头、标题栏窗口按钮、Spin busy、工具栏与
  Gallery 侧栏导航(icon 目录见 `include/lumen/core/icon_id.h`)。

即:GPU 模式下的实际画面缺**全部矢量图标**与**全部层级阴影**。

## 3. 缺口与实现方案

### 3.1 G1(核心):回放补齐 `CommandType::DrawIcon`

**现状证据**:`SkiaGpuRenderer::replayCommand`(skia_gpu_renderer.cpp,
快照 :377-426)的 switch 只有 10 个 case,无 `DrawIcon`;文件内无
`drawIcon` 任何出现。CPU 原生 submit 中对应 case 有明确教训注释——图标/
阴影命令必须执行,否则"静默丢失矢量图标与层级阴影"(
cpu_renderer.cpp 快照 :1154-1166)。这注释描述的缺陷正是 GPU 后端当下
的状态。

**命令字段解码**(`render_commands.h` `RenderCommandList::drawIcon`,
快照 :149-161):盒子 = `command.rect`,颜色 = `command.color`,线宽 =
`command.strokeWidth`,几何 = `command.polylines`(归一化 0..1 折线组)。

**实现**:在 `SkiaGpuRenderer` 增加私有 `paintIcon(...)`,几何逐行取自
`SkiaRenderer::drawIcon`(skia_renderer.cpp 快照 :474-510),关键不变量:

1. **原点设备对齐**:`box.origin.x = lround(box.origin.x * scale) / scale`
   (y 同式)。这是与 `CpuRenderer::drawIcon`(cpu_renderer.cpp 快照
   :764-772)共同约定的一致性口径——逻辑居中产生的半像素原点(如
   19px 盒装 14px 图标得 2.5)不对齐则描边虚散、奇数高图标上下不对称。
   **必须保留**,否则 GPU 与 CPU/Skia 光栅出现像素级漂移。
2. 归一化坐标 → 设备像素:`(box.origin + p * box.size) * scale`。
3. SkPaint:stroke 风格、AA、`kRound_Cap` + `kRound_Join`(与 CPU 的
   距离场端点圆帽一致)、`strokeWidth * scale`。
4. 防御 guard 与 CPU 对齐:`color.a == 0`、`box.size.width/height <= 0`、
   `polylines.empty()` 直接返回(光栅版未查 box 尺寸,补上以对齐)。

回放 case:

```cpp
case CommandType::DrawIcon:
    paintIcon(canvas, command.polylines, command.rect,
              command.color, command.strokeWidth);
    break;
```

`paintIcon` 主体(自 skia_renderer.cpp 移植,该文件无共享 Impl 成员
paint 的约束,局部 SkPaint 与本文件 paintRect 等现有函数风格一致):

```cpp
void paintIcon(SkCanvas* canvas,
               const std::vector<std::vector<core::Offset>>& polylines,
               core::Rect box, core::Color color, float strokeWidth) {
    if (color.a == 0 || polylines.empty() ||
        box.size.width <= 0.0F || box.size.height <= 0.0F) {
        return;
    }
    const float scale = deviceScale_;
    // 原点设备对齐(与 CpuRenderer/SkiaRenderer::drawIcon 同式 lround)。
    box.origin.x = static_cast<float>(std::lround(box.origin.x * scale) / scale);
    box.origin.y = static_cast<float>(std::lround(box.origin.y * scale) / scale);
    SkPath path;
    for (const auto& polyline : polylines) {
        if (polyline.empty()) continue;
        path.moveTo((box.origin.x + polyline.front().x * box.size.width) * scale,
                    (box.origin.y + polyline.front().y * box.size.height) * scale);
        for (std::size_t i = 1; i < polyline.size(); ++i) {
            path.lineTo((box.origin.x + polyline[i].x * box.size.width) * scale,
                        (box.origin.y + polyline[i].y * box.size.height) * scale);
        }
    }
    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setAntiAlias(true);
    paint.setColor(toSkColor(color));
    paint.setStrokeWidth(strokeWidth * scale);
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);
    canvas->drawPath(path, paint);
}
```

头文件依赖已在位:`SkPath`/`SkPaint` 已被 skia_gpu_renderer.cpp 引入。

### 3.2 G2(核心):回放补齐 `CommandType::DrawShadow`

**现状证据**:同 §3.1,`replayCommand` 无 `DrawShadow` case。

**命令字段解码——这是本缺口最易踩坑处**:阴影的偏移与模糊半径**没有**
专用字段,由 `RenderCommandList::drawShadow`(render_commands.h 快照
:163-177)编码进预留字段:

- 偏移 offset = `core::Offset{command.transform.tx, command.transform.ty}`
- 模糊 blur = `command.strokeWidth`
- 盒子/颜色 = `command.rect` / `command.color`

CPU 回放即按此解码(cpu_renderer.cpp 快照 :1161-1166)。**不要**把
`transform` 当作几何变换消费——对 DrawShadow 它只是 offset 的载体。

**实现**:私有 `paintShadow(...)`,取自 `SkiaRenderer::drawShadow`
(skia_renderer.cpp 快照 :512-531),σ 口径为三端共同约定:

```cpp
void paintShadow(SkCanvas* canvas, core::Rect elevatedBox, core::Color color,
                 core::Offset offset, float blur) {
    if (color.a == 0 || elevatedBox.size.width <= 0.0F ||
        elevatedBox.size.height <= 0.0F) {
        return;
    }
    const float scale = deviceScale_;
    const SkRect rect = SkRect::MakeXYWH(
        (elevatedBox.origin.x + offset.x) * scale,
        (elevatedBox.origin.y + offset.y) * scale,
        elevatedBox.size.width * scale, elevatedBox.size.height * scale);
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setAntiAlias(true);
    paint.setColor(toSkColor(color));
    if (blur > 0.0F) {
        // σ = blur*0.5*scale:CpuRenderer 三-pass box blur 同口径逼近同一
        // 高斯(cpu_renderer.cpp drawShadow 注释),damage 层已按 blur 外扩。
        paint.setMaskFilter(SkMaskFilter::MakeBlur(
            kNormal_SkBlurStyle, blur * 0.5F * scale));
    }
    canvas->drawRect(rect, paint);
}
```

回放 case:

```cpp
case CommandType::DrawShadow:
    paintShadow(canvas, command.rect, command.color,
                core::Offset{command.transform.tx, command.transform.ty},
                command.strokeWidth);
    break;
```

技术可行性:`SkMaskFilter::MakeBlur` 在 Ganesh 上有 GPU 深化路径,与光栅
后端同 API;`SkBlurTypes.h`/`SkMaskFilter.h` 已被 skia_gpu_renderer.cpp
引入(快照 :36-37,当前引入却未使用——即原实现本就预留了阴影)。

damage/bounds 无需改动:painter 侧 damage 不变量早已按阴影 blur 外扩
(core/damage.cpp,见 cpu_renderer.cpp drawShadow 注释),GPU 全帧回放
不做裁剪,更不受影响。

**决策点 D1(blur ≤ 0 语义)**:CPU 在 blur≤0 时走降级扁平面
(alpha × 0.5 的偏移矩形,cpu_renderer.cpp 快照 :932-941);Skia 光栅
在 blur≤0 时画无滤镜的满 alpha 矩形。三端不一致早已存在。建议:**GPU
跟随 Skia 光栅**(blur≤0 → 无滤镜矩形),同时在 §6 文档同步时把
"blur=0 语义统一"记为后续小项;不建议本次顺手改 CPU(超出本计划范围,
且会牵动 CPU 基线)。painter 只在 `elevation > 0` 时发阴影,token 的
shadowBlur 正常为正,该分支实际罕见。

### 3.3 G3:即时路径补齐三个虚方法覆写

**现状证据**:`SkiaGpuRenderer` 未覆写 `drawIcon`/`drawShadow`
(基类默认 no-op,renderer.h 快照 :172-186)与 `clipRounded`(基类默认
退化为矩形 clipRect,renderer.h 快照 :166-169)。生产路径全部经
`submit()` 命令回放,`ClipRounded` 在命令路径已原生处理,故此缺口当前
是潜伏项;但 `submit` 的默认适配器(renderer.cpp)正是"命令 → 即时
路径"的转发,任何走默认适配的后端组合都会触发。

**实现**:三个公开覆写,全部一行转发到 §3.1/§3.2 的私有函数与现有
ClipRounded 回放几何:

```cpp
void clipRounded(core::Rect rect, core::CornerRadius radius) override {
    if (immediateCanvas() != nullptr) {
        // 与 replayCommand 的 ClipRounded case 同几何(Skia 圆角序
        // TL,TR,BR,BL,顺时针自左上;AA 开)。
        const float s = deviceScale_;
        SkRect skRect = scaled(rect);
        const SkVector radii[4] = {
            {radius.topLeft * s, radius.topLeft * s},
            {radius.topRight * s, radius.topRight * s},
            {radius.bottomRight * s, radius.bottomRight * s},
            {radius.bottomLeft * s, radius.bottomLeft * s},
        };
        SkRRect rrect;
        rrect.setRectRadii(skRect, radii);
        immediateCanvas()->clipRRect(rrect, SkClipOp::kIntersect, true);
    }
}
void drawIcon(std::vector<std::vector<core::Offset>> polylines, core::Rect box,
              core::Color color, float strokeWidth) override {
    if (immediateCanvas() != nullptr) {
        paintIcon(immediateCanvas(), polylines, box, color, strokeWidth);
    }
}
void drawShadow(core::Rect elevatedBox, core::Color color, core::Offset offset,
                float blur) override {
    if (immediateCanvas() != nullptr) {
        paintShadow(immediateCanvas(), elevatedBox, color, offset, blur);
    }
}
```

`ClipRounded` 的回放 case 与 `clipRounded` 覆写建议抽成私有
`applyRoundedClip(SkCanvas*, const core::Rect&, const core::CornerRadius&)`
共用,消除两份圆角序拷贝。

### 3.4 G4(次要,已一致项核对结论)

以下经核对**已达标**,实现时不要画蛇添足:

- `DrawRect`/`DrawRectStroke` 几何与光栅后端逐行同式(内缩 width/2、
  圆角同步内缩、minSide clamp)。
- `DrawText`:shaped 优先 + 逐码点回退,与光栅后端同构;`fallbackGlyphs_`
  缓存口径一致。
- 图像上传:alpha 三态(Straight/Premul/Opaque)保留语义已有专门 GPU
  readback 测试(`gpu_alpha_*`)。
- 资源生命周期:图像只经 `UploadImage`/`UnloadImage` 命令进入
  (与 CPU 原生 submit 同集),无 `registerImage` 需求。
- 统计:`commandCount` 含图标/阴影命令(`RenderCommandList::drawCount`
  本就将两者计入),实现后无需改 stats。

## 4. 决策点

### 4.1 D1:blur ≤ 0 的阴影语义(见 §3.2)

GPU 跟随 Skia 光栅;三端统一(含 CPU 扁平降级路径)记为后续独立小项,
不在本计划内。

### 4.2 D2:图像采样(GPU linear vs CPU/Skia nearest)

现状:GPU `blitImage` 用 `kLinear`(skia_gpu_renderer.cpp 快照 :630-633),
CPU 与 Skia 光栅用 `kNearest`(skia_renderer.cpp 快照 :548-552,注释明言
为对齐 CPU)。仅在**缩放绘制**时可见差异;1:1 绘制两者逐字节相同。

建议:**保持 GPU kLinear 不变**,在 `lumen-visual-system-design.md` 记录
该三端差异(GPU 缩放质量优先,锚点级一致性以 1:1 采样为准)。理由:
"达到 CPU 程度"指能力与语义覆盖,不要求字节级相等——这符合既有测试
哲学(skia_smoke_tests 明确"不要求 glyph 字节相等");若强行改
kNearest,GPU 缩放图像会劣化且无收益。新测试(§5)一律在 1:1 或容差
断言下设计,规避该差异。

### 4.3 D3:partialSubmit(范围外,附重启草案)

维持 `false`。M7 实测依据:Ganesh preserve 快照 1.16× 全帧成本
(roadmap §10 M7)。若未来重启,路线是:`capabilities().partialSubmit`
置 true 前先在 `submit` 实现 damage 裁剪(`cullCommandsOutside`)+
FBO0 上按 damage 矩形 `Save/ClipRect` 局部重绘——难点在"保留 damage
之外旧像素"需要前帧纹理拷贝(这正是被实测否掉的 1.16× 来源),除非
改用 scissor + 不 clear 的交换链语义。任何实现必须按 roadmap §M7 要求
"实测结果 + 降级说明"收口。

## 5. 测试计划(tests/gpu_smoke_tests.cpp 扩展)

沿用现有 GPU 用例脚手架:`probeSkiaGpuAvailable` 失败即 `SKIP` →
`SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN` 窗口 → `allowSwap=false` →
`glReadBuffer(GL_BACK)` + `glReadPixels` 读回(**y 翻转**:采样行取
`(h-1-y)`,与现有用例一致)。

### 5.1 `gpu_icon_readback_paints_stroke_and_keeps_gaps`(新增)

命令级用例,直接录 `RenderCommandList`:

1. 背景纯色 `drawRect` 全屏;`drawIcon` 画
   `core::iconPolylines(core::IconId::Check)` 于已知盒(如
   `Rect{10,10,16,16}`,线宽 1.5,前景色取与背景强对比值);
2. `scale ∈ {1.0, 2.0}` 各提交一帧;
3. 断言:折线**段中点**像素(`(box.origin + midpoint * box.size) * scale`)
   等于图标色(±AA 容差,建议逐通道 ±2 以内,段中点为覆盖核中心);
   折线远离处(如盒外 1px 与两段之间的空隙点)等于背景色;
4. 再以 `IconId::Busy`(3/4 圆弧)重复锚点断言——弧线是 cap/join 与
   曲线覆盖的更敏感探针;
5. 追加即时路径覆盖:经基类默认适配器 `Renderer::submit` 之外,直接调用
   覆写后的 `drawIcon`(验证 §3.3 覆写生效)。

### 5.2 `gpu_shadow_readback_blur_offset_and_falloff`(新增)

命令级用例:

1. 背景纯色;`drawShadow(box={40,40,80,40}, color, offset={6,8}, blur=12)`;
   之后按 painter 真实次序在原盒位画不透明表面矩形(阴影先画、表面
   覆盖,是 paintNode 的发射次序);
2. 断言(σ = blur*0.5*scale):
   - 表面中心 = 表面色(阴影未污染前景);
   - 偏移方向外缘(盒底边 + offset + 2px)呈背景与阴影色的**混合**
     (非纯背景、非纯阴影色,容差 ±16/通道,容忍 GPU 与 CPU Skia blur
     实现差异);
   - 3σ 之外(≈盒边 + blur*2)恢复纯背景(高斯尾清零口径与 CPU 同);
   - `blur=0` 时按 D1 断言为无滤镜纯色偏移矩形(精确等值)。
3. 参照 `skia_smoke_tests` 的跨后端锚点哲学,可选加一段 CPU 后端同命
   令锚点对照(容差 ±16),防止 GPU 与 CPU 的 σ 口径漂移。

### 5.3 painter 级场景用例(新增或扩展现有 tree/list 用例)

现有 `gpu_tree_readback_*` 已有展开符 chevron 却未断言其像素(缺口无
察觉的原因)。在其采样区追加:树展开箭头线身锚点 = 前景色;带
`elevation` 的节点(如弹出菜单/卡片)阴影环带采样非背景。这保证
painter → 命令 → GPU 回放全链路,而非仅命令级。

### 5.4 回归验证

- 全量:`ctest --test-dir build-release -C Release`(GPU 用例在
  CI/本机 GL 可用时执行,不可用自动 SKIP,不影响 CPU-only 树);
- 现有断言不变量不能破坏:`gpu_renderer_submits_frame_or_reports_fallback`
  中 `CHECK_FALSE(caps.partialSubmit)` 维持(D3 范围外);
- 基准:`benchmarks/scene_bench.cpp --backend gpu`。注意补齐后 GPU 帧
  从"丢弃"变为"真实绘制"图标/阴影,若基准场景含图标/阴影,GPU 耗时
  上浮是**预期修正**而非回归;`benchmarks/run_perf_gate.py --backend gpu`
  对比的 `.perf-reference` 需按其既有流程刷新并注明原因。

## 6. 实施顺序与同步义务

建议单提交序列(每步可独立构建、测试通过):

1. **提交 1**:`paintIcon`/`paintShadow` + 两个回放 case + §5.1/§5.2
   测试(核心缺口修复,含 D1 语义)。
2. **提交 2**:即时路径三覆写 + `applyRoundedClip` 去重 + §5.1 的即时
   路径断言。
3. **提交 3**:§5.3 painter 级用例 + `lumen-visual-system-design.md`
   记录 D2 采样差异 + 路线图完成记录(§10 对应里程碑行)、
   `skia_gpu_renderer.h` 头注释能力清单补"图标/阴影"。

提交信息按仓库规范,如 `fix(render): GPU 后端补齐图标与阴影命令回放`。

**必须同步的文档**(AGENTS.md:行为变化与文档同变更):

- `src/render/skia_gpu_renderer.h` 头注释:能力清单加图标/阴影;
- `docs/lumen-self-use-roadmap.md`:§10 补完成记录;§8"命令一致性"
  由"契约"变为"已验证";
- `docs/lumen-visual-system-design.md`:阴影降级契约(roadmap §6 快照
  :311"三后端不支持时降级")更新为 GPU 原生支持;记录 D2 采样差异;
- 本文档状态改为"已完成"并链接落地提交。

## 7. 风险与注意事项

- **编码踩坑**:`DrawShadow` 的 offset/blur 藏于 `transform.tx/ty` 与
  `strokeWidth`(§3.2)。解码口径必须与 CPU 回放逐字一致,否则阴影
  位置/软度漂移且测试难定位。
- **原点对齐口径**:图标 `lround` 设备对齐是三端 crisp 一致的关键
  (§3.1 不变量 1),移植时不可"顺手优化"。
- **宏冲突**:`windows.h` 经 GL 头把 `DrawText` 宏化的防御已存在
  (skia_gpu_renderer.cpp 快照 :22-25),新增 case 无新冲突面。
- **性能**:Ganesh blur 为 GPU 深化路径,但 CI 的 llvmpipe 软件适配器上
  blur 成本显著;perf gate(§5.4)负责兜底,p95 判定已按近期 CI 调整
  只看 min+median。图标 SkPath 每帧重建与光栅后端同策略,不做缓存;
  若 bench 显示热点,再按 `IconId+盒+scale+色` 键缓存(独立 perf 提交)。
- **验证矩阵**:Linux(llvmpipe CI + 本机硬件)为准入;Windows GPU 构建
  Release-only、macOS 软件 GL 由既有 CI job 覆盖;`counter --renderer gpu
  --diagnostics` 手测命令核对诊断输出不再含降级字样。
