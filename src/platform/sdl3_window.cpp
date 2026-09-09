#include "lumen/platform/sdl3_window.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <utility>

#include <SDL3/SDL.h>

#include "lumen/core/interaction.h"

namespace lumen::platform {
namespace {

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
    explicit Sdl3Window(SDL_Window* window, SDL_Renderer* renderer)
        : window_(window), renderer_(renderer) {}

    ~Sdl3Window() override {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        SDL_DestroyRenderer(renderer_);
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

    void present(const render::PixelBuffer& buffer) override {
        if (buffer.width <= 0 || buffer.height <= 0 ||
            buffer.rgba.size() !=
                static_cast<std::size_t>(buffer.width) *
                    static_cast<std::size_t>(buffer.height) * 4) {
            return;
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
                return;
            }
        }
        if (!SDL_UpdateTexture(texture_, nullptr, buffer.rgba.data(),
                               buffer.width * 4)) {
            std::fprintf(stderr, "SDL_UpdateTexture failed: %s\n",
                         SDL_GetError());
            return;
        }
        SDL_RenderClear(renderer_);
        SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

    void setTextInputEnabled(bool enabled) override {
        if (enabled) {
            SDL_StartTextInput(window_);
        } else {
            SDL_StopTextInput(window_);
        }
    }

  private:
    // Converts the native queue into Lumen events; unmapped events are
    // dropped. Window size events re-query the drawable size (plan §2).
    void drainEvents() {
        SDL_Event sdlEvent{};
        while (SDL_PollEvent(&sdlEvent)) {
            switch (sdlEvent.type) {
                case SDL_EVENT_QUIT:
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
                    event.text = sdlEvent.text.text;
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
    SDL_Texture* texture_{nullptr};
    int textureWidth_{0};
    int textureHeight_{0};
    std::deque<Event> pending_{};
};

}  // namespace

std::unique_ptr<PlatformWindow> createSdl3Window(const Sdl3WindowDesc& desc) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return nullptr;
    }
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    SDL_Window* window =
        SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    return std::make_unique<Sdl3Window>(window, renderer);
}

}  // namespace lumen::platform
