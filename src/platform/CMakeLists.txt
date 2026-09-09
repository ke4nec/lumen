add_library(lumen-platform STATIC
    platform_window.cpp
)

target_include_directories(lumen-platform PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-platform PUBLIC lumen-core lumen-render)
target_compile_features(lumen-platform PUBLIC cxx_std_20)
