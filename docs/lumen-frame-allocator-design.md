# 原生整帧分配统计

## 1. 范围与启用

R6 的 `FrameAllocationSource` 在应用 rebuild/layout/paint 前开始，在
`Renderer::submit` 后结束。原生 source 只提供遥测，不改变文档、预览状态或帧调度。
source 的生命周期与调用均归 UI 线程；原生计数器可以接收其他线程的分配和释放。

首批提供 Linux/glibc 动态链接程序的可选 profiler，默认关闭，不新增第三方依赖。
构建 `-DLUMEN_ENABLE_FRAME_ALLOCATOR=ON` 后，以启动时 preload 显式安装：

```sh
LD_PRELOAD="$PWD/build-release/src/platform/liblumen-frame-allocator.so" \
    build-release/examples/gallery/lumen-gallery --frame-overlay
```

`runApp` 优先使用 `RunOptions.frameAllocationSource`；空值调用
`platform::makeNativeFrameAllocationSource()`，并拥有工厂返回的 source，返回或异常
退出时解绑。直接使用 `AppShell` 的调用方须拥有工厂返回值并注入壳，且在销毁 source
前解绑。显式 source 的所有权不转移。

未 preload 时工厂返回 nullptr，帧路径不创建 scope 或安装全局钩子。
Windows/macOS 工厂当前返回 nullptr，生产后端仍待实现；移动端不在本任务范围。
Linux 非 glibc、静态链接、其他 allocator、晚加载、私有 arena、直接 mmap 和 GPU
分配不在此 source 的覆盖口径内，不能把本 source 宣称为进程所有内存。

## 2. 读数口径

来源标识为 `glibc/malloc`，记录经验证的原生 malloc-family 请求，包括 C++/SDL
委托给该 family 的请求。统计范围跨应用、布局、记录、renderer 提交和同一时间段的
后台线程；后台请求归入当前时间段，不声称都是当前窗口因果引起的分配。

- `allocationCount`：scope 内成功且返回非空指针的分配/重分配次数；零字节成功请求也计数。
- `allocatedBytes`：上述请求的字节之和。calloc 为乘积，pvalloc 为其页面取整请求大小。
- `liveBytes`：上述请求在结束时仍存活的字节；开始前的对象不加入当前批次。
- `peakBytes`：上述 liveBytes 的最大值，按公开 allocator 调用完成的对象所有权变化计算。

成功 realloc 将旧的 scope 内对象移除，再计入新请求；原地增长同样算一次请求。
失败且非零大小的 realloc 保留旧对象；glibc 的 realloc(p, 0) 释放旧对象。
allocator 内部重分配瞬时复制、元数据、空闲池、开始前存活堆、OS 映射和 RSS 不计入
本口径。不同平台或不同来源的统计不能直接比较绝对峰值。

## 3. Linux 接入与可用性

独立共享库拦截 malloc/calloc/realloc/free、aligned_alloc/memalign/posix_memalign、
valloc/pvalloc 和 cfree/C23 sized free，委托原始 glibc 实现。其构造器用
RTLD_NEXT 解析实际函数；解析前用 glibc 的原始入口启动，不在钩子里做日志或堆分配。
TLS 使用 initial-exec，递归分配只在外层公开请求记一次。见
[glibc 的 malloc 替换契约](https://sourceware.org/glibc/manual/latest/html_node/Replacing-malloc.html)。

工厂验证版本化私有 ABI、完整符号绑定、下游 glibc 身份以及 C++/SDL 的实际分配
和释放路径。混用或遮蔽 allocator 时返回 nullptr；不会把局部计数作为完整 family
统计。校验使用 scope 内对象批次身份，后台存活对象或地址复用不会冒充探针对象；
begin/finish 也会检查 SDL 回调是否保持一致。该检查不能证明未知第三方的私有 arena
受覆盖，应用集成方须按来源口径使用。
显式 profiling 会串行化原生请求以保证跨线程对象账本一致，有测量开销，不应拿它与
未安装 profiler 的运行直接比较时间。

## 4. Scope 与失效

固定容量 65536 的静态对象账本不参与堆统计；原生调用与账本更新持同一互斥锁，防止
跨线程释放/地址复用早于分配入账。结束/取消不释放应用对象，下一次 begin 清空旧账本。
token 独立于 frameIndex；迟到或重复取消不会关闭其他实例的新 scope。

scope 不嵌套。其他 source 正在使用全进程计数器时，本次返回 `glibc/busy` unavailable。
容量或数值溢出、失效 token 返回 `glibc/incomplete` unavailable，不发布部分数值。
下次 scope 可恢复；source 元数据在结束原生计数后构造，避免自测报告分配。
AppShell 以 RAII 在异常和 idle 路径取消，`cancelFrame()` 幂等且 noexcept。

多线程 fork 前同步账本，子进程清除活动 scope，避免继承被其他线程持有的锁。
fork 期间的请求不保证可计数，因此父进程当前 scope 标为 incomplete。
信号处理和异步线程取消不构成额外支持承诺，遵循原生 allocator 的调用限制。

## 5. 验收证据

原生回归运行在独立进程，避免向 Catch2 主测试进程安装钩子；断言在 scope 结束后
执行。覆盖请求量/峰值、旧 scope 对象、realloc 失败/零大小、对齐/C23 free、C++/SDL、
跨线程释放、重叠/重复取消、容量耗尽恢复、实际 AppShell 和 runApp 默认 source 的
正常/异常生命周期。另运行无安装和符号被其他库遮蔽的工厂降级测试。
独立测试还覆盖不经原生 family 的 SDL 回调拒绝，以及工厂校验期间后台对象存活。

真实窗口探针可在已登录桌面运行以下命令，两个窗口都必须得到完整原生读数；
不可用或部分读数返回失败，JSON 包含采样帧数、分配次数、请求字节和最大帧峰值：

```sh
LD_PRELOAD="$PWD/build-release/src/platform/liblumen-frame-allocator.so" \
    build-release/tests/lumen-platform-live-smoke --frame-allocator --seconds 10
```

正式附件为 `frame-allocator-live.log`，同时包含 `frame_allocator_source` 与成功后
才打印的 `frame_allocator_smoke pass` 标记。验收检查器拒绝来源/会话不匹配、
fake/RSS/命令流数据、重复报告、缺失标记和非法指标；完整人工记录及其附件哈希仍是
独立必检条件。Linux self-hosted X11/Wayland 工作流已配置该探针和解析步骤，
未产生实际 workflow 验收结果。单独解析本机日志只证明这一项短 smoke。

本批默认配置全量 CTest 1120/1120，启用 profiler 的 Release 为 1127/1127。
五组原生独立进程测试为 114 个断言通过（包含重复的无安装/遮蔽用例）。

另在 Ubuntu 26.04.1 / GNOME Shell 50.1 的真实 Mutter 会话执行两窗口短 smoke：
Wayland 183 个有效 allocator 帧，Xwayland 175 个有效帧，原生 source 校验和状态保持
均通过。数据、编译产物/源码摘要及证据限制见
[`frame-allocator-linux-2026-10-08.json`](platform-evidence/frame-allocator-linux-2026-10-08.json)。
这是 source_dirty 的 CPU 诊断，Xwayland 不代表独立 X11 桌面；缺少完整 workflow、
提交归属、驱动、人工回环和浸泡记录，不使平台发布验收整体通过。Windows/macOS
生产 source 和真实性能 CI 结果仍按支持矩阵逐批登记。
