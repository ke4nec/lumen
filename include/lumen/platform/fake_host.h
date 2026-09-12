#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "lumen/platform/application_host.h"

namespace lumen::platform {

// v0.3 阶段8A (plan §4 8A): 可注入的确定性宿主。
//
// headless 测试用 fake host 模拟最小化、恢复、surface detach/attach、
// DPI 变化和多窗口事件；fake clock 提供确定性时间戳，fake clipboard 与
// fake TextInputSession 记录调用以便断言（plan §5.1 平台契约测试）。

// 手动时钟：宿主事件时间戳与测试推进显式同步。
class ManualHostClock {
  public:
    std::uint64_t nowMs{0};

    std::uint64_t advance(std::uint64_t deltaMs) {
        nowMs += deltaMs;
        return nowMs;
    }
};

class FakeClipboard final : public Clipboard {
  public:
    [[nodiscard]] bool hasText() const override { return !text_.empty(); }
    [[nodiscard]] std::string text() const override { return text_; }
    bool setText(const std::string& value) override {
        text_ = value;
        ++setCount;
        return available_;
    }
    void clear() override { text_.clear(); }

    // 测试钩子：模拟“剪贴板不可用”（无桌面会话）。
    void setAvailable(bool available) { available_ = available; }
    std::uint64_t setCount{0};

  private:
    std::string text_{};
    bool available_{true};
};

class FakeTextInputSession final : public TextInputSession {
  public:
    void start() override {
        active_ = true;
        ++startCount;
    }
    void stop() override {
        active_ = false;
        ++stopCount;
    }
    [[nodiscard]] bool active() const override { return active_; }
    void setEditingState(const TextInputEditingState& state) override {
        lastState = state;
        ++stateCount;
    }

    std::uint64_t startCount{0};
    std::uint64_t stopCount{0};
    std::uint64_t stateCount{0};
    TextInputEditingState lastState{};

  private:
    bool active_{false};
};

class FakeApplicationHost final : public ApplicationHost {
  public:
    // clock 为空时使用内部时钟（时间戳恒为 0，事件顺序仍确定）。
    explicit FakeApplicationHost(ManualHostClock* clock = nullptr);

    bool initialize() override;
    void shutdown() override;
    [[nodiscard]] core::AppLifecycle lifecycle() const override;
    bool pollEvent(core::HostEvent& out) override;

    std::optional<core::WindowId> createWindow(const WindowDesc& desc) override;
    void destroyWindow(core::WindowId id) override;
    [[nodiscard]] std::optional<core::WindowMetrics> windowMetrics(
        core::WindowId id) const override;
    [[nodiscard]] std::vector<core::WindowId> windowIds() const override;
    [[nodiscard]] PlatformWindow* platformWindow(
        core::WindowId /*id*/) const override {
        return nullptr;
    }

    [[nodiscard]] Clipboard* clipboard() override;
    [[nodiscard]] TextInputSession* textInputSession(
        core::WindowId id) override;
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    // --- M4：平台服务（确定性记录 + 失败注入） ---
    [[nodiscard]] ServiceResult openUrl(const std::string& url) override;
    [[nodiscard]] ServiceResult requestFileDialog(
        core::WindowId id, const FileDialogRequest& request) override;
    [[nodiscard]] ServiceResult postNotification(
        const NotificationRequest& request) override;
    void setCursor(core::WindowId id, SystemCursor cursor) override;
    [[nodiscard]] ServiceResult setWindowIcon(
        core::WindowId id, const WindowIcon& icon) override;

    // --- 可注入事件源（归一化事件直接入队） ---
    void pushPointerDown(core::WindowId id, core::Offset position,
                         core::PointerDevice device = core::PointerDevice::Mouse,
                         std::uint32_t pointerId = 0);
    void pushPointerUp(core::WindowId id, core::Offset position,
                       core::PointerDevice device = core::PointerDevice::Mouse,
                       std::uint32_t pointerId = 0);
    void pushPointerMove(core::WindowId id, core::Offset position,
                         core::PointerDevice device = core::PointerDevice::Mouse,
                         std::uint32_t pointerId = 0);
    void pushPointerCancel(core::WindowId id, core::Offset position,
                           std::uint32_t pointerId = 0);
    void pushWheel(core::WindowId id, core::Offset position,
                   core::Offset delta);
    void pushKeyDown(core::WindowId id, core::Key key,
                     core::KeyModifiers modifiers = core::kModifierNone,
                     char keyChar = 0);
    void pushKeyUp(core::WindowId id, core::Key key,
                   core::KeyModifiers modifiers = core::kModifierNone);
    void pushTextInput(core::WindowId id, std::string text);
    void pushTextEditing(core::WindowId id, std::string preedit, int cursor,
                         int length);
    void pushQuit();
    void pushCloseRequest(core::WindowId id);

    // --- 状态驱动：先更新 WindowMetrics，再入队事件（plan §3.1） ---
    void setLifecycle(core::AppLifecycle next);
    void resizeWindow(core::WindowId id, core::Size logical);
    // DPI 变化：drawableSize = logicalSize * scale 先行更新。
    void changeDeviceScale(core::WindowId id, float scale);
    void setSafeArea(core::WindowId id, core::EdgeInsets safeArea);
    void minimizeWindow(core::WindowId id);
    void restoreWindow(core::WindowId id);
    void setWindowFocus(core::WindowId id, bool focused);
    // 移动端 surface 语义：detach 期间暂停提交，不销毁状态树。
    void detachSurface(core::WindowId id);
    void reattachSurface(core::WindowId id);

    // --- 测试钩子 ---
    [[nodiscard]] FakeClipboard* fakeClipboard();
    [[nodiscard]] FakeTextInputSession* fakeTextInputSession(
        core::WindowId id);
    void setCapabilities(PlatformCapabilities capabilities);
    // 手动入队（测试自定义事件）。
    void pushRaw(core::HostEvent event);

    // --- M4：服务注入/记录断言 ---
    // 预置文件对话框结果（FIFO）；队列为空且未注入失败时返回
    // Unavailable。完成事件入队（FileDialogCompleted）。
    void queueFileDialogResult(FileDialogResult result);
    // 注入请求期失败（如服务不可用）。
    void setFileDialogFailure(ServiceResult failure);
    // 注入 openUrl/通知失败；空 = 成功。
    void setOpenUrlFailure(ServiceResult failure);
    void setNotificationFailure(ServiceResult failure);
    void setIconFailure(ServiceResult failure);

    // 记录（断言用）。
    struct OpenUrlCall {
        std::string url{};
        ServiceResult result{};
        bool operator==(const OpenUrlCall&) const = default;
    };
    std::vector<OpenUrlCall> openUrlCalls{};
    struct NotificationCall {
        NotificationRequest request{};
        ServiceResult result{};
        bool operator==(const NotificationCall&) const = default;
    };
    std::vector<NotificationCall> notificationCalls{};
    struct FileDialogCall {
        core::WindowId window{};
        FileDialogRequest request{};
        bool operator==(const FileDialogCall&) const = default;
    };
    std::vector<FileDialogCall> fileDialogCalls{};
    std::vector<std::pair<core::WindowId, SystemCursor>> cursorCalls{};
    struct IconCall {
        core::WindowId window{};
        WindowIcon icon{};
        bool operator==(const IconCall&) const = default;
    };
    std::vector<IconCall> iconCalls{};

  private:
    struct WindowEntry {
        core::WindowMetrics metrics{};
        bool focused{false};
        bool surfaceAttached{true};
        FakeTextInputSession textInput{};
    };

    [[nodiscard]] core::HostEvent makeEvent(core::HostEventType type,
                                            core::WindowId id);
    [[nodiscard]] WindowEntry* find(core::WindowId id);
    [[nodiscard]] std::uint64_t nowMs() const;

    ManualHostClock* clock_{nullptr};
    ManualHostClock ownedClock_{};
    std::uint64_t nextWindowId_{1};
    std::map<core::WindowId, WindowEntry> windows_{};
    std::deque<core::HostEvent> queue_{};
    core::AppLifecycle lifecycle_{core::AppLifecycle::Launching};
    bool initialized_{false};
    PlatformCapabilities capabilities_{};
    FakeClipboard clipboard_{};
    // M4 服务注入。
    std::deque<FileDialogResult> dialogResults_{};
    std::optional<ServiceResult> dialogFailure_{};
    std::optional<ServiceResult> openUrlFailure_{};
    std::optional<ServiceResult> notificationFailure_{};
    std::optional<ServiceResult> iconFailure_{};
};

}  // namespace lumen::platform
