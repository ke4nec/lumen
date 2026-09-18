add_library(lumen-widgets STATIC
    dropdown.cpp
    form.cpp
    navigator.cpp
    # 集合控件（collection-controls-design）：共享选择模型与三控制器。
    selection.cpp
    list.cpp
    tree.cpp
    # 菜单类控件（menu-controls-design）：ContextMenu/MenuBar（overlay）。
    menu.cpp
    # Splitter（splitter-design）：分栏控制器（core::SplitterSource）。
    splitter.cpp
)

target_include_directories(lumen-widgets PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 组件层：值级构建器与控制器；视觉系统后 Theme 位于 lumen-style，
# widgets 只保留控制器与组合构建器（visual-system §4.1）。
# M11：DropdownController 驱动框架级 overlay（AppShell）——widgets 依赖
# lumen-app（无反向依赖，不构成环）。
target_link_libraries(lumen-widgets PUBLIC lumen-core lumen-accessibility lumen-style lumen-app)
target_compile_features(lumen-widgets PUBLIC cxx_std_20)
