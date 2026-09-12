# M2（自用路线图）：应用壳目标。
#
# runApp/AppShell 组合 lumen-core/style/layout/render/text/accessibility/
# platform 的公共契约；公共头文件无 SDL/Skia 类型，可独立链接（依赖注入
# Fake host、SDL host 或测试 renderer）。
add_library(lumen-app STATIC
    app_shell.cpp
    run_app.cpp
)

target_include_directories(lumen-app PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-app PUBLIC
    lumen-core
    lumen-style
    lumen-layout
    lumen-render
    lumen-text
    lumen-accessibility
    lumen-platform
)
target_compile_features(lumen-app PUBLIC cxx_std_20)
