# Lumen 剪贴板深度设计（G-3）

> 状态：已实现（2026-09-29；gap-backlog G-3，P1）。
> 动机：M4 接入的剪贴板经 SDL 为纯文本；截图/图片类工具、DataGrid 选区拷成 TSV+PNG、粘贴按钮可用态刷新（有无内容）都是工具应用高频路径。
> 代码：契约 [`include/lumen/core/clipboard.h`](../include/lumen/core/clipboard.h)（MIME 数据层，默认实现降级）；SDL3 实现 `Sdl3ApplicationHost::Sdl3Clipboard`（`src/platform/sdl3_host.cpp`，SDL3 通用 data API）；Fake 实现 `include/lumen/platform/fake_host.h`；能力位 `PlatformCapabilities`；变更事件 `HostEventType::ClipboardChanged`。
> 测试：`tests/clipboard_tests.cpp`（headless 契约锁定）。

## 1. 契约（core::ClipboardProvider）

在既有纯文本四函数之上加 MIME 数据层（全部带默认实现——**纯文本宿主零改动**，默认行为 = 结构化降级）：

- `hasFormat(mime)` / `data(mime)` / `setData(mime, bytes)`：原始字节读写；`text/plain` 映射到既有 `hasText/text/setText`。
- `setFormats(vector<Entry>)`：**原子多格式放置**（一次替换剪贴板，对外提供多种表示——DataGrid 选区 = `text/tab-separated-values` + `image/png` 的模型）。默认实现降级为只写首个 `text/plain` 条目。
- `formats()`：当前可用格式列表。
- `hasImage()`：`image/png` 便捷谓词（粘贴按钮可用态）。
- 常量：`kMimeText = "text/plain"`、`kMimePng = "image/png"`、`kMimeTsv = "text/tab-separated-values"`。

框架只传字节不做编解码（PNG 编解码属应用/资源层；core 不依赖 render）。`data()` 不可用返回空——与空数据不可区分，调用方先查 `hasFormat`。

## 2. SDL3 实现

- 语义对齐：`setText`/`setFormats` 均为**整体替换**（Fake 实现同步：纯文本写入清除全部 MIME 条目）。`setFormats` 条目 >16 结构化拒绝（不静默截断）；`formats()` 去重。
- 读：`SDL_GetClipboardData(mime, &size)`（X11/Wayland 任意 MIME；`text/plain` 走既有 `SDL_GetClipboardText` 保持现状语义）。
- 写：`SDL_SetClipboardData(callback, cleanup, payload, mimes, n)`——字节所有权交 SDL，其他进程粘贴时按需取数（不复制整块进系统直到被请求）；失败（`SDL_SetClipboardData` false）时释放 payload 返回 false。空列表 = `SDL_ClearClipboardData`。
- 枚举：`SDL_GetClipboardMimeTypes`（并入 text 视图）。

## 3. 能力位（PlatformCapabilities，四态纪律）

| 位 | 含义 | 当前值 |
| --- | --- | --- |
| `clipboardFormats` | MIME 数据读写 | 三桌面 true（SDL data API） |
| `clipboardImage` | image/png 跨应用读写已验证 | Linux true（X11/Wayland）；Windows false（注册格式名 "image/png" 与应用侧 "PNG" 不匹配——待 Win32 CF_DIB/PNG 原生 seam）；macOS false（待真机验证后开启；SDL cocoa 后端有 PNG 支持，但遵循"验证过才报 true"） |
| `clipboardChange` | 变更事件 | 三桌面 true（`SDL_EVENT_CLIPBOARD_UPDATE`） |

不可用路径结构化降级：读空/写 false/事件不达，**不抛异常、不阻塞 UI 线程**（写路径无同步等待外部进程）。

## 4. 变更事件

`HostEventType::ClipboardChanged`：剪贴板为会话级（SDL 事件无窗口字段），`event.window` 留空；runApp 与 `SystemThemeChanged` 同通道**广播所有活跃窗口**的 `RunOptions.onEvent`（应用据此刷新"粘贴可用态"并请求帧）。Fake host 注入：`pushClipboardChanged()`。

## 5. 验证与遗留

- headless（本提交）：默认实现降级语义、Fake 多格式 roundtrip/双视图互通/失败注入、变更事件经 runApp 广播到 onEvent。
- 桌面 smoke（人工，参照 platform-acceptance 清单）：Linux X11/Wayland 跨应用文本/PNG 粘贴（GIMP/文件管理器互拷）；Windows/macOS 文本路径不变 + 图片位如实 false 的降级确认。
- 遗留增量：Win32（CF_DIB ↔ PNG 转换）与 macOS（NSPasteboard PNG UTI）原生 seam（复用 M12/M16 `native_services_*` 模式）；消费方示例（DataGrid 选区 → TSV+PNG 一键拷贝）随 M17 池穿插。
