#include "lumen/platform/fake_host.h"

#include <utility>

namespace lumen::platform {

FakeApplicationHost::FakeApplicationHost(ManualHostClock* clock)
    : clock_(clock != nullptr ? clock : &ownedClock_) {
    capabilities_.clipboard = true;
    capabilities_.textInput = true;
    capabilities_.ime = true;
    capabilities_.keyboard = true;
    capabilities_.mouse = true;
    capabilities_.touch = true;
    capabilities_.multiWindow = true;
    // setWindowIcon 记录进 iconCalls（runApp 窗口图标装配的 headless
    // 验收入口，gallery_icon_tests）。
    capabilities_.windowIcon = true;
    capabilities_.adapterName = "fake";
}

bool FakeApplicationHost::initialize() {
    if (initialized_) {
        return true;
    }
    initialized_ = true;
    setLifecycle(core::AppLifecycle::Active);
    return true;
}

void FakeApplicationHost::shutdown() {
    if (!initialized_) {
        return;
    }
    setLifecycle(core::AppLifecycle::Terminating);
    windows_.clear();
    initialized_ = false;
}

core::AppLifecycle FakeApplicationHost::lifecycle() const {
    return lifecycle_;
}

bool FakeApplicationHost::pollEvent(core::HostEvent& out) {
    if (queue_.empty()) {
        out = core::HostEvent{};
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

std::optional<core::WindowId> FakeApplicationHost::createWindow(
    const WindowDesc& desc) {
    if (!initialized_) {
        return std::nullopt;
    }
    core::WindowId id{nextWindowId_++};
    WindowEntry entry;
    entry.metrics.logicalSize =
        core::Size{static_cast<float>(desc.width),
                   static_cast<float>(desc.height)};
    entry.metrics.drawableSize = entry.metrics.logicalSize;
    entry.metrics.deviceScale = 1.0F;
    entry.focused = true;
    windows_.emplace(id, std::move(entry));
    return id;
}

void FakeApplicationHost::destroyWindow(core::WindowId id) {
    windows_.erase(id);
}

std::optional<core::WindowMetrics> FakeApplicationHost::windowMetrics(
    core::WindowId id) const {
    const auto it = windows_.find(id);
    if (it == windows_.end()) {
        return std::nullopt;
    }
    return it->second.metrics;
}

std::vector<core::WindowId> FakeApplicationHost::windowIds() const {
    std::vector<core::WindowId> ids;
    ids.reserve(windows_.size());
    for (const auto& [id, entry] : windows_) {
        ids.push_back(id);
    }
    return ids;
}

Clipboard* FakeApplicationHost::clipboard() { return &clipboard_; }

TextInputSession* FakeApplicationHost::textInputSession(core::WindowId id) {
    WindowEntry* entry = find(id);
    return entry != nullptr ? &entry->textInput : nullptr;
}

PlatformCapabilities FakeApplicationHost::capabilities() const {
    return capabilities_;
}

void FakeApplicationHost::noteAccessibilityBridgeActive(bool active) {
    capabilities_.accessibility = active;
}

void FakeApplicationHost::pushPointerDown(
    core::WindowId id, core::Offset position, core::PointerDevice device,
    std::uint32_t pointerId) {
    core::HostEvent event = makeEvent(core::HostEventType::PointerDown, id);
    event.position = position;
    event.device = device;
    event.pointerId = pointerId;
    event.button = core::PointerButton::Primary;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushPointerUp(
    core::WindowId id, core::Offset position, core::PointerDevice device,
    std::uint32_t pointerId) {
    core::HostEvent event = makeEvent(core::HostEventType::PointerUp, id);
    event.position = position;
    event.device = device;
    event.pointerId = pointerId;
    event.button = core::PointerButton::Primary;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushPointerMove(
    core::WindowId id, core::Offset position, core::PointerDevice device,
    std::uint32_t pointerId) {
    core::HostEvent event = makeEvent(core::HostEventType::PointerMove, id);
    event.position = position;
    event.device = device;
    event.pointerId = pointerId;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushPointerCancel(core::WindowId id,
                                            core::Offset position,
                                            std::uint32_t pointerId) {
    core::HostEvent event = makeEvent(core::HostEventType::PointerCancel, id);
    event.position = position;
    event.pointerId = pointerId;
    event.device = core::PointerDevice::Touch;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushWheel(core::WindowId id, core::Offset position,
                                    core::Offset delta) {
    core::HostEvent event = makeEvent(core::HostEventType::Wheel, id);
    event.position = position;
    event.scrollDelta = delta;
    event.device = core::PointerDevice::Mouse;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushKeyDown(core::WindowId id, core::Key key,
                                      core::KeyModifiers modifiers,
                                      char keyChar) {
    core::HostEvent event = makeEvent(core::HostEventType::KeyDown, id);
    event.keyCode = key;
    event.modifiers = modifiers;
    event.keyChar = keyChar;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushKeyUp(core::WindowId id, core::Key key,
                                    core::KeyModifiers modifiers) {
    core::HostEvent event = makeEvent(core::HostEventType::KeyUp, id);
    event.keyCode = key;
    event.modifiers = modifiers;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushTextInput(core::WindowId id, std::string text) {
    core::HostEvent event = makeEvent(core::HostEventType::TextInput, id);
    event.text = std::move(text);
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushTextEditing(core::WindowId id,
                                          std::string preedit, int cursor,
                                          int length) {
    core::HostEvent event = makeEvent(core::HostEventType::TextEditing, id);
    event.text = std::move(preedit);
    event.editCursor = cursor;
    event.editLength = length;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::pushQuit() {
    queue_.push_back(makeEvent(core::HostEventType::Quit, {}));
}

void FakeApplicationHost::pushCloseRequest(core::WindowId id) {
    queue_.push_back(makeEvent(core::HostEventType::WindowCloseRequested, id));
}

void FakeApplicationHost::pushSystemAccessibilityChanged(
    bool highContrast, bool reduceAnimation, float fontScale) {
    capabilities_.highContrast = highContrast;
    capabilities_.reduceAnimation = reduceAnimation;
    capabilities_.fontScale = fontScale;
    capabilities_.systemAccessibilityPreferences = true;
    queue_.push_back(makeEvent(core::HostEventType::SystemAccessibilityChanged, {}));
}

void FakeApplicationHost::pushSystemThemeChanged(bool prefersDarkMode) {
    // 与 SDL host 同语义：能力位刷新 + 事件广播（应用经 onEvent 消费）。
    capabilities_.prefersDarkMode = prefersDarkMode;
    queue_.push_back(makeEvent(core::HostEventType::SystemThemeChanged, {}));
}

void FakeApplicationHost::setLifecycle(core::AppLifecycle next) {
    if (next == lifecycle_) {
        return;
    }
    core::HostEvent event = makeEvent(core::HostEventType::LifecycleChanged, {});
    event.previousLifecycle = lifecycle_;
    event.lifecycle = next;
    queue_.push_back(std::move(event));
    lifecycle_ = next;
}

void FakeApplicationHost::resizeWindow(core::WindowId id, core::Size logical) {
    WindowEntry* entry = find(id);
    if (entry == nullptr) {
        return;
    }
    const float scale = entry->metrics.deviceScale;
    entry->metrics.logicalSize = logical;
    entry->metrics.drawableSize =
        core::Size{logical.width * scale, logical.height * scale};
    core::HostEvent event = makeEvent(core::HostEventType::Resize, id);
    event.pixelSize = entry->metrics.drawableSize;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::changeDeviceScale(core::WindowId id, float scale) {
    WindowEntry* entry = find(id);
    if (entry == nullptr) {
        return;
    }
    // metrics 先行：DPI 事件到达时 drawableSize 已反映新缩放（plan §3.1:
    // 窗口重建/DPI 变化必须先更新 WindowMetrics 再请求 FrameScheduler）。
    entry->metrics.deviceScale = scale;
    entry->metrics.drawableSize = core::Size{
        entry->metrics.logicalSize.width * scale,
        entry->metrics.logicalSize.height * scale};
    core::HostEvent event = makeEvent(core::HostEventType::DpiChanged, id);
    event.pixelSize = entry->metrics.drawableSize;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::setSafeArea(core::WindowId id,
                                      core::EdgeInsets safeArea) {
    WindowEntry* entry = find(id);
    if (entry != nullptr) {
        entry->metrics.safeArea = safeArea;
    }
}

void FakeApplicationHost::minimizeWindow(core::WindowId id) {
    windowCommandCalls.push_back("minimize");
    WindowEntry* entry = find(id);
    core::WindowId target = id;
    if (entry == nullptr && !windows_.empty()) {
        // 与 SDL host 同语义：应用回调常无窗口 id 上下文（main.cpp 注入
        // {}），单窗口便捷路径挂靠首个窗口。
        target = windows_.begin()->first;
        entry = &windows_.begin()->second;
    }
    if (entry == nullptr || entry->metrics.minimized) {
        return;
    }
    entry->metrics.minimized = true;
    entry->metrics.visible = false;
    queue_.push_back(makeEvent(core::HostEventType::WindowMinimized, target));
}

void FakeApplicationHost::raiseWindow(core::WindowId id) {
    windowCommandCalls.push_back("raise");
    (void)id;
}

void FakeApplicationHost::toggleMaximizeWindow(core::WindowId id) {
    WindowEntry* entry = find(id);
    core::WindowId target = id;
    if (entry == nullptr) {
        if (windows_.empty()) {
            return;
        }
        target = windows_.begin()->first;
        entry = &windows_.begin()->second;
    }
    windowCommandCalls.push_back(entry->metrics.maximized ? "restore"
                                                          : "maximize");
    entry->metrics.maximized = !entry->metrics.maximized;
    queue_.push_back(makeEvent(entry->metrics.maximized
                                   ? core::HostEventType::WindowMaximized
                                   : core::HostEventType::WindowRestored,
                               target));
}

void FakeApplicationHost::requestWindowClose(core::WindowId id) {
    windowCommandCalls.push_back("close");
    // 与 SDL host 同语义：合成 WindowCloseRequested 事件（应用侧
    // requestClose 统一拦截规则）；无效 id 挂靠首个窗口。
    core::WindowId target = id;
    if (windows_.find(target) == windows_.end()) {
        if (windows_.empty()) {
            return;
        }
        target = windows_.begin()->first;
    }
    queue_.push_back(makeEvent(core::HostEventType::WindowCloseRequested, target));
}

void FakeApplicationHost::setWindowDragRegion(
    core::WindowId id, std::function<bool(core::Offset)> predicate) {
    dragRegions[id] = std::move(predicate);
}

void FakeApplicationHost::restoreWindow(core::WindowId id) {
    WindowEntry* entry = find(id);
    if (entry == nullptr || !entry->metrics.minimized) {
        return;
    }
    entry->metrics.minimized = false;
    entry->metrics.visible = entry->surfaceAttached;
    core::HostEvent event = makeEvent(core::HostEventType::WindowRestored, id);
    event.pixelSize = entry->metrics.drawableSize;
    queue_.push_back(std::move(event));
}

void FakeApplicationHost::setWindowFocus(core::WindowId id, bool focused) {
    WindowEntry* entry = find(id);
    if (entry == nullptr || entry->focused == focused) {
        return;
    }
    entry->focused = focused;
    queue_.push_back(makeEvent(
        focused ? core::HostEventType::WindowFocusGained
                : core::HostEventType::WindowFocusLost,
        id));
}

void FakeApplicationHost::detachSurface(core::WindowId id) {
    WindowEntry* entry = find(id);
    if (entry == nullptr || !entry->surfaceAttached) {
        return;
    }
    entry->surfaceAttached = false;
    entry->metrics.visible = false;
    queue_.push_back(makeEvent(core::HostEventType::SurfaceDetached, id));
}

void FakeApplicationHost::reattachSurface(core::WindowId id) {
    WindowEntry* entry = find(id);
    if (entry == nullptr || entry->surfaceAttached) {
        return;
    }
    entry->surfaceAttached = true;
    entry->metrics.visible = !entry->metrics.minimized;
    core::HostEvent event =
        makeEvent(core::HostEventType::SurfaceReattached, id);
    event.pixelSize = entry->metrics.drawableSize;
    queue_.push_back(std::move(event));
}

FakeClipboard* FakeApplicationHost::fakeClipboard() { return &clipboard_; }

FakeTextInputSession* FakeApplicationHost::fakeTextInputSession(
    core::WindowId id) {
    WindowEntry* entry = find(id);
    return entry != nullptr ? &entry->textInput : nullptr;
}

void FakeApplicationHost::setCapabilities(PlatformCapabilities capabilities) {
    capabilities_ = std::move(capabilities);
}

void FakeApplicationHost::pushRaw(core::HostEvent event) {
    queue_.push_back(std::move(event));
}

FakeApplicationHost::WindowEntry* FakeApplicationHost::find(
    core::WindowId id) {
    const auto it = windows_.find(id);
    return it == windows_.end() ? nullptr : &it->second;
}

core::HostEvent FakeApplicationHost::makeEvent(core::HostEventType type,
                                               core::WindowId id) {
    core::HostEvent event;
    event.type = type;
    event.timestampMs = nowMs();
    event.window = id;
    return event;
}

std::uint64_t FakeApplicationHost::nowMs() const { return clock_->nowMs; }

// --- M4：平台服务（确定性记录 + 失败注入） ---

ServiceResult FakeApplicationHost::openUrl(const std::string& url) {
    ServiceResult result =
        openUrlFailure_.has_value() ? *openUrlFailure_
                                    : ServiceResult::success();
    openUrlCalls.push_back(OpenUrlCall{url, result});
    return result;
}

ServiceResult FakeApplicationHost::requestFileDialog(
    core::WindowId id, const FileDialogRequest& request) {
    fileDialogCalls.push_back(FileDialogCall{id, request});
    if (dialogFailure_.has_value()) {
        return *dialogFailure_;  // 请求期失败（同步，结构化）。
    }
    if (dialogResults_.empty()) {
        return ServiceResult::unavailable(
            "fake host: no queued dialog result");
    }
    // 完成：异步语义——结果入队为 FileDialogCompleted 事件。
    FileDialogResult result = std::move(dialogResults_.front());
    dialogResults_.pop_front();
    core::HostEvent event = makeEvent(core::HostEventType::FileDialogCompleted,
                                      id);
    event.filePaths = std::move(result.paths);
    if (!result.status.ok && result.status.error != ServiceError::Cancelled) {
        event.text = result.status.message;
    }
    queue_.push_back(std::move(event));
    return ServiceResult::success();
}

ServiceResult FakeApplicationHost::postNotification(
    const NotificationRequest& request) {
    ServiceResult result =
        notificationFailure_.has_value() ? *notificationFailure_
                                         : ServiceResult::success();
    notificationCalls.push_back(NotificationCall{request, result});
    return result;
}

void FakeApplicationHost::setCursor(core::WindowId id, SystemCursor cursor) {
    cursorCalls.emplace_back(id, cursor);
}

ServiceResult FakeApplicationHost::setWindowIcon(core::WindowId id,
                                                 const WindowIcon& icon) {
    iconCalls.push_back(IconCall{id, icon});
    return iconFailure_.has_value() ? *iconFailure_
                                    : ServiceResult::success();
}

void FakeApplicationHost::queueFileDialogResult(FileDialogResult result) {
    dialogResults_.push_back(std::move(result));
}

void FakeApplicationHost::setFileDialogFailure(ServiceResult failure) {
    dialogFailure_ = std::move(failure);
}

void FakeApplicationHost::setOpenUrlFailure(ServiceResult failure) {
    openUrlFailure_ = std::move(failure);
}

void FakeApplicationHost::setNotificationFailure(ServiceResult failure) {
    notificationFailure_ = std::move(failure);
}

void FakeApplicationHost::setIconFailure(ServiceResult failure) {
    iconFailure_ = std::move(failure);
}

}  // namespace lumen::platform
