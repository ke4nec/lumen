add_executable(lumen-designer main.cpp designer_app.cpp file_watcher.cpp)

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
  add_test(NAME designer_gallery_headless
      COMMAND lumen-designer --headless
          --file ${PROJECT_SOURCE_DIR}/examples/designer/gallery.lumen)
  set_tests_properties(designer_gallery_headless PROPERTIES TIMEOUT 20)
  add_test(NAME designer_cli_rejects_invalid_max_frames
      COMMAND ${CMAKE_COMMAND}
          -DDESIGNER=$<TARGET_FILE:lumen-designer>
          -P ${PROJECT_SOURCE_DIR}/tests/designer_cli_smoke.cmake)
  set_tests_properties(designer_cli_rejects_invalid_max_frames PROPERTIES
      TIMEOUT 20)
  find_program(LUMEN_XVFB_RUN xvfb-run)
  if(LUMEN_XVFB_RUN)
    add_test(NAME designer_window_smoke
        COMMAND ${LUMEN_XVFB_RUN} -a ${CMAKE_COMMAND} -E env
            SDL_AUDIODRIVER=dummy
            $<TARGET_FILE:lumen-designer>
            --file ${PROJECT_SOURCE_DIR}/examples/counter/counter.lumen
            --max-frames 3)
    set_tests_properties(designer_window_smoke PROPERTIES TIMEOUT 40)
    add_test(NAME designer_gallery_window_smoke
        COMMAND ${LUMEN_XVFB_RUN} -a ${CMAKE_COMMAND} -E env
            SDL_AUDIODRIVER=dummy
            $<TARGET_FILE:lumen-designer>
            --file ${PROJECT_SOURCE_DIR}/examples/designer/gallery.lumen
            --max-frames 3)
    set_tests_properties(designer_gallery_window_smoke PROPERTIES TIMEOUT 40)
  elseif(WIN32 OR APPLE)
    add_test(NAME designer_window_smoke
        COMMAND ${CMAKE_COMMAND} -E env
            SDL_AUDIODRIVER=dummy
            $<TARGET_FILE:lumen-designer>
            --file ${PROJECT_SOURCE_DIR}/examples/counter/counter.lumen
            --max-frames 3)
    set_tests_properties(designer_window_smoke PROPERTIES TIMEOUT 40)
    add_test(NAME designer_gallery_window_smoke
        COMMAND ${CMAKE_COMMAND} -E env
            SDL_AUDIODRIVER=dummy
            $<TARGET_FILE:lumen-designer>
            --file ${PROJECT_SOURCE_DIR}/examples/designer/gallery.lumen
            --max-frames 3)
    set_tests_properties(designer_gallery_window_smoke PROPERTIES TIMEOUT 40)
  endif()
endif()
