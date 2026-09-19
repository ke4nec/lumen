# Lumen GUI 控件视觉设计与实施规范

> 状态：待实施的视觉规范；本文档完成不代表控件改造或视觉验收完成。
>
> 更新日期：2026-09-15。源码核对基线：`d57625c`（`fix(gui): 对齐 Core Dark 界面并修复系统字体渲染`）。后续实现前须复核新增变更。
>
> 适用范围：Lumen C++20 自绘桌面 GUI，Windows / Linux / macOS；覆盖当前 23 种 `WidgetType` 及现有组合能力。
>
> 本文规定控件应呈现什么效果、由哪层实现以及如何验收。§2 是源码现状；§3–§11 是目标设计；§12–§14 是实施与交付要求。标为“预留”的能力不属于本轮完成条件。

## 1. 目标、依据与边界

### 1.1 本轮目标

以 **Lumen 自有跨平台风格、Core Dark 默认方向**为基础，让现有控件具有清晰的轮廓、稳定的排版、可辨认的状态和克制的动效。默认提供完整的深色与浅色体验；主题、密度、字体缩放与无障碍设置可以组合使用。

实现应沿用现有 `Theme → resolveStyle → ResolvedStyle → Layout / RenderNode → Painter → RenderCommandList`，补齐未接入的控件和部件。相同部件的测量、绘制、命中、焦点、裁剪和 damage 必须使用同一份几何结果。

本轮交付包括：框架控件改造、Gallery 状态样本、回归测试、真实桌面截图与实施记录。不能只在 Gallery 手动覆盖颜色后宣称框架控件已经完成。

### 1.2 文档职责与冲突处理

| 依据 | 职责 |
| --- | --- |
| [AGENTS.md](../AGENTS.md) | 仓库规则、平台范围、代码与测试要求 |
| [自用路线图](lumen-self-use-roadmap.md) | 项目范围、里程碑、既有完成记录与限制 |
| [视觉系统架构设计](lumen-visual-system-design.md) | 已采用的 token 三层模型、布局前解析、模块与 API 边界；其中旧实现描述需结合文末实施记录阅读 |
| 本文 | 当前控件的具体外观、状态合成、部件尺寸、实施顺序和验收标准 |
| [Gallery 设计参考](../design/gallery.html) | 四个视觉方向的参考，Core Dark 为默认；HTML 中的微缩预览、浏览器字体和特效不直接充当原生控件规格 |
| [构建与验证命令](build-commands.md)、[支持矩阵](support-matrix.md) | 验证入口、平台与后端覆盖的事实依据 |

源码决定“现在有什么”，本文决定“本轮要达到什么”。本文新增的颜色、部件 token 与样式字段都是待实现设计，不能当作现有 API 调用。若具体视觉数值与旧设计不同，以本文的目标值为准，并在实现该项时同步相关文档、示例和测试；平台范围与架构边界不由本文扩张。

### 1.3 实施边界

- 平台为三桌面，包含桌面触屏、DPI、字体缩放和窄窗口；Android/iOS、M9 不在范围。
- 后端为 `CpuRenderer`、可选 Skia 光栅及 Skia Ganesh GPU。不新增 Impeller 或其他渲染后端。
- 保留 23 种 `WidgetType`、现有 builder、绑定值、事件与焦点契约。已完成的一次视觉 API 迁移不重做；确需公开 API 调整时，先记录影响与迁移方式，再连同调用点和测试一起修改。
- 不新增 TreeView、DataGrid、Toast、独立 Menu / ContextMenu 或多窗口系统。`ListItem`、Dialog、Scrollbar 是现有节点的组合或部件，不额外计入枚举。
- 不把原生窗口标题栏替换、玻璃模糊、渐变、弹簧动画列为本轮要求。现有 Gallery 壳层继续保留，页面级布局变化服务于展示和验收控件。
- 本次文档修订只产出规范；后续收到实现任务后才执行 §12 的代码改造。

## 2. 当前源码基线与明确缺口

### 2.1 可以直接复用的能力

| 区域 | 已有能力与源码入口 | 本轮处理 |
| --- | --- | --- |
| 声明与布局 | [widget.h](../include/lumen/core/widget.h)、[layout.cpp](../src/layout/layout.cpp)：值类型 Widget、稳定 identity、布局前解析、RenderNode | 扩展部件样式和共用几何，不另建节点树 |
| 主题 | [tokens.h](../include/lumen/style/tokens.h)、[theme.h](../include/lumen/style/theme.h)、[theme.cpp](../src/style/theme.cpp)：primitive / semantic / component、四方向、light / dark、密度与无障碍派生 | 补 token 覆盖和可读性，保留派生入口 |
| 状态与解析 | [state.h](../include/lumen/style/state.h)、[resolver.cpp](../src/style/resolver.cpp)：交互快照，Button / TextField / Checkbox / Switch 专用解析 | 为其余交互控件补齐状态与部件解析 |
| 最终样式 | [style.h](../include/lumen/core/style.h)：公共样式、四种专用样式、颜色插值 | 保持纯值类型，按实际部件增加必要字段 |
| 绘制 | [painter.cpp](../src/render/painter.cpp)：统一绘制路径、图标、阴影、图片、焦点与滚动条 | 移除视觉常量，修正轮廓和裁剪 |
| 动效 | [tween.h](../include/lumen/core/tween.h)、[app_shell.h](../include/lumen/app/app_shell.h)：Tween、tick 时钟、节点 alpha、可选状态色过渡、Tooltip 延迟 | 复用调度与生命周期；状态色过渡当前为 `motionTransitions` 显式开启 |
| 组合能力 | [form.h](../include/lumen/widgets/form.h)、[navigator.h](../include/lumen/widgets/navigator.h)、[dropdown.h](../include/lumen/widgets/dropdown.h) | 统一表单、Dialog、路由与下拉浮层的外观 |
| Gallery | [gallery_app.h](../examples/gallery/gallery_app.h)、[main.cpp](../examples/gallery/main.cpp)：Overview、Buttons、Inputs、Layout、Lists、Feedback、Theme，headless 与帧导出 | 扩展既有页面，加入状态矩阵与确定性样本 |

### 2.2 内容与交互控件：13 种

下表“目标”均为后续实施要求，不是现有能力声明。

| WidgetType | 源码现状 | 本轮视觉目标 |
| --- | --- | --- |
| `Text` | 角色字体、测量、换行/裁剪与文本绘制 | 统一字号/行高/颜色层级，中英文与长文本可读 |
| `Button` | Filled / Tonal / Outline / Ghost / Danger；焦点、禁用、状态色；单个图标通道 | 五变体完整状态，真实透明轮廓，文本和图标对齐 |
| `TextField` | placeholder、光标/选区/IME、密码/只读/多行、invalid | 稳定边框、标签/辅助说明组合，输入视觉不偏移 |
| `Checkbox` | 二态、专用样式；当前 checked 标记绘制为内方块 | 空心框与明确的勾号，焦点和禁用仍能识别勾选 |
| `Switch` | 二态、轨道与滑块 token | 固定轨道，滑块位置/颜色明确，焦点不挤压滑块 |
| `Radio` | 布尔绑定；组内互斥由应用维护；走通用 resolver，圆形绘制有局部常量 | 空心外环与独立内点，专用状态，保留组行为边界 |
| `Slider` | 0–100 整数绑定，拖动和键盘改值；轨道/滑块几何在 painter 推导 | 细轨道、独立 Thumb、端点不越界、焦点可见 |
| `ProgressBar` | 0–100 确定进度，无交互 | 细轨道、准确填充、百分比与状态说明组合 |
| `Dropdown` | 值行 + `DropdownController` overlay、键盘导航、关闭/恢复焦点 | 与字段同高，箭头对齐，选中项与导航高亮分开 |
| `Tabs` | 标签 Button 子节点；selected 与内容切换由应用管理 | 统一标签行、持续选中指示、独立键盘焦点 |
| `Tooltip` | 常驻节点，AppShell 提供 hover 延迟显隐与 alpha | 紧凑提示表面、边界避让、固定延迟与减少动画策略 |
| `Icon` | `IconId` 折线目录、主题尺寸/线宽、装饰语义 | 一套描边风格，与标签光学居中 |
| `Image` | ImageId 就绪绘制；未就绪占位，当前有固定深色与几何常量 | 跟随主题的稳定占位，明确当前拉伸显示契约 |

### 2.3 布局、容器与滚动组件：10 种

| WidgetType | 接入策略 |
| --- | --- |
| `Container` | 默认透明；显式使用 panel / inset / elevated 表面，padding/边框/圆角/阴影来源一致 |
| `Row` | 无默认装饰；统一间距，支持同排文本基线和控件对齐 |
| `Column` | 无默认装饰；统一字段、分组和段落节奏 |
| `Stack` | 无默认装饰；保证覆盖顺序、命中顺序与实际裁剪相符 |
| `ScrollView` | 维持垂直滚动，裁剪内容，附属滚动条使用专用 token |
| `ListView` | 维持非虚拟纵向列表；行样式由其子节点表达，容器不获得整块选中态 |
| `VirtualList` | 可见项与缓存区物化；以稳定 key 管理行状态，复用不串焦点/选中/过渡 |
| `Grid` | 维持固定列/最小列宽布局；由列间距与最小列宽驱动重排，不自动赋予子项选择行为 |
| `FocusScope` | 无表面与边框；验证焦点边界、恢复和实际控件焦点标识 |
| `ThemeScope` | 无表面；验证子树主题覆盖、退出恢复、字体度量及浮层主题来源 |

### 2.4 不得误报为已实现的能力

1. `WidgetState` 目前只有 hovered、pressed、focused、disabled、checked、invalid、selected。`readOnly` 是 TextField 属性；没有统一的 loading、indeterminate、warning、keyboardFocused 状态。
2. `resolveStyleImpl` 对 Radio、Slider、ProgressBar、Dropdown、Tabs、Tooltip 等目前回落到通用容器解析。存在绘制分支不等于已经覆盖交互状态。
3. `ProgressBar` 没有不确定进度模式；`Radio` 没有框架级组管理；`DropdownController::Option` 当前只有 value / label，没有禁用项或分组选项。
4. `FormController` 保存错误字符串，不是 Warning / Success 多级校验模型。辅助说明由应用组合，不能声称新增一个颜色就完成了校验模型扩展。
5. `withScrollbar` 控制视口的附属绘制；当前没有专用 Thumb 拖动/自动隐藏状态机。rest / hovered / dragged / minLength token 已声明，尚需核对消费链，不能据此宣称这些交互已实现。
6. 当前焦点环在 painter 中内嵌绘制；不要依据旧头文件中的“外扩”注释直接增加越界绘制。阴影另有绘制范围处理。
7. 当前 Icon 通道不等于任意前后插槽；ImageId 为 0 不区分加载与失败；`TextStyle.lineHeight` 是倍数，不能把绝对像素行高直接赋给它。
8. 当前 CPU 阴影为扁平降级，Skia 使用模糊；headless 占位字体输出不能证明真实系统字体、IME 或跨后端视觉一致。

## 3. 视觉方向与整体秩序

### 3.1 默认视觉语言

- **中性表面 + 蓝色交互强调**：页面、面板、输入区分别成层；蓝色用于主操作、选中和焦点，绿/橙/红用于成功/警告/错误。
- **轮廓轻、操作明确**：常规按钮和字段不加阴影；浮层依靠表面、边框及适度阴影表达层级。
- **圆角有等级**：控件 4/6/8，卡片 8，Dialog 12；只有 Switch、Slider/Progress 轨道及圆形指示器使用 pill/circle。
- **文字先于装饰**：14 px 正文/标签、12 px 辅助说明；不把设计参考里的 10 px 微缩控件文字作为真实控件默认值。
- **状态不改变布局**：hover、pressed、focused、checked 不移动标签、不改变外框尺寸、不使相邻控件跳动。

Core Dark 是方向名称，可与 light/dark 模式独立组合。Ink Linen 保留暖中性色与紫色强调；Aurora Signal 保留蓝青方向的纯色近似；Utility Contrast 保留紧凑圆角和 2 px 边框。四方向共用语义、部件、状态和测试，不分别维护四套 painter。

### 3.2 页面与控件的界线

Gallery 的 hero、指标数字、侧栏和代码面板属于应用排版；不能修改 `typography.body` 来迁就首页。普通布局节点保持透明，卡片由显式组合产生。

同一操作区最多一个主要 Filled 操作，其余按 Tonal / Outline / Ghost 分配；破坏操作使用 Danger。选中项不借用 Danger。一个选项被 hover 或键盘导航高亮时，既有选中标记仍然保留。

## 4. Design Tokens：具体目标值

### 4.1 命名与派生契约

继续使用 `PrimitivePalette → ColorScheme / Metrics / Typography → component tokens`。以下使用 `colors.*` 等设计路径：已有同名字段沿用；标为“新增”的字段需在实现阶段落到 C++ Theme/ResolvedStyle，并补派生、相等比较和测试。

只允许主题工厂存放原始色值。控件、Gallery 示例和 painter 通过语义或组件 token 取得颜色与尺寸。例外是几何数学常数、透明色以及图标目录中的规范化坐标。

### 4.2 Core Dark 方向的深浅色目标

本表是**目标调色板**，不是当前 `Theme::dark/light()` 的逐项抄录。沿用当前 Core Dark 的页面、面板、主文字和蓝色识别；调整浅色表面、可读性及状态角色。新增字段的两个模式都必须有定义。

| Token | Dark | Light | 用途 / 状态 |
| --- | --- | --- | --- |
| `colors.pageBackground` | `#18181B` | `#F4F4F5` | 页面底色 |
| `colors.surface` | `#27272E` | `#FFFFFF` | 卡片、普通面板 |
| `colors.surfaceElevated` | `#34343F` | `#FFFFFF` | Dialog、菜单、Tooltip |
| `colors.surfaceSunken`（新增） | `#2E2E36` | `#F4F4F5` | 输入区、内嵌区域 |
| `colors.contentPrimary` | `#F1F1F4` | `#18181B` | 正文、输入值 |
| `colors.contentSecondary` | `#A1A1AA` | `#52525B` | 辅助说明、placeholder |
| `colors.accent` | `#568CF0` | `#3460BE` | 强调填充、已选指示 |
| `colors.onAccent` | `#000000` | `#FFFFFF` | 强调填充上的文字/勾号 |
| `colors.accentContent`（新增） | `#A8C5FA` | `#234A91` | 透明按钮、链接与强调文字 |
| `colors.accentContainer` | `#2E3C60` | `#E0EAFF` | Tonal、选中行 |
| `colors.onAccentContainer` | `#E0EAFF` | `#234A91` | Tonal / 选中行文字 |
| `colors.borderDefault` | `#3B3B45` | `#D4D4D8` | 卡片/分隔线等装饰边界 |
| `colors.borderStrong` | `#8C8C98` | `#71717A` | 字段、Outline、未选框等必要轮廓 |
| `colors.focusRing` | `#96B9FA` | `#234A91` | 焦点指示，必要时加表面对比隔离 |
| `colors.selectionBackground` | `#568CF0` / alpha 130 | `#3460BE` / alpha 64 | 文本选区；须在实际底色上合成验算 |
| `colors.statusError` | `#E05A60` | `#AA343A` | 错误边框、Danger 填充 |
| `colors.onError`（新增） | `#000000` | `#FFFFFF` | Danger 文字，不借用品牌文字色 |
| `colors.errorContent`（新增） | `#F0969A` | `#AA343A` | 错误文案/图标 |
| `colors.statusSuccess` | `#73C991` | `#256F46` | 成功文案/图标 |
| `colors.statusWarning` | `#E2B566` | `#8A5700` | 警告文案/图标 |
| `colors.disabledBackground` | `#2E2E36` | `#E4E4E7` | 禁用实体控件底色 |
| `colors.disabledContent` | `#8C8C98` | `#71717A` | 禁用文字，使用实色避免多次降透明 |
| `colors.scrim` | 黑 / alpha 132 | 黑 / alpha 96 | 模态遮罩 |
| `colors.hoverOverlay` | 白 / alpha 26 | 黑 / alpha 16 | enabled hover 表面叠加 |
| `colors.pressedOverlay` | 黑 / alpha 26 | 黑 / alpha 42 | enabled pressed 表面叠加 |

alpha 采用 0–255。颜色叠加使用现有 source-over `blendOver`；对比度在与实际宿主表面合成后计算，不能直接比较带 alpha 的 RGB。

可读性修正理由：当前 dark `onAccent ≈ #EBF1FF` 对 `#568CF0` 约为 2.90:1，不能作为 14 px 标签的目标。新版深色 Filled 使用深色文字，浅色 Filled 使用较深蓝底与白字；深色 pressed 叠加减轻，保证按下时仍可读。HTML 参考里的浅色按钮字不再优先于可读性。

### 4.3 颜色验收与高对比度

- 所有正常可用文字，包括 placeholder、Tooltip 和错误说明，目标对比度至少 **4.5:1**。重要正文在高对比模式目标至少 7:1。
- 必须靠形状识别的控件轮廓、勾号、Radio 内点、Slider Thumb 和焦点标识，与相邻颜色的对比度目标至少 **3:1**。装饰分隔线不机械套用这个门槛。
- 深浅模式的 Normal / Hovered / Pressed、Tonal / Ghost 所在表面、选区与文字组合分别验算；不能只检查 `accent / onAccent`。半透明中间帧也不能使标签失去可读性。
- 禁用态降低强调但保持可辨认的文字和选中标记；ReadOnly 不使用禁用色。错误同时提供文字或图标，选中同时提供勾号/圆点/指示条。
- 高对比模式保留方向色相，通过调整明度、前景和边框派生；边框至少 2 px、焦点环 3 px，阴影不承担唯一层级信息。所有新增 token 必须参与 `Theme::fromSettings`、方向及系统强调色适配。

上述阈值借鉴 [W3C 文字对比说明](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html)和[非文字对比说明](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html)，作为本项目桌面控件验收目标；通过颜色测试不等于完成整套无障碍验收。

### 4.4 尺度、密度和字体缩放

所有数值为 **logical px**，不是物理像素；以 `fontScale=1` 为基准。沿用现有档位算法：`index = clamp(densityBase + sizeOffset, 0, 2)`，densityBase 为 Compact=0 / Comfortable=1 / Touch=2，sizeOffset 为 Small=-1 / Medium=0 / Large=1。因此档位不是 3×3 的九种独立规格。

| 项目 | 档位 0 | 档位 1（桌面默认） | 档位 2 |
| --- | ---: | ---: | ---: |
| 控件最小高度 | 32 | 40 | 48 |
| Button 最小宽度 | 64 | 64 | 72 |
| TextField / Dropdown 最小宽度 | 96 | 120 | 144 |
| 控件水平 padding | 8 | 12 | 16 |
| 控件垂直 padding | 4 | 6 | 8 |
| 图标/文字与控件间 gap | 4 | 8 | 8 |
| 控件圆角 | 4 | 6 | 8 |
| 行内图标边长（新增档位） | 16 | 16 | 20 |
| Checkbox / Radio 指示器边长 | 16 | 18 | 22 |
| Switch 轨道宽 × 高 | 32×18 | 36×20 | 44×24 |
| Switch 滑块直径 | 12 | 14 | 16 |

- spacing 网格为 4：4 / 8 / 12 / 16 / 24 / 32 / 48；字段标签到字段 4，字段到说明 4，字段组之间 16，面板 padding 16，Dialog padding 24。
- 最小高度是下限：实际高度至少容纳文字行盒、上下 padding 与内部保护区域。大字体不得被固定 40 px 高度截断；显式固定尺寸受父约束限制时要按约定裁剪并在 Gallery 标示。
- fontScale 放大排版、最小尺寸、padding、gap、图标和指示器，几何依赖量只缩放一次。圆角与线宽保持逻辑值，必要时受组件尺寸夹取；不再额外乘一次 deviceScale。
- 指示器可视尺寸小于整行命中高度。Checkbox / Radio / Switch 整行可操作区使用上述最小高度；Touch 的标准中档选择达到 48 px。纯 Icon 不凭自身获得交互。
- 普通边框 1 px；普通焦点环 2 px。边框和焦点在节点内部绘制，不改变外框、标签基线和 intrinsic 测量。
- 小指示器的可视尺寸与部件槽位分开：Checkbox / Radio / Switch 的槽位在部件两侧各预留焦点环宽度加 1 px 隔离带，即部件宽度 + 2 × (环宽 + 1)。是否聚焦不改变槽位，也不缩小指示器、轨道或滑块。高对比模式按 3 px 环重新派生槽位；标签间距从槽位边缘计算。

### 4.5 排版、图标与阴影

| 文字角色 | 字号 / 字重 | 目标行高 | 用途 |
| --- | --- | --- | --- |
| `typography.title` | 20 / 600 | 28 | 面板/对话框标题 |
| `typography.body` | 14 / 400 | 20 | 正文、输入文本、行内容 |
| `typography.label` | 14 / 500 | 20 | 按钮、字段标签、Tabs |
| `typography.caption` | 12 / 400 | 18 | 辅助说明、Tooltip、进度说明 |
| Gallery display（应用级） | 32 / 600 | 40 | 首页 hero；窄窗口可用 24 / 32 |
| Gallery code（应用级） | 12 / 400 | 18 | DSL 与 token 值，使用可用等宽字体 |

行高列为逻辑高度；写入 `TextStyle.lineHeight` 时使用“行高 / 字号”的倍数。默认 letterSpacing 为 0，中英文混排和 fallback 使用实际字体度量，不能靠字符串长度估宽。系统字体由现有 FontManager 注入；记录实际使用字体与 fallback。不得用 headless 占位字形评估字重、基线或中文裁切。

Icon 采用现有 `IconId / iconPolylines`；16 px 图标的基准描边 1.5 px，按图标尺寸比例调整。勾号、Chevron 和关闭符号均来自目录，不用字体字符或 emoji 替代。装饰图标不重复朗读；承载操作的按钮必须有非空语义名称。

| 层级目标 | 表面 / 边框 | 阴影目标：offset / blur / alpha | 用途 |
| --- | --- | --- | --- |
| level 0 | surface / 按需边框 | 无 | 常规控件、列表、面板默认 |
| level 1 | surface / borderDefault | (0, 2) / 6 / 32 | 显式抬升的卡片 |
| level 2 | surfaceElevated / borderDefault | (0, 4) / 12 / 64 | Dropdown / Tooltip |
| level 3 | surfaceElevated / borderDefault | (0, 8) / 24 / 80 | Dialog |

阴影为黑色，属于待补的分级 token，不能把当前 `elevation` 数字直接假定成上述 blur 算法。CPU 可使用现有扁平近似，保留表面/边框与层级；高对比模式取消装饰阴影。阴影范围必须参与 damage 和裁剪计算。

## 5. 交互状态：按视觉通道合成

### 5.1 状态来源与组合规则

复用 `InteractionController + FocusManager → InteractionStateSnapshot`；声明状态来自 Widget/绑定。Normal 表示没有额外交互状态。Hovered、Pressed、Focused 可组合，Checked / Selected 在交互结束后持续存在。

**不能用一条 `Disabled > Pressed > Focused > Selected` 排序决定整个外观。** 每个视觉通道分别解析：

| 通道 | 规则 |
| --- | --- |
| 可操作性 | disabled 阻止激活/编辑/拖动并清理 pressed、hover 与无效焦点；不清空数据值、checked、selected |
| 背景 | 先取变体与 checked/selected 基底，再应用 pressed，否则 hovered；disabled 使用对应禁用表面 |
| 前景/标记 | 按当前表面取成对前景；选中、错误、禁用使用各自语义，不整体重复降低 alpha |
| 边框 | enabled invalid 使用错误边框；其次 focused / hovered / normal；disabled 使用禁用轮廓，错误说明可保留 |
| 焦点 | enabled focused 默认显示环，与 pressed、selected、checked、invalid 并存；显式 `showFocusRing=false` 仅关闭环绘制 |
| 数据装饰 | checked 的勾号、Radio 内点、selected 指示、Dropdown 当前值独立保留 |
| 编辑反馈 | caret、selection、composition 仍来自编辑状态；readOnly 保留选择/复制/焦点，拒绝编辑 |

现有 focused 不区分输入来源，默认保留所有已获得焦点的可见标识。应用可用 `Widget.showFocusRing=false` 或 `withFocusRing(widget, false)` 显式关闭焦点环；List/Tree/TreeList 视口传递到生成行，Tree 同时传递到箭头。该开关对鼠标、键盘与语义聚焦一致生效，不清除实际焦点、选择、导航或语义状态。不得用此开关冒充“仅键盘焦点”；未来增加 focus-visible 输入来源仍需要完整事件和无障碍测试。

Checked / Selected 只作用于有对应语义的控件或行部件，不把所有 Container 自动画成选中面。Checkbox / Switch 现有 `checked || selected` 兼容路径保留；Tabs 与列表按其实际 selected 声明解析，不能在迁移中悄悄改写绑定值。

`StyleOverrides` 保留现有“主题/状态解析后覆盖字段”的优先级；显式透明色、黑色和零值按字面处理。应用覆盖可能破坏可读性，Gallery 同时展示原始 token 与最终 resolved 值；不得通过改变覆盖优先级破坏旧调用。

### 5.2 必须出现的组合样本

| 组合 | 可见结果 |
| --- | --- |
| Hovered + Pressed | 只有 pressed 背景，不累计两个 overlay |
| Focused + Pressed | pressed 表面 + 独立焦点环 |
| Checked / Selected + Hovered | 原选中标记 + hover，不回到未选外观 |
| Checked / Selected + Disabled | 弱化表面/文字，仍看得出原数据值 |
| Invalid + Focused | 错误边框/说明 + 清楚的焦点环，不用错误红代替焦点全部语义 |
| ReadOnly + Focused | 正常可读的值 + 焦点/选区；没有可编辑反馈 |
| Theme 切换时 Pressed | 使用新主题合法颜色，释放/取消一次完成，无旧主题残影 |

指针移出、释放到外部、捕获取消、窗口失焦、节点删除或禁用均有状态清理测试；触摸拖动开始后取消 tap pressed，不残留 hover。键盘激活不得重复触发 pointer click。

## 6. 13 种控件的视觉规格

### 6.1 Button

部件：Surface、Border、FocusRing、Label、可选 Icon。Label 与 Icon 作为一个内容组在控件内居中，现有单图标默认为尾随，图标/文本间距取档位 gap。保留五种变体及现有点击、键盘与 disabled 语义。

| 变体 | Normal 背景 | 前景 | 边框 | 使用场景 |
| --- | --- | --- | --- | --- |
| Filled | accent | onAccent | 无 | 首要提交/确认 |
| Tonal | accentContainer | onAccentContainer | 无 | 次级强调 |
| Outline | 透明 | accentContent | borderStrong，1 px | 常规次操作 |
| Ghost | 透明 | accentContent | 无 | 工具栏/轻操作 |
| Danger | statusError | onError | 无 | 删除等破坏性操作 |

- Hover / Pressed：按 §5 改变表面；不缩放整颗按钮、不移动文字。透明按钮只出现半透明状态面，不能为了画边框把整个内部填成 border 色。
- Focused：节点内 2 px 环；填充与环颜色接近时增加 1 px 表面隔离带，几何提前预留，文字位置不变。隔离带也属于 token/部件几何。
- Disabled：Filled/Tonal/Danger 使用 disabledBackground；Outline/Ghost 保留透明基底，Outline 保留弱轮廓。文字使用 disabledContent，无 hover、阴影或 pressed。
- 短文本不低于最小宽度；可用宽度不足时单行省略，图标不盖住文字，完整名称保留在语义中。纯图标组合采用方形控件高度和明确的语义名称。
- 本轮至少验证无图标、文本+尾随图标及现有可表达的纯图标用法；leading/trailing 双插槽、loading API 属于 §11 预留，不伪造现有属性。

### 6.2 Text 与 Icon

Text 默认透明、body / contentPrimary；标签、说明和错误文字使用角色样式。普通段落自动换行，操作名称单行省略；中文、英文、数字混排与 200% 字体缩放不能切掉字形上下沿。只支持裁剪的现有路径需要补齐对应溢出目标后才能算达标，不能把裁剪叫作省略。

Icon 默认不参与焦点或点击。图标盒与同排文字行盒居中，方向性图标与语义一致。未知/None ID 采用空绘制并保持布局盒稳定，不出现随机字形；是否提示资源诊断由调试输出处理。

### 6.3 TextField 与字段组合

字段组合顺序为 `Label → TextField → SupportingText`，间距分别为 4 / 4；必填标识与字段名称属于外部标签，placeholder 不能替代标签。TextField 背景为 surfaceSunken，常态边框 borderStrong，文字 body，尺寸按 §4.4。

| 状态 | 字段外观 | 编辑表现 |
| --- | --- | --- |
| Normal | 1 px 轮廓，placeholder 为 contentSecondary | 空值显示 placeholder |
| Hovered | 轮廓增强到 focusRing，表面不闪动 | 仅 hover 不取得输入焦点；已聚焦时保留光标 |
| Focused | 焦点环 + 原表面，文字基线固定 | caret / selection / preedit 正常 |
| Invalid | statusError 边框 + errorContent 说明/图标 | 仍可输入，focused 同时保留环 |
| ReadOnly | 正常文字，底色使用 surface | 可聚焦、选择和复制，无编辑 |
| Disabled | disabledBackground / disabledContent | 不激活或接受编辑输入 |

- caret 宽度 1 logical px，绘制时避免在低 DPI 消失；选区在文字之前，preedit 下划线不覆盖文字或 selection。光标闪烁沿用 530 ms 半周期。
- 单行字段保持内容在当前视口可见；多行高度按行数与约束测量，不因状态改变 padding。密码圆点、真实文字和 IME 候选位置共享度量链。
- 表单默认预留一行 18 px SupportingText，验证文案可多行扩展；变更说明文本不得重建/失焦编辑对象。非表单的孤立字段可不预留说明行。
- 清除按钮、密码显隐按钮、前后内容插槽仅在真实事件与命中实现后展示；本轮不把它们画成可点击但无行为的图标。

### 6.4 Checkbox、Radio、Switch

共同规则：指示器与首行 label 行盒垂直居中，labelGap 为 8；长标签换行时后续行左边缘对齐标签。焦点不能改变指示器尺寸或 label 起点；disabled 保留 checked 的识别能力。

| 控件 | Off / Unchecked | On / Checked | 主要改造 |
| --- | --- | --- | --- |
| Checkbox | surfaceSunken 内部 + borderStrong 轮廓，圆角 4 | accent 填充 + onAccent 勾号 | 用 `IconId::Check` 替换当前内方块；图标 inset 3/4/5 |
| Radio | surfaceSunken 内部 + borderStrong 圆环 | accent 外环 + 独立 accent 内点，中间有表面空隙 | 内点直径为外径 0.45；不能用两次同色圆填充冒充空心环 |
| Switch | surfaceSunken 轨道 + borderStrong 轮廓，knob 为 contentPrimary | accent 轨道，knob 为 onAccent | 分别解析 knobOff / knobOn，保证开关两态的滑块对比度 |

Checkbox/Radio 的焦点环围绕指示器，可在节点内预留的区域绘制；Switch 环围绕轨道。环、隔离带、外轮廓、内部标记不得相互覆盖。Switch knob 垂直居中，左右内距统一由 `(trackHeight - knobSize) / 2` 推导，不把固定 3 px 套到所有档位。

三者常态轮廓 1 px，高对比 2 px；hover 轮廓取 focusRing，pressed 对当前轨道/指示器表面叠加 pressedOverlay，label 不位移。disabled 保留空心/勾号/圆点/滑块位置，轮廓、标记和文字取 disabledContent，底面取 disabledBackground；不依靠整节点透明度抹掉 checked 状态。

Switch 的颜色过渡按 100 ms，后续滑块位移目标 100 ms EaseOut；位移只改变绘制位置，命中区域固定。若本轮尚未打通部件数值动画，先交付正确即时位置并把位移项记为未完成，不另启控件定时器。

Radio 沿用布尔绑定与应用互斥。本轮覆盖鼠标/键盘切换和组示例，不改变组内选中值存储。Checkbox 三态不在本轮基线。

### 6.5 Slider

部件 token：trackHeight=4、trackRadius=2、thumbDiameter=16/18/22、thumbBorderWidth=2；可操作整行高度为 32/40/48。未完成轨道用 borderStrong，完成轨道用 accent，Thumb 用 surfaceElevated + accent 轮廓；label/数值说明在外部组合，不能占据拖动轨道。

- 以 Thumb 半径 `r` 和焦点保护宽度 `f = theme.metrics.focusRingWidth + 1` 恒定预留左右端点：中心从 `x+r+f` 到 `x+width-r-f`，不因 focused 状态变化重定位。值 0 和 100 时 Thumb 与焦点环均在节点内；不足以容纳两侧预留时将轨道长度夹取为 0 并居中，禁止除以零或生成负尺寸。
- 绘制值到位置和指针位置到值使用同一轨道区间；拖动、键盘、语义 value 和视觉填充同步。范围继续为 0–100 整数，不顺带增加范围滑块。
- Normal / Hovered / Pressed / Focused / Disabled 均有样本；hover/拖动时 Thumb 轮廓取 focusRing，按下时内部表面叠加 pressedOverlay。focus 环在 Thumb 外围的预留节点区域绘制；disabled 轨道/轮廓取 disabledContent，Thumb 内部取 disabledBackground。
- 拖动值即时跟手，不对用户拖动追加缓动；释放不回弹。显示在外部的百分比与实际绑定值一致。

### 6.6 ProgressBar

进度条可视高度 4/6/8，圆角为高度一半，背景 borderDefault、填充 accent，宽度为可用轨道宽 × `clamp(value,0,100)/100`。0% 无填充，100% 填充到终点；小于圆角直径的填充也不得画到轨道外。

没有 hover/pressed/focus 或拖动反馈。label 与数值由 Text 组合，默认 caption；完成/失败等业务语义以明确文字和应用选择的语义 token 表达，不从数值 100 自动推导“成功”。本轮数值变化即时呈现；不确定进度与循环动画预留。

### 6.7 Dropdown

值行与 TextField 共用高度、radius、padding、背景和必要轮廓，文字垂直居中。尾随 Chevron 使用 16/16/20 的图标盒，单独预留空间；展开时换向 ChevronUp，不修改标签位置。

浮层使用 surfaceElevated、1 px borderDefault、radius 8、level 2；与锚点间隔 4，窗口安全边距 8，内部 padding 4。宽度至少覆盖值行，且不超过窗口可用宽；长标签单行省略并保留完整语义。

选项最小高度 32/40/48，水平 padding 8/12/16。当前值使用 accentContainer + onAccentContainer，并预留 16 px 勾选列；键盘活动项使用独立焦点/轮廓，hover 使用状态面。移动高亮不立即更改选中值，Enter 确认后关闭并恢复值行焦点。

复用现有 overlay、Escape/外部点击关闭、上下边界翻转和键盘路由。项目过多时菜单最大高度为 `min(320, 可用高度)`，内容进入垂直滚动；若当前 controller 未支持滚动，须补组合并测试活动项滚入可见区。选项禁用、分组和搜索不在本轮基线。

### 6.8 Tabs

默认采用平面页签行：透明底、label、水平 padding 12，最小高度 40，页签间 gap 4，底部分隔线 borderDefault。当前项为 accentContent，并在页签底部保留 2 px accent 指示条；其余项 contentSecondary。hover 有状态面，focused 独立画环，selected+focused 同时可见。

继续以已有 Button 子节点与 selected 声明表达选中；由 Tabs 上下文解析页签外观，或用统一组件 token 的组合器实现，不能在 Gallery 给每个按钮手写颜色。内容切换由应用管理。Tab 导航/激活沿用现有行为，不宣称已经有 roving-tabindex 或方向键组选中协议。

可用宽度不足时标签在最小宽度约束内省略，页签集合可换行并按阅读顺序排列；本轮不新增水平滚动系统。换行后的 selected 指示仍归属本项，选中切换不动画改变行高。

### 6.9 Tooltip

使用 caption、contentPrimary、surfaceElevated、radius 6、borderDefault 和 level 2，padding 横 8 / 纵 6，最大宽度 280，文本可换行。与锚点间隔 8，窗口边距 8；优先下方，空间不足上翻，再做边界夹取。

hover 延迟 400 ms，出现/消失淡变 120 ms；离开、按下、滚动、窗口失焦或锚点消失时取消待显示任务。Tooltip 不获得焦点、不阻断命中、不承载操作，提示不能成为控件唯一名称。当前键盘焦点触发尚需新增接线时标明，不以 hover 演示替代键盘验证。

本轮沿用常驻节点与现有管理入口；需要逃离滚动裁剪的提示放入既有 overlay 机制，不在任意 Stack 上关闭全部裁剪。`reduceAnimation=true` 沿用当前策略：delay 与 fade 都为 0，不残留不可见节点的动画请求。

### 6.10 Image

未就绪占位：surfaceSunken、1 px borderDefault、居中图片图标，保持应用给定盒尺寸。当前目录没有 `IconId::Image`，本轮新增该目录项并保持已有 ID 的值不变。占位图标使用 contentSecondary，最大 24 px；不再按整张图片高度比例生成粗边框。

保留当前 ImageId 就绪后拉伸到盒子的默认契约；本轮不把 contain/cover、圆角裁剪或淡入声明成已有功能。未来增加这些能力要连同采样/裁剪/命中和资源生命周期设计。占位到已就绪图像不移动相邻内容；资源失效、主题切换与缓存重建必须刷新占位。加载中与失败只有资源层提供明确状态后才显示不同文案。

## 7. 容器、列表、焦点域与局部主题

### 7.1 容器与布局

`Container` 默认透明。panel 组合使用 surface + borderDefault + radius 8 + padding 16；inset 使用 surfaceSunken；只有明确抬升的容器使用 elevation。Row/Column/Stack/Grid/FocusScope/ThemeScope 不自动画边框或背景。

Row 的按钮组 gap 8，Column 的字段组 gap 16，Grid 的面板 gap 16。布局间距由 token 传入，不因主题深浅改变。边框以 border-box 内部绘制；内容 padding 只计算一次，不能同时在 Widget 与 component chrome 叠加同一个 padding。

圆角外观不自动意味着子内容圆角裁剪。使用现有矩形裁剪时保持内容 inset；若必须圆角裁剪，应先扩展统一渲染契约并测试，不能仅让 Skia 生效。

### 7.2 ScrollView / ListView / VirtualList / Scrollbar

列表行是子节点组合，单行最小高 32/40/48，双行默认 56；padding 横 12，图标与内容 gap 8，行内主/次文字间 gap 4。hover 使用状态面，selected 使用 accentContainer 与标记，focused 使用独立环。多选仅展示应用现有选择状态，不新增 selection controller。

后续集合控件已引入 `ListController`（2026-09）：新 `List` 的现行契约以
`lumen-collection-controls-design.md` §6/§10 为准，横向 padding 为 8/12/16，
由 `Theme.list` 提供选中实色、指示条、分隔线与自动空态。旧 ListView/VirtualList
继续由应用构建行。Gallery Collections 提供真实交互与七态视觉样本。

滚动容器必须裁剪内容且保持聚焦行可见。VirtualList 的交互和动效状态按稳定 key 归属，不按可见索引复用；高度修正、主题/字体变化后的滚动锚点保持可解释。

Scrollbar 继续作为 `withScrollbar` 启用的附属部件：

- 无溢出不显示；存在溢出时默认常显，轨道透明。宽度 token 8、可视 Thumb 4、最小长度 24、上下 inset 4、圆角为可视宽度一半。
- Thumb 长度按 viewport/content 比例计算，在 `[minLength, trackLength]` 内夹取；短视口时不得因 24 px 最小值越界。offset 到位置的映射与滚动范围一致。
- rest 颜色取 borderStrong，通过专用 token 传递到 RenderNode；不再在 painter 将 contentPrimary 随意乘 alpha。
- hovered / dragging / auto-hide 为 §11 预留交互；没有真实命中与事件接线前维持常显，不能做“看起来可拖但拖不动”的扩展演示。

### 7.3 FocusScope 与 ThemeScope

FocusScope 自身不绘制环；环属于真正聚焦的控件。Dialog/菜单关闭后恢复到仍存在且 enabled 的触发器；触发器已删除时落到当前作用域的可用焦点，不能持有失效 identity。

ThemeScope 内颜色、部件、字体、度量必须来自同一局部 Theme；退出后恢复父主题。Tooltip/Dropdown/Dialog 脱离原树形成 overlay 时，明确继承触发器有效主题，并应用当前无障碍设置；不能悄悄回落到窗口根主题。实现时覆盖滚动条、图标、阴影等 RenderNode 字段的局部主题来源，不能只验证文字颜色。

## 8. Form、Dialog、Navigator 与浮层生命周期

### 8.1 FormController

复用现有错误映射：字段 invalid 与错误文案同帧更新，提交失败按既有表单行为定位可用字段。错误颜色用于边框、说明和图标；不修改业务校验规则。成功/警告可作为应用状态说明呈现，扩展校验模型属于独立事项。

必须验证：提交错误、修正输入、清除错误、焦点仍在编辑字段、中文 IME 未被视觉重建打断。说明行预留与多行策略按 §6.3。

### 8.2 makeDialog

表面 surfaceElevated、radius 12、borderDefault、level 3，外侧窗口边距至少 16；常规内容宽度 240–420，窄窗口中最大宽度优先受可用空间限制。内容决定高度，最大高度受窗口可用高度约束，长正文进入内部滚动区，操作区保持可见。

标题 title、标题到正文 12、正文到操作区 24、按钮 gap 8、整体 padding 24；取消在前、确认在后，危险确认使用 Danger。沿用 `makeDialog` 的 barrier / FocusScope，不为这些布局新增 Dialog WidgetType。

当前 helper 的定位、比例尺寸和内容 padding 不保证自动达到上述内容约束；实现必须核对测量与真实 RenderNode，而不是只改 `DialogTokens` 数字。

打开时先建立模态屏障和合法焦点，再开始 alpha；关闭时若启用退场，屏障保留至结束，阻止点击穿透，随后移除并恢复焦点。反复打开/关闭需取消前次完成回调。Escape 与点击遮罩是否关闭仍由当前应用策略决定。

### 8.3 NavigatorController 与 overlay

导航默认保持即时切换；允许应用显式选择 200 ms Fade，不默认页面平移或共享轴。路由栈与返回优先级仍由 NavigatorController/应用管理，动画不决定数据路由。

绘制 alpha 不控制命中或语义可见性。若保留旧页面做过渡，它只能是无交互的绘制快照；新页面独占输入，模态 barrier 优先，完成时销毁旧快照。浮层主题、焦点恢复和关闭责任必须有单一所有者，不由各 painter 猜测。

## 9. 动效规格与渲染约束

### 9.1 本轮动效表

| 场景 | 时长 / 缓动 | 影响范围 |
| --- | --- | --- |
| Hover / Pressed / Checked 颜色 | 100 ms / EaseOut | 表面/轮廓/标记颜色；事件与绑定即时生效 |
| 键盘焦点出现 | 0 ms | 首帧完整焦点标识，不能等淡入后才可见 |
| Switch knob 位移 | 100 ms / EaseOut | 仅绘制位置；属于需补齐的部件动画 |
| Tooltip | 延迟 400 ms + fade 120 ms / EaseOut | alpha；与触发延迟分开 |
| Dropdown 打开/关闭 | 120 ms / EaseOut | 可选 alpha，保持锚点与几何固定 |
| Dialog 打开/关闭 | 200 ms / EaseOut | barrier 与表面 alpha，共同生命周期 |
| Navigator Fade | 200 ms / EaseInOut | 应用显式开启；现有 token 350 ms 需按本规格调整 |
| caret | 530 ms 半周期 | 仅编辑光标；沿用已有开关 |
| ProgressBar 值、Slider 拖动、主题/密度切换 | 0 ms | 立即达到正确状态，防止跟手延迟或主题混色 |

本轮只需现有 Linear / EaseIn / EaseOut / EaseInOut 与必要部件数值 Tween。不要求 Spring、CubicBezier、任意属性图或新动画线程。

- 新目标打断旧过渡时从当前可见值开始，不能回到上次初始值。每个 identity + property 同时最多一个有效过渡。
- 焦点标识不参与常规状态色插值；出现/消失使用当帧合法终值。由 disabled 或主题切换触发的可用性、颜色切换也直接收敛，避免中间颜色产生误导。
- `reduceAnimation=true` 使用现有 Theme 归零机制，含 Tooltip 延迟；数据状态直接到终态，移除、关闭与完成回调仍执行一次，不等待被取消的时钟。
- 隐藏、销毁、主题切换和控件类型变化时清理过渡。无活动动画、光标或惯性滚动时，不能持续申请动画帧。
- 几何布局保持目标值；颜色/alpha/knob 的中间绘制数据不回写 Widget，不在每帧分配整套视觉部件树。

### 9.2 部件与绘制顺序

视觉部件是已有节点的样式/几何值，不是第二棵可命中、可布局或可访问的运行时树。建议绘制顺序：

```text
父级有效裁剪
  → 阴影（按已计算的外扩范围，仍受父裁剪约束）
  → Surface / 状态面
  → Border
  → 内容裁剪：selection → label / text / icon → caret / preedit
  → FocusRing（保留区域内，最后绘制，不覆盖文字）
  → 该视口的 Scrollbar
根内容完成后 → Overlay barrier → Overlay surface/content/focus
```

透明 Outline、Radio 空心环与 FocusRing 不能用“不透明外矩形 + 不绘制透明内矩形”实现，那会得到实心块。优先使用已有通用图元正确组成；若当前圆角轮廓无法正确表达，最小增加通用描边命令，并同步录制/回放、CPU/Skia/GPU、裁剪和 damage 测试，不能只修一个后端。

### 9.3 DPI、缓存和性能

- 在逻辑坐标完成布局；到后端光栅阶段才按 deviceScale 对齐细线。测量与事件坐标不反复舍入。覆盖 1.0 / 1.25 / 1.5 / 2.0 的边框、圆角、Thumb 和中文基线。
- 状态前后节点外框不变；焦点/标记中间帧不越界，Slider 的两个端点尤其检查。阴影外扩纳入新旧 damage 并集。
- 主题、局部主题、字体、密度、资源版本和状态变化都必须使相应缓存失效；不能用一个全局颜色缓存跨 ThemeScope 复用。
- 状态色和 alpha 动画不得新增每帧文本整形/布局。沿用现有重建机制时记录成本，不以“纯绘制”描述未经测量的实际路径。
- 性能按 [既有性能基线](perf-baselines/README.md)与同场景命令比较：记录环境、后端、构建类型、帧时间、物化节点数和活动动画数。不同字体、viewport 或 Debug/Release 数据不能直接比较。

## 10. Gallery：可复查的控件样本

### 10.1 页面接入

复用现有页面与路由：Buttons 展示五变体；Inputs 展示字段与选择控件；Layout 展示容器/密度/长文本；Lists 展示行/滚动/VirtualList；Feedback 展示 Slider/Progress/Tooltip/Dialog；Theme 展示 token、四方向与局部覆盖。Dropdown/Tabs 放入对应输入/导航演示区，不因文档章节另造一套 Gallery。

每个控件样本包含名称、用途、真实交互实例、稳定状态矩阵、三档尺寸、主题切换、关键 token 与可运行 C++ builder 示例。强制状态只写入预览快照，不触发业务回调；交互测试另用真实 pointer / key / focus 事件。

### 10.2 必备矩阵

| 维度 | 最小覆盖 |
| --- | --- |
| 主基线 | Core Dark direction × dark/light × Comfortable/Medium，全控件全部适用状态 |
| 按钮变体 | 五变体 × Normal/Hovered/Pressed/Focused/Disabled，另有 Focused+Pressed |
| 选择/输入组合 | Checked+Disabled、Selected+Focused、Invalid+Focused、ReadOnly+Focused |
| 尺寸 | 三档，中英文长标签、带图标、窄容器 |
| 主题兼容 | 其余三个方向各自 dark/light，至少一个全控件 Normal 样本及焦点/禁用组合 |
| 无障碍 | 高对比 dark/light、reduceAnimation、fontScale 1 / 1.5 / 2；验证组合而非只切一个开关 |
| 布局 | 1440×900、1024×768、800×600、窄桌面 600×700 logical px |
| 缩放 | deviceScale 1 / 1.25 / 1.5 / 2，固定 logical viewport |
| 动效 | 注入时钟采样 t=0、0.5T、T；另覆盖打断与减少动画 |

窗口变窄时状态样本和按钮组允许换行，Grid 减少列数，说明文本换行；Dialog/菜单仍留在可用区域。不要为容纳样本把标准控件缩成 HTML 微缩预览。Gallery 壳层断点改动单独记录，不将样本可见性寄托于横向滚动。

### 10.3 截图与基准记录

截图命名建议：`<control>-<variant>-<state>-<direction>-<mode>-<density>-fs<fontScale>-dpi<scale>-<backend>-<platform>.png`，附 viewport、字体、提交、时间点和强制状态参数。图像保存位置遵守仓库生成物规则，报告保留可访问的产物链接，不把可执行文件或 build 缓存提交。

三类证据分别记录：

1. 纯值/几何/命令断言：定位规则和部件错误。
2. 固定字体、时钟、viewport 的 headless 图像：稳定回归；同环境可以逐像素比较。
3. 真实桌面字体与后端截图：评估字形、抗锯齿、细线与实际交互；不同平台不要求整图哈希一致。

`--headless --dump-frame` 当前可辅助导出 Gallery 首帧，但不是自动覆盖全部路由与状态的截图测试。实施时要补稳定样本入口，不能把一次 headless smoke 通过当作完整视觉通过。

## 11. 后续预留能力与完成边界

| 预留项 | 需要补充的真实契约 | 本轮处理 |
| --- | --- | --- |
| Button loading、前后插槽 | busy 状态/语义、重复激活抑制、内容测量与命中 | 保留部件设计空间，不添加假 loading 参数 |
| Checkbox indeterminate | 三态值、循环/切换规则、语义与绑定兼容 | 本轮验收二态；未来横线标记不得与勾号混淆 |
| Form warning / success | 分级校验模型、清理和提交策略 | 当前 errors/invalid 不扩张 |
| 不确定 ProgressBar | 模式和值域、可暂停调度、减少动画的静态替代 | 本轮确定进度 |
| Scrollbar hover / drag / auto-hide | 命中区、捕获、取消、滚动同步、计时生命周期 | 本轮常显附属绘制，token 补齐消费 |
| Radio 组、Tabs 方向键、Dropdown 禁用项/分组 | 选择模型、导航与语义协议 | 保留已有应用/controller 所有权 |
| Image fit、圆角裁剪、加载/失败区分 | 资源状态、采样规则与跨后端裁剪 | 保留默认拉伸和未就绪占位 |
| focus-visible、三档 MotionMode、Spring | 输入来源、偏好适配与完整调度 | 沿用 focused、reduceAnimation、Tween |

预留项不能阻止本轮已有控件视觉交付，也不能在交付报告中勾为已完成。§6–§9 中明确要求的可见状态、部件样式、菜单可用空间处理、正确描边和必要数值过渡，仍是本轮工作，不能统一挪到预留表规避实现。

## 12. 分阶段实施与审查顺序

每阶段执行“核对当前代码 → 实现 → review 并修复 → 验证 → 更新记录”后再继续。先复用现有能力，新增抽象须对应本文中的具体缺口。下表当前全部为待实施，不复用 M6/M10/M11 的历史完成标签。

| 阶段 | 交付内容 | 出口条件 |
| --- | --- | --- |
| S0 现状与样本冻结 | 复核 §2，记录源码提交、现有截图、缺口与测试入口；建立可重复样本 | 23 类和辅助能力都有状态/源码对应，不以旧截图代替当前结果 |
| S1 token 与通用绘制 | §4 调色板/排版/尺寸、透明描边、焦点/圆角/阴影、继承与派生 | 深浅/四方向/高对比对比度测试，透明背景与边框/焦点不实心、不越界 |
| S2 基础控件 | Text、Icon、Container、Button、TextField、Checkbox、Switch、Radio | 五按钮变体和关键组合状态通过，真实中文排版与编辑无回归 |
| S3 选择、导航与滚动 | Slider、ProgressBar、Dropdown、Tabs、ScrollView/ListView/VirtualList 及 Scrollbar | 端点、选中/焦点、菜单滚动/边界、稳定 key 与实际事件一致 |
| S4 浮层与组合 | Tooltip、Image、Form、Dialog、Navigator、ThemeScope/FocusScope；其余布局组件核对 | 主题继承、裁剪、焦点恢复、禁用与生命周期正确 |
| S5 动效与质量收敛 | 在以上每阶段已有样本上完成过渡、中断、减少动画、完整 Gallery 与跨后端验证 | 全部适用矩阵与 §13 门槛通过；预留和未验证平台单列 |

每个控件的状态和必要测试随所属阶段交付，不能等 S5 才发现 S2 的 focus/disabled 缺失。S1 涉及渲染命令变更时，先完成跨后端契约验证再开始控件迁移。

### 12.1 代码落点与最低实现约束

| 落点 | 负责内容 |
| --- | --- |
| `include/lumen/style/{tokens,theme,state}.h`、`src/style/theme.cpp` | token、主题派生和必要状态数据；颜色计算保持确定性 |
| `src/style/resolver.cpp`、`include/lumen/core/style.h` | 控件/部件样式、状态合成、最终值；Radio/Slider/Dropdown 等不再依赖通用容器回退补色 |
| `src/layout/layout.cpp`、`include/lumen/core/render_node.h` | 统一度量、部件位置与绘制边界；不重复布局一棵“视觉树” |
| `src/render/painter.cpp`、现有 renderer/command 路径 | 消费最终样式；必要描边或部件图元保持后端无关 |
| `src/app/app_shell.cpp`、`src/core/interaction.cpp` | 事件快照、动画调度、命中/取消；视觉状态不代替事件状态 |
| `src/widgets/` | 表单/下拉/模态/路由组合及主题/焦点生命周期 |
| `examples/gallery/`、`tests/` | 可复现样本、真实行为与渲染验收 |

Theme 不持有控件对象、计时器或后端资源。Renderer 不接收 Theme，不导入 style 层；`lumen-core` 不导入 SDL/Skia/平台类型。新增公共字段同步比较、重建、绑定/DSL 支持情况与文档；不要在 `StyleOverrides` 中加入任意脚本或后端句柄。

C++ builder 示例只能使用真实签名。`.lumen` 仍为受限文本 DSL，不能承诺覆盖全部 23 类；新增语法需成套更新 parser、diagnostics 和测试，纯视觉 token 调整不扩展 DSL。

## 13. 测试与视觉验收门槛

### 13.1 自动化验证

| 层级 | 优先复用的测试文件 | 必须验证的行为 |
| --- | --- | --- |
| 主题/状态 | [style_tests.cpp](../tests/style_tests.cpp) | 新 token 派生、颜色合成/对比度、状态组合、显式透明/黑色、密度与 fontScale |
| 几何/裁剪 | [layout_tests.cpp](../tests/layout_tests.cpp)、[grid_virtual_tests.cpp](../tests/grid_virtual_tests.cpp) | 状态前后外框/基线不变、Slider 端点、短视口滚动条、窄菜单/弹窗、局部主题度量 |
| 渲染命令/像素 | [render_tests.cpp](../tests/render_tests.cpp)、[render_command_tests.cpp](../tests/render_command_tests.cpp)、[damage_tests.cpp](../tests/damage_tests.cpp) | Outline 真透明、Radio 空心/内点、Check 勾号、焦点/阴影 damage、record/replay 与 submit 一致 |
| 输入/组合 | [interaction_tests.cpp](../tests/interaction_tests.cpp)、[app_shell_tests.cpp](../tests/app_shell_tests.cpp)、[visual_m6_tests.cpp](../tests/visual_m6_tests.cpp) | pointer/key/cancel、disabled 清理、表单输入、Dropdown/Tooltip/Dialog、焦点恢复 |
| 动效 | [motion_scroll_tests.cpp](../tests/motion_scroll_tests.cpp)、[tween_tests.cpp](../tests/tween_tests.cpp) | 中间帧、打断/取消、对象删除、reduceAnimation、空闲不续帧、滚动中不残留 pressed |
| Gallery/语义/字体 | [gallery_integration_tests.cpp](../tests/gallery_integration_tests.cpp)、[semantics_tests.cpp](../tests/semantics_tests.cpp)、[system_font_tests.cpp](../tests/system_font_tests.cpp) | 样本与 token 一致、语义状态、中文/真实字体路径；桌面人工验收仍需单列 |

只测试发生行为差异的契约与回归，不为每个色值或私有函数机械建立镜像测试。对比度测试覆盖状态与实际表面组合；不能断言“颜色等于新常量”就宣称可读性通过。

### 13.2 构建与运行

以 [build-commands.md](build-commands.md)为唯一命令表。每个代码实施阶段至少完成定向测试并在交付前执行完整 CPU 构建与 CTest：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

Gallery headless、真实窗口、Skia 光栅、GPU 验证使用命令表相应入口；Windows 多配置生成器的程序路径包含 `Debug/` 或 `Release/`，以实际产物路径运行。Windows Skia 遵循现有 Release/CRT 约束。

修改命令/渲染契约必须验证 CPU/Skia 光栅/GPU 的相关回放与图像路径；最终报告写出实际后端，GPU 回退到 CPU/Skia 光栅不算 GPU 通过。Linux/macOS 可由 CI 或对应主机提供证据，缺少环境时明确“未验证”，不能用 Windows 测试推断三平台完成。

### 13.3 人工视觉与交互清单

- [ ] 所有 23 种 WidgetType 都有接入说明；13 种内容/交互控件均有深浅色样本，10 种布局/容器按职责验收。
- [ ] 五种 Button 变体可分辨；Outline 内部确实透明；focused 与 pressed 可同时辨认。
- [ ] Checkbox 使用勾号；Radio 未选为空心、已选有内点；Switch 聚焦时不挤压滑块。
- [ ] TextField 的光标、选区、placeholder、错误说明和中文 IME 在真实字体下对齐；ReadOnly 与 Disabled 可分辨。
- [ ] Slider 0/100 不越界且命中映射一致；ProgressBar 0/100 与真实值一致。
- [ ] Dropdown/Tabs 的当前值与焦点不同；长菜单/窄窗口可达所有项目；Tooltip 不遮断操作。
- [ ] 三档尺寸、长文本、200% 字体缩放和非整数 DPI 没有文字重叠、细线消失或控件布局跳动。
- [ ] 局部主题的菜单、提示、滚动条和阴影不串色；切主题、回退后端、资源替换后无残影。
- [ ] 对话框打开/关闭、反复切路由、控件销毁中断动画时，没有焦点丢失、输入穿透或重复回调。
- [ ] 减少动画后立即得到可见终态，关闭/清理仍完成；空闲不因隐藏提示或过渡无限重绘。
- [ ] 有真实桌面截图与交互记录；headless、实际窗口、真实字体和各后端验证范围分别说明。

## 14. 实施记录与交付模板

实现阶段按下列结构记录，不改写本文 §2 的历史核对时间来掩盖状态变化：

```text
阶段 / 日期 / 源码提交：
已完成控件与规格章节：
新增或调整的 token / ResolvedStyle / 公共 API：
源码审查发现、修复与仍存缺口：
状态/主题/尺寸样本及截图链接：
验证命令、测试结果、真实平台/后端/字体：
性能对照的环境、场景与数据：
兼容性或视觉基准变更：
未验证事项与预留能力：
下一阶段及前置条件：
```

完成定义：本轮目标控件按同一套 token、几何和状态契约实现，Gallery 可复现，自动化与真实桌面证据支撑结论，所有未完成/未验证事项明确。颜色表或任务文档写完、示例截图看起来接近、单次 headless 通过，均不单独构成视觉系统实施完成。
