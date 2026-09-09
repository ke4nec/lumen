add_library(lumen-layout STATIC
    layout.cpp
)

target_include_directories(lumen-layout PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-layout PUBLIC lumen-core)
target_compile_features(lumen-layout PUBLIC cxx_std_20)
