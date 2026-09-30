#include "lumen/platform/sdl3_host.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "lumen/platform/sdl3_window.h"
#include "global_hotkeys.h"
#include "native_services.h"

namespace lumen::platform {
namespace {

// SDL 滚轮单位（行/刻度）到逻辑像素的换算；wheel 事件语义按逻辑像素
// 定义（plan §3.1 滚轮/触摸增量）。
constexpr float kWheelUnitPx = 40.0F;

// 自定义标题栏（lumen-titlebar-design §4.2）：hit-test 回调。回调在
// SDL_PumpEvents（本仓 UI 线程泵）内触发，读应用谓词无数据竞争；
// userdata = WindowEntry.dragRegion 的地址（map 节点稳定，destroyWindow
// 前注销）。坐标与指针事件同为窗口逻辑坐标。
SDL_HitTestResult SDLCALL titleBarHitTest(SDL_Window* window,
                                          const SDL_Point* area,
                                          void* userdata) {
    const auto* region =
        static_cast<const std::function<bool(core::Offset)>*>(userdata);
    if (window == nullptr || area == nullptr) {
        return SDL_HITTEST_NORMAL;
    }
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    const int x = area->x;
    const int y = area->y;
    // 最大化态无 resize 边（原生行为一致）：边缘落点走 caption 谓词，
    // 使标题栏拖动可还原/移动（unsnap），避免边缘返回 RESIZE 拦截拖拽。
    const bool maximized =
        (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
    // resize 边带：边 8 逻辑 px、角 12×12（角覆盖边，与系统行为一致）。
    constexpr int kEdge = 8;
    constexpr int kCorner = 12;
    const bool left = x < kEdge;
    const bool right = x >= width - kEdge;
    const bool top = y < kEdge;
    const bool bottom = y >= height - kEdge;
    if (!maximized && (left || right || top || bottom)) {
        if (x < kCorner && y < kCorner) {
            return SDL_HITTEST_RESIZE_TOPLEFT;
        }
        if (x >= width - kCorner && y < kCorner) {
            return SDL_HITTEST_RESIZE_TOPRIGHT;
        }
        if (x < kCorner && y >= height - kCorner) {
            return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        }
        if (x >= width - kCorner && y >= height - kCorner) {
            return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        }
        if (top) {
            return SDL_HITTEST_RESIZE_TOP;
        }
        if (bottom) {
            return SDL_HITTEST_RESIZE_BOTTOM;
        }
        if (left) {
            return SDL_HITTEST_RESIZE_LEFT;
        }
        return SDL_HITTEST_RESIZE_RIGHT;
    }
    if (region != nullptr &&
        (*region)(core::Offset{static_cast<float>(x), static_cast<float>(y)})) {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

core::Key mapSdlKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_BACKSPACE:
            return core::Key::Backspace;
        case SDLK_TAB:
            return core::Key::Tab;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return core::Key::Enter;
        case SDLK_ESCAPE:
            return core::Key::Escape;
        case SDLK_LEFT:
            return core::Key::Left;
        case SDLK_RIGHT:
            return core::Key::Right;
        case SDLK_UP:
            return core::Key::Up;
        case SDLK_DOWN:
            return core::Key::Down;
        case SDLK_HOME:
            return core::Key::Home;
        case SDLK_END:
            return core::Key::End;
        case SDLK_DELETE:
            return core::Key::Delete;
        case SDLK_PAGEUP:
            return core::Key::PageUp;
        case SDLK_PAGEDOWN:
            return core::Key::PageDown;
        default:
            return core::Key::None;
    }
}

core::KeyModifiers mapSdlModifiers(SDL_Keymod mod) {
    std::uint32_t modifiers = core::kModifierNone;
    if ((mod & SDL_KMOD_SHIFT) != 0) {
        modifiers |= core::kModifierShift;
    }
    if ((mod & SDL_KMOD_CTRL) != 0) {
        modifiers |= core::kModifierCtrl;
    }
    if ((mod & SDL_KMOD_ALT) != 0) {
        modifiers |= core::kModifierAlt;
    }
    if ((mod & SDL_KMOD_GUI) != 0) {
        modifiers |= core::kModifierGui;
    }
    return modifiers;
}

// 可打印字符（Ctrl/Command/Alt 和弦判定用；G-1 命令分发与编辑和弦需
// 要修饰键下的字母）；SDL 逻辑键码在 ASCII 范围内直接对应字符。消费方
//（编辑和弦/菜单 mnemonic/命令匹配）自行归一大小写；带 Ctrl/Alt/Gui 的
// 空格激活需调用方排除（interaction 树键处理与菜单行激活均已排除）。
char mapKeyChar(SDL_Keycode key, core::KeyModifiers modifiers) {
    (void)modifiers;
    if (key >= 0x20 && key < 0x7F) {
        return static_cast<char>(key);
    }
    return 0;
}

core::PointerButton mapMouseButton(std::uint8_t button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return core::PointerButton::Primary;
        case SDL_BUTTON_RIGHT:
            return core::PointerButton::Secondary;
        case SDL_BUTTON_MIDDLE:
            return core::PointerButton::Middle;
        default:
            return core::PointerButton::None;
    }
}

}  // namespace

// --- Clipboard（SDL3 实现；失败时应用侧安全降级） ---
// G-3：MIME 数据层走 SDL3 通用剪贴板 data API（SDL_GetClipboardData/
// SDL_SetClipboardData，3.2 起三桌面可用；能力差异进 PlatformCapabilities
// 如实报告——docs/lumen-clipboard-service-design.md §3）。
class Sdl3ApplicationHost::Sdl3Clipboard final : public Clipboard {
  public:
    [[nodiscard]] bool hasText() const override {
        return SDL_HasClipboardText();
    }
    [[nodiscard]] std::string text() const override {
        char* value = SDL_GetClipboardText();
        if (value == nullptr) {
            return {};
        }
        std::string result = value;
        SDL_free(value);
        return result;
    }
    bool setText(const std::string& value) override {
        return SDL_SetClipboardText(value.c_str());
    }
    void clear() override { (void)SDL_SetClipboardText(""); }

    // --- G-3：MIME 数据层 ---
    [[nodiscard]] bool hasFormat(const std::string& mimeType) const override {
        if (mimeType == kMimeText) {
            return SDL_HasClipboardText();
        }
        return SDL_HasClipboardData(mimeType.c_str());
    }
    [[nodiscard]] std::vector<std::uint8_t> data(
        const std::string& mimeType) const override {
        if (mimeType == kMimeText) {
            const std::string value = text();
            return {value.begin(), value.end()};
        }
        std::size_t size = 0;
        void* raw = SDL_GetClipboardData(mimeType.c_str(), &size);
        if (raw == nullptr) {
            return {};
        }
        const auto* bytes = static_cast<const std::uint8_t*>(raw);
        std::vector<std::uint8_t> result(bytes, bytes + size);
        SDL_free(raw);
        return result;
    }
    bool setData(const std::string& mimeType,
                 const std::vector<std::uint8_t>& bytes) override {
        return setFormats({Entry{mimeType, bytes}});
    }
    // 多格式原子放置：字节所有权交 SDL 按需回调（其他进程粘贴时取数）；
    // cleanup 释放；SDL_SetClipboardData 整体替换剪贴板。
    bool setFormats(const std::vector<Entry>& entries) override {
        if (entries.empty()) {
            return SDL_ClearClipboardData();
        }
        // 结构化拒绝超限（而非静默截断——review L-5）：应用拆批或减项。
        if (entries.size() > 16) {
            return false;
        }
        auto* payload = new std::vector<Entry>(entries);
        const char* mimeTypes[16] = {};
        const std::size_t count = entries.size();
        for (std::size_t i = 0; i < count; ++i) {
            mimeTypes[i] = (*payload)[i].mimeType.c_str();
        }
        const bool ok = SDL_SetClipboardData(
            [](void* userdata, const char* mimeType, std::size_t* size)
                -> const void* {
                auto* items = static_cast<std::vector<Entry>*>(userdata);
                for (const auto& entry : *items) {
                    if (entry.mimeType == mimeType) {
                        *size = entry.bytes.size();
                        return entry.bytes.data();
                    }
                }
                return nullptr;
            },
            [](void* userdata) {
                delete static_cast<std::vector<Entry>*>(userdata);
            },
            payload, mimeTypes, count);
        if (!ok) {
            delete payload;
            return false;
        }
        return true;
    }
    [[nodiscard]] std::vector<std::string> formats() const override {
        std::vector<std::string> result;
        if (SDL_HasClipboardText()) {
            result.push_back(kMimeText);
        }
        std::size_t count = 0;
        char** mimeTypes = SDL_GetClipboardMimeTypes(&count);
        if (mimeTypes == nullptr) {
            return result;
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (mimeTypes[i] == nullptr) {
                continue;
            }
            // 去重（SDL 枚举可能含 text/plain 变体/重复项——review L-5）。
            if (std::find(result.begin(), result.end(),
                          std::string(mimeTypes[i])) == result.end()) {
                result.emplace_back(mimeTypes[i]);
            }
        }
        SDL_free(mimeTypes);
        return result;
    }

  private:
    static constexpr const char* kMimeText =
        core::ClipboardProvider::kMimeText;
};

// --- TextInputSession（包装既有 PlatformWindow 文本输入接口） ---
// --- M4：文件对话框（SDL 异步回调 → 事件入队） ---
// SDL 文档：回调“可能在另一线程”触发——结果经互斥写入 pending，
// pollEvent（UI 线程）持锁取走并转为 FileDialogCompleted 事件，全程
// 不阻塞 UI 线程。宿主销毁后仍可能触发的回调写入进程级“孤儿”列表
//（SDL 3.2 无取消 API；数量有限，进程生命周期内安全释放由退出兜底）。
namespace {

}  // namespace

struct Sdl3ApplicationHost::PendingDialog {
    core::WindowId window{};
    std::mutex mutex{};
    std::vector<std::string> paths{};
    std::string error{};
    bool done{false};
    // SDL 要求这些过滤器在异步回调完成前保持有效；所有字符串和结构
    // 都由 PendingDialog 持有，避免 requestFileDialog 返回后的悬空指针。
    std::vector<std::string> filterNames{};
    std::vector<std::string> filterPatterns{};
    std::vector<SDL_DialogFileFilter> filters{};
    std::string title{};
    std::string defaultLocation{};

    // 孤儿安置（成员函数可访问私有嵌套类型）：宿主销毁后回调仍需
    // userdata 存活，SDL 3.2 无取消 API；数量有限，随进程终结。
    static void adopt(std::unique_ptr<PendingDialog> dialog);
};

void Sdl3ApplicationHost::PendingDialog::adopt(
    std::unique_ptr<PendingDialog> dialog) {
    static std::mutex mutex;
    static std::vector<std::unique_ptr<PendingDialog>> orphans;
    std::lock_guard<std::mutex> lock(mutex);
    orphans.push_back(std::move(dialog));
}

void Sdl3ApplicationHost::dialogCallback(void* userdata,
                                         const char* const* filelist,
                                         int /*filter*/) {
    auto* pending = static_cast<PendingDialog*>(userdata);
    {
        std::lock_guard<std::mutex> lock(pending->mutex);
        if (filelist != nullptr) {
            for (const char* const* it = filelist; *it != nullptr; ++it) {
                pending->paths.emplace_back(*it);
            }
        } else {
            pending->error = SDL_GetError();
            if (pending->error.empty()) {
                pending->error = "file dialog failed";
            }
        }
        pending->done = true;
    }
}

class Sdl3ApplicationHost::Sdl3TextInputSession final : public TextInputSession {
  public:
    explicit Sdl3TextInputSession(PlatformWindow* window)
        : window_(window) {}

    void start() override {
        if (window_ != nullptr) {
            window_->setTextInputEnabled(true);
            active_ = true;
        }
    }
    void stop() override {
        if (window_ != nullptr) {
            window_->setTextInputEnabled(false);
        }
        active_ = false;
    }
    [[nodiscard]] bool active() const override { return active_; }
    void setEditingState(const TextInputEditingState& state) override {
        if (window_ == nullptr) {
            return;
        }
        // 候选词锚点（caretRect）经 SDL_SetTextInputArea 传给 IME；
        // Linux IBus/Fcitx/Wayland 依赖它跟踪光标。
        window_->setTextInputArea(state.caretRect, 0);
    }

  private:
    PlatformWindow* window_{nullptr};
    bool active_{false};
};

Sdl3ApplicationHost::Sdl3ApplicationHost() = default;

Sdl3ApplicationHost::~Sdl3ApplicationHost() { shutdown(); }

bool Sdl3ApplicationHost::initialize() {
    if (initialized_) {
        return true;
    }
    // 与既有 createSdl3Window 的 SDL_Init 共存（SDL_Init 幂等计数）。
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    // 触摸自翻译：禁用 SDL 的合成鼠标事件，一次触摸不产生重复指针事件。
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    initialized_ = true;
    capabilities_.clipboard = true;
    capabilities_.textInput = true;
    capabilities_.ime = true;
    capabilities_.keyboard = true;
    capabilities_.mouse = true;
    capabilities_.multiWindow = true;
    capabilities_.adapterName = "sdl3";
    // M4：桌面服务可用性（SDL 3.2.10：对话框/URL/光标/图标可用；
    // 通知无 API）。
    capabilities_.fileDialogs = true;
    // M12：原生通知 seam（Win32 气泡 / DBus / AppKit；SDL 3.2 无 API）。
    capabilities_.notifications = native::notificationsAvailable();
    capabilities_.openUrl = true;
    capabilities_.cursorShape = true;
    capabilities_.windowIcon = true;
    // M15：拖入事件随视频子系统到达（DROP_* 翻译见 translateEvent）；
    // 拖出发起 SDL 3.2.10 无 API（startDrag 结构化 Unavailable）。
    capabilities_.dragDropReceive = true;
    capabilities_.dragDropStart = false;
    // M16：窗口能力（SDL3.2：SDL_SetWindowFullscreen/AlwaysOnTop/Modal）。
    capabilities_.windowFullscreen = true;
    capabilities_.windowAlwaysOnTop = true;
    capabilities_.windowModal = true;
    // G-8：窗口位置记忆（SDL_SetWindowPosition/GetWindowPosition 三桌面）。
    capabilities_.windowPosition = true;
    // M16：系统托盘（SDL_tray 跨平台）。全局快捷键走平台接缝（R4）：
    // Linux X11 真实后端（XGrabKey 独立连接）；Wayland/无显示/其他
    // 平台结构化不可用——能力位与注册失败原因一致。
    capabilities_.systemTray = true;
    auto hotkeyProbe = hotkeys::probeGlobalHotkeySession();
    hotkeys_ = hotkeys::createX11Backend(hotkeyProbe);
    if (hotkeys_ != nullptr) {
        capabilities_.globalHotkeys = true;
    } else {
        capabilities_.globalHotkeys = false;
        hotkeyUnavailableReason_ = hotkeyProbe.available
                                       ? "global hotkeys: no display "
                                         "connection"
                                       : hotkeyProbe.reason;
    }
    // G-3：剪贴板深度。MIME 数据读写走 SDL data API（三桌面）；变更事
    // 件 SDL_EVENT_CLIPBOARD_UPDATE（三桌面）。image/png 只对 Linux
    // X11/Wayland 报 true（Windows 注册格式名不匹配、macOS 未真机验
    // 证——四态纪律如实降级，原生 seam 为后续增量）。
    capabilities_.clipboardFormats = true;
    capabilities_.clipboardChange = true;
#if defined(__linux__)
    capabilities_.clipboardImage = true;
#else
    capabilities_.clipboardImage = false;
#endif
    // M12：系统主题查询（SDL_GetSystemTheme，3.2.0 起可用；此前
    // "SDL 3.2 无查询"注释有误）。UNKNOWN 保持安全默认 false。
    capabilities_.prefersDarkMode =
        SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
    if (const std::optional<core::Color> accent =
            native::systemAccentColor()) {
        capabilities_.accentColor = *accent;
    }
    refreshNativeAccessibilityPreferences();
    accessibilityPreferences_ = native::createAccessibilityPreferenceMonitor();
    refreshLifecycle();
    return true;
}

void Sdl3ApplicationHost::refreshNativeAccessibilityPreferences() {
    if (const auto preferences = native::systemAccessibilityPreferences()) {
        applyNativeAccessibilityPreferences(*preferences);
    }
}

bool Sdl3ApplicationHost::applyNativeAccessibilityPreferences(
    const native::SystemAccessibilityPreferences& preferences) {
    const bool changed = !capabilities_.systemAccessibilityPreferences ||
        capabilities_.highContrast != preferences.highContrast ||
        capabilities_.reduceAnimation != preferences.reduceAnimation ||
        capabilities_.fontScale != preferences.fontScale;
    capabilities_.systemAccessibilityPreferences = true;
    capabilities_.highContrast = preferences.highContrast;
    capabilities_.reduceAnimation = preferences.reduceAnimation;
    capabilities_.fontScale = preferences.fontScale;
    return changed;
}

void Sdl3ApplicationHost::shutdown() {
    if (!initialized_) {
        return;
    }
    lifecycle_ = core::AppLifecycle::Terminating;
    // M16：托盘先于视频子系统销毁（回调不再触发）。
    destroyTray();
    // R4：快捷键先于视频子系统释放（独立 X 连接 ungrab + 关闭）。
    hotkeys_.reset();
    hotkeyUnavailableReason_.clear();
    accessibilityPreferences_.reset();
    for (auto& [id, entry] : windows_) {
        if (entry.cursor != nullptr) {
            SDL_DestroyCursor(static_cast<SDL_Cursor*>(entry.cursor));
            entry.cursor = nullptr;
        }
    }
    windows_.clear();
    // 未完成的对话框转移到孤儿列表：SDL 回调可能晚于宿主销毁触发，
    // userdata 必须存活（SDL 3.2 无取消 API；数量有限，随进程终结）。
    if (!dialogs_.empty()) {
        for (auto& dialog : dialogs_) {
            PendingDialog::adopt(std::move(dialog));
        }
        dialogs_.clear();
    }
    clipboard_.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    initialized_ = false;
}

core::AppLifecycle Sdl3ApplicationHost::lifecycle() const {
    return lifecycle_;
}

std::size_t Sdl3ApplicationHost::translateEvent(
    void* raw, std::vector<core::HostEvent>& out) {
    const SDL_Event& sdlEvent = *static_cast<const SDL_Event*>(raw);
    const std::uint64_t timestamp = SDL_GetTicks();
    const auto push = [&](core::HostEvent event) {
        event.timestampMs = timestamp;
        out.push_back(std::move(event));
    };
    const auto windowIdOf = [&](SDL_WindowID id) {
        return core::WindowId{static_cast<std::uint64_t>(id)};
    };

    switch (sdlEvent.type) {
        case SDL_EVENT_QUIT: {
            core::HostEvent event;
            event.type = core::HostEventType::Quit;
            push(std::move(event));
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (windows_.count(sdlEvent.button.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = sdlEvent.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                             ? core::HostEventType::PointerDown
                             : core::HostEventType::PointerUp;
            event.window = windowIdOf(sdlEvent.button.windowID);
            event.position =
                core::Offset{sdlEvent.button.x, sdlEvent.button.y};
            event.device = core::PointerDevice::Mouse;
            event.button = mapMouseButton(sdlEvent.button.button);
            event.pointerId = 0;
            // 集合控件（collection-controls-design §6.3）：按下时刻的
            // 修饰键（Extended 选择 Ctrl/Shift 语义）。本 SDL3 版本的
            // 按钮事件不携带 mod，改查全局实时修饰键状态。
            event.modifiers = mapSdlModifiers(SDL_GetModState());
            push(std::move(event));
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            if (windows_.count(sdlEvent.motion.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::PointerMove;
            event.window = windowIdOf(sdlEvent.motion.windowID);
            event.position =
                core::Offset{sdlEvent.motion.x, sdlEvent.motion.y};
            event.device = core::PointerDevice::Mouse;
            event.pointerId = 0;
            push(std::move(event));
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            if (windows_.count(sdlEvent.wheel.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::Wheel;
            event.window = windowIdOf(sdlEvent.wheel.windowID);
            // wheel 事件坐标可能无效（PS/2 鼠标）；用 NAN 表示“位置未知”，
            // 交互层只在需要命中测试时使用。
            event.position = core::Offset{std::isnan(sdlEvent.wheel.mouse_x)
                                              ? 0.0F
                                              : sdlEvent.wheel.mouse_x,
                                          std::isnan(sdlEvent.wheel.mouse_y)
                                              ? 0.0F
                                              : sdlEvent.wheel.mouse_y};
            // 符号换算：框架约定 scrollDelta.y>0 = 内容向下（windowing.h），
            // 而 SDL NORMAL 语义 y>0 = 远离用户（Windows 习惯"向上滚"），
            // 需取负；FLIPPED（macOS 自然滚动）时系统已翻转，透传即可。
            // X 的 NORMAL 正向本来就是向右，不能复用 Y 的反号。
            const float wheelSign =
                sdlEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? 1.0F
                                                                   : -1.0F;
            event.scrollDelta = core::Offset{
                sdlEvent.wheel.x * kWheelUnitPx * -wheelSign,
                sdlEvent.wheel.y * kWheelUnitPx * wheelSign};
            event.modifiers = mapSdlModifiers(SDL_GetModState());
            event.device = core::PointerDevice::Mouse;
            push(std::move(event));
            break;
        }
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED: {
            if (windows_.count(sdlEvent.tfinger.windowID) == 0) {
                break;
            }
            const auto it = windows_.find(sdlEvent.tfinger.windowID);
            if (it == windows_.end()) {
                break;
            }
            const core::Size logical = it->second.window->logicalSize();
            core::HostEvent event;
            event.window = windowIdOf(sdlEvent.tfinger.windowID);
            event.device = core::PointerDevice::Touch;
            event.pointerId = static_cast<std::uint32_t>(
                sdlEvent.tfinger.fingerID & 0xFFFFFFFFU);
            if (sdlEvent.type == SDL_EVENT_FINGER_CANCELED) {
                event.type = core::HostEventType::PointerCancel;
            } else {
                event.type = sdlEvent.type == SDL_EVENT_FINGER_DOWN
                                 ? core::HostEventType::PointerDown
                                 : (sdlEvent.type == SDL_EVENT_FINGER_UP
                                        ? core::HostEventType::PointerUp
                                        : core::HostEventType::PointerMove);
            }
            // 触摸坐标是窗口归一化 0..1；换算到逻辑坐标。
            event.position =
                core::Offset{sdlEvent.tfinger.x * logical.width,
                             sdlEvent.tfinger.y * logical.height};
            push(std::move(event));
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            if (windows_.count(sdlEvent.key.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = sdlEvent.type == SDL_EVENT_KEY_DOWN
                             ? core::HostEventType::KeyDown
                             : core::HostEventType::KeyUp;
            event.window = windowIdOf(sdlEvent.key.windowID);
            event.keyCode = mapSdlKey(sdlEvent.key.key);
            event.scanCode = static_cast<int>(sdlEvent.key.scancode);
            event.modifiers = mapSdlModifiers(sdlEvent.key.mod);
            event.keyChar = mapKeyChar(sdlEvent.key.key, event.modifiers);
            push(std::move(event));
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            if (windows_.count(sdlEvent.text.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::TextInput;
            event.window = windowIdOf(sdlEvent.text.windowID);
            if (sdlEvent.text.text != nullptr) {
                event.text = sdlEvent.text.text;
            }
            push(std::move(event));
            break;
        }
        case SDL_EVENT_TEXT_EDITING: {
            if (windows_.count(sdlEvent.edit.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::TextEditing;
            event.window = windowIdOf(sdlEvent.edit.windowID);
            if (sdlEvent.edit.text != nullptr) {
                event.text = sdlEvent.edit.text;
            }
            event.editCursor = sdlEvent.edit.start;
            event.editLength = sdlEvent.edit.length;
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            const auto it = windows_.find(sdlEvent.window.windowID);
            if (it == windows_.end()) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::Resize;
            event.window = windowIdOf(sdlEvent.window.windowID);
            event.pixelSize = it->second.window->drawableSize();
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED: {
            const auto it = windows_.find(sdlEvent.window.windowID);
            if (it == windows_.end()) {
                break;
            }
            // DPI 变化单独成事件：metrics 查询即时反映新 drawable 尺寸，
            // 必须先于下一帧 surface 重建处理（plan §3.1）。
            core::HostEvent event;
            event.type = core::HostEventType::DpiChanged;
            event.window = windowIdOf(sdlEvent.window.windowID);
            event.pixelSize = it->second.window->drawableSize();
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_MINIMIZED: {
            if (windows_.count(sdlEvent.window.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::WindowMinimized;
            event.window = windowIdOf(sdlEvent.window.windowID);
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_MAXIMIZED: {
            // 自定义标题栏（lumen-titlebar-design §4.3）：toggleMaximize
            // 或系统途径（snap/双击）完成最大化；还原走 RESTORED。
            if (windows_.count(sdlEvent.window.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::WindowMaximized;
            event.window = windowIdOf(sdlEvent.window.windowID);
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
            // M16：全屏进入/退出（toggleFullscreen 或系统途径；metrics
            // 同步经事件消费者查询 windowMetrics 即时反映）。
            const auto it = windows_.find(sdlEvent.window.windowID);
            if (it == windows_.end()) {
                break;
            }
            it->second.fullscreen =
                sdlEvent.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
            core::HostEvent event;
            event.type = it->second.fullscreen
                             ? core::HostEventType::WindowFullscreenEntered
                             : core::HostEventType::WindowFullscreenExited;
            event.window = windowIdOf(sdlEvent.window.windowID);
            push(std::move(event));
            break;
        }
        case SDL_EVENT_CLIPBOARD_UPDATE: {
            // G-3：剪贴板内容变更（跨应用或本应用）。SDL 事件不带窗口
            // （剪贴板是会话级）；window 留空，runApp 转发首个窗口。
            core::HostEvent event;
            event.type = core::HostEventType::ClipboardChanged;
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_RESTORED: {
            if (windows_.count(sdlEvent.window.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::WindowRestored;
            event.window = windowIdOf(sdlEvent.window.windowID);
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED: {
            if (windows_.count(sdlEvent.window.windowID) == 0) {
                break;
            }
            // M12：窗口级光标近似——SDL_SetCursor 是进程级原语，多窗口
            // 各自设形后切换焦点会停留在"最后设置"的形状；获焦时重放
            // 该窗口缓存的形状恢复窗口级语义（未设置过则用系统默认）。
            if (WindowEntry* entry =
                    find(windowIdOf(sdlEvent.window.windowID));
                entry != nullptr && entry->cursor != nullptr) {
                SDL_SetCursor(static_cast<SDL_Cursor*>(entry->cursor));
            }
            core::HostEvent event;
            event.type = core::HostEventType::WindowFocusGained;
            event.window = windowIdOf(sdlEvent.window.windowID);
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST: {
            if (windows_.count(sdlEvent.window.windowID) == 0) {
                break;
            }
            core::HostEvent event;
            event.type = core::HostEventType::WindowFocusLost;
            event.window = windowIdOf(sdlEvent.window.windowID);
            push(std::move(event));
            break;
        }
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
            if (windows_.count(sdlEvent.window.windowID) == 0) {
                break;
            }
            // 关闭请求区别于 Quit：Navigator/Dialog 统一拦截规则（8D）。
            core::HostEvent event;
            event.type = core::HostEventType::WindowCloseRequested;
            event.window = windowIdOf(sdlEvent.window.windowID);
            push(std::move(event));
            break;
        }
        case SDL_EVENT_SYSTEM_THEME_CHANGED: {
            // M12：刷新能力位并广播（应用经 onEvent 重派生主题）。
            capabilities_.prefersDarkMode =
                SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
            // 系统主题事件也覆盖强调色变化；重新查询避免能力快照停留
            // 在初始化时的颜色。
            if (const std::optional<core::Color> accent =
                    native::systemAccentColor()) {
                capabilities_.accentColor = *accent;
            }
            core::HostEvent event;
            event.type = core::HostEventType::SystemThemeChanged;
            push(std::move(event));
            break;
        }
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_POSITION:
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_TEXT:
        case SDL_EVENT_DROP_COMPLETE: {
            // M15：OS 拖入会话（m15-roadmap §3）。BEGIN 开会话（清交付
            // 标志），POSITION 是悬停更新，FILE/TEXT 交付负载；
            // COMPLETE 在本次会话未交付负载时合成 DragLeave——SDL
            // 3.2.10 无显式 leave 事件。
            const auto it = windows_.find(sdlEvent.drop.windowID);
            if (it == windows_.end()) {
                break;
            }
            core::HostEvent event;
            event.window = windowIdOf(sdlEvent.drop.windowID);
            event.position =
                core::Offset{sdlEvent.drop.x, sdlEvent.drop.y};
            switch (sdlEvent.type) {
                case SDL_EVENT_DROP_BEGIN:
                    it->second.dropDelivered = false;
                    event.type = core::HostEventType::DragEnter;
                    break;
                case SDL_EVENT_DROP_POSITION:
                    event.type = core::HostEventType::DragMove;
                    break;
                case SDL_EVENT_DROP_FILE:
                    if (sdlEvent.drop.data != nullptr) {
                        event.filePaths.emplace_back(sdlEvent.drop.data);
                        it->second.dropDelivered = true;
                        event.type = core::HostEventType::DragDrop;
                    }
                    break;
                case SDL_EVENT_DROP_TEXT:
                    if (sdlEvent.drop.data != nullptr) {
                        event.text = sdlEvent.drop.data;
                        it->second.dropDelivered = true;
                        event.type = core::HostEventType::DragDrop;
                    }
                    break;
                case SDL_EVENT_DROP_COMPLETE:
                default:
                    // 交付过负载则 DragDrop 已终结束会话；否则视为未释放
                    // 离开（拖出窗口/按 Esc 取消）。
                    if (!it->second.dropDelivered) {
                        event.type = core::HostEventType::DragLeave;
                    }
                    break;
            }
            if (event.type != core::HostEventType::None) {
                push(std::move(event));
            }
            break;
        }
        default:
            break;
    }
    return out.size();
}


// --- M16：系统托盘与全局快捷键 ---

// M16：托盘菜单项回调上下文（SDL 类型不出头文件；宿主 trayContexts_
// 持有，随 destroyTray 清空）。
struct TrayEntryContext {
    core::WindowId window{};
    std::string command{};
    Sdl3ApplicationHost* host{};
};

namespace {

void trayEntryCallback(void* userdata, SDL_TrayEntry*) {
    // 上下文由宿主持有（trayContexts_，与托盘同寿命）——SDL 托盘条目
    // 持久存在、可多次激活（review 修复：此前 unique_ptr 首次激活即
    // 释放导致二次激活 use-after-free）。回调只读入队。
    const auto* context = static_cast<TrayEntryContext*>(userdata);
    if (context == nullptr || context->host == nullptr) {
        return;
    }
    context->host->noteTrayActivation(context->window, context->command);
}

}  // namespace

void Sdl3ApplicationHost::noteTrayActivation(core::WindowId window,
                                             const std::string& command) {
    std::lock_guard<std::mutex> lock(trayMutex_);
    trayPending_.push_back(
        PendingTrayActivation{window, command});
}

void Sdl3ApplicationHost::destroyTray() {
    if (tray_ == nullptr) {
        return;
    }
    // SDL_DestroyTray 之后回调不再触发；上下文随托盘条目一并释放
    //（SDL 拥有条目内存，TrayEntryContext 由 destroyTray 前的回调场景
    // 持有——这里只销毁托盘本体，上下文已随 keepAlive 自由）。
    SDL_DestroyTray(static_cast<SDL_Tray*>(tray_));
    tray_ = nullptr;
    trayContexts_.clear();
}

ServiceResult Sdl3ApplicationHost::setTray(core::WindowId ownerWindow,
                                           const TraySetup& tray) {
    if (!initialized_) {
        return ServiceResult::failed("host not initialized");
    }
    destroyTray();
    // 图标（空 = 平台默认）。
    SDL_Surface* iconSurface = nullptr;
    SDL_Surface* ownedSurface = nullptr;
    if (tray.icon.width > 0 && tray.icon.height > 0 &&
        tray.icon.rgba.size() ==
            static_cast<std::size_t>(tray.icon.width) *
                static_cast<std::size_t>(tray.icon.height) * 4U) {
        ownedSurface = SDL_CreateSurfaceFrom(
            tray.icon.width, tray.icon.height, SDL_PIXELFORMAT_RGBA32,
            const_cast<std::uint8_t*>(tray.icon.rgba.data()),
            static_cast<std::size_t>(tray.icon.width) * 4U);
        iconSurface = ownedSurface;
    }
    SDL_Tray* created = SDL_CreateTray(
        iconSurface, tray.tooltip.empty() ? nullptr : tray.tooltip.c_str());
    if (ownedSurface != nullptr) {
        SDL_DestroySurface(ownedSurface);
    }
    if (created == nullptr) {
        return ServiceResult::failed(std::string("SDL_CreateTray: ") +
                                     SDL_GetError());
    }
    tray_ = created;
    if (!tray.menu.empty()) {
        SDL_TrayMenu* menu = SDL_CreateTrayMenu(created);
        for (const auto& item : tray.menu) {
            // SDL 3.2.10 无 separator 条目：分隔项以禁用 "-" 近似
            //（视觉分隔；不回灌命令）。
            SDL_TrayEntryFlags flags = item.separator
                                           ? SDL_TRAYENTRY_BUTTON |
                                                 SDL_TRAYENTRY_DISABLED
                                           : SDL_TRAYENTRY_BUTTON;
            SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(
                menu, -1, item.separator ? "-" : item.label.c_str(), flags);
            if (entry == nullptr) {
                continue;
            }
            if (!item.separator && !item.command.empty()) {
                trayContexts_.push_back(
                    std::make_unique<TrayEntryContext>(
                        TrayEntryContext{ownerWindow, item.command, this}));
                SDL_SetTrayEntryCallback(
                    entry, trayEntryCallback, trayContexts_.back().get());
            }
        }
    }
    return ServiceResult::success();
}

void Sdl3ApplicationHost::removeTray() { destroyTray(); }

ServiceResult Sdl3ApplicationHost::registerGlobalHotkey(
    core::WindowId ownerWindow, const GlobalHotkeySpec& spec) {
    // R4：活跃后端直接委托（校验/冲突/释放均结构化）；其余保持
    // Unavailable + 探测原因（能力位 false 的可读出口）。
    if (hotkeys_ != nullptr) {
        return hotkeys_->registerHotkey(ownerWindow, spec);
    }
    return ServiceResult::unavailable("global hotkeys: " +
                                      hotkeyUnavailableReason_);
}

ServiceResult Sdl3ApplicationHost::unregisterGlobalHotkey(
    const std::string& id) {
    if (hotkeys_ != nullptr) {
        return hotkeys_->unregisterHotkey(id);
    }
    return ServiceResult::unavailable("global hotkeys: " +
                                      hotkeyUnavailableReason_);
}

bool Sdl3ApplicationHost::pollEvent(core::HostEvent& out) {
    if (!initialized_) {
        out = core::HostEvent{};
        return false;
    }
    if (accessibilityPreferences_) {
        if (const auto preferences = accessibilityPreferences_->poll();
            preferences && applyNativeAccessibilityPreferences(*preferences)) {
            out = {};
            out.type = core::HostEventType::SystemAccessibilityChanged;
            out.timestampMs = SDL_GetTicks();
            return true;
        }
    }
    if (!pending_.empty()) {
        out = std::move(pending_.front());
        pending_.pop_front();
        refreshLifecycle();
        return true;
    }
    // M16：托盘菜单激活（回调线程暂存 → UI 线程转 TrayActivated）。
    {
        std::optional<PendingTrayActivation> trayEvent;
        {
            std::lock_guard<std::mutex> lock(trayMutex_);
            if (!trayPending_.empty()) {
                trayEvent = std::move(trayPending_.front());
                trayPending_.pop_front();
            }
        }
        if (trayEvent.has_value()) {
            out = core::HostEvent{};
            out.type = core::HostEventType::TrayActivated;
            out.window = trayEvent->window;
            out.text = std::move(trayEvent->command);
            out.timestampMs = SDL_GetTicks();
            refreshLifecycle();
            return true;
        }
    }
    // R4：全局快捷键（X11 后端在本连接上排队；非阻塞轮询，命中转
    // GlobalHotkey 事件——text = 注册 id，window = 注册时 owner）。
    if (hotkeys_ != nullptr) {
        core::WindowId hotkeyWindow{};
        std::string hotkeyId{};
        if (hotkeys_->poll(hotkeyWindow, hotkeyId)) {
            out = core::HostEvent{};
            out.type = core::HostEventType::GlobalHotkey;
            out.window = hotkeyWindow;
            out.text = std::move(hotkeyId);
            out.timestampMs = SDL_GetTicks();
            refreshLifecycle();
            return true;
        }
    }
    // 异步 SDL 对话框回调可能来自工作线程；只在 UI 线程持锁读取已完成
    // 结果，并将其转换为正常宿主事件。未完成请求继续留在 dialogs_。
    for (auto it = dialogs_.begin(); it != dialogs_.end(); ++it) {
        PendingDialog& dialog = **it;
        bool completed = false;
        {
            std::lock_guard<std::mutex> lock(dialog.mutex);
            if (!dialog.done) {
                continue;
            }
            out = core::HostEvent{};
            out.type = core::HostEventType::FileDialogCompleted;
            out.window = dialog.window;
            out.filePaths = std::move(dialog.paths);
            out.text = std::move(dialog.error);
            completed = true;
        }
        if (completed) {
            dialogs_.erase(it);
            refreshLifecycle();
            return true;
        }
    }
    SDL_Event sdlEvent{};
    while (SDL_PollEvent(&sdlEvent)) {
        std::vector<core::HostEvent> batch;
        translateEvent(&sdlEvent, batch);
        if (!batch.empty()) {
            out = std::move(batch.front());
            for (std::size_t i = 1; i < batch.size(); ++i) {
                pending_.push_back(std::move(batch[i]));
            }
            refreshLifecycle();
            return true;
        }
    }
    out = core::HostEvent{};
    return false;
}

void Sdl3ApplicationHost::waitForEvents(std::uint32_t timeoutMs) {
    if (!initialized_) {
        return;
    }
    // SDL_WaitEventTimeout 会从队列取走唤醒事件；不能传 nullptr，否则
    // 空闲期间到达的第一条输入会被静默丢弃。先翻译并放回宿主 pending
    // 队列，下一次 pollEvent 再按正常顺序出队（这里不分发事件）。
    SDL_Event event{};
    // Native preference changes need observation even while SDL has no input.
    if (SDL_WaitEventTimeout(&event, static_cast<std::int32_t>(
            std::min(timeoutMs, accessibilityPreferences_ ? 100U : timeoutMs)))) {
        std::vector<core::HostEvent> batch;
        translateEvent(&event, batch);
        for (auto& translated : batch) {
            pending_.push_back(std::move(translated));
        }
    }
}

std::optional<core::WindowId> Sdl3ApplicationHost::createWindow(
    const WindowDesc& desc) {
    if (!initialized_) {
        return std::nullopt;
    }
    Sdl3WindowDesc windowDesc;
    windowDesc.title = desc.title;
    windowDesc.width = desc.width;
    windowDesc.height = desc.height;
    windowDesc.resizable = desc.resizable;
    windowDesc.highPixelDensity = desc.highPixelDensity;
    windowDesc.opengl = desc.opengl;
    windowDesc.softwarePresentation = desc.softwarePresentation;
    windowDesc.customTitleBar = desc.customTitleBar;
    windowDesc.transparent = desc.transparent;
    auto window = createSdl3Window(windowDesc);
    if (window == nullptr) {
        return std::nullopt;
    }
    const NativeSurfaceHandle surface = window->nativeSurface();
    auto* sdlWindow = static_cast<SDL_Window*>(surface.nativeWindow);
    // G-8：初始屏幕位置（nullopt = 系统默认；创建后设置覆盖
    // SDL_WINDOWPOS_UNDEFINED）。
    if (desc.x.has_value() && desc.y.has_value()) {
        SDL_SetWindowPosition(sdlWindow, *desc.x, *desc.y);
    }
    const core::WindowId id{SDL_GetWindowID(sdlWindow)};
    WindowEntry entry;
    entry.window = std::move(window);
    entry.textInput = std::make_unique<Sdl3TextInputSession>(
        entry.window.get());
    windows_.emplace(id.value, std::move(entry));
    if (desc.customTitleBar) {
        // hit-test 回调数据 = dragRegion 成员地址（map 节点稳定）；
        // destroyWindow 时注销。注册后 resize 边 + caption 拖拽即可用。
        WindowEntry* inserted = find(id);
        if (!SDL_SetWindowHitTest(sdlWindow, titleBarHitTest,
                                  &inserted->dragRegion)) {
            std::fprintf(stderr,
                         "SDL_SetWindowHitTest failed: %s\n",
                         SDL_GetError());
        }
    }
    refreshLifecycle();
    return id;
}

void Sdl3ApplicationHost::destroyWindow(core::WindowId id) {
    if (WindowEntry* entry = find(id)) {
        // 先注销 hit-test：回调数据指向 entry 内成员，窗口销毁前必须
        // 解除挂靠（SDL 窗口销毁本身也会清理，这里显式以防重建路径）。
        (void)SDL_SetWindowHitTest(
            static_cast<SDL_Window*>(
                entry->window->nativeSurface().nativeWindow),
            nullptr, nullptr);
    }
    windows_.erase(id.value);
    refreshLifecycle();
}

std::optional<core::WindowMetrics> Sdl3ApplicationHost::windowMetrics(
    core::WindowId id) const {
    const auto it = windows_.find(id.value);
    if (it == windows_.end()) {
        return std::nullopt;
    }
    const PlatformWindow& window = *it->second.window;
    core::WindowMetrics metrics;
    metrics.logicalSize = window.logicalSize();
    metrics.drawableSize = window.drawableSize();
    metrics.deviceScale = metrics.logicalSize.width > 0.0F
                              ? metrics.drawableSize.width /
                                    metrics.logicalSize.width
                              : 1.0F;
    metrics.visible = window.isVisible();
    metrics.minimized = window.isMinimized();
    // 自定义标题栏（lumen-titlebar-design §4.3）：最大化状态（最大化按钮
    // 图标/布局自适应消费）。
    metrics.maximized =
        (SDL_GetWindowFlags(static_cast<SDL_Window*>(
             window.nativeSurface().nativeWindow)) &
         SDL_WINDOW_MAXIMIZED) != 0;
    // M16：全屏状态来自宿主会话标志（ENTER/LEAVE_FULLSCREEN 事件回填；
    // review 修复——此前 metrics.fullscreen 恒为默认 false，消费方读到
    // 与真实状态漂移的值）。
    metrics.fullscreen = it->second.fullscreen;
    // G-8：屏幕位置（物理像素；位置记忆回读）。Wayland 无全局位置
    // 语义——SDL 返回最近设置值（SetWindowPosition 的回声），positioned
    // 恒 true 表示"SDL 视角可用"；记忆回放照常、跨显示器不复位属
    // 合成器行为（window-experience-design §1）。
    int px = 0;
    int py = 0;
    SDL_GetWindowPosition(
        static_cast<SDL_Window*>(window.nativeSurface().nativeWindow), &px,
        &py);
    metrics.x = px;
    metrics.y = py;
    metrics.positioned = true;
    return metrics;
}

std::vector<core::WindowId> Sdl3ApplicationHost::windowIds() const {
    std::vector<core::WindowId> ids;
    ids.reserve(windows_.size());
    for (const auto& [value, entry] : windows_) {
        ids.push_back(core::WindowId{value});
    }
    return ids;
}

PlatformWindow* Sdl3ApplicationHost::platformWindow(core::WindowId id) const {
    const auto it = windows_.find(id.value);
    return it == windows_.end() ? nullptr : it->second.window.get();
}

Clipboard* Sdl3ApplicationHost::clipboard() {
    if (!initialized_) {
        return nullptr;
    }
    if (clipboard_ == nullptr) {
        clipboard_ = std::make_unique<Sdl3Clipboard>();
    }
    return clipboard_.get();
}

TextInputSession* Sdl3ApplicationHost::textInputSession(core::WindowId id) {
    WindowEntry* entry = find(id);
    return entry != nullptr ? entry->textInput.get() : nullptr;
}

PlatformCapabilities Sdl3ApplicationHost::capabilities() const {
    // M4：桌面 SDL 能力（通知在 SDL 3.2.10 无 API → 不可用，结构化降级）。
    PlatformCapabilities caps = capabilities_;
    int touchCount = 0;
    SDL_TouchID* devices = SDL_GetTouchDevices(&touchCount);
    caps.touch = touchCount > 0;
    if (devices != nullptr) {
        SDL_free(devices);
    }
    return caps;
}

// --- M4：平台服务实现 ---

ServiceResult Sdl3ApplicationHost::openUrl(const std::string& url) {
    if (!SDL_OpenURL(url.c_str())) {
        return ServiceResult::failed("SDL_OpenURL: " +
                                     std::string(SDL_GetError()));
    }
    return ServiceResult::success();
}

ServiceResult Sdl3ApplicationHost::requestFileDialog(
    core::WindowId id, const FileDialogRequest& request) {
    // Headless/dummy 驱动无原生对话框实现：在 macOS 上 Cocoa 面板在
    // dummy 下可能永不回调甚至崩溃，Linux 上则走 portal 异步路径。
    // 为三端一致，dummy 下直接同步返回 Unavailable（调用方按契约降级，
    // 测试走同步失败分支），避免平台相关的异步悬挂/崩溃。
    if (const char* driver = SDL_GetCurrentVideoDriver();
        driver != nullptr && std::string(driver) == "dummy") {
        (void)request;
        (void)id;
        return ServiceResult::unavailable(
            "file dialogs unavailable with dummy video driver");
    }
    WindowEntry* entry = find(id);
    if (entry == nullptr && !windows_.empty()) {
        // 单窗口应用便捷路径：无效窗口 id 时挂靠首个窗口（对话框获得
        // 父窗口，避免无父悬浮面板）。
        entry = &windows_.begin()->second;
    }
    if (entry == nullptr) {
        return ServiceResult::failed("window not found");
    }
    SDL_Window* window =
        entry != nullptr
            ? static_cast<SDL_Window*>(entry->window->nativeSurface()
                                           .nativeWindow)
            : nullptr;
    // 过滤器（name/pattern 对；模式即过滤器串本身）。PendingDialog
    // 持有字符串，保证异步调用期间所有 c_str 指针稳定。
    auto pending = std::make_unique<PendingDialog>();
    pending->window = id;
    pending->title = request.title;
    pending->defaultLocation = request.defaultName;
    pending->filterNames.assign(request.filters.size(), "Files");
    pending->filterPatterns = request.filters;
    pending->filters.reserve(request.filters.size());
    for (std::size_t i = 0; i < request.filters.size(); ++i) {
        pending->filters.push_back(SDL_DialogFileFilter{
            pending->filterNames[i].c_str(), pending->filterPatterns[i].c_str()});
    }
    PendingDialog* raw = pending.get();
    dialogs_.push_back(std::move(pending));
    SDL_PropertiesID properties = SDL_CreateProperties();
    if (properties == 0) {
        dialogs_.pop_back();
        return ServiceResult::failed("SDL_CreateProperties: " +
                                     std::string(SDL_GetError()));
    }
    SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER,
                           window);
    SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
                           raw->filters.empty() ? nullptr : raw->filters.data());
    SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
                          static_cast<Sint64>(raw->filters.size()));
    SDL_SetBooleanProperty(properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN,
                           request.allowMultiple);
    if (!raw->title.empty()) {
        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING,
                              raw->title.c_str());
    }
    if (!raw->defaultLocation.empty()) {
        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING,
                              raw->defaultLocation.c_str());
    }
    SDL_ShowFileDialogWithProperties(
        request.forSave ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFILE,
        &Sdl3ApplicationHost::dialogCallback, raw, properties);
    SDL_DestroyProperties(properties);
    return ServiceResult::success();
}

ServiceResult Sdl3ApplicationHost::postNotification(
    const NotificationRequest& request) {
    // M12：原生通知 seam（Win32 气泡 / DBus / AppKit）。失败为结构化
    // 结果（无 shell 会话/通知守护等），不阻塞 UI 线程。
    return native::showNotification(request);
}

void Sdl3ApplicationHost::setCursor(core::WindowId id, SystemCursor cursor) {
    WindowEntry* entry = find(id);
    if (entry == nullptr) {
        return;
    }
    if (entry->cursorShape == cursor) {
        return;  // 形状未变：不重建系统光标。
    }
    static const SDL_SystemCursor kShapes[] = {
        SDL_SYSTEM_CURSOR_DEFAULT,    // Arrow
        SDL_SYSTEM_CURSOR_TEXT,       // IBeam
        SDL_SYSTEM_CURSOR_WAIT,       // Wait
        SDL_SYSTEM_CURSOR_CROSSHAIR,  // Crosshair
        SDL_SYSTEM_CURSOR_POINTER,    // PointingHand
        SDL_SYSTEM_CURSOR_MOVE,       // Grab（SDL 无 GRAB：四向移动）
        SDL_SYSTEM_CURSOR_MOVE,       // Grabbing（同上，近似）
        SDL_SYSTEM_CURSOR_MOVE,       // ResizeAll
        SDL_SYSTEM_CURSOR_NS_RESIZE,  // ResizeNS
        SDL_SYSTEM_CURSOR_EW_RESIZE,  // ResizeEW
        SDL_SYSTEM_CURSOR_NOT_ALLOWED,
    };
    const auto index = static_cast<std::size_t>(cursor);
    SDL_Cursor* shape =
        index < sizeof(kShapes) / sizeof(kShapes[0])
            ? SDL_CreateSystemCursor(kShapes[index])
            : nullptr;
    if (shape == nullptr) {
        return;
    }
    SDL_SetCursor(shape);
    if (entry->cursor != nullptr) {
        SDL_DestroyCursor(static_cast<SDL_Cursor*>(entry->cursor));
    }
    entry->cursor = shape;
    entry->cursorShape = cursor;
}

ServiceResult Sdl3ApplicationHost::setWindowIcon(core::WindowId id,
                                                 const WindowIcon& icon) {
    WindowEntry* entry = find(id);
    if (entry == nullptr) {
        return ServiceResult::failed("window not found");
    }
    if (icon.width <= 0 || icon.height <= 0 ||
        icon.rgba.size() !=
            static_cast<std::size_t>(icon.width) *
                static_cast<std::size_t>(icon.height) * 4U) {
        return ServiceResult::failed("invalid icon pixels");
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        icon.width, icon.height, SDL_PIXELFORMAT_RGBA32,
        const_cast<std::uint8_t*>(icon.rgba.data()),
        static_cast<std::size_t>(icon.width) * 4U);
    if (surface == nullptr) {
        return ServiceResult::failed("SDL_CreateSurfaceFrom: " +
                                     std::string(SDL_GetError()));
    }
    const bool ok = SDL_SetWindowIcon(
        static_cast<SDL_Window*>(
            entry->window->nativeSurface().nativeWindow),
        surface);
    SDL_DestroySurface(surface);
    if (!ok) {
        return ServiceResult::failed("SDL_SetWindowIcon: " +
                                     std::string(SDL_GetError()));
    }
    return ServiceResult::success();
}

ServiceResult Sdl3ApplicationHost::startDrag(core::WindowId,
                                             const DragOutPayload&) {
    // M15：固定 SDL 3.2.10 无拖出发起 API（receive-only）；能力位
    // dragDropStart 如实 false，SDL 升级后在此接入并翻转能力。
    return ServiceResult::unavailable(
        "SDL 3.2.10 provides no drag-start API (receive-only)");
}

// --- M16：窗口能力 ---

void Sdl3ApplicationHost::toggleFullscreen(core::WindowId id) {
    WindowEntry* entry = find(id);
    if (entry == nullptr) {
        return;
    }
    const bool enter = !entry->fullscreen;
    if (!SDL_SetWindowFullscreen(
            static_cast<SDL_Window*>(
                entry->window->nativeSurface().nativeWindow),
            enter)) {
        return;  // 平台拒绝：状态不变，事件不产生（诚实降级）。
    }
    // 状态由 ENTER/LEAVE_FULLSCREEN 事件翻译回填（系统途径进入也一致）。
}

void Sdl3ApplicationHost::setAlwaysOnTop(core::WindowId id, bool onTop) {
    WindowEntry* entry = find(id);
    if (entry == nullptr || entry->alwaysOnTop == onTop) {
        return;
    }
    entry->alwaysOnTop = onTop;
    SDL_SetWindowAlwaysOnTop(
        static_cast<SDL_Window*>(
            entry->window->nativeSurface().nativeWindow),
        onTop);
}

ServiceResult Sdl3ApplicationHost::setWindowModal(core::WindowId id,
                                                  core::WindowId parent) {
    WindowEntry* entry = find(id);
    if (entry == nullptr) {
        return ServiceResult::failed("window not found");
    }
    // SDL 3.2.10：父子关系经 SDL_SetWindowParent，模态经
    // SDL_SetWindowModal（两参数）——组合表达 OS 级模态窗口。
    if (!parent.valid()) {
        if (!SDL_SetWindowModal(
                static_cast<SDL_Window*>(
                    entry->window->nativeSurface().nativeWindow),
                false)) {
            return ServiceResult::failed(std::string("SDL_SetWindowModal: ") +
                                         SDL_GetError());
        }
        return ServiceResult::success();
    }
    WindowEntry* parentEntry = find(parent);
    if (parentEntry == nullptr) {
        return ServiceResult::failed("parent window not found");
    }
    SDL_Window* self =
        static_cast<SDL_Window*>(entry->window->nativeSurface().nativeWindow);
    SDL_Window* parentWindow = static_cast<SDL_Window*>(
        parentEntry->window->nativeSurface().nativeWindow);
    if (!SDL_SetWindowParent(self, parentWindow) ||
        !SDL_SetWindowModal(self, true)) {
        return ServiceResult::failed(std::string("SDL_SetWindowModal: ") +
                                     SDL_GetError());
    }
    return ServiceResult::success();
}

Sdl3ApplicationHost::WindowEntry* Sdl3ApplicationHost::find(
    core::WindowId id) {
    const auto it = windows_.find(id.value);
    return it == windows_.end() ? nullptr : &it->second;
}

// --- 自定义标题栏（lumen-titlebar-design §4.3）：窗口操作 ---
// 无效窗口 id 时挂靠首个窗口（requestFileDialog 的单窗口便捷路径先例：
// 应用回调常无窗口 id 上下文）。

void Sdl3ApplicationHost::minimizeWindow(core::WindowId id) {
    WindowEntry* entry = find(id);
    if (entry == nullptr && !windows_.empty()) {
        entry = &windows_.begin()->second;
    }
    if (entry == nullptr) {
        return;
    }
    SDL_MinimizeWindow(static_cast<SDL_Window*>(
        entry->window->nativeSurface().nativeWindow));
}

void Sdl3ApplicationHost::toggleMaximizeWindow(core::WindowId id) {
    WindowEntry* entry = find(id);
    if (entry == nullptr && !windows_.empty()) {
        entry = &windows_.begin()->second;
    }
    if (entry == nullptr) {
        return;
    }
    auto* sdlWindow = static_cast<SDL_Window*>(
        entry->window->nativeSurface().nativeWindow);
    const bool maximized =
        (SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_MAXIMIZED) != 0;
    if (maximized) {
        SDL_RestoreWindow(sdlWindow);
    } else {
        SDL_MaximizeWindow(sdlWindow);
    }
}

void Sdl3ApplicationHost::requestWindowClose(core::WindowId id) {
    std::uint64_t target = id.value;
    if (windows_.find(target) == windows_.end()) {
        if (windows_.empty()) {
            return;
        }
        target = windows_.begin()->first;
    }
    // 与系统 X 同路径：合成 WindowCloseRequested 事件经 pollEvent 交付
    //（runApp → shell.requestClose，应用可按 modal/路由规则消费）。
    core::HostEvent event;
    event.type = core::HostEventType::WindowCloseRequested;
    event.window = core::WindowId{target};
    pending_.push_back(std::move(event));
}

void Sdl3ApplicationHost::raiseWindow(core::WindowId id) {
    WindowEntry* entry = find(id);
    if (entry == nullptr && !windows_.empty()) {
        entry = &windows_.begin()->second;
    }
    if (entry == nullptr) {
        return;
    }
    // M13：AT 抓焦点 → 抬升窗口（Wayland 经 xdg-activation 由合成器
    // 决定是否授予键盘焦点；X11/Windows 直接置前）。
    SDL_RaiseWindow(static_cast<SDL_Window*>(
        entry->window->nativeSurface().nativeWindow));
}

void Sdl3ApplicationHost::setWindowDragRegion(
    core::WindowId id, std::function<bool(core::Offset)> predicate) {
    if (WindowEntry* entry = find(id)) {
        entry->dragRegion = std::move(predicate);
    }
}

void* Sdl3ApplicationHost::nativeWindowHandle(core::WindowId id) const {
    // 经 nativeSurface 取 SDL_Window*（void*），再按平台取原生句柄；
    // SDK 类型只在实现内出现（AGENTS.md）。
    PlatformWindow* window = platformWindow(id);
    if (window == nullptr) {
        return nullptr;
    }
    SDL_Window* sdlWindow =
        static_cast<SDL_Window*>(window->nativeSurface().nativeWindow);
    if (sdlWindow == nullptr) {
        return nullptr;
    }
    SDL_PropertiesID props = SDL_GetWindowProperties(sdlWindow);
#if defined(_WIN32)
    return SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    // NSWindow*（M13 语义桥用；contentView 由 provider 侧 ObjC++ 取）。
    return SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
    // AT-SPI 走会话总线（org.a11y.Bus），无窗口句柄需求。
    (void)props;
    return nullptr;
#endif
}

void Sdl3ApplicationHost::noteAccessibilityBridgeActive(bool active) {
    capabilities_.accessibility = active;
}

void Sdl3ApplicationHost::refreshLifecycle() {
    if (!initialized_) {
        lifecycle_ = core::AppLifecycle::Launching;
        return;
    }
    if (windows_.empty()) {
        lifecycle_ = core::AppLifecycle::Active;
        return;
    }
    // 桌面近似：全部窗口不可见（最小化/隐藏）时 Inactive；可见窗口存在
    // 则 Active。Background/Suspended 保留给移动端 host（阶段8E）。
    bool anyVisible = false;
    for (const auto& [value, entry] : windows_) {
        if (entry.window->isVisible()) {
            anyVisible = true;
            break;
        }
    }
    lifecycle_ = anyVisible ? core::AppLifecycle::Active
                            : core::AppLifecycle::Inactive;
}

}  // namespace lumen::platform
