# 原生整帧分配统计

## 1. 范围与启用

R6 的 `FrameAllocationSource` 在应用 rebuild/layout/paint 前开始，在
`Renderer::submit` 后结束。原生 source 只提供遥测，不改变文档、预览状态或帧调度。
source 的生命周期与调用均归 UI 线程；原生计数器可以接收其他线程的分配和释放。

提供 Windows x86/x64、Linux/glibc 与 macOS 动态链接程序的可选 profiler，默认关闭。
Windows 可选构建引入固定提交的 MinHook，其他平台不下载该依赖。
构建 `-DLUMEN_ENABLE_FRAME_ALLOCATOR=ON` 后，以启动时 preload 显式安装：

```sh
LD_PRELOAD="$PWD/build-release/src/platform/liblumen-frame-allocator.so" \
    build-release/examples/gallery/lumen-gallery --frame-overlay
```

`runApp` 优先使用 `RunOptions.frameAllocationSource`；空值调用
`platform::makeNativeFrameAllocationSource()`，并拥有工厂返回的 source，返回或异常
退出时解绑。直接使用 `AppShell` 的调用方须拥有工厂返回值并注入壳，且在销毁 source
前解绑。显式 source 的所有权不转移。

未显式启用 profiler 时工厂返回 nullptr，帧路径不创建 scope 或安装全局钩子。
Linux/macOS 使用启动期加载，Windows 使用下文的显式 DLL 路径；移动端不在本任务范围。
Linux 非 glibc、静态链接、其他 allocator、晚加载、私有 arena、直接 mmap 和 GPU
分配不在此 source 的覆盖口径内，不能把本 source 宣称为进程所有内存。

## 2. 读数口径

来源标识为 Linux 的 `glibc/malloc`、macOS 的 `libmalloc/malloc` 或 Windows 的
`ntdll/heap`，记录经验证的原生 allocator 请求，包括 C++/SDL
委托给该 family 的请求。统计范围跨应用、布局、记录、renderer 提交和同一时间段的
后台线程；后台请求归入当前时间段，不声称都是当前窗口因果引起的分配。

- `allocationCount`：scope 内成功且返回非空指针的分配/重分配次数；零字节成功请求也计数。
- `allocatedBytes`：上述请求的字节之和。calloc 为乘积，pvalloc 为其页面取整请求大小。
- `liveBytes`：上述请求在结束时仍存活的字节；开始前的对象不加入当前批次。
- `peakBytes`：上述 liveBytes 的最大值，按公开 allocator 调用完成的对象所有权变化计算。

成功 realloc 将旧的 scope 内对象移除，再计入新请求；原地增长同样算一次请求。
失败且非零大小的 realloc 保留旧对象；glibc 的 realloc(p, 0) 释放旧对象。
macOS 的 realloc(p, 0) 成功时替换为零字节请求对象，失败时保留旧对象；reallocf
在非零大小失败时释放旧对象。zone batch malloc 按成功返回的对象数计数。
Windows 成功 heap realloc 同样替换旧对象（包括零大小）；失败保留原对象。
Windows 的字节数取 Rtl heap 层请求，包含 CRT 为调试头、对齐和元数据增加的请求
字节，不能当作 C++/SDL 的逻辑大小。allocator 内部复制瞬时峰值、空闲池、开始前
存活堆、OS 映射和 RSS 不计入本口径。不同平台或不同来源的统计不能直接比较绝对峰值。

## 3. Linux 接入与可用性

独立共享库拦截 malloc/calloc/realloc/free、aligned_alloc/memalign/posix_memalign、
valloc/pvalloc 和 cfree/C23 sized free，委托原始 glibc 实现。其构造器用
RTLD_NEXT 解析实际函数；解析前用 glibc 的原始入口启动，不在钩子里做日志或堆分配。
TLS 使用 initial-exec，递归分配只在外层公开请求记一次。见
[glibc 的 malloc 替换契约](https://sourceware.org/glibc/manual/latest/html_node/Replacing-malloc.html)。

工厂验证版本化私有 ABI、完整符号绑定、下游 glibc 身份以及 C++/SDL 的实际分配
和释放路径。混用或遮蔽 allocator 时返回 nullptr；不会把局部计数作为完整 family
统计。校验使用 scope 内对象批次身份，后台存活对象或地址复用不会冒充探针对象；
begin/finish 也会检查 SDL 回调与原生符号绑定是否保持一致。该检查不能证明未知第三方的私有 arena
受覆盖，应用集成方须按来源口径使用。
显式 profiling 会串行化原生请求以保证跨线程对象账本一致，有测量开销，不应拿它与
未安装 profiler 的运行直接比较时间。

### macOS 接入

可选 dylib 要求 macOS 14 / SDK 14 或更新版本，以启动期注入启用：

```sh
DYLD_INSERT_LIBRARIES="$PWD/build-release/src/platform/liblumen-frame-allocator.dylib" \
    build-release/examples/gallery/lumen-gallery --frame-overlay
```

通过 Mach-O `__DATA,__interpose` 元组委托系统 libmalloc，覆盖 malloc/calloc/
realloc/reallocf/free、aligned_alloc/posix_memalign/valloc、可用的 vfree、公开
malloc_zone_* 分配/释放、batch、zone 销毁以及对应 typed 入口。较新 SDK 还编入
malloc_zone_malloc_with_options 及 typed 对应项的弱链接钩子；旧 SDK 构建的 profiler
在发现运行时具有尚未编入的公开 options 入口时返回 unavailable。
公开 zone realloc 的零大小空返回无法证明自定义 zone 是否释放旧对象，该 scope
标为 incomplete；不推测对象生命周期后发布完整数值。

dyld 将 interposer 自身的调用绑定到原实现；工厂验证每个外部替换符号及其下游
`/usr/lib/system/libsystem_malloc.dylib` 身份，拒绝其他 allocator 链与晚加载。旧 SDK
将 typed 导出标为私有 SPI，接入以私有弱链接别名隔离；不绕过 SDK 限制扩展应用
分配 API，也不承诺私有 SPI 的长期 ABI 稳定性。参考
[Apple dyld 的 interpose 定义](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/dyld-interposing.h)
和 [libmalloc typed 入口](https://github.com/apple-oss-distributions/libmalloc/blob/main/include/malloc/_malloc_type.h)。

Darwin TLS 用提前创建的 pthread key，避免 C++ thread_local 首次访问引起堆分配递归。
账本记录对象所属 zone，公开 malloc_destroy_zone 退休其 scope 内对象；batch free
在原函数可能覆盖指针数组前记账。直接调用 zone 函数指针、私有 options SPI、私有
arena、VM 和 GPU 分配不在覆盖口径内。受 SIP/hardened runtime 限制或注入不生效时，
工厂保持 nullptr；不修改系统保护、签名或全局环境设置。profiler 须驻留到进程退出，
不支持 dlclose 或运行中卸载；其 atfork 回调和 source 引用依赖该生命周期。

### Windows 接入

可选 DLL 要求 x86/x64 和支持 SEH 的 MSVC/clang-cl，默认 SDK 不受此构建要求约束。
MinHook v1.3.4 固定为提交 `c3fcafdc10146beb5919319d0683e44e3c30d537`，以私有静态库
链接到 profiler；不向 SDK 消费者传播依赖。profiler 与测试辅助 DLL 各用静态 CRT，
覆盖 CPU Debug 动态 CRT 与 Skia Release 静态 CRT 的配置。

```powershell
$env:LUMEN_FRAME_ALLOCATOR_DLL = (Resolve-Path build-release/src/platform/Release/lumen-frame-allocator.dll).Path
& build-release/examples/gallery/Release/lumen-gallery.exe --frame-overlay
Remove-Item Env:LUMEN_FRAME_ALLOCATOR_DLL
```

工厂只加载环境变量明确指定的 DLL，将路径解析为绝对路径，并限制依赖搜索到 DLL
目录和 System32；不自动在 PATH 或工作目录寻找 profiler。DLL 在调用私有 getter
之前固定到进程退出，不在 DllMain/loader lock 内安装钩子。首次工厂调用通过
InitOnce 和 MinHook 队列一次性安装 RtlAllocateHeap/RtlReAllocateHeap/RtlFreeHeap/
RtlDestroyHeap/RtlCreateHeap，随后保留系统实现与所有 flags。create/destroy 内部
管理 heap 的元数据请求受递归保护，不当作返回给应用的对象请求。安装失败返回 nullptr；部分安装
失败时保留 trampoline，确保正在执行的线程不引用已释放代码。
参考 [MinHook](https://github.com/TsudaKageyu/minhook)。

公开 Heap API、动态/静态 CRT 和其他 DLL 经这些入口的请求均进入同一账本。
账本用 heap handle 标识 owner，成功 destroy 退休该 heap 的 scope 内对象；失败
free/realloc/destroy 不推测释放。C++/SDL 探针可按请求区间识别带调试/对齐前缀的
内部用户指针，只用于验证释放与对象批次身份，不将内存内容当作可信元数据。
不经该 family 的私有 arena、VirtualAlloc/映射和 GPU 分配不在覆盖口径内。

递归标记提前保留前 64 个 TEB TLS 槽之一，避免扩展 TLS 首次访问再分配 heap；
若仅有扩展槽，工厂拒绝安装。SRW 锁串行化原生调用与账本更新；SEH 的
`__finally` 在 HEAP_GENERATE_EXCEPTIONS 等异常退出时释放锁和标记，保持原生
异常传播与 GetLastError。见 [Microsoft termination handling](https://learn.microsoft.com/en-us/windows/win32/debug/termination-handling)。
工厂验证 C++/SDL 路径，begin/finish 验证 SDL 回调、入口地址及安装后的补丁字节
未改变；后续 allocator 钩子替换导致 unavailable，不支持运行中卸载 profiler。

## 4. Scope 与失效

固定容量 65536 的静态对象账本不参与堆统计；原生调用与账本更新持同一互斥锁，防止
跨线程释放/地址复用早于分配入账。结束/取消不释放应用对象，下一次 begin 清空旧账本。
token 独立于 frameIndex；迟到或重复取消不会关闭其他实例的新 scope。

scope 不嵌套。其他 source 正在使用全进程计数器时，本次返回平台前缀的 `/busy` unavailable。
容量或数值溢出、失效 token 返回 `/incomplete` unavailable，不发布部分数值。
下次 scope 可恢复；source 元数据在结束原生计数后构造，避免自测报告分配。
AppShell 以 RAII 在异常和 idle 路径取消，`cancelFrame()` 幂等且 noexcept。

Linux/macOS 多线程 fork 前同步账本，子进程清除活动 scope，避免继承被其他线程持有的锁。
fork 期间的请求不保证可计数，因此父进程当前 scope 标为 incomplete。
信号处理和异步线程取消不构成额外支持承诺，遵循原生 allocator 的调用限制。

## 5. 验收证据

原生回归运行在独立进程，避免向 Catch2 主测试进程安装钩子；断言在 scope 结束后
执行。覆盖请求量/峰值、旧 scope 对象、realloc 失败/零大小、对齐/C23 free、C++/SDL、
跨线程释放、重叠/重复取消、容量耗尽恢复、实际 AppShell 和 runApp 默认 source 的
正常/异常生命周期。另运行无安装和符号被其他库遮蔽的工厂降级测试。
独立测试还覆盖不经原生 family 的 SDL 回调拒绝，以及工厂校验期间后台对象存活。
macOS 独立进程用例另覆盖 typed/zone/batch/销毁、新线程 TLS、reallocf、晚加载和
新 SDK options 在旧运行时的降级；常规 macOS CPU CI 和 Cocoa 现场工作流已启用。
这些 macOS 用例尚未在本机执行，配置 CI 不代表已有 CI 结果。
Windows 独立进程用例覆盖 heap/零大小/失败 realloc、heap 销毁、SEH 恢复、静态 CRT
辅助 DLL、对齐 C++/SDL、新线程、跨线程释放、scope 冲突和容量恢复、AppShell/runApp，
并单独验证缺失/错误 DLL 路径、SDL VirtualAlloc bypass、TLS 槽耗尽的降级。
Windows CPU Debug、Skia Release 及登录桌面的验收工作流已配置该可选构建；实际
Windows 测试与现场结果仍须记录，不将配置或交叉编译视作验收通过。

真实窗口探针可在已登录桌面运行以下命令，两个窗口都必须得到完整原生读数；
不可用或部分读数返回失败，JSON 包含采样帧数、分配次数、请求字节和最大帧峰值：

```sh
LD_PRELOAD="$PWD/build-release/src/platform/liblumen-frame-allocator.so" \
    build-release/tests/lumen-platform-live-smoke --frame-allocator --seconds 10
```

正式附件为 `frame-allocator-live.log`，同时包含 `frame_allocator_source` 与成功后
才打印的 `frame_allocator_smoke pass` 标记。验收检查器拒绝来源/会话不匹配、
fake/RSS/命令流数据、重复报告、缺失标记和非法指标；完整人工记录及其附件哈希仍是
独立必检条件。Linux self-hosted X11/Wayland、macOS Cocoa 与 Windows Win32 工作流已配置该探针和解析步骤，
未产生实际 workflow 验收结果。单独解析本机日志只证明这一项短 smoke。

本批默认配置全量 CTest 1120/1120，启用 profiler 的 Release 为 1127/1127。
五组原生独立进程测试为 114 个断言通过（包含重复的无安装/遮蔽用例）。

另在 Ubuntu 26.04.1 / GNOME Shell 50.1 的真实 Mutter 会话执行两窗口短 smoke：
Wayland 183 个有效 allocator 帧，Xwayland 175 个有效帧，原生 source 校验和状态保持
均通过。数据、编译产物/源码摘要及证据限制见
[`frame-allocator-linux-2026-10-08.json`](platform-evidence/frame-allocator-linux-2026-10-08.json)。
这是 source_dirty 的 CPU 诊断，Xwayland 不代表独立 X11 桌面；缺少完整 workflow、
提交归属、驱动、人工回环和浸泡记录，不使平台发布验收整体通过。macOS 后端已有
SDK 14.5 / 26.1 的 arm64/x86_64 交叉编译检查；实际 macOS/Windows 运行、窗口验收
和真实性能 CI 结果仍按支持矩阵逐批登记。

macOS 接入批次本地默认配置全量 CTest `1125/1125`，启用 Linux profiler 的 Release
为 `1132/1132`；共用账本为 25 个断言，解析器为 11 个 Python 用例通过。本机
Wayland 两窗口回归采集 133 个完整 allocator 帧并通过日志验证。
交叉检查用 Linux Clang 21.1.8，SDK 14.5/26.1，两架构 deployment target 为 macOS 14；
后端以 `-Wall -Wextra -Wpedantic -Werror` 编译，SDK 26.1 增加
`-DLUMEN_HAS_MALLOC_ZONE_OPTIONS=1`。SDK 14.5 的 dylib 还通过 ld64.lld 21.1.8
`-undefined error` 链接，检查实际 `__interpose` 段和原生导入符号。
SDK/client/native 用例的编译检查是构建证据；macOS 原生用例尚未执行，Cocoa 窗口
及 CI 结果仍待验，不能把交叉编译标记为 `frame_allocator_source=pass`。

Windows 接入批次本地默认配置全量 CTest `1126/1126`、启用 Linux profiler 的
Release `1133/1133`；共用账本 `38` 个断言、`6` 个用例，验收解析器 `12` 个 Python
用例通过。用 Linux Clang 21.1.8、MinGW 13.0 头/13.2 GNU runtime，目标
`x86_64-w64-windows-gnu` 与 `-fms-extensions -fsized-deallocation -pthread` 交叉
构建 SDK/client、profiler、辅助 DLL、SDL3 和独立测试程序；DLL 实际导出仅私有
getter，可选安装脚本的 DLL/MinHook 许可证路径已检查。
Wine 10.0 的五组独立进程测试共 `111` 个断言、`14` 个用例通过，包括真实 heap
分配失败的 SEH 清理与原生调用之后 GetLastError 保持。Wine/GNU runtime 不能
验证 MSVC `/MT[d]` 或原生 Windows：实际 MSVC CPU Debug、Skia Release、Windows
登录桌面和 macOS 原生/窗口结果仍待相应 runner，不使正式平台 record 通过。
