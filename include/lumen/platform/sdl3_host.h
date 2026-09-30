#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lumen/platform/application_host.h"

namespace lumen::platform {

// M16：托盘菜单项回调上下文（定义在 .cpp；宿主持有、与托盘同寿命）。
struct TrayEntryContext;

namespace native {
class AccessibilityPreferenceMonitor;
struct SystemAccessibilityPreferences;
}

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
    // M15：拖出（SDL 3.2.10 无 API——结构化 Unavailable，能力位 false）。
    [[nodiscard]] ServiceResult startDrag(
        core::WindowId id, const DragOutPayload& payload) override;
    // --- M16：窗口能力 ---
    void toggleFullscreen(core::WindowId id) override;
    void setAlwaysOnTop(core::WindowId id, bool onTop) override;
    [[nodiscard]] ServiceResult setWindowModal(core::WindowId id,
                                               core::WindowId parent) override;
    // --- M16：系统托盘（SDL_tray）与全局快捷键（契约降级） ---
    [[nodiscard]] ServiceResult setTray(core::WindowId ownerWindow,
                                        const TraySetup& tray) override;
    void removeTray() override;
    [[nodiscard]] ServiceResult registerGlobalHotkey(
        core::WindowId ownerWindow,
        const GlobalHotkeySpec& spec) override;
    [[nodiscard]] ServiceResult unregisterGlobalHotkey(
        const std::string& id) override;

    // --- 自定义标题栏（lumen-titlebar-design §4.3） ---
    void minimizeWindow(core::WindowId id) override;
    void toggleMaximizeWindow(core::WindowId id) override;
    void requestWindowClose(core::WindowId id) override;
    // M13：AT 抓焦点 → 抬升窗口（GrabFocus 平台惯例）。
    void raiseWindow(core::WindowId id) override;
    void setWindowDragRegion(
        core::WindowId id,
        std::function<bool(core::Offset)> predicate) override;

    // --- M13：原生无障碍桥装配 ---
    [[nodiscard]] void* nativeWindowHandle(core::WindowId id) const override;
    void noteAccessibilityBridgeActive(bool active) override;

    // 托盘回调入队（.cpp 内 SDL 签名回调经此转交；无 SDL 类型）。
    void noteTrayActivation(core::WindowId window,
                            const std::string& command);

  private:
    class Sdl3Clipboard;
    class Sdl3TextInputSession;

    struct WindowEntry {
        std::unique_ptr<PlatformWindow> window{};
        std::unique_ptr<TextInputSession> textInput{};
        // M4：窗口级系统光标缓存（不透明指针：SDL 类型不出公共头）。
        void* cursor{nullptr};
        SystemCursor cursorShape{SystemCursor::Arrow};
        // 自定义标题栏：拖拽区谓词（窗口逻辑坐标）；空 = 无拖拽区。
        std::function<bool(core::Offset)> dragRegion{};
        // M15：本次 OS 拖入会话是否已交付负载（DROP_COMPLETE 时无负载
        // 则合成 DragLeave）。
        bool dropDelivered{false};
        // M16：全屏状态（toggleFullscreen 翻转；事件翻译回填）。
        bool fullscreen{false};
        // M16：置顶请求缓存（SDL 无查询；重复设置去重用）。
        bool alwaysOnTop{false};
    };

    // M4：文件对话框异步完成暂存（回调线程填充，pollEvent 消费）。
    struct PendingDialog;
    static void dialogCallback(void* userdata, const char* const* filelist,
                               int filter);

    // M16：托盘。SDL_tray 回调可能非 UI 线程并发——经互斥队列暂存，
    // pollEvent 转 TrayActivated（PendingDialog 同模式）。tray_ 为
    // SDL_Tray* 不透明指针（SDL 类型不出公共头）；回调实现（SDL 类型
    // 签名）留在 .cpp 内部。
    struct PendingTrayActivation {
        core::WindowId window{};
        std::string command{};
    };
    void destroyTray();

    [[nodiscard]] WindowEntry* find(core::WindowId id);
    // 把单个 SDL 事件翻译为 0..n 条归一化事件；返回入队条数。
    std::size_t translateEvent(void* sdlEvent,
                               std::vector<core::HostEvent>& out);
    void refreshLifecycle();
    void refreshNativeAccessibilityPreferences();
    bool applyNativeAccessibilityPreferences(const native::SystemAccessibilityPreferences&);

    bool initialized_{false};
    core::AppLifecycle lifecycle_{core::AppLifecycle::Launching};
    std::map<std::uint64_t, WindowEntry> windows_{};
    // 单个 SDL 事件翻译出的多余归一化事件（本实现至多 1:1，预留）。
    std::deque<core::HostEvent> pending_{};
    std::unique_ptr<Sdl3Clipboard> clipboard_{};
    PlatformCapabilities capabilities_{};
    std::unique_ptr<native::AccessibilityPreferenceMonitor> accessibilityPreferences_{};
    // M4：进行中的文件对话框（至多几个；完成后移除）。
    std::vector<std::unique_ptr<PendingDialog>> dialogs_{};
    // M16：系统托盘（SDL_Tray* 不透明；空 = 未安装）与激活暂存。
    // trayContexts_ 持有菜单项回调上下文（SDL 条目持久存在、可多次
    // 激活——上下文必须与托盘同寿命，随 destroyTray 清空）。
    void* tray_{nullptr};
    std::mutex trayMutex_{};
    std::deque<PendingTrayActivation> trayPending_{};
    std::vector<std::unique_ptr<TrayEntryContext>> trayContexts_{};
};

}  // namespace lumen::platform
