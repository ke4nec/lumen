# Lumen 对话框便利层设计（G-4a）

> 状态：已实现（2026-09-30；gap-backlog G-4 第一批）。
> 动机：「确认覆盖文件？」三行代码的事，现状要手拼 `makeDialog` + 焦点恢复 + Escape 路由 + 转场（settings 示例 ~80 行样板）。
> 代码：[`include/lumen/widgets/dialog_host.h`](../include/lumen/widgets/dialog_host.h)、`src/widgets/dialog_host.cpp`。
> 测试：`tests/dialog_host_tests.cpp`（与手拼 makeDialog 同契约锁定）。
> 视觉：全部复用 makeDialog/Theme 既有 token（visual-system §7.4 DialogTokens）——本层零新增视觉，无独立 mockup。

## 1. API

`widgets::DialogHost`（应用持有一个实例，四钩子装配——见头文件示例）：

| 入口 | 语义 | 回调 |
| --- | --- | --- |
| `showMessage(title, body, buttons, onDismiss)` | 单按钮消息 | OK/barrier/Escape 均 → `onDismiss()` |
| `showConfirm(title, body, buttons, onResult)` | 确认 | OK → `true`；Cancel/barrier/Escape → `false` |
| `showPrompt(title, body, initial, buttons, onResult)` | 输入 | OK → 文本；Cancel/barrier/Escape → `nullopt` |

装配点：`build`（对话框子树叠加为栈层）、`config.onKey`（Escape，modal 优先）、`config.onCloseRequested`（打开时消费不退出）、`config.onRebuilt`（焦点安置/恢复）。busy() 拒绝嵌套便利对话框（嵌套场景走应用自拼）；**拒绝发生在改写任何状态/回调之前**（嵌套请求零副作用）。例外：**关闭转场窗口期（closing）的请求进深度 1 队列**，retire 后立即安装——回调内连环开框（"取消 A → 确认 B"链）不静默丢失。

## 2. 行为契约（与手拼 Dialog 一致，测试锁定）

- 结构：`makeDialog(body, actions, theme, "dialog-host:dismiss", key, view)`——barrier（onClick=dismiss、semanticsRole="dialog"）+ FocusScope 内容卡（Tab 不逃逸，plan §3.4）。
- 键盘：按钮经 HandlerRegistry（Enter/Space 激活聚焦按钮 = 点击同路径）；Escape = 取消（modal 优先于路由返回）；prompt 字段聚焦（光标置末尾），输入走标准 textInput 路径；**prompt 字段聚焦时 Enter = 提交**（经 onKey 层先行截获——字段自身的 Enter 失焦语义不吞掉提交预期；焦点在按钮上时不截获，按钮激活优先）。
- 焦点：打开 → 安置进域内（prompt 优先字段）；关闭 → 恢复唤起前焦点（消失则清焦点）。
- 转场：motionEnabled 时 `beginDialogTransition` 进/出场；关闭转场完成后才移除子树（settings 同款 retiring 语义）。直驱（无 tick）保持即时终态。
- 状态：prompt 值走 StateStore 独立 bind（`dialog-host:prompt`）；多次/跨实例不串值。
- 窗口关闭请求：打开时 = 取消并消费（不退出应用）。

## 3. 边界

- 同一时间至多一个便利对话框（宿主不排队；需要队列的应用在回调里再 show）。
- 长正文复用 makeDialog 的 body 滚动视口（bodyScrollOffset 由宿主持有——便利层无长文场景，恒 0）。
- scrimRadius：透明窗口应用需要时经应用自拼 makeDialog 传递（便利层默认 0）。
