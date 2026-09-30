add_executable(lumen-designer main.cpp designer_app.cpp)

target_compile_features(lumen-designer PRIVATE cxx_std_20)
target_link_libraries(lumen-designer PRIVATE
    lumen-core
    lumen-app
    lumen-layout
    lumen-render
    lumen-text
    lumen-platform
    lumen-widgets
    lumen-dsl
    SDL3::SDL3
)

if(LUMEN_ENABLE_SKIA)
  target_compile_definitions(lumen-designer PRIVATE LUMEN_HAVE_SKIA=1)
endif()

if(LUMEN_ENABLE_GPU)
  target_compile_definitions(lumen-designer PRIVATE LUMEN_HAVE_GPU=1)
  find_package(OpenGL REQUIRED)
  target_link_libraries(lumen-designer PRIVATE OpenGL::GL)
endif()

if(LUMEN_BUILD_TESTS)
  add_test(NAME designer_headless COMMAND lumen-designer --headless)
  set_tests_properties(designer_headless PROPERTIES TIMEOUT 20)
endif()
