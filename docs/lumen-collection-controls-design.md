# Lumen 集合控件设计（List / Tree / TreeList）

> 文档状态：已实施契约；List、Tree 视觉与交互补齐于 2026-09-19。
> 输入：源码现状盘点（`include/lumen/core/widget.h`、`include/lumen/core/virtual_list.h`、`src/layout/layout.cpp`）、`docs/lumen-self-use-roadmap.md` M0–M12 完成记录、`docs/lumen-visual-system-design.md`（token 三层模型/尺度表/状态规则/滚动条契约）、成熟 C++ GUI 框架的列表/树控件契约。
> 配套视觉设计稿：`design/collection-controls.html`。
> 定位：桌面自用版控件库增强，遵循既有"Widget 不可变声明 + 应用侧控制器 + 布局期物化"架构，不引入新模块。

---

## 1. 背景与问题

以下为实施前的问题盘点，保留作设计背景；当前 List / Tree / TreeList 已实现，现行契约见 §6–§11：

| 控件 | 现状 | 关键缺口 |
| --- | --- | --- |
| `ListView` | ScrollView 的语义别名（`role=list`），单子节点，**无虚拟化**（`src/layout/layout.cpp:429` 直接走 viewport 布局） | 无选择模型、无项目级键盘导航、无分隔线/空态、千项数据全量物化 |
| `VirtualList` | M3 交付的纵向虚拟化：`itemCount/estimatedExtent/extentOf/visibleRange/noteExtent` + 锚点稳定 | 无选择/激活语义、无项目交互（点击/双击/悬停仅取决于应用自建子树）、无表头、`scrollToIndex` 仅"最小移动"一种对齐 |
| Tree | **不存在** | 层级模型、展开状态、缩进、chevron、树键盘导航全部缺失 |
| TreeList（树+列） | **不存在** | 列定义、列宽分配、粘性表头、单元格构建、排序钩子缺失 |

补充事实：`Widget.selected` 标志已存在（`widget.h:250`，注释"用于列表/工具栏选中语义"），但框架内**没有任何列表基建驱动它**——选中视觉只能由应用逐项手写；`InteractionController` 的 Tab 遍历、Enter/Space 激活、hover/pressed 状态全部基于 Widget key/identity，已经具备承载列表项交互的前提。

结论：问题不是渲染或虚拟化能力不足，而是**缺少列表语义层**（选择模型、当前项、激活、树展开、列系统）以及把它们接入既有交互/语义/样式管线的通道。

## 2. 参考框架调研

| 框架 | 列表 | 树 | 树+列 | 选择模型 | 虚拟化 | 对 Lumen 的启示 |
| --- | --- | --- | --- | --- | --- | --- |
| Qt 5/6 | `QListView` | `QTreeView` | `QTreeView` + `QHeaderView`（即 QTreeWidget 形态） | `QItemSelectionModel`：current≠selected、四选择模式（SingleSelection/ MultiSelection/ ExtendedSelection/ ContiguousSelection）、anchor 区间 | model/index 驱动，按需委托绘制 | current/selected 分离；Ctrl/Shift 语义；树扁平化为可见行序列 |
| wxWidgets | `wxListCtrl`（report 模式=列） | `wxTreeCtrl` | `wxDataViewCtrl` | 索引集合 + 事件 | 虚拟模式（`wxLC_VIRTUAL`） | report 模式证明"列表+列"与"树+列"可共用列系统 |
| GTK3/4 | `GtkListBox` | `GtkTreeView`（树） | `GtkTreeView` + `GtkTreeViewColumn` | `GtkTreeSelection`（单/多/范围） | GtkTreeView 原生虚拟化；GtkListBox 非虚拟 | 列定义（title/renderer/width）独立于行的价值 |
| JUCE | `ListBox`（`ListBoxModel::paintListBoxItem`） | `TreeView` | — | `setMultipleSelectionEnabled` + `SparseSet<int>` | 行回调驱动（`getNumRows/refreshComponentForRow`） | 控制器持有行数+行绘制回调、组件按需物化的极简契约——最接近 Lumen 现有 VirtualListSource |
| Win32 | `ListView`（LVS_REPORT） | `TreeView` | ListView report 模式 | `LVIS_SELECTED`/`LVIS_FOCUSED` 位 | 虚拟模式（LVS_OWNERDATA） | current 与 selected 是两个独立位；表头 `HDN_*` 通知与控件解耦 |
| Dear ImGui | `BeginListBox` | `TreeNode` | 表格 `Table` | 应用自持 | 即时模式自持 | 反例：无共享选择模型导致每个应用重写选择逻辑——Lumen 应提供控制器 |

**采纳的共同事实**（六个框架一致）：

1. **current（焦点项）与 selected（选中集）是两个概念**，键盘导航移动 current，选择集独立维护。
2. **四种选择模式**收敛为：None / Single / Multiple / Extended（Extended = 单击重置+Ctrl 切换+Shift 区间）。
3. **树 = 扁平化的可见行序列 + 展开状态集**；展开/折叠改变行数，虚拟化在扁平序列上进行。
4. **列系统与行系统解耦**：列（id/标题/宽/对齐）独立声明，行按列取单元格。
5. **双击/Enter = 激活（activate）**，单击 = 选择；激活是应用回调，框架不定义业务含义。

**采纳的 Lumen 本土事实**（决定不做的事）：

1. Lumen 无 model/view 委托系统——保持 **source 接口 + Widget 构建**（JUCE 式），不引入 Qt 级抽象。
2. 选择状态放**应用侧控制器**（与 VirtualListController/DropdownController 同层），Widget 只携带声明——不违反"runtime state lives in Element / 控制器"的既定架构。
3. 行声明采用 `Row(collectionRow=true)`，点击、焦点、按压和激活接入 `InteractionController` 的集合行通道；每个控制器注册固定数量的 sink，避免虚拟行累积常驻 handler。

## 3. 设计目标与非目标

**目标**

1. 三个新控件类型：`List`、`Tree`、`TreeList`，共享一套虚拟化引擎与选择模型。
2. 千项 List、万节点 Tree 在 1080p 保持 O(visible) 物化（对齐 M3/M7 性能口径）。
3. 选择/当前/激活/展开语义在**视觉、键盘、指针、语义树**四层一致（M5 出口条件延伸）。
4. Widget 体积增长 ≤ 32B（实测 Release 基线 800B → 816B；新字段走指针 + packed 标志。注：Debug 构建的 MSVC STL `_ITERATOR_DEBUG_LEVEL=2` 会给每个容器 +8B，Debug 实测 920B 属工具链开销，不计入预算口径）。
5. 全部新路径有 headless 测试；基准场景进入 `lumen-scene-bench`。

**非目标（第一版明确不做）**

- 单元格内编辑（行内 TextField）——表单能力已有，组合留给应用。
- 拖拽重排/拖入拖出（DnD）——无框架级拖拽通道，留后续增强链。
- 列宽拖拽调整、列显示/隐藏 UI——列宽首版为"固定 + 权重"，调整控件留后续。
- 多列纯表格（Table/Grid 无树形态）——`Grid` 已覆盖简单网格；按需评估池。
- 水平虚拟化、横向滚动。
- 橡皮筋框选（rubber band）、type-ahead 定位。
- 排序执行（框架只给"列头点击 → 应用回调"钩子，排序由应用做）。
- 移动端（Android/iOS）——路线图明确暂缓。

## 4. 总体架构

### 4.1 分层

```text
┌───────────────────────────────────────────────────────────┐
│ 应用层：build() 产出 Widget 树                              │
│   makeList(&listController) / makeTree(&treeController)     │
│   / makeTreeList(&treeListController)                      │
├───────────────────────────────────────────────────────────┤
│ widgets 层（新文件 include/lumen/widgets/selection.h）     │
│   SelectionModel（共享：模式/current/selected 集/回调）     │
│   ListController   TreeController   TreeListController     │
│   （持有数据、选择、展开状态、滚动；注册行 onClick）          │
├───────────────────────────────────────────────────────────┤
│ core 层：VirtualListSource 布局契约                         │
│   itemCount/estimatedExtent/extentOf/offsetOfIndex/        │
│   visibleRange/buildItem/noteExtent/buildEmpty/tabStopKey    │
├───────────────────────────────────────────────────────────┤
│ layout 层：layoutVirtualList 引擎（既有）                   │
│   可见区物化、extent 修正、锚点稳定、绝对定位子项            │
│   TreeList 追加：表头高度预留 + 粘性表头绘制                 │
└───────────────────────────────────────────────────────────┘
```

核心架构决策：**Tree 与 TreeList 不需要新的虚拟化引擎**。树控制器把"可见节点序列"扁平化为行序列（`itemCount = 可见行数`），复用 M3 的 VirtualListSource 契约与 layoutVirtualList 布局路径。Qt/QTreeView、GTK/GtkTreeView、Win32/TreeView 内部同样如此。

语义层单源实现：List 与 Tree 控制器中语义相同的部分——共享键位（§6.3 与 §7.4 的交集：Ctrl+A / Up / Down / Home / End / PageUp / PageDown）、`ScrollAlignment` 四向滚动对齐、行点击/激活 sink 接线（§6.5 的前缀分发）、Shift 区间 key 序列与集合行 Row 壳——单源实现于私有 `src/widgets/collection_common.h`（inline 自由函数，不进公共 API）。`ScrollAlignment` 单一定义于公共 `include/lumen/widgets/collection.h`，两控制器经类内 `using` 别名保持 `ListController::ScrollAlignment` 等既有写法。Tree 专属键位（Left/Right）、chevron toggle 与扁平化/按 key 的 extent 缓存仍在 TreeController。

### 4.2 三个控件的职责边界

| 控件 | 数据形态 | 典型场景 | 对应参考 |
| --- | --- | --- | --- |
| **List** | 一维行序列 | 文件列表、菜单、日志、搜索结果 | QListWidget / GtkListBox / JUCE ListBox |
| **Tree** | 层级行序列，单列 | 目录树、大纲、依赖图 | QTreeWidget 单列形态 / wxTreeCtrl |
| **TreeList** | 层级行序列 + 多列 | 设置页、依赖列表（名称/版本/大小）、资产浏览器 | QTreeWidget 多列 / wxListCtrl report+分组 |

`List` 与 `Tree` 的差异只在 source 实现（平铺 vs 扁平化）；`TreeList = Tree + 列系统 + 表头`。

## 5. 共享选择模型（widgets 层）

```cpp
// include/lumen/widgets/selection.h（新）
namespace lumen::widgets {

enum class SelectionMode : std::uint8_t {
    None,       // 不可选择（仅激活/浏览）
    Single,     // 单选：单击选中并重置
    Multiple,   // 多选：单击即切换（checkbox 语义）
    Extended,   // 单击重置；Ctrl+单击切换；Shift+单击区间（默认桌面模式）
};

class SelectionModel {
  public:
    void setMode(SelectionMode mode);
    [[nodiscard]] SelectionMode mode() const;

    // current（焦点行）与 selected 集相互独立（Qt/Win32 一致语义）。
    void setCurrent(const std::string& key);          // "" = 无 current
    [[nodiscard]] const std::string& currentKey() const;

    // selected 集：按 stable key 存储（不按 index——index 随数据增删漂移，
    // key 是 Lumen Element identity 的既有唯一依据）。
    void setSelected(std::vector<std::string> keys); // 批量替换（单选/清空同路）
    bool toggle(const std::string& key);
    [[nodiscard]] bool isSelected(const std::string& key) const;
    [[nodiscard]] const std::vector<std::string>& selectedKeys() const;
    [[nodiscard]] std::size_t selectedCount() const;

    // 区间选择需要行序：由持有行序列的控制器回调提供
    //（List/Tree 控制器注入；SelectionModel 不假设数据结构）。
    using KeySequence = std::function<std::vector<std::string>(
        const std::string& from, const std::string& to)>;
    void setKeySequence(KeySequence sequence);       // [from, to] 闭区间

    // 修饰键语义（Extended 模式；控制器把指针/键盘事件翻译到这里）。
    void click(const std::string& key, bool ctrl, bool shift);
    void moveTo(const std::string& key, bool extend); // 键盘移动 current

    // 回调（UI 线程；控制器订阅后驱动重建/语义）。
    std::function<void()> onSelectionChanged{};
    std::function<void(const std::string&)> onCurrentChanged{};
};

}  // namespace lumen::widgets
```

设计要点：

1. **key 而非 index**：Lumen 的状态复用、焦点、语义 identity 全部基于 stable key（M3 接口约束"stable key 是状态复用的唯一依据"）。选择集跟随同一事实来源，index 增删漂移不破坏选择。
2. **current≠selected**：键盘导航只移 current；是否随动修改选择集由模式决定（Single/Extended：随动；None：只移焦点）。这与 Win32 `LVIS_FOCUSED`/`LVIS_SELECTED`、Qt `currentIndex`/`QItemSelection` 完全一致。
3. **区间语义交给控制器**：SelectionModel 不持有行序（Tree 的行序随展开状态变化），`KeySequence` 回调查询闭区间。

## 6. List 控件

### 6.1 ListController（widgets 层）

```cpp
class ListController final : public core::VirtualListSource {
  public:
    // --- 数据（沿用 VirtualListController 契约） ---
    void setItemCount(std::size_t count);
    void setItemBuilder(std::function<core::Widget(std::size_t)> builder);
    void setKeyOf(std::function<std::string(std::size_t)> keyOf); // index→stable key
    void setEnabledOf(std::function<bool(std::size_t)> enabledOf);
    void setEstimatedExtent(float extent);

    // --- 选择 ---
    void setSelectionMode(SelectionMode mode);
    SelectionModel& selection();

    // --- 激活（双击/Enter；语义 Activate 同路径） ---
    std::function<void(const std::string& key)> onActivated{};

    // --- 滚动（扩展既有 scrollToIndex；ScrollAlignment 单一定义于
    //      widgets/collection.h，List/Tree/TreeList 公用） ---
    void scrollToKey(const std::string& key, ScrollAlignment align);

    // --- 事件入口（应用 onKey 转发，M11 DropdownController 同模式） ---
    bool handleKey(core::Key key, core::KeyModifiers mods, char keyChar = 0);

    // --- 行构建（覆盖 VirtualListSource::buildItem） ---
    // 包装应用 builder 产物：注入 selected/enable 状态、行 chrome（分隔线
    // 交 StyleResolver token）、注册 onClick handler（选择路径）。
    [[nodiscard]] core::Widget buildItem(std::size_t index) const override;

    // 空态：itemCount()==0 时经 setEmptyBuilder 提供单棵占位子树
    //（非虚拟化、参与语义）。
    void setEmptyBuilder(std::function<core::Widget()> builder);
};
```

### 6.2 行的构建与交互路径

行 Widget 由控制器包装生成，外层是可聚焦集合 `Row`，由专用 `ListPart` 与 `ListRowResolvedStyle` 解析表面，不改变 Tree、菜单或旧 VirtualList 的样式：

```text
行 = Row(key = "<owner>:item:<stableKey>", collectionRow = true,
         selected = selection.isSelected, listPart = Row/LastRow)
     └─ 应用 builder 产物（flex=1，内容自由组合）
```

- **单击**：`onClick("list:<owner>:<key>")` 经 `InteractionController` 的
  行点击 sink（`addRowClickSink`）按前缀分发——控制器执行
  `selection().click(key, ctrl, shift)` 并请求重建。**不按行注册常驻
  handler**：虚拟化行序大（千/万级），注册表会随滚动无界累积；sink 恒
  O(1)。
- **hover/pressed/焦点环**：集合行的完整 identity 进入 WidgetState；控制器按 key 请求焦点后，AppShell 在行物化完成时解析成完整 identity。行填满视口内容宽度，右侧空白也参与命中。
- **双击激活**：`InteractionController` 已有双击检测（`lastClickMs/lastClickIdentity_`），
  `addRowActivateSink` 匹配 `<owner>:item:` 前缀后触发 `onActivated(key)`。
- **Enter/Space**：与双击、语义 Activate 使用相同的集合行激活 sink；语义 Activate 直接激活，不隐式改变选择集。
- **selected 视觉**：`Widget.selected` 进 `WidgetState`（视觉系统 §5 已含 selected 位），
  行的选中背景解析为 `Theme.list.selected = colors.accentContainer`（§10.2），
  Core Dark 下派生值即设计稿的 `#2e3c60`；分隔线/圆角同经 token 派生。
- **禁用**：`setEnabledOf(index)` 提供无需物化的可用性元数据；点击、激活、导航、区间选择和 Ctrl+A 均跳过禁用项。builder 根 `enabled=false` 也禁用已物化行；屏外项或动态禁用应提供 `setEnabledOf`，不能依赖尚未执行的 builder。禁用整个 List 会禁用其全部物化行。
- **空态**：零项时布局自动调用 `buildEmpty()`，默认居中 Folder 图标和 “No items”，可用 `setEmptyBuilder` 替换内容。最小高度为两行，受显式视口约束；无需应用另写空态分支。

### 6.3 键盘契约（List 拥有焦点时）

| 键 | 行为 |
| --- | --- |
| Up / Down | current 上/下移到可用行（Extended 随动修改选择；Single 选中该行；滚动按 `Visible` 对齐） |
| Home / End | current 移到首/末行，滚动对齐 `Start`/`End` |
| PageUp / PageDown | current 移动约一视口行数（用 estimatedExtent 折算），对齐 `Visible` |
| Ctrl+Home / Ctrl+End | 同 Home/End |
| Enter / Space | 激活 current（`onActivated`） |
| Ctrl+A | Extended 模式全选；Single 选中 current |
| Tab / Shift+Tab | 整个 List 只占一个 Tab 停靠点；进入可见 current（否则首个可见可用行），只更新 current；再次 Tab 离开，不逐行遍历 |

**路由方式**：应用 `ShellConfig.onKey` 以焦点列表为优先目标转发（M11 `handleKey` 同模式）；未被应用消费的滚动键由 `InteractionController::scrollKey` 兜底——焦点位于源视口（List/Tree/TreeList/VirtualList）内时直接驱动该源控制器（§6.5），不再落到应用默认视口。

### 6.4 Widget 声明

```cpp
enum class WidgetType { ..., List, Tree, TreeList };  // 新增三个值

// List Widget：children 必须为空；source = ListController（指针，
// 生命周期由应用持有，与 VirtualList 同契约）。
inline core::Widget makeList(const core::VirtualListSource* source,
                             std::string key = {},
                             std::optional<float> width = std::nullopt,
                             std::optional<float> height = std::nullopt,
                             float cacheExtent = 200.0F);
```

布局实现：复用 `layoutVirtualList`，List 分支约束行填满内容宽度并在零项时物化空态；边框/padding 外的内部视口用于滚动对齐与 extent，首尾行保持完整可见。

### 6.5 框架级源视口滚动（滚轮/拖动/惯性）

滚动状态在源控制器（`ScrollController`）内，框架此前只能经应用
`onWheel`/`onScrollDrag` 按 key 逐个路由（遗漏即"滚轮无效/误滚外层"）。
收口为框架路径：

- `RenderNode` 携带 `virtualSource` 指针（`makeNode` 自 Widget 复制）；
  `VirtualListSource::scrollController()` 返回源持有的控制器（默认
  `nullptr` = 滚动状态在应用侧，走原 sink 路径，静态数据源/测试源不受
  影响）。
- `InteractionController` 在命中链最近的可滚动视口为源视口时直接消费：
  滚轮（`wheel`）、键盘滚动（`scrollKey`，聚焦节点的最近源视口祖先）、
  指针/触摸拖动（M10 拖动路径，含拇指跟手换算）。已有滚动范围的视口在端点处不向外层联滚；滚轮命中空集合或内容完全放得下的集合时，跳过该视口继续向父级查找可滚动区域（`lumen-scroll-design.md` §4），与滚动条是否显式显示无关。
- 拖动释放起的惯性由 `AppShell::tick` 调 `advanceSourceFling` 逐拍推进
  （框架登记起滑的源控制器）。
- 框架滚动经 `setRebuildRequest`（AppShell 注入 `markDirty`）请求重建。

## 7. Tree 控件

### 7.1 TreeModel（应用侧数据适配器）

```cpp
// 惰性层级契约：框架不复制数据，只按需拉取（wxDataView/GtkTreeModel 模式）。
class TreeModel {
  public:
    virtual ~TreeModel() = default;
    // key 为空串时表示根的子级查询。
    [[nodiscard]] virtual std::size_t childCount(const std::string& parent) const = 0;
    [[nodiscard]] virtual std::string childAt(const std::string& parent,
                                              std::size_t index) const = 0;
    // 惰性：hasChildren 可先行返回 true 而不物化子级（千节点目录场景）。
    [[nodiscard]] virtual bool hasChildren(const std::string& key) const = 0;
    // 屏外禁用状态无需构建行即可查询；默认 true。
    [[nodiscard]] virtual bool isEnabled(const std::string& key) const;
    // 行内容（不含缩进与 chevron，由控制器注入）。
    [[nodiscard]] virtual core::Widget buildRow(const std::string& key,
                                                std::size_t depth) const = 0;
    // 数据变更通知入口由 TreeController 提供（modelChanged/setData 重建）。
};
```

### 7.2 TreeController

```cpp
class TreeController : public core::VirtualListSource {
  public:
    void setModel(const TreeModel* model);  // 应用拥有生命周期
    void modelChanged();                    // 数据变更→失效扁平缓存+请求重建
    void setEmptyBuilder(std::function<core::Widget()> builder);
    void setCurrentKey(const std::string& key, bool extend = false);

    // 展开状态：按 key 存储（数据移动/重排不丢失展开）。
    void expand(const std::string& key);
    void collapse(const std::string& key);
    void toggle(const std::string& key);
    [[nodiscard]] bool isExpanded(const std::string& key) const;
    bool expandAll(std::size_t maxRows = 10000); // 超限提前拒绝，原展开集不变
    void collapseAll();

    // 选择/激活/键盘：同 ListController（SelectionModel 复用）。共享键位、
    // 滚动对齐与 sink 接线单源实现于 src/widgets/collection_common.h；
    // Left/Right 树键位与 chevron toggle 留在本控制器。
    bool handleKey(core::Key key, core::KeyModifiers mods, char keyChar = 0);

    // 可见行查询（键盘导航/语义/测试需要）。
    struct VisibleRow { std::string key; std::size_t depth;
                        bool hasChildren; bool expanded; };
    [[nodiscard]] const std::vector<VisibleRow>& visibleRows() const; // 先刷新缓存
    [[nodiscard]] bool rowOfKey(const std::string& key, VisibleRow& row) const;
};
```

**扁平化与失效**：控制器维护 `std::vector<VisibleRow>` 缓存（key+depth+flags），由 TreeModel 深度优先遍历生成；`expand/collapse/modelChanged` 时失效重建。重建成本 O(可见节点)——与一次可见区物化同数量级；展开动画不做（首版），几何瞬变。

**extent 缓存按 key 而非 index**（内部 `std::map<std::string, float>`）：M3 的 `std::map<index, float>` 在树场景下会因展开/折叠产生大面积 index 漂移；key 缓存使"折叠再展开"保留实测高度，锚点稳定逻辑（`noteExtent` 平移）复用。

### 7.3 行构建

```text
行 = Row(key = "<owner>:item:<stableKey>", collectionRow = true,
         treePart = Row/LastRow, treeDepth = depth, selected = …)
     ├─ chevron 按钮（key = "<owner>:chev:<key>"）或等宽叶节点占位
     └─ 应用 buildRow 产物（flex=1）
```

- **chevron**：新增 `IconId::ChevronRight`（折叠）与既有 `ChevronDown`（展开）；
  chevron 是行内独立小 Button，点击只 toggle 不选择（文件管理器惯例）。
- **整行表面**：视口边框内的完整宽度参与选择命中；不透明选中底色、左侧 3px 选中条、圆角与焦点环相互独立。选中条上下 inset 4，聚焦时向内让出环宽；失焦仍保留选中底色和条。
- **缩进**：`Theme.tree.indentStep` = 20px（视觉规格 §10）；depth 增加行的内容 padding，箭头与应用内容一起移动，背景与选中条不缩进。叶行保留相同箭头槽。缩进参考线（细竖线）首版不画。
- **主题**：`TreePart` 在布局前解析 `Theme.tree.row`；共享行绘制结构，不借用 ListPart。行高/padding/圆角/间距随密度及 ControlSize，箭头和缩进随 fontScale；局部 ThemeScope 与高对比有效。
- **空态**：零根节点（含空模型）自动布局居中的 Folder + “No items”；`setEmptyBuilder` 替换内容，最小两行高，受显式视口约束。
- **禁用**：`TreeModel::isEnabled(key)` 提供屏外可用性；builder 根 `enabled=false` 禁用已物化行。指针、激活、导航、范围选择和 Ctrl+A 跳过禁用项，整个 Tree 禁用会递归禁用物化行。模型动态变化后调用 `modelChanged()`。
- **折叠焦点**：隐藏 current 子孙时将 current 恢复到折叠节点，原选择集保留；原焦点在该子孙行时同步恢复焦点并滚动可见。`collapseAll` 恢复到所属根节点。
- **物化与上限**：`expandAll` 使用有界迭代遍历，超限提前返回 false；重复/循环 key 不重复遍历。key 必须全局唯一、非空。`visibleRows()` 在返回引用前刷新缓存，后续模型/展开变化会失效该引用。

### 7.4 键盘契约（Qt/QTreeView 对齐）

| 键 | 行为 |
| --- | --- |
| Left | current 有子级且展开 → 折叠；否则 → current 移到父级 |
| Right | current 有子级且折叠 → 展开；否则 → current 移到首个子级 |
| Up / Down、Home / End、PageUp / PageDown | 同 List |
| Enter / Space | 激活 current（`onActivated`）；折叠态 Enter 先展开再激活由应用组合 |
| Ctrl+A | 同 List |
| Tab / Shift+Tab | Tree 占一个行停靠点，进入 current 或首个可见可用行，仅更新 current；再次 Tab 离开，箭头不单独占 Tab 停靠点 |

Left/Right 移动到父级/子级后按 Visible 对齐；`setCurrentKey` 同步选择、焦点和滚动。

## 8. TreeList 控件（树 + 列）

### 8.1 列模型

```cpp
// 应用持有列向量（生命周期同 source；Widget 携带裸指针，themeOverride 同模式）。
struct TreeListColumn {
    std::string id{};           // 稳定列标识（单元格构建参数）
    std::string label{};        // 表头文本（语义标签）
    float fixedWidth{0.0F};      // >0：固定像素列宽
    float weight{0.0F};         // >0：按权重分配剩余宽度
    float minWidth{0.0F};       // 弹性列的最小宽（默认 48）
    bool visible{true};         // 隐藏列（布局期跳过）
    bool sortable{false};      // 表头可点击（排序执行在应用）
};

class TreeListController final : public TreeController {
  public:
    void setColumns(std::vector<TreeListColumn> columns);
    // 单元格构建：列内容与行内容解耦；返回 Widget 由框架放入列盒。
    void setCellBuilder(std::function<core::Widget(
        const std::string& rowKey, const std::string& columnId)> builder);
    // 表头点击（sortable 列）：onHeaderClick 返回应用执行排序，控制器只
    // 重建 + 记录 sortColumn/sortDescending（表头箭头显示）。
    std::function<void(const std::string& columnId, bool descending)> onHeaderClick{};
    void setSortIndicator(std::string columnId, bool descending);
};
```

### 8.2 行内布局与列宽分配

```text
行 = Button(key = "treelist:<owner>/item:<key>")
     └─ Row[
          第一列: [chevron, indent(padding), cellBuilder(key, col0)]
          其余列: cellBuilder(key, colN)   ← 每列一个固定宽容器
        ]
```

列宽算法（布局期，宽度可用后）：

1. `W = viewportWidth - padding.horizontal()`；
2. 固定列占 `fixedWidth`，计入 `W_used`；
3. 剩余 `W_free = W - W_used - 列间距×(n-1)` 按权重比例分配给 weight 列，
   每列下限 `max(minWidth, …)`；无 weight 列时剩余宽归最后一列（或留白由
   `CrossAxisAlignment` 决定，默认归尾列）。

列宽由**控制器计算**（布局前用 `ScrollController.viewportExtent` 的已知宽度；首帧未知时按固定列先排、弹性列用估算宽，视口尺寸回填后自动重排——与 M3 Grid 的窗口变化重排同一机制）。

### 8.3 粘性表头

表头是 TreeList 特有的非滚动 chrome：

- `layoutVirtualList` 扩展：`WidgetType::TreeList` 时布局先物化表头行
  （`TreeListController::buildHeader()`，高度 = ControlSize 对应行高），置于
  内容顶部**不随 scrollOffset 平移**；行区 `visibleRangeAt` 的视口高度减去
  表头高。
- 表头单元格 = Button（key = `treelist:header:<colId>`，sortable 时注册 onClick
  → `onHeaderClick`），语义 role=button、label=列名；排序指示 = `ChevronUp/
  ChevronDown` 图标（新增 IconId）。
- 表头始终可见（列表滚动时表头不动）——参考 QHeaderView/wxListCtrl report 模式。

### 8.4 TreeList 键盘契约

同 Tree（§7.4）。列间导航（Left/Right 在列间移动）**不做**——树场景 Left/Right 已被折叠/展开占用；单元格内交互（按钮、checkbox）经 Tab 既有遍历可达。

## 9. Widget / DSL 扩展与体积预算

### 9.1 Widget 字段（M7 体积预算内的最小增量）

| 字段 | 类型 | 用途 |
| --- | --- | --- |
| `collectionSelectionMode` | `std::uint8_t`（枚举打包进既有 packed 区） | 声明选择模式（语义树与视觉一致性校验用；真实状态在控制器） |
| `collectionColumns` | `const void*` | TreeList 列向量指针（应用持有；themeOverride 同模式） |
| `collectionShowHeader` | bool（packed） | 表头显隐 |

`virtualSource` 字段与三个新类型复用（List/Tree/TreeList 的 source 均实现 `VirtualListSource`）。预算：+8B（columns 指针）+2 packed 标志 ≈ **+16B**，实测 Release 800B → 816B，满足 M7 约束（"Widget 体积直接影响 reconcile 构建/比较成本"）。体积断言按构建模式分档（Debug 见 §3 注）。

### 9.2 构建器（C++ DSL 与 .lumen）

- C++：`makeList/makeTree/makeTreeList` + `withSelectionMode` / `withFocusRing`。
- `.lumen` DSL（规划，当前转换器尚未支持集合节点）：冻结节点集追加 `list` / `tree` / `treelist` 三节点
  （属性：`selection-mode`、`cache-extent`、`show-header`；source 由 C++ 侧
  装配，DSL 只声明——与 VirtualList 现状一致：`.lumen` 不表达数据源指针）。
- golden 对照测试沿用 M2 模式。

### 9.3 语义树契约

| 节点 | role | 状态/动作 |
| --- | --- | --- |
| 控件本身 | `list`（List）/ `tree`（Tree/TreeList） | `Scroll`（复用既有滚动 action 路径） |
| 行 | `listitem`（List）/ `treeitem`（Tree/TreeList） | `selected` flag、`Activate`；treeitem 另带 `expanded`（semanticsValue "true"/"false"）与 `Expand`/`Collapse` action |
| 表头单元格 | `button` | label = 列名；点击触发 `onHeaderClick` |

语义值与 M5 冻结契约的兼容：`selected` flag 已在 M5 语义固化（invalid/hidden/selected）；`expanded` 是新增 value 通道用法，不改变既有节点。视口外缓存区行标 Hidden（沿用 M5 VirtualList 规则）。

展开/折叠已接入框架语义 action 与 Recording bridge；chevron 的 `Activate` 与指针点击走同一动态 sink。原生 UIA ExpandCollapse pattern 以及 AT-SPI/NSAccessibility 的对应映射仍未实施，不将 headless 语义回归等同于屏幕阅读器实测。

## 10. 视觉规格（详见 design/collection-controls.html）

视觉契约遵循 `docs/lumen-visual-system-design.md`（token 三层模型 §3.1、尺度表 §3.2、状态规则 §5、滚动条 §7.4）；`Theme.list`、`Theme.tree` 为独立组件层映射，复用语义颜色与既有密度尺度。

### 10.1 尺度（视觉系统 §3.2 对齐）

| 项目 | Small/Compact | Medium/Comfortable | Large/Touch | 依据 |
| --- | --- | --- | --- | --- |
| 行最小高度 | 32px | 40px | 48px | 视觉系统"控件最小高度"行——行高下限；变高项（多行文本）按实测 extent 增高 |
| 行水平内边距 | 8px | 12px | 16px | 视觉系统"水平内边距"行 |
| 行小部件圆角 | 4px | 6px | 8px | 视觉系统"小部件圆角"行（选中/current 行圆角同档） |
| 树缩进步进 | 20px/层 | 20px/层 | 20px/层 | 4px 基础网格；4 层缩进 80px 仍留足内容（Large 行高下不变） |
| chevron 图标 | 16×16，命中区 24×24 | 同 | 同 | Tree 由 `Theme.tree.chevronIconSize/chevronHitExtent` 控制，随 fontScale 缩放 |
| 分隔线 | 1px，`color.border.default` | 同 | 同 | 行间 line token |
| 空态 | 居中文本 + muted 图标，高度 ≥ 2 行最小高 | 同 | 同 | 设计稿 |

桌面默认 Medium（40px 行）；长数据列表可显式选 Small/Compact（32px），密度由应用声明、不随平台隐式改变（§3.2 约束）。

### 10.2 组件 token（三层模型 §3.1 的 component 层）

```text
list.row.background            = color.background.surface
list.row.background.hover      = colors.surfaceSunken
list.row.background.pressed    = mix(colors.surface, colors.accent, 0.32)
list.row.background.selected   = colors.accentContainer（不透明）
list.row.separator             = color.border.default
list.row.content               = color.content.primary / .disabled（disabled.content）
list.row.selectionMarker       = colors.accent；宽 3，上下 inset 4
list.empty.content             = colors.contentSecondary；图标 24
tree.indent.step               = 20
tree.chevron.hitExtent         = 24
tree.chevron.iconSize          = 16
tree.chevron.content           = color.content.secondary
tree.row.*                     = 与 list.row.* 相同映射（Theme.tree.row 独立覆盖）
treelist.header.background     = color.background.surface（粘性表头）
treelist.header.content        = color.content.secondary；sortable 悬停 → content.primary
treelist.header.height         = 行最小高度同档
```

List 与 Tree 外框为 surface、1px separator 边框、cardRadius；行 padding 纵向取 `metrics.controlPaddingY`。行绘制、命中与语义共享边框内侧的 `contentClipRect()`，滚动不能覆盖外框。分隔线最后一项省略，聚焦时由完整焦点环替代该行底线。标记尺寸与空态图标随 fontScale 缩放，线宽/圆角保持逻辑尺寸。

焦点环：`color.focus.ring` + `focusWidth`，**内嵌绘制**（V2 damage 不变量）；selected 使用实色背景及左侧指示条，与焦点环独立。disabled 保留淡化的选中指示条并取消 hover/pressed/focus。高对比主题使用加强色与环宽。滚动条复用 `ScrollbarTokens`；rest 为当前渲染状态，hovered/dragged 外观仍按视觉实施任务 §7.2 保留待接线。

**焦点环可选（默认关闭）**：`Widget.showFocusRing` 默认 `false`；在集合视口上设置
`true` 才会绘制生成行的环，Tree/TreeList 箭头（chevron）同时开启——TreeList 的
chevron 是独立 Tab 停靠点（行/箭头各自停靠，不像 Tree 的单一停靠），传递经
`configureTreeParts` 与 Tree 同契约。此设置对鼠标、键盘及语义聚焦均生效。
选择背景/左侧标记、hover/pressed、实际焦点、键盘导航、激活与语义 focused 保持不变，
几何不跳变。关闭环时，选中条无需让出环宽，分隔线照常绘制；行内应用自建交互控件
仍使用自身属性。键盘重度列表页应显式开启。

```cpp
auto list = core::withFocusRing(core::makeList(&listController, "files"), true);
auto tree = core::makeTree(&treeController, "folders");
tree.showFocusRing = true;
```

现有文本 DSL 节点支持 `showFocusRing: true`（例如 Button）；集合节点当前仍由 C++
构建。Gallery Collections 的 “Show collection focus rings” 开关（默认关闭）用于
现场比较两种外观——开关只作用于交互式 List/Tree/TreeList 视口；页面底部的状态
矩阵预览行按状态显式开环（Focus/Selected+Focused 行绘制环，与按钮状态矩阵同
口径），不受开关影响。

### 10.3 状态矩阵

行状态 = 既有 `WidgetState`（§5：hovered/pressed/focused/disabled/checked/invalid/**selected**）的组合，无新增状态位。组合视觉见设计稿状态矩阵表；优先级遵循 §5：disabled > invalid > pressed > focused/hovered，checked/selected 只影响有对应语义的控件。默认（环关闭）外观下选中标记提供形状区分；显式开启焦点环时环与标记双指示，语义状态两种外观下均保留，分别测试。

## 11. 性能与测试计划

### 11.1 性能目标

| 指标 | 目标 |
| --- | --- |
| List 千项（Mixed extent） | 与 M3 virtual-list-1000 基线偏差 ≤ 10%（nodes/cmds 应同为 O(visible)） |
| Tree 万节点全折叠 | 扁平缓存 O(根节点)；物化行数 = 可见行 |
| Tree 深层展开+滚动 | nodes ≈ 可见行 + 表头；extent 缓存命中率报告（key 命中/重建） |
| TreeList 百行×5列 | 子节点数 = 行 × (列 + chrome)，命令数与同规模 List 的比例记录进基准报告 |
| 语义 diff | 增删行/选择变化只 diff 变更行（identity 稳定） |

规划的集合专用基准场景：`list-select-1000`（滚动+连续选择）、`tree-10k-fold`（万节点折叠展开）、`treelist-100x5`。当前 scene-bench 尚未提供这些场景；本轮用千项按需物化断言验证复杂度，不宣称已验证上述耗时目标。

### 11.2 测试（Catch2，命名 `*_tests.cpp`，行为命名）

1. **SelectionModel 单测**：四模式单击/Ctrl/Shift 语义、current 独立移动、key 增删后选择保持。
2. **ListController**：首屏物化、`scrollToKey` 四对齐、双击激活、Enter 激活、Ctrl+A、锚点稳定（沿用 grid_virtual_tests 模式）。
3. **TreeController**：扁平化正确性（嵌套展开/折叠序）、惰性 hasChildren、展开后 extent 缓存 key 命中、collapseAll/expandAll 防护上限、Left/Right 键盘折叠/父级跳转。
4. **TreeListController**：列宽分配（固定+权重+minWidth）、表头粘性（滚动后表头 y 不变）、表头点击回调、隐藏列。
5. **语义/键盘一致性**：激活（点击≡Enter≡语义 Activate）、选择变化语义 flag、treeitem expanded value、Expand/Collapse 语义 action ≡ 键盘。
6. **回归**：既有 ListView/VirtualList 行为与帧哈希不变；Widget 体积静态断言（Release `sizeof(Widget)` ≤ 824，Debug ≤ 928；包含后续 Splitter 指针，ListPart、TreePart/depth 使用既有 padding——工具链调试迭代器开销）。
7. **局部 damage 与全帧逐像素一致**（AppShell 真实帧管线，M3 模式）。
8. `list_visual_tests.cpp`：三档密度/整行命中、真实按压与完整焦点 identity、屏外导航、Tab 单一停靠点、禁用元数据/整表禁用、自动空态、ThemeScope/字体缩放/高对比、分隔线与增量帧一致性。
9. `tree_visual_tests.cpp`：整行选中底色/标记/焦点像素、层级箭头与叶槽、密度/ControlSize/ThemeScope、独立 chevron、折叠焦点恢复、Tab/语义展开折叠、禁用/空态、有界展开与万项按需物化、边框裁剪与局部重绘一致性。
10. `gpu_tree_readback_preserves_indented_selection_and_border`：Skia/GPU 真实像素回读，验证缩进行在 1×/2× deviceScale、滚动前后的整行选中底色、左侧标记、焦点环和视口边框。

### 11.3 示例与验收

- Gallery `Collections`：List（200 项、四模式切换、图标/徽标/禁用行）、七态矩阵、自动空态；Tree（目录图标/计数/默认选中节点）、八态矩阵和自动空态；TreeList（列+表头排序钩子）。千项/万项虚拟化由测试覆盖。
- headless 冒烟：导航、物化行数、选择流、表头回调输出。
- 三桌面窗口 smoke 由 CI 承担（既有矩阵）。

## 12. 实施分期

| 阶段 | 内容 | 出口条件 |
| --- | --- | --- |
| P1 选择模型 + List | SelectionModel、ListController、WidgetType::List、双击 sink、键盘路由、语义、测试、Gallery 页 | List 在四选择模式下视觉/键盘/语义一致；千项基准达标 |
| P2 Tree | TreeModel、TreeController、IconId::ChevronRight、树键盘契约、测试 | 万节点折叠树性能达标；展开状态跨重建保持 |
| P3 TreeList | 列模型、表头、排序钩子、IconId::ChevronUp/Down、测试 | 表头粘性；列宽重排随窗口变化 |
| P4 DSL/基准/收口 | `.lumen` 节点、golden 测试、基准场景归档、roadmap 完成记录 | 全部测试绿；基线归档；路线图状态更新 |

各阶段独立可交付；P2 不依赖 P1 的 List Widget（但依赖 SelectionModel）。

## 13. 兼容与迁移

1. `ListView` / `VirtualList` **保留不删**（既有示例与测试依赖；M5 语义契约冻结）。
   文档标注：新代码建议 `List`；`VirtualList` 无选择需求时仍可直用。
2. `VirtualListController` 保留；`ListController` 直接组合它（几何全部委托：
   extent 缓存/锚点稳定/滚动）；Tree 的 extent 缓存按 key 存于 TreeController
   （展开折叠后 index 漂移）。公共契约均不变。
3. `InteractionController::scrollKey` 行为变化仅在"焦点位于集合行"时生效，
   其余场景哈希不变；用 headless 回归锁定。
4. 新增 RenderCommand：无（chevron 复用 DrawIcon，需扩充 `IconId` 枚举与折线目录——
   属语义 ID 扩展，不动命令层与序列化版本）。
5. List/Tree 语义层单源化：共享键位、`ScrollAlignment` 四向对齐、行点击/
   激活 sink 接线、Shift 区间序列与集合行 Row 壳单源实现于私有
   `src/widgets/collection_common.h`；`ScrollAlignment` 单一定义于公共
   `include/lumen/widgets/collection.h`（控制器类内 `using` 别名保持既有
   限定写法）。零行为变化：既有 collection 用例与帧哈希回归全绿。

## 14. 开放问题（实施前需确认）

1. `KeySequence` 区间方向语义：`[from,to]` 与 `[to,from]` 是否等价（当前设计：等价，控制器返回闭区间按行序）。
2. `expandAll` 防护上限的默认值（当前设计：10'000，超过则拒绝并返回 false——防万级目录误触）。
3. 列宽首帧未知视口时的估算策略是否需要 `estimatedColumnWidth`（当前设计：固定列先排、弹性列按 minWidth，首帧后重排）。
4. Multiple 模式下键盘 Up/Down 是否随动切换选择（当前设计：不随动，键盘只移 current；Qt MultiSelection 移动即选中，差异点需评审）。
