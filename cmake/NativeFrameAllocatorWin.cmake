enable_language(C)
include(FetchContent)

# v1.3.4; build a private copy without the upstream SDK install/export rules.
FetchContent_Declare(lumen_minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG c3fcafdc10146beb5919319d0683e44e3c30d537
    SOURCE_SUBDIR lumen-private-build)
FetchContent_MakeAvailable(lumen_minhook)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_lumen_hde hde64)
else()
  set(_lumen_hde hde32)
endif()
add_library(lumen-allocator-minhook STATIC
    ${lumen_minhook_SOURCE_DIR}/src/buffer.c
    ${lumen_minhook_SOURCE_DIR}/src/hook.c
    ${lumen_minhook_SOURCE_DIR}/src/trampoline.c
    ${lumen_minhook_SOURCE_DIR}/src/hde/${_lumen_hde}.c)
target_include_directories(lumen-allocator-minhook PUBLIC ${lumen_minhook_SOURCE_DIR}/include)
set_target_properties(lumen-allocator-minhook PROPERTIES
    LUMEN_THIRD_PARTY_LICENSE "${lumen_minhook_SOURCE_DIR}/LICENSE.txt"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
