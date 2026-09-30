// G-2（docs/lumen-runtime-diagnostics-design.md）：崩溃兜底与持久日志
// 单元测试。进程内用例不装信号处理器（catch 与注入进程语义冲突；信号
// 路径由 crash_injector + crash_injector_smoke.cmake 覆盖）——本文件
// 覆盖：脏标记生命周期、上次崩溃检测、日志分级/落盘/滚动、关闭态零
// 开销（不崩即证）与默认目录。

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <unistd.h>

#include "lumen/diagnostics/runtime_diagnostics.h"

namespace fs = std::filesystem;
using lumen::diagnostics::CrashSummary;
using lumen::diagnostics::LogLevel;
using lumen::diagnostics::RuntimeDiagnostics;
using lumen::diagnostics::RuntimeDiagnosticsOptions;

namespace {

fs::path freshDirectory(const char* tag) {
    fs::path dir = fs::temp_directory_path() /
                   ("lumen-diag-" + std::string(tag) + "-" +
                    std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

RuntimeDiagnosticsOptions inertOptions(const fs::path& dir) {
    RuntimeDiagnosticsOptions options;
    options.directory = dir.string();
    options.appName = "unit";
    options.crashCapture = false;  // 进程内用例不装处理器（注入进程覆盖）
    options.logMaxBytes = 1U << 20;
    options.logKeepFiles = 2;
    return options;
}

std::string readFile(const fs::path& path) {
    std::ifstream stream(path);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

}  // namespace

TEST_CASE("diagnostics_disabled_mode_is_inert", "[diagnostics]") {
    RuntimeDiagnosticsOptions options;  // directory 为空 = 关闭
    RuntimeDiagnostics diagnostics{options};
    CHECK_FALSE(diagnostics.start());
    diagnostics.log(LogLevel::Error, "ignored");
    RuntimeDiagnostics::logFast(LogLevel::Info, "ignored");
    diagnostics.cleanShutdown();
    CHECK(diagnostics.logFilePath().empty());
    CHECK(diagnostics.crashReportPath().empty());
}

TEST_CASE("diagnostics_marker_lifecycle_and_crash_detection",
          "[diagnostics]") {
    const fs::path dir = freshDirectory("marker");
    const fs::path marker = dir / "unit.running";
    {
        RuntimeDiagnostics diagnostics{inertOptions(dir)};
        REQUIRE(diagnostics.start());
        CHECK_FALSE(diagnostics.lastRunCrash().crashed);
        CHECK(fs::exists(marker));
        diagnostics.cleanShutdown();
        // 正常退出不留脏标记。
        CHECK_FALSE(fs::exists(marker));
    }
    {
        // 崩溃对齐：start 后不 cleanShutdown（析构路径），标记残留。
        RuntimeDiagnostics crashed{inertOptions(dir)};
        REQUIRE(crashed.start());
    }
    CHECK(fs::exists(marker));
    {
        RuntimeDiagnostics next{inertOptions(dir)};
        REQUIRE(next.start());
        const CrashSummary& crash = next.lastRunCrash();
        CHECK(crash.crashed);
        CHECK(crash.timestamp.size() >= 20);  // ISO8601 标记内容
        CHECK(crash.logPath == (dir / "unit.log").string());
        // 无崩溃报告文件（进程内未真崩）→ reportPath 为空。
        CHECK(crash.reportPath.empty());
        next.cleanShutdown();
    }
    {
        RuntimeDiagnostics clean{inertOptions(dir)};
        REQUIRE(clean.start());
        CHECK_FALSE(clean.lastRunCrash().crashed);
        clean.cleanShutdown();
    }
}

TEST_CASE("diagnostics_log_levels_format_and_persist", "[diagnostics]") {
    const fs::path dir = freshDirectory("log");
    {
        RuntimeDiagnostics diagnostics{inertOptions(dir)};
        REQUIRE(diagnostics.start());
        diagnostics.log(LogLevel::Info, "boot ok");
        diagnostics.log(LogLevel::Warn, "low disk");
        diagnostics.log(LogLevel::Error, "boom");
        diagnostics.flush();
        diagnostics.cleanShutdown();
    }
    const std::string log = readFile(dir / "unit.log");
    CHECK(log.find("[info] boot ok") != std::string::npos);
    CHECK(log.find("[warn] low disk") != std::string::npos);
    CHECK(log.find("[error] boom") != std::string::npos);
    CHECK(log.find("[debug]") == std::string::npos);
    // 时间戳前缀（行形如 2026-…T…Z [info] …）。
    CHECK(log.find("T") != std::string::npos);
    CHECK(log.find("Z [") != std::string::npos);
}

TEST_CASE("diagnostics_log_rotation_caps_files", "[diagnostics]") {
    const fs::path dir = freshDirectory("rotate");
    {
        RuntimeDiagnosticsOptions options = inertOptions(dir);
        options.logMaxBytes = 256;
        options.logKeepFiles = 2;
        RuntimeDiagnostics diagnostics{options};
        REQUIRE(diagnostics.start());
        for (int i = 0; i < 60; ++i) {
            diagnostics.log(LogLevel::Info,
                            "rotation line " + std::to_string(i));
        }
        diagnostics.flush();
        diagnostics.cleanShutdown();
    }
    CHECK(fs::exists(dir / "unit.log"));
    CHECK(fs::exists(dir / "unit.log.1"));
    const auto sizeOf = [](const fs::path& path) {
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        return ec ? std::uintmax_t{0} : size;
    };
    CHECK(sizeOf(dir / "unit.log") <= 256);
    CHECK(sizeOf(dir / "unit.log.1") <= 256);
    // 保留个数封顶（keep=2：.log/.log.1/.log.2，无 .log.3）。
    CHECK_FALSE(fs::exists(dir / "unit.log.3"));
}

TEST_CASE("diagnostics_default_directory_uses_env", "[diagnostics]") {
    const std::string dir =
        lumen::diagnostics::defaultDiagnosticsDirectory("lumen-test");
    CHECK_FALSE(dir.empty());
    CHECK(dir.find("lumen-test") != std::string::npos);
}

TEST_CASE("diagnostics_capabilities_reported_honestly", "[diagnostics]") {
    const fs::path dir = freshDirectory("caps");
    RuntimeDiagnostics diagnostics{inertOptions(dir)};
    REQUIRE(diagnostics.start());
    // 进程内用例关闭捕获：能力如实为 false。
    CHECK_FALSE(diagnostics.capabilities().signalHandlers);
    diagnostics.cleanShutdown();
}
