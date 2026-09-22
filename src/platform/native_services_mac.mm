// M12：macOS 原生服务（AppKit：强调色/深浅偏好/用户通知）。
// 仅在 macOS CI 编译验证（本地 Windows 不可编译；OBJC++）。
// NSUserNotification 自 10.14 标记弃用但仍可用；未签名二进制可能被
// 通知中心拒发——defaultUserNotificationCenter 为 nil 时结构化失败。
#include "native_services.h"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>

#include <algorithm>

namespace lumen::platform::native {

std::optional<core::Color> systemAccentColor() {
    if (@available(macOS 10.14, *)) {
        NSColor* accent = NSColor.controlAccentColor;
        NSColor* srgb = [accent colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
        if (srgb == nil) {
            return std::nullopt;
        }
        const auto channel = [](CGFloat value) {
            return static_cast<std::uint8_t>(value * 255.0F + 0.5F);
        };
        return core::Color{channel(srgb.redComponent),
                           channel(srgb.greenComponent),
                           channel(srgb.blueComponent), 255};
    }
    return std::nullopt;
}

std::optional<SystemAccessibilityPreferences>
systemAccessibilityPreferences() {
    if (@available(macOS 10.10, *)) {
        SystemAccessibilityPreferences preferences;
        NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
        preferences.highContrast =
            workspace.accessibilityDisplayShouldIncreaseContrast;
        preferences.reduceAnimation =
            workspace.accessibilityDisplayShouldReduceMotion;

        // macOS has no process-wide text-scale preference. The system font
        // size is the closest stable desktop signal and preserves 1.0 for
        // the default 13pt system body size.
        const CGFloat systemSize = [NSFont systemFontSize];
        const float scale = systemSize > 0.0 ?
                                static_cast<float>(systemSize / 13.0) :
                                1.0F;
        preferences.fontScale = std::clamp(scale, 0.75F, 2.0F);
        return preferences;
    }
    return std::nullopt;
}

bool notificationsAvailable() {
    return [NSUserNotificationCenter defaultUserNotificationCenter] != nil;
}

ServiceResult showNotification(const NotificationRequest& request) {
    NSUserNotificationCenter* center =
        [NSUserNotificationCenter defaultUserNotificationCenter];
    if (center == nil) {
        return ServiceResult::unavailable(
            "user notification center unavailable for unsigned binary");
    }
    NSUserNotification* notification = [[NSUserNotification alloc] init];
    notification.title =
        [NSString stringWithUTF8String:request.title.c_str()];
    notification.informativeText =
        [NSString stringWithUTF8String:request.body.c_str()];
    [center deliverNotification:notification];
    return ServiceResult::success();
}

}  // namespace lumen::platform::native

#endif  // defined(__APPLE__)
