# Lumen 自绘控件视觉系统设计

> 文档状态：设计基线；V1/V2 已实施（见文末实施状态）
>
> 编写时间：2026-09
>
> 适用版本：v0.3 后续维护与 v0.4+ 视觉系统演进
>
> 当前平台范围（2026-09-14）：Windows/Linux/macOS 桌面；Android/iOS 暂不实现并冻结。
> V4 按桌面适配设计，历史移动实验内容不作为当前交付或验收要求。

## 1. 文档目的

Lumen 是 C++20 自绘 GUI 框架。当前控件已经具备基本的布局、交互和绘制能力，
但 Button、TextField、Checkbox、Switch 和 Dialog 的颜色、尺寸、圆角和状态表现
仍分散在 painter、布局代码和示例构建器中。新增控件时容易产生新的硬编码，主题切换
也不能完整覆盖控件外观。

本文定义 Lumen 的视觉系统和迁移方式，目标是让：

- 控件的颜色、字体、间距、尺寸、圆角、边框和状态来自同一套语义定义。
- 布局、绘制、命中区域、局部重绘和无障碍状态使用同一份最终样式。
- light、dark、高对比度、字体缩放、减少动画和触摸密度可以由同一套 Theme 派生。
- CPU、Skia 光栅、GPU 和三桌面 host 使用相同的控件视觉契约。
- 后续增加图标、阴影、动效和响应式布局时，不需要把平台类型带入 `lumen-core`。

## 2. 当前框架基线

### 2.1 已完成能力

按照当前 Git 历史和代码结构，以下能力已经进入项目基线：

| 区域 | 当前能力 |
| --- | --- |
| 核心树 | `Widget`、`Element`、`RenderNode`、稳定 identity、状态绑定和 UI 线程所有权 |
| 布局 | Box/Flex 子集、Row、Column、Stack、padding、margin、intrinsic、baseline、滚动视口 |
| 交互 | hit test、事件冒泡、Button 点击、TextField 焦点、键盘导航、滚轮/触摸滚动、tap/drag 区分 |
| 文本 | UTF-8 校验、grapheme、selection/composing、IME preedit、密码/只读/多行字段、文本布局缓存 |
| 渲染 | CPU 光栅、Skia 光栅、Skia Ganesh GPU、命令录制/回放、裁剪、图片资源、局部重绘和 GPU 回退 |
| 平台 | SDL3 桌面 host、`ApplicationHost`、`WindowId`、窗口指标、DPI、fake host、剪贴板和文本输入契约 |
| 无障碍 | 平台无关 `SemanticsTree`、role/label/value/actions、identity diff、Recording bridge 契约 |
| 应用组件 | ScrollView、ListView、Form、Checkbox、Switch、Dialog、FocusScope、Navigator、Theme |
| DSL | C++ builder 和受限 `.lumen` 文本 DSL，包含阶段 8D 组件与属性 |
| 示例 | counter 示例和展示滚动、表单、弹窗、导航、主题切换、无障碍的 settings 示例 |
| 验证 | Catch2 单元测试、无窗口集成测试、CPU 像素测试、命令回放测试、文本/语义/平台测试 |

阶段 0–6、7A–7E、8A–8E 的实现记录见自用路线图。历史 8E 还留下 SDL-free
`MobileHostSeam` 接缝；它不代表原生移动端支持，Android/iOS 原生胶水也不再列为
当前集成目标。视觉系统的后续设计和验收只面向三桌面。

### 2.2 当前视觉问题

编写本文时旧 [`Theme`](../include/lumen/widgets/theme.h)（已删除，见 §12
实施状态）已有页面背景、surface、文本、主色、
错误色、间距和排版 token，也能派生 light/dark 与部分无障碍设置。但它还不是完整的
控件主题：

- `painter.cpp` 中仍有按钮、输入框、Checkbox、Switch 的硬编码颜色和几何值。
- `themedButton()` 和 `themedTextField()` 目前主要设置字体，不能统一控件 chrome。
- `applyTheme()` 通过修改 `Widget` 注入默认值，与 `Widget` 的不可变描述定位不一致。
- 透明色和黑色文本同时承担“未设置”的含义，无法可靠表达有意使用透明或黑色。
- `PaintOptions` 只有部分交互状态，缺少 hover、disabled、invalid、selected 等统一状态。
- 布局中的控件最小尺寸和 painter 中的实际 padding、圆角、绘制尺寸来自不同常量。

## 3. 总体设计

视觉系统采用以下数据流：

```text
Primitive Tokens
    -> Semantic Tokens
    -> Component Tokens
    -> Theme + Widget + Interaction + Accessibility
    -> StyleResolver
    -> ResolvedStyle
    -> Layout / RenderNode / Painter
    -> RenderCommandList / Renderer
```

控件只声明语义、变体、尺寸和行为。控件不直接读取 SDL、Windows、Linux、macOS
或其他平台主题 API，也不直接依赖某一组原始颜色。

Lumen 默认提供一套跨平台视觉风格。平台适配层只负责提供系统主题、系统字体、显示
缩放、触摸能力、安全区、无障碍设置等输入。若未来需要接近某个平台的外观，应通过
`PlatformThemeAdapter` 生成 Theme 输入或独立 Theme provider，不能在控件 painter 中
增加平台分支。

这里的触摸能力用于桌面触屏，安全区是通用窗口可用区域指标；窄窗口、Touch density
和这些值类型的保留不构成 Android/iOS 适配要求。

### 3.1 Token 三层模型

#### Primitive Token

Primitive token 只描述调色板和基础尺度，不直接被控件使用。例如：

```text
blue.500
neutral.050 ... neutral.950
red.500
space.1 ... space.12
radius.1 ... radius.pill
duration.fast / duration.normal / duration.slow
```

#### Semantic Token

Semantic token 描述用户能理解的角色：

```text
color.background.page
color.background.surface
color.background.elevated
color.content.primary
color.content.secondary
color.content.onAccent
color.border.default
color.border.strong
color.focus.ring
color.selection.background
color.status.error
color.status.success
color.status.warning
color.disabled.background
color.disabled.content
color.scrim
```

控件使用 semantic token，而不是 `blue.500` 或 `neutral.700`。这样改变品牌色、深色
主题或高对比度规则时，不需要遍历控件代码。

#### Component Token

Component token 描述控件部件及状态：

```text
button.filled.background
button.filled.background.hover
button.filled.background.pressed
button.filled.content
button.outline.border
textfield.background
textfield.border
textfield.border.focused
textfield.border.invalid
checkbox.indicator.checked
switch.track.on
dialog.surface
dialog.scrim
```

组件 token 可以引用 semantic token，也可以通过状态计算得到最终值。应用只在真正
需要品牌定制时提供局部 override。

List 的组件 token 位于 `Theme.list`（集合控件设计 §10）：background 引用
surface，hovered 引用 surfaceSunken，pressed 为 surface/accent 的 0.32 混合，
selected 引用不透明 accentContainer；separator/content/disabledContent/emptyContent
分别引用 borderDefault/contentPrimary/disabledContent/contentSecondary。
selectionMarker 引用 accent，宽 3、上下 inset 4；空态图标 24。标记与图标随
fontScale 缩放一次；行高、padding、圆角由下表密度档派生。焦点环独立于选中条，
禁用选中行保留淡化指示条。高对比重新派生上述颜色，不能只靠选中底色传达状态。
`ListPart` 只用于 List 壳与空态。Tree 使用独立 `TreePart` 与 `Theme.tree`：
`tree.row` 与 List 采用相同的语义颜色映射和状态优先级，可独立覆盖；复用
`ListRowResolvedStyle` 绘制分隔线、选中条与焦点环。菜单、TreeList 和旧
ListView/VirtualList 保持原有表面样式。
Tree 的 `indentStep=20`、`chevronHitExtent=24`、`chevronIconSize=16` 来自
组件 token，三档密度下不变，随 fontScale 缩放一次。箭头前景取
`contentSecondary`，hover/pressed 取行正文色，禁用取 disabledContent。
行内 padding/间距/圆角跟随密度及 ControlSize；depth 只增加行左 padding，
背景、分隔线、选中条与焦点环始终覆盖完整行宽。叶节点保留等宽箭头槽。
文件行与空态图标使用目录 `IconId::Document` / `Folder`。

### 3.2 初始尺度

第一版视觉系统采用 4 logical px 基础网格，8 px 作为常用间距节奏。初始尺度如下，
所有值都属于 Theme，可由 density 派生：

| 项目 | Small/Compact | Medium/Comfortable | Large/Touch |
| --- | ---: | ---: | ---: |
| 控件最小高度 | 32 | 40 | 48 |
| Button 最小宽度 | 64 | 64 | 72 |
| TextField 最小宽度 | 96 | 120 | 144 |
| 水平内边距 | 8 | 12 | 16 |
| 控件间距 | 4 | 8 | 8 |
| 小部件圆角 | 4 | 6 | 8 |

卡片默认圆角为 8，Dialog 默认圆角为 12，pill 组件的圆角取高度的一半。桌面默认
使用 Medium density，桌面触屏或需要更大操作区域时可选择 Large/Touch。应用明确选择 density，
不能依赖窗口平台类型隐式改变颜色或组件语义。

## 4. Theme 模型

Theme 是可复制、不可变使用的值对象，由应用或窗口根节点拥有，不使用全局可变单例。
Theme 不保存控件实例状态；控件状态由交互系统提供给样式解析器。

建议的公共结构如下：

```cpp
struct Theme {
    ColorScheme colors;
    Typography typography;
    Metrics metrics;
    ElevationTokens elevation;
    MotionTokens motion;
    IconTheme icons;
    ButtonTokens button;
    TextFieldTokens textField;
    CheckboxTokens checkbox;
    SwitchTokens switchControl;
    DialogTokens dialog;
    ScrollbarTokens scrollbar;

    static Theme dark();
    static Theme light();
    static Theme fromSettings(
        const accessibility::AccessibilitySettings& settings,
        bool darkMode,
        ControlDensity density = ControlDensity::Comfortable);
};
```

Theme 必须支持：

- light/dark 主题切换。
- high contrast：提升正文、边框和焦点环对比度，同时保留状态可辨识性。
- font scale：同步放大排版、控件最小高度和相关间距，避免文字被控件裁剪。
- reduce animation：所有状态过渡和页面转场时长归零。
- device scale：只影响逻辑坐标到像素坐标的边界，不改变语义尺寸。
- safe area：由 host 提供，作为页面布局输入，不写入颜色或控件 token。

### 4.1 Theme 与模块边界

新增 `lumen-style` 模块：

- `lumen-core` 保存几何、Widget、RenderNode 和不依赖 Theme 的 `ResolvedStyle` 值类型。
- `lumen-style` 保存 Theme、token、状态集合和 `StyleResolver`，依赖 core 与 accessibility。
- `lumen-layout` 依赖 `lumen-style`，在布局前完成样式解析并使用样式指标测量。
- `lumen-render` 只读取 RenderNode 的 resolved style，不依赖 `lumen-widgets` 或 Theme。
- `lumen-widgets` 提供控件 builder、variant、Form、Dialog、Navigator 和 Theme 入口。
- `lumen-dsl` 解析控件 variant、size、enabled、invalid 和局部样式引用。

公共头文件继续禁止引入 SDL、Skia、Objective-C、Java/JNI 或其他平台 SDK 类型。

## 5. 样式状态和解析流程

交互层提供稳定 identity 对应的状态快照：

```cpp
struct WidgetState {
    bool hovered{false};
    bool pressed{false};
    bool focused{false};
    bool disabled{false};
    bool checked{false};
    bool invalid{false};
    bool selected{false};
};
```

状态解析规则固定如下：

1. `disabled` 优先级最高，禁用控件不响应点击、键盘激活或语义 action。
2. `invalid` 影响边框、辅助文本和语义状态，但不覆盖 disabled 的可用性语义。
3. `pressed` 覆盖 hover 的背景和前景状态。
4. `focused` 默认不绘制焦点环；仅当节点显式设置 `showFocusRing=true` 时绘制可见焦点环。实际焦点、键盘导航与语义状态不受绘制开关影响。该属性不区分输入来源（鼠标/键盘/语义聚焦一致生效）。
5. `checked` 和 `selected` 只影响有对应语义的控件。
6. 状态变化即使不改变布局，也必须改变 RenderNode 的 resolved style，以便 damage
   正确覆盖旧状态和新状态。

解析接口固定为：

```cpp
struct StyleContext {
    const Theme& theme;
    const InteractionStateSnapshot& interaction;
    const accessibility::AccessibilitySettings& accessibility;
    float deviceScale{1.0F};
};

[[nodiscard]] core::ResolvedStyle resolveStyle(
    const core::Widget& widget,
    const StyleContext& context);
```

`LayoutEngine::layout()` 接收 `StyleContext`，对每个节点解析一次样式，并将结果写入
`RenderNode`. `RenderNode::sameNode()` 必须比较 resolved style。Painter 不再从 Theme
或 Widget 推断默认颜色。

`PaintOptions` 保留 caret、selection、composition 等文字绘制瞬态数据；focused、
pressed、hovered 等控件 chrome 状态进入 `StyleContext` 和 resolved style。

布局优化不得跳过状态解析或应用构建回调。Row/Column 在交叉轴尺寸已确定、
且主轴预算不再依赖该子项的宽松测量时，可直接按最终 Stretch 约束布局子节点：
包括无 flex 分配、主轴显式固定的非 flex 子项，以及预算已分配的非 shrinkWrap flex 子项。
拉伸阶段约束未变时复用本次结果，避免嵌套容器重复展开整个子树。
交叉轴未确定、预算仍依赖宽松测量或约束确实变化时保留重新布局，换行、margin、
ThemeScope 和虚拟项几何必须保持原有结果。该优化不跨帧缓存 Widget 或 Theme。

窗口循环使用 `AppShell::paintFrame()` 提交绘制，不计算整帧校验哈希。
现有 `renderFrame()` 保留确定性返回值契约，在实际需要返回时计算并缓存
当前 CPU 帧哈希；新的提交使缓存失效，外部渲染器仍返回 0。
两种入口共用状态、布局、damage、动画和语义推送流程。
CPU 全帧及 damage 清屏按预乘 RGBA 像素批量填充；缓冲区尺寸不变时复用
存储，避免先清零再逐通道覆写。局部清屏仅触及裁剪后的 damage 区域。

## 6. 公共类型和 API 改造

这是一次明确的 API 重构，不保留旧的扁平 Theme 字段兼容层。`Theme` 名称保留，内部
改为分组 token 结构。

### 6.1 Widget 属性

`Widget` 增加或统一以下声明属性：

```cpp
enum class ButtonVariant { Filled, Tonal, Outline, Ghost, Danger };
enum class ControlSize { Small, Medium, Large };

struct StyleOverrides {
    std::optional<Color> background;
    std::optional<Color> foreground;
    std::optional<Color> border;
    std::optional<CornerRadius> radius;
    std::optional<EdgeInsets> padding;
    std::optional<TextStyle> text;
};
```

Button、TextField、Checkbox、Switch、Dialog 和 Container 通过 variant、control size、
enabled、invalid、selected 及 `StyleOverrides` 表达个性化需求。`Widget` 仍是 UI
描述值，不能被 Theme 应用过程原地修改。

`Widget.showFocusRing`（默认 `false`）控制当前节点的焦点环；需要可见环时用 C++
`withFocusRing(widget, true)` 显式开启，文本 DSL 写法为 `showFocusRing: true`。
设在 List/Tree/TreeList 视口上时传递到内部生成的行，Tree/TreeList 还传递到箭头。
普通容器不会把它继承到任意子控件；行内应用自建按钮等保持自己的设置。
默认关闭时鼠标与键盘聚焦均不绘制环，仅显式开启才绘制；hover、pressed、
selected、语义 focused 与焦点导航保持原契约。焦点环所需几何预留不随此
开关变化。该属性是绘制开关，`focus-visible` 输入来源策略仍为待办。
键盘重度表面（设置页、集合、菜单、对话框）如需键盘可见性应显式开启；
高对比模式下关闭环的表面仍须靠选中底色与形状区分，不得只靠颜色。
框架自建的键盘件不走应用 opt-in，构建期恒开启：`makeTabs` 为页签按钮
统一开环（Tab 停靠点、聚焦无其他指示；上下文设置覆盖子按钮自身声明），
`makeSlider` 开环（thumb 环是
§6.5 设计内焦点指示），Splitter 分隔条（布局期物化 chrome，3px accent
线由 focusWidth>0 驱动，关环则键盘聚焦不可见），`makeDialog` actions、
Dropdown 浮动菜单选项（Enter/Esc/Up/Down 的键盘激活目标）。

现有 `makeButton()`、`makeTextField()`、`makeCheckbox()`、`makeSwitch()` builder 改为
生成这些语义属性；`themedButton()`、`themedTextField()` 和 `applyTheme()` 删除。
settings 和 counter 直接使用新的 builder/variant API。

### 6.2 ResolvedStyle

`lumen-core` 新增 `core/style.h`，只包含不依赖 Theme 的最终样式值类型。建议使用
`std::variant` 保存控件专用样式：

```cpp
struct CommonResolvedStyle {
    Color background;
    Color foreground;
    Color border;
    Color focusRing;
    Color selection;
    CornerRadius radius;
    EdgeInsets padding;
    TextStyle text;
    float borderWidth{0.0F};
    float focusWidth{0.0F};
    float elevation{0.0F};
};

struct ButtonResolvedStyle { CommonResolvedStyle common; /* button parts */ };
struct TextFieldResolvedStyle { CommonResolvedStyle common; /* field parts */ };
struct CheckboxResolvedStyle { CommonResolvedStyle common; /* indicator parts */ };
struct SwitchResolvedStyle { CommonResolvedStyle common; /* track/knob parts */ };

using ComponentResolvedStyle = std::variant<
    CommonResolvedStyle,
    ButtonResolvedStyle,
    TextFieldResolvedStyle,
    CheckboxResolvedStyle,
    SwitchResolvedStyle>;

struct ResolvedStyle {
    ComponentResolvedStyle component;
    float minWidth{0.0F};
    float minHeight{0.0F};
    float controlGap{0.0F};
};
```

`RenderNode` 持有 `ResolvedStyle style`。布局使用 `minWidth`、`minHeight`、`padding`、
`text` 和组件部件尺寸；painter 使用颜色、圆角、边框、焦点环和部件几何。

### 6.3 应用入口

应用拥有 Theme 并在每次构建/布局帧提供 `StyleContext`：

```cpp
class SettingsApp {
    style::Theme theme_;
    style::InteractionStateSnapshot interaction_;
};

auto renderTree = layout::LayoutEngine::layout(
    rootWidget, constraints, style::StyleContext{theme_, interaction_, settings});
```

不引入全局 Theme 单例。局部 ThemeScope 作为后续扩展；第一版只支持窗口根 Theme，
以保证当前 Widget/Element 结构简单且可预测。

## 7. 控件视觉契约

### 7.1 Button

支持 Filled、Tonal、Outline、Ghost、Danger 五种 variant。每种 variant 定义 rest、
hovered、pressed、focused、disabled 状态，文本颜色、背景、边框、焦点环、最小尺寸
和圆角必须来自 ButtonTokens。

Button 的文本始终使用 `Typography.label` 或控件指定的文本 override。Button 的命中
区域至少覆盖 resolved 控件尺寸，视觉内容居中，不能使用 painter 私有的固定 padding。

### 7.2 TextField

统一处理 rest、focused、invalid、readonly、disabled 状态。背景、边框、焦点环、
placeholder、selection、caret、preedit underline 和错误提示使用 TextFieldTokens。

文本布局和控件尺寸必须使用同一个 resolved `TextStyle`。字体缩放、多行、CJK、emoji、
RTL 和 IME preedit 不得导致文本溢出控件边界。

### 7.3 Checkbox 和 Switch

Checkbox 定义 indicator、check mark、label gap 和 focus ring；Switch 定义 track、knob、
on/off、disabled 和 focus ring。18x18、36x20 等当前尺寸不再由 painter 固定，统一
改为组件 token，并随 control size 派生。

checked 状态必须同时影响视觉和 semantics。禁用状态必须同时影响视觉、hit test、键盘
激活和 semantics flags。

### 7.4 Dialog、Card 和滚动条

Dialog 使用独立的 scrim、surface、elevation、圆角、内边距、标题排版和操作区间距。
Escape/返回键、modal barrier、焦点恢复和关闭 action 保持当前 8D 行为。

Card 使用 surface/elevated surface 和 elevation token。ScrollView/ListView 的滚动条
使用 scrollbar token，定义 rest、hovered、dragged 和 disabled 状态。

滚动条是覆盖在内容上方的附属部件，绘制与命中共享 `ScrollbarGeometry`，
适用于 ScrollView/ListView/VirtualList/List/Tree/TreeList 及菜单、下拉和弹窗视口。
Compact/Comfortable/Touch 的命中轨道厚度为 12/16/24，可视滑块厚度为 6/8/10，
悬停或拖动厚度为 8/10/12；默认桌面档为 16px 命中、8px 可视、10px 活动状态。
两轴同值并随字体缩放一次，DPI 在渲染边界换算；minLength=24、inset=4 独立取 token，
悬停加粗不改变滑块长度、滚动映射和内容布局。短视口几何夹取到视口内。
rest/hovered/dragged/disabled 分别取 borderStrong/contentPrimary/accent/disabledContent。
进入滑块的完整命中区域即显示 PointingHand，按住拖出轨道仍保持手形和拖动状态；
松开、取消、隐藏、禁用或内容不再溢出时恢复正确光标。滑块拖动无 slop、无释放惯性，
轨道空白处单击向该方向翻页；滚动条优先于底下的按钮、文字和集合行命中。
无溢出不显示；auto-hide 仍为预留。具体输入与验收见 `lumen-scroll-design.md` §4/§5/§7。

## 8. 图标、阴影、动效和响应式扩展

第一轮代码先实现颜色、排版、尺寸、圆角、边框、焦点环和控件状态；完整设计系统
预先冻结以下扩展契约：

- `IconId` 和 `IconTheme`：图标由语义 ID 表达，颜色默认继承 `foreground`，不把 SVG
  文件路径写入控件逻辑。Renderer 后续增加 vector path 或统一 image 适配。描边权重
  以 16px 基准档定：`strokeWidth = 1.8`（对齐设计稿 1.7–2.0；1.5 在 16px + AA 下
  偏细发糊，2026-09 调整）；圆弧几何以 ≥24 段折线逼近，多段图元（如 Search 的
  镜圆与手柄）在相接处共享端点（不得留缝）。图标盒原点按设备像素取整
  （`drawIcon` 内 `lround`，与字形 `toPixel` 同口径；CPU/Skia 双后端同式，
  尺寸不变）——逻辑居中常给出半像素原点（如 Spin 奇数高半格的 chevron），
  不取整则描边虚散、上下不对称（2026-09 Spin 像素复现，回归见 spin_tests
  `spin_stepper_chevrons_align_to_device_pixels`）。
- `ElevationTokens`：保存层级、阴影颜色、偏移和模糊半径。三后端共用同一
  `DrawShadow` 命令：Skia/GPU 按 `kNormal_SkBlurStyle`（σ = blur×0.5×scale）
  模糊；CPU 以 3-pass 可分离 box blur 近似同一 σ（blur=0 时退偏移扁平面），
  damage 口径按 `blur×2+1` 外扩覆盖模糊尾。Renderer 完全不支持阴影的平台，
  组件可退回边框/表面层级表达，不得在控件中散落阴影常量。
- `MotionTokens`：保存状态过渡、Dialog、Navigator 的时长和曲线。`reduceAnimation`
  将所有时长解析为零，并继续使用现有 FrameScheduler 的可访问性规则。
- 响应式布局：Theme 提供 density 和组件尺度，窗口宽度断点由 layout/style context
  提供。颜色和语义不因断点改变；只调整排列、间距、控件尺寸和 Dialog 宽度。
- 平台主题输入：`PlatformThemeAdapter` 只转换系统 dark mode、accent color、字体、
  contrast 和 touch capability，不直接返回平台控件对象。

## 9. 分阶段实施路线

### V1：样式基础和当前控件迁移

- 新增 `lumen-style` 和 `core/style.h`。
- 重构 Theme 为 ColorScheme、Typography、Metrics 和组件 token。
- 增加 Widget variant、control size、enabled、invalid、selected 和 StyleOverrides。
- 修改 LayoutEngine，在布局前解析样式并写入 RenderNode。
- 修改 RenderNode diff、damage 和 painter，删除 painter 中的控件硬编码常量。
- 删除 `applyTheme()` 和旧 themed helper，更新 counter、settings、Dialog 与 DSL。

出口条件：Button、TextField、Checkbox、Switch、Dialog 的外观由 Theme 完整控制；布局
和 painter 使用同一份尺寸与字体；CPU/Skia/GPU 命令路径无需知道 Theme。

### V2：状态、交互和无障碍联动

- 扩展 InteractionController 的 hover、disabled、invalid、selected 状态快照。
- 将 focus ring、pressed、checked、invalid 统一交给 StyleResolver。
- 主题切换、font scale、high contrast、density 和 reduced motion 触发正确的布局、
  语义和重绘更新。
- 确保状态样式变化能被 RenderNode diff 和局部 damage 正确发现。

出口条件：状态视觉、hit test、键盘行为和 SemanticsTree 对同一控件状态保持一致。

### V3：完整设计系统

- 接入 IconId/IconTheme、vector path 或统一图标资源。
- 增加 shadow/elevation 绘制能力和 RenderCommand 支持。
- 接入 MotionTokens、控件状态过渡、Dialog 转场和 Navigator 转场。
- 增加 ThemeScope，用于局部子树主题覆盖。
- 为 settings 示例增加完整的控件展示页和主题调试页。

### V4：三桌面主题适配与窗口可用性

- 为 Windows/Linux/macOS 提供 PlatformThemeAdapter 输入映射：系统外观、字体、
  缩放、对比度、减少动画和桌面输入能力；不可用项按能力报告降级。
- 验证桌面窗口 resize、DPI、窄窗口、不同 density 和窗口可用区域下的布局与命中。
- `lumen-style` 保持不依赖 SDL、Skia 或桌面会话的公共契约，支持确定性 headless 测试。
- 禁止把平台 `#ifdef` 扩散到 core、layout、style 和 widget painter。
- Android/iOS 主题映射、移动安全区策略和软键盘布局不在本阶段范围；M9 已冻结，不产生实现任务。

出口条件：三桌面主题输入与降级行为可验证，布局、视觉、焦点和语义一致；
不以移动 host 或设备测试作为完成条件。

## 10. 测试和验收

### 10.1 单元测试

- Primitive 到 semantic、semantic 到 component token 的映射。
- light/dark/high contrast/font scale/density/reduced motion 派生。
- 每个 Button variant 的 rest、hover、pressed、focused、disabled 解析。
- TextField 的 focused、invalid、readonly、disabled 解析。
- Checkbox/Switch 的 checked、focused、disabled 解析。
- StyleOverrides 的字段级覆盖和状态优先级。
- `ResolvedStyle` 写入 RenderNode、`sameNode()` 比较和 identity 稳定性。

### 10.2 布局和渲染测试

- Theme 指标变化后控件最小尺寸、padding、字体、圆角和布局结果正确。
- 320px 桌面窄窗口、连续 resize、font scale、桌面 Touch density 和窗口可用区域下内容可用。
- 深色/浅色/高对比度场景的 CPU headless 像素结果稳定。
- CPU、Skia 光栅和 GPU/CPU 回退录制相同语义样式的命令。
- hover、pressed、focused、invalid、checked 状态变化的局部重绘与全量重绘像素一致。
- 主题切换覆盖旧颜色和新颜色的 damage 区域，不残留上一帧控件外观。

### 10.3 交互和无障碍测试

- disabled 控件不响应 pointer、keyboard 或 semantics activate/setValue。
- 显式开启焦点环的控件在键盘焦点和语义焦点下可见且不会影响布局尺寸；默认关闭时焦点导航与语义 focused 保持可用，仅不绘制环。
- Checkbox/Switch 的 checked 视觉和 semantics flags 同步。
- TextField invalid 状态与 FormController 错误信息同步。
- Dialog 关闭、Escape/返回键、焦点恢复和 modal barrier 保持当前契约。
- Theme 切换不丢失 StateStore、Element、文本选区、IME preedit、滚动位置和 Navigator 栈。

### 10.4 构建和集成验收

按项目指南依次执行：

```sh
cmake -S . -B build -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

另外验证：

- `LUMEN_ENABLE_SKIA=ON` 的 CPU/Skia 一致性测试。
- GPU 可用、GPU 初始化失败回退 CPU、surface 重建和软件呈现失败路径。
- counter/settings 的桌面窗口 smoke 和 headless 场景。
- V4 变更前后检查桌面 CMake 依赖、测试集合和 README/支持矩阵描述没有过期。

现有 `LUMEN_BUILD_MOBILE_CORE=ON` 配置及 CI 仅可用于历史 SDL-free 实验代码的
兼容性检查，不属于 V4 的移动产品验收，也不要求或触发新增模拟器、真机或移动实现任务。

## 11. 迁移约束和风险

### 11.1 圆角 chrome 作者规则（2026-09，框架化"角部例外"）

自绘圆角容器（无边框窗口的标题栏/状态栏、卡片壳）内的**贴角子件**（caption
按钮、状态栏右缘 Toggle、任何填充到容器边缘的子节点）不得以方形填充越出
圆角——两层防线（lumen-titlebar-design §5"角部例外"）：

1. **首选 `withRoundedClip(container)`**（`Widget.clipRounded` + 容器自身
   `radius`）：子树绘制（含自身表面）按节点圆角门控（painter
   `ScopedRoundedClip` → `ClipRounded` 命令；CPU 为 SDF 覆盖率乘子、Skia
   为 clipRRect、其他后端默认降级矩形裁剪）。这是 CSS `overflow:hidden`
   圆角容器的框架等价物——子件无需各自记得带角半径。
2. **角部子件半径跟随**（显式单角半径，如 caption close 的
   `{0, 窗口圆角, 0, 0}`）：与裁剪叠加作为第二防线；窗口圆角变化
   （最大化归零）时两处同步。

ClipRounded 自 v6 引入；当前命令写 v7（包含图片 alpha 模式），兼容读 v6。
damage 经 `sameNode` 的 `clipRounded` 字段感知开关变化。透明呈现按
`PixelBuffer.alphaMode` 选择直提或兼容转换，详见 `lumen-titlebar-design.md`
§16 和 `lumen-premultiplied-alpha-rendering-plan.md` §3.5；宿主逐像素透明
支持须独立验证，不能将软件 surface 更新成功等同于合成器保留 alpha。

预乘迁移未改变 Theme 的直通颜色、圆角/阴影几何或 coverage 规则。CPU 内部帧和图片缓存
现为预乘/不透明表示，Gallery 应用默认导出仍为直通；接口用法见
[alpha 迁移说明](lumen-alpha-migration.md)。两主题/三 DPI 的 Gallery 与系统字体对照、
逐帧哈希解释和冻结容差见 [P4 验收](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)，
不能把表示迁移作为修改视觉 token 或刷新未解释 golden 的理由。

- 一次性重构允许删除旧扁平 Theme 字段和 themed helper；同一提交内必须更新示例、
  测试、DSL 和文档，保持构建可用。
- `ResolvedStyle` 必须是值类型，不能保存 Theme、SDL、Skia 或平台对象指针。
- StyleResolver 不得修改 Widget；Widget 仍是声明式输入，RenderNode 是本帧布局和绘制
  的不可变结果。
- 控件几何只能由 layout 使用的 resolved metrics 决定，painter 不得重新计算控件尺寸。
- 状态视觉变化必须进入 RenderNode diff，否则 Preserve/damage 模式会留下旧像素。
- 颜色对比度、焦点环和 disabled 状态必须在 high contrast 下仍可区分，不能只依赖颜色。关闭焦点环的表面仍须靠选中底色与形状区分；键盘重度表面应显式开启焦点环。
- 图标、阴影和动效扩展不得改变现有 `RenderCommandList` 的 CPU/Skia/GPU 回退不变量。
- Theme 只接收 host 提供的能力和指标，不接触 native handle；不为已冻结的移动平台增加专属契约。

## 12. 实施状态（2026-09 追记）

V1（样式基础和当前控件迁移）与 V2（状态、交互和无障碍联动）已实施：

- `lumen-style` 模块（`include/lumen/style/`、`src/style/`）与 `core/style.h`
  的 `ResolvedStyle` 值类型已落地；Theme 为分组 token 结构，primitive →
  semantic → component 映射函数可测（`tests/style_tests.cpp`）。
- `LayoutEngine::layout()` 接收 `StyleContext`（保留无上下文的便捷重载，
  默认暗色 Theme），identity 在遍历中按 `assignIdentities` 同规则分配。
- RenderNode 持有 resolved style 并参与 `sameNode()`/damage；hover、
  pressed、focused、disabled、checked、invalid 折算进样式；焦点环内嵌
  绘制（damage 不变量：控件绘制不越出节点矩形）。
- `applyTheme()`/themed helper 已删除；counter/settings 使用语义属性 +
  `StyleOverrides`；DSL 支持 `variant/size/enabled/invalid/selected`。
- §8 的扩展契约以 token 先行冻结：`IconTheme`/`IconId`、`ElevationTokens`、
  `MotionTokens`（reduceAnimation 归零）已入 Theme；图标/阴影绘制、状态
  过渡动画、ThemeScope 与 `PlatformThemeAdapter` 留待 V3/V4。
- 命令序列化升级 v2 以携带完整 TextStyle（resolved 样式带 weight/family）。
- V1/V2 当时的验收记录：Windows CPU Debug 为 303 个用例，SDL-free mobile-core 为
  291 个用例；counter/settings headless 与窗口 smoke 正常。Skia Release 还需
  通过 `skia_paints_counter_frame_consistently` 后才能作为完整后端门槛。
- 焦点环默认关闭（2026-09-19 追记）：§5/§6.1 改为默认不绘制、显式
  `showFocusRing=true` 才绘制。代码已同步（`widget.h:306` 默认 `false`；
  集合/树生成行继承视口设置；`gallery` 状态矩阵与焦点语义测试显式开启；
  `gallery` 集合开关默认关闭）。`design/gallery.html` 已同步为默认无环 +
  开关对比。键盘重度表面的开启落点：`makeDialog` 对 actions 子树显式
  开启（动作按钮是 Enter/Esc 的键盘激活目标），Dropdown 浮动菜单选项
  显式开启（Up/Down 高亮行即 focusNode 目标，Ghost rest 底透明、环是
  唯一指示），`makeTabs` 为页签按钮统一开启（Tab 停靠点无其他聚焦
  指示），`makeSlider` 开启（§6.5 thumb 环），Splitter 分隔条布局期
  恒开启（3px accent 线由 focusWidth>0 驱动），TreeList chevron 与
  Tree 箭头同契约随视口传递（chevron 是独立 Tab 停靠点），settings
  示例页 radio/switch/checkbox 显式开启（无其他焦点指示）；正文与
  常规表面保持默认关闭。
  保持默认关闭。

以上为历史实施记录，后续实现状态以自用路线图为准。mobile-core 数量只说明当时
通用实验配置的验证情况，不代表 Android/iOS 支持或本次复测结果。
