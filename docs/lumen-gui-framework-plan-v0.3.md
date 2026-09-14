# Lumen：C++20 自绘 GUI 框架 v0.3 桌面产品化与应用基础计划

> 后续面向自用工具类应用的桌面路线图见
> [`lumen-self-use-roadmap.md`](lumen-self-use-roadmap.md)。

> 文档状态：历史阶段设计，2026-09-14 按桌面范围修订；实现状态以自用路线图和支持矩阵为准。
> 当前范围：Windows/Linux/macOS。Android/iOS 暂不考虑，原 8E 移动目标和 v0.4 移动接入承诺取消；M9 暂缓。
> 上一版本：[v0.2 桌面 GPU 与性能工程计划](lumen-gui-framework-plan-v0.2.md)
> 适用基线：[初始框架计划](lumen-gui-framework-plan.md)

## 1. 版本定位与基线判断

v0.2 已经把 Lumen 从“能够绘制控件的原型”推进到“可测量、可回退、可发布的
桌面渲染运行时”。当前基线包括：

- `Widget`、`Element`、`RenderNode`、Box/Flex 布局、状态绑定和文本 DSL。
- CPU 光栅、Skia 光栅、Skia Ganesh GPU，以及 GPU 初始化失败时的 CPU 回退。
- `RenderCommandList`、局部重绘、`FrameScheduler`、异步图片资源和资源代际句柄。
- Windows/Linux SDL3 窗口、DPI/resize、文本输入、基础手势、热重载、诊断和 CI。
- 无窗口 counter 集成测试、CPU/Skia 像素测试、命令回放测试和固定场景基准。

提交本计划时，CPU 构建的 117 个测试和 Skia 构建的 121 个测试均已通过。这个
结果说明渲染运行时的主链路已经稳定，下一阶段继续增加同类绘制命令的收益较低。

当前真正限制 Lumen 成为跨平台 GUI 工程的部分有四类：

1. 平台契约仍以单窗口 SDL3 桌面循环为中心，缺少统一的应用生命周期、窗口标识、
   剪贴板、指针设备、键盘修饰键和桌面挂起/恢复语义。
2. 文本输入已经能提交 UTF-8 和 IME preedit 事件，但编辑模型按 code point 工作，
   没有字形 shaping、字体回退、双向文字、选区、剪贴板和完整候选词定位。
3. `RenderNode` 没有独立的语义树，因此键盘导航、屏幕阅读器、无障碍操作和高对比
   设置无法在平台之间复用。
4. 只有 Container、Row、Column、Stack、Text、Button、TextField，缺少滚动、列表、
   表单、弹窗和导航；真实应用无法在窗口尺寸变化后保持可用的内容结构。

因此 v0.3 的主线不是再添加一个后端，而是建立“同一棵 UI 树在不同平台上拥有一致
输入、文本、语义和生命周期”的应用基础。渲染后端继续沿用 v0.2 的能力报告和 CPU
回退规则。

## 2. 支持目标、范围和版本边界

### 2.1 v0.3 目标平台

| 平台 | v0.3 目标 | 渲染承诺 | 验收方式 |
| --- | --- | --- | --- |
| Windows | 保持桌面支持，补齐输入、剪贴板和 UI Automation 适配 | CPU、Skia 光栅、现有 GPU 路径 | CI + 窗口 smoke + 无障碍结构测试 |
| Linux | 保持 X11/Wayland 兼容，补齐 IBus/Fcitx、剪贴板和 AT-SPI 适配 | CPU、Skia 光栅、现有 GPU 路径 | CI/Xvfb 或 Wayland smoke + headless |
| macOS | 新增桌面窗口、输入、剪贴板、字体和 NSAccessibility 适配 | CPU、Skia 光栅；GPU 失败时必须回退 CPU | macOS CI + counter/settings smoke |

Android/iOS 不在当前目标平台中，不安排原生 host、最小移动页面、软键盘、移动
字体/GPU、无障碍或发布任务。已有 `MobileHostSeam` 和 SDL-free 配置保留为历史
实验资产；其通用 headless 验证不等于移动设备支持，也不构成本版本出口条件。

### 2.2 目标

- 把窗口、输入、文本编辑、剪贴板、生命周期和显示指标抽象成可测试的平台服务。
- 让 TextField 在 CJK、emoji、组合字符、双向文字和 IME preedit 下保持正确的选区、
  光标和候选词锚点。
- 从同一棵 RenderNode/Widget 树生成平台无关的语义树，并接入 Windows、Linux、
  macOS 的最小无障碍桥接。
- 提供 ScrollView、ListView、Form、Dialog、Navigator、Checkbox/Switch 等真实应用
  必需的基础组件，并统一键盘、鼠标、桌面触屏、滚轮和焦点行为。
- 保持 v0.2 的 CPU-only、Skia 光栅、GPU 回退、确定性 headless 和性能基线。
- 形成 Windows/Linux/macOS 的桌面发布矩阵。

### 2.3 非目标

- 不追求 Flutter 或 CSS API 兼容，不在 v0.3 引入脚本语言、插件市场或跨线程 UI 树。
- 暂不设计 Android/iOS 平台接入或移动专属能力，也不把它们预排到 v0.4。
- 不同时实现 Graphite、Metal、Vulkan 和自定义合成器；macOS GPU 先保持可选，失败
  时走 CPU/Skia 光栅。
- 不在本版本完成完整富文本编辑器、表格、虚拟化 Sliver 系统、WebAssembly 或 3D。
- 不把平台无障碍 API、Objective-C/Java/JNI、SDL 类型放进 `lumen-core`、`lumen-layout`
  或 `lumen-dsl` 的公共头文件。

### 2.4 必须保持的不变量

- UI 树和 `StateStore` 仍只由 UI 线程拥有；后台线程只能提交不可变数据和完成通知。
- 事件先归一化为 Lumen 类型，再进入交互、焦点、状态和重绘流程；平台层不得直接
  调用 Widget 回调。
- 所有窗口都有稳定 `WindowId`，事件、资源上传、语义节点和诊断都能关联到窗口。
- TextLayout 的逻辑坐标和 Renderer 的像素坐标继续由 `deviceScale` 明确分隔。
- GPU、字体、无障碍或系统服务不可用时，应用状态不能丢失；能力必须可查询并有安全
  的降级行为。
- CPU-only 构建不依赖 Skia、平台 SDK 的实现库或运行中的桌面会话。

## 3. 架构调整

### 3.1 平台宿主与应用生命周期

将当前 `PlatformWindow` 的窗口、事件和呈现职责拆成三个稳定边界：

- `ApplicationHost`：初始化/退出、事件循环、后台/前台、挂起/恢复、系统主题和
  全局服务拥有者。
- `WindowManager`/`PlatformWindow`：创建、销毁、显示、激活、最小化、窗口尺寸、
  drawable size、显示器/DPI、安全区和 `WindowId`。
- `PlatformServices`：`Clipboard`、桌面 `TextInputSession`、鼠标光标、
  文件选择器（先定义接口，不在 v0.3 实现完整对话框）。

建议的值类型如下，具体命名可以在 8A 评审后冻结：

```cpp
struct WindowId { std::uint64_t value{0}; };

struct WindowMetrics {
    core::Size logicalSize{};
    core::Size drawableSize{};
    core::EdgeInsets safeArea{};
    float deviceScale{1.0F};
    bool visible{true};
    bool minimized{false};
};

enum class AppLifecycle { Launching, Active, Inactive, Background,
                          Suspended, Terminating };
```

`Event` 需要补充时间戳、`WindowId`、键盘修饰键、逻辑键/物理键、指针设备类型、
pointer id、滚轮/桌面触屏增量、取消事件和窗口焦点变化。桌面 host 只负责填充
这些字段；交互控制器负责点击、拖动、滚动、焦点和激活。`safeArea` 保留为通用
窗口可用区域指标，桌面触屏与此指标均不代表移动端设计目标。

窗口重建、DPI 变化和应用恢复必须先更新 `WindowMetrics`，再请求 FrameScheduler；
不能在旧 surface 上提交新尺寸的命令。桌面渲染 surface 重建期间保留状态树，
只暂停提交并在 surface 可用后调用 `Renderer::resetSurface()`。

### 3.2 文本、字体和编辑模型

新增独立的 `lumen-text` 契约，避免把 Skia 类型扩散到 Widget 或 DSL：

- `FontManager`：字体族、weight/style、字体回退、字体资源生命周期和系统字体查询。
- `TextLayout`：按约束测量、分段、换行、ellipsis、baseline、字形位置和命中测试。
- `GlyphRun`/`TextMetrics`：Renderer 只消费已经确定的字形或后端可复现的文本布局结果。
- `TextEditingValue`：文本、selection、composing range；范围以 UTF-8 字节偏移之外的
  稳定 grapheme cluster 索引表示，平台转换只发生在适配层。

`TextStyle` 至少增加 family、weight、italic、letter spacing、line height、direction、
maxLines 和 overflow。布局和绘制必须共用同一份 `TextLayout`，否则光标、选区和绘制
宽度会在不同后端漂移。

首期使用已有 Skia 的字体和 shaping 能力实现桌面正式路径；CPU-only 继续提供确定性的
基础测量和有限字形回退，不能因为缺少 Skia 而无法编译或测试。UTF-8 校验、grapheme
边界、双向段落、字体缺字回退和换行规则要有独立 headless 测试，像素快照只验证
Renderer 的绘制，不把系统字体的细微差异当成跨机器失败。

TextField 需要补齐：

- 光标左右移动、按 grapheme 删除、Home/End、Shift 选区、Ctrl/Command 快捷键。
- 点击定位光标、拖动选区、双击词选中、横向滚动到光标和选区绘制。
- `TextInputSession` 的 commit、preedit、selection、候选词矩形和取消语义。
- 剪贴板复制、剪切、粘贴；密码/只读/多行模式的最小属性。

### 3.3 语义树和无障碍

在 RenderNode 之外建立 `SemanticsTree`，它描述用户能感知和操作的对象，不暴露绘制
命令。节点至少包含：

```cpp
enum class SemanticsRole { Window, Group, Text, Button, TextField, Checkbox,
                           Switch, List, ListItem, Dialog, Image };

struct SemanticsNode {
    std::string id;
    SemanticsRole role{SemanticsRole::Group};
    std::string label;
    std::string value;
    core::Rect bounds{};
    std::uint32_t flags{0};       // enabled, focused, selected, checked...
    std::uint32_t actions{0};    // focus, activate, setValue, scroll...
    std::vector<std::string> children;
};
```

Widget 提供语义默认值，应用可以覆盖 label、value、role 和 actions；语义节点使用现有
稳定 identity，重建时按 identity diff 并保留辅助技术的焦点。键盘 Tab/Shift-Tab、
箭头导航和 Button/Checkbox 的 Enter/Space 激活与语义 actions 共用 `FocusManager`。

平台桥接按能力拆分：Windows UI Automation、Linux AT-SPI、macOS NSAccessibility；
桥接失败只关闭对应能力，不影响绘制和输入。本设计只覆盖桌面平台，不冻结移动端
语义桥接契约，也不安排移动原生 accessibility tree。

### 3.4 布局、滚动和应用组件

在不破坏现有 Box/Flex API 的前提下补充：

- intrinsic measurement、baseline、文本多行高度、overflow clip 和滚动视口约束。
- `ScrollController`、`ScrollView`、`ListView`，支持滚轮、触摸拖动、惯性关闭时的
  确定性测试、键盘滚动和语义 scroll actions。首期不做大规模虚拟化，只要求稳定 key
  和可预测的子树复用。
- `Form`、`Checkbox`、`Switch`、`Dialog`、`FocusScope`、`Navigator`/`Route`。
- `Theme` 和状态颜色/间距/文字 token；组件不能直接读取 SDL 或系统主题 API。
- overlay、modal barrier、返回键/Escape、焦点恢复和窗口关闭请求的统一规则。

这些组件必须同时能由 C++ DSL 构建；文本 DSL 只增加已经冻结的属性，解析器不引入
条件、循环或脚本能力。

## 4. 分阶段实施路线

### 阶段 8A：跨平台宿主契约与兼容层

- 冻结 `WindowId`、`WindowMetrics`、`AppLifecycle`、归一化 `Event`、`Clipboard`、
  `TextInputSession` 和 `PlatformCapabilities` 的公共接口。
- 把现有 SDL3 主循环迁移到 `ApplicationHost`；保留旧 `PlatformWindow` 工厂作为
  过渡适配，避免 counter 和现有测试一次性改写。
- 增加 fake host、fake clock、fake clipboard 和可注入事件源；headless 测试可以模拟
  最小化、恢复、surface detach/attach、DPI 变化和多窗口事件。
- 将 `WindowId` 加入诊断、FrameScheduler、资源完成事件和 Renderer surface 描述。

出口条件：CPU-only 构建在无桌面会话下通过全部旧测试；counter 在两个独立窗口的
fake host 中能分别处理状态和帧；事件顺序、生命周期和 surface 重建有确定性断言。

### 阶段 8B：文本布局、IME 和编辑能力

- 实现 `lumen-text` 基础契约、字体回退、桌面 Skia shaping、CPU fallback 测量和布局缓存。
- 将 TextField 改为 selection/composing 模型，接入 Windows TSF/SDL、Linux IBus/Fcitx、
  macOS 输入法的 commit/preedit 转换。
- 增加剪贴板、修饰键、滚轮、pointer cancel、触摸 pointer id 和键盘焦点遍历。
- 增加多行 TextField 的滚动、选区绘制、候选词锚点和光标可见性。

出口条件：同一 headless 场景覆盖 ASCII、中文、emoji、组合字符和 RTL 文本的编辑、
撤销边界、选区和 frame hash；Windows/Linux/macOS 至少各有一次真实输入法 smoke；
缺少正式文本后端时仍能启动并明确报告 fallback。

### 阶段 8C：语义树、键盘导航和桌面无障碍

- 实现 `SemanticsNode`、identity diff、语义焦点和 action 分发。
- 为现有 Button/TextField 和新增 Checkbox/Switch/List/Dialog 生成正确 role、label、
  value、bounds、enabled/focused/checked 状态。
- 接入 Windows UIA、Linux AT-SPI、macOS NSAccessibility 的最小桥接，保留动态库或
  SDK 不可用时的构建开关。
- 增加高对比色、减少动画、系统字体缩放等可访问设置的只读 capability 查询，并让
  Theme 和 FrameScheduler 使用这些设置。

出口条件：counter 和 settings 示例的语义树在三种桌面平台结构一致；无障碍 action
可以触发点击、设置值、滚动和关闭 dialog；headless 可以在没有屏幕阅读器的环境中
验证整棵树、焦点顺序和 identity 稳定性。

### 阶段 8D：应用基础组件、滚动和导航

- 扩展 LayoutEngine 的 intrinsic/baseline/overflow 语义，保证文本布局和滚动视口共用
  同一套约束。
- 实现 ScrollView/ListView、ScrollController、Form、Checkbox、Switch、Dialog、
  FocusScope、Navigator/Route 和 Theme。
- 将 pointer/touch/wheel/keyboard/semantic action 汇聚到统一的手势和焦点状态机；
  明确 modal barrier、返回键、Escape、窗口关闭和焦点恢复行为。
- 新增 `examples/settings/` 或 `examples/catalog/`，同时展示滚动列表、表单验证、
  弹窗、导航、主题和无障碍标签；counter 继续作为最小回归样例。

出口条件：settings/catalog 示例可以在 320px 宽桌面窗口、DPI 缩放和窗口可用区域内使用；
连续 resize、滚动、导航返回、弹窗关闭和热重载不会丢失状态；列表 key 复用和局部
重绘与 forced full repaint 像素一致。

### 阶段 8E：macOS 桌面验证与三桌面发布门槛

- 在 SDL3 桌面契约之上完成 macOS window、resize/DPI、菜单关闭、文本输入、剪贴板、
  CPU/Skia 光栅和 NSAccessibility smoke。macOS GPU 不作为 v0.3 强制门槛。
- 建立 CI 矩阵：Windows/Linux/macOS 桌面 CPU + Skia 光栅，Windows/Linux 保留 GPU
  增强 smoke；后续三桌面 GPU 发布门槛由自用路线图 M7 定义。
- 输出按平台分层的打包说明、系统依赖、符号文件、能力探测和已知限制；禁止把
  `#ifdef` 平台分支散落到 core/layout/widget 实现。

出口条件：Windows/Linux/macOS 都能构建并运行 settings/catalog；桌面平台在 Renderer
或系统服务失败时有可见诊断和安全回退；支持矩阵、故障排查和版本化 API 文档齐全。

历史 8E 的 `MobileHostSeam`、SDL-free 构建目标和 Linux/macOS `mobile-core` CI
仍保留，验证通用代码兼容性；本计划不增加 NDK/Xcode 移动工程或模拟器/真机门槛。

## 5. 测试、基准与验收

### 5.1 单元和 headless 测试

- 平台契约：事件归一化、修饰键、pointer cancel、窗口隔离、生命周期、DPI/safe area、
  clipboard 和 TextInputSession 状态机。
- 文本：UTF-8 错误、grapheme 边界、selection/composing 合并、双向段落、字体回退、
  换行/ellipsis、baseline、命中测试和布局缓存失效。
- 交互：键盘导航、滚轮/触摸滚动、惯性开关、modal barrier、返回键、焦点恢复和语义
  action 与 Widget handler 的一致性。
- 语义：role/label/value/bounds/actions、identity diff、顺序、隐藏/禁用/选中状态和
  settings 的辅助功能设置。
- 组件：intrinsic/baseline/overflow、滚动视口、列表 key 复用、表单校验、导航栈和
  主题切换；局部重绘与全帧像素结果一致。

### 5.2 平台和集成测试

- Windows/Linux/macOS：创建/销毁多个窗口、resize/DPI、最小化恢复、剪贴板、真实或
  录制的 IME 序列、文本输入、滚轮、触摸映射和退出。
- Windows UIA、Linux AT-SPI、macOS NSAccessibility：验证节点 role、名称、边界、
  焦点和 activate/setValue/scroll action；没有桌面辅助技术时运行结构回归测试。
- counter、settings/catalog 在 CPU、Skia 光栅和可用 GPU/回退模式完成点击、输入、
  滚动、导航、resize、热重载和退出。

### 5.3 性能和稳定性门槛

- 继续使用 v0.2 固定场景作为渲染基线；v0.3 新增文本密集、滚动列表和语义 diff 场景。
- 同一机器同一配置下，p50/p95 总帧时间、UI 构建时间和提交时间相对 v0.2 不得恶化
  超过 10%；字体 shaping、语义 diff 和滚动缓存分别报告命中率。
- 长时间输入、快速 resize、反复 attach/detach、窗口创建销毁和资源压力运行不得崩溃、
  卡死、泄漏或复活旧资源。GPU 和系统服务故障必须记录 capability/fallback 原因。

完整门槛仍从 CPU-only 开始，再执行可选后端和平台 smoke：

```sh
cmake -S . -B build -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

新增的 Windows/Linux/macOS 构建命令和系统依赖必须写入 README 与 CI，不能只存在于
开发者本地脚本。平台专属测试失败时要明确区分“能力未启用”“环境不可用”和“框架
行为回归”。

## 6. 交付物和模块边界

预计新增或拆分的目标如下，名称可以在 8A API 评审时微调：

```text
include/lumen/
├─ core/             # Widget/Element/State/RenderNode 与平台无关事件值类型
├─ layout/           # intrinsic、baseline、scroll viewport 约束
├─ render/           # v0.2 命令、CPU/Skia/GPU、FrameScheduler、资源
├─ text/             # TextLayout、FontManager、TextEditingValue、TextMetrics
├─ accessibility/    # SemanticsTree、role/action、平台桥接接口
├─ platform/         # ApplicationHost、WindowManager、Clipboard、native adapters
├─ widgets/          # ScrollView、ListView、Form、Dialog、Navigator、Theme
└─ dsl/              # C++ builder 与受限文本 DSL
```

公共头文件必须继续把平台 SDK 藏在实现目标中。`lumen-text` 可以依赖可选的 Skia
实现，但 `lumen-core`、`lumen-layout` 和 `lumen-dsl` 不能依赖 Skia；
`lumen-accessibility` 的 Windows/Linux/macOS 桥接也必须是可选目标。所有新的
FetchContent 依赖要固定版本、说明许可证和 CPU-only 行为。

v0.3 交付至少包括：

- 冻结后的跨平台宿主/事件/生命周期/剪贴板/文本输入契约。
- 正式桌面文本布局与 TextField 编辑能力，包含 IME、选区、字体回退和多语言测试。
- 语义树、键盘导航、Windows/Linux/macOS 最小无障碍桥接。
- ScrollView/ListView、Form、Dialog、Navigator、Theme 和 settings/catalog 示例。
- macOS 桌面支持、三桌面窗口与生命周期验证、更新后的 CI/README/支持矩阵。

## 7. 风险控制与后续版本

- **文本和字体差异**：系统字体、字体许可和 shaping 版本会改变像素。使用 layout
  metrics、字形位置和关键几何断言作为跨机器门槛，像素快照只在固定字体环境运行。
- **IME 差异**：Windows TSF、IBus/Fcitx 和 macOS 的 preedit/commit 时序不同。
  先固定 `TextEditingValue` 状态机，再为每个平台写事件转译测试；没有 IME 时仍可用
  直接 commit 输入。
- **无障碍 API 复杂度**：桥接库和桌面会话并非每个 CI 都有。先保证语义树 headless
  正确，再将平台桥接作为可选能力和真实 smoke；桥接异常不能阻塞渲染。
- **桌面窗口恢复**：窗口重建、DPI 变化和 GPU 上下文丢失时，状态树、资源句柄和
  渲染 surface 必须分离，恢复时走统一的 reset/reupload 流程。
- **范围膨胀**：只接受当前桌面目标组件和平台契约；Android/iOS 暂缓，不因历史
  接缝代码存在而增加移动功能。其他增强以自用路线图的实际状态和用户需求为准。

后续桌面版本以三平台行为和文本/语义契约稳定为入口，按需评估桌面 GPU 后端、
富文本编辑器、多窗口导航和平台服务。移动端不作为入口条件，也不绑定后续版本；
只有重新确认需求后才另行设计。任何新增后端仍必须消费现有 `RenderCommandList`，
不能绕过 UI、布局、文本和语义边界。
