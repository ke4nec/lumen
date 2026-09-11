add_library(lumen-widgets STATIC
    form.cpp
    navigator.cpp
)

target_include_directories(lumen-widgets PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 组件层：值级构建器与控制器；视觉系统后 Theme 位于 lumen-style，
# widgets 只保留控制器与组合构建器（visual-system §4.1）。
target_link_libraries(lumen-widgets PUBLIC lumen-core lumen-accessibility lumen-style)
target_compile_features(lumen-widgets PUBLIC cxx_std_20)
