# Lumen ColorPicker 设计（G-4c）

> 状态：已实现（2026-09-30；gap-backlog G-4 第三批）。
> 动机：全库无取色控件；主题定制/标注/画图类工具高频。
> 代码：[`include/lumen/widgets/color_picker.h`](../include/lumen/widgets/color_picker.h)、`src/widgets/color_picker.cpp`。
> 测试：`tests/color_picker_tests.cpp`。
> Mockup：`design/color-picker.html`。

## 1. 结构（纯组合件，零新增 WidgetType/RenderCommand）

```
Column "color-picker"
├─ Row：预览 swatch(24px) + hex 文本（label）
├─ Slider H（bind "<key>:h"，0..100 → ×3.6°）
├─ Slider S（bind "<key>:s"，0..100 → ÷100）
├─ Slider V（bind "<key>:v"，0..100 → ÷100）
└─ Row "<key>:palette"：预置色 swatch（Ghost Button 20px + 数据色）
```

滑条拖动/键盘复用 M6 Slider 契约（跟手/方向键/语义 role=slider）；swatch 点击复用 Button/HandlerRegistry。**不引入 HSV 面板自定义绘制**（首版范围控制；面板 + 拖拽取色为后续增量）。

## 2. 值语义

- 结果 = `"#RRGGBB"` 写入 `resultBind`；`onPicked(hex)` 与写值同路径（色板点击与滑条派生一致）。
- 通道 bind 与结果 bind 分离：滑条观察者派生 hex，色板点击反推通道（H/S/V 同步），**无写环**（结果变化不回写通道）。
- 反推量化：H 通道整数格 3.6°——派生色与色板原色的色相差 ≤1 格（测试按 HSV 一致性断言，不做逐通道精确比较）。

## 3. 视觉与 token 例外

控件 chrome（滑条/按钮/文本）全部走 Theme 既有 token；**swatch 的填充色是数据本身**（用户正在挑选的颜色），不属于 chrome——这是 visual-system「颜色即内容」例外条款，与 gallery 色板演示同口径。hex 文本/预览随重建刷新（Color 为值类型不走 bind 机制，build 读 StateStore 当前值）。

## 4. 解析降级

`colorFromHex`：可选 `#`、6 位十六进制、大小写不敏感；**非法输入 = 黑色**（结构化降级，不抛异常）——粘贴外部色值/损坏持久化均安全。

## 5. 验证

HSV↔RGB 主色/黑白/色相环 × 饱和度往返（灰色 sat=0 无色相概念除外）、hex 编解码与非法降级、色板点击（结果+反推+回调同路径）、滑条派生与幂等（同值不重复回调）、build 结构（预览/滑条/色板数量）。桌面 smoke（人工）：滑条拖动手感与 M6 一致，无新增。
