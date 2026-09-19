#include "lumen/platform/sdl3_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <optional>
#include <utility>

#include <SDL3/SDL.h>

#include "lumen/core/interaction.h"

namespace lumen::platform {
namespace {

SDL_Surface* softwareWindowSurface(SDL_Window* window) {
    // SDL can otherwise implement even a window surface using GL textures.
    // The hint is consulted on the first surface creation in a video session.
    const char* hint = SDL_GetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION);
    const std::optional<std::string> previous =
        hint != nullptr ? std::optional<std::string>{hint} : std::nullopt;
    SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0",
                            SDL_HINT_OVERRIDE);
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    if (previous.has_value()) {
        SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION,
                                previous->c_str(), SDL_HINT_OVERRIDE);
    } else {
        SDL_ResetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION);
    }
    // An existing accelerated framebuffer in this video session cannot be
    // converted by changing the hint. Reject it instead of claiming fallback.
    if (SDL_GetRenderer(window) != nullptr) {
        SDL_SetError("Software presentation requires a native window surface");
        return nullptr;
    }
    return surface;
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
        default:
            return core::Key::None;
    }
}

class Sdl3Window final : public PlatformWindow {
  public:
    explicit Sdl3Window(SDL_Window* window, SDL_Renderer* renderer,
                        bool softwarePresentation)
        : window_(window), renderer_(renderer),
          softwarePresentation_(softwarePresentation) {}

    ~Sdl3Window() override {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
        }
        SDL_DestroyWindow(window_);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    Sdl3Window(const Sdl3Window&) = delete;
    Sdl3Window& operator=(const Sdl3Window&) = delete;

    Event pollEvent() override {
        if (pending_.empty()) {
            drainEvents();
        }
        if (pending_.empty()) {
            return Event{};
        }
        Event event = std::move(pending_.front());
        pending_.pop_front();
        return event;
    }

    [[nodiscard]] core::Size logicalSize() const override {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        return core::Size{static_cast<float>(width), static_cast<float>(height)};
    }

    [[nodiscard]] core::Size drawableSize() const override {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        return core::Size{static_cast<float>(width), static_cast<float>(height)};
    }

    PresentResult present(const render::PixelBuffer& buffer) override {
        if (renderer_ == nullptr && !softwarePresentation_) {
            // OpenGL 窗口没有 SDL 呈现器；GPU 适配负责交换。
            return PresentResult::Rejected;
        }
        if (buffer.width <= 0 || buffer.height <= 0 ||
            buffer.width > std::numeric_limits<int>::max() / 4 ||
            buffer.rgba.size() !=
                static_cast<std::size_t>(buffer.width) *
                    static_cast<std::size_t>(buffer.height) * 4) {
            return PresentResult::Rejected;
        }
        if (softwarePresentation_) {
            // Resize invalidates SDL's surface. Reacquire it each frame and
            // propagate the native update result (SDL_RenderPresent discards
            // backend failures in the pinned SDL version).
            SDL_Surface* destination = softwareWindowSurface(window_);
            if (destination == nullptr) {
                std::fprintf(stderr, "SDL_GetWindowSurface failed: %s\n",
                             SDL_GetError());
                return PresentResult::Rejected;
            }
            const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> source(
                SDL_CreateSurfaceFrom(buffer.width, buffer.height,
                    SDL_PIXELFORMAT_RGBA32,
                    const_cast<std::uint8_t*>(buffer.rgba.data()), buffer.width * 4),
                SDL_DestroySurface);
            if (source == nullptr ||
                !SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_NONE) ||
                !SDL_BlitSurfaceScaled(source.get(), nullptr, destination, nullptr,
                                       SDL_SCALEMODE_NEAREST) ||
                !SDL_UpdateWindowSurface(window_)) {
                std::fprintf(stderr, "SDL software present failed: %s\n",
                             SDL_GetError());
                return PresentResult::Rejected;
            }
            return PresentResult::Ok;
        }
        if (texture_ == nullptr || textureWidth_ != buffer.width ||
            textureHeight_ != buffer.height) {
            if (texture_ != nullptr) {
                SDL_DestroyTexture(texture_);
            }
            texture_ = SDL_CreateTexture(
                renderer_, SDL_PIXELFORMAT_ABGR8888,
                SDL_TEXTUREACCESS_STREAMING, buffer.width, buffer.height);
            textureWidth_ = buffer.width;
            textureHeight_ = buffer.height;
            if (texture_ == nullptr) {
                std::fprintf(stderr, "SDL_CreateTexture failed: %s\n",
                             SDL_GetError());
                return PresentResult::Rejected;
            }
        }
        if (!SDL_UpdateTexture(texture_, nullptr, buffer.rgba.data(),
                               buffer.width * 4)) {
            std::fprintf(stderr, "SDL_UpdateTexture failed: %s\n",
                         SDL_GetError());
            return PresentResult::Rejected;
        }
        if (!SDL_RenderClear(renderer_) ||
            !SDL_RenderTexture(renderer_, texture_, nullptr, nullptr) ||
            !SDL_RenderPresent(renderer_)) {
            std::fprintf(stderr, "SDL present failed: %s\n", SDL_GetError());
            return PresentResult::Rejected;
        }
        return PresentResult::Ok;
    }

    [[nodiscard]] NativeSurfaceHandle nativeSurface() const override {
        NativeSurfaceHandle handle;
        handle.nativeWindow = window_;
        handle.windowSystem = "sdl3";
        return handle;
    }

    [[nodiscard]] bool isMinimized() const override {
        const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
        return (flags & SDL_WINDOW_MINIMIZED) != 0;
    }

    [[nodiscard]] bool isVisible() const override {
        const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
        // MINIMIZED 在 HIDDEN 之外单独判断；隐藏窗口同样视为不可见。
        return (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) == 0;
    }

    void setVSyncEnabled(bool enabled) override {
        if (renderer_ != nullptr) {
            SDL_SetRenderVSync(renderer_, enabled ? 1 : 0);
        } else if (softwarePresentation_) {
            SDL_SetWindowSurfaceVSync(window_, enabled ? 1 : 0);
        }
    }

    void setTextInputEnabled(bool enabled) override {
        if (enabled) {
            SDL_StartTextInput(window_);
        } else {
            SDL_StopTextInput(window_);
        }
    }

    void setTextInputArea(const core::Rect& area, int cursor) override {
        // SDL expects window (logical) coordinates; Lumen layout already
        // works in that space, so only float->int rounding is needed. The
        // candidate window follows the TextField caret on IBus/Fcitx/Wayland.
        const SDL_Rect rect{
            static_cast<int>(std::lround(area.left())),
            static_cast<int>(std::lround(area.top())),
            static_cast<int>(std::lround(static_cast<double>(area.size.width))),
            static_cast<int>(
                std::lround(static_cast<double>(area.size.height)))};
        if (!SDL_SetTextInputArea(window_, &rect, cursor)) {
            std::fprintf(stderr, "SDL_SetTextInputArea failed: %s\n",
                         SDL_GetError());
        }
    }

  private:
    // Converts the native queue into Lumen events; unmapped events are
    // dropped. Window size events re-query the drawable size (plan §2).
    // Linux notes: X11/Wayland window-manager close arrives as
    // WINDOW_CLOSE_REQUESTED (not QUIT); fractional-scale Wayland sessions
    // report DISPLAY scale changes; touchscreens report FINGER events;
    // IBus/Fcitx composition arrives as TEXT_EDITING before TEXT_INPUT.
    void drainEvents() {
        SDL_Event sdlEvent{};
        while (SDL_PollEvent(&sdlEvent)) {
            switch (sdlEvent.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    pending_.push_back(Event{EventType::Quit});
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    if (sdlEvent.button.button != SDL_BUTTON_LEFT) {
                        break;
                    }
                    Event event;
                    // SDL3 reports mouse coordinates in window (logical)
                    // coordinates already — no DPI scaling needed here.
                    event.type = sdlEvent.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                                      ? EventType::PointerDown
                                      : EventType::PointerUp;
                    event.position =
                        core::Offset{sdlEvent.button.x, sdlEvent.button.y};
                    pending_.push_back(std::move(event));
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION: {
                    Event event;
                    event.type = EventType::PointerMove;
                    event.position =
                        core::Offset{sdlEvent.motion.x, sdlEvent.motion.y};
                    pending_.push_back(std::move(event));
                    break;
                }
                case SDL_EVENT_FINGER_DOWN:
                case SDL_EVENT_FINGER_UP:
                case SDL_EVENT_FINGER_MOTION:
                case SDL_EVENT_FINGER_CANCELED: {
                    // Touch coordinates are normalized 0..1 over the window;
                    // scale by the logical size so touch matches mouse space.
                    // SDL also emulates mouse events from touch, but handling
                    // FINGER directly keeps Linux touchscreens working even
                    // when mouse emulation is disabled. CANCELED maps to
                    // PointerUp so a cancelled touch never leaves a stuck
                    // pressed/armed button state.
                    const core::Size logical = logicalSize();
                    const bool isDown = sdlEvent.type == SDL_EVENT_FINGER_DOWN;
                    const bool isCancel =
                        sdlEvent.type == SDL_EVENT_FINGER_CANCELED;
                    if (isDown) {
                        if (activeFinger_) {
                            break;
                        }
                        activeTouchId_ = sdlEvent.tfinger.touchID;
                        activeFingerId_ = sdlEvent.tfinger.fingerID;
                        activeFinger_ = true;
                    } else if (!activeFinger_ ||
                               sdlEvent.tfinger.touchID != activeTouchId_ ||
                               sdlEvent.tfinger.fingerID != activeFingerId_) {
                        break;
                    }
                    Event event;
                    if (sdlEvent.type == SDL_EVENT_FINGER_DOWN) {
                        event.type = EventType::PointerDown;
                    } else if (sdlEvent.type == SDL_EVENT_FINGER_UP ||
                               sdlEvent.type == SDL_EVENT_FINGER_CANCELED) {
                        event.type = EventType::PointerUp;
                    } else {
                        event.type = EventType::PointerMove;
                    }
                    if (isCancel) {
                        // Cancel must release without firing: deliver the up
                        // outside the root so pointerUp clears the armed
                        // click target instead of matching it.
                        event.position = core::Offset{-1.0F, -1.0F};
                    } else {
                        event.position = core::Offset{
                            sdlEvent.tfinger.x * logical.width,
                            sdlEvent.tfinger.y * logical.height};
                    }
                    pending_.push_back(std::move(event));
                    if (isCancel || sdlEvent.type == SDL_EVENT_FINGER_UP) {
                        activeFinger_ = false;
                    }
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP: {
                    Event event;
                    event.type = sdlEvent.type == SDL_EVENT_KEY_DOWN
                                      ? EventType::KeyDown
                                      : EventType::KeyUp;
                    event.keyCode =
                        static_cast<int>(mapSdlKey(sdlEvent.key.key));
                    pending_.push_back(std::move(event));
                    break;
                }
                case SDL_EVENT_TEXT_INPUT: {
                    Event event;
                    event.type = EventType::TextInput;
                    if (sdlEvent.text.text != nullptr) {
                        event.text = sdlEvent.text.text;
                    }
                    pending_.push_back(std::move(event));
                    break;
                }
                case SDL_EVENT_TEXT_EDITING: {
                    // IME preedit (e.g. Pinyin composition): exposed but never
                    // committed here; the app keeps it for future preedit UI.
                    Event event;
                    event.type = EventType::TextEditing;
                    if (sdlEvent.edit.text != nullptr) {
                        event.text = sdlEvent.edit.text;
                    }
                    event.editCursor = sdlEvent.edit.start;
                    event.editLength = sdlEvent.edit.length;
                    pending_.push_back(std::move(event));
                    break;
                }
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                    Event event;
                    event.type = EventType::Resize;
                    event.pixelSize = drawableSize();
                    pending_.push_back(std::move(event));
                    break;
                }
                case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED: {
                    // DPI 变化单独成事件：先于下一帧 surface 重建处理
                    //（v0.2 plan §3.2），设备像素比随之刷新。
                    Event event;
                    event.type = EventType::DpiChanged;
                    event.pixelSize = drawableSize();
                    pending_.push_back(std::move(event));
                    break;
                }
                case SDL_EVENT_WINDOW_MINIMIZED:
                    pending_.push_back(Event{EventType::WindowMinimized});
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                    pending_.push_back(Event{EventType::WindowRestored});
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    pending_.push_back(Event{EventType::FocusGained});
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    pending_.push_back(Event{EventType::FocusLost});
                    break;
                default:
                    break;
            }
        }
    }

    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    bool softwarePresentation_{false};
    SDL_Texture* texture_{nullptr};
    int textureWidth_{0};
    int textureHeight_{0};
    std::deque<Event> pending_{};
    SDL_TouchID activeTouchId_{0};
    SDL_FingerID activeFingerId_{0};
    bool activeFinger_{false};
};

}  // namespace

std::unique_ptr<PlatformWindow> createSdl3Window(const Sdl3WindowDesc& desc) {
    if (desc.opengl && desc.softwarePresentation) {
        std::fprintf(stderr, "OpenGL and software presentation are exclusive\n");
        return nullptr;
    }
    // We translate the primary finger ourselves. Disable SDL's synthetic
    // mouse events so one touch cannot produce duplicate pointer events.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return nullptr;
    }
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    // 自定义标题栏（lumen-titlebar-design §4）：无边框窗口；宿主随后
    // 注册 SDL hit-test 恢复 resize 边与 caption 拖拽。
    if (desc.customTitleBar) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    // 透明窗口（design/gallery.html 圆角主界面）：按像素 alpha 交桌面
    // 合成器；应用侧清屏全透明 + 内容自绘圆角。
    if (desc.transparent) {
        flags |= SDL_WINDOW_TRANSPARENT;
    }
    // OpenGL 窗口供 Skia GPU 适配创建 GL 上下文；此时不建 SDL 呈现器，
    // CPU present 路径返回 Rejected（v0.2 阶段7C）。
    if (desc.opengl) {
        flags |= SDL_WINDOW_OPENGL;
    }
    SDL_Window* window =
        SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    SDL_Renderer* renderer = nullptr;
    if (desc.softwarePresentation) {
        if (softwareWindowSurface(window) == nullptr) {
            std::fprintf(stderr, "SDL software surface creation failed: %s\n",
                         SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return nullptr;
        }
    } else if (!desc.opengl) {
        renderer = SDL_CreateRenderer(window, nullptr);
        if (renderer == nullptr) {
            std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n",
                         SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return nullptr;
        }
    }
    return std::make_unique<Sdl3Window>(window, renderer,
                                       desc.softwarePresentation);
}

}  // namespace lumen::platform
