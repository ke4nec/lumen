# v0.3 阶段8E：platform 库拆分为 SDL-free 契约实现（窗口契约、宿主
# 契约与 fake host）与 SDL3 适配。移动核心配置只编译前者——Android/iOS
# host 用 lumen-mobile-host 接缝，不链接 SDL。
set(LUMEN_PLATFORM_SOURCES
    platform_window.cpp
    application_host.cpp
    fake_host.cpp
    frame_allocator.cpp
)

if(NOT LUMEN_BUILD_MOBILE_CORE)
  list(APPEND LUMEN_PLATFORM_SOURCES
      sdl3_window.cpp
      sdl3_host.cpp)
  # R4：全局快捷键接缝（全平台编译——Win32/macOS 走源码内 stub 分支
  # 返回平台真实原因；Linux 且 Xlib 头存在时编入 X11 后端，缺失时同
  # 样自空化，与 dbus seam 同模式）。
  list(APPEND LUMEN_PLATFORM_SOURCES global_hotkeys.cpp)
  if(WIN32)
    # R4：Win32 全局快捷键后端（RegisterHotKey + 消息专用窗口；
    # 编译门禁 = windows.yml，真实按键验收待现场）。
    list(APPEND LUMEN_PLATFORM_SOURCES global_hotkeys_win.cpp)
  endif()
  # M12：原生服务 seam（通知/强调色；SDL 3.2 无 API）。三实现按平台
  # 条件编译自空化，公共契约无平台类型。
  list(APPEND LUMEN_PLATFORM_SOURCES
      native_services_win.cpp
      native_services_dbus.cpp
      native_services_default.cpp)
  # A .mm extension makes GCC require cc1objplus before preprocessing its
  # __APPLE__ guard. Non-Apple desktops must not compile the macOS source.
  if(APPLE)
    set_source_files_properties(frame_allocator.cpp PROPERTIES
        COMPILE_OPTIONS "-fsized-deallocation")
    list(APPEND LUMEN_PLATFORM_SOURCES native_services_mac.mm)
  endif()
endif()

add_library(lumen-platform STATIC ${LUMEN_PLATFORM_SOURCES})

target_include_directories(lumen-platform PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-platform PUBLIC lumen-core lumen-render)
# SDL stays private: public headers never include SDL types (AGENTS.md).
if(NOT LUMEN_BUILD_MOBILE_CORE)
  target_compile_definitions(lumen-platform PRIVATE LUMEN_DESKTOP_FRAME_ALLOCATOR_CLIENT=1)
  target_link_libraries(lumen-platform PRIVATE SDL3::SDL3)
  if(WIN32)
    # M12：Windows 原生 seam（DWM 强调色）；R4 RegisterHotKey（user32）。
    target_link_libraries(lumen-platform PRIVATE dwmapi user32)
  endif()
  if(APPLE)
    # M12：macOS 原生 seam（AppKit：NSColor/NSColorSpace；Foundation：
    # NSString/NSUserNotification）；实现在 .mm 内部，公共头无 ObjC
    # 类型，但静态库消费方链接时仍需框架，PUBLIC 传递（缺失即示例/
    # 测试链接报 NSColor/NSString 未定义，arm64 全桌面 job 失败）。
    target_link_libraries(lumen-platform PUBLIC
        "-framework AppKit" "-framework Foundation")
    # Manual retain/release free (uses @autoreleasepool only); pin non-ARC
    # so the file never depends on a global ARC default.
    set_source_files_properties(native_services_mac.mm PROPERTIES
        COMPILE_OPTIONS "-fno-objc-arc")
  endif()
  # M12：Linux libdbus（缺失时 dbus seam 编译为空实现）。
  if(UNIX AND NOT APPLE)
    target_link_libraries(lumen-platform PRIVATE ${CMAKE_DL_LIBS})
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(DBUS QUIET dbus-1)
      if(DBUS_FOUND)
        target_compile_definitions(lumen-platform PRIVATE LUMEN_HAS_DBUS=1)
        target_include_directories(lumen-platform PRIVATE ${DBUS_INCLUDE_DIRS})
        target_link_libraries(lumen-platform PRIVATE ${DBUS_LIBRARIES})
      endif()
      # R4：全局快捷键 X11 后端链接（源码按 __has_include 自判编译分支，
      # 缺失时自空化——此处只补头文件路径与库）。
      pkg_check_modules(X11HOTKEY QUIET x11)
      if(X11HOTKEY_FOUND)
        target_include_directories(lumen-platform PRIVATE ${X11HOTKEY_INCLUDE_DIRS})
        target_link_libraries(lumen-platform PRIVATE ${X11HOTKEY_LIBRARIES})
      endif()
    endif()
  endif()
endif()
target_compile_features(lumen-platform PUBLIC cxx_std_20)

if(LUMEN_ENABLE_FRAME_ALLOCATOR)
  include(CheckCXXSourceCompiles)
  find_package(Threads REQUIRED)
  if(APPLE)
    if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 14.0)
      message(FATAL_ERROR "The optional macOS allocator profiler requires macOS 14 or newer.")
    endif()
    check_cxx_source_compiles("
      #include <malloc/malloc.h>
      #include <AvailabilityMacros.h>
      int main() {
        static_assert(MAC_OS_X_VERSION_MAX_ALLOWED >= 140000);
        return sizeof(malloc_type_id_t) == 8 ? 0 : 1;
      }" LUMEN_HAS_MAC_ALLOCATOR_SDK)
    if(NOT LUMEN_HAS_MAC_ALLOCATOR_SDK)
      message(FATAL_ERROR "The macOS allocator profiler requires the macOS 14 SDK or newer.")
    endif()
    check_cxx_source_compiles("
      #include <malloc/malloc.h>
      int main() {
        return sizeof(&malloc_zone_malloc_with_options) +
               sizeof(&malloc_type_zone_malloc_with_options);
      }" LUMEN_HAS_MALLOC_ZONE_OPTIONS)
    add_library(lumen-frame-allocator SHARED native_frame_allocator_mac.cpp)
    target_compile_options(lumen-frame-allocator PRIVATE -fno-builtin)
    if(LUMEN_HAS_MALLOC_ZONE_OPTIONS)
      target_compile_definitions(lumen-frame-allocator PRIVATE LUMEN_HAS_MALLOC_ZONE_OPTIONS=1)
    endif()
    # No dependency on Lumen/SDL; no install-time injection into applications.
    set_target_properties(lumen-frame-allocator PROPERTIES
        OSX_DEPLOYMENT_TARGET 14.0
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
  else()
    check_cxx_source_compiles("
      #include <cstddef>
      #include <features.h>
      #ifndef __GLIBC__
      #error glibc is required
      #endif
      extern \"C\" {
        void* __libc_malloc(std::size_t) noexcept;
        void* __libc_calloc(std::size_t, std::size_t) noexcept;
        void* __libc_realloc(void*, std::size_t) noexcept;
        void* __libc_memalign(std::size_t, std::size_t) noexcept;
        void __libc_free(void*) noexcept;
      }
      int main() {
        __libc_free(__libc_realloc(__libc_malloc(1), 2));
        __libc_free(__libc_calloc(1, 2));
        __libc_free(__libc_memalign(64, 64));
      }" LUMEN_HAS_GLIBC_ALLOCATOR_ENTRY_POINTS)
    if(NOT LUMEN_HAS_GLIBC_ALLOCATOR_ENTRY_POINTS)
      message(FATAL_ERROR "The frame allocator profiler requires glibc bootstrap entry points.")
    endif()
    # Standalone interposer: linking Lumen/SDL here would recurse through malloc.
    add_library(lumen-frame-allocator SHARED native_frame_allocator_linux.cpp)
    target_link_libraries(lumen-frame-allocator PRIVATE ${CMAKE_DL_LIBS})
    target_compile_options(lumen-frame-allocator PRIVATE -ftls-model=initial-exec)
    target_link_options(lumen-frame-allocator PRIVATE -Wl,-Bsymbolic-functions -Wl,-z,defs)
  endif()
  target_link_libraries(lumen-frame-allocator PRIVATE Threads::Threads)
  target_compile_features(lumen-frame-allocator PRIVATE cxx_std_20)
endif()
