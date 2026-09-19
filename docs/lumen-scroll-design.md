# Lumen 滚动系统设计（轴模型 / 输入路由 / 物理与语义契约）

> 文档状态：设计规格（2026-09；水平轴由 `docs/lumen-optimization-plan.md` P2 引入，本文档为其行为契约的唯一来源——后续所有滚动行为变更以此为准）。
> 输入：`include/lumen/core/scroll.h`（ScrollController）、`src/layout/layout.cpp::layoutScrollView`（视口约束与 offset 应用）、`src/render/painter.cpp`（滚动条）、`src/core/interaction.cpp`（滚轮/拖动/键盘路由）、`include/lumen/accessibility/bridge.h`（语义 scroll action）、M3/M10/M12 完成记录。
> 相关既有文档：`lumen-visual-system-design.md` §滚动条（token/几何）、`lumen-collection-controls-design.md`（VirtualList 纵向虚拟化）。

## 1. 范围与非目标

**范围**：声明为水平轴的 ScrollView 获得与纵向完全对等的滚动能力——布局约束镜像、滚轮（含 Shift+纵轮）、指针拖动与惯性、键盘、水平滚动条、语义 scroll action 的 X 分量。

**非目标**：

- 二维双轴联滚（同一视口同时纵横滚动）不做——单视口单活动轴；双轴留按需评估。
- 水平虚拟化（VirtualList/TreeList 横向物化）不做；TreeList 列宽仍为固定+权重。内容超宽的整树 clip 平移（P2b）待 P2a 落地后按真实痛点评估。
- RTL 镜像（水平滚动条/滚轮方向翻转）不做。
- 默认纵向路径行为零变化是硬出口（既有全部 headless 帧哈希不变）。

## 2. 轴模型

- `core::ScrollAxis { Vertical, Horizontal }`；默认 Vertical。
- **单视口单活动轴**：`Widget.scrollAxis` 声明视口活动轴（枚举一字节，入 Widget 字段打包段，体积预算以测试锁定）；RenderNode 镜像 `scrollAxis`。
- `RenderNode.scrollOffset/scrollExtent` 语义 = 活动轴上的偏移/可滚范围（字段不增，含义随轴）。
- 应用为水平视口单独持有一个横向 `ScrollController`（构造声明轴）——与既有「一视口一控制器」模型一致；VirtualList/Tree 的源控制器保持纵向。
- 符号约定两轴一致：**正 offset = 内容向轴正向滚**（纵向=向下、水平=向右）。

## 3. 布局（layoutScrollView 镜像）

| | 纵向（现状） | 水平（P2） |
| --- | --- | --- |
| 内容约束 | 宽 ≤ 视口 − padding，高不限 | 高 ≤ 视口 − padding，宽不限 |
| offset 应用 | 子 offset.y −= offset | 子 offset.x −= offset |
| extent | max(0, 内容高 − 视口高) | max(0, 内容宽 − 视口宽) |
| shrinkWrap | 无显式高时按内容高夹取 | 无显式宽时按内容宽夹取 |
| 裁剪 | clipContent 不变 | 同 |

## 4. 输入路由

- **滚轮**：`InteractionController` 的 wheel 派发按命中视口的轴选分量——纵向视口吃 `scrollDelta.y`、水平视口吃 `scrollDelta.x`（SDL 宿主已对两轴做同一符号换算，M12 规则）。**Shift+纵轮 → 横向分量**：Shift 按下时命中水平视口则把 dy 投影为 dx（桌面惯例；FLIPPED 符号随宿主换算，不二次翻转）。`wheelSink` 签名从单 dy 扩为携带完整 `Offset` delta（默认参数兼容，PointerButton 先例）。
- **拖动/惯性**：`ScrollDragSink` 泛化为活动轴分量（水平视口吃 dx）；fling 物理常量两轴共用（M10 确定性指数衰减不变）。
- **滑块冲突（M10 垂直回归的镜像，必须防）**：水平视口内起点命中 enabled 且带 bind 的 Slider 时，拖动属于滑块——`setSliderByPosition` 释放设值不得被横向拖动路由劫持。回归用例 `slider_drag_inside_horizontal_scroll_view_still_sets_value` 为出口条件。
- **键盘**：焦点在水平视口（非 TextField）时 Left/Right = 方向步进、PageUp/PageDown = 横向翻页、Home/End = 两端；纵向方向键不属于本轴（交由其他视口/焦点消费）。TextField 内 Left/Right 仍是 caret 移动（既有优先级不变）。

## 5. 滚动条（painter）

- 水平视口画横向 thumb：贴视口底边、track = 视口宽（减纵向滚动条厚度如两者共存——单轴视口无此情况）、thumb 宽 = `visibleFraction × track`、`minLength` 同 token；与纵向互为镜像。
- 单轴视口只画一条滚动条；token（thickness/thumbWidth/minLength/颜色）两轴共用，出自 `ScrollbarTokens`。

## 6. 语义契约

- `AccessibilityBridge`/`AppShell::performAccessibilityAction`/Recording 的 scroll 参数族尾部追加 `scrollDeltaX = 0.0F`（默认参数兼容扩约，PointerButton 先例）；既有 `scrollDeltaY` 不变。
- 水平视口的语义 value/scroll action 按活动轴报告（visibleFraction/maxOffset 同源）。
- 滚动语义 action ≡ 键盘路径同 handler 回执（M5 不变量保持）。

## 7. 测试口径

- **core**：横向 scrollBy/applyWheel/applyDrag/applyKey/semanticScroll/fling 物理与纵向同参数同行为（已交付：`tests/motion_scroll_tests.cpp` 两用例）。
- **layout**：宽内容横向 extent、offset 应用到 X、clip/shrinkWrap、纵向默认路径几何零变化。
- **render**：横向 thumb 几何/progress/minLength；纵向像素不变。
- **interaction**：wheel 分量路由、Shift+dy 投影、横视口内 Slider 拖动仍设值（回归）、TextField 选区路径不受影响。
- **语义**：Recording 断言横向视口 scroll action 的 scrollDeltaX 回执。
- **示例**：gallery 水平滚动演示区（超宽卡片行）集成用例。
- **不变量**：既有全部 headless 帧哈希不变。

## 8. 兼容与迁移

1. `ScrollController` 构造增加带默认值的轴参数——既有调用零改动（纵向语义逐字节不变）。
2. `Widget.scrollAxis`/`RenderNode.scrollAxis` 默认 Vertical——未声明的视口行为与现状一致。
3. `wheelSink`/`ScrollDragSink` 签名扩展走默认参数；既有调用方（gallery/settings 应用侧接线）不需要同批修改。
4. 语义 `scrollDeltaX` 尾部追加默认参数——Recording bridge 回归不受影响（M5 冻结口径的兼容扩约）。
5. 序列化版本/RenderCommand：零改动（滚动为布局/交互层概念，不进命令）。

## 9. 已知限制（收口时如实复核）

- 无双轴联滚、无水平虚拟化、无 RTL（§1 非目标）。
- Shift+纵轮投影是框架层约定（宿主无原生横轮事件时）；触控板双指横滑经 `scrollDelta.x` 原生到达。
- 水平滚动条暂不支持拖动 thumb 直接定位（与纵向 M10 拇指跟手对齐属后续增强，随真实使用评估）。
