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
