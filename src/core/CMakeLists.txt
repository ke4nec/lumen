add_library(lumen-core STATIC
    widget.cpp
    element.cpp
    render_node.cpp
    utf8.cpp
    state.cpp
    interaction.cpp
    damage.cpp
    tween.cpp
)

target_include_directories(lumen-core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_features(lumen-core PUBLIC cxx_std_20)
