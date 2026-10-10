# 设计器前置任务阶段出口复核

> 核对日期：2026-10-11。最近被测运行时提交：`dc57970d8a938dba803b0ede502626cef013bfe6`；一小时浸泡单项归属 `ab43fec9a5f47b5dbafc47a7b4db1d85d82b8a7a`。
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
| Linux X11 | Linux hosted CI/headless；旧 Xvfb/Xwayland 回归 | 独立 X11 桌面、Orca、完整同提交 record 与附件 |
| Linux Wayland | 原生 Designer 短窗口和 `glibc/malloc` 双窗口短探针 | Inspector 操作、编辑/保存/重开、实际 DPI/主题、IME、Orca、跨应用输入/剪贴板、GPU/合成器及一小时浸泡的完整记录 |
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
