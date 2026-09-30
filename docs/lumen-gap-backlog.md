# Lumen 规划外缺口待办池（M15–M19 之外）

> 文档状态：附加待办池（2026-09-29 登记；2026-09-30 采纳完成——G-1~G-7 全部交付、G-8 位置记忆/单实例交付且任务栏进度留按需池，余项见 §6 状态记录）
> 定位：收纳 2026-09-29 第二轮「自研 GUI 好用实用」缺口分析结论——M15–M19 增强链规划之外、且不属于 M14-A 遗留验收的待补能力。本文只登记缺口、证据与建议归属，**不预设里程碑编号**；某项被采纳实施时，按主路线图维护规则提升为正式任务、回写状态并在本文标记去向（各项设计文档见状态记录列）。
> 上游文档：M0–M14 见 [`lumen-self-use-roadmap.md`](lumen-self-use-roadmap.md)；M15–M19 见 [`lumen-m15-roadmap.md`](lumen-m15-roadmap.md)。
> 范围基线：Windows/Linux/macOS 桌面。移动端维持 M9 冻结；数据库/网络/同步/账号、CSS/Flutter 兼容层、3D/WASM、DSL 可编程化、框架级 i18n 维持排除（与 M15+ 路线图 §1.2 一致），本文不产生这些方向的任务。

## 1. 前置欠账（先于本池任何条目）

以下不是新方向，而是既有出口条件的未完成部分；按四态规则，补验完成前「实战可用」的宣称持续悬空。本池条目（含 P0）应排在其后或与之穿插：

- Windows 讲述人/NVDA、macOS VoiceOver 真实读屏回环（脚本 `tests/uia_reader_loop.py` / `tests/voiceover_loop.py` 就绪，现场人工验收未做）。
- Windows/macOS 真实 IME preedit/commit/cancel、候选窗锚点、跨应用剪贴板、透明合成人工清单。
- Windows/macOS 一小时双窗口浸泡（Linux 已于 2026-09 完成，跨平台侧未做）。

## 2. 缺口总表

| 编号 | 优先级 | 缺口 | 现状证据（2026-09-29） | 建议归属 |
| --- | --- | --- | --- | --- |
| G-1 | P0 | 全局快捷键/命令分发层 | 菜单快捷键串仅展示、不触发；框架按键仅文本编辑内建键与菜单 mnemonic，应用只能 `RunOptions.onKey` 手写 switch | 并入 M16 设计（与系统级热键同模型） |
| G-2 | P1 | 崩溃兜底与持久日志 | 无 signal/terminate 捕获、无 backtrace；`[diag]` 为 printf 式不落盘 | 并入 M18（release 形态可观测性）或先行小项 |
| G-3 | P1 | 剪贴板深度（图片/格式/变更监听） | `ClipboardProvider` 经 SDL 仅支持文本 | M16 平台服务扩展或独立小项 |
| G-4 | P2 | 对话框便利层与缺失控件 | 无一键 MessageBox/confirm/prompt；无编辑 ComboBox、ColorPicker、DatePicker | M17 穿插；DatePicker 留按需池 |
| G-5 | P2 | 触控板 pinch/zoom 手势 | 指针/滚轮/拖动/fling 齐备，无 pinch 事件接入 | M15 手势仲裁表之后或 M17 |
| G-6 | P2 | 新应用脚手架与上手路径 | 示例仅 counter（过小）与 settings/gallery（1427/4246 行含大量演示逻辑） | 不依赖里程碑，随时可做 |
| G-7 | P3 | 轻量持久化助手 | `StateStore` 为字符串内存表，落盘由应用手写 | 按需池；与 G-6 组合交付价值最大 |
| G-8 | P3 | 窗口体验杂项（位置记忆/单实例/任务栏进度） | 窗口能力仅 min/max/restore/DPI（`include/lumen/platform/application_host.h`） | M16 之后的薄封装 |

## 3. 条目详情

### G-1 全局快捷键/命令分发层（P0）

- **现状与证据**：菜单项快捷键串（`src/widgets/menu.cpp` 菜单控制器）仅作展示，不触发动作；框架级按键处理只有文本编辑内建键（Ctrl/Cmd+Z/X/C/V 等，`src/core/interaction.cpp`）与菜单 mnemonic；应用侧唯一入口是 `RunOptions.onKey` 手写分支。无命令注册表、无上下文感知的绑定域。
- **为什么影响好用实用**：桌面工具离了 Ctrl+S/O/F/P 不算能用；每个应用重复实现「焦点域/路由/模态/覆盖层各自生效范围」的判定，且菜单显示串与实际行为容易分叉。
- **建议实现**：框架级命令注册表（命令 id → 回调 + 快捷键 + 生效域：窗口/焦点域/模态/覆盖层），键盘路径、菜单项、工具栏按钮共享同一命令模型；菜单显示串从注册表派生（单一数据源）；与 M16 的系统级全局快捷键（RegisterHotKey/XGrabKey 等）合并设计为同一模型的两层（窗口内 vs 系统级）。
- **验证要点**：headless 覆盖域优先级、键冲突、禁用态、模态下屏蔽规则；菜单显示串与实际绑定一致性；语义 action 与键盘同路径（沿用 M5 契约）；性能门槛不回退。

### G-2 崩溃兜底与持久日志（P1）

- **现状与证据**：无 signal/terminate 处理器、无 backtrace 捕获；诊断输出为 `RunOptions.diagnostics` 控制的 printf 式 `[diag]`，不落盘。M18（inspector/帧统计）面向运行中观察，不覆盖 release 崩溃现场。
- **为什么影响好用实用**：长期驻留的个人工具崩溃即「消失」，无任何现场可事后定位。
- **建议实现**：崩溃捕获（SIGSEGV/SIGABRT/`std::terminate` → 结构化诊断 + v0.2 命令录制/回放缓冲落盘到固定文件）；日志分级 + 文件 sink（大小上限/滚动，异步写不阻塞 UI 线程）；重启时检测脏标记并提示上次崩溃、指向日志路径。全部接线走 M14-C 结构化诊断通道，不新增散乱输出。
- **验证要点**：headless 注入崩溃 → 产物内容断言；正常退出不留脏标记；关闭态零开销（性能门槛与 frame hash 不变，同 M18 零开销纪律）。

### G-3 剪贴板深度（P1）

- **现状与证据**：M4 接入的剪贴板经 SDL 为纯文本；无图片/富格式读写、无剪贴板变更监听。
- **为什么影响好用实用**：截图/图片类工具、把 DataGrid 选区拷成图片或 TSV、粘贴按钮的可用态刷新（有无内容），都是工具应用高频路径。
- **建议实现**：`ClipboardProvider` 契约扩展（格式枚举 text/image/自定义 MIME、位图读写、变更事件）；SDL 能力不足处走原生 seam（复用 M12/M16 `native_services_*` 模式）；能力与支持进 `PlatformCapabilities`，不可用结构化降级。
- **验证要点**：Fake host 确定性记录 + 失败注入；三桌面跨应用文本/图片粘贴 smoke；服务失败不阻塞 UI 线程。

### G-4 对话框便利层与缺失控件（P2）

- **现状与证据**：模态对话框需经 `widgets::makeDialog` + `NavigatorController` 手拼（`include/lumen/widgets/navigator.h`）；Dropdown 为纯选择（`include/lumen/widgets/dropdown.h`），无可编辑 ComboBox；全库无 ColorPicker、DatePicker/Calendar。
- **为什么影响好用实用**：「确认覆盖文件？」这类三行代码的事现在要写一屏；四个缺失控件中编辑 ComboBox 与 ColorPicker 在工具应用中出现频率最高。
- **建议实现**：`showMessage/showConfirm/showPrompt` 便利 API（复用 makeDialog 的模态 barrier、焦点恢复、M10 转场与语义契约）；编辑 ComboBox = TextField + overlay 菜单复用；ColorPicker（HSV 面板 + token 化取色，新 token 走 [`lumen-visual-system-design.md`](lumen-visual-system-design.md)）；DatePicker 先留按需池。新控件按仓库规范配设计文档与 `design/*.html` mockup。
- **验证要点**：headless 交互/键盘等价/语义角色；便利 API 与手拼 Dialog 行为一致（同契约测试）；四态与三后端命令一致。

### G-5 触控板 pinch/zoom 手势（P2）

- **现状与证据**：指针/滚轮/拖动/fling、双击、hover 均齐备；无 pinch/zoom 事件接入 `InteractionController`。
- **为什么影响好用实用**：图像查看、画布类工具在 macOS 触控板上的基本预期；SDL3 有多点触控/手势事件源，只差框架接入。
- **建议实现**：SDL3 pinch → InteractionController 手势；与 M15 的手势仲裁表同源设计（pinch vs 滚动 vs 拖放 vs TextField 选区）；键盘等价（Ctrl+= / Ctrl+-，依赖 G-1 命令层）。
- **验证要点**：Fake host 合成多点事件确定性用例；仲裁优先级 headless 锁定；桌面触屏 pinch smoke（桌面触屏仍在范围内）。

### G-6 新应用脚手架与上手路径（P2）

- **现状与证据**：示例只有 counter（含 `.lumen` DSL 变体，过小）与 settings/gallery（1427/4246 行，混入大量演示逻辑）；无「最小真实应用」模板与 step-by-step 入门文档。
- **为什么影响好用实用**：自用框架的「好用」一半指写新工具快；现在每个新工具从 settings 拷改，启动成本高且容易带走演示代码。
- **建议实现**：`lumen-template`（CMake 骨架 + 窗口 + 自定义标题栏/菜单 + StateStore + 最小持久化的组合）；入门文档（`docs/`，README 链接）；模板纳入 CI 构建冒烟防漂移。
- **验证要点**：模板可独立构建运行；文档命令可复制执行；与 G-7 组合时模板即持久化助手的首个消费者。

### G-7 轻量持久化助手（P3）

- **现状与证据**：`StateStore` 为字符串键值内存表（`include/lumen/core/state.h`），落盘由应用各自手写。
- **为什么影响好用实用**：每个工具重复实现原子保存/加载/损坏恢复/版本迁移样板。
- **建议实现**：百行级 preferences 助手（键值 + 类型转换 + 变更通知 + 原子写 + 版本字段 + 损坏文件降级）；不进网络/同步，维持「应用层边界内的薄助手」定位，不改变主路线图 §1.2 的排除承诺。
- **验证要点**：headless 读写/损坏文件/原子性（写一半崩溃不留半文件）；与 G-2 的脏标记机制不冲突。

### G-8 窗口体验杂项（P3）

- **现状与证据**：无窗口位置/尺寸记忆、单实例激活（二次启动聚焦已有窗口）、任务栏进度；当前窗口能力为 min/max/restore/DPI。
- **为什么影响好用实用**：长期驻留工具的体验细节，用户每天都感知。
- **建议实现**：均为 M16 窗口能力之上的薄封装；位置记忆可消费 G-7；单实例需轻量本地 IPC（本地 socket/命名锁），注意不得引入网络能力承诺；任务栏进度走三平台原生 seam（Win32/Unity D-Bus/NSDockTile）。
- **验证要点**：Fake host 断言 + 三桌面 smoke；单实例失败（锁占用异常）有结构化降级。

## 4. 处理顺序建议

1. M15 收口（当前工作区已含拖入接收契约的阶段 1 改动），与 M14-A 跨平台补验穿插。
2. G-1 并入 M16 设计阶段（窗口内命令分发与系统级热键同模型一次定形）；G-2 并入 M18 或作为先行小项（崩溃现场等不了 inspector）。
3. G-3 随 M16 平台服务扩展交付。
4. G-4/G-5 随 M17 池式穿插；G-6 不依赖任何里程碑，可随时先做。
5. G-7/G-8 按需评估；G-7 建议与 G-6 模板组合交付。

## 5. 边界与纪律

- 本池条目不因登记获得排期；采纳时按主路线图 §9 维护规则提升为正式任务，回写本文状态与去向。
- i18n/网络/数据库/同步/移动端维持路线图排除项；重开需用户明确需求。
- 所有新能力沿用既有纪律：headless 先行、三后端命令一致、`PlatformCapabilities` 四态如实报告、性能 p50/p95 ≤10% 门槛、frame hash 稳定、设计文档与 `design/*.html` 同步。

## 6. 状态记录

| 编号 | 状态 | 去向/备注 |
| --- | --- | --- |
| G-1 | 已完成（2026-09-29） | 命令分发层落地：[`lumen-command-dispatch-design.md`](lumen-command-dispatch-design.md)、`include/lumen/app/command_registry.h`；随附 SDL 和弦字符修复；系统级热键留 M16 同模型第二层 |
| G-2 | 已完成（2026-09-29） | 崩溃兜底与持久日志落地：[`lumen-runtime-diagnostics-design.md`](lumen-runtime-diagnostics-design.md)、`include/lumen/diagnostics/runtime_diagnostics.h`（新模块 lumen-diagnostics）+ 注入进程冒烟；runApp 可选接线默认零开销 |
| G-3 | 已完成（2026-09-30） | 剪贴板深度落地：[`lumen-clipboard-service-design.md`](lumen-clipboard-service-design.md)、core MIME 数据层（默认实现降级）+ SDL data API + ClipboardChanged 广播；Windows/macOS 图片位如实 false（原生 seam 遗留） |
| G-4 | 已完成（2026-09-30；DatePicker 留按需池） | DialogHost（[`lumen-dialog-host-design.md`](lumen-dialog-host-design.md)）、编辑 ComboBox（[`lumen-combo-box-design.md`](lumen-combo-box-design.md)）、ColorPicker（[`lumen-color-picker-design.md`](lumen-color-picker-design.md)）三批交付 |
| G-5 | 已完成（2026-09-30） | pinch 手势落地：[`lumen-pinch-gesture-design.md`](lumen-pinch-gesture-design.md)——多指 FINGER 流合成（SDL3 无原生 pinch 事件）、M15 仲裁表 pinch 位、键盘等价经 G-1 命令层 |
| G-6 | 已完成（2026-09-30） | 脚手架落地：`examples/template/`（纳入主构建 CI 冒烟）+ 入门文档 [`lumen-getting-started.md`](lumen-getting-started.md)（README 已链接）；模板为 G-7 持久化首个消费者 |
| G-7 | 已完成（2026-09-30） | 持久化助手落地：[`lumen-preferences-design.md`](lumen-preferences-design.md)、`include/lumen/core/preferences.h`（原子写/损坏降级/版本字段/变更通知）；G-6 模板为其首个消费者 |
| G-8 | 部分完成（2026-09-30） | 位置记忆（WindowDesc/Metrics + Preferences）与单实例（core::SingleInstanceGuard，POSIX）落地：[`lumen-window-experience-design.md`](lumen-window-experience-design.md)；任务栏进度留三平台原生 seam 按需池（SDL 3.2 无 API） |
