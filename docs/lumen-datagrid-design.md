# Lumen DataGrid 设计（M14-D 首版契约）

> 状态：首版实现（2026-09-24，M14-D 契约切片）。实现
> `include/lumen/widgets/datagrid.h` + `src/widgets/datagrid.cpp`；测试
> `tests/datagrid_tests.cpp`。视觉基线 [`design/datagrid.html`](../design/datagrid.html)。
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
