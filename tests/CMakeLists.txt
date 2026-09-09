add_executable(lumen-tests
    geometry_tests.cpp
    layout_tests.cpp
    element_tests.cpp
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

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(lumen-tests)
