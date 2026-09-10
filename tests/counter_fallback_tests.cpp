#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "renderer_fallback.h"

namespace {

class ScopedHint {
  public:
    ScopedHint(const char* name, const char* value) : name_(name) {
        const char* previous = SDL_GetHint(name_);
        if (previous != nullptr) {
            previous_ = previous;
        }
        SDL_SetHintWithPriority(name_, value, SDL_HINT_OVERRIDE);
    }
    ~ScopedHint() {
        if (previous_.has_value()) {
            SDL_SetHintWithPriority(name_, previous_->c_str(),
                                    SDL_HINT_OVERRIDE);
        } else {
            SDL_ResetHint(name_);
        }
    }

  private:
    const char* name_;
    std::optional<std::string> previous_;
};

class FailedRenderer final : public lumen::render::Renderer {
  public:
    explicit FailedRenderer(std::vector<std::string>& teardown)
        : teardown_(teardown) {}
    ~FailedRenderer() override { teardown_.push_back("renderer"); }
    void beginFrame(lumen::core::Size) override {}
    void save() override {}
    void restore() override {}
    void clipRect(lumen::core::Rect) override {}
    void drawRect(lumen::core::Rect, lumen::core::Color,
                  lumen::core::CornerRadius) override {}
    void drawText(lumen::render::TextRun, lumen::core::TextStyle) override {}
    void drawImage(lumen::render::ImageId, lumen::core::Rect) override {}
    void endFrame() override {}

  private:
    std::vector<std::string>& teardown_;
};

class OldWindow final : public lumen::platform::PlatformWindow {
  public:
    explicit OldWindow(std::vector<std::string>& teardown) : teardown_(teardown) {}
    ~OldWindow() override { teardown_.push_back("window"); }
    lumen::platform::Event pollEvent() override { return {}; }
    lumen::core::Size logicalSize() const override { return {640.0F, 480.0F}; }
    lumen::core::Size drawableSize() const override { return {1280.0F, 960.0F}; }

  private:
    std::vector<std::string>& teardown_;
};

}  // namespace

TEST_CASE("counter_fallback_preserves_state_and_replaces_window_after_renderer",
          "[counter][platform]") {
    ScopedHint driver(SDL_HINT_VIDEO_DRIVER, "dummy");
    ScopedHint rendererHint(SDL_HINT_RENDER_DRIVER, "opengl");
    ScopedHint framebufferHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "opengl");
    std::vector<std::string> teardown;
    lumen::examples::CounterApp app;
    std::unique_ptr<lumen::platform::PlatformWindow> window =
        std::make_unique<OldWindow>(teardown);
    std::unique_ptr<lumen::render::Renderer> renderer =
        std::make_unique<FailedRenderer>(teardown);
    app.setView(window->logicalSize());
    const auto initialHash = app.renderFrame();
    app.setDeviceScale(2.0F);
    app.setRenderer(renderer.get());
    const auto click = [&](const char* key) {
        const auto* node = lumen::core::findNodeByKey(app.root(), key);
        REQUIRE(node != nullptr);
        const auto center = lumen::core::absoluteOffset(app.root(), key) +
            lumen::core::Offset{node->size.width * 0.5F, node->size.height * 0.5F};
        app.pointerDown(center);
        app.pointerUp(center);
    };
    click("increment-button");
    click("name-field");
    app.textInput("kept after fallback");
    const auto focus = app.controller().focus().focusedIdentity();
    const auto caret = app.controller().caretGraphemes();
    SECTION("without active composition") {}
    SECTION("with active composition") {
        app.textEditing("ni");
        app.textEditing("nihao");
        REQUIRE(app.controller().composingActive());
    }
    app.renderFrame();

    lumen::platform::Sdl3WindowDesc desc;
    desc.opengl = true;
    REQUIRE(lumen::examples::recreateCpuWindow(app, renderer, window, desc));
    CHECK(teardown == std::vector<std::string>{"renderer", "window"});
    CHECK(renderer == nullptr);
    CHECK(window->logicalSize() == lumen::core::Size{640.0F, 480.0F});
    CHECK(app.counterValue() == 1);
    CHECK(app.state().get("name") == "kept after fallback");
    CHECK(app.wantsTextInput());
    CHECK(app.controller().focus().focusedIdentity() == focus);
    CHECK_FALSE(app.controller().composingActive());
    CHECK(app.controller().composition().empty());
    CHECK(app.controller().caretGraphemes() == caret);
    auto* native = static_cast<SDL_Window*>(window->nativeSurface().nativeWindow);
    CHECK(SDL_GetRenderer(native) == nullptr);
    CHECK((SDL_GetWindowFlags(native) & SDL_WINDOW_OPENGL) == 0);
    CHECK(std::string(SDL_GetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION)) == "opengl");
    const auto restoredHash = app.renderFrame();
    CHECK(restoredHash != initialHash);
    CHECK(restoredHash == app.renderFrame(true));
    CHECK(app.pixels().width == static_cast<int>(window->drawableSize().width));
    CHECK(app.pixels().height == static_cast<int>(window->drawableSize().height));
    CHECK(window->present(app.pixels()) == lumen::platform::PresentResult::Ok);
    app.textInput("!");
    CHECK(app.state().get("name") == "kept after fallback!");
}

TEST_CASE("software_window_presents_pixels_and_recreates_surface_after_resize",
          "[platform]") {
    ScopedHint driver(SDL_HINT_VIDEO_DRIVER, "dummy");
    ScopedHint rendererHint(SDL_HINT_RENDER_DRIVER, "opengl");
    lumen::platform::Sdl3WindowDesc desc;
    desc.width = 32;
    desc.height = 24;
    desc.softwarePresentation = true;
    auto window = lumen::platform::createSdl3Window(desc);
    REQUIRE(window != nullptr);
    auto* native = static_cast<SDL_Window*>(window->nativeSurface().nativeWindow);
    for (const auto size : {lumen::core::Size{32, 24}, lumen::core::Size{48, 36}}) {
        REQUIRE(SDL_SetWindowSize(native, static_cast<int>(size.width),
                                  static_cast<int>(size.height)));
        REQUIRE(SDL_SyncWindow(native));
        lumen::render::PixelBuffer buffer;
        buffer.width = static_cast<int>(size.width);
        buffer.height = static_cast<int>(size.height);
        buffer.rgba.resize(static_cast<std::size_t>(buffer.width * buffer.height * 4));
        for (std::size_t i = 0; i < buffer.rgba.size(); i += 4) {
            buffer.rgba[i] = 37;
            buffer.rgba[i + 1] = 91;
            buffer.rgba[i + 2] = 203;
            buffer.rgba[i + 3] = 255;
        }
        REQUIRE(window->present(buffer) == lumen::platform::PresentResult::Ok);
        SDL_Surface* surface = SDL_GetWindowSurface(native);
        REQUIRE(surface != nullptr);
        CHECK(surface->w == buffer.width);
        CHECK(surface->h == buffer.height);
        Uint8 red, green, blue, alpha;
        REQUIRE(SDL_ReadSurfacePixel(surface, surface->w - 1, surface->h - 1,
                                     &red, &green, &blue, &alpha));
        CHECK(red == 37);
        CHECK(green == 91);
        CHECK(blue == 203);
        CHECK(SDL_GetRenderer(native) == nullptr);
    }
    CHECK(window->present({}) == lumen::platform::PresentResult::Rejected);
}
