add_library(lumen-core STATIC
    widget.cpp
    element.cpp
    render_node.cpp
    utf8.cpp
    state.cpp
    interaction.cpp
    scroll.cpp
    damage.cpp
    tween.cpp
    windowing.cpp
)

target_include_directories(lumen-core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# M1：interaction 公共头（core/interaction.h）包含 lumen-text 编辑模型
# 头，实现消费其符号；与 lumen-text → lumen-core 构成真实依赖循环
#（静态库链接线重复解析；自用路线图 §5.1 模块边界不变）。
target_link_libraries(lumen-core PUBLIC lumen-text)

target_compile_features(lumen-core PUBLIC cxx_std_20)
