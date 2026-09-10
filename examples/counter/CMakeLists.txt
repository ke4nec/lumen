add_executable(lumen-counter main.cpp)

target_compile_features(lumen-counter PRIVATE cxx_std_20)
target_link_libraries(lumen-counter PRIVATE
    lumen-core
    lumen-layout
    lumen-render
    lumen-platform
    lumen-dsl
    SDL3::SDL3
)

if(LUMEN_ENABLE_SKIA)
  target_compile_definitions(lumen-counter PRIVATE LUMEN_HAVE_SKIA=1)
endif()

if(LUMEN_ENABLE_GPU)
  target_compile_definitions(lumen-counter PRIVATE LUMEN_HAVE_GPU=1)
endif()

# The DSL sample loads this asset by name next to the binary.
add_custom_command(
    TARGET lumen-counter
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/counter.lumen
            $<TARGET_FILE_DIR:lumen-counter>
)

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
