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

if(LUMEN_BUILD_TESTS)
  FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG ${LUMEN_CATCH2_GIT_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(catch2)
endif()
