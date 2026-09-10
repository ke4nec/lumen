set(LUMEN_TEST_SOURCES
    geometry_tests.cpp
    layout_tests.cpp
    element_tests.cpp
    render_tests.cpp
    render_command_tests.cpp
    frame_scheduler_tests.cpp
    resource_manager_tests.cpp
    interaction_tests.cpp
    counter_integration_tests.cpp
    counter_fallback_tests.cpp
    dsl_tests.cpp
    damage_tests.cpp
    tween_tests.cpp
    platform_smoke_tests.cpp
    platform_host_tests.cpp
    text_tests.cpp
    semantics_tests.cpp
    settings_integration_tests.cpp
    mobile_host_tests.cpp
)

# 移动核心配置（8E）：无 SDL/桌面平台层，跳过桌面平台与 SDL 依赖测试。
if(LUMEN_BUILD_MOBILE_CORE)
  list(REMOVE_ITEM LUMEN_TEST_SOURCES
      platform_smoke_tests.cpp platform_host_tests.cpp
      counter_fallback_tests.cpp)
endif()

if(LUMEN_ENABLE_SKIA)
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

target_include_directories(lumen-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/examples/counter
    ${PROJECT_SOURCE_DIR}/examples/settings
    ${stb_SOURCE_DIR}
)

target_link_libraries(lumen-tests PRIVATE
    lumen-core
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
