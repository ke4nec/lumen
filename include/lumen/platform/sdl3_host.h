#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "lumen/platform/application_host.h"

namespace lumen::platform {

// v0.3 阶段8A: SDL3 桌面 ApplicationHost。Windows/Linux/macOS 共用；
// SDL 类型全部留在实现内（AGENTS.md）。
//
// 事件归一化：SDL_WindowID 即 lumen::core::WindowId；修饰键、逻辑/物理
// 键、指针设备、pointer id、滚轮增量和取消事件都在这里填充（plan §3.1）。
class Sdl3ApplicationHost final : public ApplicationHost {
  public:
    Sdl3ApplicationHost();
    ~Sdl3ApplicationHost() override;

    Sdl3ApplicationHost(const Sdl3ApplicationHost&) = delete;
    Sdl3ApplicationHost& operator=(const Sdl3ApplicationHost&) = delete;

    bool initialize() override;
    void shutdown() override;
    [[nodiscard]] core::AppLifecycle lifecycle() const override;
    bool pollEvent(core::HostEvent& out) override;
    void waitForEvents(std::uint32_t timeoutMs) override;

    std::optional<core::WindowId> createWindow(const WindowDesc& desc) override;
    void destroyWindow(core::WindowId id) override;
    [[nodiscard]] std::optional<core::WindowMetrics> windowMetrics(
        core::WindowId id) const override;
    [[nodiscard]] std::vector<core::WindowId> windowIds() const override;
    [[nodiscard]] PlatformWindow* platformWindow(
        core::WindowId id) const override;

    [[nodiscard]] Clipboard* clipboard() override;
    [[nodiscard]] TextInputSession* textInputSession(
        core::WindowId id) override;
    [[nodiscard]] PlatformCapabilities capabilities() const override;

    // --- M4：平台服务 ---
    [[nodiscard]] ServiceResult openUrl(const std::string& url) override;
    [[nodiscard]] ServiceResult requestFileDialog(
        core::WindowId id, const FileDialogRequest& request) override;
    [[nodiscard]] ServiceResult postNotification(
        const NotificationRequest& request) override;
    void setCursor(core::WindowId id, SystemCursor cursor) override;
    [[nodiscard]] ServiceResult setWindowIcon(
        core::WindowId id, const WindowIcon& icon) override;

  private:
    class Sdl3Clipboard;
    class Sdl3TextInputSession;

    struct WindowEntry {
        std::unique_ptr<PlatformWindow> window{};
        std::unique_ptr<TextInputSession> textInput{};
        // M4：窗口级系统光标缓存（不透明指针：SDL 类型不出公共头）。
        void* cursor{nullptr};
        SystemCursor cursorShape{SystemCursor::Arrow};
    };

    // M4：文件对话框异步完成暂存（回调线程填充，pollEvent 消费）。
    struct PendingDialog;
    static void dialogCallback(void* userdata, const char* const* filelist,
                               int filter);

    [[nodiscard]] WindowEntry* find(core::WindowId id);
    // 把单个 SDL 事件翻译为 0..n 条归一化事件；返回入队条数。
    std::size_t translateEvent(void* sdlEvent,
                               std::vector<core::HostEvent>& out);
    void refreshLifecycle();

    bool initialized_{false};
    core::AppLifecycle lifecycle_{core::AppLifecycle::Launching};
    std::map<std::uint64_t, WindowEntry> windows_{};
    // 单个 SDL 事件翻译出的多余归一化事件（本实现至多 1:1，预留）。
    std::deque<core::HostEvent> pending_{};
    std::unique_ptr<Sdl3Clipboard> clipboard_{};
    PlatformCapabilities capabilities_{};
    // M4：进行中的文件对话框（至多几个；完成后移除）。
    std::vector<std::unique_ptr<PendingDialog>> dialogs_{};
};

}  // namespace lumen::platform
