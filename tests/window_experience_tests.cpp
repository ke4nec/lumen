// G-8（docs/lumen-window-experience-design.md）：窗口体验杂项测试。
// 覆盖：WindowDesc 位置应用与 WindowMetrics 位置回读（Fake host 确定
// 性）、单实例助手（Primary/SecondaryActivated/激活回调/残留 socket 恢
// 复）、位置记忆的 Preferences 往返。全部 headless。

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#include <unistd.h>

#include "lumen/core/preferences.h"
#include "lumen/core/single_instance.h"
#include "lumen/core/windowing.h"
#include "lumen/platform/fake_host.h"

namespace fs = std::filesystem;
using lumen::core::SingleInstanceGuard;
using lumen::platform::FakeApplicationHost;
using lumen::platform::WindowDesc;

namespace {

fs::path tempPath(const char* tag) {
    return fs::temp_directory_path() /
           ("lumen-winxp-" + std::string(tag) + "-" +
            std::to_string(::getpid()));
}

}  // namespace

TEST_CASE("window_desc_position_applied_and_reported", "[platform][winxp]") {
    FakeApplicationHost host;
    REQUIRE(host.initialize());
    // 带位置创建：metrics 回读 positioned 快照。
    WindowDesc desc;
    desc.width = 640;
    desc.height = 480;
    desc.x = 120;
    desc.y = 88;
    const auto window = host.createWindow(desc);
    REQUIRE(window.has_value());
    const auto metrics = host.windowMetrics(*window);
    REQUIRE(metrics.has_value());
    CHECK(metrics->positioned);
    CHECK(metrics->x == 120);
    CHECK(metrics->y == 88);
    // 不带位置：系统默认（positioned=false——应用不做记忆回放）。
    const auto plain = host.createWindow(WindowDesc{});
    REQUIRE(plain.has_value());
    const auto plainMetrics = host.windowMetrics(*plain);
    REQUIRE(plainMetrics.has_value());
    CHECK_FALSE(plainMetrics->positioned);
}

TEST_CASE("single_instance_primary_and_secondary_activation",
          "[core][winxp]") {
    const fs::path dir = tempPath("guard");
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::atomic<int> activations{0};
    SingleInstanceGuard::Config config;
    config.appName = "lumen-winxp-test";
    config.socketDirectory = dir.string();
    config.onActivateRequest = [&activations] { ++activations; };

    // 首个实例 = Primary（绑定并监听）。
    CHECK(SingleInstanceGuard::acquire(config) ==
          SingleInstanceGuard::Status::Primary);
    // 第二个实例 = SecondaryActivated（请求送达）。
    CHECK(SingleInstanceGuard::acquire(config) ==
          SingleInstanceGuard::Status::SecondaryActivated);
    // 激活回调异步到达（轮询等待；accept 线程守护）。
    for (int i = 0; i < 100 && activations.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(activations.load() == 1);
}

TEST_CASE("single_instance_stale_socket_recovered", "[core][winxp]") {
    const fs::path dir = tempPath("stale");
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string socketPath =
        dir.string() + "/lumen-lumen-stale.single-instance";
    // 模拟崩溃残留：普通文件占位（connect 必败 → unlink 兜底 bind）。
    { std::ofstream leftover(socketPath); leftover << "stale"; }
    SingleInstanceGuard::Config config;
    config.appName = "lumen-stale";
    config.socketDirectory = dir.string();
    CHECK(SingleInstanceGuard::acquire(config) ==
          SingleInstanceGuard::Status::Primary);
}

TEST_CASE("position_memory_preferences_roundtrip", "[core][winxp]") {
    // 位置记忆的应用侧模式（G-7 消费）：保存 → 重建 → 回放。
    const fs::path dir = tempPath("memory");
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string path = (dir / "state.dat").string();
    {
        lumen::core::Preferences prefs;
        prefs.setInt("window.x", 64);
        prefs.setInt("window.y", 32);
        prefs.setInt("window.width", 1024);
        prefs.setInt("window.height", 768);
        CHECK(prefs.save(path));
    }
    lumen::core::Preferences prefs;
    REQUIRE(prefs.load(path));
    CHECK(prefs.getInt("window.x") == 64);
    CHECK(prefs.getInt("window.y") == 32);
    // 回放到 WindowDesc（模板 main.cpp 同款）。
    WindowDesc desc;
    desc.x = static_cast<int>(prefs.getInt("window.x", -1));
    desc.y = static_cast<int>(prefs.getInt("window.y", -1));
    CHECK(desc.x.value() == 64);
    CHECK(desc.y.value() == 32);
}
