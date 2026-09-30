# Lumen Pinch/Zoom 手势设计（G-5）

> 状态：已实现（2026-09-30；gap-backlog G-5，P2）。
> 动机：图像查看/画布类工具在触控板/触屏上的基本预期；SDL 3.2.10 **无原生 pinch 事件**（SDL2 MULTIGESTURE 已移除）——由多点 FINGER 事件流在框架内合成。
> 代码：`core::InteractionController` 多指追踪 + pinch 状态机（`include/lumen/core/interaction.h`）、`AppShell` pointerId 透传与 `ShellConfig.onPinch`、`run_app` 事件接线。
> 测试：`tests/pinch_tests.cpp`（headless；Fake host 合成多指流）。

## 1. 事件源与透传

SDL host 把 `SDL_EVENT_FINGER_*` 归一化为 Touch `PointerDown/Move/Up`（`event.pointerId` 携带手指标识）——既有实现。G-5 把 `pointerId` 从 `HostEvent` 透传 `AppShell::pointerDown/Move/Up`（缺省 0 = 单指针，既有调用与帧哈希不变）到 `InteractionController`。

## 2. 状态机（InteractionController）

- 指针登记：`pointerId != 0` 的 Touch 事件入 `touchPointers_`（pointerId → 位置）；pointerId 0 不入表（鼠标/既有单指语义原样）。
- **第二指落下**（表非空的新 Down）：先取消主指单指手势（`pointerCancel()`——按压/滚动拖动/拖放/滑块解除，点击绝不触发），登记第二指并**武装** pinch：
  - 起始指距 `dist > 0`；
  - **起始中点不在 TextField 上**（字段选区/编辑仲裁优先——lumen-scroll-design 触摸文本路径不受 pinch 干扰）。
- **Move**（多指路径先于单指 hover/按压）：armed 时距离变化越过 **8px**（点击 slop 同量级）→ `Begin`；active 时每拍 `Update`（`scale = dist / startDist`，`center = 两指中点`）。
- **任一指抬起** → `End` 回执，触摸表清空；后续单指回到普通路径。
- **窗口失焦/系统取消**（`pointerCancel`）→ 静默清理（无 root 回执 End；sink 按 scale 值流消费，无状态残留）。

## 3. 仲裁（M15 手势仲裁表新增 pinch 位）

| 场景 | 裁定 |
| --- | --- |
| 单指按压/滚动拖动/拖放进行中，第二指落下 | 单指手势取消（pointerCancel），pinch 接管 |
| 起始中点在 TextField | 不武装——两指用于字段选区/编辑 |
| armed 未越阈值即抬指 | 回到单指语义（不产生 pinch、点击已在取消时作废） |
| pinch 会话中的新 Move（id 0 或未知 id） | 忽略（不更新 hover） |

## 4. 应用接线

`ShellConfig.onPinch(shell, root, center, scale, phase)`（返回 true = 已消费；应用改状态后自行 markDirty）。框架不内置内容缩放模型（无此抽象）；**键盘等价（Ctrl+= / Ctrl+-）属 G-1 命令层**——应用注册 `zoom-in`/`zoom-out` 命令（`KeyBinding::chord('=', Ctrl)` 等），与 pinch 汇入同一应用状态。sink 为空时手势仲裁照常（零投递零开销）。

## 5. 验证与遗留

- headless（本提交）：状态机（armed→Begin 阈值→Update→End）、触摸表生命周期、第二指取消单指（按压解除 + 无点击）、字段不武装、pointerId 缺省路径不变、Fake host 多指事件流透传。
- 桌面触屏 smoke（人工）：触控板 pinch（macOS）与触屏双指（桌面触屏在范围内）——SDL FINGER 流真实性验证，属平台验收清单。
