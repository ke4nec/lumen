# Lumen 拖放设计（M15）

> 文档状态：实施中（2026-09-29 起，分阶段落地；见 §9 实施状态）
> 范围：OS→应用 拖入（文件/文本）、应用内行/列重排拖拽。归属
> [`lumen-m15-roadmap.md`](lumen-m15-roadmap.md) §3（M15）。
> 不纳入本版：OS 拖出（SDL 3.2.10 无 API，结构化降级）、拖拽中自动
> 滚动、Esc 中途取消、跨窗口应用内拖放。

## 1. 分层契约

```text
OS 拖入     HostEvent DragEnter/Move/Drop/Leave（宿主翻译）
            → runApp onEvent 转发应用（应用决定落点语义）
应用内拖放  InteractionController 会话状态机（core）
            → widgets 层控制器（List/Tree/DataGrid）经 sink 认领/回调
视觉       Theme.dragDrop token + 框架 overlay（ghost/插入指示线）
```

两层互不相交：OS 拖入不经过交互层（无会话状态），应用内重排不经过
宿主（无 OS 事件）。二者共享的只有视觉 token。

## 2. 平台契约（OS 拖入）

- `HostEventType::{DragEnter, DragMove, DragDrop, DragLeave}`；`position`
  为窗口逻辑坐标，`DragDrop` 携带 `text` 或 `filePaths` 负载。
- SDL3 翻译：`SDL_EVENT_DROP_BEGIN` → DragEnter（清会话交付标志）；
  `DROP_POSITION` → DragMove；`DROP_FILE/TEXT` → DragDrop（置交付标志）；
  `DROP_COMPLETE` 在本次会话未交付负载时合成 **DragLeave**——SDL 3.2.10
  无显式 leave 事件。
- `ApplicationHost::startDrag(WindowId, DragOutPayload)`：拖出发起。
  SDL 3.2.10 无 API——固定返回结构化 `Unavailable`，能力位
  `PlatformCapabilities.dragDropStart` 如实 false；`dragDropReceive`
  在视频子系统可用后为 true。SDL 升级时在此接入并翻转能力。
- Fake host：`pushDragEnter/Move/DropText/DropFiles/Leave` 注入 +
  `startDrag` 记录（`dragStartCalls`）与失败注入（`setDragStartFailure`）。
- runApp 把四类事件经 `RunOptions.onEvent` 转发应用（同
  FileDialogCompleted 先例）；应用变更状态后自行请求帧。

## 3. core 会话状态机（应用内拖放）

状态机（`InteractionController`）：

```text
pointerDown → arm sink 认领（候选，行为不变）
  ↓ 移动超过启动阈值（曼哈顿距离，默认 8px，setDragThresholdPx 可调）
Start（dragging_ 置位：释放不触发点击/双击）
  ↓ pointerMove → Move（携带当前位置命中链）
Drop（pointerUp：落点命中链）/ Cancel（pointerCancel：空链）
```

手势仲裁（先到先得，后到者不咨询）：

1. 滚动条拇指（命中即独占）；
2. Splitter 分隔条（命中即独占，无 slop）；
3. Slider 滑块；
4. TextField 选区（字段命中 `selecting_`，arm 不咨询）；
5. **拖放 arm**：认领不改变按压/点击语义；越阈值才开会话，且会话期间
   滚动拖动路由不劫持该按压；
6. 视口拖动滚动（M10）。

触摸规则：行整体认领 `touchAllowed=false`——触摸列表拖动保持滚动
语义；专用拖拽句柄可 `touchAllowed=true` 抢先。`pointerDown` 尾参数
`PointerDevice device`（默认 Mouse，既有调用零改动）。

Sink 契约（`lumen-core/interaction.h`）：

- `DragArmSink(hitChain, device, DragSourceClaim&)`：认领源
  （key/identity 跨重建携带 + touchAllowed）。
- `DragSessionSink(DragPhase, position, hitChain, sourceKey,
  sourceIdentity)`：Start 后整树可能重建——sink 内只依赖 source 标识
  与当拍命中链，不得持有跨拍节点指针。

## 4. List 行重排（widgets 层）

`ListController`：

- `setReorderable(bool)` + `onReorder(fromIndex, toIndex)`。控制器不改
  应用数据序——回调内应用更新数据并触发重建。
- attach 注册 arm sink（按行 onClick 前缀 `"list:<owner>:"` 解析；禁用
  行不认领；`touchAllowed=false`）与 session sink（按行节点 key 前缀
  `"<owner>:item:"` 过滤，多集合共存）。
- 插入间隙语义：`dragInsertBefore()` ∈ [0, itemCount]；目标行下半落点
  = 间隙 target+1。Drop 提交采用**先移除后插入**语义：间隙在源行之后
  时最终落点行号 = 间隙 - 1（`onReorder` 收到的 `toIndex` 即重排后源项
  行号）。
- 会话开始后源行因数据变更消失：不响应后续阶段（不提交）。

## 5. 视觉（ghost 与插入指示线）

- 拖拽视觉经框架 overlay 承载，使用 **非模态视觉 overlay**
  （`AppShell::setVisualOverlayBuilder`/`clearVisualOverlay`，M15 新增）：
  与模态 overlay 的差异是安装/解除**不取消活动指针、不清焦点**——
  拖放会话仍由交互层主树手势拥有，本层只叠加视觉。
- ghost：源行内容 + token 表面/描边 + L2 阴影，按 `ghostGrabOffset*`
  抓取偏移跟随指针；无 barrier/FocusScope（瞬态视觉）。
- 插入指示线：accent 色横线（`indicatorThickness`），画在插入间隙目标
  行上边界（视口原点 + `offsetOfIndex(间隙)` - 滚动偏移）。
- 会话 sink 收到的命中链来自事件树（overlay 活跃期 = overlay 树）；
  落点解析一律由 sink 对主树 `shell.root()` 重新命中。

## 6. Token（视觉系统 §3.4）

`Theme.dragDrop`（`DragDropTokens`，`dragDropTokensFrom(colors)` 派生）：

| Token | 默认（dark） | 说明 |
| --- | --- | --- |
| `dropIndicator` | `colors.accent` | 插入指示线 |
| `ghostSurface` | `colors.surfaceElevated` | ghost 表面 |
| `ghostBorder` | `colors.borderStrong` | ghost 描边（1px controlBorderWidth） |
| `indicatorThickness` | 2.0 | 指示线厚度（逻辑 px） |
| `ghostGrabOffsetX/Y` | 10 / 16 | ghost 相对指针的抓取偏移 |

控件不写死常量；几何不随密度分档（交互反馈非控件部件）。

## 7. 无障碍

- 拖放功能不得成为纯指针路径：键盘等价（行移动命令/上下文菜单）与
  语义 action（Reorder/MoveUp/MoveDown）随 DataGrid 批次落地（§9）。
- 瞬态 ghost/指示线会作为 overlay 子树短暂进入语义树（generic 节点，
  无 role/action）；若读屏噪音可辨，后续增量可引入 decorative 标志。

## 8. 测试基线

- `tests/platform_host_tests.cpp`（[m15]）：事件归一化/fake 记录与失败
  注入/SDL 能力位与结构化 Unavailable。
- `tests/interaction_tests.cpp`（[m15]）：阈值启动/Move/Drop/Cancel、
  阈值内释放仍点击、阈值可调、触摸仲裁（滚动优先/句柄认领）、字段
  命中不咨询 arm。
- `tests/drag_reorder_tests.cpp`（[dragdrop]）：List 重排提交（上下向）、
  插入间隙跟踪、阈值内点击保持、Cancel 回退与 overlay 清理、
  ghost/指示线渲染契约、未启用不认领、token 派生。

## 9. 实施状态（分阶段）

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| 平台契约 | HostEvent Drag* + startDrag 降级 + 能力位 + Fake/SDL | 2026-09-29 落地（提交 f0f2773） |
| core 状态机 | arm/阈值/Move/Drop/Cancel + 仲裁 + device 参数 | 2026-09-29 落地（提交 df9579b） |
| List 重排 | onReorder + 非模态视觉 overlay + DragDropTokens | 2026-09-29 落地 |
| DataGrid 行/列重排 | 行重排复用 List 机制 + reorderColumn + 键盘等价 + 语义 action | 进行中 |
| 真实平台 smoke | 三桌面 OS 文件/文本拖入窗口 smoke | 未做（四态：headless 已验证） |

已知限制（本版）：拖拽中无自动滚动（近边缘不滚动，用户可先滚再拖）；
无 Esc 中途取消；ghost 抓取偏移为固定近似（不按按下点在行内的位置
换算）；指示线不随 RTL 镜像（RTL 镜像属 M17）。
