cmake_minimum_required(VERSION 3.20)
project(lumen_sdk_consumer LANGUAGES CXX)

find_package(Lumen CONFIG REQUIRED)
if(MSVC AND LUMEN_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "${LUMEN_MSVC_RUNTIME_LIBRARY}")
endif()
add_executable(sdk-consumer main.cpp)
target_link_libraries(sdk-consumer PRIVATE Lumen::lumen-app Lumen::lumen-dsl)
target_compile_definitions(sdk-consumer PRIVATE
  SDK_WITH_SKIA=$<BOOL:${LUMEN_WITH_SKIA}>
  SDK_WITH_GPU=$<BOOL:${LUMEN_WITH_GPU}>)
