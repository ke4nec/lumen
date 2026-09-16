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

---

## S1 token 与通用绘制

- 阶段 / 日期 / 源码提交：S1 / 2026-09-15 / 基线 `313ba03`（S0 记录提交）
- 已完成控件与规格章节：§4.2 目标调色板（CoreDark 深浅全表）、§4.3 对比度门槛、§4.4 行内图标档位、§4.5 排版行高与阴影分级、§9.2 透明描边。
- 新增或调整的 token / ResolvedStyle / 公共 API：
  - `ColorScheme` 新增 `surfaceSunken` / `accentContent` / `onError` / `errorContent`；dark `onAccent`→黑、`pressedOverlay` alpha 72→26、`disabledContent` 实色化（n400 槽）；light `accentContainer`→blue100、`selectionBackground` alpha→64。
  - CoreDark 槽位/精化对齐 §4.2 全表（`applyCoreDarkTargets` 精化 6 项槽位冲突值；`style_core_dark_matches_palette_targets` 逐项断言）。
  - 四方向可读性修正（§4.3，保留色相调明度）：Aurora dark n400 提亮、light blue500/blue700/green/amber 加深；InkLinen n500 深/浅两侧、light green/amber 加深；Utility light amber 加深；Aurora dark 选区 alpha 130→96。
  - `ElevationTokens` 改为三级查表（L1 (0,2)/6/32、L2 (0,4)/12/64、L3 (0,8)/24/80；`paramsFor` 夹取 [1,3]）；layout 折叠消费；高对比各级 alpha=0。
  - `Typography` 行高倍数按 §4.5（title 1.4、body/label 20/14、caption 1.5）；`Metrics.inlineIconSize[3]{16,16,20}` 参与 fontScale。
  - 组件 token：outline/ghost 文字→`accentContent`、danger 文字→`onError`、TextField 背景→`surfaceSunken`/边框→`borderStrong`。
  - 渲染契约：`Renderer::drawRectStroke`（圆角描边环带；默认降级为填充）+ `CommandType::DrawRectStroke` + 序列化 v5；CPU（外/内圆角矩形包含差）/Skia 光栅/Skia GPU 三后端原生实现；painter 的边框与全部焦点环（控件表面/Checkbox/Switch/Radio 指示器）改用描边命令。
  - `adaptPlatformTheme`：accent 覆盖后重派生 `accentContent`（高对比直接取 accent）。
  - 移除 AuroraSignal dark `onAccent` 特判（并入通用"深色 Filled 深色文字"规则）。
- 源码审查发现、修复与仍存缺口：
  - 发现：透明背景控件（Outline/Ghost、聚焦透明变体）此前被"边框色整块填充"渲染成实心块（§9.2 点名的实现缺陷）；`paintSurface`/`paintControlSurface` 双层填充表达是该缺陷根源。已用描边命令修复并以像素测试锁定。
  - 发现：四方向若仅对齐 CoreDark 数值，多处状态文字（浅色 amber ≈2.3:1、Aurora light accent 白字 3.48:1 等）不达 §4.3；按"保留色相调明度"修正并纳入对比度测试。
  - 仍存缺口：ScrollbarTokens 三态色仍未被 painter 消费（S3）；Slider/ProgressBar/Radio 等 painter 局部常量未动（S2/S3）；焦点环与填充相近时的 1px 隔离带（§6.1）未实现（S2 Button 细节）。
- 状态/主题/尺寸样本及截图链接：Gallery headless 帧哈希已按预期变化（frame0 `68f176c9…`）；逐像素样本由本阶段新增渲染测试承担（描边透明性/圆角不越界/Outline 内部=页面色）。
- 验证命令、测试结果、真实平台/后端/字体：
  - CPU Debug `ctest`：429/429 通过（新增 13 个测试：对比度×4 方向、§4.2 值表、阴影分级、行高、描边命令契约、透明像素）。
  - Skia 光栅 Release：441/441 通过；GPU（Ganesh+GL）Release：441/441 通过（含 CPU/Skia 一致性与 smoke）。
  - 平台：Windows 11 / VS 2026；Linux/macOS 未运行（CI 补充）；字体：headless 占位字体。
- 兼容性或视觉基准变更：
  - 命令序列化 v4→v5（新增 DrawRectStroke）；旧 blob 拒绝（版本校验），回放侧无兼容负担。
  - 视觉变更：深色 Filled/Tonal/Danger 按钮文字改深色、Outline/Ghost 文字色改 accentContent、TextField 底色/边框变体、边框/焦点环 1px 内缩几何（描边环带）、行高 1.2→1.43 倍（正文排版变高）、Aurora/InkLinen/Utility 若干状态色明度调整。Gallery 帧哈希基线随之更新。
- 未验证事项与预留能力：真实窗口人工视觉验收与 Linux/macOS 平台证据推迟至 S5 收敛；§11 预留项未动。
- 下一阶段及前置条件：S2 基础控件（Text/Icon/Container/Button/TextField/Checkbox/Switch/Radio）——勾号替换内方块（IconId::Check）、Radio 空心环+内点、Switch knobOff/knobOn、指示器焦点槽位预留、Button 图标/文字对齐与隔离带；直接消费本阶段 token 与描边命令。

---

## S2 基础控件

- 阶段 / 日期 / 源码提交：S2 / 2026-09-15 / 基线 `fcdc80d`（S1 提交）
- 已完成控件与规格章节：§6.1 Button、§6.2 Text/Icon、§6.3 TextField、§6.4 Checkbox/Switch/Radio、§5.2 关键组合状态、§4.4 指示器槽位、§4.5 图标线宽比例。
- 新增或调整的 token / ResolvedStyle / 公共 API：
  - `CheckboxTokens`：+`indicatorOutline`（Off 轮廓 borderStrong）；indicator→surfaceSunken；markInset 3/4/5；`markRadius` 删除（勾号折线不需要）。
  - `SwitchTokens`：+`trackOutline`/`knobOff`/`knobOn`；`knobInset` token 删除（resolver 按 (trackHeight-knobSize)/2 推导：3/3/4）。
  - 新增 `RadioTokens` + `RadioResolvedStyle`（indicator/indicatorOutline/indicatorChecked/dot/dotRatio 0.45/indicatorSize[3]）；resolver 增加 Radio 专用分支（不再容器回退），全 rebuild 链（baseTheme/applyHighContrast/adaptPlatformTheme/scaleComponentSizes）接入。
  - `CheckboxResolvedStyle`/`SwitchResolvedStyle`/`RadioResolvedStyle` +`slotSize`（指示器/轨道 + focusRingWidth + 1px 隔离带，§4.4；聚焦不改变槽位与标签起点）；`CommonResolvedStyle` +`focusIsolation`（Button 不透明填充聚焦时的 1px 表面隔离带）。
  - `ButtonResolvedStyle` +`iconSize/iconGap/iconStroke`（inlineIconSize 档位、controlGap、线宽 1.5×尺寸/16）。
  - 图标节点线宽按盒尺寸比例缩放（layout 折算）。
- 行为变更（painter/layout/resolver）：
  - Checkbox：Off = surfaceSunken 内部 + borderStrong 描边（空心框）；On = accent 填充 + `IconId::Check` 折线勾号（替换旧内方块）；hover 轮廓→focusRing、pressed 叠加、invalid 轮廓→statusError、disabled 保留勾选可辨认。
  - Radio：空心外环（描边）+ surfaceSunken 环内 + 独立 accent 内点（0.45×外径，环与点之间保留表面空隙）；删除 painter 局部常量（16.0F/8.0F/0.28F 内缩与"两次同色填充"）。
  - Switch：轨道 surfaceSunken + borderStrong 轮廓；knobOff=contentPrimary / knobOn=onAccent；内距按档位推导。
  - Button：图标盒取 inlineIconSize 档位、gap 取档位 gap、线宽按比例；文本+图标内容组居中；可用宽度不足时单行省略（maxLines=1 + Ellipsis，不再是纯裁剪）；不透明填充聚焦时绘制 1px 隔离带。
  - TextField：hover 轮廓增强到 focusRing（表面不变）；ReadOnly 底色 surface；caret 宽 1 logical px（+0.5 偏移对齐像素边界）；单行字段光标超出右缘时内容平移跟随（选区/preedit 共享同一偏移）。
  - 布局测量：Checkbox/Radio/Switch 用槽位宽度；Button 图标计入内容组宽度。
- 源码审查发现、修复与仍存缺口：
  - 发现并修复：radio 像素测试最初以 400×300 布局采样 200×60 缓冲导致越界崩溃（测试缺陷，改为一致视口并加边界断言）；删除 painter 中不再可达的 Radio 兼容分支与未用的 `textWidth` 助手。
  - 仍存缺口：控件标签仍为单行（§6.4 长标签换行未做，需标签宽度约束流经叶子测量，推迟并记录）；Switch knob 100ms 位移动画属 S5；隔离带用 `colors.surface` 近似宿主表面（无法感知父表面，§6.1 允许的部件几何近似）。
- 状态/主题/尺寸样本及截图链接：Gallery headless 全路由冒烟通过（frame0 `5c5e67bd…`）；新增命令/像素级样本（勾号 DrawIcon、Radio 空心像素断言、Switch 轮廓描边、Button 图标档位）。
- 验证命令、测试结果、真实平台/后端/字体：CPU Debug 438/438；Skia Release 450/450；GPU Release 450/450；Windows 11 / VS 2026；headless 占位字体（真实字体路径由 system_font_tests 与窗口路径覆盖）。
- 兼容性或视觉基准变更：Checkbox/Radio/Switch 几何与外观显著变化（槽位预留使行宽 +3~5px）；Radio 深浅色从前景色改 accent/borderStrong；Button 图标盒 14→16、线宽 1.4→1.5；TextField hover/ReadOnly 表面变化；Gallery 帧哈希基线更新。
- 未验证事项与预留能力：真实窗口人工视觉验收推迟至 S5；Checkbox 三态、Switch knob 动画、控件标签换行未做（见缺口）。
- 下一阶段及前置条件：S3 选择、导航与滚动——Slider/ProgressBar 专用 token 与端点预留、Dropdown 值行与字段同源、Tabs 上下文样式、ScrollView/ListView/VirtualList 行规格与 Scrollbar token 消费。

---

## S3 选择、导航与滚动

- 阶段 / 日期 / 源码提交：S3 / 2026-09-15 / 基线 `8945cfa`（S2 提交）
- 已完成控件与规格章节：§6.5 Slider、§6.6 ProgressBar、§6.7 Dropdown（值行 + 浮层菜单）、§6.8 Tabs、§7.2 ScrollView/ListView/VirtualList 与 Scrollbar。
- 新增或调整的 token / ResolvedStyle / 公共 API：
  - 新增 `SliderTokens` / `ProgressBarTokens` / `TabsTokens`；`ScrollbarTokens` 重定义（rest=borderStrong 实色、+thumbWidth 4/minLength 24/inset 4；hovered/dragged 占位为预留交互）；全部接入 baseTheme/HC/adaptPlatformTheme/scaleComponentSizes 重建链。
  - 新增 `SliderResolvedStyle`（trackHeight 4、thumbDiameter 16/18/22、thumbBorderWidth 2、trackInset=r+focusRingWidth+1）、`ProgressBarResolvedStyle`（trackHeight 4/6/8）、`TabsResolvedStyle`（indicator/separator/两态文字色）；Dropdown 值行复用 `ButtonResolvedStyle`（chrome=字段同源 + Chevron 槽位字段）。
  - resolver 新增 Slider/ProgressBar/Tabs/Dropdown 专用分支（§2.4 缺口 2 消除：Radio(S2)/Slider/ProgressBar/Dropdown/Tabs 全部脱离容器回退）。
  - `RenderNode` +`scrollbarColor/scrollbarThumbWidth/scrollbarMinLength`（布局折叠，painter 不再乘 alpha）。
- 行为变更：
  - Slider：端点恒定预留 r+f（值 0/100 时 Thumb 与焦点环均在节点内；可用宽不足时轨道长度 0 并居中）；未完成 borderStrong/完成 accent；Thumb=surfaceElevated+accent 轮廓（hover/focused 轮廓→focusRing、pressed 叠加、disabled 全链禁用色）；**指针→值与绘制共用 trackInset 区间**（interaction.cpp 同步改）。
  - ProgressBar：高度分档 4/6/8、圆角=高一半、填充夹取在轨道内（小于圆角直径时圆角同步收缩）；无交互状态。
  - Dropdown 值行：surfaceSunken + borderStrong + 同高/圆角/padding/最小宽；尾随 Chevron 取 inlineIconSize 档位、单独预留；展开方向由应用经 icon 声明（Gallery 已接 ChevronUp 翻转）。
  - Dropdown 菜单：L2 阴影 + 1px borderDefault + radius 8；窗口安全边距 8、锚点间隔 4、内部 padding 4；选项全部 Ghost（hover 状态面/键盘焦点环表达活动项），当前值 Tonal + 尾随 Check；菜单高度 min(320, 可用)，超长进入 ScrollView 且键盘高亮滚入可见区。
  - Tabs：布局期上下文解析（子按钮改写为 Ghost + 选中 accentContent/未选 contentSecondary + 水平 padding 12 + 行间 gap 4）；painter 绘制底部分隔线与选中项 2px accent 指示条；Gallery 不再需要手写页签颜色。
  - Scrollbar：Thumb 实色 borderStrong、可视宽 4、最小长 24（短视口夹取不越界）、上下 inset 4、圆角=可视宽一半。
- 源码审查发现、修复与仍存缺口：
  - 发现并修复：ProgressBar 测试最初假设 tight 约束下显式 120 宽生效，实际按盒模型被 tight 夹取到 200（既有行为，测试期望修正）；Slider Thumb 描边宽最初硬编码 2，改为携带 `thumbBorderWidth`。
  - 仍存缺口：Scrollbar hovered/dragged/auto-hide 为 §11 预留（常显 rest）；列表双行 56 等行规格属应用组合（Gallery 演示）；Dropdown 菜单滚动偏移按统一行高推导（行高可变时需按实测 extent 修正——当前选项行等高，记录为已知近似）。
- 状态/主题/尺寸样本及截图链接：Gallery headless 全路由冒烟通过（下拉/页签/滑杆联动路径全绿）；新增端点/区间映射、Tier 高度、分隔线+指示条、值行 chrome、长菜单滚动/边界共 6 个测试。
- 验证命令、测试结果、真实平台/后端/字体：CPU Debug 444/444；Skia Release 456/456；GPU Release 456/456；Windows 11 / VS 2026。
- 兼容性或视觉基准变更：Slider/ProgressBar/Tabs/Dropdown/滚动条外观全部按 §6.5–§6.8/§7.2 重制（旧 painter 局部常量 8.0/0.35/0.25/0.75/1.6/24/1.4/16/0.9/6.0/8.0 全部移除）；Gallery 帧哈希基线更新。
- 未验证事项与预留能力：真实窗口人工验收推迟至 S5；Scrollbar 交互态、Radio 组、Dropdown 禁用项/分组/搜索为 §11 预留。
- 下一阶段及前置条件：S4 浮层与组合——Tooltip（caption/surfaceElevated/边界避让）、Image 占位（surfaceSunken + IconId::Image）、Form/Dialog/Navigator 外观与生命周期、ThemeScope/FocusScope 核对。

---

## S4 浮层与组合

- 阶段 / 日期 / 源码提交：S4 / 2026-09-16 / 基线 `ee5a16b`（S3 提交）
- 已完成控件与规格章节：§6.9 Tooltip、§6.10 Image、§8.1 Form（核对）、§8.2 Dialog、§8.3 Navigator、§7.3 ThemeScope/FocusScope（浮层主题继承）。
- 新增或调整的 token / ResolvedStyle / 公共 API：
  - 新增 `TooltipTokens`（surfaceElevated/borderDefault/contentPrimary、padding 8/6、maxWidth 280、L2）+ resolver `resolveTooltip`；Tooltip 布局改为换行测量（maxWidth 280 夹取）+ 样式 padding，painter 按节点宽换行绘制（移除 6.0F 局部常量）。
  - resolver `resolveImage`：占位 chrome = surfaceSunken + 1px borderDefault + contentSecondary 前景；painter 占位重写为“表面 + 描边轮廓 + 居中 `IconId::Image`（最大 24px）”，移除 (39,39,42)/(82,82,91)/h×0.04/叉臂 0.25 全部局部常量；就绪位图覆盖盒子（默认拉伸契约不变）。
  - `IconId::Image` 目录项追加在枚举尾（既有 ID 值不变）。
  - `MotionTokens.navigatorTransitionMs` 350→200（§9.1）。
  - `DropdownController::open` 增加可选 `anchorTheme`（§7.3 浮层继承触发器主题；ThemeScope 内打开的菜单不回落窗口根主题；刷新沿用打开时的拷贝，主题切换期间保持打开的菜单需应用重开——已注释说明）。
  - `makeDialog`（§8.2）：+1px borderDefault 轮廓、L3 阴影（tokens.elevation）、宽度 240–420 且窗口可用宽（边距 16）优先、卡片改经 StackAlignment::Center 布局期真实居中（旧实现按估算尺寸定位但从未给卡片设置尺寸——发现的既有缺陷）、整体 padding 24 由 helper 统一施加；gallery/settings 对话框内容改为“标题→正文 12、正文→操作区 24”结构并去掉自带边距。
- 源码审查发现、修复与仍存缺口：
  - **发现并修复通用缺陷**：`TextLayout` 贪心换行在断行点回退后未把 [lineStart, i) 的 cluster 宽度计入累积，行宽可超过 maxWidth（此前无断言覆盖；Tooltip 的 280 上限测试暴露）。修复后行宽 ≤ maxWidth，全量测试无回归。
  - 发现并修复：makeDialog 旧实现的居中错位（见上）；Tooltip 测量的 padding 双重叠加风险（改为 layoutLeaf 统一追加）。
  - 仍存缺口：Dialog 超长正文进入内部滚动区未实现（当前受根约束压缩，记录）；Tooltip 键盘焦点触发接线未新增（hover 路径已有，§6.9 允许声明式记录）；Form 的 18px SupportingText 预留行属应用组合（gallery 表单已有错误文案行）。
- 状态/主题/尺寸样本及截图链接：Gallery headless 全路由冒烟通过；新增 Tooltip 换行/表面、Dialog 卡片 chrome/居中/边距、浮层主题继承、Image 占位（更新既有测试为命令级断言）共 4 个测试。
- 验证命令、测试结果、真实平台/后端/字体：CPU Debug 447/447；Skia Release 459/459；GPU Release 459/459；Windows 11 / VS 2026。
- 兼容性或视觉基准变更：Tooltip/Image/Dialog 外观重制（§6.9/§6.10/§8.2）；换行行宽修复可能使既有多行文本换行点略提前（正确性修复）；Navigator 过渡 350→200；Gallery 帧哈希基线更新。
- 未验证事项与预留能力：真实窗口人工验收推迟至 S5；Image fit/圆角裁剪/加载失败区分（§11 预留）；Tooltip reduceAnimation 已有归零策略。
- 下一阶段及前置条件：S5 动效与质量收敛——§9.1 动效表核对（状态色过渡/Switch knob 位移/Dropdown 淡入/中断与 reduceAnimation）、Gallery 状态矩阵样本、完整矩阵与门槛核对。

---

## S5 动效与质量收敛

- 阶段 / 日期 / 源码提交：S5 / 2026-09-16 / 基线 `d653ac1`（S4 提交）
- 已完成规格章节：§9.1 动效表核对、§10.1/§10.2 Gallery 样本接入、§13 门槛核对与证据归档。
- 动效表逐行核对结果（§9.1）：
  - Hover/Pressed/Checked 颜色 100ms EaseOut：`motionTransitions` opt-in + tick 时钟（既有）；中间帧/中断/reduceAnimation 归零由既有 motion 测试覆盖。
  - 键盘焦点出现 0ms：**新增测试锁定**（`focus_ring_appears_full_width_on_first_blend_frame`——环宽度不参与状态色插值，过渡中途帧即完整环宽）。
  - 主题/密度切换、disabled、ProgressBar 值、Slider 拖动 0ms：**新增测试锁定**（`theme_switch_converges_immediately_with_transitions_on`——状态过渡仅由交互快照触发，主题切换首帧即终值）；Slider/进度即时性由交互测试覆盖。
  - Tooltip 400/120、reduceAnimation 立即显示：既有测试覆盖。
  - Dialog 200ms / Navigator Fade 200ms（S4 调整）/ caret 530：既有测试覆盖。
  - Switch knob 位移 100ms：已接入状态过渡采样，并由视觉回归测试锁定中间位置与 reduceAnimation 终值。
  - Dropdown 打开/关闭 120ms alpha：**未实现**（§9.1 标注"可选"，当前即时开合）。
- Gallery 样本（§10.1/§10.2）：
  - Buttons 页新增"State matrix (forced previews)"卡：五变体 × Normal/Hover/Pressed/Focused/Focused+Pressed/Disabled；强制状态经 `resolveStyle` 在合成交互快照下解析后写入 StyleOverrides（预览单元格 disabled 保持快照稳定、不触发业务回调；Focused 列的环以边框槽近似）。
  - 新增中英文长标签窄按钮样本（单行省略路径）。
  - Sizes 三档、Enabled/Disabled/Selected/图标行、主题页四方向 × 深浅 × 高对比 × 强调色、Inputs 表单/下拉/页签/滑杆交互样本：既有。
- 新增或调整的 token / ResolvedStyle / 公共 API：无框架 API 变更（本阶段为核对、测试与 Gallery 样本）。
- 源码审查发现、修复与仍存缺口：
  - 发现并修复：Gallery 长标签按钮被 Stretch 列拉宽（包 Row 使显式 200 宽生效）。
  - 仍存缺口（本轮明确单列）：Dropdown 打开淡入未做（§9.1 标注可选）；Scrollbar hovered/dragged/auto-hide（§11 预留）。Switch knob 位移、Dialog 正文滚动、控件标签多行换行与 Tooltip 键盘焦点触发已在当前实现中补齐。
- 验证命令、测试结果、真实平台/后端/字体：
  - CPU Debug `ctest`：450/450（历史 S5 快照统计；当前工作区验证结果以本轮命令输出为准）。
  - Skia 光栅 Release：462/462；GPU（Ganesh+GL）Release：462/462（历史 S5 快照统计）。
  - 当前工作区 CPU Debug 全量回归：443/472；剩余 29 项失败，多个与已有 CPU 双缓冲/字体光栅改动相关；本轮视觉修复定向测试均通过。
  - 真实窗口冒烟：`lumen-gallery --max-frames 20`（Windows 系统字体 256 faces、gdi+stb 光栅，退出码 0）。
  - 可重复样本：`--headless --dump-frame` 导出 1024×768 RGBA 成功；帧哈希 frame0=`5e93f6ff…`。
  - 平台：Windows 11 / VS 2026 为主机证据；Linux/macOS 由 CI 覆盖（本阶段未在本地运行，明确为未验证项）。
- 人工视觉与交互清单（§13.3）：自动化/命令级证据覆盖的条目如上；**真实桌面人工逐条目验收未执行**（需要人工在真实窗口核对三档尺寸、200% 缩放、非整数 DPI、IME 等观感项）——单列为未完成事项，不冒充通过。
- 性能对照：本阶段未引入每帧分配或新文本整形路径（状态过渡复用既有机制）；未跑基准对照（无既有基线可比场景变化，按 §9.3 记录为未执行）。
- 兼容性或视觉基准变更：Gallery Buttons 页新增矩阵卡（帧哈希更新）；无框架行为变更。
- 完成边界：S0–S5 全部阶段已按 §12 顺序执行并逐阶段 review+提交；§11 预留项与本记录"仍存缺口"单列，不计入完成范围。真实桌面人工验收与 Linux/macOS 主机证据为后续补充项。


---

## S1–S5 Review 修复（2026-09-16）

- 源码基线：`a7b6e71`；本轮处理 S1–S5 review 的 12 项问题，本节随修复一并提交。
- 修复内容：
  1. TextField 的显示文本、密码掩码、preedit、换行、垂直居中和单行水平偏移统一到 `core/text_field.h`；painter、点击定位及 IME 查询共用几何。IME 返回实际光标行的矩形。空字段点击不再定位到 placeholder 的字符索引，并移除实心光标的半像素偏移，恢复 CPU 起始位置光标闪烁。
  2. Dropdown 将保存的触发器主题通过真正的 ThemeScope 包裹浮层，选项文本、尺寸与菜单阴影均继承该主题；键盘刷新继续使用同一作用域。
  3. Tooltip 按包含 padding 的最终宽度测量换行，兼顾父约束和显式宽度；默认最大外宽为 280。
  4. Checkbox / Radio / Switch 的固有高度消费 resolved minHeight，恢复密度对应的点击高度。
  5. 三类选择控件在部件两侧各预留焦点环与 1 px 间隔；环画在固定部件之外，聚焦不再缩小指示器、轨道、勾号或移动滑块；部件轮廓消费主题边框宽度。
  6. Icon 描边使用实际图标盒计算；Image 占位图标和 Checkbox 勾号不再随宿主宽度变粗。
  7. 高对比及平台颜色派生在 fontScale 之前完成；补齐 Slider Thumb、选择控件标签 gap、Tabs/Tooltip/Dialog 相关尺寸与间距的一次缩放。
  8. 系统强调色同步派生 Slider、ProgressBar 和 Tabs 的颜色。
  9. Tabs 的默认前景保留 disabled 状态，显式 foreground（含透明/黑色）及 padding（含零值）优先。
  10. scrollbarColor / scrollbarThumbWidth / scrollbarMinLength 和图标线宽纳入节点 damage 比较。
  11. Scrollbar 的专用颜色保留 token 原始 alpha，同时乘上节点与祖先转场 alpha。
  12. Gallery 状态矩阵改为自适应 Grid，每个状态标题随按钮一起换行。
- 自动化：新增 `tests/visual_regression_tests.cpp` 的 10 个回归用例，覆盖字段点击/IME/密码/多行、焦点几何与三档密度、主题组合、Tooltip 换行、图标描边、Tabs 覆盖、局部主题浮层、滚动条局部重绘像素一致性与祖先透明度，以及 600/800/1024 宽度 × fontScale 1/1.5/2 的矩阵边界。旧测试同步修正绑定数据、实际光标几何及滚入视口后点击的前置条件。
- 验证环境：Windows / VS 2026；使用 `build/review-fix/source` 隔离快照，包含本轮修复及基线 CPU renderer，未混入工作区另一个任务正在修改的 `cpu_renderer.h/.cpp`。
- 验证结果：
  - CPU Debug：`ctest --test-dir build/review-fix/out --output-on-failure -C Debug --parallel 6`，**460/460**；[测试日志](../build/review-fix/ctest.log)。
  - Skia + GPU Release：`ctest --test-dir build/review-fix/out-gpu --output-on-failure -C Release --parallel 6`，**472/472**；[测试日志](../build/review-fix/ctest-gpu.log)。GPU 提交测试实际执行 18 条断言，无跳过。
  - Gallery 全路由 `--headless` 冒烟通过；Windows `--max-frames 20 --diagnostics` 窗口运行退出码 0，CPU 后端、系统字体 256 faces、gdi+stb 光栅；[窗口日志](../build/review-fix/gallery-window.log)。
  - 使用同一 Windows 系统字体离屏导出并核对状态矩阵：[600px / fontScale 1](../build/review-fix/gallery-600-fs1.png)、[600px / fontScale 2](../build/review-fix/gallery-600-fs2.png)。核对范围为矩阵区域；Gallery 顶部/底部固定栏在 200% 下的文本裁剪不属于本轮 12 项。
- 未验证事项：Linux/macOS 本机运行、完整人工视觉清单及性能对照未执行。上述窗口冒烟与矩阵截图不替代完整人工验收；S5 已记录的动画、Dialog 长正文滚动及 §11 预留能力保持原完成边界。

---

## S6 全绘制抗锯齿与双缓冲（2026-09-16）

> 目标：CPU 后端所有几何绘制抗锯齿（圆角矩形填充/描边、图标线条）；绘制效率优化采用双缓冲（交换替代每帧全帧拷贝）。三层缓冲不适用：软件光栅 + 同步呈现没有异步呈现队列可消费第三张缓冲，双缓冲已消除逐帧拷贝。

- 阶段 / 日期 / 源码提交：S6 / 2026-09-16 / 基线 `e0dde59` + 工作区进行中的视觉几何/固定样本改造（见上一节）
- 抗锯齿实现（`src/render/cpu_renderer.cpp`）：
  - 圆角矩形填充/描边改用**设备像素空间的圆角矩形 SDF**（象限取半径的 rounded-box 距离场）：覆盖率 = `clamp(0.5 - d, 0, 1)`，经既有 `blendCoveragePixel` 混合（与字形 AA 同一通道）。描边 = 外形覆盖 − 内形覆盖（饱和相减）。
  - 整数对齐几何保持锐利（边界恰在像素网格时不引入模糊）；分数边界/圆角弧线产生 1px 过渡带——正是 1/1.25/1.5/2 DPI 与 4/6/8 小圆角的主要观感问题来源。
  - 性能防护：内部区域（边界内缩 1px 的盒+角测试）直通整像素填充，只有边界带付出距离场成本；大面积背景近似零开销。
  - 图标线条：删除"数值步进 + 方形笔刷多遍"旧光栅，改为逐像素点到线段距离（覆盖率 max 累积后单次混合；端点距离自然形成圆帽/圆角，与 Skia Round_Cap/Join 一致，跨后端观感更接近）。
  - 文本：系统字体路径本就有 coverage AA；5x7 占位字形保持确定性位图（headless 哈希稳定），不计入本轮 AA 范围。
- 双缓冲（`CpuRenderer`）：
  - `endFrame` 由全帧拷贝（`previous_ = buffer_`）改为 **back/front 指针交换**（O(1)）；`pixels()` 返回最近完成帧（present 读到稳定帧，不再与绘制竞争），首帧完成前的帧中读取回退绘制缓冲保持旧语义。
  - Preserve 帧零拷贝：beginFrame 先交换使绘制缓冲携带上一完成帧；damage 场景只把损坏区带清成底色（旧的"四周拷贝整帧"删除）。
  - 顺带修复（跨后端契约）：`Renderer::submit` 默认适配器此前丢弃 `FrameInfo.deviceScale`（仅 CpuRenderer 原生 submit 处理）——新增基类虚 `setDeviceScale` 并在默认适配器注入；Skia 光栅在 submit 路径下各 DPI 现在与 CPU 同尺度（`skia_and_cpu_strokes_keep_transparent_interiors_at_fractional_dpi` 由红转绿）。
- 测试：
  - 新增 `[render][aa]` 3 例：分数边界混合带/角弧中间值/整数对齐锐利；描边与图标线条的部分覆盖；双缓冲完成帧隔离与多帧稳定。
  - `visual_scope_theme_changes_snap_motion_and_refresh_tooltip` 的气泡采样点移到内部（原采样点位于 AA 圆角弧线上，恰为混合值——不是回归）。
  - 基线重冻结：Gallery headless frame0=`d352b4fd72889f7a`…；基准 frame_hash 变化且两次运行一致（AA 确定性）。
- 性能对照（Windows 11 / VS 2026 / Debug / `lumen-scene-bench --frames 120 --warmup 10`，同机同场景）：
  - paint p50：140.6ms → **96.7ms（-31%）**；mean 149.4ms → 110.4ms；p95 223.7ms → 217.6ms。双缓冲消除的逐帧拷贝显著超过 AA 距离场新增成本。
  - 两次 `--frames 60` 运行 frame_hash 一致（`2982e8a3d392b612`）。
- 验证：CPU Debug 476/476；Skia Release 490/490；GPU Release 490/490；Gallery `--headless` 全路由与 `--max-frames` 窗口（系统字体）冒烟通过；固定样本导出（600×700@1.25DPI → 750×875，hash 确定性）通过。
- 未验证事项：Linux/macOS 本机运行（CI 覆盖）；人工观感验收（圆角平滑度需真人确认，命令级证据为角弧中间值像素）；占位字形 AA 未做（保持 headless 确定性）。
