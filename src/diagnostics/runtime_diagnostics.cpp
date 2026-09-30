// G-2：崩溃兜底与持久日志实现。见
// docs/lumen-runtime-diagnostics-design.md 与头文件契约注释。

#include "lumen/diagnostics/runtime_diagnostics.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(LUMEN_HAVE_EXECINFO)
#include <execinfo.h>
#endif

#if !defined(_WIN32)
#include <csignal>
#include <signal.h>
#endif

namespace lumen::diagnostics {

std::atomic<RuntimeDiagnostics*> RuntimeDiagnostics::instance_{nullptr};

namespace {

namespace fs = std::filesystem;

// 处理器安全写：snprintf 组头 + write（POSIX async-signal-safe 名单内）。
inline void rawWrite(int fd, const char* data, std::size_t bytes) {
    if (bytes == 0) {
        return;
    }
    (void)::write(fd, data, bytes);
}

inline int rawOpenForTrunc(const char* path) {
#if defined(_WIN32)
    return ::_open(path, _O_WRONLY | _O_CREAT | _O_TRUNC, _S_IWRITE);
#else
    return ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
}

inline void rawClose(int fd) {
#if defined(_WIN32)
    (void)::_close(fd);
#else
    (void)::close(fd);
#endif
}

// 崩溃处理器内的信号名（固定表查表安全）。
const char* signalName(int signalNumber, bool fromTerminate) {
    if (fromTerminate) {
        return "std::terminate";
    }
    switch (signalNumber) {
#if defined(SIGSEGV)
        case SIGSEGV:
            return "SIGSEGV";
#endif
#if defined(SIGABRT)
        case SIGABRT:
            return "SIGABRT";
#endif
#if defined(SIGFPE)
        case SIGFPE:
            return "SIGFPE";
#endif
#if defined(SIGILL)
        case SIGILL:
            return "SIGILL";
#endif
#if defined(SIGBUS)
        case SIGBUS:
            return "SIGBUS";
#endif
        default:
            return "SIG?";
    }
}

// ISO8601 UTC 时间戳（秒粒度；只在普通线程调用，处理器内不构造）。
std::string timestampNow() {
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    const std::tm* parts = gmtime(&now);
    if (parts == nullptr) {
        return {};
    }
    utc = *parts;
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

// 处理器路径预解析：固定缓冲拷贝（截断安全）。
void copyPathRaw(char* dst, const std::string& path) {
    std::snprintf(dst, 512, "%s", path.c_str());
}

}  // namespace

// 非 POD 状态：日志队列/文件/前处理器。崩溃处理器绝不触碰本结构。
struct RuntimeDiagnostics::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::pair<LogLevel, std::string>> queue;
    bool stopping{false};
    // flush() 的确定性：enqueued/written 世代计数（持锁更新）。
    std::uint64_t enqueued{0};
    std::uint64_t written{0};
    // 以下仅 flusher 线程访问。
    std::FILE* logFile{nullptr};
    std::size_t logBytes{0};
#if !defined(_WIN32)
    static constexpr int kSignalCount = 5;
    struct sigaction previous[kSignalCount]{};
    bool handlersInstalled{false};
#endif
    std::terminate_handler previousTerminate{nullptr};
    bool terminateInstalled{false};
};

const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
    }
    return "info";
}

std::string defaultDiagnosticsDirectory(const std::string& appName) {
#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr) {
        return (fs::path(home) / "Library" / "Logs" / appName).string();
    }
    return {};
#else
    std::string base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME");
        xdg != nullptr && *xdg != '\0') {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home != nullptr) {
        base = (fs::path(home) / ".local" / "share").string();
    }
    if (base.empty()) {
        return {};
    }
    return (fs::path(base) / appName).string();
#endif
}

RuntimeDiagnostics::RuntimeDiagnostics(RuntimeDiagnosticsOptions options)
    : options_(std::move(options)) {}

RuntimeDiagnostics::~RuntimeDiagnostics() {
    // 崩溃对齐路径（未经 cleanShutdown）：不清脏标记——这正是"上次崩溃"
    // 的证据；摘除处理器引用并停后台线程（防悬空）。
    teardownRuntime(/*clearMarker=*/false);
}

std::string RuntimeDiagnostics::logFilePath() const {
    if (options_.directory.empty()) {
        return {};
    }
    return (fs::path(options_.directory) / (options_.appName + ".log"))
        .string();
}

std::string RuntimeDiagnostics::crashReportPath() const {
    if (options_.directory.empty()) {
        return {};
    }
    return (fs::path(options_.directory) / (options_.appName + "-crash.txt"))
        .string();
}

bool RuntimeDiagnostics::start() {
    if (options_.directory.empty()) {
        return false;
    }
    // 重复 start：先拆除既有安装——但绝不清脏标记（上次运行的崩溃证
    // 据必须存活到下面的检测）。
    teardownRuntime(/*clearMarker=*/false);
    std::error_code ec;
    fs::create_directories(options_.directory, ec);
    if (ec) {
        return false;
    }
    // 脏标记检测：残留 = 上次运行崩溃（处理器不清理；cleanShutdown 才
    // 清）。崩溃报告存在且非空才算现场证据。
    const fs::path markerPath =
        fs::path(options_.directory) / (options_.appName + ".running");
    lastRunCrash_ = CrashSummary{};
    lastRunCrash_.logPath = logFilePath();
    if (fs::exists(markerPath, ec)) {
        lastRunCrash_.crashed = true;
        if (std::FILE* marker = std::fopen(markerPath.string().c_str(), "rb");
            marker != nullptr) {
            char buffer[64] = {};
            const std::size_t got =
                std::fread(buffer, 1, sizeof(buffer) - 1, marker);
            buffer[got] = '\0';
            lastRunCrash_.timestamp = buffer;
            std::fclose(marker);
        }
        const fs::path report = crashReportPath();
        if (fs::exists(report, ec) && fs::file_size(report, ec) > 0) {
            lastRunCrash_.reportPath = report.string();
        }
    }
    // 写新标记（本次运行的脏状态）。
    if (std::FILE* marker = std::fopen(markerPath.string().c_str(), "wb");
        marker != nullptr) {
        std::fputs(timestampNow().c_str(), marker);
        std::fclose(marker);
    }
    // 处理器路径预解析（处理器内禁止字符串构造）。
    copyPathRaw(crashPathRaw_, crashReportPath());
    copyPathRaw(logPathRaw_, logFilePath());
    crashWritten_.store(false, std::memory_order_relaxed);

    impl_ = new Impl();
    // 文件日志：后台 flusher 线程（UI 线程只入队）。
    if (options_.fileLog) {
        impl_->logFile = std::fopen(logFilePath().c_str(), "ab");
        running_.store(true, std::memory_order_release);
        flusher_ = std::thread([this] {
            std::vector<std::pair<LogLevel, std::string>> batch;
            while (true) {
                std::uint64_t batchEnqueued = 0;
                {
                    std::unique_lock<std::mutex> lock(impl_->mutex);
                    impl_->cv.wait(lock, [this] {
                        return impl_->stopping || !impl_->queue.empty();
                    });
                    if (impl_->stopping && impl_->queue.empty()) {
                        return;
                    }
                    batchEnqueued = impl_->enqueued;
                    batch.swap(impl_->queue);
                }
                for (const auto& entry : batch) {
                    if (impl_->logFile == nullptr) {
                        break;
                    }
                    const std::string line =
                        timestampNow() + " [" + logLevelName(entry.first) +
                        "] " + entry.second + "\n";
                    if (impl_->logBytes + line.size() > options_.logMaxBytes) {
                        std::fclose(impl_->logFile);
                        impl_->logFile = nullptr;
                        rotateLogFiles();
                        impl_->logFile = std::fopen(logFilePath().c_str(), "wb");
                        impl_->logBytes = 0;
                        if (impl_->logFile == nullptr) {
                            break;
                        }
                    }
                    impl_->logBytes += std::fwrite(
                        line.data(), 1, line.size(), impl_->logFile);
                }
                {
                    std::lock_guard<std::mutex> lock(impl_->mutex);
                    impl_->written = batchEnqueued;
                }
                impl_->cv.notify_all();  // flush() 等待落盘完成。
                batch.clear();
            }
        });
    }
    // 崩溃捕获：信号 + terminate（POSIX 完整；Windows 如实降级）。
    if (options_.crashCapture) {
#if !defined(_WIN32)
        const int signals[Impl::kSignalCount] = {SIGSEGV, SIGABRT, SIGFPE,
                                                 SIGILL, SIGBUS};
        bool all = true;
        for (int i = 0; i < Impl::kSignalCount; ++i) {
            struct sigaction action{};
            action.sa_handler = &RuntimeDiagnostics::handleSignalTrampoline;
            sigemptyset(&action.sa_mask);
            action.sa_flags = 0;
            if (sigaction(signals[i], nullptr, &impl_->previous[i]) == 0 &&
                sigaction(signals[i], &action, nullptr) == 0) {
                // installed
            } else {
                all = false;
            }
        }
        impl_->handlersInstalled = all;
        capabilities_.signalHandlers = all;
#else
        capabilities_.signalHandlers = false;
#endif
        impl_->previousTerminate =
            std::set_terminate(&RuntimeDiagnostics::handleTerminateTrampoline);
        impl_->terminateInstalled = true;
#if defined(LUMEN_HAVE_EXECINFO)
        capabilities_.backtrace = true;
#else
        capabilities_.backtrace = false;
#endif
    }
    instance_.store(this, std::memory_order_release);
    return true;
}

void RuntimeDiagnostics::cleanShutdown() {
    teardownRuntime(/*clearMarker=*/true);
}

void RuntimeDiagnostics::teardownRuntime(bool clearMarker) {
    instance_.store(nullptr, std::memory_order_release);
    if (impl_ != nullptr) {
        if (running_.load(std::memory_order_acquire)) {
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->stopping = true;
            }
            impl_->cv.notify_all();
            if (flusher_.joinable()) {
                flusher_.join();
            }
            running_.store(false, std::memory_order_release);
        }
        if (impl_->logFile != nullptr) {
            std::fclose(impl_->logFile);
            impl_->logFile = nullptr;
        }
        if (impl_->terminateInstalled) {
            if (impl_->previousTerminate != nullptr) {
                std::set_terminate(impl_->previousTerminate);
            }
            impl_->terminateInstalled = false;
        }
#if !defined(_WIN32)
        if (impl_->handlersInstalled) {
            const int signals[Impl::kSignalCount] = {SIGSEGV, SIGABRT,
                                                     SIGFPE, SIGILL, SIGBUS};
            for (int i = 0; i < Impl::kSignalCount; ++i) {
                sigaction(signals[i], &impl_->previous[i], nullptr);
            }
            impl_->handlersInstalled = false;
        }
#endif
        delete impl_;
        impl_ = nullptr;
    }
    if (flusher_.joinable()) {
        flusher_.join();
    }
    // 清脏标记：仅干净退出（clearMarker=true）；start 的重装拆除与崩溃
    // 路径都不清。
    if (clearMarker && !options_.directory.empty()) {
        std::error_code ec;
        fs::remove(
            fs::path(options_.directory) / (options_.appName + ".running"),
            ec);
    }
}

void RuntimeDiagnostics::log(LogLevel level, std::string_view message) {
    if (impl_ == nullptr || !running_.load(std::memory_order_acquire)) {
        return;
    }
    enqueueLogLine(level, message);
    impl_->cv.notify_one();
}

void RuntimeDiagnostics::flush() {
    if (impl_ == nullptr || !running_.load(std::memory_order_acquire)) {
        return;
    }
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->cv.wait(lock,
                   [this] { return impl_->written == impl_->enqueued; });
}

void RuntimeDiagnostics::enqueueLogLine(LogLevel level,
                                        std::string_view message) {
    // 尾部环形缓冲：保留最近日志（崩溃报告嵌入）。spinlock 保多线程写
    // 序（处理器读取方不加锁，撕裂可接受）。
    while (tailLock_.test_and_set(std::memory_order_acquire)) {
    }
    const std::size_t pos = tailPos_.load(std::memory_order_relaxed);
    if (message.size() + pos <= kTailBytes) {
        std::memcpy(tail_ + pos, message.data(), message.size());
        tailPos_.store(pos + message.size(), std::memory_order_relaxed);
    } else {
        // 保留最近一半，整段搬移一次（摊销）。
        const std::size_t keep = kTailBytes / 2;
        std::memmove(tail_, tail_ + (kTailBytes - keep), keep);
        const std::size_t freeBytes = kTailBytes - keep;
        const std::size_t copy =
            message.size() < freeBytes ? message.size() : freeBytes;
        std::memcpy(tail_ + keep, message.data(), copy);
        tailPos_.store(keep + copy, std::memory_order_relaxed);
    }
    tailLock_.clear(std::memory_order_release);
    std::lock_guard<std::mutex> queueLock(impl_->mutex);
    impl_->queue.emplace_back(level, std::string(message));
    ++impl_->enqueued;
}

void RuntimeDiagnostics::rotateLogFiles() {
    namespace fs = std::filesystem;
    const fs::path base = logFilePath();
    std::error_code ec;
    // log.(n-1) → log.n … log → log.1（保留 logKeepFiles 个滚动文件）。
    for (int i = options_.logKeepFiles; i >= 1; --i) {
        const fs::path from =
            i == 1 ? base
                   : fs::path(base.string() + "." + std::to_string(i - 1));
        const fs::path to = fs::path(base.string() + "." + std::to_string(i));
        fs::remove(to, ec);
        fs::rename(from, to, ec);
    }
}

// --- 崩溃现场（async-signal-safe 区域） ---

void RuntimeDiagnostics::handleSignalTrampoline(int signalNumber) {
    RuntimeDiagnostics* self = instance_.load(std::memory_order_acquire);
    if (self != nullptr) {
        self->writeCrashReport(signalNumber, false);
    }
#if !defined(_WIN32)
    // 恢复默认并重触发：进程以真实信号语义终止（外层按 WIFSIGNALED
    // 断言），不伪装成干净退出。
    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
#else
    std::_Exit(70 + signalNumber);
#endif
}

void RuntimeDiagnostics::handleTerminateTrampoline() {
    RuntimeDiagnostics* self = instance_.load(std::memory_order_acquire);
    if (self != nullptr) {
        self->writeCrashReport(SIGABRT, true);
    }
    std::abort();
}

// 只做 async-signal-safe 动作：open/write/snprintf/backtrace_symbols_fd/
// close。appName 为 start 后不可变（构造定格），c_str() 读取无锁。
void RuntimeDiagnostics::writeCrashReport(int signalNumber,
                                          bool fromTerminate) {
    if (crashWritten_.exchange(true, std::memory_order_relaxed)) {
        return;  // terminate→abort 链路只留首份报告。
    }
    const int fd = rawOpenForTrunc(crashPathRaw_);
    if (fd < 0) {
        return;
    }
    char header[512];
    const int headerLen = std::snprintf(
        header, sizeof(header),
        "app=%s\nsignal=%s\ntimestamp=unavailable-in-handler\nlog=%s\n"
        "--- last-log-tail ---\n",
        options_.appName.c_str(), signalName(signalNumber, fromTerminate),
        logPathRaw_);
    if (headerLen > 0) {
        const std::size_t bytes =
            static_cast<std::size_t>(headerLen) < sizeof(header)
                ? static_cast<std::size_t>(headerLen)
                : sizeof(header);
        rawWrite(fd, header, bytes);
    }
    const std::size_t filled = tailPos_.load(std::memory_order_relaxed);
    if (filled > 0) {
        rawWrite(fd, tail_, filled);
        rawWrite(fd, "\n", 1);
    }
#if defined(LUMEN_HAVE_EXECINFO)
    rawWrite(fd, "--- backtrace ---\n", 18);
    void* frames[64];
    const int count = ::backtrace(frames, 64);
    if (count > 0) {
        ::backtrace_symbols_fd(frames, count, fd);
    }
#endif
    rawClose(fd);
}

}  // namespace lumen::diagnostics
