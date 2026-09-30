# Lumen 崩溃兜底与持久日志设计（G-2）

> 状态：已实现（2026-09-29；gap-backlog G-2，P1）。
> 动机：长期驻留的个人工具崩溃即"消失"，无任何现场可事后定位；既有 `[diag]` 输出为 printf 式、不落盘（M18 inspector 只覆盖运行中观察）。
> 代码：[`include/lumen/diagnostics/runtime_diagnostics.h`](../include/lumen/diagnostics/runtime_diagnostics.h)、`src/diagnostics/runtime_diagnostics.cpp`（新模块 `lumen-diagnostics`）；`RunOptions.diagnosticsDirectory` 接线（`src/app/run_app.cpp`）。
> 测试：`tests/runtime_diagnostics_tests.cpp`（进程内单测）+ `tests/crash_injector.cpp` / `crash_injector_smoke.cmake`（独立注入进程 + script 模式 CTest）。

## 1. 产物与目录

应用提供目录（`RunOptions.diagnosticsDirectory`，空 = 整个子系统关闭，零开销）；`defaultDiagnosticsDirectory(appName)` 给平台惯例默认（Linux `XDG_DATA_HOME|~/.local/share/<app>`、macOS `~/Library/Logs/<app>`）。产物：

| 文件 | 语义 |
| --- | --- |
| `<app>.running` | 脏标记：start() 写入（内容 = 启动时间戳），cleanShutdown() 清除 |
| `<app>-crash.txt` | 崩溃报告（固定文件，崩溃时覆写）：app/信号名/日志路径 + 日志尾部环形缓冲（8KB）+ backtrace |
| `<app>.log`（`.log.1`…） | 分级日志（debug/info/warn/error），单文件上限滚动（默认 1MB × 保留 3） |

## 2. 生命周期

- `start()`：建目录 → **检测残留脏标记**（= 上次运行崩溃；`lastRunCrash()` 提供摘要）→ 写新标记 → 启动日志后台线程 → 安装处理器。重复 start 先拆除既有安装但**不清标记**（上次运行的证据必须存活到检测）。
- `cleanShutdown()`：冲刷停线程、清标记、恢复前处理器。仅干净退出路径调用。
- 崩溃路径（信号/terminate 处理器）：写报告后恢复默认信号并重触发——进程以**真实信号语义**终止（外层可按 WIFSIGNALED 断言），不伪装干净退出；标记自然残留。
- 析构（未经 cleanShutdown，如被 kill）：摘除处理器引用、停线程，**保留标记**——与崩溃同语义（非正常退出即嫌疑）。

## 3. 处理器纪律（async-signal-safe）

处理器内只做：原子读单例、`open/write/snprintf/backtrace_symbols_fd/close`（POSIX async-signal-safe 名单）。路径在 start() 预解析进固定缓冲（处理器内禁止字符串构造）；`appName` 构造后不可变（`c_str()` 无锁读取）。日志尾部环形缓冲（8KB，保留最近一半、整段摊还搬移）以自旋锁保多线程写序；处理器读取方**不加锁**（写方可能正持锁崩溃——死锁比撕裂更糟），撕裂可接受（尽力而为的现场线索）。`crashWritten_` 原子标志防 terminate→abort 链路重复截断报告。

终止链：未捕获异常 → `std::terminate` 处理器（报告标记 `signal=std::terminate`）→ `abort()` → SIGABRT 处理器（已被标志挡住）→ 默认语义。SIGABRT 直抛则标记 `signal=SIGABRT`。

## 4. 日志通道

- UI 线程只入队（`log()`：尾部环形缓冲 + 队列 push + notify；关态一次原子读空转）；后台 flusher 线程批量写盘、fflush 后更新 written 世代计数；`flush()` 等待世代追平（测试确定性）。
- 滚动：单文件超上限即整批换文件（`log → log.1 → …`，保留 `logKeepFiles` 个）。
- 不新增散乱输出：运行期落盘是唯一新通道；框架侧对外的开机行仍是既有 `[diag]` key=value 格式（`[diag] last-run-crashed=<0|1> report=<path> log=<path>`，受 `RunOptions.diagnostics` 开关约束，M14-C 纪律）。

## 5. runApp 接线

`RunOptions.diagnosticsDirectory` 非空（取首个配置的窗口）→ runApp 在 `host.initialize()` 后安装，退出（cleanup 后）`cleanShutdown()`；上次运行摘要经 **`RunOptions.onDiagnosticsStarted` 回调**交付（RunOptions 按值传入 runApp，字段回传无效——回调是唯一可靠通道；应用据此提示"上次已崩溃，日志在 …"，提示 UI 属应用层，框架不弹窗）。应用也可不经 runApp 直接持有 `RuntimeDiagnostics`（进程级单例语义，UI 线程独占调用）。

## 6. 平台与能力报告

- POSIX：sigaction 捕获 SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS + `std::set_terminate`；execinfo backtrace（cmake 探测 `LUMEN_HAVE_EXECINFO`）。
- Windows：编译安全的最小路径（`_open/_write`、无信号安装），`CrashCapabilities.signalHandlers=false` 如实降级（四态纪律）；backtrace 能力位同理。
- `CrashCapabilities` 随 start() 填充，可查询。

## 7. 验证要点

- 进程内单测（不装处理器，避免与 Catch 冲突）：脏标记生命周期（start 写/干净退出清/非正常退出留）、上次崩溃检测链（崩溃对齐实例 → 下一实例 crashed=true → 干净退出 → 再下实例 false）、日志分级格式与顺序、滚动上限与保留个数、关态零开销（空转不崩即证）、默认目录取环境变量。
- 注入进程（独立可执行 + script CTest）：segv/abort/terminate 注入 → 非零退出 + 报告含信号名/日志尾部/backtrace + 脏标记残留；query → `last-run-crashed=1`；干净退出后 `last-run-crashed=0`（正常退出不留脏标记）。
- 零开销口径：默认关闭（`diagnosticsDirectory` 空）时 runApp 不安装任何处理器/线程；`logFast` 一次原子读 + 分支；帧管线无改动（frame hash 不受影响，隔离基线 884/884 通过）。
