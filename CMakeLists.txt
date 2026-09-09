cmake_minimum_required(VERSION 3.20)

project(lumen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LUMEN_BUILD_TESTS "Build Lumen tests" ON)
option(LUMEN_BUILD_EXAMPLES "Build Lumen examples" ON)
option(LUMEN_ENABLE_SKIA "Enable Skia renderer backend" OFF)

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
endif()

if(LUMEN_ENABLE_SKIA)
  message(STATUS "Lumen: Skia backend enabled (backend consistency tests run).")
endif()
