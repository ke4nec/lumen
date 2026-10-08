# 真实桌面平台验收

标准 CI 的 Xvfb/llvmpipe、macOS dummy video 和 Windows 自动化 smoke 只证明
代码可运行，不代表桌面合成器、输入法、剪贴板或屏幕阅读器已经验收。真实验收
使用 `.github/workflows/platform-acceptance.yml`，仅在已登录的 self-hosted
桌面 runner 上手动触发。

## Runner 要求

| job | runner labels | 必须存在的会话 |
| --- | --- | --- |
| `linux-x11` | `self-hosted, linux, desktop, x11` | X11 `DISPLAY`、真实合成器、可用 IBus/Fcitx、剪贴板服务 |
| `linux-wayland` | `self-hosted, linux, desktop, wayland` | Wayland `WAYLAND_DISPLAY`、真实 compositor、输入法 portal/协议 |
| `macos` | `self-hosted, macos, desktop` | 登录的 Aqua 会话，VoiceOver 可切换 |
| `windows` | `self-hosted, windows, desktop` | 登录的 Win32 会话，Narrator 或 NVDA 可切换 |

Linux 两个 job 使用当前会话的 SDL video driver，不启动 Xvfb 或 dummy driver。
`lumen-platform-live-smoke` 使用两个真实 AppShell/runApp 窗口，包含文本编辑和
千项 VirtualList；统计真实 IME、resize、滚轮事件及成功 present，按需验证
跨应用剪贴板和最终文本。未操作 IME 时 `ime_verified=false`，不会冒充已验。
每个平台 job 还直接启动 `lumen-designer --file examples/designer/gallery.design
--max-frames 3`，验证设计器编辑器/预览双窗口在真实桌面会话中创建并完成原生窗口
smoke；该结果必须在记录的 `designer_window_smoke` 项标记为 `pass`，并把
`designer-live.log` 随平台 artifact 归档。日志必须包含 Designer 进程成功退出后输出的
`designer_window_smoke pass` 标记；验收检查器会校验该标记和附件哈希。
Linux job 还加载现有 `LD_PRELOAD` present 故障夹具，确认
GPU swap 失败和软件 present 失败均进入预期诊断路径。

## 探针与长时间运行

```sh
# 自动压力：两窗口、快速 resize、反复申请/释放 64 MiB、模拟 renderer 失效重建窗口。
./build-live/tests/lumen-platform-live-smoke --seconds 3600 --resize-burst --stress-mib 64 --inject-recovery
# 人工输入：先在其他应用复制 clipboard-token，再在 primary 编辑框中全选并用 IME 输入“你好”。
./build-live/tests/lumen-platform-live-smoke --seconds 90 --expected-text 你好 --clipboard-expect clipboard-token --transparent
```

自动恢复用例在恢复后的成功帧核对文档、选区、焦点和滚动位置；这是模拟 renderer
故障，不冒充真实 GPU context loss。Linux 的原生 GPU/present 故障夹具另行执行；
其余平台及恢复后的视觉状态按人工检查项记录。透明模式只请求透明窗口，最终合成
效果必须观察桌面背景。`font_available` 记录系统字体初始化结果；冷启动视觉和
触摸板手感不能由此字段替代。

工作流默认独立运行一小时 soak，必须有两个窗口、有效 present/resize、至少
64 MiB 压力、两次恢复且状态保持；短 smoke 不满足发布验收。`soak.json` 与
人工记录分别归档。输入法取消、候选窗位置、多窗口 DPI/焦点隔离、跨应用复制粘贴、
窗口生命周期、透明合成和硬件 GPU 恢复均需逐项记录步骤和结果；Windows 另加
系统字体冷启动和触摸板。OS 拖入（`drag_drop_os_receive`）需在真实会话记录
三类路径：文件管理器拖文件入列表窗口、源应用拖选中文本入编辑框、拖离窗口
或按 Esc 取消；`dragDropStart` 拖出能力保持结构化 Unavailable，不作为验收项。
Xvfb 可用于回归脚本，本身不能作为真实桌面验收。

## 人工回环

三平台协议回环脚本（焦点导航/激活/值设置，与 `LUMEN_UIA_LIVE_SMOKE` /
AT-SPI live 冒烟互补，面向读屏器在场驱动；调用方式与前置见各文件头）：

| 平台 | 脚本 | 读屏器证据 |
| --- | --- | --- |
| Linux（X11） | `tests/atspi_orca_loop.py` | Orca debug 日志（专属 script + 事件消费 + SPEECH OUTPUT） |
| Windows | `tests/uia_reader_loop.py` | NVDA controller client 探活 + 摘要播报；讲述人进程探活；语音人工记录 |
| macOS | `tests/voiceover_loop.py` | VoiceOver 进程探活；语音人工记录 |

工作流不再接受 `confirmed` 自报。配置仓库变量 `LUMEN_ACCEPTANCE_ROOT` 为
runner 本地证据目录，按 `<root>/<40 位提交>/<platform>/record.json` 存放记录，
platform 为 `linux-x11`、`linux-wayland`、`macos` 或 `windows`。
缺失记录、提交/会话不匹配、未通过项目或附件哈希不匹配均失败；验证后连同附件上传。
Linux/macOS 常规 CPU CI 启用原生桥，其他默认 OFF 构建继续覆盖降级路径。
真实工作流同时构建 settings/Gallery，人工使用该次构建的应用进行：

1. Linux：在 counter/settings 窗口中用 Orca 顺序导航语义焦点，激活按钮，修改
   文本字段或滑块值，确认窗口 resize 后焦点和名称保持；分别在 X11 和 Wayland
   job 记录 JSON artifact。
2. macOS：用 VoiceOver 导航同一组控件，完成 press、value set 和窗口关闭/重开，
   确认焦点变化通知没有丢失。
3. Windows：分别用 Narrator 和 NVDA 完成同样的焦点、激活和值设置回环；`a11y`
   CTest 只验证 UIA 客户端链路，不替代这一步。

每次验收应保留 workflow run URL、artifact JSON、OS/桌面环境、显示服务器、
GPU/驱动、输入法和屏幕阅读器版本。没有这些信息的绿色 headless job 不得更新
[`support-matrix.md`](support-matrix.md) 的“真实平台已验证”状态。

记录格式（示例为待验模板，不是验收结果；Windows 的 readers 必须同时包含
`Narrator` 和 `NVDA`，macOS 为 `VoiceOver`）：

```json
{
  "commit": "填写被测源码的完整提交号",
  "platform": "linux-x11",
  "operator": "验收人",
  "recorded_at": "带时区的验收时间",
  "os": "系统及版本",
  "desktop": "桌面/合成器及版本",
  "gpu_driver": "GPU 型号、驱动及版本",
  "ime": "输入法及版本",
  "application": "本次构建的 lumen-settings/lumen-gallery",
  "provider": "atspi",
  "provider_available": false,
  "platform_checks": {
    "ime_preedit_commit_cancel": "pending", "ime_candidate_position": "pending",
    "clipboard_cross_app": "pending", "multiwindow_focus_dpi": "pending",
    "window_lifecycle": "pending", "transparent_composition": "pending",
    "gpu_present_recovery_state": "pending", "soak_resources": "pending",
    "drag_drop_os_receive": "pending", "frame_allocator_source": "pending",
    "designer_window_smoke": "pending"
  },
  "readers": {
    "Orca": {
      "version": "实际版本",
      "checks": {
        "read": "pending", "focus": "pending", "activate": "pending",
        "value": "pending", "editing": "pending", "dialog": "pending",
        "resize": "pending", "close_reopen": "pending"
      }
    }
  },
  "artifacts": [
    {"path": "designer-live.log", "sha256": "实际附件 SHA256"},
    {"path": "reader-trace.txt", "sha256": "实际附件 SHA256"}
  ]
}
```

`provider_available` 根据应用 `--diagnostics` 的桥接诊断填写；只有逐项实测后
才把 checks 改为 `pass`。附件应记录步骤、预期/实际结果并附日志或录像。
检查器只验证记录完整性和归属，不代替人工判断。本机已连接 GNOME/Mutter
Wayland/Xwayland 执行 allocator 短 smoke；AppKit、Windows 和完整人工回环
尚未验收，不能据 headless 或单项短 smoke 标记三平台完整验收完成。

## 待验收登记（2026-10-02）

按 [`lumen-gui-completion-plan.md`](lumen-gui-completion-plan.md) 的缺口编号，
把“实现批次已交付（headless 契约通过）”与“真实平台缺口（必须现场验收）”
分开登记。未覆盖项必须能回答：平台、原因、当前降级行为、后续归属。

| 缺口 | 平台 | 实现状态（四态） | 未覆盖原因 | 当前降级行为 | 后续归属 |
| --- | --- | --- | --- | --- | --- |
| 读屏回环 Narrator/NVDA | Windows | 接口已存在（UIA provider 编入）+ headless 已验证 | 无登录 Win32 会话执行 `tests/uia_reader_loop.py` | provider 未编入时能力报告 false，应用照常运行 | R0 / M14 出口 |
| 读屏回环 VoiceOver | macOS | 接口已存在（NSAccessibility 编入）+ headless 已验证 | 无登录 Aqua 会话执行 `tests/voiceover_loop.py` | 同上 | R0 / M14 出口 |
| 真实 IME preedit/commit/cancel、候选框定位 | Windows/macOS | headless 已验证（IME 状态机） | 无现场输入法 | 候选锚点失效时回退字段内定位；提交不受阻 | R0 / R5 |
| 跨应用剪贴板（文本/图片/自定义格式） | 三平台 | headless 已验证（G-3 core MIME + SDL data API） | 无跨应用真实复制现场 | `setText`/`setFormats` 失败返回 false，编辑状态不丢 | R0 / R5 |
| 透明合成 | Windows texture 已实测；X11/Wayland/macOS 未验 | headless 已验证（预乘表示一致） | 无真实合成器现场 | 软件窗口不支持逐像素透明时按不透明提交 | R0 |
| 1 小时双窗口浸泡 | Linux 已做（M14-A）；Windows/macOS 未做 | headless 已验证（恢复用例） | 无登录桌面长时运行 | 模拟 renderer 失效不冒充真实 GPU context loss | R2 |
| OS 拖入真实 smoke | 三平台 | headless 已验证（M15 契约） | 无真实文件管理器/源应用拖拽现场 | `dragDropStart=false`（SDL 3.2.10 无拖出 API）+ 结构化 Unavailable | R3（本日已纳入 `drag_drop_os_receive` 必检项） |
| 真实整帧 allocator source | 三平台 | scope/异常生命周期和 Linux/glibc 原生后端已有 headless 回归；GNOME/Mutter Wayland 与 Xwayland 两窗口短 smoke 通过（183/175 个有效 allocator 帧） | Windows/macOS 后端、独立 X11 桌面及完整现场 record 未补齐；短 smoke 不满足全部发布检查项 | 无安装、绑定不完整或容量溢出时 HUD 显示 unavailable；命令流容量和 RSS 不冒充整帧读数 | R6（`frame_allocator_source` 必检项；设计与现场命令见 `lumen-frame-allocator-design.md`） |
| 全局快捷键真实按键 | Windows/Linux X11 | Linux X11 后端已交付（Xvfb XTEST 端到端通过）；Win32 后端已交付（RegisterHotKey，编译级 CI 门禁）；macOS 后端未实现 | X11 桌面真实键盘按键待现场；Win32 真实按键（消息泵→UI 事件、冲突码）待现场；Wayland 会话 = `globalHotkeys=false` + 结构化 Unavailable（如实）；macOS = 结构化 Unavailable | R4（Win32/Linux 为验收缺口；macOS 为实现缺口） |
| macOS 原生菜单栏/交通灯 | macOS | 未实现 | 无实现 | 自绘 MenuBar/标题栏可用 | R4 |
| GPU 包、CPack Bundle、干净机器启动 | Windows/macOS | CI 变体已构建（package-skia-gpu） | 无干净机器安装/启动记录 | CI 解包冒烟不替代真实验收 | R1 |

此前源码基线 `a1e012e` + DP-8 门禁批次的本地 `build-debug`/Release 全量 CTest
（1116/1116、1118/1118）均通过；`build-debug` 未指定构建类型，不能作为 Release 性能输入。
包含 Designer 工程资源、恢复回归和四类性能报告/门禁校验；Linux CPU CI 已配置同机基线，
该批没有新增真实桌面、生产 allocator 或屏幕阅读器证据。
后续 Linux/glibc 原生批次已增加实际分配的 headless 证据和双窗口
`--frame-allocator` 现场探针（用法见原生分配设计文档），已通过本机 GNOME/Mutter
Wayland/Xwayland 短 smoke。原始指标与摘要保存在
[`frame-allocator-linux-2026-10-08.json`](platform-evidence/frame-allocator-linux-2026-10-08.json)。
这是未提交实现批次的单项 CPU 诊断，明确 `source_dirty=true` / `acceptance_complete=false`；
没有 workflow、完整提交归属、输入法/读屏、驱动及一小时浸泡记录，不能提交为完整
`record.json`，也不更新其他检查项的 `pass` 字段。

新增缺口进入本表时同步更新 `check_platform_acceptance.py` 的必检集合与本文
record 模板；实现推进改变四态时，本表与
[`support-matrix.md`](support-matrix.md) 必须同一变更内更新。
