# Lumen 滚动系统设计（轴模型 / 输入路由 / 物理与语义契约）

> 文档状态：核心实现已完成（2026-09）；水平轴由 `docs/lumen-optimization-plan.md` P2 引入，本文档为当前行为契约的唯一来源。Gallery 超宽卡片演示、自动隐藏、RTL 与水平虚拟化仍是明确未完成项。
> 输入：`include/lumen/core/scroll.h`（ScrollController）、`src/layout/layout.cpp::layoutScrollView`（视口约束与 offset 应用）、`src/render/painter.cpp`（滚动条）、`src/core/interaction.cpp`（滚轮/拖动/键盘路由）、`include/lumen/accessibility/bridge.h`（语义 scroll action）、M3/M10/M12 完成记录。
> 相关既有文档：`lumen-visual-system-design.md` §滚动条（token/几何）、`lumen-collection-controls-design.md`（VirtualList 纵向虚拟化）。横纵交互稿：[scrollbar-controls.html](../design/scrollbar-controls.html)。

## 1. 范围与非目标

**范围**：声明为水平轴的 ScrollView 已获得与纵向对等的滚动能力——布局约束镜像、滚轮（含 Shift+纵轮）、指针拖动与惯性、键盘、水平滚动条、语义 scroll action 的 X 分量；对应 core/layout/interaction/render 回归已交付。Gallery 超宽卡片演示仍待补。

**非目标**：

- 二维双轴联滚（同一视口同时纵横滚动）不做——单视口单活动轴；双轴留按需评估。
- 水平虚拟化（VirtualList/TreeList 横向物化）不做；TreeList 列宽仍为固定+权重。内容超宽的整树 clip 平移（P2b）待 P2a 落地后按真实痛点评估。
- RTL 镜像（水平滚动条/滚轮方向翻转）不做。
- 默认纵向布局与绘制保持兼容；滚轮对无滚动范围子视口的穿透修正规则见 §4。

## 2. 轴模型

- `core::ScrollAxis { Vertical, Horizontal }`；默认 Vertical。
- **单视口单活动轴**：`Widget.scrollAxis` 声明视口活动轴（枚举一字节，入 Widget 字段打包段，体积预算以测试锁定）；RenderNode 镜像 `scrollAxis`。
- `RenderNode.scrollOffset/scrollExtent` 语义 = 活动轴上的偏移/可滚范围（字段不增，含义随轴）。
- 应用为水平视口单独持有一个横向 `ScrollController`（构造声明轴）——与既有「一视口一控制器」模型一致；VirtualList/Tree 的源控制器保持纵向。
- 符号约定两轴一致：**正 offset = 内容向轴正向滚**（纵向=向下、水平=向右）。

## 3. 布局（layoutScrollView 镜像）

| | 纵向（现状） | 水平（当前实现） |
| --- | --- | --- |
| 内容约束 | 宽 ≤ 视口 − padding，高不限 | 高 ≤ 视口 − padding，宽不限 |
| offset 应用 | 子 offset.y −= offset | 子 offset.x −= offset |
| extent | max(0, 内容高 − 视口高) | max(0, 内容宽 − 视口宽) |
| shrinkWrap | 无显式高时按内容高夹取 | 无显式宽时按内容宽夹取 |
| 裁剪 | clipContent 不变 | 同 |

## 4. 输入路由

- **滚轮**：`InteractionController` 的 wheel 派发按命中视口的轴选分量——纵向视口吃 `scrollDelta.y`、水平视口吃 `scrollDelta.x`（SDL 宿主分别按各轴归一化方向，见下文）。**Shift+纵轮 → 横向分量**：Shift 按下时命中水平视口则把 dy 投影为 dx（桌面惯例；FLIPPED 符号随宿主换算，不二次翻转）。`wheelSink` 签名从单 dy 扩为携带完整 `Offset` delta（默认参数兼容，PointerButton 先例）。
- **嵌套滚轮传递**：沿命中链从子视口向父视口查找，跳过 `scrollExtent=0`（空内容或内容已完全放入）的区域，以及本次滚轮在其活动轴上没有分量的区域；找到具有滚动范围且轴匹配的最近视口后才派发。每一层都从原始 delta 计算轴分量，`showScrollbar` 仅控制装饰显隐，不决定滚动能力。支持 ScrollView/ListView/VirtualList/List/Tree/TreeList；源控制器和应用 sink 路径使用同一规则。已有滚动范围但到达端点的视口继续保持原有不向外层联滚的策略；键盘、拖动和惯性策略不因本次修复改变。传递只在当前事件树内进行，不能穿过模态 overlay 滚动背后的页面。
- **拖动/惯性**：`ScrollDragSink` 泛化为活动轴分量（水平视口吃 dx）；fling 物理常量两轴共用（M10 确定性指数衰减不变）。新拖动开始或取消时使用 `ScrollController::cancelDrag()` 清除速度样本并停止惯性，不能把上次滑块位移带入下一次内容拖动；`stopFling()` 只停止当前惯性，不清除进行中的速度采样。
- **方向归一化**：框架 delta.x>0 向右、delta.y>0 向下（offset 增加，内容坐标分别减去 offset）。SDL NORMAL 的 x>0 是右、y>0 是上，因此只翻转 Y；FLIPPED 先还原两轴再转换。内容拖动方向与 offset 相反，滑块拖动方向与 offset 相同；两条手势路径不得混用。依据 [SDL_MouseWheelEvent](https://wiki.libsdl.org/SDL3/SDL_MouseWheelEvent)。
- **滑块冲突（M10 垂直回归的镜像，必须防）**：水平视口内起点命中 enabled 且带 bind 的 Slider 时，拖动属于滑块——`setSliderByPosition` 释放设值不得被横向拖动路由劫持。回归用例 `slider_drag_inside_horizontal_scroll_view_still_sets_value` 为出口条件。
- **键盘**：焦点在水平视口（非 TextField）时 Left/Right = 方向步进、PageUp/PageDown = 横向翻页、Home/End = 两端；纵向方向键不属于本轴（交由其他视口/焦点消费）。TextField 内 Left/Right 仍是 caret 移动（既有优先级不变）。

## 5. 滚动条（painter）

- 水平视口画横向 thumb：贴视口底边、track = 视口宽（减纵向滚动条厚度如两者共存——单轴视口无此情况）、thumb 宽 = `visibleFraction × track`、`minLength` 同 token；与纵向互为镜像。
- 单轴视口只画一条滚动条；token（thickness/thumbWidth/minLength/颜色）两轴共用，出自 `ScrollbarTokens`。
- `ScrollbarGeometry` 是绘制/命中/拖动的共同来源；`inset` 独立于滑块厚度，长度和位置不随 hover 加粗跳动。几何全部钳制在视口内，滑块铺满短轨道时不宣称可拖。
- 默认 Comfortable：透明命中轨道 16、可视滑块 8、hover/drag 可视 10；Compact 为 12/6/8，Touch 为 24/10/12。minLength=24、inset=4，各长度随字体缩放一次。颜色与密度以视觉系统 §7.4 为准。
- 滑块命中区覆盖整条轨道横截面；悬停显示系统 PointingHand 并加粗，按住时显示 dragged 色，移出视口继续捕获。按下即捕获，无内容拖动 slop；按抓取位置占滑块的比例计算目标 offset，支持横纵两轴、重建及尺寸变化。释放/取消停止，不起惯性；内容拖动仍保留 M10 惯性。
- 滚动条先于其下方内容命中，不误触按钮、Slider、选区或列表行；单击空白轨道按该方向翻动 90% 视口。隐藏/无溢出无交互，禁用不响应；捕获目标消失或失效立即取消，静止指针在重建后重新命中，光标和样式不能残留。
- 该规则覆盖所有六类滚动视口及 Dropdown、长菜单、Dialog。源控制器由框架驱动；应用 `ScrollDragSink` 收到换算后的内容位移，滑块结束发送 `Cancel`（仅停止）而不是触发惯性的 `End`。
- 捕获期间不派发菜单行/菜单栏等被动悬停动作，避免越轨拖动切换菜单或改变捕获目标。取消后的松键不触发落点处的 Slider、Checkbox 等控件；新模态弹层替换事件树前，先向原捕获所属 sink 发送取消。源控制器自行处理的捕获不得向应用 sink 发送取消。

## 6. 语义契约

- `AccessibilityBridge`/`AppShell::performAccessibilityAction`/Recording 的 scroll 参数族尾部追加 `scrollDeltaX = 0.0F`（默认参数兼容扩约，PointerButton 先例）；既有 `scrollDeltaY` 不变。
- 水平视口的语义 value/scroll action 按活动轴报告（visibleFraction/maxOffset 同源）。
- 滚动语义 action ≡ 键盘路径同 handler 回执（M5 不变量保持）。

## 7. 测试口径

- **core**：横向 scrollBy/applyWheel/applyDrag/applyKey/semanticScroll/fling 物理与纵向同参数同行为（已交付：`tests/motion_scroll_tests.cpp` 两用例）。
- **layout**：宽内容横向 extent、offset 应用到 X、clip/shrinkWrap、纵向默认路径几何零变化。
- **render**：横向 thumb 几何/progress/minLength；两轴状态与密度共用 token，局部与完整重绘像素一致。
- **interaction**：wheel 分量路由、Shift+dy 投影、横视口内 Slider 拖动仍设值（回归）、TextField 选区路径不受影响。
- **滚动条验收**：`scrollbar_tests.cpp` 覆盖六类视口、两轴滑块、命中扩展、悬停/拖动状态、轨道翻页、内容遮挡、失效取消、密度/缩放和宿主手形映射；菜单/下拉与页面集成测试覆盖弹层及外层方向。SDL 输入测试直接注入 NORMAL/FLIPPED 双轴事件；CPU/GPU 像素输出验证实际状态与裁剪。
- **嵌套回归**：`wheel_routing_tests.cpp` 覆盖六类视口空/少量内容、多层祖先、隐藏滚动条、有溢出内容及端点、内容收缩与模态隔离；异轴输入随水平滚动 P2 集成验收；Gallery 集成覆盖空 List/Tree 上滚轮驱动外层页面。
- **语义**：Recording 断言横向视口 scroll action 的 scrollDeltaX 回执。
- **示例**：Gallery 水平滚动演示区（超宽卡片行）集成用例仍待补；现有 core/layout/interaction/render 与嵌套路由测试已覆盖行为契约。
- **视觉基准**：滚动条尺寸和状态按 §5 更新；其余控件的 headless 像素基准保持不变。

## 8. 兼容与迁移

1. `ScrollController` 构造增加带默认值的轴参数——既有调用零改动（纵向语义逐字节不变）。
2. `Widget.scrollAxis`/`RenderNode.scrollAxis` 默认 Vertical——未声明的视口行为与现状一致。
3. `wheelSink`/`ScrollDragSink` 签名扩展走默认参数；既有调用方（gallery/settings 应用侧接线）不需要同批修改。
4. 语义 `scrollDeltaX` 尾部追加默认参数——Recording bridge 回归不受影响（M5 冻结口径的兼容扩约）。
5. 序列化版本/RenderCommand：零改动（滚动为布局/交互层概念，不进命令）。

## 9. 已知限制（收口时如实复核）

- 无双轴联滚、无水平虚拟化、无 RTL（§1 非目标）。
- Shift+纵轮投影是框架层约定（宿主无原生横轮事件时）；触控板双指横滑经 `scrollDelta.x` 原生到达。
- 自动隐藏滚动条仍未实现；有溢出时保持常显，横纵滑块均支持直接拖动。
