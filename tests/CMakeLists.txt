set(LUMEN_TEST_SOURCES
    geometry_tests.cpp
    layout_tests.cpp
    element_tests.cpp
    render_tests.cpp
    interaction_tests.cpp
    counter_integration_tests.cpp
    dsl_tests.cpp
    damage_tests.cpp
    tween_tests.cpp
    platform_smoke_tests.cpp
)

if(LUMEN_ENABLE_SKIA)
    list(APPEND LUMEN_TEST_SOURCES skia_smoke_tests.cpp)
endif()

add_executable(lumen-tests ${LUMEN_TEST_SOURCES})

if(LUMEN_ENABLE_SKIA)
    # The stage-name test expects the adapter label only when it is real.
    target_compile_definitions(lumen-tests PRIVATE LUMEN_HAS_SKIA_BACKEND=1)
endif()

target_include_directories(lumen-tests PRIVATE
    ${PROJECT_SOURCE_DIR}/examples/counter
)

target_link_libraries(lumen-tests PRIVATE
    lumen-core
    lumen-layout
    lumen-render
    lumen-platform
    lumen-dsl
    Catch2::Catch2WithMain
)

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

if(WIN32)
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
