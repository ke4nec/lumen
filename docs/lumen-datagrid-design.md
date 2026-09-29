# Lumen DataGrid 设计（首版契约与桌面增强提案）

> 状态：首版实现（2026-09-24，M14-D 契约切片）+ 2026-09-28 第二批
> 增强（P0 可靠性 + P1 列模型切片，§16）、第三批（双轴几何 + 列宽拖宽
> + Theme.dataGrid token 组，§17）与第四批（多列排序 + 当前格焦点环 +
> 表头复选框三态，§18）。实现
> `include/lumen/widgets/datagrid.h` + `src/widgets/datagrid.cpp`；测试
> `tests/datagrid_tests.cpp`（24 用例：21 datagrid_* + 3 回归）+ token
> 派生用例。§10–15 为增强
> 提案与能力审计；
> [`design/datagrid.html`](../design/datagrid.html) 为增强目标交互稿，
> P0/列管理/复选框列等已按 §16 落地，其余仍为目标态。
> 输入：M14-D 路线图条目、`docs/lumen-collection-controls-design.md`（共享
> SelectionModel/集合行语义）、`docs/lumen-scroll-design.md`（滚动契约）、
> `lumen-visual-system-design.md`（token/4px grid/密度档）。

## 1. 定位与组合结构

DataGrid 是**组合件**（Spin/Toolbar 先例）：零新增 WidgetType、零布局引
擎改动。整网格 = `Column[表头行, List(行虚拟化)]`；行 = 集合行
（collectionRow 语义/选中/焦点环与 List 一致）内嵌 `Row[固定宽单元格]`。

```
DataGridController::build()
└─ Column (key = owner)
   ├─ Row (key = owner:header, 高 36)      ← 表头：排序点击/指示器
   │  ├─ Text 列头 ×N（宽 = column.width）
   └─ List (source = controller, flex 1)   ← 纵向虚拟化复用 VirtualList
      └─ 行 ×可见区（key = owner:item:<rowKey>）
         └─ Row[Text|TextField ×N（宽 = column.width）]
```

- 列宽为**固定像素**（`DataColumn.width ≥ 40`）；总宽超出视口时由应用
  包横向 ScrollView（首版契约；水平虚拟化见 §8）。
- 行高走 VirtualList 估算/实测缓存（默认 40 = 视觉系统 Medium 档）。

## 2. 列模型

`DataColumn{key, header, width, resizable, sortable, editable}`：

| 字段 | 语义 |
| --- | --- |
| `width` | 固定像素宽；`resizeColumn` 钳制到 ≥40（`kMinColumnWidth`） |
| `resizable` | false 时 `resizeColumn` 拒绝（锁定列） |
| `sortable` | 表头可点击排序 + 指示器；false 退化为静态文本 |
| `editable` | `beginEdit`/Enter 编辑入口是否接受该列（只读列） |

`columnWidths()` 供应用持久化回填。

## 3. 数据与身份

- `setRowCount` + `setCellText(row, colKey)`（显示/TSV 复制/编辑初值同
  一来源）。
- 行 stable key：`setKeyOf`（默认 `r<row>`）——选择集/焦点/语义身份的
  唯一依据（与 List/Tree 一致）。
- `setRowEnabledOf`：禁用行不参与选择/编辑/激活。

## 4. 表头与排序

- 表头单元 = 按钮化文本（onClick 身份 `grid:<owner>:sort:<col>`，经
  `addRowClickSink` 的 grid 前缀分发，修饰键忽略）。
- 排序状态由网格维护：`sortColumn()`/`sortAscending()`（表头指示器
  `▲/▼` 同源）；`requestSort(col)` 切换方向并触发 `onSortRequest(col,
  ascending)`——**数据重排由应用执行**，完成后 `setRowCount`/重建。
- 筛选：回调契约 `onFilterRequest`（应用提供过滤 UI 入口），网格不内
  置过滤界面。

## 5. 选择（共享 SelectionModel）

语义与 List（collection-controls-design §6.5）完全一致：current（焦点
行）与 selected（选中集，按 stable key）分离；四模式
None/Single/Multiple/Extended；Ctrl+单击切换、Shift+单击/移动区间；行
点击/键盘移动/`setCurrentKey` 同路径。

## 6. 键盘与编辑

| 键 | 行为 |
| --- | --- |
| Up/Down/Home/End/PageUp/PageDown | 移动 current（Extended 随动=替换）+ 滚动对齐（与 List 同源实现） |
| Left/Right | 移动列焦点（编辑目标列 `currentColumn`） |
| Enter | 进入编辑（当前行 × 当前列；只读列回退 `onRowActivated`） |
| Escape | 取消编辑 |
| Ctrl+A | 全选（Extended）/选中 current（Single） |
| Ctrl+C / Ctrl+V | TSV 复制 / 粘贴分发 |

编辑契约：

- `beginEdit(row, col)`：col 必须 `editable`；编辑器初值 = 当前单元格文
  本（state key `owner:edit`）；已有编辑先提交。
- 编辑器 = 树内 `text_field`（宽与列对齐），焦点在字段内——**提交 =
  `commitEdit()`（点击其他单元格/应用调用）或取消 = `cancelEdit()/
  Escape`**；编辑期间字段自身消费文本键。
- `setCellValidator(col, fn)`：fn 返回空串=通过；失败保留编辑态 +
  `editError()`（应用展示；编辑器 placeholder 同源显示）。
- `commitEdit()` 通过 → `onCellEdited(row, col, text)`（应用写数据 +
  重建）。
- Key 枚举无 F 键：编辑入口契约 = Enter + `beginEdit` API。

## 7. 剪贴板（TSV）

- `copySelection()`：选中行**按行序**、列按 `columns()` 顺序拼 TSV 写宿
  主剪贴板（单元格内 `\t\n\r` 替换为空格；返回写入行数）。
- `pasteRows()`：读剪贴板 → 按 `\n`/`\t` 解析 → `onRowsPasted(rows)`
  （应用校验/应用数据后 `setRowCount` + 重建；回调为整体替换语义）。
- 剪贴板经 `InteractionController::clipboard()`（宿主注入；未注入 =
  no-op false）。

## 8. 语义（无障碍）

- 行 = `listItem` role + Focus/Activate action（与 List 一致）。
- 单元格文本 = Text role（屏幕阅读器逐格可读）；表头排序单元 =
  `button` role。
- 后续增量：Grid/Table 专属 role 与单元格级焦点导航（AT-SPI Table
  接口），随 provider 细化评估。

## 9. 已知限制（后续增量，不破坏本契约）

- **水平虚拟化/双轴滚动协调**：列固定像素宽 + 应用包横向 ScrollView；
  列数极大时的水平虚拟化、双轴滚动条协调未实现。
- **RTL 镜像**、**行内编辑富化**（多行/下拉单元编辑器）、**拖放**、
  **列重排**：独立增量。
- 编辑器无失焦自动提交（TextField 无 blur 事件契约）；应用按需在路由
  切换等时机调用 `commitEdit()`。

## 10. 2026-09-28 能力审计与增强提案

本节至 §15 是**桌面增强设计**（M14-D 出口内的实施切片），其中 P0 与列
模型/复选框列切片已于同日落地（见 §16 实现记录）。
配套 [`design/datagrid.html`](../design/datagrid.html) 已从首版静态标本更新为
通用桌面业务数据表的交互评审稿。§1–9 保留首版设计背景；与代码的差异以
本节审计 + §16 为准。范围为 Windows/Linux/macOS；Touch density 指桌面
触屏，移动端继续延期。

### 10.1 当前实现与证据

核对 `include/lumen/widgets/datagrid.h`、`src/widgets/datagrid.cpp`、
`tests/datagrid_tests.cpp`；现有 6 项测试覆盖的是首版控制器契约切片。

| 能力 | 现有实现 | 缺口 / 后续设计 |
| --- | --- | --- |
| 组合 / 大行数 | Column[header, List]、VirtualListSource 行虚拟化、stable key、scrollToKey | 未内建双轴视口、列虚拟化与冻结列；HTML 小样本不证明性能 |
| 列模型 | key/header/width/resizable/sortable/editable，宽度 ≥40；columnWidths 可读回 | resizeColumn 是 API，表头未提供拖动手柄；无显隐、重排、冻结状态模型 |
| 排序 | 单列升/降状态、按钮化表头、onSortRequest | 不重排应用数据，无清除排序、多列排序与数据类型比较器 |
| 筛选 | onFilterRequest 回调声明 | 实现内无调用入口或筛选面板；搜索、条件模型、无结果状态待补 |
| 选择 | SelectionModel 四模式；current / selected 分离；键盘范围选择 | attach 中行点击 sink 调用 rowClicked(key, false, false)，鼠标 Ctrl/Shift 未透传；无复选框 UI |
| 列焦点 | Left/Right 修改 currentColumn | 文本单元格无独立点击身份或可见当前格样式，鼠标点击不能定位编辑列 |
| 编辑 | 树内 TextField、beginEdit、commitEdit、cancelEdit、同步 validator、回调 | beginEdit 仅重建编辑器，未显式转移焦点；编辑态 Enter 返回 false，由字段/应用消费，不等同于网格提交 |
| 校验 | 显式 commitEdit 失败时保留 editing 和 editError | beginEdit 忽略已有编辑的提交失败并覆盖草稿；rowClicked 忽略失败继续改变选择；placeholder 不能替代常驻错误消息 |
| 剪贴板 | 所选整行按原行序/原列序转 TSV；读剪贴板后解析并发出 onRowsPasted | 无单元格矩形选区、粘贴锚点、目标只读检查、事务回滚、CSV/Excel 导出 |
| 语义 / 状态 | listItem 行、Text 格、button 排序头；固定 No rows 空态 | 无 Grid/Table 行列元数据；缺少加载/失败/无结果分离及完整编辑错误语义 |

`datagrid_selection_follows_clicks_and_keyboard` 实际以 setCurrentKey 和
handleKey 验证选择，没有合成修饰键鼠标事件；编辑测试以 shell.state 和
commitEdit 验证校验。不得据测试名称推断指针路径、输入焦点或真实平台已验收。

### 10.2 常见桌面 DataGrid 对照

资料查阅日期：2026-09-28。以下为官方文档功能归纳，不引入外部依赖。

- [WPF DataGrid](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/controls/datagrid)：
  类型化列、编辑/校验、选择、列尺寸/重排和模板；配合数据视图进行排序、筛选、
  分组。启发：将浏览、定位、编辑、反馈设计成完整路径。
- [WPF 键鼠行为](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/controls/default-keyboard-and-mouse-behavior-in-the-datagrid-control)：
  区分导航、选择扩展和编辑态按键。Lumen 不直接照搬全部快捷键；保持既有
  Home/End 行移动，F2 需要先扩展 Key 枚举。
- [Qt QTableView](https://doc.qt.io/qt-6/qtableview.html)：model/view 与 delegate
  分工，行列显隐、尺寸和单元格导航。启发：原始数据、展示和编辑器分开，应用持有数据。
- [AG Grid 功能目录](https://www.ag-grid.com/javascript-data-grid/key-features/)：
  桌面 Web 业务表补充参考，覆盖列冻结、工具面板、分组聚合、分页、服务端数据和导出。
  高级分析并非所有原生桌面网格的默认能力，需独立判断范围。
- [WAI-ARIA Grid pattern](https://www.w3.org/WAI/ARIA/apg/patterns/grid/)：
  网格内部导航、复合控件焦点和编辑器交互参考；浏览器 ARIA 示例不代表 Lumen
  UIA/NSAccessibility/AT-SPI 原生 provider 已具备等价实现。

## 11. 增强目标与组合边界（提案）

目标用户是需要高密度查看和处理记录的桌面业务用户。默认是**行选择 + 当前单元格**，
不是电子表格。DataGrid 负责视口、列呈现、选择、焦点、编辑生命周期和请求事件；
业务工具栏、网络、权限、数据写回、持久化、导入导出由应用组合。

推荐结构：应用工具栏 / 筛选条件条 → 可选批量操作条 → 固定表头 → 数据视口 →
汇总状态栏。冻结区与滚动区必须共享行高、纵向 offset 和行身份；表头与数据区
共享横向 offset。不能通过互不协调的嵌套 ScrollView 实现冻结。

### 11.1 列与数据模型方向

以下描述是概念契约，未承诺具体 C++ 符号或 ABI：

- 保留 rowKey / columnKey 稳定身份；有排序或筛选时由应用提供 stable key，
  默认 r<index> 不足以维持重排前后的实体身份。
- 列定义区分原始类型、display formatter、copy formatter、比较/筛选策略、
  renderer/editor factory、readonly/enabled 和校验函数。
- 显示字符串不可作为金额/日期排序与校验的唯一数据来源。null、空串、0、
  加载占位明确分开；本次示例金额按 CNY 展示。
- 列布局状态含 width/min/max、visible、displayOrder、pinned；应用按稳定列 key
  序列化，未知 key 忽略，缺失列采用默认值，支持版本和恢复默认。
- 控件宽度下限仍允许 ≥40；业务列可有更大的 minWidth。原型中订单列 148、
  客户列 236、状态/负责人 112、金额 136、进度 116、日期 124、备注 248 logical px，
  均为示例配置，非全框架常量。订单标识列始终可见且排在数据列首位。
- 长内容默认单行省略并可读取完整值；自动换行/动态行高必须与虚拟行测量缓存协同。

### 11.2 搜索、排序和筛选

- 表头普通点击使用升序 → 降序 → 无排序循环；普通点击替换排序列表，Shift 点击
  追加/更新该列为最低优先级，移除后序号连续。相等值保持源顺序。
- 搜索默认匹配应用指定字段；原型为订单、客户、项目、负责人、备注，不隐式搜索金额。
- 类型化筛选候选：文本包含/等于、数值比较/范围、枚举多选、日期区间、空值条件。
  原型实际实现状态等于、负责人等于、金额下限，条件之间 AND，并与搜索、视图 AND。
- 条件编辑在面板内暂存，“应用”才改变数据视图；“重置条件”仅清空面板草稿。
  已应用条件用标签显示，可逐项移除；无结果保留条件并提供清除入口。
- 所有视图变化先提交当前编辑；校验失败阻止变化并聚焦错误格。
- 远程模式传递请求描述与请求序号，由应用取消/忽略过期结果。UI 只在结果确认后
  更新数据和计数，不将页内排序冒充全量排序。

### 11.3 选择与操作范围

- 单击数据格替换行选择并设置 current cell；Ctrl（macOS Command）单击切换该行；
  Shift 单击、Shift 上下键以 anchor 在当前视图可用行中扩展连续选择。
- 复选框独立切换行选择；表头复选框为无/部分/全选三态，作用于当前筛选结果的
  可用行，不把“可见”误解为屏幕内已物化的行。
- 默认上下/Home/End/PageUp/PageDown 移动 current 并随动行选择；左右只移动列。
  Ctrl/Command + 导航仅移动 current。禁用行不可选择、编辑或激活；只读单元格
  仍可聚焦与复制。
- 筛选隐藏已选记录时保留其 key，展示“视图内已选 / 另有隐藏”数量。原型复制与
  批量操作只作用于视图内所选可用行；导出当前视图包括禁用只读行，按当前列序。
- 数据删除移除失效 key；current 落在邻近可用行。分页选择范围必须显式标记
  “本页”或“全部查询结果”，不能默认承诺尚未获取的记录已全部选中。
- 现有 copySelection 原始列序/所有选中行语义继续保留；增强 API 需显式增加
  scope 与 column order 参数或独立入口，不静默改变既有接口。

## 12. 视觉规格与 token（提案）

来源：视觉系统 §3.1/§3.2/新增 §3.3、集合控件的选中/焦点语义、CoreDark
主题方向工厂。组件颜色只引用 Theme 语义角色；HTML 将精确参考值收束于
`.themed` 的 CSS 变量，评审板自身颜色不作为控件色板。

| 部件 | token 来源 / 提案 | Compact / Comfortable / Touch |
| --- | --- | --- |
| 数据行 | metrics.minHeight | 32 / 40 / 48 |
| 表头 | 拟新增 dataGrid.headerExtent | 32 / 36 / 44；标准档保留首版 36 |
| 格内边距 | metrics.controlPaddingX | 8 / 12 / 16 |
| 编辑器圆角 | metrics.controlRadius | 4 / 6 / 8 |
| 容器圆角 | metrics.cardRadius | 8 |
| 选择辅助列 | 拟新增 dataGrid.selectionColumnWidth | 44 / 44 / 52；命中区域至少行高 |
| 列宽手柄 | 拟新增 dataGrid.resizeHitWidth | 8 / 10 / 16；可键盘操作 |
| 正文 / 辅助字 | typography.body / typography.caption | 14 / 12，跟随 fontScale |
| 选中标记 | list.markerWidth / markerInset | 3 / 4 |
| 焦点环 | metrics.focusRingWidth + colors.focusRing | 2；高对比按 Theme 派生 |
| 冻结分界 | colors.borderStrong | 1px 实线，不依赖阴影 |

- 背景 surface；表头/hover 为 surfaceSunken；pressed 为 surface/accent 0.32
  混合；selected 为不透明 accentContainer；分隔线 borderDefault。
- 正文 contentPrimary，辅助文字 contentSecondary；状态使用 statusSuccess /
  statusWarning / errorContent，并配状态文字、图形，不能单靠颜色。
- 默认横向分隔线，不加斑马纹或竖线；密集数值场景允许可选竖线。金额和日期
  右对齐、数字等宽，金额不在每格重复币种，列头声明单位。
- selected 与 current 独立。进入单元格导航时只绘制当前格内嵌环，显式
  showFocusRing=true；不在一行所有格同时绘制焦点框。
- invalid 控制编辑器边框、错误文字和语义；错误浮层需边界避让，不参与固定行高
  布局、不挤动相邻行。框架实现需使用覆盖层，不能被 ScrollView 裁掉。
- 字体缩放对字、行高、padding 和图标派生一次；DPI 不改变逻辑尺寸。
  高对比与减少动画继续走 Theme，不在 DataGrid 中分平台写颜色。

## 13. 编辑、键盘和无障碍（提案）

### 13.1 编辑事务

状态机：idle → editing → validating → committing → idle；校验失败回到
editing+error，异步提交失败进入 draft+saveError（异步部分待后续契约）。

- 进入：双击或 Enter；先确认 row/column 可用、可编辑，记录 oldValue 与 stable key，
  挂载编辑器并显式请求焦点。F2 暂不纳入首版入口。
- 退出：Enter 提交，Esc 恢复 oldValue；Tab/Shift+Tab 先提交，再移动到下一/上一
  可编辑格（跨行，跳过禁用行）。到边界提交后回到当前格，导航态 Tab 可离开网格。
- 点击其他格、排序/筛选/切列等先提交。**commit 失败必须中止后续动作**，不能
  改变 current、丢弃编辑器或重置草稿。现有 beginEdit / rowClicked 优先修复此点。
- 输入法 composing 时 Enter 留给输入法；编辑器内的文本选择、方向键和剪贴板快捷键
  由编辑器消费，不能被网格抢走。
- 文本/数值/枚举/日期编辑器共享该生命周期；原型仅演示金额、状态、负责人、备注。
  金额范围 0–999,999,999.99、最多两位小数；备注最多 160 字符，是示例校验规则。
- 滚动虚拟化必须把草稿放在稳定状态模型中，固定编辑项或按明确策略提交/取消，
  不能因 widget 回收丢输入。异步保存使用 edit generation，避免旧回包覆盖新草稿。
- 批量粘贴和撤销是独立事务：解析 TSV → 映射锚点/可见列 → 全量校验 → 一次提交；
  错误单元格、只读目标、越界、部分成功/整体失败策略必须明确。本轮不实现。

### 13.2 导航与语义

HTML 示例用 role=grid/row/columnheader/gridcell、aria-rowindex/colindex、
aria-selected、aria-readonly、aria-disabled、aria-sort 与 roving tabindex。
可用数据格只有一个 Tab 入口；表头排序和可调分隔线分别可聚焦。右键菜单可用
上下/首末、Enter、Esc 操作，关闭恢复焦点。筛选/列设置面板保持焦点循环。

C++ 后续应提供 Grid/Table 专属语义、行列计数与索引、表头关联、排序描述、单元格
Focus/Edit 动作；虚拟化索引反映逻辑数据而非 widget 数量。选中数量和校验错误
应被宣布，不能通过 placeholder 模拟错误。Windows UIA、macOS NSAccessibility、
Linux AT-SPI 分别验收；HTML 的 ARIA 不是原生 provider 验收替代品。

## 14. 数据状态、规模和延后能力

| 状态 | 呈现与动作 |
| --- | --- |
| 首次加载 | 表头 + 骨架；不计作真实数据行；禁止批量/导出；成功后建立 current |
| 尚无数据 | 数据集为空；给出业务创建/导入入口，由应用实现 |
| 筛选无结果 | 保留条件，展示清除搜索与筛选入口 |
| 首次请求失败 | 错误文字 + 重试；不伪装成空数据 |
| 刷新失败 | 保留旧结果，注明可能过期；允许重试，不清除 selection/draft |
| 局部保存失败 | 保留草稿、标记具体格，提供重试和恢复原值 |

默认先支持已加载数据的连续虚拟滚动。远程分页与增量加载是互斥的主导航选择，
由应用按数据源能力配置；总数未知时显示“已加载 N 条”，不能制造估算总页数。
表尾汇总明确当前视图、本页或选中范围；跨页全量聚合由应用/服务器提供。

P2 再评估水平虚拟化、分组/汇总、列分组、树表/明细组合、矩形选择、粘贴事务、
撤销重做；树形数据要与 TreeList 分工。公式引擎、透视、填充柄、图表、跨表拖放、
Excel 专有格式与打印属于按需扩展，非基础控件承诺。

## 15. HTML 交付与验收建议

直接用现代桌面浏览器打开 `design/datagrid.html`，无需服务端、网络、字体 CDN
或构建。支持深/浅/高对比示意、三档密度、真实 48 行本地数据的搜索/AND 筛选、
三态/多列排序、选择与键盘导航、表头和订单列冻结、列拖宽/键盘调宽/显隐/重排、
数值与枚举编辑校验、右键详情、复制 TSV、CSV 下载和数据状态切换。

数据和布局仅保存在当前页面内存；刷新或“恢复初始示例”会复原。剪贴板权限
不足时展示可手动复制的 TSV。CSV 以当前视图和可见列导出。原型不实现服务器、
真正虚拟化、矩形选区、粘贴、分组、分页、原生 provider 或性能基准。

建议实施顺序与验收：

1. **P0 可靠性**：真实指针 Ctrl/Shift 路径；点击格定位列；编辑焦点/提交；失败拦截。
   覆盖无效输入后切格/切行/排序、IME Enter、禁用与只读、稳定 key 重排。
2. **P1 日常工作流**：双轴几何/冻结、列管理、搜索筛选/多排序、类型化呈现/编辑、
   状态壳和 Grid 语义。验证滚动到两端、拖宽后对齐、隐藏当前列、选中被筛掉、
   高对比/三档密度、100–200% 字体和 CPU/Skia 同契约；三桌面原生键鼠人工验收。
3. **P2 规模与组合**：十万行/百列为候选压力场景，以实际设备测量确定物化量、帧时、
   内存和响应预算；禁止把本 HTML 的 48 行浏览器体验作为性能结论。

## 16. 2026-09-28 第二批实现记录（P0 可靠性 + P1 列模型切片）

按 §15 建议顺序落地 P0 全部四项与 P1 的列布局/选择辅助列切片；C++ 变更
集中在 `widgets::DataGridController` 与两处框架接缝（见下）。既有 6 用例
全部保留（排序用例扩展为三态循环），新增 7 用例覆盖新契约。同日实现
review 以真实指针路径复测发现并修复三处问题（各配回归用例）：编辑器内
点击/双击误提交草稿（行点击冒泡路径缺编辑器按压守卫）、编辑态 Tab 不
提交（§13.1 Tab 先提交再移动）、列显隐/重排不先提交。修复后新增 3 回归
用例，共 16 用例，全仓 812 项测试通过（Debug + Release）。

### 16.1 P0 可靠性（§15-1 全部落地）

| 项 | 实现 |
| --- | --- |
| 修饰键透传 | 行点击 sink 统一解析 `grid:<owner>:{row,cell,check,checkall,sort}:…`，Ctrl/Shift 经 `InteractionController::pointerModifiers()` 读取（与 List `dispatchRowClick` 同源）；不再固定 false/false |
| 单元格命中定位列 | 每个数据格携带 onClick/节点 key `grid:<owner>:cell:<rowKey>:<colKey>`（colKey 末段匹配列集合，行/列 key 允许含 `:`）；格点击 = 行选择 + 列焦点；双击格直接进入该格编辑（激活 sink 同一解析） |
| 提交失败拦截 | `beginEdit`：已有编辑提交失败返回 false，不覆盖草稿；同一格重复进入 = no-op true。`rowClicked`/`requestSort`/`toggleRowSelection`/`toggleSelectAll`/`pasteRows`/`activateRow` 一律先提交，失败中止且不改选择/排序/焦点（§13.1） |
| 编辑焦点与 Enter 提交 | `beginEdit` 经新增 `InteractionController::requestFieldFocus(bind)` 登记、`AppShell::rebuildIfDirty` 布局后 `applyPendingFieldFocus(root)` 落地——`focusedBind` 建立、光标置末尾，文本输入/IME 无需先点击编辑器。编辑态 Enter 提交（`composingActive` 期间不消费，留给输入法）；Escape 取消；Tab/Shift+Tab 先提交再移动到下一/上一可编辑格（`moveEditor`：跨行、跳过禁用行、滚动对齐；提交失败中止移动，边界提交后停在当前格）；提交/取消经新增 `releaseFieldFocus()` 释放编辑焦点并把焦点放回行节点。编辑期间 Ctrl+C/V、方向键不再被网格抢走（编辑分支先行返回 false）；**编辑器内的点击/双击不打断草稿**——按压落在编辑字段（`focusedBind == owner:edit`）时行点击/激活路径直接返回，不提交不改选择 |

### 16.2 P1 列模型与选择辅助列切片

- **排序三态**：单列点击循环 升序 → 降序 → 清除（§11.2）；清除时
  `onSortRequest("", true)`，应用恢复源顺序。多列排序（Shift 追加）仍为
  目标态。
- **列模型扩展**（`DataColumn` 新增 `minWidth`/`align`/`visible`）：
  `resizeColumn` 钳制到 `max(minWidth, 40)`；`setColumnVisible`/`moveColumn`
  管理显隐与显示顺序（列向量顺序即表头/单元格/TSV 列序），**变更前先提交
  编辑，校验失败中止**（§13.1 切列先提交）；隐藏当前列时列焦点回退首个
  可见列；Left/Right 与 `setCurrentColumn` 只走可见列。
  `copySelection` 保留首版语义（全部列、原始列序——含不可见列）；仅可见
  列/当前视图的范围变体是显式新 API，不改既有接口（§11.3）。
- **选择复选框列**（§12 选择辅助列，Medium 档 44px 常量）：SelectionMode
  != None 时表头与每行首列渲染复选框格。Checkbox 不带 bind（框架 toggle
  不接管），状态由选择集重建；整格 44px 命中（wrapper onClick）。行复选
  独立切换选择、不改 current（§11.3）；表头复选框全选/清空当前可用行
  （selectableKeys 缓存摊销 O(n)，数据装配变化时失效）。**三态
  indeterminate 视觉未实现**（Widget.checked 为布尔），部分选中显示为未
  勾选。
- **格呈现**（§12）：单元格固定列宽 + 水平内边距 12（controlPaddingX
  Medium 同值）+ 单行省略（maxLines=1 + Ellipsis）；`align=End` 的数值/
  日期列经 Row 主轴 End 右对齐（Text 无段内对齐）。表头可排序列 =
  Ghost 按钮 + ChevronUp/Down 指示（TreeList 同口径）；**数值列表头仍左
  对齐**（Button 内容对齐待框架扩展）。禁用行整行禁用（含复选框）。
- **自定义空态**：`setEmptyBuilder`（§14 状态壳由应用组合——加载骨架/
  错误重试/无结果）；默认 "No rows" 不变。

### 16.3 框架接缝（core，最小增量）

- `InteractionController::requestFieldFocus(bind)` /
  `applyPendingFieldFocus(root)` / `releaseFieldFocus()`：程序化编辑焦点
  的登记/落地/释放（落地由 `AppShell::rebuildIfDirty` 在 `root_` 更新后
  调用；一次性——未命中即清除，编辑可能已被取消）。
- 其余零新增 WidgetType、零布局引擎改动（组合件边界保持）。

### 16.4 已知限制（§9 增补，不破坏契约）

- ~~列宽拖动手柄/键盘调宽、双轴滚动协调~~：已由第三批落地（§17）；
  冻结列与水平虚拟化仍为后续增量（`resizeColumn` API 不变）。
- ~~当前格无可见焦点环~~：已由第四批落地（§18.2 统一格式盒内嵌环）；
  Grid 专属语义随 §13.2 provider 评估。
- 编辑错误仍以编辑器 placeholder + `invalid` 边框呈现（错误浮层需 overlay
  边界避让，后续增量）。
- ~~表头复选框无 indeterminate 三态视觉~~：已落地（§18.3，框架
  `Widget.indeterminate`）；数值列表头不右对齐。
- 行高随内容实测（集合行 chrome + 内容高）；HTML 的 32/40/48 是目标
  token 化规格（表头高/选择列宽/手柄带宽已于 §17 token 化，行高待列
  间距语义一并评估）。

## 17. 2026-09-28 第三批实现记录（双轴几何 + 列宽拖宽 + token 组）

P1 切片续：HTML 增强稿的「列拖宽/键盘调宽」「共享横向视口」与视觉
系统 §3.3 的 `Theme.dataGrid` token 组落地。C++ 变更：`style::DataGridTokens`
（theme.h/cpp）、三处框架接缝（layout/app）与 `DataGridController` 本体。

### 17.1 Theme.dataGrid token 组

`DataGridTokens{headerExtent, selectionColumnWidth, resizeHitWidth}`，
`dataGridTokensFrom(density)` 烘焙三档（32/36/44、44/44/52、8/10/16），
`scaleComponentSizes` 参与 fontScale 派生；高对比方向工厂同样接线。
**Comfortable 档与第二批控制器常量等值——既有像素输出零变化**。颜色
继续沿用 list/集合行 token（表头 surfaceSunken、分隔线 borderDefault），
本组只承载网格几何。控制器经 `shell_->theme()` 读取（无 shell 时退回
默认档）。

### 17.2 双轴几何（共享横向视口）

- `build()` 根 = 横轴 `ScrollView`（key = owner）：`Column[表头, List]`
  整体为内容，表头与数据区**天然共享横向 offset**（同一下滚动域）；
  表头/列表/行全部取 `gridWidth()` 显式宽（= 选择列 + Σ可见列宽；
  内容窄于视口时铺满视口，行背景完整）。
- **源视口接缝**：ScrollView 携带 `virtualSource = HorizontalViewportSource`
  （新框架能力：`layoutScrollView` 布局期对源视口调 `updateViewport`
  喂视口主轴尺寸——镜像 `layoutVirtualList` 契约）。此后滚轮/拖动/
  惯性/滚动条/语义滚动由交互层直驱 `hScroll()`，**应用零接线**；
  `hScroll_` 仅供程序化定位。
- 双轴互不抢占：纵向滚轮归数据列表，横向归根视口；Shift+纵轮在表头区
  （命中链无纵向视口）按既有契约投影到横向视口（scroll-design §4）。
- 视口宽跟踪：`updateViewport` 记录视口宽并经 `requestRebuildAfterLayout`
  请求一次性收敛重建（布局期 markDirty 会被 `rebuildIfDirty` 收尾清脏
  吞掉；两帧收敛，VirtualList extent 修正同模式）。

### 17.3 列宽拖动手柄（splitter 通道复用）

- 可调整（`resizable`）列的表头右缘物化手柄（`grid:hnd:<col>`，
  Ghost Button + `splitterSource`）：**拖动跟手**（绝对边界位置 →
  `resizeColumn`，内部钳 minWidth）、**双击复位**（回 `setColumns` 初始
  宽）、**键盘 Left/Right 步进**（步长 = resizeHitWidth；Home 收缩到
  minWidth，End 无上界不动作）、ResizeEW 悬停光标；Tab 可聚焦
  （collectionRow），语义 role=splitter + 当前宽 value。
- 手柄带宽计入列宽预算（表头单元 = 列宽 − 手柄带宽），与数据格列边界
  对齐的不变式保持。
- 拖动/步进先提交编辑（§13.1）：`resizeColumn` 增加提交守卫，校验失败
  列宽不动、草稿留在错误格；手柄逐拍调用在无编辑时幂等直达。

### 17.4 框架接缝（core，最小增量）

- `layoutScrollView` 支持源视口（`virtualSource` 的 `updateViewport`
  喂视口；`makeNode` 通用拷贝 `splitterSource`——`layoutSplitter` 容器
  节点显式清零，拖拽锁定只允许命中分隔条/手柄本体）。
- `AppShell::requestRebuildAfterLayout`：布局期重建请求挂起通道
  （`rebuildIfDirty` 收尾回置 dirty，下一帧收敛；一次性）。

### 17.5 测试与已知限制

新增 5 个网格用例（横向视口表头/行同源平移 + 双轴互不抢占 + Shift
投影、窄内容铺满 + 密度切换表头高、拖动钳制 + 双击复位、键盘步进 +
Tab 序、拖宽提交守卫）与 1 个 token 派生用例；修复一处双击用例的
时间戳回退（tick 为绝对时间）。`tests/datagrid_tests.cpp` 扩至 21 用例；
`ctest -R datagrid` 19/19（含 token 派生用例）、全量 818/818
（Debug + Release）。基准 card-grid 帧哈希与本批前工作树一致
（`42fe122e01f706f6`，两跑确定性复现；接缝对无 splitterSource/
virtualSource 的既有场景为 no-op）。

剩余增量：水平虚拟化（列虚拟化）、冻结列（需冻结区与滚动区共享几何
的显式模型）、RTL 镜像、拖放、Grid 专属语义。

## 18. 2026-09-28 第四批实现记录（多列排序 + 当前格焦点环 + 表头三态）

P1 切片续（控制器层 + 一处框架控件扩展）。C++ 变更集中在
`DataGridController` 与 Checkbox 三态链（Widget/resolver/painter/语义）。

### 18.1 多列排序（§11.2）

- 状态模型：`std::vector<SortKey>`（向量序 = 优先级，front 为主排序），
  替代单列 sortColumn_/sortAscending_；`sortKeys()` 读取，
  `sortColumn()`/`sortAscending()` 保持为单列兼容视图（front 或空）。
- `requestSort(columnKey, extend = false)`：普通点击 = 单列循环
  升序 → 降序 → 清除（该列已是唯一排序列时延续循环，否则收敛为单列
  升序——多列在位时"普通点击替换排序列表"）；extend（Shift）= 追加/
  更新该列为最低优先级，升序 → 降序 → 移除（移除后序号自然连续）。
  指针路径按修饰键透传 extend。既有单列测试逐位兼容。
- 回调：`onSortRequest`（主排序键，既有接线不变）+ 新
  `onSortRequestMulti`（完整多列状态）；相等值保持源顺序由应用执行
  （稳定排序）。`setColumns` 移除失效排序键。
- 已知限制：多列优先级数字标记（"1/2"角标）待 Button 内容通道扩展，
  表头指示暂只有方向 chevron。

### 18.2 当前格焦点环（§12）

- 单元格统一格式盒：全部数据格包 `Container`（key `<cellId>:box`），
  Start 列盒内是省略文本、End 列盒内是主轴 End 的 Row。当前格
  （current 行 × current 列）的盒边框承载 `colors.focusRing` +
  `metrics.focusRingWidth`（内嵌环，画在格边界内不占布局空间；格内
  边距 12 > 环宽）。编辑中的格不显示环（编辑器自身表达焦点）。
- **格式盒无条件存在**（环样式按当前格开关）：环的出现不得改变格的
  结构 identity——否则双击检测/damage 按身份配对失效（实现 review
  实测发现：按状态插拔包装容器后第二次点击 identity 变化、双击进
  编辑失败，回归用例锁定）。
- 代价：每物化格 +1 Container 节点（当前格判断 O(1)；无基准场景
  回归）。

### 18.3 表头复选框三态（§11.3 / §16.4 收口）

- 框架 Checkbox 扩展（声明式，无 bind 通道）：`Widget.indeterminate`
  → RenderNode → resolver（`CheckboxResolvedStyle.indeterminate`，
  checked 优先）→ painter（accent 填充 + 居中横线替代勾号，线厚 =
  指示器 12% 向上取 1px）→ 语义（value="mixed"，不带 checked flag）。
- `buildHeaderCheckCell`：全部选中 = 勾选；部分选中 = indeterminate；
  无可用行 = 未勾选 + 禁用（既有）。

### 18.4 测试与已知限制

新增 3 用例（多列追加/循环/移除/普通点击收敛/指针 Shift 透传/列集
收缩清键、当前格环跟随与 identity 稳定回归、表头三态 + 语义
value="mixed"/"true"）；`datagrid_virtualizes_rows...` 与
`datagrid_numeric_column...` 的格结构断言更新为统一格式盒。datagrid
22/22、全量 821/821（Debug；Release 823/823 含 2 项基准完整性用例）。

剩余增量：水平虚拟化、冻结列、RTL 镜像、拖放、Grid 专属语义
（§13.2 provider）、多列优先级角标。
