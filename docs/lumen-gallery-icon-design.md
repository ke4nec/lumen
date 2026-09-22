# Lumen Gallery 应用图标设计（Core Dark）

> 文档状态：已实施（v1，2026-09-21）
>
> 设计稿：[`design/gallery-icon.html`](../design/gallery-icon.html)（四方向评审板，方向 01 选定）
>
> 适用范围：Gallery 示例（`examples/gallery`）的应用标识。UI 内图标仍走
> `IconId`/`IconTheme` 契约（visual-system §8），本文件只覆盖应用图标资产。

## 1. 概念与母版

双色字母标「L」：**竖笔 = Column（content.primary）、横笔 = Row（accent）**，
用框架两组布局原语构成 Gallery 的首字母。Core Dark 方向（设计板 01）为
推荐基线：徽章深色双阶渐变 + 白/蓝双色，与默认桌面主题 token 同源。

母版是代码而非图片：`examples/gallery/gallery_icon.h` 以 8×8 超采样 SDF
光栅化确定性生成直通 RGBA8。同一几何被三处消费——

- 运行时窗口图标（按 DPI 现场光栅化，见 §4）；
- 构建期资源导出（`icon_tool.cpp` → PNG/ICO/ICNS）；
- 单元测试（`tests/gallery_icon_tests.cpp`）。

## 2. 几何与分层契约

标记几何（占徽章边长比例，`galleryIconGeometry`）：

| 尺寸 | margin | box | stroke | 徽章处理 |
| --- | ---: | ---: | ---: | --- |
| 16 | 3 px | 10 px | 3 px | 平面 `#262630` |
| 20 | 4 px | 12 px | 4 px | 平面 `#262630` |
| 24 | 5 px | 14 px | 4 px | 平面 `#262630` |
| 32 | 6 px | 20 px | 6 px | 平面 `#262630` |
| ≥48 | 19% | 62% | 17% | 渐变 `#30303a→#24242f(60%)→#1d1d26` + 1px 内缘 |

- 徽章圆角恒为 22.5%；笔画端部为胶囊（圆角 = 半笔宽）；竖笔覆盖交角。
- ≤32（任务栏/小图档）收敛为平面双色、无内缘线；手调整数像素网格
  （条宽/坐标对齐设备像素，与 visual-system §8 图标盒 `lround` 同口径）。
  28 为 gallery 标题栏品牌位（非出口尺寸），按基础比例 + 平面档渲染。
- ≥48（桌面/显示档）保留渐变与 1px 内缘（整圈白 5%、上半加至 9%）。
- 颜色 token：竖笔 `#f1f1f4`、横笔 `#568cf0`（与设计板 01 同源）。

## 3. 出口尺寸与平台映射

构建期由 `lumen-gallery-icons` 工具导出到构建树
`examples/gallery/icons/`（产物不提交仓库，AGENTS 约定）：

| 产物 | 内容 | 消费方 |
| --- | --- | --- |
| `lumen-gallery-<size>.png` × 8 | 16/20/24/32/48/64/128/256 | Linux hicolor（去掉 20：125% DPI 档仅进 ICO） |
| `lumen-gallery.ico` | 8 尺寸 PNG-in-ICO | Windows exe 资源（`1 ICON` rc 嵌入） |
| `lumen-gallery.icns` | icp4/ic11/ic12/ic07/ic08 | macOS（随包安装，.app bundle 打包接入时消费） |

多尺寸的意义：Explorer/任务栏/桌面 shell 按**请求尺寸选档**，任何缩放档
都有原生位图可用，不放大位图导致发虚（用户可感知的"放大失真"）。

Linux 桌面集成与 settings 同布局（M12）：`assets/lumen-gallery.desktop`
+ `share/icons/hicolor/<size>x<size>/apps/lumen-gallery.png`（16–256 七档），
linuxdeploy 组装 AppImage 消费同一 install 布局。

## 4. 运行时窗口图标

`RunOptions.windowIcon`（provider）在渲染器装配后、首帧前调用一次
（渲染器工厂可能重建窗口，图标必须落在最终窗口上）：

- Gallery 以任务栏主档 48 为基，按 `--dpi`（1–2）现场光栅化
  `round(48 × dpi)` 母版——高 DPI 直接产出更大几何（≥48 比例档），
  系统无需放大位图。
- 宿主 `windowIcon` 能力缺失或 `setWindowIcon` 失败时诊断降级，不阻塞
  启动；资源 exe/桌面图标由打包层提供，运行时只覆盖窗口/任务栏位。
- Fake host 记录 `iconCalls` 供 headless 验收（§5）。

## 5. 测试与验收

`tests/gallery_icon_tests.cpp`：

- 几何契约：手调网格（16/32）与比例档（64）数值断言。
- 光栅：八档尺寸缓冲、圆角外透明、双色标记采样点、≤32 平面 / ≥48
  渐变 + 内缘亮于内部、16px 像素网格（笔芯列 vs 徽章列）。
- 容器：PNG 签名/IHDR/zlib 头/IEND，ICO 条目数-偏移-尾对齐，ICNS
  槽位序列与块长自洽。
- runApp 装配：provider 像素逐字节送达宿主、空图标跳过、宿主失败
  安全降级（退出码仍 0）。

桌面冒烟：Windows 上 `lumen-gallery.exe` 资源图标在 Explorer 各视图档
（16/列表 … 256/超大图标）均为原生位图；Linux 安装/解包后
`share/icons/hicolor` 各档存在且 `update-desktop-database` 无错。

## 6. 维护规则

- 改视觉（颜色/比例/分层）先改 `design/gallery-icon.html` 设计板与本文，
  同一提交更新光栅化器与测试；禁止在光栅化器里出现设计板之外的魔数。
- 新增出口尺寸需同步：`kGalleryIconExportSizes`、hicolor/ICO/ICNS 映射
  与本文表格。
- 其余示例（counter/settings）沿用 `assets/lumen.png`；如需统一品牌，
  另立变更评审，不在本契约内扩编。
