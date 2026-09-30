# Lumen 可编辑 ComboBox 设计（G-4b）

> 状态：已实现（2026-09-30；gap-backlog G-4 第二批）。
> 动机：Dropdown 为纯选择（`lumen-dropdown-design.md`），无可编辑组合框；工具应用高频（命令面板/字体/路径选择）。
> 代码：[`include/lumen/widgets/combo_box.h`](../include/lumen/widgets/combo_box.h)、`src/widgets/combo_box.cpp`。
> 测试：`tests/combo_box_tests.cpp`。
> Mockup：`design/combo-box.html`。

## 1. 结构（零新增 WidgetType/RenderCommand）

值行 = Row{TextField(flex) + Ghost Button "▾"(28px 宽)}；输入字段 bind 自由编辑，按钮唤起面板（onClick = `<key>:toggle`，点击/键盘同路径）。

面板 = `ContextMenuController::openAnchored`（锚 = 值行矩形，下方不足翻上）：barrier 模态、键盘 Up/Down/Enter/Escape、语义行、滚动、焦点恢复、M14 动效全部复用 menu-controls-design 契约——本控件不新增 overlay 逻辑。

## 2. 过滤语义

| 场景 | 行为 |
| --- | --- |
| 打开期输入（bind 变化） | 大小写不敏感子串过滤（label 与 value 都参与），面板同锚重开（复开语义清旧层，高亮重置过滤集首项） |
| 输入期无匹配 | 面板关闭（不弹空窗口） |
| 显式唤起（点 ▾ / 字段聚焦 + Down/Alt+Down） | 空过滤回退**全量选项**——过滤只在输入期生效（自由值也能看到全部） |

## 3. 值语义

- 选中：bind 写 option value + `onSelected(value)` 回调（菜单 onCommand，与键盘/点击同路径）。
- 自由值：文本不在选项内**保留**（不强制匹配）；选择后写选项 value。
- TextField 键盘（选区/词移/IME）与 G-1 命令分发不受影响（`handleKey` 只在打开时消费菜单键，未打开时仅 Down/Alt+Down 且字段聚焦才消费）。

## 4. 装配与应用接线

```cpp
widgets::ComboBoxController combo({{"apple","Apple"},…}, "bind", "combo");
combo.attach(shell);                    // toggle handler + bind 观察 + 回调
// build: combo.build(theme)
// onKey: combo.handleKey(shell, key, mods, ch)  （打开时 modal 优先）
combo.onSelected = …;
```

## 5. 验证

build/attach 结构（field bind、toggle onClick）、锚定展开与全量/过滤集、选择写回（Enter 与 Down+Enter 两态）、打开期过滤（大小写/无匹配关闭）、菜单内建导航与 Escape、自由值保留、未打开时不消费无关键。桌面 smoke（人工）：面板边界钳制与翻转、长列表滚动——与 menu-controls-design §7 同契约，不重复验收。
