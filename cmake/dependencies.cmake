include(FetchContent)

# Keep dependency revisions pinned; do not float on main (see AGENTS.md).
set(LUMEN_SDL3_GIT_TAG "release-3.2.10" CACHE STRING "Pinned SDL3 tag")
set(LUMEN_CATCH2_GIT_TAG "v3.8.1" CACHE STRING "Pinned Catch2 tag")

# SDL3 backs lumen-platform, so it is needed for every build configuration.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  sdl3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG ${LUMEN_SDL3_GIT_TAG}
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sdl3)

# stb_image backs ResourceManager decoding (v0.2 阶段7D, plan §3.3). stb has
# no release tags; pin the master commit instead (see AGENTS.md pinning rule).
set(LUMEN_STB_GIT_TAG "2c980bb59875b0d32144a71867fbdebb2f77cd20" CACHE STRING
    "Pinned stb commit")
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG ${LUMEN_STB_GIT_TAG}
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

if(LUMEN_BUILD_TESTS)
  FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG ${LUMEN_CATCH2_GIT_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(catch2)
endif()

# Optional Skia backend (plan 阶段5): default OFF so CPU-only builds stay
# fast and dependency-free. On Windows/Linux a pinned prebuilt archive is
# fetched automatically; other platforms require LUMEN_SKIA_ROOT pointing at
# a Skia install with include/ and out/Release-x64/libskia.a (Linux) or
# skia.lib (Windows).
if(LUMEN_ENABLE_SKIA)
  if(NOT DEFINED LUMEN_SKIA_ROOT)
    if(WIN32)
      set(LUMEN_SKIA_URL "https://github.com/aseprite/skia/releases/download/m124-08a5439a6b/Skia-Windows-Release-x64.zip"
          CACHE STRING "Pinned prebuilt Skia archive")
      FetchContent_Declare(
        skia_prebuilt
        URL ${LUMEN_SKIA_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      )
      FetchContent_MakeAvailable(skia_prebuilt)
      set(LUMEN_SKIA_ROOT "${skia_prebuilt_SOURCE_DIR}" CACHE INTERNAL
          "Extracted prebuilt Skia root")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      # 35MB Release-only archive (m124-08a5439a6b, clang-12, official build).
      # Links into both Debug and Release Lumen builds on Linux (no CRT
      # mismatch like Windows); CI builds Debug+Release against it.
      set(LUMEN_SKIA_URL "https://github.com/aseprite/skia/releases/download/m124-08a5439a6b/Skia-Linux-Release-x64.zip"
          CACHE STRING "Pinned prebuilt Skia archive")
      FetchContent_Declare(
        skia_prebuilt
        URL ${LUMEN_SKIA_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      )
      FetchContent_MakeAvailable(skia_prebuilt)
      set(LUMEN_SKIA_ROOT "${skia_prebuilt_SOURCE_DIR}" CACHE INTERNAL
          "Extracted prebuilt Skia root")
    else()
      message(FATAL_ERROR
        "LUMEN_ENABLE_SKIA requires LUMEN_SKIA_ROOT=<skia install> on this "
        "platform (Windows/Linux fetch the pinned prebuilt archive itself).")
    endif()
  endif()
  if(WIN32)
    # The prebuilt skia.lib bundles freetype code that references plain zlib
    # symbols; the package's zlib.lib only exports Chrome-prefixed names
    # (Cr_z_*). A small pinned zlib fills the plain symbols.
    set(LUMEN_ZLIB_GIT_TAG "v1.3.1" CACHE STRING "Pinned zlib tag")
    FetchContent_Declare(
      zlib
      GIT_REPOSITORY https://github.com/madler/zlib.git
      GIT_TAG ${LUMEN_ZLIB_GIT_TAG}
      GIT_SHALLOW TRUE
    )
    # zlib's minimum CMake is below 3.15: force CMP0091 so the static CRT set
    # above applies to zlibstatic instead of a conflicting default /MD.
    set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
    FetchContent_MakeAvailable(zlib)
    unset(CMAKE_POLICY_DEFAULT_CMP0091)
  endif()
  message(STATUS "Lumen Skia backend: ${LUMEN_SKIA_ROOT}")
endif()
