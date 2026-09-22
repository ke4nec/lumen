# Lumen 原生无障碍 Provider 设计（M13：UIA / AT-SPI / NSAccessibility）

> 文档状态：实施设计；2026-09-22 状态：P1 Windows UIA 已实施，P2 AT-SPI/P3 NSAccessibility 待做，三平台真实屏幕阅读器人工验收待做。
> 输入：源码现状盘点（`include/lumen/accessibility/bridge.h`（AccessibilityBridge 契约）、`include/lumen/accessibility/semantics.h`（语义树/role/flags/actions）、`src/app/app_shell.cpp`（pushSemantics/performAccessibilityAction）、`include/lumen/platform/application_host.h`（PlatformCapabilities））、`docs/lumen-self-use-roadmap.md` §4 M13、M5/M10/M11 完成记录、`docs/lumen-collection-controls-design.md` / `docs/lumen-menu-controls-design.md` / `docs/lumen-splitter-design.md`（新控件语义契约）。
> 定位：把 M5 冻结的语义契约接到三平台原生屏幕阅读器。**零新增 WidgetType、零新增 RenderCommand、零 core 改动**；`LUMEN_ENABLE_ACCESSIBILITY_BRIDGE`（v0.3 阶段 8C 预留开关）默认关闭，未编入时行为与现状完全一致。

---

## 1. 背景与问题

M5 已完成语义契约收口（identity diff、invalid/hidden flags、语义 action、Recording bridge 回归证据）；当时 `createPlatformAccessibilityBridge` 返回 nullptr。M13 P1 已增加可选 Windows UIA provider，P2/P3 仍待实现，且真实屏幕阅读器（讲述人/NVDA、Orca、VoiceOver）人工验收未完成。此后控件面持续扩大（List/Tree/TreeList、Menu/ContextMenu/MenuBar、Splitter、自定义标题栏），当前 `SemanticsRole` 有 23 个枚举值并覆盖原始控件与后续控件；具体映射以源码及本文 §5/§6 为准。

源码核对后的关键事实：

| 事实 | 出处 | 影响 |
| --- | --- | --- |
| 桥接契约已冻结 | `AccessibilityBridge::updateTree/setFocusedNode/noteActionPerformed` | provider 只需实现消费侧 |
| action 回灌路径已存在 | `AppShell::performAccessibilityAction`（与键盘/指针同路径） | AT 请求无需新通道 |
| 语义树按帧推送 | `AppShell::pushSemantics`（绘制末尾、identity diff） | provider 天然增量更新 |
| 数值控件 0..100 百分比 | `performSemanticsAction` SetValue（strtof 解析字符串） | RangeValue 契约直接映射 |
| SDL3 无原生无障碍 API | SDL3 头文件盘点（只有消息泵钩子） | 平台接入必须走原生层 |

## 2. 参考框架与平台事实

| 框架 | provider 形态 | 对 Lumen 的启示 |
| --- | --- | --- |
| Win32/UIA | `WM_GETOBJECT` 返回 `UiaReturnRawElementProvider` 的 provider；fragment 树 + patterns | hit-test 式应答是唯一入口；拖动/resize 一样交给平台 |
| Qt | `QAccessibleInterface` 树 + at-spi bridge（QDBus 直连 org.a11y.Bus） | 应用侧 AT-SPI 走裸 D-Bus 协议（不依赖 libatspi）是成熟先例 |
| GTK4 | `GtkAccessible` → `AtspiContext`（同样 D-Bus） | RegisterApplication + org.a11y.atspi.* 接口族 |
| Flutter | `SemanticsUpdate` → 平台 embedder 转原生 | 语义树快照 + 稳定 id 是跨 provider 的公共形态（Lumen 已有） |

**采纳的共同事实**：

1. **provider 是语义树的投影，不持有 UI 状态**：所有答案从最新 `updateTree` 快照读取；action 请求回灌 `dispatch`。
2. **稳定 identity 是 AT 焦点不漂移的前提**：UIA RuntimeId / AT-SPI 对象路径 / NSAccessibility element 身份都从语义 identity 派生。
3. **单线程 UI 拥有**：AT 调用与 updateTree 同在 UI 线程（WM_GETOBJECT SendMessage 直达、AT-SPI D-Bus 方法在泵线程分发、NSAccessibility 主线程），无锁。
4. **事件是通道，不是真相**：结构/属性/焦点事件只通知 AT 重取；provider 数据源始终是快照。

## 3. 设计目标与非目标

**目标**

1. 公共契约最小扩展：`PlatformAccessibilityHost`（dispatch 回灌 + `void*` 原生窗口句柄 + 倍率/应用名）+ 工厂改签名 + `accessibilityProviderName()` 编译事实查询。SDK 类型不进公共头。
2. `runApp` 统一装配：选项编入且 `RunOptions.nativeAccessibility`（默认 true）时创建原生桥、注入 dispatch、置位 `PlatformCapabilities.accessibility`；失败结构化降级 + 诊断。
3. Windows UIA provider（P1，本地真验）：子类化 HWND 应答 WM_GETOBJECT；fragment 树 + Invoke/Toggle/Value/RangeValue patterns；结构/属性/焦点事件。
4. AT-SPI（P2）/NSAccessibility（P3）按本文映射表实施，CI 首跑为事实来源。
5. 全部新路径 headless 可测（COM 直驱 + 事件记录器）；真实窗口端到端经 UIA 客户端 API 冒烟。

**非目标（第一版明确不做）**

- Text pattern（文本编辑的逐字/选区暴露）——TextField 以 Value pattern 值读写闭环；富文本属按需评估池。
- Scroll pattern/滚动事件——语义 Scroll action 为增量（deltaY），UIA ScrollAmount 语义不匹配；AT 用焦点导航+页面级阅读替代。
- Selection pattern（List/Tree 多选语义）——单选激活经 Invoke 闭环；多选暴露留按需。
- 多窗口（每窗口一个桥；当前单窗口应用壳）。
- 表格/图形/自定义 annotation（UIA CustomNavigation 等）。

## 4. 总体架构

```text
AppShell（语义树构建/推送/action 分发——M5 既有）
  │ pushSemantics: updateTree(tree, diff, focusedId)
  │ 焦点变化: setFocusedNode(id)
  │ action 回执: noteActionPerformed
  ▼
AccessibilityBridge（契约，v0.3 冻结）
  ▲
  │ dispatch(nodeId, action, value, scrollDeltaY)   ← AT 请求（UI 线程同步）
  │ nativeWindow / deviceScale / applicationName
PlatformAccessibilityHost（M13 新增，纯值类型）
  ▼
createPlatformAccessibilityBridge（按编译定义分发）
  ├─ LUMEN_ACCESSIBILITY_PROVIDER_UIA（src/accessibility/uia_provider.*）
  ├─ LUMEN_ACCESSIBILITY_PROVIDER_ATSPI（P2）
  └─ LUMEN_ACCESSIBILITY_PROVIDER_NSACCESSIBILITY（P3）
```

- **runApp 装配**（`src/app/run_app.cpp`）：窗口创建后经 `host.nativeWindowHandle(id)` 取原生句柄（SDL 属性：Windows `SDL_PROP_WINDOW_WIN32_HWND_POINTER`、macOS `SDL_PROP_WINDOW_COCOA_WINDOW_POINTER`、Linux nullptr——AT-SPI 不需要），`dispatch` 捕获 `shell.performAccessibilityAction`。桥为局部 `unique_ptr`：函数返回时先于宿主窗口销毁析构（断开 WM_GETOBJECT/子类/事件）。`host.noteAccessibilityBridgeActive` 如实置位能力。
- **生命周期与重入**：dispatch 可能同步触发重建/推送（handler → setState → 下一帧；或 performAccessibilityAction 内 pushSemantics），pattern 方法先拷贝自身 id，dispatch 后不再读树状态；provider 缓存按 identity 复用，桥析构统一 `UiaDisconnectProvider` 断开——AT 残留引用读快照返回 NotAvailable，不悬空。

## 5. Windows UIA provider（P1，已实施）

### 5.1 窗口接入

SDL3 只有消息泵钩子（`SDL_SetWindowsMessageHook`，Peek 循环内），而 WM_GETOBJECT 由 AT `SendMessage` 直达窗口过程——钩子拦不到。采用 `SetWindowSubclass`（comctl32）子类化宿主 HWND：

- 仅应答 `lParam == UiaRootObjectId` 的请求（MSAA `OBJID_*` 走默认，本 provider 是 UIA-only）；`UiaReturnRawElementProvider(hwnd, wParam, lParam, rootProvider)`。
- 析构顺序：`UiaReturnRawElementProvider(hwnd, 0, 0, nullptr)` → `RemoveWindowSubclass` → `detached` 置位 → 逐 provider `UiaDisconnectProvider` 并释放表内引用。
- HWND 为空（headless 测试）＝可用但不连接窗口通道（不应答、不进 UIA raise）。

### 5.2 COM 结构

| 对象 | 接口 | 承载 |
| --- | --- | --- |
| `UiaRootProvider` | Simple + Fragment + **FragmentRoot** | 语义根（role Window）；`ElementProviderFromPoint`（物理→逻辑→deepestNodeAt，子树逆序命中，空白归属根）、`GetFocus`（focusedId）；`get_HostRawElementProvider` = `UiaHostProviderFromHwnd`；RuntimeId 返回 null（HWND 根由 UIA 提供） |
| `UiaNodeProvider` | Simple + Fragment + Invoke/Toggle/Value/RangeValue（同对象，GetPatternProvider/QI 按当前快照门控） | 每语义节点一个，`state_->providers` 表缓存（identity → 同一实例） |

注：`IRawElementProviderFragmentRoot` 与 `IRawElementProviderFragment` 在 C++ 绑定中**平行**（各自仅继承 IUnknown）；根对象两者皆实现，`acquireFragment` 对 rootId 走 `static_cast<IRawElementProviderFragment*>` 基子对象。

`ProviderOptions_ServerSideProvider`；`Fragment::SetFocus` → dispatch `kActionFocus`（AT 聚焦请求走 FocusManager 路径）。

### 5.3 映射表

**Role → ControlType**（根固定 Pane：HWND 已提供窗口框架元素）：

| SemanticsRole | ControlType | 备注 |
| --- | --- | --- |
| Button / MenuItem | Button / MenuItem | |
| Menu / List / ListItem | Menu / List / ListItem | |
| Tree / TreeItem | Tree / TreeItem | |
| TextField | Edit | |
| Checkbox / Switch | CheckBox | Switch 无专用类型（勾选语义一致） |
| Radio | RadioButton | |
| Image | Image；Text → Text | |
| Slider / **Splitter** | Slider | Splitter 无专用类型（可调 0..100 值） |
| ProgressBar | ProgressBar | |
| Group | Group | |
| Window / Dialog | Pane | Dialog 另置 `UIA_IsDialogPropertyId` |

**Flags → Properties**：Enabled→IsEnabled；Focused→HasKeyboardFocus（**flag 与 focusedId 双源**——setFocusedNode 可先于下一帧 flag 推送）；Hidden→IsOffscreen；Invalid→`!IsDataValidForForm`；Checked→ToggleState（pattern）。另：IsKeyboardFocusable = actions 含 Focus 且 enabled；IsPassword 恒 false（语义层已隐藏密码值）。

**Actions → Patterns**：

| 条件 | Pattern | 行为 |
| --- | --- | --- |
| actions ∋ Activate | Invoke | Invoke → dispatch(Activate) |
| role ∈ {Checkbox, Switch, Radio} | Toggle | ToggleState 读 checked；Toggle ≡ Activate |
| role == TextField 且 ∋ SetValue | Value | 值读写；IsReadOnly = 不声明 SetValue |
| role ∈ {Slider, ProgressBar, Splitter} | RangeValue | 0..100（min/max 固定，值 strtof 解析语义 value）；SetValue → `"%.3f"` 字符串 dispatch（`InteractionController::setSliderValue/setSplitterValue` strtof 解析） |

**事件**（经 `UiaEventSink` 出口，默认实现 UIA raise，测试注入记录器）：

- added/removed 非空 → `UiaRaiseStructureChangedEvent(root, ChildrenInvalidated)`（树规模下 AT 重取，正确性优先于细粒度）。
- changed → 逐节点字段级比较旧/新快照：label→Name、value→Value/RangeValueValue、enabled→IsEnabled、checked→`UIA_ToggleToggleStatePropertyId`、focused→HasKeyboardFocus、invalid→IsDataValidForForm、bounds→BoundingRectangle。
- setFocusedNode → `UIA_AutomationFocusChangedEventId`。

**RuntimeId**：identity FNV-1a 64 → `{UiaAppendRuntimeId, lo, hi}`——跨重建稳定；removed 后表内释放。

### 5.4 坐标与 DPI

语义 bounds 为窗口逻辑坐标；`get_BoundingRectangle`/`ElementProviderFromPoint` 用 `ClientToScreen` + `GetDpiForWindow`（动态解析，Win10 1607 前回退 96）实时换算；headless（无 HWND）用 `PlatformAccessibilityHost.deviceScale` 快照。

## 6. AT-SPI2 provider（P2，规划）

Linux 侧走 org.a11y.Bus D-Bus 协议（Qt/GTK 同款；仓库已有 libdbus 会话总线先例——M12 native_services）。

- **总线接入**：`$XDG_RUNTIME_DIR/at-spi/bus` 文件取总线地址（回退会话总线 `org.a11y.Bus.GetAddress`）；不可达 → 工厂 nullptr + 诊断（无桌面会话属正常）。
- **注册**：`org.a11y.Bus.RegisterApplication`（应用根路径 `/org/lumen/accessible/<appname>`），随后实现对象接口族。
- **对象树**：每语义节点一个 D-Bus 对象路径 `/org/lumen/accessible/<appname>/<hash>`（identity 哈希，同 UIA RuntimeId 派生）。
- **接口映射**：

| AT-SPI 接口 | 承载 |
| --- | --- |
| org.a11y.atspi.Accessible | GetRole（role→AtspiRole 枚举：PushButton/MenuItem/List/Tree…近似表）/GetName/GetChildAtIndex/GetChildCount/GetParent/GetState（enabled/focusable/focused/checked/invalid→invalid_entry） |
| org.a11y.atspi.Component | GetExtents（CoordType SCREEN/WINDOW；deviceScale 换算，窗口原点经宿主窗口位置）/Contains |
| org.a11y.atspi.Action | GetActions（n_actions=1 "activate"/"focus"/"set value" 按 actions 位）/DoAction → dispatch |
| org.a11y.atspi.Value | 当前值/极值（Slider/ProgressBar/Splitter 0..100；TextField 值字符串经 EditableText？——首版 Value 只覆盖数值控件） |
| org.a11y.atspi.Socket/Embed | 桌面嵌入路径（AT 侧主动 Embed，应用侧只需响应） |

- **事件**：children-changed（结构）、property-change（accessible-name/value/state）、focus（state-changed:focused）经总线信号广播。
- **线程**：D-Bus 连接在 UI 线程泵内 `dbus_connection_dispatch`（SDL 事件等待间隙）；或 `DBusConnection` 浅集成到宿主 poll——P2 实施时定夺，保持"UI 线程拥有"不变量。
- **能力**：连接成功 → `available()` true；Orca 回环属 M13 出口验收。

## 7. NSAccessibility provider（P3，规划）

macOS 侧以 AppKit `NSAccessibilityElement` 树挂到 SDL 窗口 contentView：

- **接入**：`nativeWindowHandle` 返回 NSWindow*（SDL 属性）；provider 侧 ObjC++ 取 `contentView`，`setAccessibilityElements:` 不适用于非自绘 NSView——改为向 contentView 附加自定义 `NSAccessibilityElement` 根（`accessibilityChildren` KVC 覆盖，SDLOpenGLView 无子元素时可安全附加）。
- **映射**：role→AXRole（AXButton/AXCheckBox/AXSlider/AXList/AXOutline（Tree）/AXMenuItem/AXTextField/AXStaticText/AXImage/AXGroup/AXSplitGroup…）；label→AXDescription/AXTitle；value→AXValue（数值控件 NSNumber 0..100）；checked→AXValue @1/@0；focused→AXFocused 子树元素；bounds→AXPosition/AXSize（AppKit 点坐标=逻辑像素，天然一致）。
- **action**：`accessibilityPerformPress` → dispatch(Activate)；`accessibilitySetAccessibilityValue` → dispatch(SetValue)；Slider 键盘调节同路径。
- **事件**：`NSAccessibilityPostNotification`（FocusedUIElementChanged/LayoutChanged/ValueChanged）。
- **验收**：VoiceOver 回环（M13 出口）。

## 8. 测试口径

- **headless（ctest 常规）**：`tests/a11y_provider_tests.cpp`——工厂分流（未编入安全降级/编入名称一致）、Fake host 能力如实翻转、fragment 导航顺序、属性映射（role/flags/bounds×倍率）、Invoke/Toggle/Value/RangeValue 回灌与值域、事件序列（结构/属性/焦点）+ GetFocus 一致、RuntimeId/provider 跨更新稳定、移除后不可达、dispatch 重入（id 拷贝、残留引用安全）、根命中测试（最深节点/空白归属根）。
- **真实窗口端到端**（`LUMEN_UIA_LIVE_SMOKE=1` 启用，默认跳过）：真实 Win32 窗口 + UIA 客户端 `CUIAutomation::ElementFromHandle` → FindFirst("OK") → Invoke → dispatch 回执——完整链路（子类应答/UIA core/代理层）不依赖屏幕阅读器。
- **Recording bridge 回归**：既有语义契约测试不变（工厂未编入断言移入分流用例）。
- **未启用开关**：全量套件与 v0.3 现状一致（选项默认 OFF；OFF 构建全绿）。
- **人工验收（M13 出口）**：讲述人/NVDA、Orca、VoiceOver 各完成焦点导航/激活/值设置回环。

## 9. 性能与体积预算

- Widget/RenderNode 零字段新增；桥在应用侧持有（`runApp` 局部），core 无感。
- 语义树快照每帧按需拷贝（`std::map` 节点表）——推送只发生在树变化帧（M5 既有），静态场景零开销；事件无 AT 监听时 UIA raise 为近零成本（且 headless/未连接时短路）。
- COM provider 按节点缓存复用（不随帧重建）；`UiaDisconnectProvider` 只在节点移除/桥析构时调用。

## 10. 实施分期

| 阶段 | 内容 | 出口条件 | 状态 |
| --- | --- | --- | --- |
| P1 Windows UIA | 契约扩展 + runApp 装配 + UIA provider（子类化/fragment 树/patterns/事件）+ headless/端到端测试 | UIA 客户端端到端通过；讲述人/NVDA 人工回环 | 已实施（讲述人人工回环待办） |
| P2 Linux AT-SPI | org.a11y.Bus 直连 + §6 接口族 + 事件 | Orca 回环；CI 首跑编译/冒烟 | 待做 |
| P3 macOS NSAccessibility | §7 NSAccessibilityElement 树 + 通知 | VoiceOver 回环；CI 首跑 | 待做 |

## 11. 兼容与迁移

1. 工厂签名从 `(std::string*)` 扩为 `(const PlatformAccessibilityHost&, std::string*)`——唯一调用方（semantics 测试）同变更；Recording bridge 不受影响。
2. `RunOptions.nativeAccessibility` 默认 true：选项未编入时工厂恒 nullptr（行为不变）；编入且 headless Fake host 时桥以无窗口模式运行（不应答/不广播，能力位如实置位）。
3. `ApplicationHost` 新增 `nativeWindowHandle`/`noteAccessibilityBridgeActive` 均默认实现（契约 host 安全降级）；Fake host 如实翻转能力位。
4. 序列化版本/RenderCommand/语义契约（M5 冻结）：零改动。

## 12. 风险与已知限制

- **P1 限制**：Scroll/Selection/Text pattern 不做（§3）；Switch/Splitter 以近似 ControlType 暴露；`UiaRaiseStructureChangedEvent` 为整体失效（细粒度子树失效留优化）；WM_GETOBJECT 子类化依赖 comctl32（Win7+ 系统 DLL，无清单要求）。
- **DPI 热切换**：`GetDpiForWindow` 实时读取，但 AT-SPI/P3 的 deviceScale 为创建时快照（DPI 变化帧重建桥的路径留应用侧）。
- **AT-SPI/NSAccessibility 未实施**：能力位在两平台保持 false（工厂降级 + 诊断），不阻塞 P1 验收；M13 出口条件以三平台全收口为准。
