# 设计器前置任务阶段出口复核

> 核对日期：2026-10-12。最新 Windows 现场摘要检查器提交：`8bd12c25cd1d6ce3fc69493fcb7bca4c28b02d1e`；本批原生保存重开与一小时浸泡构建身份归属 `17b4232c206b5e1ac1c74769a58d8087b8e98b7f`，早期结果另按各提交保留。
> 本记录按 [`前置任务`](lumen-designer-prerequisites.md) §6、§10.2 核对出口。
> F0–F6 的实现与 headless 证据已有；D1/D2/D3 的完整桌面现场出口尚未闭环。

## 1. 当前可追溯证据

- 默认配置完整 CTest 1170/1170、启用 allocator 的 Release 完整 CTest 1177/1177，
  原生无障碍桥配置 1171/1171；
  两配置各注册 186 个 Designer/文档存储/工程存储用例。默认配置构建类型为空，不能称为
  Release 性能输入。
- [Windows CI](https://github.com/ke4nec/lumen/actions/runs/38015411485) 七个 job 全成功；
  [Linux CI](https://github.com/ke4nec/lumen/actions/runs/38015411609) 与
  [macOS CI](https://github.com/ke4nec/lumen/actions/runs/38015411570) 全成功。
  三平台各三个 Designer fixture smoke 成功，Linux Designer 相对性能门禁明确成功。
  完整提交归属、job/关键步骤结果见
  [`CI 原始摘要`](platform-evidence/designer-ci-2026-10-10.json)。历史 mobile-core job
  不扩大桌面范围，也不是移动端验收。
  无障碍大纲修复后的 [Windows](https://github.com/ke4nec/lumen/actions/runs/38019639083)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38019639226) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38019639093) 也全部通过；
  [该批完整摘要](platform-evidence/designer-ci-2026-10-10-dc57970.json) 保留 job 与关键步骤。
- 本机 GNOME/Wayland 的 Release Designer 三帧窗口 smoke 与原生 allocator 短探针退出 0；
  后者为 3.024s、两个窗口、102 次呈现、101 个有效 allocator 帧，来源为 `glibc/malloc`，
  `state_preserved=true`。日志、产物/fixture SHA256、构建开关和限制见
  [`原生单项证据`](platform-evidence/designer-wayland-2026-10-10.json)。
  这次构建的原生无障碍桥、Skia 和 GPU 均关闭。
- `ab43fec` 的同一 Wayland Release 探针已完成一小时压力回环：3600.03 秒、两个窗口、
  304763 帧、79934 次 resize、64 MiB 压力、两次模拟恢复，`state_preserved=true`；
  [结构化报告](platform-evidence/designer-wayland-soak-2026-10-10.json) 明确保留了
  探针提交和限制。它是原生浸泡单项证据，不是完整人工平台记录。
- `dc57970` 的原生 AT-SPI 协议复查已在当前提交内容上完成：大纲激活后目标行
  `selected=true`，属性 `text` 字段存在且获得 `FOCUSED`；[记录](platform-evidence/designer-atspi-2026-10-10.json)
  明确标注没有启动 Orca，因此不宣称读屏播报通过。
- `f021fe8` 的 Windows UIA Designer 专用回环已在三平台 CI 通过：真实 HWND 上查找
  `Text  [title]` TreeItem 并 Invoke，随后通过 Edit/ValuePattern 读写 `text` 属性；
  Windows `a11y-bridge` 的 live 步骤成功。完整运行与 job 归属见
  [`UIA Designer CI 证据`](platform-evidence/designer-ci-2026-10-10-f021fe8.json)。
- Linux Designer AT-SPI 专用回归已补齐最小文本值接口，入口为
  `tests/designer_atspi_live_tests.py` 与 `lumen-designer-atspi-live-app`。生产 D-Bus
  provider 上覆盖大纲选择、Unicode 属性读写/事件、空串/非法范围、非字段与禁用字段
  拒写；应用端验证声明事务、undo/redo、dirty、保存重开与稳定 ID。Linux CPU CI
  增加显式执行步骤；fixture 不创建 SDL 窗口，不据此声明 Orca 或现场验收完成。
  本机桥开启的 Debug 完整 CTest 1171/1171（118.82s）、通用和 Designer 原生协议
  回归均通过；旧 `1840bbe` provider 临时重链接同一 fixture，明确因缺少 Text 接口失败。
- `afc2134` 的三平台 CI 已全部通过：
  [Windows](https://github.com/ke4nec/lumen/actions/runs/38034652598)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38034652593) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38034652615)。Linux CPU
  明确通过通用与 Designer AT-SPI 两个 live 步骤，Windows `a11y-bridge` 通过
  UIA Designer live 步骤；完整摘要见
  [`afc2134 CI 原始摘要`](platform-evidence/designer-ci-2026-10-10-afc2134.json)。
- `70b8944` 修正 Windows live UIA Unicode 回环输入后，三平台 CI 再次全部通过：
  [Windows](https://github.com/ke4nec/lumen/actions/runs/38065376351) 的
  `a11y-bridge` live UIA Designer 步骤、[Linux](https://github.com/ke4nec/lumen/actions/runs/38065376373)
  的两个 AT-SPI live 步骤和 [macOS](https://github.com/ke4nec/lumen/actions/runs/38065376361)
  均成功。完整提交归属见
  [`70b8944 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-70b8944.json)。
- `8684d9a` 进一步修正 Windows UIA `ValuePattern.SetValue`：TextField 自动先聚焦，
  事务未处理时返回 UIA 错误码；[Windows](https://github.com/ke4nec/lumen/actions/runs/38070079400)
  的 live UIA Designer 步骤、[Linux](https://github.com/ke4nec/lumen/actions/runs/38070079318)
  和 [macOS](https://github.com/ke4nec/lumen/actions/runs/38070079310) 均成功。完整提交归属见
  [`8684d9a CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-8684d9a.json)。
- `be32bbf` 将 Windows UIA `Invoke/Toggle`、`SetFocus` 的禁用、未处理和节点失效结果
  统一映射为 UIA HRESULT；首轮 Windows 回归暴露旧的重入测试仍期望已删除节点返回 `S_OK`。
  `32090be` 已改为断言 `UIA_E_ELEMENTNOTAVAILABLE`，并在三平台重新通过：
  [Windows](https://github.com/ke4nec/lumen/actions/runs/38075459327)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38075459364)、
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38075459330)。完整提交、旧失败归因和本地
  验证见 [`32090be CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-32090be.json)。
- `ec96c8b` 将 macOS NSAccessibility 的 TextField `AXValue` 写入统一为
  Focus → SetValue 事务；焦点被拒绝时不提交值，数值控件保持直接 SetValue 路径。
  macOS provider 回归覆盖成功顺序和失败短路；[Windows](https://github.com/ke4nec/lumen/actions/runs/38079559702)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38079559708) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38079559793) CI 全部通过，完整归属见
  [`ec96c8b CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-ec96c8b.json)。
- `3e3d413` 将 Windows UIA 与 macOS VoiceOver 平台回环脚本扩展到 TextField/Edit
  的文本写入和回读，使读屏协议检查覆盖 Designer 属性事务；[Windows](https://github.com/ke4nec/lumen/actions/runs/38082885424)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38082885410) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38082885433) CI 全部通过，完整归属见
  [`3e3d413 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-3e3d413.json)。
- `bea6fdb` 将 Designer 离线 `RuntimeContext` 的 `ThemeScope` 引用接入稳定的
  `light`/`dark` typed handle，并让 ThemeScope 节点在属性面板暴露可编辑的 `theme`
  引用字段；应用回归覆盖成功预览、`themeOverride` 生命周期和未知主题的
  `reference.missing` 诊断。[Windows](https://github.com/ke4nec/lumen/actions/runs/38086809224)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38086809031) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38086809299) CI 全部通过，完整归属见
  [`bea6fdb CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-bea6fdb.json)。
- `29124e0` 的 review 补齐了 ThemeScope 的 `light`→`dark` 替换回归：测试验证文档引用、
  `themeOverride` 的 session 替换、主题色实际切换和未知主题诊断。[Windows](https://github.com/ke4nec/lumen/actions/runs/38090140775)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38090140766) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38090140799) CI 全部通过，完整归属见
  [`29124e0 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-29124e0.json)。
- `5252506` 的 review 继续验证 ThemeScope 引用事务：`light`→`dark` 替换后的 undo/redo
  同步恢复并重做文档引用、诊断和预览实际主题色；本机 Debug 完整 CTest 1171/1171、
  设计器应用筛选 51 用例/2979 断言、ThemeScope 定向用例 1/43、平台验收脚本 17/17 均通过。
  [Windows](https://github.com/ke4nec/lumen/actions/runs/38093296872)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38093296901) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38093296871) CI 全部通过，完整归属见
  [`5252506 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-5252506.json)。
- `a4650f4` 首次将 ThemeScope 引用接入 Windows 原生 UIA 回环，但 fixture 复用了已由解析
  Text 节点占用的 `DocumentId 2`；因此 [Windows](https://github.com/ke4nec/lumen/actions/runs/38096258305)
  的 `a11y-bridge` 失败（6/7），而 [Linux](https://github.com/ke4nec/lumen/actions/runs/38096258247)
  和 [macOS](https://github.com/ke4nec/lumen/actions/runs/38096258291) 通过。该首次失败记录见
  [`a4650f4 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-a4650f4.json)。
- `54badb4` 为 ThemeScope 使用唯一 `DocumentId 3`，修复 Windows 门禁并完成真实 UIA
  TreeItem/Edit ValuePattern 的 `light`→`dark` 写入、undo/redo、选择和保存重开回环。
  [Windows](https://github.com/ke4nec/lumen/actions/runs/38097917657)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38097917645) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38097917678) CI 全部通过；本机
  Debug 完整 CTest 1171/1171、Designer/无障碍筛选 174/174、平台验收脚本 17/17 均通过，
  完整归属见 [`54badb4 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-54badb4.json)。
- `3424331` 将 ThemeScope 引用事务接入 Linux 原生 AT-SPI Designer 回环：真实激活
  `ThemeScope  [theme-scope]`，通过 EditableText 写入 `light`→`dark`，并验证选择、
  文本编辑、主题引用 undo/redo、保存重开和 DocumentId 保持；本机原生回环通过，启用
  AT-SPI 的 Debug CTest 1172/1172、`[a11y]` 26 个用例/462 个断言、平台验收脚本 17/17
  均通过。[Windows](https://github.com/ke4nec/lumen/actions/runs/38102493031)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38102493037) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38102493035) CI 全部通过，完整归属见
  [`3424331 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-3424331.json)。
- `1677c57` 将 ThemeScope 引用事务接入 macOS 原生 NSAccessibility Designer 回环：真实
  AX Row 激活 `ThemeScope  [theme-scope]`，通过 AXValue 写入 `light`→`dark`，并验证
  Focus→SetValue、选择、dirty、undo/redo 和保存重开。[Windows](https://github.com/ke4nec/lumen/actions/runs/38105806495)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38105806450) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38105806458) CI 全部通过，Linux 本机
  Debug CTest 1171/1171 通过，完整归属见
  [`1677c57 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-1677c57.json)。
- `f0cdd34` 将生产 `lumen-designer --acceptance` 接入四个真实桌面 workflow 的 Designer
  日志：通过语义 Focus/SetValue 完成选择、属性编辑、undo/redo、保存和重开，再执行双窗口
  原生 smoke；日志 marker 与附件哈希均由验收检查器校验。新增 acceptance CTest 和包内
  smoke；本机完整 Debug CTest 1172/1172、平台验收检查器 17/17 通过。[Windows](https://github.com/ke4nec/lumen/actions/runs/38109953434)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38109953528) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38109953443) CI 全部通过，完整归属见
  [`f0cdd34 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-f0cdd34.json)。
- `30b2d90` 将生产回环扩展到 ThemeScope 命名引用：通过语义 Focus/SetValue 写入
  `light`/`dark`，验证实际预览主题、引用 undo/redo、保存重开和稳定节点 ID；日志强制唯一
  `theme_reference pass` 与 `edit_save_reopen pass` marker。本机完整 Debug CTest 1172/1172、
  平台验收检查器 17/17 通过。[Windows](https://github.com/ke4nec/lumen/actions/runs/38112946355)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38112946315) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38112946344) CI 全部通过，完整归属见
  [`30b2d90 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-30b2d90.json)。
- `ac910bd` 将生产回环补齐到 DPI/主题环境隔离：自动 resize 1024×640/1600×900，
  切换 DPI、字体缩放、高对比度并逐步核对四个 Designer 面板的渲染/语义 bounds，确认
  文档 revision 与 dirty 保持不变后恢复默认环境；日志新增唯一 `dpi_theme pass` marker，
  与 ThemeScope、编辑保存回环一起由检查器强制要求。本机完整 Debug CTest 1172/1172、
  平台验收检查器 17/17 通过。[Windows](https://github.com/ke4nec/lumen/actions/runs/38117565094)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38117565061) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38117565110) CI 全部通过，完整归属见
  [`ac910bd CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-ac910bd.json)。真实显示器
  DPI/resize、画布命中和读屏人工验收仍按 §3 保持 pending。
- `f96943b` 将生产回环补齐到结构编辑：真实命令路径插入 Text、复制、粘贴、上移、下移、
  删除，核对选区、稳定 ID、revision/dirty、节点计数和诊断，再保存重开确认新增节点保持、
  删除节点消失、DocumentId 不变；窗口日志新增唯一 `structure_edit pass` marker，并与
  其他四个 Designer marker 一起校验。本机完整 Debug CTest 1172/1172、平台验收检查器
  17/17 通过。[Windows](https://github.com/ke4nec/lumen/actions/runs/38121299685)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38121299683) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38121299697) CI 全部通过，完整归属见
  [`f96943b CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-f96943b.json)。真实键盘/读屏、
  拖拽重排、IME、跨应用剪贴板和人工 Designer 现场仍按 §3 保持 pending。
- `80ca81c` 修复结构编辑 UIA 回环的保存回读断言：结构命令先经 UIA `SetFocus` 离开
  属性编辑域，删除操作验证 UIA 路径的 undo/redo，保存重开按稳定 `DesignNodeId` 查找
  插入节点和 ThemeScope，避免节点增删后依赖子节点位置。本机启用无障碍 Debug CTest
  1173/1173、`[a11y]` 26 个用例/462 个断言、设计器 acceptance 1/1、平台验收检查器
  17/17 通过。[Windows](https://github.com/ke4nec/lumen/actions/runs/38128294059)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38128294025) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38128294020) CI 全部通过，完整归属见
  [`80ca81c CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-80ca81c.json)。该修复
  仍不替代真实键盘/读屏、拖拽重排、IME、跨应用剪贴板和人工 Designer 现场。
- `72735f4` 补齐 Windows 现场读屏协议入口：新增 `uia_designer_reader_loop.py`，在临时
  `.design` 副本上通过真实 UIA TreeItem/Edit/Button 完成 Designer 选择、Unicode 属性编辑、
  插入/复制/上移/下移/删除、保存和重开回读；Windows platform-acceptance job 与通用
  `uia_reader_loop.py` 一起执行并归档日志。脚本 AST、YAML 和平台验收检查器 17/17 通过；
  [Windows](https://github.com/ke4nec/lumen/actions/runs/38133455731)、
  [Linux](https://github.com/ke4nec/lumen/actions/runs/38133455724) 和
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38133455736) 常规 CI 全部通过，完整归属见
  [`72735f4 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-72735f4.json)。UIA 协议和进程
  探活仍不替代 Narrator/NVDA 语音人工记录及完整桌面出口。
- 后续 review 收紧了 Windows 证据绑定：`check_platform_acceptance.py` 要求 Windows
  `record.json` 同时登记本次 workflow 的 `uia-reader-live.log` 与 `uia-designer-live.log`，
  并校验两份日志各自唯一的 UIA JSON 协议摘要及完整协议集合；Designer 摘要还必须包含
  `reference.missing -> Source -> ThemeScope` 的诊断定位结果。缺少 Designer 专用日志或
  使用其他运行的日志都会被拒绝。该规则只强化证据归属，不把协议回环、进程探活或自动
  摘要当作 Narrator/NVDA 语音通过。
- `43be45f` 的 Windows、Linux、macOS 常规 CI 全部通过（Windows 7/7、Linux 7/7、macOS
  6/6）；平台验收检查器与对应 CTest 均为 17/17，通过摘要见
  [`43be45f CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-43be45f.json)。

- `d18e6c8` 修复 Windows Designer UIA 门禁的现场断言：provider 继续暴露
  `UIA_SelectionItemIsSelectedPropertyId`，真实 Designer UIA 回环继续验证大纲/ThemeScope 选中和
  `reference.missing -> Source -> ThemeScope` 诊断定位；移除 C++ UIA 代理层不稳定的重复断言。
  本机平台验收检查器 17/17、无障碍筛选 25 个用例/459 个断言通过；[Windows](https://github.com/ke4nec/lumen/actions/runs/38147655106)
  7/7、[Linux](https://github.com/ke4nec/lumen/actions/runs/38147655109) 7/7、
  [macOS](https://github.com/ke4nec/lumen/actions/runs/38147655108) 6/6 CI 全部通过，
  完整归属见 [`d18e6c8 CI 原始摘要`](platform-evidence/designer-ci-2026-10-11-d18e6c8.json)。

- 本轮 review 让 `tests/designer_atspi_live_tests.py` 在既有稳定回环后输出单行结构化摘要，
  明确 `linux`/`atspi`、`designer_select_locate`、`designer_edit_save` 和
  `ThemeScope light -> dark`；这只增强 hosted/headless 证据的可绑定性，不把 Orca 语音、
  登录桌面或完整现场 `record.json` 标为完成。

- `0748688` 将 Linux AT-SPI Designer fixture 的 Text 与 ThemeScope 激活改为调用首个已声明
  action，绕开弃用的 action 名称查询；选中、Unicode 文本、ThemeScope `light -> dark`、
  历史与保存重开继续通过，并输出结构化 `designer_select_locate`/`designer_edit_save`
  摘要。Windows 7/7、Linux 7/7、macOS 6/6 hosted CI 全部通过，Windows `a11y-bridge`
  通过；原始摘要见 [`0748688 CI 证据`](platform-evidence/designer-ci-2026-10-12-0748688.json)。
  该 fixture 仍不提供 Orca 语音或登录桌面证据，完整 `record.json`、IME、Inspector、
  DPI/主题、拖拽和 soak 继续 pending。

- 2026-10-12 阶段复查在当前源码上重新执行 Debug 全量 CTest `1172/1172`、平台验收检查器
  `17/17` 和 Linux AT-SPI fixture，均通过。隔离 X11 生产 Designer/Preview 窗口已注册并
  暴露大纲树，但真实激活触发语义树重建时，外部 AT-SPI 遍历仍可能读到瞬时对象；因此该
  探针保持观察记录，未升级为自动通过。复查证据见
  [`36d72f5 阶段复查`](platform-evidence/designer-review-2026-10-12-36d72f5.json)，完整
  读屏、IME、Inspector、DPI/主题、拖拽和 soak 仍 pending。

- `f8ac4a8` 将 Linux X11 生产窗口 AT-SPI 探针收敛为可重复门禁：新增最小顶层
  `tests/fixtures/designer_atspi_window.design`，`tests/designer_atspi_window_live_tests.py`
  直接通过 AT-SPI D-Bus `Accessible`、`Action`、`Text` 和 `EditableText` 接口启动真实
  `lumen-designer`，完成 Text 选择、Unicode 编辑、ThemeScope `light -> dark`、Save 和
  JSON 保存回读。Linux workflow 增加 `python3-dbus` 依赖并执行窗口回环；Windows 7/7
  （含 `a11y-bridge`）、Linux 7/7、macOS 6/6 hosted CI 全部通过，证据见
  [`f8ac4a8 CI 原始摘要`](platform-evidence/designer-ci-2026-10-12-f8ac4a8.json)。该协议
  回环仍不等价于 Orca 语音或登录桌面人工验收，真实 IME、Inspector、DPI/主题、拖拽、
  干净机器包和一小时 soak 继续 pending。

- 下一步现场接线已落在 `.github/workflows/platform-acceptance.yml`：Linux X11 与 Wayland
  job 在 Designer 原生窗口 smoke 后执行同一个 `designer_atspi_window_live_tests.py`，
  将结构化协议摘要保存为 `atspi-designer-live.log` 并上传；检查器要求 Linux
  `record.json` 登记该附件并按 SHA256 绑定当前 workflow 日志，严格验证 X11/Wayland
  平台字段和唯一协议摘要。该步骤要求真实会话中的 `python3-dbus`、AT-SPI bus 和生产
  窗口，属于协议证据绑定；Orca 播报、IME、Inspector、DPI/主题、拖拽和 soak 仍需现场
  记录，未因工作流接线而标记为通过。

- macOS 现场接线同步新增 `voiceover_designer_reader_loop.py`：生产 Designer AXAPI
  回环输出 `ax-designer-live.log`，检查器要求 macOS `record.json` 登记该附件并验证
  唯一的 `nsaccessibility` 选择/编辑/保存摘要。`voiceover_alive` 只作进程探活；
  VoiceOver 播报、IME、Inspector、DPI/主题、拖拽和 soak 仍需人工现场记录。

- Windows Designer UIA 回环的现场稳定性复查已完成：`uia_designer_reader_loop.py` 对语义树
  重建期间的瞬时 COM/UIA 失效统一重试，属性写入、结构命令、重开读回及诊断
  `Source`/`ThemeScope` 选中态都从当前树重新查询；超时会保留底层异常。平台验收单测
  `21/21` 通过。该修复只降低现场回环的瞬态失败率，不改变 Narrator/NVDA 语音、真实
  IME、Inspector、DPI/主题、拖拽、soak 和完整 `record.json` 的 pending 状态；Windows、
  Linux、macOS hosted CI 运行归属见 [`25bec09 CI 证据`](platform-evidence/designer-ci-2026-10-12-25bec09.json)。

- 通用 Windows UIA 回环随后同步采用瞬态重试：路由切换、焦点、滑块和文本值步骤都从当前
  UIA 树重新查询元素，减少通用读屏步骤在 Designer 专用回环前的偶发阻断；平台验收单测
  更新为 `22/22`。这仍只加强协议回环稳定性，不把 UIA 回执升级为 Narrator/NVDA 语音或
  完整桌面验收。
  本轮三平台 hosted CI 均通过，运行归属见 [`2343f5d CI 证据`](platform-evidence/designer-ci-2026-10-12-2343f5d.json)。

- Linux Orca 与 macOS VoiceOver 的通用协议回环也已收紧瞬态处理：AT-SPI/AXAPI 树更新后
  重新查找当前焦点、路由、滑块和文本节点，并把底层对象错误保留到超时诊断；三条读屏脚本
  语法检查通过，平台验收单测保持 `22/22`。这不改变 Orca/VoiceOver 语音、真实输入法或
  完整桌面 `record.json` 的 pending 状态。
  三平台 hosted CI 归属见 [`06771f2 CI 证据`](platform-evidence/designer-ci-2026-10-12-06771f2.json)。

- Designer 专用回环继续收紧：macOS AXAPI 和 Linux AT-SPI D-Bus 的 TreeItem 激活、属性
  读写及 Save 动作都会在语义树重建后重新获取当前节点。Linux X11 生产窗口回环已实际
  通过 Text 选择、Unicode 编辑、ThemeScope `light -> dark` 和保存回读；macOS 仅完成
  语法验证，真实 AXAPI 运行仍待登录 Aqua 会话。

- 8f67961 的 Designer 原生回环修复已完成三平台 hosted CI：Windows `7/7`（含 `a11y-bridge`）、Linux `7/7`、macOS `6/6` 均成功。Linux X11 生产 AT-SPI D-Bus 回环通过 Text 选择、Unicode 编辑、ThemeScope `light -> dark`、保存和 JSON 回读；macOS AXAPI 仍需登录 Aqua/辅助功能权限。平台验收检查器为 `22/22`，真实读屏语音、IME、Inspector、DPI/主题、拖拽、干净机器包、跨应用剪贴板、soak 和完整 `record.json` 继续 pending。证据见 [`8f67961 CI 证据`](platform-evidence/designer-ci-2026-10-12-8f67961.json)。

- `e2f3e91` 的初始 AT-SPI 字段等待修复三平台 CI 全部通过，归属见
  [`e2f3e91 CI 证据`](platform-evidence/designer-ci-2026-10-12-e2f3e91.json)。
  保存/重开出口复查继续补齐 Linux/macOS 的专用原生窗口回环：保存落盘后关闭进程、
  重开同一文档，在新窗口选择 Text/ThemeScope 并读回保存值；检查器必须看到布尔
  `reopened=true`。Linux X11/Xvfb 和本机 GNOME Wayland 生产回环均本地通过，新摘要通过
  协议检查器；Debug 完整 CTest `1172/1172`（116.97s）。macOS 命名 slot JSON 回读错误有旧版
  失败复现与新回归，平台验收单测为 `23/23`。macOS 原生 AXAPI 仍待登录 Aqua/权限环境，
  实际读屏和完整人工平台记录仍为 pending。

- `17b4232` 的三平台 hosted CI 全部成功（Windows `7/7`、Linux `7/7`、macOS `6/6`），
  X11/Wayland 原生保存重开日志、构建身份与 CI job 结果见
  [`本批证据`](platform-evidence/designer-ci-2026-10-12-17b4232.json)。
  Orca 补充探测未取得 Designer 属性播报；原生浸泡第一次运行 1486.200 秒后退出 1，
  未达到一小时门槛，第二次采样随后以 3600.020 秒、退出码 0 完成；该单项结果已归档，
  但不替代完整平台 record。Orca 补充探测仍未取得 Designer 属性播报，这两项分别按
  原生单项证据和读屏现场缺口登记。

- `8bd12c2` 的 Windows `7/7`、Linux `7/7`、macOS `6/6` hosted CI 全部成功；
  完整本地 CTest `1172/1172`（137.19s）、平台验收检查器 `24/24` 通过。Windows
  Designer 专用现场摘要也要求布尔 `reopened=true`，缺失、false、字符串和数值均拒绝。
  hosted UIA job 运行既有 C++ 窗口回环，Python 现场读屏脚本仍需登录会话执行。
  [归属摘要](platform-evidence/designer-ci-2026-10-12-8bd12c2.json) 保留 CI、浸泡和限制。

- 后续 AT-SPI 状态 review 复现启用节点缺少 SENSITIVE，并观察到 Orca 将可用窗口和
  Save 播报为 grayed。修复后根、大纲、可编辑字段同时具备 ENABLED/SENSITIVE，禁用
  字段同时缺少两位；旧协议断言失败，修复回环通过。完整桥开启 Debug CTest
  `1173/1173`（117.97s）、验收检查器 `24/24` 通过。
  Orca 生成了 Text、text、ThemeScope、Save 等标签/角色请求，修复后没有 grayed；
  [前后日志与脚本](platform-evidence/designer-atspi-sensitive-2026-10-12.json) 保留身份与限制。
  这只补充播报请求诊断，未录音或取得完整人工 record，所有 reader 现场出口继续 pending。

- `2244605` 的原生 AT-SPI review 补齐稳定节点的 enabled/sensitive/focusable 状态通知。
  旧 provider 在禁用时缺少 enabled 事件导致真实 libatspi 监听超时；修复后同一节点
  禁用/重新启用两向收到 detail1=0/1，客户端状态回读一致。Designer Unicode、ThemeScope、
  历史与保存重开保持通过；完整桥开启 Debug CTest `1173/1173`（119.43s）、验收检查器
  `24/24` 通过。[原始协议证据](platform-evidence/designer-atspi-state-events-2026-10-12.json)
  按源码和二进制哈希归属；没有启动 Orca，完整现场出口继续 pending。

## 2. 阶段与 fixture 对照

以下“headless 通过”只归属上述源码和自动化验证范围；涉及真实输入、合成器、读屏或
人工操作的出口仍按 §3 登记。测试名称与源文件中的行为断言及 CTest 注册核对，
文件存在或绿色 workflow 本身不能替代这些行为断言。

| 阶段 | 必须证明的出口 | 已核对实现与断言入口 | 当前结论 |
| --- | --- | --- | --- |
| F0 | 保持 DSL、Element identity 与热重载边界；不扩大移动/可编程 DSL 范围 | `src/dsl/CMakeLists.txt` 仍仅链接 `lumen-core`；`dsl_tests.cpp`、`element_tests.cpp`、`app_shell_tests.cpp` 与完整 CTest | headless 通过；边界沿用 DP-1–DP-9 |
| F1/P1 | L0 12 节点的语义 DOM/codec 往返、独立 builder golden、损坏输入诊断 | `design_document.h`、`design_codec.h`、`text_dsl.cpp`；`designer_document_tests.cpp` | headless 通过；规范化输出，不承诺注释/空白保真 |
| F2/P2 | 属性访问器/默认值/非法值、持久化分类、字段登记守护 | `design_schema.h/.cpp`；`designer_schema_tests.cpp` 的逐 schema/属性循环、74 成员聚合守护 | headless 通过；37 个 schema，组合件由应用 builder 承载 |
| F3/P3 | 类型化引用、离线占位、session/lease 生命周期、关闭和异常路径 | `runtime_context.h/.cpp`、`design_preview_frame.cpp`；`designer_runtime_context_tests.cpp`、`designer_schema_tests.cpp` | headless 通过；真实应用服务仍须显式授权 |
| F4/P4 | 稳定 DocumentId、SourceMap/CompileTrace、结构编辑后重新定位、动态节点过滤 | `design_mapping.h/.cpp`；`designer_mapping_tests.cpp`、`designer_canvas_tests.cpp`、`designer_app_tests.cpp` | headless 通过；原生多 DPI/键盘操作待现场 |
| F5/P5 | 迁移/未知字段、唯一临时文件、原子替换、冲突/恢复、失败不发布候选 | `document_store.cpp`、`project_store.cpp`、`design_workbench.cpp`；对应 storage/workbench/app 测试 | headless 通过；不承诺断电 fsync 耐久性 |
| F6/D3 | 文档事务、编辑/结构/引用/保存、资源授权、多文档会话、预览隔离、语义/键盘、相对性能 | `design_editor.cpp`、`design_resources.cpp`、`designer_app.cpp`；下表 fixture、三平台 CI、Linux 性能门禁 | headless 与 hosted CI 通过；完整 D3 现场出口未完成 |

| §10.2 指定 fixture | 实际断言覆盖与测试入口 | 证据范围 |
| --- | --- | --- |
| `l0_document_roundtrip` | 12 节点、引用、显式值、codec 相等与扩展字段：`designer_document_tests.cpp`、`document_store_tests.cpp` | headless |
| `l0_compile_golden` | 与独立 C++ builder 比较声明字段：`designer_document_tests.cpp` | headless |
| `schema_registry_guard` | 所有注册属性读写/编译、错误类型/枚举/范围/结构、字段分类：`designer_schema_tests.cpp` | headless |
| `runtime_context_fixtures` | 空/成功/错型引用、异常 builder、lease 保活、关闭回调只执行一次：`designer_runtime_context_tests.cpp`、`designer_schema_tests.cpp` | headless |
| `document_id_trace` | 复制新 ID、重排保持 ID、source span 更新、重复 runtime identity 诊断、动态行回到 source：mapping/document/app 测试 | headless |
| `selection_transform` | DPI 100%/125%/200%、两个 zoom、pan、框选、八方向/嵌套手柄与取消：`designer_canvas_tests.cpp`、app/mapping 测试 | headless；实际显示器缩放待验 |
| `diagnostic_recovery` | read/migrate/schema/reference/compile/save 阶段、位置去重、JSON 输出、保留旧文档/画面：editor/workbench/preview-frame/CLI 测试 | headless 与打包 smoke |
| `document_transaction` | 原子提交、失败保持 redo/session、750ms 合并、选择恢复、多选/复制/粘贴/拖动与保存检查点：editor/workbench/app 测试 | headless |
| `document_store_faults` | 迁移异常、截断/backup、失败保存、临时名、外部 revision 与明确覆盖再检查：document/project-store、workbench/app 测试 | headless |
| `resource_policy_lifecycle` | 授权 scheme/根、符号链接别名/越界、失败诊断、异步代数、关闭回调、双 renderer 上传：resources/image-resources/app 测试 | headless；旧代数与资源释放有行为断言 |
| `preview_state_isolation` | 绑定/visual override 不改 DOM/dirty、属性/引用与预览文本历史分离：preview/app/keyboard 测试 | headless |
| `designer_accessibility` | 稳定语义、面板/大纲/字段键盘域、IME 取消/提交、主题/DPI/字体/高对比输入：accessibility/app/keyboard 测试 | headless；真实读屏/输入法待验 |
| `preview_determinism` | 固定上下文的 DOM、诊断、节点数/frame hash、虚拟化范围、过期资源丢弃：performance/preview/resources/image-resources 测试 | headless 与 Linux 同机相对性能门禁 |

## 3. 尚未闭环的桌面出口

[`platform-acceptance.md`](platform-acceptance.md) 和
[`check_platform_acceptance.py`](../tests/check_platform_acceptance.py) 定义现场记录与检查器。
当前 `docs/platform-evidence` 没有任何完整 `record.json`；短 smoke 和 CI 摘要均明确
`acceptance_complete=false`。本机存在可连接的 Wayland 会话，没有据此证明独立 X11、
Windows 或 macOS 的登录桌面和验收人可用。

| 会话 | 已有本批证据 | 完整出口仍需什么 |
| --- | --- | --- |
| Linux X11 | Linux hosted CI/headless；Xvfb 生产 Designer 选择、Unicode 编辑、ThemeScope、保存重开协议日志 | 独立 X11 桌面、Orca、完整同提交 record 与附件 |
| Linux Wayland | 原生 Designer 保存重开协议、`glibc/malloc` 双窗口一小时浸泡（3600.020 秒、resize/64 MiB/双恢复状态保持） | Inspector 操作、人工编辑/保存/重开、实际 DPI/主题、IME、Orca、跨应用输入/剪贴板、硬件 GPU/合成器及绑定一小时日志的完整记录 |
| Windows | MSVC、UIA 自动化链路、Designer 大纲/属性 UIA live 回环、CPU/Skia/GPU 回退与三个便携包 CI | 登录 Win32 桌面、真实 IME、Narrator 和 NVDA、干净机器包验证、完整 record；额外 `font_cold_start` 与 `touchpad` 项 |
| macOS | 原生 CPU/NSAccessibility/allocator CTest 与包/Skia CI | 登录 Aqua 桌面、真实 IME、VoiceOver、干净机器包验证、完整 record |

每份正式记录必须标明真实被测 40 位提交、验收人、时间、OS/桌面、GPU/驱动、IME、
应用、provider 类型/可用性及各 reader 版本；每个必检 reader 分别完成全部十个 reader 项。
平台项除 native smoke/allocator 外，还包含 IME、跨应用剪贴板、窗口生命周期、
多窗口焦点/DPI、透明合成、GPU 恢复、OS 拖入，以及三个 Inspector/Designer 人工项。
浸泡报告须证明至少 3600 秒、两个窗口、resize、64 MiB 压力、两次模拟恢复及状态保持。
本次工作流的 Designer/allocator 日志必须与附件 SHA256 匹配；未实测的项保留 pending。

完整记录通过现有检查器后才推进支持矩阵的真实平台状态。当前阶段继续补原生单项证据，
同时等待 Windows/macOS 桌面与实际读屏/IME 验收环境；这些属于现场资源缺口，
不重新打开 DP 决策或移动端范围。
