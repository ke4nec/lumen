cmake_minimum_required(VERSION 3.20)

project(lumen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMEN_BUILD_TESTS "Build Lumen tests" ON)
option(LUMEN_BUILD_EXAMPLES "Build Lumen examples" ON)
option(LUMEN_BUILD_BENCHMARKS "Build Lumen benchmarks" OFF)
option(LUMEN_BUILD_PACKAGE "Enable install and CPack packaging rules" OFF)
option(LUMEN_ENABLE_SKIA "Enable Skia renderer backend" OFF)
# v0.2 阶段7C: Skia Ganesh GPU 后端，默认关闭；需要 LUMEN_ENABLE_SKIA。
option(LUMEN_ENABLE_GPU "Enable the Skia Ganesh GPU backend" OFF)
# v0.3 阶段8C 预留，M13 落地：平台无障碍 provider（Windows UIA /
# Linux AT-SPI / macOS NSAccessibility），默认关闭；语义树与 Recording
# 桥始终可用。编入的平台实现在 src/accessibility/CMakeLists.txt 分发。
option(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE
       "Enable platform accessibility bridge providers" OFF)
if(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE)
  if(NOT WIN32 AND NOT APPLE AND NOT UNIX)
    message(WARNING
            "LUMEN_ENABLE_ACCESSIBILITY_BRIDGE has no provider on this "
            "platform; the factory degrades to nullptr with diagnostics.")
  endif()
endif()
# v0.3 阶段8E: 移动核心。交叉编译（Android NDK / iOS Xcode）时打开：跳过
# SDL 桌面依赖与示例，构建 core/layout/render(CPU)/text/accessibility/
# widgets + mobile-host（SDL-free 接缝）静态库；不依赖桌面会话。
option(LUMEN_BUILD_MOBILE_CORE "Configure the SDL-free mobile core" OFF)
# ObjC++ is required for any Apple build that may compile .mm sources
# (platform native seam, NSAccessibility provider, AppKit tests). Enable it
# for all Apple configurations so mobile-core never sees a .mm without a
# compiler (BRIDGE off today still leaves the language ready).
if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
  set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

if(LUMEN_ENABLE_GPU AND NOT LUMEN_ENABLE_SKIA)
  message(FATAL_ERROR "LUMEN_ENABLE_GPU=ON requires LUMEN_ENABLE_SKIA=ON.")
endif()

if(LUMEN_BUILD_MOBILE_CORE AND LUMEN_ENABLE_SKIA)
  message(FATAL_ERROR "LUMEN_BUILD_MOBILE_CORE cannot combine with Skia.")
endif()
if(LUMEN_BUILD_MOBILE_CORE)
  # 桌面示例/SDL 平台层不进入移动核心配置。
  set(LUMEN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
endif()

if(MSVC AND LUMEN_ENABLE_SKIA)
  # The pinned prebuilt skia.lib is built against the static CRT; the whole
  # tree (and in-tree deps) must match. Its Release flavor only links into
  # Release configurations — build `--config Release` when Skia is on.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
  add_compile_options(/utf-8 /W4)
  # 跨平台源码使用 std::fopen；MSVC 安全 CRT 弃用提示仅噪音。
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(cmake/dependencies.cmake)

# Build the third-party PNG writer once. Gallery asset export and headless
# tests share this private implementation without depending on each other's
# source files.
add_library(lumen-stb-image-write STATIC
            ${PROJECT_SOURCE_DIR}/src/render/stb_image_write_impl.cpp)
target_include_directories(lumen-stb-image-write PRIVATE ${stb_SOURCE_DIR})
target_compile_features(lumen-stb-image-write PRIVATE cxx_std_20)

# M8：便携包运行时路径（须在目标创建前声明——子目录作用域捕获）。
if(APPLE)
  set(CMAKE_INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
  set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif()

add_subdirectory(src)

if(LUMEN_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()

if(LUMEN_BUILD_EXAMPLES)
  add_subdirectory(examples/counter)
  # v0.3 阶段8D：settings 示例（滚动/表单/弹窗/导航/主题/无障碍）。
  add_subdirectory(examples/settings)
  # Widget Gallery：控件/布局/颜色方案/主题演示（与示例同级）。
  add_subdirectory(examples/gallery)
endif()

if(LUMEN_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(LUMEN_ENABLE_SKIA)
  message(STATUS "Lumen: Skia backend enabled (backend consistency tests run).")
endif()
if(LUMEN_ENABLE_GPU)
  message(STATUS "Lumen: Skia Ganesh GPU backend enabled (7C).")
endif()
if(LUMEN_BUILD_MOBILE_CORE)
  message(STATUS "Lumen: mobile core (SDL-free host seam) configured (8E).")
endif()

# --- M8：便携发布（install 规则 + 打包入口） ---
# 可安装件：静态库/头（开发复用）、示例可执行（自用工具）、文档。
if(LUMEN_BUILD_PACKAGE)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  # Public headers must resolve from an installed SDK rather than the source
  # checkout when consumers import the exported static targets.
  set(_lumen_public_targets
      lumen-core lumen-style lumen-layout lumen-text lumen-accessibility
      lumen-render lumen-widgets lumen-platform lumen-dsl lumen-app)
  foreach(_lumen_target IN LISTS _lumen_public_targets)
    if(TARGET ${_lumen_target})
      set_property(TARGET ${_lumen_target} PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                   "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>;$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    endif()
  endforeach()

  set(LUMEN_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Lumen")
  set(_lumen_export_targets ${_lumen_public_targets})
  if(TARGET SDL3-shared)
    list(APPEND _lumen_export_targets SDL3-shared)
  endif()
  if(TARGET SDL3_Headers)
    # Keep the build target pointed at FetchContent while making the exported
    # target resolve headers from the installed SDK.
    set_property(TARGET SDL3_Headers PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                 "$<BUILD_INTERFACE:${SDL3_SOURCE_DIR}/include>;$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    list(APPEND _lumen_export_targets SDL3_Headers)
  endif()
  if(WIN32 AND TARGET zlibstatic)
    # The Windows Skia archive needs plain zlib symbols in addition to its
    # bundled zlib.lib. Keep this private archive beside the SDK libraries so
    # exported static targets remain linkable outside the build tree.
    set_property(TARGET zlibstatic PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                 "$<BUILD_INTERFACE:${zlib_BINARY_DIR}>;$<BUILD_INTERFACE:${zlib_SOURCE_DIR}>")
    list(APPEND _lumen_export_targets zlibstatic)
  endif()
  include(cmake/PackageSkia.cmake)
  if(UNIX AND NOT APPLE AND TARGET SDL3::SDL3)
    # Linux 实体 + soname 别名双实体（ZIP 归档不保留 symlink）。
    install(FILES $<TARGET_FILE:SDL3::SDL3>
            DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES $<TARGET_FILE:SDL3::SDL3>
            DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RENAME libSDL3.so.0)
  endif()
  install(TARGETS ${_lumen_export_targets}
          EXPORT LumenTargets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(EXPORT LumenTargets
          FILE LumenTargets.cmake
          NAMESPACE Lumen::
          DESTINATION ${LUMEN_CMAKE_INSTALL_DIR})
  configure_package_config_file(
          ${CMAKE_CURRENT_LIST_DIR}/cmake/LumenConfig.cmake.in
          ${CMAKE_CURRENT_BINARY_DIR}/LumenConfig.cmake
          INSTALL_DESTINATION ${LUMEN_CMAKE_INSTALL_DIR}
          PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR)
  write_basic_package_version_file(
          ${CMAKE_CURRENT_BINARY_DIR}/LumenConfigVersion.cmake
          COMPATIBILITY SameMajorVersion)
  install(FILES
          ${CMAKE_CURRENT_BINARY_DIR}/LumenConfig.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/LumenConfigVersion.cmake
          DESTINATION ${LUMEN_CMAKE_INSTALL_DIR})
  install(DIRECTORY include/lumen DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  if(DEFINED SDL3_SOURCE_DIR AND EXISTS "${SDL3_SOURCE_DIR}/include/SDL3")
    install(DIRECTORY "${SDL3_SOURCE_DIR}/include/SDL3"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()
  install(FILES README.md docs/lumen-gui-framework-plan.md
          docs/lumen-self-use-roadmap.md docs/build-commands.md
          docs/support-matrix.md docs/lumen-packaging.md
          DESTINATION ${CMAKE_INSTALL_DOCDIR})
  if(LUMEN_BUILD_EXAMPLES AND TARGET lumen-counter)
    install(TARGETS lumen-counter lumen-settings lumen-gallery
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(WIN32)
      # SDL3 DLL is installed by the SDL3-shared target above.
    endif()
    install(FILES examples/counter/counter.lumen
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
    if(UNIX AND NOT APPLE)
      # M12：Linux 桌面集成（AppImage 的 linuxdeploy 消费同一布局）。
      install(FILES assets/lumen-settings.desktop
              DESTINATION share/applications)
      install(FILES assets/lumen.png
              DESTINATION share/icons/hicolor/128x128/apps
              RENAME lumen.png)
      # Gallery 应用图标（docs/lumen-gallery-icon-design.md）：多尺寸
      # hicolor 逐档安装——桌面 shell/图标缓存按请求尺寸选档，不放大
      # 位图；desktop 入口与 settings 并列（20 为 125% DPI 档，hicolor
      # 不消费，仅留在 .ico 内）。
      install(FILES assets/lumen-gallery.desktop
              DESTINATION share/applications)
      foreach(size IN ITEMS 16 24 32 48 64 128 256)
        install(FILES ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-${size}.png
                DESTINATION share/icons/hicolor/${size}x${size}/apps
                RENAME lumen-gallery.png)
      endforeach()
    endif()
    if(APPLE)
      # macOS 图标资源先入包（完整 .app bundle 打包接入时消费）。
      install(FILES ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery.icns
              DESTINATION share/icons)
    endif()
  endif()
  # 开发归档入口：CPack 配置见 cmake/PackageConfig.cmake；Linux AppImage
  # 与 macOS .app 由各平台 workflow 从同一 install tree 组装。
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/PackageConfig.cmake)
endif()
