# Lumen 自定义标题栏设计（无边框窗口 chrome）

> 文档状态：设计稿（2026-09）
> 输入：源码现状盘点（`include/lumen/platform/application_host.h`（WindowDesc/服务契约）、`src/platform/sdl3_host.cpp`（窗口创建/事件翻译）、`include/lumen/core/interaction.h` + `src/core/interaction.cpp`（hitTestChain/onClick 链）、`src/app/run_app.cpp`（事件泵/光标同步先例））、`docs/lumen-menu-controls-design.md`（菜单栏为 chrome 一部分的先例）、`docs/lumen-splitter-design.md`（指针形状宿主映射先例）。
> 配套视觉设计稿：`design/gallery.html`（四变体标题栏）。
> 定位：桌面自用版窗口 chrome 能力——Gallery 示例以自绘标题栏替代系统标题栏。遵循"公共头无平台类型、平台差异收敛在 host 实现内"的既有架构。

---

## 1. 背景与问题

Gallery 当前窗口由系统原生标题栏承载（`WindowDesc` 只有 title/尺寸/resizable 等字段），应用内的"窗口顶栏"只是装饰（`windowAction` 注释明确"真实窗口控件由 OS 标题栏提供"）。自绘 UI 框架的桌面应用惯例（VS Code / Figma / Chromium 系）是无边框窗口 + 应用自绘标题栏：chrome 与内容同一渲染管线、主题一致、密度可控。

源码核对后当前缺口：

| 能力 | 现状 | 缺口 |
| --- | --- | --- |
| 无边框窗口 | `WindowDesc` 无相应字段；SDL 窗口恒带系统边框 | 创建路径不支持 borderless |
| 窗口操作 | 仅 `WindowCloseRequested` 事件（被动） | 应用无法请求最小化/最大化/还原/关闭 |
| 拖拽移窗 | 无 | 框架不知道哪片区域是"标题栏" |
| resize 边 | 系统边框提供 | borderless 后需要 hit-test 恢复 |
| 最大化状态 | `WindowMetrics` 有 minimized，无 maximized | 图标（最大化/还原）无法切换 |

## 2. 参考框架与平台事实

| 框架/平台 | 做法 | 对 Lumen 的启示 |
| --- | --- | --- |
| Windows (Win32) | `WM_NCHITTEST` 返回 `HTCAPTION`/`HTRESIZE`：拖动、双击最大化、Aero Snap、边缘 resize 全部原生 | **hit-test 回调是唯一正确入口**，不需要自己实现拖动循环 |
| SDL3 | `SDL_SetWindowHitTest(window, cb, data)`；cb 在事件泵线程内调用；`SDL_WINDOW_BORDERLESS` + hit-test resize 共存；borderless 最大化约束到工作区（WM_GETMINMAXINFO） | 单一回调承载 caption + 8 向 resize；回调线程 = 泵事件线程 = UI 线程（本仓单线程泵） |
| Qt | `QWindow::setFlag(FramelessWindowHint)` + `startSystemMove/startSystemResize` | 语义拆分：窗口操作（min/max/close）与拖拽区域是两组 API |
| Flutter | `window_manager` 插件 `setAsFrameless` + `startDragging`；drag 区域由 MouseRegion 声明 | 区域声明应是**应用 UI 树的知识**（哪些控件遮挡标题栏只有应用知道） |
| GTK4 | `gtk_window_set_titlebar`（自绘 widget 替代） | 菜单栏并入标题栏是标准桌面形态（省一行 chrome） |

**采纳的共同事实**：

1. **拖拽/resize/双击最大化/snap 交给平台**，通过 hit-test 回调回答"这个点是什么区域"；框架不实现拖动循环（避免与平台 snap/多显示器语义打架）。
2. **区域判定是应用 UI 树的知识**：标题栏上的菜单项、下拉、窗口按钮不可拖——只有渲染树能回答"命中点是否落在交互控件上"。
3. **窗口操作（min/max/close）是宿主服务**：与剪贴板/文件对话框同层，结构化、可降级（默认实现 no-op，Fake host 可注入）。
4. **关闭走既有 `WindowCloseRequested` 路径**：自绘 close 按钮 ≠ 绕过 `onCloseRequested`（plan §3.4 统一关闭规则——modal 先消费）。

## 3. 设计目标与非目标

**目标**

1. `WindowDesc.customTitleBar`：无边框窗口创建（SDL `SDL_WINDOW_BORDERLESS`）。
2. 应用声明拖拽区：Widget 树节点标记 `windowDrag`，`AppShell::isWindowDragPoint(logical)` 用既有命中链回答。
3. `runApp` 接线：`customTitleBar` 时向宿主注册拖拽区谓词；宿主 hit-test 回调先判 8px resize 边、再问谓词。
4. 宿主窗口操作：`minimizeWindow` / `toggleMaximizeWindow` / `requestWindowClose`（默认实现安全 no-op）。
5. 最大化状态闭环：`WindowMaximized` 事件 + `WindowMetrics.maximized`，应用图标/行为可切换。
6. Gallery 示例：48px 自绘标题栏（品牌 + 菜单栏 + 拖拽区 + 状态 + 窗口控制按钮），全部 headless 可测。

**非目标（第一版明确不做）**

- macOS 交通灯（左上红黄绿）适配——当前三端统一右上 min/max/close；平台差异位后续按 `PlatformCapabilities` 分化。
- Windows 11 Snap Layouts 悬停弹层（需原生 caption button 才有系统弹层；自绘按钮拖到屏幕边缘仍可 snap）。
- 全屏（fullscreen）模式与 F11 语义。
- 多窗口各自的标题栏状态（当前单窗口应用壳）。
- 标题栏右键系统菜单（`WM_NCHITTEST` 命中 caption 后系统右键菜单由平台提供，无需额外 API）。

## 4. 总体架构

```text
┌───────────────────────────────────────────────────────────┐
│ 应用层（gallery）：buildTitleBar（品牌/菜单/状态/窗口控制） │
│   Widget.windowDrag=true 标记标题栏行；                    │
│   handlers: window-minimize / window-maximize /            │
│             window-close → 平台命令回调（main.cpp 注入）    │
├───────────────────────────────────────────────────────────┤
│ app 层：AppShell::isWindowDragPoint(Offset)                │
│   事件树（overlay 优先）命中链：最深节点非交互控件          │
│   且链上含 windowDrag 节点 → 可拖                          │
│   runApp：WindowDesc.customTitleBar 时注册为宿主谓词        │
├───────────────────────────────────────────────────────────┤
│ core 层：Widget/RenderNode.windowDrag（布局期物化）         │
│   WindowMetrics.maximized + HostEventType::WindowMaximized │
│   IconId::Restore（还原图标）                              │
├───────────────────────────────────────────────────────────┤
│ platform 层：WindowDesc.customTitleBar                     │
│   setWindowDragRegion(id, predicate<bool(Offset)>)         │
│   minimizeWindow / toggleMaximizeWindow / requestWindowClose│
│   SDL3：BORDERLESS 标志 + SDL_SetWindowHitTest             │
│   （先 8 逻辑 px resize 边，后 caption 谓词）              │
└───────────────────────────────────────────────────────────┘
```

### 4.1 拖拽区判定规则（AppShell）

```
isWindowDragPoint(p):
  chain = hitTestChain(eventTree(), p)      // overlay 活跃期换树：
    若 overlay 命中 → false（模态层不可拖） 未命中 overlay → 主树
  target = chain[0]                          // 最深命中节点
  if target 是交互控件（onClick/可聚焦控件类型） → false
  if 链上任一节点 windowDrag → true
  else → false
```

- 标题栏行节点携带 `windowDrag`；其上的菜单项/窗口按钮/下拉是交互控件（命中链更深），自然排除。
- 标题文本、状态胶囊等非交互子节点命中时仍可拖（原生 caption 行为一致）。
- 交互控件判定与 `hoverTargetOf`/click 仲裁同口径（onClick 非空即可成为点击目标；控制类型枚举见 interaction.cpp）。

### 4.2 宿主 hit-test（SDL3）

```text
SDL_HitTest(point in window pixels):
  logical = point / deviceScale            // 宿主窗口指标换算
  border  = 8 逻辑 px（四边；四角 12×12 命中角区优先）
  if point 在边缘/角带 → SDL_HITTEST_RESIZE_*
  else if dragPredicate(logical) → SDL_HITTEST_DRAGGABLE
  else → SDL_HITTEST_NORMAL
```

- 回调在 `SDL_PollEvent` 泵内触发（本仓 UI 线程独占泵），谓词读渲染树无数据竞争。
- `SDL_WINDOW_BORDERLESS` 窗口的 resize 依赖 hit-test resize 区（SDL 在 Windows 映射 `WM_NCHITTEST`）。
- 无效窗口 id 走单窗口便捷路径（挂靠首个窗口，与 `requestFileDialog` 先例一致）。

### 4.3 窗口操作与事件

| API | SDL3 实现 | 语义 |
| --- | --- | --- |
| `minimizeWindow(id)` | `SDL_MinimizeWindow` | 产生 `WindowMinimized`（既有） |
| `toggleMaximizeWindow(id)` | `SDL_GetWindowFlags` 判 `SDL_WINDOW_MAXIMIZED` → `SDL_MaximizeWindow`/`SDL_RestoreWindow` | 产生 `WindowMaximized`（新增）/`WindowRestored`（既有） |
| `requestWindowClose(id)` | 入宿主 pending 队列合成 `WindowCloseRequested` | 与系统 X 同路径：runApp → `shell.requestClose()` → 应用可消费 |

`WindowMaximized` 翻译自 `SDL_EVENT_WINDOW_MAXIMIZED`；`WindowMetrics.maximized` 来自窗口 flags。runApp 将 `WindowMaximized`/`WindowRestored` 一并经 `RunOptions.onEvent` 转发（应用切换最大化图标/状态）。

### 4.4 应用侧窗口命令

- Gallery 定义 `setWindowCommands({minimize, toggleMaximize, requestClose})`（`std::function` 三元组，可空）；main.cpp 在窗口装配期注入宿主实现。
- headless/采样路径不注入：handler 仍执行（记录 `lastWindowCommand_` 供断言）；`requestClose` 回退 `shell.requestClose()`（modal 消费/路由返回语义在无宿主下保持可测）。

## 5. 视觉规格（design/gallery.html）

- 标题栏单行 **总高 48px（border-box：内容行 47 + 1px 底分隔线）**（替代原 56px 顶栏 + 40px 菜单栏两行）：品牌标 28×28 + 标题 14px/650 + 菜单栏（**栏项 Small 档 32px**，含下划线占位整栏 ~38px——Medium 40px 项会撑到 46px 挤满 caption）+ 弹性拖拽区（右侧状态胶囊）+ 窗口控制。
- 窗口控制按钮 **44px 宽、通高（48px 行高）、右缘贴合窗口角**（Windows 惯例；`align-items: stretch`、右缘无 padding）。**图标盒 14px**（`StyleOverrides.iconSize` 覆盖，描边随盒宽折算 ≈1.6），**字形按稿 SVG 24 栅格逐坐标归一**（Minimize 5..19 线 / Maximize 6..18 方框 / Close 7..17 叉）；min/max hover 弱表面，close = `ButtonVariant::WindowClose`（rest 幽灵；hover 实心 `ButtonTokens.windowClose`——#c42b1c + 白，系统 chrome 红常量、深浅主题同值；pressed 叠压暗）。
- **透明窗口圆角（已实现）**：`WindowDesc.transparent`（SDL_WINDOW_TRANSPARENT + runApp 将 CPU 清屏色转全透明）；应用内容自绘圆角——gallery 根容器四角 + 标题栏顶角 16px，对话框 scrim 同半径（`makeDialog` 的 `scrimRadius` 参数，防止全窗压暗涂进圆角外的透明像素）；**最大化圆角归零**（.is-maximized）。Windows DWM 合成良好；无合成器 X11 角落退化黑；外部渲染器（Skia/GPU）透明 clear 由后端装配负责（未接）。
- 最大化态：窗口直角、最大化按钮切换还原图标（双层方框，`IconId::Restore`）。
- 窗口四边 **8 逻辑 px** 透明 resize 边 + 四角 12px（平台 hit-test，视觉不呈现）。
- ≤720 折叠标题文字只留品牌标；≤1024 隐藏状态胶囊（既有断点不变）。

## 6. 测试口径

- **core/app**：`windowDrag` 物化（标题栏行节点为真、内容区为假）；`isWindowDragPoint`（标题栏空白/标题文本 → true；菜单项、窗口按钮、下拉、主内容 → false；菜单 overlay 打开时 → false）。
- **gallery 集成**：标题栏结构（品牌/菜单栏/窗口控制键存在，菜单栏在标题栏行内）；三个窗口命令 handler（记录 + 平台回调为空时安全）；最大化态图标切换（`noteWindowMaximized`）；close 走 `requestClose`（弹窗打开 → 消费关闭弹窗）。
- **平台契约**：Fake host 窗口操作记录 + `requestWindowClose` 合成事件经 `pollEvent` 交付；默认实现（未覆写时）安全 no-op。
- **冒烟**：`gallery --max-frames N`（窗口路径 borderless 创建 + hit-test 注册不崩溃）。

## 7. 风险与已知限制

- **Windows Snap Layouts 悬停弹层**不出现（需原生 caption button）；拖拽到屏幕边缘的 snap 仍可用。
- **borderless 最大化覆盖任务栏**：依赖 SDL 的 WM_GETMINMAXINFO 工作区约束（SDL2 2.0.5+ 有此处理，SDL3 沿用）；如个别平台异常，回退策略是最大化前临时恢复系统边框（未实现，按需评估）。
- **Wayland**：hit-test 无 `WM_NCHITTEST` 对应物，SDL 退化为软件模拟（拖动经 xdg-shell move）；能力位后续按需暴露。
- **字体缩放**：标题栏高度与按钮尺寸按 fontScale 派生（与既有 `scaledStyle` 同口径），8px resize 边为逻辑像素。
