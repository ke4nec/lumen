# GUI 控件视觉系统建设与现有控件品质提升任务书

> 文档用途：将本文档交给 GPT-5.6、Codex 或其他代码智能体，指导其在现有自研 GUI 框架基础上，建立独立、可扩展、渲染后端无关的控件视觉系统，并改善已经完成的控件的最终视觉效果、交互状态和动效品质。
>
> 适用项目：自研跨平台 GUI 框架，可能采用 C++20 或 Rust，支持声明式 UI DSL，并计划支持 Skia、Impeller 等不同渲染后端。
>
> 重要原则：本任务不是重新开发一个 GUI 框架，也不是简单给现有控件增加颜色和动画，而是对现有控件进行架构审查、视觉系统抽象、统一状态管理、动效建设和质量提升。

---

## 1. 任务目标

在不破坏现有功能和公共 API 的前提下，为当前已经完成的 GUI 控件建立一套独立的控件视觉系统（Control Visual System）。

最终目标：

1. 现有控件具有统一、成熟、协调的视觉风格。
2. 控件的逻辑、状态、主题、视觉树、动画和渲染后端彼此解耦。
3. 控件可以根据状态自动呈现不同视觉效果。
4. 支持 Hover、Pressed、Focused、Disabled、Selected、Checked、Loading 等状态。
5. 支持颜色、透明度、尺寸、圆角、边框、阴影、位移、缩放等属性的平滑过渡。
6. 支持统一的动画、缓动、弹簧和过渡机制。
7. 支持主题切换、深色模式、浅色模式和未来的自定义主题。
8. 视觉系统不依赖具体渲染后端，能够适配 Skia、Impeller 或其他渲染实现。
9. 不将视觉细节硬编码在每个控件中。
10. 建立 Component Gallery，用于展示、调试和回归验证所有控件状态。

---

## 2. 执行规则

### 2.1 必须先审查，后实施

在修改代码之前，必须完整审查当前项目：

- 项目目录结构。
- 构建系统和依赖。
- 当前 GUI 核心架构。
- 控件基类或组件基类。
- 布局系统。
- 状态系统。
- 事件系统。
- 输入和焦点系统。
- 主题系统。
- 样式系统。
- 绘制 API。
- 渲染后端抽象。
- 动画或定时器机制。
- 文本排版和字体系统。
- 图标系统。
- 已经完成的控件。
- 测试、示例和 Demo。
- 当前存在的性能问题和已知缺陷。

不要在不了解现有架构的情况下直接创建一套新的平行控件体系。

### 2.2 先形成审查报告

在开始编码之前，输出一份审查报告，至少包括：

1. 当前架构概览。
2. 已有控件清单。
3. 当前控件的绘制方式。
4. 当前状态和事件处理方式。
5. 当前主题与样式能力。
6. 当前动画能力。
7. 当前渲染后端耦合点。
8. 可以复用的基础设施。
9. 需要新增的抽象。
10. 可能影响兼容性的地方。
11. 风险、优先级和实施顺序。
12. 推荐的最小改造方案。

审查阶段不要急于大规模修改代码。

### 2.3 优先复用已有能力

如果项目已经具备以下能力，应优先扩展，而不是重复实现：

- 属性系统。
- 响应式状态系统。
- 样式或主题系统。
- 定时器。
- 帧循环。
- 渲染上下文。
- 几何类型。
- 颜色类型。
- 字体和文本排版。
- 输入事件。
- 焦点管理。
- 布局系统。
- 无障碍语义。
- 日志和调试工具。

### 2.4 禁止事项

未经充分论证，不得：

- 推翻现有控件体系。
- 替换整个布局系统。
- 替换整个事件系统。
- 将所有控件改成硬编码绘制。
- 将所有视觉逻辑塞进控件基类。
- 将动画逻辑散落在各个控件中。
- 绑定某个具体渲染后端的 API。
- 为了视觉效果牺牲键盘操作、可访问性和性能。
- 添加无法验证的复杂特效。
- 使用大量固定颜色和固定尺寸。
- 只修改 Demo，不改善框架本身。
- 只提供设计建议而不落地代码。
- 在没有测试的情况下声称改造完成。

---

## 3. 核心架构目标

建议将系统划分为以下层次。实际命名必须结合现有项目，不得机械照搬。

```text
Control Logic
    |
    v
Interaction State
    |
    v
Visual State Resolver
    |
    v
Theme / Design Tokens
    |
    v
Visual Tree / Visual Parts
    |
    v
Animation / Transition System
    |
    v
Rendering Abstraction
    |
    +--> Skia Backend
    |
    +--> Impeller Backend
    |
    +--> Other Backend
```

### 3.1 控件逻辑层

负责：

- 用户交互。
- 事件处理。
- 数据绑定。
- 命令触发。
- 选中状态。
- 勾选状态。
- 文本输入。
- 焦点语义。
- 控件生命周期。

不负责：

- 直接管理每一帧动画。
- 直接操作 Skia 或 Impeller。
- 在事件回调中硬编码所有视觉细节。

### 3.2 交互状态层

统一表示控件的交互状态，例如：

```text
Normal
Hovered
Pressed
Focused
KeyboardFocused
Disabled
Selected
Checked
Indeterminate
Loading
ValidationError
ValidationWarning
ReadOnly
Expanded
Collapsed
```

要求：

- 状态可以组合。
- 状态变化可被观察。
- 状态变化能够触发视觉状态重新解析。
- 鼠标、触摸、键盘和辅助输入方式应尽可能统一。
- 不应只依赖“当前鼠标是否在控件上”这种临时判断。

可以采用状态位、状态集合或其他适合现有架构的方式。

### 3.3 视觉状态解析层

视觉状态解析层负责根据：

- 控件类型。
- 当前主题。
- 控件尺寸。
- 交互状态。
- 重要性或强调级别。
- 只读、禁用、错误等语义。
- 用户偏好。

计算最终的视觉属性。

例如：

```text
Button + Primary + Hovered
    -> Hover Background
    -> Hover Border
    -> Hover Foreground
    -> Hover Elevation
    -> Hover Cursor
    -> Hover Transition
```

视觉状态解析应尽量是可测试、可预测的纯逻辑，避免与具体绘制 API 强耦合。

### 3.4 视觉部件层

控件不要只被视为一个整体矩形。应允许由多个视觉部件组成：

```text
Button
├── Background
├── Border
├── FocusRing
├── Content
│   ├── LeadingIcon
│   ├── Label
│   └── TrailingIcon
├── PressFeedback
└── LoadingIndicator
```

输入框可以包括：

```text
TextField
├── Container
├── Background
├── Border
├── FocusRing
├── LeadingContent
├── TextContent
├── Placeholder
├── ClearButton
├── TrailingContent
├── ValidationIndicator
└── SupportingText
```

要求：

- 视觉部件可以复用。
- 视觉部件可以独立参与状态和动画。
- 视觉部件不应破坏控件的布局和语义。
- 视觉部件应尽量由统一的视觉系统创建和管理。

---

## 4. Design Tokens 设计令牌系统

必须建立统一的设计令牌系统，避免控件中散落魔法数字。

### 4.1 令牌分类

至少考虑以下分类：

```text
Color Tokens
Typography Tokens
Spacing Tokens
Size Tokens
Corner Radius Tokens
Border Tokens
Shadow / Elevation Tokens
Opacity Tokens
Motion Tokens
Icon Tokens
Focus Tokens
Density Tokens
```

### 4.2 颜色令牌

不要只定义 ButtonBlue、TextGray 这类与具体控件绑定的颜色。

建议优先定义语义颜色：

```text
Surface
SurfaceElevated
SurfaceSubtle
SurfaceSunken
ContentPrimary
ContentSecondary
ContentTertiary
ContentDisabled
BorderDefault
BorderSubtle
BorderStrong
Accent
AccentHover
AccentPressed
FocusRing
Danger
Warning
Success
Info
```

颜色系统应支持：

- 浅色主题。
- 深色主题。
- 高对比度主题的扩展。
- 语义颜色映射。
- 状态颜色派生。
- 统一的透明度规则。

不要直接在控件中写死 RGB 或十六进制颜色。

### 4.3 尺寸和间距令牌

至少定义：

```text
ControlHeightSmall
ControlHeightMedium
ControlHeightLarge

Spacing1
Spacing2
Spacing3
Spacing4
Spacing6
Spacing8

CornerRadiusSmall
CornerRadiusMedium
CornerRadiusLarge
CornerRadiusPill

BorderWidthThin
BorderWidthDefault
FocusRingWidth
```

具体数值必须根据当前项目的字体、DPI、平台和设计目标确定，不要盲目照搬其他框架。

### 4.4 字体令牌

至少支持：

- 正文。
- 标签。
- 标题。
- 辅助说明。
- 输入文本。
- 按钮文本。
- 禁用文本。
- 等宽文本（如果项目需要）。

应考虑：

- 字号。
- 字重。
- 行高。
- 字间距。
- 字体回退。
- DPI 缩放。
- 中英文混排。
- 文本裁剪和省略。

### 4.5 阴影与层级

建立统一的层级概念，例如：

```text
ElevationNone
ElevationLow
ElevationMedium
ElevationHigh
ElevationOverlay
```

阴影必须考虑：

- 渲染后端是否支持。
- 性能开销。
- 高 DPI。
- 深色模式。
- 浮层和背景的对比度。
- 阴影是否真的有助于表达层级。

不要为每个控件随意增加阴影。

---

## 5. 统一控件状态模型

现有控件应逐步接入统一状态模型。

### 5.1 基础状态

至少支持：

| 状态 | 说明 |
|---|---|
| Normal | 默认状态 |
| Hovered | 指针悬停 |
| Pressed | 正在按下 |
| Focused | 获得焦点 |
| KeyboardFocused | 键盘导航获得焦点 |
| Disabled | 不可交互 |
| Selected | 被选中 |
| Checked | 已勾选 |
| Indeterminate | 不确定状态 |
| Loading | 正在执行操作 |
| Error | 错误状态 |
| Warning | 警告状态 |
| ReadOnly | 只读状态 |
| Expanded | 展开状态 |
| Collapsed | 折叠状态 |

### 5.2 状态优先级

必须明确多个状态同时存在时的优先级。

例如：

```text
Disabled > Loading > Pressed > Focused > Hovered > Normal
```

上面的顺序只是示例，最终顺序应结合控件语义确定。

不要让状态优先级隐藏在多个 if/else 中。

### 5.3 状态测试

每个基础控件至少应验证：

- 鼠标进入和离开。
- 鼠标按下和释放。
- 点击取消。
- 键盘聚焦。
- Tab 导航。
- 禁用后状态清理。
- 快速连续操作。
- 触摸或其他输入方式。
- 控件移除或销毁时状态清理。

---

## 6. 动画与过渡系统

动效必须成为框架级能力，而不是每个控件独立实现一套计时器。

### 6.1 动画系统目标

至少支持：

- 数值 Tween。
- 颜色 Tween。
- 透明度 Tween。
- 位移 Tween。
- 尺寸 Tween。
- 圆角 Tween。
- 阴影或层级过渡。
- 多属性并行动画。
- 延迟。
- 重复。
- 取消。
- 反向。
- 动画完成回调。
- 动画冲突处理。
- 减少动效模式。

### 6.2 动画驱动方式

优先使用现有帧循环或统一调度器。

动画系统不应：

- 为每个控件创建无限制的独立线程。
- 在后台线程直接操作 UI 对象。
- 依赖具体渲染后端。
- 让动画对象阻止控件销毁。
- 在控件销毁后继续更新已失效对象。

### 6.3 Tween 抽象

可以参考以下概念：

```text
Animation<T>
    - StartValue
    - EndValue
    - Duration
    - Delay
    - Easing
    - Progress
    - Direction
    - RepeatCount
    - Cancellation
```

示例：

```text
animate(
    target = button.visual.backgroundColor,
    to = theme.colors.accentHover,
    duration = motion.fast,
    easing = easeOut
)
```

具体 API 必须适配现有语言和架构。

### 6.4 缓动函数

至少考虑：

```text
Linear
EaseIn
EaseOut
EaseInOut
CubicBezier
SmoothStep
Spring
```

对于桌面控件，默认动效应：

- 快速。
- 克制。
- 可预测。
- 不影响操作效率。
- 不造成明显延迟。
- 不使用夸张的弹跳。

### 6.5 建议的动效范围

以下仅为初始建议，最终需要结合实际体验调整：

| 场景 | 建议范围 |
|---|---:|
| Hover 颜色变化 | 100–180 ms |
| Pressed 反馈 | 60–120 ms |
| Focus Ring 变化 | 100–180 ms |
| Tooltip 出现 | 100–200 ms |
| 菜单出现 | 120–220 ms |
| 面板展开 | 160–280 ms |
| 页面切换 | 180–320 ms |
| 弹簧反馈 | 根据阻尼和质量调整 |

不要为了“有动效”而给所有属性添加动画。

### 6.6 减少动效

必须支持用户减少动画的偏好：

```text
MotionMode:
    Full
    Reduced
    None
```

在 Reduced 或 None 模式下：

- 缩短动画。
- 删除大幅位移。
- 删除夸张缩放。
- 保留必要的状态反馈。
- 保证操作结果仍然清晰。

---

## 7. 控件视觉改造范围

优先改造当前已经完成、使用频率最高的控件。

推荐顺序：

1. Button。
2. Text / Label。
3. TextField / Input。
4. Checkbox。
5. RadioButton。
6. Switch / Toggle。
7. Slider。
8. ProgressBar。
9. List / ListItem。
10. ScrollBar。
11. ComboBox / Dropdown。
12. Tabs。
13. Tooltip。
14. Dialog / Modal。
15. Menu / ContextMenu。
16. Toast / Notification。
17. TreeView。
18. DataGrid。

实际顺序必须根据当前项目已有控件调整。

### 7.1 Button 改造要求

至少支持：

- Primary、Secondary、Tertiary、Danger 等语义变体。
- Small、Medium、Large 等尺寸。
- Normal、Hovered、Pressed、Focused、Disabled、Loading。
- 左图标。
- 右图标。
- 仅图标按钮。
- 文本按钮。
- 圆角和边框变体。
- 键盘操作。
- 焦点环。
- 加载指示器。
- 颜色和背景平滑过渡。
- 按下反馈。
- 内容对齐。
- 文本溢出处理。

### 7.2 TextField 改造要求

至少支持：

- Normal。
- Hovered。
- Focused。
- Disabled。
- ReadOnly。
- Error。
- Warning。
- Placeholder。
- 前置和后置内容。
- 清除按钮。
- 密码输入（如果现有功能需要）。
- 多行输入（如果现有功能需要）。
- 光标和选区。
- 键盘导航。
- 文本选择。
- 验证信息。
- 焦点环和边框过渡。

### 7.3 List / ListItem 改造要求

至少支持：

- 悬停行。
- 当前选中行。
- 多选。
- 键盘导航。
- 禁用项。
- 分组。
- 图标和辅助文本。
- 行高和密度。
- 滚动性能。
- 大量数据下的视觉稳定性。
- 选中状态与焦点状态的区分。

### 7.4 Dialog / Overlay 改造要求

至少支持：

- 遮罩。
- 层级。
- 入场和退场动画。
- 焦点管理。
- 键盘关闭。
- 点击外部关闭策略。
- 过渡期间输入锁定。
- 多窗口或多层浮层场景。
- 减少动效模式。
- 无障碍语义。

---

## 8. 视觉树和绘制要求

### 8.1 绘制顺序

应建立明确的绘制顺序，例如：

```text
1. Layout
2. Clip
3. Background
4. Shadow / Elevation
5. Border
6. Focus Ring
7. Content
8. Overlay Feedback
9. Debug Visualization
```

实际顺序应根据现有渲染架构确定。

### 8.2 绘制后端隔离

视觉系统不得直接依赖：

- Skia 特有类型。
- Impeller 特有类型。
- 某个后端的纹理对象。
- 某个后端的动画实现。
- 某个后端的事件循环。

应通过项目现有的渲染抽象或新增最小必要的绘制接口完成。

### 8.3 像素和 DPI

必须考虑：

- 高 DPI。
- 非整数缩放。
- 像素对齐。
- 细边框。
- 文本基线。
- 圆角抗锯齿。
- 阴影裁剪。
- 不同平台字体差异。
- 1 px 线条在不同缩放下的表现。

### 8.4 性能要求

重点关注：

- 状态变化是否导致整个 UI 重绘。
- 动画期间是否产生不必要的布局。
- 阴影和模糊是否昂贵。
- 大量列表项是否重复创建视觉对象。
- 是否可以缓存静态绘制结果。
- 是否存在动画泄漏。
- 是否存在频繁分配。
- 是否存在每帧重复解析主题的问题。

视觉质量不能以明显的交互卡顿为代价。

---

## 9. Component Gallery 控件展厅

必须建立或完善一个独立的 Component Gallery。

### 9.1 展示内容

每个控件至少展示：

- 控件名称。
- 控件用途。
- 所有尺寸。
- 所有主要变体。
- 所有交互状态。
- 深色主题。
- 浅色主题。
- 禁用状态。
- 错误状态。
- 键盘焦点。
- 动效演示。
- 属性和令牌信息。
- 可复制的声明式 DSL 示例。

### 9.2 建议页面结构

```text
Component Gallery
├── Overview
├── Design Tokens
├── Colors
├── Typography
├── Buttons
├── Inputs
├── Selection Controls
├── Sliders
├── Lists
├── Data Display
├── Navigation
├── Menus
├── Dialogs
├── Notifications
├── Motion
├── Accessibility
├── Performance
└── Debug
```

### 9.3 状态展示模式

不要只展示一个可以点击的控件。

应提供：

```text
Interactive Preview
State Matrix
Theme Comparison
Size Comparison
Motion Preview
Keyboard Interaction
Accessibility Information
```

必要时增加“强制状态”模式，直接查看 Hover、Pressed、Focused、Disabled 等状态。

---

## 10. DSL 和公共 API 设计要求

如果项目有声明式 UI DSL，视觉系统应能够被 DSL 使用。

示意：

```text
Button(
    text = "Save",
    variant = Primary,
    size = Medium,
    enabled = true,
    loading = false
)
```

主题示意：

```text
Theme(
    colors = ...
    typography = ...
    spacing = ...
    radii = ...
    motion = ...
)
```

动画示意：

```text
Transition(
    property = BackgroundColor,
    duration = Fast,
    easing = EaseOut
)
```

要求：

- DSL 只表达语义和配置。
- 不让 DSL 直接暴露 Skia 或 Impeller 类型。
- 视觉令牌可由主题提供。
- 控件可以覆盖少量局部配置，但不应破坏统一主题。
- 公共 API 应保持简洁、稳定和可扩展。
- 不要为了未来可能的需求设计过度复杂的 DSL。

---

## 11. 测试要求

### 11.1 单元测试

至少测试：

- 状态解析。
- 状态优先级。
- 主题令牌解析。
- 颜色派生。
- 动画插值。
- 缓动函数。
- 动画取消。
- 动画反向。
- 动画完成。
- 减少动效模式。
- 控件销毁后的动画清理。
- 尺寸和密度计算。

### 11.2 集成测试

至少测试：

- 鼠标交互。
- 键盘交互。
- 焦点移动。
- 状态切换。
- 主题切换。
- 动画期间控件销毁。
- 快速重复点击。
- 弹窗和浮层。
- 列表选中和滚动。
- 高 DPI。
- 不同渲染后端。

### 11.3 视觉回归测试

如果现有项目具备截图或渲染测试能力，应增加：

- 基础控件截图。
- 各状态截图。
- 浅色和深色主题截图。
- 不同尺寸截图。
- 不同 DPI 截图。
- 动效关键帧截图。
- 渲染后端对比。

如果暂时没有视觉回归测试能力，应至少设计可扩展的测试接口，并在 Component Gallery 中提供稳定的状态展示。

### 11.4 性能测试

至少关注：

- 首次渲染时间。
- 控件状态切换耗时。
- 动画帧率。
- 大量控件同时动画。
- 大量列表项滚动。
- 内存分配。
- CPU 使用率。
- GPU 使用率（如果可测量）。
- 主题切换耗时。

---

## 12. 分阶段实施计划

### 阶段 0：现状审查

输出：

- 架构审查报告。
- 已有控件清单。
- 现有能力复用清单。
- 问题清单。
- 改造风险。
- 详细实施计划。

完成标准：

- 明确现有架构。
- 明确不应重写的部分。
- 明确第一批改造控件。
- 明确需要新增的最小抽象。

### 阶段 1：设计令牌

实现：

- 颜色令牌。
- 字体令牌。
- 间距令牌。
- 尺寸令牌。
- 圆角令牌。
- 边框令牌。
- 阴影令牌。
- 动效令牌。
- 浅色主题。
- 深色主题。

完成标准：

- 控件不再散落硬编码视觉参数。
- 主题可以统一切换。
- 令牌有测试和文档。

### 阶段 2：状态系统

实现：

- 统一交互状态。
- 状态优先级。
- 状态变化通知。
- 视觉状态解析。
- 状态调试工具。

完成标准：

- 基础控件可以统一处理 Hover、Pressed、Focused、Disabled。
- 状态逻辑可测试。
- 不同控件不再重复实现相同状态判断。

### 阶段 3：动画与过渡

实现：

- 统一动画时钟。
- Tween。
- 缓动。
- 颜色过渡。
- 属性过渡。
- 动画取消。
- 动画生命周期。
- Reduced Motion。

完成标准：

- 控件可以使用统一动画 API。
- 动画不依赖具体渲染后端。
- 动画不会泄漏或更新已销毁对象。
- 动效不会造成明显卡顿。

### 阶段 4：基础控件改造

优先改造：

- Button。
- TextField。
- Checkbox。
- Switch。
- Slider。
- ListItem。

完成标准：

- 控件视觉统一。
- 状态完整。
- 键盘和鼠标交互正常。
- 主题切换正常。
- 动效合理。
- 有测试和 Gallery 展示。

### 阶段 5：复杂控件改造

改造：

- List。
- ComboBox。
- Tabs。
- Tooltip。
- Dialog。
- Menu。
- TreeView。
- DataGrid。

完成标准：

- 复杂控件复用基础视觉系统。
- 浮层和焦点管理正确。
- 大量数据场景性能可接受。
- 动画和状态不影响功能。

### 阶段 6：质量收敛

完成：

- 视觉回归。
- 性能优化。
- 可访问性检查。
- API 清理。
- 文档补充。
- 示例完善。
- 代码重构。
- 跨平台验证。
- 多渲染后端验证。

---

## 13. 交付物要求

每个阶段必须交付：

1. 修改后的源代码。
2. 新增或修改的测试。
3. Component Gallery 示例。
4. API 或架构文档。
5. 变更说明。
6. 风险和已知问题。
7. 构建与测试结果。
8. 必要的截图或视觉对比。
9. 后续阶段建议。

最终至少应包含：

```text
docs/
├── control-visual-system.md
├── design-tokens.md
├── interaction-states.md
├── animation-system.md
├── component-gallery.md
├── visual-testing.md
└── accessibility.md
```

如果项目已有文档目录，应遵循现有命名和组织方式，不要重复创建同义文档。

---

## 14. 代码质量要求

必须遵守：

- 现有项目的代码风格。
- 现有命名约定。
- 现有错误处理方式。
- 现有线程模型。
- 现有内存管理方式。
- 现有模块边界。
- 现有测试约定。
- 现有构建和 CI 规则。

代码应满足：

- 高内聚。
- 低耦合。
- 可测试。
- 可扩展。
- 可调试。
- 可维护。
- 不过度设计。
- 不引入无必要依赖。
- 不隐藏生命周期问题。
- 不牺牲性能和可访问性。

---

## 15. 最终验收标准

完成后必须回答以下问题：

### 架构

- 是否建立了独立的控件视觉系统？
- 控件逻辑和视觉表现是否解耦？
- 视觉系统是否与渲染后端解耦？
- 是否复用了现有框架能力？
- 是否避免创建平行的重复体系？

### 视觉

- 控件是否具有统一设计语言？
- 是否有完整的状态视觉？
- 是否支持浅色和深色主题？
- 是否减少了硬编码颜色和尺寸？
- 圆角、边框、字体、间距和层级是否统一？

### 动效

- 是否有统一动画系统？
- 是否支持缓动和过渡？
- 是否处理动画取消和生命周期？
- 是否支持减少动效？
- 是否避免过度动画？

### 交互

- 鼠标操作是否正常？
- 键盘操作是否正常？
- 焦点是否清晰？
- 禁用、错误、加载等状态是否正确？
- 是否支持必要的可访问性语义？

### 工程质量

- 是否有单元测试？
- 是否有集成测试？
- 是否有视觉回归方案？
- 是否有 Component Gallery？
- 是否验证了性能？
- 是否验证了不同 DPI？
- 是否验证了不同渲染后端？
- 是否有完整构建和测试结果？

---

## 16. 给 GPT / Codex 的执行提示词

你现在是一名资深 GUI 框架架构师、交互设计工程师和商业桌面软件工程师。

请基于当前代码库，完成“控件视觉系统建设与现有控件品质提升”任务。

严格遵守以下执行顺序：

1. 先阅读项目文档、目录结构、构建文件、核心框架代码和现有控件。
2. 识别当前已经完成的控件，不要假设项目是空白工程。
3. 先输出架构审查报告和改造计划。
4. 找出可以复用的状态、主题、布局、渲染、动画和生命周期能力。
5. 设计最小可行的独立控件视觉系统。
6. 先实现 Design Tokens。
7. 再实现统一交互状态和视觉状态解析。
8. 再实现统一动画与过渡系统。
9. 选择第一批最重要的基础控件进行改造。
10. 建立 Component Gallery，展示所有状态、主题、尺寸和动效。
11. 为关键逻辑补充单元测试和集成测试。
12. 运行构建、测试和必要的性能验证。
13. 每完成一个阶段，都说明修改了什么、为什么这样设计、有哪些风险、如何验证。
14. 如果发现现有架构存在严重问题，先提出证据和迁移方案，不要直接大规模推翻。
15. 所有视觉参数优先通过主题和设计令牌管理。
16. 所有动画优先通过统一动画系统管理。
17. 所有绘制优先通过渲染抽象完成，不要把 Skia 或 Impeller 特有 API 泄漏到控件公共层。
18. 不要只做表面美化，要改善架构、状态、动效、可访问性、性能和可维护性。
19. 不要在没有运行验证的情况下声称任务完成。
20. 最终给出完整的变更总结、测试结果、截图或 Gallery 说明，以及后续建议。

请从“现状审查报告”开始，不要直接编写大规模代码。


---

## 17. 当前项目已实现的控件与组件范围

> 本节是本项目当前状态的事实基线。后续智能体必须以实际源码为准进行核对，不得把以下组件当作待从零开发的功能。

当前源码中的 `WidgetType` 共包含 **23 种**，定义位置：

```text
D:/prj/nono/lumen/include/lumen/core/widget.h:13
```

### 17.1 内容与交互控件：13 种

| WidgetType | 中文名称 | 视觉系统改造重点 |
|---|---|---|
| `Text` | 文本 | 字体、字号、字重、行高、颜色层级、文本溢出、禁用和辅助文本 |
| `Button` | 按钮 | 变体、状态、焦点、按压反馈、图标、加载、动效、无障碍 |
| `TextField` | 文本输入框 | 边框、焦点环、光标、选区、占位符、错误状态、辅助文本 |
| `Checkbox` | 复选框 | 勾选动画、不确定状态、焦点、禁用、标签对齐 |
| `Switch` | 开关 | 滑块位移、轨道颜色、开关状态过渡、键盘操作 |
| `Radio` | 单选框 | 选中指示器、焦点、禁用、组内状态和动画 |
| `Slider` | 滑块 | 轨道、填充、Thumb、悬停、拖拽、键盘调节、数值反馈 |
| `ProgressBar` | 进度条 | 进度过渡、确定/不确定状态、颜色语义、动画节奏 |
| `Dropdown` | 下拉选择 | 输入区域、箭头、浮层、选中项、键盘导航、焦点和定位 |
| `Tabs` | 页签 | 当前项、悬停、焦点、指示器动画、滚动和溢出 |
| `Tooltip` | 提示气泡 | 延迟、入场/退场、定位、遮挡、减少动效 |
| `Icon` | 图标 | 尺寸、颜色继承、对齐、状态颜色、图标资源回退 |
| `Image` | 图像 | 加载中、资源未就绪占位、失败回退、裁剪、圆角和过渡 |

#### Button 特别说明

当前 `Button` 已支持以下视觉变体：

```text
Filled
Tonal
Outline
Ghost
Danger
```

视觉系统改造时必须保留并统一管理这些变体。每个变体都应至少覆盖：

- Normal。
- Hovered。
- Pressed。
- Focused / KeyboardFocused。
- Disabled。
- Loading（如果当前按钮功能支持）。
- 图标与文本组合。
- 不同尺寸和密度。
- 浅色主题与深色主题。

不要把每种变体实现成互相独立、重复的按钮绘制逻辑。应通过统一的 Button 视觉状态解析器、设计令牌和视觉部件复用实现。

### 17.2 布局、容器与滚动组件：10 种

| WidgetType | 中文名称 | 视觉系统相关要求 |
|---|---|---|
| `Container` | 容器 | 背景、边框、圆角、内边距、裁剪、阴影、内容层级 |
| `Row` | 行布局 | 间距、对齐、布局稳定性，不应强行增加装饰 |
| `Column` | 列布局 | 间距、对齐、布局稳定性，不应强行增加装饰 |
| `Stack` | 堆叠布局 | 层级、裁剪、覆盖关系、浮层内容和命中测试 |
| `ScrollView` | 滚动视图 | 滚动状态、裁剪、滚动条、滚动反馈、性能 |
| `ListView` | 列表视图 | 行状态、选中、悬停、焦点、分组、滚动和复用 |
| `VirtualList` | 虚拟列表 | 大数据量下的视觉稳定性、复用、滚动性能和状态保持 |
| `Grid` | 网格布局 | 间距、选中项、悬停项、响应式尺寸、滚动协作 |
| `FocusScope` | 焦点域 | 焦点边界、键盘导航、焦点恢复、无障碍语义 |
| `ThemeScope` | 主题域 | 主题继承、局部主题、令牌覆盖、主题切换和刷新范围 |

#### 布局组件的特别约束

`Row`、`Column`、`Stack`、`Grid` 等布局组件的主要职责是布局和子节点组织，不应为了“视觉丰富”而自动添加背景、边框、阴影或动画。

只有在现有 API 明确支持装饰，或设计系统明确将其作为视觉容器时，才应增加对应视觉能力。

重点应放在：

- 布局测量和排列稳定。
- 主题和设计令牌的继承。
- 子控件视觉状态不被破坏。
- 动画期间布局不抖动。
- 高 DPI 和不同窗口尺寸下的表现。
- 命中测试、裁剪和绘制顺序正确。

---

## 18. 当前项目的组件级辅助能力

以下能力不一定对应独立的 `WidgetType`，但必须纳入视觉系统、交互系统和测试范围。

| 辅助能力 | 当前职责 | 视觉系统接入要求 |
|---|---|---|
| `FormController` | 表单校验 | 错误、警告、成功、必填、辅助说明、字段状态同步 |
| `makeDialog` | 模态对话框，带 barrier 和焦点域 | 遮罩、层级、入场/退场、焦点管理、键盘关闭和动画 |
| `NavigatorController` | 路由栈与返回处理 | 页面切换、路由过渡、返回反馈、焦点恢复、减少动效 |
| `DropdownController` | 下拉浮动菜单、键盘导航 | 浮层主题、选中项、悬停、焦点、定位、动画和关闭策略 |
| `Scrollbar` | 由滚动视口绘制的滚动条，不是独立 WidgetType | 滚动条轨道、Thumb、悬停、拖拽、自动隐藏、主题和性能 |

### 18.1 FormController

`FormController` 定义位置：

```text
D:/prj/nono/lumen/include/lumen/widgets/form.h:19
```

视觉系统必须支持表单字段状态与校验结果联动：

```text
Normal
Focused
Filled
Invalid
Warning
Valid
Disabled
ReadOnly
```

要求：

- 校验状态不能只通过日志或文本表达。
- 错误状态应能影响字段边框、焦点环、图标和辅助文本。
- 错误、警告和成功状态必须有明确的语义颜色。
- 辅助文本不能导致布局无意义地跳动。
- 校验状态变化可以有克制的过渡，但不能干扰输入。
- 表单校验应与具体渲染后端解耦。
- 必须测试键盘导航、提交、校验失败后的焦点定位和错误信息展示。

### 18.2 makeDialog

`makeDialog` 创建带 barrier 和焦点域的模态对话框。

视觉系统必须覆盖：

- Barrier 遮罩颜色和透明度。
- 对话框表面、圆角、边框和层级。
- 对话框入场和退场动画。
- 内容、标题、操作区的间距。
- 默认按钮和取消按钮的视觉层级。
- 焦点进入和焦点恢复。
- Escape 关闭策略（如果当前行为支持）。
- 点击遮罩关闭策略（如果当前行为支持）。
- 动画期间的输入处理。
- Reduced Motion 模式。
- 多层对话框或浮层的层级关系。

不要只给对话框添加缩放动画。应同时考虑遮罩过渡、焦点迁移、内容稳定性和关闭时的生命周期。

### 18.3 NavigatorController

`NavigatorController` 负责路由栈和返回处理。

视觉系统接入应优先采用可配置的页面过渡机制：

```text
NoTransition
Fade
Slide
SharedAxis（如果现有架构适合）
```

要求：

- 页面切换动画由统一动画系统驱动。
- 路由栈逻辑不应直接依赖渲染后端。
- 返回操作必须保持正确的焦点恢复。
- 动画期间不能出现旧页面和新页面同时接收输入的错误。
- 支持减少动效。
- 如果当前项目尚未支持页面过渡，应先实现最小、稳定的 Fade 或 NoTransition 方案，不要引入复杂转场框架。

### 18.4 DropdownController

`DropdownController` 负责下拉浮动菜单和键盘导航。

视觉系统必须处理：

- 下拉浮层的主题和层级。
- 菜单项 Normal、Hovered、Focused、Selected、Disabled。
- 键盘上下移动和确认。
- 当前选中项的视觉反馈。
- 浮层定位、边界检测和裁剪。
- 打开和关闭动画。
- 点击外部关闭。
- Escape 关闭。
- 焦点转移和焦点恢复。
- 大量菜单项的性能。
- 深色模式和高对比度扩展。

下拉菜单的视觉部件应与未来的 `Menu`、`ContextMenu` 等能力保持可复用，但不得为了未来功能提前创建复杂的无用抽象。

### 18.5 Scrollbar

当前 `Scrollbar` **不是独立的 `WidgetType`**，而是由滚动视口通过 `withScrollbar` 启用并绘制。

因此：

- 不要把 `Scrollbar` 强行加入 23 种 `WidgetType`。
- 不要为了视觉系统重构而将其无理由改成独立控件。
- 应将其视为 `ScrollView`、`ListView`、`VirtualList` 等滚动容器的附属视觉部件。
- 滚动条的绘制、状态和动画应通过统一视觉系统管理。

至少支持：

```text
Hidden
Visible
Hovered
Dragging
Disabled
```

需要评估和实现：

- 轨道是否显示。
- Thumb 的最小尺寸。
- 滚动条自动隐藏和显示。
- 鼠标悬停。
- Thumb 拖拽。
- 滚轮或触摸滚动后的显示反馈。
- 深色和浅色主题。
- 滚动条宽度和密度。
- 高 DPI。
- 大内容区域下的性能。
- 与内容裁剪、边距和命中测试的协调。

自动隐藏动画应有明确的生命周期管理，不能在滚动视口销毁后继续运行。

---

## 19. 基于现有 23 种 WidgetType 的改造优先级

以下是建议顺序，不是要求一次性完成所有控件。智能体必须结合源码实际完成情况调整，但应解释调整原因。

### P0：视觉基础与高频交互

```text
Text
Icon
Container
Button
TextField
Checkbox
Switch
Radio
```

目标：

- 建立设计令牌。
- 建立统一状态模型。
- 建立基础视觉部件。
- 建立基础动画和过渡。
- 先验证文本、图标、容器、按钮和输入控件的整体质感。

### P1：选择、反馈和滚动

```text
Slider
ProgressBar
Dropdown
Tabs
ScrollView
Scrollbar
ListView
```

目标：

- 验证轨道、Thumb、指示器、浮层和滚动条的视觉统一。
- 验证键盘导航、选中状态和滚动性能。
- 验证 DropdownController 与滚动/浮层系统的协作。

### P2：复杂容器和数据展示

```text
VirtualList
Grid
Stack
FocusScope
ThemeScope
Image
Tooltip
```

目标：

- 验证大数据量下的视觉稳定性。
- 验证主题继承和局部覆盖。
- 验证焦点域。
- 验证资源未就绪图像占位。
- 验证提示气泡和堆叠层级。

### P3：跨组件能力

```text
FormController
makeDialog
NavigatorController
DropdownController
```

目标：

- 验证表单校验状态与控件视觉联动。
- 验证模态对话框、barrier、焦点域和动效。
- 验证路由切换、返回和焦点恢复。
- 验证下拉浮层、键盘导航和定位。

---

## 20. 针对当前项目的实施边界

本项目已经实现了 23 种 `WidgetType`，因此本任务的重点是：

```text
已有控件
    -> 统一视觉状态
    -> 统一设计令牌
    -> 统一视觉部件
    -> 统一动画与过渡
    -> 统一主题
    -> 统一测试和 Gallery
```

而不是：

```text
重新设计一套全新的 WidgetType
重新实现所有控件功能
替换现有布局系统
替换现有事件系统
替换现有渲染后端
```

如果现有控件的内部实现确实阻碍视觉系统建设，应采用以下策略：

1. 先定位具体阻碍。
2. 给出最小改造方案。
3. 评估 API 和行为兼容性。
4. 先添加测试。
5. 再逐步迁移。
6. 保留旧行为的兼容路径，除非有明确理由移除。
7. 在变更记录中说明迁移影响。

---

## 21. 给智能体的现有控件核对任务

在开始实现视觉系统前，必须对照源码完成以下核对表：

- [ ] 确认 `WidgetType` 的实际枚举数量和名称。
- [ ] 确认 `widget.h` 中 `WidgetType` 的真实定义位置。
- [ ] 确认每种控件的构造方式和公共 API。
- [ ] 确认 Button 的五种变体是否全部已经可用。
- [ ] 确认 TextField 的文本输入、光标和选区能力。
- [ ] 确认 Checkbox、Switch、Radio 的状态存储方式。
- [ ] 确认 Slider 的拖拽和键盘调节方式。
- [ ] 确认 ProgressBar 是否支持确定和不确定进度。
- [ ] 确认 DropdownController 的浮层和键盘导航实现。
- [ ] 确认 Tabs 的选中和切换逻辑。
- [ ] 确认 Tooltip 的触发、延迟和定位方式。
- [ ] 确认 Image 资源未就绪时的占位绘制。
- [ ] 确认 ScrollView、ListView、VirtualList 的滚动和裁剪方式。
- [ ] 确认 `withScrollbar` 的实际实现。
- [ ] 确认 Scrollbar 是否支持自动隐藏和拖拽。
- [ ] 确认 FocusScope 的焦点边界和焦点恢复行为。
- [ ] 确认 ThemeScope 的主题继承和覆盖机制。
- [ ] 确认 FormController 的校验状态模型。
- [ ] 确认 makeDialog 的 barrier 和焦点域实现。
- [ ] 确认 NavigatorController 的路由栈和返回处理。
- [ ] 确认现有帧循环、动画时钟或调度机制。
- [ ] 确认现有渲染后端抽象和 Skia / Impeller 耦合点。
- [ ] 确认现有测试和 Demo 的运行方式。

核对结果必须写入审查报告，不能只在内部推测。

---

## 22. 更新后的最终验收要求

除前文通用验收标准外，必须额外满足：

### 现有控件覆盖

- 23 种 `WidgetType` 均有明确的视觉系统接入策略。
- 13 种内容与交互控件均有状态和主题设计。
- 10 种布局、容器与滚动组件均完成架构核对。
- Button 的 Filled、Tonal、Outline、Ghost、Danger 五种变体均得到覆盖。
- `Scrollbar` 按附属视觉部件处理，而不是强行变成独立 `WidgetType`。
- `FormController`、`makeDialog`、`NavigatorController`、`DropdownController` 均有对应的视觉和交互测试计划。

### 不破坏现有功能

- 不得因视觉改造破坏已有控件 API。
- 不得因动画导致输入、焦点、滚动或路由行为异常。
- 不得因主题切换破坏布局和文本测量。
- 不得因视觉部件拆分破坏命中测试。
- 不得因引入缓存导致状态或资源显示错误。
- 不得因自动隐藏、浮层或过渡导致对象生命周期泄漏。

### 交付报告

最终报告必须按以下结构输出：

```text
1. 现状审查结论
2. 23 种 WidgetType 核对结果
3. 辅助能力核对结果
4. 视觉系统架构
5. Design Tokens 设计
6. 状态系统设计
7. 动画系统设计
8. 已完成的控件改造
9. Component Gallery 展示
10. 测试结果
11. 性能结果
12. 兼容性风险
13. 未完成事项
14. 后续实施计划
```
