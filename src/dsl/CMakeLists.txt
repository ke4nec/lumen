add_library(lumen-dsl STATIC
    dsl.cpp
    text_dsl.cpp
)

target_include_directories(lumen-dsl PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-dsl PUBLIC lumen-core)
target_compile_features(lumen-dsl PUBLIC cxx_std_20)
