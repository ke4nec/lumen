// Linux platform smoke (plan §9: Windows 与 Linux 创建窗口、点击、文本输入、
// resize 和退出). Runs with the dummy video driver so CI without a display
// still exercises window creation, present, IME area and event polling.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

#include "lumen/platform/sdl3_window.h"
#include "lumen/render/renderer.h"

using lumen::platform::EventType;
using lumen::platform::Sdl3WindowDesc;
using lumen::render::PixelBuffer;

TEST_CASE("platform_creates_window_and_presents", "[platform]") {
    // Dummy driver keeps CI without a display working. This persists for
    // later tests in the process, which is fine: no other test creates a
    // real window.
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    Sdl3WindowDesc desc;
    desc.title = "Lumen Linux Smoke";
    desc.width = 320;
    desc.height = 240;
    auto window = lumen::platform::createSdl3Window(desc);
    REQUIRE(window != nullptr);
    CHECK(window->logicalSize().width == 320.0F);
    CHECK(window->logicalSize().height == 240.0F);
    CHECK(window->drawableSize().width > 0.0F);
    CHECK(window->drawableSize().height > 0.0F);

    PixelBuffer buffer;
    buffer.width = 320;
    buffer.height = 240;
    buffer.rgba.assign(static_cast<std::size_t>(320 * 240 * 4), 200);
    CHECK_NOTHROW(window->present(buffer));

    // IME toggling + candidate anchor must not crash (Linux IBus/Fcitx
    // path via SDL_SetTextInputArea).
    CHECK_NOTHROW(window->setTextInputEnabled(true));
    CHECK_NOTHROW(window->setTextInputArea(
        lumen::core::Rect::fromXYWH(10.0F, 10.0F, 100.0F, 30.0F), 5));
    CHECK_NOTHROW(window->setTextInputEnabled(false));

    // Draining an idle queue is safe; any pending focus event is well-formed.
    for (int i = 0; i < 16; ++i) {
        const auto event = window->pollEvent();
        if (event.type == EventType::None) {
            break;
        }
        CHECK((event.type == EventType::FocusGained ||
               event.type == EventType::FocusLost ||
               event.type == EventType::Resize ||
               event.type == EventType::Quit));
    }
}
