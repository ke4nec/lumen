# 预乘 alpha 调用方迁移说明

2026-09-20，预乘 alpha 计划 P0–P5 交付。接口与源码以
[renderer.h](../include/lumen/render/renderer.h) 为准；设计契约见
[实施计划 §3](lumen-premultiplied-alpha-rendering-plan.md#3-目标像素契约)，
实测结果见 [P4 报告](perf-baselines/premultiplied-alpha-2026-09-20/P4.md)。

## 1. 需要调整的调用方

| 调用方式 | 迁移要求 |
| --- | --- |
| 设置 `Color`、Theme 或绘制命令颜色 | 仍传直通 RGBA，不要预先乘 alpha |
| `PixelBuffer{width, height, bytes}` | 三字段聚合仍默认 `Straight`；外部预乘数据必须显式带模式 |
| 读取 CPU/Skia 光栅 `pixels().rgba` | 同时读取 `alphaMode`；不能再假定 CPU 帧是直通 |
| 将帧注册为图片或放入 `UploadImage` | 复制整个 `PixelBuffer`，保留模式；不要只复制宽高与字节 |
| 输出 PNG、旧 raw 文件或 `.lumenrgba` | 在导出边界显式反预乘；Gallery 应用的两种 dump 路径已适配 |
| 持久化/回放命令 | 新写 v7，新程序读 v6/v7；旧程序无法读取 v7 |
| 链接预编译 Lumen 或跨库传递 `PixelBuffer` | 结构布局改变，所有二进制使用方必须重编译；不保证旧 ABI |

本次没有修改控件几何、Theme、阴影核、采样或裁剪覆盖率规则，也没有新增依赖。
裸字节哈希变化可能来自表示与量化；不能直接作为视觉回归判断。

## 2. 模式、校验与转换

内存枚举为 `enum class AlphaMode : std::uint8_t`，数值固定：

| 模式 | 数值 | 内容保证 |
| --- | ---: | --- |
| `Straight` | 0 | RGB 与 alpha 独立，A=0 可以有非零 RGB |
| `Premultiplied` | 1 | RGB 已乘 alpha，每个通道不大于 A，A=0 时 RGB=0 |
| `Opaque` | 2 | 每个像素 A=255；此时直通与预乘字节相同 |

`Opaque` 是像素内容的证明，不能因为窗口不透明就给半透明帧贴该标记。
框架用完整不透明清屏及保持不透明的 source-over 建立证明，局部更新继承有效状态。
外部数据使用默认的 `validatePixelBuffer(buffer)` 在接纳时检查内容；尺寸必须为正、
字节数精确等于宽×高×4、乘法不能溢出、枚举必须有效。非法预乘内容被拒绝，不会静默钳位。
CPU/Skia 注册、资源管理器和命令反序列化均按此约定接纳。

`PixelValidation::Structure` 只做 O(1) 的尺寸/长度/枚举检查，适用于生产者已经保证内容的
内部帧。`present()` 为避免每帧扫描采用此方式；外部调用方修改数据后必须先完成内容验证。

```cpp
#include "lumen/render/renderer.h"

bool prepareImage(lumen::render::PixelBuffer& image) {
    // image.alphaMode 必须先如实描述输入；转换幂等，已预乘输入不会再乘。
    return lumen::render::premultiplyRgbaInPlace(image);
}

bool exportStraight(const lumen::render::PixelBuffer& frame,
                    lumen::render::PixelBuffer& output) {
    if (!lumen::render::unpremultiplyRgbaInto(output, frame)) {
        return false; // 无效输入不改变 output。
    }
    // 帮助函数保留 Opaque 证明；A=255 的字节也是合法 straight。
    // 若文件格式要求明确的 straight 标记，可在成功后显式标记。
    output.alphaMode = lumen::render::AlphaMode::Straight;
    return true;
}
```

四个帮助函数 `premultiplyRgbaInPlace/Into`、`unpremultiplyRgbaInPlace/Into`
均返回 `bool`、按模式幂等，`Into` 支持源和目标为同一对象并复用目标存储。
返回 `false` 时目标不变；此约定不承诺吞掉内存分配异常。`Opaque` 原样保留。
真正执行预乘时 A=0 归零；反预乘的零 alpha 输出为透明黑。已是 `Straight` 的输入走幂等路径，
不会清掉其透明 RGB。8 位低 alpha 会丢失 RGB 精度，往返不保证还原原始字节。

## 3. 帧、图片与资源生命周期

CPU 累积缓冲和图片缓存为 `Premultiplied` 或 `Opaque`；source-over 使用统一整数舍入，
不再逐通道除以输出 alpha。普通 `Color` 绘制先把 coverage 折入有效 alpha 再生成预乘源；
图片额外 coverage 同时缩放 RGB 和 A。不要把预乘图片再包装为直通 `Color` 绘制。

CPU `pixels()` 读取不转换、不复制。首帧完成后返回上次 `endFrame()` 发布的帧，
绘制途中也如此；首个完成帧之前保留旧的即时缓冲读取语义。取得的是渲染器持有的引用，
需要跨后续绘制长期保存时应复制完整对象。Skia 光栅 `pixels()` 是上次完成帧的实际预乘/
不透明快照；GPU 仍直接提交，生产路径没有新增每帧读回。

文件解码继续产出 `Straight`，资源管理器保留原图模式及设备恢复所需数据。
CPU 的 `registerImage` 与 `UploadImage` 共用校验和一次归一化路径；重复绘制不重复转换。
Skia 光栅/GPU 按模式选择 `kUnpremul`、`kPremul` 或 `kOpaque`。同 ID 覆盖、卸载及重上传
必须携带模式和字节一起更新。CPU/Skia 的直接注册失败返回 0；资源管理器返回无效 handle。

正常解码资源在管理器保留直通副本、在 CPU 后端保存预乘缓存，二者承担恢复与绘制的不同职责。
256×256 straight 图每次接纳的 CPU 归一化 p50 实测增加约 0.24 ms；稳定资源只支付一次，
每帧重新注册会反复承担此成本。不要把上传计时排除在外的 submit 加速当作完整上传流程加速。

## 4. 窗口与导出

| 输入模式 | 透明窗口 | 不透明窗口 |
| --- | --- | --- |
| Straight | 兼容转换为预乘再提交 | 直接按 RGB 提交 |
| Premultiplied | 直接提交 | 兼容反预乘后按 RGB 提交 |
| Opaque | 直接提交 | 直接提交 |

兼容分支最多转换一次并复用 scratch；回到直接路径释放转换副本。正常框架帧走直接路径。
这只消除 Lumen 的格式转换，不代表 SDL texture 上传、surface blit 或合成器零复制。
`PresentResult::Ok` 只表示提交成功：Windows 原生 software surface 为 XRGB，不能实现逐像素透明。
Windows texture 合成器视觉、Linux/macOS 桌面及 Linux 运行时故障注入仍待实机验收，
详见 [支持矩阵](support-matrix.md) 和 [P3](perf-baselines/premultiplied-alpha-2026-09-20/P3.md)。

Gallery 应用 `--dump-frame`（headless 和固定 sample）继续导出 straight RGBA，并写明
`alpha_mode=straight`。`.lumenrgba` 继续为 `LUMENRGBA` 头及直通负载。
诊断 `lumen-alpha-gallery`、`lumen-scene-bench`、`lumen-alpha-bench` 保存实际模式，读取其 `.txt`
元数据；`alpha_frames.py` 可直接生成黑/白/棋盘合成图和 alpha 图。不要混用两种导出语义。

## 5. 命令版本与比较

`serializeCommands` 写 v7，像素字段依次为
`width:u32, height:u32, alphaMode:u32, byteCount:u32, bytes`，整数均为小端。
即使内存枚举只占一字节，磁盘字段仍是 u32；非图片命令的空像素字段默认 Straight。
这不是直接转储 C++ 结构体。无效像素负载使序列化返回空串。

`deserializeCommands` 读取 v6/v7；v6 不含模式，按 Straight 解释。更早或未知版本、非法模式、
截断和非法图片内容均拒绝，失败不修改调用方原命令列表。旧 v6 程序拒绝 v7，当前没有 v6 写入器；
需要与旧程序交换记录时，应协调升级生产者和消费者，不能简单改版本号绕过校验。

`PixelBuffer::operator==` 包含模式；`frameHash` 仍只计算宽高和原始字节。
日志必须把 hash 与模式一起解释。视觉比较统一为预乘后检查 alpha、黑/白底，避免低 alpha
反预乘放大 RGB 误差。P0 冻结的普通 CPU 比较容差为 RGB≤4、alpha 精确、超差比例 0，
P4 实测普通样本最大 RGB 差 2。旧 CPU 图片回绕缺陷的两场景使用 P0 独立参考并精确相等。
历史 M0/P0 记录不改写，逐帧模式、哈希及变化原因见
[P4-frames.json](perf-baselines/premultiplied-alpha-2026-09-20/P4-frames.json)。
