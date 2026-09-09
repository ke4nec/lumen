add_executable(lumen-tests
    geometry_tests.cpp
    layout_tests.cpp
    element_tests.cpp
    render_tests.cpp
    interaction_tests.cpp
    counter_integration_tests.cpp
)

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

if(WIN32)
  # SDL3 builds as a shared library; keep the DLL next to the test binary so
  # catch_discover_tests (which runs the executable at build time) succeeds.
  # Added before catch_discover_tests so the copy runs first.
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
catch_discover_tests(lumen-tests)
