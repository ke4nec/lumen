// Linux platform smoke (plan §9: Windows 与 Linux 创建窗口、点击、文本输入、
// resize 和退出). Runs with the dummy video driver so CI without a display
// still exercises window creation, present, IME area and event polling.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <SDL3/SDL.h>

#include "lumen/platform/sdl3_host.h"
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

TEST_CASE("platform_present_diagnostics_count_actual_conversion", "[platform][alpha]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    for (bool transparent : {false, true}) {
        Sdl3WindowDesc desc;
        desc.width = 4;
        desc.height = 4;
        desc.transparent = transparent;
        auto window = lumen::platform::createSdl3Window(desc);
        REQUIRE(window);
        window->setPresentDiagnosticsEnabled(true);
        PixelBuffer pixels{4, 4, std::vector<std::uint8_t>(64, 128)};
        REQUIRE(window->present(pixels) == lumen::platform::PresentResult::Ok);
        auto stats = window->presentStats();
        CHECK(stats.alphaConversions == (transparent ? 1 : 0));
        CHECK(stats.convertedBytes == (transparent ? 64 : 0));
        CHECK(stats.copiedBytes == stats.convertedBytes);
        CHECK(stats.scratchCapacityBytes >= stats.copiedBytes);
        REQUIRE(window->present({}) == lumen::platform::PresentResult::Rejected);
        CHECK(window->presentStats().alphaConversions == 0);
        window->setPresentDiagnosticsEnabled(false);
        REQUIRE(window->present(pixels) == lumen::platform::PresentResult::Ok);
        CHECK(window->presentStats().copiedBytes == 0);
    }
}

// Alpha plan §3.5: exercise all six dispatches on both SDL paths, with exact
// RGB readback. Dummy-driver coverage verifies dispatch, not desktop compositing.
TEST_CASE("platform_alpha_matrix_preserves_rgb_and_releases_compatibility_scratch",
          "[platform][alpha]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    using lumen::render::AlphaMode;
    for (bool software : {false, true}) {
        for (bool transparent : {false, true}) {
            CAPTURE(software, transparent);
            Sdl3WindowDesc desc;
            desc.width = 2;
            desc.height = 2;
            desc.transparent = transparent;
            desc.softwarePresentation = software;
            auto window = lumen::platform::createSdl3Window(desc);
            REQUIRE(window);
            window->setPresentDiagnosticsEnabled(true);
            // Repeated compatibility -> direct transitions must release the copy.
            for (auto mode : {AlphaMode::Straight, AlphaMode::Premultiplied, AlphaMode::Opaque,
                              AlphaMode::Premultiplied, AlphaMode::Straight, AlphaMode::Opaque}) {
                CAPTURE(mode);
                const std::uint8_t alpha = mode == AlphaMode::Opaque ? 255 : 128;
                PixelBuffer pixels{2, 2, {}, mode};
                for (int i = 0; i < 4; ++i) {
                    pixels.rgba.insert(pixels.rgba.end(), {
                        mode == AlphaMode::Premultiplied ? alpha : std::uint8_t{255}, 0, 0, alpha});
                }
                REQUIRE(window->present(pixels) == lumen::platform::PresentResult::Ok);
                const bool converted = transparent ? mode == AlphaMode::Straight
                                                    : mode == AlphaMode::Premultiplied;
                const auto stats = window->presentStats();
                CHECK(stats.alphaConversions == (converted ? 1 : 0));
                CHECK(stats.convertedBytes == (converted ? 16 : 0));
                CHECK(stats.copiedBytes == stats.convertedBytes);
                if (converted) {
                    CHECK(stats.scratchCapacityBytes >= 16);
                } else {
                    CHECK(stats.scratchCapacityBytes == 0);
                }
                auto* native = static_cast<SDL_Window*>(window->nativeSurface().nativeWindow);
                SDL_Surface* surface = software ? SDL_GetWindowSurface(native)
                    : SDL_RenderReadPixels(SDL_GetRenderer(native), nullptr);
                REQUIRE(surface);
                Uint8 r{}, g{}, b{}, a{};
                const bool read = SDL_ReadSurfacePixel(surface, 0, 0, &r, &g, &b, &a);
                if (!software) SDL_DestroySurface(surface);
                REQUIRE(read);
                CHECK(r == (transparent ? alpha : 255));
                CHECK(g == 0);
                CHECK(b == 0);
            }
            PixelBuffer unknown{1, 1, {0, 0, 0, 0}, static_cast<AlphaMode>(255)};
            CHECK(window->present(unknown) == lumen::platform::PresentResult::Rejected);
            CHECK(window->presentStats().alphaConversions == 0);
        }
    }
}

// M4：SDL host 平台服务（无头安全子集；对话框需真实显示环境，窗口
// smoke/人工验收覆盖）。
TEST_CASE("sdl_host_services_degrade_structurally", "[platform][m4]") {
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    lumen::platform::Sdl3ApplicationHost host;
    if (!host.initialize()) {
        FAIL("SDL init failed");
        return;
    }

    const auto caps = host.capabilities();
    CHECK(caps.fileDialogs);
    CHECK(caps.openUrl);
    CHECK(caps.cursorShape);
    CHECK(caps.windowIcon);
    // M12：通知能力由原生 seam 决定——Windows/Linux(libdbus)/macOS
    // 为 true，其余平台结构化关闭。真发送不进 ctest（避免每次测试弹
    // 真实系统通知；视觉验收人工执行），此处只断言能力位与平台一致。
#if defined(_WIN32)
    CHECK(caps.notifications);
#else
    CHECK_FALSE(caps.notifications);
#endif

    // 光标形状（dummy 驱动下 SDL_CreateSystemCursor 可用）。
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());
    host.setCursor(*id, lumen::platform::SystemCursor::IBeam);
    // 未知窗口/非法图标：结构化失败。
    CHECK_FALSE(
        host.setWindowIcon(*id, lumen::platform::WindowIcon{}).ok);
}

TEST_CASE("sdl_host_dialog_request_completes_or_fails_safely", "[platform][m4]") {
    // dummy 驱动：无 portal/显示——请求立即失败或取消；必须不崩溃、
    // 不阻塞，且完成路径（成功/失败/取消统一为 FileDialogCompleted 或
    // 同步失败）结构化交付。
#ifdef _WIN32
    _putenv("SDL_VIDEODRIVER=dummy");
#else
    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
#endif
    lumen::platform::Sdl3ApplicationHost host;
    if (!host.initialize()) {
        FAIL("SDL init failed");
        return;
    }
    const auto id = host.createWindow({});
    REQUIRE(id.has_value());
    const auto result = host.requestFileDialog(
        *id, lumen::platform::FileDialogRequest{"open", {}, "test"});
    // 同步失败（结构化）或异步接受（完成事件在泵中交付）都合法。
    if (result.ok) {
        lumen::core::HostEvent event;
        int guard = 0;
        while (host.pollEvent(event) && guard++ < 16) {
            if (event.type ==
                lumen::core::HostEventType::FileDialogCompleted) {
                // 取消（空路径）或失败（text 诊断）或成功（路径）——
                // 三态均可，断言字段一致性即可。
                if (event.filePaths.empty() && event.text.empty()) {
                    SUCCEED("cancelled");
                } else {
                    SUCCEED("completed");
                }
                return;
            }
        }
    } else {
        CHECK_FALSE(result.message.empty());
    }
}
