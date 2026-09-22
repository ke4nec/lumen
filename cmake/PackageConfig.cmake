# M8（自用路线图）：便携包配置。
#
# 三桌面统一 CPack 入口：Windows ZIP / Linux TGZ / macOS ZIP。
# CPack 产物是开发归档，内容来自 install 规则（静态库、头、CMake export、
# 示例和文档）；Linux AppImage 与 macOS .app 是 workflow 组装的运行时产物，
# 不改变 CPack 开发归档的职责。

set(CPACK_PACKAGE_NAME "lumen")
set(CPACK_PACKAGE_VENDOR "lumen self-use tools")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Lumen self-use GUI toolkit + counter/settings examples")
# 版本可追溯：优先 Git 描述，回退固定值（干净检出保证可追溯性）。
find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE LUMEN_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()
if(NOT LUMEN_GIT_VERSION)
  set(LUMEN_GIT_VERSION "0.0.0")
endif()
set(CPACK_PACKAGE_VERSION ${LUMEN_GIT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "lumen-${CPACK_PACKAGE_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
set(CPACK_STRIP_FILES FALSE)  # 自用包保留符号便于诊断（§8 调试符号归档）

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_SYSTEM_NAME "windows-x64")
elseif(APPLE)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_SYSTEM_NAME "macos-universal")
else()
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_SYSTEM_NAME "linux-x64")
endif()

set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}")

include(CPack)
