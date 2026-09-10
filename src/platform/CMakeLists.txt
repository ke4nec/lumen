# v0.3 阶段8E：platform 库拆分为 SDL-free 契约实现（窗口契约、宿主
# 契约与 fake host）与 SDL3 适配。移动核心配置只编译前者——Android/iOS
# host 用 lumen-mobile-host 接缝，不链接 SDL。
set(LUMEN_PLATFORM_SOURCES
    platform_window.cpp
    application_host.cpp
    fake_host.cpp
)

if(NOT LUMEN_BUILD_MOBILE_CORE)
  list(APPEND LUMEN_PLATFORM_SOURCES
      sdl3_window.cpp
      sdl3_host.cpp)
endif()

add_library(lumen-platform STATIC ${LUMEN_PLATFORM_SOURCES})

target_include_directories(lumen-platform PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-platform PUBLIC lumen-core lumen-render)
# SDL stays private: public headers never include SDL types (AGENTS.md).
if(NOT LUMEN_BUILD_MOBILE_CORE)
  target_link_libraries(lumen-platform PRIVATE SDL3::SDL3)
endif()
target_compile_features(lumen-platform PUBLIC cxx_std_20)
