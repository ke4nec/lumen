// Runs against the isolated test portal, never changes the user's OS settings.
#include <chrono>
#include <cstdio>
#include "lumen/platform/sdl3_host.h"

int main() {
    lumen::platform::Sdl3ApplicationHost host;
    if (!host.initialize()) return 1;
    auto preferences = host.capabilities();
    if (!preferences.systemAccessibilityPreferences || !preferences.highContrast ||
        preferences.reduceAnimation || preferences.fontScale != 1.25F) return 2;
    const auto start = std::chrono::steady_clock::now();
    int changes = 0;
    int polls = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
        lumen::core::HostEvent event;
        ++polls;
        while (host.pollEvent(event)) {
            if (event.type != lumen::core::HostEventType::SystemAccessibilityChanged) continue;
            ++changes;
            preferences = host.capabilities();
            if (event.window.valid() || preferences.highContrast) return 3;
            if (changes == 1 && (!preferences.reduceAnimation || preferences.fontScale != 1.5F)) return 4;
            if (changes == 2 && (preferences.reduceAnimation || preferences.fontScale != 1.0F)) return 5;
            if (changes > 2) return 6;
        }
        host.waitForEvents(5);
    }
    // The delayed portal response must not stall normal event pumping.
    if (changes != 2 || polls < 100) return 7;
    preferences = host.capabilities();
    if (preferences.highContrast || preferences.reduceAnimation || preferences.fontScale != 1.0F) return 8;
    host.shutdown();
    std::puts("native preference initial/dynamic/error round-trip passed");
}
