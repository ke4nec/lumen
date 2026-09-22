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
系统字体冷启动和触摸板。Xvfb 可用于回归脚本，本身不能作为真实桌面验收。

## 人工回环

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
    "gpu_present_recovery_state": "pending", "soak_resources": "pending"
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
  "artifacts": [{"path": "reader-trace.txt", "sha256": "实际附件 SHA256"}]
}
```

`provider_available` 根据应用 `--diagnostics` 的桥接诊断填写；只有逐项实测后
才把 checks 改为 `pass`。附件应记录步骤、预期/实际结果并附日志或录像。
检查器只验证记录完整性和归属，不代替人工判断。当前容器无真实桌面、AppKit
或 Windows，不能据本地 headless 结果标记三平台人工回环完成。
