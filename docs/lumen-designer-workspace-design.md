# Designer 工作区四方案设计

> 2026-10-13 · 可交互提案；B 画布优先已接入 C++ 应用（本文件 §8）。
> 只覆盖 Windows / Linux / macOS 桌面。
> 入口：[方案总览](../design/designer-ide.html)；同一 HTML 内置 A–D 四套方案，每版均有 light / dark。
> 本次按工作树中的实际实现审计；不把早期原型中的目标态当成已交付能力。

## 1. 现状与设计依据

### 1.1 当前实现（B 画布优先已落地）

审计入口为 `examples/designer/designer_app.cpp` 的 `buildTitleBar`、
`buildMainTools`、`buildRail`、`buildOutlinePanel`、`buildCenterPanel`
（`buildDocumentTabs`/`buildCanvasControls`/`buildCrumbBar`/`buildBottomDock`）、
`buildPropertiesPanel`、`buildWorkspace`、`buildStatusBar`、`buildBody`、
`buildUi`。旧 `buildToolbar`/`buildPreviewPanel`/`buildDiagnosticsPanel`
保留为兼容转调（分别转调主工具/中心/底栏）。

| 区域 | 当前实现事实 | 对操作的影响 | 新提案 |
| --- | --- | --- | --- |
| 全局工具栏 | 编辑、运行、文件、环境、缩放五组；22 项；字号增大时标签退为图标 | 频率、操作对象与作用域混在同一层；环境图标的当前值不够显眼 | 文件/编辑/视图菜单提供低频入口；工具栏保留保存、撤销、插入、布局、运行；画布工具就近放缩放 |
| 左栏 | 固定 240；Outline / Toolbox 两页互斥；Project 在底部；工具箱 7 组、37 类 | 插入与层级选择需要来回切换；长树与工程入口分享高度 | A 同时显示工具箱/大纲；B 按需切换；C、D 大纲独占高度 |
| 画布 | Canvas / Source / References 为中心互斥页；已有选取、辅助线、zoom/pan 等 | 引用/源码核查会离开视觉上下文 | D 分屏只读快照，引用移至底部；其他版本也可切换分屏 |
| 属性 | 固定 280；schema 可编辑值主要生成 TextField；引用也用 TextField | 数字、枚举、布尔没有充分表达类型；长属性列表不利于找到布局项 | 基本/布局/外观引用分组；类型映射到 Spin / ComboBox / Checkbox；Row/Column 展示主轴与间距 |
| 主体布局 | Row 三栏；当前没有把 Splitter 用作外壳分栏 | 大纲与属性宽度无法随任务调整 | 两个嵌套 Splitter；有下限、拖动、键盘调节、双击复位 |
| 诊断 | 默认高 96；冲突时加高；始终位于主体下方 | 无问题时仍消耗固定高度 | A/B 默认折叠到 36 高标签；C 默认布局工具；D 默认问题列表 |
| 源码 | `buildSourcePanel` 生成带行号的只读文本 | 不应误设计为可编辑 IDE 源码页 | 使用“快照 / 只读”名称，保留定位；不提供编译编辑按钮 |

现有 `design/designer-workbench.html` 已跟随五组工具栏与左栏 C 方案更新，
是当前布局的参考。`designer-toolbar-redesign.html` 与
`designer-leftpanel-redesign.html` 分别研究局部变化，本次扩展为整套工作区。
`design/designer.html` 是较早目标态原型，其中 DP-9 待决策、部分图标缺失、
源码编辑等表述不能用作当前能力结论；本次以源码与支持矩阵为准。

### 1.2 参考软件与转译

- [Qt Designer 工作区](https://doc.qt.io/qt-6/designer-to-know.html)：Widget Box、Object Inspector、Property Editor 围绕表单组织。本次借鉴“插入来源 / 对象身份 / 属性 / 结果”四类职责，A 将高频面板同时呈现。
- [Qt Designer 布局](https://doc.qt.io/qt-6/designer-layouts.html)：布局对象可在对象检查器中查看，容器可嵌套。本次借鉴可见的布局结构，C 将 Row/Column 与间距放在显著位置。
- [Visual Studio 窗口布局](https://learn.microsoft.com/en-us/visualstudio/ide/customizing-window-layouts-in-visual-studio)：工具窗口可分组、停靠、隐藏和恢复布局。本次借鉴文档区与工具区分工、按任务显示面板、恢复布局。**没有照搬任意停靠 / 浮动工具窗口系统**，现有 Splitter/Tabs 足以支撑固定预设。

以上是设计推导，参考软件并不规定 Lumen 必须采用这些布局。

## 2. 能力矩阵

“框架有控件”“Designer 已接通”“新交互提案”是不同状态。

| 设计部件 | 框架支持 | Designer 当前情况 / 本次新增 |
| --- | --- | --- |
| 对象树 | `widgets/tree.h`、TreeController、选择模型、键盘导航 | 已接通选择及重排；新稿改变面板位置 |
| 左右分隔 | `widgets/splitter.h`、SplitterController、KeepRatio、collapse | 控件已有；Designer 外壳当前固定宽，新接入属于应用层工作 |
| 菜单、命令栏 | MenuBarController / ContextMenuController、ToolBarController、CommandRegistry | 当前工具栏主要为组合 Row；改为语义化分组并接通 overflow 是后续实现项 |
| 属性编辑 | TextField / Spin / ComboBox / Checkbox / ColorPicker / Form | 当前以文本框为主；类型化呈现、检索、分组需应用层接线 |
| 文档 Tabs | `core::makeTabs` | 已有中心页签；本稿只显示当前一份文档，未虚构多文件编辑状态 |
| 问题、引用 | VirtualList / DataGrid / Tabs | 当前诊断和引用数据已接通；折叠及合并底栏为新增工作区状态 |
| 状态栏 | StatusBarController | 新稿统一显示保存、选择、反馈，不把临时预览值混入文档状态 |
| 运行预览 | 独立 preview-session、DialogHost（另有 headless 冒烟用的独立 previewShell，不开 OS 窗口） | 运行会话在设计窗内以模态覆盖层呈现（运行/调试按需出现，停止/Esc/遮罩点击隐藏）；HTML 亦为 modal，不宣称模拟原生窗口生命周期 |
| 撤销、插入、删除、复制 | DesignWorkbench、DesignDocumentHistory、文档事务 | 已有后端；原型独立模拟，后续 UI 应调用既有命令 |
| 源码与引用定位 | SourceMap / CompileTrace、只读 Source、references | 已有基础；D 新增同屏呈现与选中同步 |
| 工作区预设与专注 | Row/Column/Stack/Tabs/Splitter、Preferences 可组合 | 新应用逻辑；本次 HTML 不持久化文档/窗格 |
| 任意 DockManager、浮动工具窗 | 本次未找到可直接消费的统一停靠管理器 | 不纳入落地前提；固定四种预设 |
| 代码编辑器 | TextField / 文本布局不等于完整 IDE 编辑器 | 不承诺语法编辑、补全、调试断点或任意 C++ 双向生成 |

源码工具箱覆盖 37 类：布局 7、内容 3、输入 10、集合 7、反馈 2、组合件 6、
作用域 2。依据 `designerToolboxGroups()` 与 `designerToolboxSchemas()`，
组合件由离线 RuntimeContext 适配器承载。HTML 只模拟 Text、Button、
TextField、Checkbox、Column、Row、Container 七类常用控件的编辑路径。
不能从这七类原型反推完整 37 类都已有同等交互。

## 3. 四套工作区

| 方案 | 左侧 | 中心 | 右侧 | 底部 | 主要适用 |
| --- | --- | --- | --- | --- | --- |
| [A 经典分区](../design/designer-ide.html?variant=a) | 工具箱 / 对象树上下共存 | 画布 + 面包屑 | 类型化属性 | 可折叠问题/引用 | 常规表单、快速插入；建议默认 |
| [B 画布优先](../design/designer-ide.html?variant=b) | 48 活动栏 + 可隐藏控件/结构页 | 最大画布，隐藏标尺，可专注 | 属性 | 默认收起 | 视觉比例、检查整体效果 |
| [C 布局工坊](../design/designer-ide.html?variant=c) | 工程标识 + 全高结构树 | 画布 + 容器上下文 | 属性上 / 工具箱下 | Row/Column 布局工具 | 嵌套容器、主轴与间距 |
| [D 检查工作台](../design/designer-ide.html?variant=d) | 工程标识 + 对象树 | 画布 / 只读快照并排 | 预览环境 + 属性 | 问题/引用常驻 | 查引用、核对声明与预览 |

四版保持同一份示例设置表单、同一套主题和命令，以便比较信息组织，而非用不同
业务内容掩盖布局差异。主题与布局正交，`?variant=a&theme=light` / `?variant=a&theme=dark`
等参数提供 8 个直接入口，均指向同一个 HTML。顶部方案导航切换会重新加载初始示例，**不跨方案保留编辑**。

### 3.1 空间预算

应用显式采用 Compact；表单预览默认 Comfortable，二者是不同作用域。

| 尺度 | A | B | C | D |
| --- | ---: | ---: | ---: | ---: |
| 左面板初始宽度 | 232 | 224 + 48 活动栏 | 280 | 208 |
| 右面板初始宽度 | 288 | 280 | 280 | 248 |
| 分隔轨道 | 6 | 6 | 6 | 6 |
| 底栏初始高度 | 36 | 36 | 144 | 144 |
| 主要空间代价 | 左侧两区分享高度 | 隐藏面板需再次唤出 | 画布高度减少 | 分屏画布变窄 |

在 1200 以下降低面板默认宽度；左右边界分别不低于 180/232。
中央画布自身滚动，缩放使用逻辑坐标，标尺与网格同步缩放；默认 Fit 不裁掉整张示例表单。
不足 960 宽的浏览器采用桌面最小工作区横向滚动，不宣称移动端适配。
总览页可以窄屏阅读。高度不足时各面板内部滚动，不压缩 32 高操作行。

全屏专注、恢复工作区与收起面板均可通过“视图”菜单找到；退出专注也可按 Esc。
生产方案必须保存窗格偏好并钳制到当前窗口可用范围，这仍是后续应用工作。

## 4. 操作契约

### 4.1 选择与插入

- 画布、对象树、面包屑、诊断、引用与源码行共用一个当前 DocumentId 选择。
- 点击叶节点只选择最深节点；点击面包屑可返回容器。选中状态与键盘焦点独立；树自动滚动到当前选中项。
- 控件箱搜索按类型英文名和中文描述过滤，无匹配时给出空态。
- 点击控件插入到所选容器；所选是叶节点时插入到父容器。面板底部始终显示目标名称。
- 拖放以最近的受支持容器为目标，显示虚线轮廓；不向叶节点非法添加孩子。Container 只允许一个子节点，已满时拒绝插入/复制，多个孩子应放入 Row/Column。
- HTML 拖入追加到目标容器末尾，同级顺序通过上下移动改变；**未模拟精确落点插入或树间拖放**。
- 删除容器同时删除其子树；根节点不允许删除或复制。插入、复制、删除、重排均可撤销。
- HTML 实现单选；现有 Designer 的多选、框选、吸附等能力不因本研究被删除，但本稿不假装已完整模拟。

### 4.2 属性与布局

- 属性按“基本 / 布局 / 外观引用”呈现。TextField 编辑文字，Spin 编辑数字，ComboBox 编辑枚举，Checkbox 编辑布尔。
- 在属性输入框变更提交（change / blur）时形成一条事务；非法值不改变画布或撤销栈，保留输入并显示错误。
- 示例限制：宽 32–960、高 24–600、间距/内边距 0–64，均为原型编辑范围，不修改框架 schema 合法域。
- Row/Column 的位置由布局计算；不显示自由坐标 X/Y，不提供会与布局冲突的吸附移动操作。
- 主轴与 spacing 修改后同时更新画布、只读快照和容器底栏。
- 恢复属性可撤销；新插入节点没有初始样本时提供明确反馈。
- ColorPicker 框架已有，本稿外观区展示 Theme 引用，没有虚构颜色选择器实现。

### 4.3 文件、引用与运行

- 保存按钮下载 `settings-interaction-snapshot.json`。外层明确标记 `lumen-designer-html-study`，内部含类型化文档示意；**未经 C++ codec 编译/往返验证，不作为可交付 .design 文件**。
- 初始 `saveWorkspace` 是故意缺失的示例引用。点击问题定位 saveButton；改成 `saveSettings` 后问题消失，引用显示离线替身。
- 源码页显示当前示例文档快照，随属性变动更新，禁止编辑。行点击回选文档节点。
- F5 打开隔离的运行预览；输入、复选框和按钮回调只改变预览会话。关闭后设计文档保持原值。
- 原生应用已有文件冲突处理。本次只下载快照，不改写外部文件；后续生产 UI 应保持 `lumen-designer-file-conflict-design.md` 的重载/另存/覆盖契约，冲突反馈不能被普通折叠诊断隐藏。

### 4.4 主题、环境和键盘

- 评审条主题按钮改变工作台外壳；画布主题由“预览环境”独立设置。初次打开时二者同值。
- 切换明暗、密度、缩放、网格、面板显隐都不触碰文档 dirty / undo。
- 主色、表面、文本、边框、选中、焦点、状态色均映射 Theme；不是 CSS 反相滤镜。
- Tab 访问操作，焦点环可见。对象树上下/Home/End 导航、左右展开/返回父级；分隔条左右键按 Compact 的 12 步进，Home/End 到边界，双击恢复默认宽度。
- Ctrl/⌘ Z、Shift Z、D、S 对应撤销/重做/复制/下载；Delete 删除；F5 运行；`/` 搜索；Esc 退出临时模式。
- 输入控件拥有编辑焦点时，文字撤销优先交给浏览器输入历史，不抢占文档级命令。

## 5. 视觉契约

视觉值对应 [`lumen-visual-system-design.md` §3.6](lumen-visual-system-design.md)，
控件契约继续引用 collection-controls §6–10、splitter §7/9、toolbar §6/9、
menu-controls 和 statusbar 设计。HTML 的 CSS 变量是主题角色映射；
面板宽度、文档页签、审阅页排版是应用布局，不向控件 painter 填硬编码值。

- 4/8 间距网格；工作台 12 caption、14 body、20 title；操作区至少 32 高。
- 1px 边界区分表面；工作台不依赖毛玻璃、渐变或巨幅阴影。
- 选中使用 accentContainer 背景 + 3px 标记；焦点使用 focusRing + 2px 环。
- 深色不是纯黑填充；浅色使用白 surface 与浅灰 page 区分功能层。
- 图标为线性 SVG 示意，对应现有 IconId 语义；落实 C++ 时复用目录。
- 画布示例为浏览器近似，不是 CPU/Skia 像素级 golden；字号、控件继承、描边仍需原生 headless 验证。

## 6. 实施建议（B 已落地，A/C/D 为后续预设）

B 画布优先已按本稿 §3–§5 落为同一工作区模型的默认预设（`DesignerApp`
`centerView_`/`bottomTab_`/`zenMode_`/`leftCollapsed_` + 嵌套 Splitter），
复用既有 `DesignWorkbench` 命令与事务，编辑/预览/布局偏好分别存放。
A（经典分区）与 C/D 实现为同一模型的后续预设，而不是复制四套编辑器逻辑。

## 8. B 落地记录（2026-10-13）

对照 `design/designer-ide.html?variant=b`（Canvas Studio）一比一实现，
视觉值取 `Theme` token（visual-system §3.6），不复制 HTML 常量：

- 标题 44（品牌 + 文件/编辑/视图 + 居中工程名 + DESIGNER + 帮助 +
  右上窗口控制：最小化/最大化-还原/关闭，`designer-window-actions`；
  自绘 caption：整行 `windowDrag`，空白可拖、按钮/菜单项命中排除，
  `customTitleBar` 无边框窗口 + 宿主 `minimize/toggleMaximize/requestClose`，
  最大化态切换 Maximize/Restore 图标；对照 `design/designer-ide.html`
  `.win-controls/.caption-btn`，口径与 Gallery `titlebar-design §5` 一致：
  44 宽、通高、右缘贴合、直角无圆角，close = `WindowClose` hover 实心红；
  key/handler 刻意沿用 Gallery 裸名（`window-minimize/maximize/close`）以
  共享宿主接线心智，语义标签用中文，keys 保持英文稳定标识）+
  主工具 48（保存/打开/另存/新建/文本/撤销/重做 | 选择/插入/布局 |
  提示“更少干扰，更大画布” | 专注/调试/停止/运行，Run 唯一 Filled）+
  工作区（48 rail + 224 left + 6 + center + 6 + 280 right）+ 状态 28。
- Rail 切换控件/结构（默认控件），大纲操作（上移/下移/复制/删除）下沉
  `designer-outline-actions`，插入目标常驻 foot；工程面板保留为底部状态行。
- 中心：文档页签 40（settings.design + 设计/分屏/快照）+ 画布工具 48
  （尺寸 + 主题/密度/DPI/字号/对比/状态/网格/缩放/适合/环境）+
  编辑区（设计/分屏/快照）+ 面包屑 32 + 底栏（默认收起 36，展开 144；
  问题/引用 + 冲突重载/另存/覆盖常驻，折叠不隐藏冲突）。
- 右检查器 280：属性头 + 选中摘要 + 基本/布局/外观分组；布尔量用
  Checkbox（HTML“默认选中”同口径），值通道保持
  `designer-property-field`/`designer-reference-field` 兼容键；
  Spin（数字）/ComboBox（枚举）保持 TextField 兼容 + 原子校验，为后续接线项。
- 工作台 Compact，预览独立 Comfortable（ThemeScope 包裹画布）；标尺隐藏
  （guides 默认关）；画布 padding 48；Splitter 左 180–400、右偏好 280
  （232–400，拖动回填，resize/折叠恢复偏好、中心吃增量）、Compact 12 步进、
  双击复位、Esc 先关菜单再退专注；切换均不污染 dirty/undo。
- 标题栏文件/编辑/视图为 popover 菜单（`designer-menu-popover`，Esc 关闭，
  激活后关闭并转发原命令，低频入口与主工具单路径）。
- 运行预览为窗内模态覆盖层（`designer-run-preview-overlay` + scrim +
  `dialog` 角色卡片）：运行/调试按需出现，停止/Esc/遮罩点击隐藏，不再常驻
  第二个 OS 黑窗口；`previewShell` 保留供 headless 测试与独立会话使用。
  覆盖层打开时 Delete/排序/大纲键盘 suspend，预览内 Navigator 返回优先于关闭。

验证：`ctest --test-dir build-debug -C Debug -j4` 1185/1185 通过（含 8 例
`[b]`：几何/分栏拖拽键盘复位专注折叠/rail分屏选择/标题菜单/布尔复选/右栏
resize 偏好/运行预览按需浮层/浮层下结构键隔离）；
`LUMEN_LAYOUT_DUMP=1` 导出 B 元素盒（titlebar 44、main-tools 48、rail 48、
left 224、right 280、document-tabs 40、canvas-controls 48、crumb 32、
bottom 36/144、statusbar 28、splitter 6 轨道）供与 HTML 并排比对。

### 8.1 滚动与拖动补全（2026-10-13 追加）

- 面板 ScrollView 接入应用侧滚动状态（lumen-scroll-design §3.4/§4）：
  `onWheel`/`onScrollDrag`/`onAnimate` 驱动工具箱、工程、源码、属性与运行
  预览五个视口的 `ScrollController`，偏移随 build 写回；滚轮、滚动条拇指
  拖动、轨道翻页、触摸内容拖动与惯性可用，键盘/语义滚动同 sink。此前
  `onWheel` 只认画布平移，全部面板列表不可滚。
- 工具箱拖放补全布局组覆盖：Column/Grid/ScrollView/Splitter 逐类拖入、
  Splitter 按 schema 补足 2 个 Container 子节点、布局容器作为后续插入目
  标、落画布外释放不插入；滚动后面板仍可拖（拖放认领优先于视口滚动，
  interaction §指针）。滚出视口裁剪区的行不可命中（预期语义）。
- 画布拖动换位：Stack 子节点按住本体拖动换位（伪手柄 `drag:<id>` 复用
  resize 会话管线——吸附、父内容区钳制、预览框跟手、一步事务写回
  left/top、Esc 取消、拖动即选中）。Row/Column 等流式父级的子节点位置
  由布局拥有，不认领拖动（§4.2 契约不变）；根节点不可拖。
- `examples/designer/gallery.lumen` 与 `gallery.design` 的 Stack 补定位子
  场景（stack-card + overlay），默认画布开箱即可演示拖动换位。

验证：`ctest --test-dir build-debug -C Debug -j"$(nproc)"` 1190/1190 通过
（新增 `[drag]`/`[scroll]` 用例：工具箱布局组拖入、滚动共存、Stack 子拖动
换位一步事务、流式子节点拒绝）。

### 8.2 默认样例与编辑器默认态（2026-10-13 追加）

- 默认样例（`sample.lumen`，attach 时加载）换为 **540×400 Stack 画板**
  + 全定位子节点（toolbar/title/三张带文本的卡片/Save/Ready）：开箱即
  可拖动换位，Save 按钮不再是流式 Column 的不可拖子节点。尺寸口径：
  1024×768 默认窗口下 fit ≈0.66（16px 正文缩后 ≈10px 可读）；1280×800
  视口下 fit 判定 zoom=1 不位移（headless 单帧交互稳定）。最初的
  1024×768 画板在默认窗口 fit ≈0.35，文本 ≈5px 不可读，已回退。
- 编辑器默认窗口 1024×768（原 1280×800）；未指定 `--file` 时以默认样例
  直接运行（显式指定但加载失败仍按窗口冒烟契约退出）。
- **fit-to-view**：根节点声明 width/height 的文档（如默认画板）加载后
  自适应缩小居中（不放大，留 8px 边；`maybeFitCanvasView`，两帧收敛，
  用户先行缩放/平移则放弃）。自适应流式根保持 zoom 1。
- **参考线默认开**（网格点 + 标尺 + 选中框/手柄；菜单/画布工具可关），
  原"guides 默认关"的 B 落地决策被本条取代。
- 配套框架能力 `Widget.ignorePointer`（布局复制到 RenderNode，
  `hitTestChain` 跳过）：装饰性铬件（网格/标尺/十字对齐线/选中框/尺寸
  标）对指针透明，事件穿透到下方内容；交互手柄不设此标志。此前选中
  十字线整条压在预览上方，会挡住内容点击。
- 字段清单同步：`widgetFieldInventory` +86（`ignorePointer`，Derived）。

验证：`ctest --test-dir build-debug -C Debug` 1191/1191 通过（默认样例
契约用例：1024×768、参考线可见、fit 缩放、Save 拖动 + undo；既有默认
文档用例改为自加载源或动态定位，不再耦合默认样例结构）。

生产接入之前新增对应 headless 验收：属性失败原子性、面板切换焦点恢复、分栏最小宽、
按 DocumentId 的跨区域选择、预览环境不污染 dirty/undo。桌面现场再验 IME、DPI、
键盘和屏幕阅读器。HTML 演示不能代替这些证据。

## 7. 交付与检查

- 单个 `design/designer-ide.html` 内置总览、A/B/C/D、CSS 与 JavaScript，无外部运行依赖；复制或改名该 HTML 后也可直接离线浏览和切换方案。
- 8 种主题/布局组合可直接打开，操作说明内置于每版“设计说明”。
- 浏览器检查目标：1440×1000 八组合、1280×800 与 1024×768 布局边界；总览窄屏。
- 交互检查目标：选择联动、插入/删除/复制/排序、校验失败不提交、撤销/重做、分隔器、源码联动、引用修正、独立预览、主题与密度、快照下载。
- 本次变更不修改 C++ 控件或 Designer 产品逻辑；C++ 既有测试结果仅作工作树验证，HTML 行为以浏览器检查为准。

2026-10-13 合并前验证：Chrome headless 直接通过 `file://` 打开所有入口，
160 项浏览器断言通过（153 项跨方案流程 + 7 项容器/选中可见性/主题边界），
无 JavaScript 异常；完成 1440×1000 八组合、1280×800 八组合、
1024×768 四布局和 390 宽总览检查。JavaScript 语法与本地依赖链接检查通过。`ctest --test-dir build-debug --output-on-failure
-C Debug -j 4`：1177/1177 通过（现有 Debug 构建树）。

同日合并为单 HTML 后，160 项浏览器断言复验通过；另外将文件复制并改名到
仓库外，验证八组合入口、返回总览、方案切换、浏览器前进/后退和主题保留。
没有外部运行资源请求或 JavaScript 异常。内嵌脚本语法检查通过，
同一完整 Debug ctest 再次通过 1177/1177。

### 8.3 拖动换位虚线对齐参考（2026-10-13 追加）

- 拖动换位吸附候选扩展：父框 0/中/尾 + **同父子节点的边与中线**（拖动
  节点的左/中/右、上/中/下分别对齐目标坐标；候选按文档声明收集，与渲
  放缩放无关）。命中阈值（snapThreshold/zoom）内吸附；未命中回落既有
  8px 网格。
- 拖动期间的虚线参考（4px 划/3px 隙装饰条带，`ignorePointer` 指针透
  明）：**原位虚线框**（拖动起点矩形四边）+ **对齐虚线**（命中候选时
  全幅横/竖线，`designer-canvas-drag-dash:*`）。释放/取消即清理。

验证：`ctest --test-dir build-debug -C Debug` 1192/1192 通过（虚线对齐
用例：原始 145 → 吸附兄弟左缘 150 而非网格 144，原位框与对齐线在场，
释放清理）。

### 8.4 工具箱行尾装饰移除（2026-10-13 追加）

- 用户裁定：组头的**数量徽标**与条目行尾的**拖拽点图标**（Dots）不再
  显示——列表更干净，信息（分组归属、可拖性）不依赖装饰表达。拖放认
  领按整行 `designer:toolbox:type` 前缀，行为不变；`design/designer-
  leftpanel-redesign.html` C 版示意已同步（A/B 变体历史稿保留原样）。

### 8.5 插入目标契约补全（2026-10-13 追加）

- `insertNodeType` 落地 §4.1 的叶子回退：所选是叶子（schema
  maxChildren == 0，如 Text/Button）时，插入改到其父容器、紧随所选之
  后；容器已满（单子 Container）或无处可插时状态行给出反馈。此前叶子
  选中时插入静默失败（仅落一条 schema 诊断在底部问题面板），工具箱拖
  放表现为"拖不进去"——真实复现：点击标题文本后从工具箱拖 Column，
  会话正常启动但文档零变化、诊断 +1。
- 新增回归：叶子回退（拖放与点击两条路径，插入位 = 所选后一兄弟）、
  容器满反馈、真实鼠标近似（40 步增量移动 + 步间渲染帧 + 阈值前抖动
  不起会话）。验证 `ctest` 1194/1194。
- Review 修正（2026-10-14）：回退改为逐级向上循环——链上任何一级容
  量都会再检查（叶子在已满 Splitter 内不再产生半静默诊断）；回退插
  入保持所选的槽位归属（`location->slot`，仅 .design JSON 文档可表
  达，正确性由构造保证）。

### 8.6 首帧收敛、缩放档位与画布裁剪（2026-10-13 追加）

- **首帧收敛**：参考线/网格/选中铬件按上一帧画布节点物化，首帧必然
  缺失且原先无人请求第二帧——窗口启动停在无参考线、画板未收敛的首
  帧，直到第一次输入才刷新（用户复现"点一下才显示右侧内容"）。
  修复：首帧取不到画布节点时 `requestRebuildAfterLayout()` 回置一帧，
  runApp 的 `hasPendingFrame` 帧泵自动补齐。加载自动 fit-to-view 移除
  （首帧即终态，也消除了两帧收敛的取点偏差）。
- **缩放档位**（用户契约）：默认 100%；≤200% 步进 25%（125/150/175/
  200），>200% 步进 50%（250…500）；上限 500%、下限 10%。按钮读数走
  `designer-zoom-label`；"适合视图"按钮更名"复位 100%"（语义即复位）。
- **画布裁剪**：框架新增 `Widget.clipContent`（显式声明内容裁剪，与
  滚动视口同源的 RenderNode.clipContent 通道；字段清单 +87）。设计器
  画布声明之——100% 下画板超出可视区的部分按画布边界裁剪，不再越界
  遮挡右栏。
- 配套框架小改：`requestRebuildAfterLayout()` 提供 const 重载（调度
  请求无观察语义，const 构建路径可用）。

验证：`ctest` 1195/1195（新增缩放档位用例；canvas 缩放相关常量
1.21→1.5、×1.1→×1.25 随档位更新）。

### 8.7 容器边界与适合视图（2026-10-14，对照 design/designer-canvas.html）

- **容器边界体系**（独立开关 `designer:canvas-bounds`，默认开，画布工
  具"显示边界"按钮）：页面（文档根画板）虚线边界 + 所选节点的**父容
  器虚线轮廓**与"类型 · key"标注（父为文档根时由页面边界覆盖，不重
  复画）。与网格开关分离；虚线装饰统一 4px 划/3px 隙、指针透明，键
  为 `designer-canvas-page-dash / bounds-dash / drag-dash : 行号 : 段号`
  （矩形 0-3 边、对齐参考线 4/5）。
- **适合视图命令**（`designer:zoom-fit`，画布工具"适合"按钮）：按画布
  内容区缩小居中画板（8px 边距，不放大；放得下时 100% 居中）。缩放
  值为精确适配值，后续 ± 步进吸附回离散档位。"复位 100%"独立成钮。
- **默认样例调整为 352×264**：1024×768 默认窗口的画布内容区约
  364×275，原 540×400 画板在 100% 下声明宽被单轴视口约束钳制（横向
  压扁，即"显示不完整"的另一半根因）；缩到 352×264 后 100% 完整呈
  现且 1280×800 视口同样 zoom=1。更大画板用"适合"全览。
- 交互稿中 **Ctrl+滚轮缩放 / Space+拖动平移** 尚未接入：WheelSink 签
  名不含修饰键，需框架扩展后再做。

验证：`ctest` 1197/1197（新增边界开关/父容器标注、适合命令两用例；
默认样例契约更新为 352×264）。

### 8.8 画布一比一落地（2026-10-14，design/designer-canvas.html）

对照交互稿逐项实现的画布契约（缩放档位制沿用 §8.6 用户口径，不取稿
JS 的 0.1 连续步进）：

- **标尺**（常显）：刻度/标签按设计坐标随缩放与平移重建——8 设计 px
  步距，屏幕间距不足 8px 逐级翻倍（major ×5 / 读数 ×10 同步放大）；
  角标十字；选中中心的 accent 标线（`designer-canvas-ruler-mark-x/y`）。
- **网格点阵**（独立开关 = `designer:canvas-guides`，语义收窄为纯点
  阵）：8 设计 px 栅格随缩放/平移同步，屏幕间距不足 16px 逐级翻倍
  （控制保留树节点量）；原点跟随 pan。
- **三开关**：网格（点阵）/ 容器边界 / 对齐参考线（十字线 + 坐标徽
  标，`designer:canvas-align`）各自独立；**选中铬件常显**不受开关控制。
- **选中铬件一比一**：accent 名字徽标（"类型 · key"，`selection-name`）、
  尺寸徽标（W × H，elevated 底 + accent 边，右下外侧）、坐标徽标
  （X · Y，guide 底色，十字交点，随对齐开关）、8 手柄圆角 2px。
- **边界体系**：页面虚线边界外扩 8px + 页面 caption（页名 · 预览环境）；
  所选父容器轮廓外扩 6px + "类型 · key" 标注。
- **Ctrl+滚轮缩放**（锚定指针，档位步进）与 **Space+拖动平移**（按住
  期最优先认领 `designer:space-pan`，keyUp 复位；输入框内不拦截）。
  框架配套：`WheelSink` 增补 modifiers 末参；`AppShell::keyUp` +
  `ShellConfig::onKeyUp` 通道（keyChar 一并转发，空格归一为字符）；
  `InteractionController::wheel` 链上无任何滚动视口时的应用兜底（禁
  用/放得下的视口维持不消费边界）。
- **fit 同稿**：边距 120/100、上限放大 1.15、垂直 +8 让位 caption；
  锚定缩放（含适合）保持锚点/中心下方设计坐标不动，画布未就绪时退
  化为纯缩放。
- 画布工具：名义尺寸读数（"桌面表单 · W × H lp"）；图例（页面/容器/
  选中/对齐）与操作提示角标（指针透明装饰）。

验证：`ctest` 1200/1200（新增：Ctrl 滚轮缩放与普通滚轮平移、Space
拖动平移与释放复位、铬件徽标/标尺标线/页面 caption/图例契约；适配：
分开关语义、fit 新边距、锚定缩放下的位置断言、resize 会话的档位换算
常量）。状态栏的 X·Y/缩放/环境读数未随本批（状态栏另有契约）。

### 8.9 画布一比一 review 修正（2026-10-14）

像素采样实证（固定为回归用例 `rulers and dashes paint over the board`）
后修掉的三个问题：

- **根节点选中冲刷**：`decoratePreview` 给选中节点画 selectionBackground
  ——文档根恒选中，整个画板被 50% accent 冲刷并盖住标尺。根节点不再
  渲染选中装饰（选中由铬件表达）；非根节点维持既有行为。
- **绘制层级**：标尺/角标/图例/提示与选中铬件（十字线、选框）原在
  预览之下，被不透明画板盖住。现绘于预览之上（全部 ignorePointer，
  命中不受影响）；网格点阵仍在预览之下（舞台背景语义，稿中 artboard
  覆盖其上）。
- **舞台内缩 32px**（kCanvasStageInset）：内容原点让出内嵌标尺（20）
  + 页面边界外扩（8）与上方 caption 的裕量，复位/初始视图画板完整落
  在标尺之外；标尺刻度 0 点与网格原点随内缩对齐舞台原点。铬件/边界
  仍在 stack 坐标系取节点位置（已含内缩），不二次偏移。
- 图像像素回归采样点改为节点 70%/70%：节点中心是选中十字线的落点、
  顶部 20px 在标尺条之下，原 50% 采样踩线。

验证：`ctest` 1201/1201（新增绘制契约用例；marquee/guides 几何用例
随内缩校正）。
