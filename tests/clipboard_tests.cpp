// G-3（docs/lumen-clipboard-service-design.md）：剪贴板深度测试。
// 覆盖：默认实现降级（纯文本宿主零改动）、Fake host 多格式读写与失败
// 注入、text/plain 双视图互通、ClipboardChanged 事件广播（runApp onEvent
// 经 Fake host 驱动）。SDL 侧能力位/跨应用粘贴为桌面 smoke 项（文档
// §5），headless 只锁契约。

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "lumen/app/app_shell.h"
#include "lumen/core/clipboard.h"
#include "lumen/core/windowing.h"
#include "lumen/platform/fake_host.h"

using lumen::app::AppShell;
using lumen::app::RunOptions;
using lumen::app::ShellConfig;
using lumen::core::ClipboardProvider;
using lumen::core::HostEvent;
using lumen::core::Size;
using lumen::platform::FakeApplicationHost;
using lumen::platform::FakeClipboard;

namespace {

// 最小纯文本实现（模拟未升级的宿主/应用侧 provider）：MIME 层必须全
// 部结构化降级，不崩溃、不误报。
class TextOnlyClipboard final : public ClipboardProvider {
  public:
    [[nodiscard]] bool hasText() const override { return !text_.empty(); }
    [[nodiscard]] std::string text() const override { return text_; }
    bool setText(const std::string& value) override {
        text_ = value;
        return true;
    }
    void clear() override { text_.clear(); }

  private:
    std::string text_{};
};

std::vector<std::uint8_t> bytesOf(const std::string& value) {
    return {value.begin(), value.end()};
}

}  // namespace

TEST_CASE("clipboard_default_impl_degrades_to_text", "[clipboard]") {
    TextOnlyClipboard clipboard;
    clipboard.setText("hello");
    CHECK(clipboard.hasFormat(ClipboardProvider::kMimeText));
    CHECK_FALSE(clipboard.hasFormat(ClipboardProvider::kMimePng));
    CHECK(clipboard.data(ClipboardProvider::kMimeText) == bytesOf("hello"));
    CHECK(clipboard.data(ClipboardProvider::kMimePng).empty());
    // 非文本格式写入结构化拒绝。
    CHECK_FALSE(clipboard.setData(ClipboardProvider::kMimePng, {1, 2, 3}));
    CHECK(clipboard.setData(ClipboardProvider::kMimeText,
                            bytesOf("plain")));
    CHECK(clipboard.text() == "plain");
    // setFormats 降级：只写 text/plain 条目。
    CHECK(clipboard.setFormats(
        {{ClipboardProvider::kMimeTsv, bytesOf("a\tb")},
         {ClipboardProvider::kMimeText, bytesOf("a b")}}));
    CHECK(clipboard.text() == "a b");
    const auto formats = clipboard.formats();
    REQUIRE(formats.size() == 1);
    CHECK(formats[0] == ClipboardProvider::kMimeText);
    CHECK_FALSE(clipboard.hasImage());
    clipboard.clear();
    CHECK(clipboard.formats().empty());
}

TEST_CASE("fake_clipboard_multi_format_roundtrip", "[clipboard]") {
    FakeClipboard clipboard;
    CHECK(clipboard.formats().empty());

    const std::vector<std::uint8_t> tsv = bytesOf("col1\tcol2\n");
    const std::vector<std::uint8_t> png = {0x89, 0x50, 0x4E, 0x47, 1, 2, 3};
    REQUIRE(clipboard.setFormats(
        {{ClipboardProvider::kMimeTsv, tsv}, {ClipboardProvider::kMimePng, png}}));
    CHECK(clipboard.setFormatsCount == 1);
    CHECK(clipboard.data(ClipboardProvider::kMimeTsv) == tsv);
    CHECK(clipboard.data(ClipboardProvider::kMimePng) == png);
    CHECK(clipboard.hasFormat(ClipboardProvider::kMimePng));
    CHECK(clipboard.hasImage());
    CHECK_FALSE(clipboard.hasFormat(ClipboardProvider::kMimeText));
    // 多格式列表可查（顺序保持放置顺序）。
    const auto formats = clipboard.formats();
    REQUIRE(formats.size() == 2);
    CHECK(formats[0] == ClipboardProvider::kMimeTsv);
    CHECK(formats[1] == ClipboardProvider::kMimePng);
    // setData 单格式 = setFormats 单条目。
    CHECK(clipboard.setData(ClipboardProvider::kMimePng, png));
    CHECK(clipboard.formats().size() == 1);

    // text/plain 双视图：纯文本路径写入后 MIME 视图可见。
    CHECK(clipboard.setText("hello"));
    CHECK(clipboard.hasFormat(ClipboardProvider::kMimeText));
    const auto mixed = clipboard.formats();
    CHECK(std::find(mixed.begin(), mixed.end(),
                    std::string(ClipboardProvider::kMimeText)) !=
          mixed.end());
    clipboard.clear();
    CHECK(clipboard.formats().empty());
    CHECK_FALSE(clipboard.hasImage());
}

TEST_CASE("fake_clipboard_failure_injection", "[clipboard]") {
    FakeClipboard clipboard;
    clipboard.setAvailable(false);
    CHECK_FALSE(clipboard.setText("x"));
    CHECK_FALSE(clipboard.hasText());
    CHECK_FALSE(clipboard.hasFormat(ClipboardProvider::kMimeText));
    CHECK_FALSE(clipboard.hasFormat(ClipboardProvider::kMimePng));
    CHECK(clipboard.text().empty());
    CHECK(clipboard.data(ClipboardProvider::kMimePng).empty());
    CHECK_FALSE(clipboard.setFormats(
        {{ClipboardProvider::kMimePng, {1, 2, 3}}}));
    CHECK(clipboard.formats().empty());
    // 恢复后可用。
    clipboard.setAvailable(true);
    CHECK(clipboard.setText("back"));
    CHECK(clipboard.hasText());
}

TEST_CASE("fake_clipboard_set_text_replaces_mime_entries",
          "[clipboard]") {
    // M-3 review：setText 是整体替换（SDL 语义）——旧 MIME 条目不可读。
    FakeClipboard clipboard;
    REQUIRE(clipboard.setFormats(
        {{ClipboardProvider::kMimePng, {1, 2, 3}}}));
    CHECK(clipboard.hasImage());
    REQUIRE(clipboard.setText("plain"));
    CHECK_FALSE(clipboard.hasImage());
    CHECK(clipboard.data(ClipboardProvider::kMimePng).empty());
    const auto formats = clipboard.formats();
    REQUIRE(formats.size() == 1);
    CHECK(formats[0] == ClipboardProvider::kMimeText);
}

TEST_CASE("clipboard_changed_event_broadcasts_to_on_event",
          "[clipboard][app]") {
    FakeApplicationHost host;
    ShellConfig config;
    config.initialView = Size{200.0F, 150.0F};
    config.build = [] { return lumen::core::Widget{}; };
    AppShell shell{config};
    REQUIRE(host.initialize());

    std::vector<HostEvent> received;
    RunOptions options;
    options.maxFrames = 1;
    options.onEvent = [&received](AppShell&, const HostEvent& event) {
        received.push_back(event);
        return;
    };
    // runApp 退出前注入变更事件（Fake host 队列在事件泵轮次被消费）。
    host.pushClipboardChanged();
    CHECK(lumen::app::runApp(shell, host, options) == 0);
    // 按类型过滤（runApp 生命周期还会产生窗口类事件）。
    int clipboardEvents = 0;
    for (const auto& event : received) {
        if (event.type == lumen::core::HostEventType::ClipboardChanged) {
            ++clipboardEvents;
            CHECK_FALSE(event.window.valid());  // 会话级，无归属窗口
        }
    }
    CHECK(clipboardEvents == 1);
}
