// G-2：崩溃注入进程（独立可执行，非 Catch 用例）。
//
// 用法：crash_injector <dir> <mode>
//   segv      — 解引用空指针（SIGSEGV）
//   abort     — abort()（SIGABRT）
//   terminate — 未捕获异常（std::terminate → abort 链）
//   query     — 打印 last-run-crashed 状态后干净退出（清脏标记）
//
// 由 tests/crash_injector_smoke.cmake 以 script 模式驱动断言：崩溃模式
// 退出码非 0（信号语义）、崩溃报告内容（信号名/日志尾部/backtrace）、
// 脏标记残留；query 模式输出 last-run-crashed=<0|1>。

#include <cstdio>
#include <stdexcept>
#include <string>

#include "lumen/diagnostics/runtime_diagnostics.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <dir> <mode>\n", argv[0]);
        return 2;
    }
    const std::string directory = argv[1];
    const std::string mode = argv[2];

    lumen::diagnostics::RuntimeDiagnosticsOptions options;
    options.directory = directory;
    options.appName = "inject";
    options.logMaxBytes = 64U * 1024U;
    lumen::diagnostics::RuntimeDiagnostics diagnostics{options};
    if (!diagnostics.start()) {
        std::fprintf(stderr, "start failed\n");
        return 2;
    }
    diagnostics.log(lumen::diagnostics::LogLevel::Info,
                    "injector-alive mode=" + mode);
    diagnostics.flush();

    if (mode == "segv") {
        volatile int* sink = nullptr;
        *sink = 1;  // SIGSEGV：处理器写报告后恢复默认信号语义
        return 0;
    }
    if (mode == "abort") {
        std::abort();  // NOLINT(concurrency-mt-unsafe) 测试注入
    }
    if (mode == "terminate") {
        throw std::runtime_error("injector-uncaught");
    }
    if (mode == "query") {
        const auto& crash = diagnostics.lastRunCrash();
        std::printf("last-run-crashed=%d report=%s\n",
                    crash.crashed ? 1 : 0, crash.reportPath.c_str());
        std::fflush(stdout);
        diagnostics.cleanShutdown();
        return 0;
    }
    std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
    return 2;
}
