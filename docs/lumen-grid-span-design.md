# Lumen Grid 跨行列（rowspan/colspan）设计

> 文档状态：已实现（2026-09-30，completion-plan 阶段C 首项 / m15-roadmap M17 池）
> 适用范围：`lumen-core` Grid 布局（M3 网格基线的跨行列扩展）
> 视觉基线：[`lumen-visual-system-design.md`](lumen-visual-system-design.md)（token/间距/密度不新增）
> Mockup：[`design/grid-span.html`](../design/grid-span.html)；Gallery 样本：Layout 页 `spans-grid`

## 1. 背景与范围

M3 交付的 Grid 为流式等分网格（行主序、单元宽均分、行高 = 行内最大外部
高度）。自用工具的卡片墙/表单页需要「标题行横跨全宽、特色卡跨两列」的
排布，此前只能用嵌套 Row/Column 拼出，列宽无法与相邻网格对齐。

本设计为 Grid 直接子项增加跨行列声明：

```cpp
core::withGridSpan(child, /*columnSpan=*/2, /*rowSpan=*/1)
```

不纳入本设计（维持 M3 边界）：显式行列坐标定位（grid-area/grid-lines）、
跨行项的垂直拉伸填充、子项在跨行区间的分布策略、横向滚动网格。

## 2. 几何契约

### 2.1 放置（流式 + 占位表）

- 子项按**声明序**经光标放置：光标行优先扫描首个「宽 × 高整块空闲」的
  单元矩形；当前列放不下（越列宽）或与占位冲突则右移一列，越列宽换行。
  空行必然空闲，扫描有限终止（同约束同结果，几何确定性）。
- `columnSpan` 布局期钳到 `[1, 列数]`（列数 = 固定值或最小列宽自适应推
  导值）；`rowSpan` 钳到 `[1, 255]`。通道为 `std::uint8_t`（M7 体积预
  算：字段占用既有 2 字节尾部填充，`sizeof(Widget)` 不变），
  `withGridSpan` 构造期另把负值/0/超 255 钳到 `[1, 255]`。
- 占位表按需扩行（稀疏）；总行数 = 放置结束后的占位行数。

### 2.2 测量与行高

- 子项测量约束：宽度紧约束 = `跨列数 × 单元宽 + (跨列数 − 1) × 列间距`
  （span=1 即单格宽，M3 语义不变）；高度不限。
- 行高两段解析（确定性顺序）：
  1. **常规项**（rowSpan=1）：所在行高 = 行内常规项外部高度（含 margin）
     最大值——M3 语义原样保留；
  2. **跨行项**按放置序：实测外部高度 −（跨行行高和 + `(rowSpan−1) ×
     行间距`）> 0 时，差额**计入最后一个跨行**。多个跨行项重叠时按声明
     序累积（先到先分配，后到者读到已更新的行高）。
- 跨行项**不参与**自身起始行的行高（它是「覆盖多行」的声明，不是「撑
  高某一行」的常规内容）；差额只落在末跨行，不按比例分摊。

### 2.3 定位与输出序

- 单元 x = `padding.left + 列 × (单元宽 + 列间距) + margin.left`；
  y = `padding.top + 行顶（行高前缀和 + 行间距）+ margin.top`。子项在
  跨行区间内**顶端对齐**，不垂直拉伸。
- `node.children` 按子项**声明序**输出（span=1 时声明序 = M3 行主序，
  逐字节同几何；带 span 时绘制/语义序 = 声明序，与遮挡直觉一致）。

### 2.4 兼容性（span=1 逐字节等价）

未声明 span（默认 1/1）的网格：占位退化为顺序行主序、测量宽度 = 单元
宽、行高 = 行内最大、输出序一致——与 M3 基线**逐字节相同**。既有 golden
/帧哈希/`grid_virtual_tests` 全部不动即是回归证据（本设计的
`grid_explicit_span_one_matches_default_geometry` 显式断言）。

## 3. 交互、语义与 damage

- 跨行列是纯布局属性：不改变子项的命中测试、焦点序（声明序）、语义树
  与 action（`WidgetType::Grid` 语义 role 不变）。
- identity 不因 span 变化（路径 identity 与 span 无关）；span 变化 =
  `Widget` 字段不等 → 节点 diff → 局部 damage，走既有管线。
- 键盘/指针无新增行为（网格不是焦点容器；子项各自可聚焦）。

## 4. 已知限制（如实登记）

- 跨行项顶部对齐、不拉伸；若其高度小于跨行区间，底部留白（与 M3「行
  高由内容决定」一致，不引入分布策略）。
- 无显式坐标定位：稀疏布局需用占位子项垫位（空 `Widget` 亦可占位）。
- colspan 钳制发生在布局期，`RenderNode` 不回写钳后值（Widget 声明保
  留原值）；诊断用 `--dump-tree` 观察实际几何。
- 与 DataGrid（表格控件，`lumen-datagrid-design.md`）无关：本设计只作
  用于 `WidgetType::Grid` 布局容器。

## 5. 测试矩阵（tests/grid_virtual_tests.cpp `[grid][span]`）

| 用例 | 断言 |
| --- | --- |
| `grid_colspan_spans_cells_and_wraps` | 跨 2 列宽度 = 2 格 + gap；后续项绕位/换行坐标 |
| `grid_colspan_is_clamped_to_column_count` | 超列数声明钳到列数（占满整行），后续项换行 |
| `grid_rowspan_deficit_grows_last_spanned_row` | 差额计入末跨行；被跨行占用的单元由后续项绕行；内容高 = 跨行项高 |
| `grid_rowspan_counts_row_gap_in_spanned_extent` | 跨行覆盖计入 `(rowSpan−1) × rowGap` |
| `grid_explicit_span_one_matches_default_geometry` | 显式 1/1 与缺省 span 整树 `operator==` 相等 |
| `grid_spans_work_with_adaptive_column_count` | 自适应列数推导后 span 按导出列宽计算 |

回归证据：既有 grid/layout 用例（span=1 路径）与 Gallery/Settings 帧哈
希用例不修改全过。Gallery 集成：Layout 页 `spans-grid`（宽格 + 高格 +
绕行格）；mockup `design/grid-span.html` 为同构静态稿。

## 6. 状态

- 四态：**headless 已验证**（上表单测 + 既有回归）。
- 平台无涉（纯 core 布局）；真实平台 smoke 不适用（无平台代码路径）。
