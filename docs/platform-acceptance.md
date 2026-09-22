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
`lumen-platform-live-smoke` 会在窗口存活期间反复 present，可选快速 resize，验证
IME text-input session/caret 区域和剪贴板 round-trip，并输出带 driver、帧数和
resize 事件的 JSON。Linux job 还加载现有 `LD_PRELOAD` present 故障夹具，确认
GPU swap 失败和软件 present 失败均进入预期诊断路径。

## 人工回环

触发 workflow 时，只有完成对应回环才能把输入选为 `confirmed`，否则 job 失败：

1. Linux：在 counter/settings 窗口中用 Orca 顺序导航语义焦点，激活按钮，修改
   文本字段或滑块值，确认窗口 resize 后焦点和名称保持；分别在 X11 和 Wayland
   job 记录 JSON artifact。
2. macOS：用 VoiceOver 导航同一组控件，完成 press、value set 和窗口关闭/重开，
   确认焦点变化通知没有丢失。
3. Windows：用 Narrator 或 NVDA 完成同样的焦点、激活和值设置回环；`a11y`
   CTest 只验证 UIA 客户端链路，不替代这一步。

每次验收应保留 workflow run URL、artifact JSON、OS/桌面环境、显示服务器、
GPU/驱动、输入法和屏幕阅读器版本。没有这些信息的绿色 headless job 不得更新
[`support-matrix.md`](support-matrix.md) 的“真实平台已验证”状态。
