add_library(lumen-platform STATIC
    platform_window.cpp
    sdl3_window.cpp
)

target_include_directories(lumen-platform PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-platform PUBLIC lumen-core lumen-render)
# SDL stays private: public headers never include SDL types (AGENTS.md).
target_link_libraries(lumen-platform PRIVATE SDL3::SDL3)
target_compile_features(lumen-platform PUBLIC cxx_std_20)
