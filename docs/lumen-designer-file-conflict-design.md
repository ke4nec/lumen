# Designer 文件冲突恢复

本规范落实 [`lumen-designer-prerequisites.md`](lumen-designer-prerequisites.md) §4.14 第 5 条。
文档/工程在加载后被外部修改时，普通保存必须拒绝；保留本地 DOM、选择、dirty 和历史，
在诊断面板显示明确的恢复动作。视觉基线沿用 [`lumen-visual-system-design.md`](lumen-visual-system-design.md)
的 surfaceSunken、caption/label、按钮 Danger 变体、focus ring 和 Metrics 密度值；
对照稿为 [`designer-file-conflict.html`](../design/designer-file-conflict.html)。

## 1. 恢复契约

- **Reload external file**：重新读取同一路径，成功后替换本地文档并重置历史；提示会丢弃
  本地修改。读取/编译失败继续保留旧 DOM/画布和冲突恢复入口。
- **Save as**：沿用原生文件对话框和既有另存流程。取消/失败不清除冲突；保存成功才更新
  文件路径与 saved revision。再次选择当前路径仍执行普通 revision 检测。
- **Overwrite external file**：用户主动选择覆盖，保存当前本地声明。点击时使用冲突出现
  时观察到的内容 revision；该文件再次外部修改时拒绝写入并展示新的冲突，不能无限制
  force-save。工程同时冻结 manifest 和所有页面的外部 revision，并在写任一页面前全部
  预检；路径仍须留在授权工程根内。磁盘内容无法读取时覆盖禁用，可另存或修复后重载。

成功加载另一文档、切换工程页或成功保存会清除过期冲突入口。覆盖不重放业务回调；
仍由 DocumentStore/ProjectStore 验证声明、保留合法备份并原子替换单个文件。
工程多文件保存沿用现有逐文件原子替换，不宣称跨文件掉电原子性。

## 2. 展示、键盘和语义

冲突恢复入口位于诊断面板中，正文明确说明 Reload 丢弃本地修改、Overwrite 替换外部
修改。按钮依次为 Reload、Save as、Overwrite；覆盖使用已有 Danger 变体，信息不只依赖
颜色。全部按钮参与 Tab 导航、显示 Theme focus ring，并支持语义 Activate；按钮和
CommandRegistry 共享动作，普通 Ctrl/Cmd+S 始终执行受 revision 保护的保存。
工程冲突时按钮明确写作 Reload/Overwrite external project，正文说明覆盖 manifest
及所有页面，避免把工程级替换表现为只替换当前页。
按钮使用现有 Grid 随窗口宽度和字体档位排列为 1–3 列，列宽由 Theme textFieldMinWidth
派生；诊断面板只在冲突态增加按 Theme Metrics、动作行数与字体大小计算的高度。

## 3. 验收

headless 覆盖外部修改后原文件/DOM/dirty/选择不变、三动作存在且语义可激活、重载失败
保留画布、另存取消/成功、明确覆盖的备份/保存状态、观察后再次外部修改拒绝，以及工程
后页或 manifest 再次修改时不先写前页。真实三桌面的键盘、读屏及原生对话框结果仍归
platform-acceptance 的 `designer_edit_save_reopen` / `designer_edit_save` 项。
