# lumen-template（G-6）：新应用脚手架。
#
# 模板纳入主构建（CI 冒烟防漂移）；依赖与 counter 同口径（app + platform
# SDL host）。拷贝此目录改名即可开始新工具应用——见
# docs/lumen-getting-started.md。
add_executable(lumen-template
    main.cpp
)

target_include_directories(lumen-template PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(lumen-template PRIVATE
    lumen-app
    lumen-widgets
    lumen-dsl
    lumen-platform
    lumen-diagnostics
)

target_compile_features(lumen-template PRIVATE cxx_std_20)
