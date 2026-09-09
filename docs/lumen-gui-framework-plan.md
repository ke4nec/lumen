# Lumen：C++20 自绘 GUI 框架渐进式实现计划

## 1. 项目目标

Lumen 是一个用于学习和研究自绘 UI 框架的 C++20 项目，整体体验参考 Flutter：UI 使用声明式方式描述，框架负责布局、绘制、事件分发和状态更新。

框架的渲染引擎采用可替换设计。前期先实现 CPU 软件光栅器，随后接入 Skia；未来可以增加 Impler 或其他渲染后端，而不改变 UI 层和布局层。

第一阶段以“可交互最小闭环”为目标：窗口、布局、绘制、点击、焦点、文本输入、状态更新、重绘和一个示例应用全部跑通。

## 2. 已确定的技术选择

- 语言标准：C++20。
- 首批平台：Windows 与 Linux。
- 窗口和输入：SDL3，负责窗口创建、事件轮询、文本输入和 framebuffer 展示；渲染命令由 Lumen 自己生成。
- 构建系统：CMake + FetchContent。
- 测试框架：Catch2。
- UI 描述：C++ 声明式 API 与文本 DSL 并行支持。
- 文本 DSL 范围：声明式布局子集；状态和回调由 C++ 注册。
- 布局模型：Box 约束 + Flex 子集，提供 Row、Column、Stack、padding、margin 和对齐。
- 状态模型：单向状态 + 回调注册，不在首版引入响应式依赖追踪。
- 首个渲染目标：CPU 软件光栅器；同时定义 Skia 适配接口。

SDL3 的窗口像素尺寸可能在窗口创建后变化，因此平台层必须处理窗口像素尺寸变化事件并重新查询 drawable size。相关行为参考 [SDL3 创建窗口文档](https://wiki.libsdl.org/SDL3/SDL_CreateWindow)。

## 3. 工程结构

建议建立以下模块：

```text
lumen/
├─ CMakeLists.txt
├─ cmake/
├─ include/lumen/
│  ├─ core/
│  ├─ layout/
│  ├─ render/
│  ├─ platform/
│  └─ dsl/
├─ src/
│  ├─ core/
│  ├─ layout/
│  ├─ render/
│  ├─ platform/
│  └─ dsl/
├─ tests/
├─ examples/counter/
└─ docs/
```

CMake 目标：

- `lumen-core`
- `lumen-layout`
- `lumen-render`
- `lumen-platform`
- `lumen-dsl`
- `lumen-counter`

构建选项：

- `LUMEN_BUILD_EXAMPLES=ON`
- `LUMEN_BUILD_TESTS=ON`
- `LUMEN_ENABLE_SKIA=OFF`

Skia 适配器在代码中实现，但默认关闭，以保持 CPU 后端的快速构建；启用后执行后端一致性测试。

## 4. 核心架构

### 4.1 Widget、Element 与 RenderNode

- `Widget` 是不可变的 UI 描述对象，表达节点类型、属性和子节点。
- `Element` 保存 Widget 的运行时实例、状态订阅、父子关系和生命周期。
- `RenderNode` 保存布局结果、绘制属性、命中区域和子节点顺序。
- Widget 更新后通过类型和 key 进行子树复用；首版只要求稳定类型和可选字符串 key。
- UI 在单线程 UI loop 中运行，状态变更通过 invalidate 标记受影响节点。

### 4.2 几何和样式类型

提供以下基础类型：

```cpp
struct Size;
struct Offset;
struct Rect;
struct Constraints;
struct Color;
struct CornerRadius;
struct EdgeInsets;
struct TextStyle;
```

所有布局计算使用浮点逻辑坐标；平台 framebuffer 使用像素坐标。逻辑坐标到像素坐标的缩放在平台/渲染边界处理。

### 4.3 Renderer 接口

```cpp
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void beginFrame(Size viewport) = 0;
    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void clipRect(Rect rect) = 0;
    virtual void drawRect(Rect rect, Color color,
                          CornerRadius radius = {}) = 0;
    virtual void drawText(TextRun run, TextStyle style) = 0;
    virtual void drawImage(ImageId id, Rect destination) = 0;
    virtual void endFrame() = 0;
};
```

首版实现：

- `CpuRenderer`：RGBA framebuffer，支持纯色矩形、圆角矩形、裁剪、基础位图和占位文本。
- `SkiaRenderer`：把同一组绘制命令映射到 Skia，提供抗锯齿和正式文字绘制。
- 未来 `ImplerRenderer` 直接实现 `Renderer`，不改变 Widget、布局和 DSL。

### 4.4 平台接口

```cpp
class PlatformWindow {
public:
    virtual ~PlatformWindow() = default;
    virtual Event pollEvent() = 0;
    virtual Size logicalSize() const = 0;
    virtual Size drawableSize() const = 0;
    virtual void present(const PixelBuffer& buffer) = 0;
};
```

SDL3 平台实现负责：

- 创建和销毁窗口。
- 轮询鼠标、键盘、文本输入、焦点和窗口大小事件。
- 将 CPU framebuffer 上传到 SDL texture 并展示。
- 将 SDL 事件转换为 Lumen 的平台无关事件。

## 5. 布局、绘制和事件

### 5.1 布局范围

首版组件：

- `Container`
- `Row`
- `Column`
- `Stack`
- `Text`
- `Button`
- `TextField`

支持：

- 最小/最大/固定尺寸约束。
- padding 与 margin。
- 主轴和交叉轴对齐。
- Row/Column 的 flex 分配。
- Stack 的相对定位。

暂不实现 intrinsic size、baseline 对齐、复杂文本排版和完整 Flutter 约束语义。

### 5.2 绘制流程

每帧执行：

1. 收集状态变更和平台事件。
2. 对受影响子树执行 build/reconcile。
3. 从根节点向下执行 layout。
4. 生成绘制命令并提交给 Renderer。
5. 将 CPU framebuffer 或 Skia 输出提交给 PlatformWindow。

初版每次状态更新都可以整帧重绘；脏矩形和绘制缓存作为后续优化阶段。

### 5.3 事件流程

- 命中测试按反向绘制顺序执行，顶部节点优先。
- 事件先发送到目标节点，再沿父链冒泡。
- `Button` 处理 pointer down/up，并在有效点击时触发回调。
- `TextField` 通过 FocusManager 获取焦点，接收 keyboard 和 text input 事件。
- 窗口 resize 事件会更新根约束并触发布局。
- 首版不实现复杂手势识别、拖拽系统和多指触控。

## 6. DSL 设计

### 6.1 C++ 声明式 API

提供 builder/lambda 风格 API：

```cpp
auto page = column({
    text("Count: ", bind("counter")),
    button("Increment", onClick("increment")),
    text_field(bind("name"), placeholder("Name"))
});
```

API 只负责生成 Widget 描述；状态存储、事件回调和窗口生命周期由应用层管理。

### 6.2 文本 DSL

第一版采用手写 lexer + recursive-descent parser，文件扩展名为 `.lumen`。

示例：

```text
page Counter {
  Column(padding: 16) {
    Text("Count: ", bind: counter)
    Button("Increment", onClick: increment)
    TextField(bind: name, placeholder: "Name")
  }
}
```

支持内容：

- 节点名称和嵌套结构。
- 数值、字符串、颜色、布尔属性。
- 样式属性。
- `bind` 状态绑定。
- `onClick` 等事件名。

DSL 不包含脚本、变量声明、条件语句和循环。解析结果转换为同一套 Widget 描述对象。

错误必须包含文件名、行号、列号和期望 token。

## 7. 状态和应用模型

采用单向状态流：

1. 应用创建 `StateStore`。
2. 应用注册 `counter`、`name` 等状态键。
3. DSL 通过 `bind` 读取状态。
4. 事件名映射到 C++ 回调。
5. 回调更新状态。
6. 相关 Element 被 invalidate，重新 build、layout 和 paint。

示例应用 `counter` 必须同时支持：

- C++ DSL 加载。
- 文本 DSL 加载。
- Button 点击计数。
- TextField 文本输入。
- 窗口缩放后的自适应布局。
- CPU/Skia 后端切换。

## 8. 分阶段路线

### 阶段 0：工程骨架

- 创建 CMake、模块目录、FetchContent 和 Catch2 测试入口。
- 建立最小 README 和本计划文档。
- 添加一个能编译运行的空窗口示例。

### 阶段 1：核心树和布局

- 实现 Widget、Element、RenderNode 生命周期。
- 实现几何类型、约束传播和 Box/Flex 子集。
- 添加离屏布局测试和 RenderNode 树测试。

### 阶段 2：CPU 渲染和 SDL3 平台层

- 实现绘制命令和 CpuRenderer。
- 实现 SDL3 窗口、事件转换、文本输入和 framebuffer 展示。
- 完成矩形、圆角矩形、裁剪和占位文本。

### 阶段 3：交互与 C++ DSL

- 实现 hit test、事件冒泡、FocusManager、Button 和 TextField。
- 实现状态订阅、invalidate 和重绘。
- 交付可交互 counter 示例。

### 阶段 4：文本 DSL

- 实现 lexer、parser、AST/Widget 转换和错误报告。
- 让 counter 示例可以通过 `.lumen` 文件加载。
- 添加解析 golden tests 和运行时绑定测试。

### 阶段 5：Skia 适配

- 实现 SkiaRenderer。
- 为同一绘制命令集增加 CPU/Skia 输出一致性 smoke test。
- 保持 Skia 为可选依赖，确保 CPU-only 构建可用。

### 阶段 6：框架完善

- 脏矩形和绘制缓存。
- 图片、字体和资源生命周期。
- 动画和基础手势。
- DSL 编译缓存与开发期热重载。
- Impler 后端适配。

## 9. 测试与验收标准

- 几何与布局：约束传播、Row/Column 分配、padding/margin、Stack 对齐和 resize。
- 事件：命中顺序、遮挡、冒泡、focus、Button 点击和 TextField 输入。
- DSL：合法文档、默认属性、绑定、事件名和行列号错误报告。
- CPU 渲染：绘制命令、裁剪、圆角矩形和离屏 framebuffer 像素断言。
- 状态：状态更新触发相关节点重绘，节点销毁后订阅清理。
- 集成：无窗口模式运行 counter 并生成稳定 frame hash。
- 平台 smoke：Windows 与 Linux 创建窗口、点击、文本输入、resize 和退出。
- Skia smoke：启用 `LUMEN_ENABLE_SKIA` 后运行同一 counter 和绘制命令回放。

第一里程碑通过条件：

- Windows 和 Linux 均能启动示例窗口。
- Button 点击和 TextField 输入可见生效。
- 窗口缩放后布局正确。
- C++ DSL 与文本 DSL 都能构建同一 UI。
- CPU 单元测试和无窗口集成测试通过。

## 10. 首版边界和设计假设

- 单窗口、单 UI 线程、UTF-8 文本。
- 首版允许整帧重绘。
- 不追求 Flutter API 兼容。
- 不在首阶段实现完整字体排版、动画、无障碍、GPU 合成、复杂 IME 和热重载。
- Renderer、Widget、布局和 DSL 之间使用明确接口隔离，保证未来替换 Skia 为 Impler 时无需重写 UI 层。
