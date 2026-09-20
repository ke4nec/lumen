set(LUMEN_TEST_SOURCES
    geometry_tests.cpp
    # 视觉系统（visual-system §10.1）：token 映射、Theme 派生、状态解析
    # 与 StyleOverrides 契约。
    style_tests.cpp
    layout_tests.cpp
    element_tests.cpp
    render_tests.cpp
    alpha_reference_tests.cpp
    alpha_mode_tests.cpp
    alpha_cpu_tests.cpp
    render_command_tests.cpp
    frame_scheduler_tests.cpp
    resource_manager_tests.cpp
    interaction_tests.cpp
    wheel_routing_tests.cpp
    scrollbar_tests.cpp
    counter_integration_tests.cpp
    counter_fallback_tests.cpp
    # M2：应用壳（runApp/AppShell）headless 验收——Fake host 驱动。
    app_shell_tests.cpp
    # M3：Grid/Image/VirtualList（几何稳定、可见区物化、锚点/damage）。
    grid_virtual_tests.cpp
    # 集合控件（list/tree/treelist）：SelectionModel、控制器、扁平化、
    # 列宽与语义角色（docs/lumen-collection-controls-design.md）。
    collection_tests.cpp
    list_visual_tests.cpp
    tree_visual_tests.cpp
    # 菜单类控件（context/menubar）：Secondary 穿透、面板/键盘/级联、
    # 语义角色（docs/lumen-menu-controls-design.md）。
    menu_tests.cpp
    # M14 菜单动效（menubar-variants 版本 A+D）：打开淡入/上升、高亮
    # 动能矩形、栏 Tonal/下划线、reduceAnimation 与直驱终态。
    menu_motion_tests.cpp
    # Splitter：布局分配/钳制、拖动/键盘/双击复位、语义
    #（docs/lumen-splitter-design.md）。
    splitter_tests.cpp
    # Spin 控件：值模型/提交/自动重复/键盘/滚轮/禁用/Chrome 三态
    #（docs/lumen-spin-design.md，2026-09）。
    spin_tests.cpp
    # ToolBar 控件：命令激活/键盘漫游/溢出折叠/面板/tooltip/语义
    #（docs/lumen-toolbar-design.md，2026-09）。
    toolbar_tests.cpp
    # StatusBar 控件：消息生命周期/项折叠/indeterminate/busy/语义/派生
    #（docs/lumen-statusbar-design.md，2026-09）。
    statusbar_tests.cpp
    # 自定义标题栏：windowDrag 物化/拖拽判定、Gallery 窗口命令/最大化态、
    # Fake host 窗口操作契约与 runApp 注册（docs/lumen-titlebar-design.md）。
    titlebar_tests.cpp
    # M6：视觉系统 V3 与控件库（图标/阴影/滚动条/控件交互/语义）。
    visual_m6_tests.cpp
    visual_regression_tests.cpp
    # M10：动效与滚动体验（转场/整节点透明度/状态色过渡/惯性滚动）。
    motion_scroll_tests.cpp
    dsl_tests.cpp
    damage_tests.cpp
    tween_tests.cpp
    platform_smoke_tests.cpp
    platform_host_tests.cpp
    text_tests.cpp
    # 桌面系统字体（Windows 雅黑 / CPU 真实字形光栅）。
    system_font_tests.cpp
    font_metrics_tests.cpp
    semantics_tests.cpp
    # M13：原生无障碍 provider（工厂分流/能力上报/Windows UIA COM 直驱）。
    a11y_provider_tests.cpp
    settings_integration_tests.cpp
    gallery_integration_tests.cpp
    mobile_host_tests.cpp
)

# 移动核心配置（8E）：无 SDL/桌面平台层，跳过桌面平台与 SDL 依赖测试。
if(LUMEN_BUILD_MOBILE_CORE)
  list(REMOVE_ITEM LUMEN_TEST_SOURCES
      platform_smoke_tests.cpp platform_host_tests.cpp
      counter_fallback_tests.cpp)
endif()

if(LUMEN_ENABLE_SKIA)
    list(APPEND LUMEN_TEST_SOURCES alpha_skia_tests.cpp)
    list(APPEND LUMEN_TEST_SOURCES skia_smoke_tests.cpp)
endif()

# v0.2 阶段7C: GPU smoke（探测失败自动跳过；硬件 GPU 为增强门槛）。
if(LUMEN_ENABLE_GPU)
    list(APPEND LUMEN_TEST_SOURCES gpu_smoke_tests.cpp)
endif()

add_executable(lumen-tests ${LUMEN_TEST_SOURCES})

if(LUMEN_ENABLE_SKIA)
    # The stage-name test expects the adapter label only when it is real.
    target_compile_definitions(lumen-tests PRIVATE LUMEN_HAS_SKIA_BACKEND=1)
endif()

if(LUMEN_ENABLE_GPU)
    target_compile_definitions(lumen-tests PRIVATE LUMEN_HAS_GPU_BACKEND=1)
endif()

if(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE AND WIN32)
    # 与 lumen-accessibility 同名编译定义：测试直驱 UIA provider COM 对象。
    target_compile_definitions(lumen-tests PRIVATE
        LUMEN_ACCESSIBILITY_PROVIDER_UIA=1)
    target_include_directories(lumen-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/accessibility)
endif()

target_include_directories(lumen-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/examples/counter
    ${PROJECT_SOURCE_DIR}/examples/settings
    ${PROJECT_SOURCE_DIR}/examples/gallery
    ${stb_SOURCE_DIR}
)

target_link_libraries(lumen-tests PRIVATE
    lumen-core
    lumen-app
    lumen-style
    lumen-layout
    lumen-render
    lumen-text
    lumen-accessibility
    lumen-widgets
    lumen-platform
    lumen-dsl
    lumen-mobile-host
    Catch2::Catch2WithMain
)

if(NOT LUMEN_BUILD_MOBILE_CORE)
  target_link_libraries(lumen-tests PRIVATE SDL3::SDL3)
endif()

if(LUMEN_ENABLE_GPU)
  # GPU 构建的测试链接 Skia 静态归档（引用 GLX）；GL 尾置解析。
  find_package(OpenGL REQUIRED)
  target_link_libraries(lumen-tests PRIVATE OpenGL::GL)
endif()

target_compile_features(lumen-tests PRIVATE cxx_std_20)

# The DSL test reads the counter document asset next to the binary on every
# platform; added before catch_discover_tests so the copy runs first.
add_custom_command(
    TARGET lumen-tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PROJECT_SOURCE_DIR}/examples/counter/counter.lumen
            $<TARGET_FILE_DIR:lumen-tests>
)

if(WIN32 AND NOT LUMEN_BUILD_MOBILE_CORE)
  # SDL3 builds as a shared library; keep the DLL next to the test binary so
  # catch_discover_tests (which runs the executable at build time) succeeds.
  add_custom_command(
    TARGET lumen-tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:lumen-tests>
    COMMAND_EXPAND_LISTS
  )
endif()

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
# Tests run next to the binary so copied assets (counter.lumen, SDL3.dll)
# resolve by name.
catch_discover_tests(lumen-tests WORKING_DIRECTORY $<TARGET_FILE_DIR:lumen-tests>)

if(LUMEN_ENABLE_GPU AND LUMEN_BUILD_EXAMPLES AND
   CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(lumen-gpu-present-failure SHARED gpu_present_failure.cpp)
    target_link_libraries(lumen-gpu-present-failure PRIVATE SDL3::SDL3)
    add_library(lumen-software-present-failure SHARED gpu_present_failure.cpp)
    target_compile_definitions(lumen-software-present-failure PRIVATE
        LUMEN_TEST_FAIL_SOFTWARE_PRESENT=1)
    target_link_libraries(lumen-software-present-failure PRIVATE SDL3::SDL3)
endif()
