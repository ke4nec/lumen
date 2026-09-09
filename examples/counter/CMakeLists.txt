add_executable(lumen-counter main.cpp)

target_compile_features(lumen-counter PRIVATE cxx_std_20)
target_link_libraries(lumen-counter PRIVATE SDL3::SDL3)

if(WIN32)
  # SDL3 builds as a shared library; keep the DLL next to the sample so it
  # runs directly from the build tree.
  add_custom_command(
    TARGET lumen-counter
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:lumen-counter>
    COMMAND_EXPAND_LISTS
  )
endif()
