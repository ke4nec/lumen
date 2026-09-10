add_library(lumen-widgets STATIC
    theme.cpp
    form.cpp
    navigator.cpp
)

target_include_directories(lumen-widgets PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 组件层：值级构建器与控制器；Theme 消费可访问性设置（阶段8C）。
target_link_libraries(lumen-widgets PUBLIC lumen-core lumen-accessibility)
target_compile_features(lumen-widgets PUBLIC cxx_std_20)
