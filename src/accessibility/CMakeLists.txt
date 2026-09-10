add_library(lumen-accessibility STATIC
    semantics.cpp
    bridge.cpp
)

target_include_directories(lumen-accessibility PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 语义树消费 core（RenderNode/FocusManager/InteractionController）；平台
# 桥接是可选目标，不把 SDK 类型带进公共头（plan §6）。
target_link_libraries(lumen-accessibility PUBLIC lumen-core)
target_compile_features(lumen-accessibility PUBLIC cxx_std_20)
