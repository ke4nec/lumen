# Lumen 平台支持矩阵（v0.3）

> 状态：随 v0.3 阶段 8A–8E 更新（2026-09）。构建命令与系统依赖的单一
> 事实来源是 README 与 `.github/workflows/`。

## 桌面平台

| 平台 | 窗口/输入 | 剪贴板 | 文本/IME | 无障碍 | 渲染 | CI 验证 |
| --- | --- | --- | --- | --- | --- | --- |
| Windows | SDL3（多窗口、resize/DPI、触摸 pointer id） | SDL3 剪贴板（`platform::Clipboard`） | UTF-8 commit + IME preedit（TSF 经 SDL）；修饰键/逻辑键归一化 | 语义树 + Recording 桥；UIA 原生桥为可选目标（未编入时能力报告 false） | CPU、Skia 光栅、Skia GPU（失败回退 CPU） | `windows.yml`：cpu / skia-raster / skia-gpu |
| Linux | SDL3（X11/Wayland） | 同上 | UTF-8 + IBus/Fcitx preedit（候选词锚点经 `SDL_SetTextInputArea`） | 语义树 + Recording 桥；AT-SPI 原生桥为可选目标 | 同上 | `linux.yml`：cpu / skia / skia-gpu（Xvfb + llvmpipe） |
| macOS | SDL3（v0.3 新增桌面支持；菜单关闭经统一关闭规则） | 同上 | UTF-8 + 输入法 preedit（经 SDL） | 语义树 + Recording 桥；NSAccessibility 原生桥为可选目标 | CPU、Skia 光栅；GPU 可选、失败回退 CPU（非门槛） | `macos.yml`：cpu（含 settings 冒烟） |

三平台共用：`ApplicationHost` 契约、归一化 `HostEvent`（时间戳/修饰键/
逻辑与物理键/指针设备/pointer id/滚轮/取消/关闭请求）、语义树与 action
分发、`lumen-text` 编辑模型。counter/settings 示例三平台同源。

## 移动平台（实验性，v0.3 接缝门槛）

| 平台 | v0.3 承诺 | 实现 | 验证 |
| --- | --- | --- | --- |
| Android | SDL-free host 接缝（实验性） | `lumen-mobile-host`：surface attach/detach、pause/resume、安全区、触摸归一化、返回键；Android JNI/NativeActivity 胶水尚未纳入本仓库 | `linux.yml` mobile-core 只验证通用静态库和 headless；NDK/模拟器目标待实现 |
| iOS | SDL-free host 接缝（实验性） | 同一 `MobileHostSeam` 状态机；iOS Objective-C++ 胶水尚未纳入本仓库 | `macos.yml` mobile-core 只验证通用静态库和 headless；Xcode/模拟器目标待实现 |

移动端不承诺（v0.4 再评估）：商店发布、完整移动端控件、后台渲染、原生
accessibility tree、Metal/Graphite。

## 后端与能力

| 能力 | 提供方 | 降级行为 |
| --- | --- | --- |
| CPU 光栅 | `CpuRenderer`（确定性占位字体） | 无需降级；CPU-only 构建不依赖 SDL 实现库与桌面会话 |
| Skia 光栅 | `SkiaRenderer`（可选 `LUMEN_ENABLE_SKIA`） | 未编入时能力报告 `backendName=cpu`，应用安全运行 |
| Skia GPU | `SkiaGpuRenderer`（可选 `LUMEN_ENABLE_GPU`） | 探测/初始化失败自动回退 CPU，诊断记录原因（v0.2 §7C） |
| 文本 shaping | `lumen-text` + `PlaceholderFontManager`（确定性） | 桌面正式 shaping 由可选 Skia 实现提供；缺失时布局/编辑照常（回退明确报告） |
| 剪贴板 | `platform::Clipboard` / `core::ClipboardProvider` | 不可用时 `setText` 返回 false，编辑状态不丢 |
| 语义桥接 | `AccessibilityBridge`（接口 + Recording 桥） | 当前只提供平台无关契约与 Recording 桥；平台原生 provider 尚未实现，工厂返回 nullptr 并给出原因 |
| 可访问性设置 | `PlatformCapabilities`（只读查询） | 高对比/减少动画/字体缩放由 `Theme::fromSettings` 与 `FrameScheduler::setReduceAnimation` 消费 |

## 已知限制（v0.3）

- 键盘撤销（undo 栈）未实现：`TextEditingValue` 状态机已为撤销边界预留
  （纯函数编辑操作），撤销栈与快捷键列入 v0.4。
- RTL 为逐 grapheme 视觉逆序的确定性近似：纯 RTL/LTR 段落正确，混合方
  向重排（UAX#9 完整实现）与双向光标映射列入后续版本。
- 列表无虚拟化：ListView 要求稳定 key 与可预测子树复用；大规模虚拟化
  Sliver 系统按计划属 v0.4。
- 惯性滚动默认关闭（确定性测试优先）；动量物理列入 v0.4。
- 平台原生无障碍桥（UIA/AT-SPI/NSAccessibility）的完整 provider 实现
  属 v0.4；v0.3 冻结了桥接契约与 headless 验证路径。
