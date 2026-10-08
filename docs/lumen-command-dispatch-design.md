# Lumen 命令分发层设计（G-1）

> 状态：已实现（2026-09-29；gap-backlog G-1，P0）。
> 上游：[`lumen-gap-backlog.md`](lumen-gap-backlog.md) §3 G-1；登记动机——菜单快捷键串仅展示、应用只能 `RunOptions.onKey` 手写 switch、每个应用重复实现焦点域/模态/覆盖层判定。
> 代码：[`include/lumen/app/command_registry.h`](../include/lumen/app/command_registry.h)、`src/app/command_registry.cpp`、`AppShell::keyDown/dispatchCommand`、`MenuItem::command`（[`lumen/widgets/menu.h`](../include/lumen/widgets/menu.h)）。
> 测试：`tests/command_registry_tests.cpp`（headless，全部经 AppShell 直驱）。

## 1. 模型

命令是应用声名的值（`CommandSpec`），注册进 `AppShell::commands()`：

- `id`：稳定标识（菜单/工具栏/语义引用同一 id）。
- `binding`（`KeyBinding`）：`letter`（可打印键和弦，Ctrl+S/纯字母）或 `key`（功能键，Esc/Enter/方向键）+ 修饰键集；两者互斥，都空 = 无绑定（仅 `invokeCommand` 显式触发）。匹配输入是归一化事件（`core::Key/KeyModifiers/keyChar`），平台无关；字母大小写不敏感。
- `scope` + `domain`：生效域（§3）。
- `invoke(AppShell&)`：动作体（UI 线程）。
- `enabled()`：动态启用查询（空 = 恒启用）；禁用命令不分发、菜单行派生禁用。

`bindingLabel(KeyBinding)` 从绑定派生展示串（"Ctrl+S"/"Ctrl+Shift+Z"/"Esc"/"F10"/"Alt"；修饰键顺序 Ctrl/Alt/Shift/Cmd，Gui 显示为 Cmd）。**菜单快捷键列的唯一数据源**——应用不再手写显示串，杜绝显示与行为分叉。

## 2. 分发顺序（AppShell::keyDown）

```
1. config.onKey            应用级拦截（Escape/返回统一规则等，既有语义不变）
2. 命令相位 A（仅和弦）      Ctrl/Alt/Gui 绑定；字段保护见 §4
3. InteractionController    Tab 遍历 / Enter·Space 激活 / 滚动键 / 字段编辑
   （未消费时）
4. 命令相位 B（仅纯键）      无修饰功能键（Esc/Enter/方向键/纯字母）
```

- 和弦先于交互层：**输入中保存**（字段聚焦时 Ctrl+S）是桌面工具的基本预期。
- 纯键后于交互层：Tab 遍历、滚动、字段编辑、按钮激活优先；`InteractionController::keyDown` 返回消费回执（本设计引入），未消费才落命令。
- 与语义同路径（M5 契约）：菜单/工具栏/语义激活经 `AppShell::invokeCommand(id)` → `CommandRegistry::invoke`，与键盘命中同一 `CommandSpec::invoke`。

## 3. 生效域与模态屏蔽

| 域 | 生效条件 | 优先级 |
| --- | --- | --- |
| `Modal` | 模态打开时（唯一可分发域） | 模态期独占 |
| `FocusDomain` | 焦点位于 `domain`（FocusScope key/identity，任意祖先域）内 | 高 |
| `Window` | 无模态时窗口级兜底 | 低 |

- 模态 = 应用声明（`AppShell::setModalActive`，makeDialog 类主树模态由应用知会）∨ 框架模态 overlay（菜单/下拉，`setOverlay/setOverlayBuilder`）；非模态视觉 overlay（M15 拖拽 ghost）不屏蔽。
- 匹配在命中域内按注册序取首个绑定命中；**域内禁用命中不跨域回退**（可预期：存在但禁用）。
- 系统级热键（RegisterHotKey/XGrabKey）预留为同一命令模型的第二层（M16）：命中后回灌 `invokeCommand`，不另建分发模型。

## 4. 字段保护（内建编辑和弦）

字段聚焦（`wantsTextInput`）时，Ctrl/Gui + Z/Y/A/C/X/V（±Shift，无 Alt）归文本编辑路径，命令不分发；其余和弦照常分发。普通无修饰编辑键让位给字段；`F10`/`Alt` 不属于文本编辑，字段返回未消费并保持焦点、选区与 IME preedit，允许命令兜底相位继续分发。Alt+字母不受保护（菜单 mnemonic 路径）。

## 5. 菜单集成（MenuItem::command）

`MenuItem.command` 非空时，控制器在唤起/级联展开时解析（`resolveCommandItems`）：

- 快捷键列 = `bindingLabel(绑定)`（覆盖应用手写串；无绑定时保留应用串）。
- 启用态 = 应用声明 ∧ 注册表 `enabled`。
- 激活优先走注册表 `invoke`（键盘同路径）；**未注册 id 不改写显示/启用，激活回退 `onCommand(id)`**（渐进迁移：命令可后注册）。

settings 示例已迁移文件/视图/帮助菜单（`.command` + 注册表），Ctrl+N/O/S/B// 与 save-as 禁用态真实生效。

## 6. 冲突与诊断

同 scope+domain 重复绑定：**首注册者胜**，冲突进 `CommandRegistry::conflicts()`（确定性记录：绑定串/双方 id/域），测试与运行期体检可查询；不新增散乱输出通道（M14-C 纪律）。

## 7. 平台事件修复（随本项交付）

SDL 宿主 `mapKeyChar` 原仅在无修饰键时上报字符——桌面路径 Ctrl+Z/X/C/V 编辑和弦、集合 Ctrl+A 全选与菜单 Alt mnemonic 实际失效（仅 headless 覆盖）。现 ASCII 键码恒上报；消费方排除和弦空格激活（interaction 树键处理与菜单行空格激活均已排除）。

## 8. 验证要点（headless 全覆盖）

域优先级与回退、模态屏蔽（应用声明/框架 overlay）、键冲突仲裁与记录、禁用态（键盘 + 菜单派生）、字段保护（内建和弦 vs Ctrl+S）、纯键回退（Enter 激活按钮优先于默认命令、Escape 字段消费优先）、菜单显示串派生与激活同路径、注册守卫（空 id/空回调/同 id 覆盖）。性能：注册表空时 `keyDown` 两相位各一次 `empty()` 短路，零额外分配；frame hash 不受影响（全量回归 873/873 通过）。
