#pragma once

#include <cstdint>
#include <deque>
#include <functional>
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
        ++setCount;
        if (!available_) {
            return false;  // 与 SDL 语义一致：失败不落账
        }
        // 整体替换：纯文本写入清除全部 MIME 条目（SDL_SetClipboardText
        // 替换整个剪贴板——与真实语义对齐，review M-3）。
        text_ = value;
        entries_.clear();
        return true;
    }
    void clear() override {
        text_.clear();
        entries_.clear();
    }

    // --- G-3：MIME 数据层（确定性记录 + 失败注入） ---
    [[nodiscard]] bool hasFormat(const std::string& mimeType) const override {
        if (!available_) {
            return false;
        }
        if (mimeType == core::ClipboardProvider::kMimeText) {
            return !text_.empty();
        }
        return findEntry(mimeType) != nullptr;
    }
    [[nodiscard]] std::vector<std::uint8_t> data(
        const std::string& mimeType) const override {
        const auto* entry = findEntry(mimeType);
        return entry != nullptr ? entry->bytes : std::vector<std::uint8_t>{};
    }
    bool setData(const std::string& mimeType,
                 const std::vector<std::uint8_t>& bytes) override {
        return setFormats({Entry{mimeType, bytes}});
    }
    bool setFormats(const std::vector<Entry>& entries) override {
        ++setFormatsCount;
        if (!available_) {
            return false;
        }
        entries_ = entries;
        // text/plain 与既有 text() 视图互通（单一事实，双视图）。
        text_.clear();
        for (const auto& entry : entries_) {
            if (entry.mimeType == core::ClipboardProvider::kMimeText) {
                text_.assign(entry.bytes.begin(), entry.bytes.end());
            }
        }
        return true;
    }
    [[nodiscard]] std::vector<std::string> formats() const override {
        std::vector<std::string> result;
        if (!available_) {
            return result;
        }
        for (const auto& entry : entries_) {
            result.push_back(entry.mimeType);
        }
        if (text_.empty() && entries_.empty()) {
            return result;
        }
        if (!text_.empty() && findEntry(
                                  core::ClipboardProvider::kMimeText) ==
                                  nullptr) {
            result.push_back(core::ClipboardProvider::kMimeText);
        }
        return result;
    }

    // 测试钩子：模拟”剪贴板不可用”（无桌面会话）。
    void setAvailable(bool available) { available_ = available; }
    std::uint64_t setCount{0};
    std::uint64_t setFormatsCount{0};

  private:
    [[nodiscard]] const Entry* findEntry(const std::string& mimeType) const {
        for (const auto& entry : entries_) {
            if (entry.mimeType == mimeType) {
                return &entry;
            }
        }
        return nullptr;
    }

    std::string text_{};
    std::vector<Entry> entries_{};
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

    // M13：原生语义桥激活状态如实入能力（headless 断言用）。
    void noteAccessibilityBridgeActive(bool active) override;

    // --- M4：平台服务（确定性记录 + 失败注入） ---
    [[nodiscard]] ServiceResult openUrl(const std::string& url) override;
    [[nodiscard]] ServiceResult requestFileDialog(
        core::WindowId id, const FileDialogRequest& request) override;
    [[nodiscard]] ServiceResult postNotification(
        const NotificationRequest& request) override;
    void setCursor(core::WindowId id, SystemCursor cursor) override;
    [[nodiscard]] ServiceResult setWindowIcon(
        core::WindowId id, const WindowIcon& icon) override;
    // M15：拖出（记录 + 失败注入；默认成功，模拟可用服务）。
    [[nodiscard]] ServiceResult startDrag(
        core::WindowId id, const DragOutPayload& payload) override;

    // --- 自定义标题栏（lumen-titlebar-design §4.3）：窗口操作（记录 +
    // 状态驱动语义合一：更新 metrics 并入队对应事件） ---
    void minimizeWindow(core::WindowId id) override;
    void toggleMaximizeWindow(core::WindowId id) override;
    void requestWindowClose(core::WindowId id) override;
    // --- M16：窗口能力（同记录 + 状态驱动语义） ---
    void toggleFullscreen(core::WindowId id) override;
    void setAlwaysOnTop(core::WindowId id, bool onTop) override;
    [[nodiscard]] ServiceResult setWindowModal(core::WindowId id,
                                               core::WindowId parent) override;
    // --- M16：托盘与全局快捷键（记录 + 失败注入 + 事件注入） ---
    [[nodiscard]] ServiceResult setTray(core::WindowId ownerWindow,
                                        const TraySetup& tray) override;
    void removeTray() override;
    [[nodiscard]] ServiceResult registerGlobalHotkey(
        core::WindowId ownerWindow,
        const GlobalHotkeySpec& spec) override;
    [[nodiscard]] ServiceResult unregisterGlobalHotkey(
        const std::string& id) override;
    // M13：记录型 raise（AT 抓焦点链路断言）。
    void raiseWindow(core::WindowId id) override;
    void setWindowDragRegion(
        core::WindowId id,
        std::function<bool(core::Offset)> predicate) override;

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
    // M12：系统主题切换事件（同时覆写能力位 prefersDarkMode，模拟
    // SDL host 的"刷新能力 + 广播"语义）。
    void pushSystemThemeChanged(bool prefersDarkMode);
    void pushSystemAccessibilityChanged(bool highContrast, bool reduceAnimation,
                                        float fontScale);
    // M16：托盘/全局快捷键事件注入（TrayActivated.text = 菜单项
    // command、GlobalHotkey.text = 快捷键 id）。
    void pushTrayActivated(core::WindowId owner, std::string command);
    void pushGlobalHotkey(core::WindowId owner, std::string id);
    // G-3：剪贴板内容变更事件注入（会话级，window 为空）。
    void pushClipboardChanged();
    // M15：OS 拖入会话事件注入（DragEnter/Move/Leave 只带位置；Drop 携带
    // 文本或文件负载——与宿主翻译后的归一化字段一致）。
    void pushDragEnter(core::WindowId id, core::Offset position);
    void pushDragMove(core::WindowId id, core::Offset position);
    void pushDragDropText(core::WindowId id, core::Offset position,
                          std::string text);
    void pushDragDropFiles(core::WindowId id, core::Offset position,
                           std::vector<std::string> paths);
    void pushDragLeave(core::WindowId id, core::Offset position);

    // --- 状态驱动：先更新 WindowMetrics，再入队事件（plan §3.1） ---
    void setLifecycle(core::AppLifecycle next);
    void resizeWindow(core::WindowId id, core::Size logical);
    // DPI 变化：drawableSize = logicalSize * scale 先行更新。
    void changeDeviceScale(core::WindowId id, float scale);
    void setSafeArea(core::WindowId id, core::EdgeInsets safeArea);
    // minimizeWindow 已升级为窗口操作 override（原状态驱动语义保留）。
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
    // M15：注入拖出失败；空 = 成功。
    void setDragStartFailure(ServiceResult failure);
    // M16：注入托盘/快捷键失败；空 = 成功。
    void setTrayFailure(ServiceResult failure);
    void setHotkeyFailure(ServiceResult failure);

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
    // M15：拖出调用记录（结果 = 成功或注入的失败）。
    struct DragStartCall {
        core::WindowId window{};
        DragOutPayload payload{};
        ServiceResult result{};
        bool operator==(const DragStartCall&) const = default;
    };
    std::vector<DragStartCall> dragStartCalls{};
    // 自定义标题栏：窗口操作记录（minimize/maximize-toggle/close）与
    // 各窗口拖拽区谓词（测试直接调用谓词断言注册结果）。
    std::vector<std::string> windowCommandCalls{};
    std::map<core::WindowId, std::function<bool(core::Offset)>> dragRegions{};
    // M16：置顶/模态记录（fullscreen 走 windowCommandCalls + 事件）。
    std::vector<std::pair<core::WindowId, bool>> alwaysOnTopCalls{};
    struct ModalCall {
        core::WindowId window{};
        core::WindowId parent{};
        ServiceResult result{};
        bool operator==(const ModalCall&) const = default;
    };
    std::vector<ModalCall> modalCalls{};
    // M16：托盘/快捷键记录。
    struct TrayCall {
        core::WindowId window{};
        TraySetup setup{};
        ServiceResult result{};
        bool operator==(const TrayCall&) const = default;
    };
    std::vector<TrayCall> trayCalls{};
    int removeTrayCalls{0};
    struct HotkeyCall {
        core::WindowId window{};
        GlobalHotkeySpec spec{};
        ServiceResult result{};
        bool operator==(const HotkeyCall&) const = default;
    };
    std::vector<HotkeyCall> hotkeyCalls{};
    int hotkeyUnregisterCalls{0};

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
    std::optional<ServiceResult> dragStartFailure_{};
    std::optional<ServiceResult> trayFailure_{};
    std::optional<ServiceResult> hotkeyFailure_{};
};

}  // namespace lumen::platform
