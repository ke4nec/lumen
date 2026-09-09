add_library(lumen-render STATIC
    renderer.cpp
)

target_include_directories(lumen-render PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-render PUBLIC lumen-core)
target_compile_features(lumen-render PUBLIC cxx_std_20)
