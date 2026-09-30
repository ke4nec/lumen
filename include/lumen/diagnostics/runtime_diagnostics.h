#pragma once

// G-2（gap-backlog）：崩溃兜底与持久日志。
//
// 长期驻留的个人工具崩溃即"消失"——本模块提供事后定位现场：
//   - 崩溃捕获：SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS 与 std::terminate
//     → 固定文件崩溃报告（信号名、时间戳、backtrace、日志尾部环形缓冲）；
//   - 持久日志：分级（Debug/Info/Warn/Error）+ 文件 sink（大小上限滚动
//     + 后台线程异步冲刷，UI 线程只入队）；
//   - 脏标记：start() 写标记、cleanShutdown() 清除；下次 start() 检测到
//     残留标记 = 上次运行崩溃（lastRunCrash() 提供报告路径，应用可提示）。
//
// 接线纪律（docs/lumen-runtime-diagnostics-design.md）：
//   - 全部产物落 directory（应用提供；defaultDiagnosticsDirectory() 给
//     XDG/平台惯例默认值）；框架侧仅经既有 [diag] key=value 行报告
//     last-run-crashed，不新增散乱输出（M14-C 通道纪律）。
//   - 关闭态（未 start / RunOptions.diagnosticsDirectory 为空）零开销：
//     logFast 一次原子读 + 分支；不触碰帧管线（frame hash 不变）。
//   - 信号处理器内只做 async-signal-safe 动作（write/backtrace_symbols_fd）；
//     日志环形缓冲以原子快照读取（尽力而为，撕裂可接受）。
//
// 平台：POSIX 完整实现（sigaction + execinfo backtrace，cmake 探测）；
// 无 execinfo 时退化为无 backtrace（CrashCapabilities 如实报告）。UI 线程
// 语义：log() 可多线程调用（内部自旋锁保序）；cleanShutdown 只能 UI 线程。

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace lumen::diagnostics {

enum class LogLevel : std::uint8_t {
    Debug,
    Info,
    Warn,
    Error,
};

[[nodiscard]] const char* logLevelName(LogLevel level);

// 崩溃报告能力（平台探测结果，四态纪律的如实报告）。
struct CrashCapabilities {
    bool signalHandlers{false};  // sigaction/signal 安装成功
    bool backtrace{false};       // execinfo 可用
};

// 上次运行崩溃摘要（start() 时检测；crashed=false 时其余字段为空）。
struct CrashSummary {
    bool crashed{false};
    std::string reportPath{};   // 上次崩溃报告文件（若存在）
    std::string logPath{};      // 本次日志文件（提示用）
    std::string timestamp{};    // 上次 start() 时间（标记内容）
};

struct RuntimeDiagnosticsOptions {
    // 产物目录（日志/崩溃报告/脏标记）。空串 = 全部关闭（零开销）。
    std::string directory{};
    std::string appName{"lumen-app"};  // 文件名前缀
    // 文件日志（分级 + 滚动 + 后台冲刷）。false = 仅崩溃捕获。
    bool fileLog{true};
    std::size_t logMaxBytes{1U << 20};  // 单文件上限（超出滚动到 .1/.2…）
    int logKeepFiles{3};                // 滚动保留个数
    bool crashCapture{true};            // 信号/terminate 处理器
};

// 平台惯例目录（Linux XDG_DATA_HOME|~/.local/share、macOS
// ~/Library/Logs、其余 HOME）；取不到环境变量返回空串（调用方决定降级）。
[[nodiscard]] std::string defaultDiagnosticsDirectory(
    const std::string& appName);

class RuntimeDiagnostics {
  public:
    explicit RuntimeDiagnostics(RuntimeDiagnosticsOptions options = {});
    ~RuntimeDiagnostics();

    RuntimeDiagnostics(const RuntimeDiagnostics&) = delete;
    RuntimeDiagnostics& operator=(const RuntimeDiagnostics&) = delete;
    RuntimeDiagnostics(RuntimeDiagnostics&&) = delete;
    RuntimeDiagnostics& operator=(RuntimeDiagnostics&&) = delete;

    // 安装：建目录 → 检测脏标记（lastRunCrash 填充）→ 写新标记 → 启动
    // 日志线程 + 信号处理器。失败（目录不可写等）安全降级：返回 false，
    // 进程内能力清零（后续 log 为空转）。重复 start 先做 cleanShutdown。
    bool start();
    // 干净退出：冲刷并停日志线程、清脏标记、恢复处理器。崩溃路径绝不
    // 经过这里（处理器内直接 _exit）。
    void cleanShutdown();

    // 分级日志（UI 线程为主；线程安全）。未 start/未启用 = 空转。
    void log(LogLevel level, std::string_view message);
    // 冲刷等待（测试用；后台线程持锁写盘）。
    void flush();

    [[nodiscard]] const CrashSummary& lastRunCrash() const {
        return lastRunCrash_;
    }
    [[nodiscard]] const CrashCapabilities& capabilities() const {
        return capabilities_;
    }
    [[nodiscard]] std::string logFilePath() const;
    [[nodiscard]] std::string crashReportPath() const;

    // 进程级单例（处理器/便捷入口访问）。start() 设置、cleanShutdown/
    // 析构清除。UI 线程独占语义由调用方保证。
    [[nodiscard]] static RuntimeDiagnostics* instance() {
        return instance_.load(std::memory_order_acquire);
    }
    // 便捷入口（instance() 为空 = 一次原子读空转，零开销纪律）。
    static void logFast(LogLevel level, std::string_view message) {
        RuntimeDiagnostics* self = instance();
        if (self != nullptr) {
            self->log(level, message);
        }
    }

  private:
    struct Impl;
    // 信号/terminate 处理器入口（静态；查 instance() 分发）。
    static void handleSignalTrampoline(int signalNumber);
    static void handleTerminateTrampoline();
    void writeCrashReport(int signalNumber, bool fromTerminate);
    void enqueueLogLine(LogLevel level, std::string_view message);
    void rotateLogFiles();
    // 停后台线程/关文件/恢复处理器；clearMarker=false 保留脏标记
    //（start() 内部重装前的拆除——上次运行的崩溃证据必须存活到检测）。
    void teardownRuntime(bool clearMarker);

    RuntimeDiagnosticsOptions options_{};
    CrashSummary lastRunCrash_{};
    CrashCapabilities capabilities_{};
    // 崩溃现场（处理器可读的 POD 区；字符串构造在处理器内禁止——路径
    // 预先解析进固定缓冲）。crashWritten 防 terminate→abort 链路重复
    // 截断报告。
    char crashPathRaw_[512]{};
    char logPathRaw_[512]{};
    std::atomic<bool> crashWritten_{false};
    // 日志尾部环形缓冲（崩溃报告嵌入用；写方自旋锁 + 原子写位，读取方
    // 无锁快照、撕裂可接受）。
    static constexpr std::size_t kTailBytes = 8192;
    char tail_[kTailBytes]{};
    std::atomic<std::size_t> tailPos_{0};
    std::atomic_flag tailLock_ = ATOMIC_FLAG_INIT;

    std::atomic<bool> running_{false};
    std::thread flusher_{};
    Impl* impl_{nullptr};  // 文件流/队列/处理器状态（非 POD，处理器不触）
    static std::atomic<RuntimeDiagnostics*> instance_;
};

}  // namespace lumen::diagnostics
