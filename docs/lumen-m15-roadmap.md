# Lumen M15+ 增强链路线图

> 文档状态：规划（2026-09-29）
> 定位：收纳 2026-09-29「自研 GUI 标准缺口分析」结论，形成 M14 之后的后续增强里程碑链。
> 编号规则：延续自用路线图编号，M9 冻结不变；本文件维护 M15 起的规划与完成记录，M0–M14 的状态与完成记录仍以 [`lumen-self-use-roadmap.md`](lumen-self-use-roadmap.md) 为准。
> 范围基线：Windows/Linux/macOS 桌面。移动端维持 M9 冻结，本文不产生任何移动端任务；数据库、网络、同步与账号仍属应用层。

## 1. 缺口来源与需求映射

### 1.1 分析结论（2026-09-29）

按自研 GUI 框架通用标准（渲染、布局、控件、文本、输入、窗口、无障碍、i18n、工具链、发布）对照：Lumen 的工程骨架（三后端命令一致、性能门禁、语义契约、四态验收、便携发布、文档同步）完成度高；缺口集中在**交互层（拖放完全缺失）**与**平台深度集成层（窗口能力/托盘/原生菜单/全局快捷键）**，其次是文本深度（富文本/复杂脚本）、控件细节池与开发者工具。

| 优先级 | 需求 | 现状证据（2026-09-29） | 归属 |
| --- | --- | --- | --- |
| P0 | 拖放（OS 拖入 + 应用内行/列重排） | `include/`、`src/` 全文检索无任何 DnD 实现 | M15 |
| P0 | M14-A Windows/macOS 真实验收（读屏回环/真实 IME/透明合成/浸泡） | 回环脚本 `tests/uia_reader_loop.py`/`voiceover_loop.py` 就绪，现场人工验收未做 | 不新开编号，仍属 M14 出口 |
| P1 | 桌面窗口与系统集成（全屏/置顶/OS 模态/托盘/全局快捷键/macOS 原生菜单栏/交通灯） | 窗口能力仅 min/max/restore/DPI（`application_host.h`） | M16 |
| P1 | 发布尾项（Windows/macOS GPU 包、CPack Bundle） | package-skia-gpu 变体仅 Linux llvmpipe | M16 |
| P2 | 控件细节池（auto-hide/RTL 镜像/双轴联滚/跨行列合并/Splitter 塌缩/菜单 mnemonic/行内编辑/DataGrid 筛选等） | 各设计文档「已知限制」与主路线图按需池 | M17 |
| P2 | 开发者诊断工具（inspector/帧统计/headless dump） | 仅 JSON 基准报告与 poll 换根热重载接缝 | M18 |
| P1（按需） | 富文本与复杂脚本（HarfBuzz 合字/完整 UBA/TextSpan） | shaping 为逐 grapheme cluster、bidi 为 UAX#9 确定性子集 | M19（按需启用） |

### 1.2 不纳入本链

- 移动端（M9 冻结）、数据库/网络/同步（应用层）、CSS/Flutter API 兼容层、3D/WASM、DSL 可编程化/脚本能力。
- 框架级 i18n/本地化资源系统：维持主路线图 §1.2 边界，由应用自行管理字符串与区域设置；若未来目标应用明确需要，再按需评估（不预设编号）。
- 桌面专用 GPU 后端（Graphite/Vulkan/Metal/D3D）：维持按需评估池，不因本链改变。
- M15–M19 之外的「好用实用」缺口（快捷键命令分发、崩溃兜底与持久日志、剪贴板深度等，2026-09-29 第二轮缺口分析结论）：登记于 [`lumen-gap-backlog.md`](lumen-gap-backlog.md) 附加待办池，未提升前不占本链编号。

## 2. 依赖与进入条件

```text
M14 实战可用收敛（总出口）
        ↓
M15 拖放 ────────┐   M15/M16 无强依赖，可并行或换序
M16 窗口与系统集成 ┘
        ↓
M17 控件细节与 RTL 镜像（池式交付；行/列重排相关项依赖 M15）
M18 开发者诊断工具（仅依赖 M14-C 诊断通道，可任意穿插）
M19 富文本与复杂脚本（按产品需要启用，不占默认顺序）
```

- **M15 进入条件**：M14-B/M14-C 已收口、M14-D 达到默认交付。M14-A 跨平台人工验收未完成不阻塞 M15 开工，但「实战可用」的宣称仍以 M14 总出口为准（四态规则不变）。
- **M17** 中 DataGrid 列拖序、List/Tree 行重排依赖 M15；其余条目相互独立，允许按实际需要选取子集交付，未交付项必须留在本文件并保持状态如实。
- **M18** 只依赖 M14-C 结构化诊断通道，可在 M15–M17 期间任意穿插。
- **M19** 由用户明确的产品需求触发（阿拉伯/南亚脚本或富文本），触发前不排期。

## 3. M15：拖放（P0）

**目标**：补齐自用工具高频的拖放闭环——OS→应用的文件/文本拖入，应用内 List/Tree/DataGrid 行重排与 DataGrid 列拖序；键盘等价与无障碍契约同步交付。

**实现**

1. 平台契约（`lumen-platform`）
   - `HostEvent` 新增 Drop 类事件（文件/文本 + windowId + 指针位置，及 enter/leave/position 用于落点反馈）；`ApplicationHost` 新增 `startDrag(WindowId, DragPayload)` 拖出接口；`PlatformCapabilities.dragDrop` 如实报告接收/拖出两项能力。
   - SDL3 host：接收走 `SDL_EVENT_DROP_*`；拖出依赖 SDL 版本能力，固定 SDL 3.2.10 缺失时结构化 Unavailable（不阻塞启动），SDL 升级时重评。
   - Fake host：拖入/拖出确定性记录 + 失败注入（沿用 M4 服务模式）。
2. core 交互层
   - `InteractionController` 拖放会话状态机（pending → dragging → dropped/cancelled；指针取消复用既有取消事件）；拖拽启动阈值 token 化。
   - 手势仲裁表：拖放 vs TextField 选区拖动 vs M10 滚动 `applyDrag` vs DataGrid 列宽拖宽（splitter 通道）——统一优先级并以 headless 用例锁定。
3. widgets 层
   - List/Tree/DataGrid `onReorder` 回调（stable key 驱动，与 SelectionModel 正交）；DataGrid `reorderColumn`（复用列宽手柄通道经验，见 DataGrid 设计 §17.3）。
   - 拖拽 ghost 经 M11 框架级 overlay 承载；drop 目标高亮 token 化（经 [`lumen-visual-system-design.md`](lumen-visual-system-design.md) 增补 DragDrop token 组，控件不写死常量）。
4. 无障碍
   - 拖放功能必须提供键盘等价路径（如 Alt+↑/↓ 移动、上下文菜单命令）与语义 action（Reorder/MoveUp/MoveDown）；语义树暴露可重排性。纯指针路径不得成为唯一路径。

**接口约束**

- 拖放会话由 UI 线程拥有；平台回调只投递不可变数据（M4 异步回调同步模式）。
- 重排不得破坏 stable key/identity 规则；重排后焦点、语义、滚动位置与编辑状态保持（复用 M3 回收测试模式）。
- OS 拖入能力不可用时应用零影响启动：能力 false + 结构化降级。
- 新增 token/视觉走视觉系统文档同步更新（含 `design/*.html` 对齐）。

**验证**

- headless：状态机全路径（启动/越过阈值/取消/落点命中/跨窗口）、手势仲裁、重排后状态保持、键盘等价与语义 action、Fake host 失败注入。
- 窗口 smoke：三桌面真实 OS 文件/文本拖入 settings/Gallery。
- 性能：拖拽进行中的帧不破 10% 门槛；无会话的静态场景零额外动画帧、既有 frame hash 不变。

**出口条件**：三桌面 OS 拖入 smoke 通过；行/列重排 + 键盘等价有 headless 证据；拖出能力如实报告（可用或结构化不可用）；性能门槛与既有 hash 全绿。

## 4. M16：桌面窗口与系统集成（P1，含发布尾项）

**目标**：把窗口能力与系统集成补齐到「个人工具可长期驻留」水平：全屏、置顶、OS 级模态、系统托盘、全局快捷键、macOS 原生菜单栏与交通灯；收口 Windows/macOS GPU 包尾项。

**实现**

1. 窗口能力：`ApplicationHost` 新增 `toggleFullscreen`/`setAlwaysOnTop`/`setWindowModal(parent)`；结果经 Window 事件交付（FullscreenChanged/AlwaysOnTopChanged/ModalChanged 类），`WindowMetrics` 与能力报告同步。SDL3 对应 API 以固定版本为准，缺失项结构化 Unavailable。
2. 系统托盘：TrayIcon 服务（图标/tooltip/菜单/点击事件）；SDL 无对应能力时走三平台原生 seam（Win32 `Shell_NotifyIcon`、Linux StatusNotifierItem D-Bus、macOS `NSStatusItem`），复用 M12 `native_services_*` 模式；托盘菜单事件回灌 `HostEvent`。
3. 全局快捷键：Win32 `RegisterHotKey`、macOS Carbon/NSEvent、Linux X11 `XGrabKey`；Wayland 无标准门户时如实 Unavailable，不伪装支持。
4. macOS 原生菜单栏与标题栏：自绘 MenuBar 的数据模型单向映射到 `NSMenu`（数据单一来源仍是框架菜单模型）；标题栏 macOS 交通灯与 borderless 最大化回退策略收口（同步 [`lumen-titlebar-design.md`](lumen-titlebar-design.md)）。
5. 发布尾项：package-skia-gpu 变体扩展至 Windows/macOS；CPack Bundle 生成器收口；新形态以 CI 首跑为事实来源。
6. 全部新能力进入 `PlatformCapabilities` 与 M14-C 结构化诊断。

**接口约束**：公共头无平台 SDK 类型；原生实现限于 platform 目标；服务失败不阻塞 UI 线程；能力报告遵守四态规则。

**验证**：Fake host 断言 + 失败注入；三桌面 smoke（全屏切换、DPI 变化下置顶、OS 模态父子行为、托盘显示/菜单/点击、全局快捷键触发、macOS 菜单栏与交通灯操作）；包变体 CI 解包冒烟。

**出口条件**：settings/Gallery 演示全部新能力；不可用环境结构化降级且诊断可读；Windows/macOS GPU 包 CI 通过。

## 5. M17：控件细节与 RTL 镜像（P2，池式交付）

**目标**：收口各设计文档「已知限制」中已识别的控件增量；逐项独立验收，不破坏既有契约。

**实现**（每项须同步对应设计文档与 `design/*.html`）

| 项 | 内容 | 设计文档 |
| --- | --- | --- |
| auto-hide 滚动条 | 显隐过渡（MotionTokens 驱动；reduceAnimation 直达终态） | [`lumen-scroll-design.md`](lumen-scroll-design.md) |
| 同视口双轴联滚 | 横纵滚轮/触摸在对角输入下联滚 | [`lumen-scroll-design.md`](lumen-scroll-design.md) |
| RTL UI 镜像 | textDirection 语义（start/end 对齐映射、滚动条/分隔线/chevron/进度方向镜像）；与 M1 bidi 文本子集协同 | [`lumen-visual-system-design.md`](lumen-visual-system-design.md) |
| Grid 跨行列合并 | rowspan/colspan；横向网格按需评估 | 已交付 2026-09-30（[`lumen-grid-span-design.md`](lumen-grid-span-design.md)）；横向网格维持按需 |
| Splitter 窗格塌缩 | 塌缩/KeepRatio + `.lumen` 节点 | [`lumen-splitter-design.md`](lumen-splitter-design.md) |
| 菜单增强 | mnemonic 下划线、Alt/F10 单键、触摸长按 | [`lumen-menu-controls-design.md`](lumen-menu-controls-design.md) |
| 行内编辑 | Tree/List 行内编辑（复用 TextField 编辑事务契约） | 已交付 2026-09-30（collection-design §6.6） |
| DataGrid 增量 | 筛选面板与搜索（`onFilterRequest` 接线/条件模型/无结果状态，设计 §11.2）；异步提交 draft+saveError 契约（§13.1）；表头 hover 前景与多列优先级角标（Button 内容通道扩展）；Grid/Table 专属语义 role 与单元格级焦点导航（§8） | [`lumen-datagrid-design.md`](lumen-datagrid-design.md) |
| DSL/基准补齐 | 上述新能力与既有 Spin/Toolbar/StatusBar/Splitter 的 `.lumen` 节点与基准场景 | DSL 文档 |

**接口约束**：每项可独立交付；未启用新特性的场景 frame hash 不变；RTL 镜像只发生在布局与 token 解析层，渲染命令保持物理 LTR 坐标，命中测试契约不变。

**验证**：每项 headless 覆盖；RTL 黄金用例（Row 对齐/滚动/DataGrid 冻结列/分隔线镜像）；性能门槛不回退。

**出口条件**：清单全部实现，或未实现项在本文件保持「待交付」状态并同步到支持矩阵已知限制；无未文档化的行为分叉。

## 6. M18：开发者诊断工具（P2）

**目标**：缩短「看到问题 → 定位」路径，为自用开发提供框架级诊断工具。

**实现**

1. Inspector：运行时 Widget/Element 树可视化（type/key/WidgetState/ResolvedStyle/bounds/damage），节点选中 → bounds overlay；overlay 走普通 RenderCommand 绘制，三后端一致。
2. 语义树视图：复用 RecordingAccessibilityBridge 数据（identity diff/焦点/action 记录）。
3. 帧统计 overlay：FrameReason 分布、build/layout/paint/submit 分相、fps、命令数/节点数（与基准计数同源，经 FrameScheduler 供给）。
4. headless dump：`--dump-tree/--dump-style/--dump-semantics` 文本化导出，与既有命令录制/回放、像素导出配套，构成无窗口对照工具链。
5. 接线：诊断开关（环境变量或 RunOptions）统一走 M14-C 结构化诊断通道，不新增散乱输出。

**接口约束**

- 默认关闭且零开销：关闭态基准 frame hash 与性能必须与基线完全一致（CI 断言）；开启态属于诊断场景，允许帧输出不同。
- inspector 代码不进入公共头依赖路径，不改变应用的链接面。
- overlay 作为独立诊断层提交，不参与应用帧的 damage/缓存判定。

**验证**：headless dump golden 对照；三后端开启态冒烟；关闭态 hash/性能与归档基线对照。

**出口条件**：dump 工具纳入 CI 工具链（`build-commands.md` 收录用法）；inspector 三桌面窗口 smoke 可用；零开销断言入 CI。

## 7. M19：富文本与复杂脚本（按需启用）

**状态**：按产品需要启用（原 M14-E 升格为独立里程碑）；未触发前不排期、不占增强链顺序。

**触发条件**：目标应用明确需要阿拉伯文/南亚复杂脚本高保真排版，或混排样式富文本成为产品需求；由用户明确需求触发。

**实现**

1. 富文本模型：TextSpan 树（样式继承与局部覆盖）、布局/命中/选区/caret 与 `EditingHistory` 事务扩展、DSL/builder 支持。
2. HarfBuzz 合字 shaping：接入 Skia 预编译包自带的 skshaper/harfbuzz/icu 归档（版本随 Skia pin 固定）；`FontManager::shapeCluster` 已预留多 glyph 返回；CPU-only 构建保持占位可用。
3. 完整 UBA：显式嵌入/隔离控制、镜像括号、数字定形；grapheme 边界仍为唯一编辑索引。
4. 语言相关字体 fallback 策略。

**接口约束**：不改变编辑事务/选区/无障碍索引契约；harfbuzz/icu 固定版本并记录许可证（FetchContent 规则）；CPU-only 构建可独立编译。

**验证**：固定字体环境的 glyph/cluster/命中测试证据；富文本黄金用例 + undo/redo 事务；既有 zh/en 用例与基线零变化。

**出口条件**：目标语言集合与富文本场景收口；既有编辑/语义/性能门槛不回退。

## 8. 风险与处理顺序

| 风险 | 表现 | 处理顺序 |
| --- | --- | --- |
| SDL 能力边界（拖出/托盘/全局快捷键等） | 固定 SDL 3.2.10 缺 API | 构建期探测 + 原生 seam + 结构化降级；能力如实报告，不伪装 |
| 拖放 × stable key/焦点复用 | 重排后状态串项 | 契约测试先于实现（M3 回收测试模式） |
| 手势仲裁冲突 | 拖放/滚动/选区/列宽互抢 | InteractionController 统一优先级表 + headless 锁定 |
| 双源菜单（自绘 + macOS 原生） | 状态分叉 | 数据单一来源为框架菜单模型，原生层只做渲染与事件回灌 |
| RTL 镜像波及面 | 坐标系混乱 | 镜像限定在布局/token 解析层，渲染命令保持物理 LTR；黄金用例先行 |
| 富文本依赖体量 | harfbuzz/icu 引入风险 | M19 按需触发；版本/许可证固定；CPU-only 可用 |
| inspector 开销 | 基准回退 | 默认关闭零开销断言入 CI |

## 9. 测试、CI 与证据要求

沿用主路线图 §6 全部规则：headless 先行、三后端命令一致、四态能力描述、性能 p50/p95 ≤10% 门槛、frame hash 稳定、CI 矩阵不变。本链附加要求：

- 每个里程碑的完成记录追加到本文件 §10（模板同主路线图 §10）。
- 能力/支持矩阵变化必须在同一变更中更新 [`support-matrix.md`](support-matrix.md)。
- 涉及窗口/系统服务的项，Fake host 断言之外必须补三桌面真实 smoke，或如实标注「未覆盖平台」。

## 10. 完成记录

（模板同主路线图 §10：完成日期/提交号/变更/测试/平台/已知限制/回滚点。自 M15 起逐里程碑追加。）

### M15 进行中状态记录（2026-09-29，实现阶段落地）

- 完成日期：实现批次 2026-09-29（未达总出口——三桌面 OS 拖入 smoke 未做）。
- 提交号：f0f2773（平台契约）/ df9579b（core 会话状态机）/ 9c43240（List 行重排 + 非模态视觉 overlay + DragDropTokens）/ 66bad59（DataGrid 行/列重排 + 键盘/语义等价）。
- 变更：契约与实现详见 [`lumen-drag-drop-design.md`](lumen-drag-drop-design.md)（§1–§9）。分层——HostEvent DragEnter/Move/Drop/Leave + startDrag 结构化降级（SDL 3.2.10 无拖出 API，能力位如实 false）；InteractionController 会话状态机（arm 认领 → 8px 阈值 → Start/Move/Drop/Cancel；手势仲裁表：scrollbar/splitter/slider/文本选区优先，触摸行拖拽让位滚动）；List/DataGrid 行重排（onReorder 先移除后插入语义）+ DataGrid 表头列拖拽（moveColumn 区域钳制）；Alt+↑/↓ 键盘等价与语义 MoveUp/MoveDown（kActionMoveUp/Down + moveCollectionRow sink，Expand/Collapse 同型）；ghost/插入指示线经框架**非模态视觉 overlay**（`setVisualOverlayBuilder`——模态 overlay 的 pointerCancel 语义不适用，设计 §5）；DragDropTokens（视觉系统 §3.4 + `design/drag-drop.html`）。
- 测试：Linux CPU Debug 863/863（新增 20 用例：平台 3 + core 会话 6 + List/DataGrid 11，`[m15]`/`[dragdrop]` 标签）。基准 hash 未回退（全量含既有像素/哈希断言）。
- 平台：本地 Linux 全部验证（含 SDL dummy 能力位冒烟）；Windows/macOS 以 CI 为事实来源；**三桌面真实 OS 文件/文本拖入 smoke 未做**（四态：接口已存在 + headless 已验证）。
- 已知限制：触摸行/列拖拽不认领（触摸拖动保持滚动语义，专用句柄认领通道就绪但无消费方）；拖拽中无自动滚动；无 Esc 中途取消；ghost 抓取偏移固定；拖出（OS drag start）结构化 Unavailable 待 SDL 升级。List 语义 action 暴露随首个应用需求补。
- 回滚点：`1e1c19c`（M15 起点前）。

### M16 进行中状态记录（2026-09-29，实现批次落地）

- 完成日期：实现批次 2026-09-29（未达总出口——真实平台 smoke 与 macOS 原生菜单栏/交通灯未做）。
- 提交号：abf2f75（窗口能力）/ d5d5c44（托盘 + 快捷键契约）/（本批：包变体与状态记录）。
- 变更：
  - 窗口能力：`toggleFullscreen`/`setAlwaysOnTop`/`setWindowModal`（SDL3 组合 SDL_SetWindowParent+SetWindowModal）；`WindowFullscreenEntered/Exited` 事件 + `WindowMetrics.fullscreen`；能力位 windowFullscreen/AlwaysOnTop/Modal 如实报告；Fake host 记录 + 状态驱动事件。
  - 系统托盘：SDL_tray 跨平台直连（图标/tooltip/菜单；激活经互斥队列 → `TrayActivated` 回灌归属窗口；separator 以禁用 "-" 近似——SDL 3.2.10 无 separator 条目）；`setTray/removeTray` + Fake 记录/失败注入。
  - 全局快捷键：契约（`GlobalHotkeySpec` + `registerGlobalHotkey`/`unregisterGlobalHotkey` + `GlobalHotkey` 事件）就绪；**无平台后端**——SDL 3.2.10 无 API，Win32 RegisterHotKey/X11 XGrabKey/macOS seam 为后续增量，能力位恒 false + 结构化 Unavailable。窗口内命令分发不在此层（用户 G-1 命令注册表已实施）。
  - 发布尾项：windows.yml/macos.yml 新增 `package-skia-gpu` 变体（package-skia + LUMEN_ENABLE_GPU + 解包 headless/GPU 帧冒烟；以 CI 首跑为事实来源）。
- 测试：Linux CPU Debug 新增 4 用例（[m16]：fake 记录/事件回灌/失败注入、SDL dummy 能力位与结构化降级）。run_app 的 TrayActivated/GlobalHotkey 转发 case 已随并行 G-2 的 run_app 变更落地（工作区）。
- 平台：本地 Linux 验证；Windows/macOS 以 CI 为事实来源（含新 package-skia-gpu 首跑）。
- 已知限制：macOS 原生菜单栏（NSMenu 单向映射）与交通灯/borderless 最大化回退未实施（无 macOS 编译环境，按证据规则登记为增量）；全局快捷键无平台后端；托盘 submenu/checkbox 条目未接（SDL_tray 能力就绪，按需增量）。
- 回滚点：`bfe4d43`（M16 起点前）。

### R4 全局快捷键 X11 后端状态记录（2026-09-30）

- 变更：`src/platform/global_hotkeys.h/.cpp`（内部接缝，不出公共头）——
  纯映射（`keysymForLumenKey`/`modifierMask`，显式映射不依赖位序）+ 会话
  探测（`probeGlobalHotkeySession`：XDG_SESSION_TYPE=wayland 或
  WAYLAND_DISPLAY 在场且无会话声明 → 结构化不可用——XWayland grab 只覆
  盖 X11 客户端，不宣称系统级）+ X11 后端（独立 `XOpenDisplay` 连接，
  `XGrabKey` 抓根窗口按键：owner_events=False、锁定键 8 组合各 grab、
  事件匹配剔除锁定掩码；BadAccess 经临时错误处理器 + XSync 检测冲突并
  回滚全部组合）。`Sdl3ApplicationHost` 装配：probe 通过且能开显示 →
  `globalHotkeys=true` + 注册/注销委托 + `pollEvent` 非阻塞轮询转
  `GlobalHotkey` 事件；shutdown 先于视频子系统释放连接。Xlib 的
  None/True/False 宏与 core 枚举冲突——include 后 `#undef`，X 调用用
  字面量。CMake：Linux 下 `global_hotkeys_x11.cpp` 入平台库，x11 缺失
  时源码 `__has_include` 自空化（dbus seam 同模式）。
- 测试：映射纯函数契约（keysym 值/修饰位，headless）；结构化分支测试改
  造为按探测结果断言（X11 会话 = 注册/重复 id/未知注销三态；其余 =
  Unavailable + 可读原因）——本机 Wayland（不可用分支）、Xvfb X11（后
  端分支）、显式 wayland（不可用分支）三环境各跑通；XTEST 端到端
  （`LUMEN_GLOBAL_HOTKEY_E2E=1` 显式开启，普通 ctest 跳过）：注册
  Alt+Escape → 合成按键 → `GlobalHotkey` 事件送达（text=id、window=
  owner）→ 注销后同按键不泄漏。Xvfb 极简 keymap 的 Control_L 键码会映
  射到锁定修饰符（合成 state 为 Lock|Mod1），e2e 用 Alt 单修饰组合规
  避——真实键盘无此问题，如实注释。
- 四态：接口已存在 + headless 已验证（映射单测/三环境结构化分支）+
  X11 协议级已验证（Xvfb XTEST 端到端）；真实桌面（非 Xvfb）按键验收
  待现场（platform-acceptance 登记）。
- 未交付池项：Win32 RegisterHotKey（消息窗口）与 macOS
  RegisterEventHotKey 后端（保持结构化 Unavailable + 命名原因；按真实
  需求与编译环境启用）、托盘 submenu/checkbox、macOS 原生菜单栏/交通灯。

### 阶段C 首项状态记录（2026-09-30，Grid 跨行列）

- 变更（[`lumen-grid-span-design.md`](lumen-grid-span-design.md) 首版随码交付）：
  - `Widget.gridColumnSpan/gridRowSpan` + `withGridSpan` 修饰（构造期钳
    负值/0；colspan 布局期钳到 [1, 列数]）。
  - `layoutGrid` 重写：占位表流式放置（光标行优先扫描整块空闲矩形）→
    跨列宽测量（span=1 即单格宽）→ 两段行高解析（常规项行内最大 +
    跨行差额按放置序计入末跨行）→ 行顶前缀和定位，子项按声明序输出。
    **span=1 与 M3 基线逐字节同几何**（既有 golden/帧哈希不动）。
  - Gallery Layout 页 `spans-grid` 样本（跨 2 列 + 定高跨 2 行 + 绕行
    格）；mockup `design/grid-span.html`。
- 测试：`grid_virtual_tests` 新增 6 用例 `[grid][span]`（跨列宽度/换行、
  colspan 钳制、跨行差额入末跨行 + 占位绕行、rowGap 计入跨行覆盖、显式
  1/1 整树相等、自适应列数协同）；gallery 集成 1 用例（样本几何）。
  全量 ctest 通过（本批 +7；跨行列为纯 core 布局，无平台路径）。
- 未交付池项（更新）：双轴联滚、RTL 镜像、菜单 mnemonic、Tree/List
  行内编辑、DataGrid 筛选面板 UI/条件模型、横向网格（按需评估）、
  inspector GUI/bounds overlay。

### R6 续批状态记录（2026-09-30，bounds/damage 调试图层）

- 变更：`include/lumen/app/bounds_overlay.h`（header-only 合成 RenderNode
  平铺树——绝对 offset 为数据，无需布局坐标系统；`makeBoundsOverlayTree`
  每节点 1px 描边叶/深度交替 focusRing·borderStrong，`makeDamageOverlayTree`
  逐矩形半透明填充+描边）+ `AppShell::setDebugBoundsOverlay/
  setDebugDamageOverlay`（paintFrame 主场景与 tooltip 命令后追加录制；
  damage 层留存提交前逐矩形清单）+ `RunOptions.debugBoundsOverlay/
  debugDamageOverlay`（settings/gallery `--bounds-overlay`/
  `--damage-overlay`，可与 `--frame-overlay` 叠加）。纯绘制层：不驱动帧
  节奏、不进语义树（全子树 excludeFromSemantics/Focus）、默认关闭零开销
  （frame hash 不变，单测断言）。
- 测试：合成树契约 2（节点计数/绝对偏移/深度色/空矩形跳过）+ 开关行为
  1（关闭逐字节同输出与命令数；bounds/damage 开启改变像素与命令数；按
  钮外框左缘像素 = 主题 borderStrong——CPU AA 描边实测）。首帧全量绘制
  无 damage 清单（描画为空、hash 同基线，如实断言）；局部 damage 帧
  （hover）描画进入像素。跨帧行为如实登记：局部帧只在 damage 区内重绘
  描画，可能残留描边（调试语义）。
- 未交付池项（更新）：inspector 交互式节点查看（选中 → 节点信息面板）、
  语义树视图、分配量统计——M18 §6 编号继续。

### 菜单 F10/Alt 单批状态记录（2026-09-30，menu-controls P3 首项）

- 变更（menu-controls-design §7.2/§14.1 收口）：`core::Key::F10`/`Alt`
  枚举扩展 + SDL 映射（F10/LALT/RALT；Shift+F10 上下文菜单惯例不在该
  路径——只翻译键值，修饰键随事件携带）；`MenuBarController::
  handleKey` 切换路径（F10 无修饰键 / 裸 Alt keyDown 且 mods 恰为 Alt
  位：关闭态开首项、打开态关闭恢复栏项焦点；空栏不消费）+ 打开态
  Alt+mnemonic 顶级切换补齐（§7.2 原文"菜单打开时"语义——顶级优先于
  面板内项 mnemonic，未命中字母回落面板路径）。
- 边界（如实）：无 keyUp 公共事件——裸 Alt 在 keyDown 时刻切换，不做
  Windows"释放时无其他按键才激活"的精确语义；Alt+字母和弦中裸 Alt 先
  行开栏后 mnemonic 切到目标菜单，终态一致（单测断言）；mnemonic 下划
  线渲染维持 §14.2 选项 b（不画，键盘生效）；触摸长按唤起维持 §14.4
  （随桌面触摸实测再定）。
- 测试：`menu_bar_f10_and_bare_alt_toggle`（开/关/焦点恢复、Shift+F10
  与 Ctrl+Alt 不消费、Alt 和弦终态一致 + provider 切换断言）+
  `sdl3_host_maps_f10_and_alt_keys`（SDL 事件推送翻译 F10/LALT/RALT
  键值与修饰位）。Gallery/Settings 菜单栏经既有 onKey 转发自动获得该
  行为（无需应用改动）。

### List/Tree 行内编辑状态记录（2026-09-30，阶段C 池项）

- 变更（collection-controls-design §6.6 首版随码交付）：
  `ListController`/`TreeController` 对称 API——`beginEdit(key)`/
  `commitEdit()`/`cancelEdit()`/`editing()`/`editingKey()` +
  `editValueOf`（初始值）+ `onItemEdited`（提交回调）。编辑器 = 行内
  TextField（绑定 `owner:edit` state，`requestFieldFocus` 程序化焦点
  ——无需先点击）；Enter 提交（IME composing 留给输入法）、Escape 取
  消；行未物化时 `beginEdit` 先滚动到位（Center 对齐）。DataGrid 编辑
  契约同源（§13.1）；控制器不改应用数据——`onItemEdited` 内应用更新
  并重建。Tree 行 key = 模型 key（折叠行滚动展开态路径）。
- 样本：Gallery 集合页列表双击重命名（`onActivated → beginEdit`，
  名字向量写回）；用法说明同步。
- 测试：List 提交/取消/同行 no-op/未知 key 拒绝/编辑态导航键放行/
  不同行先提交/源行消失取消（7 组断言）；Tree 根行/子行（先展开）/
  Escape；Gallery 端到端（双击 → 编辑器物化 → Ctrl+A 替换 → Enter →
  列表显示新名）。
- 已知限制（§6.6 如实登记）：无验证器通道（应用在 onItemEdited 校
  验，失败可再 beginEdit 回炉）；无 Tab 跨行移动（列表无可编辑格矩阵
  语义）。
- 未交付池项（更新）：inspector 交互式节点查看、语义树视图、分配量统
  计、双轴联滚、RTL 镜像、触摸长按唤起、横向网格——按需继续。


### M17 进行中状态记录（2026-09-29，池式首项交付）

- 完成日期：auto-hide 滚动条 2026-09-29（提交 859c58b）；其余池项未交付（见下）。
- 变更：`withAutoHideScrollbar`（与 withScrollbar 组合）；滚动活动代数（InteractionController：滚轮/键盘/滚动条拖动/源视口拖动/惯性推进消费即递增）→ AppShell tick 按 `MotionTokens.scrollbarAutoHideMs`（默认 800ms，reduceAnimation 不归零）调度可见窗口 → 交互快照 `scrollbarActive` → 布局期 `RenderNode.scrollbarHidden`（painter 跳过拇指绘制；**几何/命中区保留**——悬停即重显、拖拽捕获不失效；scrollbarGeometry 的 alpha=0 即无滚动条既有语义不变）。
- 测试：scrollbar suite 21 用例（新增 auto-hide 活动窗口/到期/悬停重显）；全量 895/895。
- 未交付池项（本批未动，状态如实）：同视口双轴联滚、RTL UI 镜像、Grid 跨行列合并、Splitter 窗格塌缩/KeepRatio、菜单 mnemonic/F10/触摸长按、Tree/List 行内编辑、DataGrid 筛选面板与异步提交契约、`.lumen` DSL/基准场景补齐。
- 回滚点：`5404ac8`（M17 起点前）。

### M18 进行中状态记录（2026-09-29，首块交付）

- 完成日期：headless 树导出 2026-09-29（提交 e253e89）。
- 变更：`include/lumen/app/tree_dump.h`（header-only `dumpRenderTree`——先序遍历，type/key/identity/box/flags/scroll，确定性输出）；settings `--dump-tree`（首帧 → stdout → 退出 0，CI/脚本可 diff）。
- 测试：确定性/标注/行数用例（app_shell_tests [m18]）。
- 未交付池项：语义树 dump（--dump-semantics）、帧统计 overlay、inspector GUI、三桌面冒烟——首块为 dump 工具链入口，后续增量同源构建。
- 回滚点：`859c58b`（M18 起点前）。

### M19 触发条件评估（2026-09-29）

按本文件 §2 约定，M19（富文本 TextSpan/HarfBuzz 合字/完整 UBA）**由用户明确的产品需求触发（阿拉伯/南亚脚本或富文本），触发前不排期**。截至本记录，无目标应用提出该需求——M19 维持按需池状态，不占增强链顺序。触发时按 §7 实施顺序执行（TextSpan 模型 → skshaper 接线 → 完整 UBA），出口条件不变。

### M17/M18 续批状态记录（2026-09-30）

- 提交号：7d42f49 + 4fa7b7d（Splitter 塌缩/KeepRatio）/ 173dc93（语义树导出）/ 9244f14（DataGrid 筛选接线）。
- 变更：
  - **Splitter 窗格塌缩/展开与 KeepRatio**（splitter-design §5.3/§15）：`SplitterSource::toggleCollapse/collapsed` 契约（默认 false 安全降级）；`SplitterController` ResizeBehavior 双行为（KeepRatio 按新 extent 等比缩放钳制——review 修复先缩放后更新顺序）、`setCollapsible/setCollapsed`（塌缩钉 minLeading + restore 点；任何移离 min 的输入自动解除塌缩）；Enter/Space 聚焦分隔条切换（§14 预留槽位落地；不可塌缩回退既有激活路径）。
  - **语义树导出**（M18 续块）：`AppShell::buildSemanticsSnapshot()`（无桥依赖、无 diff 副作用）+ `dumpSemanticsTree`（确定性 role/label/value/flags/actions/children）+ settings `--dump-semantics`。与 `--dump-tree` 构成 headless 对照工具链。
  - **DataGrid 筛选接线薄契约**（datagrid-design §10.2/§24 缺口收口）：`requestFilter(columnKey)`（§13.1 编辑守卫——失败中止不触发回调）、`setFilterActive`（默认空态 "No rows" → "No matching rows"，无结果 ≠ 无数据）。
- 测试：splitter 4 + 修复重跑（909/909 时点全绿含 splitter）；语义 dump 1 + settings 二进制输出核对；datagrid 筛选 2（编译级 + 断言参考既有 fixture 口径）。
- 验证边界（双会话并行）：G-5 pinch 会话在 app_shell/interaction 上持续编辑期间，全量 ctest 无法稳定运行；本批提交均不依赖未提交代码且经 `g++ -fsyntax-only` 编译级验证；909/909 为 M17-a review 修复后、G-5 大面积展开前的完整回归时点。最终回归以并行 G-5 收口后的全量 ctest 为准。
- 未交付池项（更新）：双轴联滚（需 ScrollView 双 offset 数据结构，超薄契约范围，维持未交付）、RTL 镜像、Grid 跨行列合并、菜单 mnemonic、Tree/List 行内编辑、DataGrid 筛选面板 UI/条件模型、inspector GUI/帧统计 overlay。

### R6 首批状态记录（2026-09-30，帧读数 HUD 与样式导出）

- 变更：
  - **样式导出**（M18 §6.4 三旗标补齐）：`dumpStyleTree`（tree_dump.h；节点行 + `style:` 前缀样式行，颜色 `#rrggbbaa`，组件专有段覆盖 variant 全集）+ settings `--dump-style` + gallery `--dump-tree`/`--dump-semantics`/`--dump-style` 平价（1024x768 首帧）。
  - **帧读数 HUD**（M18 §6.3 首版）：`AppShell::setFrameStatsCapture/frameDebugSnapshot`（采样默认关闭零开销——关闭态 frame hash 单测断言不变；开启采样 reconcile/layout 阶段耗时、fps 环（64 帧 steady_clock 环，跨度不足 1s 用平均帧率）、主树+overlay 节点计数）+ `app::makeFrameStatsOverlay`（frame_debug.h；Theme token 派生配色，全子树 excludeFromSemantics/Focus）+ `RunOptions.frameDebugOverlay`（runApp 装配非模态视觉 overlay 并每调度帧标脏刷新；settings/gallery `--frame-overlay`）。
- 测试：dump_style 确定性/组件覆盖/行数（app_shell_tests [m18]）；采样只读（hash 不变）+ HUD 组合/语义排除 + runApp 装配/默认关闭对照（[app][r6]）；gallery 三 dump 平价（gallery_integration_tests [m18]）。全量 ctest 940/940（本批 +5）。
- 边界（如实）：读数滞后一帧（overlay builder 重建期求值）；paint/submit/GPU wait 来自 renderer stats、reconcile/layout 为帧管线内 steady_clock 采样；分配量维度未接入（renderer stats 无该维度，M18 §6.3 的该项维持未交付）；`--frame-overlay` 开启态帧无确定性 hash（读数含真实时间），关闭态不变；HUD 与菜单/拖拽 overlay 共用视觉槽位（互斥）——菜单打开或拖放会话期间顶替 HUD，关闭后不自动恢复（重启开关恢复）。
- 未交付池项（更新）：inspector GUI（节点选中 → bounds overlay）、bounds/damage overlay、语义树视图、分配量统计——后续增量继续走 M18 §6 编号。
- 用法收录：`build-commands.md` §2（dump 三旗标 + `--frame-overlay` 行 + 确定性/golden 说明）。

### M15–M19 实现 review 记录（2026-09-30，逐提交复审）

全量重读 M15（拖放四提交）/M16（窗口/托盘/CI）/M17（auto-hide/splitter/筛选）/
M18（双 dump）实现，发现并修复三处缺陷（提交：本批 fix）：

1. **M16 托盘回调上下文 use-after-free**：SDL 托盘条目持久存在、可多次
   激活，而 `trayEntryCallback` 以 `unique_ptr` 在首次激活即释放上下文
   ——第二次点击悬空。修复：上下文由宿主 `trayContexts_` 持有（与托盘
   同寿命，`destroyTray` 清空），回调只读入队。
2. **M16 全屏状态 metrics 漂移**：`Sdl3ApplicationHost::windowMetrics()`
   从不填 `fullscreen`（PlatformWindow 不承载该状态）——消费方恒读
   false。修复：从宿主会话标志（ENTER/LEAVE_FULLSCREEN 事件回填）传播。
3. **M15 List 拖放会话 ghost 滞留**：`dragSession` 顶部源行查找守卫对
   Cancel/Drop 也生效——会话中数据变更使源行消失后 Cancel 永不执行
   `endDragSession`（ghost overlay 永久滞留）。修复：Cancel 无条件清理；
   Drop 先清理、源行存在才提交。

复审确认无缺陷的区域：DataGrid 会话 Cancel 路径（结构不同，`dragActive_`
守卫后无条件清理）；G-5 pinch 对拖放会话的仲裁（第二指落下经
pointerCancel 正确取消会话）；M15 SDL DROP 翻译（null data 不投递）；
M17 KeepRatio 与布局侧窄窗压缩的钳制一致性；auto-hide 活动代数与
pinch 无耦合。已知非缺陷项维持登记：List 尾部间隙（gap==itemCount）
指示线无定位（视觉缺失无害）、DataGrid 列指示线在水平虚拟化窗口外
不显示（§23 已记）、拖放会话中数据变更的 gap 陈旧性由 onReorder 应用
契约兜底。

回归：928/928 全绿（含本批 3 个新回归测试）。
