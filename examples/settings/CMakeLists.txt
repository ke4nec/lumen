add_executable(lumen-settings main.cpp)

target_compile_features(lumen-settings PRIVATE cxx_std_20)
target_link_libraries(lumen-settings PRIVATE
    lumen-core
    lumen-app
    lumen-layout
    lumen-render
    lumen-text
    lumen-accessibility
    lumen-widgets
    lumen-platform
    lumen-dsl
    SDL3::SDL3
)

if(WIN32)
  # SDL3 builds as a shared library; keep the DLL next to the sample so it
  # runs directly from the build tree.
  add_custom_command(
    TARGET lumen-settings
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:lumen-settings>
    COMMAND_EXPAND_LISTS
  )
endif()

