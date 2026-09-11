add_library(lumen-style STATIC
    theme.cpp
    resolver.cpp
)

target_include_directories(lumen-style PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 视觉系统（visual-system-design §4.1）：Theme、token、状态集合与
# StyleResolver。依赖 core 与 accessibility；layout 消费 style，render 只
# 读 RenderNode 的 resolved style，不依赖本模块。
target_link_libraries(lumen-style PUBLIC lumen-core lumen-accessibility)
target_compile_features(lumen-style PUBLIC cxx_std_20)
