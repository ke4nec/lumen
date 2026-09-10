cmake_minimum_required(VERSION 3.20)

project(lumen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMEN_BUILD_TESTS "Build Lumen tests" ON)
option(LUMEN_BUILD_EXAMPLES "Build Lumen examples" ON)
option(LUMEN_BUILD_BENCHMARKS "Build Lumen benchmarks" OFF)
option(LUMEN_ENABLE_SKIA "Enable Skia renderer backend" OFF)
# v0.2 阶段7C: Skia Ganesh GPU 后端，默认关闭；需要 LUMEN_ENABLE_SKIA。
option(LUMEN_ENABLE_GPU "Enable the Skia Ganesh GPU backend" OFF)
# v0.3 阶段8C: 平台无障碍桥接（Windows UIA / Linux AT-SPI / macOS
# NSAccessibility），默认关闭；语义树与 Recording 桥始终可用。
option(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE
       "Enable platform accessibility bridges" OFF)
# v0.3 阶段8E: 移动核心。交叉编译（Android NDK / iOS Xcode）时打开：跳过
# SDL 桌面依赖与示例，构建 core/layout/render(CPU)/text/accessibility/
# widgets + mobile-host（SDL-free 接缝）静态库；不依赖桌面会话。
option(LUMEN_BUILD_MOBILE_CORE "Configure the SDL-free mobile core" OFF)

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
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include(cmake/dependencies.cmake)

add_subdirectory(src)

if(LUMEN_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()

if(LUMEN_BUILD_EXAMPLES)
  add_subdirectory(examples/counter)
  # v0.3 阶段8D：settings 示例（滚动/表单/弹窗/导航/主题/无障碍）。
  add_subdirectory(examples/settings)
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
