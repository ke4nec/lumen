// G-7（docs/lumen-preferences-design.md）：轻量持久化助手测试。
// 覆盖：读写/类型转换与降级、变更通知（幂等不通知）、原子写（写中断
// 模拟 = tmp 残留 + 主文件完整）、损坏文件降级、版本字段、转义往返。
// 全部 headless（临时目录）。

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <unistd.h>

#include "lumen/core/preferences.h"

namespace fs = std::filesystem;
using lumen::core::Preferences;

namespace {

fs::path tempPath(const char* tag) {
    return fs::temp_directory_path() /
           ("lumen-prefs-" + std::string(tag) + "-" +
            std::to_string(::getpid()));
}

}  // namespace

TEST_CASE("preferences_typed_roundtrip_and_degradation",
          "[core][preferences]") {
    Preferences prefs;
    prefs.setString("name", "lumen");
    prefs.setInt("count", 42);
    prefs.setBool("enabled", true);
    prefs.setDouble("ratio", 0.75);
    CHECK(prefs.getString("name") == "lumen");
    CHECK(prefs.getInt("count") == 42);
    CHECK(prefs.getBool("enabled"));
    CHECK(prefs.getDouble("ratio") == 0.75);
    // 类型不匹配 = fallback 降级。
    CHECK(prefs.getInt("name", -1) == -1);
    CHECK(prefs.getBool("name", true));
    CHECK(prefs.getDouble("name", 1.5) == 1.5);
    CHECK(prefs.getBool("missing", true));
    // contains/remove/size。
    CHECK(prefs.contains("name"));
    CHECK(prefs.remove("name"));
    CHECK_FALSE(prefs.contains("name"));
    CHECK_FALSE(prefs.remove("name"));
    prefs.clear();
    CHECK(prefs.size() == 0);
}

TEST_CASE("preferences_change_notification_is_idempotent",
          "[core][preferences]") {
    Preferences prefs;
    int notifications = 0;
    std::string lastKey;
    const auto id = prefs.subscribe([&](const std::string& key) {
        ++notifications;
        lastKey = key;
    });
    prefs.setString("key", "a");
    CHECK(notifications == 1);
    CHECK(lastKey == "key");
    // 同值再写：不通知。
    prefs.setString("key", "a");
    CHECK(notifications == 1);
    prefs.setString("key", "b");
    CHECK(notifications == 2);
    prefs.remove("key");
    CHECK(notifications == 3);
    prefs.unsubscribe(id);
    prefs.setString("key", "c");
    CHECK(notifications == 3);
}

TEST_CASE("preferences_save_load_roundtrip_with_version",
          "[core][preferences]") {
    const fs::path path = tempPath("roundtrip") / "prefs.dat";
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    {
        Preferences prefs;
        prefs.setVersion(3);
        prefs.setString("name", "多行\n值");
        prefs.setInt("count", 7);
        prefs.setString("with=eq", "a=b\\c");
        CHECK(prefs.save(path.string()));
    }
    {
        Preferences prefs;
        CHECK(prefs.load(path.string()));
        CHECK(prefs.version() == 3);
        CHECK(prefs.getString("name") == "多行\n值");
        CHECK(prefs.getInt("count") == 7);
        CHECK(prefs.getString("with=eq") == "a=b\\c");
    }
}

TEST_CASE("preferences_corrupt_file_degrades_to_empty",
          "[core][preferences]") {
    const fs::path dir = tempPath("corrupt");
    std::error_code ec;
    fs::create_directories(dir, ec);
    // 截断/垃圾内容 → 损坏降级（false + 空表）。
    const fs::path bad = dir / "bad.dat";
    std::ofstream(bad) << "not a prefs file\n";
    Preferences prefs;
    CHECK_FALSE(prefs.load(bad.string()));
    CHECK(prefs.size() == 0);
    // 非法行同样降级。
    const fs::path badLine = dir / "badline.dat";
    std::ofstream(badLine)
        << "# lumen-prefs v1\nversion=1\nnodelimiter\n";
    CHECK_FALSE(prefs.load(badLine.string()));
    CHECK(prefs.size() == 0);
    // 缺失文件 = 首次运行（ok + 空表）。
    CHECK(prefs.load((dir / "missing.dat").string()));
    CHECK(prefs.size() == 0);
}

TEST_CASE("preferences_atomic_save_leaves_no_half_file",
          "[core][preferences]") {
    // 写一半崩溃的模拟：tmp 文件损坏不影响主文件；正常保存后主文件完
    // 整可读回；保存失败（不可写目录）不破坏内存态。
    const fs::path dir = tempPath("atomic");
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path path = dir / "prefs.dat";
    Preferences prefs;
    prefs.setString("a", "1");
    REQUIRE(prefs.save(path.string()));
    // 模拟写一半崩溃残留：坏 tmp + 完整主文件。
    std::ofstream(path.string() + ".tmp") << "# lumen-prefs v1\nver";
    Preferences reloaded;
    REQUIRE(reloaded.load(path.string()));
    CHECK(reloaded.getString("a") == "1");
    // 正常保存覆盖残留 tmp。
    reloaded.setString("b", "2");
    CHECK(reloaded.save(path.string()));
    CHECK_FALSE(fs::exists(path.string() + ".tmp"));
    Preferences final;
    CHECK(final.load(path.string()));
    CHECK(final.getString("b") == "2");
    // 不可写目录的保存失败：返回 false，内存态不变。
    Preferences stuck;
    stuck.setString("x", "y");
    CHECK_FALSE(stuck.save((dir / "no-such-dir" / "p.dat").string()));
    CHECK(stuck.getString("x") == "y");
}

TEST_CASE("preferences_observer_mutation_during_notification",
          "[core][preferences]") {
    // H-1 review：观察者内 unsubscribe/subscribe 不得使迭代器失效
    //（拷贝 id + 重查，StateStore 同模式）。
    Preferences prefs;
    int callsA = 0;
    int callsB = 0;
    int callsC = 0;
    int callsD = 0;
    lumen::core::Preferences::ObserverId idB = 0;
    lumen::core::Preferences::ObserverId idD = 0;
    (void)idD;
    prefs.subscribe([&](const std::string&) {
        ++callsA;
        prefs.unsubscribe(idB);  // A 在通知中取消 B
    });
    idB = prefs.subscribe([&](const std::string&) { ++callsB; });
    prefs.subscribe([&](const std::string&) {
        ++callsC;
        // C 在通知中新增 D——D 不收本次通知。
        idD = prefs.subscribe([&](const std::string&) { ++callsD; });
    });
    prefs.setString("k", "v");
    CHECK(callsA == 1);
    CHECK(callsB == 0);  // 被 A 取消：未收到本次通知
    CHECK(callsC == 1);
    CHECK(callsD == 0);  // 通知中新增：不收本次
    // 下一次通知 D 正常接收。
    prefs.setString("k", "v2");
    CHECK(callsC == 2);
    CHECK(callsD == 1);
}

TEST_CASE("preferences_escape_roundtrip_specials", "[core][preferences]") {
    const std::string special = "a=b\\c\nd=e";
    CHECK(Preferences::unescape(Preferences::escape(special)) == special);
    CHECK(Preferences::escape("").empty());
    CHECK(Preferences::unescape("\\\\n") == "\\n");
}
