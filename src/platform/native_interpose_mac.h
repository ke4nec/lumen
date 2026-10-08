#pragma once

// dyld's two-pointer Mach-O interpose section is supported at process launch.
// Its convenience header is not shipped in the public macOS SDK.
#define LUMEN_DYLD_INTERPOSE(replacementFunction, originalFunction) \
    __attribute__((used, section("__DATA,__interpose"))) \
    static const struct { const void* replacement; const void* original; } \
        nativeInterpose_##originalFunction{ \
            reinterpret_cast<const void*>(&replacementFunction), \
            reinterpret_cast<const void*>(&originalFunction)};
