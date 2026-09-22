# Lumen 菜单类控件设计（ContextMenu / MenuBar）

> 文档状态：已实施（高级特性按需评估）（2026-09）
> 输入：源码现状盘点（`include/lumen/widgets/dropdown.h`、`include/lumen/app/app_shell.h`（M11 overlay 契约）、`include/lumen/core/interaction.h`（sink 家族）、`include/lumen/core/windowing.h`（`PointerButton::Secondary` 已入事件值类型）、`include/lumen/accessibility/semantics.h`）、`docs/lumen-self-use-roadmap.md` M5/M10/M11 完成记录、`docs/lumen-collection-controls-design.md`（行交互与 key 前缀惯例）、`docs/lumen-visual-system-design.md`（token 三层模型/尺度表 §3.2/状态规则 §5/elevation §8）。
> 配套视觉设计稿：`design/menu-controls.html`。
> 定位：桌面自用版控件库增强，遵循既有"Widget 不可变声明 + 应用侧控制器 + M11 框架级 overlay"架构；**零新增 WidgetType、零新增 RenderCommand**。

---

## 1. 背景与问题

源码核对后，当前命令入口类控件的现状：

| 控件 | 现状 | 关键缺口 |
| --- | --- | --- |
| `Dropdown` | 值选择控件：值行 + M11 浮动菜单（overlay + 锚定 + Up/Down/Enter/Esc 键盘导航 + barrier） | 语义是"从 N 个值里选一个"，不是命令分发：无分隔线、无 checkable 项、无快捷键展示、无子菜单、不能在任意位置唤起 |
| `Button` | 命令入口，但必须常驻布局 | 无瞬态命令面板 |
| `Dialog` | 模态内容面板 | 不是命令面板 |
| 集合控件行（List/Tree/TreeList） | 行 identity 稳定（`list:<owner>/item:<key>`），单击/双击/键盘齐备 | 无右键上下文菜单——文件管理器类应用的核心闭环缺一半 |

补充事实：`HostEvent.button` 已携带 `PointerButton::{Primary, Secondary, Middle}`（`windowing.h`），但 `AppShell::pointerDown`/`InteractionController::pointerDown` 的签名**不接收 button**——Secondary 按下当前被静默忽略（无 press/armed 语义也无回调）。这是唯一需要的框架级缝隙，其余全部复用既有能力：

- M11 框架级 overlay：独立布局、全窗 barrier、主树 identity 稳定、`setOverlayBuilder`（主树布局后重求值，锚定随 resize/主题刷新）。
- `DropdownController` 已验证"锚定菜单 + 边界钳制 + 键盘导航 + 焦点恢复"完整路径。
- sink 家族已成型：`WheelSink`/`ScrollDragSink`/`RowActivateSink`/`RowClickSink`（`addRowClickSink` 按前缀分发）。
- 图标目录齐备：`IconId::Check`（checkable 勾选）、`ChevronRight`（子菜单级联指示）——**零新增图标**。

结论：菜单类控件不是渲染或布局问题，而是**命令面板语义层**（菜单项模型、级联、右键唤起通道、菜单栏）——一个纯 widgets 层控制器 + 一条 Secondary 按键穿透缝隙。

## 2. 参考框架调研

| 框架 | 上下文菜单 | 菜单栏 | 菜单项模型 | 对 Lumen 的启示 |
| --- | --- | --- | --- | --- |
| Qt 5/6 | `QMenu::exec(pos)` | `QMenuBar`（原生菜单栏可同步） | `QAction`：text/icon/shortcut/checkable/separator/submenu/enabled | 一个 Action 模型同时服务菜单栏/上下文菜单/工具栏；mnemonic 用 `&` 内嵌标记 |
| GTK4 | `GtkPopoverMenu` + `show_context_menu` 信号 | `GtkPopoverMenuBar` | GMenu 声明式模型（section/submenu/action） | 菜单 = 声明式模型 + 弹层；阻止默认右键行为交应用 |
| Win32 | `TrackPopupMenu(TPM_RIGHTBUTTON)` | `HMENU`/`CreateMenu` | `MENUITEMINFO`：type(f/string/separator)/state(checked/disabled) | current 高亮与选中是两个位；菜单打开期间捕获全输入 |
| macOS | `NSMenu`（AppKit 全局） | `NSMainMenu`（系统菜单栏） | `NSMenuItem`：keyEquivalent + keyEquivalentModifierMask | 快捷键展示（⌘S）与执行解耦展示层；菜单栏属窗口 chrome |
| JUCE | `PopupMenu::showAt` + `Component::mouseDown` 右键判定 | 无内置 | item(text/action/subMenu/enabled/toggled) + `addSeparator()` | 控制器持有 item 树、回调激活——最接近 Lumen 控制器模式 |

**采纳的共同事实**：

1. **菜单项 = 统一命令描述**：label + 可选图标 + 可选勾选 + 可选快捷键展示 + 可选子菜单 + enabled + 分隔线；"激活"是应用回调，框架不定义业务含义（与集合控件的 activate 契约一致）。
2. **上下文菜单在指针位置唤起，菜单栏在栏项下方锚定**——两者共享同一套菜单面板渲染/键盘/关闭语义，只差锚定来源。
3. **快捷键展示与快捷键执行分离**：菜单只显示 `Ctrl+S` 文本；实际按键分发在应用 `onKey`（框架不建全局加速键表）。
4. **菜单打开期间是模态的**：全窗 barrier、键盘被菜单消费、点击外部/滚轮/Esc 关闭；焦点在关闭后恢复到唤起者。
5. **Secondary 按下不进入普通点击语义**：不 press、不 arm onClick、不触发拖动——右键只属于上下文菜单通道。

**采纳的 Lumen 本土事实**（决定不做的事）：

1. Lumen 无 Action 抽象、无工具栏——菜单模型就是 `MenuItem` 值类型 + 控制器，不引入 Qt 级 QAction 注册表。
2. 菜单面板 = overlay + Button 行（M11 `DropdownController` 已验证）；**不做树内展开**（M6 Dropdown 旧形态的反例已由 M11 纠正）。
3. 系统菜单栏集成（macOS `NSMainMenu`、Windows 原生 `HMENU`）不纳入——自绘菜单栏即可满足自用工具；原生同步留按需评估。

## 3. 设计目标与非目标

**目标**

1. 两个 widgets 层控制器：`ContextMenuController`（指针位置唤起）与 `MenuBarController`（栏项锚定），共享一套菜单面板构建与键盘/关闭语义。
2. 菜单项模型覆盖：label / icon / checkable + checked / 快捷键展示 / 子菜单级联 / 分隔线 / disabled / mnemonic。
3. 框架缝隙最小化：`PointerButton` 从 `HostEvent` 穿透到 `InteractionController`（新增 Secondary press sink）；其余零 core 改动。
4. 键盘、指针、语义三层一致（M5 出口条件延伸）：激活 ≡ Enter ≡ 语义 Activate；勾选 ≡ 语义 toggle；高亮项焦点可见。
5. 全部新路径有 headless 测试；既有示例与测试帧哈希不变（Secondary 缺省参数保持旧行为）。

**非目标（第一版明确不做）**

- 全局快捷键/加速键表（菜单只**展示**快捷键文本；执行在应用 `onKey`）。
- 系统菜单栏原生同步（NSMainMenu/HMENU）；tear-off 菜单；菜单内嵌任意控件（滑块/文本字段）。
- 触摸长按唤起上下文菜单（桌面触摸在范围内，但长按手势通道留按需评估——见 §14）。
- 超长菜单的滚动箭头区（首版用既有 ScrollView 兜底，见 §6.4）。
- 菜单内 i18n/助记符资源系统（应用自管理字符串，路线图既定边界）。

## 4. 总体架构

```text
┌───────────────────────────────────────────────────────────┐
│ 应用层：持有 MenuItem 树与命令回调；onKey 转发菜单键盘；    │
│   SecondaryPressSink / ShellConfig 回调里 open(...)         │
├───────────────────────────────────────────────────────────┤
│ widgets 层（新文件 include/lumen/widgets/menu.h）          │
│   MenuItem（值类型模型）                                    │
│   MenuPanelBuilder（共享：面板 Widget 构建/键盘状态机）      │
│   ContextMenuController   MenuBarController                │
├───────────────────────────────────────────────────────────┤
│ app 层（既有，零改动）：M11 overlay（setOverlayBuilder/     │
│   clearOverlay/barrier/独立 identity/语义附加子树）          │
├───────────────────────────────────────────────────────────┤
│ core 层（唯一新缝隙）：PointerButton 穿透 +                 │
│   addSecondaryPressSink（sink 家族第 5 个成员）             │
└───────────────────────────────────────────────────────────┘
```

核心架构决策：**菜单是 overlay 上的一棵 Button 子树，不是新 WidgetType**。菜单项行复用 Button 的 hover/pressed/焦点/语义激活路径（M11 浮动菜单与集合控件行已双重验证）；菜单容器是带 elevation 的面板容器。`WidgetType`/RenderCommand/序列化版本零改动。

## 5. 菜单项模型（widgets 层）

```cpp
// include/lumen/widgets/menu.h（新）
namespace lumen::widgets {

struct MenuItem {
    std::string id{};          // 稳定标识：行 key = "menu:item:<id>"；回调参数
    std::string label{};       // 显示文本（空 + separator 时忽略）
    core::IconId icon{core::IconId::None};  // 左侧图标槽（None = 空槽对齐）
    bool separator{false};     // 分隔线行（其余字段忽略，不可聚焦）
    bool checkable{false};     // 显示勾选槽；checked 状态应用维护
    bool checked{false};
    std::string shortcut{};    // 展示文本（"Ctrl+S"）；仅显示，不执行
    bool hasSubmenu{false};    // true 时 submenu 回调提供子级（懒构建）
    bool enabled{true};
    char mnemonic{0};          // Alt+keyChar 助记字母（0 = 无）；P2 接线
};

// 菜单 = 项序列 + 懒子级回调。应用持有数据，控制器只读。
using MenuItems = std::vector<MenuItem>;
using SubmenuProvider = std::function<MenuItems(const std::string& id)>;

}  // namespace lumen::widgets
```

设计要点：

1. **checked 由应用维护**（与 Checkbox 的 bind 值同层）：菜单是瞬态面板，选择状态的事实来源在应用 StateStore；点击 checkable 项 → `onCommand(id)` → 应用翻转状态 → 控制器重开/刷新面板。
2. **子菜单懒构建**（`hasSubmenu` + provider）：深层菜单树按需展开，构建成本 O(可见面板)。
3. **mnemonic 是数据不是标记语法**：不采用 Qt `&文件` 内嵌标记，显式字段避免转义规则；首版不渲染下划线（见 §14）。

## 6. ContextMenu 控件

### 6.1 ContextMenuController

```cpp
class ContextMenuController {
  public:
    // 在指针位置（逻辑坐标）打开菜单。anchorTheme 语义同 DropdownController
    //（ThemeScope 内唤起时传入作用域主题；空 = shell 当前主题）。
    void open(app::AppShell& shell, core::Offset position,
              MenuItems items, SubmenuProvider submenu = {},
              const style::Theme* anchorTheme = nullptr);
    void close(app::AppShell& shell);
    [[nodiscard]] bool isOpen() const;

    // 键盘导航（应用 onKey 以 modal 优先级转发；仅打开时消费）。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers mods, char keyChar = 0);

    // 命令回调：普通项与 checkable 项统一走这里（含 "submenu 项被直接
    // 激活"时由应用自行决定展开或执行）。UI 线程；关闭菜单后触发。
    std::function<void(const std::string& id)> onCommand{};

    // 关闭后焦点恢复目标（默认 = 唤起前焦点；应用可指定行 key）。
    void setFocusRestoreKey(std::string key);
};
```

### 6.2 唤起通道（框架缝隙）

**事件穿透**：`AppShell::pointerDown`/`InteractionController::pointerDown` 增加缺省参数 `core::PointerButton button = core::PointerButton::Primary`（`runApp` 从 `HostEvent.button` 填充；headless 测试与既有调用零改动，帧哈希不变）。

**Secondary 语义**（`InteractionController`）：

- `button == Secondary` 的按下：**不进入** press/armed/drag/slide 路径（右键不产生点击、不抢占 Slider/滚动拖动/文本选区），hover 状态照常更新。
- 新增 sink（`interaction.h`，家族第 5 个成员）：

```cpp
// Secondary 按下咨询链：命中链 + 指针位置；返回是否消费。
// 命中链不做 enabled 过滤（对禁用行弹"属性"类菜单是合法场景）。
using SecondaryPressSink = std::function<bool(
    const std::vector<const core::RenderNode*>& hitChain,
    core::Offset position)>;
void addSecondaryPressSink(SecondaryPressSink sink);
```

- 无 sink 消费时维持现状（静默忽略），保证既有测试行为不变。

**应用接线**（无框架强制）：

```cpp
// 应用（或 ListController 的便捷集成，见 §6.5）注册 sink：
interaction.addSecondaryPressSink([&](chain, pos) {
    auto row = findKeyPrefix(chain, "list:files/item:");   // 命中集合行
    if (!row) return false;                                 // 非目标区域不消费
    contextMenu.open(shell, pos, buildMenuFor(rowKey));     // 应用提供项
    return true;
});
```

### 6.3 定位与翻转

- 菜单原点 = 指针位置 + (2, 2)px（避免指针压住首行文字，Win32 惯例）。
- 视口钳制：右缘不足 → 菜单左移至完全可见（或翻转到指针左侧）；下缘不足 → 上翻；最终 clamp 到视口内 8px 边距（`DropdownController` 同算法，抽出共享 `clampMenuRect` 私有实现）。
- 子菜单级联：原点 = 父行右缘 + 0、父行顶对齐；右缘不足翻左；级联各层独立钳制。

### 6.4 键盘契约（菜单打开期间，modal）

| 键 | 行为 |
| --- | --- |
| Up / Down | 高亮上/下一**可聚焦**项（跳过 separator/disabled；不环绕，到端即停——与集合行一致） |
| Home / End | 高亮首/末可聚焦项 |
| Enter / Space | 激活高亮项：普通/checkable → `onCommand`；子菜单项 → 展开子级并高亮首项 |
| Right | 高亮项有子菜单 → 展开；无 → 忽略 |
| Left | 位于子级 → 关闭子级、高亮回父项；顶级 → 忽略 |
| Escape | 关闭当前级；顶级 → 关闭全部并恢复焦点（`DropdownController` 同模式） |
| Tab / Shift+Tab | 关闭全部菜单并恢复焦点（菜单模态期间 Tab 不逃逸到主树） |
| Alt + keyChar | 命中高亮面板内 mnemonic 项 → 直接激活（P2 接线，见 §8.2） |

**超长菜单**：面板最大高度 = 视口高 − 2×8px；超出时面板内部经 ScrollView 兜底（overlay builder 已支持 wheel sink 注入），滚动跟随高亮项（`Visible` 对齐）。专用滚动箭头区不做。

滚动条按 `lumen-scroll-design.md` §5 支持手形悬停、轨道翻页和捕获拖动。按住滚动条期间，指针经过菜单行或其他菜单栏项不触发悬停级联/切换；释放后恢复正常悬停规则。

**悬停级联（M14 已实现）**：菜单打开期间悬停 `hasSubmenu` 项即自动展开子级（`MotionTokens::menuSubmenuHoverMs` 默认 0 = 立即，原生菜单惯例；担心掠过误弹的应用可设 300 之类去抖——去抖期内移走/移到其他项不展开）；悬停同级**其他**项则收起级联回到该层（含键盘展开的级联；级联源行自身不动，指针在源行与子面板间往返稳定）；点击 / Right 仍为立即展开。计时经 overlay animate sink 逐 tick 步进（pointer sink 无钟武装、首拍盖章，与打开动效同口径）。

**关闭时机**：barrier 点击、滚轮（任意位置）、窗口 resize（overlay builder 重求值后若锚定越界则关闭）、`close()` 显式调用。模态期间主树指针/键盘全部 NotHandled（M11 语义模态边界，M12 已统一）。**barrier 仅输入模态、视觉透明**——菜单不是对话框，不压暗内容（Dialog scrim 只属于 Dialog；Dropdown 浮动菜单同口径）。

### 6.5 集合控件集成（P2 便捷层）

`ListController`/`TreeController` 增加可选便捷接口（内部注册 SecondaryPressSink，key 前缀匹配自有行）：

```cpp
// 行右键 → 应用按行 key 提供菜单项（返回空 = 不弹）。
void setContextMenuProvider(
    std::function<lumen::widgets::MenuItems(const std::string& rowKey)> provider);
```

Tree 的子级展开/行语义不受影响；对禁用行同样回调（由应用决定项集）。不提供 provider 的应用零成本（不注册 sink）。

## 7. MenuBar 控件

### 7.1 MenuBarController

```cpp
class MenuBarController {
  public:
    struct TopMenu { std::string id; std::string title; char mnemonic{0}; };

    void setMenus(std::vector<TopMenu> menus);   // 栏结构（项级懒取）
    void setMenuProvider(std::function<MenuItems(const std::string& id)> provider);

    // 栏 Widget：普通主树子树（Ghost Button 行，非 overlay）。
    // 栏项 key = "menu:bar:<id>"；打开的菜单经 ContextMenuController 同一
    // 面板路径锚定在栏项下方（锚定矩形 = 栏项 bounds）。
    [[nodiscard]] core::Widget build(const style::Theme& theme) const;

    // 事件入口：应用 ShellConfig.onKey 转发（栏焦点时）。
    bool handleKey(app::AppShell& shell, core::Key key,
                   core::KeyModifiers mods, char keyChar = 0);
};
```

### 7.2 行为契约

- **栏 = 普通 Widget 子树**（Row + Ghost Button），参与 Tab 遍历与语义树；菜单面板 = overlay。栏不模态，菜单打开才模态。
- 打开方式：点击栏项，或**菜单已打开时 hover 切换**相邻栏项（桌面惯例：拖过栏即切换）。
- 菜单打开期间：Left/Right 切换顶级菜单（锚点随之移动），Esc 关闭并焦点恢复栏项。
- Alt+mnemonic：栏拥有焦点或菜单打开时，`Alt + keyChar` 直接打开对应顶级菜单并高亮首项；mnemonic 大小写不敏感。
- 键盘打开栏菜单的 F10/Alt 单键路径需要 `Key` 枚举扩展（当前无 F 键/Alt 键值），留开放问题（§14）。

## 8. 语义契约与键盘一致性

### 8.1 语义树（SemanticsRole 尾部追加，Tree/TreeItem 先例）

| 节点 | role | 状态/动作 |
| --- | --- | --- |
| 菜单面板 | `menu`（新 role） | 模态期间作为根语义附加子树（overlay 既有路径） |
| 菜单项 | `menuitem`（新 role） | label = 项文本；`checked` flag（checkable 项）；`Activate` action ≡ Enter ≡ 单击；current 由**焦点**表达（不暴露 `selected`——M14 起高亮背景由动能矩形常驻承载，两配置语义一致） |
| 子菜单项 | `menuitem` | 另带 semanticsValue `hasSubmenu="true"`；Activate = 展开（同键盘） |
| 分隔线 | 不进语义树 | 纯视觉（既无 label 也无 action；屏幕阅读器惯例跳过） |
| 菜单栏 | 既有 role 复用 | 栏 = Group；栏项 = Button（label 含 mnemonic 去除标记后的文本） |

### 8.2 三层一致性断言（headless 验收）

- 单击 ≡ Enter ≡ 语义 Activate 触发同一 `onCommand(id)`。
- checkable 勾选视觉（Check 图标）≡ `checked` 语义 flag ≡ 应用状态翻转。
- 键盘高亮项必有可见指示（§10.3，选中底色块常驻——视觉系统 §5 规则 4；菜单行永不叠焦点环）。
- barrier/Esc/Tab 关闭后主树焦点恢复到指定 key（`DropdownController` 值行恢复同模式）。
- 模态期间主树节点 Activate/滚动语义 NotHandled（M12 统一规则回归）。

## 9. Widget / DSL 扩展与体积预算

| 项 | 增量 |
| --- | --- |
| `WidgetType` | **零新增**（菜单面板 = overlay 上的 Container/Row/Button 组合） |
| `Widget` 字段 | **零新增**（菜单数据在控制器，Widget 只承载声明；M7 体积预算无感） |
| RenderCommand / 序列化版本 | **零改动**（elevation 复用 DrawShadow，勾选/级联复用 DrawIcon） |
| `.lumen` DSL | 不加节点（菜单为运行时 overlay 行为，与 Dialog 同理由不进冻结节点集） |
| C++ DSL | `makeMenuBar(controller, theme)` 便捷 builder（组合既有 Row/Button） |

框架侧唯一接口增量：`InteractionController::pointerDown` 的 button 参数与 `SecondaryPressSink`（`addSecondaryPressSink`）。

## 10. 视觉规格（详见 design/menu-controls.html）

视觉契约完全遵循 `docs/lumen-visual-system-design.md`（token 三层模型 §3.1、尺度表 §3.2、状态规则 §5、elevation §8），本节只做部件级映射，**不新增颜色/尺度槽位**。

### 10.1 尺度（视觉系统 §3.2 对齐）

| 项目 | Small/Compact | Medium/Comfortable | Large/Touch | 依据 |
| --- | --- | --- | --- | --- |
| 菜单项最小高度 | 32px | 40px | 48px | 视觉系统"控件最小高度"行（长菜单应用可显式选 Small） |
| 菜单项水平内边距 | 8px | 12px | 16px | 视觉系统"水平内边距"行 |
| 菜单项圆角 | 4px | 6px | 8px | 视觉系统"小部件圆角"行 |
| 图标/勾选槽宽 | 20px（图标 16 + gap 4） | 同 | 同 | chevron 命中区惯例同源（collection §10.1） |
| 快捷键列 | 右对齐，与 label 间隙 ≥ 24px | 同 | 同 | 4px 网格；muted 前景 |
| 子菜单指示 | ChevronRight 16px，右缘内 16px | 同 | 同 | `IconId` 既有 |
| 分隔线 | 1px，水平 inset 8px | 同 | 同 | `color.border.default` |
| 面板内边距 | 4px | 同 | 同 | 项圆角与面板圆角间留白（4px 网格） |
| 面板最小/最大宽 | 160px / 320px | 同 | 同 | 快捷键列可达性；超宽内容省略号 |
| 面板最大高 | 视口 − 16px | 同 | 同 | 超出经 ScrollView 兜底（§6.4） |
| 面板圆角 | 8px | 同 | 同 | 视觉系统"卡片默认圆角"行 |

### 10.2 组件 token（三层模型 §3.1 的 component 层）

```text
menu.panel.background       = color.background.elevated
menu.panel.border           = color.border.default
menu.panel.radius           = 8（卡片档）
menu.panel.elevation        = ElevationTokens level 2（(0,4)/12/64）
menu.panel.innerPadding     = 4
menu.item.background.hover  = surface 层 hover 派生（surface→elevated 之间）
menu.item.background.current= color.selection.background（键盘高亮）
menu.item.content           = color.content.primary / .disabled
menu.item.content.shortcut  = color.content.secondary
menu.item.separator         = color.border.default
menu.open.fadeMs            = 120（对齐 tooltipFadeMs；reduceAnimation 归零）
```

菜单打开淡入经 M10 转场驱动（overlay 根 `transitionAlpha`），`reduceAnimation` 零时长直达（FrameScheduler 既有规则）。

**M14 动效实现口径（2026-09-18，`design/menubar-variants.html` 版本 A+D 已落地）**：

- 悬停等待与 tween 的活跃状态独立合并：配置非零 `menuSubmenuHoverMs` 时，即使打开/高亮动画先结束，悬停计时仍须维持 tick，直到展开或取消；采样输出不得覆盖计时状态（2026-09-21）。
- `MotionTokens::menuOpenFadeMs = 120`：整面板淡入 + 位移（顶级上升 6px、子菜单沿级联方向滑入 4px，同一 EaseOut 进度），经 overlay animate sink（`setOverlayBuilder` 第 4 参数）逐 tick 采样；不经 tick 的直驱输出保持即时终态（与状态过渡 `motionEnabled()` 同口径）。**采样与悬停展开同拍有序**（2026-09-21）：悬停展开在 tick 内先于动效采样执行，本拍新增层级随即起表（`openT = 0`），首次绘制即动效起点——否则新层级以默认终态（alpha 1、无位移）先绘制一帧，慢帧率（Debug）下呈现"先整幅出现、再消失重放动效"的闪帧；键盘/直驱展开（事件阶段、展开后无同拍采样）保持 pending 即时终态口径不变。
- `MotionTokens::menuHighlightSlideMs = 90`：键盘高亮 = 动能矩形滑移。高亮背景**常驻**由独立 selection 矩形承载（动效/无动效两口径统一——与行 selected 背景同色同矩形、source-over 复合等价，像素一致；跨分隔线高度变形；焦点环仍随焦点行）；行不再折算 `selected`（§8.1 契约：menuitem 的 current 语义由焦点表达，`kSemanticsSelected` 不暴露——两配置下语义一致）。
- 关闭即时（不出场淡出）：瞬态命令面板的关闭延迟直接吃命令分发延迟，有意不做。
- 分隔线几何：1px 线 + 上下 4 呼吸（共 9px 占位）+ 水平 inset 8（本稿 msep 同口径）。

### 10.3 状态矩阵

| 状态组合 | 背景 | 前景/内容 | 指示 |
| --- | --- | --- | --- |
| normal | 透明（面板 elevated 底） | ink | — |
| hover | surface hover 派生 | ink | — |
| current（键盘高亮） | `color.selection.background`（动能矩形承载） | ink | **无焦点环**（M14：resolver 对 `collectionRow && semanticsRole=="menuItem"` 的行置零 focusWidth——零新增 Widget 字段；环叠选中底色双指示冗余，聚焦可见性由选中底色块满足，规则 4 合规） |
| hover + current | current 覆盖 hover（§5 规则 3 同源） | ink | 无焦点环（同上） |
| checked（checkable） | 不变 | ink | 图标槽 Check 图标（`Widget.checked` → 既有状态位） |
| disabled | 不变 | `color.disabled.content` | 命中拒绝 + 键盘跳过 + 语义一致 |
| separator | — | — | 1px 线，不可聚焦 |

高对比主题：勾选必须有 Check 图标形状（不是色块）；current 的选中底色块保持可见——**不得只靠细微色差区分**（视觉系统 §11 约束；高对比派生加大 selection 与表面的对比）。

### 10.4 MenuBar 视觉

栏项 = Ghost Button（rest 透明、hover surface 派生、打开时 Tonal 态表示激活）；栏高 = 控件最小高度档（32/40/48）；栏与内容间 1px `color.border.default` 分隔。菜单面板规格同 ContextMenu（锚定改为栏项下方）。

**M14 打开态指示（已实现）**：打开的栏项在 Tonal 之上叠加底部 2px accent 下划线（常驻 2px 占位保栏高稳定；随 `menuOpenFadeMs` 渐入——transform 通道未接线，以透明度等价表达宽度生长）；顶级切换 = 重新锚定并重放打开动效；关闭即回 Ghost、下划线消失。`MenuBarController::build(const style::Theme&)` 提供 accent/动效口径。

## 11. 性能与测试计划

### 11.1 性能口径

菜单为小树（典型 ≤ 50 项，懒子级 O(可见面板)），不进基准场景；要求：

- 打开 = 一次 overlay 全量重绘（M11 既有路径），无额外动画帧空转（M12 deadline 教训回归）。
- 键盘导航仅 damage 高亮变化行（局部重绘与全帧逐像素一致，M3 模式回归）。
- Secondary 缺省参数化后既有全部测试与帧哈希不变。

### 11.2 测试（Catch2，`*_tests.cpp`，行为命名）

1. **PointerButton 穿透**：Secondary 按下不触发 click/press/armed/drag；sink 消费/未消费两路；Middle 同理静默。
2. **ContextMenuController**：open/close 生命周期、定位钳制（右/下缘翻转、8px 边距）、键盘全契约（Up/Down 跳过 separator/disabled、Enter/Space/Right/Left/Escape/Tab）、超长菜单 ScrollView 兜底与高亮跟随。
3. **MenuItem 模型**：checkable 激活回调、快捷键**仅展示**（onKey 不被菜单拦截）、disabled 四层一致。
4. **子菜单**：懒构建只调用被展开的 provider、级联定位翻转、Left 回父级、Escape 逐级关闭。
5. **语义**：menu/menuitem role 树、checked flag、Activate ≡ Enter ≡ 单击同 handler 回执（RecordingBridge）、模态期主树 NotHandled、分隔线缺席。
6. **MenuBar**：栏 Widget 组合（Ghost 按钮/键/key 前缀）、hover 切换、Left/Right 顶级切换、Alt+keyChar 打开、Esc 焦点恢复栏项。
7. **M14 动效**（`tests/menu_motion_tests.cpp`）：打开淡入+上升全程采样（含 renderFrame 冒烟）、动能矩形跨分隔线滑移、栏 Tonal/下划线渐入与切换重放、reduceAnimation 首拍终态、不经 tick 直驱即时终态回归、overlay animate sink 生命周期。
8. **回归**：gallery/settings 哈希不变；reduceAnimation 菜单零时长；高对比/density/fontScale 派生（style 既有用例模式）。

### 11.3 示例与验收

- settings 新页 `Menus`：栏（文件/视图/帮助）+ 三个右键场景（列表行/树行/空白处）+ checkable/快捷键/子菜单演示。
- gallery 控件清单页补菜单样本。
- headless 冒烟：打开→导航→激活→关闭全链路输出；三桌面窗口 smoke 由 CI 承担。

## 12. 实施分期

| 阶段 | 内容 | 出口条件 |
| --- | --- | --- |
| P1 上下文菜单 | PointerButton 穿透 + SecondaryPressSink + MenuItem/ContextMenuController（平面菜单：label/icon/checkable/shortcut/separator/disabled）+ 定位钳制 + 语义 + 测试 + settings 演示页 | 右键全链路（唤起→键盘→激活→关闭→焦点恢复）三层一致；既有哈希不变 |
| P2 菜单栏与级联 | 子菜单懒构建与级联、MenuBarController（hover 切换/左右切换/Alt+mnemonic）、集合控件 setContextMenuProvider 便捷层 | 栏菜单与上下文菜单共享面板语义；mnemonic 键盘可达 |
| P3 按需评估 | 触摸长按唤起、F10/Alt 单键（Key 枚举扩展）、专用滚动箭头区、mnemonic 下划线渲染 | — |

各阶段独立可交付；P2 依赖 P1 的面板构建与键盘状态机（`MenuPanelBuilder` 抽出为共享私有实现）。

## 13. 兼容与迁移

1. `DropdownController` 不动（值选择语义独立保留）；面板构建的钳制算法抽出共享后 Dropdown 行为不变（既有 2 用例回归）。
2. `AppShell::pointerDown` 的 button 为**缺省参数**：runApp 填充真实值，既有 headless 直驱调用不改动、帧哈希不变。
3. `SecondaryPressSink` 无注册时行为与现状完全一致（静默忽略 Secondary）。
4. `SemanticsRole` 尾部追加 `Menu`/`MenuItem`，既有 role 数值不变（Tree/TreeItem 先例）；语义序列化（如有版本）同步 bump。
5. 新增 RenderCommand：无；序列化版本：不动。

## 14. 开放问题（实施前需确认）

1. **F10/Alt 单键打开菜单栏**：`Key` 枚举当前无功能键与 Alt 键值；扩展枚举成本（平台映射三处）vs 收益（Windows 惯例）——建议 P3 评估，首版仅 Alt+字母。
2. **mnemonic 下划线渲染**：TextStyle 无静态下划线通道（preedit underline 是 painter 专用）；选项：a) 扩展 TextStyle b) 首版不画下划线仅键盘生效。建议 b。
3. **快捷键文本的平台书写**（Ctrl+S vs ⌘S vs Ctrl, S）：框架不做键名本地化，应用自填字符串（当前设计）；是否提供 `formatShortcut(KeyModifiers, char)` 便捷工具留按需。
4. **触摸长按唤起**：PointerDevice::Touch + press 时长 > 500ms 判定（timestampMs 已携带）；与滚动/选区手势的冲突仲裁——建议随桌面触摸实测再定。
5. **菜单内嵌控件**（如"缩放"滑块行）：参考 Qt widget action；首版明确不做，行内只能 Icon/label/shortcut/check/chevron。
