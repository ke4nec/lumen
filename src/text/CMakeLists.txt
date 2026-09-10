add_library(lumen-text STATIC
    grapheme.cpp
    font_manager.cpp
    text_layout.cpp
    editing_value.cpp
)

target_include_directories(lumen-text PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# lumen-text 只依赖 lumen-core（plan §6：Skia 实现是可选目标，契约不含
# 平台/Skia 类型）。
target_link_libraries(lumen-text PUBLIC lumen-core)
target_compile_features(lumen-text PUBLIC cxx_std_20)
