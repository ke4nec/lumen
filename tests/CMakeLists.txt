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
    # G-1：命令分发层（docs/lumen-command-dispatch-design.md）——域优先
    # 级、模态屏蔽、冲突仲裁、字段保护与菜单同路径派生。
    command_registry_tests.cpp
    # G-2：崩溃兜底与持久日志（docs/lumen-runtime-diagnostics-design.md）
    # ——脏标记生命周期、日志分级/滚动、关闭态零开销。
    runtime_diagnostics_tests.cpp
    # G-3：剪贴板深度（docs/lumen-clipboard-service-design.md）——MIME
    # 数据层降级/多格式读写/失败注入与变更事件广播。
    clipboard_tests.cpp
    # G-4：对话框便利层（docs/lumen-dialog-host-design.md）——与手拼
    # makeDialog 同契约。
    dialog_host_tests.cpp
    # G-4：可编辑 ComboBox（docs/lumen-combo-box-design.md）——过滤/
    # 选择/键盘路径（复用 ContextMenu overlay）。
    combo_box_tests.cpp
    # G-4：ColorPicker（docs/lumen-color-picker-design.md）——HSV 派生/
    # 色板反推/hex 降级。
    color_picker_tests.cpp
    # G-5：pinch 手势（docs/lumen-pinch-gesture-design.md）——两指合成
    # 状态机、仲裁（取消单指/字段优先）与 pointerId 透传。
    pinch_tests.cpp
    # G-7：轻量持久化助手（docs/lumen-preferences-design.md）——原子写/
    # 损坏降级/类型转换/变更通知。
    preferences_tests.cpp
    # G-8：窗口体验（docs/lumen-window-experience-design.md）——位置记
    # 忆契约与单实例助手（Primary/Secondary/残留恢复）。
    window_experience_tests.cpp
    # M3：Grid/Image/VirtualList（几何稳定、可见区物化、锚点/damage）。
    grid_virtual_tests.cpp
    # 集合控件（list/tree/treelist）：SelectionModel、控制器、扁平化、
    # 列宽与语义角色（docs/lumen-collection-controls-design.md）。
    collection_tests.cpp
    # M15 拖放（docs/lumen-drag-drop-design.md）：List 行重排会话、
    # ghost/插入指示 overlay、DragDropTokens 派生。
    drag_reorder_tests.cpp
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
    datagrid_tests.cpp
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
    designer_document_tests.cpp
    designer_mapping_tests.cpp
    designer_schema_tests.cpp
    designer_runtime_context_tests.cpp
    designer_editor_tests.cpp
    designer_preview_tests.cpp
    designer_preview_frame_tests.cpp
    designer_workbench_tests.cpp
    designer_accessibility_tests.cpp
    designer_resources_tests.cpp
    designer_performance_tests.cpp
    designer_app_tests.cpp
    document_store_tests.cpp
    project_store_tests.cpp
    example_kit_tests.cpp
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
    # Gallery 应用图标（Core Dark，design/gallery-icon.html 01）：几何/
    # 光栅分层、PNG/ICO/ICNS 容器与 runApp 窗口图标装配
    #（docs/lumen-gallery-icon-design.md）。
    gallery_icon_tests.cpp
    mobile_host_tests.cpp
)

# Gallery asset encoding is a build-time concern, but its output contract is
# covered by the same headless test binary. The private stb writer target owns
# the single third-party implementation used by tests and the export tool.
list(APPEND LUMEN_TEST_SOURCES
    ${PROJECT_SOURCE_DIR}/examples/gallery/gallery_icon.cpp)
list(APPEND LUMEN_TEST_SOURCES
    ${PROJECT_SOURCE_DIR}/examples/designer/designer_app.cpp
    ${PROJECT_SOURCE_DIR}/examples/designer/file_watcher.cpp)

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

# F6 设计器性能证据：独立进程安装全局 new/delete 记账，避免污染主测试
# 进程；只输出预览操作期间的 live/peak heap bytes。
if(NOT LUMEN_BUILD_MOBILE_CORE)
    add_executable(lumen-designer-memory-probe
        ${PROJECT_SOURCE_DIR}/benchmarks/designer_bench.cpp)
    target_link_libraries(lumen-designer-memory-probe PRIVATE
        lumen-dsl lumen-layout lumen-render lumen-widgets)
    target_compile_features(lumen-designer-memory-probe PRIVATE cxx_std_20)
    target_compile_definitions(lumen-designer-memory-probe PRIVATE
        LUMEN_BENCH_BUILD_TYPE="$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Unspecified>"
        LUMEN_BENCH_TOOLCHAIN="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
    add_test(NAME designer_memory_peak
        COMMAND lumen-designer-memory-probe)
    set_tests_properties(designer_memory_peak PROPERTIES TIMEOUT 30)
endif()

if(APPLE AND LUMEN_ENABLE_ACCESSIBILITY_BRIDGE AND NOT LUMEN_BUILD_MOBILE_CORE)
    target_sources(lumen-tests PRIVATE nsaccessibility_tests.mm)
    target_compile_definitions(lumen-tests PRIVATE LUMEN_ACCESSIBILITY_PROVIDER_NSACCESSIBILITY=1)
    target_include_directories(lumen-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/accessibility)
    # Manual retain/release in the AppKit test; pin non-ARC explicitly.
    set_source_files_properties(nsaccessibility_tests.mm PROPERTIES
        COMPILE_OPTIONS "-fno-objc-arc")
endif()

if(NOT LUMEN_BUILD_MOBILE_CORE)
    # Real desktop acceptance probe.  It deliberately honors the caller's
    # SDL_VIDEODRIVER (X11/Wayland/Cocoa/Win32) and is not a CTest dummy smoke.
    add_executable(lumen-platform-live-smoke platform_live_smoke.cpp)
    target_link_libraries(lumen-platform-live-smoke PRIVATE
        lumen-app lumen-widgets lumen-platform lumen-render SDL3::SDL3)
    target_compile_features(lumen-platform-live-smoke PRIVATE cxx_std_20)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT LUMEN_BUILD_MOBILE_CORE)
    add_executable(lumen-native-preferences-live-app native_preferences_live_app.cpp)
    target_link_libraries(lumen-native-preferences-live-app PRIVATE lumen-platform)
endif()

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

if(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE AND UNIX AND NOT APPLE)
    add_executable(lumen-atspi-live-app atspi_live_app.cpp)
    target_link_libraries(lumen-atspi-live-app PRIVATE lumen-app lumen-widgets)
    target_compile_definitions(lumen-tests PRIVATE
        LUMEN_ACCESSIBILITY_PROVIDER_ATSPI=1)
    target_include_directories(lumen-tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/accessibility)
endif()

target_include_directories(lumen-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/examples/counter
    ${PROJECT_SOURCE_DIR}/examples/designer
    ${PROJECT_SOURCE_DIR}/examples/settings
    ${PROJECT_SOURCE_DIR}/examples/gallery
    ${PROJECT_SOURCE_DIR}/examples/common
    ${stb_SOURCE_DIR}
)

# R4：X11 全局快捷键测试（内部接缝头 + XTEST 端到端；Linux 且依赖可用时）。
if(UNIX AND NOT APPLE AND NOT LUMEN_BUILD_MOBILE_CORE)
  target_include_directories(lumen-tests PRIVATE
      ${PROJECT_SOURCE_DIR}/src/platform)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(X11TEST QUIET xtst)
    if(X11TEST_FOUND)
      target_compile_definitions(lumen-tests PRIVATE LUMEN_HAS_XTEST=1)
      target_include_directories(lumen-tests PRIVATE ${X11TEST_INCLUDE_DIRS})
      target_link_libraries(lumen-tests PRIVATE ${X11TEST_LIBRARIES})
    endif()
  endif()
endif()

target_link_libraries(lumen-tests PRIVATE
    lumen-core
    lumen-app
    lumen-style
    lumen-layout
    lumen-render
    lumen-text
    lumen-accessibility
    lumen-diagnostics
    lumen-widgets
    lumen-platform
    lumen-dsl
    lumen-mobile-host
    lumen-stb-image-write
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
# 系统字体重负载用例（system-fonts-slow：全系统字体扫描/拷贝/光栅化）
# 在 CI runner 争用时曾超全局 --timeout 300（2026-09-28 Windows skia-gpu
# 门禁）；单独注册为 600s。per-test TIMEOUT 属性优先于 ctest --timeout。
catch_discover_tests(lumen-tests
    TEST_SPEC "~[system-fonts-slow]"
    WORKING_DIRECTORY $<TARGET_FILE_DIR:lumen-tests>)
catch_discover_tests(lumen-tests
    TEST_SPEC "[system-fonts-slow]"
    PROPERTIES TIMEOUT 600
    WORKING_DIRECTORY $<TARGET_FILE_DIR:lumen-tests>)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME platform_acceptance_evidence_integrity
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/platform_acceptance_tests.py)
    if(TARGET lumen-designer-memory-probe)
        add_test(NAME designer_benchmark_report_integrity
            COMMAND ${Python3_EXECUTABLE} -B
                ${CMAKE_CURRENT_SOURCE_DIR}/designer_benchmark_tests.py
                $<TARGET_FILE:lumen-designer-memory-probe>)
        set_tests_properties(designer_benchmark_report_integrity PROPERTIES TIMEOUT 60)
    endif()
endif()

# G-2：崩溃注入冒烟（独立进程 + script 模式断言；POSIX 信号语义）。
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_executable(crash_injector crash_injector.cpp)
    target_compile_features(crash_injector PRIVATE cxx_std_20)
    target_link_libraries(crash_injector PRIVATE lumen-diagnostics)
    add_test(NAME crash_injector_smoke
        COMMAND ${CMAKE_COMMAND}
            -DINJECTOR=$<TARGET_FILE:crash_injector>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/crash_injector_smoke.cmake)
endif()

if(LUMEN_ENABLE_GPU AND LUMEN_BUILD_EXAMPLES AND
   CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(lumen-gpu-present-failure SHARED gpu_present_failure.cpp)
    target_link_libraries(lumen-gpu-present-failure PRIVATE SDL3::SDL3)
    add_library(lumen-software-present-failure SHARED gpu_present_failure.cpp)
    target_compile_definitions(lumen-software-present-failure PRIVATE
        LUMEN_TEST_FAIL_SOFTWARE_PRESENT=1)
    target_link_libraries(lumen-software-present-failure PRIVATE SDL3::SDL3)
endif()
