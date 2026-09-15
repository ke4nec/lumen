# 控件视觉系统实施记录

> 本文是 [`gui-control-visual-system-task.md`](gui-control-visual-system-task.md) §12 分阶段实施（S0–S5）的记录文件，按 §14 模板逐阶段追加。
> 每阶段记录当次核对的真实源码状态；不改写历史条目来掩盖状态变化。

---

## S0 现状与样本冻结

- 阶段 / 日期 / 源码提交：S0 / 2026-09-15 / 源码基线 `d57625c`（文档基线一致，未新增代码变更）
- 已完成控件与规格章节：无代码改造；完成 §2 全量复核（下方映射表）、缺口清单冻结与可重复样本入口验证。

### 23 种 WidgetType 的状态 / 源码对应

来源核对：`src/style/resolver.cpp`（`resolveStyleImpl` 分发，resolver.cpp:354-372）、`src/render/painter.cpp`（`paintNode` 分发，painter.cpp:433-797）、`src/layout/layout.cpp`（`layoutSingle` 分发，layout.cpp:309-373；固有测量 `measureLeafIntrinsic`，layout.cpp:150-248）。

| WidgetType | resolver | painter | layout | 视觉现状摘要 |
| --- | --- | --- | --- | --- |
| `Text` | 专用（resolveText） | 文本 + 裁剪 | 专用测量 | 角色 body/contentPrimary；无字号层级差异样本 |
| `Button` | 专用（五变体） | `paintControlSurface` + 居中文本 + 可选尾随图标 | 专用（min 尺寸 + chrome padding） | 五变体齐；透明变体真透明；图标几何 painter 推导（iconGap 0.35×iconSize 等局部常量） |
| `TextField` | 专用 | `paintTextField`（选区/preedit/caret/密码） | 专用 | 状态齐（invalid/focused/readonly/disabled）；背景/边框来自 TextFieldTokens |
| `Checkbox` | 专用 | 内方块 mark（`drawRect`），**非勾号** | 专用 | §6.4 点名的旧实现：选中标记为内缩方块 |
| `Switch` | 专用 | 轨道 + 单一 knob 色 | 专用 | 无 knobOff/knobOn 区分；knobInset 固定 3 px 不随档位推导 |
| `Radio` | **容器回退** | 外环 + 内点为**同色两次填充**（前景色） | 借 Checkbox 测量 | §6.4 点名：非空心环；无专用 token/状态 |
| `Slider` | **容器回退** | trackHeight=max(8,h×0.35)、thumb=1.6×track、alpha 0.25/0.75 全部 painter 局部常量 | 局部常量 24/1.4 | §6.5 点名：无端点预留、无独立 Thumb 轮廓 |
| `ProgressBar` | **容器回退** | 与 Slider 共享分支 | 局部常量 16/0.9 | 无高度分档（4/6/8）；颜色非 token |
| `Dropdown` | **容器回退** | 值行走 `paintControlSurface`；文本左移 8.0F、右距 6.0F 局部常量 | 通用 default 测量 | M11 overlay 菜单由 `widgets::DropdownController` 组合；值行无字段同源 token |
| `Tabs` | **容器回退** | **无专用绘制**（等同 Row+子 Button） | `layoutFlex`（当 Row） | 无选中指示条/分隔线；外观完全由应用组装 |
| `Tooltip` | **容器回退** | 表面 + 文本左移 6.0F | `EdgeInsets::all(6)` 帧 | 无 caption/surfaceElevated/边界避让 token |
| `Icon` | 容器回退（装饰节点） | `drawIcon` 折线目录 | `icons.defaultSize` | 折线目录 12 个 ID；无 `IconId::Image` |
| `Image` | 容器回退 | 占位：硬编码色 (39,39,42)/(82,82,91)、边框 max(1,h×0.04)、叉臂 0.25 | 通用 default 测量 | §6.10 点名的旧占位实现 |
| `Container` | 专用（containerCommon） | `paintSurface` | `layoutContainer` | 透明默认；panel/inset 组合由应用表达 |
| `Row`/`Column` | 容器回退 | 无装饰（正确） | `layoutFlex` | 间距由应用传入，无 token 默认 |
| `Stack` | 容器回退 | 无装饰 | `layoutStack` | 命中顺序 = 逆序绘制，正确 |
| `ScrollView`/`ListView` | 容器回退 | `paintSurface` + 裁剪 + 附属滚动条 | `layoutScrollView` | 滚动条 Thumb = foreground×0.45 alpha（painter 局部常量）；ScrollbarTokens rest/hovered/dragged/minLength **已声明未消费** |
| `VirtualList` | 容器回退 | 同 ScrollView | `layoutVirtualList` | 稳定 key 物化已有测试 |
| `Grid` | 容器回退 | `paintSurface` | `layoutGrid` | 布局职责符合 §7.1 |
| `FocusScope` | 容器回退 | 无表面（正确） | `layoutContainer` | 焦点边界/恢复由 FocusManager 管理 |
| `ThemeScope` | 容器回退（+ScopedThemeOverride） | 无表面（正确） | 主题覆盖后 `layoutContainer` | 派生链经 thread_local 覆盖（resolver.cpp:340-350） |

### 辅助能力对应

| 能力 | 现状（源码证据） |
| --- | --- |
| 状态模型 | `WidgetState` 仅 hovered/pressed/focused/disabled/checked/invalid/selected（include/lumen/core/state.h）；readOnly 是 TextField 属性；无 loading/indeterminate/warning |
| 焦点环 | 内嵌绘制（painter.cpp:207-226），宽度 `metrics.focusRingWidth`（默认 2，高对比 3，theme.cpp:466） |
| 高对比 | `applyHighContrast`（theme.cpp:442-468）：边框 2 px/环 3 px、accent/边框派生、组件 token 重建 |
| 阴影 | `ElevationTokens` 单级（blur 12/offset 0,4/alpha 96）；无 level 0–3 分级；CPU 为扁平降级 |
| 动效 | `motionTransitions` 显式开启状态色过渡；Tooltip 400/120 已有；Switch knob 位移动画无 |
| Gallery | 7 路由 + headless 冒烟 + `--dump-frame` 首帧 RGBA（examples/gallery/main.cpp:105-129）；**无强制 hover/pressed 状态矩阵样本**（Buttons 页仅 Enabled/Disabled/Selected，gallery_app.h:1189-1214） |

### ColorScheme 与 §4.2 目标的差异（S1 输入）

- **缺失字段**：`surfaceSunken`、`accentContent`、`onError`、`errorContent`（grep 无命中）；Danger 文字借 `onAccent`、Outline/Ghost 文字借 `accent`（theme.cpp:168-174）。
- **值不符**：dark `onAccent` ≈#EBF1FF（2.90:1，需换 #000000）；light `accent`/`surface`/`surfaceElevated`/`contentSecondary`/`focusRing`/`onAccentContainer`/success/warning；`selectionBackground` light alpha 130→64；`pressedOverlay` dark alpha 72→26；`disabledContent` 半透明→实色；`borderStrong` dark #555562→#8C8C98。
- **消费缺口**：ScrollbarTokens 三态色 + minLength 未消费（painter 用 alpha 乘法）。

### 测试入口（冻结）

| 用途 | 入口 |
| --- | --- |
| 全量构建+测试 | `docs/build-commands.md` §2 CPU-only 行（VS 多配置带 `-C Debug`） |
| 视觉单测 | `tests/style_tests.cpp`、`tests/render_tests.cpp`、`tests/render_command_tests.cpp`、`tests/visual_m6_tests.cpp`、`tests/layout_tests.cpp`、`tests/damage_tests.cpp`、`tests/grid_virtual_tests.cpp`、`tests/interaction_tests.cpp`、`tests/app_shell_tests.cpp`、`tests/motion_scroll_tests.cpp`、`tests/tween_tests.cpp`、`tests/gallery_integration_tests.cpp`、`tests/semantics_tests.cpp`、`tests/system_font_tests.cpp` |
| 可重复样本 | `build/examples/gallery/Debug/lumen-gallery --headless --dump-frame <path>`（1024×768、占位字体、确定性帧哈希 frame0..frame8） |
| 真实字体样本 | 同二进制窗口路径（注入系统字体，main.cpp:321-334） |

- 源码审查发现、修复与仍存缺口：见上表；S0 无代码修复。
- 状态/主题/尺寸样本及截图链接：仓库当前无历史截图资产；以 headless `--dump-frame` 为可重复样本入口（S1 起归档到 build 产物外部的报告链接，不入库）。
- 验证命令、测试结果、真实平台/后端/字体：Windows 11 / VS 2026 / CPU Debug：
  - `ctest --test-dir build -C Debug`：416/416 通过（0 失败）。
  - `lumen-gallery --headless`：全部路由冒烟通过；冻结基线帧哈希 frame0=`1f3c81e23e689424` … frame8=`9fa44e0ccf3ca50c`（占位字体、1024×768）。
  - `lumen-gallery --headless --dump-frame <path>`：导出 1024×768 RGBA（3145728 字节）成功。
  - Skia 光栅/GPU/Linux/macOS 未在本阶段运行（按 §13.2 由 S5 收敛阶段补齐）。
- 基线冻结：S0 帧哈希序列为后续阶段回归对照；任何视觉变更应使哈希按预期变化，未预期的哈希漂移必须在阶段 review 中解释。
- 兼容性或视觉基准变更：无。
- 未验证事项与预留能力：真实桌面截图与跨后端验证按 §10.3 推迟到 S5 收敛；§11 预留项不在本轮。
- 下一阶段及前置条件：S1（token 与通用绘制）——新增四个 ColorScheme 字段并全链派生（dark/light/四方向/高对比/系统强调色）、对齐 §4.2 值表、阴影分级 token、透明描边与焦点/圆角契约复核；跨后端契约（CPU 命令路径）先行验证。
