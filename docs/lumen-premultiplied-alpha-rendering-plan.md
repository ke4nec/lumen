# Lumen 预乘 alpha 渲染改造计划与任务清单

> 状态：P0–P5 已完成。before 基线保留，CPU 已迁移预乘累积；视觉与性能证据见 P4，调用方迁移见 [迁移说明](lumen-alpha-migration.md)。平台未运行事项仍按 P3/P5 保留待验，不表示三平台透明合成均已验收。
> 日期：2026-09-20。计划编写时核对基点为 `8779ccd` 及当时工作树；实际实施从 `e06f3d9` 开始，阶段提交与验证见第 9 节。
> 目标：应用颜色保持直通 RGBA，渲染器内部采用预乘 RGBA 累积，帧缓冲携带明确的 alpha 格式，透明窗口按格式直接呈现。
> 范围：Windows、Linux、macOS 桌面；CPU 光栅、Skia 光栅、Skia Ganesh GPU 的共用像素边界。按既有能力优化交付，不新增里程碑编号。

## 1. 改造目的与边界

本项消除 CPU 半透明混合中的重复乘除和透明窗口呈现前的整帧格式转换，并统一图片、离屏结果、上传命令、截图和窗口对像素数据的解释。

预期收益必须分别验证：

| 收益 | 实现途径 | 验证方式 |
| --- | --- | --- |
| 减少 CPU 混合运算 | 预乘 source-over 不再逐通道除以输出 alpha | 混合微基准与透明场景的提交耗时 |
| 减少透明窗口呈现准备成本 | 已预乘帧直接提交，省去转换副本 | 转换次数、复制字节数、临时缓冲容量与准备耗时 |
| 降低格式混用风险 | 像素格式随缓冲、资源和序列化传播 | 格式往返、重复转换、跨后端与窗口回归 |
| 保留后端替换空间 | 公共格式不包含 Skia、SDL 类型 | CPU-only 构建与所有后端消费同一命令的验证 |

以上是计划设定的验证目标，已测收益与代价见 P4，不承诺整应用提升某个百分比。布局、文本排版、GPU 提交等待可能仍是主要成本。正确的直通与预乘实现应产生基本相同的画面；8 位量化和舍入差异需单独审查。

本轮不包含：

- 替换 Skia、接入 Impeller/Graphite、新增 Vulkan/Metal/D3D 后端或移动端支持。
- 改变 Theme、控件几何、字体排版、阴影核、图片采样方式或圆角覆盖率算法。
- 线性光空间合成、广色域/HDR、RGBA16F、LCD 子像素文字 AA。
- 新增分组透明度、离屏图层或重新定义重合裁剪边界的面积求交算法。
- 在同一批次叠加 SIMD、并行光栅、分块渲染等优化；先获得可解释的标量基线。

预乘 alpha 与具体引擎无绑定关系。Impeller 的官方混合说明也要求混合源颜色预乘，参见第 11 节；本项不以 Flutter 的后端迁移作为 Lumen 更换后端的依据。

## 2. 迁移前实现核对（历史基点）

以下保留计划编写时的源码事实，不描述迁移后的实现。当前模式契约见第 3 节及迁移说明；旧注释、测试数量和历史性能报告不能代替实时源码复核。

| 位置 | 迁移前行为 | 迁移关注点 |
| --- | --- | --- |
| [renderer.h](../include/lumen/render/renderer.h)：`PixelBuffer` | 仅有宽、高、RGBA8 字节；无 alpha 格式标记 | 同一类型承载直通图片和不同后端的帧结果 |
| [cpu_renderer.cpp](../src/render/cpu_renderer.cpp)：`blendPixel` | 圆角覆盖率已经只乘源 alpha；source-over 根据目标 alpha 计算并除以 `outA`，保存直通 RGB | 当前不能直接认定仍有“ClipRounded 把源 RGB 变暗”的缺陷；本项是存储与混合约定迁移 |
| 同文件：`blendCoveragePixel`、`fillSpan` | 字形/形状 coverage 折入 alpha；不透明 span 有批量写入快速路径 | 保留快速路径，区分直通颜色输入与预乘图片输入 |
| 同文件：`beginFrame`、`endFrame`、`submit` | 双缓冲交换，Preserve 同步上一轮 damage 后清理本轮区域 | 格式、清屏颜色和前后缓冲状态必须一起维护 |
| 同文件：`registerImage`、`UploadImage`、`drawImage` | 两个上传入口分别保存字节；最近邻采样后经 `Color::fromRGBA` 进入直通混合 | 上传入口统一归一化；预乘采样不能再走直通颜色解释 |
| [skia_renderer.cpp](../src/render/skia_renderer.cpp)：`beginFrame`、`endFrame` | 表面为 `kPremul_SkAlphaType`；读回沿用 surface 的 `imageInfo()` | 读回实际是预乘，不能因为注释中有 “straight into” 就解释成直通 alpha |
| Skia 光栅 `registerImage` 与 [skia_gpu_renderer.cpp](../src/render/skia_gpu_renderer.cpp) 的 `uploadImage` | 上传固定声明 `kUnpremul_SkAlphaType` | 改为按输入格式选择 Skia alpha type |
| [sdl3_window.cpp](../src/platform/sdl3_window.cpp)：`present` | 透明窗口无条件调用 `premultiplyRgbaInto`；软件 surface 与 SDL texture 两条路径呈现 | 必须识别已预乘数据，覆盖 CPU 与 Skia 光栅；Skia GPU 自行交换表面 |
| [resource_manager.cpp](../src/render/resource_manager.cpp)、[image_decode.cpp](../src/render/image_decode.cpp) | 解码产物为直通 RGBA；管理器保存 CPU 数据并生成上传命令 | 保留资源所有权和重上传机制，避免每帧转换 |
| [render_commands.cpp](../src/render/render_commands.cpp) | 当前版本为 v6；像素负载不携带 alpha 格式；解码仅接受当前版本 | 新格式需要版本化与明确旧记录读取策略 |
| [examples/gallery/main.cpp](../examples/gallery/main.cpp) | `--dump-frame` 直接写 `pixels().rgba` | 默认导出继续提供直通 RGBA，不能悄悄变成预乘文件 |
| [scene_bench.cpp](../benchmarks/scene_bench.cpp) | 现有 headless 场景统计构建、布局、绘制等，支持 CPU/Skia 光栅 | 没有真实窗口 present 测量，不能用它证明窗口转换已经加速 |

需要在 P0 单独复现 Skia 光栅读回预乘数据进入透明窗口后是否被再次预乘。源码边界已有不一致证据，但本计划不把未运行的窗口结果写成已确认的视觉故障。

P0 复查补充：Windows 软件窗口已读回复现 Skia 红色 128 被再次转换为 64，证据见 [P0 记录](perf-baselines/premultiplied-alpha-2026-09-20/P0.md)。此外，当前不透明 SDL texture 没有显式设置 blend，而固定 SDL 3.2.10 对含 alpha 格式默认 BLEND；因此第 3.5 节“不透明窗口忽略 alpha”是本次必须统一的目标契约，不能视为所有旧分支已满足的事实。P1 应显式设置提交混合方式，并增加半透明外部输入的回归。

## 3. 目标像素契约

### 3.1 数据模型

已在 `lumen::render` 引入 `enum class AlphaMode : std::uint8_t`，并在 `PixelBuffer` 原有字段尾部追加 `alphaMode`：

| 模式 | 语义 | 不变量 |
| --- | --- | --- |
| `Straight = 0` | RGB 与 alpha 独立；默认输入格式 | 允许 A=0 时携带非零 RGB；转换为预乘时归零 |
| `Premultiplied = 1` | RGB 已乘 alpha | 每个像素 `R/G/B <= A`；A=0 时 RGB=0 |
| `Opaque = 2` | 已证明所有像素 A=255，直通与预乘字节相同 | 是必须兑现的内容保证，不能只因窗口不透明就设置 |

- 默认 `Straight`，保持应用手工构造图片的含义。追加字段尽量保留旧聚合初始化的源码兼容；结构体布局改变意味着使用方需要重编译，不承诺旧二进制 ABI。
- `core::Color`、Theme token 和绘制命令中的颜色参数始终是直通 RGBA；不把预乘颜色伪装成普通 `Color` 传递。
- CPU 前后帧缓冲、CPU 图片绘制缓存采用 `Premultiplied` 或已验证的 `Opaque`；Skia 帧读回准确标记对应模式。
- `pixels()` 返回渲染器实际格式，读取 `.rgba` 的调用方必须检查模式；需要直通数据时显式转换，禁止在每次 `pixels()` 调用或每帧 `endFrame()` 中隐式生成直通副本。
- `PixelBuffer::operator==` 包含模式。相同字节但不同格式不能被当作相同资源或相同帧语义。
- `Opaque` 由创建者证明：例如 A=255 全量清屏后，仅执行保留不透明性的 source-over 操作。Preserve 必须继承有效证明；不能从局部区域推断整帧。无法证明时使用 `Premultiplied`，不为猜测不透明性增加每帧全图扫描。

`Opaque` 用来保持不透明窗口快速路径，避免迁移之后因无法判断 alpha 而对每帧反预乘。文件解码、第三方图片声明该模式时须在接纳边界验证一次全部 alpha。

### 3.2 转换与验证

- 统一转换帮助函数提供直通→预乘、预乘→直通，以及可复用目标缓冲的拷贝版；名称沿用/扩展现有 `premultiplyRgbaInPlace`、`premultiplyRgbaInto` 风格。
- 转换以模式为依据，重复预乘必须保持字节不变；转换完成同步标记。`Opaque` 无需算术转换，保持其证明。
- 就地版和拷贝版明确支持源/目标为同一对象；错误输入返回明确结果，不能留下已更新模式但未转换完字节的缓冲。
- 检查尺寸、乘法溢出、字节长度和枚举值；外部预乘数据违反 `RGB <= A` 时拒绝，不通过静默钳位掩盖格式标错。
- 大缓冲的逐像素不变量检查放在解码、注册/上传、反序列化等接纳边界；内部已验证缓存的每次 draw/present 不重复全量检查。调试断言不得进入 Release 稳态热路径。
- `present` 每帧只做结构和枚举检查，内容不变量由帧生产者保证；外部调用方在生成/修改数据后使用公共验证函数。不能一面要求呈现零扫描，一面在该入口默认逐像素验证。
- A=0 的反预乘输出固定透明黑；A>0 时四舍五入并钳位。8 位低 alpha 的 RGB 信息会丢失，不要求 `Straight -> Premultiplied -> Straight` 与原字节完全相同。

### 3.3 混合与覆盖率

用归一化值表示，`Ps/Pd` 为预乘 RGB，`As/Ad` 为 alpha：

```text
Pout = Ps + Pd * (1 - As)
Aout = As + Ad * (1 - As)
```

整数实现统一采用有足够位宽的中间值，并集中定义 `mul255(x, a) = (x * a + 127) / 255`。不再在逐像素 source-over 中除以可变的 `Aout`。测试中另写独立参考模型，不直接调用生产混合函数计算期望值。

覆盖率分两种入口：

1. **直通 `Color` 绘制**：沿现有约定把形状/字形覆盖率和 clip 覆盖率折入有效 alpha，再由源 RGB 与有效 alpha 生成预乘源颜色。颜色值本身不预先变暗。
2. **预乘图片采样**：图片已带 alpha；额外 coverage 同时缩放预乘 RGB 和 alpha，再进入预乘混合，不能再把图片 RGB 乘一遍其自身 alpha。

嵌套 `ClipRounded` 仍沿用 `roundedCoverage` 的求交规则；形状 AA 与 clip 的覆盖率组合、SDF 几何和采样位置不在本次改变。发现重复几何门控等其他问题时另列复现，不能把它混入格式迁移并统一刷新哈希。

保留 A=0 跳过、A=255 覆写、不受部分覆盖率影响的不透明 span 批量填充。清屏色须先转换为内部格式，完整清屏和 damage 清屏使用相同字节。

### 3.4 资源、缓存与后端

| 边界 | 目标行为 |
| --- | --- |
| 图片文件解码 | PNG 等及现有 `.lumenrgba` 均显式产出 `Straight`；原始文件格式不变 |
| `ResourceManager` CPU 资源 | 保留输入模式，保存一份可供设备重建重上传的数据；常规解码资源保持直通 |
| CPU `registerImage` / 命令 `UploadImage` | 共用校验与归一化路径，进入绘制缓存时转换一次；已预乘输入直接保留 |
| CPU 图片绘制 | 读取预乘缓存，经专用预乘入口混合；保持最近邻采样 |
| Skia 光栅/GPU 上传 | 按模式映射 `kUnpremul`、`kPremul`、`kOpaque`，不能固定声明一种模式 |
| Skia 光栅读回 | 标记预乘；仅在能证明全帧 A=255 时标记 `Opaque`；不每帧反预乘 |
| 重上传/同 ID 覆盖/设备恢复 | 模式随字节保留，旧缓存同步失效；不能仅替换标记或继续读旧副本 |
| 截图/原始 RGBA 导出 | 在导出动作发生时转换成直通，保留现有默认语义，元数据写明 `alpha_mode=straight` |

资源管理器的直通副本与后端预乘副本承担不同职责。不得把现有两级所有权偷换为共享可写缓冲，也不因此默认增加第三份全量副本。转换后的图片缓存大小仍按 RGBA8 计算，额外临时峰值、缓存命中和卸载后释放情况纳入测量。

### 3.5 窗口呈现

`PlatformWindow::present` 按输入模式和窗口属性选择路径：

| 输入 | 透明窗口 | 不透明窗口 |
| --- | --- | --- |
| `Straight` | 为兼容外部输入转换一次，再提交；允许复用 scratch | 保持现有 RGB 字节解释 |
| `Premultiplied` | 直接提交，零 alpha 转换、零转换副本 | 兼容路径显式反预乘再提交，保持旧的“忽略 alpha、显示直通 RGB”含义 |
| `Opaque` | 直接提交 | 直接提交 |

- 框架正常的不透明帧通过有效的 `Opaque` 证明走快速路径；不能把预乘半透明 RGB 直接交给忽略 alpha 的窗口，否则会变暗。上表兼容分支的成本必须出现在基准报告中。
- 透明窗口仍保持清屏 alpha=0、SDL surface/texture 的 blend mode 与输入格式匹配；同时覆盖 `softwarePresentation` 和 SDL renderer 两条路径。
- 正常透明帧的 scratch 不应被分配或持续保留为等尺寸副本；曾走兼容分支后回到直接路径，需在切换点释放不再需要的大副本，避免反复分配。
- “直接提交”仅指省掉 Lumen 的格式转换副本，不代表 SDL texture 上传、blit 或系统合成器零复制。
- 保留 `PresentResult` 错误传播、resize surface 重建和 GPU 故障回退顺序。共享窗口呈现层不能继续假定输入总来自 `CpuRenderer`。

### 3.6 序列化、哈希和兼容性

**命令记录**：写入 v7；alpha 模式字段采用固定宽度和固定枚举值。新读取器支持本次迁移前的 v6，把其图片负载解释为 `Straight`；不扩展到更早版本。未知版本、非法模式、截断和非法像素负载均拒绝，失败不修改调用方原有输出命令列表。旧读取器拒绝 v7，当前没有 v6 写入器，详见迁移说明。

新像素字段顺序为 `width:u32, height:u32, alphaMode:u32, byteCount:u32, bytes`，沿用既有整数编码顺序。内存枚举为一字节不影响磁盘固定宽度；v6 读取分支跳过不存在的模式字段。按既有全字段记录方式，无图片命令的空负载也写出默认 `Straight`，不依赖 C++ 结构体布局序列化。

**原始图片文件**：`.lumenrgba` 的 `LUMENRGBA` 头和直通负载语义保持不变；写出前转换，不把预乘图片塞进旧格式。

**帧哈希**：本轮保留 `frameHash` 的“宽高+原始字节”算法，避免因为给哈希增加枚举而使所有场景无条件变化。报告另外携带 `alpha_mode`；不同模式的裸哈希不能证明语义相同。帧相等性测试必须检查模式，或先转换为统一比较表示。

**视觉比较**：先把两端转为共同的预乘表示，并分别合成到固定黑底、白底后比较。不要仅把低 alpha 像素反预乘再直接比较 RGB，否则量化误差会被放大。透明黑归一化、存储模式变化和真实视觉变化分别记录。

**历史基线**：[M0 基线](perf-baselines/README.md)与已有 JSON 保留。建立本次迁移的独立 before/after 报告和哈希映射；未解释的变化阻塞该阶段。只有审查通过的预乘表示/舍入差异可更新相关测试期望。不得用本计划批量覆盖历史证据或全局放宽视觉容差。

## 4. 实施顺序与任务

执行顺序为 `P0 -> P1 -> P2 -> P3 -> P4 -> P5`。每阶段完成后先 review、修复确认的问题、运行对应检查并记录证据，再进入下一阶段。阶段应保持可构建、可测试；任何临时兼容分支必须有明确删除阶段。

### P0：冻结现状、复现与基准

- [x] P0.1 记录实际提交、工作树差异、工具链、依赖和构建选项；隔离并保留其他控件任务的未提交工作。
- [x] P0.2 按第 2 节复查全部像素生产者/消费者；确认当前 source-over、coverage、Skia 读回与窗口路径。
- [x] P0.3 建立透明底单色边缘、半透明叠层、图片再上传和 Skia 透明呈现的最小复现；建立独立目标算术参考模型以冻结量化误差规则，记录当前通过项和既有缺陷。
- [x] P0.4 先补可在迁移前后复用的测量场景和转换计数，再采集第 6 节基线；测量工具改动独立于算法迁移。
- [x] P0.5 保存现有固定场景和 Gallery 样本的原始帧、模式说明、黑/白底合成图及哈希。

**出口**：有可重复的 before 报告、窗口转换成本、复现记录和运行环境；未把历史 CPU/Skia 差异混作本次新增回归。若总帧成本主要在其他环节，记录优先级判断，不编造速度收益。

### P1：建立格式边界，先使所有消费者理解模式

- [x] P1.1 增加 `AlphaMode`、字段、校验与幂等转换；显式 `Opaque` 证明规则写进公共接口注释。
- [x] P1.2 所有图片接纳点、资源复制、命令上传保留模式；CPU 在本阶段仍可把输入转换为当前直通内部格式，迁移分支仅保留到 P2。
- [x] P1.3 Skia 上传按模式选择 alpha type，读回声明实际格式；提前修正 SDL 格式分派，避免只标记输出却仍二次预乘。
- [x] P1.4 完成窗口第 3.5 节矩阵、默认直通导出适配、新命令版本与 v6 读取；更新调用方和头文件契约。
- [x] P1.5 增加模式与转换、序列化兼容、窗口路径选择、图片往返测试；CPU 仍保存直通的事实准确记录。

**出口**：生产者与消费者可同时处理两种有 alpha 的表示，默认手工图片兼容；CPU 旧算法保持基点行为（P0 已刻画的通道回绕在 P2 修复），Skia 读回不会因新标记被重复预乘。此阶段不得提前宣称 CPU 渲染已迁移。

### P2：迁移 CPU 帧缓冲、混合与图片缓存

- [x] P2.1 完整清屏与局部清屏写入预乘颜色，初始化前后缓冲的模式和不透明性证明。
- [x] P2.2 重写公共 source-over，分开直通 `Color` 与预乘图片入口；保留透明跳过、不透明覆写和 span 快速路径。
- [x] P2.3 检查 fill/stroke、系统字形/占位字形、图标、阴影、图片与圆角 clip 的全部入口；按第 3.3 节折算 coverage。
- [x] P2.4 两个图片上传入口统一成一次归一化；同 ID 覆盖、卸载、重上传和空/非法图片行为有回归。
- [x] P2.5 前后缓冲交换、damage 同步、resize/DPI/清屏色变化与 Preserve 退回全量路径携带正确模式；必要时失效旧帧。
- [x] P2.6 删除 P1 的 CPU 直通内部兼容分支；`pixels()` 返回真实预乘/不透明帧，不做隐藏全图转换。

**出口**：第 5 节 CPU 算术与生命周期用例通过，全量/局部/即时/命令回放输出在相同格式下严格一致；正常透明呈现走直接路径。运行完整 CPU CTest。

### P3：联调 Skia、桌面窗口与导出

- [x] P3.1 CPU 与 Skia 光栅使用相同格式契约，跨后端图片传递及同命令回放正确。
- [x] P3.2 Skia GPU 仅调整资源格式边界和必要测试，保留 Ganesh/GL 管线及资源生命周期；不引入新的 GPU 每帧读回。
- [x] P3.3 三桌面平台逐路径源码核对；Windows 软件 surface、SDL texture、resize/最小化恢复及兼容输入实测。Linux/macOS 与实际合成器视觉仍待平台验证，见 P3 记录。
- [x] P3.4 GPU 初始化失败/运行时失败后回退，模式随资源重上传正确恢复；状态、文字与呈现错误处理保持已有契约。
- [x] P3.5 Gallery 默认 raw 导出仍为直通；PNG 转换与元数据一致，低 alpha 精度限制有测试说明。
- [x] P3.6 直接路径不保留呈现转换副本，兼容路径正确复用/释放临时存储，验证不透明帧快速路径。

**出口**：CPU、Skia 光栅和 GPU 配置均可构建并通过相关完整测试；有实际窗口和导出证据。未运行的平台或硬件路径标为待验证，不能视为三平台完成。

### P4：视觉回归审查与性能验收

- [x] P4.1 执行第 5 节矩阵，为像素差异生成模式、alpha、黑底/白底合成结果和空间分布报告。
- [x] P4.2 为每个需要更新的哈希写明原因与对应样本；无关几何/颜色/字体变化逐项排除。
- [x] P4.3 同机交错运行 before/after，按第 6 节比较提交、呈现准备和端到端耗时、内存及分配。
- [x] P4.4 检查不透明场景没有引入隐式反预乘或每帧模式扫描；图片转换没有移到 draw 热路径。
- [x] P4.5 完成一次覆盖源数据→缓存→回放→呈现→导出的全链路 review，修复后只重跑受影响检查及要求的全量门槛。

**出口**：正确性全部通过；性能报告区分已测收益、无显著变化和未测项目，满足第 6.3 节。哈希更新有依据，不能以新基准掩盖回归。

### P5：文档、兼容说明与交付

- [x] P5.1 同步 renderer/CPU/Skia/资源/平台接口注释，更新标题栏与视觉系统中的旧直通呈现说明。
- [x] P5.2 更新构建命令、截图格式说明、基准元数据和支持矩阵中的相关契约；历史记录采用追加解释，保留旧基线。
- [x] P5.3 按第 9 节完成交付记录，记录重编译要求、`pixels()` 语义变化、命令版本及 v6 兼容规则。
- [x] P5.4 全部出口满足后，向自用路线图追加完成记录；未验证的平台保留待办。最终代码与文档统一 review。

**出口**：调用方有明确迁移方式，每一项性能/平台结论可定位到证据，任务勾选与实际实现一致。

## 5. 正确性与视觉验收矩阵

### 5.1 必须自动化的行为

| 类别 | 最低覆盖 | 断言 |
| --- | --- | --- |
| 格式转换 | A=0/1/2/127/128/254/255；非零透明 RGB；重复转换；原地别名 | 幂等、零 alpha 归零、模式同步；所有通道预乘结果合法 |
| 数值合成 | 透明/半透明/不透明目标，彩色源，连续多层 source-over | 与独立整数参考模型一致；另用高精度模型报告量化误差 |
| 不透明证明 | 不透明清屏、局部清屏、旧透明帧 Preserve、清屏色切换 | 不误标 `Opaque`；保留正确的快速路径 |
| coverage | fill/stroke/文字/图标/阴影；单层和嵌套圆角 clip；图片再裁剪 | 覆盖率不重复乘源 alpha；维持现有几何规则；所有预乘像素 RGB<=A |
| 图片生命周期 | 两种输入表示、Opaque 输入、CPU 帧再注册为图片、同 ID 替换、卸载/恢复 | 无二次预乘、无旧缓存、原始调用方数据不被意外修改 |
| 帧生命周期 | 即时 vs submit，全量 vs Preserve，多轮交错 damage，resize/DPI、双缓冲 | 同一 CPU 实现及模式下逐字节一致；首帧完成后 `pixels()` 始终是上次完成帧；首个完成帧前保留既有即时缓冲读取语义 |
| 记录回放 | 新版本各模式 roundtrip、v6 fixture、非法模式、损坏长度、未知版本 | 模式与字节保存；失败不部分覆盖输出；回放等价 |
| 窗口 | 第 3.5 节六种组合；重复呈现、surface/texture 重建、返回失败 | 直接路径不转换；兼容路径最多一次；颜色和 alpha 按契约解释 |
| 跨后端 | CPU/Skia 光栅同场景、GPU 图片上传与故障回退 | 同一表示比较；透明源图片不变暗；无新增每帧 GPU 读回 |
| 导出 | Gallery 两类 dump 路径、raw→PNG、透明黑与低 alpha | 默认直通、模式元数据准确；重新加载后视觉一致 |

转换的单通道 `(color, alpha)` 输入空间可穷举，或使用等价的完整参数化验证。多层合成必须固定绘制顺序；不能要求 8 位算术在重新分组后仍逐字节相同。

推荐落点：现有 [render_tests.cpp](../tests/render_tests.cpp)、[render_command_tests.cpp](../tests/render_command_tests.cpp)、[resource_manager_tests.cpp](../tests/resource_manager_tests.cpp)、[skia_smoke_tests.cpp](../tests/skia_smoke_tests.cpp)、[gpu_smoke_tests.cpp](../tests/gpu_smoke_tests.cpp)、[counter_fallback_tests.cpp](../tests/counter_fallback_tests.cpp)、[platform_smoke_tests.cpp](../tests/platform_smoke_tests.cpp)。转换/参考模型用例较多时可新增专用测试文件并接入 `tests/CMakeLists.txt`；名称与本节契约对应。

### 5.2 视觉样本与容差

- 基础样本：透明底纯红/纯白圆角、部分透明叠层、渐变 alpha 图片、软阴影与抗锯齿文字；在透明、黑、白、棋盘背景上观察。
- Gallery：标题栏 close hover/pressed、普通/最大化四角、Spin 圆角门控、菜单阴影、outline/ghost 控件；Core Dark 与 light、DPI=1/1.25/2。
- 精确矩形和可计算颜色样本使用独立参考值；不受迁移影响的纯不透明覆写必须精确相等。
- CPU 旧直通结果与新预乘结果先统一表示；先记录量化差，再确定具体场景可接受的最大误差和超差像素比例。P0 冻结规则后不得因实现失败而扩大容差。
- P0 已冻结普通 CPU 迁移比较为预乘 RGB 最大差 4、alpha 完全相等、超差比例 0；另独立复现旧直通通道 256→0 回绕。`images`/`upload` 的错误旧帧不能作颜色 golden，P2/P4 必须与 P0 的 `alpha_image_reference.py` 目标参考逐字节相等，见 [P0 记录](perf-baselines/premultiplied-alpha-2026-09-20/P0.md)。此项不改变采样或几何规则，也不扩大普通场景容差。
- CPU/Skia 的几何 AA、系统字体和阴影实现可能不同：继续使用有来源的场景容差，分别检查 alpha 和黑/白底合成结果，不要求跨后端整帧哈希相同。
- 任何持续暗边/亮边、透明角变黑、边框缺失、布局或字形位置漂移均为失败；哈希变化本身不能证明画面正确或错误。

## 6. 性能基线与验收

### 6.1 测量场景

| 场景 | 工具与状态 | 重点指标 |
| --- | --- | --- |
| card-grid、text-heavy、resource-upload | `lumen-scene-bench`；保持旧场景内容和参数 | paint/构建/layout 的 p50/p95、分配和命令数；该工具未导出上传计时 |
| 透明半透明叠层、裁剪边缘、图片叠加 | P0 新增 `lumen-alpha-bench --scenario layers/edges/images`（分别运行） | 单独渲染提交耗时、物理尺寸、输出模式 |
| 图片一次上传后重复绘制 vs 每帧上传 | `lumen-alpha-bench --scenario images` / `--scenario upload` | 转换只发生在接纳边界；冷启动与稳态成本分别报告 |
| 1080p/4K 透明窗口 | `lumen-alpha-bench --scenario probe --present software/texture`（分别运行） | 呈现准备、SDL 提交、整帧 wall time、scratch 容量与复制字节 |
| 不透明窗口与兼容输入 | 同一窗口驱动，第 3.5 节矩阵 | Opaque 快速路径无转换；特殊兼容路径成本明确 |

P0 新增测量只引入观测和场景，保持迁移前绘制算法。before/after 使用同一套驱动与输入，避免算法版本与测量口径同时变化。

### 6.2 方法与报告

- 使用 Release；相同机器、编译器/选项、依赖、后端、字体、尺寸、DPI、场景、刷新/vsync 设置。CPU-only 与带 Skia 构建的结果不直接交叉比较。
- 既有 canonical 场景使用 warmup=30、frames=300；至少三组独立运行，按 before/after 交错执行，报告每组 p50/p95 和组间波动，不只挑最快一次。
- 新场景在 P0 固定参数，1080p 与 4K 独立记录。真实窗口提交的 vsync/合成器等待与 Lumen 格式转换准备耗时分开报告。
- 新增 `alpha_mode`、物理像素尺寸、clear alpha、转换次数、转换/复制字节数、scratch 容量、测量范围等元数据；现有 JSON 字段含义保持。
- 记录 `submit` 与 `present` 是否已包含在某个阶段计时内，禁止把重叠指标直接相加形成“总耗时”。headless 的 total 不能称为真实上屏耗时。
- 原始报告、帧与截图写入忽略的构建目录；审查后将精简 before/after JSON 与说明归档到 `docs/perf-baselines/` 的独立子目录，不覆盖旧文件。
- 若实施时存在其他未提交改动，报告包含提交 ID 与差异清单；baseline/after 只允许在本项被测因素上不同。

### 6.3 出口标准

1. **正确性先通过**：第 5 节及完整 CTest 为前置；错误输出不能用性能收益抵消。
2. **结构目标可计数**：普通预乘透明帧的呈现 alpha 转换次数=0、转换副本字节=0；稳定图片不每帧归一化；CPU source-over 没有逐通道除以 `Aout`；不透明标准路径不退化为每帧反预乘。
3. **旧场景无显著回退**：沿用仓库 10% 回归门槛，适用阶段和 p95 的恶化超过 10% 必须调查；对微小耗时同时报告绝对值与噪声。历史归档继续检查，本机配对结果用于本次归因，两者用途分开。
4. **收益按实测标注**：至少报告透明混合与呈现准备两项 before/after；不预设“提升 20%”等保证。结果落在噪声内时标为无显著提速，不扩写为整应用性能提升。
5. **内存证据**：省去一份 RGBA8 转换副本理论为 `width * height * 4` 字节（1080p 约 7.91 MiB，4K 约 31.64 MiB），报告实际峰值/稳态差；不把它当作整个渲染器内存降幅。
6. **决策留痕**：若没有可测速度收益，但格式正确性或内存收益成立，可如实交付对应收益；出现无法解释的回退则该阶段保持未完成，不通过刷新基线宣布优化成功。

## 7. 构建与验证命令

命令从仓库根目录运行，实际依赖要求以 [统一命令表](build-commands.md) 为准。下面是独立目录的复现模板；本轮实际使用目录、工具链、阶段运行结果见第 9 节及各阶段报告，不把模板当作执行日志。

### 7.1 CPU Debug 正确性

```sh
cmake -S . -B build-alpha-cpu -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON -DLUMEN_ENABLE_SKIA=OFF -DLUMEN_ENABLE_GPU=OFF
cmake --build build-alpha-cpu --config Debug
ctest --test-dir build-alpha-cpu --output-on-failure -C Debug
```

### 7.2 CPU Release 与现有基准

```sh
cmake -S . -B build-alpha-bench -DCMAKE_BUILD_TYPE=Release -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_BENCHMARKS=ON -DLUMEN_ENABLE_SKIA=OFF -DLUMEN_ENABLE_GPU=OFF
cmake --build build-alpha-bench --config Release
ctest --test-dir build-alpha-bench --output-on-failure -C Release
```

以下为单配置生成器的可执行路径；Windows 多配置生成器在 `benchmarks/` 后增加 `Release/`，扩展名为 `.exe`：

```sh
./build-alpha-bench/benchmarks/lumen-scene-bench --backend cpu --scenario card-grid-6x8-1080p --warmup 30 --frames 300 --json
./build-alpha-bench/benchmarks/lumen-scene-bench --backend cpu --scenario text-heavy --warmup 30 --frames 300 --json
./build-alpha-bench/benchmarks/lumen-scene-bench --backend cpu --scenario resource-upload --warmup 30 --frames 300 --json
```

按第 6.2 节分别保存每次输出。P0 已增加 `lumen-alpha-bench`、`lumen-alpha-gallery` 和三组/配对采样驱动，P4 补充呈现探针与汇总；具体场景、参数、计时范围见 [统一命令表 §4](build-commands.md#4-预乘-alpha-迁移的固定测量入口p02026-09-20)，运行记录见 [P0](perf-baselines/premultiplied-alpha-2026-09-20/P0.md) 与 [P4](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)。

### 7.3 Skia 光栅与 GPU

```sh
cmake -S . -B build-alpha-skia -DCMAKE_BUILD_TYPE=Release -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON -DLUMEN_BUILD_BENCHMARKS=ON -DLUMEN_ENABLE_SKIA=ON -DLUMEN_ENABLE_GPU=OFF
cmake --build build-alpha-skia --config Release
ctest --test-dir build-alpha-skia --output-on-failure -C Release

cmake -S . -B build-alpha-gpu -DCMAKE_BUILD_TYPE=Release -DLUMEN_BUILD_TESTS=ON -DLUMEN_BUILD_EXAMPLES=ON -DLUMEN_ENABLE_SKIA=ON -DLUMEN_ENABLE_GPU=ON
cmake --build build-alpha-gpu --config Release
ctest --test-dir build-alpha-gpu --output-on-failure -C Release
```

Skia 预编译包在 Windows 使用 Release；需要时按统一命令表提供 `LUMEN_SKIA_ROOT`。GPU 测试的 skip、软件 GPU、真实硬件与平台窗口支持必须分别记录。没有可用图形环境的 headless 通过不等于透明窗口验收通过。

### 7.4 视觉与窗口验证

复用 [Gallery 固定样本命令](build-commands.md)及其 raw 导出；迁移期间核对导出转换。使用实际支持的平台桌面会话测试软件呈现与 SDL renderer。GPU 故障路径复用 [counter_gpu_smoke.cmake](../tests/counter_gpu_smoke.cmake)及已有 CTest 注册，Linux 注入路径与其他平台人工/自动窗口证据分别记录，不虚构跨平台通用故障注入命令。

每个实施阶段先跑相关定向用例，提交前跑完整 `ctest --output-on-failure`；阶段结束后的重复验证以新改动或未解决问题为依据。实际测试数量以当次发现结果为准。

## 8. 预计代码与文档落点

| 组 | 文件/目录 | 职责 |
| --- | --- | --- |
| 公共像素契约 | `include/lumen/render/renderer.h`、`src/render/renderer.cpp` | 模式、转换、验证、哈希说明 |
| CPU | `include/lumen/render/cpu_renderer.h`、`src/render/cpu_renderer.cpp` | 帧格式、混合、coverage、图片缓存、Preserve |
| 命令与资源 | `include/lumen/render/render_commands.h`、`src/render/render_commands.cpp`、`resource_manager.*`、`image_decode.cpp` | 模式传播、版本兼容、接纳边界 |
| Skia | `include/lumen/render/skia_renderer.h`、`src/render/skia_renderer.cpp`、`src/render/skia_gpu_renderer.cpp` | 上传/读回 alpha type 与跨后端契约 |
| 平台与调用方 | `include/lumen/platform/platform_window.h`、`src/platform/sdl3_window.cpp`、`app_shell.*`、counter/gallery 示例 | 呈现分派、像素读取说明、导出与回退 |
| 验证 | `tests/`、`benchmarks/scene_bench.cpp`、相应 CMake 清单 | 独立参考模型、回归、透明场景、窗口测量 |
| 设计与交付 | `lumen-titlebar-design.md` 第 16 节、`lumen-visual-system-design.md` 圆角呈现说明、`build-commands.md`、`perf-baselines/README.md`、`support-matrix.md` | 当前实现说明、格式迁移与验证能力 |
| 完成记录 | `lumen-self-use-roadmap.md` 第 10 节、本文件第 9 节 | 验证后追加结果；计划阶段不提前标记完成 |

这是影响面清单，不要求为了满足文件数量而改动每个文件。实施时以调用链为准，保留与本项无关的用户改动。控件设计和 HTML 稿默认不改变；若发现必须改变外观或行为，应先记录原因，并按仓库规则同步对应设计文档与稿件。

## 9. 阶段交付记录

每阶段完成时追加一条；源码实现、正确性验证、性能测量和平台窗口验收分开记录。

```text
阶段 / 日期：
基点提交 / 当前提交 / 未提交差异：
实现内容与接口变化：
格式与不透明性证明检查：
review 发现及修复：
测试命令、配置、通过/失败/跳过：
平台与真实窗口/GPU 覆盖：
before/after 报告路径与测量参数：
p50/p95、复制/转换字节、临时内存结果：
哈希变化清单、原因与视觉对照：
未运行事项 / 已知限制 / 后续依赖：
本阶段出口是否满足：
```

### P0 / 2026-09-20

基点 `e06f3d9`；本阶段增加测量与参考工具，未迁移生产混合算法。完整环境、review 修复项、三组逐项结果和原始帧索引见 [P0 记录](perf-baselines/premultiplied-alpha-2026-09-20/P0.md)。CPU-only Release 全量 CTest 741/741、Skia raster Release 751/751；各 45 次固定基准运行，保存 62 张原始帧及四种合成/alpha 图。透明呈现每帧转换一次，1080p/4K 副本分别为 8,294,400 / 33,177,600 字节。Skia 二次预乘、旧 CPU 直通通道回绕已独立复现；后者冻结正确目标参考，不作为兼容 golden。

阶段出口满足，before 构建不再重编。CPU Debug、GPU、Linux/macOS 与原生透明合成验收留到后续阶段；P1–P5 未完成，无性能收益结论。

### P1 / 2026-09-20

基点 `a28c26f`；增加 AlphaMode、校验/转换、所有资源与呈现消费者、v7/v6 读写及直通导出适配，CPU 暂时仍为直通。完整 review 与证据见 [P1 记录](perf-baselines/premultiplied-alpha-2026-09-20/P1.md)。CPU Debug / Release 均 749/749，Skia raster Release 760/760，GPU Release 770/770（无跳过），真实 GL 图片模式回归通过。GPU 门槛暴露并修复旧 List 测试未显式开启焦点环的夹具遗漏，无控件外观变更。11 个固定帧哈希与 P0 一致；Windows Skia software 红色由错误 64 恢复到 128，software/texture 均零 Lumen 转换与 scratch。阶段出口满足，P2–P5 继续；本阶段探针不作为性能收益证据。

### P2 / 2026-09-20

基点 `ea8a0c3`；CPU 帧、清屏、source-over 与图片缓存已统一为预乘/不透明格式，删除 P1 临时直通桥接。review 修复配置失效与已发布帧状态混用、damage 同步的模式继承。完整证据见 [P2 记录](perf-baselines/premultiplied-alpha-2026-09-20/P2.md)。CPU Debug / Release 各 754/754，Skia raster 765/765，GPU 775/775，无跳过。图片两场景逐字节符合 P0 独立参考，42 张 Gallery 的 alpha 精确相等、RGB 无超差；正常 CPU 窗口四条路径转换/副本/scratch 全部为零。P2 出口满足，P3–P5 继续；尚无正式性能收益结论。

### P3 / 2026-09-20

基点 `d4993ef`；Skia Preserve 的半透明 clear 重复叠加先复现再修复，CPU/Skia 帧作为图片及同命令回放等价。GPU 三种模式资源经历真实上下文销毁重建与 CPU 重上传后颜色正确，无生产每帧读回。CPU Debug / Release 各 755/755，Skia raster 768/768，GPU 779/779，无跳过；日志 `build/ctest-alpha-p3.log`、`build-alpha-after/ctest-p3.log`、`build-alpha-skia-after/ctest-p3.log`、`build-gpu/ctest-alpha-p3.log`。Windows 实窗生命周期 584 断言通过，Gallery 两个导出路径保持 straight。完整证据与三桌面宿主限制见 [P3 记录](perf-baselines/premultiplied-alpha-2026-09-20/P3.md)。阶段出口满足；Windows 软件透明不支持、实际合成器视觉、Linux/macOS 及 Linux 运行时故障注入明确保留待验。

### P4 / 2026-09-20

基点 `a45ebd6`；修复 MSVC 非内联像素混合带来的额外开销，整数公式与像素结果不变。CPU Release / Debug 各 755/755、Skia raster 768/768、GPU 779/779，无跳过；最终日志均为各构建目录的 `ctest-p4-final.log`。62 个视觉样本 alpha 精确一致、普通 RGB 最大差 2，图片场景与 P0 独立参考逐字节相等；逐项模式/哈希及 310 张视图路径见 [P4 记录](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)。

CPU / Skia 各 90 次正式运行（45 对），保留全部单组超限，并追加 98 次对照与 12 次透明呈现诊断。CPU 叠层 submit p50/p95 的三组配对变化中位数 -41.15%/-43.07%；正常透明准备转换、复制和 scratch 均为零，1080p/4K 省去约 7.91/31.64 MiB 转换副本。CPU 边缘场景 p50 约 +5.74%，256×256 图片每次归一化约增加 0.24 ms；Skia 无稳定 headless 提速结论。所有时间、波动归因限制和平台待验项均保留，未改写 P0 容差、历史基线或控件 golden。阶段出口满足，继续 P5。

### P5 / 2026-09-20

基点 `316127a`；仅文档与注释收口，无生产行为变更。新增 [调用方迁移说明](lumen-alpha-migration.md)，同步构建命令、基线来源、支持矩阵、标题栏/视觉契约及自用路线图。公开结构需重编译，旧三字段聚合默认 Straight；CPU `pixels()` 返回真实预乘/不透明帧且无隐式转换，首帧完成前的即时读取例外明确保留；命令写 v7、读 v6/v7，旧程序拒绝 v7。

最终 review 修正 UploadImage 与解码实现中残留的直通后端注释、透明清屏即合成器支持的过强表述，以及计划里迁移前事实被误读为现状的时态。公共 renderer/CPU/Skia/资源/平台接口已逐项核对。格式帮助函数保留 Opaque、低 alpha 往返有损和 Straight 幂等时保留透明 RGB 均写明；未改控件设计数值或 HTML 外观。

CPU Release `build-alpha-after` / Debug `build` 各 755/755，Skia raster Release `build-alpha-skia-after` 768/768，GPU Release `build-gpu` 779/779，无跳过。各配置重新构建后运行 `ctest --test-dir <目录> --output-on-failure -C <配置> --parallel 6`，日志为对应目录的 `build-p5.log` / `ctest-p5.log`。性能仍引用 P4 的冻结测量，未因注释重建重采样或替换原 SHA；来源对应关系见 [基线说明](perf-baselines/README.md#4-预乘-alpha-迁移的独立配对记录)。

源码与文档统一 review、新增/更新链接与陈旧契约检查通过。P0–P5 出口满足，历史帧/性能报告和冻结容差保留；Windows software 逐像素透明不支持、Windows texture 合成器视觉、Linux/macOS 桌面及 Linux 运行时故障注入仍待验。GPU 只交付正确性证据，未声明性能收益；实际 4K drawable 不等于物理 4K 屏幕验收。

| 阶段 | 提交 |
| --- | --- |
| P0 | `a28c26f` |
| P1 | `ea8a0c3` |
| P2 | `d4993ef` |
| P3 | `a45ebd6` |
| P4 | `316127a` |
| P5 | 本记录所在的 `docs(render): 完成预乘 Alpha 迁移与交付文档` 提交 |

最终关闭清单：

- [x] P0–P5 出口均有记录，源码中的模式和文档一致。
- [x] 公共 `Color` 仍为直通；CPU 内部帧/缓存统一预乘；Opaque 声明可信。
- [x] 正常透明呈现无额外 alpha 转换；不透明正常路径无新增整帧反预乘。
- [x] 图片/命令/窗口/导出全链路无格式丢失，v6 兼容和未知版本拒绝有测试。
- [x] 全量与局部重绘、CPU/Skia/GPU、回退与真实窗口证据完整，未跑项明确保留。
- [x] 视觉差异经审查，哈希更新有逐项依据，旧性能基线保留。
- [x] 性能收益有实测，未显著变化与不适用项如实标注。

## 10. 提交拆分与回滚

- 以阶段形成可构建的提交；P1 的生产者标记和消费者分派必须作为同一可用边界落地，不能发布“只增加标记”的中间状态。
- P2 的帧格式与混合公式一起切换，不能只回滚其一。P3/P4 的纠错若修改 P2 契约，回填本计划与相关测试。
- 回滚首先恢复格式生产/消费的一致性。P1 的多格式消费者与新版本读取能力可保留，以继续读已产生的记录；若回滚整个版本支持，应明确新记录将无法被旧程序读取。
- 既有磁盘原图与历史基线不作就地重写。一次迁移不创建永久维护的双套 CPU 混合实现；性能对照使用独立构建产物。
- 提交遵循仓库 Conventional Commits，例如 `perf(render): 统一 CPU 预乘像素累积与呈现`；只包含本项改动。

## 11. 设计依据与参考

- [GUI 框架基线](lumen-gui-framework-plan.md)、[桌面 GPU 与性能计划](lumen-gui-framework-plan-v0.2.md)、[当前桌面范围](lumen-self-use-roadmap.md)。
- [视觉系统](lumen-visual-system-design.md)、[标题栏与透明窗口契约](lumen-titlebar-design.md)。P2 起 CPU 当前行为已为预乘；P0/P1 记录保留旧直通事实，后续现状说明按已完成阶段更新。
- [性能基线与比较规则](perf-baselines/README.md)、[统一构建与验证命令](build-commands.md)。
- [W3C Simple alpha compositing](https://www.w3.org/TR/compositing-1/#simplealphacompositing)：source-over 与预乘表示公式。
- [Skia alpha type](https://api.skia.org/SkAlphaType_8h.html)：直通、预乘和不透明的像素解释。
- [Impeller color blending](https://flutter.googlesource.com/mirrors/flutter.git/+/HEAD/docs/engine/impeller/docs/blending.md)：混合使用预乘输入的设计说明；引擎路线变化不影响本计划的基础像素契约。

外部资料作为原理依据，实施细节和验收以本仓库固定依赖与实时源码为准；本计划不引入任何新依赖。
